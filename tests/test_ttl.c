#include "harness.h"
#include "register.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int json_has_expires_after_updated(const char *json)
{
    const char *u = NULL;
    const char *e = NULL;

    if (json == NULL) {
        return 0;
    }
    u = strstr(json, "\"updated_at\":");
    e = strstr(json, "\"expires_at\":");
    return u != NULL && e != NULL && e > u;
}

TEST(add_json_expires_at_null)
{
    char *db = make_temp_db_path();
    const char *a[] = {"--json", "add", "durable"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_TRUE(json_has_expires_after_updated(r.out));
    ASSERT_STR_CONTAINS(r.out, "\"expires_at\":null");
    cmd_result_free(&r);
    free(db);
}

TEST(add_ttl_1h_is_future_canonical)
{
    char *db = make_temp_db_path();
    const char *a[] = {"--json", "add", "--ttl", "1h", "temp"};
    CmdResult r;
    const char *exp = NULL;
    const char *upd = NULL;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, 5, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_TRUE(json_has_expires_after_updated(r.out));
    exp = strstr(r.out, "\"expires_at\":\"");
    upd = strstr(r.out, "\"updated_at\":\"");
    ASSERT_TRUE(exp != NULL);
    ASSERT_TRUE(upd != NULL);
    if (exp == NULL || upd == NULL) {
        cmd_result_free(&r);
        free(db);
        return;
    }
    /* Canonical .mmmZ and later than updated_at (lexicographic ISO). */
    ASSERT_TRUE(strlen(exp) > 15U);
    ASSERT_TRUE(strstr(exp, "Z") != NULL);
    ASSERT_TRUE(strncmp(exp + 14, upd + 14, 24) > 0);
    cmd_result_free(&r);
    free(db);
}

TEST(add_expires_past_z_is_trash_only)
{
    char *db = make_temp_db_path();
    const char *a[] = {"--json", "add", "--expires", "2020-01-01T00:00:00Z", "old"};
    const char *list[] = {"--json", "list"};
    const char *trash[] = {"--json", "list", "--trash"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, 5, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "2020-01-01T00:00:00.000Z");
    cmd_result_free(&r);

    r = run_remember(db, list, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"total\":0");
    cmd_result_free(&r);

    r = run_remember(db, trash, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "old");
    cmd_result_free(&r);
    free(db);
}

TEST(add_expires_bare_second_pads_ms)
{
    char *db = make_temp_db_path();
    const char *a[] = {"--json", "add", "--expires", "2020-01-01T00:00:59Z", "pad"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, 5, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "2020-01-01T00:00:59.000Z");
    cmd_result_free(&r);
    free(db);
}

TEST(get_trash_of_active_exits_three)
{
    char *db = make_temp_db_path();
    const char *a[] = {"add", "live"};
    const char *g[] = {"get", "--trash", "1"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, g, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 3);
    ASSERT_STREQ(r.err, "remember: not_in_trash\n");
    ASSERT_TRUE(r.out == NULL || r.out[0] == '\0');
    cmd_result_free(&r);
    free(db);
}

TEST(update_trash_clear_expires_restores_cli)
{
    char *db = make_temp_db_path();
    const char *a[] = {"--json", "add", "--expires", "2020-01-01T00:00:00.000Z", "row"};
    const char *u[] = {"update", "--trash", "1", "--clear-expires"};
    const char *g[] = {"get", "1"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, 5, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, u, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, g, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    free(db);
}

TEST(update_trash_ttl_leaves_trash)
{
    char *db = make_temp_db_path();
    const char *a[] = {"add", "--expires", "2020-01-01T00:00:00Z", "row"};
    const char *u[] = {"--json", "update", "--trash", "1", "--ttl", "7d"};
    const char *g[] = {"get", "1"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, u, 6, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"expires_at\":\"");
    cmd_result_free(&r);
    r = run_remember(db, g, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    free(db);
}

TEST(keyless_add_revives_expired_cli)
{
    char *db = make_temp_db_path();
    const char *a1[] = {"add", "--expires", "2020-01-01T00:00:00Z", "--tag", "old", "same body"};
    const char *a2[] = {"--json", "add", "--tag", "wip", "same body"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a1, 6, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, a2, 5, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"action\":\"merged\"");
    ASSERT_STR_CONTAINS(r.out, "\"expires_at\":null");
    ASSERT_STR_CONTAINS(r.out, "\"id\":1");
    cmd_result_free(&r);
    free(db);
}

TEST(purge_trash_json_and_empty)
{
    char *db = make_temp_db_path();
    const char *a[] = {"add", "--expires", "2020-01-01T00:00:00Z", "gone"};
    const char *keep[] = {"add", "stay"};
    const char *p[] = {"--json", "purge-trash"};
    const char *p2[] = {"purge-trash"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, keep, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    r = run_remember(db, p, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"action\":\"deleted\"");
    ASSERT_STR_CONTAINS(r.out, "\"count\":1");
    ASSERT_STR_CONTAINS(r.out, "gone");
    cmd_result_free(&r);

    r = run_remember(db, p2, 1, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    trim_trailing_newlines(r.out);
    ASSERT_STREQ(r.out, "0");
    cmd_result_free(&r);
    free(db);
}

TEST(ttl_and_expires_mutex)
{
    char *db = make_temp_db_path();
    const char *a[] = {"add", "--ttl", "1h", "--expires", "2020-01-01T00:00:00Z", "x"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, 6, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(invalid_ttl_tokens)
{
    char *db = make_temp_db_path();
    const char *bad[][2] = {{"0", "0"},       {"7", "7"},   {"07d", "07d"}, {"7x", "7x"},
                            {"1.5d", "1.5d"}, {"1M", "1M"}, {"+7d", "+7d"}};
    size_t i = 0;
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        const char *a[] = {"add", "--ttl", bad[i][0], "x"};
        r = run_remember(db, a, 4, NULL);
        ASSERT_EQ_INT(r.exit_code, 1);
        cmd_result_free(&r);
    }
    free(db);
}

TEST(human_list_has_related_column)
{
    char *db = make_temp_db_path();
    const char *a[] = {"add", "--tag", "t", "hello list"};
    const char *l[] = {"list"};
    CmdResult r;
    int pipes = 0;
    const char *p = NULL;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, l, 1, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_NOT_CONTAINS(r.out, "expires");
    for (p = r.out != NULL ? r.out : ""; *p != '\0'; p++) {
        if (*p == '|') {
            pipes++;
        }
    }
    ASSERT_EQ_INT(pipes, 5);
    cmd_result_free(&r);
    free(db);
}

TEST(add_expires_date_only_local_eod)
{
    char *db = make_temp_db_path();
    const char *a[] = {"--json", "add", "--expires", "2099-12-31", "eod"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, 5, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"expires_at\":\"");
    ASSERT_STR_CONTAINS(r.out, ".999Z");
    cmd_result_free(&r);
    free(db);
}

TEST(add_expires_frac_pad_and_trunc)
{
    char *db = make_temp_db_path();
    const char *pad[] = {"--json", "add", "--expires", "2020-01-01T00:00:00.1Z", "p"};
    const char *trunc[] = {"--json", "add", "--expires", "2020-01-01T00:00:00.12345Z", "t"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, pad, 5, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "2020-01-01T00:00:00.100Z");
    cmd_result_free(&r);
    r = run_remember(db, trunc, 5, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "2020-01-01T00:00:00.123Z");
    cmd_result_free(&r);
    free(db);
}

TEST(purge_trash_rejects_args)
{
    char *db = make_temp_db_path();
    const char *flag[] = {"purge-trash", "--trash"};
    const char *pos[] = {"purge-trash", "nope"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, flag, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, pos, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(tags_and_search_trash_bin)
{
    char *db = make_temp_db_path();
    const char *a[] = {"add", "--expires", "2020-01-01T00:00:00Z", "--tag", "tmp", "gone"};
    const char *tags[] = {"--json", "tags", "--trash"};
    const char *search[] = {"--json", "search", "--trash", "gone"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, 6, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, tags, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "tmp");
    cmd_result_free(&r);
    r = run_remember(db, search, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "gone");
    cmd_result_free(&r);
    free(db);
}

TEST(add_ttl_minutes_and_weeks)
{
    char *db = make_temp_db_path();
    const char *m[] = {"--json", "add", "--ttl", "30m", "mins"};
    const char *w[] = {"--json", "add", "--ttl", "1w", "week"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, m, 5, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"expires_at\":\"");
    cmd_result_free(&r);
    r = run_remember(db, w, 5, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"expires_at\":\"");
    cmd_result_free(&r);
    free(db);
}

TEST(update_expires_on_active)
{
    char *db = make_temp_db_path();
    const char *a[] = {"add", "live"};
    const char *u[] = {"--json", "update", "1", "--expires", "2029-01-01T00:00:00Z"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, u, 5, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "2029-01-01T00:00:00.000Z");
    cmd_result_free(&r);
    free(db);
}

TEST(invalid_expires_tokens)
{
    char *db = make_temp_db_path();
    const char *bad[] = {"2020-01-01Z", "2020-01-01 00:00:00Z", "2020-01-01T00:00Z",
                         "2020-01-01T00:00:00+00:00", "2020-01-01T00:00:00."};
    size_t i = 0;
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        const char *a[] = {"add", "--expires", bad[i], "x"};
        r = run_remember(db, a, 4, NULL);
        ASSERT_EQ_INT(r.exit_code, 1);
        cmd_result_free(&r);
    }
    free(db);
}

TEST(help_mentions_ttl_and_exit_three)
{
    char *db = make_temp_db_path();
    const char *h[] = {"--help"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, h, 1, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "purge-trash");
    ASSERT_STR_CONTAINS(r.out, "  3");
    cmd_result_free(&r);
    {
        const char *hp[] = {"help", "purge-trash"};
        r = run_remember(db, hp, 2, NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STR_CONTAINS(r.out, "Permanently delete");
        cmd_result_free(&r);
    }
    free(db);
}

void register_ttl_tests(void)
{
    RUN_TEST(add_json_expires_at_null);
    RUN_TEST(add_ttl_1h_is_future_canonical);
    RUN_TEST(add_expires_past_z_is_trash_only);
    RUN_TEST(add_expires_bare_second_pads_ms);
    RUN_TEST(get_trash_of_active_exits_three);
    RUN_TEST(update_trash_clear_expires_restores_cli);
    RUN_TEST(update_trash_ttl_leaves_trash);
    RUN_TEST(keyless_add_revives_expired_cli);
    RUN_TEST(purge_trash_json_and_empty);
    RUN_TEST(ttl_and_expires_mutex);
    RUN_TEST(invalid_ttl_tokens);
    RUN_TEST(human_list_has_related_column);
    RUN_TEST(add_expires_date_only_local_eod);
    RUN_TEST(add_expires_frac_pad_and_trunc);
    RUN_TEST(purge_trash_rejects_args);
    RUN_TEST(tags_and_search_trash_bin);
    RUN_TEST(add_ttl_minutes_and_weeks);
    RUN_TEST(update_expires_on_active);
    RUN_TEST(invalid_expires_tokens);
    RUN_TEST(help_mentions_ttl_and_exit_three);
}
