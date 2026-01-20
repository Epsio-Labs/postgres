/*-------------------------------------------------------------------------
 *
 * pgplanner_functions.c
 *    Built-in function registration for the standalone PostgreSQL planner.
 *
 * This file registers function metadata needed for planning.
 * The planner needs to know about functions to understand:
 * - Return types of expressions
 * - Volatility (for optimization decisions)
 * - Strictness (for NULL handling)
 * - Aggregate functions for GROUP BY planning
 *
 * Copyright (c) 2024, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "catalog/pg_proc.h"
#include "catalog/pg_aggregate.h"
#include "utils/fmgroids.h"
#include "utils/hsearch.h"
#include "utils/memutils.h"

#include "pgplanner.h"
#include "pgplanner_internal.h"

/*
 * Function information for built-in functions.
 * We only need functions that the planner directly looks up.
 */
static const PgFunctionInfo builtin_functions[] = {
    /*
     * Selectivity estimator functions
     * These are critical for cost estimation
     */
    {F_EQSEL, "eqsel", 701, 4, {2281, 26, 2281, 23}, true, false, 's'},
    {F_NEQSEL, "neqsel", 701, 4, {2281, 26, 2281, 23}, true, false, 's'},
    {F_SCALARLTSEL, "scalarltsel", 701, 4, {2281, 26, 2281, 23}, true, false, 's'},
    {F_SCALARLESEL, "scalarlesel", 701, 4, {2281, 26, 2281, 23}, true, false, 's'},
    {F_SCALARGTSEL, "scalargtsel", 701, 4, {2281, 26, 2281, 23}, true, false, 's'},
    {F_SCALARGESEL, "scalargesel", 701, 4, {2281, 26, 2281, 23}, true, false, 's'},
    {F_LIKESEL, "likesel", 701, 4, {2281, 26, 2281, 23}, true, false, 's'},
    {F_NLIKESEL, "nlikesel", 701, 4, {2281, 26, 2281, 23}, true, false, 's'},

    /*
     * Join selectivity estimator functions
     */
    {F_EQJOINSEL, "eqjoinsel", 701, 5, {2281, 26, 2281, 21, 2281}, true, false, 's'},
    {F_NEQJOINSEL, "neqjoinsel", 701, 5, {2281, 26, 2281, 21, 2281}, true, false, 's'},
    {F_SCALARLTJOINSEL, "scalarltjoinsel", 701, 5, {2281, 26, 2281, 21, 2281}, true, false, 's'},
    {F_SCALARLEJOINSEL, "scalarlejoinsel", 701, 5, {2281, 26, 2281, 21, 2281}, true, false, 's'},
    {F_SCALARGTJOINSEL, "scalargtjoinsel", 701, 5, {2281, 26, 2281, 21, 2281}, true, false, 's'},
    {F_SCALARGEJOINSEL, "scalargejoinsel", 701, 5, {2281, 26, 2281, 21, 2281}, true, false, 's'},
    {F_LIKEJOINSEL, "likejoinsel", 701, 5, {2281, 26, 2281, 21, 2281}, true, false, 's'},
    {F_NLIKEJOINSEL, "nlikejoinsel", 701, 5, {2281, 26, 2281, 21, 2281}, true, false, 's'},

    /*
     * Comparison functions (for btree)
     */
    {F_BTINT4CMP, "btint4cmp", 23, 2, {23, 23}, true, false, 'i'},
    {F_BTINT8CMP, "btint8cmp", 23, 2, {20, 20}, true, false, 'i'},
    {F_BTINT2CMP, "btint2cmp", 23, 2, {21, 21}, true, false, 'i'},
    {F_BTFLOAT4CMP, "btfloat4cmp", 23, 2, {700, 700}, true, false, 'i'},
    {F_BTFLOAT8CMP, "btfloat8cmp", 23, 2, {701, 701}, true, false, 'i'},
    {F_BTTEXTCMP, "bttextcmp", 23, 2, {25, 25}, true, false, 'i'},
    {F_BTOIDCMP, "btoidcmp", 23, 2, {26, 26}, true, false, 'i'},
    {F_DATE_CMP, "date_cmp", 23, 2, {1082, 1082}, true, false, 'i'},
    {F_TIMESTAMP_CMP, "timestamp_cmp", 23, 2, {1114, 1114}, true, false, 'i'},
    {F_TIMESTAMPTZ_CMP, "timestamptz_cmp", 23, 2, {1184, 1184}, true, false, 'i'},

    /*
     * Hash functions (for hash join/aggregation)
     */
    {F_HASHINT4, "hashint4", 23, 1, {23}, true, false, 'i'},
    {F_HASHINT8, "hashint8", 23, 1, {20}, true, false, 'i'},
    {F_HASHINT2, "hashint2", 23, 1, {21}, true, false, 'i'},
    {F_HASHFLOAT4, "hashfloat4", 23, 1, {700}, true, false, 'i'},
    {F_HASHFLOAT8, "hashfloat8", 23, 1, {701}, true, false, 'i'},
    {F_HASHTEXT, "hashtext", 23, 1, {25}, true, false, 'i'},
    {F_HASHOID, "hashoid", 23, 1, {26}, true, false, 'i'},

    /*
     * Type input/output functions
     * These are needed for constant folding and expression evaluation
     */
    {F_INT4IN, "int4in", 23, 1, {2275}, true, false, 'i'},
    {F_INT4OUT, "int4out", 2275, 1, {23}, true, false, 'i'},
    {F_INT8IN, "int8in", 20, 1, {2275}, true, false, 'i'},
    {F_INT8OUT, "int8out", 2275, 1, {20}, true, false, 'i'},
    {F_FLOAT8IN, "float8in", 701, 1, {2275}, true, false, 'i'},
    {F_FLOAT8OUT, "float8out", 2275, 1, {701}, true, false, 'i'},
    {F_TEXTIN, "textin", 25, 1, {2275}, true, false, 'i'},
    {F_TEXTOUT, "textout", 2275, 1, {25}, true, false, 'i'},
    {F_BOOLIN, "boolin", 16, 1, {2275}, true, false, 'i'},
    {F_BOOLOUT, "boolout", 2275, 1, {16}, true, false, 'i'},

    /*
     * Arithmetic functions
     */
    {F_INT4PL, "int4pl", 23, 2, {23, 23}, true, false, 'i'},
    {F_INT4MI, "int4mi", 23, 2, {23, 23}, true, false, 'i'},
    {F_INT4MUL, "int4mul", 23, 2, {23, 23}, true, false, 'i'},
    {F_INT4DIV, "int4div", 23, 2, {23, 23}, true, false, 'i'},
    {F_INT8PL, "int8pl", 20, 2, {20, 20}, true, false, 'i'},
    {F_INT8MI, "int8mi", 20, 2, {20, 20}, true, false, 'i'},
    {F_INT8MUL, "int8mul", 20, 2, {20, 20}, true, false, 'i'},
    {F_INT8DIV, "int8div", 20, 2, {20, 20}, true, false, 'i'},
    {F_FLOAT8PL, "float8pl", 701, 2, {701, 701}, true, false, 'i'},
    {F_FLOAT8MI, "float8mi", 701, 2, {701, 701}, true, false, 'i'},
    {F_FLOAT8MUL, "float8mul", 701, 2, {701, 701}, true, false, 'i'},
    {F_FLOAT8DIV, "float8div", 701, 2, {701, 701}, true, false, 'i'},

    /*
     * Comparison functions
     */
    {F_INT4EQ, "int4eq", 16, 2, {23, 23}, true, false, 'i'},
    {F_INT4NE, "int4ne", 16, 2, {23, 23}, true, false, 'i'},
    {F_INT4LT, "int4lt", 16, 2, {23, 23}, true, false, 'i'},
    {F_INT4LE, "int4le", 16, 2, {23, 23}, true, false, 'i'},
    {F_INT4GT, "int4gt", 16, 2, {23, 23}, true, false, 'i'},
    {F_INT4GE, "int4ge", 16, 2, {23, 23}, true, false, 'i'},
    {F_INT8EQ, "int8eq", 16, 2, {20, 20}, true, false, 'i'},
    {F_INT8NE, "int8ne", 16, 2, {20, 20}, true, false, 'i'},
    {F_INT8LT, "int8lt", 16, 2, {20, 20}, true, false, 'i'},
    {F_INT8LE, "int8le", 16, 2, {20, 20}, true, false, 'i'},
    {F_INT8GT, "int8gt", 16, 2, {20, 20}, true, false, 'i'},
    {F_INT8GE, "int8ge", 16, 2, {20, 20}, true, false, 'i'},
    {F_TEXTEQ, "texteq", 16, 2, {25, 25}, true, false, 'i'},
    {F_TEXTNE, "textne", 16, 2, {25, 25}, true, false, 'i'},
    {F_TEXT_LT, "text_lt", 16, 2, {25, 25}, true, false, 'i'},
    {F_TEXT_LE, "text_le", 16, 2, {25, 25}, true, false, 'i'},
    {F_TEXT_GT, "text_gt", 16, 2, {25, 25}, true, false, 'i'},
    {F_TEXT_GE, "text_ge", 16, 2, {25, 25}, true, false, 'i'},

    /*
     * String functions
     */
    {F_TEXTCAT, "textcat", 25, 2, {25, 25}, true, false, 'i'},
    {F_TEXTLIKE, "textlike", 16, 2, {25, 25}, true, false, 'i'},
    {F_TEXTNLIKE, "textnlike", 16, 2, {25, 25}, true, false, 'i'},

    /* Sentinel */
    {0, NULL, 0, 0, {0}, false, false, 0}
};

/*
 * Aggregate function information
 */
typedef struct PgAggregateInfo
{
    Oid         aggfnoid;       /* Aggregate function OID */
    const char *aggname;
    Oid         aggtranstype;   /* Transition type */
    Oid         aggfinaltype;   /* Final result type */
    Oid         aggtransfn;     /* Transition function */
    Oid         aggfinalfn;     /* Final function (0 if none) */
    Oid         aggcombinefn;   /* Combine function (for parallel) */
    bool        aggfinalextra;
    char        aggfinalmodify;
    int         aggnumdirectargs;
} PgAggregateInfo;

static const PgAggregateInfo builtin_aggregates[] = {
    /* count(*) - special case, handled separately */
    {2803, "count", 20, 20, F_INT8INC_ANY, 0, F_INT8PL, false, 'r', 0},
    /* count(any) */
    {2147, "count", 20, 20, F_INT8INC, 0, F_INT8PL, false, 'r', 0},

    /* sum(int4) -> int8 */
    {2108, "sum", 20, 20, F_INT4_SUM, 0, F_INT8PL, false, 'r', 0},
    /* sum(int8) -> numeric */
    {2107, "sum", 1700, 1700, F_INT8_SUM, 0, 0, false, 'r', 0},
    /* sum(float8) -> float8 */
    {2111, "sum", 701, 701, F_FLOAT8PL, 0, F_FLOAT8PL, false, 'r', 0},
    /* sum(numeric) -> numeric */
    {2114, "sum", 1700, 1700, F_NUMERIC_ADD, 0, F_NUMERIC_ADD, false, 'r', 0},

    /* avg(int4) -> numeric */
    {2101, "avg", 1016, 1700, F_INT4_AVG_ACCUM, F_INT8_AVG, 0, false, 'r', 0},
    /* avg(int8) -> numeric */
    {2100, "avg", 1016, 1700, F_INT8_AVG_ACCUM, F_INT8_AVG, 0, false, 'r', 0},
    /* avg(float8) -> float8 */
    {2105, "avg", 1022, 701, F_FLOAT8_ACCUM, F_FLOAT8_AVG, 0, false, 'r', 0},
    /* avg(numeric) -> numeric */
    {2103, "avg", 1231, 1700, F_NUMERIC_AVG_ACCUM, F_NUMERIC_AVG, 0, false, 'r', 0},

    /* min(int4) */
    {2132, "min", 23, 23, F_INT4SMALLER, 0, F_INT4SMALLER, false, 'r', 0},
    /* min(int8) */
    {2131, "min", 20, 20, F_INT8SMALLER, 0, F_INT8SMALLER, false, 'r', 0},
    /* min(float8) */
    {2136, "min", 701, 701, F_FLOAT8SMALLER, 0, F_FLOAT8SMALLER, false, 'r', 0},
    /* min(text) */
    {2145, "min", 25, 25, F_TEXT_SMALLER, 0, F_TEXT_SMALLER, false, 'r', 0},
    /* min(date) */
    {2138, "min", 1082, 1082, F_DATE_SMALLER, 0, F_DATE_SMALLER, false, 'r', 0},
    /* min(timestamp) */
    {2142, "min", 1114, 1114, F_TIMESTAMP_SMALLER, 0, F_TIMESTAMP_SMALLER, false, 'r', 0},
    /* min(timestamptz) */
    {2143, "min", 1184, 1184, F_TIMESTAMPTZ_SMALLER, 0, F_TIMESTAMPTZ_SMALLER, false, 'r', 0},
    /* min(numeric) */
    {2146, "min", 1700, 1700, F_NUMERIC_SMALLER, 0, F_NUMERIC_SMALLER, false, 'r', 0},

    /* max(int4) */
    {2116, "max", 23, 23, F_INT4LARGER, 0, F_INT4LARGER, false, 'r', 0},
    /* max(int8) */
    {2115, "max", 20, 20, F_INT8LARGER, 0, F_INT8LARGER, false, 'r', 0},
    /* max(float8) */
    {2120, "max", 701, 701, F_FLOAT8LARGER, 0, F_FLOAT8LARGER, false, 'r', 0},
    /* max(text) */
    {2129, "max", 25, 25, F_TEXT_LARGER, 0, F_TEXT_LARGER, false, 'r', 0},
    /* max(date) */
    {2122, "max", 1082, 1082, F_DATE_LARGER, 0, F_DATE_LARGER, false, 'r', 0},
    /* max(timestamp) */
    {2126, "max", 1114, 1114, F_TIMESTAMP_LARGER, 0, F_TIMESTAMP_LARGER, false, 'r', 0},
    /* max(timestamptz) */
    {2127, "max", 1184, 1184, F_TIMESTAMPTZ_LARGER, 0, F_TIMESTAMPTZ_LARGER, false, 'r', 0},
    /* max(numeric) */
    {2130, "max", 1700, 1700, F_NUMERIC_LARGER, 0, F_NUMERIC_LARGER, false, 'r', 0},

    /* Sentinel */
    {0, NULL, 0, 0, 0, 0, 0, false, 0, 0}
};

/* Hash table for function lookup */
static HTAB *FunctionOidHash = NULL;

typedef struct FunctionOidEntry
{
    Oid key;
    const PgFunctionInfo *info;
} FunctionOidEntry;

/* Forward declarations */
static void pgplanner_register_function_syscache(const PgFunctionInfo *funcinfo);
static void pgplanner_register_aggregate_syscache(const PgAggregateInfo *agginfo);

/*
 * pgplanner_init_functions
 *    Initialize the built-in function system.
 */
void
pgplanner_init_functions(void)
{
    HASHCTL hashctl;
    const PgFunctionInfo *funcinfo;
    const PgAggregateInfo *agginfo;
    MemoryContext oldcxt;

    oldcxt = MemoryContextSwitchTo(CacheMemoryContext);

    /*
     * Create hash table for OID -> FunctionInfo lookup
     */
    memset(&hashctl, 0, sizeof(hashctl));
    hashctl.keysize = sizeof(Oid);
    hashctl.entrysize = sizeof(FunctionOidEntry);
    hashctl.hcxt = CacheMemoryContext;

    FunctionOidHash = hash_create("PgPlanner Function OID Hash",
                                  256,
                                  &hashctl,
                                  HASH_ELEM | HASH_BLOBS | HASH_CONTEXT);

    /*
     * Register all built-in functions
     */
    for (funcinfo = builtin_functions; funcinfo->funcname != NULL; funcinfo++)
    {
        FunctionOidEntry *entry;
        bool found;

        entry = hash_search(FunctionOidHash, &funcinfo->funcid, HASH_ENTER, &found);
        if (!found)
            entry->info = funcinfo;

        /* Also register with syscache */
        pgplanner_register_function_syscache(funcinfo);
    }

    /*
     * Register aggregate functions
     */
    for (agginfo = builtin_aggregates; agginfo->aggname != NULL; agginfo++)
    {
        pgplanner_register_aggregate_syscache(agginfo);
    }

    MemoryContextSwitchTo(oldcxt);
}

/*
 * pgplanner_register_function_syscache
 *    Create a fake pg_proc syscache entry.
 */
static void
pgplanner_register_function_syscache(const PgFunctionInfo *funcinfo)
{
    /* TODO: Insert into syscache when we have proper infrastructure */
    (void) funcinfo;
}

/*
 * pgplanner_register_aggregate_syscache
 *    Create fake pg_proc and pg_aggregate syscache entries for an aggregate.
 */
static void
pgplanner_register_aggregate_syscache(const PgAggregateInfo *agginfo)
{
    /* TODO: Insert into syscache when we have proper infrastructure */
    (void) agginfo;
}

/*
 * pgplanner_get_function_info
 *    Look up function information by OID.
 */
const PgFunctionInfo *
pgplanner_get_function_info(Oid funcid)
{
    FunctionOidEntry *entry;

    if (FunctionOidHash == NULL)
        return NULL;

    entry = hash_search(FunctionOidHash, &funcid, HASH_FIND, NULL);
    if (entry == NULL)
        return NULL;

    return entry->info;
}
