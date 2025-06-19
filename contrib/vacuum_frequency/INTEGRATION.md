# Integrating VACUUM Frequency Hooks with PostgreSQL Core

## Overview

The `vacuum_frequency` extension defines hooks that allow custom control over VACUUM frequency, but these hooks need to be integrated into PostgreSQL's autovacuum system to be functional.

## Current Status

The extension currently defines hooks but **they are not called by PostgreSQL's autovacuum system**. This is because:

1. PostgreSQL's autovacuum decision-making is hardcoded in `src/backend/postmaster/autovacuum.c`
2. The hooks we defined are not integrated into the autovacuum workflow
3. No hook calls exist in the autovacuum decision path

## Integration Options

### Option 1: PostgreSQL Core Patch (Recommended for Production)

To make this extension fully functional, you need to patch PostgreSQL's autovacuum system:

1. **Apply the patch** in `autovacuum_hooks_patch.diff` to your PostgreSQL source
2. **Rebuild PostgreSQL** with the patch applied
3. **Install the extension** normally

The patch adds hook calls in two key places:
- `relation_needs_vacanalyze()` - For overriding VACUUM decisions
- `autovacuum_do_vac_analyze()` - For adjusting VACUUM parameters

### Option 2: Alternative Hook Points

If you prefer not to patch PostgreSQL core, you can use existing hook points:

#### A. Process Utility Hook
```c
// In your extension
static ProcessUtility_hook_type prev_ProcessUtility = NULL;

static void
vacuum_frequency_ProcessUtility(PlannedStmt *pstmt, const char *queryString,
                               bool readOnlyTree, ProcessUtilityContext context,
                               ParamListInfo params, QueryEnvironment *queryEnv,
                               DestReceiver *dest, QueryCompletion *qc)
{
    // Check if this is a VACUUM command
    if (IsA(pstmt->utilityStmt, VacuumStmt))
    {
        // Your custom logic here
    }
    
    // Call previous hook
    if (prev_ProcessUtility)
        prev_ProcessUtility(pstmt, queryString, readOnlyTree, context,
                           params, queryEnv, dest, qc);
    else
        standard_ProcessUtility(pstmt, queryString, readOnlyTree, context,
                               params, queryEnv, dest, qc);
}
```

#### B. Object Access Hook
```c
// Monitor table access and trigger custom VACUUM logic
static ObjectAccessHook vacuum_frequency_object_access_hook = NULL;

static void
vacuum_frequency_object_access(ObjectAccessType access, Oid classId, Oid objectId,
                              int subId, void *arg)
{
    if (access == OAT_POST_CREATE && classId == RelationRelationId)
    {
        // New table created - set up custom VACUUM frequency
    }
}
```

#### C. Background Worker
```c
// Create a background worker that monitors tables and triggers VACUUM
static void
vacuum_frequency_main(Datum main_arg)
{
    // Custom VACUUM scheduling logic
    // Use VACUUM commands with custom parameters
}
```

### Option 3: SQL-Level Control

Use PostgreSQL's existing autovacuum controls:

```sql
-- Disable autovacuum for specific tables
ALTER TABLE my_table SET (autovacuum_enabled = false);

-- Set custom thresholds
ALTER TABLE my_table SET (
    autovacuum_vacuum_threshold = 1000,
    autovacuum_vacuum_scale_factor = 0.1
);

-- Then use pg_cron or similar to run custom VACUUM commands
```

## Implementation Steps

### For Option 1 (Core Patch):

1. **Download PostgreSQL source** matching your version
2. **Apply the patch**:
   ```bash
   cd postgresql-source
   patch -p1 < contrib/vacuum_frequency/autovacuum_hooks_patch.diff
   ```
3. **Rebuild PostgreSQL**:
   ```bash
   ./configure --prefix=/usr/local/pgsql
   make
   make install
   ```
4. **Install the extension**:
   ```bash
   cd contrib/vacuum_frequency
   make install
   ```
5. **Configure PostgreSQL**:
   ```sql
   -- Add to postgresql.conf
   shared_preload_libraries = 'vacuum_frequency'
   
   -- Restart PostgreSQL
   -- Create extension
   CREATE EXTENSION vacuum_frequency;
   ```

### For Option 2 (Alternative Hooks):

1. **Modify the extension** to use existing hook points
2. **Remove the custom hook definitions** from `vacuum_frequency.h`
3. **Implement the alternative hook handlers** in `vacuum_frequency.c`
4. **Install normally** without patching PostgreSQL core

## Testing the Integration

After integration, test that your hooks are being called:

```sql
-- Enable debug logging
SET log_statement = 'all';
SET log_min_duration_statement = 0;

-- Create a test table
CREATE TABLE test_vacuum_frequency (id serial, data text);

-- Insert and update data to generate dead tuples
INSERT INTO test_vacuum_frequency (data) VALUES ('test');
UPDATE test_vacuum_frequency SET data = 'updated';

-- Check logs for hook activity
-- You should see debug messages from your extension
```

## Limitations

1. **Core patches require rebuilding PostgreSQL** - not suitable for managed databases
2. **Alternative hooks have limited scope** - can't fully control autovacuum decisions
3. **Performance impact** - hook calls add overhead to autovacuum decisions
4. **Compatibility** - patches may need updates for new PostgreSQL versions

## Recommendations

- **For development/testing**: Use Option 1 with a patched PostgreSQL
- **For production with managed databases**: Use Option 3 (SQL-level control)
- **For custom deployments**: Use Option 1 for full control
- **For extensions**: Use Option 2 to avoid core modifications

## Future Improvements

Consider contributing the hook integration to PostgreSQL core:

1. **Submit a patch** to PostgreSQL development mailing list
2. **Add proper documentation** for the new hooks
3. **Include regression tests** for the hook functionality
4. **Ensure backward compatibility** with existing autovacuum behavior 