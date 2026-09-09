#include "normalize.h"

#include "sha256.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* ---- ASCII / UTF-8 byte constants (RFC 3629) -----------------------------
 * Kept unsigned so the mask/shift arithmetic below stays unsigned (avoids
 * sign-conversion on the unsigned-int code point and signed-bitwise shifts). */
static const unsigned ASCII_MAX = 0x7FU;             /* last single-byte code point */
static const unsigned ASCII_DEL = 0x7FU;             /* DEL control */
static const unsigned ASCII_FIRST_PRINTABLE = 0x20U; /* first non-control byte */

static const unsigned UTF8_CONT_MASK = 0xC0U;
static const unsigned UTF8_CONT_TAG = 0x80U;
static const unsigned UTF8_CONT_PAYLOAD = 0x3FU;
static const unsigned UTF8_CONT_SHIFT = 6U;
static const unsigned UTF8_LEAD2_MASK = 0xE0U;
static const unsigned UTF8_LEAD2_TAG = 0xC0U;
static const unsigned UTF8_LEAD2_MIN = 0xC2U;
static const unsigned UTF8_LEAD2_PAYLOAD = 0x1FU;
static const unsigned UTF8_LEAD3_MASK = 0xF0U;
static const unsigned UTF8_LEAD3_TAG = 0xE0U;
static const unsigned UTF8_LEAD3_PAYLOAD = 0x0FU;
static const unsigned UTF8_LEAD4_MASK = 0xF8U;
static const unsigned UTF8_LEAD4_TAG = 0xF0U;
static const unsigned UTF8_LEAD4_MAX = 0xF4U;
static const unsigned UTF8_LEAD4_PAYLOAD = 0x07U;

static const unsigned CP_2BYTE_MIN = 0x800U;
static const unsigned CP_SURROGATE_MIN = 0xD800U;
static const unsigned CP_SURROGATE_MAX = 0xDFFFU;
static const unsigned CP_4BYTE_MIN = 0x10000U;
static const unsigned CP_MAX = 0x10FFFFU;

static const unsigned HEX_NIBBLE_MASK = 0x0FU;

/* ---- ASCII class helpers ------------------------------------------------- */

static bool is_ascii_ws(unsigned char c)
{
    /* Design: space, tab, LF, CR, VT, FF. */
    /* Explicit true/false: C relational ops yield int (clang-tidy). */
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') {
        return true;
    }
    return false;
}

static bool is_ascii_control(unsigned char c)
{
    if (c < ASCII_FIRST_PRINTABLE || c == ASCII_DEL) {
        return true;
    }
    return false;
}

/* Token forbids ASCII control and non-space whitespace (after outer trim).
   Internal spaces are allowed; edges are already trimmed by the caller.
   ponytail: internal space runs are kept as-typed (not collapsed), same as
   the body — collapse in normalize_token if "a  b" vs "a b" duplicates bite. */
static bool is_token_forbidden(unsigned char c)
{
    if (c == ' ') {
        return false;
    }
    if (is_ascii_ws(c) || is_ascii_control(c)) {
        return true;
    }
    return false;
}

/* A half-open [start, end) byte range. */
typedef struct {
    size_t start;
    size_t end;
} Span;

/* [start, end) after stripping leading/trailing ASCII whitespace. */
static Span ascii_ws_trim_span(const char *s, size_t len)
{
    size_t start = 0;
    size_t end = len;

    while (start < end && is_ascii_ws((unsigned char)s[start])) {
        start++;
    }
    while (end > start && is_ascii_ws((unsigned char)s[end - 1U])) {
        end--;
    }
    return (Span){.start = start, .end = end};
}

/* ---- UTF-8 (RFC 3629) ---------------------------------------------------- */

/* Decode one non-ASCII lead byte into need/cp_prefix; false = invalid lead. */
static bool utf8_lead(unsigned char c, size_t *need, unsigned int *cp)
{
    if ((c & UTF8_LEAD2_MASK) == UTF8_LEAD2_TAG) {
        if (c < UTF8_LEAD2_MIN) {
            return false; /* overlong 2-byte */
        }
        *need = 2;
        *cp = c & UTF8_LEAD2_PAYLOAD;
        return true;
    }
    if ((c & UTF8_LEAD3_MASK) == UTF8_LEAD3_TAG) {
        *need = 3;
        *cp = c & UTF8_LEAD3_PAYLOAD;
        return true;
    }
    if ((c & UTF8_LEAD4_MASK) == UTF8_LEAD4_TAG) {
        if (c > UTF8_LEAD4_MAX) {
            return false; /* would exceed U+10FFFF */
        }
        *need = 4;
        *cp = c & UTF8_LEAD4_PAYLOAD;
        return true;
    }
    return false;
}

/* Append continuation bytes; false if truncated or bad continuation. */
static bool utf8_cont(const char *s, size_t len, size_t i, size_t need, unsigned int *cp)
{
    size_t j = 0;

    if (i + need > len) {
        return false;
    }
    for (j = 1; j < need; j++) {
        unsigned char cc = (unsigned char)s[i + j];
        if ((cc & UTF8_CONT_MASK) != UTF8_CONT_TAG) {
            return false;
        }
        *cp = (*cp << UTF8_CONT_SHIFT) | (cc & UTF8_CONT_PAYLOAD);
    }
    return true;
}

/* A decoded scalar: the sequence length that produced it and its code point.
   Grouped so the two convertible values cannot be transposed at the call site. */
typedef struct {
    size_t need;
    unsigned int cp;
} Utf8Scalar;

/* Reject overlong encodings, surrogates, and out-of-range scalar values. */
static bool utf8_cp_ok(Utf8Scalar dec)
{
    if (dec.need == 3U) {
        if (dec.cp < CP_2BYTE_MIN) {
            return false;
        }
        if (dec.cp >= CP_SURROGATE_MIN && dec.cp <= CP_SURROGATE_MAX) {
            return false;
        }
        return true;
    }
    if (dec.need == 4U) {
        if (dec.cp >= CP_4BYTE_MIN && dec.cp <= CP_MAX) {
            return true;
        }
        return false;
    }
    return true; /* 2-byte lead already rejected overlongs */
}

/*
 * Validate that s[0..len) is well-formed UTF-8.
 * Rejects overlong encodings, surrogates, and code points above U+10FFFF.
 */
static bool utf8_is_valid(const char *s, size_t len)
{
    size_t i = 0;

    while (i < len) {
        unsigned char c = (unsigned char)s[i];
        size_t need = 0;
        unsigned int cp = 0;

        if (c <= ASCII_MAX) {
            i++;
            continue;
        }
        if (!utf8_lead(c, &need, &cp)) {
            return false;
        }
        if (!utf8_cont(s, len, i, need, &cp)) {
            return false;
        }
        if (!utf8_cp_ok((Utf8Scalar){.need = need, .cp = cp})) {
            return false;
        }
        i += need;
    }
    return true;
}

/* ---- body ---------------------------------------------------------------- */

NormStatus body_trim_copy(const char *src, size_t src_len, char **out, size_t *out_len)
{
    size_t start = 0;
    size_t end = 0;
    size_t n = 0;
    char *buf = NULL;

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
       body, its length, and its hash all agree (no bytes after the terminator).
       Pure C11 (no strnlen — POSIX, and IWYU/glibc hide it under -std=c11). */
    {
        size_t i = 0;
        for (i = 0; i < src_len; i++) {
            if (src[i] == '\0') {
                src_len = i;
                break;
            }
        }
    }

    {
        Span span = ascii_ws_trim_span(src, src_len);
        start = span.start;
        end = span.end;
    }
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

    buf = (char *)malloc(n + 1U);
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
    size_t start = 0;
    size_t end = 0;
    size_t n = 0;
    size_t i = 0;

    /* No usable output buffer is a caller bug, not an over-long token. */
    if (out == NULL || out_cap == 0U) {
        return NORM_ERR_INTERNAL;
    }
    out[0] = '\0';

    if (src == NULL) {
        return NORM_ERR_EMPTY;
    }

    {
        Span span = ascii_ws_trim_span(src, strlen(src));
        start = span.start;
        end = span.end;
    }
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
    size_t i = 0;
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
        out_hex[hi] = k_hex[((unsigned int)b >> 4U) & HEX_NIBBLE_MASK];
        out_hex[lo] = k_hex[b & HEX_NIBBLE_MASK];
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
