#ifndef REMEMBER_STORE_H
#define REMEMBER_STORE_H

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
    STORE_ERR_QUERY /* invalid FTS5 MATCH syntax (search) */
} StoreStatus;

/* Short ASCII label for st (no trailing newline). Never NULL. */
const char *store_status_message(StoreStatus st);

typedef enum { STORE_ADD_CREATED = 0, STORE_ADD_MERGED, STORE_ADD_UPDATED } StoreAddAction;

/* Heap-owned entry snapshot. Release with store_entry_free(). */
typedef struct {
    long long id;
    char *key; /* NULL if keyless */
    char *body;
    char **tags; /* ntags heap strings; may be NULL when ntags == 0 */
    size_t ntags;
    char *source;
    char *created_at;
    char *updated_at;
} Entry;

/* Open or create a store at path. Creates parent directories (mode 0700).
 * On success returns a non-NULL Store*. On failure returns NULL and, if
 * err/errlen are usable, writes a short message into err (NUL-terminated).
 * Schema: user_version 0 → create at 1; 1 → ok; >1 → refuse.
 */
Store *store_open(const char *path, char *err, size_t errlen);

/* Close and free. Safe with NULL. */
void store_close(Store *s);

/* Release heap fields of *e and zero it. Safe with NULL e or zeroed Entry. */
void store_entry_free(Entry *e);

/*
 * Insert or merge/upsert one memory.
 *
 * body / body_hash / source are required (already normalized by the command).
 * key_or_null is NULL for keyless, or a normalized key.
 * tags are normalized names (ntags may be 0; tags may be NULL then).
 *
 * On STORE_OK, *out_action and *out_entry are filled (entry heap-owned).
 * On failure, *out_entry is left untouched / zeroed by the caller first.
 */
StoreStatus store_add(Store *s, const char *body, const char *body_hash, const char *key_or_null,
                      const char *const *tags, size_t ntags, const char *source,
                      StoreAddAction *out_action, Entry *out_entry);

/* Load one entry by id. STORE_ERR_NOT_FOUND if missing. */
StoreStatus store_get(Store *s, long long id, Entry *out_entry);

/* Load one entry by normalized key. STORE_ERR_NOT_FOUND if missing. */
StoreStatus store_get_by_key(Store *s, const char *key, Entry *out_entry);

/*
 * List filters (AND across tags). All string fields are normalized by the
 * command layer; NULL / ntags==0 means "no filter on that axis".
 * limit and offset are required as given (CLI enforces default/max/min).
 */
typedef struct {
    const char *const *tags; /* may be NULL when ntags == 0 */
    size_t ntags;
    const char *source; /* NULL = any */
    const char *key;    /* NULL = any; exact match */
    size_t limit;
    size_t offset;
} ListQuery;

/*
 * List entries newest-first (updated_at DESC, id DESC) with optional filters.
 * On STORE_OK: *out_entries is a heap array of *out_count Entries (free each
 * with store_entry_free, then free the array); *out_total is the unpaged count.
 */
StoreStatus store_list(Store *s, const ListQuery *q, Entry **out_entries, size_t *out_count,
                       size_t *out_total);

/*
 * Ranked FTS5 search. query is raw FTS5 MATCH syntax (required, non-empty at CLI).
 * filters reuse ListQuery: tag AND, optional source/key, limit/offset.
 * Rank: bm25(entries_fts) ASC, then updated_at DESC, id DESC.
 * On STORE_OK: same ownership as store_list. STORE_ERR_QUERY on bad MATCH syntax.
 */
typedef struct {
    const char *query; /* FTS5 MATCH string */
    ListQuery filters;
} SearchQuery;

StoreStatus store_search(Store *s, const SearchQuery *q, Entry **out_entries, size_t *out_count,
                         size_t *out_total);

/*
 * Hard-delete one entry. Under one write transaction: load snapshot, remove FTS
 * row, DELETE entry (CASCADE entry_tags), GC orphan tags. *out_deleted is a
 * heap snapshot of the removed row (caller frees with store_entry_free).
 * STORE_ERR_NOT_FOUND if missing.
 */
StoreStatus store_delete_by_id(Store *s, long long id, Entry *out_deleted);

/* Same as store_delete_by_id, located by normalized key. */
StoreStatus store_delete_by_key(Store *s, const char *key, Entry *out_deleted);

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
