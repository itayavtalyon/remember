#include "harness.h"
#include "register.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SYNC_ID_BUF = 40, VV_BUF = 96 };

// NOLINTBEGIN(readability-identifier-length,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

/* Copy the first JSON "sync_id":"…" value into out (NUL-terminated). */
static int json_copy_sync_id(const char *json, char *out, size_t outlen)
{
    const char *p = NULL;
    size_t i = 0;

    if (json == NULL || out == NULL || outlen < 2U) {
        return -1;
    }
    p = strstr(json, "\"sync_id\":\"");
    if (p == NULL) {
        return -1;
    }
    p += strlen("\"sync_id\":\"");
    while (i + 1U < outlen && p[i] != '\0' && p[i] != '"') {
        out[i] = p[i];
        i++;
    }
    if (p[i] != '"') {
        return -1;
    }
    out[i] = '\0';
    return 0;
}

static int json_copy_vv(const char *json, char *out, size_t outlen)
{
    const char *p = NULL;
    const char *end = NULL;
    size_t n = 0;

    if (json == NULL || out == NULL || outlen < 3U) {
        return -1;
    }
    p = strstr(json, "\"version_vector\":");
    if (p == NULL) {
        return -1;
    }
    p += strlen("\"version_vector\":");
    if (*p != '{') {
        return -1;
    }
    end = strchr(p, '}');
    if (end == NULL) {
        return -1;
    }
    n = (size_t)(end - p) + 1U;
    if (n >= outlen) {
        return -1;
    }
    memcpy(out, p, n);
    out[n] = '\0';
    return 0;
}

TEST(cli_get_by_sync_id)
{
    char *db = make_temp_db_path();
    CmdResult r;
    char sync[SYNC_ID_BUF];
    const char *a[] = {"--json", "add", "--key", "k:sync", "body"};
    const char *g_id[] = {"--json", "get", "1"};
    const char *g_sync[4];

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(json_copy_sync_id(r.out, sync, sizeof(sync)), 0);
    cmd_result_free(&r);

    r = run_remember(db, g_id, sizeof(g_id) / sizeof(g_id[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"body\":\"body\"");
    cmd_result_free(&r);

    g_sync[0] = "--json";
    g_sync[1] = "get";
    g_sync[2] = "--sync-id";
    g_sync[3] = sync;
    r = run_remember(db, g_sync, 4U, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"body\":\"body\"");
    ASSERT_STR_CONTAINS(r.out, sync);
    cmd_result_free(&r);
    free(db);
}

TEST(cli_locator_sync_id_mutex_and_invalid)
{
    char *db = make_temp_db_path();
    CmdResult r;
    char sync[SYNC_ID_BUF];
    const char *a[] = {"--json", "add", "x"};
    const char *both_id[] = {"get", "1", "--sync-id", "018f0000-0000-7000-8000-000000000001"};
    const char *both_key[] = {"get", "--key", "k", "--sync-id",
                              "018f0000-0000-7000-8000-000000000001"};
    const char *missing[] = {"get"};
    const char *bad_case[] = {"get", "--sync-id", "018F0000-0000-7000-8000-000000000001"};
    const char *bad_ver[] = {"get", "--sync-id", "018f0000-0000-4000-8000-000000000001"};
    const char *miss[] = {"get", "--sync-id", "018f0000-0000-7000-8000-000000000099"};

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(json_copy_sync_id(r.out, sync, sizeof(sync)), 0);
    cmd_result_free(&r);

    r = run_remember(db, both_id, sizeof(both_id) / sizeof(both_id[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "exactly one");
    cmd_result_free(&r);

    r = run_remember(db, both_key, sizeof(both_key) / sizeof(both_key[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "exactly one");
    cmd_result_free(&r);

    r = run_remember(db, missing, sizeof(missing) / sizeof(missing[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "missing");
    cmd_result_free(&r);

    r = run_remember(db, bad_case, sizeof(bad_case) / sizeof(bad_case[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "invalid sync-id");
    cmd_result_free(&r);

    r = run_remember(db, bad_ver, sizeof(bad_ver) / sizeof(bad_ver[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "invalid sync-id");
    cmd_result_free(&r);

    r = run_remember(db, miss, sizeof(miss) / sizeof(miss[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 2);
    cmd_result_free(&r);
    (void)sync;
    free(db);
}

TEST(cli_delete_update_rekey_by_sync_id)
{
    char *db = make_temp_db_path();
    CmdResult r;
    char sync[SYNC_ID_BUF];
    char vv1[VV_BUF];
    char vv2[VV_BUF];
    char vv3[VV_BUF];
    const char *a[] = {"--json", "add", "--key", "old", "v1"};
    const char *upd[6];
    const char *rekey[6];
    const char *del[4];
    const char *get_del[5];

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(json_copy_sync_id(r.out, sync, sizeof(sync)), 0);
    ASSERT_EQ_INT(json_copy_vv(r.out, vv1, sizeof(vv1)), 0);
    cmd_result_free(&r);

    upd[0] = "--json";
    upd[1] = "update";
    upd[2] = "--sync-id";
    upd[3] = sync;
    upd[4] = "--text";
    upd[5] = "v2";
    r = run_remember(db, upd, 6U, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"body\":\"v2\"");
    ASSERT_STR_CONTAINS(r.out, sync);
    ASSERT_EQ_INT(json_copy_vv(r.out, vv2, sizeof(vv2)), 0);
    ASSERT_TRUE(strcmp(vv1, vv2) != 0);
    cmd_result_free(&r);

    rekey[0] = "--json";
    rekey[1] = "rekey";
    rekey[2] = "--sync-id";
    rekey[3] = sync;
    rekey[4] = "--to-key";
    rekey[5] = "new";
    r = run_remember(db, rekey, 6U, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"key\":\"new\"");
    ASSERT_STR_CONTAINS(r.out, sync);
    ASSERT_EQ_INT(json_copy_vv(r.out, vv3, sizeof(vv3)), 0);
    ASSERT_TRUE(strcmp(vv2, vv3) != 0);
    cmd_result_free(&r);

    del[0] = "--json";
    del[1] = "delete";
    del[2] = "--sync-id";
    del[3] = sync;
    r = run_remember(db, del, 4U, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"bin\":\"deleted\"");
    ASSERT_STR_CONTAINS(r.out, sync);
    cmd_result_free(&r);

    get_del[0] = "--json";
    get_del[1] = "get";
    get_del[2] = "--deleted";
    get_del[3] = "--sync-id";
    get_del[4] = sync;
    r = run_remember(db, get_del, 5U, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"bin\":\"deleted\"");
    cmd_result_free(&r);
    free(db);
}

TEST(cli_link_from_to_sync_id)
{
    char *db = make_temp_db_path();
    CmdResult r;
    char sa[SYNC_ID_BUF];
    char sb[SYNC_ID_BUF];
    const char *a1[] = {"--json", "add", "alpha"};
    const char *a2[] = {"--json", "add", "beta"};
    const char *link[8];
    const char *sugar[3];
    const char *bad[7];

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a1, sizeof(a1) / sizeof(a1[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(json_copy_sync_id(r.out, sa, sizeof(sa)), 0);
    cmd_result_free(&r);
    r = run_remember(db, a2, sizeof(a2) / sizeof(a2[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(json_copy_sync_id(r.out, sb, sizeof(sb)), 0);
    cmd_result_free(&r);

    link[0] = "--json";
    link[1] = "link";
    link[2] = "--from-sync-id";
    link[3] = sa;
    link[4] = "--to-sync-id";
    link[5] = sb;
    link[6] = "--kind";
    link[7] = "cites";
    r = run_remember(db, link, 8U, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"action\":\"created\"");
    ASSERT_STR_CONTAINS(r.out, "\"type\":\"cites\"");
    cmd_result_free(&r);

    sugar[0] = "link";
    sugar[1] = sa;
    sugar[2] = sb;
    r = run_remember(db, sugar, 3U, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "invalid id");
    cmd_result_free(&r);

    bad[0] = "link";
    bad[1] = "--from";
    bad[2] = "1";
    bad[3] = "--from-sync-id";
    bad[4] = sa;
    bad[5] = "--to";
    bad[6] = "2";
    r = run_remember(db, bad, 7U, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "exactly one");
    cmd_result_free(&r);
    free(db);
}

TEST(cli_graph_deleted_end_needs_flag)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *a1[] = {"add", "live"};
    const char *a2[] = {"add", "soon-gone"};
    const char *d[] = {"delete", "2"};
    const char *noflag[] = {"link", "--from", "1", "--to", "2"};
    const char *with[] = {"--json", "link", "--deleted", "--from", "1", "--to", "2"};
    const char *rel[] = {"--json", "related", "1"};
    const char *rel_del[] = {"--json", "related", "--deleted", "1"};

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a1, sizeof(a1) / sizeof(a1[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, a2, sizeof(a2) / sizeof(a2[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, d, sizeof(d) / sizeof(d[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    r = run_remember(db, noflag, sizeof(noflag) / sizeof(noflag[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 2);
    cmd_result_free(&r);

    r = run_remember(db, with, sizeof(with) / sizeof(with[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"action\":\"created\"");
    cmd_result_free(&r);

    r = run_remember(db, rel, sizeof(rel) / sizeof(rel[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"count\":0");
    cmd_result_free(&r);

    r = run_remember(db, rel_del, sizeof(rel_del) / sizeof(rel_del[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"bin\":\"deleted\"");
    cmd_result_free(&r);
    free(db);
}

TEST(cli_link_unlink_bump_vv_noop_unlink_does_not)
{
    char *db = make_temp_db_path();
    CmdResult r;
    char vv_a0[VV_BUF];
    char vv_b0[VV_BUF];
    char vv_a1[VV_BUF];
    char vv_b1[VV_BUF];
    char vv_a2[VV_BUF];
    char vv_b2[VV_BUF];
    char vv_a3[VV_BUF];
    const char *a1[] = {"--json", "add", "a"};
    const char *a2[] = {"--json", "add", "b"};
    const char *ga[] = {"--json", "get", "1"};
    const char *gb[] = {"--json", "get", "2"};
    const char *link[] = {"link", "1", "2"};
    const char *unlink[] = {"unlink", "1", "2"};
    const char *noop[] = {"unlink", "1", "2", "--kind", "related"};

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a1, sizeof(a1) / sizeof(a1[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, a2, sizeof(a2) / sizeof(a2[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    r = run_remember(db, ga, sizeof(ga) / sizeof(ga[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(json_copy_vv(r.out, vv_a0, sizeof(vv_a0)), 0);
    cmd_result_free(&r);
    r = run_remember(db, gb, sizeof(gb) / sizeof(gb[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(json_copy_vv(r.out, vv_b0, sizeof(vv_b0)), 0);
    cmd_result_free(&r);

    r = run_remember(db, link, sizeof(link) / sizeof(link[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    r = run_remember(db, ga, sizeof(ga) / sizeof(ga[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(json_copy_vv(r.out, vv_a1, sizeof(vv_a1)), 0);
    ASSERT_TRUE(strcmp(vv_a0, vv_a1) != 0);
    cmd_result_free(&r);
    r = run_remember(db, gb, sizeof(gb) / sizeof(gb[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(json_copy_vv(r.out, vv_b1, sizeof(vv_b1)), 0);
    ASSERT_TRUE(strcmp(vv_b0, vv_b1) != 0);
    cmd_result_free(&r);

    r = run_remember(db, unlink, sizeof(unlink) / sizeof(unlink[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    r = run_remember(db, ga, sizeof(ga) / sizeof(ga[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(json_copy_vv(r.out, vv_a2, sizeof(vv_a2)), 0);
    ASSERT_TRUE(strcmp(vv_a1, vv_a2) != 0);
    cmd_result_free(&r);
    r = run_remember(db, gb, sizeof(gb) / sizeof(gb[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(json_copy_vv(r.out, vv_b2, sizeof(vv_b2)), 0);
    ASSERT_TRUE(strcmp(vv_b1, vv_b2) != 0);
    cmd_result_free(&r);

    r = run_remember(db, noop, sizeof(noop) / sizeof(noop[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, ga, sizeof(ga) / sizeof(ga[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(json_copy_vv(r.out, vv_a3, sizeof(vv_a3)), 0);
    ASSERT_STREQ(vv_a3, vv_a2);
    cmd_result_free(&r);
    free(db);
}

TEST(cli_graph_deleted_via_key_and_sync_id)
{
    char *db = make_temp_db_path();
    CmdResult r;
    char sync[SYNC_ID_BUF];
    const char *a1[] = {"--json", "add", "--key", "livek", "live"};
    const char *a2[] = {"--json", "add", "--key", "gonek", "gone"};
    const char *d[] = {"delete", "--key", "gonek"};
    const char *by_key[] = {"--json", "link",     "--deleted", "--from-key",
                            "livek",  "--to-key", "gonek"};
    const char *rel_sync[4];
    const char *miss_sync[] = {"related", "--sync-id"};
    const char *hard[5];
    const char *upd_miss[] = {"update", "--sync-id", "018f0000-0000-7000-8000-000000000099",
                              "--text", "x"};
    const char *rekey_miss[] = {"rekey", "--sync-id", "018f0000-0000-7000-8000-000000000099",
                                "--to-key", "z"};

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a1, sizeof(a1) / sizeof(a1[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, a2, sizeof(a2) / sizeof(a2[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(json_copy_sync_id(r.out, sync, sizeof(sync)), 0);
    cmd_result_free(&r);
    r = run_remember(db, d, sizeof(d) / sizeof(d[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    r = run_remember(db, by_key, sizeof(by_key) / sizeof(by_key[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"action\":\"created\"");
    cmd_result_free(&r);

    {
        const char *by_sync[6];
        by_sync[0] = "link";
        by_sync[1] = "--deleted";
        by_sync[2] = "--from-key";
        by_sync[3] = "livek";
        by_sync[4] = "--to-sync-id";
        by_sync[5] = sync;
        r = run_remember(db, by_sync, 6U, NULL);
        ASSERT_EQ_INT(r.exit_code, 0); /* merge existing related */
        cmd_result_free(&r);
    }

    rel_sync[0] = "--json";
    rel_sync[1] = "related";
    rel_sync[2] = "--sync-id";
    rel_sync[3] = sync;
    r = run_remember(db, rel_sync, 4U, NULL);
    ASSERT_EQ_INT(r.exit_code, 2); /* deleted subject without --deleted */
    cmd_result_free(&r);

    rel_sync[0] = "--json";
    rel_sync[1] = "related";
    rel_sync[2] = "--deleted";
    /* rebuild: related --deleted --sync-id SYNC */
    {
        const char *rel_del_sync[5];
        rel_del_sync[0] = "--json";
        rel_del_sync[1] = "related";
        rel_del_sync[2] = "--deleted";
        rel_del_sync[3] = "--sync-id";
        rel_del_sync[4] = sync;
        r = run_remember(db, rel_del_sync, 5U, NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
    }

    { /* related --deleted --key resolves the deleted subject by key */
        const char *rel_del_key[] = {"--json", "related", "--deleted", "--key", "gonek"};
        r = run_remember(db, rel_del_key, sizeof(rel_del_key) / sizeof(rel_del_key[0]), NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
    }

    r = run_remember(db, miss_sync, sizeof(miss_sync) / sizeof(miss_sync[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "missing value for --sync-id");
    cmd_result_free(&r);

    hard[0] = "--json";
    hard[1] = "delete";
    hard[2] = "--deleted";
    hard[3] = "--sync-id";
    hard[4] = sync;
    r = run_remember(db, hard, 5U, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"action\":\"deleted\"");
    cmd_result_free(&r);

    r = run_remember(db, upd_miss, sizeof(upd_miss) / sizeof(upd_miss[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 2);
    cmd_result_free(&r);
    r = run_remember(db, rekey_miss, sizeof(rekey_miss) / sizeof(rekey_miss[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 2);
    cmd_result_free(&r);
    free(db);
}

void register_sync_locators_tests(void)
{
    RUN_TEST(cli_get_by_sync_id);
    RUN_TEST(cli_locator_sync_id_mutex_and_invalid);
    RUN_TEST(cli_delete_update_rekey_by_sync_id);
    RUN_TEST(cli_link_from_to_sync_id);
    RUN_TEST(cli_graph_deleted_end_needs_flag);
    RUN_TEST(cli_link_unlink_bump_vv_noop_unlink_does_not);
    RUN_TEST(cli_graph_deleted_via_key_and_sync_id);
}

// NOLINTEND(readability-identifier-length,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
