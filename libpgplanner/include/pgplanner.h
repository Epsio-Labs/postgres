/*-------------------------------------------------------------------------
 *
 * pgplanner.h
 *    Public API for the standalone PostgreSQL planner library.
 *
 * This library exposes the PostgreSQL query planner for use by external
 * databases (e.g., Rust-based databases via FFI) without requiring a
 * running PostgreSQL instance.
 *
 * Copyright (c) 2024, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */
#ifndef PGPLANNER_H
#define PGPLANNER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "pgplanner_query.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * PostgreSQL OID type (unsigned 32-bit integer)
 */
typedef uint32_t PgOid;

#define InvalidPgOid    ((PgOid) 0)

/*
 * Library version
 */
#define PGPLANNER_VERSION_MAJOR 1
#define PGPLANNER_VERSION_MINOR 0
#define PGPLANNER_VERSION_PATCH 0

/*
 * Error codes
 */
typedef enum PgPlannerError
{
    PGPLANNER_OK = 0,
    PGPLANNER_ERROR_NOT_INITIALIZED = 1,
    PGPLANNER_ERROR_ALREADY_INITIALIZED = 2,
    PGPLANNER_ERROR_PARSE_ERROR = 3,
    PGPLANNER_ERROR_ANALYZE_ERROR = 4,
    PGPLANNER_ERROR_PLAN_ERROR = 5,
    PGPLANNER_ERROR_INVALID_TABLE = 6,
    PGPLANNER_ERROR_INVALID_TYPE = 7,
    PGPLANNER_ERROR_OUT_OF_MEMORY = 8,
    PGPLANNER_ERROR_INTERNAL = 99
} PgPlannerError;

/*-------------------------------------------------------------------------
 * Library Lifecycle
 *-------------------------------------------------------------------------
 */

/*
 * Initialize the planner library.
 *
 * This must be called before any other pgplanner_* functions.
 * Can only be called once; call pgplanner_shutdown() first to reinitialize.
 *
 * Returns: PGPLANNER_OK on success, error code otherwise.
 */
int pgplanner_init(void);

/*
 * Shutdown the planner library and free all resources.
 *
 * After calling this, pgplanner_init() can be called again.
 */
void pgplanner_shutdown(void);

/*
 * Check if the library is initialized.
 *
 * Returns: true if initialized, false otherwise.
 */
bool pgplanner_is_initialized(void);

/*
 * Get the library version string.
 *
 * Returns: Static string like "1.0.0"
 */
const char *pgplanner_version(void);

/*-------------------------------------------------------------------------
 * Table Schema Registration
 *-------------------------------------------------------------------------
 */

/*
 * Column definition for table registration.
 */
typedef struct PgColumnDef
{
    const char *name;           /* Column name (required) */
    const char *type_name;      /* Type name: "int4", "text", "timestamp", etc. */
    int32_t     typmod;         /* Type modifier, -1 for none */
    bool        not_null;       /* NOT NULL constraint */
} PgColumnDef;

/*
 * Index definition for table registration.
 */
typedef struct PgIndexDef
{
    const char *name;           /* Index name (required) */
    int         num_columns;    /* Number of columns in index */
    const int  *column_nums;    /* 1-based column numbers */
    const char *access_method;  /* "btree", "hash", "gist", "gin" */
    bool        is_unique;      /* UNIQUE index */
    bool        is_primary;     /* PRIMARY KEY */
} PgIndexDef;

/*
 * Table definition for registration.
 */
typedef struct PgTableDef
{
    const char          *schema_name;   /* Schema name, NULL for "public" */
    const char          *table_name;    /* Table name (required) */
    int                  num_columns;   /* Number of columns */
    const PgColumnDef   *columns;       /* Array of column definitions */
    int                  num_indexes;   /* Number of indexes (can be 0) */
    const PgIndexDef    *indexes;       /* Array of index definitions */
} PgTableDef;

/*
 * Callback function type for table lookup.
 *
 * This callback is invoked when the planner needs to resolve a table that
 * hasn't been pre-registered. The callback should return a PgTableDef
 * describing the table, or NULL if the table doesn't exist.
 *
 * Parameters:
 *   schema_name - Schema name, or NULL for unqualified table references
 *   table_name  - Table name being looked up
 *   user_data   - User-provided context pointer
 *
 * Returns: Pointer to a PgTableDef structure, or NULL if table not found.
 *          The returned PgTableDef and all its contents (strings, arrays)
 *          must remain valid until the planning operation completes.
 *          The caller (library) does NOT free this memory.
 */
typedef const PgTableDef *(*PgTableLookupCallback)(const char *schema_name,
                                                    const char *table_name,
                                                    void *user_data);

/*
 * Set a callback function for table lookup.
 *
 * When the planner encounters a table reference that hasn't been registered,
 * it will call this callback to get the table definition. This allows
 * lazy/on-demand table registration instead of pre-registering all tables.
 *
 * The callback mechanism is useful when:
 * - You don't know all tables in advance
 * - You want to load table definitions on-demand
 * - You're integrating with a database that has dynamic schema
 *
 * Parameters:
 *   callback  - Callback function, or NULL to disable callbacks
 *   user_data - User-provided context pointer passed to callback
 *
 * Note: Only one callback can be active at a time. Setting a new callback
 *       replaces any previously set callback.
 */
void pgplanner_set_table_lookup_callback(PgTableLookupCallback callback,
                                         void *user_data);

/*
 * Get the currently registered table lookup callback.
 *
 * Parameters:
 *   callback  - Output parameter for the callback function (may be NULL)
 *   user_data - Output parameter for the user data (may be NULL)
 */
void pgplanner_get_table_lookup_callback(PgTableLookupCallback *callback,
                                         void **user_data);

/*-------------------------------------------------------------------------
 * Query Analysis
 *-------------------------------------------------------------------------
 */

/*
 * Parse and analyze a SQL query, returning a LOGICAL query plan.
 *
 * This is the PREFERRED API for building database engines that want to
 * implement their own physical planning. It returns a logical representation
 * of the query (what tables to join, what filters to apply, what to output)
 * but NOT how to execute it (no join algorithms, no access methods).
 *
 * The logical plan includes:
 *   - Tables and subqueries referenced (range table)
 *   - Join structure (which tables join to which, join conditions)
 *   - Filter predicates (WHERE clause)
 *   - Output columns (SELECT list)
 *   - Aggregations, grouping, window functions
 *   - Sorting, distinct, limit/offset
 *   - CTEs, set operations (UNION/INTERSECT/EXCEPT)
 *
 * This function is thread-safe (uses internal mutex).
 *
 * Parameters:
 *   sql - SQL query string
 *   error_code - Output parameter for error code (may be NULL)
 *   error_message - Output parameter for error message (may be NULL)
 *                   Caller must free with pgplanner_free() if non-NULL.
 *
 * Returns: PgLogicalQuery structure, or NULL on error.
 *          Caller must free with pgquery_free().
 */
PgLogicalQuery *pgplanner_analyze_query(const char *sql, int *error_code, char **error_message);

/*-------------------------------------------------------------------------
 * Error Handling
 *-------------------------------------------------------------------------
 */

/*
 * Extended error information structure.
 *
 * Contains detailed error information including position in the query
 * where the error occurred, which is useful for generating user-friendly
 * error messages with context.
 */
typedef struct PgPlannerErrorInfo
{
    int         error_code;     /* PgPlannerError code */
    char       *message;        /* Primary error message (required) */
    char       *detail;         /* Detailed error explanation (may be NULL) */
    char       *hint;           /* Hint for fixing the error (may be NULL) */
    char       *context;        /* Additional context (may be NULL) */
    int         position;       /* Character position in SQL (1-based), 0 if unknown */
    char       *schema_name;    /* Schema name if relevant (may be NULL) */
    char       *table_name;     /* Table name if relevant (may be NULL) */
    char       *column_name;    /* Column name if relevant (may be NULL) */
} PgPlannerErrorInfo;

/*
 * Parse and analyze a SQL query with extended error information.
 *
 * Same as pgplanner_analyze_query() but returns detailed error information
 * including the position in the SQL where the error occurred.
 *
 * Parameters:
 *   sql - SQL query string
 *   error - Output parameter for extended error info (may be NULL).
 *           If non-NULL and an error occurs, *error will be set to
 *           a newly allocated PgPlannerErrorInfo. Caller must free
 *           with pgplanner_free_error_info().
 *           If no error, *error will be set to NULL.
 *
 * Returns: PgLogicalQuery structure, or NULL on error.
 *          Caller must free with pgquery_free().
 */
PgLogicalQuery *pgplanner_analyze_query_ex(const char *sql, PgPlannerErrorInfo **error);

/*
 * Free an error info structure.
 *
 * Parameters:
 *   error - Error info structure from pgplanner_analyze_query_ex(). May be NULL.
 */
void pgplanner_free_error_info(PgPlannerErrorInfo *error);

/*
 * Format an error message with context from the query string.
 *
 * Creates a human-readable error message that shows where in the query
 * the error occurred, similar to psql output:
 *
 *   ERROR: column "foo" does not exist
 *   LINE 1: SELECT foo FROM bar
 *                  ^
 *
 * Parameters:
 *   error - Error info structure
 *   sql   - The original SQL query string
 *
 * Returns: Formatted error string. Caller must free with pgplanner_free().
 */
char *pgplanner_format_error(const PgPlannerErrorInfo *error, const char *sql);

/*
 * Get the last error message.
 *
 * Returns: Static error message string, or NULL if no error.
 *          The string is valid until the next pgplanner_* call.
 */
const char *pgplanner_get_last_error(void);

/*
 * Get error code name as string.
 *
 * Parameters:
 *   error_code - Error code from PgPlannerError enum
 *
 * Returns: Static string like "PGPLANNER_ERROR_PARSE_ERROR"
 */
const char *pgplanner_error_name(int error_code);

/*-------------------------------------------------------------------------
 * Memory Management
 *-------------------------------------------------------------------------
 */

/*
 * Allocate memory that will be managed by the library.
 *
 * Useful for creating structures to pass to the library.
 * Memory is allocated in a tracked context for safety.
 *
 * Parameters:
 *   size - Number of bytes to allocate
 *
 * Returns: Pointer to allocated memory, or NULL on failure.
 */
void *pgplanner_alloc(size_t size);

/*
 * Free memory allocated by pgplanner_alloc().
 *
 * Parameters:
 *   ptr - Pointer from pgplanner_alloc(). May be NULL.
 */
void pgplanner_free(void *ptr);

/*
 * Duplicate a string using library memory allocation.
 *
 * Parameters:
 *   str - String to duplicate
 *
 * Returns: Duplicated string, or NULL on failure.
 */
char *pgplanner_strdup(const char *str);

/*-------------------------------------------------------------------------
 * Utility Functions
 *-------------------------------------------------------------------------
 */

/*
 * Get supported type names.
 *
 * Returns: NULL-terminated array of type name strings.
 *          Do not free (static data).
 */
const char **pgplanner_list_types(void);

/*
 * Check if a type name is supported.
 *
 * Parameters:
 *   type_name - Type name to check
 *
 * Returns: true if supported, false otherwise.
 */
bool pgplanner_type_exists(const char *type_name);

#ifdef __cplusplus
}
#endif

#endif /* PGPLANNER_H */
