/*-------------------------------------------------------------------------
 *
 * test_api.c
 *    Minimal API tests for libpgplanner (compilation verification only).
 *
 * This test file verifies that the public API header compiles correctly
 * and the basic types are properly defined. It does NOT require linking
 * with PostgreSQL backend code.
 *
 * Copyright (c) 2024, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Include the public headers */
#include "pgplanner_query.h"
#include "pgplanner.h"

static int tests_run = 0;
static int tests_passed = 0;

/* Test callback function for callback type tests */
static const PgTableDef *api_test_callback(const char *schema, const char *table, void *data)
{
    (void)schema; (void)table; (void)data;
    return NULL;
}

#define TEST(name, cond) do { \
    printf("  Testing %s... ", name); \
    tests_run++; \
    if (cond) { \
        printf("PASSED\n"); \
        tests_passed++; \
    } else { \
        printf("FAILED\n"); \
    } \
} while(0)

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("Running API compilation tests...\n\n");

    /*
     * Type size tests - verify structs are properly defined
     */
    printf("Type tests:\n");
    TEST("PgColumnDef size", sizeof(PgColumnDef) >= sizeof(void*));
    TEST("PgIndexDef size", sizeof(PgIndexDef) >= sizeof(void*));
    TEST("PgTableDef size", sizeof(PgTableDef) >= sizeof(void*));

    /*
     * Error code tests
     */
    printf("\nError code tests:\n");
    TEST("PGPLANNER_OK is 0", PGPLANNER_OK == 0);
    TEST("PGPLANNER_ERROR_NOT_INITIALIZED is 1", PGPLANNER_ERROR_NOT_INITIALIZED == 1);
    TEST("PGPLANNER_ERROR_ALREADY_INITIALIZED is 2", PGPLANNER_ERROR_ALREADY_INITIALIZED == 2);
    TEST("PGPLANNER_ERROR_PARSE_ERROR is 3", PGPLANNER_ERROR_PARSE_ERROR == 3);
    TEST("PGPLANNER_ERROR_ANALYZE_ERROR is 4", PGPLANNER_ERROR_ANALYZE_ERROR == 4);
    TEST("PGPLANNER_ERROR_PLAN_ERROR is 5", PGPLANNER_ERROR_PLAN_ERROR == 5);
    TEST("PGPLANNER_ERROR_INVALID_TABLE is 6", PGPLANNER_ERROR_INVALID_TABLE == 6);
    TEST("PGPLANNER_ERROR_INVALID_TYPE is 7", PGPLANNER_ERROR_INVALID_TYPE == 7);
    TEST("PGPLANNER_ERROR_OUT_OF_MEMORY is 8", PGPLANNER_ERROR_OUT_OF_MEMORY == 8);
    TEST("PGPLANNER_ERROR_INTERNAL is 99", PGPLANNER_ERROR_INTERNAL == 99);

    /*
     * InvalidPgOid test
     */
    printf("\nOID tests:\n");
    TEST("InvalidPgOid is 0", InvalidPgOid == 0);

    /*
     * PgColumnDef field tests
     */
    printf("\nPgColumnDef field tests:\n");
    {
        PgColumnDef col = {
            .name = "test_col",
            .type_name = "int4",
            .typmod = -1,
            .not_null = true
        };
        TEST("PgColumnDef name assignment", strcmp(col.name, "test_col") == 0);
        TEST("PgColumnDef type_name assignment", strcmp(col.type_name, "int4") == 0);
        TEST("PgColumnDef typmod assignment", col.typmod == -1);
        TEST("PgColumnDef not_null assignment", col.not_null == true);
    }

    /*
     * PgIndexDef field tests
     */
    printf("\nPgIndexDef field tests:\n");
    {
        int cols[] = {1, 2};
        PgIndexDef idx = {
            .name = "test_idx",
            .num_columns = 2,
            .column_nums = cols,
            .access_method = "btree",
            .is_unique = true,
            .is_primary = false
        };
        TEST("PgIndexDef name assignment", strcmp(idx.name, "test_idx") == 0);
        TEST("PgIndexDef num_columns assignment", idx.num_columns == 2);
        TEST("PgIndexDef column_nums assignment", idx.column_nums[0] == 1 && idx.column_nums[1] == 2);
        TEST("PgIndexDef access_method assignment", strcmp(idx.access_method, "btree") == 0);
        TEST("PgIndexDef is_unique assignment", idx.is_unique == true);
        TEST("PgIndexDef is_primary assignment", idx.is_primary == false);
    }

    /*
     * PgTableDef field tests
     */
    printf("\nPgTableDef field tests:\n");
    {
        PgColumnDef cols[2] = {
            {"id", "int4", -1, true},
            {"name", "text", -1, false}
        };
        PgTableDef table = {
            .schema_name = "public",
            .table_name = "users",
            .num_columns = 2,
            .columns = cols,
            .num_indexes = 0,
            .indexes = NULL
        };
        TEST("PgTableDef schema_name assignment", strcmp(table.schema_name, "public") == 0);
        TEST("PgTableDef table_name assignment", strcmp(table.table_name, "users") == 0);
        TEST("PgTableDef num_columns assignment", table.num_columns == 2);
        TEST("PgTableDef columns assignment", table.columns == cols);
    }

    /*
     * Command type tests (shared between logical and physical)
     */
    printf("\nCommand type tests:\n");
    TEST("PG_CMD_UNKNOWN is 0", PG_CMD_UNKNOWN == 0);
    TEST("PG_CMD_SELECT is 1", PG_CMD_SELECT == 1);
    TEST("PG_CMD_INSERT is 2", PG_CMD_INSERT == 2);
    TEST("PG_CMD_UPDATE is 3", PG_CMD_UPDATE == 3);
    TEST("PG_CMD_DELETE is 4", PG_CMD_DELETE == 4);
    TEST("PG_CMD_MERGE is 5", PG_CMD_MERGE == 5);

    /*
     * Join type tests
     */
    printf("\nJoin type tests:\n");
    TEST("PG_JOIN_INNER is 0", PG_JOIN_INNER == 0);
    TEST("PG_JOIN_LEFT is 1", PG_JOIN_LEFT == 1);
    TEST("PG_JOIN_FULL is 2", PG_JOIN_FULL == 2);
    TEST("PG_JOIN_RIGHT is 3", PG_JOIN_RIGHT == 3);
    TEST("PG_JOIN_SEMI is 4", PG_JOIN_SEMI == 4);
    TEST("PG_JOIN_ANTI is 5", PG_JOIN_ANTI == 5);

    /*
     * RTE Kind tests
     */
    printf("\nRTE Kind tests:\n");
    TEST("PG_RTE_RELATION is 0", PG_RTE_RELATION == 0);
    TEST("PG_RTE_SUBQUERY is 1", PG_RTE_SUBQUERY == 1);
    TEST("PG_RTE_JOIN is 2", PG_RTE_JOIN == 2);
    TEST("PG_RTE_FUNCTION is 3", PG_RTE_FUNCTION == 3);
    TEST("PG_RTE_VALUES is 5", PG_RTE_VALUES == 5);
    TEST("PG_RTE_CTE is 6", PG_RTE_CTE == 6);

    /*
     * Logical Expression Type tests (PGQ_ prefix for logical query expressions)
     */
    printf("\nLogical Expression Type tests:\n");
    TEST("PGQ_EXPR_VAR is 1", PGQ_EXPR_VAR == 1);
    TEST("PGQ_EXPR_CONST is 2", PGQ_EXPR_CONST == 2);
    TEST("PGQ_EXPR_PARAM is 3", PGQ_EXPR_PARAM == 3);
    TEST("PGQ_EXPR_OP is 4", PGQ_EXPR_OP == 4);
    TEST("PGQ_EXPR_FUNC is 5", PGQ_EXPR_FUNC == 5);
    TEST("PGQ_EXPR_AGGREF is 6", PGQ_EXPR_AGGREF == 6);
    TEST("PGQ_EXPR_WINDOWFUNC is 7", PGQ_EXPR_WINDOWFUNC == 7);
    TEST("PGQ_EXPR_AND is 8", PGQ_EXPR_AND == 8);
    TEST("PGQ_EXPR_OR is 9", PGQ_EXPR_OR == 9);
    TEST("PGQ_EXPR_NOT is 10", PGQ_EXPR_NOT == 10);
    TEST("PGQ_EXPR_SUBLINK is 18", PGQ_EXPR_SUBLINK == 18);
    TEST("PGQ_EXPR_CASE is 16", PGQ_EXPR_CASE == 16);

    /*
     * Set Operation tests (PGQ_ prefix for logical queries)
     */
    printf("\nSet Operation tests:\n");
    TEST("PGQ_SETOP_NONE is 0", PGQ_SETOP_NONE == 0);
    TEST("PGQ_SETOP_UNION is 1", PGQ_SETOP_UNION == 1);
    TEST("PGQ_SETOP_INTERSECT is 2", PGQ_SETOP_INTERSECT == 2);
    TEST("PGQ_SETOP_EXCEPT is 3", PGQ_SETOP_EXCEPT == 3);

    /*
     * Sublink Type tests
     */
    printf("\nSublink Type tests:\n");
    TEST("PG_SUBLINK_EXISTS is 0", PG_SUBLINK_EXISTS == 0);
    TEST("PG_SUBLINK_ALL is 1", PG_SUBLINK_ALL == 1);
    TEST("PG_SUBLINK_ANY is 2", PG_SUBLINK_ANY == 2);
    TEST("PG_SUBLINK_EXPR is 4", PG_SUBLINK_EXPR == 4);

    /*
     * Logical Query Structure tests
     */
    printf("\nLogical Query Structure tests:\n");
    TEST("PgLogicalQuery size", sizeof(PgLogicalQuery) > sizeof(void*));
    TEST("PgLogicalExpr size", sizeof(PgLogicalExpr) > sizeof(void*));
    TEST("PgRangeTableEntry size", sizeof(PgRangeTableEntry) > sizeof(void*));
    TEST("PgFromExpr size", sizeof(PgFromExpr) > sizeof(void*));
    TEST("PgJoinExpr size", sizeof(PgJoinExpr) > sizeof(void*));
    TEST("PgCTE size", sizeof(PgCTE) > sizeof(void*));
    TEST("PgWindowClause size", sizeof(PgWindowClause) > sizeof(void*));
    TEST("PgSortGroupClause size", sizeof(PgSortGroupClause) > 0);

    /*
     * PgLogicalQuery field tests
     */
    printf("\nPgLogicalQuery field tests:\n");
    {
        PgLogicalQuery query = {
            .command_type = PG_CMD_SELECT,
            .query_id = 54321,
            .has_aggs = true,
            .has_window_funcs = false,
            .has_sublinks = true,
            .num_rtable = 3,
            .num_targets = 5
        };
        TEST("PgLogicalQuery command_type", query.command_type == PG_CMD_SELECT);
        TEST("PgLogicalQuery query_id", query.query_id == 54321);
        TEST("PgLogicalQuery has_aggs", query.has_aggs == true);
        TEST("PgLogicalQuery has_sublinks", query.has_sublinks == true);
        TEST("PgLogicalQuery num_rtable", query.num_rtable == 3);
        TEST("PgLogicalQuery num_targets", query.num_targets == 5);
    }

    /*
     * PgLogicalExpr Var field tests
     */
    printf("\nPgLogicalExpr Var field tests:\n");
    {
        PgLogicalExpr expr = {
            .type = PGQ_EXPR_VAR,
            .result_type = 23,
            .data.var.varno = 1,
            .data.var.varattno = 3,
            .data.var.varlevelsup = 0
        };
        TEST("PgLogicalExpr VAR type", expr.type == PGQ_EXPR_VAR);
        TEST("PgLogicalExpr VAR result_type", expr.result_type == 23);
        TEST("PgLogicalExpr VAR varno", expr.data.var.varno == 1);
        TEST("PgLogicalExpr VAR varattno", expr.data.var.varattno == 3);
    }

    /*
     * PgRangeTableEntry field tests
     */
    printf("\nPgRangeTableEntry field tests:\n");
    {
        PgRangeTableEntry rte = {
            .kind = PG_RTE_RELATION,
            .relid = 16384,
            .relname = "users",
            .inh = true
        };
        TEST("PgRangeTableEntry kind", rte.kind == PG_RTE_RELATION);
        TEST("PgRangeTableEntry relid", rte.relid == 16384);
        TEST("PgRangeTableEntry relname", strcmp(rte.relname, "users") == 0);
        TEST("PgRangeTableEntry inh", rte.inh == true);
    }

    /*
     * PgPlannerErrorInfo tests
     */
    printf("\nPgPlannerErrorInfo tests:\n");
    {
        PgPlannerErrorInfo err = {
            .error_code = PGPLANNER_ERROR_ANALYZE_ERROR,
            .message = "column \"foo\" does not exist",
            .detail = NULL,
            .hint = NULL,
            .context = NULL,
            .position = 8,
            .schema_name = NULL,
            .table_name = "users",
            .column_name = "foo"
        };
        TEST("PgPlannerErrorInfo size", sizeof(PgPlannerErrorInfo) >= sizeof(void*));
        TEST("PgPlannerErrorInfo error_code", err.error_code == PGPLANNER_ERROR_ANALYZE_ERROR);
        TEST("PgPlannerErrorInfo message", strcmp(err.message, "column \"foo\" does not exist") == 0);
        TEST("PgPlannerErrorInfo detail is NULL", err.detail == NULL);
        TEST("PgPlannerErrorInfo position", err.position == 8);
        TEST("PgPlannerErrorInfo table_name", strcmp(err.table_name, "users") == 0);
        TEST("PgPlannerErrorInfo column_name", strcmp(err.column_name, "foo") == 0);
    }

    /*
     * Test error info with position for typical syntax error scenarios
     */
    printf("\nError position scenario tests:\n");
    {
        /* Scenario 1: Syntax error at start of query */
        PgPlannerErrorInfo err1 = {
            .error_code = PGPLANNER_ERROR_PARSE_ERROR,
            .message = "syntax error at or near \"SELEC\"",
            .position = 1
        };
        TEST("Syntax error at position 1", err1.position == 1);

        /* Scenario 2: Unknown column in middle of query */
        PgPlannerErrorInfo err2 = {
            .error_code = PGPLANNER_ERROR_ANALYZE_ERROR,
            .message = "column \"nonexistent\" does not exist",
            .position = 8,
            .column_name = "nonexistent"
        };
        TEST("Column error at position 8", err2.position == 8);
        TEST("Column name captured", strcmp(err2.column_name, "nonexistent") == 0);

        /* Scenario 3: Unknown table */
        PgPlannerErrorInfo err3 = {
            .error_code = PGPLANNER_ERROR_ANALYZE_ERROR,
            .message = "relation \"unknown_table\" does not exist",
            .position = 15,
            .table_name = "unknown_table"
        };
        TEST("Table error at position 15", err3.position == 15);
        TEST("Table name captured", strcmp(err3.table_name, "unknown_table") == 0);

        /* Scenario 4: Error with hint */
        PgPlannerErrorInfo err4 = {
            .error_code = PGPLANNER_ERROR_ANALYZE_ERROR,
            .message = "column reference \"id\" is ambiguous",
            .hint = "Use a table qualifier to disambiguate",
            .position = 8
        };
        TEST("Error with hint", err4.hint != NULL);
        TEST("Hint message", strcmp(err4.hint, "Use a table qualifier to disambiguate") == 0);

        /* Scenario 5: No position (internal error) */
        PgPlannerErrorInfo err5 = {
            .error_code = PGPLANNER_ERROR_INTERNAL,
            .message = "internal error occurred",
            .position = 0
        };
        TEST("No position (0) for internal error", err5.position == 0);
    }

    /*
     * Callback type tests
     */
    printf("\nCallback type tests:\n");
    {
        /* Test that PgTableLookupCallback type is defined */
        PgTableLookupCallback callback_ptr = NULL;
        TEST("PgTableLookupCallback type exists", sizeof(callback_ptr) == sizeof(void*));

        /* Test that we can assign a callback function */
        callback_ptr = api_test_callback;
        TEST("PgTableLookupCallback can be assigned", callback_ptr == api_test_callback);

        /* Test callback signature - NULL schema */
        const PgTableDef *result = callback_ptr(NULL, "test_table", NULL);
        TEST("PgTableLookupCallback NULL schema accepted", result == NULL);

        /* Test callback signature - with schema */
        result = callback_ptr("public", "test_table", NULL);
        TEST("PgTableLookupCallback with schema accepted", result == NULL);

        /* Test callback signature - with user data */
        int user_data = 42;
        result = callback_ptr("public", "test_table", &user_data);
        TEST("PgTableLookupCallback with user_data accepted", result == NULL);
    }

    printf("\n----------------------------------------\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);
    printf("----------------------------------------\n");

    printf("\nNote: These tests verify API structure only.\n");
    printf("Full query analysis tests require PostgreSQL backend initialization.\n");

    return (tests_passed == tests_run) ? 0 : 1;
}
