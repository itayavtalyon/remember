#include "harness.h"
#include "register.h"
#include "remember_app.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    FACADE_ARGV_MAX = 32, /* max argv slots built for remember_run */
    FACADE_PATH_BUF = 192,
    FACADE_CMD_BUF = 400,
    FACADE_SQL_BUF = 320,
    FACADE_SIDE_BUF = 208
};

/*
 * Facade parity: remember_run (in-process, what the GUI links) must produce the
 * same bytes as the CLI subprocess for the same argv against the same DB. Both
 * paths share output.c and the run() pipeline, so this guards the stream
 * injection (appio) and any future drift.
 *
 * Timestamps, sync_id, and version_vector are the fields that legitimately
 * differ between two separate writes, so mutation cases mask those values
 * before comparing; read cases compare exact bytes (stored ids are identical
 * across reads of the same DB).
 */

/* Run remember_run with argv {"remember","--db",db,<cmd...>} into heap buffers. */
typedef struct {
    char **out;
    char **err;
} FacadeCapture;

static int facade_run(const char *db, const char *const *cmd, size_t ncmd, FacadeCapture cap)
{
    char **out = cap.out;
    char **err = cap.err;
    const char *argv[FACADE_ARGV_MAX];
    size_t n = 0U;
    size_t i = 0;
    char *obuf = NULL;
    char *ebuf = NULL;
    size_t olen = 0U;
    size_t elen = 0U;
    FILE *of = NULL;
    FILE *ef = NULL;
    int rc = 0;

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

static void mask_version_vector(char *s)
{
    char *p = s;

    while ((p = strstr(p, "\"version_vector\":")) != NULL) {
        p += strlen("\"version_vector\":");
        if (*p != '{') {
            continue;
        }
        p++;
        while (*p != '\0' && *p != '}') {
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
    mask_field(s, "\"deleted_at\":\"");
    mask_field(s, "\"sync_id\":\"");
    mask_version_vector(s);
}

/* Assert remember_run stdout/stderr/exit byte-match the CLI for a read command. */
static void assert_read_parity(const char *db, const char *const *cmd, size_t ncmd)
{
    CmdResult sub = run_remember(db, cmd, ncmd, NULL);
    char *fout = NULL;
    char *ferr = NULL;
    int frc = facade_run(db, cmd, ncmd, (FacadeCapture){.out = &fout, .err = &ferr});

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

    r = run_remember(db, a1, sizeof(a1) / sizeof(a1[0]), NULL);
    cmd_result_free(&r);
    r = run_remember(db, a2, sizeof(a2) / sizeof(a2[0]), NULL);
    cmd_result_free(&r);
    r = run_remember(db, a3, sizeof(a3) / sizeof(a3[0]), NULL);
    cmd_result_free(&r);
}

/* Seed three, then link 1->2 (cites) and 1<->3 (related) so read parity
   exercises the links[] stub path in-process. */
static void seed_three_linked(const char *db)
{
    const char *l1[] = {"link", "--from", "1", "--to", "2", "--kind", "cites"};
    const char *l2[] = {"link", "1", "3"};
    CmdResult r;

    seed_three(db);
    r = run_remember(db, l1, sizeof(l1) / sizeof(l1[0]), NULL);
    cmd_result_free(&r);
    r = run_remember(db, l2, sizeof(l2) / sizeof(l2[0]), NULL);
    cmd_result_free(&r);
}

typedef void (*SeedFn)(const char *db);

/* Mutations: seed identical pre-state on two DBs, run cmd on each, compare
   timestamp/sync_id-masked stdout + exit. */
static void assert_mutation_parity(SeedFn seed, const char *const *cmd, size_t ncmd)
{
    char *db1 = make_temp_db_path();
    char *db2 = make_temp_db_path();
    CmdResult sub;
    char *fout = NULL;
    char *ferr = NULL;
    int frc = 0;

    ASSERT_TRUE(db1 != NULL && db2 != NULL);
    if (seed != NULL) {
        seed(db1);
        seed(db2);
    }
    sub = run_remember(db1, cmd, ncmd, NULL);
    frc = facade_run(db2, cmd, ncmd, (FacadeCapture){.out = &fout, .err = &ferr});
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

TEST(facade_list_matches_cli)
{
    char *db = make_temp_db_path();
    const char *cmd[] = {"--json", "list"};
    ASSERT_TRUE(db != NULL);
    seed_three(db);
    assert_read_parity(db, cmd, sizeof(cmd) / sizeof(cmd[0]));
    free(db);
}

TEST(facade_search_matches_cli)
{
    char *db = make_temp_db_path();
    const char *cmd[] = {"--json", "search", "beta"};
    ASSERT_TRUE(db != NULL);
    seed_three(db);
    assert_read_parity(db, cmd, sizeof(cmd) / sizeof(cmd[0]));
    free(db);
}

TEST(facade_search_bad_query_matches_cli)
{
    char *db = make_temp_db_path();
    const char *cmd[] = {"--json", "search", "\"unterminated"};
    ASSERT_TRUE(db != NULL);
    seed_three(db);
    assert_read_parity(db, cmd, sizeof(cmd) / sizeof(cmd[0]));
    free(db);
}

TEST(facade_get_matches_cli)
{
    char *db = make_temp_db_path();
    const char *cmd[] = {"--json", "get", "1"};
    ASSERT_TRUE(db != NULL);
    seed_three(db);
    assert_read_parity(db, cmd, sizeof(cmd) / sizeof(cmd[0]));
    free(db);
}

TEST(facade_get_missing_matches_cli)
{
    char *db = make_temp_db_path();
    const char *cmd[] = {"--json", "get", "999"};
    ASSERT_TRUE(db != NULL);
    seed_three(db);
    assert_read_parity(db, cmd, sizeof(cmd) / sizeof(cmd[0])); /* exit 2 + "not found" on stderr */
    free(db);
}

TEST(facade_tags_matches_cli)
{
    char *db = make_temp_db_path();
    const char *cmd[] = {"--json", "tags"};
    ASSERT_TRUE(db != NULL);
    seed_three(db);
    assert_read_parity(db, cmd, sizeof(cmd) / sizeof(cmd[0]));
    free(db);
}

TEST(facade_unknown_option_matches_cli)
{
    char *db = make_temp_db_path();
    const char *cmd[] = {"list", "--bogus"};
    ASSERT_TRUE(db != NULL);
    assert_read_parity(db, cmd,
                       sizeof(cmd) / sizeof(cmd[0])); /* exit 1 + unknown option on stderr */
    free(db);
}

TEST(facade_help_matches_cli)
{
    char *db = make_temp_db_path();
    const char *cmd[] = {"help", "tags"};
    ASSERT_TRUE(db != NULL);
    assert_read_parity(db, cmd, sizeof(cmd) / sizeof(cmd[0]));
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
    int frc = 0;

    ASSERT_TRUE(db1 != NULL && db2 != NULL);
    sub = run_remember(db1, cmd, sizeof(cmd) / sizeof(cmd[0]), NULL);
    frc = facade_run(db2, cmd, sizeof(cmd) / sizeof(cmd[0]),
                     (FacadeCapture){.out = &fout, .err = &ferr});
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
    int frc = 0;

    ASSERT_TRUE(db1 != NULL && db2 != NULL);
    s1 = run_remember(db1, seed, sizeof(seed) / sizeof(seed[0]), NULL);
    cmd_result_free(&s1);
    s2 = run_remember(db2, seed, sizeof(seed) / sizeof(seed[0]), NULL);
    cmd_result_free(&s2);

    sub = run_remember(db1, cmd, sizeof(cmd) / sizeof(cmd[0]), NULL);
    frc = facade_run(db2, cmd, sizeof(cmd) / sizeof(cmd[0]),
                     (FacadeCapture){.out = &fout, .err = &ferr});
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
    int frc = 0;

    ASSERT_TRUE(db1 != NULL && db2 != NULL);
    s1 = run_remember(db1, seed, sizeof(seed) / sizeof(seed[0]), NULL);
    cmd_result_free(&s1);
    s2 = run_remember(db2, seed, sizeof(seed) / sizeof(seed[0]), NULL);
    cmd_result_free(&s2);

    sub = run_remember(db1, cmd, sizeof(cmd) / sizeof(cmd[0]), NULL);
    frc = facade_run(db2, cmd, 3, (FacadeCapture){.out = &fout, .err = &ferr});
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

TEST(facade_purge_trash_matches_cli)
{
    char *db1 = make_temp_db_path();
    char *db2 = make_temp_db_path();
    const char *seed[] = {"add", "--expires", "2020-01-01T00:00:00Z", "gone"};
    const char *cmd[] = {"--json", "purge-trash"};
    CmdResult s1;
    CmdResult s2;
    CmdResult sub;
    char *fout = NULL;
    char *ferr = NULL;
    int frc = 0;

    ASSERT_TRUE(db1 != NULL && db2 != NULL);
    s1 = run_remember(db1, seed, sizeof(seed) / sizeof(seed[0]), NULL);
    cmd_result_free(&s1);
    s2 = run_remember(db2, seed, sizeof(seed) / sizeof(seed[0]), NULL);
    cmd_result_free(&s2);
    sub = run_remember(db1, cmd, sizeof(cmd) / sizeof(cmd[0]), NULL);
    frc = facade_run(db2, cmd, 2, (FacadeCapture){.out = &fout, .err = &ferr});
    ASSERT_EQ_INT(frc, sub.exit_code);
    mask_timestamps(sub.out);
    mask_timestamps(fout);
    ASSERT_STREQ(fout, sub.out);
    ASSERT_STREQ(ferr, sub.err);
    free(fout);
    free(ferr);
    cmd_result_free(&sub);
    free(db1);
    free(db2);
}

TEST(facade_list_trash_matches_cli)
{
    char *db = make_temp_db_path();
    const char *seed[] = {"add", "--expires", "2020-01-01T00:00:00Z", "gone"};
    const char *cmd[] = {"--json", "list", "--trash"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, seed, sizeof(seed) / sizeof(seed[0]), NULL);
    cmd_result_free(&r);
    assert_read_parity(db, cmd, sizeof(cmd) / sizeof(cmd[0]));
    free(db);
}

/* Read parity for related and for get/list with a non-empty links[] stub. */
TEST(facade_related_matches_cli)
{
    char *db = make_temp_db_path();
    const char *cmd[] = {"--json", "related", "1"};
    ASSERT_TRUE(db != NULL);
    seed_three_linked(db);
    assert_read_parity(db, cmd, sizeof(cmd) / sizeof(cmd[0]));
    free(db);
}

TEST(facade_get_with_links_matches_cli)
{
    char *db = make_temp_db_path();
    const char *cmd[] = {"--json", "get", "1"};
    ASSERT_TRUE(db != NULL);
    seed_three_linked(db);
    assert_read_parity(db, cmd, sizeof(cmd) / sizeof(cmd[0]));
    free(db);
}

/* Mutations: link, unlink, rekey byte-match the CLI. */
TEST(facade_link_matches_cli)
{
    const char *cmd[] = {"--json", "link", "1", "2", "--kind", "cites"};
    assert_mutation_parity(seed_three, cmd, sizeof(cmd) / sizeof(cmd[0]));
}

TEST(facade_unlink_matches_cli)
{
    const char *cmd[] = {"--json", "unlink", "1", "2"};
    assert_mutation_parity(seed_three_linked, cmd, sizeof(cmd) / sizeof(cmd[0]));
}

TEST(facade_rekey_matches_cli)
{
    const char *cmd[] = {"--json", "rekey", "--key", "k", "--to-key", "k2"};
    assert_mutation_parity(seed_three, cmd, sizeof(cmd) / sizeof(cmd[0]));
}

/* Stage 6: import / conflicts / conflict accept byte-match the CLI. */
// NOLINTBEGIN(bugprone-command-processor,cert-env33-c,concurrency-mt-unsafe)
TEST(facade_import_matches_cli)
{
    char *db1 = make_temp_db_path();
    char *db2 = make_temp_db_path();
    char *foreign = make_temp_db_path();
    const char *seed_f[] = {"--json", "add", "--key", "slot", "from foreign"};
    const char *cmd[] = {"--json", "import", "--from-db", NULL};
    CmdResult seed_foreign;
    CmdResult sub;
    char *fout = NULL;
    char *ferr = NULL;
    int frc = 0;

    ASSERT_TRUE(db1 != NULL && db2 != NULL && foreign != NULL);
    seed_foreign = run_remember(foreign, seed_f, sizeof(seed_f) / sizeof(seed_f[0]), NULL);
    ASSERT_EQ_INT(seed_foreign.exit_code, 0);
    cmd_result_free(&seed_foreign);

    cmd[3] = foreign;
    sub = run_remember(db1, cmd, sizeof(cmd) / sizeof(cmd[0]), NULL);
    frc = facade_run(db2, cmd, sizeof(cmd) / sizeof(cmd[0]),
                     (FacadeCapture){.out = &fout, .err = &ferr});
    ASSERT_EQ_INT(frc, sub.exit_code);
    mask_timestamps(sub.out);
    mask_timestamps(fout);
    ASSERT_STREQ(fout, sub.out);
    ASSERT_STREQ(ferr, sub.err);
    free(fout);
    free(ferr);
    cmd_result_free(&sub);
    free(db1);
    free(db2);
    free(foreign);
}

TEST(facade_conflicts_matches_cli)
{
    char tmpl[] = "/tmp/remember-facade-c-XXXXXX";
    char *dir = NULL;
    char local[FACADE_PATH_BUF];
    char foreign[FACADE_PATH_BUF];
    CmdResult r;
    const char *add[] = {"--json", "add", "--key", "k", "local"};
    const char *imp[] = {"--json", "import", "--from-db", NULL};
    const char *cmd[] = {"--json", "conflicts"};
    char *dev = NULL;

    dir = mkdtemp(tmpl);
    ASSERT_TRUE(dir != NULL);
    if (dir == NULL) {
        return;
    }
    (void)snprintf(local, sizeof(local), "%s/local.db", dir);
    (void)snprintf(foreign, sizeof(foreign), "%s/foreign.db", dir);

    r = run_remember(local, add, sizeof(add) / sizeof(add[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    {
        char cp[FACADE_CMD_BUF];
        (void)snprintf(cp, sizeof(cp), "cp '%s' '%s'", local, foreign);
        ASSERT_EQ_INT(system(cp), 0);
    }
    free(harness_sqlite_query_line(
        foreign, "UPDATE entries SET body='incoming', "
                 "body_hash='abababababababababababababababababababababababababababababababab',"
                 "version_vector='{\"bbbbbbbb-bbbb-7bbb-bbbb-bbbbbbbbbbbb\":1}' WHERE id=1;"));
    dev = harness_sqlite_query_line(local, "SELECT device_id FROM devices LIMIT 1;");
    ASSERT_TRUE(dev != NULL);
    {
        char sql[FACADE_SQL_BUF];
        (void)snprintf(sql, sizeof(sql),
                       "UPDATE entries SET version_vector='{\"%s\":1}' WHERE id=1;", dev);
        free(harness_sqlite_query_line(local, sql));
    }
    free(dev);

    imp[3] = foreign;
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":1");
    cmd_result_free(&r);

    assert_read_parity(local, cmd, sizeof(cmd) / sizeof(cmd[0]));

    (void)remove(local);
    (void)remove(foreign);
    {
        char side[FACADE_SIDE_BUF];
        (void)snprintf(side, sizeof(side), "%s.device_id", local);
        (void)remove(side);
    }
    (void)rmdir(dir);
}

TEST(facade_conflict_accept_matches_cli)
{
    char tmpl[] = "/tmp/remember-facade-a-XXXXXX";
    char *dir = NULL;
    char local1[FACADE_PATH_BUF];
    char local2[FACADE_PATH_BUF];
    char foreign[FACADE_PATH_BUF];
    CmdResult r;
    const char *add[] = {"--json", "add", "--key", "k", "local"};
    const char *imp[] = {"--json", "import", "--from-db", NULL};
    const char *accept[] = {"--json", "conflict", "accept", "--id", "1", "--keep", "local"};
    char *fout = NULL;
    char *ferr = NULL;
    int frc = 0;
    char *dev = NULL;

    dir = mkdtemp(tmpl);
    ASSERT_TRUE(dir != NULL);
    if (dir == NULL) {
        return;
    }
    (void)snprintf(local1, sizeof(local1), "%s/a.db", dir);
    (void)snprintf(local2, sizeof(local2), "%s/b.db", dir);
    (void)snprintf(foreign, sizeof(foreign), "%s/f.db", dir);

    r = run_remember(local1, add, sizeof(add) / sizeof(add[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    {
        char cp[FACADE_CMD_BUF];
        (void)snprintf(cp, sizeof(cp), "cp '%s' '%s'", local1, foreign);
        ASSERT_EQ_INT(system(cp), 0);
    }
    free(harness_sqlite_query_line(
        foreign, "UPDATE entries SET body='incoming', "
                 "body_hash='abababababababababababababababababababababababababababababababab',"
                 "version_vector='{\"bbbbbbbb-bbbb-7bbb-bbbb-bbbbbbbbbbbb\":1}' WHERE id=1;"));
    dev = harness_sqlite_query_line(local1, "SELECT device_id FROM devices LIMIT 1;");
    ASSERT_TRUE(dev != NULL);
    {
        char sql[FACADE_SQL_BUF];
        (void)snprintf(sql, sizeof(sql),
                       "UPDATE entries SET version_vector='{\"%s\":1}' WHERE id=1;", dev);
        free(harness_sqlite_query_line(local1, sql));
    }
    free(dev);

    imp[3] = foreign;
    r = run_remember(local1, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    {
        char cp[FACADE_CMD_BUF];
        (void)snprintf(cp, sizeof(cp), "cp '%s' '%s'", local1, local2);
        ASSERT_EQ_INT(system(cp), 0);
    }

    r = run_remember(local1, accept, sizeof(accept) / sizeof(accept[0]), NULL);
    frc = facade_run(local2, accept, sizeof(accept) / sizeof(accept[0]),
                     (FacadeCapture){.out = &fout, .err = &ferr});
    ASSERT_EQ_INT(frc, r.exit_code);
    mask_timestamps(r.out);
    mask_timestamps(fout);
    ASSERT_STREQ(fout, r.out);
    ASSERT_STREQ(ferr, r.err);
    free(fout);
    free(ferr);
    cmd_result_free(&r);

    (void)remove(local1);
    (void)remove(local2);
    (void)remove(foreign);
    {
        char side[FACADE_SIDE_BUF];
        (void)snprintf(side, sizeof(side), "%s.device_id", local1);
        (void)remove(side);
        (void)snprintf(side, sizeof(side), "%s.device_id", local2);
        (void)remove(side);
    }
    (void)rmdir(dir);
}
// NOLINTEND(bugprone-command-processor,cert-env33-c,concurrency-mt-unsafe)

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
    RUN_TEST(facade_purge_trash_matches_cli);
    RUN_TEST(facade_list_trash_matches_cli);
    RUN_TEST(facade_related_matches_cli);
    RUN_TEST(facade_get_with_links_matches_cli);
    RUN_TEST(facade_link_matches_cli);
    RUN_TEST(facade_unlink_matches_cli);
    RUN_TEST(facade_rekey_matches_cli);
    RUN_TEST(facade_import_matches_cli);
    RUN_TEST(facade_conflicts_matches_cli);
    RUN_TEST(facade_conflict_accept_matches_cli);
}
