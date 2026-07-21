#include "test.h"
#include "harness.h"
#include "register.h"

#include <stdlib.h>

/*
 * --key key-value slots (design log Round 8).
 * Key is identity when present; body-hash dedupe is keyless-only.
 */

TEST(add_key_creates_slot)
{
    char *db = make_temp_db_path();
    CmdResult r, g;
    const char *a[] = {"add", "--json", "--key", "pref:editor", "helix"};
    const char *gargs[] = {"get", "--json", "--key", "pref:editor"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, 5, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"action\":\"created\"");
    ASSERT_STR_CONTAINS(r.out, "\"key\":\"pref:editor\"");
    ASSERT_STR_CONTAINS(r.out, "helix");
    cmd_result_free(&r);
    g = run_remember(db, gargs, 4, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "helix");
    cmd_result_free(&g);
    free(db);
}

TEST(add_key_upsert_same_id_replaces_body)
{
    char *db = make_temp_db_path();
    CmdResult r1, r2, g;
    const char *a1[] = {"add", "--json", "--key", "pref:editor", "helix"};
    const char *a2[] = {"add", "--json", "--key", "pref:editor", "zed"};
    const char *gargs[] = {"get", "--json", "--key", "pref:editor"};
    ASSERT_TRUE(db != NULL);
    r1 = run_remember(db, a1, 5, NULL);
    r2 = run_remember(db, a2, 5, NULL);
    ASSERT_EQ_INT(r1.exit_code, 0);
    ASSERT_EQ_INT(r2.exit_code, 0);
    ASSERT_STR_CONTAINS(r2.out, "\"action\":\"updated\"");
    ASSERT_STR_CONTAINS(r1.out, "\"id\":1");
    ASSERT_STR_CONTAINS(r2.out, "\"id\":1");
    cmd_result_free(&r1);
    cmd_result_free(&r2);
    g = run_remember(db, gargs, 4, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "zed");
    ASSERT_STR_NOT_CONTAINS(g.out, "helix");
    cmd_result_free(&g);
    free(db);
}

TEST(add_key_upsert_unions_tags)
{
    char *db = make_temp_db_path();
    CmdResult r1, r2, g;
    const char *a1[] = {"add", "--key", "k", "--tag", "a", "v1"};
    const char *a2[] = {"add", "--key", "k", "--tag", "b", "v2"};
    const char *gargs[] = {"get", "--json", "--key", "k"};
    ASSERT_TRUE(db != NULL);
    r1 = run_remember(db, a1, 6, NULL);
    r2 = run_remember(db, a2, 6, NULL);
    ASSERT_EQ_INT(r1.exit_code, 0);
    ASSERT_EQ_INT(r2.exit_code, 0);
    cmd_result_free(&r1);
    cmd_result_free(&r2);
    g = run_remember(db, gargs, 4, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "v2");
    ASSERT_STR_CONTAINS(g.out, "a");
    ASSERT_STR_CONTAINS(g.out, "b");
    cmd_result_free(&g);
    free(db);
}

TEST(add_key_keeps_original_source)
{
    char *db = make_temp_db_path();
    CmdResult r1, r2, g;
    const char *a1[] = {"add", "--key", "k", "--source", "human", "one"};
    const char *a2[] = {"add", "--key", "k", "--source", "agent", "two"};
    const char *gargs[] = {"get", "--json", "--key", "k"};
    ASSERT_TRUE(db != NULL);
    r1 = run_remember(db, a1, 6, NULL);
    r2 = run_remember(db, a2, 6, NULL);
    ASSERT_EQ_INT(r1.exit_code, 0);
    ASSERT_EQ_INT(r2.exit_code, 0);
    cmd_result_free(&r1);
    cmd_result_free(&r2);
    g = run_remember(db, gargs, 4, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "\"source\":\"human\"");
    ASSERT_STR_CONTAINS(g.out, "two");
    cmd_result_free(&g);
    free(db);
}

TEST(keyed_entries_may_share_body_text)
{
    char *db = make_temp_db_path();
    CmdResult r1, r2, l;
    const char *a1[] = {"add", "--json", "--key", "k1", "same"};
    const char *a2[] = {"add", "--json", "--key", "k2", "same"};
    const char *largs[] = {"list", "--json"};
    ASSERT_TRUE(db != NULL);
    r1 = run_remember(db, a1, 5, NULL);
    r2 = run_remember(db, a2, 5, NULL);
    ASSERT_EQ_INT(r1.exit_code, 0);
    ASSERT_EQ_INT(r2.exit_code, 0);
    ASSERT_STR_CONTAINS(r1.out, "\"id\":1");
    ASSERT_STR_CONTAINS(r2.out, "\"id\":2");
    cmd_result_free(&r1);
    cmd_result_free(&r2);
    l = run_remember(db, largs, 2, NULL);
    ASSERT_EQ_INT(l.exit_code, 0);
    ASSERT_STR_CONTAINS(l.out, "\"total\":2");
    cmd_result_free(&l);
    free(db);
}

TEST(keyless_still_dedupes_among_keyless_only)
{
    char *db = make_temp_db_path();
    CmdResult rk, r1, r2;
    const char *ak[] = {"add", "--key", "slot", "shared text"};
    const char *a1[] = {"add", "shared text"};
    const char *a2[] = {"add", "shared text"};
    ASSERT_TRUE(db != NULL);
    rk = run_remember(db, ak, 4, NULL);
    r1 = run_remember(db, a1, 2, NULL);
    r2 = run_remember(db, a2, 2, NULL);
    ASSERT_EQ_INT(rk.exit_code, 0);
    ASSERT_EQ_INT(r1.exit_code, 0);
    ASSERT_EQ_INT(r2.exit_code, 0);
    /* keyless pair share one id; keyed is separate */
    ASSERT_EQ_INT(parse_id_stdout(r1.out), parse_id_stdout(r2.out));
    ASSERT_TRUE(parse_id_stdout(rk.out) != parse_id_stdout(r1.out));
    cmd_result_free(&rk);
    cmd_result_free(&r1);
    cmd_result_free(&r2);
    free(db);
}

TEST(get_by_key_missing_exits_two)
{
    char *db = make_temp_db_path();
    CmdResult g;
    const char *gargs[] = {"get", "--key", "nope"};
    ASSERT_TRUE(db != NULL);
    {
        const char *a[] = {"add", "x"};
        CmdResult r = run_remember(db, a, 2, NULL);
        cmd_result_free(&r);
    }
    g = run_remember(db, gargs, 3, NULL);
    ASSERT_EQ_INT(g.exit_code, 2);
    cmd_result_free(&g);
    free(db);
}

TEST(get_both_id_and_key_rejected)
{
    char *db = make_temp_db_path();
    CmdResult g;
    const char *a[] = {"add", "--key", "k", "body"};
    const char *gargs[] = {"get", "1", "--key", "k"};
    ASSERT_TRUE(db != NULL);
    {
        CmdResult r = run_remember(db, a, 4, NULL);
        cmd_result_free(&r);
    }
    g = run_remember(db, gargs, 4, NULL);
    ASSERT_EQ_INT(g.exit_code, 1);
    cmd_result_free(&g);
    free(db);
}

TEST(get_neither_id_nor_key_rejected)
{
    char *db = make_temp_db_path();
    CmdResult g;
    const char *gargs[] = {"get"};
    ASSERT_TRUE(db != NULL);
    g = run_remember(db, gargs, 1, NULL);
    ASSERT_EQ_INT(g.exit_code, 1);
    cmd_result_free(&g);
    free(db);
}

TEST(delete_by_key)
{
    char *db = make_temp_db_path();
    CmdResult r, d, g;
    const char *a[] = {"add", "--key", "k", "to remove"};
    const char *dargs[] = {"delete", "--json", "--key", "k"};
    const char *gargs[] = {"get", "--key", "k"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, 4, NULL);
    cmd_result_free(&r);
    d = run_remember(db, dargs, 4, NULL);
    ASSERT_EQ_INT(d.exit_code, 0);
    ASSERT_STR_CONTAINS(d.out, "\"action\":\"deleted\"");
    ASSERT_STR_CONTAINS(d.out, "to remove");
    cmd_result_free(&d);
    g = run_remember(db, gargs, 3, NULL);
    ASSERT_EQ_INT(g.exit_code, 2);
    cmd_result_free(&g);
    free(db);
}

TEST(update_by_key_text)
{
    char *db = make_temp_db_path();
    CmdResult r, u, g;
    const char *a[] = {"add", "--key", "k", "--tag", "t", "old"};
    const char *uargs[] = {"update", "--key", "k", "--text", "new"};
    const char *gargs[] = {"get", "--json", "--key", "k"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, 6, NULL);
    cmd_result_free(&r);
    u = run_remember(db, uargs, 5, NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    cmd_result_free(&u);
    g = run_remember(db, gargs, 4, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "new");
    ASSERT_STR_CONTAINS(g.out, "t"); /* tags unchanged */
    cmd_result_free(&g);
    free(db);
}

TEST(update_keyed_no_body_hash_conflict)
{
    /* Keyed entry may take a body already used by another keyless/keyed row. */
    char *db = make_temp_db_path();
    CmdResult r, u;
    const char *a1[] = {"add", "shared body"};
    const char *a2[] = {"add", "--key", "k", "other"};
    const char *uargs[] = {"update", "--key", "k", "--text", "shared body"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a1, 2, NULL);
    cmd_result_free(&r);
    r = run_remember(db, a2, 4, NULL);
    cmd_result_free(&r);
    u = run_remember(db, uargs, 5, NULL);
    ASSERT_EQ_INT(u.exit_code, 0);
    cmd_result_free(&u);
    free(db);
}

TEST(list_filter_by_key)
{
    char *db = make_temp_db_path();
    CmdResult r, l;
    const char *a1[] = {"add", "--key", "keep", "aaa"};
    const char *a2[] = {"add", "--key", "other", "bbb"};
    const char *largs[] = {"list", "--json", "--key", "keep"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a1, 4, NULL);
    cmd_result_free(&r);
    r = run_remember(db, a2, 4, NULL);
    cmd_result_free(&r);
    l = run_remember(db, largs, 4, NULL);
    ASSERT_EQ_INT(l.exit_code, 0);
    ASSERT_STR_CONTAINS(l.out, "aaa");
    ASSERT_STR_NOT_CONTAINS(l.out, "bbb");
    ASSERT_STR_CONTAINS(l.out, "\"total\":1");
    cmd_result_free(&l);
    free(db);
}

TEST(search_filter_by_key)
{
    char *db = make_temp_db_path();
    CmdResult r, s;
    const char *a1[] = {"add", "--key", "keep", "unique word alpha"};
    const char *a2[] = {"add", "--key", "other", "unique word beta"};
    const char *sargs[] = {"search", "--json", "--key", "keep", "unique"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a1, 4, NULL);
    cmd_result_free(&r);
    r = run_remember(db, a2, 4, NULL);
    cmd_result_free(&r);
    s = run_remember(db, sargs, 5, NULL);
    ASSERT_EQ_INT(s.exit_code, 0);
    ASSERT_STR_CONTAINS(s.out, "alpha");
    ASSERT_STR_NOT_CONTAINS(s.out, "beta");
    cmd_result_free(&s);
    free(db);
}

TEST(add_key_ascii_casefold)
{
    char *db = make_temp_db_path();
    CmdResult r, g;
    const char *a[] = {"add", "--key", "Pref:Editor", "v"};
    const char *gargs[] = {"get", "--json", "--key", "pref:editor"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    g = run_remember(db, gargs, 4, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "\"key\":\"pref:editor\"");
    cmd_result_free(&g);
    free(db);
}

TEST(add_empty_key_rejected)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *a[] = {"add", "--key", "", "body"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(delete_by_key_missing_exits_two)
{
    char *db = make_temp_db_path();
    CmdResult d;
    const char *dargs[] = {"delete", "--key", "missing"};
    ASSERT_TRUE(db != NULL);
    {
        const char *a[] = {"add", "x"};
        CmdResult r = run_remember(db, a, 2, NULL);
        cmd_result_free(&r);
    }
    d = run_remember(db, dargs, 3, NULL);
    ASSERT_EQ_INT(d.exit_code, 2);
    cmd_result_free(&d);
    free(db);
}

TEST(update_by_key_missing_exits_two)
{
    char *db = make_temp_db_path();
    CmdResult u;
    const char *uargs[] = {"update", "--key", "missing", "--text", "n"};
    ASSERT_TRUE(db != NULL);
    {
        const char *a[] = {"add", "x"};
        CmdResult r = run_remember(db, a, 2, NULL);
        cmd_result_free(&r);
    }
    u = run_remember(db, uargs, 5, NULL);
    ASSERT_EQ_INT(u.exit_code, 2);
    cmd_result_free(&u);
    free(db);
}

TEST(update_both_id_and_key_rejected)
{
    char *db = make_temp_db_path();
    CmdResult u;
    const char *a[] = {"add", "--key", "k", "body"};
    const char *uargs[] = {"update", "1", "--key", "k", "--text", "n"};
    ASSERT_TRUE(db != NULL);
    {
        CmdResult r = run_remember(db, a, 4, NULL);
        cmd_result_free(&r);
    }
    u = run_remember(db, uargs, 6, NULL);
    ASSERT_EQ_INT(u.exit_code, 1);
    cmd_result_free(&u);
    free(db);
}

void register_key_tests(void)
{
    RUN_TEST(add_key_creates_slot);
    RUN_TEST(add_key_upsert_same_id_replaces_body);
    RUN_TEST(add_key_upsert_unions_tags);
    RUN_TEST(add_key_keeps_original_source);
    RUN_TEST(keyed_entries_may_share_body_text);
    RUN_TEST(keyless_still_dedupes_among_keyless_only);
    RUN_TEST(get_by_key_missing_exits_two);
    RUN_TEST(get_both_id_and_key_rejected);
    RUN_TEST(get_neither_id_nor_key_rejected);
    RUN_TEST(delete_by_key);
    RUN_TEST(update_by_key_text);
    RUN_TEST(update_keyed_no_body_hash_conflict);
    RUN_TEST(list_filter_by_key);
    RUN_TEST(search_filter_by_key);
    RUN_TEST(add_key_ascii_casefold);
    RUN_TEST(add_empty_key_rejected);
    RUN_TEST(delete_by_key_missing_exits_two);
    RUN_TEST(update_by_key_missing_exits_two);
    RUN_TEST(update_both_id_and_key_rejected);
}
