#include "unicode.h"

#include "utf8proc.h"

#include <stdint.h>
#include <string.h>

/*
 * Unicode 18 PropList White_Space equals Zs+Zl+Zp plus these six controls
 * (25 scalars, no extras). utf8proc 2.12 exposes the general category and not
 * that binary property. Bidi class is a different set: U+0009 is S, U+00A0 is
 * CS, U+2029 is B.
 */
static const uint32_t CP_MAX = 0x10FFFFU;
/* U+0009..U+000D are TAB, LF, VT, FF, CR. U+0085 is NEL. */
static const uint32_t WS_C0_FIRST = 0x0009U;
static const uint32_t WS_C0_LAST = 0x000DU;
static const uint32_t WS_NEL = 0x0085U;

/* unicode_category returns the vendor value unchanged, so pin every value. */
#define PIN_CAT(name)                                                                              \
    _Static_assert((int)UTF8PROC_CATEGORY_##name == (int)UNICODE_CAT_##name, #name)
PIN_CAT(CN);
PIN_CAT(LU);
PIN_CAT(LL);
PIN_CAT(LT);
PIN_CAT(LM);
PIN_CAT(LO);
PIN_CAT(MN);
PIN_CAT(MC);
PIN_CAT(ME);
PIN_CAT(ND);
PIN_CAT(NL);
PIN_CAT(NO);
PIN_CAT(PC);
PIN_CAT(PD);
PIN_CAT(PS);
PIN_CAT(PE);
PIN_CAT(PI);
PIN_CAT(PF);
PIN_CAT(PO);
PIN_CAT(SM);
PIN_CAT(SC);
PIN_CAT(SK);
PIN_CAT(SO);
PIN_CAT(ZS);
PIN_CAT(ZL);
PIN_CAT(ZP);
PIN_CAT(CC);
PIN_CAT(CF);
PIN_CAT(CS);
PIN_CAT(CO);
#undef PIN_CAT

#ifdef REMEMBER_TEST_HOOKS
/* NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) */
static int g_nfc_inject;
#endif

static const utf8proc_uint8_t *as_u8(const char *s)
{
    return (const utf8proc_uint8_t *)s;
}

/* Shrink to the first NUL. Reject a length utf8proc cannot index. */
static UnicodeStatus bounded_bytes(const char *src, size_t src_len, const char **bytes, size_t *len)
{
    const char *nul = NULL;

    if (src == NULL) {
        return UNICODE_ERR_INTERNAL;
    }
    if (src_len > (size_t)PTRDIFF_MAX) {
        return UNICODE_ERR_INTERNAL;
    }
    nul = (const char *)memchr(src, '\0', src_len);
    if (nul != NULL) {
        src_len = (size_t)(nul - src);
    }
    *bytes = src;
    *len = src_len;
    return UNICODE_OK;
}

#ifdef REMEMBER_TEST_HOOKS
void unicode_test_inject_nfc(int which)
{
    g_nfc_inject = which;
}
#endif

UnicodeStatus unicode_nfc(const char *src, size_t src_len, char **out, size_t *out_len)
{
    const char *bytes = NULL;
    size_t len = 0;
    utf8proc_uint8_t *dst = NULL;
    utf8proc_ssize_t n = 0;
    UnicodeStatus st = UNICODE_OK;

    if (out == NULL) {
        return UNICODE_ERR_INTERNAL;
    }
    *out = NULL;
    if (out_len != NULL) {
        *out_len = 0;
    }
    st = bounded_bytes(src, src_len, &bytes, &len);
    if (st != UNICODE_OK) {
        return st;
    }
#ifdef REMEMBER_TEST_HOOKS
    if (g_nfc_inject != 0) {
        int which = g_nfc_inject;

        g_nfc_inject = 0;
        n = (which == UNICODE_TEST_INJECT_NOMEM) ? UTF8PROC_ERROR_NOMEM : UTF8PROC_ERROR_OVERFLOW;
    } else
#endif
    {
        /* Same flags as upstream utf8proc_NFC. The vendor flag enum is a signed int. */
        n = utf8proc_map(as_u8(bytes), (utf8proc_ssize_t)len, &dst,
                         UTF8PROC_STABLE | UTF8PROC_COMPOSE); // NOLINT(hicpp-signed-bitwise)
    }
    if (n < 0) {
        if (n == UTF8PROC_ERROR_INVALIDUTF8) {
            return UNICODE_ERR_INVALID_UTF8;
        }
        if (n == UTF8PROC_ERROR_NOMEM) {
            return UNICODE_ERR_OOM;
        }
        return UNICODE_ERR_INTERNAL;
    }
    *out = (char *)dst;
    if (out_len != NULL) {
        *out_len = (size_t)n;
    }
    return UNICODE_OK;
}

UnicodeStatus unicode_graphemes_open(UnicodeGraphemes *iter, const char *src, size_t src_len)
{
    const char *bytes = NULL;
    size_t len = 0;
    size_t i = 0;
    UnicodeStatus st = UNICODE_OK;

    if (iter == NULL) {
        return UNICODE_ERR_INTERNAL;
    }
    *iter = (UnicodeGraphemes){NULL, 0, 0};
    st = bounded_bytes(src, src_len, &bytes, &len);
    if (st != UNICODE_OK) {
        return st;
    }
    while (i < len) {
        utf8proc_int32_t cp = 0;
        utf8proc_ssize_t n = utf8proc_iterate(as_u8(bytes + i), (utf8proc_ssize_t)(len - i), &cp);

        if (n < 0) {
            return UNICODE_ERR_INVALID_UTF8;
        }
        i += (size_t)n;
    }
    iter->bytes = bytes;
    iter->length = len;
    return UNICODE_OK;
}

UnicodeStatus unicode_graphemes_next(UnicodeGraphemes *iter, size_t *start, size_t *nbytes)
{
    size_t i = 0;
    utf8proc_int32_t prev = 0;
    utf8proc_int32_t state = 0;
    utf8proc_ssize_t n = 0;

    if (iter == NULL || start == NULL || nbytes == NULL || iter->bytes == NULL) {
        return UNICODE_ERR_INTERNAL;
    }
    if (iter->offset >= iter->length) {
        return UNICODE_DONE;
    }
    i = iter->offset;
    n = utf8proc_iterate(as_u8(iter->bytes + i), (utf8proc_ssize_t)(iter->length - i), &prev);
    if (n <= 0) {
        return UNICODE_ERR_INTERNAL;
    }
    i += (size_t)n;
    while (i < iter->length) {
        utf8proc_int32_t next_cp = 0;
        utf8proc_ssize_t n2 = utf8proc_iterate(as_u8(iter->bytes + i),
                                               (utf8proc_ssize_t)(iter->length - i), &next_cp);

        if (n2 <= 0) {
            return UNICODE_ERR_INTERNAL;
        }
        if (utf8proc_grapheme_break_stateful(prev, next_cp, &state)) {
            break;
        }
        prev = next_cp;
        i += (size_t)n2;
    }
    *start = iter->offset;
    *nbytes = i - iter->offset;
    iter->offset = i;
    return UNICODE_OK;
}

UnicodeStatus unicode_decode(const char *src, size_t len, uint32_t *cp, size_t *nbytes)
{
    utf8proc_int32_t scalar = 0;
    utf8proc_ssize_t n = 0;

    if (src == NULL || cp == NULL || nbytes == NULL || len == 0 || len > (size_t)PTRDIFF_MAX) {
        return UNICODE_ERR_INTERNAL;
    }
    n = utf8proc_iterate(as_u8(src), (utf8proc_ssize_t)len, &scalar);
    if (n < 0) {
        return UNICODE_ERR_INVALID_UTF8;
    }
    *cp = (uint32_t)scalar;
    *nbytes = (size_t)n;
    return UNICODE_OK;
}

/* cppcheck-suppress staticFunction ; public API (unicode.h); used by tests */
UnicodeCategory unicode_category(uint32_t cp)
{
    /* utf8proc also maps out-of-range values to Cn, but the cast below is
       implementation-defined above INT32_MAX (C11 6.3.1.3). */
    if (cp > CP_MAX) {
        return UNICODE_CAT_CN;
    }
    return (UnicodeCategory)utf8proc_category((utf8proc_int32_t)cp);
}

bool unicode_is_whitespace(uint32_t cp)
{
    UnicodeCategory cat = unicode_category(cp);

    if (cat == UNICODE_CAT_ZS || cat == UNICODE_CAT_ZL || cat == UNICODE_CAT_ZP) {
        return true;
    }
    if (cp >= WS_C0_FIRST && cp <= WS_C0_LAST) {
        return true;
    }
    return cp == WS_NEL;
}
