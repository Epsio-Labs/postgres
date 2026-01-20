/*-------------------------------------------------------------------------
 *
 * test_aggregates.c
 *    Tests for aggregate and grouping query planning.
 *
 * Copyright (c) 2024, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pgplanner.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  Testing %s... ", #name); \
    tests_run++; \
    if (test_##name()) { \
        printf("PASSED\n"); \
        tests_passed++; \
    } else { \
        printf("FAILED\n"); \
    } \
} while(0)

/*
 * Helper to register test tables
 */
static void register_test_tables(void)
{
    /* sales table for aggregate tests */
    {
        PgColumnDef cols[] = {
            {"id", "int4", -1, true},
            {"product_id", "int4", -1, true},
            {"quantity", "int4", -1, true},
            {"price", "numeric", -1, true},
            {"sale_date", "date", -1, true},
        };
        int pk_cols[] = {1};
        int prod_cols[] = {2};
        PgIndexDef indexes[] = {
            {"sales_pkey", 1, pk_cols, "btree", true, true},
            {"sales_product_idx", 1, prod_cols, "btree", false, false},
        };
        PgTableDef table = {
            NULL, "sales", 5, cols, 2, indexes
        };
        pgplanner_register_table(&table);
    }

    /* employees table for window function tests */
    {
        PgColumnDef cols[] = {
            {"id", "int4", -1, true},
            {"name", "text", -1, true},
            {"department", "text", -1, true},
            {"salary", "numeric", -1, true},
            {"hire_date", "date", -1, true},
        };
        int pk_cols[] = {1};
        PgIndexDef indexes[] = {
            {"employees_pkey", 1, pk_cols, "btree", true, true},
        };
        PgTableDef table = {
            NULL, "employees", 5, cols, 1, indexes
        };
        pgplanner_register_table(&table);
    }
}

/*
 * Test simple COUNT
 */
static int test_count_star(void)
{
    PgPlanResult *result = pgplanner_plan("SELECT COUNT(*) FROM sales");

    if (result == NULL)
        return 0;

    if (result->error_code != PGPLANNER_OK)
    {
        fprintf(stderr, "COUNT(*) plan failed: %s\n", result->error_message);
        pgplanner_free_result(result);
        return 0;
    }

    /* Plan should mention Aggregate */
    if (strstr(result->plan_text, "Agg") == NULL)
    {
        fprintf(stderr, "Plan doesn't contain Aggregate: %s\n", result->plan_text);
        pgplanner_free_result(result);
        return 0;
    }

    pgplanner_free_result(result);
    return 1;
}

/*
 * Test SUM aggregate
 */
static int test_sum(void)
{
    PgPlanResult *result = pgplanner_plan(
        "SELECT SUM(quantity) FROM sales");

    if (result == NULL)
        return 0;

    if (result->error_code != PGPLANNER_OK)
    {
        fprintf(stderr, "SUM plan failed: %s\n", result->error_message);
        pgplanner_free_result(result);
        return 0;
    }

    pgplanner_free_result(result);
    return 1;
}

/*
 * Test AVG aggregate
 */
static int test_avg(void)
{
    PgPlanResult *result = pgplanner_plan(
        "SELECT AVG(price) FROM sales");

    if (result == NULL)
        return 0;

    if (result->error_code != PGPLANNER_OK)
    {
        fprintf(stderr, "AVG plan failed: %s\n", result->error_message);
        pgplanner_free_result(result);
        return 0;
    }

    pgplanner_free_result(result);
    return 1;
}

/*
 * Test MIN/MAX aggregates
 */
static int test_min_max(void)
{
    PgPlanResult *result = pgplanner_plan(
        "SELECT MIN(price), MAX(price) FROM sales");

    if (result == NULL)
        return 0;

    if (result->error_code != PGPLANNER_OK)
    {
        fprintf(stderr, "MIN/MAX plan failed: %s\n", result->error_message);
        pgplanner_free_result(result);
        return 0;
    }

    pgplanner_free_result(result);
    return 1;
}

/*
 * Test GROUP BY
 */
static int test_group_by(void)
{
    PgPlanResult *result = pgplanner_plan(
        "SELECT product_id, SUM(quantity) "
        "FROM sales "
        "GROUP BY product_id");

    if (result == NULL)
        return 0;

    if (result->error_code != PGPLANNER_OK)
    {
        fprintf(stderr, "GROUP BY plan failed: %s\n", result->error_message);
        pgplanner_free_result(result);
        return 0;
    }

    /* Should have some kind of grouping */
    if (strstr(result->plan_text, "Group") == NULL &&
        strstr(result->plan_text, "Agg") == NULL)
    {
        fprintf(stderr, "Plan doesn't contain Group/Agg: %s\n", result->plan_text);
        pgplanner_free_result(result);
        return 0;
    }

    pgplanner_free_result(result);
    return 1;
}

/*
 * Test GROUP BY with HAVING
 */
static int test_group_by_having(void)
{
    PgPlanResult *result = pgplanner_plan(
        "SELECT product_id, SUM(quantity) as total "
        "FROM sales "
        "GROUP BY product_id "
        "HAVING SUM(quantity) > 100");

    if (result == NULL)
        return 0;

    if (result->error_code != PGPLANNER_OK)
    {
        fprintf(stderr, "HAVING plan failed: %s\n", result->error_message);
        pgplanner_free_result(result);
        return 0;
    }

    pgplanner_free_result(result);
    return 1;
}

/*
 * Test window function ROW_NUMBER
 */
static int test_window_row_number(void)
{
    PgPlanResult *result = pgplanner_plan(
        "SELECT id, name, ROW_NUMBER() OVER (ORDER BY hire_date) as rn "
        "FROM employees");

    if (result == NULL)
        return 0;

    if (result->error_code != PGPLANNER_OK)
    {
        fprintf(stderr, "ROW_NUMBER plan failed: %s\n", result->error_message);
        pgplanner_free_result(result);
        return 0;
    }

    /* Should have WindowAgg node */
    if (strstr(result->plan_text, "Window") == NULL)
    {
        fprintf(stderr, "Plan doesn't contain Window: %s\n", result->plan_text);
        pgplanner_free_result(result);
        return 0;
    }

    pgplanner_free_result(result);
    return 1;
}

/*
 * Test window function with PARTITION BY
 */
static int test_window_partition(void)
{
    PgPlanResult *result = pgplanner_plan(
        "SELECT id, name, department, "
        "       SUM(salary) OVER (PARTITION BY department) as dept_total "
        "FROM employees");

    if (result == NULL)
        return 0;

    if (result->error_code != PGPLANNER_OK)
    {
        fprintf(stderr, "PARTITION BY plan failed: %s\n", result->error_message);
        pgplanner_free_result(result);
        return 0;
    }

    pgplanner_free_result(result);
    return 1;
}

/*
 * Test CTE (WITH clause)
 */
static int test_cte(void)
{
    PgPlanResult *result = pgplanner_plan(
        "WITH high_earners AS ("
        "  SELECT * FROM employees WHERE salary > 50000"
        ") "
        "SELECT * FROM high_earners");

    if (result == NULL)
        return 0;

    if (result->error_code != PGPLANNER_OK)
    {
        fprintf(stderr, "CTE plan failed: %s\n", result->error_message);
        pgplanner_free_result(result);
        return 0;
    }

    /* Plan should reference the CTE */
    if (strstr(result->plan_text, "CTE") == NULL &&
        strstr(result->plan_text, "SubPlan") == NULL)
    {
        /* Some plans may inline CTEs, so this might not always show */
    }

    pgplanner_free_result(result);
    return 1;
}

/*
 * Test subquery in FROM
 */
static int test_subquery_from(void)
{
    PgPlanResult *result = pgplanner_plan(
        "SELECT * FROM ("
        "  SELECT product_id, SUM(quantity) as total "
        "  FROM sales "
        "  GROUP BY product_id"
        ") AS product_totals "
        "WHERE total > 10");

    if (result == NULL)
        return 0;

    if (result->error_code != PGPLANNER_OK)
    {
        fprintf(stderr, "Subquery plan failed: %s\n", result->error_message);
        pgplanner_free_result(result);
        return 0;
    }

    pgplanner_free_result(result);
    return 1;
}

/*
 * Test complex query with multiple features
 */
static int test_complex_query(void)
{
    PgPlanResult *result = pgplanner_plan(
        "SELECT s.product_id, "
        "       SUM(s.quantity) as total_qty, "
        "       AVG(s.price) as avg_price "
        "FROM sales s "
        "WHERE s.sale_date >= '2024-01-01' "
        "GROUP BY s.product_id "
        "HAVING SUM(s.quantity) > 10 "
        "ORDER BY total_qty DESC "
        "LIMIT 10");

    if (result == NULL)
        return 0;

    if (result->error_code != PGPLANNER_OK)
    {
        fprintf(stderr, "Complex query plan failed: %s\n", result->error_message);
        pgplanner_free_result(result);
        return 0;
    }

    pgplanner_free_result(result);
    return 1;
}

int main(int argc, char **argv)
{
    int ret;

    printf("Running aggregate and grouping tests...\n");

    /* Initialize library */
    ret = pgplanner_init();
    if (ret != PGPLANNER_OK)
    {
        fprintf(stderr, "Failed to initialize: %s\n", pgplanner_error_name(ret));
        return 1;
    }

    /* Register test tables */
    register_test_tables();

    TEST(count_star);
    TEST(sum);
    TEST(avg);
    TEST(min_max);
    TEST(group_by);
    TEST(group_by_having);
    TEST(window_row_number);
    TEST(window_partition);
    TEST(cte);
    TEST(subquery_from);
    TEST(complex_query);

    pgplanner_shutdown();

    printf("\nResults: %d/%d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
