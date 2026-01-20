/*-------------------------------------------------------------------------
 *
 * test_init.c
 *    Basic initialization tests for libpgplanner.
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

#define TEST(name, code) do { \
    printf("  Testing %s... ", name); \
    tests_run++; \
    if (code) { \
        printf("PASSED\n"); \
        tests_passed++; \
    } else { \
        printf("FAILED\n"); \
    } \
} while(0)

int main(int argc, char **argv)
{
    printf("Running basic API tests (compilation verification)...\n");

    /*
     * These are compilation tests - they verify that the API
     * is properly declared and can be linked against.
     * Full functional tests require a more complete setup.
     */

    /* Test: API types exist */
    TEST("PgColumnDef type exists", sizeof(PgColumnDef) > 0);
    TEST("PgIndexDef type exists", sizeof(PgIndexDef) > 0);
    TEST("PgTableDef type exists", sizeof(PgTableDef) > 0);
    TEST("PgPlanResult type exists", sizeof(PgPlanResult) > 0);

    /* Test: Error codes are defined */
    TEST("PGPLANNER_OK is 0", PGPLANNER_OK == 0);
    TEST("PGPLANNER_ERROR_NOT_INITIALIZED defined", PGPLANNER_ERROR_NOT_INITIALIZED == 1);
    TEST("PGPLANNER_ERROR_PARSE_ERROR defined", PGPLANNER_ERROR_PARSE_ERROR == 3);

    /* Test: Version function is callable */
    TEST("pgplanner_version returns non-null", pgplanner_version() != NULL);
    TEST("Version starts with expected prefix",
         strncmp(pgplanner_version(), "libpgplanner", 12) == 0);

    /* Test: is_initialized returns false before init */
    TEST("Not initialized before init", !pgplanner_is_initialized());

    /* Test: Error name function works */
    TEST("Error name for OK",
         strcmp(pgplanner_error_name(PGPLANNER_OK), "OK") == 0);
    TEST("Error name for NOT_INITIALIZED",
         strcmp(pgplanner_error_name(PGPLANNER_ERROR_NOT_INITIALIZED), "NOT_INITIALIZED") == 0);

    printf("\nResults: %d/%d tests passed\n", tests_passed, tests_run);
    printf("\nNote: Full planner tests require running PostgreSQL backend initialization.\n");
    printf("These tests verify the API is properly declared and basic functionality works.\n");

    return (tests_passed == tests_run) ? 0 : 1;
}
