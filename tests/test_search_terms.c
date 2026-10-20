#include "register.h"
#include "search_terms.h"
#include "test.h"
#include "unicode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Pure unit tests for search_terms (plan 18 stage 2, D13). Byte escapes:
 * U+00E9 c3 a9; U+0301 cc 81; U+3000 e3 80 80; U+00A0 c2 a0; U+0085 c2 85;
 * Hebrew samekh d7 a1, pe d7 a4, resh d7 a8, tsere d6 b5, segol d6 b6;
 * U+1F6D1 f0 9f 9b 91; ZWJ e2 80 8d; U+2026 e2 80 a6.
 */

enum {
    UNWRITTEN = 9,    /* out value the call must leave alone */
    TOKENS_17 = 17,   /* one past the token cap */
    CPS_65 = 65,      /* one past the code point cap */
    LABEL_60 = 60,    /* the grapheme cut */
    LABEL_61 = 61,    /* one past the grapheme cut */
    QUERY_CAP = 1024, /* scratch for built queries */
    COUNT_FEW = 1,
    COUNT_MANY = 5
};

static const char SAMEKH_PE_RESH[] = "\xd7\xa1\xd7\xa4\xd7\xa8";
static const char VOCALIZED_SEFER[] = "\xd7\xa1\xd6\xb5\xd7\xa4\xd6\xb6\xd7\xa8";
static const char ELLIPSIS[] = "\xe2\x80\xa6";

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

/* query builds exactly the tokens in want, in order. */
static void assert_tokens(const char *query, const char *const *want, size_t n)
{
    SearchTokens toks;
    size_t i = 0;
    SearchStatus st = search_tokens_build(query, &toks);

    ASSERT_EQ_INT(st, SEARCH_OK);
    if (st != SEARCH_OK) {
        return;
    }
    ASSERT_EQ_INT((int)toks.count, (int)n);
    for (i = 0; i < n && i < toks.count; i++) {
        ASSERT_STREQ(toks.tokens[i], want[i]);
    }
}

/* text classified against the tokens and q of query has tier want. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) input, then expected
static void assert_tier(const char *query, const char *text, SearchTier want)
{
    SearchTokens toks;
    char q[QUERY_CAP];
    SearchFacet facet = {.text = NULL, .tier = SEARCH_TIER_NONE};
    SearchStatus st = search_tokens_build(query, &toks);

    ASSERT_EQ_INT(st, SEARCH_OK);
    st = search_q(query, q, sizeof q);
    ASSERT_EQ_INT(st, SEARCH_OK);
    st = search_facet_classify(&toks, q, text, 0, &facet);
    ASSERT_EQ_INT(st, SEARCH_OK);
    ASSERT_EQ_INT(facet.tier, want);
}

/* search_q(query) is want. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) input, then expected
static void assert_q(const char *query, const char *want)
{
    char q[QUERY_CAP];
    SearchStatus st = search_q(query, q, sizeof q);

    ASSERT_EQ_INT(st, SEARCH_OK);
    ASSERT_STREQ(q, want);
}

/* The label of (body, key) is want. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) input, then expected
static void assert_label(const char *body, const char *key, const char *want)
{
    char *out = NULL;
    SearchStatus st = search_label(body, key, &out);

    ASSERT_EQ_INT(st, SEARCH_OK);
    ASSERT_STREQ(out, want);
    free(out);
}

/* n copies of unit, joined by sep (may be ""), into buf. */
static void given_repeated(char *buf, size_t cap, const char *unit, const char *sep, size_t n)
{
    size_t i = 0;

    buf[0] = '\0';
    for (i = 0; i < n; i++) {
        if (i > 0) {
            (void)strncat(buf, sep, cap - strlen(buf) - 1U);
        }
        (void)strncat(buf, unit, cap - strlen(buf) - 1U);
    }
}

/* a, then b, into buf. */
static void given_join(char *buf, size_t cap, const char *a, const char *b)
{
    (void)snprintf(buf, cap, "%s%s", a, b);
}

/* ---- tokens (E2.2 steps 1-3) ------------------------------------------- */

TEST(search_tokens_split_on_ascii_punctuation_and_space)
{
    static const char *const want[] = {"recs", "books", "boo"};

    assert_tokens("recs:books  boo", want, COUNT_OF(want));
}

TEST(search_tokens_split_on_a_double_quote)
{
    /* '"' is outside [A-Za-z0-9], so no token ever holds one (E2.2 step 4 is vacuous). */
    static const char *const want[] = {"a", "b"};

    assert_tokens("a\"b", want, COUNT_OF(want));
}

TEST(search_tokens_keep_ascii_letters_and_digits_together)
{
    /* The edges of [A-Za-z0-9] stay inside one token. */
    static const char *const want[] = {"azaz09", "plan18"};

    assert_tokens("AZaz09 plan18", want, COUNT_OF(want));
}

TEST(search_tokens_split_on_each_neighbor_of_the_alnum_ranges)
{
    /* '/' ':' '@' '[' '`' '{' sit just outside 0-9, A-Z, and a-z. */
    static const char *const want[] = {"a", "b", "c", "d", "e", "f", "g"};

    assert_tokens("a/b:c@d[e`f{g", want, COUNT_OF(want));
}

TEST(search_tokens_split_on_del_but_keep_u0080)
{
    /* U+007F is ASCII outside the set; U+0080 is not ASCII and not White_Space. */
    static const char *const want[] = {"a", "b\xc2\x80"
                                            "c"};

    assert_tokens("a\x7f"
                  "b\xc2\x80"
                  "c",
                  want, COUNT_OF(want));
}

TEST(search_tokens_are_ascii_lowercased)
{
    /* E2.5 lowercases ASCII only; U+00C9 stays. */
    static const char *const want[] = {"book", "\xc3\x89t\xc3\xa9"};

    assert_tokens("BoOK \xc3\x89T\xc3\xa9", want, COUNT_OF(want));
}

TEST(search_tokens_compose_to_nfc)
{
    static const char *const want[] = {"caf\xc3\xa9"};

    assert_tokens("cafe\xcc\x81", want, COUNT_OF(want));
}

TEST(search_tokens_split_on_ideographic_space)
{
    static const char *const want[] = {"a", "b"};

    assert_tokens("a\xe3\x80\x80"
                  "b",
                  want, COUNT_OF(want));
}

TEST(search_tokens_split_on_no_break_space_and_nel)
{
    static const char *const want[] = {"a", "b", "c"};

    assert_tokens("a\xc2\xa0"
                  "b\xc2\x85"
                  "c",
                  want, COUNT_OF(want));
}

TEST(search_tokens_keep_non_ascii_inside_a_token)
{
    /* U+20AC is not whitespace and not ASCII, so it does not split. */
    static const char *const want[] = {"a\xe2\x82\xac"
                                       "b",
                                       "\xf0\x9f\x9b\x91"};

    assert_tokens("a\xe2\x82\xac"
                  "b \xf0\x9f\x9b\x91",
                  want, COUNT_OF(want));
}

TEST(search_tokens_keep_the_first_16)
{
    static const char *const want[] = {"a", "b", "c", "d", "e", "f", "g", "h",
                                       "i", "j", "k", "l", "m", "n", "o", "p"};

    assert_tokens("a b c d e f g h i j k l m n o p q", want, COUNT_OF(want));
}

TEST(search_tokens_keep_the_first_64_code_points)
{
    char query[QUERY_CAP];
    char want64[QUERY_CAP];
    const char *want[1] = {want64};

    given_repeated(query, sizeof query, "a", "", CPS_65);
    given_repeated(want64, sizeof want64, "a", "", SEARCH_TOKEN_CPS_MAX);
    assert_tokens(query, want, COUNT_OF(want));
}

TEST(search_tokens_cap_counts_code_points_not_bytes)
{
    /* 65 four-byte scalars: the kept token is 256 bytes, the buffer's edge. */
    char query[QUERY_CAP];
    char want64[QUERY_CAP];
    const char *want[1] = {want64};

    given_repeated(query, sizeof query, "\xf0\x9f\x9b\x91", "", CPS_65);
    given_repeated(want64, sizeof want64, "\xf0\x9f\x9b\x91", "", SEARCH_TOKEN_CPS_MAX);
    assert_tokens(query, want, COUNT_OF(want));
}

TEST(search_tokens_remove_hebrew_marks)
{
    const char *want[] = {SAMEKH_PE_RESH};

    assert_tokens(VOCALIZED_SEFER, want, COUNT_OF(want));
}

TEST(search_tokens_cap_before_the_fold)
{
    /* Samekh + 63 tsere fill the 64 code points, so the pe after them is cut;
       then the fold removes the marks (E2.2 step 2 before step 3). */
    char query[QUERY_CAP];
    char marks[QUERY_CAP];
    static const char *const want[] = {"\xd7\xa1"};

    given_repeated(marks, sizeof marks, "\xd6\xb5", "", SEARCH_TOKEN_CPS_MAX - 1);
    (void)snprintf(query, sizeof query, "\xd7\xa1%s\xd7\xa4", marks);
    assert_tokens(query, want, COUNT_OF(want));
}

TEST(search_tokens_drop_a_token_the_fold_empties)
{
    const char *want[] = {SAMEKH_PE_RESH};
    char query[QUERY_CAP];

    given_join(query, sizeof query, SAMEKH_PE_RESH, " \xd6\xb5");
    assert_tokens(query, want, COUNT_OF(want));
}

TEST(search_tokens_an_emptied_token_still_used_a_slot)
{
    /* 15 letters, one mark-only token (slot 16), then "q" (slot 17, cut). */
    static const char *const want[] = {"a", "b", "c", "d", "e", "f", "g", "h",
                                       "i", "j", "k", "l", "m", "n", "o"};

    assert_tokens("a b c d e f g h i j k l m n o \xd6\xb5 q", want, COUNT_OF(want));
}

TEST(search_tokens_only_separators_is_no_token)
{
    assert_tokens("  :: \" () \xe3\x80\x80", NULL, 0);
}

TEST(search_tokens_empty_query_is_no_token)
{
    assert_tokens("", NULL, 0);
}

TEST(search_tokens_invalid_utf8_is_rejected)
{
    SearchTokens toks;
    SearchStatus st = SEARCH_OK;

    toks.count = UNWRITTEN;
    st = search_tokens_build("ok \xff", &toks);
    ASSERT_EQ_INT(st, SEARCH_ERR_INVALID_UTF8);
    ASSERT_EQ_INT((int)toks.count, UNWRITTEN);
}

TEST(search_tokens_nfc_oom_is_oom)
{
    SearchTokens toks;
    SearchStatus st = SEARCH_OK;

    unicode_test_inject_nfc(UNICODE_TEST_INJECT_NOMEM);
    st = search_tokens_build("a", &toks);
    ASSERT_EQ_INT(st, SEARCH_ERR_OOM);
}

TEST(search_tokens_nfc_internal_is_internal)
{
    SearchTokens toks;
    SearchStatus st = SEARCH_OK;

    unicode_test_inject_nfc(UNICODE_TEST_INJECT_UNEXPECTED);
    st = search_tokens_build("a", &toks);
    ASSERT_EQ_INT(st, SEARCH_ERR_INTERNAL);
}

TEST(search_tokens_null_arguments_are_internal)
{
    SearchTokens toks;

    ASSERT_EQ_INT(search_tokens_build(NULL, &toks), SEARCH_ERR_INTERNAL);
    ASSERT_EQ_INT(search_tokens_build("a", NULL), SEARCH_ERR_INTERNAL);
}

/* ---- q (E2.4) ------------------------------------------------------------ */

TEST(search_q_trims_ascii_whitespace_and_lowercases_ascii)
{
    assert_q(" \t Recs:BO \r\n", "recs:bo");
}

TEST(search_q_lowercases_only_a_to_z)
{
    assert_q("AZ@[`{", "az@[`{");
}

TEST(search_q_keeps_non_ascii_case_and_unicode_spaces)
{
    assert_q("\xe3\x80\x80\xc3\x89X", "\xe3\x80\x80\xc3\x89x");
}

TEST(search_q_blank_is_empty)
{
    assert_q(" \t\n", "");
}

TEST(search_q_short_buffer_is_internal)
{
    char q[3] = {'x', 'x', 'x'};

    ASSERT_EQ_INT(search_q("abc", q, sizeof q), SEARCH_ERR_INTERNAL);
    ASSERT_EQ_INT(q[0], 'x');
}

TEST(search_q_null_arguments_are_internal)
{
    char q[4];

    ASSERT_EQ_INT(search_q(NULL, q, sizeof q), SEARCH_ERR_INTERNAL);
    ASSERT_EQ_INT(search_q("a", NULL, sizeof q), SEARCH_ERR_INTERNAL);
}

/* ---- tiers (E2.5, E2.6, D28) --------------------------------------------- */

TEST(search_tier_word_prefix_of_a_key_segment_matches)
{
    assert_tier("book", "recs:books", SEARCH_TIER_2);
}

TEST(search_tier_one_segment_serves_two_tokens)
{
    assert_tier("boo book", "recs:books", SEARCH_TIER_2);
}

TEST(search_tier_a_token_without_a_segment_does_not_match)
{
    assert_tier("recs games", "recs:books", SEARCH_TIER_NONE);
}

TEST(search_tier_whole_key_prefix_is_tier_1)
{
    assert_tier("recs:bo", "recs:books", SEARCH_TIER_1);
}

TEST(search_tier_in_order_tokens_are_tier_2)
{
    assert_tier("fort cloud", "project:fortkeeps-cloud", SEARCH_TIER_2);
}

TEST(search_tier_out_of_order_tokens_are_tier_3)
{
    assert_tier("cloud fortkeeps", "project:fortkeeps-cloud", SEARCH_TIER_3);
}

TEST(search_tier_in_order_skips_an_earlier_segment)
{
    /* "b" is at index 1; the next "a" must come from index 2, not index 0. */
    assert_tier("b a", "a:b:ab", SEARCH_TIER_2);
}

TEST(search_tier_word_order_does_not_decide_the_match)
{
    assert_tier("books recs", "recs:books", SEARCH_TIER_3);
}

TEST(search_tier_splits_on_every_listed_separator)
{
    assert_tier("a b c d e f g", "a:b-c_d.e/f g", SEARCH_TIER_2);
}

TEST(search_tier_splits_on_unicode_whitespace)
{
    assert_tier("b",
                "a\xe3\x80\x80"
                "b",
                SEARCH_TIER_2);
}

TEST(search_tier_does_not_split_on_other_punctuation)
{
    /* '+' splits a token but not a segment: "c++notes" stays one segment. */
    assert_tier("notes", "c++notes", SEARCH_TIER_NONE);
}

TEST(search_tier_matches_an_nfd_key_through_nfc)
{
    assert_tier("caf\xc3\xa9", "cafe\xcc\x81:menu", SEARCH_TIER_2);
}

TEST(search_tier_matches_a_vocalized_key_through_the_fold)
{
    char key[QUERY_CAP];

    given_join(key, sizeof key, VOCALIZED_SEFER, ":x");
    assert_tier(SAMEKH_PE_RESH, key, SEARCH_TIER_2);
}

TEST(search_tier_no_tokens_is_none_without_a_prefix)
{
    assert_tier("::", "recs:books", SEARCH_TIER_NONE);
}

TEST(search_tier_no_tokens_still_takes_a_whole_prefix)
{
    assert_tier("::", "::odd", SEARCH_TIER_1);
}

TEST(search_tier_blank_query_matches_nothing)
{
    assert_tier("  ", "recs:books", SEARCH_TIER_NONE);
}

TEST(search_tier_exact_text_sets_exact)
{
    SearchTokens toks;
    SearchFacet facet = {.text = NULL, .tier = SEARCH_TIER_NONE};
    SearchStatus st = search_tokens_build("recs", &toks);

    ASSERT_EQ_INT(st, SEARCH_OK);
    st = search_facet_classify(&toks, "recs", "recs", COUNT_MANY, &facet);
    ASSERT_EQ_INT(st, SEARCH_OK);
    ASSERT_TRUE(facet.exact);
    ASSERT_EQ_INT(facet.tier, SEARCH_TIER_1);
    ASSERT_EQ_INT((int)facet.count, COUNT_MANY);
    ASSERT_STREQ(facet.text, "recs");
}

TEST(search_tier_prefix_text_is_not_exact)
{
    SearchTokens toks;
    SearchFacet facet = {.text = NULL, .tier = SEARCH_TIER_NONE, .exact = true};
    SearchStatus st = search_tokens_build("recs", &toks);

    ASSERT_EQ_INT(st, SEARCH_OK);
    st = search_facet_classify(&toks, "recs", "recs:books", 0, &facet);
    ASSERT_EQ_INT(st, SEARCH_OK);
    ASSERT_FALSE(facet.exact);
}

TEST(search_tier_invalid_utf8_text_is_rejected)
{
    SearchTokens toks;
    SearchFacet facet = {.text = NULL, .tier = SEARCH_TIER_3};
    SearchStatus st = search_tokens_build("a", &toks);

    ASSERT_EQ_INT(st, SEARCH_OK);
    st = search_facet_classify(&toks, "a", "b:\xff", 0, &facet);
    ASSERT_EQ_INT(st, SEARCH_ERR_INVALID_UTF8);
    ASSERT_EQ_INT(facet.tier, SEARCH_TIER_3);
}

TEST(search_tier_nfc_oom_is_oom)
{
    SearchTokens toks;
    SearchFacet facet;
    SearchStatus st = search_tokens_build("a", &toks);

    ASSERT_EQ_INT(st, SEARCH_OK);
    unicode_test_inject_nfc(UNICODE_TEST_INJECT_NOMEM);
    st = search_facet_classify(&toks, "a", "b:a", 0, &facet);
    ASSERT_EQ_INT(st, SEARCH_ERR_OOM);
}

TEST(search_tier_null_arguments_are_internal)
{
    SearchTokens toks;
    SearchFacet facet;

    toks.count = 0;
    ASSERT_EQ_INT(search_facet_classify(NULL, "a", "a", 0, &facet), SEARCH_ERR_INTERNAL);
    ASSERT_EQ_INT(search_facet_classify(&toks, NULL, "a", 0, &facet), SEARCH_ERR_INTERNAL);
    ASSERT_EQ_INT(search_facet_classify(&toks, "a", NULL, 0, &facet), SEARCH_ERR_INTERNAL);
    ASSERT_EQ_INT(search_facet_classify(&toks, "a", "a", 0, NULL), SEARCH_ERR_INTERNAL);
}

/* ---- facet order (E2.6, D28) --------------------------------------------- */

TEST(search_key_order_is_tier_then_code_point)
{
    SearchFacet keys[] = {
        {.text = "\xc3\xa9", .tier = SEARCH_TIER_2}, {.text = "z", .tier = SEARCH_TIER_2},
        {.text = "a", .tier = SEARCH_TIER_3},        {.text = "a:b", .tier = SEARCH_TIER_2},
        {.text = "b", .tier = SEARCH_TIER_1},        {.text = "B", .tier = SEARCH_TIER_2},
    };

    qsort(keys, sizeof keys / sizeof keys[0], sizeof keys[0], search_key_order);
    ASSERT_STREQ(keys[0].text, "b");
    ASSERT_STREQ(keys[1].text, "B");
    ASSERT_STREQ(keys[2].text, "a:b");
    ASSERT_STREQ(keys[3].text, "z");
    ASSERT_STREQ(keys[4].text, "\xc3\xa9");
    ASSERT_STREQ(keys[5].text, "a");
}

TEST(search_tag_order_is_exact_tier_count_then_code_point)
{
    SearchFacet tags[] = {
        {.text = "t3many", .count = COUNT_MANY, .tier = SEARCH_TIER_3},
        {.text = "t2few", .count = COUNT_FEW, .tier = SEARCH_TIER_2},
        {.text = "t1b", .count = COUNT_FEW, .tier = SEARCH_TIER_1},
        {.text = "t1a", .count = COUNT_FEW, .tier = SEARCH_TIER_1},
        {.text = "t1many", .count = COUNT_MANY, .tier = SEARCH_TIER_1},
        {.text = "t", .count = COUNT_FEW, .tier = SEARCH_TIER_1, .exact = true},
    };

    qsort(tags, sizeof tags / sizeof tags[0], sizeof tags[0], search_tag_order);
    ASSERT_STREQ(tags[0].text, "t");
    ASSERT_STREQ(tags[1].text, "t1many");
    ASSERT_STREQ(tags[2].text, "t1a");
    ASSERT_STREQ(tags[3].text, "t1b");
    ASSERT_STREQ(tags[4].text, "t2few");
    ASSERT_STREQ(tags[5].text, "t3many");
}

/* ---- label (E2.6, D12, P2) ----------------------------------------------- */

TEST(search_label_is_the_title)
{
    assert_label("# Book recommendations\nmore text", "recs:books", "Book recommendations");
}

TEST(search_label_title_is_ascii_trimmed)
{
    assert_label("#   Title \t\r\nbody", NULL, "Title");
}

TEST(search_label_blank_title_falls_back_to_the_key)
{
    assert_label("#   \nbody", "k:one", "k:one");
}

TEST(search_label_hash_without_a_space_is_not_a_title)
{
    assert_label("#Title", "k:one", "k:one");
}

TEST(search_label_title_only_comes_from_the_first_line)
{
    assert_label("intro\n# Later", NULL, "intro # Later");
}

TEST(search_label_without_title_is_the_key)
{
    assert_label("plain body", "pref:editor", "pref:editor");
}

TEST(search_label_collapses_whitespace_and_controls)
{
    /* Tab, LF, U+0001, U+3000, and NEL runs become one space each. */
    assert_label("a\t\n b\x01"
                 "c\xe3\x80\x80\xc2\x85"
                 "d",
                 NULL, "a b c d");
}

TEST(search_label_trims_the_plain_text)
{
    assert_label("\n\t x \xe3\x80\x80", NULL, "x");
}

TEST(search_label_keeps_a_zero_width_joiner)
{
    /* ZWJ is Cf, not Cc, so it is not a control for D12. */
    assert_label("a\xe2\x80\x8d"
                 "b",
                 NULL,
                 "a\xe2\x80\x8d"
                 "b");
}

TEST(search_label_sixty_graphemes_have_no_ellipsis)
{
    char body[QUERY_CAP];

    given_repeated(body, sizeof body, "a", "", LABEL_60);
    assert_label(body, NULL, body);
}

TEST(search_label_sixty_one_graphemes_are_cut_with_an_ellipsis)
{
    char letters[QUERY_CAP];
    char body[QUERY_CAP];
    char want[QUERY_CAP];

    given_repeated(body, sizeof body, "a", "", LABEL_61);
    given_repeated(letters, sizeof letters, "a", "", LABEL_60);
    given_join(want, sizeof want, letters, ELLIPSIS);
    assert_label(body, NULL, want);
}

TEST(search_label_cut_keeps_combining_marks_with_their_base)
{
    char letters[QUERY_CAP];
    char body[QUERY_CAP];
    char want[QUERY_CAP];

    given_repeated(body, sizeof body, "e\xcc\x81", "", LABEL_61);
    given_repeated(letters, sizeof letters, "e\xcc\x81", "", LABEL_60);
    given_join(want, sizeof want, letters, ELLIPSIS);
    assert_label(body, NULL, want);
}

TEST(search_label_counts_a_zwj_emoji_sequence_as_one_grapheme)
{
    /* 59 letters + one ZWJ sequence = 60 graphemes; one more letter is cut. */
    static const char stop_zwj_stop[] = "\xf0\x9f\x9b\x91\xe2\x80\x8d\xf0\x9f\x9b\x91";
    char letters[QUERY_CAP];
    char kept[QUERY_CAP];
    char body[QUERY_CAP];
    char want[QUERY_CAP];

    given_repeated(letters, sizeof letters, "a", "", LABEL_60 - 1);
    given_join(kept, sizeof kept, letters, stop_zwj_stop);
    given_join(body, sizeof body, kept, "z");
    given_join(want, sizeof want, kept, ELLIPSIS);
    assert_label(body, NULL, want);
}

TEST(search_label_invalid_utf8_plain_text_is_rejected)
{
    char sentinel = 'x';
    char *out = &sentinel;
    SearchStatus st = search_label("ok \xff", NULL, &out);

    ASSERT_EQ_INT(st, SEARCH_ERR_INVALID_UTF8);
    ASSERT_TRUE(out == NULL);
}

TEST(search_label_null_arguments_are_internal)
{
    char *out = NULL;

    ASSERT_EQ_INT(search_label(NULL, "k", &out), SEARCH_ERR_INTERNAL);
    ASSERT_EQ_INT(search_label("b", "k", NULL), SEARCH_ERR_INTERNAL);
}

void register_search_terms_tests(void)
{
    RUN_TEST(search_tokens_split_on_ascii_punctuation_and_space);
    RUN_TEST(search_tokens_split_on_a_double_quote);
    RUN_TEST(search_tokens_keep_ascii_letters_and_digits_together);
    RUN_TEST(search_tokens_split_on_each_neighbor_of_the_alnum_ranges);
    RUN_TEST(search_tokens_split_on_del_but_keep_u0080);
    RUN_TEST(search_tokens_are_ascii_lowercased);
    RUN_TEST(search_tokens_compose_to_nfc);
    RUN_TEST(search_tokens_split_on_ideographic_space);
    RUN_TEST(search_tokens_split_on_no_break_space_and_nel);
    RUN_TEST(search_tokens_keep_non_ascii_inside_a_token);
    RUN_TEST(search_tokens_keep_the_first_16);
    RUN_TEST(search_tokens_keep_the_first_64_code_points);
    RUN_TEST(search_tokens_cap_counts_code_points_not_bytes);
    RUN_TEST(search_tokens_remove_hebrew_marks);
    RUN_TEST(search_tokens_cap_before_the_fold);
    RUN_TEST(search_tokens_drop_a_token_the_fold_empties);
    RUN_TEST(search_tokens_an_emptied_token_still_used_a_slot);
    RUN_TEST(search_tokens_only_separators_is_no_token);
    RUN_TEST(search_tokens_empty_query_is_no_token);
    RUN_TEST(search_tokens_invalid_utf8_is_rejected);
    RUN_TEST(search_tokens_nfc_oom_is_oom);
    RUN_TEST(search_tokens_nfc_internal_is_internal);
    RUN_TEST(search_tokens_null_arguments_are_internal);
    RUN_TEST(search_q_trims_ascii_whitespace_and_lowercases_ascii);
    RUN_TEST(search_q_lowercases_only_a_to_z);
    RUN_TEST(search_q_keeps_non_ascii_case_and_unicode_spaces);
    RUN_TEST(search_q_blank_is_empty);
    RUN_TEST(search_q_short_buffer_is_internal);
    RUN_TEST(search_q_null_arguments_are_internal);
    RUN_TEST(search_tier_word_prefix_of_a_key_segment_matches);
    RUN_TEST(search_tier_one_segment_serves_two_tokens);
    RUN_TEST(search_tier_a_token_without_a_segment_does_not_match);
    RUN_TEST(search_tier_whole_key_prefix_is_tier_1);
    RUN_TEST(search_tier_in_order_tokens_are_tier_2);
    RUN_TEST(search_tier_out_of_order_tokens_are_tier_3);
    RUN_TEST(search_tier_in_order_skips_an_earlier_segment);
    RUN_TEST(search_tier_word_order_does_not_decide_the_match);
    RUN_TEST(search_tier_splits_on_every_listed_separator);
    RUN_TEST(search_tier_splits_on_unicode_whitespace);
    RUN_TEST(search_tier_does_not_split_on_other_punctuation);
    RUN_TEST(search_tier_matches_an_nfd_key_through_nfc);
    RUN_TEST(search_tier_matches_a_vocalized_key_through_the_fold);
    RUN_TEST(search_tier_no_tokens_is_none_without_a_prefix);
    RUN_TEST(search_tier_no_tokens_still_takes_a_whole_prefix);
    RUN_TEST(search_tier_blank_query_matches_nothing);
    RUN_TEST(search_tier_exact_text_sets_exact);
    RUN_TEST(search_tier_prefix_text_is_not_exact);
    RUN_TEST(search_tier_invalid_utf8_text_is_rejected);
    RUN_TEST(search_tier_nfc_oom_is_oom);
    RUN_TEST(search_tier_null_arguments_are_internal);
    RUN_TEST(search_key_order_is_tier_then_code_point);
    RUN_TEST(search_tag_order_is_exact_tier_count_then_code_point);
    RUN_TEST(search_label_is_the_title);
    RUN_TEST(search_label_title_is_ascii_trimmed);
    RUN_TEST(search_label_blank_title_falls_back_to_the_key);
    RUN_TEST(search_label_hash_without_a_space_is_not_a_title);
    RUN_TEST(search_label_title_only_comes_from_the_first_line);
    RUN_TEST(search_label_without_title_is_the_key);
    RUN_TEST(search_label_collapses_whitespace_and_controls);
    RUN_TEST(search_label_trims_the_plain_text);
    RUN_TEST(search_label_keeps_a_zero_width_joiner);
    RUN_TEST(search_label_sixty_graphemes_have_no_ellipsis);
    RUN_TEST(search_label_sixty_one_graphemes_are_cut_with_an_ellipsis);
    RUN_TEST(search_label_cut_keeps_combining_marks_with_their_base);
    RUN_TEST(search_label_counts_a_zwj_emoji_sequence_as_one_grapheme);
    RUN_TEST(search_label_invalid_utf8_plain_text_is_rejected);
    RUN_TEST(search_label_null_arguments_are_internal);
}
