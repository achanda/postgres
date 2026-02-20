/*-------------------------------------------------------------------------
 *
 * custom_autovacuum.h
 *	  Header file for custom autovacuum policy framework
 *
 * Portions Copyright (c) 1996-2025, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/custom_autovacuum.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef CUSTOM_AUTOVACUUM_H
#define CUSTOM_AUTOVACUUM_H

#include "postgres.h"
#include "catalog/pg_class.h"

/* Forward declarations */
struct PgStat_StatTabEntry;
struct AutoVacOpts;

/*
 * Context structure passed to custom autovacuum policy functions
 */
typedef struct CustomAutovacuumPolicyContext
{
	Oid			relation_oid;	/* The table being considered */
	Oid			database_oid;	/* Database containing the table */
	char	   *relation_name;	/* Table name */
	char	   *schema_name;	/* Schema name */
	Form_pg_class class_form;	/* pg_class tuple data */
	struct PgStat_StatTabEntry *stats;	/* Statistics for the table */
	struct AutoVacOpts *relopts;	/* Table's autovacuum options */
	bool		force_vacuum;	/* Whether wraparound forces vacuum */
	int			effective_multixact_freeze_max_age;
	/* Additional context for policy decisions */
	float4		vacthresh;		/* Calculated vacuum threshold */
	float4		anlthresh;		/* Calculated analyze threshold */
	float4		vactuples;		/* Current dead tuples */
	float4		anltuples;		/* Current modified tuples since analyze */
}			CustomAutovacuumPolicyContext;

/*
 * Result structure returned by custom autovacuum policy functions
 */
typedef struct CustomAutovacuumPolicyResult
{
	bool		should_vacuum;	/* Whether to vacuum this table */
	bool		should_analyze; /* Whether to analyze this table */
	bool		skip_table;		/* Skip this table entirely */
	char	   *reason;			/* Human-readable reason for decision */
	/* Optional overrides */
	int			custom_vac_cost_limit;	/* Override vacuum cost limit */
	double		custom_vac_cost_delay;	/* Override vacuum cost delay */
}			CustomAutovacuumPolicyResult;

/*
 * Function pointer type for custom autovacuum policy functions
 */
typedef CustomAutovacuumPolicyResult(*CustomAutovacuumPolicyFunction) (
																	   CustomAutovacuumPolicyContext * context);

/*
 * Hook type for custom autovacuum policy decisions
 */
typedef CustomAutovacuumPolicyResult(*custom_autovacuum_policy_hook_type) (
																		   CustomAutovacuumPolicyContext * context);

/*
 * Hook variable - declared in autovacuum.c, accessible to extensions
 */
extern PGDLLIMPORT custom_autovacuum_policy_hook_type custom_autovacuum_policy_hook;

#endif							/* CUSTOM_AUTOVACUUM_H */
