#ifndef REMEMBER_UNICODE_H
#define REMEMBER_UNICODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Unicode port. No I/O and no store knowledge.
 * The vendor header is included only from unicode.c.
 */

typedef enum {
    UNICODE_OK = 0,
    UNICODE_DONE, /* grapheme iterator has no further cluster */
    UNICODE_ERR_INVALID_UTF8,
    UNICODE_ERR_OOM,
    UNICODE_ERR_INTERNAL
} UnicodeStatus;

/* Unicode general categories. Values match the pinned utf8proc enum. */
typedef enum {
    UNICODE_CAT_CN = 0, /* Other, not assigned */
    UNICODE_CAT_LU,     /* Letter, uppercase */
    UNICODE_CAT_LL,     /* Letter, lowercase */
    UNICODE_CAT_LT,     /* Letter, titlecase */
    UNICODE_CAT_LM,     /* Letter, modifier */
    UNICODE_CAT_LO,     /* Letter, other */
    UNICODE_CAT_MN,     /* Mark, nonspacing */
    UNICODE_CAT_MC,     /* Mark, spacing combining */
    UNICODE_CAT_ME,     /* Mark, enclosing */
    UNICODE_CAT_ND,     /* Number, decimal digit */
    UNICODE_CAT_NL,     /* Number, letter */
    UNICODE_CAT_NO,     /* Number, other */
    UNICODE_CAT_PC,     /* Punctuation, connector */
    UNICODE_CAT_PD,     /* Punctuation, dash */
    UNICODE_CAT_PS,     /* Punctuation, open */
    UNICODE_CAT_PE,     /* Punctuation, close */
    UNICODE_CAT_PI,     /* Punctuation, initial quote */
    UNICODE_CAT_PF,     /* Punctuation, final quote */
    UNICODE_CAT_PO,     /* Punctuation, other */
    UNICODE_CAT_SM,     /* Symbol, math */
    UNICODE_CAT_SC,     /* Symbol, currency */
    UNICODE_CAT_SK,     /* Symbol, modifier */
    UNICODE_CAT_SO,     /* Symbol, other */
    UNICODE_CAT_ZS,     /* Separator, space */
    UNICODE_CAT_ZL,     /* Separator, line */
    UNICODE_CAT_ZP,     /* Separator, paragraph */
    UNICODE_CAT_CC,     /* Other, control */
    UNICODE_CAT_CF,     /* Other, format */
    UNICODE_CAT_CS,     /* Other, surrogate */
    UNICODE_CAT_CO      /* Other, private use */
} UnicodeCategory;

/*
 * NFC of src[0..src_len). A NUL inside src_len ends the input. *out is
 * overwritten and a previous string is not freed, so free it before the call.
 * On UNICODE_OK, *out is a heap NUL-terminated string (free with free());
 * *out_len, when non-NULL, is the byte length without the NUL. out == NULL,
 * or src == NULL, is UNICODE_ERR_INTERNAL. Invalid UTF-8 is
 * UNICODE_ERR_INVALID_UTF8 and *out is NULL.
 */
UnicodeStatus unicode_nfc(const char *src, size_t src_len, char **out, size_t *out_len);

/*
 * Grapheme-cluster iterator (UAX #29 extended grapheme clusters, stateful).
 * The caller keeps src alive. Do not read the fields.
 * A failed open leaves the iterator unusable (next returns INTERNAL).
 */
typedef struct {
    const char *bytes;
    size_t length;
    size_t offset;
} UnicodeGraphemes;

UnicodeStatus unicode_graphemes_open(UnicodeGraphemes *iter, const char *src, size_t src_len);

/* UNICODE_OK writes one cluster's byte span. UNICODE_DONE at the end. */
UnicodeStatus unicode_graphemes_next(UnicodeGraphemes *iter, size_t *start, size_t *nbytes);

/* General category. A code point above U+10FFFF is CN. */
UnicodeCategory unicode_category(uint32_t cp);

/* Unicode White_Space (PropList), not the bidi class WS. */
bool unicode_is_whitespace(uint32_t cp);

#ifdef REMEMBER_TEST_HOOKS
/* Next unicode_nfc call returns OOM, or an unexpected library status. */
enum { UNICODE_TEST_INJECT_NOMEM = 1, UNICODE_TEST_INJECT_UNEXPECTED = 2 };
void unicode_test_inject_nfc(int which);
#endif

#endif /* REMEMBER_UNICODE_H */
