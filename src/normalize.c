#include "normalize.h"

#include "sha256.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Last: under REMEMBER_TEST_HOOKS every malloc below goes through the shared
   fault hook (decision:plan18b-shared-alloc-hook). */
#ifdef REMEMBER_TEST_HOOKS
#include "test_alloc.h" // IWYU pragma: keep
#endif

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

void ascii_ws_trim(const char *s, size_t *start, size_t *end)
{
    while (*start < *end && is_ascii_ws((unsigned char)s[*start])) {
        (*start)++;
    }
    while (*end > *start && is_ascii_ws((unsigned char)s[*end - 1U])) {
        (*end)--;
    }
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
    char pad_[4]; /* explicit tail padding (kept -Wpadded-clean) */
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

bool utf8_is_valid(const char *s, size_t len)
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

/* Hebrew points and cantillation dropped from FTS rows (E10.1). Letters stay. */
static const unsigned HEBREW_MARK_BLOCK_LO = 0x0591U;
static const unsigned HEBREW_MARK_BLOCK_HI = 0x05BDU;
static const unsigned HEBREW_MARK_RAFE = 0x05BFU;
static const unsigned HEBREW_MARK_SHIN_LO = 0x05C1U;
static const unsigned HEBREW_MARK_SHIN_HI = 0x05C2U;
static const unsigned HEBREW_MARK_DOT_LO = 0x05C4U;
static const unsigned HEBREW_MARK_DOT_HI = 0x05C5U;
static const unsigned HEBREW_MARK_QAMATS = 0x05C7U;

bool is_hebrew_mark(unsigned int cp)
{
    if (cp >= HEBREW_MARK_BLOCK_LO && cp <= HEBREW_MARK_BLOCK_HI) {
        return true;
    }
    if (cp == HEBREW_MARK_RAFE) {
        return true;
    }
    if (cp >= HEBREW_MARK_SHIN_LO && cp <= HEBREW_MARK_SHIN_HI) {
        return true;
    }
    if (cp >= HEBREW_MARK_DOT_LO && cp <= HEBREW_MARK_DOT_HI) {
        return true;
    }
    if (cp == HEBREW_MARK_QAMATS) {
        return true;
    }
    return false;
}

/* Decode one scalar at s[i], or false without reading past len. */
static bool utf8_one(const char *s, size_t len, size_t i, Utf8Scalar *out)
{
    unsigned char c = (unsigned char)s[i];
    size_t need = 0;
    unsigned int cp = 0;

    if (!utf8_lead(c, &need, &cp)) {
        return false;
    }
    if (!utf8_cont(s, len, i, need, &cp)) {
        return false;
    }
    *out = (Utf8Scalar){.need = need, .cp = cp};
    return utf8_cp_ok(*out);
}

/* cppcheck-suppress staticFunction ; public API (normalize.h); FTS row text */
NormStatus fold_hebrew_marks(const char *src, size_t src_len, char **out)
{
    char *buf = NULL;
    size_t i = 0;
    size_t w = 0;

    if (out == NULL) {
        return NORM_ERR_INTERNAL;
    }
    *out = NULL;
    if (src == NULL) {
        return NORM_ERR_INTERNAL;
    }
    for (i = 0; i < src_len; i++) {
        if (src[i] == '\0') {
            src_len = i;
            break;
        }
    }

    buf = (char *)malloc(src_len + 1U);
    if (buf == NULL) {
        return NORM_ERR_OOM;
    }
    i = 0;
    while (i < src_len) {
        unsigned char c = (unsigned char)src[i];
        Utf8Scalar dec;

        if (c <= ASCII_MAX) {
            buf[w++] = (char)c;
            i++;
            continue;
        }
        if (!utf8_one(src, src_len, i, &dec)) {
            buf[w++] = (char)c;
            i++;
            continue;
        }
        if (!is_hebrew_mark(dec.cp)) {
            memcpy(buf + w, src + i, dec.need);
            w += dec.need;
        }
        i += dec.need;
    }
    buf[w] = '\0';
    *out = buf;
    return NORM_OK;
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

    end = src_len;
    ascii_ws_trim(src, &start, &end);
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

/* cppcheck-suppress staticFunction ; public API (normalize.h); used by output.c and tests */
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

    end = strlen(src);
    ascii_ws_trim(src, &start, &end);
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

/* ---- canonical stored forms (plan 18c) ------------------------------------ */

enum {
    UUID_TEXT_LEN = 36,
    UUID_DASH_A = 8,
    UUID_DASH_B = 13,
    UUID_DASH_C = 18,
    UUID_DASH_D = 23,
    UUID_VERSION_AT = 14,
    UUID_VARIANT_AT = 19,
    VV_COUNTER_DIGITS_MAX = 18 /* any 18-digit number fits int64: no range check */
};

static bool is_lower_hex(unsigned char c)
{
    if (c >= '0' && c <= '9') {
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        return true;
    }
    return false;
}

/* s[0..36) is a UUID v7. It stops at the first wrong byte, so it never reads
   past a NUL. */
static bool uuid_v7_prefix(const char *s)
{
    size_t i = 0;

    for (i = 0; i < (size_t)UUID_TEXT_LEN; i++) {
        unsigned char c = (unsigned char)s[i];

        if (i == UUID_DASH_A || i == UUID_DASH_B || i == UUID_DASH_C || i == UUID_DASH_D) {
            if (c != '-') {
                return false;
            }
        } else if (!is_lower_hex(c)) {
            return false;
        }
    }
    if (s[UUID_VERSION_AT] != '7') {
        return false;
    }
    /* A lowercase hex digit is never NUL, so strchr finds only the variants. */
    if (strchr("89ab", s[UUID_VARIANT_AT]) == NULL) {
        return false;
    }
    return true;
}

bool uuid_v7_is_canonical(const char *s)
{
    if (s == NULL || !uuid_v7_prefix(s) || s[UUID_TEXT_LEN] != '\0') {
        return false;
    }
    return true;
}

bool utc_stamp_is_canonical(const char *s)
{
    static const char shape[] = "dddd-dd-ddTdd:dd:dd.dddZ";
    size_t i = 0;

    if (s == NULL) {
        return false;
    }
    for (i = 0; shape[i] != '\0'; i++) {
        unsigned char c = (unsigned char)s[i];

        if (shape[i] == 'd') {
            if (c < '0' || c > '9') {
                return false;
            }
        } else if (c != (unsigned char)shape[i]) {
            return false;
        }
    }
    if (s[i] != '\0') {
        return false;
    }
    return true;
}

/* A JSON counter: "0", or a digit 1-9 and more digits, at most 18 in all. */
static bool skip_vv_counter(const char **cursor)
{
    const char *p = *cursor;
    size_t n = 0;

    if (*p == '0') {
        p++;
        n = 1;
    } else {
        while (*p >= '0' && *p <= '9') {
            p++;
            n++;
        }
    }
    if (n == 0U || n > (size_t)VV_COUNTER_DIGITS_MAX) {
        return false;
    }
    *cursor = p;
    return true;
}

/* The device that starts at vec[dev_at] already appears earlier in vec. The pairs
   before it are canonical, so a device starts right after {" or ,". */
// ponytail: rescans earlier pairs, O(n^2) over at most a few hundred bytes of devices.
static bool vv_repeats_device(const char *vec, size_t dev_at)
{
    size_t i = 0;

    for (i = 2; i < dev_at; i++) {
        if (vec[i - 1U] == '"' && (vec[i - 2U] == '{' || vec[i - 2U] == ',') &&
            memcmp(vec + i, vec + dev_at, (size_t)UUID_TEXT_LEN) == 0) {
            return true;
        }
    }
    return false;
}

bool vv_json_is_canonical(const char *s)
{
    const char *p = NULL;

    if (s == NULL || s[0] != '{') {
        return false;
    }
    p = s + 1;
    if (*p != '}') {
        for (;;) {
            if (*p != '"' || !uuid_v7_prefix(p + 1) || p[1 + UUID_TEXT_LEN] != '"' ||
                p[2 + UUID_TEXT_LEN] != ':' || vv_repeats_device(s, (size_t)(p + 1 - s))) {
                return false;
            }
            p += UUID_TEXT_LEN + 3;
            if (!skip_vv_counter(&p)) {
                return false;
            }
            if (*p == '}') {
                break;
            }
            if (*p != ',') {
                return false;
            }
            p++;
        }
    }
    if (p[1] != '\0') {
        return false;
    }
    return true;
}

bool source_is_valid(const char *s)
{
    static const char *const tokens[] = {"human", "agent", "tool", "share", "unknown"};
    size_t i = 0;

    if (s == NULL) {
        return false;
    }
    for (i = 0; i < sizeof(tokens) / sizeof(tokens[0]); i++) {
        if (strcmp(s, tokens[i]) == 0) {
            return true;
        }
    }
    return false;
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
