#ifndef REMEMBER_COMMANDS_COMMON_H
#define REMEMBER_COMMANDS_COMMON_H

/*
 * Shared helpers for command TUs only (cmd_*.c). Not a public API —
 * callers outside the commands layer use commands.h.
 */

#include "diag.h"
#include "normalize.h"
#include "store.h"

#include <stdbool.h>
#include <stddef.h>

/* Stack buffer size for a canonical ISO-8601 UTC timestamp
   ("YYYY-MM-DDTHH:MM:SS.mmmZ", 24 bytes + NUL). Must be >= 25. */
enum { ISO_TS_BUFSIZE = 32 };

void err_msg(DiagCode code, const char *msg);
#ifdef __GNUC__
__attribute__((format(printf, 2, 3)))
#endif
void err_fmt(DiagCode code, const char *fmt, ...);

/* SQLITE failures (E4.7 class → E4.8 text + E4.9 detail); anything else keeps
   the code_for_store_status code with the legacy store_status_message text. */
void err_store(const Store *s, StoreStatus st);
void err_norm(NormStatus st, const char *kind);

/* usage, except "out of memory" and the internal sentences D9 names. */
DiagCode code_for_cmd_text(const char *msg);

/* store_open / device err buffer. decision:plan16-open-message-codes. */
DiagCode code_for_store_text(const char *msg);

DiagCode code_for_store_status(StoreStatus st);
DiagCode code_for_norm(NormStatus st);

/* Structured failure class → catalog code (NONE is a store bug: internal). */
DiagCode code_for_store_class(StoreErrorClass error_class);

/* Emit one captured SQLite failure (E4.8 text, E4.9 detail with path).
   A NULL or NONE detail is a store bug: internal error. */
void err_store_sqlite(const StoreError *detail, const char *path);

/* Parse a positive entry id token. 0 ok, -1 invalid (does not print). */
int parse_entry_id(const char *raw, long long *out_id);

const char *norm_token_message(NormStatus st, const char *kind);

const char *action_name(StoreAddAction a);

/* Growable argv-alias list (tags). Does not own the strings. */
int push_cstr_ptr(const char ***arr, size_t *n, size_t *cap, const char *t);

/* Result of take_value: rc 0 with value set, or rc -1 with err (the missing_msg). */
typedef struct {
    const char *value;
    const char *err;
    int rc;
    char pad_[4]; /* explicit tail padding (kept -Wpadded-clean) */
} TakeValue;

/* Take the next token as an option value; advances *i on success. */
TakeValue take_value(int *i, int rest_argc, const char **rest_argv, const char *missing_msg);

int normalize_tags(const char *const *tag_raw, size_t ntag_raw, char ***out_tags, size_t *out_ntags,
                   const char **err);
void free_tag_list(char **tags, size_t ntags);

/* Map store status to process exit; prints the E4.8 line for SQLITE failures. */
int store_status_to_exit(const Store *s, StoreStatus st);

/* Parsed --expired / --deleted / --trash (alias of --expired). */
typedef struct {
    bool expired;
    bool deleted;
    bool trash_alias;
    /* NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers) */
    char pad_[5]; /* explicit tail padding (kept -Wpadded-clean) */
} CmdBinOpts;

/* 1 if arg is a bin flag and was recorded; 0 if not a bin flag. */
int cmd_bin_take_flag(const char *arg, CmdBinOpts *opts);

/* Deprecation stderr for --trash; mutex --expired/--deleted. 0 ok. */
int cmd_bin_resolve(const CmdBinOpts *opts, StoreBin *out);

/* Compact *n in place, freeing neighbors whose bin is deleted. */
void cmd_neighbors_drop_deleted(StoreNeighbor *rows, size_t *n, const char *now);

/*
 * Load body from argv token or stdin. Applies body_trim_copy (64 KiB / UTF-8 /
 * empty). On success *out_body is heap-owned. Returns 0 or -1 with *err.
 *
 * dash_is_stdin non-zero: body_raw "-" reads stdin (CLI convention).
 * dash_is_stdin zero: "-" is a literal one-character body (after `--` on add,
 * or `--text=-` on update).
 */
int load_body(const char *body_raw, int dash_is_stdin, char **out_body, size_t *out_len,
              const char **err);

/* The mutually-exclusive --ttl / --expires flag values (either may be NULL). */
typedef struct {
    const char *ttl_raw;
    const char *expires_raw;
} ExpiryFlags;

/* Result of resolve_expiry_flags: rc 0 with expires (NULL if neither flag given,
   else points into the caller's out buffer), or rc -1 with err. */
typedef struct {
    const char *expires;
    const char *err;
    int rc;
    char pad_[4]; /* explicit tail padding (kept -Wpadded-clean) */
} ExpiryResult;

/*
 * Resolve the --ttl / --expires mutex. On rc 0, expires is NULL (neither) or points
 * into out (canonical .mmmZ); out must outlive use of expires. rc -1 sets err.
 */
ExpiryResult resolve_expiry_flags(ExpiryFlags flags, const char *now, char *out, size_t outlen);

/*
 * Filter timestamp (E5.2a–E5.2c). On 0, out holds the canonical
 * YYYY-MM-DDTHH:MM:SS.mmmZ form. On -1, out is unchanged. Does not print.
 * out must hold ISO_TS_BUFSIZE bytes. Date-only is local midnight.
 */
int parse_filter_ts(const char *in, char out[ISO_TS_BUFSIZE]);

#endif /* REMEMBER_COMMANDS_COMMON_H */
