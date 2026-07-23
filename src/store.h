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

/* Open or create a store at path. Creates parent directories (mode 0700).
 * On success returns a non-NULL Store*. On failure returns NULL and, if
 * err/errlen are usable, writes a short message into err (NUL-terminated).
 * Schema: user_version 0 → create at 1; 1 → ok; >1 → refuse.
 */
Store *store_open(const char *path, char *err, size_t errlen);

/* Close and free. Safe with NULL. */
void store_close(Store *s);

#endif /* REMEMBER_STORE_H */
