#include "harness.h"
#include "register.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    SQL_BUFSIZE = 160,
    UUID_CANON_LEN = 36,
    UUID_DASH_0 = 8,
    UUID_DASH_1 = 13,
    UUID_DASH_2 = 18,
    UUID_DASH_3 = 23,
    UUID_VER_CHAR = 14,
    GET_ARGV_MAX = 5
};

static void mark_deleted(const char *db, long long id)
{
    char sql[SQL_BUFSIZE];

    (void)snprintf(sql, sizeof(sql),
                   "UPDATE entries SET deleted_at='2020-01-01T00:00:00.000Z' WHERE id=%lld;", id);
    free(harness_sqlite_query_line(db, sql));
}

static const char *first_obj_after(const char *json, const char *key)
{
    const char *p = NULL;

    if (json == NULL || key == NULL) {
        return NULL;
    }
    p = strstr(json, key);
    if (p == NULL) {
        return NULL;
    }
    return strchr(p, '{');
}

static int keys_in_order(const char *obj, const char *const *keys, size_t n)
{
    const char *p = obj;
    size_t i = 0;

    if (p == NULL) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        const char *hit = strstr(p, keys[i]);
        if (hit == NULL) {
            return 0;
        }
        p = hit + strlen(keys[i]);
    }
    return 1;
}

static int uuid_v7_at(const char *s)
{
    size_t i = 0;

    if (s == NULL) {
        return 0;
    }
    if (strlen(s) < UUID_CANON_LEN) {
        return 0;
    }
    for (i = 0; i < UUID_CANON_LEN; i++) {
        char c = s[i];
        if (i == (size_t)UUID_DASH_0 || i == (size_t)UUID_DASH_1 || i == (size_t)UUID_DASH_2 ||
            i == (size_t)UUID_DASH_3) {
            if (c != '-') {
                return 0;
            }
            continue;
        }
        if ((c < '0' || c > '9') && (c < 'a' || c > 'f')) {
            return 0;
        }
    }
    return s[UUID_VER_CHAR] == '7';
}

static int json_sync_id_is_v7(const char *json)
{
    const char *p = NULL;

    if (json == NULL) {
        return 0;
    }
    p = strstr(json, "\"sync_id\":\"");
    if (p == NULL) {
        return 0;
    }
    p += strlen("\"sync_id\":\"");
    return uuid_v7_at(p);
}

TEST(json_entry_field_order_bin_always_enum)
{
    static const char *const k_fields[] = {
        "\"id\":",         "\"sync_id\":",    "\"key\":",        "\"body\":",
        "\"tags\":",       "\"source\":",     "\"created_at\":", "\"updated_at\":",
        "\"expires_at\":", "\"deleted_at\":", "\"bin\":",        "\"version_vector\":",
        "\"links\":"};
    char *db = make_temp_db_path();
    const char *a[] = {"add", "--key", "k:live", "hello"};
    const char *g[] = {"--json", "get", "1"};
    CmdResult r;
    const char *entry = NULL;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    r = run_remember(db, g, sizeof(g) / sizeof(g[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    entry = first_obj_after(r.out, "\"entries\":");
    ASSERT_TRUE(keys_in_order(entry, k_fields, sizeof(k_fields) / sizeof(k_fields[0])));
    ASSERT_TRUE(json_sync_id_is_v7(r.out));
    ASSERT_STR_CONTAINS(r.out, "\"bin\":\"live\"");
    ASSERT_STR_CONTAINS(r.out, "\"deleted_at\":null");
    ASSERT_STR_CONTAINS(r.out, "\"version_vector\":{");
    ASSERT_STR_NOT_CONTAINS(r.out, "\"trash\"");
    cmd_result_free(&r);
    free(db);
}

TEST(json_expired_and_deleted_bins)
{
    char *db = make_temp_db_path();
    const char *exp[] = {"add", "--expires", "2020-01-01T00:00:00Z", "gone"};
    const char *del[] = {"add", "wiped"};
    const char *gexp[] = {"--json", "get", "--expired", "1"};
    const char *gdel[] = {"--json", "get", "--deleted", "2"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, exp, sizeof(exp) / sizeof(exp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, del, sizeof(del) / sizeof(del[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    mark_deleted(db, 2);

    r = run_remember(db, gexp, sizeof(gexp) / sizeof(gexp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"bin\":\"expired\"");
    ASSERT_STR_NOT_CONTAINS(r.out, "\"trash\"");
    cmd_result_free(&r);

    r = run_remember(db, gdel, sizeof(gdel) / sizeof(gdel[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"bin\":\"deleted\"");
    ASSERT_STR_CONTAINS(r.out, "\"deleted_at\":\"");
    ASSERT_STR_NOT_CONTAINS(r.out, "\"trash\"");
    cmd_result_free(&r);
    free(db);
}

TEST(json_stubs_sync_id_bin_no_trash)
{
    static const char *const k_stub[] = {
        "\"id\":", "\"sync_id\":", "\"key\":", "\"type\":", "\"bin\":", "\"preview\":"};
    char *db = make_temp_db_path();
    const char *keep[] = {"add", "keep"};
    const char *exp[] = {"add", "--expires", "2020-01-01T00:00:00Z", "gone-n"};
    const char *link[] = {"link", "1", "2"};
    const char *g[] = {"--json", "get", "1"};
    CmdResult r;
    const char *stub = NULL;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, keep, sizeof(keep) / sizeof(keep[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, exp, sizeof(exp) / sizeof(exp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, link, sizeof(link) / sizeof(link[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    r = run_remember(db, g, sizeof(g) / sizeof(g[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    stub = first_obj_after(r.out, "\"links\":");
    ASSERT_TRUE(keys_in_order(stub, k_stub, sizeof(k_stub) / sizeof(k_stub[0])));
    ASSERT_TRUE(json_sync_id_is_v7(stub));
    ASSERT_STR_CONTAINS(r.out, "\"bin\":\"expired\"");
    ASSERT_STR_NOT_CONTAINS(r.out, "\"trash\"");
    cmd_result_free(&r);
    free(db);
}

TEST(trash_alias_expired_deprecation_skips_deleted)
{
    char *db = make_temp_db_path();
    const char *exp[] = {"add", "--expires", "2020-01-01T00:00:00Z", "old"};
    const char *live[] = {"add", "fresh"};
    const char *lst_trash[] = {"--json", "list", "--trash"};
    const char *lst_exp[] = {"--json", "list", "--expired"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, exp, sizeof(exp) / sizeof(exp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, live, sizeof(live) / sizeof(live[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    mark_deleted(db, 2);

    r = run_remember(db, lst_trash, sizeof(lst_trash) / sizeof(lst_trash[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.err, "remember: --trash is deprecated; use --expired");
    ASSERT_STR_CONTAINS(r.out, "old");
    ASSERT_STR_NOT_CONTAINS(r.out, "fresh");
    cmd_result_free(&r);

    r = run_remember(db, lst_exp, sizeof(lst_exp) / sizeof(lst_exp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_TRUE(r.err == NULL || strstr(r.err, "deprecated") == NULL);
    ASSERT_STR_CONTAINS(r.out, "old");
    ASSERT_STR_NOT_CONTAINS(r.out, "fresh");
    cmd_result_free(&r);
    free(db);
}

TEST(expired_deleted_mutex)
{
    char *db = make_temp_db_path();
    const char *a[] = {"add", "x"};
    const char *both[] = {"list", "--expired", "--deleted"};
    const char *alias[] = {"get", "--trash", "--deleted", "1"};
    const char *upd_both[] = {"update", "--expired", "--deleted", "1", "--text", "z"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    r = run_remember(db, both, sizeof(both) / sizeof(both[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "cannot combine --expired and --deleted");
    cmd_result_free(&r);

    r = run_remember(db, alias, sizeof(alias) / sizeof(alias[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "cannot combine --expired and --deleted");
    ASSERT_STR_CONTAINS(r.err, "remember: --trash is deprecated; use --expired");
    cmd_result_free(&r);

    r = run_remember(db, upd_both, sizeof(upd_both) / sizeof(upd_both[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "cannot combine --expired and --deleted");
    cmd_result_free(&r);
    free(db);
}

TEST(exit3_matrix_round7)
{
    char *db = make_temp_db_path();
    const char *live[] = {"add", "live"};
    const char *exp[] = {"add", "--expires", "2020-01-01T00:00:00Z", "expired"};
    const char *del[] = {"add", "deleted"};
    CmdResult r;
    size_t i = 0;
    /* Looked-in flag, locator id, want exit, stderr token (NULL if ok). */
    static const struct {
        const char *flag;
        const char *id;
        const char *token;
        int exit_code;
        char pad_[4]; /* -Wpadded */
    } k_cases[] = {
        {.flag = NULL, .id = "1", .token = NULL, .exit_code = 0, .pad_ = {0, 0, 0, 0}},
        {.flag = NULL, .id = "2", .token = "expired", .exit_code = 3, .pad_ = {0, 0, 0, 0}},
        {.flag = NULL, .id = "3", .token = "deleted", .exit_code = 3, .pad_ = {0, 0, 0, 0}},
        {.flag = "--expired",
         .id = "1",
         .token = "not_expired",
         .exit_code = 3,
         .pad_ = {0, 0, 0, 0}},
        {.flag = "--expired", .id = "2", .token = NULL, .exit_code = 0, .pad_ = {0, 0, 0, 0}},
        {.flag = "--expired", .id = "3", .token = "deleted", .exit_code = 3, .pad_ = {0, 0, 0, 0}},
        {.flag = "--deleted",
         .id = "1",
         .token = "not_deleted",
         .exit_code = 3,
         .pad_ = {0, 0, 0, 0}},
        {.flag = "--deleted", .id = "2", .token = "expired", .exit_code = 3, .pad_ = {0, 0, 0, 0}},
        {.flag = "--deleted", .id = "3", .token = NULL, .exit_code = 0, .pad_ = {0, 0, 0, 0}},
    };

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, live, sizeof(live) / sizeof(live[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, exp, sizeof(exp) / sizeof(exp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, del, sizeof(del) / sizeof(del[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    mark_deleted(db, 3);

    for (i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        const char *argv[GET_ARGV_MAX];
        size_t n = 0U;
        argv[n++] = "get";
        if (k_cases[i].flag != NULL) {
            argv[n++] = k_cases[i].flag;
        }
        argv[n++] = k_cases[i].id;
        r = run_remember(db, argv, n, NULL);
        ASSERT_EQ_INT(r.exit_code, k_cases[i].exit_code);
        if (k_cases[i].token != NULL) {
            char want[SQL_BUFSIZE];
            (void)snprintf(want, sizeof(want), "remember: %s\n", k_cases[i].token);
            ASSERT_STREQ(r.err, want);
        }
        cmd_result_free(&r);
    }
    free(db);
}

TEST(default_list_omits_expired_and_deleted)
{
    char *db = make_temp_db_path();
    const char *live[] = {"add", "keep"};
    const char *exp[] = {"add", "--expires", "2020-01-01T00:00:00Z", "old"};
    const char *del[] = {"add", "gone"};
    const char *lst[] = {"--json", "list"};
    const char *lst_e[] = {"--json", "list", "--expired"};
    const char *lst_d[] = {"--json", "list", "--deleted"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, live, sizeof(live) / sizeof(live[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, exp, sizeof(exp) / sizeof(exp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, del, sizeof(del) / sizeof(del[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    mark_deleted(db, 3);

    r = run_remember(db, lst, sizeof(lst) / sizeof(lst[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "keep");
    ASSERT_STR_NOT_CONTAINS(r.out, "old");
    ASSERT_STR_NOT_CONTAINS(r.out, "gone");
    cmd_result_free(&r);

    r = run_remember(db, lst_e, sizeof(lst_e) / sizeof(lst_e[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "old");
    ASSERT_STR_NOT_CONTAINS(r.out, "keep");
    ASSERT_STR_NOT_CONTAINS(r.out, "gone");
    cmd_result_free(&r);

    r = run_remember(db, lst_d, sizeof(lst_d) / sizeof(lst_d[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "gone");
    ASSERT_STR_NOT_CONTAINS(r.out, "keep");
    ASSERT_STR_NOT_CONTAINS(r.out, "old");
    cmd_result_free(&r);
    free(db);
}

TEST(human_expired_and_deleted_neighbor_marks)
{
    char *db = make_temp_db_path();
    const char *hub[] = {"add", "hub"};
    const char *exp[] = {"add", "--expires", "2020-01-01T00:00:00Z", "old-n"};
    const char *del[] = {"add", "dead-n"};
    const char *l2[] = {"link", "1", "2"};
    const char *l3[] = {"link", "1", "3"};
    const char *lst[] = {"list"};
    const char *hget[] = {"get", "1"};
    const char *rel_d[] = {"related", "--deleted", "1"};
    const char *jrel[] = {"--json", "related", "1"};
    const char *jrel_d[] = {"--json", "related", "--deleted", "1"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, hub, sizeof(hub) / sizeof(hub[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, exp, sizeof(exp) / sizeof(exp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, del, sizeof(del) / sizeof(del[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, l2, sizeof(l2) / sizeof(l2[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, l3, sizeof(l3) / sizeof(l3[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    mark_deleted(db, 3);

    r = run_remember(db, lst, sizeof(lst) / sizeof(lst[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "2[expired]");
    ASSERT_STR_NOT_CONTAINS(r.out, "[trash]");
    ASSERT_STR_NOT_CONTAINS(r.out, "3[deleted]");
    cmd_result_free(&r);

    r = run_remember(db, hget, sizeof(hget) / sizeof(hget[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "[expired]");
    ASSERT_STR_NOT_CONTAINS(r.out, "[trash]");
    ASSERT_STR_NOT_CONTAINS(r.out, "[deleted]");
    cmd_result_free(&r);

    r = run_remember(db, jrel, sizeof(jrel) / sizeof(jrel[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"bin\":\"expired\"");
    ASSERT_STR_NOT_CONTAINS(r.out, "\"bin\":\"deleted\"");
    ASSERT_STR_NOT_CONTAINS(r.out, "\"trash\"");
    cmd_result_free(&r);

    r = run_remember(db, rel_d, sizeof(rel_d) / sizeof(rel_d[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "[deleted]");
    cmd_result_free(&r);

    r = run_remember(db, jrel_d, sizeof(jrel_d) / sizeof(jrel_d[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"bin\":\"deleted\"");
    ASSERT_STR_CONTAINS(r.out, "\"sync_id\":\"");
    ASSERT_STR_NOT_CONTAINS(r.out, "\"trash\"");
    cmd_result_free(&r);
    free(db);
}

void register_sync_output_tests(void)
{
    RUN_TEST(json_entry_field_order_bin_always_enum);
    RUN_TEST(json_expired_and_deleted_bins);
    RUN_TEST(json_stubs_sync_id_bin_no_trash);
    RUN_TEST(trash_alias_expired_deprecation_skips_deleted);
    RUN_TEST(expired_deleted_mutex);
    RUN_TEST(exit3_matrix_round7);
    RUN_TEST(default_list_omits_expired_and_deleted);
    RUN_TEST(human_expired_and_deleted_neighbor_marks);
}
