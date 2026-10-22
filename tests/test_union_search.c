#include "harness.h"
#include "register.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

/*
 * remember search, union of the prefix part and the raw part (plan 18,
 * 006 E2). Fixture = the 006 Examples.
 */

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

enum { LONG_QUERY = 10000, NESTED_PARENS = 2000 };

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

void register_union_search_tests(void)
{
    RUN_TEST(union_search_unparseable_raw_part_is_a_json_warning);
    RUN_TEST(union_search_no_input_fails);
    RUN_TEST(union_search_long_query_succeeds);
    RUN_TEST(union_search_deep_nesting_is_a_warning);
    RUN_TEST(union_search_invalid_utf8_is_usage);
    RUN_TEST(union_search_parsed_raw_part_has_no_warning);
}
