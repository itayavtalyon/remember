#ifndef REMEMBER_UTIL_H
#define REMEMBER_UTIL_H

#include <stddef.h>

/*
 * Memory guard only, far above the 64 KiB post-trim body limit. body_trim_copy
 * owns the real size rule so stdin and argv bodies share one enforcer.
 */
#define REMEMBER_STDIN_MAX ((size_t)16U * 1024U * 1024U)

/* Precedence: --db > REMEMBER_DB > ~/.remember/remember.db.
 * Rejects :memory: and file: URIs (silent data-loss via sqlite open flags).
 */
int util_resolve_db_path(const char *cli_db, char *buf, size_t buflen, char *err, size_t errlen);

/* Substring markers only (not a real FS-type probe). Callers warn on stderr. */
int util_path_looks_synced(const char *path);

/* Heap buffer, NUL-terminated. -2 if content exceeds max_len; -1 I/O or OOM. */
int util_read_stdin(char **out, size_t *out_len, size_t max_len);

#endif /* REMEMBER_UTIL_H */
