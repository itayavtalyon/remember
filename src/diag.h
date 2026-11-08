#ifndef REMEMBER_DIAG_H
#define REMEMBER_DIAG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

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
    DIAG_SCHEMA_TOO_NEW,
    DIAG_INVALID_ARGUMENT,
    DIAG_CODE_COUNT
} DiagCode;

/* Code token ("usage"), or NULL when code is outside the catalog. */
const char *diag_code_name(DiagCode code);

/* True and writes *out when name is exactly one catalog token. */
bool diag_code_from_name(const char *name, DiagCode *out);

/* The code's error.fix sentence; NULL for usage (built from the command) and
   for a code outside the catalog. */
const char *diag_code_fix(DiagCode code);

/* Warning codes (E4.6 + D5). Not members of the error catalog. */
typedef enum {
    DIAG_WARN_RAW_QUERY_IGNORED = 0,
    DIAG_WARN_DEPRECATED_FLAG,
    DIAG_WARN_SYNCED_FOLDER,
    DIAG_WARN_SETTING_INVALID,
    DIAG_WARN_FLAG_MEANING_CHANGED, /* --trash, one release (M-23) */
    DIAG_WARN_UNKNOWN_FIELD,        /* C-35, once per file or stdin */
    DIAG_WARN_COUNT
} DiagWarning;

const char *diag_warning_name(DiagWarning code);
bool diag_warning_from_name(const char *name, DiagWarning *out);

/*
 * Report one error.
 * Plain: "remember: <message>\n" on the error stream.
 * JSON (between diag_json_begin and diag_json_end): buffers
 * {"code","message","fix","retryable"} for the envelope and writes nothing.
 * message is the plain text without the "remember: " prefix (E4.4); fix is
 * the code's (usage: the command's help); retryable is true for busy,
 * io_error and out_of_memory. Only the first error of a run is kept. NULL
 * message is empty. Returns 0, or -1 when code is outside the catalog or the
 * write fails.
 */
int diag_error(DiagCode code, const char *message);

/* message and fix are named so the two sentences cannot be swapped.
   Plain prints message, a space, and fix. JSON keeps them split and uses
   fix instead of the catalog sentence. */
typedef struct {
    const char *message;
    const char *fix;
} DiagSplit;

int diag_error_split(DiagCode code, DiagSplit split);

/*
 * Write one SQLite failure (E4.8 text, E4.9 detail). code is one of the eight
 * SQLite codes (busy, read_only, disk_full, cannot_open, not_a_database,
 * corrupt, io_error, database); detail carries the captured failure (never
 * NULL, class never NONE); path is the brain path or NULL (emitted absolute
 * per D7, only for the six codes E4.9 names; a path that will not absolutize
 * is omitted). Plain: "remember: database error: <cause> (<SQLITE_NAME>).
 * <fix>" (database: "remember: database error (<SQLITE_NAME>). Try again. If
 * it keeps happening, report the details from --json."). JSON: the E4.4
 * message, the E4.8 fix as "fix", plus "detail" (sqlite_code, sqlite_extended, sqlite_name,
 * sqlite_message always; errno only non-zero; path only for the six codes;
 * reason cannot_open only; fts_only corrupt only). Returns 0, or -1 when the
 * code is not a SQLite code, detail is unusable, or the write fails.
 */
int diag_error_sqlite(DiagCode code, const StoreError *detail, const char *path);

/*
 * Write one warning. Plain: "remember: warning: <message>\n" (Itay
 * 2026-10-15; amends E4.6 "note:" and D5), except raw_query_ignored, which is
 * "remember: warning: raw query ignored: <message>\n" (E2.3). JSON: buffers
 * {"code","message"} as one item of the envelope's warnings; message is the
 * <text> without "warning: ". NULL message is empty. deprecated_flag and
 * synced_folder print even when the command then fails (Itay 2026-10-15);
 * raw_query_ignored is called only by a search that succeeds (E4.6).
 * Returns -1 when code is outside the warning catalog or the write fails.
 */
int diag_warning(DiagWarning code, const char *message);

/* One string member of a warning's JSON "detail" object. */
typedef struct {
    const char *name;
    const char *value;
} DiagDetailField;

/*
 * diag_warning plus a JSON "detail" object of string members, in the given
 * order, after "message". The plain line is the same as diag_warning's.
 * nfields 0 writes no detail. Returns -1 like diag_warning, or when fields is
 * NULL and nfields is not 0.
 */
int diag_warning_detail(DiagWarning code, const char *message, const DiagDetailField *fields,
                        size_t nfields);

/* path NULL is stdin. name is the unknown field. */
typedef struct {
    const char *path;
    const char *name;
} DiagUnknownField;

/*
 * C-35. Warn once for this path, or once for stdin. The caller keeps reading.
 * Plain is the file line or the stdin line. JSON code is unknown_field.
 * detail is field, then where (the path, or stdin). Returns -1 when name is
 * NULL or the warning cannot be written. A later field in the same file or
 * stream returns 0 and prints nothing.
 */
int diag_unknown_field(DiagUnknownField field);

/* Zero the warning count and the once-per-file set at the start of remember_run. */
void diag_warnings_reset(void);

/* Warnings written since the reset, plain and JSON. */
size_t diag_warning_count(void);

/*
 * --json for one remember_run: from diag_json_begin to diag_json_end, errors
 * and warnings are buffered for the stdout envelope, and nothing goes to the
 * error stream (JE-2.8). command names the usage fix ("get" gives "Run
 * 'remember get --help' ..."); NULL when no command is known. Returns -1 when
 * the buffers cannot be made (out of memory); diag stays in plain mode, and
 * the caller still calls diag_json_end.
 * ponytail: process-wide, same reason as the appio streams (single-threaded CLI).
 */
int diag_json_begin(const char *command);

/* True when this run buffered an error. */
bool diag_json_has_error(void);

/* "error":{...}, the run's first error. -1 when a write fails. */
int diag_json_write_error(FILE *out);

/* ,"warnings":[...] when any warning was buffered; nothing otherwise. -1 when a write fails. */
int diag_json_write_warnings(FILE *out);

/* Frees the buffers and returns to plain mode. Safe when not begun. */
void diag_json_end(void);

#endif /* REMEMBER_DIAG_H */
