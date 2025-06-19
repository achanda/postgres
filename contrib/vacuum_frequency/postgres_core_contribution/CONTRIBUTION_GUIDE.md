# Contributing Autovacuum Hooks to PostgreSQL Core

## Overview

This guide explains how to contribute the autovacuum hook system to PostgreSQL core, enabling extensions to control VACUUM frequency and parameters.

## Files to Submit

### 1. Core Patch Files

#### `autovacuum_hooks.patch`
- **Purpose**: Adds hook points to autovacuum.c and vacuum.h
- **Files Modified**: 
  - `src/backend/postmaster/autovacuum.c`
  - `src/include/commands/vacuum.h`

#### `vacuum_hooks.c`
- **Purpose**: Implements hook variable definitions
- **Location**: `src/backend/commands/vacuum_hooks.c`

### 2. Documentation

#### `doc/src/sgml/hooks.sgml`
Add documentation for the new hooks:

```xml
<sect2 id="autovacuum-hooks">
 <title>Autovacuum Hooks</title>
 
 <para>
  The following hooks allow extensions to control autovacuum behavior:
 </para>
 
 <variablelist>
  <varlistentry>
   <term><varname>vacuum_should_vacuum_hook</varname></term>
   <listitem>
    <para>
     Called during autovacuum decision making. Return true to force VACUUM,
     false to skip it, or NULL to use default logic.
    </para>
   </listitem>
  </varlistentry>
  
  <varlistentry>
   <term><varname>vacuum_adjust_params_hook</varname></term>
   <listitem>
    <para>
     Called before VACUUM execution to modify parameters like cost limits,
     freeze age, etc.
    </para>
   </listitem>
  </varlistentry>
  
  <varlistentry>
   <term><varname>vacuum_get_priority_hook</varname></term>
   <listitem>
    <para>
     Called to determine VACUUM priority. Higher values = higher priority.
    </para>
   </listitem>
  </varlistentry>
  
  <varlistentry>
   <term><varname>vacuum_post_scan_hook</varname></term>
   <listitem>
    <para>
     Called after VACUUM completes to record results and statistics.
    </para>
   </listitem>
  </varlistentry>
 </variablelist>
</sect2>
```

## Contribution Process

### 1. **Prepare Your Environment**

```bash
# Clone PostgreSQL repository
git clone https://git.postgresql.org/git/postgresql.git
cd postgresql

# Create a feature branch
git checkout -b autovacuum-hooks

# Apply your patches
patch -p1 < autovacuum_hooks.patch
```

### 2. **Build and Test**

```bash
# Configure and build
./configure --enable-debug --enable-cassert
make -j$(nproc)

# Run regression tests
make check

# Run specific autovacuum tests
make check-world PGOPTIONS="-c autovacuum=on"
```

### 3. **Submit to Mailing List**

#### Email Template

```
Subject: [PATCH] Add hook points for autovacuum decision making

Hi hackers,

This patch adds hook points to PostgreSQL's autovacuum system to allow
extensions to control VACUUM frequency and parameters without modifying
core code.

The hooks are:

- vacuum_should_vacuum_hook: Override autovacuum decisions
- vacuum_adjust_params_hook: Modify VACUUM parameters  
- vacuum_get_priority_hook: Set VACUUM priority
- vacuum_post_scan_hook: Record VACUUM results

This enables extensions like vacuum_frequency to implement custom
VACUUM scheduling logic.

Testing:
- All regression tests pass
- Tested with vacuum_frequency extension
- No performance regression observed

Discussion: [Link to previous discussion thread]

Best regards,
[Your Name]
```

### 4. **Address Review Comments**

Common review points to address:

- **Performance**: Ensure hooks don't add significant overhead
- **Thread Safety**: Verify hooks work correctly in multi-process environment
- **Error Handling**: Add proper error handling for hook failures
- **Documentation**: Ensure all hooks are properly documented
- **Testing**: Add specific tests for hook functionality

### 5. **Iterate and Refine**

Based on community feedback:

1. **Simplify**: Remove unnecessary complexity
2. **Clarify**: Improve documentation and comments
3. **Test**: Add more comprehensive tests
4. **Optimize**: Address performance concerns

## Alternative Approaches

If the core contribution is not accepted, consider:

### 1. **Background Worker Approach**
Use PostgreSQL's background worker system to implement custom VACUUM scheduling:

```c
// Register a background worker that monitors tables
// and triggers VACUUM based on custom logic
```

### 2. **Event Trigger Approach**
Use PostgreSQL's event trigger system to intercept VACUUM commands:

```sql
CREATE EVENT TRIGGER vacuum_control ON ddl_command_end
EXECUTE FUNCTION vacuum_control_function();
```

### 3. **Custom VACUUM Command**
Create a custom VACUUM command that extensions can call:

```sql
-- Custom VACUUM command with frequency control
SELECT custom_vacuum('table_name', 'frequency_control');
```

## Success Criteria

Your contribution will be successful if:

1. **Accepted by community**: Patch is reviewed and accepted
2. **Well documented**: Clear documentation for extension developers
3. **Well tested**: Comprehensive test coverage
4. **Performance neutral**: No significant performance impact
5. **Backward compatible**: Doesn't break existing functionality

## Timeline

- **Week 1-2**: Initial patch preparation and testing
- **Week 3-4**: Submit to pgsql-hackers mailing list
- **Week 5-8**: Address review comments and iterate
- **Week 9-12**: Final review and potential acceptance

## Resources

- [PostgreSQL Development Guide](https://www.postgresql.org/docs/current/contributing.html)
- [pgsql-hackers Mailing List](https://www.postgresql.org/list/pgsql-hackers/)
- [PostgreSQL Git Repository](https://git.postgresql.org/git/postgresql.git)
- [Extension Development](https://www.postgresql.org/docs/current/extend.html)

## Example Extension Usage

Once the hooks are accepted, extensions can use them like this:

```c
// In your extension's _PG_init function
vacuum_should_vacuum_hook = my_vacuum_decision_hook;
vacuum_adjust_params_hook = my_vacuum_params_hook;

// Hook implementations
bool my_vacuum_decision_hook(Relation rel, VacuumParams *params, ...)
{
    // Custom logic to decide if VACUUM should run
    return should_vacuum_table(rel);
}

void my_vacuum_params_hook(Relation rel, VacuumParams *params, ...)
{
    // Custom logic to adjust VACUUM parameters
    params->freeze_min_age = custom_freeze_age(rel);
}
```

This approach provides a clean, extensible way to control VACUUM behavior without modifying PostgreSQL core. 