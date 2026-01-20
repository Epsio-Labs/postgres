/*-------------------------------------------------------------------------
 *
 * pgplanner_operators.c
 *    Built-in operator registration for the standalone PostgreSQL planner.
 *
 * This file registers the operators needed for planning queries.
 * Operators are essential for the planner to understand comparisons,
 * arithmetic, and other operations in query predicates.
 *
 * Copyright (c) 2024, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "catalog/pg_operator.h"
#include "catalog/pg_opfamily.h"
#include "catalog/pg_amop.h"
#include "catalog/pg_amproc.h"
#include "utils/fmgroids.h"
#include "utils/hsearch.h"
#include "utils/memutils.h"

#include "pgplanner.h"
#include "pgplanner_internal.h"

/*
 * Operator information for built-in operators.
 * This is derived from pg_operator.dat in the PostgreSQL source.
 */
static const PgOperatorInfo builtin_operators[] = {
    /*
     * Boolean operators
     */
    /* bool = bool */
    {91, "=", 16, 16, 16, F_BOOLEQ, true, true, 91, 85, F_EQSEL, F_EQJOINSEL},
    /* bool <> bool */
    {85, "<>", 16, 16, 16, F_BOOLNE, false, false, 85, 91, F_NEQSEL, F_NEQJOINSEL},
    /* bool < bool */
    {58, "<", 16, 16, 16, F_BOOLLT, false, false, 59, 1695, F_SCALARLTSEL, F_SCALARLTJOINSEL},
    /* bool > bool */
    {59, ">", 16, 16, 16, F_BOOLGT, false, false, 58, 1694, F_SCALARGTSEL, F_SCALARGTJOINSEL},
    /* bool <= bool */
    {1694, "<=", 16, 16, 16, F_BOOLLE, false, false, 1695, 59, F_SCALARLESEL, F_SCALARLEJOINSEL},
    /* bool >= bool */
    {1695, ">=", 16, 16, 16, F_BOOLGE, false, false, 1694, 58, F_SCALARGESEL, F_SCALARGEJOINSEL},

    /*
     * Integer operators (int4)
     */
    /* int4 = int4 */
    {96, "=", 23, 23, 16, F_INT4EQ, true, true, 96, 518, F_EQSEL, F_EQJOINSEL},
    /* int4 <> int4 */
    {518, "<>", 23, 23, 16, F_INT4NE, false, false, 518, 96, F_NEQSEL, F_NEQJOINSEL},
    /* int4 < int4 */
    {97, "<", 23, 23, 16, F_INT4LT, false, false, 521, 525, F_SCALARLTSEL, F_SCALARLTJOINSEL},
    /* int4 > int4 */
    {521, ">", 23, 23, 16, F_INT4GT, false, false, 97, 523, F_SCALARGTSEL, F_SCALARGTJOINSEL},
    /* int4 <= int4 */
    {523, "<=", 23, 23, 16, F_INT4LE, false, false, 525, 521, F_SCALARLESEL, F_SCALARLEJOINSEL},
    /* int4 >= int4 */
    {525, ">=", 23, 23, 16, F_INT4GE, false, false, 523, 97, F_SCALARGESEL, F_SCALARGEJOINSEL},
    /* int4 + int4 */
    {551, "+", 23, 23, 23, F_INT4PL, false, false, 551, 0, 0, 0},
    /* int4 - int4 */
    {555, "-", 23, 23, 23, F_INT4MI, false, false, 0, 0, 0, 0},
    /* int4 * int4 */
    {514, "*", 23, 23, 23, F_INT4MUL, false, false, 514, 0, 0, 0},
    /* int4 / int4 */
    {528, "/", 23, 23, 23, F_INT4DIV, false, false, 0, 0, 0, 0},
    /* int4 % int4 */
    {530, "%", 23, 23, 23, F_INT4MOD, false, false, 0, 0, 0, 0},

    /*
     * Integer operators (int8)
     */
    /* int8 = int8 */
    {410, "=", 20, 20, 16, F_INT8EQ, true, true, 410, 411, F_EQSEL, F_EQJOINSEL},
    /* int8 <> int8 */
    {411, "<>", 20, 20, 16, F_INT8NE, false, false, 411, 410, F_NEQSEL, F_NEQJOINSEL},
    /* int8 < int8 */
    {412, "<", 20, 20, 16, F_INT8LT, false, false, 413, 415, F_SCALARLTSEL, F_SCALARLTJOINSEL},
    /* int8 > int8 */
    {413, ">", 20, 20, 16, F_INT8GT, false, false, 412, 414, F_SCALARGTSEL, F_SCALARGTJOINSEL},
    /* int8 <= int8 */
    {414, "<=", 20, 20, 16, F_INT8LE, false, false, 415, 413, F_SCALARLESEL, F_SCALARLEJOINSEL},
    /* int8 >= int8 */
    {415, ">=", 20, 20, 16, F_INT8GE, false, false, 414, 412, F_SCALARGESEL, F_SCALARGEJOINSEL},
    /* int8 + int8 */
    {684, "+", 20, 20, 20, F_INT8PL, false, false, 684, 0, 0, 0},
    /* int8 - int8 */
    {685, "-", 20, 20, 20, F_INT8MI, false, false, 0, 0, 0, 0},
    /* int8 * int8 */
    {686, "*", 20, 20, 20, F_INT8MUL, false, false, 686, 0, 0, 0},
    /* int8 / int8 */
    {687, "/", 20, 20, 20, F_INT8DIV, false, false, 0, 0, 0, 0},

    /*
     * Integer operators (int2)
     */
    /* int2 = int2 */
    {94, "=", 21, 21, 16, F_INT2EQ, true, true, 94, 519, F_EQSEL, F_EQJOINSEL},
    /* int2 <> int2 */
    {519, "<>", 21, 21, 16, F_INT2NE, false, false, 519, 94, F_NEQSEL, F_NEQJOINSEL},
    /* int2 < int2 */
    {95, "<", 21, 21, 16, F_INT2LT, false, false, 520, 524, F_SCALARLTSEL, F_SCALARLTJOINSEL},
    /* int2 > int2 */
    {520, ">", 21, 21, 16, F_INT2GT, false, false, 95, 522, F_SCALARGTSEL, F_SCALARGTJOINSEL},
    /* int2 <= int2 */
    {522, "<=", 21, 21, 16, F_INT2LE, false, false, 524, 520, F_SCALARLESEL, F_SCALARLEJOINSEL},
    /* int2 >= int2 */
    {524, ">=", 21, 21, 16, F_INT2GE, false, false, 522, 95, F_SCALARGESEL, F_SCALARGEJOINSEL},

    /*
     * Cross-type integer comparisons (int4 vs int8)
     */
    /* int4 = int8 */
    {416, "=", 23, 20, 16, F_INT48EQ, true, true, 410, 417, F_EQSEL, F_EQJOINSEL},
    /* int4 <> int8 */
    {417, "<>", 23, 20, 16, F_INT48NE, false, false, 417, 416, F_NEQSEL, F_NEQJOINSEL},
    /* int4 < int8 */
    {418, "<", 23, 20, 16, F_INT48LT, false, false, 419, 430, F_SCALARLTSEL, F_SCALARLTJOINSEL},
    /* int4 > int8 */
    {419, ">", 23, 20, 16, F_INT48GT, false, false, 418, 420, F_SCALARGTSEL, F_SCALARGTJOINSEL},
    /* int4 <= int8 */
    {420, "<=", 23, 20, 16, F_INT48LE, false, false, 430, 419, F_SCALARLESEL, F_SCALARLEJOINSEL},
    /* int4 >= int8 */
    {430, ">=", 23, 20, 16, F_INT48GE, false, false, 420, 418, F_SCALARGESEL, F_SCALARGEJOINSEL},

    /*
     * Floating point operators (float8)
     */
    /* float8 = float8 */
    {670, "=", 701, 701, 16, F_FLOAT8EQ, true, true, 670, 671, F_EQSEL, F_EQJOINSEL},
    /* float8 <> float8 */
    {671, "<>", 701, 701, 16, F_FLOAT8NE, false, false, 671, 670, F_NEQSEL, F_NEQJOINSEL},
    /* float8 < float8 */
    {672, "<", 701, 701, 16, F_FLOAT8LT, false, false, 674, 675, F_SCALARLTSEL, F_SCALARLTJOINSEL},
    /* float8 > float8 */
    {674, ">", 701, 701, 16, F_FLOAT8GT, false, false, 672, 673, F_SCALARGTSEL, F_SCALARGTJOINSEL},
    /* float8 <= float8 */
    {673, "<=", 701, 701, 16, F_FLOAT8LE, false, false, 675, 674, F_SCALARLESEL, F_SCALARLEJOINSEL},
    /* float8 >= float8 */
    {675, ">=", 701, 701, 16, F_FLOAT8GE, false, false, 673, 672, F_SCALARGESEL, F_SCALARGEJOINSEL},
    /* float8 + float8 */
    {591, "+", 701, 701, 701, F_FLOAT8PL, false, false, 591, 0, 0, 0},
    /* float8 - float8 */
    {592, "-", 701, 701, 701, F_FLOAT8MI, false, false, 0, 0, 0, 0},
    /* float8 * float8 */
    {594, "*", 701, 701, 701, F_FLOAT8MUL, false, false, 594, 0, 0, 0},
    /* float8 / float8 */
    {593, "/", 701, 701, 701, F_FLOAT8DIV, false, false, 0, 0, 0, 0},

    /*
     * Floating point operators (float4)
     */
    /* float4 = float4 */
    {620, "=", 700, 700, 16, F_FLOAT4EQ, true, true, 620, 621, F_EQSEL, F_EQJOINSEL},
    /* float4 <> float4 */
    {621, "<>", 700, 700, 16, F_FLOAT4NE, false, false, 621, 620, F_NEQSEL, F_NEQJOINSEL},
    /* float4 < float4 */
    {622, "<", 700, 700, 16, F_FLOAT4LT, false, false, 624, 625, F_SCALARLTSEL, F_SCALARLTJOINSEL},
    /* float4 > float4 */
    {624, ">", 700, 700, 16, F_FLOAT4GT, false, false, 622, 623, F_SCALARGTSEL, F_SCALARGTJOINSEL},
    /* float4 <= float4 */
    {623, "<=", 700, 700, 16, F_FLOAT4LE, false, false, 625, 624, F_SCALARLESEL, F_SCALARLEJOINSEL},
    /* float4 >= float4 */
    {625, ">=", 700, 700, 16, F_FLOAT4GE, false, false, 623, 622, F_SCALARGESEL, F_SCALARGEJOINSEL},

    /*
     * Text/String operators
     */
    /* text = text */
    {98, "=", 25, 25, 16, F_TEXTEQ, true, true, 98, 531, F_EQSEL, F_EQJOINSEL},
    /* text <> text */
    {531, "<>", 25, 25, 16, F_TEXTNE, false, false, 531, 98, F_NEQSEL, F_NEQJOINSEL},
    /* text < text */
    {664, "<", 25, 25, 16, F_TEXT_LT, false, false, 666, 667, F_SCALARLTSEL, F_SCALARLTJOINSEL},
    /* text > text */
    {666, ">", 25, 25, 16, F_TEXT_GT, false, false, 664, 665, F_SCALARGTSEL, F_SCALARGTJOINSEL},
    /* text <= text */
    {665, "<=", 25, 25, 16, F_TEXT_LE, false, false, 667, 666, F_SCALARLESEL, F_SCALARLEJOINSEL},
    /* text >= text */
    {667, ">=", 25, 25, 16, F_TEXT_GE, false, false, 665, 664, F_SCALARGESEL, F_SCALARGEJOINSEL},
    /* text || text (concatenation) */
    {654, "||", 25, 25, 25, F_TEXTCAT, false, false, 0, 0, 0, 0},
    /* text ~~ text (LIKE) */
    {1209, "~~", 25, 25, 16, F_TEXTLIKE, false, false, 0, 1210, F_LIKESEL, F_LIKEJOINSEL},
    /* text !~~ text (NOT LIKE) */
    {1210, "!~~", 25, 25, 16, F_TEXTNLIKE, false, false, 0, 1209, F_NLIKESEL, F_NLIKEJOINSEL},

    /*
     * Varchar operators (using text operators underneath)
     */
    /* varchar = varchar */
    {1070, "=", 1043, 1043, 16, F_TEXTEQ, true, true, 1070, 1071, F_EQSEL, F_EQJOINSEL},
    /* varchar <> varchar */
    {1071, "<>", 1043, 1043, 16, F_TEXTNE, false, false, 1071, 1070, F_NEQSEL, F_NEQJOINSEL},
    /* varchar < varchar */
    {1072, "<", 1043, 1043, 16, F_TEXT_LT, false, false, 1074, 1075, F_SCALARLTSEL, F_SCALARLTJOINSEL},
    /* varchar > varchar */
    {1074, ">", 1043, 1043, 16, F_TEXT_GT, false, false, 1072, 1073, F_SCALARGTSEL, F_SCALARGTJOINSEL},
    /* varchar <= varchar */
    {1073, "<=", 1043, 1043, 16, F_TEXT_LE, false, false, 1075, 1074, F_SCALARLESEL, F_SCALARLEJOINSEL},
    /* varchar >= varchar */
    {1075, ">=", 1043, 1043, 16, F_TEXT_GE, false, false, 1073, 1072, F_SCALARGESEL, F_SCALARGEJOINSEL},

    /*
     * Date operators
     */
    /* date = date */
    {1093, "=", 1082, 1082, 16, F_DATE_EQ, true, true, 1093, 1094, F_EQSEL, F_EQJOINSEL},
    /* date <> date */
    {1094, "<>", 1082, 1082, 16, F_DATE_NE, false, false, 1094, 1093, F_NEQSEL, F_NEQJOINSEL},
    /* date < date */
    {1095, "<", 1082, 1082, 16, F_DATE_LT, false, false, 1097, 1098, F_SCALARLTSEL, F_SCALARLTJOINSEL},
    /* date > date */
    {1097, ">", 1082, 1082, 16, F_DATE_GT, false, false, 1095, 1096, F_SCALARGTSEL, F_SCALARGTJOINSEL},
    /* date <= date */
    {1096, "<=", 1082, 1082, 16, F_DATE_LE, false, false, 1098, 1097, F_SCALARLESEL, F_SCALARLEJOINSEL},
    /* date >= date */
    {1098, ">=", 1082, 1082, 16, F_DATE_GE, false, false, 1096, 1095, F_SCALARGESEL, F_SCALARGEJOINSEL},

    /*
     * Timestamp operators
     */
    /* timestamp = timestamp */
    {2060, "=", 1114, 1114, 16, F_TIMESTAMP_EQ, true, true, 2060, 2061, F_EQSEL, F_EQJOINSEL},
    /* timestamp <> timestamp */
    {2061, "<>", 1114, 1114, 16, F_TIMESTAMP_NE, false, false, 2061, 2060, F_NEQSEL, F_NEQJOINSEL},
    /* timestamp < timestamp */
    {2062, "<", 1114, 1114, 16, F_TIMESTAMP_LT, false, false, 2064, 2065, F_SCALARLTSEL, F_SCALARLTJOINSEL},
    /* timestamp > timestamp */
    {2064, ">", 1114, 1114, 16, F_TIMESTAMP_GT, false, false, 2062, 2063, F_SCALARGTSEL, F_SCALARGTJOINSEL},
    /* timestamp <= timestamp */
    {2063, "<=", 1114, 1114, 16, F_TIMESTAMP_LE, false, false, 2065, 2064, F_SCALARLESEL, F_SCALARLEJOINSEL},
    /* timestamp >= timestamp */
    {2065, ">=", 1114, 1114, 16, F_TIMESTAMP_GE, false, false, 2063, 2062, F_SCALARGESEL, F_SCALARGEJOINSEL},

    /*
     * Timestamptz operators
     */
    /* timestamptz = timestamptz */
    {1320, "=", 1184, 1184, 16, F_TIMESTAMPTZ_EQ, true, true, 1320, 1321, F_EQSEL, F_EQJOINSEL},
    /* timestamptz <> timestamptz */
    {1321, "<>", 1184, 1184, 16, F_TIMESTAMPTZ_NE, false, false, 1321, 1320, F_NEQSEL, F_NEQJOINSEL},
    /* timestamptz < timestamptz */
    {1322, "<", 1184, 1184, 16, F_TIMESTAMPTZ_LT, false, false, 1324, 1325, F_SCALARLTSEL, F_SCALARLTJOINSEL},
    /* timestamptz > timestamptz */
    {1324, ">", 1184, 1184, 16, F_TIMESTAMPTZ_GT, false, false, 1322, 1323, F_SCALARGTSEL, F_SCALARGTJOINSEL},
    /* timestamptz <= timestamptz */
    {1323, "<=", 1184, 1184, 16, F_TIMESTAMPTZ_LE, false, false, 1325, 1324, F_SCALARLESEL, F_SCALARLEJOINSEL},
    /* timestamptz >= timestamptz */
    {1325, ">=", 1184, 1184, 16, F_TIMESTAMPTZ_GE, false, false, 1323, 1322, F_SCALARGESEL, F_SCALARGEJOINSEL},

    /*
     * Numeric operators
     */
    /* numeric = numeric */
    {1752, "=", 1700, 1700, 16, F_NUMERIC_EQ, true, true, 1752, 1753, F_EQSEL, F_EQJOINSEL},
    /* numeric <> numeric */
    {1753, "<>", 1700, 1700, 16, F_NUMERIC_NE, false, false, 1753, 1752, F_NEQSEL, F_NEQJOINSEL},
    /* numeric < numeric */
    {1754, "<", 1700, 1700, 16, F_NUMERIC_LT, false, false, 1756, 1757, F_SCALARLTSEL, F_SCALARLTJOINSEL},
    /* numeric > numeric */
    {1756, ">", 1700, 1700, 16, F_NUMERIC_GT, false, false, 1754, 1755, F_SCALARGTSEL, F_SCALARGTJOINSEL},
    /* numeric <= numeric */
    {1755, "<=", 1700, 1700, 16, F_NUMERIC_LE, false, false, 1757, 1756, F_SCALARLESEL, F_SCALARLEJOINSEL},
    /* numeric >= numeric */
    {1757, ">=", 1700, 1700, 16, F_NUMERIC_GE, false, false, 1755, 1754, F_SCALARGESEL, F_SCALARGEJOINSEL},
    /* numeric + numeric */
    {1758, "+", 1700, 1700, 1700, F_NUMERIC_ADD, false, false, 1758, 0, 0, 0},
    /* numeric - numeric */
    {1759, "-", 1700, 1700, 1700, F_NUMERIC_SUB, false, false, 0, 0, 0, 0},
    /* numeric * numeric */
    {1760, "*", 1700, 1700, 1700, F_NUMERIC_MUL, false, false, 1760, 0, 0, 0},
    /* numeric / numeric */
    {1761, "/", 1700, 1700, 1700, F_NUMERIC_DIV, false, false, 0, 0, 0, 0},

    /*
     * OID operators
     */
    /* oid = oid */
    {607, "=", 26, 26, 16, F_OIDEQ, true, true, 607, 608, F_EQSEL, F_EQJOINSEL},
    /* oid <> oid */
    {608, "<>", 26, 26, 16, F_OIDNE, false, false, 608, 607, F_NEQSEL, F_NEQJOINSEL},
    /* oid < oid */
    {609, "<", 26, 26, 16, F_OIDLT, false, false, 610, 612, F_SCALARLTSEL, F_SCALARLTJOINSEL},
    /* oid > oid */
    {610, ">", 26, 26, 16, F_OIDGT, false, false, 609, 611, F_SCALARGTSEL, F_SCALARGTJOINSEL},
    /* oid <= oid */
    {611, "<=", 26, 26, 16, F_OIDLE, false, false, 612, 610, F_SCALARLESEL, F_SCALARLEJOINSEL},
    /* oid >= oid */
    {612, ">=", 26, 26, 16, F_OIDGE, false, false, 611, 609, F_SCALARGESEL, F_SCALARGEJOINSEL},

    /* Sentinel */
    {0, NULL, 0, 0, 0, 0, false, false, 0, 0, 0, 0}
};

/* Hash table for operator lookup */
static HTAB *OperatorOidHash = NULL;

typedef struct OperatorOidEntry
{
    Oid key;
    const PgOperatorInfo *info;
} OperatorOidEntry;

/* Forward declarations */
static void pgplanner_register_operator_syscache(const PgOperatorInfo *oprinfo);
static void pgplanner_init_btree_opfamilies(void);

/*
 * pgplanner_init_operators
 *    Initialize the built-in operator system.
 */
void
pgplanner_init_operators(void)
{
    HASHCTL hashctl;
    const PgOperatorInfo *oprinfo;
    MemoryContext oldcxt;

    oldcxt = MemoryContextSwitchTo(CacheMemoryContext);

    /*
     * Create hash table for OID -> OperatorInfo lookup
     */
    memset(&hashctl, 0, sizeof(hashctl));
    hashctl.keysize = sizeof(Oid);
    hashctl.entrysize = sizeof(OperatorOidEntry);
    hashctl.hcxt = CacheMemoryContext;

    OperatorOidHash = hash_create("PgPlanner Operator OID Hash",
                                  256,
                                  &hashctl,
                                  HASH_ELEM | HASH_BLOBS | HASH_CONTEXT);

    /*
     * Register all built-in operators
     */
    for (oprinfo = builtin_operators; oprinfo->oprname != NULL; oprinfo++)
    {
        OperatorOidEntry *entry;
        bool found;

        entry = hash_search(OperatorOidHash, &oprinfo->oprid, HASH_ENTER, &found);
        if (!found)
            entry->info = oprinfo;

        /* Also register with syscache */
        pgplanner_register_operator_syscache(oprinfo);
    }

    /*
     * Initialize B-tree operator families and classes
     * These are needed for index planning
     */
    pgplanner_init_btree_opfamilies();

    MemoryContextSwitchTo(oldcxt);
}

/*
 * pgplanner_register_operator_syscache
 *    Create a fake pg_operator syscache entry.
 */
static void
pgplanner_register_operator_syscache(const PgOperatorInfo *oprinfo)
{
    /* TODO: Insert into syscache when we have proper infrastructure */
    /* For now, we rely on the hash table lookup */
}

/*
 * pgplanner_init_btree_opfamilies
 *    Initialize B-tree operator families for index planning.
 */
static void
pgplanner_init_btree_opfamilies(void)
{
    /*
     * The planner needs operator families to understand which operators
     * can be used with which indexes. For B-tree indexes, we need to
     * register the standard comparison operators.
     *
     * Key operator families:
     * - integer_ops (OID 1976): for int2, int4, int8
     * - float_ops (OID 1970): for float4, float8
     * - text_ops (OID 1994): for text, varchar, bpchar
     * - date_ops (OID 434): for date
     * - timestamp_ops (OID 2039): for timestamp
     * - timestamptz_ops (OID 1314): for timestamptz
     * - numeric_ops (OID 1988): for numeric
     * - oid_ops (OID 1989): for oid
     * - bool_ops (OID 424): for bool
     */

    /* TODO: Register operator families and classes with syscache */
    /* This requires more infrastructure for pg_opfamily, pg_opclass,
     * pg_amop, and pg_amproc syscaches */
}

/*
 * pgplanner_get_operator_info
 *    Look up operator information by OID.
 */
const PgOperatorInfo *
pgplanner_get_operator_info(Oid oprid)
{
    OperatorOidEntry *entry;

    if (OperatorOidHash == NULL)
        return NULL;

    entry = hash_search(OperatorOidHash, &oprid, HASH_FIND, NULL);
    if (entry == NULL)
        return NULL;

    return entry->info;
}
