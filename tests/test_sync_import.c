#include "harness.h"
#include "register.h"
#include "store.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Stage 5: import --from-db + conflicts + conflict accept (005 Round 7). */
// NOLINTBEGIN(readability-identifier-length,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,android-cloexec-fopen,bugprone-command-processor,cert-env33-c,concurrency-mt-unsafe,bugprone-unchecked-string-to-number-conversion,cert-err34-c)

enum { SYNC_BUF = 40, PATH_BUF = 192, SQL_BUF = 512, VV_BUF = 160 };

static const char k_hash_a[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static const char k_now[] = "2026-06-15T12:00:00.000Z";

static int json_copy_quoted(const char *json, const char *key, char *out, size_t outlen)
{
    char needle[64];
    const char *p = NULL;
    size_t i = 0;

    if (json == NULL || key == NULL || out == NULL || outlen == 0U) {
        return -1;
    }
    (void)snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    p = strstr(json, needle);
    if (p == NULL) {
        return -1;
    }
    p += strlen(needle);
    while (p[i] != '\0' && p[i] != '"' && i + 1U < outlen) {
        out[i] = p[i];
        i++;
    }
    if (p[i] != '"') {
        return -1;
    }
    out[i] = '\0';
    return 0;
}

static char *sidecar_path(const char *db)
{
    size_t n = 0;
    char *out = NULL;

    if (db == NULL) {
        return NULL;
    }
    n = strlen(db) + strlen(".device_id") + 1U;
    out = (char *)malloc(n);
    if (out == NULL) {
        return NULL;
    }
    (void)snprintf(out, n, "%s.device_id", db);
    return out;
}

static void force_vv(const char *db, long long id, const char *vv_json)
{
    char sql[SQL_BUF];

    (void)snprintf(sql, sizeof(sql), "UPDATE entries SET version_vector='%s' WHERE id=%lld;",
                   vv_json, id);
    free(harness_sqlite_query_line(db, sql));
}

static void force_body(const char *db, long long id, const char *body, const char *hash)
{
    char sql[SQL_BUF];

    (void)snprintf(sql, sizeof(sql), "UPDATE entries SET body='%s', body_hash='%s' WHERE id=%lld;",
                   body, hash, id);
    free(harness_sqlite_query_line(db, sql));
}

TEST(cli_import_inserts_preserves_sync_id_remaps_links_no_device_register)
{
    char *local = make_temp_db_path();
    char *foreign = make_temp_db_path();
    CmdResult r;
    char sync_a[SYNC_BUF];
    char sync_b[SYNC_BUF];
    const char *add_a[] = {"--json", "add", "--key", "ka", "alpha"};
    const char *add_b[] = {"--json", "add", "--key", "kb", "beta"};
    const char *link[] = {"--json",   "link", "--from-key", "ka",
                          "--to-key", "kb",   "--kind",     "cites"};
    const char *imp[] = {"--json", "import", "--from-db", NULL};
    const char *get_a[] = {"--json", "get", "--key", "ka"};
    const char *rel[] = {"--json", "related", "--key", "ka"};
    char *devices = NULL;

    ASSERT_TRUE(local != NULL && foreign != NULL);
    r = run_remember(foreign, add_a, sizeof(add_a) / sizeof(add_a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(json_copy_quoted(r.out, "sync_id", sync_a, sizeof(sync_a)), 0);
    cmd_result_free(&r);
    r = run_remember(foreign, add_b, sizeof(add_b) / sizeof(add_b[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(json_copy_quoted(r.out, "sync_id", sync_b, sizeof(sync_b)), 0);
    cmd_result_free(&r);
    r = run_remember(foreign, link, sizeof(link) / sizeof(link[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    /* Destination starts empty (open via list). */
    {
        const char *list[] = {"--json", "list"};
        r = run_remember(local, list, sizeof(list) / sizeof(list[0]), NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
    }

    imp[3] = foreign;
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"action\":\"imported\"");
    ASSERT_STR_CONTAINS(r.out, "\"inserted\":2");
    ASSERT_STR_CONTAINS(r.out, "\"updated\":0");
    ASSERT_STR_CONTAINS(r.out, "\"unchanged\":0");
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":0");
    cmd_result_free(&r);

    r = run_remember(local, get_a, sizeof(get_a) / sizeof(get_a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, sync_a);
    ASSERT_STR_CONTAINS(r.out, "alpha");
    cmd_result_free(&r);

    r = run_remember(local, rel, sizeof(rel) / sizeof(rel[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, sync_b);
    ASSERT_STR_CONTAINS(r.out, "\"type\":\"cites\"");
    cmd_result_free(&r);

    devices = harness_sqlite_query_line(local, "SELECT COUNT(*) FROM devices;");
    ASSERT_STREQ(devices != NULL ? devices : "", "1");
    free(devices);

    free(local);
    free(foreign);
}

TEST(cli_import_refuses_v3_source)
{
    char *local = make_temp_db_path();
    char *foreign = make_temp_db_path();
    CmdResult r;
    const char *list[] = {"--json", "list"};
    const char *imp[] = {"--json", "import", "--from-db", NULL};

    ASSERT_TRUE(local != NULL && foreign != NULL);
    r = run_remember(local, list, sizeof(list) / sizeof(list[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    free(harness_sqlite_query_line(
        foreign, "CREATE TABLE entries(id INTEGER PRIMARY KEY, key TEXT, body TEXT NOT NULL, "
                 "body_hash TEXT NOT NULL, source TEXT NOT NULL, created_at TEXT NOT NULL, "
                 "updated_at TEXT NOT NULL, expires_at TEXT);"
                 "INSERT INTO entries(id, body, body_hash, source, created_at, updated_at) VALUES"
                 "(1,'x','h','human','2026-01-01T00:00:00.000Z','2026-01-01T00:00:00.000Z');"
                 "PRAGMA user_version=3;"));

    imp[3] = foreign;
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "source database is older than this remember");
    cmd_result_free(&r);
    free(local);
    free(foreign);
}

TEST(cli_import_identical_automerge_concurrent_conflict)
{
    char tmpl[] = "/tmp/remember-imp-XXXXXX";
    char *dir = NULL;
    char local[PATH_BUF];
    char foreign[PATH_BUF];
    CmdResult r;
    char sync_id[SYNC_BUF];
    char *dev = NULL;
    const char *add[] = {"--json", "add", "--key", "same", "--tag", "t", "same body"};
    const char *imp[] = {"--json", "import", "--from-db", NULL};
    const char *conflicts[] = {"--json", "conflicts"};

    dir = mkdtemp(tmpl);
    ASSERT_TRUE(dir != NULL);
    if (dir == NULL) {
        return;
    }
    (void)snprintf(local, sizeof(local), "%s/local.db", dir);
    (void)snprintf(foreign, sizeof(foreign), "%s/foreign.db", dir);

    r = run_remember(local, add, sizeof(add) / sizeof(add[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(json_copy_quoted(r.out, "sync_id", sync_id, sizeof(sync_id)), 0);
    cmd_result_free(&r);

    /* Copy DB only (no sidecar) so foreign is a distinct replica file. */
    {
        char cmd[PATH_BUF * 2];
        (void)snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", local, foreign);
        ASSERT_EQ_INT(system(cmd), 0);
    }

    /* Identical content + concurrent VV → auto-merge (no conflict). */
    dev = harness_sqlite_query_line(local, "SELECT device_id FROM devices LIMIT 1;");
    ASSERT_TRUE(dev != NULL);
    {
        char vv_local[VV_BUF];
        char vv_foreign[VV_BUF];
        (void)snprintf(vv_local, sizeof(vv_local), "{\"%s\":1}", dev);
        (void)snprintf(vv_foreign, sizeof(vv_foreign),
                       "{\"ffffffff-ffff-7fff-bfff-ffffffffffff\":1}");
        force_vv(local, 1, vv_local);
        force_vv(foreign, 1, vv_foreign);
    }

    imp[3] = foreign;
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"unchanged\":1");
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":0");
    cmd_result_free(&r);

    /* Concurrent + different body → conflict. */
    force_body(foreign, 1, "other body",
               "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    {
        char vv_foreign[VV_BUF];
        (void)snprintf(vv_foreign, sizeof(vv_foreign),
                       "{\"%s\":1,\"ffffffff-ffff-7fff-bfff-ffffffffffff\":2}", dev);
        force_vv(foreign, 1, vv_foreign);
    }
    /* Local still has only local:1 after prior merge may have maxed — reset concurrent. */
    {
        char vv_local[VV_BUF];
        (void)snprintf(vv_local, sizeof(vv_local), "{\"%s\":3}", dev);
        force_vv(local, 1, vv_local);
    }

    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":1");
    cmd_result_free(&r);

    r = run_remember(local, conflicts, sizeof(conflicts) / sizeof(conflicts[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"reason\":\"concurrent_vv\"");
    ASSERT_STR_CONTAINS(r.out, sync_id);
    cmd_result_free(&r);

    free(dev);
    (void)remove(local);
    (void)remove(foreign);
    {
        char side[PATH_BUF + 16];
        (void)snprintf(side, sizeof(side), "%s.device_id", local);
        (void)remove(side);
    }
    (void)rmdir(dir);
}

TEST(cli_import_key_clash_and_hash_clash_exit_0)
{
    char *local = make_temp_db_path();
    char *foreign = make_temp_db_path();
    CmdResult r;
    const char *add_local[] = {"--json", "add", "--key", "slot", "local body"};
    const char *add_foreign[] = {"--json", "add", "--key", "slot", "foreign body"};
    const char *add_kl_local[] = {"--json", "add", "shared hash body"};
    const char *add_kl_foreign[] = {"--json", "add", "shared hash body"};
    const char *imp[] = {"--json", "import", "--from-db", NULL};
    const char *conflicts[] = {"--json", "conflicts"};

    ASSERT_TRUE(local != NULL && foreign != NULL);
    r = run_remember(local, add_local, sizeof(add_local) / sizeof(add_local[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(foreign, add_foreign, sizeof(add_foreign) / sizeof(add_foreign[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    imp[3] = foreign;
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":1");
    cmd_result_free(&r);

    r = run_remember(local, conflicts, sizeof(conflicts) / sizeof(conflicts[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"reason\":\"key_clash\"");
    cmd_result_free(&r);

    /* Fresh DBs for hash_clash (keyless same body → different sync_ids). */
    free(local);
    free(foreign);
    local = make_temp_db_path();
    foreign = make_temp_db_path();
    ASSERT_TRUE(local != NULL && foreign != NULL);
    r = run_remember(local, add_kl_local, sizeof(add_kl_local) / sizeof(add_kl_local[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(foreign, add_kl_foreign, sizeof(add_kl_foreign) / sizeof(add_kl_foreign[0]),
                     NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    /* Same body_hash merge on add within one DB — foreign add creates its own sync_id.
       Import of that sync_id against local's keyless hash → hash_clash. */
    imp[3] = foreign;
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":1");
    cmd_result_free(&r);
    r = run_remember(local, conflicts, sizeof(conflicts) / sizeof(conflicts[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"reason\":\"hash_clash\"");
    cmd_result_free(&r);

    free(local);
    free(foreign);
}

TEST(cli_import_key_clash_reimport_idempotent)
{
    char *local = make_temp_db_path();
    char *foreign = make_temp_db_path();
    CmdResult r;
    char *nconf = NULL;
    const char *add_local[] = {"--json", "add", "--key", "slot", "local body"};
    const char *add_foreign[] = {"--json", "add", "--key", "slot", "foreign body"};
    const char *imp[] = {"--json", "import", "--from-db", NULL};
    const char *conflicts[] = {"--json", "conflicts"};

    ASSERT_TRUE(local != NULL && foreign != NULL);
    r = run_remember(local, add_local, sizeof(add_local) / sizeof(add_local[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(foreign, add_foreign, sizeof(add_foreign) / sizeof(add_foreign[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    imp[3] = foreign;
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":1");
    cmd_result_free(&r);

    r = run_remember(local, conflicts, sizeof(conflicts) / sizeof(conflicts[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"count\":1");
    cmd_result_free(&r);

    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":0");
    cmd_result_free(&r);

    nconf = harness_sqlite_query_line(local, "SELECT COUNT(*) FROM conflicts;");
    ASSERT_STREQ(nconf != NULL ? nconf : "", "1");
    free(nconf);

    force_body(foreign, 1, "foreign body v2",
               "bebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebebe");
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":0");
    cmd_result_free(&r);

    nconf = harness_sqlite_query_line(local, "SELECT COUNT(*) FROM conflicts;");
    ASSERT_STREQ(nconf != NULL ? nconf : "", "1");
    free(nconf);

    r = run_remember(local, conflicts, sizeof(conflicts) / sizeof(conflicts[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"count\":1");
    ASSERT_STR_CONTAINS(r.out, "foreign body v2");
    cmd_result_free(&r);

    free(local);
    free(foreign);
}

TEST(cli_conflict_accept_keep_both_concurrent_remints)
{
    char tmpl[] = "/tmp/remember-both-XXXXXX";
    char *dir = NULL;
    char local[PATH_BUF];
    char foreign[PATH_BUF];
    CmdResult r;
    char sync_orig[SYNC_BUF];
    char *dev = NULL;
    const char *add[] = {"--json", "add", "--key", "k", "local-body"};
    const char *imp[] = {"--json", "import", "--from-db", NULL};
    const char *accept[] = {"--json", "conflict", "accept", "--id", "1", "--keep", "both"};
    const char *list[] = {"--json", "list", "--limit", "10"};

    dir = mkdtemp(tmpl);
    ASSERT_TRUE(dir != NULL);
    if (dir == NULL) {
        return;
    }
    (void)snprintf(local, sizeof(local), "%s/local.db", dir);
    (void)snprintf(foreign, sizeof(foreign), "%s/foreign.db", dir);

    r = run_remember(local, add, sizeof(add) / sizeof(add[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(json_copy_quoted(r.out, "sync_id", sync_orig, sizeof(sync_orig)), 0);
    cmd_result_free(&r);
    {
        char cmd[PATH_BUF * 2];
        (void)snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", local, foreign);
        ASSERT_EQ_INT(system(cmd), 0);
    }
    dev = harness_sqlite_query_line(local, "SELECT device_id FROM devices LIMIT 1;");
    ASSERT_TRUE(dev != NULL);
    force_body(foreign, 1, "incoming-body",
               "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
    {
        char vv_l[VV_BUF];
        char vv_f[VV_BUF];
        (void)snprintf(vv_l, sizeof(vv_l), "{\"%s\":1}", dev);
        (void)snprintf(vv_f, sizeof(vv_f), "{\"aaaaaaaa-aaaa-7aaa-baaa-aaaaaaaaaaaa\":1}");
        force_vv(local, 1, vv_l);
        force_vv(foreign, 1, vv_f);
    }

    imp[3] = foreign;
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":1");
    cmd_result_free(&r);

    r = run_remember(local, accept, sizeof(accept) / sizeof(accept[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"action\":\"accepted\"");
    ASSERT_STR_CONTAINS(r.out, "\"count\":2");
    ASSERT_STR_CONTAINS(r.out, sync_orig); /* local keeps S */
    cmd_result_free(&r);

    r = run_remember(local, list, sizeof(list) / sizeof(list[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "local-body");
    ASSERT_STR_CONTAINS(r.out, "incoming-body");
    /* Incoming reminted: original sync_id appears once (local); second has different id. */
    {
        const char *p1 = strstr(r.out, sync_orig);
        const char *p2 = NULL;
        ASSERT_TRUE(p1 != NULL);
        p2 = strstr(p1 + strlen(sync_orig), sync_orig);
        ASSERT_TRUE(p2 == NULL);
    }
    cmd_result_free(&r);

    free(dev);
    (void)remove(local);
    (void)remove(foreign);
    {
        char side[PATH_BUF + 16];
        (void)snprintf(side, sizeof(side), "%s.device_id", local);
        (void)remove(side);
    }
    (void)rmdir(dir);
}

TEST(cli_conflict_accept_keep_both_key_clash_keeps_incoming_sync_id)
{
    char *local = make_temp_db_path();
    char *foreign = make_temp_db_path();
    CmdResult r;
    char sync_in[SYNC_BUF];
    const char *add_l[] = {"--json", "add", "--key", "slot", "L"};
    const char *add_f[] = {"--json", "add", "--key", "slot", "F"};
    const char *imp[] = {"--json", "import", "--from-db", NULL};
    const char *accept[] = {"--json", "conflict", "accept", "--id", "1", "--keep", "both"};
    const char *get_in[] = {"--json", "get", "--sync-id", NULL};
    const char *list_del[] = {"--json", "list"};

    ASSERT_TRUE(local != NULL && foreign != NULL);
    r = run_remember(local, add_l, sizeof(add_l) / sizeof(add_l[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(foreign, add_f, sizeof(add_f) / sizeof(add_f[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(json_copy_quoted(r.out, "sync_id", sync_in, sizeof(sync_in)), 0);
    cmd_result_free(&r);

    imp[3] = foreign;
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":1");
    cmd_result_free(&r);

    r = run_remember(local, accept, sizeof(accept) / sizeof(accept[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"count\":2");
    cmd_result_free(&r);

    get_in[3] = sync_in;
    r = run_remember(local, get_in, sizeof(get_in) / sizeof(get_in[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"body\":\"F\"");
    ASSERT_STR_CONTAINS(r.out, "\"key\":null"); /* keyless — slot taken */
    cmd_result_free(&r);

    r = run_remember(local, list_del, sizeof(list_del) / sizeof(list_del[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"body\":\"L\"");
    ASSERT_STR_CONTAINS(r.out, "\"key\":\"slot\"");
    cmd_result_free(&r);

    free(local);
    free(foreign);
}

TEST(cli_import_incoming_dominates_updates)
{
    char tmpl[] = "/tmp/remember-dom-XXXXXX";
    char *dir = NULL;
    char local[PATH_BUF];
    char foreign[PATH_BUF];
    CmdResult r;
    char *dev = NULL;
    const char *add[] = {"--json", "add", "--key", "k", "old"};
    const char *imp[] = {"--json", "import", "--from-db", NULL};
    const char *get[] = {"--json", "get", "--key", "k"};

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
        char cmd[PATH_BUF * 2];
        (void)snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", local, foreign);
        ASSERT_EQ_INT(system(cmd), 0);
    }
    force_body(foreign, 1, "new",
               "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd");
    dev = harness_sqlite_query_line(local, "SELECT device_id FROM devices LIMIT 1;");
    ASSERT_TRUE(dev != NULL);
    {
        char vv_l[VV_BUF];
        char vv_f[VV_BUF];
        (void)snprintf(vv_l, sizeof(vv_l), "{\"%s\":1}", dev);
        (void)snprintf(vv_f, sizeof(vv_f), "{\"%s\":2}", dev);
        force_vv(local, 1, vv_l);
        force_vv(foreign, 1, vv_f);
    }

    imp[3] = foreign;
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"updated\":1");
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":0");
    cmd_result_free(&r);

    r = run_remember(local, get, sizeof(get) / sizeof(get[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"body\":\"new\"");
    cmd_result_free(&r);

    free(dev);
    (void)remove(local);
    (void)remove(foreign);
    {
        char side[PATH_BUF + 16];
        (void)snprintf(side, sizeof(side), "%s.device_id", local);
        (void)remove(side);
    }
    (void)rmdir(dir);
}

TEST(store_import_no_sidecar_copy_devices_stay_one)
{
    char *local = make_temp_db_path();
    char *foreign = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *dst = NULL;
    Store *src = NULL;
    StoreImportCounts counts;
    StoreAddAction act = STORE_ADD_CREATED;
    Entry e;
    char *devices = NULL;
    char *side_f = NULL;

    ASSERT_TRUE(local != NULL && foreign != NULL);
    memset(&e, 0, sizeof(e));
    memset(&counts, 0, sizeof(counts));
    src = store_open(foreign, err, sizeof(err));
    ASSERT_TRUE(src != NULL);
    ASSERT_EQ_STATUS(
        store_add(src, "body", k_hash_a, "k", NULL, 0U, "agent", NULL, k_now, &act, &e), STORE_OK);
    store_entry_free(&e);
    store_close(src);

    side_f = sidecar_path(foreign);
    ASSERT_TRUE(side_f != NULL);
    ASSERT_EQ_INT(unlink(side_f), 0); /* remove so import must not recreate it */

    dst = store_open(local, err, sizeof(err));
    ASSERT_TRUE(dst != NULL);
    ASSERT_EQ_STATUS(store_import(dst, foreign, k_now, &counts), STORE_OK);
    ASSERT_TRUE(counts.inserted == 1U);
    ASSERT_TRUE(counts.conflicts == 0U);
    store_close(dst);

    devices = harness_sqlite_query_line(local, "SELECT COUNT(*) FROM devices;");
    ASSERT_STREQ(devices != NULL ? devices : "", "1");
    free(devices);
    ASSERT_EQ_INT(access(side_f, F_OK), -1); /* still absent */
    free(side_f);
    free(local);
    free(foreign);
}

TEST(cli_conflict_accept_keep_local_and_incoming)
{
    char tmpl[] = "/tmp/remember-keep-XXXXXX";
    char *dir = NULL;
    char local[PATH_BUF];
    char foreign[PATH_BUF];
    CmdResult r;
    char *dev = NULL;
    const char *add[] = {"--json", "add", "--key", "k", "local-body"};
    const char *imp[] = {"--json", "import", "--from-db", NULL};
    const char *accept_local[] = {"--json", "conflict", "accept", "--id", "1", "--keep", "local"};
    const char *get[] = {"--json", "get", "--key", "k"};
    const char *conflicts_human[] = {"conflicts"};

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
        char cmd[PATH_BUF * 2];
        (void)snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", local, foreign);
        ASSERT_EQ_INT(system(cmd), 0);
    }
    force_body(foreign, 1, "incoming-body",
               "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
    dev = harness_sqlite_query_line(local, "SELECT device_id FROM devices LIMIT 1;");
    ASSERT_TRUE(dev != NULL);
    {
        char vv_l[VV_BUF];
        char vv_f[VV_BUF];
        (void)snprintf(vv_l, sizeof(vv_l), "{\"%s\":1}", dev);
        (void)snprintf(vv_f, sizeof(vv_f), "{\"bbbbbbbb-bbbb-7bbb-bbbb-bbbbbbbbbbbb\":1}");
        force_vv(local, 1, vv_l);
        force_vv(foreign, 1, vv_f);
    }

    imp[3] = foreign;
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":1");
    cmd_result_free(&r);

    r = run_remember(local, conflicts_human, sizeof(conflicts_human) / sizeof(conflicts_human[0]),
                     NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "concurrent_vv");
    cmd_result_free(&r);

    r = run_remember(local, accept_local, sizeof(accept_local) / sizeof(accept_local[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"action\":\"accepted\"");
    ASSERT_STR_CONTAINS(r.out, "\"count\":1");
    ASSERT_STR_CONTAINS(r.out, "local-body");
    cmd_result_free(&r);

    r = run_remember(local, get, sizeof(get) / sizeof(get[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "local-body");
    cmd_result_free(&r);

    /* Fresh conflict for keep incoming. */
    force_body(foreign, 1, "win-body",
               "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    {
        char vv_l[VV_BUF];
        char vv_f[VV_BUF];
        (void)snprintf(vv_l, sizeof(vv_l), "{\"%s\":5}", dev);
        (void)snprintf(vv_f, sizeof(vv_f), "{\"cccccccc-cccc-7ccc-bccc-cccccccccccc\":1}");
        force_vv(local, 1, vv_l);
        force_vv(foreign, 1, vv_f);
    }
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":1");
    cmd_result_free(&r);

    {
        const char *clist[] = {"--json", "conflicts"};
        char idbuf[16];
        const char *p = NULL;
        r = run_remember(local, clist, sizeof(clist) / sizeof(clist[0]), NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        p = strstr(r.out, "\"id\":");
        ASSERT_TRUE(p != NULL);
        {
            long id = 0;
            ASSERT_TRUE(sscanf(p, "\"id\":%ld", &id) == 1);
            (void)snprintf(idbuf, sizeof(idbuf), "%ld", id);
        }
        cmd_result_free(&r);
        {
            const char *accept_in[] = {"--json", "conflict", "accept",  "--id",
                                       idbuf,    "--keep",   "incoming"};
            r = run_remember(local, accept_in, sizeof(accept_in) / sizeof(accept_in[0]), NULL);
            ASSERT_EQ_INT(r.exit_code, 0);
            ASSERT_STR_CONTAINS(r.out, "\"action\":\"accepted\"");
            ASSERT_STR_CONTAINS(r.out, "win-body");
            cmd_result_free(&r);
        }
    }

    r = run_remember(local, get, sizeof(get) / sizeof(get[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "win-body");
    cmd_result_free(&r);

    free(dev);
    (void)remove(local);
    (void)remove(foreign);
    {
        char side[PATH_BUF + 16];
        (void)snprintf(side, sizeof(side), "%s.device_id", local);
        (void)remove(side);
    }
    (void)rmdir(dir);
}

TEST(cli_conflict_accept_keep_incoming_key_clash_demotes)
{
    char *local = make_temp_db_path();
    char *foreign = make_temp_db_path();
    CmdResult r;
    const char *add_l[] = {"--json", "add", "--key", "slot", "L"};
    const char *add_f[] = {"--json", "add", "--key", "slot", "F"};
    const char *imp[] = {"--json", "import", "--from-db", NULL};
    const char *accept[] = {"--json", "conflict", "accept", "--id", "1", "--keep", "incoming"};
    const char *get_slot[] = {"--json", "get", "--key", "slot"};

    ASSERT_TRUE(local != NULL && foreign != NULL);
    r = run_remember(local, add_l, sizeof(add_l) / sizeof(add_l[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(foreign, add_f, sizeof(add_f) / sizeof(add_f[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    imp[3] = foreign;
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":1");
    cmd_result_free(&r);

    r = run_remember(local, accept, sizeof(accept) / sizeof(accept[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"body\":\"F\"");
    cmd_result_free(&r);

    r = run_remember(local, get_slot, sizeof(get_slot) / sizeof(get_slot[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"body\":\"F\"");
    ASSERT_STR_CONTAINS(r.out, "\"key\":\"slot\"");
    cmd_result_free(&r);

    free(local);
    free(foreign);
}

/* Local VV strictly greater → unchanged (vv_compare *out = -1). */
TEST(cli_import_local_dominates_unchanged)
{
    char tmpl[] = "/tmp/remember-locdom-XXXXXX";
    char *dir = NULL;
    char local[PATH_BUF];
    char foreign[PATH_BUF];
    CmdResult r;
    char *dev = NULL;
    const char *add[] = {"--json", "add", "--key", "k", "local-wins"};
    const char *imp[] = {"--json", "import", "--from-db", NULL};
    const char *get[] = {"--json", "get", "--key", "k"};

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
        char cmd[PATH_BUF * 2];
        (void)snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", local, foreign);
        ASSERT_EQ_INT(system(cmd), 0);
    }
    force_body(foreign, 1, "stale-foreign",
               "1111111111111111111111111111111111111111111111111111111111111111");
    dev = harness_sqlite_query_line(local, "SELECT device_id FROM devices LIMIT 1;");
    ASSERT_TRUE(dev != NULL);
    {
        char vv_l[VV_BUF];
        char vv_f[VV_BUF];
        (void)snprintf(vv_l, sizeof(vv_l), "{\"%s\":3}", dev);
        (void)snprintf(vv_f, sizeof(vv_f), "{\"%s\":1}", dev);
        force_vv(local, 1, vv_l);
        force_vv(foreign, 1, vv_f);
    }

    imp[3] = foreign;
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"unchanged\":1");
    ASSERT_STR_CONTAINS(r.out, "\"updated\":0");
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":0");
    cmd_result_free(&r);

    r = run_remember(local, get, sizeof(get) / sizeof(get[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "local-wins");
    cmd_result_free(&r);

    free(dev);
    (void)remove(local);
    (void)remove(foreign);
    {
        char side[PATH_BUF + 16];
        (void)snprintf(side, sizeof(side), "%s.device_id", local);
        (void)remove(side);
    }
    (void)rmdir(dir);
}

/* Insert soft-deleted and expired foreign rows (bind + load expires/deleted). */
TEST(cli_import_soft_deleted_and_expired_rows)
{
    char *local = make_temp_db_path();
    char *foreign = make_temp_db_path();
    CmdResult r;
    const char *list[] = {"--json", "list"};
    const char *add_del[] = {"--json", "add", "--key", "del", "gone"};
    const char *add_exp[] = {
        "--json", "add", "--key", "exp", "--expires", "2020-01-01T00:00:00.000Z", "old"};
    const char *del[] = {"--json", "delete", "--key", "del"};
    const char *imp[] = {"--json", "import", "--from-db", NULL};
    const char *list_del[] = {"--json", "list", "--deleted"};
    const char *list_exp[] = {"--json", "list", "--expired"};

    ASSERT_TRUE(local != NULL && foreign != NULL);
    r = run_remember(local, list, sizeof(list) / sizeof(list[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    r = run_remember(foreign, add_del, sizeof(add_del) / sizeof(add_del[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(foreign, del, sizeof(del) / sizeof(del[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(foreign, add_exp, sizeof(add_exp) / sizeof(add_exp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    imp[3] = foreign;
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"inserted\":2");
    cmd_result_free(&r);

    r = run_remember(local, list_del, sizeof(list_del) / sizeof(list_del[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "gone");
    cmd_result_free(&r);
    r = run_remember(local, list_exp, sizeof(list_exp) / sizeof(list_exp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "old");
    cmd_result_free(&r);

    free(local);
    free(foreign);
}

/* Conflict snapshot escapes ", \\, \\n, \\t, C0, and other JSON controls. */
TEST(cli_import_conflict_snapshot_json_escapes)
{
    char tmpl[] = "/tmp/remember-esc-XXXXXX";
    char *dir = NULL;
    char local[PATH_BUF];
    char foreign[PATH_BUF];
    CmdResult r;
    char *dev = NULL;
    char body[] = {'a', '"', '\\', '\b', '\f', '\n', '\r', '\t', (char)0x01, 'z', '\0'};
    const char *add[] = {"--json", "add", "--key", "k", body};
    const char *imp[] = {"--json", "import", "--from-db", NULL};
    const char *conflicts[] = {"--json", "conflicts"};

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
        char cmd[PATH_BUF * 2];
        (void)snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", local, foreign);
        ASSERT_EQ_INT(system(cmd), 0);
    }
    force_body(foreign, 1, "other",
               "2222222222222222222222222222222222222222222222222222222222222222");
    dev = harness_sqlite_query_line(local, "SELECT device_id FROM devices LIMIT 1;");
    ASSERT_TRUE(dev != NULL);
    {
        char vv_l[VV_BUF];
        char vv_f[VV_BUF];
        (void)snprintf(vv_l, sizeof(vv_l), "{\"%s\":1}", dev);
        (void)snprintf(vv_f, sizeof(vv_f), "{\"dddddddd-dddd-7ddd-bddd-dddddddddddd\":1}");
        force_vv(local, 1, vv_l);
        force_vv(foreign, 1, vv_f);
    }

    imp[3] = foreign;
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":1");
    cmd_result_free(&r);

    r = run_remember(local, conflicts, sizeof(conflicts) / sizeof(conflicts[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\\\"");
    ASSERT_STR_CONTAINS(r.out, "\\\\");
    ASSERT_STR_CONTAINS(r.out, "\\b");
    ASSERT_STR_CONTAINS(r.out, "\\f");
    ASSERT_STR_CONTAINS(r.out, "\\n");
    ASSERT_STR_CONTAINS(r.out, "\\r");
    ASSERT_STR_CONTAINS(r.out, "\\t");
    ASSERT_STR_CONTAINS(r.out, "\\u0001");
    cmd_result_free(&r);

    free(dev);
    (void)remove(local);
    (void)remove(foreign);
    {
        char side[PATH_BUF + 16];
        (void)snprintf(side, sizeof(side), "%s.device_id", local);
        (void)remove(side);
    }
    (void)rmdir(dir);
}

TEST(cli_import_and_conflict_human_and_help)
{
    char *local = make_temp_db_path();
    char *foreign = make_temp_db_path();
    CmdResult r;
    const char *add_f[] = {"add", "human-import-body"};
    const char *list[] = {"list"};
    const char *imp[] = {"import", "--from-db", NULL};
    const char *help_conflicts[] = {"help", "conflicts"};
    const char *help_conflict[] = {"help", "conflict"};

    ASSERT_TRUE(local != NULL && foreign != NULL);
    r = run_remember(local, list, sizeof(list) / sizeof(list[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(foreign, add_f, sizeof(add_f) / sizeof(add_f[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    imp[2] = foreign;
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "inserted 1");
    ASSERT_STR_CONTAINS(r.out, "unchanged 0");
    cmd_result_free(&r);

    r = run_remember(local, help_conflicts, sizeof(help_conflicts) / sizeof(help_conflicts[0]),
                     NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "unresolved import conflicts");
    cmd_result_free(&r);
    r = run_remember(local, help_conflict, sizeof(help_conflict) / sizeof(help_conflict[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "conflict accept");
    cmd_result_free(&r);

    free(local);
    free(foreign);
}

TEST(cli_import_conflict_error_paths)
{
    char *local = make_temp_db_path();
    char *foreign = make_temp_db_path();
    CmdResult r;
    const char *list[] = {"--json", "list"};
    const char *imp_unknown[] = {"import", "--from-db", NULL, "--bogus"};
    const char *imp_missing[] = {"import"};
    const char *conflicts_args[] = {"conflicts", "x"};
    const char *accept_unknown[] = {"conflict", "accept", "--id", "1", "--keep", "local", "--nope"};
    const char *accept_missing[] = {"conflict", "accept", "--keep", "local"};
    const char *accept_nf[] = {"conflict", "accept", "--id", "99", "--keep", "local"};

    ASSERT_TRUE(local != NULL && foreign != NULL);
    r = run_remember(local, list, sizeof(list) / sizeof(list[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(foreign, list, sizeof(list) / sizeof(list[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    imp_unknown[2] = foreign;
    r = run_remember(local, imp_unknown, sizeof(imp_unknown) / sizeof(imp_unknown[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "unknown option");
    cmd_result_free(&r);

    r = run_remember(local, imp_missing, sizeof(imp_missing) / sizeof(imp_missing[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "--from-db");
    cmd_result_free(&r);

    r = run_remember(local, conflicts_args, sizeof(conflicts_args) / sizeof(conflicts_args[0]),
                     NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "no arguments");
    cmd_result_free(&r);

    r = run_remember(local, accept_unknown, sizeof(accept_unknown) / sizeof(accept_unknown[0]),
                     NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "unknown option");
    cmd_result_free(&r);

    r = run_remember(local, accept_missing, sizeof(accept_missing) / sizeof(accept_missing[0]),
                     NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "--id");
    cmd_result_free(&r);

    r = run_remember(local, accept_nf, sizeof(accept_nf) / sizeof(accept_nf[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 2);
    cmd_result_free(&r);

    free(local);
    free(foreign);
}

TEST(cli_conflict_accept_human)
{
    char *local = make_temp_db_path();
    char *foreign = make_temp_db_path();
    CmdResult r;
    const char *add_l[] = {"--json", "add", "--key", "slot", "L"};
    const char *add_f[] = {"--json", "add", "--key", "slot", "F"};
    const char *imp[] = {"--json", "import", "--from-db", NULL};
    const char *accept[] = {"conflict", "accept", "--id", "1", "--keep", "local"};

    ASSERT_TRUE(local != NULL && foreign != NULL);
    r = run_remember(local, add_l, sizeof(add_l) / sizeof(add_l[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(foreign, add_f, sizeof(add_f) / sizeof(add_f[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    imp[3] = foreign;
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    r = run_remember(local, accept, sizeof(accept) / sizeof(accept[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_TRUE(r.out != NULL && r.out[0] != '\0');
    ASSERT_STR_CONTAINS(r.out, "1");
    cmd_result_free(&r);

    free(local);
    free(foreign);
}

/*
 * Incoming dominates a keyless row whose body_hash is already owned by a
 * different local sync_id → hash_clash inside import_merge_present.
 */
TEST(cli_import_incoming_dominates_keyless_hash_clash)
{
    char tmpl[] = "/tmp/remember-hashdom-XXXXXX";
    char *dir = NULL;
    char local[PATH_BUF];
    char foreign[PATH_BUF];
    CmdResult r;
    char *dev = NULL;
    char sync_s[SYNC_BUF];
    const char *add_s[] = {"--json", "add", "--key", "s", "seed"};
    const char *add_occ[] = {"--json", "add", "shared-hash-body"};
    const char *imp[] = {"--json", "import", "--from-db", NULL};
    const char *conflicts[] = {"--json", "conflicts"};

    dir = mkdtemp(tmpl);
    ASSERT_TRUE(dir != NULL);
    if (dir == NULL) {
        return;
    }
    (void)snprintf(local, sizeof(local), "%s/local.db", dir);
    (void)snprintf(foreign, sizeof(foreign), "%s/foreign.db", dir);

    r = run_remember(local, add_s, sizeof(add_s) / sizeof(add_s[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(json_copy_quoted(r.out, "sync_id", sync_s, sizeof(sync_s)), 0);
    cmd_result_free(&r);
    r = run_remember(local, add_occ, sizeof(add_occ) / sizeof(add_occ[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    /* Foreign has only the sync_s row (no occupant) so keyless+hash is legal there. */
    {
        char cmd[PATH_BUF * 2];
        (void)snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", local, foreign);
        ASSERT_EQ_INT(system(cmd), 0);
    }
    free(harness_sqlite_query_line(foreign, "DELETE FROM entries WHERE key IS NULL;"));
    {
        char *hash = harness_sqlite_query_line(
            local, "SELECT body_hash FROM entries WHERE key IS NULL LIMIT 1;");
        char sql[SQL_BUF];
        ASSERT_TRUE(hash != NULL);
        (void)snprintf(sql, sizeof(sql),
                       "UPDATE entries SET key=NULL, body='shared-hash-body', body_hash='%s' "
                       "WHERE sync_id='%s';",
                       hash, sync_s);
        free(harness_sqlite_query_line(foreign, sql));
        free(hash);
    }
    dev = harness_sqlite_query_line(local, "SELECT device_id FROM devices LIMIT 1;");
    ASSERT_TRUE(dev != NULL);
    {
        char vv_l[VV_BUF];
        char vv_f[VV_BUF];
        char sql[SQL_BUF];
        (void)snprintf(vv_l, sizeof(vv_l), "{\"%s\":1}", dev);
        (void)snprintf(vv_f, sizeof(vv_f), "{\"%s\":5}", dev);
        (void)snprintf(sql, sizeof(sql),
                       "UPDATE entries SET version_vector='%s' WHERE sync_id='%s';", vv_l, sync_s);
        free(harness_sqlite_query_line(local, sql));
        (void)snprintf(sql, sizeof(sql),
                       "UPDATE entries SET version_vector='%s' WHERE sync_id='%s';", vv_f, sync_s);
        free(harness_sqlite_query_line(foreign, sql));
    }

    imp[3] = foreign;
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":1");
    cmd_result_free(&r);

    r = run_remember(local, conflicts, sizeof(conflicts) / sizeof(conflicts[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"reason\":\"hash_clash\"");
    cmd_result_free(&r);

    free(dev);
    (void)remove(local);
    (void)remove(foreign);
    {
        char side[PATH_BUF + 16];
        (void)snprintf(side, sizeof(side), "%s.device_id", local);
        (void)remove(side);
    }
    (void)rmdir(dir);
}

/* Accept keep incoming with 10+ tags exercises json_each tag growth. */
TEST(cli_conflict_accept_incoming_many_tags)
{
    char tmpl[] = "/tmp/remember-tags-XXXXXX";
    char *dir = NULL;
    char local[PATH_BUF];
    char foreign[PATH_BUF];
    CmdResult r;
    char *dev = NULL;
    const char *add[] = {"--json", "add",   "--key", "k",         "--tag", "t0",    "--tag",
                         "t1",     "--tag", "t2",    "--tag",     "t3",    "--tag", "t4",
                         "--tag",  "t5",    "--tag", "t6",        "--tag", "t7",    "--tag",
                         "t8",     "--tag", "t9",    "body-local"};
    const char *imp[] = {"--json", "import", "--from-db", NULL};
    const char *accept[] = {"--json", "conflict", "accept", "--id", "1", "--keep", "incoming"};
    const char *get[] = {"--json", "get", "--key", "k"};

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
        char cmd[PATH_BUF * 2];
        (void)snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", local, foreign);
        ASSERT_EQ_INT(system(cmd), 0);
    }
    force_body(foreign, 1, "body-in",
               "3333333333333333333333333333333333333333333333333333333333333333");
    /* Keep the 10 tags on foreign; only body differs for concurrent_vv. */
    dev = harness_sqlite_query_line(local, "SELECT device_id FROM devices LIMIT 1;");
    ASSERT_TRUE(dev != NULL);
    {
        char vv_l[VV_BUF];
        char vv_f[VV_BUF];
        (void)snprintf(vv_l, sizeof(vv_l), "{\"%s\":1}", dev);
        (void)snprintf(vv_f, sizeof(vv_f), "{\"eeeeeeee-eeee-7eee-beee-eeeeeeeeeeee\":1}");
        force_vv(local, 1, vv_l);
        force_vv(foreign, 1, vv_f);
    }

    imp[3] = foreign;
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":1");
    cmd_result_free(&r);

    r = run_remember(local, accept, sizeof(accept) / sizeof(accept[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "body-in");
    cmd_result_free(&r);

    r = run_remember(local, get, sizeof(get) / sizeof(get[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"t9\"");
    ASSERT_STR_CONTAINS(r.out, "body-in");
    cmd_result_free(&r);

    free(dev);
    (void)remove(local);
    (void)remove(foreign);
    {
        char side[PATH_BUF + 16];
        (void)snprintf(side, sizeof(side), "%s.device_id", local);
        (void)remove(side);
    }
    (void)rmdir(dir);
}

TEST(cli_conflict_accept_hash_clash_keep_incoming_and_both_refuse_fake_key)
{
    char *local = make_temp_db_path();
    char *foreign = make_temp_db_path();
    CmdResult r;
    const char *add_l[] = {"--json", "add", "shared hash body"};
    const char *add_f[] = {"--json", "add", "shared hash body"};
    const char *imp[] = {"--json", "import", "--from-db", NULL};
    const char *accept_in[] = {"--json", "conflict", "accept", "--id", "1", "--keep", "incoming"};
    const char *accept_both[] = {"--json", "conflict", "accept", "--id", "1", "--keep", "both"};
    const char *accept_local[] = {"--json", "conflict", "accept", "--id", "1", "--keep", "local"};

    ASSERT_TRUE(local != NULL && foreign != NULL);
    r = run_remember(local, add_l, sizeof(add_l) / sizeof(add_l[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(foreign, add_f, sizeof(add_f) / sizeof(add_f[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    imp[3] = foreign;
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":1");
    cmd_result_free(&r);

    /* decision:sync-import-hash-demote — no key=sync_id; refuse until keep local. */
    r = run_remember(local, accept_in, sizeof(accept_in) / sizeof(accept_in[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "cannot free unique key or body hash");
    cmd_result_free(&r);

    r = run_remember(local, accept_both, sizeof(accept_both) / sizeof(accept_both[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "cannot free unique key or body hash");
    cmd_result_free(&r);

    r = run_remember(local, accept_local, sizeof(accept_local) / sizeof(accept_local[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"action\":\"accepted\"");
    cmd_result_free(&r);

    free(local);
    free(foreign);
}

/* Incoming dominates applying expires_at + deleted_at + clearing key. */
TEST(cli_import_incoming_dominates_expires_deleted_keyless)
{
    char tmpl[] = "/tmp/remember-dom2-XXXXXX";
    char *dir = NULL;
    char local[PATH_BUF];
    char foreign[PATH_BUF];
    CmdResult r;
    char *dev = NULL;
    char sync[SYNC_BUF];
    const char *add[] = {"--json", "add", "--key", "k", "live"};
    const char *imp[] = {"--json", "import", "--from-db", NULL};
    const char *get_del[5];

    dir = mkdtemp(tmpl);
    ASSERT_TRUE(dir != NULL);
    if (dir == NULL) {
        return;
    }
    (void)snprintf(local, sizeof(local), "%s/local.db", dir);
    (void)snprintf(foreign, sizeof(foreign), "%s/foreign.db", dir);

    r = run_remember(local, add, sizeof(add) / sizeof(add[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(json_copy_quoted(r.out, "sync_id", sync, sizeof(sync)), 0);
    cmd_result_free(&r);
    {
        char cmd[PATH_BUF * 2];
        (void)snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", local, foreign);
        ASSERT_EQ_INT(system(cmd), 0);
    }
    /* Soft-deleted occupies deleted bin; expires cleared on soft-delete in product,
       but import apply still binds whatever foreign sends. */
    free(harness_sqlite_query_line(
        foreign, "UPDATE entries SET key=NULL, body='gone', "
                 "body_hash='4444444444444444444444444444444444444444444444444444444444444444',"
                 "expires_at='2020-06-01T00:00:00.000Z',"
                 "deleted_at='2026-01-01T00:00:00.000Z' WHERE id=1;"));
    get_del[0] = "--json";
    get_del[1] = "get";
    get_del[2] = "--sync-id";
    get_del[3] = sync;
    get_del[4] = "--deleted";
    dev = harness_sqlite_query_line(local, "SELECT device_id FROM devices LIMIT 1;");
    ASSERT_TRUE(dev != NULL);
    {
        char vv_l[VV_BUF];
        char vv_f[VV_BUF];
        (void)snprintf(vv_l, sizeof(vv_l), "{\"%s\":1}", dev);
        (void)snprintf(vv_f, sizeof(vv_f), "{\"%s\":9}", dev);
        force_vv(local, 1, vv_l);
        force_vv(foreign, 1, vv_f);
    }

    imp[3] = foreign;
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"updated\":1");
    cmd_result_free(&r);

    r = run_remember(local, get_del, sizeof(get_del) / sizeof(get_del[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "gone");
    ASSERT_STR_CONTAINS(r.out, "\"key\":null");
    ASSERT_STR_CONTAINS(r.out, "\"bin\":\"deleted\"");
    cmd_result_free(&r);

    free(dev);
    (void)remove(local);
    (void)remove(foreign);
    {
        char side[PATH_BUF + 16];
        (void)snprintf(side, sizeof(side), "%s.device_id", local);
        (void)remove(side);
    }
    (void)rmdir(dir);
}

/* Bad VV on foreign → import fail path (close + rollback). */
TEST(cli_import_bad_vv_fails_cleanly)
{
    char tmpl[] = "/tmp/remember-badvv-XXXXXX";
    char *dir = NULL;
    char local[PATH_BUF];
    char foreign[PATH_BUF];
    CmdResult r;
    const char *add[] = {"--json", "add", "--key", "k", "x"};
    const char *imp[] = {"--json", "import", "--from-db", NULL};

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
        char cmd[PATH_BUF * 2];
        (void)snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", local, foreign);
        ASSERT_EQ_INT(system(cmd), 0);
    }
    force_vv(foreign, 1, "not-a-vv");

    imp[3] = foreign;
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);

    (void)remove(local);
    (void)remove(foreign);
    {
        char side[PATH_BUF + 16];
        (void)snprintf(side, sizeof(side), "%s.device_id", local);
        (void)remove(side);
    }
    (void)rmdir(dir);
}

/* Corrupt conflicts.reason → conflicts list / accept return store error. */
TEST(cli_conflicts_bad_reason_errors)
{
    char *local = make_temp_db_path();
    CmdResult r;
    const char *list[] = {"--json", "list"};
    const char *conflicts[] = {"--json", "conflicts"};
    const char *accept[] = {"--json", "conflict", "accept", "--id", "1", "--keep", "local"};

    ASSERT_TRUE(local != NULL);
    r = run_remember(local, list, sizeof(list) / sizeof(list[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    free(harness_sqlite_query_line(
        local, "INSERT INTO conflicts(sync_id, reason, local_json, incoming_json, created_at) "
               "VALUES('aaaaaaaa-aaaa-7aaa-baaa-aaaaaaaaaaaa','bogus','{}','{}',"
               "'2026-01-01T00:00:00.000Z');"));

    r = run_remember(local, conflicts, sizeof(conflicts) / sizeof(conflicts[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);

    r = run_remember(local, accept, sizeof(accept) / sizeof(accept[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);

    free(local);
}

/*
 * concurrent_vv + --keep incoming where incoming rekeys onto another local
 * row's key: demote that occupant (never raw SQLITE unique / database error).
 */
TEST(cli_conflict_accept_concurrent_keep_incoming_demotes_other_key)
{
    char tmpl[] = "/tmp/remember-cvi-XXXXXX";
    char *dir = NULL;
    char local[PATH_BUF];
    char foreign[PATH_BUF];
    CmdResult r;
    char sync_shared[SYNC_BUF];
    char *dev = NULL;
    const char *add_shared[] = {"--json", "add", "--key", "shared", "local-body"};
    const char *add_other[] = {"--json", "add", "--key", "other", "other-body"};
    const char *imp[] = {"--json", "import", "--from-db", NULL};
    const char *accept[] = {"--json", "conflict", "accept", "--id", "1", "--keep", "incoming"};
    const char *get_other[] = {"--json", "get", "--key", "other"};

    dir = mkdtemp(tmpl);
    ASSERT_TRUE(dir != NULL);
    if (dir == NULL) {
        return;
    }
    (void)snprintf(local, sizeof(local), "%s/local.db", dir);
    (void)snprintf(foreign, sizeof(foreign), "%s/foreign.db", dir);

    r = run_remember(local, add_shared, sizeof(add_shared) / sizeof(add_shared[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(json_copy_quoted(r.out, "sync_id", sync_shared, sizeof(sync_shared)), 0);
    cmd_result_free(&r);
    r = run_remember(local, add_other, sizeof(add_other) / sizeof(add_other[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    {
        char cmd[PATH_BUF * 2];
        (void)snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", local, foreign);
        ASSERT_EQ_INT(system(cmd), 0);
    }
    free(harness_sqlite_query_line(foreign, "DELETE FROM entries WHERE key='other';"));
    {
        char sql[SQL_BUF];
        (void)snprintf(sql, sizeof(sql),
                       "UPDATE entries SET key='other', body='incoming-body', "
                       "body_hash='%s', version_vector="
                       "'{\"bbbbbbbb-bbbb-7bbb-bbbb-bbbbbbbbbbbb\":1}' WHERE sync_id='%s';",
                       k_hash_a, sync_shared);
        free(harness_sqlite_query_line(foreign, sql));
    }
    dev = harness_sqlite_query_line(local, "SELECT device_id FROM devices LIMIT 1;");
    ASSERT_TRUE(dev != NULL);
    {
        char vv_l[VV_BUF];
        (void)snprintf(vv_l, sizeof(vv_l), "{\"%s\":1}", dev);
        force_vv(local, 1, vv_l);
    }

    imp[3] = foreign;
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":1");
    cmd_result_free(&r);

    r = run_remember(local, accept, sizeof(accept) / sizeof(accept[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"action\":\"accepted\"");
    ASSERT_STR_CONTAINS(r.out, "incoming-body");
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);

    r = run_remember(local, get_other, sizeof(get_other) / sizeof(get_other[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "incoming-body");
    cmd_result_free(&r);

    free(dev);
    (void)remove(local);
    (void)remove(foreign);
    {
        char side[PATH_BUF + 16];
        (void)snprintf(side, sizeof(side), "%s.device_id", local);
        (void)remove(side);
    }
    (void)rmdir(dir);
}

/*
 * concurrent_vv + --keep incoming keyless, but another local keyless row
 * already owns that body_hash → UNIQUE_TAKEN (decision:sync-import-hash-demote).
 */
TEST(cli_conflict_accept_concurrent_keep_incoming_hash_taken_refuses)
{
    char tmpl[] = "/tmp/remember-cvh-XXXXXX";
    char *dir = NULL;
    char local[PATH_BUF];
    char foreign[PATH_BUF];
    CmdResult r;
    char sync_shared[SYNC_BUF];
    char *dev = NULL;
    char *hash = NULL;
    const char *add_occ[] = {"--json", "add", "contested-hash-body"};
    const char *add_shared[] = {"--json", "add", "--key", "shared", "local-body"};
    const char *imp[] = {"--json", "import", "--from-db", NULL};
    const char *accept[] = {"--json", "conflict", "accept", "--id", "1", "--keep", "incoming"};

    dir = mkdtemp(tmpl);
    ASSERT_TRUE(dir != NULL);
    if (dir == NULL) {
        return;
    }
    (void)snprintf(local, sizeof(local), "%s/local.db", dir);
    (void)snprintf(foreign, sizeof(foreign), "%s/foreign.db", dir);

    r = run_remember(local, add_occ, sizeof(add_occ) / sizeof(add_occ[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(local, add_shared, sizeof(add_shared) / sizeof(add_shared[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_EQ_INT(json_copy_quoted(r.out, "sync_id", sync_shared, sizeof(sync_shared)), 0);
    cmd_result_free(&r);

    hash = harness_sqlite_query_line(local,
                                     "SELECT body_hash FROM entries WHERE key IS NULL LIMIT 1;");
    ASSERT_TRUE(hash != NULL && hash[0] != '\0');
    {
        char cmd[PATH_BUF * 2];
        (void)snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", local, foreign);
        ASSERT_EQ_INT(system(cmd), 0);
    }
    free(harness_sqlite_query_line(foreign, "DELETE FROM entries WHERE key IS NULL;"));
    {
        char sql[SQL_BUF];
        (void)snprintf(sql, sizeof(sql),
                       "UPDATE entries SET key=NULL, body='contested-hash-body', "
                       "body_hash='%s', version_vector="
                       "'{\"bbbbbbbb-bbbb-7bbb-bbbb-bbbbbbbbbbbb\":1}' WHERE sync_id='%s';",
                       hash, sync_shared);
        free(harness_sqlite_query_line(foreign, sql));
    }
    dev = harness_sqlite_query_line(local, "SELECT device_id FROM devices LIMIT 1;");
    ASSERT_TRUE(dev != NULL);
    {
        char *sid = NULL;
        char vv_l[VV_BUF];
        char sql[SQL_BUF];

        sid = harness_sqlite_query_line(local, "SELECT id FROM entries WHERE key='shared';");
        ASSERT_TRUE(sid != NULL);
        (void)snprintf(vv_l, sizeof(vv_l), "{\"%s\":1}", dev);
        (void)snprintf(sql, sizeof(sql), "UPDATE entries SET version_vector='%s' WHERE id=%s;",
                       vv_l, sid);
        free(harness_sqlite_query_line(local, sql));
        free(sid);
    }

    imp[3] = foreign;
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":1");
    cmd_result_free(&r);

    r = run_remember(local, accept, sizeof(accept) / sizeof(accept[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "cannot free unique key or body hash");
    cmd_result_free(&r);

    free(hash);
    free(dev);
    (void)remove(local);
    (void)remove(foreign);
    {
        char side[PATH_BUF + 16];
        (void)snprintf(side, sizeof(side), "%s.device_id", local);
        (void)remove(side);
    }
    (void)rmdir(dir);
}

TEST(cli_import_missing_source_errors)
{
    char *local = make_temp_db_path();
    CmdResult r;
    const char *list[] = {"--json", "list"};
    const char *imp[] = {"--json", "import", "--from-db", "/tmp/remember-no-such-db-xyz.db"};

    ASSERT_TRUE(local != NULL);
    r = run_remember(local, list, sizeof(list) / sizeof(list[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);

    free(local);
}

/*
 * decision:sync-import-hash-demote — demoting a keyed row onto a contested
 * keyless body_hash fails with clear ASCII (no key=sync_id hatch).
 */
TEST(cli_conflict_accept_demote_hash_escape_refuses)
{
    char *local = make_temp_db_path();
    char *foreign = make_temp_db_path();
    CmdResult r;
    const char *add_kl[] = {"--json", "add", "shared body"};
    const char *add_keyed[] = {"--json", "add", "--key", "slot", "shared body"};
    const char *add_f[] = {"--json", "add", "--key", "slot", "foreign"};
    const char *imp[] = {"--json", "import", "--from-db", NULL};
    const char *accept[] = {"--json", "conflict", "accept", "--id", "1", "--keep", "incoming"};

    ASSERT_TRUE(local != NULL && foreign != NULL);
    r = run_remember(local, add_kl, sizeof(add_kl) / sizeof(add_kl[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(local, add_keyed, sizeof(add_keyed) / sizeof(add_keyed[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(foreign, add_f, sizeof(add_f) / sizeof(add_f[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    imp[3] = foreign;
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":1");
    cmd_result_free(&r);

    r = run_remember(local, accept, sizeof(accept) / sizeof(accept[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "cannot free unique key or body hash");
    cmd_result_free(&r);

    free(local);
    free(foreign);
}

TEST(cli_import_concurrent_reimport_idempotent)
{
    char tmpl[] = "/tmp/remember-reimp-XXXXXX";
    char *dir = NULL;
    char local[PATH_BUF];
    char foreign[PATH_BUF];
    CmdResult r;
    char *dev = NULL;
    char *nconf = NULL;
    const char *add[] = {"--json", "add", "--key", "k", "local-body"};
    const char *imp[] = {"--json", "import", "--from-db", NULL};
    const char *clist[] = {"--json", "conflicts"};

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
        char cmd[PATH_BUF * 2];
        (void)snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", local, foreign);
        ASSERT_EQ_INT(system(cmd), 0);
    }
    force_body(foreign, 1, "incoming-body",
               "abababababababababababababababababababababababababababababababab");
    dev = harness_sqlite_query_line(local, "SELECT device_id FROM devices LIMIT 1;");
    ASSERT_TRUE(dev != NULL);
    {
        char vv_l[VV_BUF];
        char vv_f[VV_BUF];
        (void)snprintf(vv_l, sizeof(vv_l), "{\"%s\":1}", dev);
        (void)snprintf(vv_f, sizeof(vv_f), "{\"dddddddd-dddd-7ddd-bddd-dddddddddddd\":1}");
        force_vv(local, 1, vv_l);
        force_vv(foreign, 1, vv_f);
    }

    imp[3] = foreign;
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":1");
    cmd_result_free(&r);

    r = run_remember(local, clist, sizeof(clist) / sizeof(clist[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"count\":1");
    cmd_result_free(&r);

    /* Re-import identical foreign: local now dominates → unchanged, still 1 row. */
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":0");
    cmd_result_free(&r);

    nconf = harness_sqlite_query_line(local, "SELECT COUNT(*) FROM conflicts;");
    ASSERT_STREQ(nconf != NULL ? nconf : "", "1");
    free(nconf);

    /* Advance foreign VV while still concurrent → refresh same row (no insert). */
    force_body(foreign, 1, "incoming-body-v2",
               "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd");
    force_vv(foreign, 1, "{\"dddddddd-dddd-7ddd-bddd-dddddddddddd\":2}");
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":0");
    cmd_result_free(&r);

    nconf = harness_sqlite_query_line(local, "SELECT COUNT(*) FROM conflicts;");
    ASSERT_STREQ(nconf != NULL ? nconf : "", "1");
    free(nconf);

    r = run_remember(local, clist, sizeof(clist) / sizeof(clist[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"count\":1");
    ASSERT_STR_CONTAINS(r.out, "incoming-body-v2");
    cmd_result_free(&r);

    free(dev);
    (void)remove(local);
    (void)remove(foreign);
    {
        char side[PATH_BUF + 16];
        (void)snprintf(side, sizeof(side), "%s.device_id", local);
        (void)remove(side);
    }
    (void)rmdir(dir);
}

/* Pairwise-max path where a device is present in local VV with clock 0. */
TEST(cli_import_vv_zero_clock_in_a_membership)
{
    char tmpl[] = "/tmp/remember-vv0-XXXXXX";
    char *dir = NULL;
    char local[PATH_BUF];
    char foreign[PATH_BUF];
    CmdResult r;
    char *dev = NULL;
    const char *add[] = {"--json", "add", "--key", "k", "same body"};
    const char *imp[] = {"--json", "import", "--from-db", NULL};

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
        char cmd[PATH_BUF * 2];
        (void)snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", local, foreign);
        ASSERT_EQ_INT(system(cmd), 0);
    }
    dev = harness_sqlite_query_line(local, "SELECT device_id FROM devices LIMIT 1;");
    ASSERT_TRUE(dev != NULL);
    {
        char vv_l[VV_BUF];
        char vv_f[VV_BUF];
        (void)snprintf(vv_l, sizeof(vv_l), "{\"%s\":3,\"aaaaaaaa-aaaa-7aaa-baaa-aaaaaaaaaaaa\":0}",
                       dev);
        (void)snprintf(vv_f, sizeof(vv_f), "{\"aaaaaaaa-aaaa-7aaa-baaa-aaaaaaaaaaaa\":2}");
        force_vv(local, 1, vv_l);
        force_vv(foreign, 1, vv_f);
    }

    imp[3] = foreign;
    r = run_remember(local, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"unchanged\":1");
    cmd_result_free(&r);

    free(dev);
    (void)remove(local);
    (void)remove(foreign);
    {
        char side[PATH_BUF + 16];
        (void)snprintf(side, sizeof(side), "%s.device_id", local);
        (void)remove(side);
    }
    (void)rmdir(dir);
}

void register_sync_import_tests(void)
{
    RUN_TEST(cli_import_inserts_preserves_sync_id_remaps_links_no_device_register);
    RUN_TEST(cli_import_refuses_v3_source);
    RUN_TEST(cli_import_identical_automerge_concurrent_conflict);
    RUN_TEST(cli_import_key_clash_and_hash_clash_exit_0);
    RUN_TEST(cli_import_key_clash_reimport_idempotent);
    RUN_TEST(cli_conflict_accept_keep_both_concurrent_remints);
    RUN_TEST(cli_conflict_accept_keep_both_key_clash_keeps_incoming_sync_id);
    RUN_TEST(cli_import_incoming_dominates_updates);
    RUN_TEST(store_import_no_sidecar_copy_devices_stay_one);
    RUN_TEST(cli_conflict_accept_keep_local_and_incoming);
    RUN_TEST(cli_conflict_accept_keep_incoming_key_clash_demotes);
    RUN_TEST(cli_import_local_dominates_unchanged);
    RUN_TEST(cli_import_soft_deleted_and_expired_rows);
    RUN_TEST(cli_import_conflict_snapshot_json_escapes);
    RUN_TEST(cli_import_and_conflict_human_and_help);
    RUN_TEST(cli_import_conflict_error_paths);
    RUN_TEST(cli_conflict_accept_human);
    RUN_TEST(cli_import_incoming_dominates_keyless_hash_clash);
    RUN_TEST(cli_conflict_accept_incoming_many_tags);
    RUN_TEST(cli_conflict_accept_hash_clash_keep_incoming_and_both_refuse_fake_key);
    RUN_TEST(cli_import_incoming_dominates_expires_deleted_keyless);
    RUN_TEST(cli_import_bad_vv_fails_cleanly);
    RUN_TEST(cli_conflicts_bad_reason_errors);
    RUN_TEST(cli_conflict_accept_concurrent_keep_incoming_demotes_other_key);
    RUN_TEST(cli_conflict_accept_concurrent_keep_incoming_hash_taken_refuses);
    RUN_TEST(cli_import_missing_source_errors);
    RUN_TEST(cli_conflict_accept_demote_hash_escape_refuses);
    RUN_TEST(cli_import_vv_zero_clock_in_a_membership);
    RUN_TEST(cli_import_concurrent_reimport_idempotent);
}

// NOLINTEND(readability-identifier-length,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,android-cloexec-fopen,bugprone-command-processor,cert-env33-c,concurrency-mt-unsafe,bugprone-unchecked-string-to-number-conversion,cert-err34-c)
