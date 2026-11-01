#include "harness.h"
#include "register.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SYNC_ID_BUF = 40, VV_BUF = 96 };

// NOLINTBEGIN(readability-identifier-length,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

/* Copy the first JSON "id":"…" value into out (NUL-terminated). */
static int json_copy_sync_id(const char *json, char *out, size_t outlen)
{
    const char *p = NULL;
    size_t i = 0;

    if (json == NULL || out == NULL || outlen < 2U) {
        return -1;
    }
    p = strstr(json, "\"id\":\"");
    if (p == NULL) {
        return -1;
    }
    p += strlen("\"id\":\"");
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

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(json_copy_sync_id(r.out, sync, sizeof(sync)), 0);
    cmd_result_free(&r);

    r = run_remember(db, g_id, sizeof(g_id) / sizeof(g_id[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"body\":\"body\"");
    ASSERT_STR_CONTAINS(r.out, sync);
    ASSERT_STR_NOT_CONTAINS(r.out, "sync_id");
    cmd_result_free(&r);
    free(db);
}

TEST(cli_locator_sync_id_mutex_and_invalid)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *a[] = {"--json", "add", "--key", "k", "x"};
    const char *both[] = {"get", "1", "--key", "k"};
    const char *missing[] = {"get"};
    const char *link_both[] = {"link", "--from", "1", "--from-key", "k", "--to", "1"};
    const char *rel_both[] = {"related", "1", "--key", "k"};
    const char *rel_missing[] = {"related"};

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    r = run_remember(db, both, sizeof(both) / sizeof(both[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "exactly one");
    cmd_result_free(&r);

    r = run_remember(db, missing, sizeof(missing) / sizeof(missing[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "missing");
    cmd_result_free(&r);

    r = run_remember(db, link_both, sizeof(link_both) / sizeof(link_both[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "exactly one");
    cmd_result_free(&r);

    r = run_remember(db, rel_both, sizeof(rel_both) / sizeof(rel_both[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "exactly one");
    cmd_result_free(&r);

    r = run_remember(db, rel_missing, sizeof(rel_missing) / sizeof(rel_missing[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "missing");
    cmd_result_free(&r);
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
    const char *upd[] = {"--json", "update", "1", "--text", "v2"};
    const char *rekey[] = {"--json", "rekey", "1", "--to-key", "new"};
    const char *del[] = {"--json", "delete", "1"};
    const char *get_del[] = {"--json", "get", "--deleted", "1"};

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(json_copy_sync_id(r.out, sync, sizeof(sync)), 0);
    ASSERT_EQ_INT(json_copy_vv(r.out, vv1, sizeof(vv1)), 0);
    cmd_result_free(&r);

    r = run_remember(db, upd, sizeof(upd) / sizeof(upd[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"body\":\"v2\"");
    ASSERT_STR_CONTAINS(r.out, sync);
    ASSERT_EQ_INT(json_copy_vv(r.out, vv2, sizeof(vv2)), 0);
    ASSERT_TRUE(strcmp(vv1, vv2) != 0);
    cmd_result_free(&r);

    r = run_remember(db, rekey, sizeof(rekey) / sizeof(rekey[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"key\":\"new\"");
    ASSERT_STR_CONTAINS(r.out, sync);
    ASSERT_EQ_INT(json_copy_vv(r.out, vv3, sizeof(vv3)), 0);
    ASSERT_TRUE(strcmp(vv2, vv3) != 0);
    cmd_result_free(&r);

    r = run_remember(db, del, sizeof(del) / sizeof(del[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"bin\":\"deleted\"");
    ASSERT_STR_CONTAINS(r.out, sync);
    cmd_result_free(&r);

    r = run_remember(db, get_del, sizeof(get_del) / sizeof(get_del[0]), NULL);
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
    const char *link[] = {"--json", "link", "--from", "1", "--to", "2", "--kind", "cites"};
    const char *sugar[4];
    const char *bad[] = {"link", "--from", "1", "--from-key", "k", "--to", "2"};

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a1, sizeof(a1) / sizeof(a1[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(json_copy_sync_id(r.out, sa, sizeof(sa)), 0);
    cmd_result_free(&r);
    r = run_remember(db, a2, sizeof(a2) / sizeof(a2[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(json_copy_sync_id(r.out, sb, sizeof(sb)), 0);
    cmd_result_free(&r);

    r = run_remember(db, link, sizeof(link) / sizeof(link[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"action\":\"created\"");
    ASSERT_STR_CONTAINS(r.out, "\"type\":\"cites\"");
    cmd_result_free(&r);

    /* Sugar has no --kind, so this is a related edge, not a second cites. */
    sugar[0] = "--json";
    sugar[1] = "link";
    sugar[2] = sa;
    sugar[3] = sb;
    r = run_remember(db, sugar, 4U, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"action\":\"created\"");
    ASSERT_STR_CONTAINS(r.out, "\"type\":\"related\"");
    cmd_result_free(&r);

    r = run_remember(db, bad, sizeof(bad) / sizeof(bad[0]), NULL);
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
    const char *a1[] = {"--json", "add", "--key", "livek", "live"};
    const char *a2[] = {"--json", "add", "--key", "gonek", "gone"};
    const char *d[] = {"delete", "--key", "gonek"};
    const char *by_key[] = {"--json", "link",     "--deleted", "--from-key",
                            "livek",  "--to-key", "gonek"};
    const char *again[] = {"link", "--deleted", "--from-key", "livek", "--to-key", "gonek"};
    const char *rel_gone[] = {"related", "--key", "gonek"};
    const char *unknown[] = {"related", "--sync-id", "1"};
    const char *hard[] = {"--json", "delete", "--deleted", "--key", "gonek"};
    const char *upd_miss[] = {"update", "99", "--text", "x"};
    const char *rekey_miss[] = {"rekey", "99", "--to-key", "z"};

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

    r = run_remember(db, by_key, sizeof(by_key) / sizeof(by_key[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"action\":\"created\"");
    cmd_result_free(&r);

    r = run_remember(db, again, sizeof(again) / sizeof(again[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0); /* merge existing related */
    cmd_result_free(&r);

    r = run_remember(db, rel_gone, sizeof(rel_gone) / sizeof(rel_gone[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 2); /* deleted subject without --deleted */
    cmd_result_free(&r);

    { /* related --deleted --key resolves the deleted subject by key */
        const char *rel_del_key[] = {"--json", "related", "--deleted", "--key", "gonek"};
        r = run_remember(db, rel_del_key, sizeof(rel_del_key) / sizeof(rel_del_key[0]), NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
    }

    r = run_remember(db, unknown, sizeof(unknown) / sizeof(unknown[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "unknown option '--sync-id'");
    cmd_result_free(&r);

    r = run_remember(db, hard, sizeof(hard) / sizeof(hard[0]), NULL);
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
