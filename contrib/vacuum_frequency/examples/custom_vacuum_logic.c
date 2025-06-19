/*
 * Example custom VACUUM logic for the vacuum_frequency extension
 * 
 * This file demonstrates how to extend the VACUUM control functions
 * with custom logic. Copy these functions into vacuum_frequency.c
 * and modify them according to your needs.
 */

#include "postgres.h"
#include "vacuum_frequency.h"

/* Example 1: Custom time-based scheduling - only vacuum on weekends */
static bool
custom_should_vacuum_by_time(TableFrequency *tf)
{
    TimestampTz now = GetCurrentTimestamp();
    struct tm *tm = localtime(&now);
    
    /* Only vacuum on weekends (Saturday = 6, Sunday = 0) */
    if (tm->tm_wday == 0 || tm->tm_wday == 6)
    {
        elog(DEBUG1, "VACUUM frequency: Weekend - allowing VACUUM");
        return true;
    }
    
    elog(DEBUG1, "VACUUM frequency: Weekday - deferring VACUUM");
    return false;
}

/* Example 2: Custom activity-based scheduling - percentage-based */
static bool
custom_should_vacuum_by_activity(TableFrequency *tf, double n_dead_tup, double n_live_tup)
{
    /* Always vacuum if dead tuples exceed high threshold */
    if (n_dead_tup > high_activity_threshold)
    {
        elog(DEBUG1, "VACUUM frequency: High dead tuple count - allowing VACUUM");
        return true;
    }
    
    /* Don't vacuum if dead tuples are below low threshold */
    if (n_dead_tup < low_activity_threshold)
    {
        elog(DEBUG1, "VACUUM frequency: Low dead tuple count - deferring VACUUM");
        return false;
    }
    
    /* For moderate activity, check dead tuple percentage */
    if (n_live_tup > 0)
    {
        double dead_percentage = (n_dead_tup / (n_dead_tup + n_live_tup)) * 100;
        
        if (dead_percentage > 5.0) /* Vacuum if dead tuples > 5% */
        {
            elog(DEBUG1, "VACUUM frequency: Dead tuple percentage %.2f%% - allowing VACUUM", 
                 dead_percentage);
            return true;
        }
    }
    
    elog(DEBUG1, "VACUUM frequency: Moderate activity - deferring VACUUM");
    return false;
}

/* Example 3: Custom workload-based scheduling - business hours */
static bool
custom_should_vacuum_by_workload(Relation rel)
{
    TimestampTz now = GetCurrentTimestamp();
    struct tm *tm = localtime(&now);
    int current_hour = tm->tm_hour;
    
    /* Defer VACUUM during business hours (9 AM - 6 PM) */
    if (current_hour >= 9 && current_hour <= 18)
    {
        elog(DEBUG1, "VACUUM frequency: Business hours - deferring VACUUM");
        return false;
    }
    
    /* Allow VACUUM during off-hours */
    elog(DEBUG1, "VACUUM frequency: Off-hours - allowing VACUUM");
    return true;
}

/* Example 4: Custom priority calculation */
static int
custom_vacuum_get_priority(Relation rel, double n_dead_tup, double n_live_tup)
{
    int priority = 0;
    const char *relname = RelationGetRelationName(rel);
    
    /* Higher priority for system tables */
    if (strncmp(relname, "pg_", 3) == 0)
    {
        priority += 2000;
        elog(DEBUG1, "VACUUM frequency: System table %s - high priority", relname);
    }
    
    /* Higher priority for tables with high dead tuple ratio */
    if (n_live_tup > 0)
    {
        double ratio = n_dead_tup / n_live_tup;
        priority += (int)(ratio * 1000);
        
        if (ratio > 0.1) /* More than 10% dead tuples */
        {
            elog(DEBUG1, "VACUUM frequency: Table %s has high dead tuple ratio %.2f - priority %d", 
                 relname, ratio, priority);
        }
    }
    
    /* Higher priority for critical tables (customize this list) */
    if (strcmp(relname, "users") == 0 || 
        strcmp(relname, "orders") == 0 ||
        strcmp(relname, "transactions") == 0)
    {
        priority += 1500;
        elog(DEBUG1, "VACUUM frequency: Critical table %s - high priority", relname);
    }
    
    return priority;
}

/* Example 5: Custom parameter adjustment */
static void
custom_vacuum_adjust_params(Relation rel, VacuumParams *params, 
                           double n_dead_tup, double n_live_tup)
{
    const char *relname = RelationGetRelationName(rel);
    
    /* More aggressive VACUUM for tables with high dead tuple count */
    if (n_dead_tup > high_activity_threshold)
    {
        params->freeze_min_age = Min(params->freeze_min_age, 500000);  /* More aggressive */
        params->freeze_table_age = Min(params->freeze_table_age, 100000000);
        
        elog(DEBUG1, "VACUUM frequency: Table %s - aggressive parameters", relname);
    }
    
    /* Less aggressive VACUUM for tables with low activity */
    else if (n_dead_tup < low_activity_threshold)
    {
        params->freeze_min_age = Max(params->freeze_min_age, 100000000);  /* Less aggressive */
        params->freeze_table_age = Max(params->freeze_table_age, 300000000);
        
        elog(DEBUG1, "VACUUM frequency: Table %s - conservative parameters", relname);
    }
    
    /* Special handling for log tables */
    if (strstr(relname, "log") != NULL || strstr(relname, "audit") != NULL)
    {
        /* Log tables often have high turnover - more aggressive VACUUM */
        params->freeze_min_age = Min(params->freeze_min_age, 1000000);
        params->freeze_table_age = Min(params->freeze_table_age, 50000000);
        
        elog(DEBUG1, "VACUUM frequency: Log table %s - aggressive parameters", relname);
    }
}

/* Example 6: Custom post-scan logging */
static void
custom_vacuum_post_scan(Relation rel, VacuumParams *params,
                       BlockNumber scanned_pages, int64 tuples_deleted, int64 tuples_frozen)
{
    const char *relname = RelationGetRelationName(rel);
    
    /* Log VACUUM results for monitoring */
    elog(LOG, "VACUUM frequency: Table %s - scanned %u pages, deleted %ld tuples, frozen %ld tuples",
         relname, scanned_pages, tuples_deleted, tuples_frozen);
    
    /* Update custom statistics if needed */
    if (tuples_deleted > 1000)
    {
        elog(LOG, "VACUUM frequency: Table %s had significant cleanup (%ld dead tuples removed)",
             relname, tuples_deleted);
    }
}

/*
 * To use these custom functions, replace the corresponding functions in vacuum_frequency.c:
 * 
 * 1. Replace should_vacuum_by_time() with custom_should_vacuum_by_time()
 * 2. Replace should_vacuum_by_activity() with custom_should_vacuum_by_activity()
 * 3. Replace should_vacuum_by_workload() with custom_should_vacuum_by_workload()
 * 4. Replace vacuum_frequency_get_priority() with custom_vacuum_get_priority()
 * 5. Replace vacuum_frequency_adjust_params() with custom_vacuum_adjust_params()
 * 6. Replace vacuum_frequency_post_scan() with custom_vacuum_post_scan()
 * 
 * Remember to update function signatures and add any necessary includes.
 */ 