#include "postgres.h"
#include "vacuum_frequency.h"

#include "access/table.h"
#include "catalog/pg_class.h"
#include "commands/vacuum.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "storage/lwlock.h"
#include "storage/shmem.h"
#include "utils/guc.h"
#include "utils/hsearch.h"
#include "utils/rel.h"
#include "pgtime.h"            // For pg_localtime, struct pg_tm
#include "utils/memutils.h"      // For TopMemoryContext
#include "catalog/namespace.h"  // For get_namespace_name
#include "storage/ipc.h"        // For on_shmem_exit

#ifndef LWTRANCHE_VACUUM_FREQUENCY
#define LWTRANCHE_VACUUM_FREQUENCY 2001
#endif

#ifndef HAVE_DECL_OID_HASH
extern uint32 oid_hash(const void *key, Size keysize);
#endif

#ifndef HAVE_DECL_GET_NAMESPACE_NAME
extern char *get_namespace_name(Oid nspid);
#endif

PG_MODULE_MAGIC_EXT(
    .name = "vacuum_frequency",
    .version = PG_VERSION
);

/* Extension state in shared memory */
typedef struct VacuumFrequencySharedState
{
    LWLock lock;
    HTAB *table_frequencies;
    bool initialized;
} VacuumFrequencySharedState;

/* Per-table frequency settings */
typedef struct TableFrequency
{
    Oid table_oid;
    int vacuum_interval_hours;
    TimestampTz last_vacuum_time;
    int64 last_dead_tuples;
    bool enabled;
    char table_name[NAMEDATALEN];
    char schema_name[NAMEDATALEN];
} TableFrequency;

/* Local state */
static VacuumFrequencySharedState *shared_state = NULL;
static bool extension_enabled = true;

/* GUC variables */
static int default_vacuum_interval_hours = 24;
static int high_activity_threshold = 10000;
static int low_activity_threshold = 100;
static bool enable_time_based_scheduling = true;
static bool enable_activity_based_scheduling = true;
static bool enable_workload_based_scheduling = false;
static int peak_hours_start = 9;
static int peak_hours_end = 17;

/* Function prototypes */
static void vacuum_frequency_shmem_startup(void);
static void vacuum_frequency_shmem_shutdown(int code, Datum arg);
static bool vacuum_frequency_should_vacuum(Relation rel, VacuumParams *params,
                                          double n_dead_tup, double n_live_tup,
                                          TransactionId relfrozenxid,
                                          MultiXactId relminmxid);
static void vacuum_frequency_adjust_params(Relation rel, VacuumParams *params,
                                          double n_dead_tup, double n_live_tup);
static int vacuum_frequency_get_priority(Relation rel, double n_dead_tup,
                                        double n_live_tup);
static void vacuum_frequency_post_scan(Relation rel, VacuumParams *params,
                                      BlockNumber scanned_pages,
                                      int64 tuples_deleted,
                                      int64 tuples_frozen);
static TableFrequency *get_table_frequency(Oid table_oid);
static void set_table_frequency(Oid table_oid, const char *schema_name,
                               const char *table_name, int interval_hours);
static bool should_vacuum_by_time(TableFrequency *tf);
static bool should_vacuum_by_activity(TableFrequency *tf, double n_dead_tup);
static bool should_vacuum_by_workload(Relation rel);
static bool is_peak_hours(void);
static bool is_table_under_heavy_load(Relation rel);
static double get_table_update_rate(Relation rel);
static bool is_critical_table(Relation rel);
static bool is_near_wraparound(Relation rel);
static void get_table_name_and_schema(Oid table_oid, char **schema_name, char **table_name);

/* Hook function pointers */
static vacuum_should_vacuum_hook_type prev_should_vacuum_hook = NULL;
static vacuum_adjust_params_hook_type prev_adjust_params_hook = NULL;
static vacuum_get_priority_hook_type prev_get_priority_hook = NULL;
static vacuum_post_scan_hook_type prev_post_scan_hook = NULL;

/* Hook implementations */
static bool
vacuum_frequency_should_vacuum(Relation rel, VacuumParams *params,
                              double n_dead_tup, double n_live_tup,
                              TransactionId relfrozenxid,
                              MultiXactId relminmxid)
{
    TableFrequency *tf;
    bool should_vacuum = true;
    
    /* Call previous hook if exists */
    if (prev_should_vacuum_hook)
    {
        should_vacuum = (*prev_should_vacuum_hook) (rel, params, n_dead_tup, 
                                                   n_live_tup, relfrozenxid, 
                                                   relminmxid);
        if (!should_vacuum)
            return false;
    }
    
    if (!extension_enabled || !shared_state || !shared_state->initialized)
        return should_vacuum;
    
    LWLockAcquire(&shared_state->lock, LW_SHARED);
    
    tf = get_table_frequency(RelationGetRelid(rel));
    if (!tf || !tf->enabled)
    {
        LWLockRelease(&shared_state->lock);
        return should_vacuum;
    }
    
    /* Check time-based scheduling */
    if (enable_time_based_scheduling && !should_vacuum_by_time(tf))
    {
        should_vacuum = false;
    }
    
    /* Check activity-based scheduling */
    if (should_vacuum && enable_activity_based_scheduling && 
        !should_vacuum_by_activity(tf, n_dead_tup))
    {
        should_vacuum = false;
    }
    
    /* Check workload-based scheduling */
    if (should_vacuum && enable_workload_based_scheduling && 
        !should_vacuum_by_workload(rel))
    {
        should_vacuum = false;
    }
    
    LWLockRelease(&shared_state->lock);
    
    /* Log decision */
    if (should_vacuum)
    {
        elog(DEBUG1, "VACUUM frequency: Allowing VACUUM for table %s (dead_tuples=%f)",
             RelationGetRelationName(rel), n_dead_tup);
    }
    else
    {
        elog(DEBUG1, "VACUUM frequency: Skipping VACUUM for table %s (dead_tuples=%f)",
             RelationGetRelationName(rel), n_dead_tup);
    }
    
    return should_vacuum;
}

static void
vacuum_frequency_adjust_params(Relation rel, VacuumParams *params,
                              double n_dead_tup, double n_live_tup)
{
    TableFrequency *tf;
    
    /* Call previous hook if exists */
    if (prev_adjust_params_hook)
        (*prev_adjust_params_hook) (rel, params, n_dead_tup, n_live_tup);
    
    if (!extension_enabled || !shared_state || !shared_state->initialized)
        return;
    
    LWLockAcquire(&shared_state->lock, LW_SHARED);
    
    tf = get_table_frequency(RelationGetRelid(rel));
    if (!tf || !tf->enabled)
    {
        LWLockRelease(&shared_state->lock);
        return;
    }
    
    /* Adjust VACUUM parameters based on table frequency settings */
    if (n_dead_tup > high_activity_threshold)
    {
        /* High activity - make VACUUM more aggressive */
        params->freeze_min_age = Min(params->freeze_min_age, 1000000);
        params->freeze_table_age = Min(params->freeze_table_age, 150000000);
    }
    else if (n_dead_tup < low_activity_threshold)
    {
        /* Low activity - make VACUUM less aggressive */
        params->freeze_min_age = Max(params->freeze_min_age, 50000000);
        params->freeze_table_age = Max(params->freeze_table_age, 200000000);
    }
    
    LWLockRelease(&shared_state->lock);
}

static int
vacuum_frequency_get_priority(Relation rel, double n_dead_tup, double n_live_tup)
{
    TableFrequency *tf;
    int priority = 0;
    
    /* Call previous hook if exists */
    if (prev_get_priority_hook)
        priority = (*prev_get_priority_hook) (rel, n_dead_tup, n_live_tup);
    
    if (!extension_enabled || !shared_state || !shared_state->initialized)
        return priority;
    
    LWLockAcquire(&shared_state->lock, LW_SHARED);
    
    tf = get_table_frequency(RelationGetRelid(rel));
    if (!tf || !tf->enabled)
    {
        LWLockRelease(&shared_state->lock);
        return priority;
    }
    
    /* Higher priority for critical tables */
    if (is_critical_table(rel))
        priority += 1000;
    
    /* Higher priority for tables approaching wraparound */
    if (is_near_wraparound(rel))
        priority += 500;
    
    /* Higher priority for tables with more dead tuples */
    priority += (int)(n_dead_tup / 100);
    
    /* Higher priority for tables that haven't been vacuumed recently */
    if (tf->last_vacuum_time > 0)
    {
        TimestampTz now = GetCurrentTimestamp();
        int hours_since_vacuum = (int)((now - tf->last_vacuum_time) / 3600000000);
        priority += hours_since_vacuum;
    }
    
    LWLockRelease(&shared_state->lock);
    
    return priority;
}

static void
vacuum_frequency_post_scan(Relation rel, VacuumParams *params,
                          BlockNumber scanned_pages,
                          int64 tuples_deleted,
                          int64 tuples_frozen)
{
    TableFrequency *tf;
    
    /* Call previous hook if exists */
    if (prev_post_scan_hook)
        (*prev_post_scan_hook) (rel, params, scanned_pages, 
                               tuples_deleted, tuples_frozen);
    
    if (!extension_enabled || !shared_state || !shared_state->initialized)
        return;
    
    LWLockAcquire(&shared_state->lock, LW_EXCLUSIVE);
    
    tf = get_table_frequency(RelationGetRelid(rel));
    if (tf && tf->enabled)
    {
        tf->last_vacuum_time = GetCurrentTimestamp();
        tf->last_dead_tuples = tuples_deleted;
    }
    
    LWLockRelease(&shared_state->lock);
}

/* Helper functions */
static TableFrequency *
get_table_frequency(Oid table_oid)
{
    TableFrequency *tf;
    bool found;
    
    if (!shared_state->table_frequencies)
        return NULL;
    
    tf = hash_search(shared_state->table_frequencies, &table_oid, HASH_FIND, &found);
    return found ? tf : NULL;
}

static void
set_table_frequency(Oid table_oid, const char *schema_name,
                   const char *table_name, int interval_hours)
{
    TableFrequency *tf;
    bool found;
    
    if (!shared_state->table_frequencies)
        return;
    
    tf = hash_search(shared_state->table_frequencies, &table_oid, HASH_ENTER, &found);
    if (!found)
    {
        tf->table_oid = table_oid;
        tf->last_vacuum_time = 0;
        tf->last_dead_tuples = 0;
        tf->enabled = true;
    }
    tf->vacuum_interval_hours = interval_hours;
    strlcpy(tf->schema_name, schema_name, NAMEDATALEN);
    strlcpy(tf->table_name, table_name, NAMEDATALEN);
}

static bool
should_vacuum_by_time(TableFrequency *tf)
{
    if (tf->last_vacuum_time == 0)
        return true; /* Never vacuumed before */
    
    TimestampTz now = GetCurrentTimestamp();
    int hours_since_vacuum = (int)((now - tf->last_vacuum_time) / 3600000000);
    
    return hours_since_vacuum >= tf->vacuum_interval_hours;
}

static bool
should_vacuum_by_activity(TableFrequency *tf, double n_dead_tup)
{
    /* Always vacuum if dead tuples exceed high threshold */
    if (n_dead_tup > high_activity_threshold)
        return true;
    
    /* Don't vacuum if dead tuples are below low threshold */
    if (n_dead_tup < low_activity_threshold)
        return false;
    
    /* For moderate activity, check if there's been significant change */
    int64 dead_tup_change = (int64)n_dead_tup - tf->last_dead_tuples;
    return dead_tup_change > (low_activity_threshold / 2);
}

static bool
should_vacuum_by_workload(Relation rel)
{
    /* Defer VACUUM during peak hours */
    if (is_peak_hours())
        return false;
    
    /* Defer VACUUM if table is under heavy load */
    if (is_table_under_heavy_load(rel))
        return false;
    
    return true;
}

static bool
is_peak_hours(void)
{
    TimestampTz now = GetCurrentTimestamp();
    pg_time_t unix_time;
    struct pg_tm *tm_ptr;
    int current_hour;

    unix_time = timestamptz_to_time_t(now);
    tm_ptr = pg_localtime(&unix_time, session_timezone);
    if (tm_ptr == NULL)
        return false;
    current_hour = tm_ptr->tm_hour;
    return (current_hour >= peak_hours_start && current_hour < peak_hours_end);
}

static bool
is_table_under_heavy_load(Relation rel)
{
    /* Simple heuristic: check if table has many active transactions */
    /* This is a placeholder - in a real implementation, you'd check */
    /* pg_stat_activity, locks, etc. */
    return false;
}

static double
get_table_update_rate(Relation rel)
{
    /* Placeholder - would calculate update rate from statistics */
    return 0.0;
}

static bool
is_critical_table(Relation rel)
{
    /* Placeholder - would check if table is marked as critical */
    /* Could be based on table name, schema, or custom metadata */
    return false;
}

static bool
is_near_wraparound(Relation rel)
{
    /* Placeholder - would check if table is approaching XID wraparound */
    return false;
}

/* Shared memory management */
static void
vacuum_frequency_shmem_startup(void)
{
    HASHCTL hash_ctl;
    bool found;
    
    /* Create or attach to shared memory */
    shared_state = ShmemInitStruct("VacuumFrequencyState",
                                  sizeof(VacuumFrequencySharedState),
                                  &found);
    
    if (!found)
    {
        /* First time - initialize */
        LWLockInitialize(&shared_state->lock, LWTRANCHE_VACUUM_FREQUENCY);
        shared_state->initialized = false;
        shared_state->table_frequencies = NULL;
    }
    
    /* Initialize hash table if not already done */
    if (!shared_state->initialized)
    {
        memset(&hash_ctl, 0, sizeof(hash_ctl));
        hash_ctl.keysize = sizeof(Oid);
        hash_ctl.entrysize = sizeof(TableFrequency);
        hash_ctl.hash = oid_hash;
        hash_ctl.hcxt = TopMemoryContext;
        
        shared_state->table_frequencies = hash_create("vacuum frequency table settings",
                                                     100, &hash_ctl,
                                                     HASH_ELEM | HASH_FUNCTION | HASH_CONTEXT);
        shared_state->initialized = true;
    }
}

static void
vacuum_frequency_shmem_shutdown(int code, Datum arg)
{
    /* Cleanup if needed */
}

/* Helper function to get table name and schema */
static void
get_table_name_and_schema(Oid table_oid, char **schema_name, char **table_name)
{
    Relation rel;
    Form_pg_class classForm;
    
    rel = table_open(table_oid, AccessShareLock);
    classForm = rel->rd_rel;
    
    *table_name = pstrdup(NameStr(classForm->relname));
    *schema_name = get_namespace_name(classForm->relnamespace);
    
    table_close(rel, AccessShareLock);
}

/* Module initialization */
void
_PG_init(void)
{
    /* Define GUC variables */
    DefineCustomIntVariable("vacuum_frequency.default_interval_hours",
                           "Default VACUUM interval in hours",
                           NULL,
                           &default_vacuum_interval_hours,
                           24,
                           0, 8760, /* 0 to 1 year */
                           PGC_SUSET,
                           0,
                           NULL, NULL, NULL);
    
    DefineCustomIntVariable("vacuum_frequency.high_activity_threshold",
                           "High activity threshold for dead tuples",
                           NULL,
                           &high_activity_threshold,
                           10000,
                           0, INT_MAX,
                           PGC_SUSET,
                           0,
                           NULL, NULL, NULL);
    
    DefineCustomIntVariable("vacuum_frequency.low_activity_threshold",
                           "Low activity threshold for dead tuples",
                           NULL,
                           &low_activity_threshold,
                           100,
                           0, INT_MAX,
                           PGC_SUSET,
                           0,
                           NULL, NULL, NULL);
    
    DefineCustomBoolVariable("vacuum_frequency.enable_time_based_scheduling",
                            "Enable time-based VACUUM scheduling",
                            NULL,
                            &enable_time_based_scheduling,
                            true,
                            PGC_SUSET,
                            0,
                            NULL, NULL, NULL);
    
    DefineCustomBoolVariable("vacuum_frequency.enable_activity_based_scheduling",
                            "Enable activity-based VACUUM scheduling",
                            NULL,
                            &enable_activity_based_scheduling,
                            true,
                            PGC_SUSET,
                            0,
                            NULL, NULL, NULL);
    
    DefineCustomBoolVariable("vacuum_frequency.enable_workload_based_scheduling",
                            "Enable workload-based VACUUM scheduling",
                            NULL,
                            &enable_workload_based_scheduling,
                            false,
                            PGC_SUSET,
                            0,
                            NULL, NULL, NULL);
    
    DefineCustomIntVariable("vacuum_frequency.peak_hours_start",
                           "Start hour for peak workload period",
                           NULL,
                           &peak_hours_start,
                           9,
                           0, 23,
                           PGC_SUSET,
                           0,
                           NULL, NULL, NULL);
    
    DefineCustomIntVariable("vacuum_frequency.peak_hours_end",
                           "End hour for peak workload period",
                           NULL,
                           &peak_hours_end,
                           17,
                           0, 23,
                           PGC_SUSET,
                           0,
                           NULL, NULL, NULL);
    
    MarkGUCPrefixReserved("vacuum_frequency");
    
    /* Request shared memory */
    RequestAddinShmemSpace(sizeof(VacuumFrequencySharedState));
    RequestNamedLWLockTranche("vacuum_frequency", 1);
    
    /* Initialize shared memory immediately */
    vacuum_frequency_shmem_startup();
    
    /* Install hooks */
    prev_should_vacuum_hook = vacuum_should_vacuum_hook;
    vacuum_should_vacuum_hook = vacuum_frequency_should_vacuum;
    
    prev_adjust_params_hook = vacuum_adjust_params_hook;
    vacuum_adjust_params_hook = vacuum_frequency_adjust_params;
    
    prev_get_priority_hook = vacuum_get_priority_hook;
    vacuum_get_priority_hook = vacuum_frequency_get_priority;
    
    prev_post_scan_hook = vacuum_post_scan_hook;
    vacuum_post_scan_hook = vacuum_frequency_post_scan;
    
    on_proc_exit(vacuum_frequency_shmem_shutdown, 0);
} 