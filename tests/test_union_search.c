#include "harness.h"
#include "register.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * remember search, union of the prefix part and the raw part (plan 18,
 * 006 E2). Fixture = the 006 Examples.
 */

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

enum { LONG_QUERY = 10000, NESTED_PARENS = 2000, MANY_TAGS = 60, TAG_LEN = 4 };

static void given_add(const char *db, const char *const *args, size_t nargs)
{
    CmdResult r = run_remember(db, args, nargs, NULL);

    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
}

static char *given_examples(void)
{
    static const char *const books[] = {"add",        "--key", "recs:books",
                                        "--tag",      "recs",  "--tag",
                                        "type:books", "--",    "# Book recommendations"};
    static const char *const dentist[] = {"add", "--tag", "family", "Book the dentist"};
    static const char *const zed[] = {"add", "--key", "pref:editor", "Use Zed"};
    char *db = make_temp_db_path();

    ASSERT_TRUE(db != NULL);
    if (db != NULL) {
        given_add(db, books, COUNT_OF(books));
        given_add(db, dentist, COUNT_OF(dentist));
        given_add(db, zed, COUNT_OF(zed));
    }
    return db;
}

/* Example 5: an unparseable raw part is a JSON warning on exit 0. */
TEST(union_search_unparseable_raw_part_is_a_json_warning)
{
    char *db = given_examples();
    const char *const args[] = {"--json", "--json-errors", "search", "foo AND"};
    CmdResult r = run_remember(db, args, COUNT_OF(args), NULL);

    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "{\"version\":1,\"warning\":{\"code\":\"raw_query_ignored\",\"message\":"
                        "\"fts5: syntax error near \\\"\\\"\"}}\n");
    cmd_result_free(&r);
    free(db);
}

/* E2 verification / E3.1 / E3.3: no search input fails, and none prints
   the retired "invalid search query". "--" passes -x as text
   (decision:plan18-dash-query-needs-double-dash). */
TEST(union_search_no_input_fails)
{
    static const char *const inputs[] = {
        "C++",
        "\"",
        "recs:books",
        "key:recs",
        "foo AND",
        "a AND",
        "NEAR(",
        "*",
        "-x",
        "(",
        "\xd7\xa1\xd7\xa4\xd7\xa8", /* Hebrew sefer */
        "\xf0\x9f\x93\x9a",         /* U+1F4DA books emoji */
    };
    char *db = given_examples();
    size_t i = 0;

    for (i = 0; i < COUNT_OF(inputs); i++) {
        const char *const args[] = {"search", "--", inputs[i]};
        CmdResult r = run_remember(db, args, COUNT_OF(args), NULL);

        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STR_NOT_CONTAINS(r.err, "invalid search query");
        cmd_result_free(&r);
    }
    free(db);
}

/* E2 verification: a 10,000-character query exits 0. */
TEST(union_search_long_query_succeeds)
{
    char *db = given_examples();
    char *query = (char *)malloc(LONG_QUERY + 1);
    CmdResult r;

    ASSERT_TRUE(query != NULL);
    if (query == NULL) {
        free(db);
        return;
    }
    memset(query, 'a', LONG_QUERY);
    query[LONG_QUERY] = '\0';
    {
        const char *const args[] = {"search", query};
        r = run_remember(db, args, COUNT_OF(args), NULL);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    free(query);
    free(db);
}

/* D1: FTS5's parser stack overflow is "does not parse", so a warning on exit 0. */
TEST(union_search_deep_nesting_is_a_warning)
{
    char *db = given_examples();
    char query[NESTED_PARENS + 1];
    CmdResult r;

    memset(query, '(', NESTED_PARENS);
    query[NESTED_PARENS] = '\0';
    {
        const char *const args[] = {"--json-errors", "search", query};
        r = run_remember(db, args, COUNT_OF(args), NULL);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "{\"version\":1,\"warning\":{\"code\":\"raw_query_ignored\",\"message\":"
                        "\"fts5: parser stack overflow\"}}\n");
    cmd_result_free(&r);
    free(db);
}

/* D27: invalid UTF-8 in the query is a usage error. */
TEST(union_search_invalid_utf8_is_usage)
{
    char *db = given_examples();
    const char *const args[] = {"--json-errors", "search", "boo\xff"};
    CmdResult r = run_remember(db, args, COUNT_OF(args), NULL);

    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, "{\"version\":1,\"error\":{\"code\":\"usage\",\"message\":"
                        "\"invalid UTF-8 in search query\"}}\n");
    cmd_result_free(&r);
    free(db);
}

/* Example 3: a raw part that parses gives no warning. */
TEST(union_search_parsed_raw_part_has_no_warning)
{
    char *db = given_examples();
    const char *const args[] = {"--json", "--json-errors", "search", "book OR zed"};
    CmdResult r = run_remember(db, args, COUNT_OF(args), NULL);

    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "\"total\":3");
    cmd_result_free(&r);
    free(db);
}

/* E2.1: a --facet-limit outside 1..200 is a usage error. */
TEST(union_search_facet_limit_out_of_range_is_usage)
{
    static const char *const values[] = {"0",  "201", "x", "-1", "", "18446744073709551616",
                                         " 5", "+5"};
    char *db = given_examples();
    size_t i = 0;

    for (i = 0; i < COUNT_OF(values); i++) {
        const char *const args[] = {"--json-errors", "search", "--facet-limit", values[i], "boo"};
        CmdResult r = run_remember(db, args, COUNT_OF(args), NULL);

        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STREQ(r.out, "");
        ASSERT_STREQ(r.err, "{\"version\":1,\"error\":{\"code\":\"usage\",\"message\":"
                            "\"invalid --facet-limit (must be 1..200)\"}}\n");
        cmd_result_free(&r);
    }
    free(db);
}

/* Example 1 / E2.6: search --json writes keys and tags between total and entries. */
TEST(union_search_json_carries_the_facets_before_entries)
{
    char *db = given_examples();
    const char *const args[] = {"--json", "search", "boo"};
    CmdResult r = run_remember(db, args, COUNT_OF(args), NULL);

    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"count\":2,\"total\":2,\"keys\":{\"total\":1,\"items\":[{"
                               "\"key\":\"recs:books\",\"sync_id\":\"");
    ASSERT_STR_CONTAINS(r.out, "\"label\":\"Book recommendations\"");
    ASSERT_STR_CONTAINS(r.out, "\"tags\":{\"total\":1,\"items\":[{\"tag\":\"type:books\","
                               "\"count\":1}]},\"entries\":[{");
    cmd_result_free(&r);
    free(db);
}

/* E2.7: the facet lines come before the entry lines. */
TEST(union_search_human_prints_the_facets_before_entries)
{
    char *db = given_examples();
    const char *const args[] = {"search", "boo"};
    CmdResult r = run_remember(db, args, COUNT_OF(args), NULL);

    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "keys: recs:books\ntags: type:books (1)\n1 | recs:books | ");
    cmd_result_free(&r);
    free(db);
}

/* One memory "tagged" with tags t00..t59, each a tier 1 match for "t". */
static char *given_many_tags(void)
{
    char names[MANY_TAGS][TAG_LEN];
    const char *args[2 + (2 * MANY_TAGS) + 1];
    size_t n = 0;
    size_t i = 0;
    char *db = make_temp_db_path();

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return NULL;
    }
    args[n++] = "add";
    for (i = 0; i < MANY_TAGS; i++) {
        (void)snprintf(names[i], sizeof names[i], "t%02zu", i);
        args[n++] = "--tag";
        args[n++] = names[i];
    }
    args[n++] = "tagged";
    given_add(db, args, n);
    return db;
}

/* Tag items in a --json search, counted by their "tag" member. */
static size_t tag_items_in(const char *out)
{
    size_t n = 0;
    const char *p = out;

    while ((p = strstr(p, "{\"tag\":\"")) != NULL) {
        n++;
        p++;
    }
    return n;
}

/* E2.1: without --facet-limit the store returns 50 of the 60 tag items. */
TEST(union_search_facet_limit_defaults_to_50)
{
    char *db = given_many_tags();
    const char *const args[] = {"--json", "search", "t"};
    CmdResult r = run_remember(db, args, COUNT_OF(args), NULL);

    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"tags\":{\"total\":60,");
    ASSERT_EQ_INT((int)tag_items_in(r.out), 50);
    cmd_result_free(&r);
    free(db);
}

/* E2.1: both ends of 1..200 reach the store as given. */
TEST(union_search_facet_limit_accepts_both_bounds)
{
    static const struct {
        const char *limit;
        size_t items;
    } cases[] = {{"1", 1}, {"200", MANY_TAGS}};
    char *db = given_many_tags();
    size_t i = 0;

    for (i = 0; i < COUNT_OF(cases); i++) {
        const char *const args[] = {"--json", "search", "--facet-limit", cases[i].limit, "t"};
        CmdResult r = run_remember(db, args, COUNT_OF(args), NULL);

        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_EQ_INT((int)tag_items_in(r.out), (int)cases[i].items);
        cmd_result_free(&r);
    }
    free(db);
}

/* Example 2 / E2.3: the facets print and the plain warning names what was ignored. */
TEST(union_search_example_2_has_facets_and_the_plain_warning)
{
    char *db = given_examples();
    const char *const args[] = {"--json", "search", "recs:bo"};
    CmdResult r = run_remember(db, args, COUNT_OF(args), NULL);

    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"count\":1,\"total\":1,\"keys\":{\"total\":1,\"items\":[{"
                               "\"key\":\"recs:books\",");
    ASSERT_STR_CONTAINS(r.out, "\"tags\":{\"total\":0,\"items\":[]},\"entries\":[{");
    ASSERT_STREQ(r.err, "remember: warning: raw query ignored: no such column: recs\n");
    cmd_result_free(&r);
    free(db);
}

/* Example 4 / E2.5d: a filter scopes the facets, not only the entries. */
TEST(union_search_example_4_filter_scopes_the_facets)
{
    char *db = given_examples();
    const char *const args[] = {"--json", "search", "boo", "--tag", "family"};
    CmdResult r = run_remember(db, args, COUNT_OF(args), NULL);

    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"count\":1,\"total\":1,\"keys\":{\"total\":0,\"items\":[]},"
                               "\"tags\":{\"total\":0,\"items\":[]},\"entries\":[{");
    ASSERT_STR_CONTAINS(r.out, "Book the dentist");
    cmd_result_free(&r);
    free(db);
}

TEST(union_search_facet_limit_without_a_value_is_usage)
{
    char *db = given_examples();
    const char *const args[] = {"search", "boo", "--facet-limit"};
    CmdResult r = run_remember(db, args, COUNT_OF(args), NULL);

    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "remember: missing value for --facet-limit\n");
    cmd_result_free(&r);
    free(db);
}

void register_union_search_tests(void)
{
    RUN_TEST(union_search_unparseable_raw_part_is_a_json_warning);
    RUN_TEST(union_search_no_input_fails);
    RUN_TEST(union_search_long_query_succeeds);
    RUN_TEST(union_search_deep_nesting_is_a_warning);
    RUN_TEST(union_search_invalid_utf8_is_usage);
    RUN_TEST(union_search_parsed_raw_part_has_no_warning);
    RUN_TEST(union_search_facet_limit_out_of_range_is_usage);
    RUN_TEST(union_search_json_carries_the_facets_before_entries);
    RUN_TEST(union_search_human_prints_the_facets_before_entries);
    RUN_TEST(union_search_facet_limit_defaults_to_50);
    RUN_TEST(union_search_facet_limit_accepts_both_bounds);
    RUN_TEST(union_search_example_2_has_facets_and_the_plain_warning);
    RUN_TEST(union_search_example_4_filter_scopes_the_facets);
    RUN_TEST(union_search_facet_limit_without_a_value_is_usage);
}
