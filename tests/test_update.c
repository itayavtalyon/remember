#include "harness.h"
#include "register.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

/* Copy the value of a `"key":"..."` JSON string field into out (empty if absent). */
static void extract_json_str(const char *json, const char *key, char *out, size_t outsz)
{
    const char *p;
    const char *end;
    size_t len;

    out[0] = '\0';
    p = (json != NULL) ? strstr(json, key) : NULL;
    if (p == NULL) {
        return;
    }
    p += strlen(key);
    end = strchr(p, '"');
    if (end == NULL) {
        return;
    }
    len = (size_t)(end - p);
    if (len >= outsz) {
        len = outsz - 1U;
    }
    memcpy(out, p, len);
    out[len] = '\0';
}

static void seed_tagged(const char *db)
{
    const char *args[] = {"add", "--tag", "a", "--tag", "b", "--source", "human", "original body"};
    CmdResult r = run_remember(db, args, 8, NULL);
    cmd_result_free(&r);
}

TEST(update_text_only_keeps_tags)
{
    char *db = make_temp_db_path();
    CmdResult u;
    CmdResult g;
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
    CmdResult u;
    CmdResult g;
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
    CmdResult u;
    CmdResult g;
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
    CmdResult u;
    CmdResult g;
    const char *uargs[] = {"update", "1", "--text", "both changed", "--tag", "only"};
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
    CmdResult r;
    CmdResult u;
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
    CmdResult u;
    CmdResult g;
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
    CmdResult u;
    CmdResult g;
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
    CmdResult u;
    CmdResult g;
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
    CmdResult u;
    CmdResult g;
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

TEST(update_replace_tags_keeps_shared_tag_on_other_entry)
{
    /* Replacing entry 1's tags must not GC a tag entry 2 still uses. */
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult u;
    CmdResult g;
    const char *a1[] = {"add", "--tag", "shared", "--tag", "x", "e1"};
    const char *a2[] = {"add", "--tag", "shared", "--tag", "y", "e2"};
    const char *uargs[] = {"update", "1", "--tag", "only"};
    const char *gargs[] = {"get", "--json", "2"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a1, 6, NULL);
    cmd_result_free(&r);
    r = run_remember(db, a2, 6, NULL);
    cmd_result_free(&r);
    u = run_remember(db, uargs, 4, NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    cmd_result_free(&u);
    g = run_remember(db, gargs, 3, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "shared");
    ASSERT_STR_CONTAINS(g.out, "y");
    cmd_result_free(&g);
    free(db);
}

TEST(update_by_key_clear_tags)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult u;
    CmdResult g;
    const char *a[] = {"add", "--key", "k", "--tag", "t", "body"};
    const char *uargs[] = {"update", "--key", "k", "--clear-tags"};
    const char *gargs[] = {"get", "--json", "--key", "k"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, 6, NULL);
    cmd_result_free(&r);
    u = run_remember(db, uargs, 4, NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    cmd_result_free(&u);
    g = run_remember(db, gargs, 4, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "\"tags\":[]");
    cmd_result_free(&g);
    free(db);
}

TEST(fts_reflects_tag_update)
{
    /* Tag replace re-syncs FTS: old tag token drops, new one is findable. */
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult u;
    CmdResult s;
    const char *a[] = {"add", "--tag", "oldtagxyz", "plain body"};
    const char *uargs[] = {"update", "1", "--tag", "newtagabc"};
    const char *sold[] = {"search", "--json", "oldtagxyz"};
    const char *snew[] = {"search", "--json", "newtagabc"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, 4, NULL);
    cmd_result_free(&r);
    u = run_remember(db, uargs, 4, NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    cmd_result_free(&u);
    s = run_remember(db, sold, 3, NULL);
    ASSERT_EQ_INT(s.exit_code, 0);
    ASSERT_STR_CONTAINS(s.out, "\"total\":0");
    cmd_result_free(&s);
    s = run_remember(db, snew, 3, NULL);
    ASSERT_EQ_INT(s.exit_code, 0);
    ASSERT_STR_CONTAINS(s.out, "plain body");
    cmd_result_free(&s);
    free(db);
}

TEST(update_moves_entry_to_top_of_list)
{
    /* Regression: sub-second updated_at so a just-updated older entry sorts
       ahead of a newer one added in the same second (newest-first). */
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult u;
    CmdResult l;
    const char *a1[] = {"add", "first"};
    const char *a2[] = {"add", "second"};
    const char *uargs[] = {"update", "1", "--text", "first edited"};
    const char *largs[] = {"list", "--json"};
    const char *p1;
    const char *p2;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a1, 2, NULL);
    cmd_result_free(&r);
    r = run_remember(db, a2, 2, NULL);
    cmd_result_free(&r);
    u = run_remember(db, uargs, 4, NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    cmd_result_free(&u);
    l = run_remember(db, largs, 2, NULL);
    ASSERT_EQ_INT(l.exit_code, 0);
    p1 = (l.out != NULL) ? strstr(l.out, "\"id\":1") : NULL;
    p2 = (l.out != NULL) ? strstr(l.out, "\"id\":2") : NULL;
    ASSERT_TRUE(p1 != NULL);
    ASSERT_TRUE(p2 != NULL);
    ASSERT_TRUE(p1 < p2); /* updated id 1 precedes untouched id 2 */
    cmd_result_free(&l);
    free(db);
}

TEST(update_bumps_updated_at)
{
    /* A successful update always advances updated_at (sub-second resolution
       makes the change observable without sleeping). */
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult u;
    char before[32];
    char after[32];
    const char *a[] = {"add", "--json", "orig body"};
    const char *uargs[] = {"update", "--json", "1", "--text", "changed body"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    extract_json_str(r.out, "\"updated_at\":\"", before, sizeof(before));
    cmd_result_free(&r);
    ASSERT_TRUE(before[0] != '\0');
    u = run_remember(db, uargs, 5, NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    extract_json_str(u.out, "\"updated_at\":\"", after, sizeof(after));
    ASSERT_TRUE(after[0] != '\0');
    ASSERT_TRUE(strcmp(before, after) != 0);
    cmd_result_free(&u);
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
    RUN_TEST(update_replace_tags_keeps_shared_tag_on_other_entry);
    RUN_TEST(update_by_key_clear_tags);
    RUN_TEST(fts_reflects_tag_update);
    RUN_TEST(update_moves_entry_to_top_of_list);
    RUN_TEST(update_bumps_updated_at);
}
