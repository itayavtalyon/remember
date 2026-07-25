#include "harness.h"
#include "register.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

TEST(add_basic_prints_id_one)
{
    char *db = make_temp_db_path();
    const char *args[] = {"add", "hello memory"};
    CmdResult r;
    long id;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    id = parse_id_stdout(r.out);
    ASSERT_EQ_INT(id, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(add_with_tags_and_source_human)
{
    char *db = make_temp_db_path();
    const char *args[] = {"add",    "--tag",    "pref",  "--tag",
                          "editor", "--source", "human", "use helix"};
    CmdResult r;
    const char *gargs[] = {"get", "--json", "1"};
    CmdResult g;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 8, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(parse_id_stdout(r.out), 1);
    cmd_result_free(&r);

    g = run_remember(db, gargs, 3, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "\"source\":\"human\"");
    ASSERT_STR_CONTAINS(g.out, "pref");
    ASSERT_STR_CONTAINS(g.out, "editor");
    ASSERT_STR_CONTAINS(g.out, "use helix");
    cmd_result_free(&g);
    free(db);
}

TEST(add_default_source_is_unknown)
{
    char *db = make_temp_db_path();
    const char *args[] = {"add", "no source flag"};
    CmdResult r;
    const char *gargs[] = {"get", "--json", "1"};
    CmdResult g;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    g = run_remember(db, gargs, 3, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "\"source\":\"unknown\"");
    cmd_result_free(&g);
    free(db);
}

TEST(add_source_agent_tool_accepted)
{
    char *db = make_temp_db_path();
    CmdResult r1;
    CmdResult r2;
    const char *a1[] = {"add", "--source", "agent", "from agent"};
    const char *a2[] = {"add", "--source", "tool", "from tool"};
    ASSERT_TRUE(db != NULL);
    r1 = run_remember(db, a1, 4, NULL);
    r2 = run_remember(db, a2, 4, NULL);
    ASSERT_EQ_INT(r1.exit_code, 0);
    ASSERT_EQ_INT(r2.exit_code, 0);
    cmd_result_free(&r1);
    cmd_result_free(&r2);
    free(db);
}

TEST(add_invalid_source_rejected)
{
    char *db = make_temp_db_path();
    const char *args[] = {"add", "--source", "robot", "nope"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(add_empty_body_rejected)
{
    char *db = make_temp_db_path();
    const char *args[] = {"add", ""};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(add_whitespace_only_body_rejected)
{
    char *db = make_temp_db_path();
    const char *args[] = {"add", "   \t\n  "};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(add_body_trimmed_before_store)
{
    char *db = make_temp_db_path();
    const char *args[] = {"add", "  padded body  "};
    CmdResult r;
    const char *gargs[] = {"get", "--json", "1"};
    CmdResult g;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    g = run_remember(db, gargs, 3, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "\"body\":\"padded body\"");
    cmd_result_free(&g);
    free(db);
}

TEST(add_dedupe_same_body_merges_tags)
{
    char *db = make_temp_db_path();
    CmdResult r1;
    CmdResult r2;
    CmdResult g;
    const char *a1[] = {"add", "--tag", "a", "same text"};
    const char *a2[] = {"add", "--tag", "b", "same text"};
    const char *gargs[] = {"get", "--json", "1"};
    long id1;
    long id2;
    ASSERT_TRUE(db != NULL);
    r1 = run_remember(db, a1, 4, NULL);
    r2 = run_remember(db, a2, 4, NULL);
    ASSERT_EQ_INT(r1.exit_code, 0);
    ASSERT_EQ_INT(r2.exit_code, 0);
    id1 = parse_id_stdout(r1.out);
    id2 = parse_id_stdout(r2.out);
    ASSERT_EQ_INT(id1, id2);
    cmd_result_free(&r1);
    cmd_result_free(&r2);
    g = run_remember(db, gargs, 3, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "a");
    ASSERT_STR_CONTAINS(g.out, "b");
    cmd_result_free(&g);
    free(db);
}

TEST(add_dedupe_trim_equivalent_bodies)
{
    char *db = make_temp_db_path();
    CmdResult r1;
    CmdResult r2;
    const char *a1[] = {"add", "  hello  "};
    const char *a2[] = {"add", "hello"};
    ASSERT_TRUE(db != NULL);
    r1 = run_remember(db, a1, 2, NULL);
    r2 = run_remember(db, a2, 2, NULL);
    ASSERT_EQ_INT(r1.exit_code, 0);
    ASSERT_EQ_INT(r2.exit_code, 0);
    ASSERT_EQ_INT(parse_id_stdout(r1.out), parse_id_stdout(r2.out));
    cmd_result_free(&r1);
    cmd_result_free(&r2);
    free(db);
}

TEST(add_dedupe_keeps_original_source)
{
    char *db = make_temp_db_path();
    CmdResult r1;
    CmdResult r2;
    CmdResult g;
    const char *a1[] = {"add", "--source", "human", "stable body"};
    const char *a2[] = {"add", "--source", "agent", "--tag", "x", "stable body"};
    const char *gargs[] = {"get", "--json", "1"};
    ASSERT_TRUE(db != NULL);
    r1 = run_remember(db, a1, 4, NULL);
    r2 = run_remember(db, a2, 6, NULL);
    ASSERT_EQ_INT(r1.exit_code, 0);
    ASSERT_EQ_INT(r2.exit_code, 0);
    cmd_result_free(&r1);
    cmd_result_free(&r2);
    g = run_remember(db, gargs, 3, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "\"source\":\"human\"");
    cmd_result_free(&g);
    free(db);
}

TEST(add_json_created_shape)
{
    char *db = make_temp_db_path();
    const char *args[] = {"add", "--json", "--tag", "t", "json body"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 5, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"version\":1");
    ASSERT_STR_CONTAINS(r.out, "\"action\":\"created\"");
    ASSERT_STR_CONTAINS(r.out, "\"count\":1");
    ASSERT_STR_CONTAINS(r.out, "\"entries\"");
    ASSERT_STR_CONTAINS(r.out, "json body");
    ASSERT_STR_CONTAINS(r.out, "t");
    cmd_result_free(&r);
    free(db);
}

TEST(add_json_merged_shape)
{
    char *db = make_temp_db_path();
    CmdResult r1;
    CmdResult r2;
    const char *a1[] = {"add", "--json", "dup"};
    const char *a2[] = {"add", "--json", "--tag", "m", "dup"};
    ASSERT_TRUE(db != NULL);
    r1 = run_remember(db, a1, 3, NULL);
    r2 = run_remember(db, a2, 5, NULL);
    ASSERT_EQ_INT(r1.exit_code, 0);
    ASSERT_EQ_INT(r2.exit_code, 0);
    ASSERT_STR_CONTAINS(r2.out, "\"action\":\"merged\"");
    ASSERT_STR_CONTAINS(r2.out, "\"entries\"");
    cmd_result_free(&r1);
    cmd_result_free(&r2);
    free(db);
}

TEST(add_stdin_body_dash)
{
    char *db = make_temp_db_path();
    const char *args[] = {"add", "-"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 2, "from stdin body");
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(parse_id_stdout(r.out), 1);
    cmd_result_free(&r);
    free(db);
}

TEST(add_body_over_64kib_rejected)
{
    char *db = make_temp_db_path();
    char *big;
    const char *args[2];
    CmdResult r;
    size_t i;
    ASSERT_TRUE(db != NULL);
    big = malloc(65537U + 1U);
    ASSERT_TRUE(big != NULL);
    if (big == NULL) {
        free(db);
        return;
    }
    for (i = 0; i < 65537U; i++) {
        big[i] = 'a';
    }
    big[65537U] = '\0';
    args[0] = "add";
    args[1] = big;
    r = run_remember(db, args, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(big);
    free(db);
}

TEST(add_tag_ascii_casefold)
{
    char *db = make_temp_db_path();
    const char *args[] = {"add", "--tag", "Foo", "tagged"};
    CmdResult r;
    const char *gargs[] = {"get", "--json", "1"};
    CmdResult g;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    g = run_remember(db, gargs, 3, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "foo");
    cmd_result_free(&g);
    free(db);
}

TEST(add_empty_tag_rejected)
{
    char *db = make_temp_db_path();
    const char *args[] = {"add", "--tag", "", "body"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(add_tag_with_space_rejected)
{
    char *db = make_temp_db_path();
    const char *args[] = {"add", "--tag", "two words", "body"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(add_tag_too_long_rejected)
{
    char *db = make_temp_db_path();
    char tag[66];
    const char *args[4];
    CmdResult r;
    size_t i;
    ASSERT_TRUE(db != NULL);
    for (i = 0; i < 65U; i++) {
        tag[i] = 't';
    }
    tag[65] = '\0';
    args[0] = "add";
    args[1] = "--tag";
    args[2] = tag;
    args[3] = "body";
    r = run_remember(db, args, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(add_tag_project_colon_style_allowed)
{
    char *db = make_temp_db_path();
    const char *args[] = {"add", "--tag", "project:remember", "note"};
    CmdResult r;
    const char *gargs[] = {"get", "--json", "1"};
    CmdResult g;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    g = run_remember(db, gargs, 3, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "project:remember");
    cmd_result_free(&g);
    free(db);
}

TEST(add_missing_body_rejected)
{
    char *db = make_temp_db_path();
    const char *args[] = {"add", "--tag", "only"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(add_json_body_with_control_and_quotes_stays_valid)
{
    char *db = make_temp_db_path();
    /* Includes ", \, newline, ESC — must escape in JSON and store intact. */
    char body[] = {'q', '"', 'u',  'o', 't',        'e', ' ', '\\',
                   ' ', 'n', '\n', 'e', (char)0x1b, 'x', '\0'};
    const char *args[] = {"add", "--json", body};
    const char *gargs[] = {"get", "--json", "1"};
    CmdResult r;
    CmdResult g;
    char *hex_stored;
    char hex_expect[64];
    size_t i;
    size_t body_len = sizeof(body) - 1U;
    ASSERT_TRUE(db != NULL);
    ASSERT_TRUE((body_len * 2U) + 1U <= sizeof(hex_expect));

    r = run_remember(db, args, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    /* Envelope stays parseable: quotes/backslash/newline/ESC are escaped. */
    ASSERT_STR_CONTAINS(r.out, "\"version\":1");
    ASSERT_STR_CONTAINS(r.out, "\"action\":\"created\"");
    ASSERT_STR_CONTAINS(r.out, "\\\"");
    ASSERT_STR_CONTAINS(r.out, "\\\\");
    ASSERT_STR_CONTAINS(r.out, "\\n");
    ASSERT_STR_CONTAINS(r.out, "\\u001b");
    /* Raw ESC must not appear unescaped in the JSON text. */
    ASSERT_TRUE(strchr(r.out, (char)0x1b) == NULL);
    cmd_result_free(&r);

    /* Round-trip via get --json: same escapes, no raw control on stdout. */
    g = run_remember(db, gargs, 3, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "\\\"");
    ASSERT_STR_CONTAINS(g.out, "\\\\");
    ASSERT_STR_CONTAINS(g.out, "\\n");
    ASSERT_STR_CONTAINS(g.out, "\\u001b");
    ASSERT_TRUE(strchr(g.out, (char)0x1b) == NULL);
    cmd_result_free(&g);

    /* On-disk body equals original bytes (hex: first-line helper can't carry \n). */
    for (i = 0; i < body_len; i++) {
        static const char k_hex[] = "0123456789ABCDEF";
        unsigned char b = (unsigned char)body[i];
        hex_expect[i * 2U] = k_hex[b >> 4];
        hex_expect[(i * 2U) + 1U] = k_hex[b & 0x0FU];
    }
    hex_expect[body_len * 2U] = '\0';
    hex_stored = harness_sqlite_query_line(db, "SELECT hex(body) FROM entries WHERE id=1;");
    ASSERT_TRUE(hex_stored != NULL);
    if (hex_stored != NULL) {
        ASSERT_STREQ(hex_stored, hex_expect);
        free(hex_stored);
    }
    free(db);
}

/*
 * stdin and argv must validate a body identically: a body that trims to exactly
 * 64 KiB is accepted regardless of surrounding whitespace (regression for the
 * stdin path capping raw bytes before trim).
 */
TEST(add_stdin_body_at_limit_accepted)
{
    char *db = make_temp_db_path();
    const char *args[] = {"add", "-"};
    char *in;
    CmdResult r;
    size_t i;

    ASSERT_TRUE(db != NULL);
    in = malloc(65536U + 2U); /* 64 KiB body + one trailing newline + NUL */
    ASSERT_TRUE(in != NULL);
    if (in == NULL) {
        free(db);
        return;
    }
    for (i = 0; i < 65536U; i++) {
        in[i] = 'a';
    }
    in[65536U] = '\n';
    in[65537U] = '\0';

    r = run_remember(db, args, 2, in);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(parse_id_stdout(r.out), 1);
    cmd_result_free(&r);
    free(in);
    free(db);
}

/* Over-limit after trim is rejected on stdin with the same message as argv. */
TEST(add_stdin_body_over_limit_rejected)
{
    char *db = make_temp_db_path();
    const char *args[] = {"add", "-"};
    char *in;
    CmdResult r;
    size_t i;

    ASSERT_TRUE(db != NULL);
    in = malloc(65537U + 1U);
    ASSERT_TRUE(in != NULL);
    if (in == NULL) {
        free(db);
        return;
    }
    for (i = 0; i < 65537U; i++) {
        in[i] = 'a';
    }
    in[65537U] = '\0';

    r = run_remember(db, args, 2, in);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "64 KiB");
    cmd_result_free(&r);
    free(in);
    free(db);
}

/*
 * get output safety + id parsing. These live here (an always-gated suite) rather
 * than the still-red get/list/delete suite, so this step's get behavior is
 * guarded now.
 *
 * Human `get` must not emit raw terminal escapes: a stored ESC renders as '?'.
 */
TEST(get_human_body_neutralizes_control_chars)
{
    char *db = make_temp_db_path();
    char body[] = {'x', (char)0x1b, 'y', '\0'};
    const char *aargs[] = {"add", body};
    const char *gargs[] = {"get", "1"};
    CmdResult a;
    CmdResult g;

    ASSERT_TRUE(db != NULL);
    a = run_remember(db, aargs, 2, NULL);
    ASSERT_EQ_INT(a.exit_code, 0);
    cmd_result_free(&a);

    g = run_remember(db, gargs, 2, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_TRUE(strchr(g.out, (char)0x1b) == NULL); /* no raw ESC */
    ASSERT_STR_CONTAINS(g.out, "x?y");
    cmd_result_free(&g);
    free(db);
}

/* But newlines are legitimate body content and must survive human `get`. */
TEST(get_human_body_preserves_newlines)
{
    char *db = make_temp_db_path();
    const char *aargs[] = {"add", "line1\nline2"};
    const char *gargs[] = {"get", "1"};
    CmdResult a;
    CmdResult g;

    ASSERT_TRUE(db != NULL);
    a = run_remember(db, aargs, 2, NULL);
    ASSERT_EQ_INT(a.exit_code, 0);
    cmd_result_free(&a);

    g = run_remember(db, gargs, 2, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "line1\nline2");
    cmd_result_free(&g);
    free(db);
}

/* An id that overflows long long is an invalid id (exit 1), not a lookup miss. */
TEST(get_overflow_id_rejected)
{
    char *db = make_temp_db_path();
    const char *gargs[] = {"get", "99999999999999999999999"};
    CmdResult g;

    ASSERT_TRUE(db != NULL);
    g = run_remember(db, gargs, 2, NULL);
    ASSERT_EQ_INT(g.exit_code, 1);
    ASSERT_STR_CONTAINS(g.err, "invalid id");
    cmd_result_free(&g);
    free(db);
}

void register_add_tests(void)
{
    RUN_TEST(add_basic_prints_id_one);
    RUN_TEST(add_with_tags_and_source_human);
    RUN_TEST(add_default_source_is_unknown);
    RUN_TEST(add_source_agent_tool_accepted);
    RUN_TEST(add_invalid_source_rejected);
    RUN_TEST(add_empty_body_rejected);
    RUN_TEST(add_whitespace_only_body_rejected);
    RUN_TEST(add_body_trimmed_before_store);
    RUN_TEST(add_dedupe_same_body_merges_tags);
    RUN_TEST(add_dedupe_trim_equivalent_bodies);
    RUN_TEST(add_dedupe_keeps_original_source);
    RUN_TEST(add_json_created_shape);
    RUN_TEST(add_json_merged_shape);
    RUN_TEST(add_stdin_body_dash);
    RUN_TEST(add_body_over_64kib_rejected);
    RUN_TEST(add_tag_ascii_casefold);
    RUN_TEST(add_empty_tag_rejected);
    RUN_TEST(add_tag_with_space_rejected);
    RUN_TEST(add_tag_too_long_rejected);
    RUN_TEST(add_tag_project_colon_style_allowed);
    RUN_TEST(add_missing_body_rejected);
    RUN_TEST(add_json_body_with_control_and_quotes_stays_valid);
    RUN_TEST(add_stdin_body_at_limit_accepted);
    RUN_TEST(add_stdin_body_over_limit_rejected);
    RUN_TEST(get_human_body_neutralizes_control_chars);
    RUN_TEST(get_human_body_preserves_newlines);
    RUN_TEST(get_overflow_id_rejected);
}
