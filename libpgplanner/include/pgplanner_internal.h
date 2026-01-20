/*-------------------------------------------------------------------------
 *
 * pgplanner_internal.h
 *    Internal declarations for the standalone PostgreSQL planner library.
 *
 * This header is not part of the public API and should not be included
 * by library users.
 *
 * Copyright (c) 2024, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */
#ifndef PGPLANNER_INTERNAL_H
#define PGPLANNER_INTERNAL_H

#include "postgres.h"
#include "access/htup.h"
#include "access/tupdesc.h"
#include "nodes/pg_list.h"
#include "utils/hsearch.h"
#include "utils/rel.h"

/* Memory context for user-facing allocations */
extern MemoryContext PgPlannerContext;

/*-------------------------------------------------------------------------
 * Initialization functions (called from pgplanner_init.c)
 *-------------------------------------------------------------------------
 */

/* Type system initialization (pgplanner_types.c) */
extern void pgplanner_init_types(void);
extern Oid pgplanner_lookup_type_oid(const char *type_name);
extern bool pgplanner_type_exists_internal(const char *type_name);

/* Operator system initialization (pgplanner_operators.c) */
extern void pgplanner_init_operators(void);

/* Function system initialization (pgplanner_functions.c) */
extern void pgplanner_init_functions(void);

/* Namespace initialization (pgplanner_catalog.c) */
extern void pgplanner_init_namespace(void);

/* Relation cleanup (pgplanner_relations.c) */
extern void pgplanner_cleanup_relations(void);

/*-------------------------------------------------------------------------
 * Error handling
 *-------------------------------------------------------------------------
 */

/* Set the last error message */
extern void pgplanner_set_error(const char *fmt, ...) pg_attribute_printf(1, 2);

/*-------------------------------------------------------------------------
 * Locking (from pgplanner_init.c)
 *-------------------------------------------------------------------------
 */

extern void pgplanner_lock(void);
extern void pgplanner_unlock(void);

/*-------------------------------------------------------------------------
 * Catalog helpers (pgplanner_catalog.c)
 *-------------------------------------------------------------------------
 */

/* Insert a fake tuple into a syscache */
extern void pgplanner_insert_syscache_entry(int cacheId, HeapTuple tuple);

/* Create namespace entry */
extern Oid pgplanner_create_namespace(const char *nspname, bool is_public);

/* Get/create public namespace OID */
extern Oid pgplanner_get_public_namespace(void);

/*-------------------------------------------------------------------------
 * Relation helpers (pgplanner_relations.c)
 *-------------------------------------------------------------------------
 */

/* OID allocation */
extern Oid pgplanner_alloc_oid(void);

/* Table lookup via callback (returns InvalidOid if not found) */
extern Oid pgplanner_lookup_table_via_callback(const char *schema_name,
                                               const char *table_name);

/* Find a registered table by name (internal use) */
extern Oid pgplanner_find_table(const char *schema_name,
                                const char *table_name);

/* Internal relation registration */
extern Relation pgplanner_create_relation(const char *relname,
                                          Oid relnamespace,
                                          Oid relid,
                                          TupleDesc tupdesc,
                                          char relkind);

/*-------------------------------------------------------------------------
 * Type helpers (pgplanner_types.c)
 *-------------------------------------------------------------------------
 */

typedef struct PgTypeInfo
{
    Oid         typid;
    const char *typname;
    int16       typlen;
    bool        typbyval;
    char        typalign;
    char        typstorage;
    char        typtype;        /* 'b' = base, 'c' = composite, etc. */
    Oid         typelem;        /* Array element type OID */
    Oid         typarray;       /* Array type OID for this type */
    Oid         typinput;       /* Input function OID */
    Oid         typoutput;      /* Output function OID */
    Oid         typcollation;   /* Default collation */
} PgTypeInfo;

extern const PgTypeInfo *pgplanner_get_type_info(Oid typid);
extern const PgTypeInfo *pgplanner_get_type_info_by_name(const char *typname);

/*-------------------------------------------------------------------------
 * Operator helpers (pgplanner_operators.c)
 *-------------------------------------------------------------------------
 */

typedef struct PgOperatorInfo
{
    Oid         oprid;
    const char *oprname;
    Oid         oprleft;        /* Left operand type */
    Oid         oprright;       /* Right operand type */
    Oid         oprresult;      /* Result type */
    Oid         oprcode;        /* Implementing function OID */
    bool        oprcanmerge;    /* Can use merge join */
    bool        oprcanhash;     /* Can use hash join */
    Oid         oprcom;         /* Commutator operator OID */
    Oid         oprnegate;      /* Negator operator OID */
    Oid         oprrest;        /* Restriction selectivity function */
    Oid         oprjoin;        /* Join selectivity function */
} PgOperatorInfo;

extern const PgOperatorInfo *pgplanner_get_operator_info(Oid oprid);

/*-------------------------------------------------------------------------
 * Function helpers (pgplanner_functions.c)
 *-------------------------------------------------------------------------
 */

typedef struct PgFunctionInfo
{
    Oid         funcid;
    const char *funcname;
    Oid         funcrettype;
    int         funcnargs;
    Oid         funcargtypes[FUNC_MAX_ARGS];
    bool        funcstrict;
    bool        funcretset;
    char        funcvolatile;   /* 'i' = immutable, 's' = stable, 'v' = volatile */
} PgFunctionInfo;

extern const PgFunctionInfo *pgplanner_get_function_info(Oid funcid);

/*-------------------------------------------------------------------------
 * Memory helpers (pgplanner_memory.c)
 *-------------------------------------------------------------------------
 */

extern void *pgplanner_alloc_zero(size_t size);
extern void *pgplanner_realloc(void *ptr, size_t size);
extern void *pgplanner_memdup(const void *data, size_t size);

/*-------------------------------------------------------------------------
 * Query conversion (pgplanner_query.c)
 *-------------------------------------------------------------------------
 */

/* Forward declaration - full type in pgplanner_query.h */
struct PgLogicalQuery;
struct Query;

extern struct PgLogicalQuery *pgplanner_convert_query(struct Query *query);

/*-------------------------------------------------------------------------
 * Constants
 *-------------------------------------------------------------------------
 */

/* First OID available for user objects */
#define PGPLANNER_FIRST_USER_OID    16384

/* Namespace OIDs */
#define PGPLANNER_PG_CATALOG_OID    11
#define PGPLANNER_PUBLIC_OID        2200

#endif /* PGPLANNER_INTERNAL_H */
