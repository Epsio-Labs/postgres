/*-------------------------------------------------------------------------
 *
 * test_tables.c
 *    Tests for table lookup callback functionality.
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
 * Callback data for testing
 */
static PgColumnDef callback_test_cols[] = {
    {"id", "int4", -1, true},
    {"value", "text", -1, false},
};

static PgTableDef callback_test_table = {
    .schema_name = NULL,
    .table_name = "callback_table",
    .num_columns = 2,
    .columns = callback_test_cols,
    .num_indexes = 0,
    .indexes = NULL,
};

static int callback_invocations = 0;

static const PgTableDef *test_table_callback(const char *schema_name,
                                             const char *table_name,
                                             void *user_data)
{
    callback_invocations++;

    /* Return our test table if the name matches */
    if (strcmp(table_name, "callback_table") == 0)
        return &callback_test_table;

    /* Also support schema-qualified lookup */
    if (schema_name != NULL && strcmp(schema_name, "test_schema") == 0 &&
        strcmp(table_name, "schema_callback_table") == 0)
    {
        static PgTableDef schema_table = {
            .schema_name = "test_schema",
            .table_name = "schema_callback_table",
            .num_columns = 2,
            .columns = NULL,  /* Will be set below */
            .num_indexes = 0,
            .indexes = NULL,
        };
        schema_table.columns = callback_test_cols;
        return &schema_table;
    }

    return NULL;
}

/*
 * Test setting and getting callback
 */
static int test_callback_set_get(void)
{
    PgTableLookupCallback cb;
    void *data;
    int test_data = 42;

    /* Initially no callback */
    pgplanner_get_table_lookup_callback(&cb, &data);
    if (cb != NULL || data != NULL)
        return 0;

    /* Set callback */
    pgplanner_set_table_lookup_callback(test_table_callback, &test_data);

    /* Get and verify */
    pgplanner_get_table_lookup_callback(&cb, &data);
    if (cb != test_table_callback)
        return 0;
    if (data != &test_data)
        return 0;

    /* Clear callback */
    pgplanner_set_table_lookup_callback(NULL, NULL);

    /* Verify cleared */
    pgplanner_get_table_lookup_callback(&cb, &data);
    if (cb != NULL || data != NULL)
        return 0;

    return 1;
}

/*
 * Test callback signature compatibility
 */
static int test_callback_signature(void)
{
    /* Verify callback can be called with various argument combinations */
    callback_invocations = 0;

    /* NULL schema */
    const PgTableDef *result = test_table_callback(NULL, "callback_table", NULL);
    if (result != &callback_test_table)
        return 0;

    /* With schema */
    result = test_table_callback("test_schema", "schema_callback_table", NULL);
    if (result == NULL)
        return 0;

    /* Unknown table returns NULL */
    result = test_table_callback(NULL, "nonexistent", NULL);
    if (result != NULL)
        return 0;

    /* Verify invocations were counted */
    if (callback_invocations != 3)
        return 0;

    return 1;
}

/*
 * Test callback with user data
 */
static int test_callback_user_data(void)
{
    int expected_value = 12345;
    pgplanner_set_table_lookup_callback(test_table_callback, &expected_value);

    /* Verify the user data was stored correctly */
    PgTableLookupCallback cb;
    void *data;
    pgplanner_get_table_lookup_callback(&cb, &data);

    if (cb != test_table_callback)
        return 0;
    if (data != &expected_value)
        return 0;

    pgplanner_set_table_lookup_callback(NULL, NULL);
    return 1;
}

int main(int argc, char **argv)
{
    int ret;

    printf("Running table callback tests...\n");

    /* Initialize library */
    ret = pgplanner_init();
    if (ret != PGPLANNER_OK)
    {
        fprintf(stderr, "Failed to initialize: %s\n", pgplanner_error_name(ret));
        return 1;
    }

    TEST(callback_set_get);
    TEST(callback_signature);
    TEST(callback_user_data);

    pgplanner_shutdown();

    printf("\nResults: %d/%d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
