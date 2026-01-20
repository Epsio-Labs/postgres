/*-------------------------------------------------------------------------
 *
 * pgplanner_relations.c
 *    Relation (table/index) registration for the standalone PostgreSQL planner.
 *
 * This file implements the ability to register table schemas with the planner.
 * It follows the pattern from PostgreSQL's formrdesc() function to create
 * fake relation entries that the planner can use.
 *
 * Copyright (c) 2024, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/heapam.h"
#include "access/htup_details.h"
#include "access/multixact.h"
#include "access/tableam.h"
#include "access/tupdesc.h"
#include "catalog/heap.h"
#include "catalog/index.h"
#include "catalog/namespace.h"
#include "catalog/pg_am.h"
#include "catalog/pg_authid_d.h"
#include "catalog/pg_class.h"
#include "catalog/pg_index.h"
#include "catalog/pg_namespace.h"
#include "storage/lmgr.h"
#include "storage/backendid.h"
#include "utils/builtins.h"
#include "utils/hsearch.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/relcache.h"
#include "utils/syscache.h"

#include "pgplanner.h"
#include "pgplanner_internal.h"

/* Next available OID for user objects */
static Oid NextUserOid = PGPLANNER_FIRST_USER_OID;

/* Hash table for table name lookup */
static HTAB *TableNameHash = NULL;

typedef struct TableNameKey
{
    char schema_name[NAMEDATALEN];
    char table_name[NAMEDATALEN];
} TableNameKey;

typedef struct TableNameEntry
{
    TableNameKey key;
    Oid table_oid;
} TableNameEntry;

/* List of registered table OIDs for cleanup */
static List *RegisteredTableOids = NIL;

/* Hash table to store relations by OID (non-static for stubs access) */
HTAB *RelationOidHash = NULL;

/* Table lookup callback */
static PgTableLookupCallback TableLookupCallback = NULL;
static void *TableLookupUserData = NULL;

typedef struct RelationOidEntry
{
    Oid key;
    Relation relation;
} RelationOidEntry;

/* Forward declarations */
static void pgplanner_register_index_internal(Oid table_oid, Relation table_rel,
                                              const PgIndexDef *index_def);
static void ensure_relation_hash(void);
static void pgplanner_relation_cache_insert(Relation relation);

/*
 * pgplanner_alloc_oid
 *    Allocate a new OID for a user object.
 */
Oid
pgplanner_alloc_oid(void)
{
    return NextUserOid++;
}

/*
 * Create the table name hash if needed
 */
static void
ensure_table_name_hash(void)
{
    if (TableNameHash == NULL)
    {
        HASHCTL hashctl;

        memset(&hashctl, 0, sizeof(hashctl));
        hashctl.keysize = sizeof(TableNameKey);
        hashctl.entrysize = sizeof(TableNameEntry);
        hashctl.hcxt = CacheMemoryContext;

        TableNameHash = hash_create("PgPlanner Table Name Hash",
                                    64,
                                    &hashctl,
                                    HASH_ELEM | HASH_BLOBS | HASH_CONTEXT);
    }
}

/*
 * Build a TupleDesc from column definitions
 */
static TupleDesc
build_tuple_desc(int num_columns, const PgColumnDef *columns, Oid relid)
{
    TupleDesc tupdesc;
    int i;
    bool has_not_null = false;

    tupdesc = CreateTemplateTupleDesc(num_columns);

    for (i = 0; i < num_columns; i++)
    {
        const PgColumnDef *col = &columns[i];
        Form_pg_attribute attr = TupleDescAttr(tupdesc, i);
        const PgTypeInfo *typeinfo;

        /* Look up the type */
        typeinfo = pgplanner_get_type_info_by_name(col->type_name);
        if (typeinfo == NULL)
        {
            pgplanner_set_error("Unknown type: %s", col->type_name);
            pfree(tupdesc);
            return NULL;
        }

        /* Fill in the attribute */
        memset(attr, 0, ATTRIBUTE_FIXED_PART_SIZE);

        attr->attrelid = relid;
        attr->attnum = i + 1;
        namestrcpy(&attr->attname, col->name);
        attr->atttypid = typeinfo->typid;
        attr->attlen = typeinfo->typlen;
        attr->atttypmod = col->typmod;
        attr->attbyval = typeinfo->typbyval;
        attr->attalign = typeinfo->typalign;
        attr->attstorage = typeinfo->typstorage;
        attr->attnotnull = col->not_null;
        attr->atthasdef = false;
        attr->atthasmissing = false;
        attr->attidentity = '\0';
        attr->attgenerated = '\0';
        attr->attisdropped = false;
        attr->attislocal = true;
        attr->attinhcount = 0;
        attr->attcollation = typeinfo->typcollation;

        if (col->not_null)
            has_not_null = true;
    }

    /* Initialize first attribute's cache offset */
    if (num_columns > 0)
        TupleDescAttr(tupdesc, 0)->attcacheoff = 0;

    /* Set up NOT NULL constraint info if needed */
    if (has_not_null)
    {
        TupleConstr *constr = (TupleConstr *) palloc0(sizeof(TupleConstr));
        constr->has_not_null = true;
        tupdesc->constr = constr;
    }

    return tupdesc;
}

/*
 * Create a Relation structure for a table
 */
static Relation
create_table_relation(const char *relname, Oid relnamespace, Oid relid,
                      TupleDesc tupdesc)
{
    Relation relation;
    MemoryContext oldcxt;

    oldcxt = MemoryContextSwitchTo(CacheMemoryContext);

    /* Allocate the RelationData structure */
    relation = (Relation) palloc0(sizeof(RelationData));

    /* Basic setup */
    relation->rd_smgr = NULL;
    relation->rd_refcnt = 1;
    relation->rd_isnailed = true;       /* Keep in cache */
    relation->rd_isvalid = true;
    relation->rd_indexvalid = true;
    relation->rd_statvalid = false;

    relation->rd_createSubid = InvalidSubTransactionId;
    relation->rd_newRelfilelocatorSubid = InvalidSubTransactionId;
    relation->rd_firstRelfilelocatorSubid = InvalidSubTransactionId;
    relation->rd_droppedSubid = InvalidSubTransactionId;
    relation->rd_backend = InvalidBackendId;
    relation->rd_islocaltemp = false;

    /* Create the pg_class tuple data */
    relation->rd_rel = (Form_pg_class) palloc0(CLASS_TUPLE_SIZE);
    namestrcpy(&relation->rd_rel->relname, relname);
    relation->rd_rel->relnamespace = relnamespace;
    relation->rd_rel->reltype = InvalidOid;     /* No row type */
    relation->rd_rel->reloftype = InvalidOid;
    relation->rd_rel->relowner = BOOTSTRAP_SUPERUSERID;
    relation->rd_rel->relam = HEAP_TABLE_AM_OID;
    relation->rd_rel->relfilenode = InvalidOid;
    relation->rd_rel->reltablespace = InvalidOid;
    relation->rd_rel->relpages = 10;            /* Default estimate */
    relation->rd_rel->reltuples = 1000;         /* Default estimate */
    relation->rd_rel->relallvisible = 0;
    relation->rd_rel->reltoastrelid = InvalidOid;
    relation->rd_rel->relhasindex = false;      /* Will update if indexes added */
    relation->rd_rel->relisshared = false;
    relation->rd_rel->relpersistence = RELPERSISTENCE_PERMANENT;
    relation->rd_rel->relkind = RELKIND_RELATION;
    relation->rd_rel->relnatts = tupdesc->natts;
    relation->rd_rel->relchecks = 0;
    relation->rd_rel->relhasrules = false;
    relation->rd_rel->relhastriggers = false;
    relation->rd_rel->relhassubclass = false;
    relation->rd_rel->relrowsecurity = false;
    relation->rd_rel->relforcerowsecurity = false;
    relation->rd_rel->relispopulated = true;
    relation->rd_rel->relreplident = REPLICA_IDENTITY_NOTHING;
    relation->rd_rel->relispartition = false;
    relation->rd_rel->relrewrite = InvalidOid;
    relation->rd_rel->relfrozenxid = InvalidTransactionId;
    relation->rd_rel->relminmxid = InvalidMultiXactId;

    /* Set the tuple descriptor */
    relation->rd_att = tupdesc;
    tupdesc->tdrefcount = 1;
    tupdesc->tdtypeid = InvalidOid;
    tupdesc->tdtypmod = -1;

    /* Set the relation OID */
    RelationGetRelid(relation) = relid;

    /* Initialize lock info */
    relation->rd_lockInfo.lockRelId.relId = relid;
    relation->rd_lockInfo.lockRelId.dbId = InvalidOid;

    /* Initialize index list (empty for now) */
    relation->rd_indexlist = NIL;
    relation->rd_pkindex = InvalidOid;
    relation->rd_replidindex = InvalidOid;

    /* No foreign keys, constraints, etc. */
    relation->rd_fkeylist = NIL;
    relation->rd_fkeyvalid = true;

    /* Table access method */
    relation->rd_tableam = GetHeapamTableAmRoutine();

    MemoryContextSwitchTo(oldcxt);

    return relation;
}

/*
 * pgplanner_register_table
 *    Register a table schema with the planner (internal use only).
 */
static PgOid
pgplanner_register_table(const PgTableDef *table)
{
    Oid table_oid;
    Oid namespace_oid;
    TupleDesc tupdesc;
    Relation relation;
    TableNameEntry *name_entry;
    TableNameKey name_key;
    bool found;
    MemoryContext oldcxt;

    if (!pgplanner_is_initialized())
    {
        pgplanner_set_error("Library not initialized");
        return InvalidPgOid;
    }

    if (table == NULL || table->table_name == NULL)
    {
        pgplanner_set_error("Invalid table definition");
        return InvalidPgOid;
    }

    if (table->num_columns <= 0 || table->columns == NULL)
    {
        pgplanner_set_error("Table must have at least one column");
        return InvalidPgOid;
    }

    pgplanner_lock();

    oldcxt = MemoryContextSwitchTo(CacheMemoryContext);

    /* Get or create namespace */
    if (table->schema_name == NULL || table->schema_name[0] == '\0')
        namespace_oid = pgplanner_get_public_namespace();
    else
        namespace_oid = pgplanner_create_namespace(table->schema_name, false);

    /* Allocate OID for the table */
    table_oid = pgplanner_alloc_oid();

    /* Build the tuple descriptor */
    tupdesc = build_tuple_desc(table->num_columns, table->columns, table_oid);
    if (tupdesc == NULL)
    {
        MemoryContextSwitchTo(oldcxt);
        pgplanner_unlock();
        return InvalidPgOid;
    }

    /* Create the relation structure */
    relation = create_table_relation(table->table_name, namespace_oid,
                                     table_oid, tupdesc);

    /* Insert into our relation cache */
    pgplanner_relation_cache_insert(relation);

    /* Register in name hash for lookup */
    ensure_table_name_hash();
    memset(&name_key, 0, sizeof(name_key));
    if (table->schema_name)
        strlcpy(name_key.schema_name, table->schema_name, NAMEDATALEN);
    else
        strlcpy(name_key.schema_name, "public", NAMEDATALEN);
    strlcpy(name_key.table_name, table->table_name, NAMEDATALEN);

    name_entry = hash_search(TableNameHash, &name_key, HASH_ENTER, &found);
    if (!found)
        name_entry->table_oid = table_oid;

    /* Track for cleanup */
    RegisteredTableOids = lappend_oid(RegisteredTableOids, table_oid);

    /* Register indexes if any */
    if (table->num_indexes > 0 && table->indexes != NULL)
    {
        int i;
        for (i = 0; i < table->num_indexes; i++)
        {
            pgplanner_register_index_internal(table_oid, relation,
                                              &table->indexes[i]);
        }
        /* Update relation to indicate it has indexes */
        relation->rd_rel->relhasindex = true;
    }

    MemoryContextSwitchTo(oldcxt);
    pgplanner_unlock();

    return table_oid;
}

/*
 * Ensure the relation OID hash table is initialized
 */
static void
ensure_relation_hash(void)
{
    if (RelationOidHash == NULL)
    {
        HASHCTL hashctl;

        memset(&hashctl, 0, sizeof(hashctl));
        hashctl.keysize = sizeof(Oid);
        hashctl.entrysize = sizeof(RelationOidEntry);
        hashctl.hcxt = CacheMemoryContext;

        RelationOidHash = hash_create("PgPlanner Relation OID Hash",
                                      128,
                                      &hashctl,
                                      HASH_ELEM | HASH_BLOBS | HASH_CONTEXT);
    }
}

/*
 * Insert a relation into our cache
 */
static void
pgplanner_relation_cache_insert(Relation relation)
{
    RelationOidEntry *entry;
    bool found;

    ensure_relation_hash();

    entry = hash_search(RelationOidHash, &relation->rd_id, HASH_ENTER, &found);
    entry->relation = relation;
}

/*
 * Register an index for a table
 */
static void
pgplanner_register_index_internal(Oid table_oid, Relation table_rel,
                                  const PgIndexDef *index_def)
{
    Oid index_oid;
    Relation index_rel;
    TupleDesc index_tupdesc;
    int i;
    MemoryContext oldcxt;

    oldcxt = MemoryContextSwitchTo(CacheMemoryContext);

    /* Allocate OID for the index */
    index_oid = pgplanner_alloc_oid();

    /* Build a minimal tuple descriptor for the index */
    index_tupdesc = CreateTemplateTupleDesc(index_def->num_columns);

    for (i = 0; i < index_def->num_columns; i++)
    {
        int col_num = index_def->column_nums[i];
        Form_pg_attribute src_attr;
        Form_pg_attribute dst_attr;

        if (col_num < 1 || col_num > table_rel->rd_att->natts)
        {
            elog(WARNING, "Invalid column number %d for index %s",
                 col_num, index_def->name);
            continue;
        }

        src_attr = TupleDescAttr(table_rel->rd_att, col_num - 1);
        dst_attr = TupleDescAttr(index_tupdesc, i);

        memcpy(dst_attr, src_attr, ATTRIBUTE_FIXED_PART_SIZE);
        dst_attr->attrelid = index_oid;
        dst_attr->attnum = i + 1;
    }

    /* Create the index relation structure */
    index_rel = (Relation) palloc0(sizeof(RelationData));
    index_rel->rd_smgr = NULL;
    index_rel->rd_refcnt = 1;
    index_rel->rd_isnailed = true;
    index_rel->rd_isvalid = true;
    index_rel->rd_indexvalid = true;

    index_rel->rd_rel = (Form_pg_class) palloc0(CLASS_TUPLE_SIZE);
    namestrcpy(&index_rel->rd_rel->relname, index_def->name);
    index_rel->rd_rel->relnamespace = table_rel->rd_rel->relnamespace;
    index_rel->rd_rel->relkind = RELKIND_INDEX;
    index_rel->rd_rel->relnatts = index_def->num_columns;
    index_rel->rd_rel->relpages = 5;    /* Estimate */
    index_rel->rd_rel->reltuples = table_rel->rd_rel->reltuples;

    /* Determine access method */
    if (index_def->access_method == NULL ||
        strcmp(index_def->access_method, "btree") == 0)
    {
        index_rel->rd_rel->relam = BTREE_AM_OID;
    }
    else if (strcmp(index_def->access_method, "hash") == 0)
    {
        index_rel->rd_rel->relam = HASH_AM_OID;
    }
    else
    {
        index_rel->rd_rel->relam = BTREE_AM_OID;  /* Default */
    }

    index_rel->rd_att = index_tupdesc;
    index_tupdesc->tdrefcount = 1;

    RelationGetRelid(index_rel) = index_oid;

    index_rel->rd_lockInfo.lockRelId.relId = index_oid;
    index_rel->rd_lockInfo.lockRelId.dbId = InvalidOid;

    /* Set index relation pointer to table */
    index_rel->rd_index = (Form_pg_index) palloc0(sizeof(FormData_pg_index));
    index_rel->rd_index->indexrelid = index_oid;
    index_rel->rd_index->indrelid = table_oid;
    index_rel->rd_index->indnatts = index_def->num_columns;
    index_rel->rd_index->indnkeyatts = index_def->num_columns;
    index_rel->rd_index->indisunique = index_def->is_unique;
    index_rel->rd_index->indisprimary = index_def->is_primary;
    index_rel->rd_index->indisexclusion = false;
    index_rel->rd_index->indimmediate = true;
    index_rel->rd_index->indisclustered = false;
    index_rel->rd_index->indisvalid = true;
    index_rel->rd_index->indcheckxmin = false;
    index_rel->rd_index->indisready = true;
    index_rel->rd_index->indislive = true;

    /* Set up indkey (attribute numbers) */
    for (i = 0; i < index_def->num_columns && i < INDEX_MAX_KEYS; i++)
    {
        index_rel->rd_index->indkey.values[i] = index_def->column_nums[i];
    }

    /* Insert index into our relation cache */
    pgplanner_relation_cache_insert(index_rel);

    /* Add to table's index list */
    table_rel->rd_indexlist = lappend_oid(table_rel->rd_indexlist, index_oid);

    /* Track primary key */
    if (index_def->is_primary)
        table_rel->rd_pkindex = index_oid;

    MemoryContextSwitchTo(oldcxt);
}

/*
 * pgplanner_find_table
 *    Find a table OID by name (internal use).
 */
PgOid
pgplanner_find_table(const char *schema_name, const char *table_name)
{
    TableNameKey name_key;
    TableNameEntry *entry;

    if (!pgplanner_is_initialized() || table_name == NULL)
        return InvalidPgOid;

    if (TableNameHash == NULL)
        return InvalidPgOid;

    memset(&name_key, 0, sizeof(name_key));
    if (schema_name)
        strlcpy(name_key.schema_name, schema_name, NAMEDATALEN);
    else
        strlcpy(name_key.schema_name, "public", NAMEDATALEN);
    strlcpy(name_key.table_name, table_name, NAMEDATALEN);

    entry = hash_search(TableNameHash, &name_key, HASH_FIND, NULL);
    if (entry == NULL)
        return InvalidPgOid;

    return entry->table_oid;
}

/*
 * pgplanner_set_table_lookup_callback
 *    Set a callback function for table lookup.
 */
void
pgplanner_set_table_lookup_callback(PgTableLookupCallback callback,
                                    void *user_data)
{
    TableLookupCallback = callback;
    TableLookupUserData = user_data;
}

/*
 * pgplanner_get_table_lookup_callback
 *    Get the currently registered table lookup callback.
 */
void
pgplanner_get_table_lookup_callback(PgTableLookupCallback *callback,
                                    void **user_data)
{
    if (callback)
        *callback = TableLookupCallback;
    if (user_data)
        *user_data = TableLookupUserData;
}

/*
 * pgplanner_lookup_table_via_callback
 *    Try to look up a table using the registered callback.
 *
 * Returns the table OID if the callback provides a definition,
 * or InvalidOid if no callback is registered or the callback returns NULL.
 */
PgOid
pgplanner_lookup_table_via_callback(const char *schema_name,
                                    const char *table_name)
{
    const PgTableDef *table_def;
    PgOid oid;

    if (TableLookupCallback == NULL)
        return InvalidPgOid;

    table_def = TableLookupCallback(schema_name, table_name, TableLookupUserData);
    if (table_def == NULL)
        return InvalidPgOid;

    /* Register the table definition */
    oid = pgplanner_register_table(table_def);

    return oid;
}

/*
 * pgplanner_cleanup_relations
 *    Clean up all registered relations (called at shutdown).
 */
void
pgplanner_cleanup_relations(void)
{
    /* Clear tracked list */
    if (RegisteredTableOids != NIL)
    {
        list_free(RegisteredTableOids);
        RegisteredTableOids = NIL;
    }

    /* Clear name hash */
    if (TableNameHash != NULL)
    {
        hash_destroy(TableNameHash);
        TableNameHash = NULL;
    }

    /* Clear relation OID hash */
    if (RelationOidHash != NULL)
    {
        hash_destroy(RelationOidHash);
        RelationOidHash = NULL;
    }

    /* Clear callback */
    TableLookupCallback = NULL;
    TableLookupUserData = NULL;

    /* Reset OID counter */
    NextUserOid = PGPLANNER_FIRST_USER_OID;
}
