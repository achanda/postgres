#ifndef VACUUM_FREQUENCY_H
#define VACUUM_FREQUENCY_H

#include "postgres.h"
#include "commands/vacuum.h"

/* Global hook variables - these will be used by PostgreSQL core */
extern PGDLLIMPORT vacuum_should_vacuum_hook_type vacuum_should_vacuum_hook;
extern PGDLLIMPORT vacuum_adjust_params_hook_type vacuum_adjust_params_hook;
extern PGDLLIMPORT vacuum_get_priority_hook_type vacuum_get_priority_hook;
extern PGDLLIMPORT vacuum_post_scan_hook_type vacuum_post_scan_hook;

#endif /* VACUUM_FREQUENCY_H */ 