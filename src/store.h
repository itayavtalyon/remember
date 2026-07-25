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
    STORE_ERR_INTERNAL
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
 * List entries newest-first (updated_at DESC, id DESC).
 * limit 0 means default (100). offset skips that many.
 * On STORE_OK: *out_entries is a heap array of *out_count Entries (free each
 * with store_entry_free, then free the array); *out_total is the unpaged count.
 */
StoreStatus store_list(Store *s, size_t limit, size_t offset, Entry **out_entries,
                       size_t *out_count, size_t *out_total);

#endif /* REMEMBER_STORE_H */
