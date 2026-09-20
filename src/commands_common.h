#ifndef REMEMBER_COMMANDS_COMMON_H
#define REMEMBER_COMMANDS_COMMON_H

/*
 * Shared helpers for command TUs only (cmd_*.c). Not a public API —
 * callers outside the commands layer use commands.h.
 */

#include "normalize.h"
#include "store.h"

#include <stddef.h>

/* Stack buffer size for a canonical ISO-8601 UTC timestamp
   ("YYYY-MM-DDTHH:MM:SS.mmmZ", 24 bytes + NUL). Must be >= 25. */
enum { ISO_TS_BUFSIZE = 32 };

void err_msg(const char *msg);

/* Parse a positive entry id token. 0 ok, -1 invalid (does not print). */
int parse_entry_id(const char *raw, long long *out_id);

const char *norm_token_message(NormStatus st, const char *kind);

int source_is_valid(const char *s);
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

/* Map store status to process exit; prints store_status_message on error. */
int store_status_to_exit(StoreStatus st);

/* Map a CLI --trash / --expired flag to the expired bin (default live). */
static inline StoreBin cmd_bin_expired(bool expired_flag)
{
    if (expired_flag) {
        return STORE_BIN_EXPIRED;
    }
    return STORE_BIN_LIVE;
}

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

#endif /* REMEMBER_COMMANDS_COMMON_H */
