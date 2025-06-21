/*-------------------------------------------------------------------------
 *
 * custom_autovacuum.c
 *	  Custom autovacuum policy framework
 *
 * This extension provides a framework for implementing custom autovacuum
 * policies as C functions. It allows users to override the default
 * autovacuum decision logic with a single custom policy.
 *
 * Portions Copyright (c) 1996-2025, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  contrib/custom_autovacuum/custom_autovacuum.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <limits.h>
#include <string.h>

#include "fmgr.h"
#include "utils/guc.h"
#include "utils/memutils.h"
#include "utils/timestamp.h"
#include "pgstat.h"
#include "access/htup_details.h"
#include "utils/rel.h"
#include "postmaster/autovacuum.h"
#include "custom_autovacuum.h"

PG_MODULE_MAGIC_EXT(
    .name = "custom_autovacuum",
    .version = PG_VERSION
);

/* Global variables */
static custom_autovacuum_policy_hook_type original_hook = NULL;
static bool custom_autovacuum_enabled = true;

/* Hook variable - declared in autovacuum.c, accessible to extensions */
extern PGDLLIMPORT custom_autovacuum_policy_hook_type custom_autovacuum_policy_hook;

/*
 * Single custom autovacuum policy function
 * 
 * This policy uses a proportional controller to dynamically adjust vacuum
 * aggressiveness based on the rate of dead tuple accumulation. The idea is:
 * - If dead tuples are accumulating quickly, vacuum more aggressively (lower threshold)
 * - If dead tuples are accumulating slowly, vacuum less aggressively (higher threshold)
 * 
 * The controller calculates the rate of dead tuple accumulation and adjusts
 * the vacuum threshold proportionally.
 */
static CustomAutovacuumPolicyResult
custom_autovacuum_policy(CustomAutovacuumPolicyContext *context)
{
    CustomAutovacuumPolicyResult result = {0};
    float4 current_dead_tuples;
    TimestampTz last_vacuum;
    TimestampTz now;
    double time_since_last_vacuum_ms;
    double dead_tuple_rate_per_ms;
    double dead_tuple_rate_per_sec;
    float4 base_threshold;
    float4 kp;
    float4 target_rate;
    float4 min_threshold;
    float4 max_threshold;
    float4 error;
    float4 adjusted_threshold;
    
    if (!context->stats)
        return result;
    
    /* Get current statistics */
    current_dead_tuples = context->stats->dead_tuples;
    last_vacuum = context->stats->last_vacuum_time;
    now = GetCurrentTimestamp();
    
    /* Calculate time since last vacuum in milliseconds (matching PostgreSQL's approach) */
    time_since_last_vacuum_ms = 0;
    if (last_vacuum > 0)
    {
        time_since_last_vacuum_ms = (now - last_vacuum) / 1000.0; /* Convert to milliseconds */
    }
    
    /* If no vacuum has been done yet or very recent, use a conservative approach */
    if (time_since_last_vacuum_ms <= 0 || time_since_last_vacuum_ms < 60000) /* Less than 1 minute */
    {
        /* Use default threshold for new tables */
        if (current_dead_tuples > context->vacthresh)
        {
            result.should_vacuum = true;
            result.reason = "Custom policy: new table with high dead tuple count";
        }
        return result;
    }
    
    /* Calculate dead tuple accumulation rate (dead tuples per millisecond, then convert to per second)
     * This matches the approach in vacuum_log_rates() where rates are calculated as:
     * rate = count / time_in_milliseconds
     */
    dead_tuple_rate_per_ms = current_dead_tuples / time_since_last_vacuum_ms;
    dead_tuple_rate_per_sec = dead_tuple_rate_per_ms * 1000.0; /* Convert to per second */
    
    /* Base threshold from context (this is the default autovacuum threshold) */
    base_threshold = context->vacthresh;
    
    /* Proportional controller parameters */
    kp = 0.1;  /* Proportional gain - adjust this to control sensitivity */
    target_rate = 0.1;  /* Target dead tuple rate (dead tuples per second) */
    min_threshold = 50.0;  /* Minimum threshold */
    max_threshold = base_threshold * 3.0;  /* Maximum threshold */
    
    /* Calculate error (difference between current rate and target rate) */
    error = dead_tuple_rate_per_sec - target_rate;
    
    /* Apply proportional control to adjust threshold */
    adjusted_threshold = base_threshold - (kp * error * base_threshold);
    
    /* Clamp threshold to reasonable bounds */
    if (adjusted_threshold < min_threshold)
        adjusted_threshold = min_threshold;
    if (adjusted_threshold > max_threshold)
        adjusted_threshold = max_threshold;
    
    /* Determine if we should vacuum based on adjusted threshold */
    if (current_dead_tuples > adjusted_threshold)
    {
        result.should_vacuum = true;
        
        /* Adjust cost parameters based on accumulation rate */
        if (dead_tuple_rate_per_sec > target_rate * 2.0) /* High accumulation rate */
        {
            result.custom_vac_cost_limit = 2000;  /* Higher cost limit for aggressive vacuum */
            result.custom_vac_cost_delay = 0.0;   /* No delay for aggressive vacuum */
            result.reason = pstrdup(psprintf("Custom policy: high accumulation rate (%.2f dead tuples/sec), aggressive vacuum", dead_tuple_rate_per_sec));
        }
        else if (dead_tuple_rate_per_sec > target_rate) /* Moderate accumulation rate */
        {
            result.custom_vac_cost_limit = 1000;  /* Moderate cost limit */
            result.custom_vac_cost_delay = 5.0;   /* Small delay */
            result.reason = pstrdup(psprintf("Custom policy: moderate accumulation rate (%.2f dead tuples/sec)", dead_tuple_rate_per_sec));
        }
        else /* Low accumulation rate */
        {
            result.custom_vac_cost_limit = 500;   /* Lower cost limit */
            result.custom_vac_cost_delay = 10.0;  /* Higher delay */
            result.reason = pstrdup(psprintf("Custom policy: low accumulation rate (%.2f dead tuples/sec), conservative vacuum", dead_tuple_rate_per_sec));
        }
        
        /* Log the controller decision for debugging (matching PostgreSQL's logging format) */
        elog(DEBUG3, "Custom autovacuum policy: dead_tuples=%.0f, time_since_vacuum=%.0f ms, rate=%.2f/sec, base_thresh=%.0f, adj_thresh=%.0f, error=%.2f",
             current_dead_tuples, time_since_last_vacuum_ms, dead_tuple_rate_per_sec, base_threshold, adjusted_threshold, error);
    }
    else
    {
        result.skip_table = true;
        result.reason = pstrdup(psprintf("Custom policy: below adjusted threshold (%.0f < %.0f, rate=%.2f/sec)", 
                                current_dead_tuples, adjusted_threshold, dead_tuple_rate_per_sec));
    }
    
    return result;
}

/* Hook function that calls the single policy */
static CustomAutovacuumPolicyResult
custom_autovacuum_policy_hook_func(CustomAutovacuumPolicyContext *context)
{
    if (!custom_autovacuum_enabled)
        return (CustomAutovacuumPolicyResult) {0};
    
    return custom_autovacuum_policy(context);
}

/*
 * Module initialization
 */
void
_PG_init(void)
{
    /* Install the hook */
    original_hook = custom_autovacuum_policy_hook;
    custom_autovacuum_policy_hook = custom_autovacuum_policy_hook_func;
    
    /* Define GUC variables */
    DefineCustomBoolVariable("custom_autovacuum.enabled",
                            "Enable custom autovacuum policy",
                            NULL,
                            &custom_autovacuum_enabled,
                            true,
                            PGC_SIGHUP,
                            0,
                            NULL, NULL, NULL);
    
    MarkGUCPrefixReserved("custom_autovacuum");
} 