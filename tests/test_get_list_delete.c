#include "harness.h"
#include "register.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void seed_three(const char *db)
{
    CmdResult r;
    const char *a1[] = {"add", "--tag", "a", "--source", "human", "alpha first"};
    const char *a2[] = {"add", "--tag", "b", "--source", "agent", "beta second"};
    const char *a3[] = {"add", "--tag", "a", "--tag", "b", "--source", "tool", "gamma third"};
    r = run_remember(db, a1, 6, NULL);
    cmd_result_free(&r);
    /* ensure updated_at ordering: tiny sleep not portable enough; rely on
     * sequential updates via update --text later when implemented. Order of
     * add is still updated_at increasing. */
    r = run_remember(db, a2, 6, NULL);
    cmd_result_free(&r);
    r = run_remember(db, a3, 8, NULL);
    cmd_result_free(&r);
}

TEST(get_existing_json_envelope)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult g;
    const char *a[] = {"add", "--tag", "t", "get me"};
    const char *gargs[] = {"get", "--json", "1"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    g = run_remember(db, gargs, 3, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "\"version\":1");
    ASSERT_STR_CONTAINS(g.out, "\"entries\"");
    ASSERT_STR_CONTAINS(g.out, "get me");
    ASSERT_STR_CONTAINS(g.out, "\"id\":1");
    cmd_result_free(&g);
    free(db);
}

TEST(get_missing_exits_two)
{
    char *db = make_temp_db_path();
    const char *gargs[] = {"get", "99"};
    CmdResult g;
    ASSERT_TRUE(db != NULL);
    /* empty db still valid path */
    {
        const char *a[] = {"add", "placeholder"};
        CmdResult r = run_remember(db, a, 2, NULL);
        cmd_result_free(&r);
    }
    g = run_remember(db, gargs, 2, NULL);
    ASSERT_EQ_INT(g.exit_code, 2);
    cmd_result_free(&g);
    free(db);
}

TEST(get_human_shows_body)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult g;
    const char *a[] = {"add", "human get body"};
    const char *gargs[] = {"get", "1"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, 2, NULL);
    cmd_result_free(&r);
    g = run_remember(db, gargs, 2, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "human get body");
    cmd_result_free(&g);
    free(db);
}

TEST(list_empty_exits_zero)
{
    char *db = make_temp_db_path();
    const char *args[] = {"list"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    /* create empty db by listing before any add — or add+delete.
     * list on missing file should create schema and return empty. */
    r = run_remember(db, args, 1, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    free(db);
}

TEST(list_default_order_updated_at_desc)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *largs[] = {"list", "--json"};
    ASSERT_TRUE(db != NULL);
    seed_three(db);
    r = run_remember(db, largs, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    /* newest first: id 3 before id 1 in JSON text order of entries array */
    {
        const char *p3 = strstr(r.out, "\"id\":3");
        const char *p1 = strstr(r.out, "\"id\":1");
        ASSERT_TRUE(p3 != NULL && p1 != NULL);
        ASSERT_TRUE(p3 < p1);
    }
    cmd_result_free(&r);
    free(db);
}

TEST(list_filter_tag_and)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *largs[] = {"list", "--json", "--tag", "a", "--tag", "b"};
    ASSERT_TRUE(db != NULL);
    seed_three(db);
    r = run_remember(db, largs, 6, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "gamma third");
    ASSERT_STR_NOT_CONTAINS(r.out, "alpha first");
    ASSERT_STR_NOT_CONTAINS(r.out, "beta second");
    cmd_result_free(&r);
    free(db);
}

TEST(list_filter_source)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *largs[] = {"list", "--json", "--source", "agent"};
    ASSERT_TRUE(db != NULL);
    seed_three(db);
    r = run_remember(db, largs, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "beta second");
    ASSERT_STR_NOT_CONTAINS(r.out, "alpha first");
    cmd_result_free(&r);
    free(db);
}

TEST(list_limit)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *largs[] = {"list", "--json", "--limit", "2"};
    int count = 0;
    const char *p;
    ASSERT_TRUE(db != NULL);
    seed_three(db);
    r = run_remember(db, largs, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    p = r.out;
    while ((p = strstr(p, "\"id\":")) != NULL) {
        count++;
        p += 5;
    }
    ASSERT_EQ_INT(count, 2);
    cmd_result_free(&r);
    free(db);
}

TEST(list_limit_default_is_twenty)
{
    char *db = make_temp_db_path();
    CmdResult r;
    int i;
    int count = 0;
    const char *p;
    const char *largs[] = {"list", "--json"};
    ASSERT_TRUE(db != NULL);
    for (i = 0; i < 25; i++) {
        char body[64];
        const char *a[2];
        CmdResult ar;
        (void)snprintf(body, sizeof(body), "item number %d unique", i);
        a[0] = "add";
        a[1] = body;
        ar = run_remember(db, a, 2, NULL);
        cmd_result_free(&ar);
    }
    r = run_remember(db, largs, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    p = r.out;
    while ((p = strstr(p, "\"id\":")) != NULL) {
        count++;
        p += 5;
    }
    ASSERT_EQ_INT(count, 20);
    cmd_result_free(&r);
    free(db);
}

TEST(list_limit_zero_rejected)
{
    char *db = make_temp_db_path();
    const char *args[] = {"list", "--limit", "0"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(list_limit_over_hard_cap_rejected)
{
    char *db = make_temp_db_path();
    const char *args[] = {"list", "--limit", "1001"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(delete_existing)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult d;
    CmdResult g;
    const char *a[] = {"add", "to delete"};
    const char *dargs[] = {"delete", "1"};
    const char *gargs[] = {"get", "1"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, 2, NULL);
    cmd_result_free(&r);
    d = run_remember(db, dargs, 2, NULL);
    ASSERT_EQ_INT(d.exit_code, 0);
    cmd_result_free(&d);
    g = run_remember(db, gargs, 2, NULL);
    ASSERT_EQ_INT(g.exit_code, 2);
    cmd_result_free(&g);
    free(db);
}

TEST(delete_missing_exits_two)
{
    char *db = make_temp_db_path();
    const char *dargs[] = {"delete", "42"};
    CmdResult d;
    ASSERT_TRUE(db != NULL);
    {
        const char *a[] = {"add", "x"};
        CmdResult r = run_remember(db, a, 2, NULL);
        cmd_result_free(&r);
    }
    d = run_remember(db, dargs, 2, NULL);
    ASSERT_EQ_INT(d.exit_code, 2);
    cmd_result_free(&d);
    free(db);
}

TEST(delete_json_shape)
{
    char *db = make_temp_db_path();
    CmdResult r;
    CmdResult d;
    const char *a[] = {"add", "bye"};
    const char *dargs[] = {"delete", "--json", "1"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, 2, NULL);
    cmd_result_free(&r);
    d = run_remember(db, dargs, 3, NULL);
    ASSERT_EQ_INT(d.exit_code, 0);
    ASSERT_STR_CONTAINS(d.out, "\"version\":1");
    ASSERT_STR_CONTAINS(d.out, "\"action\":\"deleted\"");
    ASSERT_STR_CONTAINS(d.out, "\"count\":1");
    ASSERT_STR_CONTAINS(d.out, "\"entries\"");
    ASSERT_STR_CONTAINS(d.out, "bye");
    cmd_result_free(&d);
    free(db);
}

TEST(list_json_paging_fields)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *largs[] = {"list", "--json", "--limit", "2", "--offset", "0"};
    ASSERT_TRUE(db != NULL);
    seed_three(db);
    r = run_remember(db, largs, 6, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"offset\":0");
    ASSERT_STR_CONTAINS(r.out, "\"limit\":2");
    ASSERT_STR_CONTAINS(r.out, "\"count\":2");
    ASSERT_STR_CONTAINS(r.out, "\"total\":3");
    cmd_result_free(&r);
    free(db);
}

TEST(list_offset_pages)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *largs[] = {"list", "--json", "--limit", "1", "--offset", "1"};
    ASSERT_TRUE(db != NULL);
    seed_three(db);
    r = run_remember(db, largs, 6, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"count\":1");
    ASSERT_STR_CONTAINS(r.out, "\"total\":3");
    /* offset 1 of DESC order → second newest = id 2 */
    ASSERT_STR_CONTAINS(r.out, "\"id\":2");
    cmd_result_free(&r);
    free(db);
}

TEST(list_offset_negative_rejected)
{
    char *db = make_temp_db_path();
    const char *args[] = {"list", "--offset", "-1"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

void register_get_list_delete_tests(void)
{
    RUN_TEST(get_existing_json_envelope);
    RUN_TEST(get_missing_exits_two);
    RUN_TEST(get_human_shows_body);
    RUN_TEST(list_empty_exits_zero);
    RUN_TEST(list_default_order_updated_at_desc);
    RUN_TEST(list_filter_tag_and);
    RUN_TEST(list_filter_source);
    RUN_TEST(list_limit);
    RUN_TEST(list_limit_default_is_twenty);
    RUN_TEST(list_limit_zero_rejected);
    RUN_TEST(list_limit_over_hard_cap_rejected);
    RUN_TEST(delete_existing);
    RUN_TEST(delete_missing_exits_two);
    RUN_TEST(delete_json_shape);
    RUN_TEST(list_json_paging_fields);
    RUN_TEST(list_offset_pages);
    RUN_TEST(list_offset_negative_rejected);
}
