/*-------------------------------------------------------------------------
 *
 * test_types.c
 *    Tests for type system functionality.
 *
 * Copyright (c) 2024, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

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
 * Test type existence checks
 */
static int test_type_exists(void)
{
    /* Common types should exist */
    if (!pgplanner_type_exists("int4"))
        return 0;
    if (!pgplanner_type_exists("int8"))
        return 0;
    if (!pgplanner_type_exists("text"))
        return 0;
    if (!pgplanner_type_exists("bool"))
        return 0;
    if (!pgplanner_type_exists("float8"))
        return 0;
    if (!pgplanner_type_exists("timestamp"))
        return 0;
    if (!pgplanner_type_exists("date"))
        return 0;

    /* Non-existent type should not exist */
    if (pgplanner_type_exists("nonexistent_type_xyz"))
        return 0;

    return 1;
}

/*
 * Test type aliases
 */
static int test_type_aliases(void)
{
    /* int2, int4, int8 should exist */
    if (!pgplanner_type_exists("int2"))
        return 0;
    if (!pgplanner_type_exists("int4"))
        return 0;
    if (!pgplanner_type_exists("int8"))
        return 0;

    /* float4, float8 should exist */
    if (!pgplanner_type_exists("float4"))
        return 0;
    if (!pgplanner_type_exists("float8"))
        return 0;

    /* varchar and text should exist */
    if (!pgplanner_type_exists("varchar"))
        return 0;
    if (!pgplanner_type_exists("text"))
        return 0;

    return 1;
}

/*
 * Test listing types
 */
static int test_list_types(void)
{
    const char **types;
    int count = 0;
    int has_int4 = 0;
    int has_text = 0;

    types = pgplanner_list_types();
    if (types == NULL)
        return 0;

    /* Count types and check for expected ones */
    for (int i = 0; types[i] != NULL; i++)
    {
        count++;
        if (strcmp(types[i], "int4") == 0)
            has_int4 = 1;
        if (strcmp(types[i], "text") == 0)
            has_text = 1;
    }

    /* Should have some types */
    if (count < 10)
        return 0;

    /* Should include int4 and text */
    if (!has_int4 || !has_text)
        return 0;

    return 1;
}

/*
 * Test type before initialization
 */
static int test_type_uninitialized(void)
{
    /* Shutdown first */
    pgplanner_shutdown();

    /* Type checks should fail gracefully when not initialized */
    if (pgplanner_type_exists("int4"))
        return 0;

    /* Re-initialize for other tests */
    if (pgplanner_init() != PGPLANNER_OK)
        return 0;

    return 1;
}

int main(int argc, char **argv)
{
    int ret;

    printf("Running type system tests...\n");

    /* Initialize library */
    ret = pgplanner_init();
    if (ret != PGPLANNER_OK)
    {
        fprintf(stderr, "Failed to initialize: %s\n", pgplanner_error_name(ret));
        return 1;
    }

    TEST(type_exists);
    TEST(type_aliases);
    TEST(list_types);
    TEST(type_uninitialized);

    pgplanner_shutdown();

    printf("\nResults: %d/%d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
