#ifndef REMEMBER_DIAG_H
#define REMEMBER_DIAG_H

#include <stdbool.h>

#include "store.h"

/*
 * Closed error-code catalog (design log 006, E4.3). One string per code.
 * Warning codes are a separate catalog (E4.6, amended by D5).
 */

typedef enum {
    DIAG_USAGE = 0,
    DIAG_NOT_FOUND,
    DIAG_DATABASE,
    DIAG_OUT_OF_MEMORY,
    DIAG_INTERNAL,
    DIAG_INVALID_QUERY,
    DIAG_KEY_CONFLICT,
    DIAG_BODY_CONFLICT,
    DIAG_EXPIRED,
    DIAG_NOT_EXPIRED,
    DIAG_DELETED,
    DIAG_NOT_DELETED,
    DIAG_NOT_SINGLE_DEVICE,
    DIAG_SELF_LINK,
    DIAG_CYCLE,
    DIAG_SOURCE_TOO_OLD,
    DIAG_UNIQUE_TAKEN,
    DIAG_STALE,
    DIAG_NEWER_DATABASE,
    DIAG_UNSUPPORTED_DATABASE,
    DIAG_IMPORT_PARSE,
    DIAG_BUSY,
    DIAG_READ_ONLY,
    DIAG_DISK_FULL,
    DIAG_CANNOT_OPEN,
    DIAG_NOT_A_DATABASE,
    DIAG_CORRUPT,
    DIAG_IO_ERROR,
    DIAG_CODE_COUNT
} DiagCode;

/* Code token ("usage"), or NULL when code is outside the catalog. */
const char *diag_code_name(DiagCode code);

/* True and writes *out when name is exactly one catalog token. */
bool diag_code_from_name(const char *name, DiagCode *out);

/* Warning codes (E4.6 + D5). Not members of the error catalog. */
typedef enum {
    DIAG_WARN_RAW_QUERY_IGNORED = 0,
    DIAG_WARN_DEPRECATED_FLAG,
    DIAG_WARN_SYNCED_FOLDER,
    DIAG_WARN_COUNT
} DiagWarning;

const char *diag_warning_name(DiagWarning code);
bool diag_warning_from_name(const char *name, DiagWarning *out);

/*
 * Write one error to the error stream.
 * Plain (--json-errors off): "remember: <message>\n".
 * JSON (--json-errors on, see diag_set_json_errors):
 * {"version":1,"error":{"code":"<code>","message":"<message>"}} and no plain line. message is the
 * plain text without the "remember: " prefix (E4.4). NULL message is empty. Returns 0, or -1 when
 * code is outside the catalog or the write fails.
 */
int diag_error(DiagCode code, const char *message);

/*
 * Write one SQLite failure (E4.8 text, E4.9 detail). code is one of the eight
 * SQLite codes (busy, read_only, disk_full, cannot_open, not_a_database,
 * corrupt, io_error, database); detail carries the captured failure (never
 * NULL, class never NONE); path is the brain path or NULL (emitted absolute
 * per D7, only for the six codes E4.9 names; a path that will not absolutize
 * is omitted). Plain: "remember: database error: <cause> (<SQLITE_NAME>).
 * <fix>" (database: "remember: database error (<SQLITE_NAME>). Try again. If
 * it keeps happening, report the details from --json-errors."). JSON: the
 * E4.4 message plus "detail" (sqlite_code, sqlite_extended, sqlite_name,
 * sqlite_message always; errno only non-zero; path only for the six codes;
 * reason cannot_open only; fts_only corrupt only). Returns 0, or -1 when the
 * code is not a SQLite code, detail is unusable, or the write fails.
 */
int diag_error_sqlite(DiagCode code, const StoreError *detail, const char *path);

/*
 * Write one warning. Plain: "remember: warning: <message>\n" for every code
 * (Itay 2026-10-15; amends E4.6 "note:" and D5). JSON:
 * {"version":1,"warning":{"code":"<code>","message":"<message>"}}; message is
 * the <text> without "warning: ". NULL message is empty. deprecated_flag and
 * synced_folder print even when the command then fails (Itay 2026-10-15);
 * raw_query_ignored is called only by a search that succeeds (E4.6).
 * Returns -1 when code is outside the warning catalog or the write fails.
 */
int diag_warning(DiagWarning code, const char *message);

/*
 * Process-wide --json-errors switch. remember_run sets it for one call and
 * clears it. diag_error and diag_warning read it, so no caller passes the
 * mode. ponytail: one flag, same reason as the appio streams (single-threaded CLI).
 */
void diag_set_json_errors(bool enabled);

#endif /* REMEMBER_DIAG_H */
