/*-------------------------------------------------------------------------
 *
 * simple_select.c
 *    Example: Basic usage of the standalone PostgreSQL planner library.
 *
 * This example demonstrates:
 * - Initializing the library
 * - Registering a table schema
 * - Planning a simple SELECT query
 * - Examining the query plan
 *
 * Copyright (c) 2024, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>

#include "pgplanner.h"

int main(int argc, char **argv)
{
    int ret;
    PgOid users_oid;
    PgPlanResult *result;

    printf("=== PostgreSQL Standalone Planner Example ===\n\n");

    /*
     * Step 1: Initialize the library
     */
    printf("Initializing planner library...\n");
    ret = pgplanner_init();
    if (ret != PGPLANNER_OK)
    {
        fprintf(stderr, "Failed to initialize: %s (%s)\n",
                pgplanner_error_name(ret),
                pgplanner_get_last_error());
        return 1;
    }
    printf("Library version: %s\n\n", pgplanner_version());

    /*
     * Step 2: Register table schema
     *
     * Before we can plan queries, we need to tell the planner
     * about our tables. This is like CREATE TABLE but only
     * registers metadata - no actual table is created.
     */
    printf("Registering 'users' table...\n");

    PgColumnDef user_cols[] = {
        {"id", "int4", -1, true},           /* id INT NOT NULL */
        {"name", "text", -1, true},         /* name TEXT NOT NULL */
        {"email", "text", -1, true},        /* email TEXT NOT NULL */
        {"created_at", "timestamp", -1, false}, /* created_at TIMESTAMP */
    };

    /* Define primary key index */
    int pk_cols[] = {1};  /* First column (id) */
    PgIndexDef user_indexes[] = {
        {
            .name = "users_pkey",
            .num_columns = 1,
            .column_nums = pk_cols,
            .access_method = "btree",
            .is_unique = true,
            .is_primary = true,
        },
    };

    PgTableDef users_table = {
        .schema_name = NULL,        /* Use public schema */
        .table_name = "users",
        .num_columns = 4,
        .columns = user_cols,
        .num_indexes = 1,
        .indexes = user_indexes,
    };

    users_oid = pgplanner_register_table(&users_table);
    if (users_oid == InvalidPgOid)
    {
        fprintf(stderr, "Failed to register table: %s\n",
                pgplanner_get_last_error());
        pgplanner_shutdown();
        return 1;
    }
    printf("Table registered with OID: %u\n\n", users_oid);

    /*
     * Step 3: Plan a simple SELECT query
     */
    const char *sql = "SELECT id, name FROM users WHERE id = 42";
    printf("Planning query: %s\n\n", sql);

    result = pgplanner_plan(sql);

    if (result->error_code != PGPLANNER_OK)
    {
        fprintf(stderr, "Planning failed: %s\n", result->error_message);
        pgplanner_free_result(result);
        pgplanner_shutdown();
        return 1;
    }

    /*
     * Step 4: Examine the plan
     *
     * The plan is returned in PostgreSQL's nodeToString format.
     * This is a serialized representation of the plan tree.
     */
    printf("Query Plan:\n");
    printf("----------------------------------------\n");
    printf("%s\n", result->plan_text);
    printf("----------------------------------------\n\n");

    /*
     * Step 5: Try another query
     */
    pgplanner_free_result(result);

    sql = "SELECT * FROM users ORDER BY created_at DESC LIMIT 10";
    printf("Planning query: %s\n\n", sql);

    result = pgplanner_plan(sql);

    if (result->error_code == PGPLANNER_OK)
    {
        printf("Query Plan:\n");
        printf("----------------------------------------\n");
        printf("%.500s...\n", result->plan_text);  /* Truncate for display */
        printf("----------------------------------------\n\n");
    }
    else
    {
        fprintf(stderr, "Planning failed: %s\n", result->error_message);
    }

    /*
     * Step 6: Cleanup
     */
    pgplanner_free_result(result);
    pgplanner_shutdown();

    printf("Done!\n");
    return 0;
}
