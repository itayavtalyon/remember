#include "test.h"
#include "harness.h"
#include "register.h"

#include <stdlib.h>

static void seed_tagged(const char *db)
{
    const char *args[] = {"add", "--tag", "a", "--tag", "b", "--source", "human",
                          "original body"};
    CmdResult r = run_remember(db, args, 8, NULL);
    cmd_result_free(&r);
}

TEST(update_text_only_keeps_tags)
{
    char *db = make_temp_db_path();
    CmdResult u, g;
    const char *uargs[] = {"update", "1", "--text", "new body only"};
    const char *gargs[] = {"get", "--json", "1"};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, 4, NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    ASSERT_EQ_INT(parse_id_stdout(u.out), 1);
    cmd_result_free(&u);
    g = run_remember(db, gargs, 3, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "new body only");
    ASSERT_STR_CONTAINS(g.out, "a");
    ASSERT_STR_CONTAINS(g.out, "b");
    cmd_result_free(&g);
    free(db);
}

TEST(update_tags_only_keeps_body)
{
    char *db = make_temp_db_path();
    CmdResult u, g;
    const char *uargs[] = {"update", "1", "--tag", "z"};
    const char *gargs[] = {"get", "--json", "1"};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, 4, NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    cmd_result_free(&u);
    g = run_remember(db, gargs, 3, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "original body");
    ASSERT_STR_CONTAINS(g.out, "z");
    ASSERT_STR_NOT_CONTAINS(g.out, "\"a\"");
    cmd_result_free(&g);
    free(db);
}

TEST(update_clear_tags_flag)
{
    char *db = make_temp_db_path();
    CmdResult u, g;
    const char *uargs[] = {"update", "1", "--clear-tags"};
    const char *gargs[] = {"get", "--json", "1"};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, 3, NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    cmd_result_free(&u);
    g = run_remember(db, gargs, 3, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "original body");
    ASSERT_STR_CONTAINS(g.out, "\"tags\":[]");
    cmd_result_free(&g);
    free(db);
}

TEST(update_tag_and_clear_tags_rejected)
{
    char *db = make_temp_db_path();
    CmdResult u;
    const char *uargs[] = {"update", "1", "--tag", "x", "--clear-tags"};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, 5, NULL);
    ASSERT_EQ_INT(u.exit_code, 1);
    cmd_result_free(&u);
    free(db);
}

TEST(update_same_text_still_succeeds_and_returns_entry)
{
    /* Successful update always refreshes updated_at (writing is a touch). */
    char *db = make_temp_db_path();
    CmdResult u;
    const char *uargs[] = {"update", "--json", "1", "--text", "original body"};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, 5, NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    ASSERT_STR_CONTAINS(u.out, "\"action\":\"updated\"");
    ASSERT_STR_CONTAINS(u.out, "\"entries\"");
    ASSERT_STR_CONTAINS(u.out, "original body");
    cmd_result_free(&u);
    free(db);
}

TEST(update_text_and_tags_together)
{
    char *db = make_temp_db_path();
    CmdResult u, g;
    const char *uargs[] = {"update", "1", "--text", "both changed", "--tag",
                           "only"};
    const char *gargs[] = {"get", "--json", "1"};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, 6, NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    cmd_result_free(&u);
    g = run_remember(db, gargs, 3, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "both changed");
    ASSERT_STR_CONTAINS(g.out, "only");
    ASSERT_STR_NOT_CONTAINS(g.out, "\"a\"");
    cmd_result_free(&g);
    free(db);
}

TEST(update_no_change_rejected)
{
    char *db = make_temp_db_path();
    CmdResult u;
    const char *uargs[] = {"update", "1"};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, 2, NULL);
    ASSERT_EQ_INT(u.exit_code, 1);
    cmd_result_free(&u);
    free(db);
}

TEST(update_missing_id_exits_two)
{
    char *db = make_temp_db_path();
    CmdResult u;
    const char *uargs[] = {"update", "99", "--text", "nope"};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, 4, NULL);
    ASSERT_EQ_INT(u.exit_code, 2);
    cmd_result_free(&u);
    free(db);
}

TEST(update_body_hash_collision_rejected)
{
    char *db = make_temp_db_path();
    CmdResult r, u;
    const char *a1[] = {"add", "body one"};
    const char *a2[] = {"add", "body two"};
    const char *uargs[] = {"update", "1", "--text", "body two"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a1, 2, NULL);
    cmd_result_free(&r);
    r = run_remember(db, a2, 2, NULL);
    cmd_result_free(&r);
    u = run_remember(db, uargs, 4, NULL);
    ASSERT_EQ_INT(u.exit_code, 1);
    ASSERT_STR_CONTAINS(u.err, "2");
    cmd_result_free(&u);
    free(db);
}

TEST(update_empty_text_rejected)
{
    char *db = make_temp_db_path();
    CmdResult u;
    const char *uargs[] = {"update", "1", "--text", "   "};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, 4, NULL);
    ASSERT_EQ_INT(u.exit_code, 1);
    cmd_result_free(&u);
    free(db);
}

TEST(update_source_immutable)
{
    char *db = make_temp_db_path();
    CmdResult u, g;
    const char *uargs[] = {"update", "1", "--text", "still human source"};
    const char *gargs[] = {"get", "--json", "1"};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, 4, NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    cmd_result_free(&u);
    g = run_remember(db, gargs, 3, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "\"source\":\"human\"");
    cmd_result_free(&g);
    free(db);
}

TEST(update_json_shape)
{
    char *db = make_temp_db_path();
    CmdResult u;
    const char *uargs[] = {"update", "--json", "1", "--text", "json update"};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, 5, NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    ASSERT_STR_CONTAINS(u.out, "\"version\":1");
    ASSERT_STR_CONTAINS(u.out, "\"action\":\"updated\"");
    ASSERT_STR_CONTAINS(u.out, "\"count\":1");
    ASSERT_STR_CONTAINS(u.out, "\"entries\"");
    ASSERT_STR_CONTAINS(u.out, "json update");
    cmd_result_free(&u);
    free(db);
}

TEST(update_text_stdin_dash)
{
    char *db = make_temp_db_path();
    CmdResult u, g;
    const char *uargs[] = {"update", "1", "--text", "-"};
    const char *gargs[] = {"get", "--json", "1"};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, 4, "stdin update body");
    ASSERT_EQ_INT(u.exit_code, 0);
    cmd_result_free(&u);
    g = run_remember(db, gargs, 3, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "stdin update body");
    cmd_result_free(&g);
    free(db);
}

TEST(update_positional_body_not_accepted)
{
    /* Regression: positional body on update must not work (tag wipe footgun). */
    char *db = make_temp_db_path();
    CmdResult u, g;
    const char *uargs[] = {"update", "1", "positional not allowed"};
    const char *gargs[] = {"get", "--json", "1"};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, 3, NULL);
    ASSERT_EQ_INT(u.exit_code, 1);
    cmd_result_free(&u);
    g = run_remember(db, gargs, 3, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "original body");
    ASSERT_STR_CONTAINS(g.out, "a");
    cmd_result_free(&g);
    free(db);
}

TEST(update_replace_tags_multiple)
{
    char *db = make_temp_db_path();
    CmdResult u, g;
    const char *uargs[] = {"update", "1", "--tag", "one", "--tag", "two"};
    const char *gargs[] = {"get", "--json", "1"};
    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    u = run_remember(db, uargs, 6, NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    cmd_result_free(&u);
    g = run_remember(db, gargs, 3, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "one");
    ASSERT_STR_CONTAINS(g.out, "two");
    ASSERT_STR_NOT_CONTAINS(g.out, "\"a\"");
    cmd_result_free(&g);
    free(db);
}

void register_update_tests(void)
{
    RUN_TEST(update_text_only_keeps_tags);
    RUN_TEST(update_tags_only_keeps_body);
    RUN_TEST(update_clear_tags_flag);
    RUN_TEST(update_tag_and_clear_tags_rejected);
    RUN_TEST(update_same_text_still_succeeds_and_returns_entry);
    RUN_TEST(update_text_and_tags_together);
    RUN_TEST(update_no_change_rejected);
    RUN_TEST(update_missing_id_exits_two);
    RUN_TEST(update_body_hash_collision_rejected);
    RUN_TEST(update_empty_text_rejected);
    RUN_TEST(update_source_immutable);
    RUN_TEST(update_json_shape);
    RUN_TEST(update_text_stdin_dash);
    RUN_TEST(update_positional_body_not_accepted);
    RUN_TEST(update_replace_tags_multiple);
}
