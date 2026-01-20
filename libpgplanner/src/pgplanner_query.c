/*-------------------------------------------------------------------------
 *
 * pgplanner_query.c
 *    Convert PostgreSQL Query to FFI-safe logical query structures.
 *
 * This file implements the conversion from PostgreSQL's internal Query
 * struct to our FFI-safe representation for use by external databases.
 *
 * Copyright (c) 2024, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/stratnum.h"
#include "catalog/pg_type.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "nodes/parsenodes.h"
#include "nodes/primnodes.h"
#include "parser/parsetree.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"

#include "pgplanner.h"
#include "pgplanner_query.h"
#include "pgplanner_internal.h"

/*-------------------------------------------------------------------------
 * Forward declarations
 *-------------------------------------------------------------------------
 */
static PgLogicalExpr *convert_expr(Node *node);
static PgLogicalTargetEntry *convert_target_entry(TargetEntry *te);
static PgRangeTableEntry *convert_rte(RangeTblEntry *rte);
static PgFromExpr *convert_from_expr(FromExpr *from, List *rtable);
static PgJoinExpr *convert_join_expr(JoinExpr *join);
static PgSortGroupClause *convert_sort_group_clause(List *clause, int *count);
static PgWindowClause *convert_window_clause(WindowClause *wc);
static PgCTE *convert_cte(CommonTableExpr *cte);
static PgSetOperationStmt *convert_set_operations(Node *setop);
static char *get_operator_name_safe(Oid opno);
static char *get_function_name_safe(Oid funcid);
static char *get_type_name_safe(Oid typid);

/*-------------------------------------------------------------------------
 * Name lookup tables
 *-------------------------------------------------------------------------
 */
static const char *cmd_type_names[] = {
    "UNKNOWN", "SELECT", "INSERT", "UPDATE", "DELETE", "MERGE", "UTILITY"
};

static const char *join_type_names[] = {
    "INNER", "LEFT", "FULL", "RIGHT", "SEMI", "ANTI"
};

static const char *rte_kind_names[] = {
    "RELATION", "SUBQUERY", "JOIN", "FUNCTION", "TABLEFUNC",
    "VALUES", "CTE", "NAMEDTUPLESTORE", "RESULT", "GROUP"
};

static const char *expr_type_names[] = {
    "UNKNOWN", "VAR", "CONST", "PARAM", "OP", "FUNC", "AGGREF",
    "WINDOWFUNC", "AND", "OR", "NOT", "ISNULL", "ISNOTNULL",
    "DISTINCT", "NULLIF", "SCALARARRAYOP", "CASE", "COALESCE",
    "SUBLINK", "CAST", "COERCE", "ROW", "ARRAY", "FIELDSELECT"
};

static const char *setop_names[] = {
    "NONE", "UNION", "INTERSECT", "EXCEPT"
};

/*-------------------------------------------------------------------------
 * Public API: Name lookup functions
 *-------------------------------------------------------------------------
 */
const char *
pgquery_cmd_type_name(PgCmdType type)
{
    if (type < 0 || type > PG_CMD_UTILITY)
        return "UNKNOWN";
    return cmd_type_names[type];
}

const char *
pgquery_join_type_name(PgLogicalJoinType type)
{
    if (type < 0 || type > PG_JOIN_ANTI)
        return "UNKNOWN";
    return join_type_names[type];
}

const char *
pgquery_rte_kind_name(PgRTEKind kind)
{
    if (kind < 0 || kind > PG_RTE_GROUP)
        return "UNKNOWN";
    return rte_kind_names[kind];
}

const char *
pgquery_expr_type_name(PgLogicalExprType type)
{
    if (type < 0 || type > PGQ_EXPR_FIELDSELECT)
        return "UNKNOWN";
    return expr_type_names[type];
}

const char *
pgquery_setop_name(PgSetOperation op)
{
    if (op < 0 || op > PGQ_SETOP_EXCEPT)
        return "UNKNOWN";
    return setop_names[op];
}

/*-------------------------------------------------------------------------
 * Memory allocation helpers
 *-------------------------------------------------------------------------
 */
static void *
query_alloc(size_t size)
{
    return pgplanner_alloc_zero(size);
}

static char *
query_strdup(const char *str)
{
    if (str == NULL)
        return NULL;
    return pgplanner_strdup(str);
}

static void *
query_alloc_array(size_t count, size_t elemsize)
{
    if (count == 0)
        return NULL;
    return pgplanner_alloc_zero(count * elemsize);
}

/*-------------------------------------------------------------------------
 * Main conversion function: Query -> PgLogicalQuery
 *-------------------------------------------------------------------------
 */
PgLogicalQuery *
pgplanner_convert_query(Query *query)
{
    PgLogicalQuery *result;
    ListCell *lc;
    int i;

    if (query == NULL)
        return NULL;

    result = query_alloc(sizeof(PgLogicalQuery));

    /* Command type */
    switch (query->commandType)
    {
        case CMD_SELECT:
            result->command_type = PG_CMD_SELECT;
            break;
        case CMD_INSERT:
            result->command_type = PG_CMD_INSERT;
            break;
        case CMD_UPDATE:
            result->command_type = PG_CMD_UPDATE;
            break;
        case CMD_DELETE:
            result->command_type = PG_CMD_DELETE;
            break;
        case CMD_MERGE:
            result->command_type = PG_CMD_MERGE;
            break;
        case CMD_UTILITY:
            result->command_type = PG_CMD_UTILITY;
            break;
        default:
            result->command_type = PG_CMD_UNKNOWN;
            break;
    }

    result->query_id = query->queryId;

    /* Query characteristics */
    result->has_aggs = query->hasAggs;
    result->has_window_funcs = query->hasWindowFuncs;
    result->has_sublinks = query->hasSubLinks;
    result->has_distinct_on = query->hasDistinctOn;
    result->has_recursive_cte = query->hasRecursive;
    result->has_modifying_cte = query->hasModifyingCTE;
    result->has_for_update = query->hasForUpdate;

    /* CTEs */
    result->num_ctes = list_length(query->cteList);
    if (result->num_ctes > 0)
    {
        result->cte_list = query_alloc_array(result->num_ctes, sizeof(PgCTE *));
        i = 0;
        foreach(lc, query->cteList)
        {
            result->cte_list[i] = convert_cte((CommonTableExpr *) lfirst(lc));
            i++;
        }
    }

    /* Range table */
    result->num_rtable = list_length(query->rtable);
    if (result->num_rtable > 0)
    {
        result->rtable = query_alloc_array(result->num_rtable,
                                           sizeof(PgRangeTableEntry *));
        i = 0;
        foreach(lc, query->rtable)
        {
            result->rtable[i] = convert_rte((RangeTblEntry *) lfirst(lc));
            i++;
        }
    }

    /* FROM/WHERE (jointree) */
    if (query->jointree)
        result->jointree = convert_from_expr(query->jointree, query->rtable);

    /* Target list */
    result->num_targets = list_length(query->targetList);
    if (result->num_targets > 0)
    {
        result->target_list = query_alloc_array(result->num_targets,
                                                sizeof(PgLogicalTargetEntry *));
        i = 0;
        foreach(lc, query->targetList)
        {
            result->target_list[i] = convert_target_entry((TargetEntry *) lfirst(lc));
            i++;
        }
    }

    /* Result relation */
    result->result_relation = query->resultRelation;

    /* GROUP BY */
    result->group_clause = convert_sort_group_clause(query->groupClause,
                                                     &result->num_group_cols);
    result->group_distinct = query->groupDistinct;

    /* HAVING */
    result->having_qual = convert_expr(query->havingQual);

    /* Window clauses */
    result->num_windows = list_length(query->windowClause);
    if (result->num_windows > 0)
    {
        result->window_clauses = query_alloc_array(result->num_windows,
                                                   sizeof(PgWindowClause *));
        i = 0;
        foreach(lc, query->windowClause)
        {
            result->window_clauses[i] = convert_window_clause((WindowClause *) lfirst(lc));
            i++;
        }
    }

    /* DISTINCT */
    result->distinct_clause = convert_sort_group_clause(query->distinctClause,
                                                        &result->num_distinct_cols);

    /* ORDER BY */
    result->sort_clause = convert_sort_group_clause(query->sortClause,
                                                    &result->num_sort_cols);

    /* LIMIT/OFFSET */
    result->limit_offset = convert_expr(query->limitOffset);
    result->limit_count = convert_expr(query->limitCount);
    result->limit_with_ties = (query->limitOption == LIMIT_OPTION_WITH_TIES);

    /* Set operations */
    if (query->setOperations)
        result->set_operations = convert_set_operations(query->setOperations);

    /* RETURNING */
    result->num_returning = list_length(query->returningList);
    if (result->num_returning > 0)
    {
        result->returning_list = query_alloc_array(result->num_returning,
                                                   sizeof(PgLogicalTargetEntry *));
        i = 0;
        foreach(lc, query->returningList)
        {
            result->returning_list[i] = convert_target_entry((TargetEntry *) lfirst(lc));
            i++;
        }
    }

    return result;
}

/*-------------------------------------------------------------------------
 * Convert RangeTblEntry
 *-------------------------------------------------------------------------
 */
static PgRangeTableEntry *
convert_rte(RangeTblEntry *rte)
{
    PgRangeTableEntry *result;
    ListCell *lc;
    int i;

    if (rte == NULL)
        return NULL;

    result = query_alloc(sizeof(PgRangeTableEntry));

    /* Kind */
    switch (rte->rtekind)
    {
        case RTE_RELATION:
            result->kind = PG_RTE_RELATION;
            break;
        case RTE_SUBQUERY:
            result->kind = PG_RTE_SUBQUERY;
            break;
        case RTE_JOIN:
            result->kind = PG_RTE_JOIN;
            break;
        case RTE_FUNCTION:
            result->kind = PG_RTE_FUNCTION;
            break;
        case RTE_TABLEFUNC:
            result->kind = PG_RTE_TABLEFUNC;
            break;
        case RTE_VALUES:
            result->kind = PG_RTE_VALUES;
            break;
        case RTE_CTE:
            result->kind = PG_RTE_CTE;
            break;
        case RTE_NAMEDTUPLESTORE:
            result->kind = PG_RTE_NAMEDTUPLESTORE;
            break;
        case RTE_RESULT:
            result->kind = PG_RTE_RESULT;
            break;
        /* RTE_GROUP doesn't exist in this PostgreSQL version */
        default:
            result->kind = PG_RTE_RELATION;
            break;
    }

    /* Alias */
    if (rte->alias)
        result->alias = query_strdup(rte->alias->aliasname);

    /* Effective reference name and columns */
    if (rte->eref)
    {
        result->eref_name = query_strdup(rte->eref->aliasname);
        result->num_columns = list_length(rte->eref->colnames);
        if (result->num_columns > 0)
        {
            result->eref_columns = query_alloc_array(result->num_columns, sizeof(char *));
            i = 0;
            foreach(lc, rte->eref->colnames)
            {
                result->eref_columns[i] = query_strdup(strVal(lfirst(lc)));
                i++;
            }
        }
    }

    /* Kind-specific data */
    switch (rte->rtekind)
    {
        case RTE_RELATION:
            result->relid = rte->relid;
            result->relname = query_strdup(get_rel_name(rte->relid));
            /* Schema lookup would require additional catalog access */
            result->inh = rte->inh;
            break;

        case RTE_SUBQUERY:
            if (rte->subquery)
                result->subquery = pgplanner_convert_query(rte->subquery);
            break;

        case RTE_JOIN:
            switch (rte->jointype)
            {
                case JOIN_INNER:
                    result->join_type = PG_JOIN_INNER;
                    break;
                case JOIN_LEFT:
                    result->join_type = PG_JOIN_LEFT;
                    break;
                case JOIN_FULL:
                    result->join_type = PG_JOIN_FULL;
                    break;
                case JOIN_RIGHT:
                    result->join_type = PG_JOIN_RIGHT;
                    break;
                case JOIN_SEMI:
                    result->join_type = PG_JOIN_SEMI;
                    break;
                case JOIN_ANTI:
                    result->join_type = PG_JOIN_ANTI;
                    break;
                default:
                    result->join_type = PG_JOIN_INNER;
                    break;
            }
            break;

        case RTE_CTE:
            result->ctename = query_strdup(rte->ctename);
            break;

        default:
            break;
    }

    return result;
}

/*-------------------------------------------------------------------------
 * Convert FromExpr (FROM ... WHERE ...)
 *-------------------------------------------------------------------------
 */
static PgFromExpr *
convert_from_expr(FromExpr *from, List *rtable)
{
    PgFromExpr *result;
    ListCell *lc;
    int num_rtes = 0;
    int num_joins = 0;
    int ri = 0, ji = 0;

    if (from == NULL)
        return NULL;

    result = query_alloc(sizeof(PgFromExpr));

    /* Count RTEs vs JoinExprs in fromlist */
    foreach(lc, from->fromlist)
    {
        Node *node = lfirst(lc);
        if (IsA(node, RangeTblRef))
            num_rtes++;
        else if (IsA(node, JoinExpr))
            num_joins++;
    }

    /* Allocate arrays */
    if (num_rtes > 0)
        result->rte_indexes = query_alloc_array(num_rtes, sizeof(int));
    result->num_rtes = num_rtes;

    if (num_joins > 0)
        result->joins = query_alloc_array(num_joins, sizeof(PgJoinExpr *));
    result->num_joins = num_joins;

    /* Convert items */
    foreach(lc, from->fromlist)
    {
        Node *node = lfirst(lc);
        if (IsA(node, RangeTblRef))
        {
            RangeTblRef *rtr = (RangeTblRef *) node;
            result->rte_indexes[ri++] = rtr->rtindex;
        }
        else if (IsA(node, JoinExpr))
        {
            result->joins[ji++] = convert_join_expr((JoinExpr *) node);
        }
    }

    /* WHERE clause */
    result->quals = convert_expr(from->quals);

    return result;
}

/*-------------------------------------------------------------------------
 * Convert JoinExpr
 *-------------------------------------------------------------------------
 */
static PgJoinExpr *
convert_join_expr(JoinExpr *join)
{
    PgJoinExpr *result;
    ListCell *lc;
    int i;

    if (join == NULL)
        return NULL;

    result = query_alloc(sizeof(PgJoinExpr));

    /* Join type */
    switch (join->jointype)
    {
        case JOIN_INNER:
            result->join_type = PG_JOIN_INNER;
            break;
        case JOIN_LEFT:
            result->join_type = PG_JOIN_LEFT;
            break;
        case JOIN_FULL:
            result->join_type = PG_JOIN_FULL;
            break;
        case JOIN_RIGHT:
            result->join_type = PG_JOIN_RIGHT;
            break;
        case JOIN_SEMI:
            result->join_type = PG_JOIN_SEMI;
            break;
        case JOIN_ANTI:
            result->join_type = PG_JOIN_ANTI;
            break;
        default:
            result->join_type = PG_JOIN_INNER;
            break;
    }

    result->is_natural = join->isNatural;

    /* Left side */
    if (IsA(join->larg, RangeTblRef))
    {
        RangeTblRef *rtr = (RangeTblRef *) join->larg;
        result->left_rte = rtr->rtindex;
    }
    else if (IsA(join->larg, JoinExpr))
    {
        result->left_join = convert_join_expr((JoinExpr *) join->larg);
    }

    /* Right side */
    if (IsA(join->rarg, RangeTblRef))
    {
        RangeTblRef *rtr = (RangeTblRef *) join->rarg;
        result->right_rte = rtr->rtindex;
    }
    else if (IsA(join->rarg, JoinExpr))
    {
        result->right_join = convert_join_expr((JoinExpr *) join->rarg);
    }

    /* ON clause */
    result->quals = convert_expr(join->quals);

    /* USING clause */
    result->num_using = list_length(join->usingClause);
    if (result->num_using > 0)
    {
        result->using_columns = query_alloc_array(result->num_using, sizeof(char *));
        i = 0;
        foreach(lc, join->usingClause)
        {
            result->using_columns[i] = query_strdup(strVal(lfirst(lc)));
            i++;
        }
    }

    result->rtindex = join->rtindex;

    return result;
}

/*-------------------------------------------------------------------------
 * Convert TargetEntry
 *-------------------------------------------------------------------------
 */
static PgLogicalTargetEntry *
convert_target_entry(TargetEntry *te)
{
    PgLogicalTargetEntry *result;

    if (te == NULL)
        return NULL;

    result = query_alloc(sizeof(PgLogicalTargetEntry));

    result->expr = convert_expr((Node *) te->expr);
    result->resno = te->resno;
    result->resname = query_strdup(te->resname);
    result->sortgroupref = te->ressortgroupref;
    result->orig_tbl = te->resorigtbl;
    result->orig_col = te->resorigcol;
    result->is_junk = te->resjunk;

    return result;
}

/*-------------------------------------------------------------------------
 * Convert expression node
 *-------------------------------------------------------------------------
 */
static PgLogicalExpr *
convert_expr(Node *node)
{
    PgLogicalExpr *result;
    ListCell *lc;
    int i;

    if (node == NULL)
        return NULL;

    result = query_alloc(sizeof(PgLogicalExpr));

    switch (nodeTag(node))
    {
        case T_Var:
            {
                Var *var = (Var *) node;
                result->type = PGQ_EXPR_VAR;
                result->result_type = var->vartype;
                result->type_mod = var->vartypmod;
                result->collation = var->varcollid;

                result->data.var.varno = var->varno;
                result->data.var.varattno = var->varattno;
                result->data.var.varlevelsup = var->varlevelsup;
            }
            break;

        case T_Const:
            {
                Const *con = (Const *) node;
                result->type = PGQ_EXPR_CONST;
                result->result_type = con->consttype;
                result->type_mod = con->consttypmod;
                result->collation = con->constcollid;

                result->data.constant.is_null = con->constisnull;
                if (!con->constisnull)
                {
                    Oid typoutput;
                    bool typIsVarlena;
                    getTypeOutputInfo(con->consttype, &typoutput, &typIsVarlena);
                    result->data.constant.value_str =
                        query_strdup(OidOutputFunctionCall(typoutput, con->constvalue));
                }
            }
            break;

        case T_Param:
            {
                Param *param = (Param *) node;
                result->type = PGQ_EXPR_PARAM;
                result->result_type = param->paramtype;
                result->type_mod = param->paramtypmod;
                result->collation = param->paramcollid;

                result->data.param.paramid = param->paramid;
            }
            break;

        case T_OpExpr:
            {
                OpExpr *op = (OpExpr *) node;
                result->type = PGQ_EXPR_OP;
                result->result_type = op->opresulttype;
                result->collation = op->opcollid;

                result->data.op.opno = op->opno;
                result->data.op.opname = get_operator_name_safe(op->opno);

                result->data.op.num_args = list_length(op->args);
                if (result->data.op.num_args > 0)
                {
                    result->data.op.args = query_alloc_array(result->data.op.num_args,
                                                             sizeof(PgLogicalExpr *));
                    i = 0;
                    foreach(lc, op->args)
                    {
                        result->data.op.args[i] = convert_expr((Node *) lfirst(lc));
                        i++;
                    }
                }
            }
            break;

        case T_FuncExpr:
            {
                FuncExpr *func = (FuncExpr *) node;
                result->type = PGQ_EXPR_FUNC;
                result->result_type = func->funcresulttype;
                result->collation = func->funccollid;

                result->data.func.funcid = func->funcid;
                result->data.func.funcname = get_function_name_safe(func->funcid);
                result->data.func.is_set_returning = func->funcretset;

                result->data.func.num_args = list_length(func->args);
                if (result->data.func.num_args > 0)
                {
                    result->data.func.args = query_alloc_array(result->data.func.num_args,
                                                               sizeof(PgLogicalExpr *));
                    i = 0;
                    foreach(lc, func->args)
                    {
                        result->data.func.args[i] = convert_expr((Node *) lfirst(lc));
                        i++;
                    }
                }
            }
            break;

        case T_Aggref:
            {
                Aggref *agg = (Aggref *) node;
                result->type = PGQ_EXPR_AGGREF;
                result->result_type = agg->aggtype;
                result->collation = agg->aggcollid;

                result->data.aggref.aggfnoid = agg->aggfnoid;
                result->data.aggref.aggname = get_function_name_safe(agg->aggfnoid);
                result->data.aggref.is_distinct = (agg->aggdistinct != NIL);
                result->data.aggref.is_star = agg->aggstar;

                result->data.aggref.num_args = list_length(agg->args);
                if (result->data.aggref.num_args > 0)
                {
                    result->data.aggref.args = query_alloc_array(result->data.aggref.num_args,
                                                                 sizeof(PgLogicalExpr *));
                    i = 0;
                    foreach(lc, agg->args)
                    {
                        TargetEntry *te = (TargetEntry *) lfirst(lc);
                        result->data.aggref.args[i] = convert_expr((Node *) te->expr);
                        i++;
                    }
                }

                result->data.aggref.filter = convert_expr((Node *) agg->aggfilter);

                /* ORDER BY within aggregate */
                result->data.aggref.num_order_by = list_length(agg->aggorder);
                if (result->data.aggref.num_order_by > 0)
                {
                    result->data.aggref.order_by =
                        query_alloc_array(result->data.aggref.num_order_by,
                                         sizeof(PgLogicalExpr *));
                    i = 0;
                    foreach(lc, agg->aggorder)
                    {
                        SortGroupClause *sgc = (SortGroupClause *) lfirst(lc);
                        /* Store sort info - simplified for now */
                        result->data.aggref.order_by[i] = NULL;
                        i++;
                    }
                }
            }
            break;

        case T_WindowFunc:
            {
                WindowFunc *wf = (WindowFunc *) node;
                result->type = PGQ_EXPR_WINDOWFUNC;
                result->result_type = wf->wintype;
                result->collation = wf->wincollid;

                result->data.windowfunc.winfnoid = wf->winfnoid;
                result->data.windowfunc.winname = get_function_name_safe(wf->winfnoid);
                result->data.windowfunc.winref = wf->winref;

                result->data.windowfunc.num_args = list_length(wf->args);
                if (result->data.windowfunc.num_args > 0)
                {
                    result->data.windowfunc.args =
                        query_alloc_array(result->data.windowfunc.num_args,
                                         sizeof(PgLogicalExpr *));
                    i = 0;
                    foreach(lc, wf->args)
                    {
                        result->data.windowfunc.args[i] = convert_expr((Node *) lfirst(lc));
                        i++;
                    }
                }
            }
            break;

        case T_BoolExpr:
            {
                BoolExpr *boolexpr = (BoolExpr *) node;

                switch (boolexpr->boolop)
                {
                    case AND_EXPR:
                        result->type = PGQ_EXPR_AND;
                        break;
                    case OR_EXPR:
                        result->type = PGQ_EXPR_OR;
                        break;
                    case NOT_EXPR:
                        result->type = PGQ_EXPR_NOT;
                        break;
                }

                result->result_type = BOOLOID;

                if (boolexpr->boolop == NOT_EXPR)
                {
                    result->data.unary.arg = convert_expr((Node *) linitial(boolexpr->args));
                }
                else
                {
                    result->data.boolexpr.num_args = list_length(boolexpr->args);
                    result->data.boolexpr.args =
                        query_alloc_array(result->data.boolexpr.num_args,
                                         sizeof(PgLogicalExpr *));
                    i = 0;
                    foreach(lc, boolexpr->args)
                    {
                        result->data.boolexpr.args[i] = convert_expr((Node *) lfirst(lc));
                        i++;
                    }
                }
            }
            break;

        case T_NullTest:
            {
                NullTest *nt = (NullTest *) node;
                result->type = (nt->nulltesttype == IS_NULL) ? PGQ_EXPR_ISNULL : PGQ_EXPR_ISNOTNULL;
                result->result_type = BOOLOID;
                result->data.unary.arg = convert_expr((Node *) nt->arg);
            }
            break;

        case T_CaseExpr:
            {
                CaseExpr *ce = (CaseExpr *) node;
                result->type = PGQ_EXPR_CASE;
                result->result_type = ce->casetype;
                result->collation = ce->casecollid;

                result->data.caseexpr.test_expr = convert_expr((Node *) ce->arg);
                result->data.caseexpr.else_expr = convert_expr((Node *) ce->defresult);

                result->data.caseexpr.num_whens = list_length(ce->args);
                if (result->data.caseexpr.num_whens > 0)
                {
                    result->data.caseexpr.when_exprs =
                        query_alloc_array(result->data.caseexpr.num_whens,
                                         sizeof(PgLogicalExpr *));
                    result->data.caseexpr.then_exprs =
                        query_alloc_array(result->data.caseexpr.num_whens,
                                         sizeof(PgLogicalExpr *));
                    i = 0;
                    foreach(lc, ce->args)
                    {
                        CaseWhen *cw = (CaseWhen *) lfirst(lc);
                        result->data.caseexpr.when_exprs[i] = convert_expr((Node *) cw->expr);
                        result->data.caseexpr.then_exprs[i] = convert_expr((Node *) cw->result);
                        i++;
                    }
                }
            }
            break;

        case T_CoalesceExpr:
            {
                CoalesceExpr *ce = (CoalesceExpr *) node;
                result->type = PGQ_EXPR_COALESCE;
                result->result_type = ce->coalescetype;
                result->collation = ce->coalescecollid;

                result->data.coalesce.num_args = list_length(ce->args);
                if (result->data.coalesce.num_args > 0)
                {
                    result->data.coalesce.args =
                        query_alloc_array(result->data.coalesce.num_args,
                                         sizeof(PgLogicalExpr *));
                    i = 0;
                    foreach(lc, ce->args)
                    {
                        result->data.coalesce.args[i] = convert_expr((Node *) lfirst(lc));
                        i++;
                    }
                }
            }
            break;

        case T_SubLink:
            {
                SubLink *sl = (SubLink *) node;
                result->type = PGQ_EXPR_SUBLINK;
                result->result_type = BOOLOID; /* Usually boolean for EXISTS/IN */

                switch (sl->subLinkType)
                {
                    case EXISTS_SUBLINK:
                        result->data.sublink.sublink_type = PG_SUBLINK_EXISTS;
                        break;
                    case ALL_SUBLINK:
                        result->data.sublink.sublink_type = PG_SUBLINK_ALL;
                        break;
                    case ANY_SUBLINK:
                        result->data.sublink.sublink_type = PG_SUBLINK_ANY;
                        break;
                    case ROWCOMPARE_SUBLINK:
                        result->data.sublink.sublink_type = PG_SUBLINK_ROWCOMPARE;
                        break;
                    case EXPR_SUBLINK:
                        result->data.sublink.sublink_type = PG_SUBLINK_EXPR;
                        break;
                    case MULTIEXPR_SUBLINK:
                        result->data.sublink.sublink_type = PG_SUBLINK_MULTIEXPR;
                        break;
                    case ARRAY_SUBLINK:
                        result->data.sublink.sublink_type = PG_SUBLINK_ARRAY;
                        break;
                    default:
                        result->data.sublink.sublink_type = PG_SUBLINK_EXPR;
                        break;
                }

                result->data.sublink.test_expr = convert_expr(sl->testexpr);

                /* Get operator info for ANY/ALL */
                if (sl->operName != NIL)
                {
                    result->data.sublink.opname = query_strdup(strVal(linitial(sl->operName)));
                }

                /* Convert subquery */
                if (sl->subselect && IsA(sl->subselect, Query))
                {
                    result->data.sublink.subquery =
                        pgplanner_convert_query((Query *) sl->subselect);
                }
            }
            break;

        case T_ScalarArrayOpExpr:
            {
                ScalarArrayOpExpr *sa = (ScalarArrayOpExpr *) node;
                result->type = PGQ_EXPR_SCALARARRAYOP;
                result->result_type = BOOLOID;

                result->data.scalararrayop.opno = sa->opno;
                result->data.scalararrayop.opname = get_operator_name_safe(sa->opno);
                result->data.scalararrayop.use_or = sa->useOr;

                if (list_length(sa->args) >= 2)
                {
                    result->data.scalararrayop.scalar =
                        convert_expr((Node *) linitial(sa->args));
                    result->data.scalararrayop.array =
                        convert_expr((Node *) lsecond(sa->args));
                }
            }
            break;

        case T_RelabelType:
            {
                RelabelType *rt = (RelabelType *) node;
                result->type = PGQ_EXPR_COERCE;
                result->result_type = rt->resulttype;
                result->type_mod = rt->resulttypmod;
                result->collation = rt->resultcollid;

                result->data.cast.arg = convert_expr((Node *) rt->arg);
                result->data.cast.target_type = rt->resulttype;
                result->data.cast.type_name = get_type_name_safe(rt->resulttype);
            }
            break;

        case T_CoerceViaIO:
            {
                CoerceViaIO *cio = (CoerceViaIO *) node;
                result->type = PGQ_EXPR_CAST;
                result->result_type = cio->resulttype;
                result->collation = cio->resultcollid;

                result->data.cast.arg = convert_expr((Node *) cio->arg);
                result->data.cast.target_type = cio->resulttype;
                result->data.cast.type_name = get_type_name_safe(cio->resulttype);
            }
            break;

        case T_ArrayExpr:
            {
                ArrayExpr *ae = (ArrayExpr *) node;
                result->type = PGQ_EXPR_ARRAY;
                result->result_type = ae->array_typeid;
                result->collation = ae->array_collid;

                result->data.composite.num_elements = list_length(ae->elements);
                if (result->data.composite.num_elements > 0)
                {
                    result->data.composite.elements =
                        query_alloc_array(result->data.composite.num_elements,
                                         sizeof(PgLogicalExpr *));
                    i = 0;
                    foreach(lc, ae->elements)
                    {
                        result->data.composite.elements[i] =
                            convert_expr((Node *) lfirst(lc));
                        i++;
                    }
                }
            }
            break;

        case T_RowExpr:
            {
                RowExpr *re = (RowExpr *) node;
                result->type = PGQ_EXPR_ROW;
                result->result_type = re->row_typeid;

                result->data.composite.num_elements = list_length(re->args);
                if (result->data.composite.num_elements > 0)
                {
                    result->data.composite.elements =
                        query_alloc_array(result->data.composite.num_elements,
                                         sizeof(PgLogicalExpr *));
                    i = 0;
                    foreach(lc, re->args)
                    {
                        result->data.composite.elements[i] =
                            convert_expr((Node *) lfirst(lc));
                        i++;
                    }
                }
            }
            break;

        case T_FieldSelect:
            {
                FieldSelect *fs = (FieldSelect *) node;
                result->type = PGQ_EXPR_FIELDSELECT;
                result->result_type = fs->resulttype;
                result->type_mod = fs->resulttypmod;
                result->collation = fs->resultcollid;

                result->data.fieldselect.arg = convert_expr((Node *) fs->arg);
                result->data.fieldselect.fieldnum = fs->fieldnum;
            }
            break;

        default:
            result->type = PGQ_EXPR_UNKNOWN;
            break;
    }

    return result;
}

/*-------------------------------------------------------------------------
 * Convert SortGroupClause list
 *-------------------------------------------------------------------------
 */
static PgSortGroupClause *
convert_sort_group_clause(List *clause, int *count)
{
    PgSortGroupClause *result;
    ListCell *lc;
    int i;

    *count = list_length(clause);
    if (*count == 0)
        return NULL;

    result = query_alloc_array(*count, sizeof(PgSortGroupClause));

    i = 0;
    foreach(lc, clause)
    {
        SortGroupClause *sgc = (SortGroupClause *) lfirst(lc);

        result[i].target_ref = sgc->tleSortGroupRef;
        result[i].eqop = sgc->eqop;
        result[i].sortop = sgc->sortop;
        /*
         * Determine sort direction from the sortop. In PostgreSQL, the sort
         * operator determines the direction: BTLessStrategyNumber (1) = ASC,
         * BTGreaterStrategyNumber (3) = DESC. We use get_ordering_op_properties
         * to look this up.
         */
        {
            Oid opfamily;
            Oid opcintype;
            int16 strategy;

            if (sgc->sortop != InvalidOid &&
                get_ordering_op_properties(sgc->sortop, &opfamily, &opcintype, &strategy))
            {
                result[i].sort_dir = (strategy == BTGreaterStrategyNumber) ?
                    PGQ_SORT_DESC : PGQ_SORT_ASC;
            }
            else
            {
                result[i].sort_dir = PGQ_SORT_ASC;  /* Default to ASC */
            }
        }
        result[i].nulls_order = sgc->nulls_first ? PGQ_NULLS_FIRST : PGQ_NULLS_LAST;
        result[i].hashable = sgc->hashable;
        i++;
    }

    return result;
}

/*-------------------------------------------------------------------------
 * Convert WindowClause
 *-------------------------------------------------------------------------
 */
static PgWindowClause *
convert_window_clause(WindowClause *wc)
{
    PgWindowClause *result;

    if (wc == NULL)
        return NULL;

    result = query_alloc(sizeof(PgWindowClause));

    result->name = query_strdup(wc->name);
    result->refname = query_strdup(wc->refname);

    result->partition_clause = convert_sort_group_clause(wc->partitionClause,
                                                         &result->num_partition);
    result->order_clause = convert_sort_group_clause(wc->orderClause,
                                                     &result->num_order);

    result->frame_options = wc->frameOptions;
    result->start_offset = convert_expr(wc->startOffset);
    result->end_offset = convert_expr(wc->endOffset);
    result->winref = wc->winref;

    return result;
}

/*-------------------------------------------------------------------------
 * Convert CommonTableExpr (CTE)
 *-------------------------------------------------------------------------
 */
static PgCTE *
convert_cte(CommonTableExpr *cte)
{
    PgCTE *result;
    ListCell *lc;
    int i;

    if (cte == NULL)
        return NULL;

    result = query_alloc(sizeof(PgCTE));

    result->name = query_strdup(cte->ctename);
    result->is_recursive = cte->cterecursive;
    result->is_materialized = (cte->ctematerialized == CTEMaterializeAlways);
    result->ref_count = cte->cterefcount;

    /* Column names */
    result->num_columns = list_length(cte->aliascolnames);
    if (result->num_columns > 0)
    {
        result->column_names = query_alloc_array(result->num_columns, sizeof(char *));
        i = 0;
        foreach(lc, cte->aliascolnames)
        {
            result->column_names[i] = query_strdup(strVal(lfirst(lc)));
            i++;
        }
    }

    /* CTE query */
    if (cte->ctequery && IsA(cte->ctequery, Query))
        result->query = pgplanner_convert_query((Query *) cte->ctequery);

    return result;
}

/*-------------------------------------------------------------------------
 * Convert SetOperationStmt
 *-------------------------------------------------------------------------
 */
static PgSetOperationStmt *
convert_set_operations(Node *setop)
{
    PgSetOperationStmt *result;
    SetOperationStmt *sos;

    if (setop == NULL || !IsA(setop, SetOperationStmt))
        return NULL;

    sos = (SetOperationStmt *) setop;
    result = query_alloc(sizeof(PgSetOperationStmt));

    switch (sos->op)
    {
        case SETOP_UNION:
            result->op = PGQ_SETOP_UNION;
            break;
        case SETOP_INTERSECT:
            result->op = PGQ_SETOP_INTERSECT;
            break;
        case SETOP_EXCEPT:
            result->op = PGQ_SETOP_EXCEPT;
            break;
        default:
            result->op = PGQ_SETOP_NONE;
            break;
    }

    result->all = sos->all;

    /* Recursively convert left and right */
    if (sos->larg)
    {
        if (IsA(sos->larg, SetOperationStmt))
        {
            /* Nested set operation - need to wrap in a pseudo-query */
            result->larg = query_alloc(sizeof(PgLogicalQuery));
            result->larg->set_operations = convert_set_operations(sos->larg);
        }
        else if (IsA(sos->larg, RangeTblRef))
        {
            /* Reference to a subquery in the range table */
            result->larg = query_alloc(sizeof(PgLogicalQuery));
            result->larg->command_type = PG_CMD_SELECT;
        }
    }

    if (sos->rarg)
    {
        if (IsA(sos->rarg, SetOperationStmt))
        {
            result->rarg = query_alloc(sizeof(PgLogicalQuery));
            result->rarg->set_operations = convert_set_operations(sos->rarg);
        }
        else if (IsA(sos->rarg, RangeTblRef))
        {
            result->rarg = query_alloc(sizeof(PgLogicalQuery));
            result->rarg->command_type = PG_CMD_SELECT;
        }
    }

    return result;
}

/*-------------------------------------------------------------------------
 * Helper functions for name lookup
 *-------------------------------------------------------------------------
 */
static char *
get_operator_name_safe(Oid opno)
{
    const PgOperatorInfo *opinfo = pgplanner_get_operator_info(opno);
    if (opinfo != NULL)
        return query_strdup(opinfo->oprname);

    char buf[32];
    snprintf(buf, sizeof(buf), "op_%u", opno);
    return query_strdup(buf);
}

static char *
get_function_name_safe(Oid funcid)
{
    const PgFunctionInfo *funcinfo = pgplanner_get_function_info(funcid);
    if (funcinfo != NULL)
        return query_strdup(funcinfo->funcname);

    char buf[32];
    snprintf(buf, sizeof(buf), "func_%u", funcid);
    return query_strdup(buf);
}

static char *
get_type_name_safe(Oid typid)
{
    const PgTypeInfo *typeinfo = pgplanner_get_type_info(typid);
    if (typeinfo != NULL)
        return query_strdup(typeinfo->typname);

    char buf[32];
    snprintf(buf, sizeof(buf), "type_%u", typid);
    return query_strdup(buf);
}

/*-------------------------------------------------------------------------
 * Memory cleanup: free expression
 *-------------------------------------------------------------------------
 */
void
pgquery_free_expr(PgLogicalExpr *expr)
{
    int i;

    if (expr == NULL)
        return;

    switch (expr->type)
    {
        case PGQ_EXPR_VAR:
            pgplanner_free(expr->data.var.varname);
            break;

        case PGQ_EXPR_CONST:
            pgplanner_free(expr->data.constant.value_str);
            break;

        case PGQ_EXPR_OP:
            pgplanner_free(expr->data.op.opname);
            for (i = 0; i < expr->data.op.num_args; i++)
                pgquery_free_expr(expr->data.op.args[i]);
            pgplanner_free(expr->data.op.args);
            break;

        case PGQ_EXPR_FUNC:
            pgplanner_free(expr->data.func.funcname);
            for (i = 0; i < expr->data.func.num_args; i++)
                pgquery_free_expr(expr->data.func.args[i]);
            pgplanner_free(expr->data.func.args);
            break;

        case PGQ_EXPR_AGGREF:
            pgplanner_free(expr->data.aggref.aggname);
            for (i = 0; i < expr->data.aggref.num_args; i++)
                pgquery_free_expr(expr->data.aggref.args[i]);
            pgplanner_free(expr->data.aggref.args);
            pgquery_free_expr(expr->data.aggref.filter);
            pgplanner_free(expr->data.aggref.order_by);
            break;

        case PGQ_EXPR_WINDOWFUNC:
            pgplanner_free(expr->data.windowfunc.winname);
            for (i = 0; i < expr->data.windowfunc.num_args; i++)
                pgquery_free_expr(expr->data.windowfunc.args[i]);
            pgplanner_free(expr->data.windowfunc.args);
            break;

        case PGQ_EXPR_AND:
        case PGQ_EXPR_OR:
            for (i = 0; i < expr->data.boolexpr.num_args; i++)
                pgquery_free_expr(expr->data.boolexpr.args[i]);
            pgplanner_free(expr->data.boolexpr.args);
            break;

        case PGQ_EXPR_NOT:
        case PGQ_EXPR_ISNULL:
        case PGQ_EXPR_ISNOTNULL:
            pgquery_free_expr(expr->data.unary.arg);
            break;

        case PGQ_EXPR_CASE:
            pgquery_free_expr(expr->data.caseexpr.test_expr);
            pgquery_free_expr(expr->data.caseexpr.else_expr);
            for (i = 0; i < expr->data.caseexpr.num_whens; i++)
            {
                pgquery_free_expr(expr->data.caseexpr.when_exprs[i]);
                pgquery_free_expr(expr->data.caseexpr.then_exprs[i]);
            }
            pgplanner_free(expr->data.caseexpr.when_exprs);
            pgplanner_free(expr->data.caseexpr.then_exprs);
            break;

        case PGQ_EXPR_COALESCE:
            for (i = 0; i < expr->data.coalesce.num_args; i++)
                pgquery_free_expr(expr->data.coalesce.args[i]);
            pgplanner_free(expr->data.coalesce.args);
            break;

        case PGQ_EXPR_SUBLINK:
            pgquery_free_expr(expr->data.sublink.test_expr);
            pgplanner_free(expr->data.sublink.opname);
            pgquery_free(expr->data.sublink.subquery);
            break;

        case PGQ_EXPR_CAST:
        case PGQ_EXPR_COERCE:
            pgquery_free_expr(expr->data.cast.arg);
            pgplanner_free(expr->data.cast.type_name);
            break;

        case PGQ_EXPR_ROW:
        case PGQ_EXPR_ARRAY:
            for (i = 0; i < expr->data.composite.num_elements; i++)
                pgquery_free_expr(expr->data.composite.elements[i]);
            pgplanner_free(expr->data.composite.elements);
            break;

        case PGQ_EXPR_FIELDSELECT:
            pgquery_free_expr(expr->data.fieldselect.arg);
            pgplanner_free(expr->data.fieldselect.fieldname);
            break;

        case PGQ_EXPR_SCALARARRAYOP:
            pgplanner_free(expr->data.scalararrayop.opname);
            pgquery_free_expr(expr->data.scalararrayop.scalar);
            pgquery_free_expr(expr->data.scalararrayop.array);
            break;

        default:
            break;
    }

    pgplanner_free(expr);
}

/*-------------------------------------------------------------------------
 * Memory cleanup: free query
 *-------------------------------------------------------------------------
 */
void
pgquery_free(PgLogicalQuery *query)
{
    int i;

    if (query == NULL)
        return;

    /* CTEs */
    for (i = 0; i < query->num_ctes; i++)
    {
        if (query->cte_list[i])
        {
            pgplanner_free(query->cte_list[i]->name);
            for (int j = 0; j < query->cte_list[i]->num_columns; j++)
                pgplanner_free(query->cte_list[i]->column_names[j]);
            pgplanner_free(query->cte_list[i]->column_names);
            pgquery_free(query->cte_list[i]->query);
            pgplanner_free(query->cte_list[i]);
        }
    }
    pgplanner_free(query->cte_list);

    /* Range table */
    for (i = 0; i < query->num_rtable; i++)
    {
        if (query->rtable[i])
        {
            pgplanner_free(query->rtable[i]->alias);
            pgplanner_free(query->rtable[i]->eref_name);
            for (int j = 0; j < query->rtable[i]->num_columns; j++)
                pgplanner_free(query->rtable[i]->eref_columns[j]);
            pgplanner_free(query->rtable[i]->eref_columns);
            pgplanner_free(query->rtable[i]->relname);
            pgplanner_free(query->rtable[i]->schemaname);
            pgquery_free(query->rtable[i]->subquery);
            pgplanner_free(query->rtable[i]->ctename);
            pgplanner_free(query->rtable[i]);
        }
    }
    pgplanner_free(query->rtable);

    /* Jointree */
    if (query->jointree)
    {
        pgplanner_free(query->jointree->rte_indexes);
        for (i = 0; i < query->jointree->num_joins; i++)
        {
            /* Free join expressions recursively - simplified */
            pgplanner_free(query->jointree->joins[i]);
        }
        pgplanner_free(query->jointree->joins);
        pgquery_free_expr(query->jointree->quals);
        pgplanner_free(query->jointree);
    }

    /* Target list */
    for (i = 0; i < query->num_targets; i++)
    {
        if (query->target_list[i])
        {
            pgquery_free_expr(query->target_list[i]->expr);
            pgplanner_free(query->target_list[i]->resname);
            pgplanner_free(query->target_list[i]);
        }
    }
    pgplanner_free(query->target_list);

    /* Group clause */
    pgplanner_free(query->group_clause);

    /* Having */
    pgquery_free_expr(query->having_qual);

    /* Window clauses */
    for (i = 0; i < query->num_windows; i++)
    {
        if (query->window_clauses[i])
        {
            pgplanner_free(query->window_clauses[i]->name);
            pgplanner_free(query->window_clauses[i]->refname);
            pgplanner_free(query->window_clauses[i]->partition_clause);
            pgplanner_free(query->window_clauses[i]->order_clause);
            pgquery_free_expr(query->window_clauses[i]->start_offset);
            pgquery_free_expr(query->window_clauses[i]->end_offset);
            pgplanner_free(query->window_clauses[i]);
        }
    }
    pgplanner_free(query->window_clauses);

    /* Distinct/Sort clauses */
    pgplanner_free(query->distinct_clause);
    pgplanner_free(query->sort_clause);

    /* Limit */
    pgquery_free_expr(query->limit_offset);
    pgquery_free_expr(query->limit_count);

    /* Set operations */
    if (query->set_operations)
    {
        pgquery_free(query->set_operations->larg);
        pgquery_free(query->set_operations->rarg);
        pgplanner_free(query->set_operations);
    }

    /* Returning list */
    for (i = 0; i < query->num_returning; i++)
    {
        if (query->returning_list[i])
        {
            pgquery_free_expr(query->returning_list[i]->expr);
            pgplanner_free(query->returning_list[i]->resname);
            pgplanner_free(query->returning_list[i]);
        }
    }
    pgplanner_free(query->returning_list);

    pgplanner_free(query);
}

/*-------------------------------------------------------------------------
 * Get RTE by index
 *-------------------------------------------------------------------------
 */
PgRangeTableEntry *
pgquery_get_rte(PgLogicalQuery *query, int rtindex)
{
    if (query == NULL || rtindex < 1 || rtindex > query->num_rtable)
        return NULL;
    return query->rtable[rtindex - 1];
}

/*-------------------------------------------------------------------------
 * Walk all expressions in query
 *-------------------------------------------------------------------------
 */
static void walk_expr_tree(PgLogicalExpr *expr, PgQueryExprWalker walker, void *context);

static void
walk_expr_tree(PgLogicalExpr *expr, PgQueryExprWalker walker, void *context)
{
    int i;

    if (expr == NULL)
        return;

    if (!walker(expr, context))
        return;

    /* Recursively walk children based on expression type */
    switch (expr->type)
    {
        case PGQ_EXPR_OP:
            for (i = 0; i < expr->data.op.num_args; i++)
                walk_expr_tree(expr->data.op.args[i], walker, context);
            break;

        case PGQ_EXPR_FUNC:
            for (i = 0; i < expr->data.func.num_args; i++)
                walk_expr_tree(expr->data.func.args[i], walker, context);
            break;

        case PGQ_EXPR_AGGREF:
            for (i = 0; i < expr->data.aggref.num_args; i++)
                walk_expr_tree(expr->data.aggref.args[i], walker, context);
            walk_expr_tree(expr->data.aggref.filter, walker, context);
            break;

        case PGQ_EXPR_WINDOWFUNC:
            for (i = 0; i < expr->data.windowfunc.num_args; i++)
                walk_expr_tree(expr->data.windowfunc.args[i], walker, context);
            break;

        case PGQ_EXPR_AND:
        case PGQ_EXPR_OR:
            for (i = 0; i < expr->data.boolexpr.num_args; i++)
                walk_expr_tree(expr->data.boolexpr.args[i], walker, context);
            break;

        case PGQ_EXPR_NOT:
        case PGQ_EXPR_ISNULL:
        case PGQ_EXPR_ISNOTNULL:
            walk_expr_tree(expr->data.unary.arg, walker, context);
            break;

        case PGQ_EXPR_CASE:
            walk_expr_tree(expr->data.caseexpr.test_expr, walker, context);
            for (i = 0; i < expr->data.caseexpr.num_whens; i++)
            {
                walk_expr_tree(expr->data.caseexpr.when_exprs[i], walker, context);
                walk_expr_tree(expr->data.caseexpr.then_exprs[i], walker, context);
            }
            walk_expr_tree(expr->data.caseexpr.else_expr, walker, context);
            break;

        case PGQ_EXPR_COALESCE:
            for (i = 0; i < expr->data.coalesce.num_args; i++)
                walk_expr_tree(expr->data.coalesce.args[i], walker, context);
            break;

        case PGQ_EXPR_SUBLINK:
            walk_expr_tree(expr->data.sublink.test_expr, walker, context);
            break;

        case PGQ_EXPR_CAST:
        case PGQ_EXPR_COERCE:
            walk_expr_tree(expr->data.cast.arg, walker, context);
            break;

        case PGQ_EXPR_ROW:
        case PGQ_EXPR_ARRAY:
            for (i = 0; i < expr->data.composite.num_elements; i++)
                walk_expr_tree(expr->data.composite.elements[i], walker, context);
            break;

        case PGQ_EXPR_FIELDSELECT:
            walk_expr_tree(expr->data.fieldselect.arg, walker, context);
            break;

        case PGQ_EXPR_SCALARARRAYOP:
            walk_expr_tree(expr->data.scalararrayop.scalar, walker, context);
            walk_expr_tree(expr->data.scalararrayop.array, walker, context);
            break;

        default:
            break;
    }
}

void
pgquery_walk_exprs(PgLogicalQuery *query, PgQueryExprWalker walker, void *context)
{
    int i;

    if (query == NULL)
        return;

    /* Walk target list */
    for (i = 0; i < query->num_targets; i++)
    {
        if (query->target_list[i])
            walk_expr_tree(query->target_list[i]->expr, walker, context);
    }

    /* Walk WHERE clause */
    if (query->jointree)
        walk_expr_tree(query->jointree->quals, walker, context);

    /* Walk HAVING */
    walk_expr_tree(query->having_qual, walker, context);

    /* Walk LIMIT/OFFSET */
    walk_expr_tree(query->limit_offset, walker, context);
    walk_expr_tree(query->limit_count, walker, context);
}

/*-------------------------------------------------------------------------
 * Convert query to string (for debugging)
 *-------------------------------------------------------------------------
 */
char *
pgquery_to_string(PgLogicalQuery *query)
{
    /* Simple implementation - could be expanded */
    char buf[256];
    if (query == NULL)
        return query_strdup("(null)");

    snprintf(buf, sizeof(buf),
             "Query(type=%s, tables=%d, targets=%d, has_aggs=%d)",
             pgquery_cmd_type_name(query->command_type),
             query->num_rtable,
             query->num_targets,
             query->has_aggs);

    return query_strdup(buf);
}
