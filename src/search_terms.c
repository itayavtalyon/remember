#include "search_terms.h"

#include "normalize.h"
#include "unicode.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Last: under REMEMBER_TEST_HOOKS every malloc below goes through the shared
   fault hook (decision:plan18b-shared-alloc-hook). */
#ifdef REMEMBER_TEST_HOOKS
#include "test_alloc.h" // IWYU pragma: keep
#endif

enum { UTF8_MAX_BYTES = 4, ASCII_LIMIT = 0x80 };

_Static_assert((size_t)SEARCH_TOKEN_BYTES_MAX >=
                   (size_t)SEARCH_TOKEN_CPS_MAX * (size_t)UTF8_MAX_BYTES,
               "a token of 64 code points fits its buffer");

static const char TITLE_MARK[] = "# ";
static const char ELLIPSIS[] = "\xe2\x80\xa6"; /* U+2026 */

/* No caller passes UNICODE_DONE: plain_label stops on it before mapping. */
static SearchStatus from_unicode(UnicodeStatus st)
{
    if (st == UNICODE_OK) {
        return SEARCH_OK;
    }
    if (st == UNICODE_ERR_INVALID_UTF8) {
        return SEARCH_ERR_INVALID_UTF8;
    }
    if (st == UNICODE_ERR_OOM) {
        return SEARCH_ERR_OOM;
    }
    return SEARCH_ERR_INTERNAL;
}

/* The scalar at s[i..len). */
static SearchStatus decode_at(const char *s, size_t len, size_t i, uint32_t *cp, size_t *n)
{
    return from_unicode(unicode_decode(s + i, len - i, cp, n));
}

static bool is_ascii_alnum(uint32_t cp)
{
    if ((cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z')) {
        return true;
    }
    return false;
}

static char ascii_lower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

/* E2.2 step 1: Unicode White_Space and every ASCII character outside [A-Za-z0-9]. */
static bool splits_token(uint32_t cp)
{
    if (cp < ASCII_LIMIT) {
        if (is_ascii_alnum(cp)) {
            return false;
        }
        return true;
    }
    return unicode_is_whitespace(cp);
}

/* E2.5: ':', '-', '_', '.', '/' and Unicode White_Space. */
static bool splits_segment(uint32_t cp)
{
    if (cp == ':' || cp == '-' || cp == '_' || cp == '.' || cp == '/') {
        return true;
    }
    return unicode_is_whitespace(cp);
}

static SearchStatus dup_bytes(const char *s, size_t n, char **out)
{
    char *copy = (char *)malloc(n + 1U);

    if (copy == NULL) {
        return SEARCH_ERR_OOM;
    }
    memcpy(copy, s, n);
    copy[n] = '\0';
    *out = copy;
    return SEARCH_OK;
}

/*
 * One token from nfc[*i..len): at most 64 code points kept, the Hebrew marks
 * among them dropped, ASCII lowercased. *i ends on the separator after it.
 */
static SearchStatus read_token(const char *nfc, size_t len, size_t *i, char *dst, size_t *dst_len)
{
    size_t cps = 0;
    size_t w = 0;
    uint32_t cp = 0;
    size_t n = 0;

    while (*i < len) {
        SearchStatus st = decode_at(nfc, len, *i, &cp, &n);

        if (st != SEARCH_OK) {
            return st;
        }
        if (splits_token(cp)) {
            break;
        }
        if (cps < SEARCH_TOKEN_CPS_MAX && !is_hebrew_mark(cp)) {
            memcpy(dst + w, nfc + *i, n);
            if (n == 1U) {
                dst[w] = ascii_lower(dst[w]);
            }
            w += n;
        }
        cps++;
        *i += n;
    }
    dst[w] = '\0';
    *dst_len = w;
    return SEARCH_OK;
}

SearchStatus search_tokens_build(const char *query, SearchTokens *out)
{
    char *nfc = NULL;
    size_t len = 0;
    size_t i = 0;
    size_t slots = 0;
    uint32_t cp = 0;
    size_t n = 0;
    SearchStatus st = SEARCH_OK;

    if (query == NULL || out == NULL) {
        return SEARCH_ERR_INTERNAL;
    }
    /* ponytail: NFC runs on the whole query before the 16 x 64 cap (E2.2), and
       utf8proc's canonical reorder is quadratic in one combining-mark run
       (100k marks ~3 s at -O2); only ARG_MAX bounds it. Upgrade path: NFC only
       a prefix of the query, or split first and NFC each kept token
       (decision:plan18-nfc-cost-accept). */
    st = from_unicode(unicode_nfc(query, strlen(query), &nfc, &len));
    if (st != SEARCH_OK) {
        return st;
    }
    out->count = 0;
    while (i < len && slots < SEARCH_TOKENS_MAX) {
        size_t token_len = 0;

        st = decode_at(nfc, len, i, &cp, &n);
        if (st != SEARCH_OK) {
            goto cleanup;
        }
        if (splits_token(cp)) {
            i += n;
            continue;
        }
        slots++;
        st = read_token(nfc, len, &i, out->tokens[out->count], &token_len);
        if (st != SEARCH_OK) {
            goto cleanup;
        }
        if (token_len > 0) {
            out->count++;
        }
    }
cleanup:
    free(nfc);
    return st;
}

SearchStatus search_q(const char *query, char *out, size_t out_cap)
{
    size_t start = 0;
    size_t end = 0;
    size_t k = 0;

    if (query == NULL || out == NULL) {
        return SEARCH_ERR_INTERNAL;
    }
    end = strlen(query);
    if (out_cap <= end) {
        return SEARCH_ERR_INTERNAL;
    }
    ascii_ws_trim(query, &start, &end);
    for (k = 0; start + k < end; k++) {
        out[k] = ascii_lower(query[start + k]);
    }
    out[k] = '\0';
    return SEARCH_OK;
}

static bool segment_starts_with(const char *seg, size_t seg_len, const char *token)
{
    size_t token_len = strlen(token);

    if (seg_len >= token_len && memcmp(seg, token, token_len) == 0) {
        return true;
    }
    return false;
}

/*
 * The start offset of the first segment of text[0..len), at or after offset
 * from, that begins with token; len when there is none.
 */
static SearchStatus find_segment(const char *text, size_t len, const char *token, size_t from,
                                 size_t *found)
{
    size_t i = from;
    size_t seg = from;
    uint32_t cp = 0;
    size_t n = 0;

    *found = len;
    while (i < len) {
        SearchStatus st = decode_at(text, len, i, &cp, &n);

        if (st != SEARCH_OK) {
            return st;
        }
        if (splits_segment(cp)) {
            if (segment_starts_with(text + seg, i - seg, token)) {
                *found = seg;
                return SEARCH_OK;
            }
            seg = i + n;
        }
        i += n;
    }
    if (segment_starts_with(text + seg, len - seg, token)) {
        *found = seg;
    }
    return SEARCH_OK;
}

/*
 * E2.5 / E2.6 tiers 2 and 3 on text that is already NFC and folded. Greedy
 * earliest match keeps the in-order test exact: a later token may reuse the
 * segment the previous token used.
 */
static SearchStatus segment_tier(const SearchTokens *tokens, const char *text, size_t len,
                                 SearchTier *tier)
{
    size_t k = 0;
    size_t cursor = 0;
    bool in_order = true;
    size_t found = 0;

    for (k = 0; k < tokens->count; k++) {
        SearchStatus st = find_segment(text, len, tokens->tokens[k], 0, &found);

        if (st != SEARCH_OK) {
            return st;
        }
        if (found == len) {
            *tier = SEARCH_TIER_NONE;
            return SEARCH_OK;
        }
        if (in_order) {
            st = find_segment(text, len, tokens->tokens[k], cursor, &found);
            if (st != SEARCH_OK) {
                return st;
            }
            in_order = (found != len);
            cursor = found;
        }
    }
    *tier = SEARCH_TIER_3;
    if (in_order) {
        *tier = SEARCH_TIER_2;
    }
    return SEARCH_OK;
}

SearchStatus search_facet_classify(const SearchTokens *tokens, const char *q, const char *text,
                                   long long count, SearchFacet *out)
{
    SearchFacet facet = {.text = text, .count = count, .tier = SEARCH_TIER_NONE};
    size_t q_len = 0;
    char *nfc = NULL;
    size_t nfc_len = 0;
    char *folded = NULL;
    SearchStatus st = SEARCH_OK;

    if (tokens == NULL || q == NULL || text == NULL || out == NULL) {
        return SEARCH_ERR_INTERNAL;
    }
    q_len = strlen(q);
    facet.exact = (strcmp(text, q) == 0);
    if (q_len > 0 && strncmp(text, q, q_len) == 0) {
        facet.tier = SEARCH_TIER_1;
        *out = facet;
        return SEARCH_OK;
    }
    if (tokens->count == 0) {
        *out = facet;
        return SEARCH_OK;
    }
    st = from_unicode(unicode_nfc(text, strlen(text), &nfc, &nfc_len));
    if (st != SEARCH_OK) {
        return st;
    }
    /* nfc and &folded are non-NULL, so the fold fails only when malloc does. */
    st = (fold_hebrew_marks(nfc, nfc_len, &folded) == NORM_OK) ? SEARCH_OK : SEARCH_ERR_OOM;
    if (st != SEARCH_OK) {
        goto cleanup;
    }
    st = segment_tier(tokens, folded, strlen(folded), &facet.tier);
    if (st == SEARCH_OK) {
        *out = facet;
    }
cleanup:
    free(folded);
    free(nfc);
    return st;
}

static int tier_order(const SearchFacet *a, const SearchFacet *b)
{
    if (a->tier == b->tier) {
        return 0;
    }
    return (a->tier < b->tier) ? -1 : 1;
}

/* strcmp compares bytes as unsigned char, which is code point order for UTF-8. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) qsort's signature
int search_key_order(const void *a, const void *b)
{
    const SearchFacet *lhs = (const SearchFacet *)a;
    const SearchFacet *rhs = (const SearchFacet *)b;
    int by_tier = tier_order(lhs, rhs);

    if (by_tier != 0) {
        return by_tier;
    }
    return strcmp(lhs->text, rhs->text);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) qsort's signature
int search_tag_order(const void *a, const void *b)
{
    const SearchFacet *lhs = (const SearchFacet *)a;
    const SearchFacet *rhs = (const SearchFacet *)b;
    int by_tier = tier_order(lhs, rhs);

    if (lhs->exact != rhs->exact) {
        if (lhs->exact) {
            return -1;
        }
        return 1;
    }
    if (by_tier != 0) {
        return by_tier;
    }
    if (lhs->count != rhs->count) {
        return (lhs->count > rhs->count) ? -1 : 1;
    }
    return strcmp(lhs->text, rhs->text);
}

/* D12 plain text: each run of White_Space or Cc becomes one space; trimmed. */
static SearchStatus plain_text(const char *body, size_t len, char *dst, size_t *dst_len)
{
    size_t i = 0;
    size_t w = 0;
    bool gap = false;
    uint32_t cp = 0;
    size_t n = 0;

    while (i < len) {
        SearchStatus st = decode_at(body, len, i, &cp, &n);

        if (st != SEARCH_OK) {
            return st;
        }
        if (unicode_is_whitespace(cp) || unicode_category(cp) == UNICODE_CAT_CC) {
            gap = (w > 0);
        } else {
            if (gap) {
                dst[w++] = ' ';
                gap = false;
            }
            memcpy(dst + w, body + i, n);
            w += n;
        }
        i += n;
    }
    dst[w] = '\0';
    *dst_len = w;
    return SEARCH_OK;
}

/* The first 60 grapheme clusters of the plain text, plus U+2026 when cut (P2). */
static SearchStatus plain_label(const char *body, char **out)
{
    size_t len = strlen(body);
    char *buf = (char *)malloc(len + sizeof ELLIPSIS);
    size_t text_len = 0;
    UnicodeGraphemes iter = {NULL, 0, 0};
    size_t start = 0;
    size_t nbytes = 0;
    size_t cut = 0;
    size_t k = 0;
    SearchStatus st = SEARCH_OK;

    if (buf == NULL) {
        return SEARCH_ERR_OOM;
    }
    st = plain_text(body, len, buf, &text_len);
    if (st != SEARCH_OK) {
        goto cleanup;
    }
    st = from_unicode(unicode_graphemes_open(&iter, buf, text_len));
    if (st != SEARCH_OK) {
        goto cleanup;
    }
    for (k = 0; k < SEARCH_LABEL_GRAPHEMES; k++) {
        UnicodeStatus next = unicode_graphemes_next(&iter, &start, &nbytes);

        if (next == UNICODE_DONE) {
            break;
        }
        st = from_unicode(next);
        if (st != SEARCH_OK) {
            goto cleanup;
        }
        cut = start + nbytes;
    }
    if (cut < text_len) {
        memcpy(buf + cut, ELLIPSIS, sizeof ELLIPSIS);
    }
    *out = buf;
    buf = NULL;
cleanup:
    free(buf);
    return st;
}

SearchStatus search_label(const char *body, const char *key, char **out)
{
    if (body == NULL || out == NULL) {
        return SEARCH_ERR_INTERNAL;
    }
    *out = NULL;
    if (strncmp(body, TITLE_MARK, sizeof TITLE_MARK - 1U) == 0) {
        const char *newline = strchr(body, '\n');
        size_t start = sizeof TITLE_MARK - 1U;
        size_t end = (newline != NULL) ? (size_t)(newline - body) : strlen(body);

        ascii_ws_trim(body, &start, &end);
        if (start < end) {
            return dup_bytes(body + start, end - start, out);
        }
    }
    if (key != NULL) {
        return dup_bytes(key, strlen(key), out);
    }
    return plain_label(body, out);
}
