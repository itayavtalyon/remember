#include "harness.h"
#include "normalize.h"
#include "register.h"
#include "store.h"
#include "test.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    SCHEMA_V5_LIMIT = 20,
    SCHEMA_V5_COMPARE_SQL = 1024,
    SCHEMA_V5_PREPARE_TRIES = 40,
    DEVICE_COUNTER_BUF = 48
};
enum { SIDECAR_READ_BUF = 80, SIDECAR_FILE_BUF = 128 };
/* Successful sqlite3_exec calls before CREATE settings on a v4 open:
   foreign_keys ON, busy_timeout, foreign_keys OFF, BEGIN IMMEDIATE,
   CREATE bucket_prefixes (the v6 step rebuilds the search index later).
   The next exec is the first one after E1.3 step 3. */
enum { SCHEMA_V5_EXEC_AFTER_STEP3 = 5, SCHEMA_V5_EXEC_TRIES = 60 };

static const char k_hash_a[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static const char k_now[] = "2026-06-15T12:00:00.000Z";
/* U+05E1 U+05B5 U+05E4 U+05B6 U+05E8 (samekh-tsere pe-segol resh). */
static const char k_niqqud_body[] = "\xD7\xA1\xD6\xB5\xD7\xA4\xD6\xB6\xD7\xA8";
static const char k_plain_hebrew[] = "\xD7\xA1\xD7\xA4\xD7\xA8";

static void assert_query_is(const char *db, QueryExpect check)
{
    char *row = harness_sqlite_query_line(db, check.sql);

    ASSERT_TRUE(row != NULL);
    ASSERT_STREQ(row != NULL ? row : "", check.want);
    free(row);
}

static void exec_sql(const char *db, const char *sql)
{
    free(harness_sqlite_query_line(db, sql));
}

/* The v4 fixture's devices row. Open must not copy this into the sidecar. */
static const char k_v4_device[] = "018f6b2e-7c3a-7b2e-8f3a-7c3a7b2e8f3a";
static const char k_sidecar_other[] = "01900000-0000-7000-8000-0000000000ab";

/* path then contents. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void write_text(const char *path, const char *text)
{
    FILE *f = NULL;

    /* NOLINTNEXTLINE(android-cloexec-fopen) */
    f = fopen(path, "w");
    ASSERT_TRUE(f != NULL);
    if (f != NULL) {
        (void)fputs(text, f);
        (void)fclose(f);
    }
}

static char *read_text(const char *path)
{
    FILE *f = NULL;
    char buf[SIDECAR_FILE_BUF];
    size_t n = 0;
    char *out = NULL;

    /* NOLINTNEXTLINE(android-cloexec-fopen) */
    f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    n = fread(buf, 1, sizeof(buf) - 1U, f);
    (void)fclose(f);
    buf[n] = '\0';
    out = (char *)malloc(n + 1U);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, buf, n + 1U);
    return out;
}

static void assert_no_devices_table(const char *db)
{
    char *devices = harness_sqlite_query_line(
        db, "SELECT name FROM sqlite_master WHERE type='table' AND name='devices';");

    ASSERT_TRUE(devices == NULL);
    free(devices);
}

static void free_page(PageResult page)
{
    size_t i = 0;

    for (i = 0; i < page.count; i++) {
        store_entry_free(&page.entries[i]);
    }
    free(page.entries);
}

static Store *given_open(const char *db)
{
    char err[ERR_BUFSIZE];
    Store *s = NULL;

    err[0] = '\0';
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    ASSERT_STREQ(err, "");
    return s;
}

/* v4 shape from before this plan. Unchanged objects use the fresh DDL text so
   sqlite_master matches after migration. FTS is body+tags. conflicts has no origin. */
static void given_v4_brain(const char *db)
{
    exec_sql(db, "CREATE TABLE entries (\n"
                 "  id INTEGER PRIMARY KEY,\n"
                 "  key TEXT,\n"
                 "  body TEXT NOT NULL,\n"
                 "  body_hash TEXT NOT NULL,\n"
                 "  source TEXT NOT NULL,\n"
                 "  created_at TEXT NOT NULL,\n"
                 "  updated_at TEXT NOT NULL,\n"
                 "  expires_at TEXT,\n"
                 "  sync_id TEXT NOT NULL,\n"
                 "  deleted_at TEXT,\n"
                 "  version_vector TEXT NOT NULL\n"
                 ");");
    exec_sql(db, "CREATE UNIQUE INDEX ux_entries_key ON entries(key) WHERE key IS NOT NULL;"
                 "CREATE UNIQUE INDEX ux_entries_bodyhash ON entries(body_hash) WHERE key IS NULL;"
                 "CREATE UNIQUE INDEX ux_entries_sync_id ON entries(sync_id);");
    exec_sql(db, "CREATE TABLE tags (\n"
                 "  id INTEGER PRIMARY KEY,\n"
                 "  name TEXT NOT NULL UNIQUE\n"
                 ");\n"
                 "CREATE TABLE entry_tags (\n"
                 "  entry_id INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE,\n"
                 "  tag_id INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,\n"
                 "  PRIMARY KEY (entry_id, tag_id)\n"
                 ");");
    exec_sql(db, "CREATE VIRTUAL TABLE entries_fts USING fts5(\n"
                 "  body,\n"
                 "  tags,\n"
                 "  tokenize = 'unicode61 remove_diacritics 2'\n"
                 ");");
    exec_sql(db, "CREATE TABLE entry_links (\n"
                 "  from_id    INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE,\n"
                 "  to_id      INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE,\n"
                 "  kind       TEXT NOT NULL CHECK (kind IN ('related', 'supersedes', 'cites')),\n"
                 "  created_at TEXT NOT NULL,\n"
                 "  updated_at TEXT NOT NULL,\n"
                 "  CHECK (from_id != to_id)\n"
                 ");\n"
                 "CREATE UNIQUE INDEX entry_links_edge\n"
                 "  ON entry_links(from_id, to_id, kind);\n"
                 "CREATE INDEX entry_links_to ON entry_links(to_id);");
    exec_sql(db, "CREATE TABLE devices (\n"
                 "  device_id  TEXT PRIMARY KEY,\n"
                 "  first_seen TEXT NOT NULL,\n"
                 "  last_seen  TEXT NOT NULL\n"
                 ");\n"
                 "CREATE TABLE conflicts (\n"
                 "  id            INTEGER PRIMARY KEY,\n"
                 "  sync_id       TEXT NOT NULL,\n"
                 "  reason        TEXT NOT NULL,\n"
                 "  local_json    TEXT NOT NULL,\n"
                 "  incoming_json TEXT NOT NULL,\n"
                 "  created_at    TEXT NOT NULL\n"
                 ");");
    exec_sql(db, "INSERT INTO entries(id, key, body, body_hash, source, created_at, updated_at, "
                 "sync_id, version_vector) VALUES (1, 'pref:editor', 'notes', "
                 "'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa', "
                 "'human', '2026-01-01T00:00:00.000Z', '2026-01-01T00:00:00.000Z', "
                 "'018f6b2e-7c3a-7c00-8000-000000000001', "
                 "'{\"018f6b2e-7c3a-7b2e-8f3a-7c3a7b2e8f3a\":1}');");
    exec_sql(db, "INSERT INTO entries(id, key, body, body_hash, source, created_at, updated_at, "
                 "deleted_at, sync_id, version_vector) VALUES (2, NULL, 'archivedword', "
                 "'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb', "
                 "'human', '2026-01-01T00:00:00.000Z', '2026-01-01T00:00:00.000Z', "
                 "'2026-02-01T00:00:00.000Z', '018f6b2e-7c3a-7c00-8000-000000000002', "
                 "'{\"018f6b2e-7c3a-7b2e-8f3a-7c3a7b2e8f3a\":1}');");
    exec_sql(db, "INSERT INTO tags(id, name) VALUES (1, 'tooling');"
                 "INSERT INTO entry_tags(entry_id, tag_id) VALUES (1, 1);"
                 "INSERT INTO conflicts(id, sync_id, reason, local_json, incoming_json, "
                 "created_at) VALUES (7, '018f6b2e-7c3a-7c00-8000-000000000001', 'concurrent_vv', "
                 "'{\"l\":1}', '{\"i\":1}', '2026-03-01T00:00:00.000Z');");
    exec_sql(db, "INSERT INTO entries_fts(rowid, body, tags) VALUES (1, 'notes', 'tooling'), (2, "
                 "'archivedword', '');"
                 "INSERT INTO devices(device_id, first_seen, last_seen) VALUES "
                 "('018f6b2e-7c3a-7b2e-8f3a-7c3a7b2e8f3a', '2026-01-01T00:00:00.000Z', "
                 "'2026-01-01T00:00:00.000Z');"
                 "PRAGMA user_version=4;");
}

TEST(schema_v5_fresh_keeps_the_v5_objects_v6_did_not_replace)
{
    char *db = make_temp_db_path();
    Store *s = NULL;

    ASSERT_TRUE(db != NULL);
    s = given_open(db);
    store_close(s);

    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "6"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT name FROM pragma_table_info('entries_fts') "
                                             "WHERE cid=0;",
                                      .want = "key"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT name FROM pragma_table_info('entries_fts') "
                                             "WHERE cid=1;",
                                      .want = "body"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT name FROM pragma_table_info('entries_fts') "
                                             "WHERE cid=2;",
                                      .want = "tags"});
    /* v6 replaced the E12 tables (007 §10 settings steps 3 and 4). */
    assert_query_is(
        db, (QueryExpect){.sql = "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND "
                                 "name='bucket_prefixes';",
                          .want = "0"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM pragma_table_info('settings') "
                                             "WHERE name='key';",
                                      .want = "1"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM pragma_table_info('conflicts') "
                                             "WHERE name='origin';",
                                      .want = "1"});
    {
        char *devices = harness_sqlite_query_line(
            db, "SELECT name FROM sqlite_master WHERE type='table' AND name='devices';");
        ASSERT_TRUE(devices == NULL);
        free(devices);
    }
    free(db);
}

TEST(schema_v5_search_finds_key_word)
{
    char *db = make_temp_db_path();
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    SearchQuery q;
    PageResult page;

    ASSERT_TRUE(db != NULL);
    s = given_open(db);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "notes", k_hash_a, "pref:editor", NULL, 0U, "human", NULL, k_now, &act, &e),
        STORE_OK);
    store_entry_free(&e);

    memset(&q, 0, sizeof(q));
    q.query = "editor";
    q.filters.limit = SCHEMA_V5_LIMIT;
    page = store_search(s, &q, k_now).page;
    ASSERT_EQ_STATUS(page.st, STORE_OK);
    ASSERT_EQ_INT((int)page.total, 1);
    ASSERT_STREQ(page.count > 0U ? page.entries[0].key : "", "pref:editor");
    free_page(page);
    store_close(s);
    free(db);
}

TEST(schema_v5_rekey_search_follows_the_new_key)
{
    char *db = make_temp_db_path();
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    SearchQuery q;
    PageResult page;
    long long conflict = 0;

    ASSERT_TRUE(db != NULL);
    s = given_open(db);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "notes", k_hash_a, "pref:editor", NULL, 0U, "human", NULL, k_now, &act, &e),
        STORE_OK);
    store_entry_free(&e);

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_rekey(s, 1,
                                 (RekeyKeys){.key_or_null = NULL, .new_key_or_null = "pref:vim"},
                                 STORE_BIN_LIVE, k_now, &e, &conflict),
                     STORE_OK);
    ASSERT_STREQ(e.key, "pref:vim");
    store_entry_free(&e);

    memset(&q, 0, sizeof(q));
    q.query = "editor";
    q.filters.limit = SCHEMA_V5_LIMIT;
    page = store_search(s, &q, k_now).page;
    ASSERT_EQ_STATUS(page.st, STORE_OK);
    ASSERT_EQ_INT((int)page.total, 0);
    free_page(page);

    memset(&q, 0, sizeof(q));
    q.query = "vim";
    q.filters.limit = SCHEMA_V5_LIMIT;
    page = store_search(s, &q, k_now).page;
    ASSERT_EQ_STATUS(page.st, STORE_OK);
    ASSERT_EQ_INT((int)page.total, 1);
    ASSERT_STREQ(page.count > 0U ? page.entries[0].key : "", "pref:vim");
    free_page(page);
    store_close(s);
    free(db);
}

TEST(schema_v5_search_folds_hebrew_marks_in_the_row)
{
    char *db = make_temp_db_path();
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    SearchQuery q;
    PageResult page;

    ASSERT_TRUE(db != NULL);
    s = given_open(db);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, k_niqqud_body, k_hash_a, NULL, NULL, 0U, "human", NULL, k_now, &act, &e),
        STORE_OK);
    ASSERT_STREQ(e.body, k_niqqud_body);
    store_entry_free(&e);

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_LIVE, k_now, &e), STORE_OK);
    ASSERT_STREQ(e.body, k_niqqud_body);
    store_entry_free(&e);

    memset(&q, 0, sizeof(q));
    q.query = k_plain_hebrew;
    q.filters.limit = SCHEMA_V5_LIMIT;
    page = store_search(s, &q, k_now).page;
    ASSERT_EQ_STATUS(page.st, STORE_OK);
    ASSERT_EQ_INT((int)page.total, 1);
    ASSERT_STREQ(page.count > 0U ? page.entries[0].body : "", k_niqqud_body);
    free_page(page);
    store_close(s);
    free(db);
}

TEST(schema_v5_v4_migrates_to_the_fresh_v6_schema)
{
    char *fresh = make_temp_db_path();
    char *old = make_temp_db_path();
    Store *s = NULL;
    SearchQuery q;
    PageResult page;
    char sql[SCHEMA_V5_COMPARE_SQL];

    ASSERT_TRUE(fresh != NULL);
    ASSERT_TRUE(old != NULL);
    s = given_open(fresh);
    store_close(s);

    given_v4_brain(old);
    assert_query_is(old, (QueryExpect){.sql = "PRAGMA user_version;", .want = "4"});
    s = given_open(old);
    assert_query_is(old, (QueryExpect){.sql = "PRAGMA user_version;", .want = "6"});
    {
        char *devices = harness_sqlite_query_line(
            old, "SELECT name FROM sqlite_master WHERE type='table' AND name='devices';");
        ASSERT_TRUE(devices == NULL);
        free(devices);
    }
    assert_query_is(old, (QueryExpect){.sql = "SELECT COUNT(*) FROM entries_fts;", .want = "2"});
    assert_query_is(old,
                    (QueryExpect){.sql = "SELECT COUNT(*) FROM entries_fts WHERE entries_fts MATCH "
                                         "'archivedword';",
                                  .want = "1"});
    assert_query_is(old,
                    (QueryExpect){.sql = "SELECT id || '|' || sync_id || '|' || reason || '|' || "
                                         "local_json || '|' || incoming_json || '|' || created_at "
                                         "|| '|' || IFNULL(origin, 'NULL') FROM conflicts;",
                                  .want = "7|018f6b2e-7c3a-7c00-8000-000000000001|concurrent_vv|"
                                          "{\"l\":1}|{\"i\":1}|2026-03-01T00:00:00.000Z|NULL"});
    assert_query_is(old,
                    (QueryExpect){.sql = "SELECT rowid FROM entries_fts WHERE entries_fts MATCH "
                                         "'tooling';",
                                  .want = "1"});

    memset(&q, 0, sizeof(q));
    q.query = "editor";
    q.filters.limit = SCHEMA_V5_LIMIT;
    page = store_search(s, &q, k_now).page;
    ASSERT_EQ_STATUS(page.st, STORE_OK);
    ASSERT_EQ_INT((int)page.total, 1);
    free_page(page);
    store_close(s);

    (void)snprintf(sql, sizeof(sql),
                   "ATTACH '%s' AS migrated; SELECT ("
                   "(SELECT COUNT(*) FROM (SELECT IFNULL(sql,'') s FROM main.sqlite_master "
                   "EXCEPT SELECT IFNULL(sql,'') FROM migrated.sqlite_master)) + "
                   "(SELECT COUNT(*) FROM (SELECT IFNULL(sql,'') s FROM migrated.sqlite_master "
                   "EXCEPT SELECT IFNULL(sql,'') FROM main.sqlite_master)));",
                   old);
    assert_query_is(fresh, (QueryExpect){.sql = sql, .want = "0"});
    free(fresh);
    free(old);
}

/* Body/tag search gives the same ids in the same rank order before and after
   v4 -> v5 (fixture query set; no word here is a key part). */
TEST(schema_v5_v4_body_and_tag_search_is_unchanged_by_migration)
{
    static const char *const queries[] = {"notes", "tooling", "archivedword",
                                          "notes OR archivedword OR tooling"};
    enum { NQ = sizeof(queries) / sizeof(queries[0]) };
    char *db = make_temp_db_path();
    char *before[NQ] = {NULL};
    char sql[SCHEMA_V5_COMPARE_SQL];
    Store *s = NULL;
    size_t i = 0;

    ASSERT_TRUE(db != NULL);
    given_v4_brain(db);
    for (i = 0; i < NQ; i++) {
        (void)snprintf(sql, sizeof(sql),
                       "SELECT IFNULL(group_concat(rowid), '-') FROM (SELECT rowid FROM "
                       "entries_fts WHERE entries_fts MATCH '%s' ORDER BY rank, rowid);",
                       queries[i]);
        before[i] = harness_sqlite_query_line(db, sql);
    }
    s = given_open(db);
    store_close(s);
    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "6"});
    for (i = 0; i < NQ; i++) {
        (void)snprintf(sql, sizeof(sql),
                       "SELECT IFNULL(group_concat(rowid), '-') FROM (SELECT rowid FROM "
                       "entries_fts WHERE entries_fts MATCH '%s' ORDER BY rank, rowid);",
                       queries[i]);
        ASSERT_TRUE(before[i] != NULL && strcmp(before[i], "-") != 0);
        assert_query_is(db, (QueryExpect){.sql = sql, .want = before[i] != NULL ? before[i] : ""});
        free(before[i]);
    }
    free(db);
}

TEST(schema_v5_v4_missing_conflicts_creates_the_table)
{
    char *db = make_temp_db_path();
    Store *s = NULL;

    ASSERT_TRUE(db != NULL);
    given_v4_brain(db);
    exec_sql(db, "DROP TABLE conflicts;");
    s = given_open(db);
    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "6"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM pragma_table_info('conflicts') "
                                             "WHERE name='origin';",
                                      .want = "1"});
    store_close(s);
    free(db);
}

#ifdef REMEMBER_TEST_HOOKS
TEST(schema_v5_fts_rebuild_prepare_failure_keeps_v4)
{
    char *db = make_temp_db_path();
    int n = 0;
    int saw = 0;

    ASSERT_TRUE(db != NULL);
    given_v4_brain(db);
    for (n = 0; n < SCHEMA_V5_PREPARE_TRIES && saw == 0; n++) {
        char err[ERR_BUFSIZE];
        Store *s = NULL;

        err[0] = '\0';
        store_test_fail_prepare_after(n);
        s = store_open(db, err, sizeof(err));
        store_test_fail_prepare_after(-1);
        if (s == NULL && strcmp(err, "cannot rebuild search index") == 0) {
            assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "4"});
            saw = 1;
        }
        store_close(s);
    }
    ASSERT_EQ_INT(saw, 1);
    free(db);
}

/* E1.3 is one transaction. A failure after bucket_prefixes is created must
   leave the file at v4 with the old FTS. Reopen then migrates. */
TEST(schema_v5_fault_after_step3_keeps_v4)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;

    ASSERT_TRUE(db != NULL);
    given_v4_brain(db);
    err[0] = '\0';
    store_test_fail_exec_after(SCHEMA_V5_EXEC_AFTER_STEP3);
    s = store_open(db, err, sizeof(err));
    store_test_fail_exec_after(-1);
    ASSERT_TRUE(s == NULL);
    ASSERT_STR_CONTAINS(err, "sqlite:");
    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "4"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT name FROM pragma_table_info('entries_fts') "
                                             "WHERE cid=0;",
                                      .want = "body"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM entries_fts;", .want = "2"});
    assert_query_is(db,
                    (QueryExpect){.sql = "SELECT COUNT(*) FROM entries_fts WHERE entries_fts MATCH "
                                         "'archivedword';",
                                  .want = "1"});
    assert_query_is(
        db, (QueryExpect){.sql = "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND "
                                 "name='bucket_prefixes';",
                          .want = "0"});
    assert_query_is(db,
                    (QueryExpect){.sql = "SELECT COUNT(*) FROM sqlite_master WHERE type='table' "
                                         "AND name='settings';",
                                  .want = "0"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM pragma_table_info('conflicts') "
                                             "WHERE name='origin';",
                                      .want = "0"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM devices;", .want = "1"});

    s = given_open(db);
    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "6"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT name FROM pragma_table_info('entries_fts') "
                                             "WHERE cid=0;",
                                      .want = "key"});
    assert_query_is(db,
                    (QueryExpect){.sql = "SELECT COUNT(*) FROM entries_fts WHERE entries_fts MATCH "
                                         "'archivedword';",
                                  .want = "1"});
    {
        char *devices = harness_sqlite_query_line(
            db, "SELECT name FROM sqlite_master WHERE type='table' AND name='devices';");
        ASSERT_TRUE(devices == NULL);
        free(devices);
    }
    store_close(s);
    free(db);
}

/* Every exec position of a v4 open, not one counted statement: each failed
   open leaves v4 and the old FTS; the first clean open migrates. */
TEST(schema_v5_v4_open_exec_fault_sweep)
{
    char *db = make_temp_db_path();
    Store *s = NULL;
    int n = 0;

    ASSERT_TRUE(db != NULL);
    given_v4_brain(db);
    for (n = 0; n < SCHEMA_V5_EXEC_TRIES && s == NULL; n++) {
        char err[ERR_BUFSIZE];

        err[0] = '\0';
        store_test_fail_exec_after(n);
        s = store_open(db, err, sizeof(err));
        store_test_fail_exec_after(-1);
        if (s == NULL) {
            assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "4"});
            assert_query_is(
                db, (QueryExpect){.sql = "SELECT name FROM pragma_table_info('entries_fts') "
                                         "WHERE cid=0;",
                                  .want = "body"});
            assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM entries_fts WHERE "
                                                     "entries_fts MATCH 'archivedword';",
                                              .want = "1"});
        }
    }
    ASSERT_TRUE(s != NULL);
    ASSERT_TRUE(n > SCHEMA_V5_EXEC_AFTER_STEP3);
    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "6"});
    store_close(s);
    free(db);
}
#endif

static void assert_fold(const char *src, size_t len, const char *want)
{
    char *out = NULL;
    NormStatus ns = fold_hebrew_marks(src, len, &out);

    ASSERT_EQ_STATUS(ns, NORM_OK);
    ASSERT_STREQ(out != NULL ? out : "", want);
    free(out);
}

TEST(fold_hebrew_marks_drops_the_listed_points)
{
    char *out = NULL;
    NormStatus ns = NORM_OK;
    /* alef, tsere, rafe, maqaf, shin-dot, paseq, upper-dot, qamats-qatan. */
    const char marked[] = "\xD7\x90\xD6\xB5\xD6\xBF\xD6\xBE\xD7\x81\xD7\x80\xD7\x84\xD7\x87";
    const char kept[] = "\xD7\x90\xD6\xBE\xD7\x80";
    const char multibyte[] = "\xC3\xA9\xF0\x9F\x98\x80";
    char with_nul[] = {'a', 'b', '\0', 'c'};
    const char edges[] = "\xD6\x90\xD6\x91\xD6\xBD\xD6\xBE\xD6\xBF\xD7\x80\xD7\x81\xD7\x82\xD7\x83"
                         "\xD7\x84\xD7\x85\xD7\x86\xD7\x87\xD7\x88";

    assert_fold("", 0U, "");
    assert_fold(marked, sizeof(marked) - 1U, kept);
    assert_fold(multibyte, sizeof(multibyte) - 1U, multibyte);
    assert_fold(with_nul, sizeof(with_nul), "ab");
    /* Every range edge: U+0590 0591 05BD 05BE 05BF 05C0 05C1 05C2 05C3 05C4 05C5 05C6 05C7 05C8;
       kept U+0590 05BE 05C0 05C3 05C6 05C8. */
    assert_fold(edges, sizeof(edges) - 1U, "\xD6\x90\xD6\xBE\xD7\x80\xD7\x83\xD7\x86\xD7\x88");

    ns = fold_hebrew_marks(NULL, 0U, &out);
    ASSERT_EQ_STATUS(ns, NORM_ERR_INTERNAL);
    ns = fold_hebrew_marks("", 0U, NULL);
    ASSERT_EQ_STATUS(ns, NORM_ERR_INTERNAL);
}

TEST(fold_hebrew_marks_copies_a_truncated_sequence)
{
    const char truncated_lead[] = "\xE0";
    char truncated[] = {truncated_lead[0]};
    char *out = NULL;
    NormStatus ns = fold_hebrew_marks(truncated, sizeof(truncated), &out);
    const char surrogate[] = "\xED\xA0\x80";
    const char *folded = NULL;

    ASSERT_EQ_STATUS(ns, NORM_OK);
    folded = (out != NULL) ? out : "";
    ASSERT_STREQ(folded, truncated_lead);
    free(out);
    out = NULL;

    ns = fold_hebrew_marks(surrogate, sizeof(surrogate) - 1U, &out);
    ASSERT_EQ_STATUS(ns, NORM_OK);
    folded = (out != NULL) ? out : "";
    ASSERT_EQ_INT((int)strlen(folded), (int)(sizeof(surrogate) - 1U));
    free(out);
}

/* v4 brain, sidecar gone: mint a new id. Do not copy the devices row. */
TEST(schema_v5_v4_missing_sidecar_mints_a_new_id)
{
    char *db = make_temp_db_path();
    char *side = NULL;
    char *id = NULL;
    Store *s = NULL;

    ASSERT_TRUE(db != NULL);
    given_v4_brain(db);
    side = harness_sidecar_path(db);
    ASSERT_TRUE(side != NULL);
    s = given_open(db);
    store_close(s);

    id = harness_read_line(side);
    ASSERT_TRUE(id != NULL);
    ASSERT_TRUE(strcmp(id != NULL ? id : "", k_v4_device) != 0);
    assert_no_devices_table(db);
    free(id);
    free(side);
    free(db);
}

TEST(schema_v5_v4_sidecar_is_kept_and_devices_row_is_ignored)
{
    char *db = make_temp_db_path();
    char *side = NULL;
    char *legacy = NULL;
    char *id = NULL;
    char line[SIDECAR_READ_BUF];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;

    ASSERT_TRUE(db != NULL);
    given_v4_brain(db);
    legacy = harness_side_path(db, ".device_id");
    side = harness_sidecar_path(db);
    ASSERT_TRUE(side != NULL && legacy != NULL);
    (void)snprintf(line, sizeof(line), "%s\n", k_sidecar_other);
    write_text(legacy, line);
    s = given_open(db);
    /* The v6 step moved the id into <brain>.d (007 §10 steps 1 and 3). */
    id = harness_read_line(side);
    ASSERT_STREQ(id, k_sidecar_other);
    ASSERT_TRUE(access(legacy, F_OK) != 0);
    free(legacy);
    assert_no_devices_table(db);

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "fresh", k_hash_a, "pref:fresh", NULL, 0U, "human", NULL, k_now, &act, &e),
        STORE_OK);
    ASSERT_TRUE(e.version_vector != NULL && strstr(e.version_vector, k_sidecar_other) != NULL);
    ASSERT_TRUE(e.version_vector != NULL && strstr(e.version_vector, k_v4_device) == NULL);
    store_entry_free(&e);
    store_close(s);
    free(id);
    free(side);
    free(db);
}

static void assert_invalid_sidecar_unchanged(const char *text)
{
    char *db = make_temp_db_path();
    char *side = NULL;
    char err[ERR_BUFSIZE];
    char *before = NULL;
    char *after = NULL;
    Store *s = NULL;

    ASSERT_TRUE(db != NULL);
    s = given_open(db);
    store_close(s);
    side = harness_sidecar_path(db);
    ASSERT_TRUE(side != NULL);
    write_text(side, text);
    before = read_text(side);
    err[0] = '\0';
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s == NULL);
    ASSERT_STR_CONTAINS(err, "invalid device_id sidecar");
    after = read_text(side);
    ASSERT_STREQ(before != NULL ? before : "", after != NULL ? after : "");
    free(before);
    free(after);
    free(side);
    free(db);
}

TEST(schema_v5_invalid_sidecar_is_left_unchanged)
{
    char upper[SIDECAR_READ_BUF];
    char two[SIDECAR_FILE_BUF];

    (void)snprintf(upper, sizeof(upper), "018F6B2E-7C3A-7C00-8000-000000000099\n");
    (void)snprintf(two, sizeof(two), "%s\njunk\n", k_sidecar_other);
    assert_invalid_sidecar_unchanged("not-a-uuid\n");
    assert_invalid_sidecar_unchanged(upper);
    assert_invalid_sidecar_unchanged(two);
    assert_invalid_sidecar_unchanged("");
}

TEST(store_device_new_keeps_the_vector_until_the_next_edit)
{
    char *db = make_temp_db_path();
    Store *s = NULL;
    Entry entry;
    StoreAddAction action = STORE_ADD_CREATED;
    char previous[STORE_DEVICE_ID_LEN + 1];
    char err[ERR_BUFSIZE];
    char saved_vv[SIDECAR_FILE_BUF];
    char old_id[STORE_DEVICE_ID_LEN + 1];
    char old_counter[DEVICE_COUNTER_BUF];
    char new_counter[DEVICE_COUNTER_BUF];
    bool had_prev = false;
    char *side = NULL;
    struct stat st;

    ASSERT_TRUE(db != NULL);
    s = given_open(db);
    if (s == NULL) {
        free(db);
        return;
    }
    memset(&entry, 0, sizeof(entry));
    ASSERT_EQ_STATUS(store_add(s, "helix", k_hash_a, "pref:editor", NULL, 0U, "human", NULL, k_now,
                               &action, &entry),
                     STORE_OK);
    ASSERT_TRUE(entry.version_vector != NULL);
    ASSERT_TRUE(snprintf(saved_vv, sizeof(saved_vv), "%s",
                         entry.version_vector != NULL ? entry.version_vector : "") > 0);
    ASSERT_TRUE(store_device_id(s) != NULL);
    ASSERT_TRUE(snprintf(old_id, sizeof(old_id), "%s",
                         store_device_id(s) != NULL ? store_device_id(s) : "") > 0);
    store_entry_free(&entry);

    err[0] = '\0';
    ASSERT_EQ_STATUS(store_device_new(s, previous, sizeof(previous), &had_prev, err, sizeof(err)),
                     STORE_OK);
    ASSERT_STREQ(err, "");
    ASSERT_TRUE(had_prev);
    ASSERT_STREQ(previous, old_id);
    ASSERT_TRUE(strcmp(store_device_id(s), old_id) != 0);
    side = harness_sidecar_path(db);
    {
        char *line = harness_read_line(side);

        ASSERT_STREQ(line != NULL ? line : "", store_device_id(s));
        free(line);
    }
    ASSERT_EQ_INT(stat(side, &st), 0);
    ASSERT_EQ_STATUS((st.st_mode & (unsigned)PERM_BITS_MASK), DB_FILE_PERMS);

    memset(&entry, 0, sizeof(entry));
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_LIVE, k_now, &entry), STORE_OK);
    ASSERT_STREQ(entry.version_vector, saved_vv);
    store_entry_free(&entry);

    memset(&entry, 0, sizeof(entry));
    ASSERT_EQ_STATUS(store_update(s, 1,
                                  &(StoreUpdate){
                                      .set_body = true,
                                      .body = "nvim",
                                      .body_hash = k_hash_a,
                                  },
                                  k_now, &entry, NULL),
                     STORE_OK);
    (void)snprintf(old_counter, sizeof(old_counter), "\"%s\":1", old_id);
    (void)snprintf(new_counter, sizeof(new_counter), "\"%s\":1", store_device_id(s));
    ASSERT_STR_CONTAINS(entry.version_vector, old_counter);
    ASSERT_STR_CONTAINS(entry.version_vector, new_counter);
    store_entry_free(&entry);
    store_close(s);
    free(side);
    free(db);
}

TEST(store_device_new_missing_file_has_no_previous)
{
    char *db = make_temp_db_path();
    Store *s = NULL;
    Entry entry;
    StoreAddAction action = STORE_ADD_CREATED;
    char previous[STORE_DEVICE_ID_LEN + 1];
    char err[ERR_BUFSIZE];
    char saved_vv[SIDECAR_FILE_BUF];
    bool had_prev = true;
    char *side = NULL;

    ASSERT_TRUE(db != NULL);
    s = given_open(db);
    if (s == NULL) {
        free(db);
        return;
    }
    memset(&entry, 0, sizeof(entry));
    ASSERT_EQ_STATUS(
        store_add(s, "helix", k_hash_a, NULL, NULL, 0U, "human", NULL, k_now, &action, &entry),
        STORE_OK);
    ASSERT_TRUE(snprintf(saved_vv, sizeof(saved_vv), "%s",
                         entry.version_vector != NULL ? entry.version_vector : "") > 0);
    store_entry_free(&entry);
    side = harness_sidecar_path(db);
    ASSERT_EQ_INT(remove(side), 0);

    err[0] = '\0';
    previous[0] = 'x';
    ASSERT_EQ_STATUS(store_device_new(s, previous, sizeof(previous), &had_prev, err, sizeof(err)),
                     STORE_OK);
    ASSERT_FALSE(had_prev);
    ASSERT_STREQ(previous, "");
    {
        char *line = harness_read_line(side);

        ASSERT_STREQ(line != NULL ? line : "", store_device_id(s));
        free(line);
    }
    memset(&entry, 0, sizeof(entry));
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_LIVE, k_now, &entry), STORE_OK);
    ASSERT_STREQ(entry.version_vector, saved_vv);
    store_entry_free(&entry);
    store_close(s);
    free(side);
    free(db);
}

/* device new always mints (Itay 2026-10-13): an invalid file is replaced and
   reported like a missing one. Plain store_open still refuses it (E14.4); the
   device-new opener mints over it. */
TEST(store_device_new_replaces_an_invalid_sidecar)
{
    char *db = make_temp_db_path();
    Store *s = NULL;
    char previous[STORE_DEVICE_ID_LEN + 1];
    char err[ERR_BUFSIZE];
    char old_id[STORE_DEVICE_ID_LEN + 1];
    char want[STORE_DEVICE_ID_LEN + 2];
    bool had_prev = true;
    char *side = NULL;
    char *after = NULL;

    ASSERT_TRUE(db != NULL);
    s = given_open(db);
    if (s == NULL) {
        free(db);
        return;
    }
    ASSERT_TRUE(snprintf(old_id, sizeof(old_id), "%s",
                         store_device_id(s) != NULL ? store_device_id(s) : "") > 0);
    side = harness_sidecar_path(db);
    write_text(side, "GARBAGE\n");
    err[0] = '\0';
    ASSERT_EQ_STATUS(store_device_new(s, previous, sizeof(previous), &had_prev, err, sizeof(err)),
                     STORE_OK);
    ASSERT_TRUE(!had_prev);
    ASSERT_STREQ(previous, "");
    ASSERT_TRUE(strcmp(store_device_id(s), old_id) != 0);
    (void)snprintf(want, sizeof(want), "%s\n", store_device_id(s));
    after = read_text(side);
    ASSERT_STREQ(after != NULL ? after : "", want);
    store_close(s);
    free(after);

    write_text(side, "GARBAGE\n");
    err[0] = '\0';
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s == NULL);
    ASSERT_STR_CONTAINS(err, "invalid device_id sidecar");
    err[0] = '\0';
    s = store_open_opts(db,
                        &(StoreOpenOpts){.busy_timeout_ms = STORE_DEFAULT_BUSY_TIMEOUT_MS,
                                         .replace_invalid_sidecar = true},
                        err, sizeof(err), NULL);
    ASSERT_TRUE(s != NULL);
    (void)snprintf(want, sizeof(want), "%s\n", s != NULL ? store_device_id(s) : "");
    after = read_text(side);
    ASSERT_STREQ(after != NULL ? after : "", want);
    store_close(s);
    free(after);
    free(side);
    free(db);
}

#ifdef REMEMBER_TEST_HOOKS
TEST(store_device_new_rename_failure_keeps_the_old_id)
{
    char *db = make_temp_db_path();
    Store *s = NULL;
    char previous[STORE_DEVICE_ID_LEN + 1];
    char err[ERR_BUFSIZE];
    char old_id[STORE_DEVICE_ID_LEN + 1];
    bool had_prev = false;
    char *side = NULL;
    char *tmp = NULL;

    ASSERT_TRUE(db != NULL);
    s = given_open(db);
    if (s == NULL) {
        free(db);
        return;
    }
    ASSERT_TRUE(snprintf(old_id, sizeof(old_id), "%s",
                         store_device_id(s) != NULL ? store_device_id(s) : "") > 0);
    side = harness_sidecar_path(db);
    store_test_fail_sidecar_rename(1);
    err[0] = '\0';
    ASSERT_EQ_STATUS(store_device_new(s, previous, sizeof(previous), &had_prev, err, sizeof(err)),
                     STORE_ERR_INTERNAL);
    ASSERT_STR_CONTAINS(err, "cannot write device_id sidecar");
    {
        char *line = harness_read_line(side);

        ASSERT_STREQ(line != NULL ? line : "", old_id);
        free(line);
    }
    ASSERT_STREQ(store_device_id(s), old_id);
    /* The failed write leaves no temp file behind: <brain>.d holds device_id only. */
    tmp = harness_side_path(db, ".d");
    ASSERT_TRUE(tmp != NULL);
    if (tmp != NULL) {
        DIR *dir = opendir(tmp);
        const struct dirent *ent = NULL;
        int others = 0;

        ASSERT_TRUE(dir != NULL);
        /* NOLINTNEXTLINE(concurrency-mt-unsafe) -- single-threaded test runner */
        while (dir != NULL && (ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0 &&
                strcmp(ent->d_name, "device_id") != 0) {
                others++;
            }
        }
        if (dir != NULL) {
            (void)closedir(dir);
        }
        ASSERT_EQ_INT(others, 0);
        free(tmp);
    }
    store_close(s);
    free(side);
    free(db);
}

TEST(store_import_probe_prepare_fail_imports_nothing)
{
    char *local = make_temp_db_path();
    char *foreign = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *dst = NULL;
    Store *src = NULL;
    StoreImportCounts counts;

    ASSERT_TRUE(local != NULL && foreign != NULL);
    dst = store_open(local, err, sizeof(err));
    src = store_open(foreign, err, sizeof(err));
    ASSERT_TRUE(dst != NULL && src != NULL);
    store_close(src);
    harness_make_v5_source(foreign);
    /* user_version prepare succeeds; the next prepare is the origin probe. */
    store_test_fail_prepare_after(1);
    memset(&counts, 0, sizeof(counts));
    ASSERT_EQ_STATUS(store_import(dst, foreign, k_now, &counts), STORE_ERR_SQLITE);
    store_test_fail_prepare_after(-1);
    store_close(dst);
    assert_query_is(foreign, (QueryExpect){.sql = "PRAGMA user_version;", .want = "5"});
    assert_query_is(local, (QueryExpect){.sql = "SELECT COUNT(*) FROM entries;", .want = "0"});
    free(local);
    free(foreign);
}
#endif

void register_schema_v5_tests(void)
{
    RUN_TEST(schema_v5_fresh_keeps_the_v5_objects_v6_did_not_replace);
    RUN_TEST(schema_v5_v4_missing_sidecar_mints_a_new_id);
    RUN_TEST(schema_v5_v4_sidecar_is_kept_and_devices_row_is_ignored);
    RUN_TEST(schema_v5_invalid_sidecar_is_left_unchanged);
    RUN_TEST(store_device_new_keeps_the_vector_until_the_next_edit);
    RUN_TEST(store_device_new_missing_file_has_no_previous);
    RUN_TEST(store_device_new_replaces_an_invalid_sidecar);
#ifdef REMEMBER_TEST_HOOKS
    RUN_TEST(store_device_new_rename_failure_keeps_the_old_id);
#endif
    RUN_TEST(schema_v5_search_finds_key_word);
    RUN_TEST(schema_v5_rekey_search_follows_the_new_key);
    RUN_TEST(schema_v5_search_folds_hebrew_marks_in_the_row);
    RUN_TEST(schema_v5_v4_migrates_to_the_fresh_v6_schema);
    RUN_TEST(schema_v5_v4_body_and_tag_search_is_unchanged_by_migration);
    RUN_TEST(schema_v5_v4_missing_conflicts_creates_the_table);
#ifdef REMEMBER_TEST_HOOKS
    RUN_TEST(schema_v5_fts_rebuild_prepare_failure_keeps_v4);
    RUN_TEST(schema_v5_fault_after_step3_keeps_v4);
    RUN_TEST(schema_v5_v4_open_exec_fault_sweep);
    RUN_TEST(store_import_probe_prepare_fail_imports_nothing);
#endif
    RUN_TEST(fold_hebrew_marks_drops_the_listed_points);
    RUN_TEST(fold_hebrew_marks_copies_a_truncated_sequence);
}
