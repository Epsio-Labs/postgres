/*-------------------------------------------------------------------------
 *
 * pgplanner_init.c
 *    Library initialization for the standalone PostgreSQL planner.
 *
 * This file implements the initialization sequence required to use the
 * PostgreSQL planner without a running PostgreSQL instance.
 *
 * Copyright (c) 2024, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <pthread.h>

#include "access/xact.h"
#include "catalog/namespace.h"
#include "catalog/pg_collation.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "nodes/memnodes.h"
#include "storage/ipc.h"
#include "storage/proc.h"
#include "utils/guc.h"
#include "utils/guc_tables.h"
#include "utils/plancache.h"
#include "utils/memutils.h"
#include "utils/palloc.h"
#include "utils/syscache.h"
#include "utils/catcache.h"

#include "pgplanner.h"
#include "pgplanner_internal.h"

/* Mutex for thread safety */
static pthread_mutex_t planner_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Initialization state */
static bool pgplanner_initialized = false;

/* Last error message */
static char last_error_message[1024] = {0};

/* Memory context for user-facing allocations */
MemoryContext PgPlannerContext = NULL;

/* Forward declarations */
static void pgplanner_init_memory(void);
static void pgplanner_init_guc(void);
static void pgplanner_init_caches(void);
static void pgplanner_init_misc(void);

/*
 * pgplanner_init
 *    Initialize the planner library.
 */
int
pgplanner_init(void)
{
    pthread_mutex_lock(&planner_mutex);

    if (pgplanner_initialized)
    {
        pthread_mutex_unlock(&planner_mutex);
        snprintf(last_error_message, sizeof(last_error_message),
                 "Library already initialized");
        return PGPLANNER_ERROR_ALREADY_INITIALIZED;
    }

    PG_TRY();
    {
        /*
         * Step 1: Initialize memory context system
         * This creates TopMemoryContext and ErrorContext
         */
        pgplanner_init_memory();

        /*
         * Step 2: Initialize GUC (Grand Unified Configuration)
         * Sets up planner configuration variables
         */
        pgplanner_init_guc();

        /*
         * Step 3: Initialize caches
         * Sets up relation cache and system catalog cache structures
         */
        pgplanner_init_caches();

        /*
         * Step 4: Initialize miscellaneous subsystems
         */
        pgplanner_init_misc();

        /*
         * Step 5: Populate built-in types, operators, and functions
         * These are needed for the planner to understand expressions
         */
        pgplanner_init_types();
        pgplanner_init_operators();
        pgplanner_init_functions();

        pgplanner_initialized = true;
    }
    PG_CATCH();
    {
        /* Capture error message */
        ErrorData *edata = CopyErrorData();
        snprintf(last_error_message, sizeof(last_error_message),
                 "Initialization failed: %s", edata->message);
        FreeErrorData(edata);
        FlushErrorState();

        pthread_mutex_unlock(&planner_mutex);
        return PGPLANNER_ERROR_INTERNAL;
    }
    PG_END_TRY();

    pthread_mutex_unlock(&planner_mutex);
    return PGPLANNER_OK;
}

/*
 * pgplanner_shutdown
 *    Shutdown the planner library and free all resources.
 */
void
pgplanner_shutdown(void)
{
    pthread_mutex_lock(&planner_mutex);

    if (!pgplanner_initialized)
    {
        pthread_mutex_unlock(&planner_mutex);
        return;
    }

    /*
     * Clean up in reverse order of initialization
     */

    /* Clean up our custom context */
    if (PgPlannerContext != NULL)
    {
        MemoryContextDelete(PgPlannerContext);
        PgPlannerContext = NULL;
    }

    /* Clean up registered relations */
    pgplanner_cleanup_relations();

    /*
     * Note: We don't clean up TopMemoryContext because some code
     * paths may still reference it. In practice, this library is
     * typically loaded once per process.
     */

    pgplanner_initialized = false;
    last_error_message[0] = '\0';

    pthread_mutex_unlock(&planner_mutex);
}

/*
 * pgplanner_is_initialized
 *    Check if the library is initialized.
 */
bool
pgplanner_is_initialized(void)
{
    bool result;
    pthread_mutex_lock(&planner_mutex);
    result = pgplanner_initialized;
    pthread_mutex_unlock(&planner_mutex);
    return result;
}

/*
 * pgplanner_version
 *    Get the library version string.
 */
const char *
pgplanner_version(void)
{
    static char version[32];
    snprintf(version, sizeof(version), "%d.%d.%d",
             PGPLANNER_VERSION_MAJOR,
             PGPLANNER_VERSION_MINOR,
             PGPLANNER_VERSION_PATCH);
    return version;
}

/*
 * pgplanner_get_last_error
 *    Get the last error message.
 */
const char *
pgplanner_get_last_error(void)
{
    if (last_error_message[0] == '\0')
        return NULL;
    return last_error_message;
}

/*
 * pgplanner_set_error
 *    Set the last error message (internal use).
 */
void
pgplanner_set_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(last_error_message, sizeof(last_error_message), fmt, args);
    va_end(args);
}

/*
 * pgplanner_error_name
 *    Get error code name as string.
 */
const char *
pgplanner_error_name(int error_code)
{
    switch (error_code)
    {
        case PGPLANNER_OK:
            return "PGPLANNER_OK";
        case PGPLANNER_ERROR_NOT_INITIALIZED:
            return "PGPLANNER_ERROR_NOT_INITIALIZED";
        case PGPLANNER_ERROR_ALREADY_INITIALIZED:
            return "PGPLANNER_ERROR_ALREADY_INITIALIZED";
        case PGPLANNER_ERROR_PARSE_ERROR:
            return "PGPLANNER_ERROR_PARSE_ERROR";
        case PGPLANNER_ERROR_ANALYZE_ERROR:
            return "PGPLANNER_ERROR_ANALYZE_ERROR";
        case PGPLANNER_ERROR_PLAN_ERROR:
            return "PGPLANNER_ERROR_PLAN_ERROR";
        case PGPLANNER_ERROR_INVALID_TABLE:
            return "PGPLANNER_ERROR_INVALID_TABLE";
        case PGPLANNER_ERROR_INVALID_TYPE:
            return "PGPLANNER_ERROR_INVALID_TYPE";
        case PGPLANNER_ERROR_OUT_OF_MEMORY:
            return "PGPLANNER_ERROR_OUT_OF_MEMORY";
        case PGPLANNER_ERROR_INTERNAL:
            return "PGPLANNER_ERROR_INTERNAL";
        default:
            return "PGPLANNER_ERROR_UNKNOWN";
    }
}

/*
 * pgplanner_lock
 *    Acquire the global planner lock (for internal use).
 */
void
pgplanner_lock(void)
{
    pthread_mutex_lock(&planner_mutex);
}

/*
 * pgplanner_unlock
 *    Release the global planner lock (for internal use).
 */
void
pgplanner_unlock(void)
{
    pthread_mutex_unlock(&planner_mutex);
}

/*-------------------------------------------------------------------------
 * Internal initialization functions
 *-------------------------------------------------------------------------
 */

/*
 * Initialize memory context system
 */
static void
pgplanner_init_memory(void)
{
    /*
     * Initialize the memory context subsystem.
     * This creates TopMemoryContext and ErrorContext.
     */
    if (TopMemoryContext == NULL)
        MemoryContextInit();

    /*
     * Create CacheMemoryContext for relation and catalog caches.
     * This is normally done by CreateCacheMemoryContext() but we
     * do it explicitly here.
     */
    if (CacheMemoryContext == NULL)
    {
        CacheMemoryContext = AllocSetContextCreate(TopMemoryContext,
                                                   "CacheMemoryContext",
                                                   ALLOCSET_DEFAULT_SIZES);
    }

    /*
     * Create our own context for user-facing allocations
     */
    PgPlannerContext = AllocSetContextCreate(TopMemoryContext,
                                             "PgPlannerContext",
                                             ALLOCSET_DEFAULT_SIZES);
}

/*
 * Initialize GUC configuration system
 */
static void
pgplanner_init_guc(void)
{
    /*
     * Build GUC variable tables
     */
    build_guc_variables();

    /*
     * Set essential planner GUCs to reasonable defaults
     */
    SetConfigOption("work_mem", "4MB", PGC_USERSET, PGC_S_OVERRIDE);
    SetConfigOption("effective_cache_size", "4GB", PGC_USERSET, PGC_S_OVERRIDE);
    SetConfigOption("random_page_cost", "4.0", PGC_USERSET, PGC_S_OVERRIDE);
    SetConfigOption("seq_page_cost", "1.0", PGC_USERSET, PGC_S_OVERRIDE);
    SetConfigOption("cpu_tuple_cost", "0.01", PGC_USERSET, PGC_S_OVERRIDE);
    SetConfigOption("cpu_index_tuple_cost", "0.005", PGC_USERSET, PGC_S_OVERRIDE);
    SetConfigOption("cpu_operator_cost", "0.0025", PGC_USERSET, PGC_S_OVERRIDE);

    /* Enable all join methods */
    SetConfigOption("enable_hashjoin", "on", PGC_USERSET, PGC_S_OVERRIDE);
    SetConfigOption("enable_mergejoin", "on", PGC_USERSET, PGC_S_OVERRIDE);
    SetConfigOption("enable_nestloop", "on", PGC_USERSET, PGC_S_OVERRIDE);

    /* Enable all scan methods */
    SetConfigOption("enable_seqscan", "on", PGC_USERSET, PGC_S_OVERRIDE);
    SetConfigOption("enable_indexscan", "on", PGC_USERSET, PGC_S_OVERRIDE);
    SetConfigOption("enable_indexonlyscan", "on", PGC_USERSET, PGC_S_OVERRIDE);
    SetConfigOption("enable_bitmapscan", "on", PGC_USERSET, PGC_S_OVERRIDE);

    /* Other planner settings */
    SetConfigOption("enable_sort", "on", PGC_USERSET, PGC_S_OVERRIDE);
    SetConfigOption("enable_hashagg", "on", PGC_USERSET, PGC_S_OVERRIDE);
    SetConfigOption("enable_material", "on", PGC_USERSET, PGC_S_OVERRIDE);

    /* Disable parallel query (not supported in standalone mode) */
    SetConfigOption("max_parallel_workers_per_gather", "0", PGC_USERSET, PGC_S_OVERRIDE);
}

/*
 * Initialize cache systems
 */
static void
pgplanner_init_caches(void)
{
    /*
     * Initialize the relation cache hash table.
     * This doesn't read any catalog data, just sets up the structure.
     */
    RelationCacheInitialize();

    /*
     * Initialize the system catalog cache.
     * Again, this just creates the cache structures without reading data.
     */
    InitCatalogCache();

    /*
     * Initialize the plan cache (for prepared statements).
     * We don't really need this but it registers invalidation callbacks.
     */
    InitPlanCache();
}

/*
 * Initialize miscellaneous subsystems
 */
static void
pgplanner_init_misc(void)
{
    /*
     * Set up standalone mode flags.
     * This disables features that require a running server.
     */
    IsUnderPostmaster = false;

    /*
     * Initialize client encoding (for error messages, etc.)
     */
    SetDatabaseEncoding(PG_UTF8);

    /*
     * Set default namespace search path
     * We'll use a simple path: public, pg_catalog
     */
    pgplanner_init_namespace();
}
