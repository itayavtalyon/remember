#include "harness.h"
#include "register.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void seed_search_corpus(const char *db)
{
    CmdResult r;
    const char *a1[] = {"add", "--tag", "pref", "--tag", "editor", "Preferred editor is helix"};
    const char *a2[] = {"add", "--tag", "decision", "--source", "agent", "Use FTS5 not embeddings"};
    const char *a3[] = {"add", "--tag", "pref", "--source", "human", "Font size 14 in terminal"};
    r = run_remember(db, a1, 6, NULL);
    cmd_result_free(&r);
    r = run_remember(db, a2, 6, NULL);
    cmd_result_free(&r);
    r = run_remember(db, a3, 6, NULL);
    cmd_result_free(&r);
}

TEST(search_finds_body_token)
{
    char *db = make_temp_db_path();
    const char *args[] = {"search", "--json", "helix"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    seed_search_corpus(db);
    r = run_remember(db, args, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "helix");
    ASSERT_STR_CONTAINS(r.out, "\"version\":1");
    cmd_result_free(&r);
    free(db);
}

TEST(search_finds_tag_token_via_fts)
{
    char *db = make_temp_db_path();
    const char *args[] = {"search", "--json", "decision"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    seed_search_corpus(db);
    r = run_remember(db, args, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "FTS5");
    cmd_result_free(&r);
    free(db);
}

TEST(search_tag_filter_ands_with_query)
{
    char *db = make_temp_db_path();
    const char *args[] = {"search", "--json", "--tag", "pref", "editor"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    seed_search_corpus(db);
    r = run_remember(db, args, 5, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "helix");
    ASSERT_STR_NOT_CONTAINS(r.out, "Font size");
    cmd_result_free(&r);
    free(db);
}

TEST(search_source_filter)
{
    char *db = make_temp_db_path();
    const char *args[] = {"search", "--json", "--source", "agent", "FTS5"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    seed_search_corpus(db);
    r = run_remember(db, args, 5, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "embeddings");
    ASSERT_STR_NOT_CONTAINS(r.out, "helix");
    cmd_result_free(&r);
    free(db);
}

TEST(search_empty_query_rejected)
{
    char *db = make_temp_db_path();
    const char *args[] = {"search", ""};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "empty search query");
    cmd_result_free(&r);
    free(db);
}

TEST(search_whitespace_only_query_rejected)
{
    char *db = make_temp_db_path();
    const char *args[] = {"search", "   \t  "};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "empty search query");
    cmd_result_free(&r);
    free(db);
}

/* Hits both leading and trailing ASCII-ws trim (coverage for end-- path). */
TEST(search_query_trims_outer_whitespace)
{
    char *db = make_temp_db_path();
    const char *args[] = {"search", "--json", "  helix  \t"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    seed_search_corpus(db);
    r = run_remember(db, args, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "Preferred editor is helix");
    cmd_result_free(&r);
    free(db);
}

TEST(search_missing_query_rejected)
{
    char *db = make_temp_db_path();
    const char *args[] = {"search"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 1, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(search_no_matches_exits_zero)
{
    char *db = make_temp_db_path();
    const char *args[] = {"search", "--json", "zzznomatch999"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    seed_search_corpus(db);
    r = run_remember(db, args, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"entries\":[]");
    ASSERT_STR_CONTAINS(r.out, "\"total\":0");
    ASSERT_STR_CONTAINS(r.out, "\"count\":0");
    cmd_result_free(&r);
    free(db);
}

TEST(search_json_paging_fields)
{
    char *db = make_temp_db_path();
    const char *args[] = {
        "search", "--json", "--limit", "1", "--offset", "0", "pref OR helix OR FTS5 OR Font"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    seed_search_corpus(db);
    r = run_remember(db, args, 7, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"offset\":0");
    ASSERT_STR_CONTAINS(r.out, "\"limit\":1");
    ASSERT_STR_CONTAINS(r.out, "\"count\":1");
    ASSERT_STR_CONTAINS(r.out, "\"total\":");
    cmd_result_free(&r);
    free(db);
}

TEST(search_json_includes_full_body)
{
    char *db = make_temp_db_path();
    const char *args[] = {"search", "--json", "helix"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    seed_search_corpus(db);
    r = run_remember(db, args, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "Preferred editor is helix");
    cmd_result_free(&r);
    free(db);
}

TEST(search_human_preview_not_only_id)
{
    char *db = make_temp_db_path();
    const char *args[] = {"search", "helix"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    seed_search_corpus(db);
    r = run_remember(db, args, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    /* human mode: id present and some body preview */
    ASSERT_STR_CONTAINS(r.out, "1");
    ASSERT_STR_CONTAINS(r.out, "helix");
    cmd_result_free(&r);
    free(db);
}

TEST(search_limit)
{
    char *db = make_temp_db_path();
    int i;
    CmdResult r;
    int count = 0;
    const char *p;
    const char *args[] = {"search", "--json", "--limit", "3", "unique"};
    ASSERT_TRUE(db != NULL);
    for (i = 0; i < 10; i++) {
        char body[64];
        const char *a[2];
        CmdResult ar;
        (void)snprintf(body, sizeof(body), "unique hit number %d", i);
        a[0] = "add";
        a[1] = body;
        ar = run_remember(db, a, 2, NULL);
        cmd_result_free(&ar);
    }
    r = run_remember(db, args, 5, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    p = r.out;
    while ((p = strstr(p, "\"id\":")) != NULL) {
        count++;
        p += 5;
    }
    ASSERT_EQ_INT(count, 3);
    cmd_result_free(&r);
    free(db);
}

TEST(search_invalid_fts_syntax_rejected)
{
    char *db = make_temp_db_path();
    const char *args[] = {"search", "AND AND"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    {
        const char *a[] = {"add", "something searchable"};
        CmdResult ar = run_remember(db, a, 2, NULL);
        cmd_result_free(&ar);
    }
    r = run_remember(db, args, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(search_multi_tag_and_filter)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *a1[] = {"add", "--tag", "x", "--tag", "y", "both tags here"};
    const char *a2[] = {"add", "--tag", "x", "only x tag here"};
    const char *s[] = {"search", "--json", "--tag", "x", "--tag", "y", "tags"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a1, 6, NULL);
    cmd_result_free(&r);
    r = run_remember(db, a2, 4, NULL);
    cmd_result_free(&r);
    r = run_remember(db, s, 7, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "both tags here");
    ASSERT_STR_NOT_CONTAINS(r.out, "only x tag here");
    cmd_result_free(&r);
    free(db);
}

void register_search_tests(void)
{
    RUN_TEST(search_finds_body_token);
    RUN_TEST(search_finds_tag_token_via_fts);
    RUN_TEST(search_tag_filter_ands_with_query);
    RUN_TEST(search_source_filter);
    RUN_TEST(search_empty_query_rejected);
    RUN_TEST(search_whitespace_only_query_rejected);
    RUN_TEST(search_query_trims_outer_whitespace);
    RUN_TEST(search_missing_query_rejected);
    RUN_TEST(search_no_matches_exits_zero);
    RUN_TEST(search_json_paging_fields);
    RUN_TEST(search_json_includes_full_body);
    RUN_TEST(search_human_preview_not_only_id);
    RUN_TEST(search_limit);
    RUN_TEST(search_invalid_fts_syntax_rejected);
    RUN_TEST(search_multi_tag_and_filter);
}
