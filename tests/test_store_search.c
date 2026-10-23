#include "harness.h"
#include "normalize.h"
#include "register.h"
#include "store.h"
#include "test.h"
#include "test_alloc.h"
#include "unicode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * store_search union (plan 18 stage 3): P prefix part, E2.1a parse probe,
 * (P) OR (R), de-duplication, E2.4 ranking. Fixture = 006 Examples.
 */

/* GROW_PAST: more tag candidates than the store's first array capacity (8). */
enum { PAGE_LIMIT = 20, FACET_LIMIT = 50, BODIES_CAP = 512, GROW_PAST = 9 };

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

/* A search with the default facet limit and no filters. */
static SearchQuery given_facet_query(const char *query)
{
    SearchQuery sq;

    memset(&sq, 0, sizeof(sq));
    sq.query = query;
    sq.filters.limit = PAGE_LIMIT;
    sq.facet_limit = FACET_LIMIT;
    return sq;
}

static SearchResult when_search(Store *s, const char *query)
{
    SearchQuery sq = given_facet_query(query);

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
    store_search_result_free(&res);
}

/* store_search(sq) succeeds with keys facet items want ("a|b"), in order, and
   no item past the limit. */
static void assert_keys_for(Store *s, const SearchQuery *sq, const char *want)
{
    SearchResult res = store_search(s, sq, k_now);
    char got[BODIES_CAP];
    size_t i = 0;

    ASSERT_EQ_STATUS(res.page.st, STORE_OK);
    got[0] = '\0';
    for (i = 0; i < res.keys.count; i++) {
        (void)strncat(got, i > 0 ? "|" : "", sizeof(got) - strlen(got) - 1U);
        (void)strncat(got, res.keys.items[i].key, sizeof(got) - strlen(got) - 1U);
    }
    ASSERT_STREQ(got, want);
    ASSERT_EQ_INT((int)res.keys.total, (int)res.keys.count);
    store_search_result_free(&res);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) input, then expected
static void assert_keys(Store *s, const char *query, const char *want)
{
    SearchQuery sq = given_facet_query(query);

    assert_keys_for(s, &sq, want);
}

/* store_search(sq) succeeds with tags facet items want ("a (1)|b (2)"), in
   order, and no item past the limit. */
static void assert_tags_for(Store *s, const SearchQuery *sq, const char *want)
{
    SearchResult res = store_search(s, sq, k_now);
    char got[BODIES_CAP];
    char item[BODIES_CAP];
    size_t i = 0;

    ASSERT_EQ_STATUS(res.page.st, STORE_OK);
    got[0] = '\0';
    for (i = 0; i < res.tags.count; i++) {
        (void)snprintf(item, sizeof(item), "%s%s (%zu)", i > 0 ? "|" : "", res.tags.items[i].tag,
                       res.tags.items[i].count);
        (void)strncat(got, item, sizeof(got) - strlen(got) - 1U);
    }
    ASSERT_STREQ(got, want);
    ASSERT_EQ_INT((int)res.tags.total, (int)res.tags.count);
    store_search_result_free(&res);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) input, then expected
static void assert_tags(Store *s, const char *query, const char *want)
{
    SearchQuery sq = given_facet_query(query);

    assert_tags_for(s, &sq, want);
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
    store_search_result_free(&res);
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
    store_search_result_free(&res);
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
    store_search_result_free(&res);
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
    store_search_result_free(&res);
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

/* E2.5b / Example 1: a key the query text-matches is a keys facet item. */
TEST(store_search_facets_a_matching_key)
{
    char *db = NULL;
    Store *s = given_examples(&db);

    assert_keys(s, "boo", "recs:books");
    store_close(s);
    free(db);
}

/* Example 2: recs:bo is a tier 1 key; no tag holds both tokens. */
TEST(store_search_facets_example_2)
{
    char *db = NULL;
    Store *s = given_examples(&db);

    assert_keys(s, "recs:bo", "recs:books");
    assert_tags(s, "recs:bo", "");
    store_close(s);
    free(db);
}

/* E2.6: keys sort by tier, then by code point inside a tier (the store sorts;
   search_terms pins each rule). SQLite alone returns them in key order. */
TEST(store_search_orders_keys_by_tier_then_code_point)
{
    char *db = NULL;
    Store *s = given_store(&db);

    given_memory(s, (GivenMemory){.body = "one", .key = "\xc3\xa9:b"});
    given_memory(s, (GivenMemory){.body = "two", .key = "zeta:b"});
    given_memory(s, (GivenMemory){.body = "three", .key = "b:z"});
    given_memory(s, (GivenMemory){.body = "four", .key = "a:b"});
    assert_keys(s, "b", "b:z|a:b|zeta:b|\xc3\xa9:b");
    store_close(s);
    free(db);
}

/* E2.6: items holds at most facet_limit keys, the first in order; total
   counts every scoped match. */
TEST(store_search_caps_key_items_and_counts_every_match)
{
    char *db = NULL;
    Store *s = given_store(&db);
    SearchQuery sq = given_facet_query("b");
    SearchResult res;

    given_memory(s, (GivenMemory){.body = "one", .key = "a:b"});
    given_memory(s, (GivenMemory){.body = "two", .key = "b:z"});
    sq.facet_limit = 1U;
    res = store_search(s, &sq, k_now);
    ASSERT_EQ_STATUS(res.page.st, STORE_OK);
    ASSERT_EQ_INT((int)res.keys.total, 2);
    ASSERT_EQ_INT((int)res.keys.count, 1);
    if (res.keys.count == 1U) {
        ASSERT_STREQ(res.keys.items[0].key, "b:z");
    }
    store_search_result_free(&res);
    store_close(s);
    free(db);
}

/* store_search(query) succeeds with one keys item whose label is want. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) input, then expected
static void assert_key_label(Store *s, const char *query, const char *want)
{
    SearchResult res = when_search(s, query);

    ASSERT_EQ_STATUS(res.page.st, STORE_OK);
    ASSERT_EQ_INT((int)res.keys.count, 1);
    if (res.keys.count == 1U) {
        ASSERT_STREQ(res.keys.items[0].label, want);
    }
    store_search_result_free(&res);
}

/* E2.6 / Example 1: a key's label is its memory's "# " title. */
TEST(store_search_labels_a_key_with_its_title)
{
    char *db = NULL;
    Store *s = given_examples(&db);

    assert_key_label(s, "boo", "Book recommendations");
    store_close(s);
    free(db);
}

/* E2.6: without a title the label is the key, not the body's plain text. */
TEST(store_search_labels_a_key_without_a_title_with_the_key)
{
    char *db = NULL;
    Store *s = given_examples(&db);

    assert_key_label(s, "edit", "pref:editor");
    store_close(s);
    free(db);
}

/* E2.6: a keys item carries its memory's sync_id, source and updated_at
   (re-added later, so updated_at is not created_at). */
TEST(store_search_key_item_carries_its_memory_fields)
{
    char *db = NULL;
    Store *s = given_store(&db);
    SearchResult res;

    given_memory(s, (GivenMemory){
                        .body = "old", .key = "recs:books", .created = "2026-01-01T00:00:00.000Z"});
    given_memory(s, (GivenMemory){.body = "new", .key = "recs:books"});
    res = when_search(s, "recs");
    ASSERT_EQ_STATUS(res.page.st, STORE_OK);
    ASSERT_EQ_INT((int)res.keys.count, 1);
    ASSERT_EQ_INT((int)res.page.count, 1);
    if (res.keys.count == 1U && res.page.count == 1U) {
        ASSERT_STREQ(res.keys.items[0].sync_id, res.page.entries[0].sync_id);
        ASSERT_STREQ(res.keys.items[0].source, "human");
        ASSERT_STREQ(res.keys.items[0].updated_at, k_now);
    }
    store_search_result_free(&res);
    store_close(s);
    free(db);
}

/* E2.5d / Example 4: filters scope the facets; recs:books has no family tag,
   and the in-scope tag family does not match. */
TEST(store_search_filters_scope_the_facets)
{
    static const char *const family[] = {"family"};
    char *db = NULL;
    Store *s = given_examples(&db);
    SearchQuery sq = given_facet_query("boo");

    sq.filters.tags = family;
    sq.filters.ntags = 1U;
    assert_keys_for(s, &sq, "");
    assert_tags_for(s, &sq, "");
    store_close(s);
    free(db);
}

/* E2.5d: the bin scopes the facets (handover probe 4). */
TEST(store_search_bin_scopes_the_keys)
{
    char *db = NULL;
    Store *s = given_examples(&db);
    SearchQuery sq = given_facet_query("boo");
    Entry gone;

    memset(&gone, 0, sizeof(gone));
    ASSERT_EQ_STATUS(store_soft_delete_by_key(s, "recs:books", k_now, &gone), STORE_OK);
    store_entry_free(&gone);
    assert_keys_for(s, &sq, "");
    sq.filters.bin = STORE_BIN_DELETED;
    assert_keys_for(s, &sq, "recs:books");
    store_close(s);
    free(db);
}

/* E5.3: since/until scope the facets on updated_at, as for the entries. */
TEST(store_search_since_scopes_the_keys_on_updated_at)
{
    char *db = NULL;
    Store *s = given_store(&db);
    SearchQuery sq = given_facet_query("recs");

    given_memory(s, (GivenMemory){
                        .body = "old", .key = "recs:books", .created = "2026-01-01T00:00:00.000Z"});
    given_memory(s, (GivenMemory){.body = "new", .key = "recs:books"});
    given_memory(
        s, (GivenMemory){.body = "x", .key = "recs:games", .created = "2026-01-01T00:00:00.000Z"});
    sq.filters.since = "2026-06-01T00:00:00.000Z";
    assert_keys_for(s, &sq, "recs:books");
    store_close(s);
    free(db);
}

/* A facet statement failing is a database error and drops the page already
   built (prepares: probe, COUNT, SELECT, the two rows' tags, keys). */
TEST(store_search_facet_failure_drops_the_page)
{
    enum { KEYS_PREPARE = 5 };
    char *db = NULL;
    Store *s = given_examples(&db);
    SearchResult res;

    store_test_fail_prepare_after(KEYS_PREPARE);
    res = when_search(s, "boo");
    ASSERT_EQ_STATUS(res.page.st, STORE_ERR_SQLITE);
    ASSERT_TRUE(res.page.entries == NULL);
    ASSERT_EQ_INT((int)res.page.count, 0);
    store_close(s);
    free(db);
}

/* Fail each call that fail_after counts in turn (0, 1, ...) until the search
   runs clean; every failed run must be want. Returns the clean result. */
static SearchResult when_each_failure_until_clean(Store *s, const char *query,
                                                  void (*fail_after)(int), StoreStatus want)
{
    enum { SWEEP_MAX = 400 };
    SearchResult res = {.page = {.st = STORE_ERR_INTERNAL}};
    int n = 0;

    for (n = 0; n < SWEEP_MAX; n++) {
        fail_after(n);
        res = when_search(s, query);
        if (res.page.st == STORE_OK) {
            break;
        }
        ASSERT_EQ_STATUS(res.page.st, want);
    }
    fail_after(-1);
    ASSERT_TRUE(n > 0 && n < SWEEP_MAX);
    return res;
}

/* A failed allocation anywhere in search_terms is OOM, never a result with a
   facet item or its label missing. */
TEST(store_search_term_alloc_failure_is_oom_not_a_missing_key)
{
    char *db = NULL;
    Store *s = given_examples(&db);
    SearchResult res =
        when_each_failure_until_clean(s, "boo", test_alloc_fail_after, STORE_ERR_OOM);

    ASSERT_EQ_INT((int)res.tags.count, 1);
    ASSERT_EQ_INT((int)res.keys.count, 1);
    if (res.keys.count == 1U) {
        ASSERT_STREQ(res.keys.items[0].label, "Book recommendations");
    }
    store_search_result_free(&res);
    store_close(s);
    free(db);
}

/* decision:plan18-facet-invalid-utf8-no-match: a stored key that is not valid
   UTF-8 (a hand-edited store) is a non-match; the search still succeeds. */
TEST(store_search_skips_a_stored_key_with_invalid_utf8)
{
    char *db = NULL;
    Store *s = given_store(&db);

    given_memory(s, (GivenMemory){.body = "one", .key = "b\xff:x"});
    given_memory(s, (GivenMemory){.body = "two", .key = "b:x"});
    assert_keys(s, "x b", "b:x");
    store_close(s);
    free(db);
}

/* E2.5b / Example 1: a tag the query text-matches is a tags facet item. */
TEST(store_search_facets_a_matching_tag)
{
    char *db = NULL;
    Store *s = given_examples(&db);

    assert_tags(s, "boo", "type:books (1)");
    store_close(s);
    free(db);
}

/* E2.6: a tag's count is the number of memories in scope that carry it. */
TEST(store_search_counts_the_memories_that_carry_a_tag)
{
    static const char *const books[] = {"type:books"};
    static const char *const family_books[] = {"family", "type:books"};
    char *db = NULL;
    Store *s = given_store(&db);

    given_memory(s, (GivenMemory){.body = "one", .tags = books, .ntags = 1U});
    given_memory(s, (GivenMemory){.body = "two", .tags = family_books, .ntags = 2U});
    assert_tags(s, "type", "type:books (2)");
    store_close(s);
    free(db);
}

/* E2.5d: filters scope the tags and their counts. */
TEST(store_search_filters_scope_the_tag_counts)
{
    static const char *const books[] = {"type:books"};
    static const char *const family_books[] = {"family", "type:books"};
    static const char *const family[] = {"family"};
    char *db = NULL;
    Store *s = given_store(&db);
    SearchQuery sq = given_facet_query("type");

    given_memory(s, (GivenMemory){.body = "one", .tags = books, .ntags = 1U});
    given_memory(s, (GivenMemory){.body = "two", .tags = family_books, .ntags = 2U});
    sq.filters.tags = family;
    sq.filters.ntags = 1U;
    assert_tags_for(s, &sq, "type:books (1)");
    store_close(s);
    free(db);
}

/* D28: tags sort exact, tier, count DESC, then code point (the store sorts;
   search_terms pins each rule). SQLite alone returns them by name. */
TEST(store_search_orders_tags_exact_tier_then_count)
{
    static const char *const first[] = {"b", "b:z", "c:b"};
    static const char *const second[] = {"b:z", "c:b"};
    static const char *const third[] = {"a:b"};
    char *db = NULL;
    Store *s = given_store(&db);

    given_memory(s, (GivenMemory){.body = "one", .tags = first, .ntags = 3U});
    given_memory(s, (GivenMemory){.body = "two", .tags = second, .ntags = 2U});
    given_memory(s, (GivenMemory){.body = "three", .tags = third, .ntags = 1U});
    assert_tags(s, "b", "b (1)|b:z (2)|c:b (2)|a:b (1)");
    store_close(s);
    free(db);
}

/* One memory that carries tags t00..t(n-1). */
static void given_numbered_tags(Store *s, int n)
{
    enum { TAGS_MAX = 60, TAG_LEN = 4 };
    char names[TAGS_MAX][TAG_LEN];
    const char *tags[TAGS_MAX];
    size_t i = 0;

    for (i = 0; i < (size_t)n && i < (size_t)TAGS_MAX; i++) {
        (void)snprintf(names[i], sizeof(names[i]), "t%02zu", i);
        tags[i] = names[i];
    }
    given_memory(s, (GivenMemory){.body = "tagged", .tags = tags, .ntags = i});
}

/* E2.1 / E2.6: 60 matching tags give total 60 and facet_limit items. */
TEST(store_search_caps_tag_items_at_the_facet_limit)
{
    enum { TAGS = 60, FACET_MAX = 200 };
    char *db = NULL;
    Store *s = given_store(&db);
    SearchQuery sq = given_facet_query("t");
    SearchResult res;

    given_numbered_tags(s, TAGS);
    res = store_search(s, &sq, k_now);
    ASSERT_EQ_STATUS(res.page.st, STORE_OK);
    ASSERT_EQ_INT((int)res.tags.total, TAGS);
    ASSERT_EQ_INT((int)res.tags.count, FACET_LIMIT);
    store_search_result_free(&res);
    sq.facet_limit = FACET_MAX;
    res = store_search(s, &sq, k_now);
    ASSERT_EQ_INT((int)res.tags.count, TAGS);
    store_search_result_free(&res);
    store_close(s);
    free(db);
}

/* facet_limit 0 counts the matches and returns no items. */
TEST(store_search_facet_limit_zero_returns_totals_only)
{
    char *db = NULL;
    Store *s = given_examples(&db);
    SearchQuery sq = given_facet_query("boo");
    SearchResult res;

    sq.facet_limit = 0U;
    res = store_search(s, &sq, k_now);
    ASSERT_EQ_STATUS(res.page.st, STORE_OK);
    ASSERT_EQ_INT((int)res.keys.total, 1);
    ASSERT_EQ_INT((int)res.tags.total, 1);
    ASSERT_TRUE(res.keys.items == NULL && res.tags.items == NULL);
    store_search_result_free(&res);
    store_close(s);
    free(db);
}

/* decision:plan18-facets-independent: with P empty and R unparseable the
   entries are empty, yet a key q prefixes is still a facet item. */
TEST(store_search_facets_do_not_need_a_memory_match)
{
    char *db = NULL;
    Store *s = given_store(&db);

    given_memory(s, (GivenMemory){.body = "todo", .key = "(draft)"});
    assert_bodies(s, "(", "");
    assert_keys(s, "(", "(draft)");
    store_close(s);
    free(db);
}

/* E2.1 (4): a blank query has empty facets. */
TEST(store_search_blank_query_has_no_facets)
{
    char *db = NULL;
    Store *s = given_examples(&db);

    assert_keys(s, " \t ", "");
    assert_tags(s, " \t ", "");
    store_close(s);
    free(db);
}

/* E2.6 / handover probe 6: --offset and --limit page the entries only. */
TEST(store_search_paging_leaves_the_facets_whole)
{
    char *db = NULL;
    Store *s = given_examples(&db);
    SearchQuery sq = given_facet_query("boo");

    sq.filters.limit = 1U;
    sq.filters.offset = 1U;
    assert_keys_for(s, &sq, "recs:books");
    assert_tags_for(s, &sq, "type:books (1)");
    store_close(s);
    free(db);
}

/* With no page query, the facet statements come right after the probe. */
static Store *given_facets_only(char **db)
{
    Store *s = given_store(db);

    given_memory(s, (GivenMemory){.body = "todo", .key = "(draft)"});
    return s;
}

/* A facet statement failing is a database error and drops the warning. */
static void assert_facet_failure_is_sqlite(Store *s)
{
    SearchResult res = when_search(s, "(");

    ASSERT_EQ_STATUS(res.page.st, STORE_ERR_SQLITE);
    ASSERT_TRUE(res.warning == NULL && res.keys.items == NULL);
}

/* Prepares: probe, keys, tags. */
TEST(store_search_tags_prepare_failure_is_sqlite)
{
    char *db = NULL;
    Store *s = given_facets_only(&db);

    store_test_fail_prepare_after(2);
    assert_facet_failure_is_sqlite(s);
    store_close(s);
    free(db);
}

/* Steps: probe, then the keys query's first row. */
TEST(store_search_facet_step_failure_is_sqlite)
{
    char *db = NULL;
    Store *s = given_facets_only(&db);

    store_test_fail_step_after(1);
    assert_facet_failure_is_sqlite(s);
    store_close(s);
    free(db);
}

/* A failed store allocation while the facets are built (copies, growth, the
   item arrays) is OOM, never a result with an item missing. */
TEST(store_search_store_alloc_failure_is_oom_not_a_missing_facet)
{
    char *db = NULL;
    Store *s = given_examples(&db);
    SearchResult res;

    given_numbered_tags(s, GROW_PAST);
    given_memory(s, (GivenMemory){.body = "# Todo", .key = "todo"});
    res = when_each_failure_until_clean(s, "t", store_test_fail_alloc_after, STORE_ERR_OOM);
    ASSERT_EQ_INT((int)res.keys.count, 1);
    ASSERT_EQ_INT((int)res.tags.count, GROW_PAST + 1); /* t00..t08 and type:books */
    store_search_result_free(&res);
    store_close(s);
    free(db);
}

/* Contract: more filters than the facet query can bind is an internal error
   (the CLI caps --tag at 50). With no page query the facets meet it first. */
TEST(store_search_too_many_filters_for_the_facets_is_internal)
{
    enum { TOO_MANY = 64 };
    char *db = NULL;
    Store *s = given_facets_only(&db);
    SearchQuery sq = given_facet_query("(");
    const char *tags[TOO_MANY];
    size_t i = 0;

    for (i = 0; i < (size_t)TOO_MANY; i++) {
        tags[i] = "t";
    }
    sq.filters.tags = tags;
    sq.filters.ntags = TOO_MANY;
    ASSERT_EQ_STATUS(store_search(s, &sq, k_now).page.st, STORE_ERR_INTERNAL);
    store_close(s);
    free(db);
}

/* store_search_result_free is NULL-safe. */
TEST(store_search_result_free_accepts_null)
{
    store_search_result_free(NULL);
    ASSERT_TRUE(true);
}

/* A Unicode failure while a stored key is classified is internal, never OOM
   and never a missing item. */
TEST(store_search_unicode_failure_is_internal_not_a_missing_key)
{
    char *db = NULL;
    Store *s = given_examples(&db);
    SearchResult res =
        when_each_failure_until_clean(s, "boo", unicode_test_fail_after, STORE_ERR_INTERNAL);

    ASSERT_EQ_INT((int)res.keys.count, 1);
    store_search_result_free(&res);
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
    RUN_TEST(store_search_facets_a_matching_key);
    RUN_TEST(store_search_facets_example_2);
    RUN_TEST(store_search_orders_keys_by_tier_then_code_point);
    RUN_TEST(store_search_caps_key_items_and_counts_every_match);
    RUN_TEST(store_search_labels_a_key_with_its_title);
    RUN_TEST(store_search_labels_a_key_without_a_title_with_the_key);
    RUN_TEST(store_search_key_item_carries_its_memory_fields);
    RUN_TEST(store_search_filters_scope_the_facets);
    RUN_TEST(store_search_bin_scopes_the_keys);
    RUN_TEST(store_search_since_scopes_the_keys_on_updated_at);
    RUN_TEST(store_search_facet_failure_drops_the_page);
    RUN_TEST(store_search_term_alloc_failure_is_oom_not_a_missing_key);
    RUN_TEST(store_search_skips_a_stored_key_with_invalid_utf8);
    RUN_TEST(store_search_facets_a_matching_tag);
    RUN_TEST(store_search_counts_the_memories_that_carry_a_tag);
    RUN_TEST(store_search_filters_scope_the_tag_counts);
    RUN_TEST(store_search_orders_tags_exact_tier_then_count);
    RUN_TEST(store_search_caps_tag_items_at_the_facet_limit);
    RUN_TEST(store_search_facet_limit_zero_returns_totals_only);
    RUN_TEST(store_search_facets_do_not_need_a_memory_match);
    RUN_TEST(store_search_blank_query_has_no_facets);
    RUN_TEST(store_search_paging_leaves_the_facets_whole);
    RUN_TEST(store_search_tags_prepare_failure_is_sqlite);
    RUN_TEST(store_search_facet_step_failure_is_sqlite);
    RUN_TEST(store_search_store_alloc_failure_is_oom_not_a_missing_facet);
    RUN_TEST(store_search_too_many_filters_for_the_facets_is_internal);
    RUN_TEST(store_search_result_free_accepts_null);
    RUN_TEST(store_search_unicode_failure_is_internal_not_a_missing_key);
}
