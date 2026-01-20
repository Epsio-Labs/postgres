/*-------------------------------------------------------------------------
 *
 * pgplanner_api.c
 *    Main planning API for the standalone PostgreSQL planner library.
 *
 * This file implements the core planning function that parses SQL,
 * performs semantic analysis, and generates query plans.
 *
 * Copyright (c) 2024, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <string.h>

#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "parser/analyze.h"
#include "parser/parser.h"
#include "rewrite/rewriteHandler.h"
#include "tcop/tcopprot.h"
#include "utils/memutils.h"

#include "pgplanner.h"
#include "pgplanner_query.h"
#include "pgplanner_internal.h"

/*
 * pgplanner_analyze_query
 *    Parse and analyze a SQL query, returning a logical query plan.
 *
 * This is the preferred API for databases that want to implement their
 * own physical planning. It stops after semantic analysis and returns
 * the logical query structure.
 */
PgLogicalQuery *
pgplanner_analyze_query(const char *sql, int *error_code, char **error_message)
{
    PgLogicalQuery *result = NULL;
    MemoryContext analyze_context;
    MemoryContext oldcxt;

    /* Initialize error outputs */
    if (error_code)
        *error_code = PGPLANNER_OK;
    if (error_message)
        *error_message = NULL;

    /* Validate input */
    if (sql == NULL)
    {
        if (error_code)
            *error_code = PGPLANNER_ERROR_PARSE_ERROR;
        if (error_message)
            *error_message = pgplanner_strdup("SQL string is NULL");
        return NULL;
    }

    /* Check initialization */
    if (!pgplanner_is_initialized())
    {
        if (error_code)
            *error_code = PGPLANNER_ERROR_NOT_INITIALIZED;
        if (error_message)
            *error_message = pgplanner_strdup("Library not initialized");
        return NULL;
    }

    /* Acquire lock for thread safety */
    pgplanner_lock();

    /*
     * Create a temporary memory context for parsing/analysis.
     */
    analyze_context = AllocSetContextCreate(TopMemoryContext,
                                            "PgPlanner Analyze Context",
                                            ALLOCSET_DEFAULT_SIZES);
    oldcxt = MemoryContextSwitchTo(analyze_context);

    PG_TRY();
    {
        List *raw_parsetree_list;
        RawStmt *raw_stmt;
        Query *query;
        List *querytree_list;

        /*
         * Step 1: Parse the SQL string
         */
        raw_parsetree_list = raw_parser(sql, RAW_PARSE_DEFAULT);

        if (raw_parsetree_list == NIL)
        {
            ereport(ERROR,
                    (errcode(ERRCODE_SYNTAX_ERROR),
                     errmsg("empty query")));
        }

        if (list_length(raw_parsetree_list) > 1)
        {
            ereport(ERROR,
                    (errcode(ERRCODE_SYNTAX_ERROR),
                     errmsg("multiple statements not supported, got %d",
                            list_length(raw_parsetree_list))));
        }

        raw_stmt = linitial_node(RawStmt, raw_parsetree_list);

        /*
         * Step 2: Perform semantic analysis
         *
         * This transforms the raw parse tree into a Query with:
         * - Resolved table references
         * - Type information
         * - Resolved column references
         */
        query = parse_analyze_fixedparams(raw_stmt, sql, NULL, 0, NULL);

        if (query == NULL)
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("parse analysis returned NULL")));
        }

        /*
         * Step 3: Apply rewrite rules (view expansion, etc.)
         *
         * This handles views, rules, and security policies.
         */
        querytree_list = pg_rewrite_query(query);

        if (querytree_list == NIL)
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("query rewrite returned empty list")));
        }

        query = linitial_node(Query, querytree_list);

        /*
         * Step 4: Convert to FFI-safe logical query structure
         *
         * This is where we STOP - no physical planning.
         * The Query struct contains the logical plan.
         */
        result = pgplanner_convert_query(query);

        if (error_code)
            *error_code = PGPLANNER_OK;
    }
    PG_CATCH();
    {
        /* Capture error information */
        ErrorData *edata;
        int err = PGPLANNER_ERROR_INTERNAL;

        MemoryContextSwitchTo(oldcxt);
        edata = CopyErrorData();

        /* Map SQLSTATE to our error codes */
        if (edata->sqlerrcode == ERRCODE_SYNTAX_ERROR)
            err = PGPLANNER_ERROR_PARSE_ERROR;
        else if (edata->sqlerrcode == ERRCODE_UNDEFINED_TABLE ||
                 edata->sqlerrcode == ERRCODE_UNDEFINED_COLUMN)
            err = PGPLANNER_ERROR_ANALYZE_ERROR;

        if (error_code)
            *error_code = err;
        if (error_message)
            *error_message = pgplanner_strdup(edata->message ? edata->message : "Unknown error");

        FreeErrorData(edata);
        FlushErrorState();

        /* Clean up and return */
        MemoryContextDelete(analyze_context);
        pgplanner_unlock();

        return NULL;
    }
    PG_END_TRY();

    /* Clean up analysis context */
    MemoryContextSwitchTo(oldcxt);
    MemoryContextDelete(analyze_context);

    pgplanner_unlock();

    return result;
}

/*
 * Helper function to create error info from PostgreSQL ErrorData
 */
static PgPlannerErrorInfo *
create_error_info(ErrorData *edata)
{
    PgPlannerErrorInfo *info;
    int err_code = PGPLANNER_ERROR_INTERNAL;

    info = (PgPlannerErrorInfo *) malloc(sizeof(PgPlannerErrorInfo));
    if (info == NULL)
        return NULL;

    memset(info, 0, sizeof(PgPlannerErrorInfo));

    /* Map SQLSTATE to our error codes */
    if (edata->sqlerrcode == ERRCODE_SYNTAX_ERROR)
        err_code = PGPLANNER_ERROR_PARSE_ERROR;
    else if (edata->sqlerrcode == ERRCODE_UNDEFINED_TABLE ||
             edata->sqlerrcode == ERRCODE_UNDEFINED_COLUMN)
        err_code = PGPLANNER_ERROR_ANALYZE_ERROR;

    info->error_code = err_code;
    info->message = edata->message ? strdup(edata->message) : strdup("Unknown error");
    info->detail = edata->detail ? strdup(edata->detail) : NULL;
    info->hint = edata->hint ? strdup(edata->hint) : NULL;
    info->context = edata->context ? strdup(edata->context) : NULL;
    info->position = edata->cursorpos;  /* 1-based position, 0 if unknown */
    info->schema_name = edata->schema_name ? strdup(edata->schema_name) : NULL;
    info->table_name = edata->table_name ? strdup(edata->table_name) : NULL;
    info->column_name = edata->column_name ? strdup(edata->column_name) : NULL;

    return info;
}

/*
 * pgplanner_free_error_info
 *    Free an extended error info structure.
 */
void
pgplanner_free_error_info(PgPlannerErrorInfo *error)
{
    if (error == NULL)
        return;

    if (error->message)
        free(error->message);
    if (error->detail)
        free(error->detail);
    if (error->hint)
        free(error->hint);
    if (error->context)
        free(error->context);
    if (error->schema_name)
        free(error->schema_name);
    if (error->table_name)
        free(error->table_name);
    if (error->column_name)
        free(error->column_name);

    free(error);
}

/*
 * pgplanner_format_error
 *    Format an error message with position context from the query.
 */
char *
pgplanner_format_error(const PgPlannerErrorInfo *error, const char *sql)
{
    char *result;
    size_t result_size;
    int line_num = 1;
    int col_num = 1;
    const char *line_start;
    const char *line_end;
    const char *p;
    int pos;
    size_t line_len;
    int i;
    size_t offset;

    if (error == NULL)
        return strdup("(no error)");

    if (error->position <= 0 || sql == NULL)
    {
        /* No position info, just return the message with optional detail/hint */
        result_size = strlen(error->message) + 256;
        if (error->detail)
            result_size += strlen(error->detail) + 16;
        if (error->hint)
            result_size += strlen(error->hint) + 16;

        result = (char *) malloc(result_size);
        if (result == NULL)
            return NULL;

        offset = sprintf(result, "ERROR: %s", error->message);
        if (error->detail)
            offset += sprintf(result + offset, "\nDETAIL: %s", error->detail);
        if (error->hint)
            offset += sprintf(result + offset, "\nHINT: %s", error->hint);

        return result;
    }

    /* Find the line and column for the error position */
    pos = error->position;  /* 1-based */
    line_start = sql;

    for (p = sql; *p && (p - sql) < pos - 1; p++)
    {
        if (*p == '\n')
        {
            line_num++;
            col_num = 1;
            line_start = p + 1;
        }
        else
        {
            col_num++;
        }
    }

    /* Find end of the line */
    line_end = line_start;
    while (*line_end && *line_end != '\n')
        line_end++;

    line_len = line_end - line_start;

    /* Build the formatted error message */
    result_size = strlen(error->message) + line_len + 256;
    if (error->detail)
        result_size += strlen(error->detail) + 16;
    if (error->hint)
        result_size += strlen(error->hint) + 16;

    result = (char *) malloc(result_size);
    if (result == NULL)
        return NULL;

    /* Format: ERROR: message\nLINE n: <line>\n       ^  */
    offset = sprintf(result, "ERROR: %s\n", error->message);
    offset += sprintf(result + offset, "LINE %d: ", line_num);

    /* Copy the line content */
    memcpy(result + offset, line_start, line_len);
    offset += line_len;
    result[offset++] = '\n';

    /* Add the caret indicator */
    /* Calculate the position of "LINE n: " prefix */
    {
        char prefix[32];
        int prefix_len = sprintf(prefix, "LINE %d: ", line_num);

        /* Add spaces to align the caret */
        for (i = 0; i < prefix_len + col_num - 1; i++)
            result[offset++] = ' ';
        result[offset++] = '^';
    }

    /* Add detail and hint if present */
    if (error->detail)
        offset += sprintf(result + offset, "\nDETAIL: %s", error->detail);
    if (error->hint)
        offset += sprintf(result + offset, "\nHINT: %s", error->hint);

    result[offset] = '\0';

    return result;
}

/*
 * pgplanner_analyze_query_ex
 *    Parse and analyze a SQL query with extended error information.
 */
PgLogicalQuery *
pgplanner_analyze_query_ex(const char *sql, PgPlannerErrorInfo **error)
{
    PgLogicalQuery *result = NULL;
    MemoryContext analyze_context;
    MemoryContext oldcxt;

    /* Initialize error output */
    if (error)
        *error = NULL;

    /* Validate input */
    if (sql == NULL)
    {
        if (error)
        {
            *error = (PgPlannerErrorInfo *) malloc(sizeof(PgPlannerErrorInfo));
            if (*error)
            {
                memset(*error, 0, sizeof(PgPlannerErrorInfo));
                (*error)->error_code = PGPLANNER_ERROR_PARSE_ERROR;
                (*error)->message = strdup("SQL string is NULL");
                (*error)->position = 0;
            }
        }
        return NULL;
    }

    /* Check initialization */
    if (!pgplanner_is_initialized())
    {
        if (error)
        {
            *error = (PgPlannerErrorInfo *) malloc(sizeof(PgPlannerErrorInfo));
            if (*error)
            {
                memset(*error, 0, sizeof(PgPlannerErrorInfo));
                (*error)->error_code = PGPLANNER_ERROR_NOT_INITIALIZED;
                (*error)->message = strdup("Library not initialized");
                (*error)->position = 0;
            }
        }
        return NULL;
    }

    /* Acquire lock for thread safety */
    pgplanner_lock();

    /*
     * Create a temporary memory context for parsing/analysis.
     */
    analyze_context = AllocSetContextCreate(TopMemoryContext,
                                            "PgPlanner Analyze Context Ex",
                                            ALLOCSET_DEFAULT_SIZES);
    oldcxt = MemoryContextSwitchTo(analyze_context);

    PG_TRY();
    {
        List *raw_parsetree_list;
        RawStmt *raw_stmt;
        Query *query;
        List *querytree_list;

        /*
         * Step 1: Parse the SQL string
         */
        raw_parsetree_list = raw_parser(sql, RAW_PARSE_DEFAULT);

        if (raw_parsetree_list == NIL)
        {
            ereport(ERROR,
                    (errcode(ERRCODE_SYNTAX_ERROR),
                     errmsg("empty query")));
        }

        if (list_length(raw_parsetree_list) > 1)
        {
            ereport(ERROR,
                    (errcode(ERRCODE_SYNTAX_ERROR),
                     errmsg("multiple statements not supported, got %d",
                            list_length(raw_parsetree_list))));
        }

        raw_stmt = linitial_node(RawStmt, raw_parsetree_list);

        /*
         * Step 2: Perform semantic analysis
         */
        query = parse_analyze_fixedparams(raw_stmt, sql, NULL, 0, NULL);

        if (query == NULL)
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("parse analysis returned NULL")));
        }

        /*
         * Step 3: Apply rewrite rules
         */
        querytree_list = pg_rewrite_query(query);

        if (querytree_list == NIL)
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("query rewrite returned empty list")));
        }

        query = linitial_node(Query, querytree_list);

        /*
         * Step 4: Convert to FFI-safe logical query structure
         */
        result = pgplanner_convert_query(query);
    }
    PG_CATCH();
    {
        /* Capture error information */
        ErrorData *edata;

        MemoryContextSwitchTo(oldcxt);
        edata = CopyErrorData();

        /* Create extended error info if requested */
        if (error)
            *error = create_error_info(edata);

        FreeErrorData(edata);
        FlushErrorState();

        /* Clean up and return */
        MemoryContextDelete(analyze_context);
        pgplanner_unlock();

        return NULL;
    }
    PG_END_TRY();

    /* Clean up analysis context */
    MemoryContextSwitchTo(oldcxt);
    MemoryContextDelete(analyze_context);

    pgplanner_unlock();

    return result;
}
