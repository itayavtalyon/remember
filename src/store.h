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
    STORE_ERR_QUERY,             /* invalid FTS5 MATCH syntax (search) */
    STORE_ERR_CONFLICT,          /* keyless body-hash collision on update */
    STORE_ERR_EXPIRED,           /* locator hit expired without STORE_BIN_EXPIRED */
    STORE_ERR_NOT_EXPIRED,       /* locator used STORE_BIN_EXPIRED but row is live */
    STORE_ERR_DELETED,           /* locator hit deleted without STORE_BIN_DELETED */
    STORE_ERR_NOT_DELETED,       /* locator used STORE_BIN_DELETED but row is live */
    STORE_ERR_NOT_SINGLE_DEVICE, /* catalog only; no v5 command returns it */
    STORE_ERR_SELF_LINK,         /* from_id == to_id */
    STORE_ERR_CYCLE,             /* supersedes would cycle */
    STORE_ERR_SOURCE_TOO_OLD,    /* import --from-db: source user_version < 4 */
    STORE_ERR_SOURCE_TOO_NEW,    /* import --from-db: source user_version > 5 */
    STORE_ERR_UNIQUE_TAKEN,      /* accept cannot free key/hash without inventing a key */
    STORE_ERR_STALE /* if_updated_at is not the row's updated_at; nothing was written */
} StoreStatus;

/* Locator / list / search / tags bin. LIVE is the zero value (zero-init queries). */
typedef enum { STORE_BIN_LIVE = 0, STORE_BIN_EXPIRED, STORE_BIN_DELETED } StoreBin;

/* List time column. UPDATED is the zero value (zero-init queries keep today's order). */
typedef enum { STORE_SORT_UPDATED = 0, STORE_SORT_CREATED } StoreSort;
/* live: no deleted_at and (no expiry or expires_at > now);
 * expired: no deleted_at and expires_at <= now;
 * deleted: deleted_at set. now may be NULL (then expiry cannot match). */
StoreBin store_bin_of(const char *deleted_at, const char *expires_at, const char *now);

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

/* Structured SQLite failure (plan 16 stage 3). The zero value means no SQLite
 * failure was captured. store.h exposes plain C only: ints, fixed strings,
 * and enums — never engine header types or macros. The mapping from extended
 * code to class and the extended-name table live in the adapter. */
typedef enum {
    STORE_ERROR_NONE = 0,
    STORE_ERROR_BUSY,
    STORE_ERROR_READ_ONLY,
    STORE_ERROR_DISK_FULL,
    STORE_ERROR_CANNOT_OPEN,
    STORE_ERROR_NOT_A_DATABASE,
    STORE_ERROR_CORRUPT,
    STORE_ERROR_IO_ERROR,
    STORE_ERROR_DATABASE
} StoreErrorClass;

/* cannot_open detail (E4.7): EACCES, ENOENT, EISDIR/CANTOPEN_ISDIR, else other. */
typedef enum {
    STORE_REASON_NONE = 0,
    STORE_REASON_PERMISSION,
    STORE_REASON_MISSING,
    STORE_REASON_IS_DIRECTORY,
    STORE_REASON_OTHER
} StoreOpenReason;

enum {
    STORE_SQLITE_NAME_LEN = 32,
    STORE_SQLITE_MSG_LEN = 256,
};

typedef struct {
    StoreErrorClass error_class;
    int sqlite_code;     /* primary result code (low byte of the extended code) */
    int sqlite_extended; /* full extended result code */
    char sqlite_name[STORE_SQLITE_NAME_LEN];
    char sqlite_message[STORE_SQLITE_MSG_LEN];
    int sys_errno;          /* OS errno captured at the failure; 0 when none */
    StoreOpenReason reason; /* cannot_open only; NONE otherwise */
    bool fts_only;          /* corrupt is FTS-only (set by the stage 6 check) */
    bool import_source;     /* the failing call ran on the import --from-db source */
    char pad_[2];           /* explicit tail padding (kept -Wpadded-clean) */
} StoreError;

/* Open options (plan 16 stage 3). NULL opts means defaults (5000 ms, create).
 * The CLI sets busy_timeout_ms from --busy-timeout and no_create from --no-create. */
enum { STORE_DEFAULT_BUSY_TIMEOUT_MS = 5000 };

typedef struct {
    int busy_timeout_ms;          /* PRAGMA busy_timeout; used as given when opts != NULL */
    bool no_create;               /* missing file fails instead of creating (E4.11) */
    bool replace_invalid_sidecar; /* `device new` only: an invalid sidecar counts as
                                     missing and is minted over (Itay 2026-10-13) */
    char pad_[2];                 /* explicit tail padding (kept -Wpadded-clean) */
} StoreOpenOpts;

/* Open or create a store at path. Creates parent directories (mode 0700).
 * On success returns a non-NULL Store*. On failure returns NULL and, if
 * err/errlen are usable, writes a short message into err (NUL-terminated).
 * Schema: user_version 0 creates 5; 1/2/3 climb to 5; 4 migrates to 5; 5 is
 * current; >5 refuses with "database is newer than this remember".
 * Sidecar <path>.device_id (mode 0600) is the only device id. A missing file
 * is minted. An invalid file is an error and is left unchanged.
 */
Store *store_open(const char *path, char *err, size_t errlen);

/*
 * store_open with options and a structured SQLite failure. opts NULL means
 * defaults (STORE_DEFAULT_BUSY_TIMEOUT_MS, create, keep an invalid sidecar an
 * error). out_error NULL means no detail. On failure with an SQLite cause,
 * *out_error carries code/extended/name/message/errno (E4.7); otherwise it is
 * zeroed (the err text still names the failure). With no_create, a missing
 * file fails before any mkdir and creates nothing (D8).
 */
Store *store_open_opts(const char *path, const StoreOpenOpts *opts, char *err, size_t errlen,
                       StoreError *out_error);

/*
 * Last captured SQLite failure on s. Meaningful only when the most recent op
 * returned STORE_ERR_SQLITE; otherwise the zero value. Valid until the next
 * op on s or store_close. Never NULL when s is non-NULL.
 */
const StoreError *store_last_error(const Store *s);

/* Path s was opened with (borrowed; valid until store_close). NULL if s NULL. */
const char *store_path(const Store *s);

/* Close and free. Safe with NULL. */
void store_close(Store *s);

/* UUID text length of a device id, not counting the NUL. */
enum { STORE_DEVICE_ID_LEN = 36 };

/* Writer id bound at open. NULL if s is NULL. Valid until store_close or a
 * successful store_device_new on the same handle. */
const char *store_device_id(const Store *s);

/*
 * Mint a new sidecar id and bind it. Does not change any version_vector.
 * Takes the same write lock as a missing-sidecar open, so parallel mints cannot
 * share <path>.device_id.tmp.
 * On STORE_OK, the sidecar and this handle both hold the new id. *had_prev is
 * true and prev_out (STORE_DEVICE_ID_LEN + 1) holds the id the file had. A
 * missing or invalid file sets *had_prev false and prev_out empty (JSON null)
 * and is replaced. Open mints a missing file first, so a CLI call reports that
 * id (D4). Allocation fails before the rename, so the file stays.
 * err receives the open-path message. prev_out / had_prev are meaningful only
 * on STORE_OK.
 */
StoreStatus store_device_new(Store *s, char *prev_out, size_t prev_len, bool *had_prev, char *err,
                             size_t errlen);

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
 * sort is the list time column (order and since/until). Zero is updated.
 * since is inclusive and until is exclusive; both are canonical .mmmZ or NULL.
 * Search reuses the struct: it ignores sort and applies since/until to updated_at.
 */
typedef struct {
    const char *const *tags; /* may be NULL when ntags == 0 */
    const char *source;      /* NULL = any */
    const char *key;         /* NULL = any; exact match */
    size_t ntags;
    size_t limit;
    size_t offset;
    StoreBin bin;   /* LIVE (default) / EXPIRED / DELETED */
    StoreSort sort; /* UPDATED (zero) / CREATED */
    const char *since;
    const char *until;
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
 * List entries by the sort column DESC, then id DESC, with optional filters.
 * Zero sort is updated_at. since/until bound that same column.
 * now is required (canonical .mmmZ). Bin filter: live / expired / deleted.
 * See PageResult for ownership. An unknown sort is STORE_ERR_INTERNAL.
 */
PageResult store_list(Store *s, const ListQuery *q, const char *now);

/*
 * Ranked FTS5 search. query is raw FTS5 MATCH syntax (required, non-empty at CLI).
 * filters reuse ListQuery: tag AND, optional source/key, limit/offset, since/until.
 * Rank stays bm25(entries_fts) ASC, then updated_at DESC, id DESC. filters.sort
 * is ignored. since/until filter updated_at, not the sort column.
 * Same ownership as store_list (see PageResult). STORE_ERR_QUERY on bad MATCH syntax.
 */
typedef struct {
    const char *query; /* FTS5 MATCH string */
    ListQuery filters;
} SearchQuery;

PageResult store_search(Store *s, const SearchQuery *q, const char *now);

/*
 * Soft-delete one entry (locator: live or expired). Sets deleted_at=now, clears
 * expires_at, keeps body/tags/key/sync_id/edges/FTS, bumps VV + updated_at.
 * Already deleted → STORE_ERR_DELETED. *out is the post-mutation snapshot.
 */
StoreStatus store_soft_delete_by_id(Store *s, long long id, const char *now, Entry *out);
StoreStatus store_soft_delete_by_key(Store *s, const char *key, const char *now, Entry *out);

/*
 * Hard CASCADE one row. bin must be EXPIRED or DELETED (LIVE → INTERNAL; hard
 * cannot default). Open already required a valid sidecar. Removes FTS.
 * *out is a snapshot of the removed row.
 */
StoreStatus store_hard_delete_by_id(Store *s, long long id, StoreBin bin, const char *now,
                                    Entry *out);
StoreStatus store_hard_delete_by_key(Store *s, const char *key, StoreBin bin, const char *now,
                                     Entry *out);

/*
 * Update one entry by id (id > 0, key_or_null NULL) or normalized key
 * (key_or_null set; id ignored). set_body / set_tags / set_expires / undelete
 * are independent opt-ins; at least one must be true at the command layer.
 * set_expires + expires_at NULL clears expiry; set_expires + ISO writes it.
 * undelete clears deleted_at (locator is normally STORE_BIN_DELETED).
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
 * if_updated_at NULL skips the guard. Any other string is compared byte for
 * byte with the row's updated_at inside this write transaction. A mismatch
 * is STORE_ERR_STALE and the transaction rolls back.
 */
StoreStatus store_update(Store *s, long long id, const char *key_or_null, bool set_body,
                         const char *body, const char *body_hash, bool set_tags,
                         const char *const *tags, size_t ntags, bool set_expires,
                         const char *expires_at, bool undelete, StoreBin bin, const char *now,
                         const char *if_updated_at, Entry *out_entry, long long *out_conflict_id);

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
 * Hard-wipe every row in bin (EXPIRED or DELETED; LIVE → INTERNAL). Open
 * already required a valid sidecar. On STORE_OK: *out_entries is all snapshots
 * (*out_count; 0/NULL if empty). Caller frees each entry then the array.
 */
StoreStatus store_purge(Store *s, StoreBin bin, const char *now, Entry **out_entries,
                        size_t *out_count);

/* store_purge(EXPIRED). Kept so existing expired-bin call sites stay short. */
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
    char *neighbor_sync_id;    /* UUID v7; required on a successful load */
    char *neighbor_deleted_at; /* NULL if not soft-deleted */
    StoreEdgeKind kind;
    char pad_[4]; /* explicit tail padding (kept -Wpadded-clean) */
} StoreNeighbor;

void store_neighbor_free(StoreNeighbor *n);
void store_neighbors_free(StoreNeighbor *rows, size_t count);

/* Load by id/key across live+expired (graph locators). Deleted is excluded.
 * Missing (or deleted) → NOT_FOUND. */
StoreStatus store_get_any(Store *s, long long id, Entry *out_entry);
StoreStatus store_get_any_by_key(Store *s, const char *key, Entry *out_entry);
StoreStatus store_get_any_by_sync_id(Store *s, const char *sync_id, Entry *out_entry);

/* Load by id/key/sync_id in any bin (including deleted). For graph --deleted. */
StoreStatus store_get_row(Store *s, long long id, Entry *out_entry);
StoreStatus store_get_row_by_key(Store *s, const char *key, Entry *out_entry);
StoreStatus store_get_row_by_sync_id(Store *s, const char *sync_id, Entry *out_entry);

/* True if s is canonical lowercase UUID v7 (8-4-4-4-12). */
int store_sync_id_is_canonical(const char *s);

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

/* Counts from store_import (JSON envelope fields). */
typedef struct {
    size_t inserted;
    size_t updated;
    size_t unchanged;
    size_t conflicts;
} StoreImportCounts;

/*
 * Merge another remember DB by sync_id (Round 7 VV / unique rules). Opens
 * src_path read-only inside the adapter (never store_open; never touches the
 * foreign sidecar; never reads devices). user_version < 4 →
 * STORE_ERR_SOURCE_TOO_OLD. user_version > 5 → STORE_ERR_SOURCE_TOO_NEW
 * ("database is newer than this remember"). A missing conflicts.origin column
 * or a missing bucket_prefixes / settings table is absent, not an error; those
 * rows are not copied. Exit-path JSON: action imported + these counts;
 * STORE_OK even when conflicts > 0.
 */
StoreStatus store_import(Store *dst, const char *src_path, const char *now,
                         StoreImportCounts *out_counts);

/* Closed conflict reason enum (Round 7). Unknown on disk → store error. */
typedef enum {
    STORE_CONFLICT_CONCURRENT_VV = 0,
    STORE_CONFLICT_KEY_CLASH,
    STORE_CONFLICT_HASH_CLASH
} StoreConflictReason;

/* Canonical reason tokens (concurrent_vv|key_clash|hash_clash). NULL if invalid. */
const char *store_conflict_reason_str(StoreConflictReason reason);

typedef struct {
    long long id;
    char *sync_id;
    char *local_json;
    char *incoming_json;
    char *created_at;
    StoreConflictReason reason;
    char pad_[4];
} StoreConflict;

/* List open conflicts (id ASC). Caller frees with store_conflicts_free. */
StoreStatus store_conflicts_list(Store *s, StoreConflict **out_rows, size_t *out_count);
void store_conflicts_free(StoreConflict *rows, size_t count);

typedef enum { STORE_KEEP_LOCAL = 0, STORE_KEEP_INCOMING, STORE_KEEP_BOTH } StoreConflictKeep;

/*
 * Resolve one conflict. --keep both: remint sync_id on concurrent_vv; on
 * key/hash clash keep incoming sync_id (keyless if slot taken). Unions
 * incoming VV into survivor(s). *out_entries / *out_count are post-accept
 * snapshots (1 or 2); caller frees each then the array. Missing id → NOT_FOUND.
 */
StoreStatus store_conflict_accept(Store *s, long long conflict_id, StoreConflictKeep keep,
                                  const char *now, Entry **out_entries, size_t *out_count);

/*
 * Rebuild the search index (E4.14). quick_check runs on the base tables first.
 * A failure is STORE_ERR_SQLITE, class corrupt, fts_only false. Rebuild runs in
 * one transaction, then integrity-check. *out_entries is COUNT(*) of entries,
 * every bin. Never repairs on its own (E4.10).
 */
StoreStatus store_repair_search_index(Store *s, size_t *out_entries);

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
/* Report SQLITE_SCHEMA from exec (concurrent-DDL simulation for the SCHEMA
   retry); same countdown convention as the other hooks. Pass -1 to disable. */
void store_test_fail_exec_schema_after(int n);
/* Fail the next sidecar rename when n > 0. Pass 0 to clear. */
void store_test_fail_sidecar_rename(int n);
/*
 * Read-only probes for the E4.7 mapping (stage 3 coverage: the class table,
 * the extended-name table, the cannot_open reason, and the message clamp).
 * Pure functions of their inputs; they touch no database.
 */
StoreErrorClass store_test_classify_extended(int extended);
const char *store_test_sqlite_name(int extended);
StoreOpenReason store_test_derive_reason(int extended, int sys_errno);
void store_test_copy_message(StoreError *out, const char *msg);
void store_test_capture_null(StoreError *out);
/* SQLITE_DBCONFIG_DEFENSIVE on s's connection: 1 on, 0 off, -1 for NULL. */
int store_test_defensive(const Store *s);
#endif

#endif /* REMEMBER_STORE_H */
