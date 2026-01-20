/*-------------------------------------------------------------------------
 *
 * pgplanner_catalog.c
 *    Catalog/syscache helpers for the standalone PostgreSQL planner.
 *
 * This file provides functions to populate the system catalog caches
 * with fake entries, allowing the planner to look up types, operators,
 * functions, and relations without accessing actual catalog tables.
 *
 * Copyright (c) 2024, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/htup_details.h"
#include "catalog/pg_class.h"
#include "catalog/pg_collation.h"
#include "catalog/pg_collation_d.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_type.h"
#include "utils/catcache.h"
#include "utils/hsearch.h"
#include "utils/memutils.h"
#include "utils/syscache.h"

#include "pgplanner.h"
#include "pgplanner_internal.h"

/*
 * For a standalone planner, we need to intercept syscache lookups
 * and provide our own data instead of reading from actual catalogs.
 *
 * The approach is:
 * 1. Pre-populate hash tables with all the metadata we need
 * 2. Hook into or replace syscache lookup functions
 * 3. Return our fake data when the planner queries the catalogs
 *
 * Since directly modifying catcache internals is complex, we instead
 * ensure all necessary data is available through our hash tables
 * and rely on the planner's fallback paths or provide wrapper functions.
 */

/*-------------------------------------------------------------------------
 * Fake syscache entry management
 *
 * Since we can't easily inject entries into PostgreSQL's catcache,
 * we maintain parallel hash tables and intercept lookups where possible.
 *-------------------------------------------------------------------------
 */

/*
 * pgplanner_insert_syscache_entry
 *    Insert a fake tuple into our shadow syscache.
 *
 * This is a placeholder for when we need more sophisticated syscache
 * interception. For now, most lookups go through our custom hash tables.
 */
void
pgplanner_insert_syscache_entry(int cacheId, HeapTuple tuple)
{
    /*
     * TODO: Implement proper syscache shadowing if needed.
     *
     * The challenge is that PostgreSQL's catcache is designed to read
     * from actual heap tables. For a standalone library, we need either:
     *
     * 1. Modify SearchSysCache to check our hash tables first
     * 2. Pre-populate the catcache during initialization
     * 3. Provide our own lookup functions that the planner can use
     *
     * Currently, we use approach #3 via our hash tables in
     * pgplanner_types.c, pgplanner_operators.c, etc.
     */
}

/*-------------------------------------------------------------------------
 * Namespace lookup helpers
 *
 * These help the planner resolve schema-qualified names.
 *-------------------------------------------------------------------------
 */

typedef struct NamespaceEntry
{
    Oid nspoid;
    char nspname[NAMEDATALEN];
} NamespaceEntry;

static HTAB *NamespaceOidHash = NULL;
static HTAB *NamespaceNameHash = NULL;

/*
 * Initialize namespace hash tables
 */
static void
ensure_namespace_hashes(void)
{
    HASHCTL hashctl;

    if (NamespaceOidHash != NULL)
        return;

    memset(&hashctl, 0, sizeof(hashctl));
    hashctl.keysize = sizeof(Oid);
    hashctl.entrysize = sizeof(NamespaceEntry);
    hashctl.hcxt = CacheMemoryContext;

    NamespaceOidHash = hash_create("PgPlanner Namespace OID Hash",
                                   32,
                                   &hashctl,
                                   HASH_ELEM | HASH_BLOBS | HASH_CONTEXT);

    memset(&hashctl, 0, sizeof(hashctl));
    hashctl.keysize = NAMEDATALEN;
    hashctl.entrysize = sizeof(NamespaceEntry);
    hashctl.hcxt = CacheMemoryContext;

    NamespaceNameHash = hash_create("PgPlanner Namespace Name Hash",
                                    32,
                                    &hashctl,
                                    HASH_ELEM | HASH_STRINGS | HASH_CONTEXT);

    /* Register built-in namespaces */
    {
        NamespaceEntry *entry;
        bool found;

        /* pg_catalog */
        entry = hash_search(NamespaceOidHash, &(Oid){PGPLANNER_PG_CATALOG_OID},
                            HASH_ENTER, &found);
        if (!found)
        {
            entry->nspoid = PGPLANNER_PG_CATALOG_OID;
            strlcpy(entry->nspname, "pg_catalog", NAMEDATALEN);
        }
        entry = hash_search(NamespaceNameHash, "pg_catalog", HASH_ENTER, &found);
        if (!found)
        {
            entry->nspoid = PGPLANNER_PG_CATALOG_OID;
            strlcpy(entry->nspname, "pg_catalog", NAMEDATALEN);
        }

        /* public */
        entry = hash_search(NamespaceOidHash, &(Oid){PGPLANNER_PUBLIC_OID},
                            HASH_ENTER, &found);
        if (!found)
        {
            entry->nspoid = PGPLANNER_PUBLIC_OID;
            strlcpy(entry->nspname, "public", NAMEDATALEN);
        }
        entry = hash_search(NamespaceNameHash, "public", HASH_ENTER, &found);
        if (!found)
        {
            entry->nspoid = PGPLANNER_PUBLIC_OID;
            strlcpy(entry->nspname, "public", NAMEDATALEN);
        }
    }
}

/*-------------------------------------------------------------------------
 * Collation lookup helpers
 *-------------------------------------------------------------------------
 */

#ifdef PG_PLANNER_STANDALONE

Oid
get_collation_oid(List *collname, bool missing_ok)
{
    /*
     * For standalone mode, return the default collation.
     * A more complete implementation would look up the collation name.
     */
    return DEFAULT_COLLATION_OID;
}

char *
get_collation_name(Oid colloid)
{
    if (colloid == DEFAULT_COLLATION_OID)
        return pstrdup("default");
    if (colloid == C_COLLATION_OID)
        return pstrdup("C");
    return pstrdup("default");
}

Oid
get_collation_isdeterministic(Oid colloid)
{
    return true;  /* All our collations are deterministic */
}

#endif /* PG_PLANNER_STANDALONE */

/*-------------------------------------------------------------------------
 * Type lookup helpers
 *
 * These wrap our type hash table for PostgreSQL-compatible lookups.
 *-------------------------------------------------------------------------
 */

#ifdef PG_PLANNER_STANDALONE

Oid
TypenameGetTypid(const char *typname)
{
    return pgplanner_lookup_type_oid(typname);
}

Oid
TypenameGetTypidExtended(const char *typname, bool temp_ok)
{
    return pgplanner_lookup_type_oid(typname);
}

char *
format_type_be(Oid type_oid)
{
    const PgTypeInfo *info = pgplanner_get_type_info(type_oid);
    if (info != NULL)
        return pstrdup(info->typname);
    return pstrdup("unknown");
}

char *
format_type_with_typemod(Oid type_oid, int32 typemod)
{
    return format_type_be(type_oid);
}

bool
type_is_rowtype(Oid typid)
{
    const PgTypeInfo *info = pgplanner_get_type_info(typid);
    if (info == NULL)
        return false;
    return (info->typtype == 'c');  /* composite type */
}

bool
type_is_enum(Oid typid)
{
    const PgTypeInfo *info = pgplanner_get_type_info(typid);
    if (info == NULL)
        return false;
    return (info->typtype == 'e');  /* enum type */
}

bool
type_is_range(Oid typid)
{
    const PgTypeInfo *info = pgplanner_get_type_info(typid);
    if (info == NULL)
        return false;
    return (info->typtype == 'r');  /* range type */
}

bool
type_is_multirange(Oid typid)
{
    return false;  /* We don't support multirange types yet */
}

Oid
get_element_type(Oid typid)
{
    const PgTypeInfo *info = pgplanner_get_type_info(typid);
    if (info == NULL)
        return InvalidOid;
    return info->typelem;
}

Oid
get_array_type(Oid typid)
{
    const PgTypeInfo *info = pgplanner_get_type_info(typid);
    if (info == NULL)
        return InvalidOid;
    return info->typarray;
}

Oid
getBaseType(Oid typid)
{
    /* We don't support domain types, so base type is always itself */
    return typid;
}

int16
get_typlen(Oid typid)
{
    const PgTypeInfo *info = pgplanner_get_type_info(typid);
    if (info == NULL)
        return 0;
    return info->typlen;
}

bool
get_typbyval(Oid typid)
{
    const PgTypeInfo *info = pgplanner_get_type_info(typid);
    if (info == NULL)
        return false;
    return info->typbyval;
}

void
get_typlenbyvalalign(Oid typid, int16 *typlen, bool *typbyval, char *typalign)
{
    const PgTypeInfo *info = pgplanner_get_type_info(typid);
    if (info == NULL)
    {
        *typlen = -1;
        *typbyval = false;
        *typalign = 'i';
        return;
    }
    *typlen = info->typlen;
    *typbyval = info->typbyval;
    *typalign = info->typalign;
}

char
get_typstorage(Oid typid)
{
    const PgTypeInfo *info = pgplanner_get_type_info(typid);
    if (info == NULL)
        return 'p';
    return info->typstorage;
}

Oid
get_typcollation(Oid typid)
{
    const PgTypeInfo *info = pgplanner_get_type_info(typid);
    if (info == NULL)
        return InvalidOid;
    return info->typcollation;
}

bool
type_is_collatable(Oid typid)
{
    const PgTypeInfo *info = pgplanner_get_type_info(typid);
    if (info == NULL)
        return false;
    return OidIsValid(info->typcollation);
}

#endif /* PG_PLANNER_STANDALONE */

/*-------------------------------------------------------------------------
 * Operator lookup helpers
 *-------------------------------------------------------------------------
 */

#ifdef PG_PLANNER_STANDALONE

Oid
get_opcode(Oid opno)
{
    const PgOperatorInfo *info = pgplanner_get_operator_info(opno);
    if (info == NULL)
        return InvalidOid;
    return info->oprcode;
}

char *
get_opname(Oid opno)
{
    const PgOperatorInfo *info = pgplanner_get_operator_info(opno);
    if (info == NULL)
        return NULL;
    return pstrdup(info->oprname);
}

Oid
get_commutator(Oid opno)
{
    const PgOperatorInfo *info = pgplanner_get_operator_info(opno);
    if (info == NULL)
        return InvalidOid;
    return info->oprcom;
}

Oid
get_negator(Oid opno)
{
    const PgOperatorInfo *info = pgplanner_get_operator_info(opno);
    if (info == NULL)
        return InvalidOid;
    return info->oprnegate;
}

RegProcedure
get_oprrest(Oid opno)
{
    const PgOperatorInfo *info = pgplanner_get_operator_info(opno);
    if (info == NULL)
        return InvalidOid;
    return info->oprrest;
}

RegProcedure
get_oprjoin(Oid opno)
{
    const PgOperatorInfo *info = pgplanner_get_operator_info(opno);
    if (info == NULL)
        return InvalidOid;
    return info->oprjoin;
}

bool
op_mergejoinable(Oid opno, Oid inputtype)
{
    const PgOperatorInfo *info = pgplanner_get_operator_info(opno);
    if (info == NULL)
        return false;
    return info->oprcanmerge;
}

bool
op_hashjoinable(Oid opno, Oid inputtype)
{
    const PgOperatorInfo *info = pgplanner_get_operator_info(opno);
    if (info == NULL)
        return false;
    return info->oprcanhash;
}

void
get_op_opfamily_properties(Oid opno, Oid opfamily, bool ordering_op,
                           int *strategy, Oid *lefttype, Oid *righttype)
{
    /*
     * This would require looking up pg_amop entries.
     * For now, provide default values.
     */
    const PgOperatorInfo *info = pgplanner_get_operator_info(opno);

    *lefttype = info ? info->oprleft : InvalidOid;
    *righttype = info ? info->oprright : InvalidOid;
    *strategy = 0;  /* Unknown strategy */
}

#endif /* PG_PLANNER_STANDALONE */

/*-------------------------------------------------------------------------
 * Function lookup helpers
 *-------------------------------------------------------------------------
 */

#ifdef PG_PLANNER_STANDALONE

char
func_volatile(Oid funcid)
{
    const PgFunctionInfo *info = pgplanner_get_function_info(funcid);
    if (info == NULL)
        return PROVOLATILE_VOLATILE;  /* Default to volatile */
    return info->funcvolatile;
}

bool
func_strict(Oid funcid)
{
    const PgFunctionInfo *info = pgplanner_get_function_info(funcid);
    if (info == NULL)
        return false;
    return info->funcstrict;
}

Oid
get_func_rettype(Oid funcid)
{
    const PgFunctionInfo *info = pgplanner_get_function_info(funcid);
    if (info == NULL)
        return InvalidOid;
    return info->funcrettype;
}

int
get_func_nargs(Oid funcid)
{
    const PgFunctionInfo *info = pgplanner_get_function_info(funcid);
    if (info == NULL)
        return 0;
    return info->funcnargs;
}

char *
get_func_name(Oid funcid)
{
    const PgFunctionInfo *info = pgplanner_get_function_info(funcid);
    if (info == NULL)
        return NULL;
    return pstrdup(info->funcname);
}

bool
get_func_retset(Oid funcid)
{
    const PgFunctionInfo *info = pgplanner_get_function_info(funcid);
    if (info == NULL)
        return false;
    return info->funcretset;
}

#endif /* PG_PLANNER_STANDALONE */
