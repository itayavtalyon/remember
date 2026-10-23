#include "harness.h"
#include "normalize.h"
#include "register.h"
#include "store.h"
#include "test.h"
#include "unicode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * store_search union (plan 18 stage 3): P prefix part, E2.1a parse probe,
 * (P) OR (R), de-duplication, E2.4 ranking. Fixture = 006 Examples.
 */

enum { PAGE_LIMIT = 20, BODIES_CAP = 512 };

static const char k_now[] = "2026-06-15T12:00:00.000Z";

typedef struct {
    const char *body;
    const char *key;
    const char *const *tags;
    size_t ntags;
    const char *created;
} GivenMemory;

static Store *given_store(char **db)
{
    char err[ERR_BUFSIZE];

    *db = make_temp_db_path();
    return (*db != NULL) ? store_open(*db, err, sizeof(err)) : NULL;
}

/* One live memory with its real body hash. */
static void given_memory(Store *s, GivenMemory g)
{
    char hash[REMEMBER_SHA256_HEX_LEN + 1];
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;

    body_hash_hex(g.body, strlen(g.body), hash);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_add(s, g.body, hash, g.key, g.tags, g.ntags, "human", NULL,
                               g.created != NULL ? g.created : k_now, &act, &e),
                     STORE_OK);
    store_entry_free(&e);
}

/* The three memories of the 006 Examples. */
static Store *given_examples(char **db)
{
    static const char *const books_tags[] = {"recs", "type:books"};
    static const char *const family_tags[] = {"family"};
    Store *s = given_store(db);

    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        return NULL;
    }
    given_memory(s, (GivenMemory){.body = "# Book recommendations",
                                  .key = "recs:books",
                                  .tags = books_tags,
                                  .ntags = 2U});
    given_memory(s, (GivenMemory){.body = "Book the dentist", .tags = family_tags, .ntags = 1U});
    given_memory(s, (GivenMemory){.body = "Use Zed", .key = "pref:editor"});
    return s;
}

static void free_page(PageResult page)
{
    size_t i = 0;

    for (i = 0; i < page.count; i++) {
        store_entry_free(&page.entries[i]);
    }
    free(page.entries);
}

static SearchResult when_search(Store *s, const char *query)
{
    SearchQuery sq;

    memset(&sq, 0, sizeof(sq));
    sq.query = query;
    sq.filters.limit = PAGE_LIMIT;
    return store_search(s, &sq, k_now);
}

/* store_search(query) succeeds and returns the bodies want ("a|b|c"), in order. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) input, then expected
static void assert_bodies(Store *s, const char *query, const char *want)
{
    SearchResult res = when_search(s, query);
    char got[BODIES_CAP];
    size_t i = 0;

    ASSERT_EQ_STATUS(res.page.st, STORE_OK);
    got[0] = '\0';
    for (i = 0; i < res.page.count; i++) {
        (void)strncat(got, i > 0 ? "|" : "", sizeof(got) - strlen(got) - 1U);
        (void)strncat(got, res.page.entries[i].body, sizeof(got) - strlen(got) - 1U);
    }
    ASSERT_STREQ(got, want);
    free_page(res.page);
    free(res.warning);
}

/* store_search(query) succeeds with warning want (NULL: no warning). */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) input, then expected
static void assert_warning(Store *s, const char *query, const char *want)
{
    SearchResult res = when_search(s, query);

    ASSERT_EQ_STATUS(res.page.st, STORE_OK);
    if (want == NULL) {
        ASSERT_TRUE(res.warning == NULL);
    } else {
        ASSERT_STREQ(res.warning, want);
    }
    free_page(res.page);
    free(res.warning);
}

/* E2.2 / Example 1: a word prefix matches through P. */
TEST(store_search_matches_a_word_prefix)
{
    char *db = NULL;
    Store *s = given_examples(&db);

    assert_bodies(s, "boo", "# Book recommendations|Book the dentist");
    store_close(s);
    free(db);
}

/* E2.1 (5) / Example 2: a raw part FTS5 cannot parse leaves P alone. */
TEST(store_search_runs_p_alone_when_r_does_not_parse)
{
    char *db = NULL;
    Store *s = given_examples(&db);

    assert_bodies(s, "recs:bo", "# Book recommendations");
    store_close(s);
    free(db);
}

/* E2.3: the warning carries the FTS5 message. */
TEST(store_search_warns_with_the_fts_message)
{
    char *db = NULL;
    Store *s = given_examples(&db);

    assert_warning(s, "recs:bo", "no such column: recs");
    store_close(s);
    free(db);
}

/* E2.1 (5): no P and an unparseable R is an empty result, still warned. */
TEST(store_search_without_p_and_unparseable_r_is_empty)
{
    char *db = NULL;
    Store *s = given_examples(&db);

    assert_bodies(s, "\"", "");
    assert_warning(s, "\"", "unterminated string");
    store_close(s);
    free(db);
}

/* E2.1 (4): a blank query is an empty result with no warning. */
TEST(store_search_blank_query_is_empty_without_warning)
{
    char *db = NULL;
    Store *s = given_examples(&db);

    assert_bodies(s, " \t ", "");
    assert_warning(s, " \t ", NULL);
    store_close(s);
    free(db);
}

/* E2.1 (3) / Example 3: a raw part that parses adds its rows to P's. */
TEST(store_search_unions_the_raw_part)
{
    char *db = NULL;
    Store *s = given_examples(&db);
    SearchResult res = when_search(s, "book OR zed");

    ASSERT_EQ_STATUS(res.page.st, STORE_OK);
    ASSERT_EQ_INT((int)res.page.total, 3);
    ASSERT_TRUE(res.warning == NULL);
    free_page(res.page);
    store_close(s);
    free(db);
}

/* E2.1b: a row both parts match appears once, and total counts it once. */
TEST(store_search_counts_a_row_both_parts_match_once)
{
    char *db = NULL;
    Store *s = given_examples(&db);
    SearchResult res = when_search(s, "book");

    ASSERT_EQ_STATUS(res.page.st, STORE_OK);
    ASSERT_EQ_INT((int)res.page.count, 2);
    ASSERT_EQ_INT((int)res.page.total, 2);
    free_page(res.page);
    store_close(s);
    free(db);
}

/* E2.1 (3): P empty (a lone Hebrew mark folds away) runs R alone. */
TEST(store_search_runs_r_alone_when_p_is_empty)
{
    char *db = NULL;
    Store *s = given_examples(&db);

    assert_bodies(s, "\xd6\xb5", "");
    assert_warning(s, "\xd6\xb5", NULL);
    store_close(s);
    free(db);
}

/* E2.4 (1)-(2): the exact key, then a key q prefixes, then bm25. bm25 alone
   ranks the first three in reverse (probed), so each key rule is needed; the
   last key differs from q only in q's last character. */
TEST(store_search_ranks_exact_key_then_key_prefix_first)
{
    char *db = NULL;
    Store *s = given_store(&db);

    given_memory(s, (GivenMemory){.body = "editor editor editor", .key = "pref:editor"});
    given_memory(
        s, (GivenMemory){.body = "one editor two three four", .key = "editor:main:notes:old"});
    given_memory(s,
                 (GivenMemory){.body = "alpha beta gamma delta epsilon zeta eta theta iota kappa",
                               .key = "editor"});
    given_memory(
        s,
        (GivenMemory){.body = "editor lambda mu nu xi omicron pi rho sigma tau upsilon phi chi psi",
                      .key = "editox"});
    assert_bodies(s, "Editor",
                  "alpha beta gamma delta epsilon zeta eta theta iota kappa|"
                  "one editor two three four|editor editor editor|"
                  "editor lambda mu nu xi omicron pi rho sigma tau upsilon phi chi psi");
    store_close(s);
    free(db);
}

/* E2.4: q is QUERY trimmed and lowercased in the store too (the CLI trims, but
   the store owns E2.1's R and q). bm25 alone puts pref:editor first. */
TEST(store_search_trims_the_query_before_ranking)
{
    char *db = NULL;
    Store *s = given_store(&db);

    given_memory(s, (GivenMemory){.body = "editor editor editor", .key = "pref:editor"});
    given_memory(s,
                 (GivenMemory){.body = "alpha beta gamma delta epsilon zeta eta theta iota kappa",
                               .key = "editor"});
    assert_bodies(s, "  Editor \t",
                  "alpha beta gamma delta epsilon zeta eta theta iota kappa|editor editor editor");
    store_close(s);
    free(db);
}

/* E2.4 (1)-(2): a key that misses q does not outrank a keyless row with a better bm25. */
TEST(store_search_ranks_a_keyless_row_by_bm25)
{
    char *db = NULL;
    Store *s = given_store(&db);

    given_memory(s, (GivenMemory){.body = "zed one two three four five", .key = "other"});
    given_memory(s, (GivenMemory){.body = "zed"});
    assert_bodies(s, "zed", "zed|zed one two three four five");
    store_close(s);
    free(db);
}

/* E2.4 (3): bm25 weights key 10, body 1, tags 4. Default weights give the
   reverse body, tags, key; each single-weight change reorders these three. */
TEST(store_search_ranks_by_weighted_bm25)
{
    static const char *const tags[] = {"a", "b", "c", "zed"};
    char *db = NULL;
    Store *s = given_store(&db);

    given_memory(s, (GivenMemory){.body = "filler words here", .key = "a:b:c:d:zed"});
    given_memory(s, (GivenMemory){.body = "zed one two"});
    given_memory(s, (GivenMemory){.body = "more filler words", .tags = tags, .ntags = 4U});
    assert_bodies(s, "zed", "filler words here|more filler words|zed one two");
    store_close(s);
    free(db);
}

/* E2.1a / E3.2: SQLITE_ERROR whose message is not a D1 prefix is a real
   error, not a warning. The injected step leaves "not an error" as the message. */
TEST(store_search_probe_error_without_a_d1_message_is_real)
{
    char *db = NULL;
    Store *s = given_examples(&db);
    SearchResult res;

    store_test_fail_step_after(0);
    res = when_search(s, "boo");
    ASSERT_EQ_STATUS(res.page.st, STORE_ERR_SQLITE);
    ASSERT_TRUE(res.warning == NULL);
    store_close(s);
    free(db);
}

/* E3.2: the probe's prepare failing is a real error. */
TEST(store_search_probe_prepare_failure_is_real)
{
    char *db = NULL;
    Store *s = given_examples(&db);
    SearchResult res;

    store_test_fail_prepare_after(0);
    res = when_search(s, "boo");
    ASSERT_EQ_STATUS(res.page.st, STORE_ERR_SQLITE);
    ASSERT_TRUE(res.warning == NULL);
    store_close(s);
    free(db);
}

/* The prefix part running out of memory is OOM, not an internal error. */
TEST(store_search_token_oom_is_oom)
{
    char *db = NULL;
    Store *s = given_examples(&db);
    SearchResult res;

    unicode_test_inject_nfc(UNICODE_TEST_INJECT_NOMEM);
    res = when_search(s, "boo");
    ASSERT_EQ_STATUS(res.page.st, STORE_ERR_OOM);
    ASSERT_TRUE(res.warning == NULL);
    store_close(s);
    free(db);
}

/* D27 is the CLI's check; invalid UTF-8 reaching the store is a contract violation. */
TEST(store_search_invalid_utf8_is_internal)
{
    char *db = NULL;
    Store *s = given_examples(&db);
    SearchResult res = when_search(s, "boo\xff");

    ASSERT_EQ_STATUS(res.page.st, STORE_ERR_INTERNAL);
    ASSERT_TRUE(res.warning == NULL);
    store_close(s);
    free(db);
}

/* E2.1b: --offset and --limit page the de-duplicated set. */
TEST(store_search_pages_the_deduplicated_set)
{
    char *db = NULL;
    Store *s = given_examples(&db);
    SearchQuery sq;
    SearchResult res;

    memset(&sq, 0, sizeof(sq));
    sq.query = "book";
    sq.filters.limit = 1U;
    sq.filters.offset = 1U;
    res = store_search(s, &sq, k_now);
    ASSERT_EQ_STATUS(res.page.st, STORE_OK);
    ASSERT_EQ_INT((int)res.page.total, 2);
    ASSERT_EQ_INT((int)res.page.count, 1);
    if (res.page.count == 1U) {
        ASSERT_STREQ(res.page.entries[0].body, "Book the dentist");
    }
    free_page(res.page);
    store_close(s);
    free(db);
}

/* E1 (plan 15 kept): a key segment is searchable. */
TEST(store_search_finds_a_key_segment)
{
    char *db = NULL;
    Store *s = given_examples(&db);

    assert_bodies(s, "editor", "Use Zed");
    store_close(s);
    free(db);
}

/* E10 verification: an unvocalized prefix finds a body with niqqud. */
TEST(store_search_hebrew_prefix_finds_niqqud)
{
    char *db = NULL;
    Store *s = given_store(&db);

    given_memory(s, (GivenMemory){.body = "\xd7\xa1\xd6\xb5\xd7\xa4\xd6\xb6\xd7\xa8"});
    assert_bodies(s, "\xd7\xa1\xd7\xa4", "\xd7\xa1\xd6\xb5\xd7\xa4\xd6\xb6\xd7\xa8");
    store_close(s);
    free(db);
}

/* E10.1: R is not folded, yet a vocalized query finds the row through P. */
TEST(store_search_vocalized_query_matches_through_p)
{
    char *db = NULL;
    Store *s = given_store(&db);

    given_memory(s, (GivenMemory){.body = "\xd7\xa1\xd6\xb5\xd7\xa4\xd6\xb6\xd7\xa8"});
    assert_bodies(s, "\xd7\xa1\xd6\xb5\xd7\xa4\xd6\xb6\xd7\xa8",
                  "\xd7\xa1\xd6\xb5\xd7\xa4\xd6\xb6\xd7\xa8");
    store_close(s);
    free(db);
}

/* Contract: NULL store, query, query text, or now is an internal error. */
TEST(store_search_null_arguments_are_internal)
{
    char *db = NULL;
    Store *s = given_examples(&db);
    SearchQuery sq;

    memset(&sq, 0, sizeof(sq));
    ASSERT_EQ_STATUS(store_search(NULL, &sq, k_now).page.st, STORE_ERR_INTERNAL);
    ASSERT_EQ_STATUS(store_search(s, NULL, k_now).page.st, STORE_ERR_INTERNAL);
    ASSERT_EQ_STATUS(store_search(s, &sq, k_now).page.st, STORE_ERR_INTERNAL);
    sq.query = "boo";
    ASSERT_EQ_STATUS(store_search(s, &sq, NULL).page.st, STORE_ERR_INTERNAL);
    store_close(s);
    free(db);
}

/* A failed allocation of R, q, or the MATCH text is OOM. */
TEST(store_search_alloc_failure_is_oom)
{
    char *db = NULL;
    Store *s = given_examples(&db);
    SearchResult res;

    store_test_fail_alloc_after(0);
    res = when_search(s, "boo");
    ASSERT_EQ_STATUS(res.page.st, STORE_ERR_OOM);
    store_close(s);
    free(db);
}

/* The read transaction failing to begin is a database error. */
TEST(store_search_begin_failure_is_sqlite)
{
    char *db = NULL;
    Store *s = given_examples(&db);
    SearchResult res;

    store_test_fail_exec_after(0);
    res = when_search(s, "boo");
    ASSERT_EQ_STATUS(res.page.st, STORE_ERR_SQLITE);
    store_close(s);
    free(db);
}

/* A failed COMMIT is a database error and drops the page and the warning. */
TEST(store_search_commit_failure_drops_page_and_warning)
{
    char *db = NULL;
    Store *s = given_examples(&db);
    SearchResult res;

    store_test_fail_exec_after(1);
    res = when_search(s, "recs:bo");
    ASSERT_EQ_STATUS(res.page.st, STORE_ERR_SQLITE);
    ASSERT_TRUE(res.page.entries == NULL);
    ASSERT_TRUE(res.warning == NULL);
    store_close(s);
    free(db);
}

/* Copying the FTS5 message running out of memory is OOM, not a silent parse. */
TEST(store_search_warning_copy_oom_is_oom)
{
    char *db = NULL;
    Store *s = given_examples(&db);
    SearchResult res;

    store_test_fail_alloc_after(3); /* R, q, the MATCH text, then the message */
    res = when_search(s, "recs:bo");
    ASSERT_EQ_STATUS(res.page.st, STORE_ERR_OOM);
    ASSERT_TRUE(res.warning == NULL);
    store_close(s);
    free(db);
}

void register_store_search_tests(void)
{
    RUN_TEST(store_search_matches_a_word_prefix);
    RUN_TEST(store_search_runs_p_alone_when_r_does_not_parse);
    RUN_TEST(store_search_warns_with_the_fts_message);
    RUN_TEST(store_search_without_p_and_unparseable_r_is_empty);
    RUN_TEST(store_search_blank_query_is_empty_without_warning);
    RUN_TEST(store_search_unions_the_raw_part);
    RUN_TEST(store_search_counts_a_row_both_parts_match_once);
    RUN_TEST(store_search_runs_r_alone_when_p_is_empty);
    RUN_TEST(store_search_ranks_exact_key_then_key_prefix_first);
    RUN_TEST(store_search_trims_the_query_before_ranking);
    RUN_TEST(store_search_ranks_a_keyless_row_by_bm25);
    RUN_TEST(store_search_ranks_by_weighted_bm25);
    RUN_TEST(store_search_probe_error_without_a_d1_message_is_real);
    RUN_TEST(store_search_probe_prepare_failure_is_real);
    RUN_TEST(store_search_token_oom_is_oom);
    RUN_TEST(store_search_invalid_utf8_is_internal);
    RUN_TEST(store_search_pages_the_deduplicated_set);
    RUN_TEST(store_search_finds_a_key_segment);
    RUN_TEST(store_search_hebrew_prefix_finds_niqqud);
    RUN_TEST(store_search_vocalized_query_matches_through_p);
    RUN_TEST(store_search_null_arguments_are_internal);
    RUN_TEST(store_search_alloc_failure_is_oom);
    RUN_TEST(store_search_begin_failure_is_sqlite);
    RUN_TEST(store_search_commit_failure_drops_page_and_warning);
    RUN_TEST(store_search_warning_copy_oom_is_oom);
}
