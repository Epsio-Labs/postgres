/*-------------------------------------------------------------------------
 *
 * join_query.c
 *    Example: Planning queries with JOINs.
 *
 * This example demonstrates:
 * - Registering multiple related tables
 * - Planning JOIN queries
 * - Different join types (INNER, LEFT, etc.)
 *
 * Copyright (c) 2024, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>

#include "pgplanner.h"

/*
 * Register the users table
 */
static PgOid register_users_table(void)
{
    PgColumnDef cols[] = {
        {"id", "int4", -1, true},
        {"name", "text", -1, true},
        {"email", "text", -1, true},
    };

    int pk_cols[] = {1};
    PgIndexDef indexes[] = {
        {"users_pkey", 1, pk_cols, "btree", true, true},
    };

    PgTableDef table = {
        .schema_name = NULL,
        .table_name = "users",
        .num_columns = 3,
        .columns = cols,
        .num_indexes = 1,
        .indexes = indexes,
    };

    return pgplanner_register_table(&table);
}

/*
 * Register the orders table
 */
static PgOid register_orders_table(void)
{
    PgColumnDef cols[] = {
        {"id", "int4", -1, true},
        {"user_id", "int4", -1, true},
        {"total", "numeric", -1, true},
        {"status", "text", -1, true},
        {"created_at", "timestamp", -1, false},
    };

    int pk_cols[] = {1};
    int user_cols[] = {2};
    PgIndexDef indexes[] = {
        {"orders_pkey", 1, pk_cols, "btree", true, true},
        {"orders_user_idx", 1, user_cols, "btree", false, false},
    };

    PgTableDef table = {
        .schema_name = NULL,
        .table_name = "orders",
        .num_columns = 5,
        .columns = cols,
        .num_indexes = 2,
        .indexes = indexes,
    };

    return pgplanner_register_table(&table);
}

/*
 * Register the order_items table
 */
static PgOid register_order_items_table(void)
{
    PgColumnDef cols[] = {
        {"id", "int4", -1, true},
        {"order_id", "int4", -1, true},
        {"product_id", "int4", -1, true},
        {"quantity", "int4", -1, true},
        {"price", "numeric", -1, true},
    };

    int pk_cols[] = {1};
    int order_cols[] = {2};
    PgIndexDef indexes[] = {
        {"order_items_pkey", 1, pk_cols, "btree", true, true},
        {"order_items_order_idx", 1, order_cols, "btree", false, false},
    };

    PgTableDef table = {
        .schema_name = NULL,
        .table_name = "order_items",
        .num_columns = 5,
        .columns = cols,
        .num_indexes = 2,
        .indexes = indexes,
    };

    return pgplanner_register_table(&table);
}

/*
 * Plan a query and print the result
 */
static void plan_and_print(const char *description, const char *sql)
{
    PgPlanResult *result;

    printf("=== %s ===\n", description);
    printf("SQL: %s\n\n", sql);

    result = pgplanner_plan(sql);

    if (result->error_code == PGPLANNER_OK)
    {
        /* Print first 800 chars of plan */
        int len = strlen(result->plan_text);
        if (len > 800)
        {
            printf("Plan (truncated):\n%.800s...\n\n", result->plan_text);
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
}

int main(int argc, char **argv)
{
    int ret;

    printf("=== PostgreSQL Planner JOIN Example ===\n\n");

    /* Initialize */
    ret = pgplanner_init();
    if (ret != PGPLANNER_OK)
    {
        fprintf(stderr, "Init failed: %s\n", pgplanner_error_name(ret));
        return 1;
    }

    /* Register tables */
    printf("Registering tables...\n");
    if (register_users_table() == InvalidPgOid ||
        register_orders_table() == InvalidPgOid ||
        register_order_items_table() == InvalidPgOid)
    {
        fprintf(stderr, "Failed to register tables\n");
        pgplanner_shutdown();
        return 1;
    }
    printf("Tables registered.\n\n");

    /* Simple INNER JOIN */
    plan_and_print("Simple INNER JOIN",
        "SELECT u.name, o.total "
        "FROM users u "
        "JOIN orders o ON u.id = o.user_id");

    /* LEFT JOIN */
    plan_and_print("LEFT JOIN (users with or without orders)",
        "SELECT u.name, COUNT(o.id) as order_count "
        "FROM users u "
        "LEFT JOIN orders o ON u.id = o.user_id "
        "GROUP BY u.id, u.name");

    /* Three-way JOIN */
    plan_and_print("Three-way JOIN",
        "SELECT u.name, o.id as order_id, oi.product_id, oi.quantity "
        "FROM users u "
        "JOIN orders o ON u.id = o.user_id "
        "JOIN order_items oi ON o.id = oi.order_id "
        "WHERE o.status = 'completed'");

    /* JOIN with aggregation */
    plan_and_print("JOIN with aggregation",
        "SELECT u.name, SUM(o.total) as total_spent "
        "FROM users u "
        "JOIN orders o ON u.id = o.user_id "
        "GROUP BY u.id, u.name "
        "HAVING SUM(o.total) > 1000 "
        "ORDER BY total_spent DESC");

    /* Self-join simulation with subquery */
    plan_and_print("Subquery in FROM clause",
        "SELECT u.name, order_summary.total_orders, order_summary.total_amount "
        "FROM users u "
        "JOIN ("
        "  SELECT user_id, COUNT(*) as total_orders, SUM(total) as total_amount "
        "  FROM orders "
        "  GROUP BY user_id"
        ") order_summary ON u.id = order_summary.user_id");

    /* Cleanup */
    pgplanner_shutdown();

    printf("Done!\n");
    return 0;
}
