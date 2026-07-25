#ifndef REMEMBER_UTIL_H
#define REMEMBER_UTIL_H

#include <stddef.h>

/*
 * Hard cap on raw stdin bytes — a memory guard only, deliberately far above the
 * body limit. The real "64 KiB after trim" rule is enforced downstream by
 * body_trim_copy, so a stdin body and the same argv body validate identically
 * (surrounding whitespace no longer trips a premature reject). Only pathological
 * input beyond this cap is refused outright.
 */
#define REMEMBER_STDIN_MAX ((size_t)16U * 1024U * 1024U)

/* Resolve db path: --db if set, else REMEMBER_DB, else ~/.remember/remember.db.
 * Writes into buf (NUL-terminated). Returns 0 on success, -1 on error
 * (message in err if usable). Rejects empty, ":memory:", and "file:" URIs.
 */
int util_resolve_db_path(const char *cli_db, char *buf, size_t buflen, char *err, size_t errlen);

/* Read all of stdin into a heap buffer (NUL-terminated). Caps at max_len bytes
 * of content; if more is available, returns -2 (too large) and frees any
 * partial buffer. Returns 0 and *out on success; -1 on I/O/OOM (*out NULL).
 */
int util_read_stdin(char **out, size_t *out_len, size_t max_len);

#endif /* REMEMBER_UTIL_H */
