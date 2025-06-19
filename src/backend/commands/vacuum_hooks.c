/*-------------------------------------------------------------------------
 *
 * vacuum_hooks.c
 *    Hook variables for autovacuum control
 *
 * This file defines the hook variables that extensions can use to
 * control autovacuum behavior.
 *
 * Copyright (c) 2024, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "commands/vacuum.h"

/* Hook variables - initialized to NULL */
PGDLLIMPORT vacuum_should_vacuum_hook_type vacuum_should_vacuum_hook = NULL;
PGDLLIMPORT vacuum_adjust_params_hook_type vacuum_adjust_params_hook = NULL;
PGDLLIMPORT vacuum_get_priority_hook_type vacuum_get_priority_hook = NULL;
PGDLLIMPORT vacuum_post_scan_hook_type vacuum_post_scan_hook = NULL; 