/*-------------------------------------------------------------------------
 *
 * pgplanner_stubs.c
 *    Stub functions for PostgreSQL backend features not needed for planning.
 *
 * The planner is tightly integrated with PostgreSQL's backend, but many
 * features are only needed for actual query execution. This file provides
 * stub implementations that allow the planner to work standalone.
 *
 * Copyright (c) 2024, PostgreSQL Global Development Group
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/xact.h"
#include "catalog/namespace.h"
#include "catalog/pg_authid_d.h"
#include "catalog/pg_collation.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "optimizer/plancat.h"
#include "utils/selfuncs.h"
#include "storage/lmgr.h"
#include "storage/lock.h"
#include "utils/acl.h"
#include "utils/hsearch.h"
#include "utils/memutils.h"
#include "utils/snapmgr.h"

#include "pgplanner.h"
#include "pgplanner_internal.h"

/*-------------------------------------------------------------------------
 * Global Variables
 *-------------------------------------------------------------------------
 */

/* Indicate we're not running under postmaster */
bool IsUnderPostmaster = false;

/* Current user ID - always superuser in standalone mode */
static Oid StandaloneUserId = BOOTSTRAP_SUPERUSERID;

/*-------------------------------------------------------------------------
 * Lock Manager Stubs
 *
 * The planner doesn't actually need to acquire locks - it only plans
 * queries, it doesn't execute them. We provide no-op stubs.
 *-------------------------------------------------------------------------
 */

#ifdef PG_PLANNER_STANDALONE

void
LockRelationOid(Oid relid, LOCKMODE lockmode)
{
    /* No-op: planner doesn't need actual locks */
}

void
UnlockRelationOid(Oid relid, LOCKMODE lockmode)
{
    /* No-op */
}

bool
ConditionalLockRelationOid(Oid relid, LOCKMODE lockmode)
{
    /* Always succeed */
    return true;
}

void
LockRelation(Relation relation, LOCKMODE lockmode)
{
    /* No-op */
}

void
UnlockRelation(Relation relation, LOCKMODE lockmode)
{
    /* No-op */
}

bool
LockHasWaitersRelation(Relation relation, LOCKMODE lockmode)
{
    return false;
}

void
LockDatabaseObject(Oid classid, Oid objid, uint16 objsubid, LOCKMODE lockmode)
{
    /* No-op */
}

void
UnlockDatabaseObject(Oid classid, Oid objid, uint16 objsubid, LOCKMODE lockmode)
{
    /* No-op */
}

/*-------------------------------------------------------------------------
 * ACL/Permission Stubs
 *
 * All permission checks succeed in standalone mode.
 *-------------------------------------------------------------------------
 */

AclResult
pg_class_aclcheck(Oid table_oid, Oid roleid, AclMode mode)
{
    return ACLCHECK_OK;
}

AclResult
pg_attribute_aclcheck(Oid table_oid, AttrNumber attnum, Oid roleid, AclMode mode)
{
    return ACLCHECK_OK;
}

AclResult
pg_attribute_aclcheck_all(Oid table_oid, Oid roleid, AclMode mode, AclMaskHow how)
{
    return ACLCHECK_OK;
}

AclResult
pg_database_aclcheck(Oid db_oid, Oid roleid, AclMode mode)
{
    return ACLCHECK_OK;
}

AclResult
pg_namespace_aclcheck(Oid nsp_oid, Oid roleid, AclMode mode)
{
    return ACLCHECK_OK;
}

AclResult
pg_proc_aclcheck(Oid proc_oid, Oid roleid, AclMode mode)
{
    return ACLCHECK_OK;
}

AclResult
pg_type_aclcheck(Oid type_oid, Oid roleid, AclMode mode)
{
    return ACLCHECK_OK;
}

AclResult
object_aclcheck(Oid classid, Oid objectid, Oid roleid, AclMode mode)
{
    return ACLCHECK_OK;
}

bool
has_privs_of_role(Oid member, Oid role)
{
    return true;
}

bool
is_member_of_role(Oid member, Oid role)
{
    return true;
}

Oid
get_role_oid(const char *rolename, bool missing_ok)
{
    return BOOTSTRAP_SUPERUSERID;
}

/*-------------------------------------------------------------------------
 * Transaction State Stubs
 *-------------------------------------------------------------------------
 */

bool
IsTransactionState(void)
{
    return true;
}

TransactionId
GetCurrentTransactionId(void)
{
    return FirstNormalTransactionId;
}

TransactionId
GetCurrentTransactionIdIfAny(void)
{
    return FirstNormalTransactionId;
}

bool
TransactionIdIsCurrentTransactionId(TransactionId xid)
{
    return (xid == FirstNormalTransactionId);
}

CommandId
GetCurrentCommandId(bool used)
{
    return FirstCommandId;
}

/*-------------------------------------------------------------------------
 * Snapshot Stubs
 *-------------------------------------------------------------------------
 */

static SnapshotData StandaloneSnapshotData = {
    .snapshot_type = SNAPSHOT_MVCC,
};

Snapshot
GetTransactionSnapshot(void)
{
    return &StandaloneSnapshotData;
}

Snapshot
GetLatestSnapshot(void)
{
    return &StandaloneSnapshotData;
}

Snapshot
GetActiveSnapshot(void)
{
    return &StandaloneSnapshotData;
}

bool
ActiveSnapshotSet(void)
{
    return true;
}

void
PushActiveSnapshot(Snapshot snapshot)
{
    /* No-op */
}

void
PopActiveSnapshot(void)
{
    /* No-op */
}

Snapshot
RegisterSnapshot(Snapshot snapshot)
{
    return snapshot;
}

void
UnregisterSnapshot(Snapshot snapshot)
{
    /* No-op */
}

/*-------------------------------------------------------------------------
 * Namespace/Schema Stubs
 *-------------------------------------------------------------------------
 */

/* Hash table for namespace lookup */
static HTAB *NamespaceOidHash = NULL;
static HTAB *NamespaceNameHash = NULL;
static Oid NextNamespaceOid = 16384;

typedef struct NamespaceOidEntry
{
    Oid key;
    char name[NAMEDATALEN];
} NamespaceOidEntry;

typedef struct NamespaceNameEntry
{
    char key[NAMEDATALEN];
    Oid oid;
} NamespaceNameEntry;

static void
ensure_namespace_hashes(void)
{
    HASHCTL hashctl;
    NamespaceOidEntry *oid_entry;
    NamespaceNameEntry *name_entry;
    bool found;

    if (NamespaceOidHash != NULL)
        return;

    /* Create OID lookup hash */
    memset(&hashctl, 0, sizeof(hashctl));
    hashctl.keysize = sizeof(Oid);
    hashctl.entrysize = sizeof(NamespaceOidEntry);
    hashctl.hcxt = CurrentMemoryContext;

    NamespaceOidHash = hash_create("Namespace OID Hash",
                                   32, &hashctl,
                                   HASH_ELEM | HASH_BLOBS | HASH_CONTEXT);

    /* Create name lookup hash */
    memset(&hashctl, 0, sizeof(hashctl));
    hashctl.keysize = NAMEDATALEN;
    hashctl.entrysize = sizeof(NamespaceNameEntry);
    hashctl.hcxt = CurrentMemoryContext;

    NamespaceNameHash = hash_create("Namespace Name Hash",
                                    32, &hashctl,
                                    HASH_ELEM | HASH_STRINGS | HASH_CONTEXT);

    /* Register pg_catalog namespace (OID 11) */
    oid_entry = hash_search(NamespaceOidHash, &(Oid){11}, HASH_ENTER, &found);
    strlcpy(oid_entry->name, "pg_catalog", NAMEDATALEN);

    name_entry = hash_search(NamespaceNameHash, "pg_catalog", HASH_ENTER, &found);
    name_entry->oid = 11;

    /* Register public namespace (OID 2200) */
    oid_entry = hash_search(NamespaceOidHash, &(Oid){2200}, HASH_ENTER, &found);
    strlcpy(oid_entry->name, "public", NAMEDATALEN);

    name_entry = hash_search(NamespaceNameHash, "public", HASH_ENTER, &found);
    name_entry->oid = 2200;
}

void
pgplanner_init_namespace(void)
{
    ensure_namespace_hashes();
}

Oid
pgplanner_create_namespace(const char *name, bool if_not_exists)
{
    NamespaceNameEntry *name_entry;
    NamespaceOidEntry *oid_entry;
    bool found;
    Oid nsp_oid;

    ensure_namespace_hashes();

    /* Check if already exists */
    name_entry = hash_search(NamespaceNameHash, name, HASH_FIND, NULL);
    if (name_entry != NULL)
    {
        if (if_not_exists)
            return name_entry->oid;
        return InvalidOid;
    }

    /* Create new namespace */
    nsp_oid = NextNamespaceOid++;

    oid_entry = hash_search(NamespaceOidHash, &nsp_oid, HASH_ENTER, &found);
    strlcpy(oid_entry->name, name, NAMEDATALEN);

    name_entry = hash_search(NamespaceNameHash, name, HASH_ENTER, &found);
    name_entry->oid = nsp_oid;

    return nsp_oid;
}

Oid
get_namespace_oid(const char *nspname, bool missing_ok)
{
    NamespaceNameEntry *entry;

    ensure_namespace_hashes();

    entry = hash_search(NamespaceNameHash, nspname, HASH_FIND, NULL);
    if (entry == NULL)
    {
        if (!missing_ok)
            ereport(ERROR,
                    (errcode(ERRCODE_UNDEFINED_SCHEMA),
                     errmsg("schema \"%s\" does not exist", nspname)));
        return InvalidOid;
    }
    return entry->oid;
}

char *
get_namespace_name(Oid nspid)
{
    NamespaceOidEntry *entry;

    ensure_namespace_hashes();

    entry = hash_search(NamespaceOidHash, &nspid, HASH_FIND, NULL);
    if (entry == NULL)
        return NULL;
    return pstrdup(entry->name);
}

/*-------------------------------------------------------------------------
 * User/Role Stubs
 *-------------------------------------------------------------------------
 */

Oid
GetUserId(void)
{
    return StandaloneUserId;
}

Oid
GetSessionUserId(void)
{
    return StandaloneUserId;
}

Oid
GetOuterUserId(void)
{
    return StandaloneUserId;
}

Oid
GetCurrentRoleId(void)
{
    return StandaloneUserId;
}

bool
superuser(void)
{
    return true;
}

bool
superuser_arg(Oid roleid)
{
    return true;
}

/*-------------------------------------------------------------------------
 * Encoding Stubs
 *-------------------------------------------------------------------------
 */

int
GetDatabaseEncoding(void)
{
    return PG_UTF8;
}

int
pg_database_encoding_max_length(void)
{
    return 4;  /* UTF-8 max */
}

/*-------------------------------------------------------------------------
 * Relation Cache Stub
 *
 * We maintain our own relation cache that is separate from PostgreSQL's
 * internal RelationIdCache. This stub provides access to our cache.
 *-------------------------------------------------------------------------
 */

/* Our relation cache hash table (defined in pgplanner_relations.c) */
extern HTAB *RelationOidHash;

typedef struct RelationOidEntry
{
    Oid key;
    Relation relation;
} RelationOidEntry;

Relation
RelationIdGetRelation(Oid relationId)
{
    RelationOidEntry *entry;

    if (RelationOidHash == NULL)
        return NULL;

    entry = hash_search(RelationOidHash, &relationId, HASH_FIND, NULL);
    if (entry == NULL)
        return NULL;

    /* Increment reference count */
    entry->relation->rd_refcnt++;

    return entry->relation;
}

void
RelationClose(Relation relation)
{
    if (relation == NULL)
        return;

    /* Decrement reference count */
    if (relation->rd_refcnt > 0)
        relation->rd_refcnt--;
}

/*-------------------------------------------------------------------------
 * Relation Lookup Stub
 *
 * This is a critical hook for the table callback mechanism. When the
 * parser/analyzer looks up a table by name, it calls get_relname_relid.
 * We override this to:
 * 1. Check our registered tables first
 * 2. If not found, invoke the callback to get the table definition
 * 3. Register the table if callback provides a definition
 *-------------------------------------------------------------------------
 */

Oid
get_relname_relid(const char *relname, Oid relnamespace)
{
    Oid result;

    /* First check if table is already registered via our find_table function */
    /* We need to convert the namespace OID to a schema name */
    char *schema_name = get_namespace_name(relnamespace);

    result = pgplanner_find_table(schema_name, relname);

    /* If not found, try the callback */
    if (!OidIsValid(result))
    {
        result = pgplanner_lookup_table_via_callback(schema_name, relname);
    }

    if (schema_name)
        pfree(schema_name);

    return result;
}

/*-------------------------------------------------------------------------
 * Statistics Hook Stub
 *-------------------------------------------------------------------------
 */

get_relation_stats_hook_type get_relation_stats_hook = NULL;

/*-------------------------------------------------------------------------
 * Misc Stubs
 *-------------------------------------------------------------------------
 */

void
check_stack_depth(void)
{
    /* No-op in standalone mode */
}

void
ProcessInterrupts(void)
{
    /* No-op */
}

bool
proc_exit_inprogress = false;

#endif /* PG_PLANNER_STANDALONE */
