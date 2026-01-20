/*-------------------------------------------------------------------------
 *
 * aggregate_query.c
 *    Example: Planning queries with aggregates, GROUP BY, and window functions.
 *
 * This example demonstrates:
 * - Aggregate functions (COUNT, SUM, AVG, MIN, MAX)
 * - GROUP BY and HAVING clauses
 * - Window functions (ROW_NUMBER, SUM OVER, etc.)
 * - CTEs (Common Table Expressions)
 *
 * Copyright (c) 2024, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>

#include "pgplanner.h"

/*
 * Register test tables for analytics queries
 */
static void register_tables(void)
{
    /* sales table */
    {
        PgColumnDef cols[] = {
            {"id", "int4", -1, true},
            {"product_id", "int4", -1, true},
            {"region", "text", -1, true},
            {"quantity", "int4", -1, true},
            {"amount", "numeric", -1, true},
            {"sale_date", "date", -1, true},
        };
        int pk_cols[] = {1};
        int prod_cols[] = {2};
        int date_cols[] = {6};
        PgIndexDef indexes[] = {
            {"sales_pkey", 1, pk_cols, "btree", true, true},
            {"sales_product_idx", 1, prod_cols, "btree", false, false},
            {"sales_date_idx", 1, date_cols, "btree", false, false},
        };
        PgTableDef table = {
            NULL, "sales", 6, cols, 3, indexes
        };
        pgplanner_register_table(&table);
    }

    /* products table */
    {
        PgColumnDef cols[] = {
            {"id", "int4", -1, true},
            {"name", "text", -1, true},
            {"category", "text", -1, true},
            {"price", "numeric", -1, true},
        };
        int pk_cols[] = {1};
        PgIndexDef indexes[] = {
            {"products_pkey", 1, pk_cols, "btree", true, true},
        };
        PgTableDef table = {
            NULL, "products", 4, cols, 1, indexes
        };
        pgplanner_register_table(&table);
    }

    /* employees table for window function examples */
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
 * Plan a query and print the result
 */
static void plan_and_print(const char *description, const char *sql)
{
    PgPlanResult *result;

    printf("=== %s ===\n", description);
    printf("SQL:\n%s\n\n", sql);

    result = pgplanner_plan(sql);

    if (result->error_code == PGPLANNER_OK)
    {
        /* Print first 600 chars of plan */
        int len = strlen(result->plan_text);
        if (len > 600)
        {
            printf("Plan (truncated):\n%.600s...\n\n", result->plan_text);
        }
        else
        {
            printf("Plan:\n%s\n\n", result->plan_text);
        }
    }
    else
    {
        printf("Error: %s\n\n", result->error_message);
    }

    pgplanner_free_result(result);
    printf("----------------------------------------\n\n");
}

int main(int argc, char **argv)
{
    int ret;

    printf("=== PostgreSQL Planner Aggregates Example ===\n\n");

    /* Initialize */
    ret = pgplanner_init();
    if (ret != PGPLANNER_OK)
    {
        fprintf(stderr, "Init failed: %s\n", pgplanner_error_name(ret));
        return 1;
    }

    /* Register tables */
    register_tables();
    printf("Tables registered.\n\n");

    /*
     * Basic Aggregates
     */
    plan_and_print("Basic COUNT",
        "SELECT COUNT(*) FROM sales");

    plan_and_print("Multiple aggregates",
        "SELECT\n"
        "    COUNT(*) as total_sales,\n"
        "    SUM(amount) as total_amount,\n"
        "    AVG(amount) as avg_amount,\n"
        "    MIN(amount) as min_sale,\n"
        "    MAX(amount) as max_sale\n"
        "FROM sales");

    /*
     * GROUP BY
     */
    plan_and_print("GROUP BY single column",
        "SELECT\n"
        "    region,\n"
        "    SUM(amount) as total_sales\n"
        "FROM sales\n"
        "GROUP BY region");

    plan_and_print("GROUP BY multiple columns",
        "SELECT\n"
        "    region,\n"
        "    product_id,\n"
        "    SUM(quantity) as total_qty,\n"
        "    SUM(amount) as total_amount\n"
        "FROM sales\n"
        "GROUP BY region, product_id\n"
        "ORDER BY total_amount DESC");

    /*
     * HAVING
     */
    plan_and_print("GROUP BY with HAVING",
        "SELECT\n"
        "    product_id,\n"
        "    SUM(amount) as total_sales\n"
        "FROM sales\n"
        "GROUP BY product_id\n"
        "HAVING SUM(amount) > 10000\n"
        "ORDER BY total_sales DESC");

    /*
     * Window Functions
     */
    plan_and_print("ROW_NUMBER window function",
        "SELECT\n"
        "    id,\n"
        "    name,\n"
        "    salary,\n"
        "    ROW_NUMBER() OVER (ORDER BY salary DESC) as rank\n"
        "FROM employees");

    plan_and_print("Window function with PARTITION BY",
        "SELECT\n"
        "    id,\n"
        "    name,\n"
        "    department,\n"
        "    salary,\n"
        "    SUM(salary) OVER (PARTITION BY department) as dept_total,\n"
        "    salary * 100.0 / SUM(salary) OVER (PARTITION BY department) as pct_of_dept\n"
        "FROM employees");

    plan_and_print("Running total with window function",
        "SELECT\n"
        "    sale_date,\n"
        "    amount,\n"
        "    SUM(amount) OVER (ORDER BY sale_date) as running_total\n"
        "FROM sales");

    /*
     * CTEs (Common Table Expressions)
     */
    plan_and_print("Simple CTE",
        "WITH high_value_sales AS (\n"
        "    SELECT * FROM sales WHERE amount > 1000\n"
        ")\n"
        "SELECT region, COUNT(*) as count\n"
        "FROM high_value_sales\n"
        "GROUP BY region");

    plan_and_print("CTE with aggregation",
        "WITH product_stats AS (\n"
        "    SELECT\n"
        "        product_id,\n"
        "        SUM(amount) as total_sales,\n"
        "        AVG(amount) as avg_sale\n"
        "    FROM sales\n"
        "    GROUP BY product_id\n"
        ")\n"
        "SELECT\n"
        "    p.name,\n"
        "    ps.total_sales,\n"
        "    ps.avg_sale\n"
        "FROM products p\n"
        "JOIN product_stats ps ON p.id = ps.product_id\n"
        "ORDER BY ps.total_sales DESC\n"
        "LIMIT 10");

    /*
     * Complex Analytics Query
     */
    plan_and_print("Complex analytics query",
        "WITH monthly_sales AS (\n"
        "    SELECT\n"
        "        product_id,\n"
        "        region,\n"
        "        SUM(amount) as monthly_total\n"
        "    FROM sales\n"
        "    WHERE sale_date >= '2024-01-01'\n"
        "    GROUP BY product_id, region\n"
        ")\n"
        "SELECT\n"
        "    p.name,\n"
        "    ms.region,\n"
        "    ms.monthly_total,\n"
        "    SUM(ms.monthly_total) OVER (PARTITION BY p.category) as category_total,\n"
        "    ROW_NUMBER() OVER (PARTITION BY ms.region ORDER BY ms.monthly_total DESC) as region_rank\n"
        "FROM monthly_sales ms\n"
        "JOIN products p ON ms.product_id = p.id\n"
        "ORDER BY ms.region, region_rank");

    /* Cleanup */
    pgplanner_shutdown();

    printf("Done!\n");
    return 0;
}
