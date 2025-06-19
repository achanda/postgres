# PostgreSQL Core Contribution: Autovacuum Hooks

## Overview

This directory contains the files needed to contribute autovacuum hook functionality to PostgreSQL core. These hooks allow extensions to control VACUUM frequency and parameters without modifying PostgreSQL source code.

## Files

### Core Patches

- **`autovacuum_hooks.patch`** - Main patch file that adds hook points to autovacuum.c and vacuum.h
- **`vacuum_hooks.c`** - Implementation file defining the hook variables

### Documentation

- **`CONTRIBUTION_GUIDE.md`** - Comprehensive guide for contributing to PostgreSQL core
- **`test_hooks.sql`** - Test file to validate hook functionality

## Hook System

The contribution adds four hook points to PostgreSQL's autovacuum system:

### 1. `vacuum_should_vacuum_hook`
**Purpose**: Override autovacuum decisions
**Called**: During autovacuum decision making
**Returns**: `bool` - true to force VACUUM, false to skip, NULL for default logic

### 2. `vacuum_adjust_params_hook`
**Purpose**: Modify VACUUM parameters before execution
**Called**: Before VACUUM execution
**Parameters**: Can modify cost limits, freeze age, etc.

### 3. `vacuum_get_priority_hook`
**Purpose**: Set VACUUM priority
**Called**: During autovacuum scheduling
**Returns**: `int` - higher values = higher priority

### 4. `vacuum_post_scan_hook`
**Purpose**: Record VACUUM results and statistics
**Called**: After VACUUM completes
**Parameters**: Receives scan results, tuples deleted/frozen

## Benefits

### For Extension Developers
- **Clean API**: Standardized way to control VACUUM behavior
- **No Core Modifications**: Extensions work without PostgreSQL patches
- **Performance**: Hooks are called only when needed
- **Flexibility**: Full control over VACUUM decisions and parameters

### For PostgreSQL Core
- **Extensibility**: Enables sophisticated VACUUM control without core changes
- **Maintainability**: Keeps core autovacuum logic simple
- **Backward Compatibility**: Existing behavior unchanged when hooks are NULL
- **Performance**: Minimal overhead when hooks are not used

## Usage Example

Once the hooks are accepted into PostgreSQL core, extensions can use them like this:

```c
// In extension's _PG_init function
void _PG_init(void)
{
    // Register hooks
    vacuum_should_vacuum_hook = my_vacuum_decision_hook;
    vacuum_adjust_params_hook = my_vacuum_params_hook;
}

// Hook implementation
bool my_vacuum_decision_hook(Relation rel, VacuumParams *params,
                            double n_dead_tup, double n_live_tup,
                            TransactionId relfrozenxid, MultiXactId relminmxid)
{
    // Custom logic: only vacuum if dead tuple ratio > 20%
    if (n_live_tup > 0 && (n_dead_tup / n_live_tup) > 0.2)
        return true;
    return false;
}
```

## Contribution Process

1. **Review**: Read `CONTRIBUTION_GUIDE.md` for detailed instructions
2. **Test**: Apply patches and run PostgreSQL tests
3. **Submit**: Send patch to pgsql-hackers@lists.postgresql.org
4. **Iterate**: Address review comments and refine the patch
5. **Acceptance**: Patch is reviewed and potentially accepted

## Alternative Approaches

If the core contribution is not accepted, consider:

1. **Background Workers**: Use PostgreSQL's background worker system
2. **Event Triggers**: Use PostgreSQL's event trigger system
3. **Custom Commands**: Create custom VACUUM commands
4. **External Tools**: Build external VACUUM management tools

## Status

- **Current**: Ready for submission to PostgreSQL core
- **Next Steps**: Submit to pgsql-hackers mailing list
- **Timeline**: 3-6 months for review and potential acceptance

## Resources

- [PostgreSQL Development Guide](https://www.postgresql.org/docs/current/contributing.html)
- [pgsql-hackers Mailing List](https://www.postgresql.org/list/pgsql-hackers/)
- [Extension Development](https://www.postgresql.org/docs/current/extend.html)

## Support

For questions about this contribution:

1. **PostgreSQL Community**: pgsql-hackers@lists.postgresql.org
2. **Extension Development**: pgsql-general@lists.postgresql.org
3. **Documentation**: PostgreSQL documentation and wiki

---

**Note**: This contribution represents a significant enhancement to PostgreSQL's extensibility. The hooks provide a clean, efficient way for extensions to control VACUUM behavior without modifying core code, benefiting the entire PostgreSQL ecosystem. 