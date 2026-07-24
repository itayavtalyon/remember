#ifndef REMEMBER_NORMALIZE_H
#define REMEMBER_NORMALIZE_H

#include <stddef.h>

/* Design: body max after trim is 64 KiB. */
#define REMEMBER_BODY_MAX 65536

/* Design: tag/key max UTF-8 bytes after trim. */
#define REMEMBER_TOKEN_MAX 64

/* Lowercase hex SHA-256 (no NUL). Buffer needs +1 for terminator. */
#define REMEMBER_SHA256_HEX_LEN 64

typedef enum {
    NORM_OK = 0,
    NORM_ERR_EMPTY,        /* empty after trim */
    NORM_ERR_TOO_LONG,     /* body > 64 KiB or token > 64 bytes */
    NORM_ERR_INVALID_UTF8, /* ill-formed UTF-8 sequence */
    NORM_ERR_INVALID_CHAR, /* ASCII whitespace or control inside token */
    NORM_ERR_OOM           /* allocation failure (body_trim_copy only) */
} NormStatus;

/*
 * Trim leading/trailing ASCII whitespace from src[0..src_len).
 * Reject empty, length > REMEMBER_BODY_MAX, or invalid UTF-8.
 * On NORM_OK, *out is a heap NUL-terminated copy of the trimmed body (free
 * with free()). If out_len is non-NULL, *out_len is the byte length (no NUL).
 * src may be NULL only when src_len is 0.
 */
NormStatus body_trim_copy(const char *src, size_t src_len, char **out, size_t *out_len);

/*
 * Shared tag/key algorithm (design: identical rules).
 * Trim; reject empty / invalid UTF-8 / internal ASCII whitespace or control /
 * length > REMEMBER_TOKEN_MAX; ASCII A–Z → a–z. Writes NUL-terminated result
 * into out. out_cap should be at least REMEMBER_TOKEN_MAX + 1.
 */
NormStatus normalize_token(const char *src, char *out, size_t out_cap);

/* Aliases for call-site clarity; same implementation as normalize_token. */
NormStatus normalize_tag(const char *src, char *out, size_t out_cap);
NormStatus normalize_key(const char *src, char *out, size_t out_cap);

/*
 * Lowercase hex SHA-256 of data[0..len) into out_hex
 * (must provide REMEMBER_SHA256_HEX_LEN + 1 bytes).
 */
void body_hash_hex(const void *data, size_t len, char out_hex[REMEMBER_SHA256_HEX_LEN + 1]);

/* Short ASCII label for st (no trailing newline). Never NULL. */
const char *norm_status_string(NormStatus st);

#endif /* REMEMBER_NORMALIZE_H */
