/*-------------------------------------------------------------------------
 *
 * test_planning.c
 *    Tests for query analysis functionality using table callbacks.
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
 * Table definitions for testing - provided via callback
 */

/* users table */
static PgColumnDef users_cols[] = {
    {"id", "int4", -1, true},
    {"name", "text", -1, true},
    {"email", "text", -1, true},
    {"created_at", "timestamp", -1, false},
};
static int users_pk_cols[] = {1};
static PgIndexDef users_indexes[] = {
    {"users_pkey", 1, users_pk_cols, "btree", true, true},
};
static PgTableDef users_table = {
    NULL, "users", 4, users_cols, 1, users_indexes
};

/* orders table */
static PgColumnDef orders_cols[] = {
    {"id", "int4", -1, true},
    {"user_id", "int4", -1, true},
    {"total", "numeric", -1, true},
    {"order_date", "date", -1, false},
};
static int orders_pk_cols[] = {1};
static int orders_user_cols[] = {2};
static PgIndexDef orders_indexes[] = {
    {"orders_pkey", 1, orders_pk_cols, "btree", true, true},
    {"orders_user_idx", 1, orders_user_cols, "btree", false, false},
};
static PgTableDef orders_table = {
    NULL, "orders", 4, orders_cols, 2, orders_indexes
};

/* products table */
static PgColumnDef products_cols[] = {
    {"id", "int4", -1, true},
    {"name", "text", -1, true},
    {"price", "numeric", -1, true},
    {"category", "text", -1, false},
};
static int products_pk_cols[] = {1};
static PgIndexDef products_indexes[] = {
    {"products_pkey", 1, products_pk_cols, "btree", true, true},
};
static PgTableDef products_table = {
    NULL, "products", 4, products_cols, 1, products_indexes
};

/* employees table (for callback-specific tests) */
static PgColumnDef employees_cols[] = {
    {"id", "int4", -1, true},
    {"name", "text", -1, true},
    {"department", "text", -1, false},
    {"salary", "numeric", -1, false},
};
static int employees_pk_cols[] = {1};
static PgIndexDef employees_indexes[] = {
    {"employees_pkey", 1, employees_pk_cols, "btree", true, true},
};
static PgTableDef employees_table = {
    NULL, "employees", 4, employees_cols, 1, employees_indexes
};

/* departments table */
static PgColumnDef departments_cols[] = {
    {"id", "int4", -1, true},
    {"name", "text", -1, true},
};
static int departments_pk_cols[] = {1};
static PgIndexDef departments_indexes[] = {
    {"departments_pkey", 1, departments_pk_cols, "btree", true, true},
};
static PgTableDef departments_table = {
    NULL, "departments", 2, departments_cols, 1, departments_indexes
};

static int callback_lookup_count = 0;

/*
 * Table lookup callback that provides all test tables
 */
static const PgTableDef *test_table_callback(const char *schema_name,
                                              const char *table_name,
                                              void *user_data)
{
    callback_lookup_count++;

    (void)schema_name;  /* Ignore schema for these tests */
    (void)user_data;

    if (strcmp(table_name, "users") == 0)
        return &users_table;
    if (strcmp(table_name, "orders") == 0)
        return &orders_table;
    if (strcmp(table_name, "products") == 0)
        return &products_table;
    if (strcmp(table_name, "employees") == 0)
        return &employees_table;
    if (strcmp(table_name, "departments") == 0)
        return &departments_table;

    return NULL;  /* Unknown table */
}

/*
 * Test simple SELECT *
 */
static int test_simple_select_star(void)
{
    int error_code;
    char *error_message = NULL;
    PgLogicalQuery *query = pgplanner_analyze_query("SELECT * FROM users",
                                                     &error_code, &error_message);

    if (query == NULL)
    {
        fprintf(stderr, "Analysis failed: %s\n", error_message ? error_message : "unknown");
        if (error_message) pgplanner_free(error_message);
        return 0;
    }

    /* Query should be a SELECT */
    if (query->command_type != PG_CMD_SELECT)
    {
        pgquery_free(query);
        return 0;
    }

    /* Should have at least one table in range table */
    if (query->num_rtable < 1)
    {
        pgquery_free(query);
        return 0;
    }

    /* Should have target list entries (SELECT *) */
    if (query->num_targets < 1)
    {
        pgquery_free(query);
        return 0;
    }

    pgquery_free(query);
    return 1;
}

/*
 * Test SELECT with specific columns
 */
static int test_select_columns(void)
{
    int error_code;
    char *error_message = NULL;
    PgLogicalQuery *query = pgplanner_analyze_query("SELECT id, name FROM users",
                                                     &error_code, &error_message);

    if (query == NULL)
    {
        fprintf(stderr, "Analysis failed: %s\n", error_message ? error_message : "unknown");
        if (error_message) pgplanner_free(error_message);
        return 0;
    }

    /* Should have exactly 2 target entries */
    if (query->num_targets != 2)
    {
        fprintf(stderr, "Expected 2 targets, got %d\n", query->num_targets);
        pgquery_free(query);
        return 0;
    }

    pgquery_free(query);
    return 1;
}

/*
 * Test SELECT with WHERE clause
 */
static int test_select_where(void)
{
    int error_code;
    char *error_message = NULL;
    PgLogicalQuery *query = pgplanner_analyze_query("SELECT * FROM users WHERE id = 1",
                                                     &error_code, &error_message);

    if (query == NULL)
    {
        fprintf(stderr, "Analysis failed: %s\n", error_message ? error_message : "unknown");
        if (error_message) pgplanner_free(error_message);
        return 0;
    }

    /* Should have a jointree with quals */
    if (query->jointree == NULL)
    {
        pgquery_free(query);
        return 0;
    }

    /* WHERE clause should produce quals */
    if (query->jointree->quals == NULL)
    {
        fprintf(stderr, "Expected WHERE quals, got NULL\n");
        pgquery_free(query);
        return 0;
    }

    pgquery_free(query);
    return 1;
}

/*
 * Test SELECT with ORDER BY
 */
static int test_select_order_by(void)
{
    int error_code;
    char *error_message = NULL;
    PgLogicalQuery *query = pgplanner_analyze_query(
        "SELECT * FROM users ORDER BY name",
        &error_code, &error_message);

    if (query == NULL)
    {
        fprintf(stderr, "Analysis failed: %s\n", error_message ? error_message : "unknown");
        if (error_message) pgplanner_free(error_message);
        return 0;
    }

    /* Should have sort clause */
    if (query->num_sort_cols < 1)
    {
        fprintf(stderr, "Expected sort columns, got %d\n", query->num_sort_cols);
        pgquery_free(query);
        return 0;
    }

    pgquery_free(query);
    return 1;
}

/*
 * Test SELECT with LIMIT
 */
static int test_select_limit(void)
{
    int error_code;
    char *error_message = NULL;
    PgLogicalQuery *query = pgplanner_analyze_query(
        "SELECT * FROM users LIMIT 10",
        &error_code, &error_message);

    if (query == NULL)
    {
        fprintf(stderr, "Analysis failed: %s\n", error_message ? error_message : "unknown");
        if (error_message) pgplanner_free(error_message);
        return 0;
    }

    /* Should have limit count */
    if (query->limit_count == NULL)
    {
        fprintf(stderr, "Expected limit count, got NULL\n");
        pgquery_free(query);
        return 0;
    }

    pgquery_free(query);
    return 1;
}

/*
 * Test simple JOIN
 */
static int test_simple_join(void)
{
    int error_code;
    char *error_message = NULL;
    PgLogicalQuery *query = pgplanner_analyze_query(
        "SELECT u.name, o.total "
        "FROM users u JOIN orders o ON u.id = o.user_id",
        &error_code, &error_message);

    if (query == NULL)
    {
        fprintf(stderr, "Join analysis failed: %s\n", error_message ? error_message : "unknown");
        if (error_message) pgplanner_free(error_message);
        return 0;
    }

    /* Should have multiple tables in range table */
    if (query->num_rtable < 2)
    {
        fprintf(stderr, "Expected 2+ tables, got %d\n", query->num_rtable);
        pgquery_free(query);
        return 0;
    }

    pgquery_free(query);
    return 1;
}

/*
 * Test LEFT JOIN
 */
static int test_left_join(void)
{
    int error_code;
    char *error_message = NULL;
    PgLogicalQuery *query = pgplanner_analyze_query(
        "SELECT u.name, o.total "
        "FROM users u LEFT JOIN orders o ON u.id = o.user_id",
        &error_code, &error_message);

    if (query == NULL)
    {
        fprintf(stderr, "Left join analysis failed: %s\n", error_message ? error_message : "unknown");
        if (error_message) pgplanner_free(error_message);
        return 0;
    }

    /* Should have multiple tables */
    if (query->num_rtable < 2)
    {
        pgquery_free(query);
        return 0;
    }

    pgquery_free(query);
    return 1;
}

/*
 * Test invalid table (should fail)
 */
static int test_invalid_table(void)
{
    int error_code;
    char *error_message = NULL;
    PgLogicalQuery *query = pgplanner_analyze_query(
        "SELECT * FROM nonexistent_table",
        &error_code, &error_message);

    /* Should fail */
    if (query != NULL)
    {
        pgquery_free(query);
        return 0;
    }

    /* Error code should indicate analysis error */
    if (error_code != PGPLANNER_ERROR_ANALYZE_ERROR &&
        error_code != PGPLANNER_ERROR_INTERNAL)
    {
        if (error_message) pgplanner_free(error_message);
        return 0;
    }

    if (error_message) pgplanner_free(error_message);
    return 1;
}

/*
 * Test syntax error (should fail)
 */
static int test_syntax_error(void)
{
    int error_code;
    char *error_message = NULL;
    PgLogicalQuery *query = pgplanner_analyze_query("SELEC * FROM users",
                                                     &error_code, &error_message);

    /* Should fail */
    if (query != NULL)
    {
        pgquery_free(query);
        return 0;
    }

    /* Should have a parse error */
    if (error_code != PGPLANNER_ERROR_PARSE_ERROR &&
        error_code != PGPLANNER_ERROR_INTERNAL)
    {
        if (error_message) pgplanner_free(error_message);
        return 0;
    }

    if (error_message) pgplanner_free(error_message);
    return 1;
}

/*
 * Test NULL SQL string
 */
static int test_null_sql(void)
{
    int error_code;
    char *error_message = NULL;
    PgLogicalQuery *query = pgplanner_analyze_query(NULL, &error_code, &error_message);

    /* Should fail */
    if (query != NULL)
    {
        pgquery_free(query);
        return 0;
    }

    /* Should have an error */
    if (error_code == PGPLANNER_OK)
    {
        if (error_message) pgplanner_free(error_message);
        return 0;
    }

    if (error_message) pgplanner_free(error_message);
    return 1;
}

/*
 * Test that callback is invoked for table lookup
 */
static int test_callback_invoked(void)
{
    int initial_count = callback_lookup_count;
    int error_code;
    char *error_message = NULL;

    PgLogicalQuery *query = pgplanner_analyze_query("SELECT * FROM employees",
                                                     &error_code, &error_message);

    if (query == NULL)
    {
        fprintf(stderr, "Callback query failed: %s\n", error_message ? error_message : "unknown");
        if (error_message) pgplanner_free(error_message);
        return 0;
    }

    /* Verify callback was invoked (at least once for this new table) */
    if (callback_lookup_count <= initial_count)
    {
        fprintf(stderr, "Callback was not invoked\n");
        pgquery_free(query);
        return 0;
    }

    pgquery_free(query);
    return 1;
}

/*
 * Test callback-based join
 */
static int test_callback_join(void)
{
    int error_code;
    char *error_message = NULL;
    PgLogicalQuery *query = pgplanner_analyze_query(
        "SELECT e.name, d.name "
        "FROM employees e JOIN departments d ON e.department = d.name",
        &error_code, &error_message);

    if (query == NULL)
    {
        fprintf(stderr, "Callback join failed: %s\n", error_message ? error_message : "unknown");
        if (error_message) pgplanner_free(error_message);
        return 0;
    }

    pgquery_free(query);
    return 1;
}

/*
 * Test that tables are cached after first lookup
 */
static int test_callback_cached(void)
{
    int error_code;
    char *error_message = NULL;

    /* First query to ensure table is registered */
    PgLogicalQuery *query = pgplanner_analyze_query("SELECT * FROM employees",
                                                     &error_code, &error_message);
    if (query == NULL)
    {
        if (error_message) pgplanner_free(error_message);
        return 0;
    }
    pgquery_free(query);

    int count_after_first = callback_lookup_count;

    /* Second query - table should be cached */
    query = pgplanner_analyze_query("SELECT id, name FROM employees WHERE id > 10",
                                    &error_code, &error_message);
    if (query == NULL)
    {
        if (error_message) pgplanner_free(error_message);
        return 0;
    }
    pgquery_free(query);

    /* Callback count should be the same (table was cached) */
    if (callback_lookup_count != count_after_first)
    {
        fprintf(stderr, "Callback invoked again for cached table: %d -> %d\n",
                count_after_first, callback_lookup_count);
        return 0;
    }

    return 1;
}

/*
 * Test aggregates
 */
static int test_aggregates(void)
{
    int error_code;
    char *error_message = NULL;
    PgLogicalQuery *query = pgplanner_analyze_query(
        "SELECT COUNT(*), SUM(total) FROM orders",
        &error_code, &error_message);

    if (query == NULL)
    {
        fprintf(stderr, "Aggregate analysis failed: %s\n", error_message ? error_message : "unknown");
        if (error_message) pgplanner_free(error_message);
        return 0;
    }

    /* Should indicate it has aggregates */
    if (!query->has_aggs)
    {
        fprintf(stderr, "Expected has_aggs=true\n");
        pgquery_free(query);
        return 0;
    }

    pgquery_free(query);
    return 1;
}

/*
 * Test GROUP BY
 */
static int test_group_by(void)
{
    int error_code;
    char *error_message = NULL;
    PgLogicalQuery *query = pgplanner_analyze_query(
        "SELECT user_id, COUNT(*) FROM orders GROUP BY user_id",
        &error_code, &error_message);

    if (query == NULL)
    {
        fprintf(stderr, "Group by analysis failed: %s\n", error_message ? error_message : "unknown");
        if (error_message) pgplanner_free(error_message);
        return 0;
    }

    /* Should have group clause */
    if (query->num_group_cols < 1)
    {
        fprintf(stderr, "Expected group columns, got %d\n", query->num_group_cols);
        pgquery_free(query);
        return 0;
    }

    pgquery_free(query);
    return 1;
}

int main(int argc, char **argv)
{
    int ret;

    (void)argc;
    (void)argv;

    printf("Running query analysis tests...\n");

    /* Initialize library */
    ret = pgplanner_init();
    if (ret != PGPLANNER_OK)
    {
        fprintf(stderr, "Failed to initialize: %s\n", pgplanner_error_name(ret));
        return 1;
    }

    /* Set up the table lookup callback */
    pgplanner_set_table_lookup_callback(test_table_callback, NULL);

    TEST(simple_select_star);
    TEST(select_columns);
    TEST(select_where);
    TEST(select_order_by);
    TEST(select_limit);
    TEST(simple_join);
    TEST(left_join);
    TEST(invalid_table);
    TEST(syntax_error);
    TEST(null_sql);
    TEST(callback_invoked);
    TEST(callback_join);
    TEST(callback_cached);
    TEST(aggregates);
    TEST(group_by);

    pgplanner_shutdown();

    printf("\nResults: %d/%d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
