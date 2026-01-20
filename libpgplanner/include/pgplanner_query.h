/*-------------------------------------------------------------------------
 *
 * pgplanner_query.h
 *    FFI-safe logical query plan structures for the standalone PostgreSQL planner.
 *
 * These structures represent the LOGICAL query plan (Query struct) which
 * contains what operations need to be performed, but NOT how to perform them.
 * This is suitable for use by databases that want to implement their own
 * physical planning/execution.
 *
 * Key difference from physical plans:
 *   - Logical: "Join tables A and B on A.id = B.id"
 *   - Physical: "Use hash join with A as outer, build hash on B.id"
 *
 * Copyright (c) 2024, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */
#ifndef PGPLANNER_QUERY_H
#define PGPLANNER_QUERY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * NOTE: Command types and join types are shared with pgplanner_plan.h
 * Use PgCommandType and PgJoinType from that header.
 * We use typedefs here for logical plan-specific naming.
 */

/* Forward reference to avoid circular dependency */
#ifndef PGPLANNER_PLAN_H
/* Define these only if pgplanner_plan.h hasn't been included yet */
typedef enum PgCommandType
{
    PG_CMD_UNKNOWN = 0,
    PG_CMD_SELECT,
    PG_CMD_INSERT,
    PG_CMD_UPDATE,
    PG_CMD_DELETE,
    PG_CMD_MERGE,
    PG_CMD_UTILITY,
} PgCommandType;

typedef enum PgJoinType
{
    PG_JOIN_INNER = 0,
    PG_JOIN_LEFT,
    PG_JOIN_FULL,
    PG_JOIN_RIGHT,
    PG_JOIN_SEMI,
    PG_JOIN_ANTI,
} PgJoinType;
#endif

/* Use the same types for logical plans */
typedef PgCommandType PgCmdType;
typedef PgJoinType PgLogicalJoinType;

/*-------------------------------------------------------------------------
 * Set Operation Types
 * Prefixed with PGQ_ to avoid conflicts
 *-------------------------------------------------------------------------
 */
typedef enum PgSetOperation
{
    PGQ_SETOP_NONE = 0,
    PGQ_SETOP_UNION = 1,
    PGQ_SETOP_INTERSECT = 2,
    PGQ_SETOP_EXCEPT = 3,
} PgSetOperation;

/*-------------------------------------------------------------------------
 * Range Table Entry Types
 *-------------------------------------------------------------------------
 */
typedef enum PgRTEKind
{
    PG_RTE_RELATION = 0,    /* ordinary relation reference */
    PG_RTE_SUBQUERY = 1,    /* subquery in FROM */
    PG_RTE_JOIN = 2,        /* join */
    PG_RTE_FUNCTION = 3,    /* function in FROM */
    PG_RTE_TABLEFUNC = 4,   /* TableFunc (.e.g., XMLTABLE) */
    PG_RTE_VALUES = 5,      /* VALUES clause */
    PG_RTE_CTE = 6,         /* common table expression */
    PG_RTE_NAMEDTUPLESTORE = 7,
    PG_RTE_RESULT = 8,      /* result of a trivial query */
    PG_RTE_GROUP = 9,       /* grouping operation */
} PgRTEKind;

/*-------------------------------------------------------------------------
 * Expression Types (for logical queries)
 * Prefixed with PGQ_ to avoid conflicts with pgplanner_plan.h
 *-------------------------------------------------------------------------
 */
typedef enum PgLogicalExprType
{
    PGQ_EXPR_UNKNOWN = 0,

    /* Variables and constants */
    PGQ_EXPR_VAR = 1,            /* Column reference */
    PGQ_EXPR_CONST = 2,          /* Constant value */
    PGQ_EXPR_PARAM = 3,          /* Parameter placeholder ($1, $2, etc.) */

    /* Operators and functions */
    PGQ_EXPR_OP = 4,             /* Operator expression (a = b, a + b) */
    PGQ_EXPR_FUNC = 5,           /* Function call */
    PGQ_EXPR_AGGREF = 6,         /* Aggregate function (SUM, COUNT, etc.) */
    PGQ_EXPR_WINDOWFUNC = 7,     /* Window function */

    /* Boolean expressions */
    PGQ_EXPR_AND = 8,            /* AND expression */
    PGQ_EXPR_OR = 9,             /* OR expression */
    PGQ_EXPR_NOT = 10,           /* NOT expression */

    /* Null tests */
    PGQ_EXPR_ISNULL = 11,        /* IS NULL */
    PGQ_EXPR_ISNOTNULL = 12,     /* IS NOT NULL */

    /* Comparisons */
    PGQ_EXPR_DISTINCT = 13,      /* IS DISTINCT FROM */
    PGQ_EXPR_NULLIF = 14,        /* NULLIF(a, b) */
    PGQ_EXPR_SCALARARRAYOP = 15, /* ANY/ALL (array) */

    /* Conditional */
    PGQ_EXPR_CASE = 16,          /* CASE expression */
    PGQ_EXPR_COALESCE = 17,      /* COALESCE */

    /* Subqueries */
    PGQ_EXPR_SUBLINK = 18,       /* Subquery (EXISTS, IN, scalar, etc.) */

    /* Type coercion */
    PGQ_EXPR_CAST = 19,          /* Type cast */
    PGQ_EXPR_COERCE = 20,        /* Implicit coercion */

    /* Row and array constructors */
    PGQ_EXPR_ROW = 21,           /* ROW(...) */
    PGQ_EXPR_ARRAY = 22,         /* ARRAY[...] */

    /* Field access */
    PGQ_EXPR_FIELDSELECT = 23,   /* (composite).field */
} PgLogicalExprType;

/*-------------------------------------------------------------------------
 * Sublink Types (for subqueries in expressions)
 *-------------------------------------------------------------------------
 */
typedef enum PgSublinkType
{
    PG_SUBLINK_EXISTS = 0,      /* EXISTS(SELECT ...) */
    PG_SUBLINK_ALL = 1,         /* (expr) op ALL (SELECT ...) */
    PG_SUBLINK_ANY = 2,         /* (expr) op ANY (SELECT ...) / IN */
    PG_SUBLINK_ROWCOMPARE = 3,  /* (expr, expr) op (SELECT ...) */
    PG_SUBLINK_EXPR = 4,        /* (SELECT single value) */
    PG_SUBLINK_MULTIEXPR = 5,   /* (SELECT multiple values) */
    PG_SUBLINK_ARRAY = 6,       /* ARRAY(SELECT ...) */
} PgSublinkType;

/*-------------------------------------------------------------------------
 * Sort/Order Direction
 * Prefixed with PGQ_ to avoid conflicts
 *-------------------------------------------------------------------------
 */
typedef enum PgSortDir
{
    PGQ_SORT_DEFAULT = 0,
    PGQ_SORT_ASC = 1,
    PGQ_SORT_DESC = 2,
} PgSortDir;

typedef enum PgNullsOrder
{
    PGQ_NULLS_DEFAULT = 0,
    PGQ_NULLS_FIRST = 1,
    PGQ_NULLS_LAST = 2,
} PgNullsOrder;

/*-------------------------------------------------------------------------
 * Forward Declarations
 *-------------------------------------------------------------------------
 */
typedef struct PgLogicalExpr PgLogicalExpr;
typedef struct PgLogicalQuery PgLogicalQuery;
typedef struct PgRangeTableEntry PgRangeTableEntry;
typedef struct PgQueryTargetEntry PgLogicalTargetEntry;
typedef struct PgFromExpr PgFromExpr;
typedef struct PgJoinExpr PgJoinExpr;

/*-------------------------------------------------------------------------
 * Expression Structure
 *-------------------------------------------------------------------------
 */
struct PgLogicalExpr
{
    PgLogicalExprType type;
    uint32_t    result_type;    /* Result type OID */
    int32_t     type_mod;       /* Type modifier */
    uint32_t    collation;      /* Collation OID */

    union
    {
        /* PG_EXPR_VAR: Column reference */
        struct
        {
            uint32_t    varno;          /* Range table index (1-based) */
            int16_t     varattno;       /* Attribute number (1-based, 0=whole row) */
            int32_t     varlevelsup;    /* Subquery nesting level */
            char       *varname;        /* Column name (may be NULL) */
        } var;

        /* PG_EXPR_CONST: Constant value */
        struct
        {
            bool        is_null;
            char       *value_str;      /* String representation */
            int32_t     value_len;      /* For binary data */
        } constant;

        /* PG_EXPR_PARAM: Parameter */
        struct
        {
            int32_t     paramid;        /* Parameter number ($1, $2, etc.) */
        } param;

        /* PG_EXPR_OP: Operator expression */
        struct
        {
            uint32_t    opno;           /* Operator OID */
            char       *opname;         /* Operator name ("=", "<", "+", etc.) */
            PgLogicalExpr **args;       /* Argument expressions */
            int         num_args;
        } op;

        /* PG_EXPR_FUNC: Function call */
        struct
        {
            uint32_t    funcid;         /* Function OID */
            char       *funcname;       /* Function name */
            PgLogicalExpr **args;       /* Argument expressions */
            int         num_args;
            bool        is_set_returning;
        } func;

        /* PG_EXPR_AGGREF: Aggregate function */
        struct
        {
            uint32_t    aggfnoid;       /* Aggregate function OID */
            char       *aggname;        /* e.g., "sum", "count", "avg" */
            PgLogicalExpr **args;       /* Argument expressions */
            int         num_args;
            PgLogicalExpr *filter;      /* FILTER clause (may be NULL) */
            bool        is_distinct;    /* DISTINCT specified? */
            bool        is_star;        /* COUNT(*)? */
            PgLogicalExpr **order_by;   /* ORDER BY within aggregate */
            int         num_order_by;
        } aggref;

        /* PG_EXPR_WINDOWFUNC: Window function */
        struct
        {
            uint32_t    winfnoid;       /* Window function OID */
            char       *winname;        /* Function name */
            PgLogicalExpr **args;
            int         num_args;
            uint32_t    winref;         /* Reference to window clause */
        } windowfunc;

        /* PG_EXPR_AND, PG_EXPR_OR: Boolean expressions */
        struct
        {
            PgLogicalExpr **args;
            int         num_args;
        } boolexpr;

        /* PG_EXPR_NOT, PG_EXPR_ISNULL, PG_EXPR_ISNOTNULL */
        struct
        {
            PgLogicalExpr *arg;
        } unary;

        /* PG_EXPR_CASE: CASE expression */
        struct
        {
            PgLogicalExpr *test_expr;   /* Implicit test expr (may be NULL) */
            PgLogicalExpr **when_exprs; /* WHEN conditions */
            PgLogicalExpr **then_exprs; /* THEN results */
            int         num_whens;
            PgLogicalExpr *else_expr;   /* ELSE result (may be NULL) */
        } caseexpr;

        /* PG_EXPR_COALESCE */
        struct
        {
            PgLogicalExpr **args;
            int         num_args;
        } coalesce;

        /* PG_EXPR_SUBLINK: Subquery */
        struct
        {
            PgSublinkType sublink_type;
            PgLogicalExpr *test_expr;   /* Left-hand expression (may be NULL) */
            uint32_t    opno;           /* Comparison operator OID (for ANY/ALL) */
            char       *opname;         /* Operator name */
            PgLogicalQuery *subquery;   /* The subquery */
        } sublink;

        /* PG_EXPR_CAST, PG_EXPR_COERCE */
        struct
        {
            PgLogicalExpr *arg;
            uint32_t    target_type;    /* Target type OID */
            char       *type_name;      /* Target type name */
        } cast;

        /* PG_EXPR_ROW, PG_EXPR_ARRAY */
        struct
        {
            PgLogicalExpr **elements;
            int         num_elements;
        } composite;

        /* PG_EXPR_FIELDSELECT */
        struct
        {
            PgLogicalExpr *arg;
            int16_t     fieldnum;       /* Field number (1-based) */
            char       *fieldname;      /* Field name */
        } fieldselect;

        /* PG_EXPR_SCALARARRAYOP: ANY/ALL with array */
        struct
        {
            uint32_t    opno;
            char       *opname;
            bool        use_or;         /* true = ANY, false = ALL */
            PgLogicalExpr *scalar;      /* Left side scalar */
            PgLogicalExpr *array;       /* Right side array */
        } scalararrayop;

    } data;
};

/*-------------------------------------------------------------------------
 * Target Entry (SELECT list item)
 *-------------------------------------------------------------------------
 */
struct PgQueryTargetEntry
{
    PgLogicalExpr *expr;        /* The expression */
    int16_t     resno;          /* Output column number (1-based) */
    char       *resname;        /* Column name/alias (may be NULL) */
    uint32_t    sortgroupref;   /* Sort/group clause reference (0 if none) */
    uint32_t    orig_tbl;       /* Source table OID (0 if computed) */
    int16_t     orig_col;       /* Source column number (0 if computed) */
    bool        is_junk;        /* Junk column (not in final output)? */
};

/*-------------------------------------------------------------------------
 * Sort/Group Clause Item
 *-------------------------------------------------------------------------
 */
typedef struct PgSortGroupClause
{
    uint32_t    target_ref;     /* Reference to target list entry */
    uint32_t    eqop;           /* Equality operator OID */
    uint32_t    sortop;         /* Sort operator OID (0 if no sort) */
    PgSortDir   sort_dir;       /* ASC or DESC */
    PgNullsOrder nulls_order;   /* NULLS FIRST or LAST */
    bool        hashable;       /* Can use hashing? */
} PgSortGroupClause;

/*-------------------------------------------------------------------------
 * Window Clause
 *-------------------------------------------------------------------------
 */
typedef struct PgWindowClause
{
    char       *name;           /* Window name (may be NULL) */
    char       *refname;        /* Referenced window name (may be NULL) */

    /* PARTITION BY */
    PgSortGroupClause *partition_clause;
    int         num_partition;

    /* ORDER BY */
    PgSortGroupClause *order_clause;
    int         num_order;

    /* Frame specification */
    int         frame_options;  /* Frame options bitmask */
    PgLogicalExpr *start_offset;
    PgLogicalExpr *end_offset;

    uint32_t    winref;         /* ID for window function references */
} PgWindowClause;

/*-------------------------------------------------------------------------
 * Common Table Expression (CTE)
 *-------------------------------------------------------------------------
 */
typedef struct PgCTE
{
    char       *name;           /* CTE name */
    char      **column_names;   /* Column aliases (may be NULL) */
    int         num_columns;
    PgLogicalQuery *query;      /* The CTE's subquery */
    bool        is_recursive;   /* RECURSIVE? */
    bool        is_materialized;/* MATERIALIZED hint */
    int         ref_count;      /* Number of references to this CTE */
} PgCTE;

/*-------------------------------------------------------------------------
 * Join Expression (logical join in FROM clause)
 *-------------------------------------------------------------------------
 */
struct PgJoinExpr
{
    PgLogicalJoinType join_type;
    bool        is_natural;     /* NATURAL JOIN? */

    /* Left and right can be RTE indexes, JoinExprs, or FromExprs */
    int         left_rte;       /* Left RTE index if > 0 */
    PgJoinExpr *left_join;      /* Left join subtree if non-NULL */

    int         right_rte;      /* Right RTE index if > 0 */
    PgJoinExpr *right_join;     /* Right join subtree if non-NULL */

    /* Join condition */
    PgLogicalExpr *quals;       /* ON clause condition */
    char      **using_columns;  /* USING column names (may be NULL) */
    int         num_using;

    int         rtindex;        /* Range table index for this join */
};

/*-------------------------------------------------------------------------
 * FROM Expression (represents FROM ... WHERE ...)
 *-------------------------------------------------------------------------
 */
struct PgFromExpr
{
    /* List of items in FROM clause - can be RTE indexes or JoinExprs */
    int        *rte_indexes;    /* Array of RTE indexes */
    int         num_rtes;
    PgJoinExpr **joins;         /* Array of join expressions */
    int         num_joins;

    /* WHERE clause */
    PgLogicalExpr *quals;       /* WHERE conditions (may be NULL) */
};

/*-------------------------------------------------------------------------
 * Range Table Entry
 *-------------------------------------------------------------------------
 */
struct PgRangeTableEntry
{
    PgRTEKind   kind;
    char       *alias;          /* User-specified alias (may be NULL) */
    char       *eref_name;      /* Effective name for references */
    char      **eref_columns;   /* Effective column names */
    int         num_columns;

    /* For RTE_RELATION */
    uint32_t    relid;          /* Table OID */
    char       *relname;        /* Table name */
    char       *schemaname;     /* Schema name */
    bool        inh;            /* Include inheritance children? */

    /* For RTE_SUBQUERY */
    PgLogicalQuery *subquery;   /* The subquery */

    /* For RTE_JOIN */
    PgLogicalJoinType join_type;

    /* For RTE_FUNCTION */
    PgLogicalExpr **functions;  /* Function calls */
    int         num_functions;

    /* For RTE_VALUES */
    PgLogicalExpr ***values_lists; /* Array of row arrays */
    int         num_rows;
    int         num_cols;

    /* For RTE_CTE */
    char       *ctename;        /* CTE name */
    int         cte_index;      /* Index into cte_list */
};

/*-------------------------------------------------------------------------
 * Set Operation Statement (UNION/INTERSECT/EXCEPT)
 *-------------------------------------------------------------------------
 */
typedef struct PgSetOperationStmt
{
    PgSetOperation op;          /* UNION, INTERSECT, or EXCEPT */
    bool        all;            /* ALL specified (no duplicate removal)? */
    PgLogicalQuery *larg;       /* Left query */
    PgLogicalQuery *rarg;       /* Right query */
} PgSetOperationStmt;

/*-------------------------------------------------------------------------
 * Logical Query (main structure)
 *
 * This represents the analyzed and rewritten query, ready for planning.
 *-------------------------------------------------------------------------
 */
struct PgLogicalQuery
{
    PgCmdType   command_type;   /* SELECT, INSERT, UPDATE, DELETE, etc. */
    int64_t     query_id;       /* Query identifier */

    /* Query characteristics */
    bool        has_aggs;       /* Has aggregate functions? */
    bool        has_window_funcs;
    bool        has_sublinks;   /* Has subqueries? */
    bool        has_distinct_on;
    bool        has_recursive_cte;
    bool        has_modifying_cte;
    bool        has_for_update;

    /* CTEs (WITH clause) */
    PgCTE     **cte_list;
    int         num_ctes;

    /* Range table (all tables/subqueries referenced) */
    PgRangeTableEntry **rtable;
    int         num_rtable;

    /* FROM clause and WHERE clause */
    PgFromExpr *jointree;

    /* Target list (SELECT columns or INSERT/UPDATE values) */
    PgLogicalTargetEntry **target_list;
    int         num_targets;

    /* For INSERT/UPDATE/DELETE: target table RTE index */
    int         result_relation;

    /* GROUP BY clause */
    PgSortGroupClause *group_clause;
    int         num_group_cols;
    bool        group_distinct; /* GROUP BY DISTINCT? */

    /* HAVING clause */
    PgLogicalExpr *having_qual;

    /* Window clauses */
    PgWindowClause **window_clauses;
    int         num_windows;

    /* DISTINCT clause */
    PgSortGroupClause *distinct_clause;
    int         num_distinct_cols;

    /* ORDER BY clause */
    PgSortGroupClause *sort_clause;
    int         num_sort_cols;

    /* LIMIT / OFFSET */
    PgLogicalExpr *limit_offset;
    PgLogicalExpr *limit_count;
    bool        limit_with_ties;/* FETCH ... WITH TIES */

    /* Set operations (UNION/INTERSECT/EXCEPT) */
    PgSetOperationStmt *set_operations;

    /* RETURNING clause (for INSERT/UPDATE/DELETE) */
    PgLogicalTargetEntry **returning_list;
    int         num_returning;
};

/*-------------------------------------------------------------------------
 * API Functions
 *-------------------------------------------------------------------------
 */

/* Get string names for enum values */
const char *pgquery_cmd_type_name(PgCmdType type);
const char *pgquery_join_type_name(PgLogicalJoinType type);
const char *pgquery_rte_kind_name(PgRTEKind kind);
const char *pgquery_expr_type_name(PgLogicalExprType type);
const char *pgquery_setop_name(PgSetOperation op);

/* Memory cleanup */
void pgquery_free(PgLogicalQuery *query);
void pgquery_free_expr(PgLogicalExpr *expr);

/* Query traversal helpers */
typedef bool (*PgQueryExprWalker)(PgLogicalExpr *expr, void *context);
void pgquery_walk_exprs(PgLogicalQuery *query, PgQueryExprWalker walker, void *context);

/* Get table info from range table */
PgRangeTableEntry *pgquery_get_rte(PgLogicalQuery *query, int rtindex);

/* Print query structure (for debugging) */
char *pgquery_to_string(PgLogicalQuery *query);

#ifdef __cplusplus
}
#endif

#endif /* PGPLANNER_QUERY_H */
