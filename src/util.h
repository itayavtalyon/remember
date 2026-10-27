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

/* user.db path. Does not create the file.
   REMEMBER_USER_DB when it is set and non-empty, else
   $XDG_DATA_HOME/remember/user.db when XDG_DATA_HOME is set and non-empty,
   else $HOME/.remember/user.db. An empty variable counts as unset.
   Rejects :memory: and file: URIs. */
int util_resolve_user_db_path(char *buf, size_t buflen, char *err, size_t errlen);

/* Substring markers only (not a real FS-type probe). Callers warn on stderr. */
int util_path_looks_synced(const char *path);

/* Heap buffer, NUL-terminated. -2 if content exceeds max_len; -1 I/O or OOM. */
int util_read_stdin(char **out, size_t *out_len, size_t max_len);

/*
 * Absolute form of path (D7): a relative path joins the cwd; "." and ".."
 * normalize lexically with no symlink resolution. Applies equally to --db,
 * REMEMBER_DB, and the built-in path. 0 on success, -1 on bad input, getcwd
 * failure, or a short buffer.
 */
int util_absolute_path(const char *path, char *buf, size_t buflen);

/* ASCII decimal digits only. Rejects empty text, space, any sign, trailing junk, and ERANGE. */
int util_parse_nonneg(const char *raw, unsigned long long *out);

#endif /* REMEMBER_UTIL_H */
