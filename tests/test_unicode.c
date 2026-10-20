#include "register.h"
#include "test.h"
#include "unicode.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    UNWRITTEN = 9,            /* out-param value the call must reset or leave alone */
    INVALID_UTF8_BYTE = 0xFF, /* never a valid UTF-8 byte */
    NFC_LONG = 65536          /* longer than a single UTF-8 sequence */
};

/* NFC(src[0..len)) is UNICODE_OK and equals want, with its byte length. */
static void assert_nfc(const char *src, size_t len, const char *want)
{
    char *out = NULL;
    size_t n = UNWRITTEN;
    UnicodeStatus st = unicode_nfc(src, len, &out, &n);

    ASSERT_EQ_INT(st, UNICODE_OK);
    ASSERT_STREQ(out, want);
    ASSERT_EQ_INT((int)n, (int)strlen(want));
    free(out);
}

/* NFC(src[0..len)) fails with want and resets both out-params. */
static void assert_nfc_fails(UnicodeStatus want, const char *src, size_t len)
{
    char *out = NULL;
    size_t n = UNWRITTEN;
    UnicodeStatus st = unicode_nfc(src, len, &out, &n);

    ASSERT_EQ_INT(st, want);
    ASSERT_TRUE(out == NULL);
    ASSERT_EQ_INT((int)n, 0);
    free(out);
}

/* src[0..len) splits into clusters of these byte lengths, then stays DONE. */
static void assert_clusters(const char *src, size_t len, const size_t *want, size_t count)
{
    UnicodeGraphemes iter = {NULL, 0, 0};
    size_t start = 0;
    size_t n = 0;
    size_t at = 0;
    size_t k = 0;
    UnicodeStatus st = unicode_graphemes_open(&iter, src, len);

    ASSERT_EQ_INT(st, UNICODE_OK);
    for (k = 0; k < count; k++) {
        st = unicode_graphemes_next(&iter, &start, &n);
        ASSERT_EQ_INT(st, UNICODE_OK);
        ASSERT_EQ_INT((int)start, (int)at);
        ASSERT_EQ_INT((int)n, (int)want[k]);
        at += want[k];
    }
    st = unicode_graphemes_next(&iter, &start, &n);
    ASSERT_EQ_INT(st, UNICODE_DONE);
    st = unicode_graphemes_next(&iter, &start, &n);
    ASSERT_EQ_INT(st, UNICODE_DONE);
}

/* next() on iter fails with UNICODE_ERR_INTERNAL and writes no span. */
static void assert_next_is_internal(UnicodeGraphemes *iter)
{
    size_t start = UNWRITTEN;
    size_t n = UNWRITTEN;
    UnicodeStatus st = unicode_graphemes_next(iter, &start, &n);

    ASSERT_EQ_INT(st, UNICODE_ERR_INTERNAL);
    ASSERT_EQ_INT((int)start, UNWRITTEN);
    ASSERT_EQ_INT((int)n, UNWRITTEN);
}

TEST(unicode_nfc_composes_e_acute)
{
    /* NormalizationTest-18.0.0.txt: NFC(0065 0301) = 00E9. */
    assert_nfc("\x65\xcc\x81", 3, "\xc3\xa9");
}

TEST(unicode_nfc_keeps_already_composed_e_acute)
{
    assert_nfc("\xc3\xa9", 2, "\xc3\xa9");
}

TEST(unicode_nfc_keeps_spaces_around_a_composition)
{
    static const char src[] = " \x65\xcc\x81 ";

    assert_nfc(src, sizeof src - 1U, " \xc3\xa9 ");
}

TEST(unicode_nfc_does_not_compatibility_decompose_fi)
{
    /* NormalizationTest-18.0.0.txt: NFC(FB01) = FB01; NFKC = 0066 0069. */
    assert_nfc("\xef\xac\x81", 3, "\xef\xac\x81");
}

TEST(unicode_nfc_composes_hangul_l_v)
{
    /* NormalizationTest-18.0.0.txt: NFC(1100 1161) = AC00. */
    static const char src[] = "\xe1\x84\x80\xe1\x85\xa1";

    assert_nfc(src, sizeof src - 1U, "\xea\xb0\x80");
}

TEST(unicode_nfc_empty_is_empty)
{
    assert_nfc("", 0, "");
}

TEST(unicode_nfc_stops_at_an_interior_nul)
{
    const char src[] = {'a', '\0', 'b'};

    assert_nfc(src, sizeof src, "a");
}

TEST(unicode_nfc_rejects_a_lone_invalid_byte)
{
    assert_nfc_fails(UNICODE_ERR_INVALID_UTF8, "\xff", 1);
}

TEST(unicode_nfc_rejects_a_truncated_sequence)
{
    assert_nfc_fails(UNICODE_ERR_INVALID_UTF8, "\xc3", 1);
}

TEST(unicode_nfc_rejects_a_lone_continuation_byte)
{
    assert_nfc_fails(UNICODE_ERR_INVALID_UTF8, "\x80", 1);
}

TEST(unicode_nfc_rejects_an_overlong_nul)
{
    assert_nfc_fails(UNICODE_ERR_INVALID_UTF8, "\xc0\x80", 2);
}

TEST(unicode_nfc_null_source_is_internal_and_resets_out)
{
    char dummy = 'x';
    char *out = &dummy;
    size_t n = UNWRITTEN;
    UnicodeStatus st = unicode_nfc(NULL, 1, &out, &n);

    ASSERT_EQ_INT(st, UNICODE_ERR_INTERNAL);
    ASSERT_TRUE(out == NULL);
    ASSERT_EQ_INT((int)n, 0);
}

TEST(unicode_nfc_null_out_is_internal)
{
    size_t n = UNWRITTEN;
    UnicodeStatus st = unicode_nfc("a", 1, NULL, &n);

    ASSERT_EQ_INT(st, UNICODE_ERR_INTERNAL);
    ASSERT_EQ_INT((int)n, UNWRITTEN);
}

TEST(unicode_nfc_allows_a_null_length_out)
{
    char *out = NULL;
    UnicodeStatus st = unicode_nfc("a", 1, &out, NULL);

    ASSERT_EQ_INT(st, UNICODE_OK);
    ASSERT_STREQ(out, "a");
    free(out);
}

TEST(unicode_nfc_rejects_a_length_past_ptrdiff_max)
{
    const char src[1] = {'a'};
    size_t too_big = (size_t)PTRDIFF_MAX;

    too_big += 1U;
    assert_nfc_fails(UNICODE_ERR_INTERNAL, src, too_big);
}

TEST(unicode_nfc_identity_on_a_long_ascii_string)
{
    char *src = (char *)malloc(NFC_LONG + 1);

    ASSERT_TRUE(src != NULL);
    if (src == NULL) {
        return;
    }
    memset(src, 'a', NFC_LONG);
    src[NFC_LONG] = '\0';
    assert_nfc(src, NFC_LONG, src);
    free(src);
}

TEST(unicode_nfc_injected_nomem_applies_once)
{
    unicode_test_inject_nfc(UNICODE_TEST_INJECT_NOMEM);
    assert_nfc_fails(UNICODE_ERR_OOM, "a", 1);
    assert_nfc("a", 1, "a");
}

TEST(unicode_nfc_injected_unexpected_status_is_internal)
{
    unicode_test_inject_nfc(UNICODE_TEST_INJECT_UNEXPECTED);
    assert_nfc_fails(UNICODE_ERR_INTERNAL, "a", 1);
}

TEST(unicode_graphemes_crlf_is_one_cluster)
{
    /* GraphemeBreakTest-18.0.0.txt line 29: ÷ 000D × 000A ÷ */
    static const size_t want[] = {2};

    assert_clusters("\r\n", 2, want, 1);
}

TEST(unicode_graphemes_combining_mark_stays_with_the_base)
{
    /* GraphemeBreakTest-18.0.0.txt line 840: ÷ 0061 × 0308 ÷ 0062 ÷ */
    static const size_t want[] = {3, 1};

    assert_clusters("a\xcc\x88"
                    "b",
                    4, want, 2);
}

TEST(unicode_graphemes_regional_indicators_pair_then_break)
{
    /* GraphemeBreakTest-18.0.0.txt line 834: ÷ 1F1E6 × 1F1E7 ÷ 1F1E8 ÷ 0062 ÷ */
    static const char src[] = "\xf0\x9f\x87\xa6\xf0\x9f\x87\xa7\xf0\x9f\x87\xa8"
                              "b";
    static const size_t want[] = {8, 4, 1};

    assert_clusters(src, sizeof src - 1U, want, 3);
}

TEST(unicode_graphemes_zwj_emoji_is_one_cluster)
{
    /* GraphemeBreakTest-18.0.0.txt line 847: ÷ 1F6D1 × 200D × 1F6D1 ÷ */
    static const char src[] = "\xf0\x9f\x9b\x91\xe2\x80\x8d\xf0\x9f\x9b\x91";
    static const size_t want[] = {11};

    assert_clusters(src, sizeof src - 1U, want, 1);
}

TEST(unicode_graphemes_indic_conjunct_is_one_cluster)
{
    /* GraphemeBreakTest-18.0.0.txt line 854: ÷ 0915 × 094D × 200D × 0924 ÷ */
    static const char src[] = "\xe0\xa4\x95\xe0\xa5\x8d\xe2\x80\x8d\xe0\xa4\xa4";
    static const size_t want[] = {12};

    assert_clusters(src, sizeof src - 1U, want, 1);
}

TEST(unicode_graphemes_two_ascii_letters_are_two_clusters)
{
    static const size_t want[] = {1, 1};

    assert_clusters("ab", 2, want, 2);
}

TEST(unicode_graphemes_stops_at_an_interior_nul)
{
    const char src[] = {'a', '\0', 'b'};
    static const size_t want[] = {1};

    assert_clusters(src, sizeof src, want, 1);
}

TEST(unicode_graphemes_empty_is_done)
{
    assert_clusters("", 0, NULL, 0);
}

TEST(unicode_graphemes_next_rejects_invalidated_bytes)
{
    char buf[] = {'a'};
    UnicodeGraphemes iter = {NULL, 0, 0};
    UnicodeStatus st = unicode_graphemes_open(&iter, buf, 1);

    ASSERT_EQ_INT(st, UNICODE_OK);
    /* cppcheck-suppress unreadVariable ; next() reads the byte through the open iterator */
    buf[0] = (char)INVALID_UTF8_BYTE;
    assert_next_is_internal(&iter);
}

TEST(unicode_graphemes_next_rejects_a_later_invalid_byte)
{
    char buf[] = {'a', 'b'};
    UnicodeGraphemes iter = {NULL, 0, 0};
    UnicodeStatus st = unicode_graphemes_open(&iter, buf, 2);

    ASSERT_EQ_INT(st, UNICODE_OK);
    /* cppcheck-suppress unreadVariable ; next() reads the byte through the open iterator */
    buf[1] = (char)INVALID_UTF8_BYTE;
    assert_next_is_internal(&iter);
}

TEST(unicode_graphemes_failed_reopen_drops_the_previous_string)
{
    UnicodeGraphemes iter = {NULL, 0, 0};
    UnicodeStatus st = unicode_graphemes_open(&iter, "ab", 2);

    ASSERT_EQ_INT(st, UNICODE_OK);
    st = unicode_graphemes_open(&iter, "\xff", 1);
    ASSERT_EQ_INT(st, UNICODE_ERR_INVALID_UTF8);
    assert_next_is_internal(&iter);
}

TEST(unicode_graphemes_reopen_restarts_at_the_first_byte)
{
    UnicodeGraphemes iter = {NULL, 0, 0};
    size_t start = UNWRITTEN;
    size_t n = UNWRITTEN;
    UnicodeStatus st = unicode_graphemes_open(&iter, "ab", 2);

    ASSERT_EQ_INT(st, UNICODE_OK);
    st = unicode_graphemes_next(&iter, &start, &n);
    ASSERT_EQ_INT(st, UNICODE_OK);
    st = unicode_graphemes_open(&iter, "cd", 2);
    ASSERT_EQ_INT(st, UNICODE_OK);
    st = unicode_graphemes_next(&iter, &start, &n);
    ASSERT_EQ_INT(st, UNICODE_OK);
    ASSERT_EQ_INT((int)start, 0);
    ASSERT_EQ_INT((int)n, 1);
}

TEST(unicode_graphemes_invalid_open_is_not_iterable)
{
    UnicodeGraphemes iter = {NULL, 0, 0};
    UnicodeStatus st = unicode_graphemes_open(&iter, "\xff", 1);

    ASSERT_EQ_INT(st, UNICODE_ERR_INVALID_UTF8);
    assert_next_is_internal(&iter);
}

TEST(unicode_graphemes_unopened_next_is_internal)
{
    UnicodeGraphemes iter = {NULL, 0, 0};

    assert_next_is_internal(&iter);
}

TEST(unicode_graphemes_null_iterator_is_internal)
{
    UnicodeStatus st = unicode_graphemes_open(NULL, "a", 1);

    ASSERT_EQ_INT(st, UNICODE_ERR_INTERNAL);
    assert_next_is_internal(NULL);
}

TEST(unicode_graphemes_null_source_is_internal)
{
    UnicodeGraphemes iter = {NULL, 0, 0};
    UnicodeStatus st = unicode_graphemes_open(&iter, NULL, 1);

    ASSERT_EQ_INT(st, UNICODE_ERR_INTERNAL);
}

TEST(unicode_graphemes_null_span_outs_are_internal)
{
    UnicodeGraphemes iter = {NULL, 0, 0};
    size_t start = 0;
    size_t n = 0;
    UnicodeStatus st = unicode_graphemes_open(&iter, "a", 1);

    ASSERT_EQ_INT(st, UNICODE_OK);
    st = unicode_graphemes_next(&iter, NULL, &n);
    ASSERT_EQ_INT(st, UNICODE_ERR_INTERNAL);
    st = unicode_graphemes_next(&iter, &start, NULL);
    ASSERT_EQ_INT(st, UNICODE_ERR_INTERNAL);
}

TEST(unicode_graphemes_length_past_ptrdiff_max_is_internal)
{
    UnicodeGraphemes iter = {NULL, 0, 0};
    const char src[1] = {'a'};
    size_t too_big = (size_t)PTRDIFF_MAX;
    UnicodeStatus st = UNICODE_OK;

    too_big += 1U;
    st = unicode_graphemes_open(&iter, src, too_big);
    ASSERT_EQ_INT(st, UNICODE_ERR_INTERNAL);
}

TEST(unicode_decode_reads_one_scalar_of_each_length)
{
    /* U+0061, U+00E9, U+3000, U+1F6D1: 1, 2, 3, and 4 UTF-8 bytes. */
    static const char src[] = "a\xc3\xa9\xe3\x80\x80\xf0\x9f\x9b\x91";
    size_t at = 0;
    uint32_t cp = 0;
    size_t n = 0;
    UnicodeStatus st = unicode_decode(src, sizeof src - 1U, &cp, &n);

    ASSERT_EQ_INT(st, UNICODE_OK);
    ASSERT_EQ_INT((long long)cp, 0x61);
    ASSERT_EQ_INT((int)n, 1);
    at += n;
    st = unicode_decode(src + at, sizeof src - 1U - at, &cp, &n);
    ASSERT_EQ_INT(st, UNICODE_OK);
    ASSERT_EQ_INT((long long)cp, 0xE9);
    ASSERT_EQ_INT((int)n, 2);
    at += n;
    st = unicode_decode(src + at, sizeof src - 1U - at, &cp, &n);
    ASSERT_EQ_INT(st, UNICODE_OK);
    ASSERT_EQ_INT((long long)cp, 0x3000);
    ASSERT_EQ_INT((int)n, 3);
    at += n;
    st = unicode_decode(src + at, sizeof src - 1U - at, &cp, &n);
    ASSERT_EQ_INT(st, UNICODE_OK);
    ASSERT_EQ_INT((long long)cp, 0x1F6D1);
    ASSERT_EQ_INT((int)n, 4);
}

TEST(unicode_decode_does_not_read_past_len)
{
    /* A whole 2-byte sequence, but len cuts it after the lead byte. */
    uint32_t cp = UNWRITTEN;
    size_t n = UNWRITTEN;
    UnicodeStatus st = unicode_decode("\xc3\xa9", 1, &cp, &n);

    ASSERT_EQ_INT(st, UNICODE_ERR_INVALID_UTF8);
    ASSERT_EQ_INT((long long)cp, UNWRITTEN);
    ASSERT_EQ_INT((int)n, UNWRITTEN);
}

TEST(unicode_decode_rejects_an_invalid_byte)
{
    uint32_t cp = UNWRITTEN;
    size_t n = UNWRITTEN;
    UnicodeStatus st = unicode_decode("\xff", 1, &cp, &n);

    ASSERT_EQ_INT(st, UNICODE_ERR_INVALID_UTF8);
    ASSERT_EQ_INT((int)n, UNWRITTEN);
}

TEST(unicode_decode_bad_arguments_are_internal)
{
    uint32_t cp = 0;
    size_t n = 0;

    ASSERT_EQ_INT(unicode_decode(NULL, 1, &cp, &n), UNICODE_ERR_INTERNAL);
    ASSERT_EQ_INT(unicode_decode("a", 0, &cp, &n), UNICODE_ERR_INTERNAL);
    ASSERT_EQ_INT(unicode_decode("a", 1, NULL, &n), UNICODE_ERR_INTERNAL);
    ASSERT_EQ_INT(unicode_decode("a", 1, &cp, NULL), UNICODE_ERR_INTERNAL);
}

TEST(unicode_decode_length_past_ptrdiff_max_is_internal)
{
    const char src[1] = {'a'};
    size_t too_big = (size_t)PTRDIFF_MAX;
    uint32_t cp = 0;
    size_t n = 0;

    too_big += 1U;
    ASSERT_EQ_INT(unicode_decode(src, too_big, &cp, &n), UNICODE_ERR_INTERNAL);
}

TEST(unicode_category_matches_unicode_general_category)
{
    ASSERT_EQ_INT(unicode_category(0x0041), UNICODE_CAT_LU);
    ASSERT_EQ_INT(unicode_category(0x0061), UNICODE_CAT_LL);
    ASSERT_EQ_INT(unicode_category(0x0301), UNICODE_CAT_MN);
    ASSERT_EQ_INT(unicode_category(0x0020), UNICODE_CAT_ZS);
    ASSERT_EQ_INT(unicode_category(0x2028), UNICODE_CAT_ZL);
    ASSERT_EQ_INT(unicode_category(0x2029), UNICODE_CAT_ZP);
    ASSERT_EQ_INT(unicode_category(0x0009), UNICODE_CAT_CC);
    ASSERT_EQ_INT(unicode_category(0x200B), UNICODE_CAT_CF);
    ASSERT_EQ_INT(unicode_category(0xD800), UNICODE_CAT_CS);
    ASSERT_EQ_INT(unicode_category(0xE000), UNICODE_CAT_CO);
    /* GraphemeBreakTest calls U+0378 reserved. */
    ASSERT_EQ_INT(unicode_category(0x0378), UNICODE_CAT_CN);
    ASSERT_EQ_INT(unicode_category(0x110000), UNICODE_CAT_CN);
    ASSERT_EQ_INT(unicode_category(0xFFFFFFFFU), UNICODE_CAT_CN);
}

TEST(unicode_whitespace_is_unicode_18_white_space)
{
    /* PropList-18.0.0.txt White_Space. U+0009, U+00A0, and U+2029 are in this
       set and are not bidi class WS. */
    ASSERT_TRUE(unicode_is_whitespace(0x0009));
    ASSERT_TRUE(unicode_is_whitespace(0x000A));
    ASSERT_TRUE(unicode_is_whitespace(0x000B));
    ASSERT_TRUE(unicode_is_whitespace(0x000C));
    ASSERT_TRUE(unicode_is_whitespace(0x000D));
    ASSERT_TRUE(unicode_is_whitespace(0x0020));
    ASSERT_TRUE(unicode_is_whitespace(0x0085));
    ASSERT_TRUE(unicode_is_whitespace(0x00A0));
    ASSERT_TRUE(unicode_is_whitespace(0x1680));
    ASSERT_TRUE(unicode_is_whitespace(0x2000));
    ASSERT_TRUE(unicode_is_whitespace(0x2001));
    ASSERT_TRUE(unicode_is_whitespace(0x2002));
    ASSERT_TRUE(unicode_is_whitespace(0x2003));
    ASSERT_TRUE(unicode_is_whitespace(0x2004));
    ASSERT_TRUE(unicode_is_whitespace(0x2005));
    ASSERT_TRUE(unicode_is_whitespace(0x2006));
    ASSERT_TRUE(unicode_is_whitespace(0x2007));
    ASSERT_TRUE(unicode_is_whitespace(0x2008));
    ASSERT_TRUE(unicode_is_whitespace(0x2009));
    ASSERT_TRUE(unicode_is_whitespace(0x200A));
    ASSERT_TRUE(unicode_is_whitespace(0x2028));
    ASSERT_TRUE(unicode_is_whitespace(0x2029));
    ASSERT_TRUE(unicode_is_whitespace(0x202F));
    ASSERT_TRUE(unicode_is_whitespace(0x205F));
    ASSERT_TRUE(unicode_is_whitespace(0x3000));
}

TEST(unicode_whitespace_rejects_near_misses)
{
    /* Each neighbor of a White_Space range edge, plus ZWSP and MVS. */
    ASSERT_FALSE(unicode_is_whitespace(0x0000));
    ASSERT_FALSE(unicode_is_whitespace(0x0008));
    ASSERT_FALSE(unicode_is_whitespace(0x000E));
    ASSERT_FALSE(unicode_is_whitespace(0x0041));
    ASSERT_FALSE(unicode_is_whitespace(0x0084));
    ASSERT_FALSE(unicode_is_whitespace(0x0086));
    ASSERT_FALSE(unicode_is_whitespace(0x00A1));
    ASSERT_FALSE(unicode_is_whitespace(0x180E));
    ASSERT_FALSE(unicode_is_whitespace(0x200B));
    ASSERT_FALSE(unicode_is_whitespace(0x10FFFF));
    ASSERT_FALSE(unicode_is_whitespace(0x110000));
}

void register_unicode_tests(void)
{
    RUN_TEST(unicode_nfc_composes_e_acute);
    RUN_TEST(unicode_nfc_keeps_already_composed_e_acute);
    RUN_TEST(unicode_nfc_keeps_spaces_around_a_composition);
    RUN_TEST(unicode_nfc_does_not_compatibility_decompose_fi);
    RUN_TEST(unicode_nfc_composes_hangul_l_v);
    RUN_TEST(unicode_nfc_empty_is_empty);
    RUN_TEST(unicode_nfc_stops_at_an_interior_nul);
    RUN_TEST(unicode_nfc_rejects_a_lone_invalid_byte);
    RUN_TEST(unicode_nfc_rejects_a_truncated_sequence);
    RUN_TEST(unicode_nfc_rejects_a_lone_continuation_byte);
    RUN_TEST(unicode_nfc_rejects_an_overlong_nul);
    RUN_TEST(unicode_nfc_null_source_is_internal_and_resets_out);
    RUN_TEST(unicode_nfc_null_out_is_internal);
    RUN_TEST(unicode_nfc_allows_a_null_length_out);
    RUN_TEST(unicode_nfc_rejects_a_length_past_ptrdiff_max);
    RUN_TEST(unicode_nfc_identity_on_a_long_ascii_string);
    RUN_TEST(unicode_nfc_injected_nomem_applies_once);
    RUN_TEST(unicode_nfc_injected_unexpected_status_is_internal);
    RUN_TEST(unicode_graphemes_crlf_is_one_cluster);
    RUN_TEST(unicode_graphemes_combining_mark_stays_with_the_base);
    RUN_TEST(unicode_graphemes_regional_indicators_pair_then_break);
    RUN_TEST(unicode_graphemes_zwj_emoji_is_one_cluster);
    RUN_TEST(unicode_graphemes_indic_conjunct_is_one_cluster);
    RUN_TEST(unicode_graphemes_two_ascii_letters_are_two_clusters);
    RUN_TEST(unicode_graphemes_stops_at_an_interior_nul);
    RUN_TEST(unicode_graphemes_empty_is_done);
    RUN_TEST(unicode_graphemes_next_rejects_invalidated_bytes);
    RUN_TEST(unicode_graphemes_next_rejects_a_later_invalid_byte);
    RUN_TEST(unicode_graphemes_failed_reopen_drops_the_previous_string);
    RUN_TEST(unicode_graphemes_reopen_restarts_at_the_first_byte);
    RUN_TEST(unicode_graphemes_invalid_open_is_not_iterable);
    RUN_TEST(unicode_graphemes_unopened_next_is_internal);
    RUN_TEST(unicode_graphemes_null_iterator_is_internal);
    RUN_TEST(unicode_graphemes_null_source_is_internal);
    RUN_TEST(unicode_graphemes_null_span_outs_are_internal);
    RUN_TEST(unicode_graphemes_length_past_ptrdiff_max_is_internal);
    RUN_TEST(unicode_decode_reads_one_scalar_of_each_length);
    RUN_TEST(unicode_decode_does_not_read_past_len);
    RUN_TEST(unicode_decode_rejects_an_invalid_byte);
    RUN_TEST(unicode_decode_bad_arguments_are_internal);
    RUN_TEST(unicode_decode_length_past_ptrdiff_max_is_internal);
    RUN_TEST(unicode_category_matches_unicode_general_category);
    RUN_TEST(unicode_whitespace_is_unicode_18_white_space);
    RUN_TEST(unicode_whitespace_rejects_near_misses);
}
