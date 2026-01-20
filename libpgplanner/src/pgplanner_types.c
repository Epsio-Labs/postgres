/*-------------------------------------------------------------------------
 *
 * pgplanner_types.c
 *    Built-in type registration for the standalone PostgreSQL planner.
 *
 * This file registers all the built-in PostgreSQL types that are needed
 * for planning queries. Types are inserted into the syscache so the
 * planner can resolve type names and perform type-related operations.
 *
 * Copyright (c) 2024, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/htup_details.h"
#include "catalog/pg_authid_d.h"
#include "catalog/pg_collation.h"
#include "catalog/pg_namespace_d.h"
#include "catalog/pg_type.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/hsearch.h"
#include "utils/memutils.h"
#include "utils/syscache.h"

#include "pgplanner.h"
#include "pgplanner_internal.h"

/*
 * Type information for built-in types.
 * This is derived from pg_type.dat in the PostgreSQL source.
 */
static const PgTypeInfo builtin_types[] = {
    /* Boolean */
    {16, "bool", 1, true, 'c', 'p', 'b', 0, 1000, F_BOOLIN, F_BOOLOUT, InvalidOid},

    /* Binary */
    {17, "bytea", -1, false, 'i', 'x', 'b', 0, 1001, F_BYTEAIN, F_BYTEAOUT, InvalidOid},

    /* Character types */
    {18, "char", 1, true, 'c', 'p', 'b', 0, 1002, F_CHARIN, F_CHAROUT, InvalidOid},
    {19, "name", NAMEDATALEN, false, 'c', 'p', 'b', 18, 1003, F_NAMEIN, F_NAMEOUT, C_COLLATION_OID},

    /* Integer types */
    {20, "int8", 8, FLOAT8PASSBYVAL, 'd', 'p', 'b', 0, 1016, F_INT8IN, F_INT8OUT, InvalidOid},
    {21, "int2", 2, true, 's', 'p', 'b', 0, 1005, F_INT2IN, F_INT2OUT, InvalidOid},
    {23, "int4", 4, true, 'i', 'p', 'b', 0, 1007, F_INT4IN, F_INT4OUT, InvalidOid},

    /* OID and related */
    {26, "oid", 4, true, 'i', 'p', 'b', 0, 1028, F_OIDIN, F_OIDOUT, InvalidOid},

    /* Text/String types */
    {25, "text", -1, false, 'i', 'x', 'b', 0, 1009, F_TEXTIN, F_TEXTOUT, DEFAULT_COLLATION_OID},
    {1042, "bpchar", -1, false, 'i', 'x', 'b', 0, 1014, F_BPCHARIN, F_BPCHAROUT, DEFAULT_COLLATION_OID},
    {1043, "varchar", -1, false, 'i', 'x', 'b', 0, 1015, F_VARCHARIN, F_VARCHAROUT, DEFAULT_COLLATION_OID},

    /* Floating point types */
    {700, "float4", 4, true, 'i', 'p', 'b', 0, 1021, F_FLOAT4IN, F_FLOAT4OUT, InvalidOid},
    {701, "float8", 8, FLOAT8PASSBYVAL, 'd', 'p', 'b', 0, 1022, F_FLOAT8IN, F_FLOAT8OUT, InvalidOid},

    /* Numeric */
    {1700, "numeric", -1, false, 'i', 'm', 'b', 0, 1231, F_NUMERIC_IN, F_NUMERIC_OUT, InvalidOid},

    /* Date/Time types */
    {1082, "date", 4, true, 'i', 'p', 'b', 0, 1182, F_DATE_IN, F_DATE_OUT, InvalidOid},
    {1083, "time", 8, FLOAT8PASSBYVAL, 'd', 'p', 'b', 0, 1183, F_TIME_IN, F_TIME_OUT, InvalidOid},
    {1114, "timestamp", 8, FLOAT8PASSBYVAL, 'd', 'p', 'b', 0, 1115, F_TIMESTAMP_IN, F_TIMESTAMP_OUT, InvalidOid},
    {1184, "timestamptz", 8, FLOAT8PASSBYVAL, 'd', 'p', 'b', 0, 1185, F_TIMESTAMPTZ_IN, F_TIMESTAMPTZ_OUT, InvalidOid},
    {1186, "interval", 16, false, 'd', 'p', 'b', 0, 1187, F_INTERVAL_IN, F_INTERVAL_OUT, InvalidOid},
    {1266, "timetz", 12, false, 'd', 'p', 'b', 0, 1270, F_TIMETZ_IN, F_TIMETZ_OUT, InvalidOid},

    /* UUID */
    {2950, "uuid", 16, false, 'c', 'p', 'b', 0, 2951, F_UUID_IN, F_UUID_OUT, InvalidOid},

    /* JSON */
    {114, "json", -1, false, 'i', 'x', 'b', 0, 199, F_JSON_IN, F_JSON_OUT, InvalidOid},
    {3802, "jsonb", -1, false, 'i', 'x', 'b', 0, 3807, F_JSONB_IN, F_JSONB_OUT, InvalidOid},

    /* Special types */
    {2278, "void", 4, true, 'i', 'p', 'p', 0, 0, F_VOID_IN, F_VOID_OUT, InvalidOid},
    {705, "unknown", -2, false, 'c', 'p', 'p', 0, 0, F_UNKNOWNIN, F_UNKNOWNOUT, InvalidOid},
    {2249, "record", -1, false, 'd', 'x', 'p', 0, 2287, F_RECORD_IN, F_RECORD_OUT, InvalidOid},

    /* Array types for common types */
    {1000, "_bool", -1, false, 'i', 'x', 'b', 16, 0, F_ARRAY_IN, F_ARRAY_OUT, InvalidOid},
    {1001, "_bytea", -1, false, 'i', 'x', 'b', 17, 0, F_ARRAY_IN, F_ARRAY_OUT, InvalidOid},
    {1005, "_int2", -1, false, 'i', 'x', 'b', 21, 0, F_ARRAY_IN, F_ARRAY_OUT, InvalidOid},
    {1007, "_int4", -1, false, 'i', 'x', 'b', 23, 0, F_ARRAY_IN, F_ARRAY_OUT, InvalidOid},
    {1009, "_text", -1, false, 'i', 'x', 'b', 25, 0, F_ARRAY_IN, F_ARRAY_OUT, DEFAULT_COLLATION_OID},
    {1014, "_bpchar", -1, false, 'i', 'x', 'b', 1042, 0, F_ARRAY_IN, F_ARRAY_OUT, DEFAULT_COLLATION_OID},
    {1015, "_varchar", -1, false, 'i', 'x', 'b', 1043, 0, F_ARRAY_IN, F_ARRAY_OUT, DEFAULT_COLLATION_OID},
    {1016, "_int8", -1, false, 'i', 'x', 'b', 20, 0, F_ARRAY_IN, F_ARRAY_OUT, InvalidOid},
    {1021, "_float4", -1, false, 'i', 'x', 'b', 700, 0, F_ARRAY_IN, F_ARRAY_OUT, InvalidOid},
    {1022, "_float8", -1, false, 'i', 'x', 'b', 701, 0, F_ARRAY_IN, F_ARRAY_OUT, InvalidOid},
    {1028, "_oid", -1, false, 'i', 'x', 'b', 26, 0, F_ARRAY_IN, F_ARRAY_OUT, InvalidOid},
    {1115, "_timestamp", -1, false, 'i', 'x', 'b', 1114, 0, F_ARRAY_IN, F_ARRAY_OUT, InvalidOid},
    {1182, "_date", -1, false, 'i', 'x', 'b', 1082, 0, F_ARRAY_IN, F_ARRAY_OUT, InvalidOid},
    {1185, "_timestamptz", -1, false, 'i', 'x', 'b', 1184, 0, F_ARRAY_IN, F_ARRAY_OUT, InvalidOid},
    {1231, "_numeric", -1, false, 'i', 'x', 'b', 1700, 0, F_ARRAY_IN, F_ARRAY_OUT, InvalidOid},
    {2287, "_record", -1, false, 'i', 'x', 'p', 2249, 0, F_ARRAY_IN, F_ARRAY_OUT, InvalidOid},

    /* Sentinel */
    {0, NULL, 0, false, 0, 0, 0, 0, 0, 0, 0, 0}
};

/* Hash tables for type lookup */
static HTAB *TypeOidHash = NULL;
static HTAB *TypeNameHash = NULL;

/* Hash table entry structures */
typedef struct TypeOidEntry
{
    Oid key;
    const PgTypeInfo *info;
} TypeOidEntry;

typedef struct TypeNameEntry
{
    char key[NAMEDATALEN];
    Oid typid;
} TypeNameEntry;

/* Type name list for pgplanner_list_types() */
static const char *type_names[] = {
    "bool", "bytea", "char", "name",
    "int2", "int4", "int8",
    "float4", "float8", "numeric",
    "text", "varchar", "bpchar",
    "date", "time", "timetz", "timestamp", "timestamptz", "interval",
    "uuid", "json", "jsonb",
    "oid", "void", "unknown", "record",
    NULL
};

/* Forward declaration */
static void pgplanner_register_type_syscache(const PgTypeInfo *typeinfo);

/*
 * pgplanner_init_types
 *    Initialize the built-in type system.
 */
void
pgplanner_init_types(void)
{
    HASHCTL hashctl;
    const PgTypeInfo *typeinfo;
    MemoryContext oldcxt;

    oldcxt = MemoryContextSwitchTo(CacheMemoryContext);

    /*
     * Create hash table for OID -> TypeInfo lookup
     */
    memset(&hashctl, 0, sizeof(hashctl));
    hashctl.keysize = sizeof(Oid);
    hashctl.entrysize = sizeof(TypeOidEntry);
    hashctl.hcxt = CacheMemoryContext;

    TypeOidHash = hash_create("PgPlanner Type OID Hash",
                              128,
                              &hashctl,
                              HASH_ELEM | HASH_BLOBS | HASH_CONTEXT);

    /*
     * Create hash table for name -> OID lookup
     */
    memset(&hashctl, 0, sizeof(hashctl));
    hashctl.keysize = NAMEDATALEN;
    hashctl.entrysize = sizeof(TypeNameEntry);
    hashctl.hcxt = CacheMemoryContext;

    TypeNameHash = hash_create("PgPlanner Type Name Hash",
                               128,
                               &hashctl,
                               HASH_ELEM | HASH_STRINGS | HASH_CONTEXT);

    /*
     * Register all built-in types
     */
    for (typeinfo = builtin_types; typeinfo->typname != NULL; typeinfo++)
    {
        TypeOidEntry *oid_entry;
        TypeNameEntry *name_entry;
        bool found;

        /* Insert into OID hash */
        oid_entry = hash_search(TypeOidHash, &typeinfo->typid, HASH_ENTER, &found);
        if (!found)
            oid_entry->info = typeinfo;

        /* Insert into name hash */
        name_entry = hash_search(TypeNameHash, typeinfo->typname, HASH_ENTER, &found);
        if (!found)
            name_entry->typid = typeinfo->typid;

        /* Also register with syscache for SearchSysCache lookups */
        pgplanner_register_type_syscache(typeinfo);
    }

    MemoryContextSwitchTo(oldcxt);
}

/*
 * pgplanner_register_type_syscache
 *    Placeholder for syscache registration.
 *
 * Note: Since we intercept syscache lookups at a higher level via
 * our hash tables and stub functions, we don't need to actually
 * insert entries into the syscache. This function is a no-op
 * placeholder for potential future use.
 */
static void
pgplanner_register_type_syscache(const PgTypeInfo *typeinfo)
{
    /*
     * Type registration is handled by our hash tables.
     * Syscache lookups are intercepted by our stub functions.
     */
    (void) typeinfo;  /* Suppress unused parameter warning */
}

/*
 * pgplanner_get_type_info
 *    Look up type information by OID.
 */
const PgTypeInfo *
pgplanner_get_type_info(Oid typid)
{
    TypeOidEntry *entry;

    if (TypeOidHash == NULL)
        return NULL;

    entry = hash_search(TypeOidHash, &typid, HASH_FIND, NULL);
    if (entry == NULL)
        return NULL;

    return entry->info;
}

/*
 * pgplanner_get_type_info_by_name
 *    Look up type information by name.
 */
const PgTypeInfo *
pgplanner_get_type_info_by_name(const char *typname)
{
    TypeNameEntry *entry;

    if (TypeNameHash == NULL)
        return NULL;

    entry = hash_search(TypeNameHash, typname, HASH_FIND, NULL);
    if (entry == NULL)
        return NULL;

    return pgplanner_get_type_info(entry->typid);
}

/*
 * pgplanner_lookup_type_oid
 *    Look up a type OID by name.
 */
Oid
pgplanner_lookup_type_oid(const char *type_name)
{
    const PgTypeInfo *info = pgplanner_get_type_info_by_name(type_name);
    if (info == NULL)
        return InvalidOid;
    return info->typid;
}

/*
 * pgplanner_type_exists_internal
 *    Check if a type name exists.
 */
bool
pgplanner_type_exists_internal(const char *type_name)
{
    return pgplanner_lookup_type_oid(type_name) != InvalidOid;
}

/*
 * pgplanner_type_exists
 *    Public API: Check if a type name is supported.
 */
bool
pgplanner_type_exists(const char *type_name)
{
    if (!pgplanner_is_initialized())
        return false;
    return pgplanner_type_exists_internal(type_name);
}

/*
 * pgplanner_list_types
 *    Public API: Get list of supported type names.
 */
const char **
pgplanner_list_types(void)
{
    return type_names;
}
