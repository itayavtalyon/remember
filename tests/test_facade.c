#include "harness.h"
#include "register.h"
#include "remember_app.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Facade parity: remember_run (in-process, what the GUI links) must produce the
 * same bytes as the CLI subprocess for the same argv against the same DB. Both
 * paths share output.c and the run() pipeline, so this guards the stream
 * injection (appio) and any future drift.
 *
 * Timestamps are the one field that legitimately differs between two separate
 * writes, so mutation cases mask created_at/updated_at values before comparing;
 * read cases compare exact bytes (stored timestamps are identical across reads).
 */

/* Run remember_run with argv {"remember","--db",db,<cmd...>} into heap buffers. */
static int facade_run(const char *db, const char *const *cmd, size_t ncmd, char **out, char **err)
{
    const char *argv[32];
    size_t n = 0U;
    size_t i;
    char *obuf = NULL;
    char *ebuf = NULL;
    size_t olen = 0U;
    size_t elen = 0U;
    FILE *of;
    FILE *ef;
    int rc;

    argv[n++] = "remember";
    argv[n++] = "--db";
    argv[n++] = db;
    for (i = 0; i < ncmd; i++) {
        argv[n++] = cmd[i];
    }
    of = open_memstream(&obuf, &olen);
    ef = open_memstream(&ebuf, &elen);
    rc = remember_run((int)n, (char *const *)argv, of, ef);
    (void)fclose(of);
    (void)fclose(ef);
    *out = obuf;
    *err = ebuf;
    return rc;
}

/* Overwrite a "<field>":"VALUE" value with X's so timestamps do not break diff. */
static void mask_field(char *s, const char *field)
{
    char *p = s;
    size_t flen = strlen(field);

    while ((p = strstr(p, field)) != NULL) {
        p += flen;
        while (*p != '\0' && *p != '"') {
            *p = 'X';
            p++;
        }
    }
}

static void mask_timestamps(char *s)
{
    if (s == NULL) {
        return;
    }
    mask_field(s, "\"created_at\":\"");
    mask_field(s, "\"updated_at\":\"");
}

/* Assert remember_run stdout/stderr/exit byte-match the CLI for a read command. */
static void assert_read_parity(const char *db, const char *const *cmd, size_t ncmd)
{
    CmdResult sub = run_remember(db, cmd, ncmd, NULL);
    char *fout = NULL;
    char *ferr = NULL;
    int frc = facade_run(db, cmd, ncmd, &fout, &ferr);

    ASSERT_EQ_INT(frc, sub.exit_code);
    ASSERT_STREQ(fout, sub.out);
    ASSERT_STREQ(ferr, sub.err);
    free(fout);
    free(ferr);
    cmd_result_free(&sub);
}

static void seed_three(const char *db)
{
    const char *a1[] = {"add", "--tag", "a", "--source", "human", "alpha first"};
    const char *a2[] = {"add", "--tag", "b", "--source", "agent", "beta second"};
    const char *a3[] = {"add", "--key", "k", "--tag", "a", "gamma third"};
    CmdResult r;

    r = run_remember(db, a1, 6, NULL);
    cmd_result_free(&r);
    r = run_remember(db, a2, 6, NULL);
    cmd_result_free(&r);
    r = run_remember(db, a3, 6, NULL);
    cmd_result_free(&r);
}

TEST(facade_list_matches_cli)
{
    char *db = make_temp_db_path();
    const char *cmd[] = {"--json", "list"};
    ASSERT_TRUE(db != NULL);
    seed_three(db);
    assert_read_parity(db, cmd, 2);
    free(db);
}

TEST(facade_search_matches_cli)
{
    char *db = make_temp_db_path();
    const char *cmd[] = {"--json", "search", "beta"};
    ASSERT_TRUE(db != NULL);
    seed_three(db);
    assert_read_parity(db, cmd, 3);
    free(db);
}

TEST(facade_search_bad_query_matches_cli)
{
    char *db = make_temp_db_path();
    const char *cmd[] = {"--json", "search", "\"unterminated"};
    ASSERT_TRUE(db != NULL);
    seed_three(db);
    assert_read_parity(db, cmd, 3);
    free(db);
}

TEST(facade_get_matches_cli)
{
    char *db = make_temp_db_path();
    const char *cmd[] = {"--json", "get", "1"};
    ASSERT_TRUE(db != NULL);
    seed_three(db);
    assert_read_parity(db, cmd, 3);
    free(db);
}

TEST(facade_get_missing_matches_cli)
{
    char *db = make_temp_db_path();
    const char *cmd[] = {"--json", "get", "999"};
    ASSERT_TRUE(db != NULL);
    seed_three(db);
    assert_read_parity(db, cmd, 3); /* exit 2 + "not found" on stderr */
    free(db);
}

TEST(facade_tags_matches_cli)
{
    char *db = make_temp_db_path();
    const char *cmd[] = {"--json", "tags"};
    ASSERT_TRUE(db != NULL);
    seed_three(db);
    assert_read_parity(db, cmd, 2);
    free(db);
}

TEST(facade_unknown_option_matches_cli)
{
    char *db = make_temp_db_path();
    const char *cmd[] = {"list", "--bogus"};
    ASSERT_TRUE(db != NULL);
    assert_read_parity(db, cmd, 2); /* exit 1 + unknown option on stderr */
    free(db);
}

TEST(facade_help_matches_cli)
{
    char *db = make_temp_db_path();
    const char *cmd[] = {"help", "tags"};
    ASSERT_TRUE(db != NULL);
    assert_read_parity(db, cmd, 2);
    free(db);
}

/* Mutations: identical pre-state on two DBs, compare timestamp-masked bytes. */
TEST(facade_add_matches_cli)
{
    char *db1 = make_temp_db_path();
    char *db2 = make_temp_db_path();
    const char *cmd[] = {"--json", "add", "--tag", "x", "hello world"};
    CmdResult sub;
    char *fout = NULL;
    char *ferr = NULL;
    int frc;

    ASSERT_TRUE(db1 != NULL && db2 != NULL);
    sub = run_remember(db1, cmd, 5, NULL);
    frc = facade_run(db2, cmd, 5, &fout, &ferr);
    ASSERT_EQ_INT(frc, sub.exit_code);
    mask_timestamps(sub.out);
    mask_timestamps(fout);
    ASSERT_STREQ(fout, sub.out);
    free(fout);
    free(ferr);
    cmd_result_free(&sub);
    free(db1);
    free(db2);
}

TEST(facade_update_matches_cli)
{
    char *db1 = make_temp_db_path();
    char *db2 = make_temp_db_path();
    const char *seed[] = {"add", "original body"};
    const char *cmd[] = {"--json", "update", "1", "--text", "new body", "--tag", "t"};
    CmdResult s1;
    CmdResult s2;
    CmdResult sub;
    char *fout = NULL;
    char *ferr = NULL;
    int frc;

    ASSERT_TRUE(db1 != NULL && db2 != NULL);
    s1 = run_remember(db1, seed, 2, NULL);
    cmd_result_free(&s1);
    s2 = run_remember(db2, seed, 2, NULL);
    cmd_result_free(&s2);

    sub = run_remember(db1, cmd, 7, NULL);
    frc = facade_run(db2, cmd, 7, &fout, &ferr);
    ASSERT_EQ_INT(frc, sub.exit_code);
    mask_timestamps(sub.out);
    mask_timestamps(fout);
    ASSERT_STREQ(fout, sub.out);
    free(fout);
    free(ferr);
    cmd_result_free(&sub);
    free(db1);
    free(db2);
}

TEST(facade_delete_matches_cli)
{
    char *db1 = make_temp_db_path();
    char *db2 = make_temp_db_path();
    const char *seed[] = {"add", "to remove"};
    const char *cmd[] = {"--json", "delete", "1"};
    CmdResult s1;
    CmdResult s2;
    CmdResult sub;
    char *fout = NULL;
    char *ferr = NULL;
    int frc;

    ASSERT_TRUE(db1 != NULL && db2 != NULL);
    s1 = run_remember(db1, seed, 2, NULL);
    cmd_result_free(&s1);
    s2 = run_remember(db2, seed, 2, NULL);
    cmd_result_free(&s2);

    sub = run_remember(db1, cmd, 3, NULL);
    frc = facade_run(db2, cmd, 3, &fout, &ferr);
    ASSERT_EQ_INT(frc, sub.exit_code);
    mask_timestamps(sub.out);
    mask_timestamps(fout);
    ASSERT_STREQ(fout, sub.out);
    free(fout);
    free(ferr);
    cmd_result_free(&sub);
    free(db1);
    free(db2);
}

void register_facade_tests(void)
{
    RUN_TEST(facade_list_matches_cli);
    RUN_TEST(facade_search_matches_cli);
    RUN_TEST(facade_search_bad_query_matches_cli);
    RUN_TEST(facade_get_matches_cli);
    RUN_TEST(facade_get_missing_matches_cli);
    RUN_TEST(facade_tags_matches_cli);
    RUN_TEST(facade_unknown_option_matches_cli);
    RUN_TEST(facade_help_matches_cli);
    RUN_TEST(facade_add_matches_cli);
    RUN_TEST(facade_update_matches_cli);
    RUN_TEST(facade_delete_matches_cli);
}
