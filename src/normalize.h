#ifndef REMEMBER_NORMALIZE_H
#define REMEMBER_NORMALIZE_H

#include <stdbool.h>
#include <stddef.h>

/* Design: body max after trim is 64 KiB. */
enum { REMEMBER_BODY_MAX = 65536 };

/* Design: tag/key max UTF-8 bytes after trim. */
enum { REMEMBER_TOKEN_MAX = 64 };

/* Lowercase hex SHA-256 (no NUL). Buffer needs +1 for terminator. */
enum { REMEMBER_SHA256_HEX_LEN = 64 };

typedef enum {
    NORM_OK = 0,
    NORM_ERR_EMPTY,        /* empty after trim */
    NORM_ERR_TOO_LONG,     /* body > 64 KiB or token > 64 bytes */
    NORM_ERR_INVALID_UTF8, /* ill-formed UTF-8 sequence */
    NORM_ERR_INVALID_CHAR, /* ASCII control or non-space whitespace inside token */
    NORM_ERR_OOM,          /* allocation failure (body_trim_copy only) */
    NORM_ERR_INTERNAL      /* caller contract violation: missing/too-small out buffer */
} NormStatus;

/*
 * Trim leading/trailing ASCII whitespace and copy.
 *
 * The body is a C string: the first NUL within src[0..src_len) ends it, and any
 * bytes after are ignored (so *out_len always equals strlen(*out)). Pass
 * src_len as the readable extent; strlen(src) is the common choice.
 *
 * Reject empty (NORM_ERR_EMPTY), length > REMEMBER_BODY_MAX (NORM_ERR_TOO_LONG),
 * or invalid UTF-8 (NORM_ERR_INVALID_UTF8). On NORM_OK, *out is a heap
 * NUL-terminated copy of the trimmed body (free with free()); if out_len is
 * non-NULL, *out_len is the byte length (no NUL). src == NULL → NORM_ERR_EMPTY
 * (src_len is ignored). out == NULL is a contract violation → NORM_ERR_INTERNAL.
 */
NormStatus body_trim_copy(const char *src, size_t src_len, char **out, size_t *out_len);

/*
 * Shared tag/key algorithm (design: identical rules).
 * Trim edges; internal ASCII spaces are allowed; reject empty / invalid UTF-8 /
 * internal ASCII control or non-space whitespace (tab, LF, CR, VT, FF) /
 * length > REMEMBER_TOKEN_MAX (NORM_ERR_TOO_LONG); ASCII A–Z → a–z. Writes
 * NUL-terminated result into out. out_cap must be at least REMEMBER_TOKEN_MAX + 1;
 * a missing or too-small buffer is a contract violation → NORM_ERR_INTERNAL
 * (distinct from an over-long token).
 */
NormStatus normalize_token(const char *src, char *out, size_t out_cap);

/* Aliases for call-site clarity; same implementation as normalize_token. */
NormStatus normalize_tag(const char *src, char *out, size_t out_cap);
NormStatus normalize_key(const char *src, char *out, size_t out_cap);

/*
 * Copy src with the Hebrew marks U+0591–U+05BD, U+05BF, U+05C1–U+05C2,
 * U+05C4–U+05C5 and U+05C7 removed. The stored body keeps the original bytes;
 * FTS rows use this copy. Invalid or truncated UTF-8 is copied one byte at a
 * time and is never read past src_len. A NUL inside src_len ends the input.
 * On NORM_OK, *out is a heap NUL-terminated string (free it). out == NULL or
 * src == NULL is a contract violation → NORM_ERR_INTERNAL.
 */
NormStatus fold_hebrew_marks(const char *src, size_t src_len, char **out);

/*
 * True when s[0..len) is well-formed UTF-8: no overlong encodings, no
 * surrogates, nothing above U+10FFFF.
 */
bool utf8_is_valid(const char *s, size_t len);

/* True for the E10.1 marks that fold_hebrew_marks removes. */
bool is_hebrew_mark(unsigned int cp);

/*
 * Shrink [*start, *end) of s past leading and trailing ASCII whitespace (space,
 * tab, LF, CR, VT, FF): the one trim every design rule means by "ASCII-trimmed".
 */
void ascii_ws_trim(const char *s, size_t *start, size_t *end);

/*
 * Lowercase hex SHA-256 of data[0..len) into out_hex
 * (must provide REMEMBER_SHA256_HEX_LEN + 1 bytes).
 */
void body_hash_hex(const void *data, size_t len, char out_hex[REMEMBER_SHA256_HEX_LEN + 1]);

/* Short ASCII label for st (no trailing newline). Never NULL. */
const char *norm_status_string(NormStatus st);

/*
 * Canonical stored forms. Import accepts a peer row only when its fields have
 * the shape a local write produces (decision:plan18c-validate-peer-fields).
 */

/* 36-char lowercase UUID v7 text: 8-4-4-4-12 hex, version 7, variant 8..b. */
bool uuid_v7_is_canonical(const char *s);

/* The UTC stamp every release writes: YYYY-MM-DDTHH:MM:SS.mmmZ (shape only). */
bool utc_stamp_is_canonical(const char *s);

/* A compact version vector: {} or {"<uuid v7>":<n>,...} with n a non-negative
   integer of at most 18 digits, no spaces, and no device twice. */
bool vv_json_is_canonical(const char *s);

/* One of the closed source tokens: human, agent, tool, share, unknown. */
bool source_is_valid(const char *s);

/* Bucket prefix (007 ST-5.4): at most 32 code points before the ':'. */
enum { REMEMBER_BUCKET_PREFIX_MAX_POINTS = 32 };
/* Output bytes: 32 four-byte code points, the ':', and the NUL. */
enum { REMEMBER_BUCKET_PREFIX_BUF = (REMEMBER_BUCKET_PREFIX_MAX_POINTS * 4) + 2 };

/*
 * 007 ST-5.4: 1 to 32 code points of \p{L}, \p{Nd}, '-', '_' or '.', then one
 * ':'. ASCII letters fold to lowercase. No trim and no NFC: the input is the
 * stored text. NORM_OK writes the folded prefix into out. An empty name or no
 * final ':' is NORM_ERR_EMPTY or NORM_ERR_INVALID_CHAR, any other code point
 * is NORM_ERR_INVALID_CHAR, more than 32 is NORM_ERR_TOO_LONG, ill-formed
 * UTF-8 is NORM_ERR_INVALID_UTF8; each of these leaves out empty. NULL src
 * or out, or out_cap below REMEMBER_BUCKET_PREFIX_BUF, is NORM_ERR_INTERNAL
 * and writes nothing.
 */
NormStatus bucket_prefix_fold(const char *src, char *out, size_t out_cap);

/* Canonical duration or size text, including the NUL. Long enough for any
   whole-millisecond or whole-byte value that fits in a long long. */
enum { NORM_MEASURE_CANON_CAP = 24 };

/*
 * Duration grammar ^[0-9]+(\.[0-9]{1,15})?(ms|s|m|h)$ as a whole number of
 * milliseconds, then the canonical unit: the largest of h, m, s, ms in which
 * the number is at least 1 and has at most 3 fraction digits. No trailing
 * zeros. 0 uses ms. False leaves *whole_ms unchanged. A buffer of
 * NORM_MEASURE_CANON_CAP bytes is left empty, including when src or
 * whole_ms is NULL. A shorter buffer is not written.
 */
bool norm_duration_canonical(const char *src, long long *whole_ms, char *out, size_t out_cap);

/* Same rules for ^[0-9]+(\.[0-9]{1,15})?(MiB|KiB|B)$ and a whole number of
   bytes. 0 uses B. */
bool norm_size_canonical(const char *src, long long *whole_bytes, char *out, size_t out_cap);

#endif /* REMEMBER_NORMALIZE_H */
