#include "normalize.h"

#include "sha256.h"

#include <stdlib.h>
#include <string.h>

/* ---- ASCII class helpers ------------------------------------------------- */

static int is_ascii_ws(unsigned char c)
{
    /* Design: space, tab, LF, CR, VT, FF. */
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static int is_ascii_control(unsigned char c)
{
    return c < 0x20U || c == 0x7FU;
}

/* Token forbids ASCII whitespace and control (after outer trim). */
static int is_token_forbidden(unsigned char c)
{
    return is_ascii_ws(c) || is_ascii_control(c);
}

/* Span [start, end) after stripping leading/trailing ASCII whitespace. */
static void ascii_ws_trim_span(const char *s, size_t len, size_t *start_out, size_t *end_out)
{
    size_t start = 0;
    size_t end = len;

    while (start < end && is_ascii_ws((unsigned char)s[start])) {
        start++;
    }
    while (end > start && is_ascii_ws((unsigned char)s[end - 1U])) {
        end--;
    }
    *start_out = start;
    *end_out = end;
}

/* ---- UTF-8 (RFC 3629) ---------------------------------------------------- */

/* Decode one non-ASCII lead byte into need/cp_prefix; 0 = invalid lead. */
static int utf8_lead(unsigned char c, size_t *need, unsigned int *cp)
{
    if ((c & 0xE0U) == 0xC0U) {
        if (c < 0xC2U) {
            return 0; /* overlong 2-byte */
        }
        *need = 2;
        *cp = c & 0x1FU;
        return 1;
    }
    if ((c & 0xF0U) == 0xE0U) {
        *need = 3;
        *cp = c & 0x0FU;
        return 1;
    }
    if ((c & 0xF8U) == 0xF0U) {
        if (c > 0xF4U) {
            return 0; /* would exceed U+10FFFF */
        }
        *need = 4;
        *cp = c & 0x07U;
        return 1;
    }
    return 0;
}

/* Append continuation bytes; returns 0 if truncated or bad continuation. */
static int utf8_cont(const char *s, size_t len, size_t i, size_t need, unsigned int *cp)
{
    size_t j;

    if (i + need > len) {
        return 0;
    }
    for (j = 1; j < need; j++) {
        unsigned char cc = (unsigned char)s[i + j];
        if ((cc & 0xC0U) != 0x80U) {
            return 0;
        }
        *cp = (*cp << 6) | (cc & 0x3FU);
    }
    return 1;
}

/* Reject overlong encodings, surrogates, and out-of-range scalar values. */
static int utf8_cp_ok(size_t need, unsigned int cp)
{
    if (need == 3U) {
        if (cp < 0x800U) {
            return 0;
        }
        if (cp >= 0xD800U && cp <= 0xDFFFU) {
            return 0;
        }
        return 1;
    }
    if (need == 4U) {
        return cp >= 0x10000U && cp <= 0x10FFFFU;
    }
    return 1; /* 2-byte lead already rejected overlongs */
}

/*
 * Validate that s[0..len) is well-formed UTF-8.
 * Rejects overlong encodings, surrogates, and code points above U+10FFFF.
 */
static int utf8_is_valid(const char *s, size_t len)
{
    size_t i = 0;

    while (i < len) {
        unsigned char c = (unsigned char)s[i];
        size_t need = 0;
        unsigned int cp = 0;

        if (c <= 0x7FU) {
            i++;
            continue;
        }
        if (!utf8_lead(c, &need, &cp)) {
            return 0;
        }
        if (!utf8_cont(s, len, i, need, &cp)) {
            return 0;
        }
        if (!utf8_cp_ok(need, cp)) {
            return 0;
        }
        i += need;
    }
    return 1;
}

/* ---- body ---------------------------------------------------------------- */

NormStatus body_trim_copy(const char *src, size_t src_len, char **out, size_t *out_len)
{
    size_t start;
    size_t end;
    size_t n;
    char *buf;

    if (out == NULL) {
        return NORM_ERR_INTERNAL;
    }
    *out = NULL;
    if (out_len != NULL) {
        *out_len = 0;
    }

    if (src == NULL) {
        return NORM_ERR_EMPTY;
    }

    /* NUL ends the string: ignore anything past the first NUL so the stored
       body, its length, and its hash all agree (no bytes after the terminator). */
    src_len = strnlen(src, src_len);

    ascii_ws_trim_span(src, src_len, &start, &end);
    if (start >= end) {
        return NORM_ERR_EMPTY;
    }
    n = end - start;
    if (n > (size_t)REMEMBER_BODY_MAX) {
        return NORM_ERR_TOO_LONG;
    }
    if (!utf8_is_valid(src + start, n)) {
        return NORM_ERR_INVALID_UTF8;
    }

    buf = malloc(n + 1U);
    if (buf == NULL) {
        return NORM_ERR_OOM;
    }
    memcpy(buf, src + start, n);
    buf[n] = '\0';
    *out = buf;
    if (out_len != NULL) {
        *out_len = n;
    }
    return NORM_OK;
}

/* ---- tag / key ----------------------------------------------------------- */

NormStatus normalize_token(const char *src, char *out, size_t out_cap)
{
    size_t start;
    size_t end;
    size_t n;
    size_t i;

    /* No usable output buffer is a caller bug, not an over-long token. */
    if (out == NULL || out_cap == 0U) {
        return NORM_ERR_INTERNAL;
    }
    out[0] = '\0';

    if (src == NULL) {
        return NORM_ERR_EMPTY;
    }

    ascii_ws_trim_span(src, strlen(src), &start, &end);
    if (start >= end) {
        return NORM_ERR_EMPTY;
    }
    n = end - start;
    if (n > (size_t)REMEMBER_TOKEN_MAX) {
        return NORM_ERR_TOO_LONG;
    }
    /* Token fits the spec but not the caller's buffer: contract violation. */
    if (n + 1U > out_cap) {
        return NORM_ERR_INTERNAL;
    }
    if (!utf8_is_valid(src + start, n)) {
        return NORM_ERR_INVALID_UTF8;
    }

    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)src[start + i];
        if (is_token_forbidden(c)) {
            return NORM_ERR_INVALID_CHAR;
        }
        /* ASCII casefold only. */
        if (c >= 'A' && c <= 'Z') {
            c = (unsigned char)(c - (unsigned char)'A' + (unsigned char)'a');
        }
        out[i] = (char)c;
    }
    out[n] = '\0';
    return NORM_OK;
}

NormStatus normalize_tag(const char *src, char *out, size_t out_cap)
{
    return normalize_token(src, out, out_cap);
}

NormStatus normalize_key(const char *src, char *out, size_t out_cap)
{
    return normalize_token(src, out, out_cap);
}

/* ---- hash ---------------------------------------------------------------- */

void body_hash_hex(const void *data, size_t len, char out_hex[REMEMBER_SHA256_HEX_LEN + 1])
{
    SHA256_CTX ctx;
    BYTE digest[SHA256_BLOCK_SIZE];
    size_t i;
    static const char k_hex[] = "0123456789abcdef";

    /* Empty-string digest is well-defined (NIST vector); callers reject empty
     * bodies before hashing, but zero-length remains valid for tests. */
    sha256_init(&ctx);
    if (data != NULL && len > 0U) {
        sha256_update(&ctx, (const BYTE *)data, len);
    }
    sha256_final(&ctx, digest);

    for (i = 0; i < SHA256_BLOCK_SIZE; i++) {
        unsigned char b = digest[i];
        size_t hi = i * 2U;
        size_t lo = hi + 1U;
        out_hex[hi] = k_hex[(b >> 4) & 0x0FU];
        out_hex[lo] = k_hex[b & 0x0FU];
    }
    out_hex[REMEMBER_SHA256_HEX_LEN] = '\0';
}

const char *norm_status_string(NormStatus st)
{
    switch (st) {
    case NORM_OK:
        return "ok";
    case NORM_ERR_EMPTY:
        return "empty";
    case NORM_ERR_TOO_LONG:
        return "too long";
    case NORM_ERR_INVALID_UTF8:
        return "invalid UTF-8";
    case NORM_ERR_INVALID_CHAR:
        return "invalid character";
    case NORM_ERR_OOM:
        return "out of memory";
    case NORM_ERR_INTERNAL:
        return "internal error";
    default:
        return "unknown normalize error";
    }
}
