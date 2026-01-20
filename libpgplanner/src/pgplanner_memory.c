/*-------------------------------------------------------------------------
 *
 * pgplanner_memory.c
 *    Memory management for the standalone PostgreSQL planner library.
 *
 * This file provides memory allocation functions that are safe to use
 * across FFI boundaries. Memory is allocated in a tracked context to
 * prevent leaks and allow proper cleanup.
 *
 * Copyright (c) 2024, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "utils/memutils.h"
#include "utils/palloc.h"

#include "pgplanner.h"
#include "pgplanner_internal.h"

/*
 * pgplanner_alloc
 *    Allocate memory in the library's memory context.
 *
 * This is safe to use from FFI code. The memory will be properly
 * tracked and can be freed with pgplanner_free().
 */
void *
pgplanner_alloc(size_t size)
{
    void *ptr;
    MemoryContext oldcxt;

    if (!pgplanner_is_initialized())
        return NULL;

    if (PgPlannerContext == NULL)
        return NULL;

    pgplanner_lock();

    oldcxt = MemoryContextSwitchTo(PgPlannerContext);
    ptr = palloc(size);
    MemoryContextSwitchTo(oldcxt);

    pgplanner_unlock();

    return ptr;
}

/*
 * pgplanner_free
 *    Free memory allocated by pgplanner_alloc().
 */
void
pgplanner_free(void *ptr)
{
    if (ptr == NULL)
        return;

    if (!pgplanner_is_initialized())
        return;

    pgplanner_lock();

    /*
     * Note: pfree() can only free memory from the current context tree.
     * We assume the memory was allocated in PgPlannerContext.
     */
    pfree(ptr);

    pgplanner_unlock();
}

/*
 * pgplanner_strdup
 *    Duplicate a string using library memory allocation.
 */
char *
pgplanner_strdup(const char *str)
{
    char *result;
    MemoryContext oldcxt;
    size_t len;

    if (!pgplanner_is_initialized() || str == NULL)
        return NULL;

    if (PgPlannerContext == NULL)
        return NULL;

    pgplanner_lock();

    oldcxt = MemoryContextSwitchTo(PgPlannerContext);

    len = strlen(str) + 1;
    result = palloc(len);
    memcpy(result, str, len);

    MemoryContextSwitchTo(oldcxt);

    pgplanner_unlock();

    return result;
}

/*
 * pgplanner_alloc_zero
 *    Allocate zero-initialized memory.
 */
void *
pgplanner_alloc_zero(size_t size)
{
    void *ptr;
    MemoryContext oldcxt;

    if (!pgplanner_is_initialized())
        return NULL;

    if (PgPlannerContext == NULL)
        return NULL;

    pgplanner_lock();

    oldcxt = MemoryContextSwitchTo(PgPlannerContext);
    ptr = palloc0(size);
    MemoryContextSwitchTo(oldcxt);

    pgplanner_unlock();

    return ptr;
}

/*
 * pgplanner_realloc
 *    Reallocate memory.
 */
void *
pgplanner_realloc(void *ptr, size_t size)
{
    void *newptr;
    MemoryContext oldcxt;

    if (!pgplanner_is_initialized())
        return NULL;

    if (PgPlannerContext == NULL)
        return NULL;

    pgplanner_lock();

    oldcxt = MemoryContextSwitchTo(PgPlannerContext);

    if (ptr == NULL)
        newptr = palloc(size);
    else
        newptr = repalloc(ptr, size);

    MemoryContextSwitchTo(oldcxt);

    pgplanner_unlock();

    return newptr;
}

/*
 * pgplanner_memdup
 *    Duplicate a memory block.
 */
void *
pgplanner_memdup(const void *data, size_t size)
{
    void *result;
    MemoryContext oldcxt;

    if (!pgplanner_is_initialized() || data == NULL || size == 0)
        return NULL;

    if (PgPlannerContext == NULL)
        return NULL;

    pgplanner_lock();

    oldcxt = MemoryContextSwitchTo(PgPlannerContext);

    result = palloc(size);
    memcpy(result, data, size);

    MemoryContextSwitchTo(oldcxt);

    pgplanner_unlock();

    return result;
}
