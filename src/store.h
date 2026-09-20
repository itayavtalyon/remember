#ifndef REMEMBER_STORE_H
#define REMEMBER_STORE_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Persistence port. Callers never include SQLite.
 * The only implementation is store_sqlite.c.
 */

/* Stack path bound for open/mkdir helpers (not PATH_MAX — that is POSIX-only). */
#ifndef REMEMBER_PATH_MAX
#define REMEMBER_PATH_MAX 4096
#endif

typedef struct Store Store;

typedef enum {
    STORE_OK = 0,
    STORE_ERR_NOT_FOUND,
    STORE_ERR_SQLITE,
    STORE_ERR_OOM,
    STORE_ERR_INTERNAL,
    STORE_ERR_QUERY,       /* invalid FTS5 MATCH syntax (search) */
    STORE_ERR_CONFLICT,    /* keyless body-hash collision on update */
    STORE_ERR_EXPIRED,     /* locator hit expired without STORE_BIN_EXPIRED */
    STORE_ERR_NOT_EXPIRED, /* locator used STORE_BIN_EXPIRED but row is live */
    STORE_ERR_DELETED,     /* locator hit deleted without STORE_BIN_DELETED */
    STORE_ERR_NOT_DELETED, /* locator used STORE_BIN_DELETED but row is live */
    STORE_ERR_SELF_LINK,   /* from_id == to_id */
    STORE_ERR_CYCLE        /* supersedes would cycle */
} StoreStatus;

/* Locator / list / search / tags bin. LIVE is the zero value (zero-init queries). */
typedef enum { STORE_BIN_LIVE = 0, STORE_BIN_EXPIRED, STORE_BIN_DELETED } StoreBin;

/* Short ASCII label for st (no trailing newline). Never NULL. */
const char *store_status_message(StoreStatus st);

typedef enum { STORE_ADD_CREATED = 0, STORE_ADD_MERGED, STORE_ADD_UPDATED } StoreAddAction;

/* Heap-owned entry snapshot. Release with store_entry_free(). */
typedef struct {
    long long id;
    char *sync_id; /* UUID v7 canonical string; immutable after create */
    char *key;     /* NULL if keyless */
    char *body;
    char **tags; /* ntags heap strings; may be NULL when ntags == 0 */
    size_t ntags;
    char *source;
    char *created_at;
    char *updated_at;
    char *expires_at;     /* NULL if none (durable) */
    char *deleted_at;     /* NULL if not soft-deleted */
    char *version_vector; /* JSON object; never NULL after a successful load */
} Entry;

/* Open or create a store at path. Creates parent directories (mode 0700).
 * On success returns a non-NULL Store*. On failure returns NULL and, if
 * err/errlen are usable, writes a short message into err (NUL-terminated).
 * Schema: user_version 0 → create at 4; 1/2/3 → migrate to 4; 4 → ok;
 * >4 → refuse. Sidecar: <path>.device_id (mode 0600).
 */
Store *store_open(const char *path, char *err, size_t errlen);

/* Close and free. Safe with NULL. */
void store_close(Store *s);

/* Release heap fields of *e and zero it. Safe with NULL e or zeroed Entry. */
void store_entry_free(Entry *e);

/* Canonical ISO-8601 UTC .mmmZ (25-byte buffer). 0 on success, -1 on failure. */
int utc_now(char *buf, size_t buflen);

/*
 * Insert or merge/upsert one memory.
 *
 * body / body_hash / source are required (already normalized by the command).
 * key_or_null is NULL for keyless, or a normalized key.
 * tags are normalized names (ntags may be 0; tags may be NULL then).
 * expires_at is NULL (durable) or a canonical .mmmZ string.
 * now is the command's utc_now snapshot (required; used for revive + timestamps).
 *
 * An add that hits an expired row revives: keep id; union tags; incoming
 * expiry or NULL; replace body on keyed upsert; bump updated_at.
 *
 * On STORE_OK, *out_action and *out_entry are filled (entry heap-owned).
 * On failure, *out_entry is left untouched / zeroed by the caller first.
 */
StoreStatus store_add(Store *s, const char *body, const char *body_hash, const char *key_or_null,
                      const char *const *tags, size_t ntags, const char *source,
                      const char *expires_at, const char *now, StoreAddAction *out_action,
                      Entry *out_entry);

/* Load one entry by id. Missing in all bins → NOT_FOUND; wrong bin → EXPIRED /
 * NOT_EXPIRED / DELETED / NOT_DELETED (Round 7 matrix). */
StoreStatus store_get(Store *s, long long id, StoreBin bin, const char *now, Entry *out_entry);

/* Load one entry by normalized key. Same bin contract as store_get. */
StoreStatus store_get_by_key(Store *s, const char *key, StoreBin bin, const char *now,
                             Entry *out_entry);

/* Load one entry by canonical sync_id. Same bin contract as store_get. */
StoreStatus store_get_by_sync_id(Store *s, const char *sync_id, StoreBin bin, const char *now,
                                 Entry *out_entry);

/*
 * List filters (AND across tags). All string fields are normalized by the
 * command layer; NULL / ntags==0 means "no filter on that axis".
 * limit and offset are required as given (CLI enforces default/max/min).
 */
typedef struct {
    const char *const *tags; /* may be NULL when ntags == 0 */
    const char *source;      /* NULL = any */
    const char *key;         /* NULL = any; exact match */
    size_t ntags;
    size_t limit;
    size_t offset;
    StoreBin bin; /* LIVE (default) / EXPIRED / DELETED */
    char pad_[4]; /* explicit tail padding to keep the struct -Wpadded-clean */
} ListQuery;

/*
 * Result of a counted, paged query (store_list / store_search). On st == STORE_OK,
 * entries is a heap array of count Entries (free each with store_entry_free, then
 * free the array) and total is the unpaged count; on error entries is NULL and
 * count/total are 0. count and total travel together so they cannot be transposed.
 */
typedef struct {
    Entry *entries;
    size_t count;
    size_t total;
    StoreStatus st;
    char pad_[4]; /* explicit tail padding (kept -Wpadded-clean) */
} PageResult;

/*
 * List entries newest-first (updated_at DESC, id DESC) with optional filters.
 * now is required (canonical .mmmZ). Bin filter: live / expired / deleted.
 * See PageResult for ownership.
 */
PageResult store_list(Store *s, const ListQuery *q, const char *now);

/*
 * Ranked FTS5 search. query is raw FTS5 MATCH syntax (required, non-empty at CLI).
 * filters reuse ListQuery: tag AND, optional source/key, limit/offset.
 * Rank: bm25(entries_fts) ASC, then updated_at DESC, id DESC.
 * Same ownership as store_list (see PageResult). STORE_ERR_QUERY on bad MATCH syntax.
 */
typedef struct {
    const char *query; /* FTS5 MATCH string */
    ListQuery filters;
} SearchQuery;

PageResult store_search(Store *s, const SearchQuery *q, const char *now);

/*
 * Hard-delete one entry. Under one write transaction: load snapshot, remove FTS
 * row, DELETE entry (CASCADE entry_tags), GC orphan tags. *out_deleted is a
 * heap snapshot of the removed row (caller frees with store_entry_free).
 * STORE_ERR_NOT_FOUND if missing.
 */
StoreStatus store_delete_by_id(Store *s, long long id, StoreBin bin, const char *now,
                               Entry *out_deleted);

/* Same as store_delete_by_id, located by normalized key. */
StoreStatus store_delete_by_key(Store *s, const char *key, StoreBin bin, const char *now,
                                Entry *out_deleted);

/*
 * Update one entry by id (id > 0, key_or_null NULL) or normalized key
 * (key_or_null set; id ignored). set_body / set_tags / set_expires are
 * independent opt-ins; at least one must be true at the command layer.
 * set_expires + expires_at NULL clears expiry; set_expires + ISO writes it.
 * bin + now apply the locator bin (same as get/delete).
 * On success always refreshes updated_at and re-syncs FTS in the same write
 * transaction. Never changes source or key.
 *
 * Keyless body-hash conflict: if set_body and the entry has no key and another
 * keyless row already owns body_hash → STORE_ERR_CONFLICT and, when
 * out_conflict_id is non-NULL, the other entry's id. Keyed entries skip the
 * hash uniqueness check.
 *
 * When set_tags is true, ntags==0 clears tags; otherwise tags replace the set.
 * body / body_hash required when set_body; tags may be NULL when ntags==0.
 */
StoreStatus store_update(Store *s, long long id, const char *key_or_null, bool set_body,
                         const char *body, const char *body_hash, bool set_tags,
                         const char *const *tags, size_t ntags, bool set_expires,
                         const char *expires_at, StoreBin bin, const char *now, Entry *out_entry,
                         long long *out_conflict_id);

/* One tag with the number of entries carrying it. name is heap-owned. */
typedef struct {
    char *name;
    long long count;
} TagCount;

/*
 * All distinct tags with their entry counts, sorted by name (COLLATE BINARY).
 * Tags with zero entries never appear (orphans are GC'd on delete). On STORE_OK,
 * *out_tags is a heap array of *out_count TagCount (release with store_tags_free);
 * an empty database yields *out_tags == NULL and *out_count == 0.
 */
StoreStatus store_tags(Store *s, StoreBin bin, const char *now, TagCount **out_tags,
                       size_t *out_count);

/* Release a store_tags result. Safe with NULL. */
void store_tags_free(TagCount *tags, size_t count);

/*
 * Permanently delete every expired row (one write txn, FTS + tag GC).
 * On STORE_OK: *out_entries is all snapshots (*out_count; 0/NULL if empty).
 * Caller frees each entry then the array.
 */
StoreStatus store_purge_trash(Store *s, const char *now, Entry **out_entries, size_t *out_count);

/* Stored kind only. cited_by / superseded_by are output-only (CLI). */
typedef enum { STORE_EDGE_RELATED = 0, STORE_EDGE_SUPERSEDES, STORE_EDGE_CITES } StoreEdgeKind;

/* A directed (from_id -> to_id) endpoint pair. Passed by value so call sites
   name each end (designated initializer) and cannot silently transpose them. */
typedef struct {
    long long from_id;
    long long to_id;
} StoreEdge;

/* Rekey selector + target: current key to match (NULL => locate by id / keyless)
   and the new key (NULL => clear to keyless). Named so the two cannot transpose. */
typedef struct {
    const char *key_or_null;
    const char *new_key_or_null;
} RekeyKeys;

typedef enum { STORE_LINK_CREATED = 0, STORE_LINK_MERGED, STORE_LINK_DELETED } StoreLinkAction;

typedef enum {
    STORE_NEIGHBOR_ALL = 0,
    STORE_NEIGHBOR_OUTGOING,
    STORE_NEIGHBOR_INCOMING
} StoreNeighborDir;

/* One edge as seen from subject_id. Neighbor fields are the other end. */
typedef struct {
    long long subject_id;
    long long from_id;
    long long to_id;
    char *edge_updated_at;
    long long neighbor_id;
    char *neighbor_key; /* NULL if keyless */
    char *neighbor_body;
    char *neighbor_expires_at; /* NULL if durable */
    StoreEdgeKind kind;
    char pad_[4]; /* explicit tail padding (kept -Wpadded-clean) */
} StoreNeighbor;

void store_neighbor_free(StoreNeighbor *n);
void store_neighbors_free(StoreNeighbor *rows, size_t count);

/* Load by id/key across live+expired (graph locators). Deleted is excluded.
 * Missing (or deleted) → NOT_FOUND. */
StoreStatus store_get_any(Store *s, long long id, Entry *out_entry);
StoreStatus store_get_any_by_key(Store *s, const char *key, Entry *out_entry);

/*
 * Upsert one edge. related is stored as (min,max,related). Directed is
 * (from,to,kind). Self-link → SELF_LINK; missing id → NOT_FOUND; supersedes
 * cycle → CYCLE. Real write bumps updated_at on both endpoints.
 * *out_stub is subject-relative to from_id (caller frees).
 */
StoreStatus store_link(Store *s, StoreEdge edge, StoreEdgeKind kind, const char *now,
                       StoreLinkAction *out_action, StoreNeighbor *out_stub);

/*
 * Delete edges. kind NULL = all kinds between the unordered pair. related
 * canonicalizes. Directed deletes the given (from,to,kind) only. Missing is
 * OK (count 0, no bump). Real delete bumps both endpoints.
 */
StoreStatus store_unlink(Store *s, long long from_id, long long to_id, const StoreEdgeKind *kind,
                         const char *now, StoreNeighbor **out_stubs, size_t *out_count);

/*
 * Neighbors of one subject. kind NULL = all stored kinds. related edges appear
 * under OUTGOING and INCOMING. Order: edge updated_at DESC, neighbor id DESC.
 */
StoreStatus store_list_neighbors(Store *s, long long subject_id, const StoreEdgeKind *kind,
                                 StoreNeighborDir dir, const char *now, StoreNeighbor **out,
                                 size_t *out_count);

/* All neighbors of a page of ids (one query). subject_id set per row. */
StoreStatus store_list_neighbors_for(Store *s, const long long *ids, size_t nids, const char *now,
                                     StoreNeighbor **out, size_t *out_count);

/*
 * Change key in place. Locator like update (id or key_or_null, bin+now).
 * new_key_or_null NULL = clear (demote); non-NULL = set/rename (must be
 * non-empty). Id and edges preserved. NEWKEY taken or demote body-hash
 * collision → CONFLICT + *out_conflict_id. Always bumps updated_at.
 */
StoreStatus store_rekey(Store *s, long long id, RekeyKeys keys, StoreBin bin, const char *now,
                        Entry *out_entry, long long *out_conflict_id);

/*
 * Test-only fault injection (compiled when REMEMBER_TEST_HOOKS is defined).
 * fail_after: number of successful allocs/prepares before the next fails;
 * use 0 to fail the immediate next call. Pass -1 to disable.
 */
#ifdef REMEMBER_TEST_HOOKS
void store_test_fail_alloc_after(int n);
void store_test_fail_prepare_after(int n);
void store_test_fail_step_after(int n);
void store_test_fail_exec_after(int n);
#endif

#endif /* REMEMBER_STORE_H */
