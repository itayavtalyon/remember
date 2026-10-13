#include "harness.h"
#include "normalize.h"
#include "register.h"
#include "store.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SCHEMA_V5_LIMIT = 20, SCHEMA_V5_COMPARE_SQL = 1024, SCHEMA_V5_PREPARE_TRIES = 40 };
/* Successful sqlite3_exec calls before CREATE settings on a v4 open:
   foreign_keys ON, busy_timeout, foreign_keys OFF, BEGIN IMMEDIATE,
   DROP entries_fts, CREATE entries_fts, CREATE bucket_prefixes.
   The next exec is the first one after E1.3 step 3. */
enum { SCHEMA_V5_EXEC_AFTER_STEP3 = 7, SCHEMA_V5_EXEC_TRIES = 40 };

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

TEST(schema_v5_fresh_has_v5_objects)
{
    char *db = make_temp_db_path();
    Store *s = NULL;

    ASSERT_TRUE(db != NULL);
    s = given_open(db);
    store_close(s);

    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "5"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT name FROM pragma_table_info('entries_fts') "
                                             "WHERE cid=0;",
                                      .want = "key"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT name FROM pragma_table_info('entries_fts') "
                                             "WHERE cid=1;",
                                      .want = "body"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT name FROM pragma_table_info('entries_fts') "
                                             "WHERE cid=2;",
                                      .want = "tags"});
    assert_query_is(db,
                    (QueryExpect){.sql = "SELECT name FROM sqlite_master WHERE type='table' AND "
                                         "name='bucket_prefixes';",
                                  .want = "bucket_prefixes"});
    assert_query_is(db,
                    (QueryExpect){.sql = "SELECT name FROM sqlite_master WHERE type='table' AND "
                                         "name='settings';",
                                  .want = "settings"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM pragma_table_info('conflicts') "
                                             "WHERE name='origin';",
                                      .want = "1"});
    assert_query_is(db,
                    (QueryExpect){.sql = "SELECT name FROM sqlite_master WHERE type='table' AND "
                                         "name='devices';",
                                  .want = "devices"});
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
    page = store_search(s, &q, k_now);
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
    page = store_search(s, &q, k_now);
    ASSERT_EQ_STATUS(page.st, STORE_OK);
    ASSERT_EQ_INT((int)page.total, 0);
    free_page(page);

    memset(&q, 0, sizeof(q));
    q.query = "vim";
    q.filters.limit = SCHEMA_V5_LIMIT;
    page = store_search(s, &q, k_now);
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
    page = store_search(s, &q, k_now);
    ASSERT_EQ_STATUS(page.st, STORE_OK);
    ASSERT_EQ_INT((int)page.total, 1);
    ASSERT_STREQ(page.count > 0U ? page.entries[0].body : "", k_niqqud_body);
    free_page(page);
    store_close(s);
    free(db);
}

TEST(schema_v5_v4_migrates_to_the_fresh_schema)
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
    assert_query_is(old, (QueryExpect){.sql = "PRAGMA user_version;", .want = "5"});
    assert_query_is(old, (QueryExpect){.sql = "SELECT COUNT(*) FROM devices;", .want = "1"});
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
    page = store_search(s, &q, k_now);
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

TEST(schema_v5_v4_missing_conflicts_creates_the_table)
{
    char *db = make_temp_db_path();
    Store *s = NULL;

    ASSERT_TRUE(db != NULL);
    given_v4_brain(db);
    exec_sql(db, "DROP TABLE conflicts;");
    s = given_open(db);
    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "5"});
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
    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "5"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT name FROM pragma_table_info('entries_fts') "
                                             "WHERE cid=0;",
                                      .want = "key"});
    assert_query_is(db,
                    (QueryExpect){.sql = "SELECT COUNT(*) FROM entries_fts WHERE entries_fts MATCH "
                                         "'archivedword';",
                                  .want = "1"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM devices;", .want = "1"});
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
    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "5"});
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

void register_schema_v5_tests(void)
{
    RUN_TEST(schema_v5_fresh_has_v5_objects);
    RUN_TEST(schema_v5_search_finds_key_word);
    RUN_TEST(schema_v5_rekey_search_follows_the_new_key);
    RUN_TEST(schema_v5_search_folds_hebrew_marks_in_the_row);
    RUN_TEST(schema_v5_v4_migrates_to_the_fresh_schema);
    RUN_TEST(schema_v5_v4_missing_conflicts_creates_the_table);
#ifdef REMEMBER_TEST_HOOKS
    RUN_TEST(schema_v5_fts_rebuild_prepare_failure_keeps_v4);
    RUN_TEST(schema_v5_fault_after_step3_keeps_v4);
    RUN_TEST(schema_v5_v4_open_exec_fault_sweep);
#endif
    RUN_TEST(fold_hebrew_marks_drops_the_listed_points);
    RUN_TEST(fold_hebrew_marks_copies_a_truncated_sequence);
}
