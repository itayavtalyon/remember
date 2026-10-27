#include "harness.h"
#include "normalize.h"
#include "register.h"
#include "store.h"
#include "test.h"

#include <fcntl.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * Plan 19: the one v5 -> v6 migration (007 §10, 008 §8, 010 §17).
 * A v5 fixture is a brain this build created, rewritten to the v5 shape by
 * harness_make_v5_source, with the rows each test needs added by SQL.
 */

enum { V6_SQL_BUF = 1024, V6_FAULT_TRIES = 200, V6_SEARCH_LIMIT = 20, V6_LINE_SLACK = 8 };

static const char k_now[] = "2026-06-15T12:00:00.000Z";

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

/* An empty v5 brain with its device sidecar. */
static void given_empty_v5_brain(const char *db)
{
    store_close(given_open(db));
    harness_make_v5_source(db);
    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "5"});
}

/* Four memories with gaps in the integer ids, every source, one tag, two
   links, one conflict, and the FTS rows the v5 writer kept. Row 7 is deleted
   and past its expiry; row 9 is live and past it; row 11 expires later. */
static void given_v5_brain(const char *db)
{
    given_empty_v5_brain(db);
    exec_sql(db,
             "INSERT INTO entries(id, key, body, body_hash, source, created_at, updated_at, "
             "expires_at, sync_id, deleted_at, version_vector) VALUES "
             "(3, 'pref:editor', 'notes', printf('%.64c', 'a'), 'agent', "
             "'2026-01-01T00:00:00.000Z', '2026-01-02T00:00:00.000Z', NULL, "
             "'018f6b2e-7c3a-7c00-8000-000000000003', NULL, '{}'),"
             "(7, NULL, 'archivedword', printf('%.64c', 'b'), 'unknown', "
             "'2026-01-01T00:00:00.000Z', '2026-01-01T00:00:00.000Z', "
             "'2020-01-01T00:00:00.000Z', '018f6b2e-7c3a-7c00-8000-000000000007', "
             "'2026-02-01T00:00:00.000Z', '{}'),"
             "(9, 'due', 'due body', printf('%.64c', 'c'), 'human', "
             "'2026-01-01T00:00:00.000Z', '2026-01-01T00:00:00.000Z', "
             "'2020-01-01T00:00:00.000Z', '018f6b2e-7c3a-7c00-8000-000000000009', NULL, '{}'),"
             "(11, 'later', 'later body', printf('%.64c', 'd'), 'tool', "
             "'2026-01-01T00:00:00.000Z', '2026-01-01T00:00:00.000Z', "
             "'2999-01-01T00:00:00.000Z', '018f6b2e-7c3a-7c00-8000-000000000011', NULL, '{}');");
    exec_sql(db, "INSERT INTO tags(id, name) VALUES (4, 'tooling');"
                 "INSERT INTO entry_tags(entry_id, tag_id) VALUES (3, 4);"
                 "INSERT INTO entry_links(from_id, to_id, kind, created_at, updated_at) VALUES "
                 "(3, 9, 'cites', '2026-01-03T00:00:00.000Z', '2026-01-03T00:00:00.000Z'),"
                 "(3, 11, 'related', '2026-01-04T00:00:00.000Z', '2026-01-04T00:00:00.000Z');"
                 "INSERT INTO conflicts(id, sync_id, reason, local_json, incoming_json, "
                 "created_at, origin) VALUES (5, '018f6b2e-7c3a-7c00-8000-000000000003', "
                 "'concurrent_vv', '{\"l\":1}', '{\"i\":1}', '2026-03-01T00:00:00.000Z', NULL);"
                 "INSERT INTO entries_fts(rowid, key, body, tags) VALUES "
                 "(3, 'pref:editor', 'notes', 'tooling'), (7, NULL, 'archivedword', ''), "
                 "(9, 'due', 'due body', ''), (11, 'later', 'later body', '');");
}

static void free_page(PageResult page)
{
    size_t i = 0;

    for (i = 0; i < page.count; i++) {
        store_entry_free(&page.entries[i]);
    }
    free(page.entries);
}

/* M1. */
TEST(schema_v6_old_ids_become_local_id_and_id)
{
    char *db = make_temp_db_path();

    ASSERT_TRUE(db != NULL);
    given_v5_brain(db);
    store_close(given_open(db));

    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "6"});
    assert_query_is(db,
                    (QueryExpect){.sql = "SELECT group_concat(local_id || '=' || id, ' ') FROM "
                                         "(SELECT local_id, id FROM entries ORDER BY local_id);",
                                  .want = "3=018f6b2e-7c3a-7c00-8000-000000000003 "
                                          "7=018f6b2e-7c3a-7c00-8000-000000000007 "
                                          "9=018f6b2e-7c3a-7c00-8000-000000000009 "
                                          "11=018f6b2e-7c3a-7c00-8000-000000000011"});
    /* Every other value is kept. */
    assert_query_is(db, (QueryExpect){.sql = "SELECT key || '|' || body || '|' || created_at || "
                                             "'|' || updated_at || '|' || version_vector FROM "
                                             "entries WHERE local_id = 3;",
                                      .want = "pref:editor|notes|2026-01-01T00:00:00.000Z|"
                                              "2026-01-02T00:00:00.000Z|{}"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT deleted_at || '|' || expires_at FROM entries "
                                             "WHERE local_id = 7;",
                                      .want = "2026-02-01T00:00:00.000Z|2020-01-01T00:00:00.000Z"});
    assert_query_is(
        db, (QueryExpect){.sql = "SELECT COUNT(*) FROM pragma_table_info('entries') WHERE name IN "
                                 "('sync_id', 'source');",
                          .want = "0"});
    free(db);
}

/* M2: the integer foreign keys stay on local_id. */
TEST(schema_v6_links_tags_conflicts_and_fts_resolve_through_local_id)
{
    char *db = make_temp_db_path();
    Store *s = NULL;
    StoreNeighbor *rows = NULL;
    size_t nrows = 0U;
    SearchQuery q;
    PageResult page;
    Entry e;
    char *link_id = NULL;

    ASSERT_TRUE(db != NULL);
    given_v5_brain(db);
    s = given_open(db);

    {
        char *dangling = harness_sqlite_query_line(db, "PRAGMA foreign_key_check;");

        ASSERT_TRUE(dangling == NULL);
        free(dangling);
    }
    assert_query_is(db, (QueryExpect){.sql = "PRAGMA integrity_check;", .want = "ok"});
    assert_query_is(db,
                    (QueryExpect){.sql = "SELECT group_concat(from_id || '>' || to_id || ' ' || "
                                         "kind || ' ' || version_vector || ' ' || "
                                         "IFNULL(deleted_at, 'NULL') || ' ' || change_seq, ', ') "
                                         "FROM (SELECT * FROM entry_links ORDER BY rowid);",
                                  .want = "3>9 cites {} NULL 0, 3>11 related {} NULL 0"});
    link_id = harness_sqlite_query_line(db, "SELECT id FROM entry_links WHERE kind = 'cites';");
    ASSERT_TRUE(uuid_v7_is_canonical(link_id));
    free(link_id);
    assert_query_is(
        db, (QueryExpect){.sql = "SELECT COUNT(DISTINCT id) FROM entry_links;", .want = "2"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT e.id FROM entry_tags et JOIN entries e ON "
                                             "e.local_id = et.entry_id;",
                                      .want = "018f6b2e-7c3a-7c00-8000-000000000003"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT id || '|' || sync_id || '|' || reason FROM "
                                             "conflicts;",
                                      .want = "5|018f6b2e-7c3a-7c00-8000-000000000003|"
                                              "concurrent_vv"});

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get(s, 3, STORE_BIN_LIVE, k_now, &e), STORE_OK);
    ASSERT_STREQ(e.sync_id, "018f6b2e-7c3a-7c00-8000-000000000003");
    ASSERT_EQ_INT((int)e.ntags, 1);
    ASSERT_STREQ(e.ntags > 0U ? e.tags[0] : "", "tooling");
    store_entry_free(&e);
    ASSERT_EQ_STATUS(store_list_neighbors(s, 3, NULL, STORE_NEIGHBOR_ALL, k_now, &rows, &nrows),
                     STORE_OK);
    ASSERT_EQ_INT((int)nrows, 2);
    ASSERT_STREQ(nrows > 1U ? rows[1].neighbor_sync_id : "",
                 "018f6b2e-7c3a-7c00-8000-000000000009");
    store_neighbors_free(rows, nrows);

    memset(&q, 0, sizeof(q));
    q.query = "notes";
    q.filters.limit = V6_SEARCH_LIMIT;
    page = store_search(s, &q, k_now).page;
    ASSERT_EQ_STATUS(page.st, STORE_OK);
    ASSERT_EQ_INT((int)page.total, 1);
    ASSERT_EQ_INT(page.count > 0U ? (int)page.entries[0].id : 0, 3);
    free_page(page);
    store_close(s);
    free(db);
}

/* 008-14 (the migration half). */
TEST(schema_v6_brain_meta_has_one_row_and_tombstones_are_empty)
{
    char *db = make_temp_db_path();
    char *brain_id = NULL;

    ASSERT_TRUE(db != NULL);
    given_v5_brain(db);
    store_close(given_open(db));

    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM brain_meta;", .want = "1"});
    brain_id = harness_sqlite_query_line(db, "SELECT brain_id FROM brain_meta;");
    ASSERT_TRUE(uuid_v7_is_canonical(brain_id));
    free(brain_id);
    assert_query_is(db, (QueryExpect){.sql = "SELECT change_seq || '|' || min_reader_version FROM "
                                             "brain_meta;",
                                      .want = "0|0.3.0"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM tombstones;", .want = "0"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM entries WHERE change_seq != 0;",
                                      .want = "0"});
    assert_query_is(db,
                    (QueryExpect){.sql = "SELECT group_concat(name, ',') FROM (SELECT name FROM "
                                         "pragma_table_info('tombstones') ORDER BY cid);",
                                  .want = "id,change_seq,purged_at"});
    free(db);
}

/* Actor backfill (008 §8 step 1): kind copies source, unknown becomes human. */
TEST(schema_v6_actors_come_from_the_old_source)
{
    char *db = make_temp_db_path();

    ASSERT_TRUE(db != NULL);
    given_v5_brain(db);
    store_close(given_open(db));

    assert_query_is(db,
                    (QueryExpect){.sql = "SELECT group_concat(local_id || ' ' || created_by, "
                                         "', ') FROM (SELECT * FROM entries ORDER BY local_id);",
                                  .want = "3 {\"kind\":\"agent\",\"id\":null}, "
                                          "7 {\"kind\":\"human\",\"id\":null}, "
                                          "9 {\"kind\":\"human\",\"id\":null}, "
                                          "11 {\"kind\":\"tool\",\"id\":null}"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM entries WHERE updated_by IS NOT "
                                             "created_by;",
                                      .want = "0"});
    free(db);
}

/* 010-14, the migration clause: due rows are marked, and nothing is emitted. */
TEST(schema_v6_due_rows_get_expired_at_and_the_outbox_is_empty)
{
    char *db = make_temp_db_path();

    ASSERT_TRUE(db != NULL);
    given_v5_brain(db);
    store_close(given_open(db));

    assert_query_is(db, (QueryExpect){.sql = "SELECT group_concat(local_id || '=' || "
                                             "IFNULL(expired_at, 'NULL'), ' ') FROM (SELECT * FROM "
                                             "entries ORDER BY local_id);",
                                      .want = "3=NULL 7=NULL 9=2020-01-01T00:00:00.000Z 11=NULL"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM outbox;", .want = "0"});
    assert_query_is(db,
                    (QueryExpect){.sql = "SELECT group_concat(name, ',') FROM (SELECT name FROM "
                                         "pragma_table_info('outbox') ORDER BY cid);",
                                  .want = "event_id,type,payload,recipients,created_at"});
    free(db);
}

/* 007-14: the E12.7 effective list, with no item limit (10 stay 10). The order
   list comes first, without its non-live item; the rest follow by added_at. */
TEST(schema_v6_prefixes_become_one_row_with_every_live_prefix)
{
    char *db = make_temp_db_path();
    Store *s = NULL;
    char sql[V6_SQL_BUF];

    ASSERT_TRUE(db != NULL);
    given_empty_v5_brain(db);
    exec_sql(db, "INSERT INTO bucket_prefixes(prefix, added_at, removed_at, device_id) VALUES "
                 "('p01:', '2026-01-01T00:00:00.000Z', NULL, 'd'),"
                 "('p02:', '2026-01-02T00:00:00.000Z', NULL, 'd'),"
                 "('p03:', '2026-01-03T00:00:00.000Z', NULL, 'd'),"
                 "('p04:', '2026-01-04T00:00:00.000Z', NULL, 'd'),"
                 "('p05:', '2026-01-05T00:00:00.000Z', NULL, 'd'),"
                 "('p06:', '2026-01-06T00:00:00.000Z', NULL, 'd'),"
                 "('p07:', '2026-01-07T00:00:00.000Z', NULL, 'd'),"
                 "('p08:', '2026-01-08T00:00:00.000Z', NULL, 'd'),"
                 "('p09:', '2026-01-09T00:00:00.000Z', '2026-01-01T00:00:00.000Z', 'd'),"
                 "('p10:', '2026-01-10T00:00:00.000Z', NULL, 'd'),"
                 "('gone:', '2026-01-01T00:00:00.000Z', '2026-02-01T00:00:00.000Z', 'd');"
                 "INSERT INTO settings(name, value, updated_at) VALUES ('bucket_prefix_order', "
                 "'[\"p10:\",\"gone:\",\"p03:\"]', '2026-03-01T00:00:00.000Z');");
    s = given_open(db);
    ASSERT_EQ_INT((int)store_open_warning_count(s), 0);

    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM settings;", .want = "1"});
    assert_query_is(
        db,
        (QueryExpect){.sql = "SELECT value FROM settings WHERE key = 'remember.buckets.prefixes';",
                      .want = "[\"p10:\",\"p03:\",\"p01:\",\"p02:\",\"p04:\",\"p05:\",\"p06:\","
                              "\"p07:\",\"p08:\",\"p09:\"]"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT updated_by || '|' || updated_at || '|' || "
                                             "IFNULL(deleted_at, 'NULL') || '|' || change_seq FROM "
                                             "settings;",
                                      .want = "{\"kind\":\"human\",\"id\":null}|"
                                              "2026-03-01T00:00:00.000Z|NULL|1"});
    (void)snprintf(sql, sizeof(sql),
                   "SELECT (device_id = '%s') || '|' || (version_vector = '{\"%s\":1}') FROM "
                   "settings;",
                   store_device_id(s), store_device_id(s));
    assert_query_is(db, (QueryExpect){.sql = sql, .want = "1|1"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT change_seq FROM brain_meta;", .want = "1"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM sqlite_master WHERE name IN "
                                             "('bucket_prefixes', 'settings_v6', "
                                             "'settings_history_v6');",
                                      .want = "0"});
    assert_query_is(db,
                    (QueryExpect){.sql = "SELECT COUNT(*) FROM settings_history;", .want = "0"});
    store_close(s);
    free(db);
}

/* No order row: updated_at is the migration time. */
TEST(schema_v6_prefixes_without_an_order_take_the_migration_time)
{
    char *db = make_temp_db_path();
    char *at = NULL;

    ASSERT_TRUE(db != NULL);
    given_empty_v5_brain(db);
    exec_sql(db, "INSERT INTO bucket_prefixes(prefix, added_at, removed_at, device_id) VALUES "
                 "('b:', '2026-01-02T00:00:00.000Z', NULL, 'd'),"
                 "('a:', '2026-01-02T00:00:00.000Z', NULL, 'd');");
    store_close(given_open(db));

    assert_query_is(db,
                    (QueryExpect){.sql = "SELECT value FROM settings;", .want = "[\"a:\",\"b:\"]"});
    at = harness_sqlite_query_line(db, "SELECT updated_at FROM settings;");
    ASSERT_TRUE(utc_stamp_is_canonical(at));
    ASSERT_TRUE(at != NULL && strcmp(at, "2026-03-01T00:00:00.000Z") > 0);
    free(at);
    free(db);
}

/* An order value that is not a JSON array counts as no order. */
TEST(schema_v6_an_order_that_is_not_an_array_is_ignored)
{
    char *db = make_temp_db_path();

    ASSERT_TRUE(db != NULL);
    given_empty_v5_brain(db);
    exec_sql(db, "INSERT INTO bucket_prefixes(prefix, added_at, removed_at, device_id) VALUES "
                 "('b:', '2026-01-01T00:00:00.000Z', NULL, 'd'),"
                 "('a:', '2026-01-02T00:00:00.000Z', NULL, 'd');"
                 "INSERT INTO settings(name, value, updated_at) VALUES ('bucket_prefix_order', "
                 "'{\"a:\":0}', '2026-03-01T00:00:00.000Z');");
    store_close(given_open(db));

    assert_query_is(db, (QueryExpect){.sql = "SELECT value || '|' || updated_at FROM settings;",
                                      .want = "[\"b:\",\"a:\"]|2026-03-01T00:00:00.000Z"});
    free(db);
}

/* 007-14: a bad prefix is left out and warns; a folded duplicate too. */
TEST(schema_v6_bad_and_duplicate_prefixes_are_left_out_and_warn)
{
    char *db = make_temp_db_path();
    Store *s = NULL;
    StoreSettingWarning w;

    ASSERT_TRUE(db != NULL);
    given_empty_v5_brain(db);
    exec_sql(db, "INSERT INTO bucket_prefixes(prefix, added_at, removed_at, device_id) VALUES "
                 "('project:', '2026-01-01T00:00:00.000Z', NULL, 'd'),"
                 "('my project:', '2026-01-02T00:00:00.000Z', NULL, 'd'),"
                 "('Project:', '2026-01-03T00:00:00.000Z', NULL, 'd'),"
                 "('Area:', '2026-01-04T00:00:00.000Z', NULL, 'd');");
    s = given_open(db);

    assert_query_is(db, (QueryExpect){.sql = "SELECT value FROM settings;",
                                      .want = "[\"project:\",\"area:\"]"});
    ASSERT_EQ_INT((int)store_open_warning_count(s), 2);
    w = store_open_warning(s, 0U);
    ASSERT_STREQ(w.key, "remember.buckets.prefixes");
    ASSERT_STREQ(w.value, "my project:");
    ASSERT_STREQ(w.reason, "bad_prefix_shape");
    w = store_open_warning(s, 1U);
    ASSERT_STREQ(w.value, "Project:");
    ASSERT_STREQ(w.reason, "duplicate_prefix");
    w = store_open_warning(s, 2U);
    ASSERT_TRUE(w.key == NULL && w.value == NULL && w.reason == NULL);
    ASSERT_EQ_INT((int)store_open_warning_count(NULL), 0);
    w = store_open_warning(NULL, 0U);
    ASSERT_TRUE(w.key == NULL);
    store_close(s);

    /* The warnings belong to the open that migrated. */
    s = given_open(db);
    ASSERT_EQ_INT((int)store_open_warning_count(s), 0);
    store_close(s);
    free(db);
}

/* Every live prefix fails: no row, and the counter stays 0. */
TEST(schema_v6_every_prefix_bad_writes_no_row)
{
    char *db = make_temp_db_path();
    Store *s = NULL;

    ASSERT_TRUE(db != NULL);
    given_empty_v5_brain(db);
    exec_sql(db, "INSERT INTO bucket_prefixes(prefix, added_at, removed_at, device_id) VALUES "
                 "('no colon', '2026-01-01T00:00:00.000Z', NULL, 'd'),"
                 "(':', '2026-01-02T00:00:00.000Z', NULL, 'd');");
    s = given_open(db);
    ASSERT_EQ_INT((int)store_open_warning_count(s), 2);
    store_close(s);

    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM settings;", .want = "0"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT change_seq FROM brain_meta;", .want = "0"});
    free(db);
}

/* ST-10.1 (the empty E12 row). */
TEST(schema_v6_empty_e12_writes_no_row)
{
    char *db = make_temp_db_path();

    ASSERT_TRUE(db != NULL);
    given_v5_brain(db);
    store_close(given_open(db));

    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM settings;", .want = "0"});
    assert_query_is(db,
                    (QueryExpect){.sql = "SELECT COUNT(*) FROM settings_history;", .want = "0"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT change_seq FROM brain_meta;", .want = "0"});
    free(db);
}

/* A fresh brain takes the same one step, so the two schemas are equal. */
TEST(schema_v6_fresh_and_migrated_schemas_are_equal)
{
    char *fresh = make_temp_db_path();
    char *old = make_temp_db_path();
    char sql[V6_SQL_BUF];

    ASSERT_TRUE(fresh != NULL && old != NULL);
    store_close(given_open(fresh));
    given_v5_brain(old);
    store_close(given_open(old));
    (void)snprintf(sql, sizeof(sql),
                   "ATTACH '%s' AS migrated; SELECT ("
                   "(SELECT COUNT(*) FROM (SELECT IFNULL(sql,'') s FROM main.sqlite_master "
                   "EXCEPT SELECT IFNULL(sql,'') FROM migrated.sqlite_master)) + "
                   "(SELECT COUNT(*) FROM (SELECT IFNULL(sql,'') s FROM migrated.sqlite_master "
                   "EXCEPT SELECT IFNULL(sql,'') FROM main.sqlite_master)));",
                   old);
    assert_query_is(fresh, (QueryExpect){.sql = sql, .want = "0"});
    assert_query_is(fresh, (QueryExpect){.sql = "PRAGMA user_version;", .want = "6"});
    free(fresh);
    free(old);
}

/* New writes keep today's actor behavior, through the migration's map. */
TEST(schema_v6_a_new_write_stores_the_source_as_an_actor)
{
    char *db = make_temp_db_path();
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    ListQuery q;
    PageResult page;

    ASSERT_TRUE(db != NULL);
    s = given_open(db);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_add(s, "one",
                               "1111111111111111111111111111111111111111111111111111111111111111",
                               NULL, NULL, 0U, "unknown", NULL, k_now, &act, &e),
                     STORE_OK);
    ASSERT_STREQ(e.source, "human");
    store_entry_free(&e);
    ASSERT_EQ_STATUS(store_add(s, "two",
                               "2222222222222222222222222222222222222222222222222222222222222222",
                               NULL, NULL, 0U, "agent", NULL, k_now, &act, &e),
                     STORE_OK);
    ASSERT_STREQ(e.source, "agent");
    store_entry_free(&e);
    assert_query_is(db,
                    (QueryExpect){.sql = "SELECT group_concat(created_by || updated_by, ' ') FROM "
                                         "(SELECT * FROM entries ORDER BY local_id);",
                                  .want = "{\"kind\":\"human\",\"id\":null}"
                                          "{\"kind\":\"human\",\"id\":null} "
                                          "{\"kind\":\"agent\",\"id\":null}"
                                          "{\"kind\":\"agent\",\"id\":null}"});

    /* The source filter reads the actor kind. */
    memset(&q, 0, sizeof(q));
    q.source = "agent";
    q.limit = V6_SEARCH_LIMIT;
    page = store_list(s, &q, k_now);
    ASSERT_EQ_STATUS(page.st, STORE_OK);
    ASSERT_EQ_INT((int)page.total, 1);
    ASSERT_STREQ(page.count > 0U ? page.entries[0].body : "", "two");
    free_page(page);
    /* unknown is stored as human, and the filter maps it the same way. */
    q.source = "unknown";
    page = store_list(s, &q, k_now);
    ASSERT_EQ_INT((int)page.total, 1);
    ASSERT_STREQ(page.count > 0U ? page.entries[0].body : "", "one");
    free_page(page);
    store_close(s);
    free(db);
}

/* ---- change_seq on writes (008 JE-5.2; Itay 2026-10-25: bump it now) ---- */

static const char k_hash_one[] = "1111111111111111111111111111111111111111111111111111111111111111";
static const char k_hash_two[] = "2222222222222222222222222222222222222222222222222222222222222222";

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void assert_seqs(const char *db, const char *want_entries, const char *want_brain)
{
    assert_query_is(db, (QueryExpect){.sql = "SELECT group_concat(local_id || ':' || change_seq, "
                                             "' ') FROM (SELECT * FROM entries ORDER BY local_id);",
                                      .want = want_entries});
    assert_query_is(db,
                    (QueryExpect){.sql = "SELECT change_seq FROM brain_meta;", .want = want_brain});
}

static long long given_added(Store *s, const char *body, const char *hash, const char *tag)
{
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    const char *tags[] = {tag};
    long long id = 0;

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, body, hash, NULL, tags, tag != NULL ? 1U : 0U, "human", NULL, k_now, &act, &e),
        STORE_OK);
    id = e.id;
    store_entry_free(&e);
    return id;
}

/* Each changed row takes the next value once per write; a no-op takes none. */
TEST(schema_v6_each_changed_entry_takes_the_next_counter_value_once)
{
    char *db = make_temp_db_path();
    Store *s = NULL;
    Entry e;
    StoreUpdate u;

    ASSERT_TRUE(db != NULL);
    s = given_open(db);
    ASSERT_EQ_INT((int)given_added(s, "one", k_hash_one, NULL), 1);
    assert_seqs(db, "1:1", "1");
    /* The same keyless body with nothing new changes nothing. */
    (void)given_added(s, "one", k_hash_one, NULL);
    assert_seqs(db, "1:1", "1");
    /* A merge that adds a tag changes the row: one value. */
    (void)given_added(s, "one", k_hash_one, "t");
    assert_seqs(db, "1:2", "2");

    /* An update touching body, tags and the vector still takes one value. */
    memset(&u, 0, sizeof(u));
    u.set_body = true;
    u.body = "uno";
    u.body_hash = k_hash_two;
    u.set_tags = true;
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_update(s, 1, &u, k_now, &e, NULL), STORE_OK);
    store_entry_free(&e);
    assert_seqs(db, "1:3", "3");

    ASSERT_EQ_STATUS(store_soft_delete_by_id(s, 1, k_now, &e), STORE_OK);
    store_entry_free(&e);
    assert_seqs(db, "1:4", "4");
    memset(&u, 0, sizeof(u));
    u.undelete = true;
    u.bin = STORE_BIN_DELETED;
    ASSERT_EQ_STATUS(store_update(s, 1, &u, k_now, &e, NULL), STORE_OK);
    store_entry_free(&e);
    assert_seqs(db, "1:5", "5");
    ASSERT_EQ_STATUS(store_rekey(s, 1, (RekeyKeys){.key_or_null = NULL, .new_key_or_null = "k"},
                                 STORE_BIN_LIVE, k_now, &e, NULL),
                     STORE_OK);
    store_entry_free(&e);
    assert_seqs(db, "1:6", "6");
    store_close(s);
    free(db);
}

/* A write that rolls back takes no value, and the next write starts clean. */
TEST(schema_v6_a_rolled_back_write_leaves_the_counter)
{
    char *db = make_temp_db_path();
    Store *s = NULL;
    Entry e;
    StoreUpdate u;
    long long other = 0;

    ASSERT_TRUE(db != NULL);
    s = given_open(db);
    (void)given_added(s, "one", k_hash_one, NULL);
    (void)given_added(s, "two", k_hash_two, NULL);
    memset(&u, 0, sizeof(u));
    u.set_body = true;
    u.body = "two";
    u.body_hash = k_hash_two;
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_update(s, 1, &u, k_now, &e, &other), STORE_ERR_CONFLICT);
    assert_seqs(db, "1:1 2:2", "2");
    memset(&u, 0, sizeof(u));
    u.set_tags = true;
    ASSERT_EQ_STATUS(store_update(s, 1, &u, k_now, &e, NULL), STORE_OK);
    store_entry_free(&e);
    assert_seqs(db, "1:3 2:2", "3");
    store_close(s);
    free(db);
}

/* link: the link row and both ends; unlink: both ends. */
TEST(schema_v6_link_and_unlink_take_counter_values)
{
    char *db = make_temp_db_path();
    Store *s = NULL;
    StoreNeighbor stub;
    StoreNeighbor *stubs = NULL;
    size_t nstubs = 0U;
    StoreLinkAction action = STORE_LINK_CREATED;

    ASSERT_TRUE(db != NULL);
    s = given_open(db);
    (void)given_added(s, "one", k_hash_one, NULL);
    (void)given_added(s, "two", k_hash_two, NULL);
    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(store_link(s, (StoreEdge){.from_id = 1, .to_id = 2}, STORE_EDGE_CITES, k_now,
                                &action, &stub),
                     STORE_OK);
    store_neighbor_free(&stub);
    assert_query_is(db, (QueryExpect){.sql = "SELECT change_seq FROM entry_links;", .want = "3"});
    assert_seqs(db, "1:4 2:5", "5");
    /* Linking again touches the edge: three new values. */
    ASSERT_EQ_STATUS(store_link(s, (StoreEdge){.from_id = 1, .to_id = 2}, STORE_EDGE_CITES, k_now,
                                &action, &stub),
                     STORE_OK);
    ASSERT_EQ_INT((int)action, (int)STORE_LINK_MERGED);
    store_neighbor_free(&stub);
    assert_query_is(db, (QueryExpect){.sql = "SELECT change_seq FROM entry_links;", .want = "6"});
    assert_seqs(db, "1:7 2:8", "8");
    ASSERT_EQ_STATUS(store_unlink(s, 1, 2, NULL, k_now, &stubs, &nstubs), STORE_OK);
    store_neighbors_free(stubs, nstubs);
    assert_seqs(db, "1:9 2:10", "10");
    store_close(s);
    free(db);
}

/* Purge deletes the row and writes one tombstone with the next value. */
TEST(schema_v6_purge_writes_one_tombstone_per_row)
{
    char *db = make_temp_db_path();
    Store *s = NULL;
    Entry e;
    Entry *rows = NULL;
    size_t nrows = 0U;
    char sql[V6_SQL_BUF];
    char id_one[STORE_DEVICE_ID_LEN + 1];

    ASSERT_TRUE(db != NULL);
    s = given_open(db);
    (void)given_added(s, "one", k_hash_one, NULL);
    (void)given_added(s, "two", k_hash_two, NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_soft_delete_by_id(s, 1, k_now, &e), STORE_OK);
    (void)snprintf(id_one, sizeof(id_one), "%s", e.sync_id);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(store_hard_delete_by_id(s, 1, STORE_BIN_DELETED, k_now, &e), STORE_OK);
    store_entry_free(&e);
    (void)snprintf(sql, sizeof(sql),
                   "SELECT (id = '%s') || '|' || change_seq || '|' || purged_at FROM tombstones;",
                   id_one);
    assert_query_is(db, (QueryExpect){.sql = sql, .want = "1|4|2026-06-15T12:00:00.000Z"});
    assert_seqs(db, "2:2", "4");

    ASSERT_EQ_STATUS(store_soft_delete_by_id(s, 2, k_now, &e), STORE_OK);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(store_purge(s, STORE_BIN_DELETED, k_now, &rows, &nrows), STORE_OK);
    ASSERT_EQ_INT((int)nrows, 1);
    store_entry_free(&rows[0]);
    free(rows);
    assert_query_is(db, (QueryExpect){.sql = "SELECT group_concat(change_seq, ' ') FROM (SELECT * "
                                             "FROM tombstones ORDER BY change_seq);",
                                      .want = "4 6"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT change_seq FROM brain_meta;", .want = "6"});
    store_close(s);
    free(db);
}

/* After a migration that wrote the prefix row (counter 1), the next write is 2. */
TEST(schema_v6_the_first_write_after_a_prefix_row_is_two)
{
    char *db = make_temp_db_path();
    Store *s = NULL;

    ASSERT_TRUE(db != NULL);
    given_empty_v5_brain(db);
    exec_sql(db, "INSERT INTO bucket_prefixes(prefix, added_at, removed_at, device_id) VALUES "
                 "('a:', '2026-01-01T00:00:00.000Z', NULL, 'd');");
    s = given_open(db);
    (void)given_added(s, "one", k_hash_one, NULL);
    assert_seqs(db, "1:2", "2");
    store_close(s);
    free(db);
}

/* Import: each written row takes a value; an identical re-import takes none. */
TEST(schema_v6_import_stamps_written_rows_only)
{
    char *db = make_temp_db_path();
    char *peer = make_temp_db_path();
    Store *s = NULL;
    StoreImportCounts counts;
    StoreNeighbor stub;
    StoreLinkAction action = STORE_LINK_CREATED;
    char hash_one[REMEMBER_SHA256_HEX_LEN + 1];
    char hash_two[REMEMBER_SHA256_HEX_LEN + 1];

    ASSERT_TRUE(db != NULL && peer != NULL);
    /* Import checks each body against its hash. */
    body_hash_hex("one", strlen("one"), hash_one);
    body_hash_hex("two", strlen("two"), hash_two);
    s = given_open(peer);
    (void)given_added(s, "one", hash_one, NULL);
    (void)given_added(s, "two", hash_two, NULL);
    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(store_link(s, (StoreEdge){.from_id = 1, .to_id = 2}, STORE_EDGE_CITES, k_now,
                                &action, &stub),
                     STORE_OK);
    store_neighbor_free(&stub);
    store_close(s);
    harness_make_v5_source(peer);

    s = given_open(db);
    memset(&counts, 0, sizeof(counts));
    ASSERT_EQ_STATUS(store_import(s, peer, k_now, &counts), STORE_OK);
    ASSERT_EQ_INT((int)counts.inserted, 2);
    assert_seqs(db, "1:1 2:2", "3");
    assert_query_is(db, (QueryExpect){.sql = "SELECT change_seq FROM entry_links;", .want = "3"});
    ASSERT_EQ_STATUS(store_import(s, peer, k_now, &counts), STORE_OK);
    ASSERT_EQ_INT((int)counts.unchanged, 2);
    assert_seqs(db, "1:1 2:2", "3");
    store_close(s);
    free(db);
    free(peer);
}

/* ---- the device id file moves into <brain>.d (007 §10 steps 1, 3, 4) ---- */

/* Put the brain's device id back where v5 kept it: <brain>.device_id, and no
   <brain>.d. Returns the id (heap). */
static char *given_legacy_sidecar(const char *db)
{
    char *side = harness_side_path(db, ".d/device_id");
    char *dir = harness_side_path(db, ".d");
    char *legacy = harness_side_path(db, ".device_id");
    char *id = NULL;

    ASSERT_TRUE(side != NULL && dir != NULL && legacy != NULL);
    id = harness_read_line(side);
    ASSERT_TRUE(uuid_v7_is_canonical(id));
    ASSERT_EQ_INT(rename(side, legacy), 0);
    ASSERT_EQ_INT(rmdir(dir), 0);
    free(side);
    free(dir);
    free(legacy);
    return id;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void assert_file_is(const char *db, const char *suffix, const char *want)
{
    char *path = harness_side_path(db, suffix);
    char *line = path != NULL ? harness_read_line(path) : NULL;

    if (want == NULL) {
        ASSERT_TRUE(path != NULL && access(path, F_OK) != 0);
    } else {
        ASSERT_STREQ(line != NULL ? line : "(missing)", want);
    }
    free(line);
    free(path);
}

TEST(schema_v6_the_device_id_moves_into_the_brain_folder)
{
    char *db = make_temp_db_path();
    char *id = NULL;
    char *dir = NULL;
    char *side = NULL;
    Store *s = NULL;
    struct stat st;

    ASSERT_TRUE(db != NULL);
    given_v5_brain(db);
    id = given_legacy_sidecar(db);
    s = given_open(db);
    ASSERT_STREQ(store_device_id(s), id);
    store_close(s);

    assert_file_is(db, ".d/device_id", id);
    assert_file_is(db, ".device_id", NULL);
    dir = harness_side_path(db, ".d");
    side = harness_side_path(db, ".d/device_id");
    ASSERT_EQ_INT(stat(dir, &st), 0);
    ASSERT_EQ_INT((int)(st.st_mode & (unsigned)PERM_BITS_MASK), DIR_PERMS);
    ASSERT_EQ_INT(stat(side, &st), 0);
    ASSERT_EQ_INT((int)(st.st_mode & (unsigned)PERM_BITS_MASK), DB_FILE_PERMS);
    free(dir);
    free(side);
    free(id);
    free(db);
}

/* Step 4: a v6 brain whose old file is still the only one (a crash between
   COMMIT and the copy). The open finishes the copy and the delete. */
TEST(schema_v6_a_v6_open_finishes_a_half_done_move)
{
    char *db = make_temp_db_path();
    char *id = NULL;
    Store *s = NULL;

    ASSERT_TRUE(db != NULL);
    store_close(given_open(db));
    id = given_legacy_sidecar(db);
    s = given_open(db);
    ASSERT_STREQ(store_device_id(s), id);
    store_close(s);
    assert_file_is(db, ".d/device_id", id);
    assert_file_is(db, ".device_id", NULL);
    free(id);
    free(db);
}

/* An invalid old file is the open's error, as an invalid sidecar is, and
   stays; `device new` mints over it and the old file goes. */
TEST(schema_v6_an_invalid_old_device_id_file_is_an_error)
{
    char *db = make_temp_db_path();
    char *legacy = NULL;
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    FILE *f = NULL;

    ASSERT_TRUE(db != NULL);
    given_v5_brain(db);
    free(given_legacy_sidecar(db));
    legacy = harness_side_path(db, ".device_id");
    ASSERT_TRUE(legacy != NULL);
    /* NOLINTNEXTLINE(android-cloexec-fopen) */
    f = fopen(legacy, "w");
    ASSERT_TRUE(f != NULL);
    if (f != NULL) {
        (void)fputs("not-a-uuid\n", f);
        (void)fclose(f);
    }
    err[0] = '\0';
    ASSERT_TRUE(store_open(db, err, sizeof(err)) == NULL);
    ASSERT_STREQ(err, "invalid device_id sidecar");
    assert_file_is(db, ".device_id", "not-a-uuid");
    assert_file_is(db, ".d/device_id", NULL);
    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "5"});

    s = store_open_opts(db,
                        &(StoreOpenOpts){.busy_timeout_ms = STORE_DEFAULT_BUSY_TIMEOUT_MS,
                                         .replace_invalid_sidecar = true},
                        err, sizeof(err), NULL);
    ASSERT_TRUE(s != NULL);
    assert_file_is(db, ".d/device_id", s != NULL ? store_device_id(s) : "");
    assert_file_is(db, ".device_id", NULL);
    store_close(s);
    free(legacy);
    free(db);
}

/* ---- schema_too_new (007 ST-10.2) ----------------------------------------- */

/* A brain asking for a newer reader opens; every read works; every write is
   refused and changes nothing. */
TEST(schema_v6_a_newer_min_reader_version_makes_the_brain_read_only)
{
    char *db = make_temp_db_path();
    char *peer = make_temp_db_path();
    Store *s = NULL;
    Entry e;
    Entry *rows = NULL;
    size_t nrows = 0U;
    StoreAddAction act = STORE_ADD_CREATED;
    StoreUpdate u;
    StoreNeighbor stub;
    StoreNeighbor *stubs = NULL;
    size_t nstubs = 0U;
    StoreLinkAction action = STORE_LINK_CREATED;
    StoreImportCounts counts;
    size_t entries = 0U;
    ListQuery q;
    PageResult page;

    ASSERT_TRUE(db != NULL && peer != NULL);
    s = given_open(db);
    (void)given_added(s, "one", k_hash_one, NULL);
    (void)given_added(s, "two", k_hash_two, NULL);
    store_close(s);
    store_close(given_open(peer));
    harness_make_v5_source(peer);
    exec_sql(db, "UPDATE brain_meta SET min_reader_version = '0.4.0';");

    s = given_open(db);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_LIVE, k_now, &e), STORE_OK);
    store_entry_free(&e);
    memset(&q, 0, sizeof(q));
    q.limit = V6_SEARCH_LIMIT;
    page = store_list(s, &q, k_now);
    ASSERT_EQ_INT((int)page.total, 2);
    free_page(page);

    ASSERT_EQ_STATUS(
        store_add(s, "three", k_hash_one, NULL, NULL, 0U, "human", NULL, k_now, &act, &e),
        STORE_ERR_SCHEMA_TOO_NEW);
    memset(&u, 0, sizeof(u));
    u.set_tags = true;
    ASSERT_EQ_STATUS(store_update(s, 1, &u, k_now, &e, NULL), STORE_ERR_SCHEMA_TOO_NEW);
    ASSERT_EQ_STATUS(store_soft_delete_by_id(s, 1, k_now, &e), STORE_ERR_SCHEMA_TOO_NEW);
    ASSERT_EQ_STATUS(store_hard_delete_by_id(s, 1, STORE_BIN_DELETED, k_now, &e),
                     STORE_ERR_SCHEMA_TOO_NEW);
    ASSERT_EQ_STATUS(store_purge(s, STORE_BIN_DELETED, k_now, &rows, &nrows),
                     STORE_ERR_SCHEMA_TOO_NEW);
    ASSERT_EQ_STATUS(store_link(s, (StoreEdge){.from_id = 1, .to_id = 2}, STORE_EDGE_CITES, k_now,
                                &action, &stub),
                     STORE_ERR_SCHEMA_TOO_NEW);
    ASSERT_EQ_STATUS(store_unlink(s, 1, 2, NULL, k_now, &stubs, &nstubs), STORE_ERR_SCHEMA_TOO_NEW);
    ASSERT_EQ_STATUS(store_rekey(s, 1, (RekeyKeys){.key_or_null = NULL, .new_key_or_null = "k"},
                                 STORE_BIN_LIVE, k_now, &e, NULL),
                     STORE_ERR_SCHEMA_TOO_NEW);
    ASSERT_EQ_STATUS(store_import(s, peer, k_now, &counts), STORE_ERR_SCHEMA_TOO_NEW);
    ASSERT_EQ_STATUS(store_conflict_accept(s, 1, STORE_KEEP_LOCAL, k_now, &rows, &nrows),
                     STORE_ERR_SCHEMA_TOO_NEW);
    ASSERT_EQ_STATUS(store_repair_search_index(s, &entries), STORE_ERR_SCHEMA_TOO_NEW);
    ASSERT_STREQ(store_status_message(STORE_ERR_SCHEMA_TOO_NEW),
                 "this brain was written by a newer remember. You can read it. Upgrade to write "
                 "to it.");
    store_close(s);
    assert_seqs(db, "1:1 2:2", "2");
    free(db);
    free(peer);
}

/* The comparison is numeric per part; a value this engine cannot parse counts
   as newer. Equal or older reads and writes. */
TEST(schema_v6_min_reader_version_compares_each_part)
{
    static const struct {
        const char *value;
        int refused;
        char pad_[4];
    } k_cases[] = {
        {"0.3.0", 0, {0}},
        {"0.2.9", 0, {0}},
        {"0.2.10", 0, {0}},
        {"0.3.1", 1, {0}},
        {"0.10.0", 1, {0}},
        {"1.0.0", 1, {0}},
        {"0.3", 1, {0}},
        {"0.3.0.1", 1, {0}},
        {"v0.3.0", 1, {0}},
        {"0.3.x", 1, {0}},
        {"", 1, {0}},
        {"0.-1.0", 1, {0}},
        {"99999999999999999999.0.0", 1, {0}},
    };
    char *db = make_temp_db_path();
    size_t i = 0;

    ASSERT_TRUE(db != NULL);
    store_close(given_open(db));
    for (i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        char sql[V6_SQL_BUF];
        Store *s = NULL;
        StoreStatus st = STORE_OK;
        Entry e;
        StoreAddAction act = STORE_ADD_CREATED;
        char hash[REMEMBER_SHA256_HEX_LEN + 1];

        (void)snprintf(sql, sizeof(sql), "UPDATE brain_meta SET min_reader_version = '%s';",
                       k_cases[i].value);
        exec_sql(db, sql);
        (void)snprintf(sql, sizeof(sql), "body %zu", i);
        body_hash_hex(sql, strlen(sql), hash);
        s = given_open(db);
        memset(&e, 0, sizeof(e));
        st = store_add(s, sql, hash, NULL, NULL, 0U, "human", NULL, k_now, &act, &e);
        store_entry_free(&e);
        store_close(s);
        ASSERT_EQ_STATUS(st, k_cases[i].refused ? STORE_ERR_SCHEMA_TOO_NEW : STORE_OK);
    }
    free(db);
}

/* Review 2026-10-26: a v5 brain whose search index is damaged still migrates.
   The index is derived, so the v6 step rebuilds it from the rows. */
TEST(schema_v6_a_damaged_v5_search_index_is_rebuilt)
{
    char *db = make_temp_db_path();
    Store *s = NULL;
    SearchQuery q;
    PageResult page;

    ASSERT_TRUE(db != NULL);
    given_v5_brain(db);
    /* The sqlite3 shell is defensive and will not write a shadow table. */
    {
        sqlite3 *raw = NULL;

        ASSERT_EQ_INT(sqlite3_open(db, &raw), SQLITE_OK);
        ASSERT_EQ_INT(sqlite3_exec(raw,
                                   "UPDATE entries_fts_data SET block = zeroblob(32) "
                                   "WHERE id = (SELECT MIN(id) FROM entries_fts_data);",
                                   NULL, NULL, NULL),
                      SQLITE_OK);
        (void)sqlite3_close(raw);
    }
    assert_query_is(db, (QueryExpect){.sql = "PRAGMA integrity_check;",
                                      .want = "malformed inverted index for FTS5 table "
                                              "main.entries_fts"});
    s = given_open(db);
    memset(&q, 0, sizeof(q));
    q.query = "notes";
    q.filters.limit = V6_SEARCH_LIMIT;
    page = store_search(s, &q, k_now).page;
    ASSERT_EQ_STATUS(page.st, STORE_OK);
    ASSERT_EQ_INT((int)page.total, 1);
    free_page(page);
    store_close(s);
    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "6"});
    assert_query_is(db, (QueryExpect){.sql = "PRAGMA integrity_check;", .want = "ok"});
    free(db);
}

/* `device new` writes the sidecar, not the brain, so a read-only brain allows it. */
TEST(schema_v6_device_new_runs_on_a_read_only_brain)
{
    char *db = make_temp_db_path();
    Store *s = NULL;
    char previous[STORE_DEVICE_ID_LEN + 1];
    char before[STORE_DEVICE_ID_LEN + 1];
    char err[ERR_BUFSIZE];
    bool had_prev = false;

    ASSERT_TRUE(db != NULL);
    store_close(given_open(db));
    exec_sql(db, "UPDATE brain_meta SET min_reader_version = '0.4.0';");
    s = given_open(db);
    (void)snprintf(before, sizeof(before), "%s", store_device_id(s));
    err[0] = '\0';
    ASSERT_EQ_STATUS(store_device_new(s, previous, sizeof(previous), &had_prev, err, sizeof(err)),
                     STORE_OK);
    ASSERT_TRUE(had_prev);
    ASSERT_STREQ(previous, before);
    ASSERT_TRUE(strcmp(store_device_id(s), before) != 0);
    assert_file_is(db, ".d/device_id", store_device_id(s));
    store_close(s);
    free(db);
}

#ifdef REMEMBER_TEST_HOOKS
/* The v5 shape is still there: user_version, the old columns, the E12 tables. */
static void assert_still_v5(const char *db)
{
    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "5"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM pragma_table_info('entries') "
                                             "WHERE name IN ('sync_id', 'source');",
                                      .want = "2"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM sqlite_master WHERE name IN "
                                             "('bucket_prefixes', 'brain_meta', 'outbox');",
                                      .want = "1"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM entries;", .want = "4"});
}

typedef void (*FaultSetter)(int n);

/* Fail each position in turn: every failed open leaves v5; the first clean
   open migrates. Prefix rows put the settings step on the path too. */
static void sweep_v5_open(FaultSetter set_fault)
{
    char *db = make_temp_db_path();
    Store *s = NULL;
    int n = 0;

    ASSERT_TRUE(db != NULL);
    given_v5_brain(db);
    exec_sql(db, "INSERT INTO bucket_prefixes(prefix, added_at, removed_at, device_id) VALUES "
                 "('a:', '2026-01-01T00:00:00.000Z', NULL, 'd'),"
                 "('bad prefix', '2026-01-02T00:00:00.000Z', NULL, 'd');"
                 "INSERT INTO settings(name, value, updated_at) VALUES ('bucket_prefix_order', "
                 "'[\"a:\"]', '2026-03-01T00:00:00.000Z');");
    for (n = 0; n < V6_FAULT_TRIES && s == NULL; n++) {
        char err[ERR_BUFSIZE];

        err[0] = '\0';
        set_fault(n);
        s = store_open(db, err, sizeof(err));
        set_fault(-1);
        if (s == NULL) {
            char *version = harness_sqlite_query_line(db, "PRAGMA user_version;");

            ASSERT_TRUE(err[0] != '\0');
            /* All of v6 or none of it: a fault after COMMIT (the reader-version
               read) leaves the finished v6 file. */
            if (version != NULL && strcmp(version, "6") == 0) {
                assert_query_is(
                    db, (QueryExpect){.sql = "SELECT value FROM settings;", .want = "[\"a:\"]"});
            } else {
                assert_still_v5(db);
            }
            free(version);
        }
    }
    ASSERT_TRUE(s != NULL);
    ASSERT_TRUE(n > 1);
    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "6"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT value FROM settings;", .want = "[\"a:\"]"});
    store_close(s);
    free(db);
}

/* M3 (the transaction half): a failure anywhere inside leaves user_version 5. */
TEST(schema_v6_exec_fault_anywhere_keeps_v5)
{
    sweep_v5_open(store_test_fail_exec_after);
}

TEST(schema_v6_prepare_fault_anywhere_keeps_v5)
{
    sweep_v5_open(store_test_fail_prepare_after);
}

TEST(schema_v6_step_fault_anywhere_keeps_v5)
{
    sweep_v5_open(store_test_fail_step_after);
}

TEST(schema_v6_alloc_fault_anywhere_keeps_v5)
{
    sweep_v5_open(store_test_fail_alloc_after);
}

/* 007-27: a failure after the copy and before COMMIT leaves <brain>.device_id;
   the next open keeps that id and finishes the move. */
TEST(schema_v6_a_crash_before_commit_keeps_the_device_id)
{
    char *db = make_temp_db_path();
    char *id = NULL;
    Store *s = NULL;
    int n = 0;
    int failed = 0;
    int copied = 0;

    ASSERT_TRUE(db != NULL);
    given_v5_brain(db);
    id = given_legacy_sidecar(db);
    for (n = 0; n < V6_FAULT_TRIES && s == NULL; n++) {
        char err[ERR_BUFSIZE];

        err[0] = '\0';
        store_test_fail_exec_after(n);
        s = store_open(db, err, sizeof(err));
        store_test_fail_exec_after(-1);
        if (s == NULL) {
            failed++;
            assert_still_v5(db);
            assert_file_is(db, ".device_id", id);
            /* A fault before the copy leaves no new file; after it, the same id. */
            {
                char *side = harness_side_path(db, ".d/device_id");
                char *line = side != NULL ? harness_read_line(side) : NULL;

                ASSERT_TRUE(line == NULL || strcmp(line, id) == 0);
                copied += (line != NULL) ? 1 : 0;
                free(line);
                free(side);
            }
        }
    }
    ASSERT_TRUE(failed > 1);
    ASSERT_TRUE(copied > 0);
    ASSERT_TRUE(s != NULL);
    ASSERT_STREQ(s != NULL ? store_device_id(s) : "", id);
    store_close(s);
    assert_file_is(db, ".d/device_id", id);
    assert_file_is(db, ".device_id", NULL);
    free(id);
    free(db);
}

/* A broken foreign key fails the check before user_version moves. */
TEST(schema_v6_dangling_reference_keeps_v5)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];

    ASSERT_TRUE(db != NULL);
    given_v5_brain(db);
    exec_sql(db, "INSERT INTO entry_tags(entry_id, tag_id) VALUES (99, 4);");
    err[0] = '\0';
    ASSERT_TRUE(store_open(db, err, sizeof(err)) == NULL);
    ASSERT_STREQ(err, "migration left a dangling reference");
    assert_still_v5(db);
    free(db);
}
#endif

void register_schema_v6_tests(void)
{
    RUN_TEST(schema_v6_old_ids_become_local_id_and_id);
    RUN_TEST(schema_v6_links_tags_conflicts_and_fts_resolve_through_local_id);
    RUN_TEST(schema_v6_brain_meta_has_one_row_and_tombstones_are_empty);
    RUN_TEST(schema_v6_actors_come_from_the_old_source);
    RUN_TEST(schema_v6_due_rows_get_expired_at_and_the_outbox_is_empty);
    RUN_TEST(schema_v6_prefixes_become_one_row_with_every_live_prefix);
    RUN_TEST(schema_v6_prefixes_without_an_order_take_the_migration_time);
    RUN_TEST(schema_v6_an_order_that_is_not_an_array_is_ignored);
    RUN_TEST(schema_v6_bad_and_duplicate_prefixes_are_left_out_and_warn);
    RUN_TEST(schema_v6_every_prefix_bad_writes_no_row);
    RUN_TEST(schema_v6_empty_e12_writes_no_row);
    RUN_TEST(schema_v6_fresh_and_migrated_schemas_are_equal);
    RUN_TEST(schema_v6_a_new_write_stores_the_source_as_an_actor);
    RUN_TEST(schema_v6_each_changed_entry_takes_the_next_counter_value_once);
    RUN_TEST(schema_v6_a_rolled_back_write_leaves_the_counter);
    RUN_TEST(schema_v6_link_and_unlink_take_counter_values);
    RUN_TEST(schema_v6_purge_writes_one_tombstone_per_row);
    RUN_TEST(schema_v6_the_first_write_after_a_prefix_row_is_two);
    RUN_TEST(schema_v6_import_stamps_written_rows_only);
    RUN_TEST(schema_v6_the_device_id_moves_into_the_brain_folder);
    RUN_TEST(schema_v6_a_v6_open_finishes_a_half_done_move);
    RUN_TEST(schema_v6_an_invalid_old_device_id_file_is_an_error);
    RUN_TEST(schema_v6_a_newer_min_reader_version_makes_the_brain_read_only);
    RUN_TEST(schema_v6_min_reader_version_compares_each_part);
    RUN_TEST(schema_v6_a_damaged_v5_search_index_is_rebuilt);
    RUN_TEST(schema_v6_device_new_runs_on_a_read_only_brain);
#ifdef REMEMBER_TEST_HOOKS
    RUN_TEST(schema_v6_exec_fault_anywhere_keeps_v5);
    RUN_TEST(schema_v6_prepare_fault_anywhere_keeps_v5);
    RUN_TEST(schema_v6_step_fault_anywhere_keeps_v5);
    RUN_TEST(schema_v6_alloc_fault_anywhere_keeps_v5);
    RUN_TEST(schema_v6_dangling_reference_keeps_v5);
    RUN_TEST(schema_v6_a_crash_before_commit_keeps_the_device_id);
#endif
}

/* The CLI prints each warning the open's migration produced (007 ST-7.15). */
TEST(schema_v6_cli_prints_setting_invalid_for_a_left_out_prefix)
{
    char *db = make_temp_db_path();
    char *db2 = make_temp_db_path();
    const char *list[] = {"list"};
    const char *list_json[] = {"--json-errors", "list"};
    CmdResult r;

    ASSERT_TRUE(db != NULL && db2 != NULL);
    given_empty_v5_brain(db);
    exec_sql(db, "INSERT INTO bucket_prefixes(prefix, added_at, removed_at, device_id) VALUES "
                 "('ok:', '2026-01-01T00:00:00.000Z', NULL, 'd'),"
                 "('my project:', '2026-01-02T00:00:00.000Z', NULL, 'd');");
    r = run_remember(db, list, sizeof(list) / sizeof(list[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "remember: warning: skipped \"remember.buckets.prefixes\": \"my "
                        "project:\" isn't a valid value.\n");
    cmd_result_free(&r);
    /* Once: the next open finds a v6 brain. */
    r = run_remember(db, list, sizeof(list) / sizeof(list[0]), NULL);
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);

    given_empty_v5_brain(db2);
    exec_sql(db2, "INSERT INTO bucket_prefixes(prefix, added_at, removed_at, device_id) VALUES "
                  "('my project:', '2026-01-02T00:00:00.000Z', NULL, 'd');");
    r = run_remember(db2, list_json, sizeof(list_json) / sizeof(list_json[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "{\"version\":1,\"warning\":{\"code\":\"setting_invalid\",\"message\":"
                        "\"skipped \\\"remember.buckets.prefixes\\\": \\\"my project:\\\" isn't a "
                        "valid value.\",\"detail\":{\"key\":\"remember.buckets.prefixes\","
                        "\"value\":\"my project:\",\"reason\":\"bad_prefix_shape\","
                        "\"item\":\"my project:\"}}}\n");
    cmd_result_free(&r);
    free(db);
    free(db2);
}

/* 007 ST-10.2 plain line. The JSON fix waits on COPY-PENDING C-01, so the JSON
   check reads only the code. */
TEST(schema_v6_cli_a_write_to_a_newer_brain_prints_schema_too_new)
{
    static const char k_plain[] = "remember: this brain was written by a newer remember. You can "
                                  "read it. Upgrade to write to it.\n";
    char *db = make_temp_db_path();
    const char *add[] = {"add", "seed"};
    const char *list[] = {"list"};
    const char *add_json[] = {"--json-errors", "add", "another"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, add, sizeof(add) / sizeof(add[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    exec_sql(db, "UPDATE brain_meta SET min_reader_version = '0.4.0';");

    r = run_remember(db, list, sizeof(list) / sizeof(list[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "seed");
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);

    r = run_remember(db, add, sizeof(add) / sizeof(add[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, k_plain);
    cmd_result_free(&r);

    r = run_remember(db, add_json, sizeof(add_json) / sizeof(add_json[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "\"code\":\"schema_too_new\"");
    cmd_result_free(&r);
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM entries;", .want = "1"});
    free(db);
}

enum { V6_PARALLEL_OPENS = 8, V6_EXIT_EXEC_FAIL = 127 };

/* Starts V6_PARALLEL_OPENS `remember --db <db> list` runs at once and waits for
   all of them; returns how many did not exit 0 (a failed fork counts). fork and
   execv, not a shell, so the paths need no quoting. */
static int count_failed_parallel_lists(const char *db)
{
    pid_t pids[V6_PARALLEL_OPENS];
    int started = 0;
    int succeeded = 0;
    int i = 0;
    /* execv wants char *const[]; the child does not write through these. */
    /* NOLINTBEGIN(clang-diagnostic-cast-qual) */
    char *argv[] = {(char *)g_remember_bin, (char *)"--db", (char *)db, (char *)"list", NULL};
    /* NOLINTEND(clang-diagnostic-cast-qual) */

    for (started = 0; started < V6_PARALLEL_OPENS; started++) {
        pids[started] = fork();
        if (pids[started] < 0) {
            break;
        }
        if (pids[started] == 0) {
            int devnull = open("/dev/null", O_WRONLY | O_CLOEXEC);
            if (devnull >= 0) {
                (void)dup2(devnull, STDOUT_FILENO);
                (void)dup2(devnull, STDERR_FILENO);
            }
            execv(g_remember_bin, argv);
            _exit(V6_EXIT_EXEC_FAIL);
        }
    }
    for (i = 0; i < started; i++) {
        int status = 0;
        if (waitpid(pids[i], &status, 0) == pids[i] && WIFEXITED(status) &&
            WEXITSTATUS(status) == 0) {
            succeeded++;
        }
    }
    return V6_PARALLEL_OPENS - succeeded;
}

/* Review 2026-10-26: parallel first opens of a v5 brain all succeed and agree
   on the device id (the copy before BEGIN uses a unique temp file). */
TEST(schema_v6_cli_parallel_first_opens_of_a_v5_brain_all_succeed)
{
    enum { V6_PARALLEL_ROUNDS = 5 };
    int round = 0;

    for (round = 0; round < V6_PARALLEL_ROUNDS; round++) {
        char *db = make_temp_db_path();
        char *id = NULL;

        ASSERT_TRUE(db != NULL);
        if (db == NULL) {
            return;
        }
        given_v5_brain(db);
        id = given_legacy_sidecar(db);
        ASSERT_EQ_INT(count_failed_parallel_lists(db), 0);
        assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "6"});
        assert_file_is(db, ".d/device_id", id);
        assert_file_is(db, ".device_id", NULL);
        free(id);
        free(db);
    }
}

#ifdef REMEMBER_TEST_VFS
/* setenv/unsetenv drive the shim; the test runner is single-threaded. */
// NOLINTBEGIN(concurrency-mt-unsafe)
/* M3: a failing VFS write anywhere in the v6 step leaves user_version 5 and
   the old device id file; the first open with no fault migrates. */
TEST(schema_v6_cli_a_failing_write_keeps_v5_and_the_old_device_id)
{
    char *db = make_temp_db_path();
    char *id = NULL;
    const char *list[] = {"list"};
    char budget[V6_SQL_BUF];
    CmdResult r;
    int n = 0;
    int failed = 0;
    bool done = false;

    ASSERT_TRUE(db != NULL);
    given_v5_brain(db);
    id = given_legacy_sidecar(db);
    for (n = 0; n < V6_FAULT_TRIES && !done; n++) {
        (void)snprintf(budget, sizeof(budget), "%d", n);
        ASSERT_EQ_INT(setenv("REMEMBER_TEST_FAIL_WRITE", budget, 1), 0);
        r = run_remember(db, list, sizeof(list) / sizeof(list[0]), NULL);
        ASSERT_EQ_INT(unsetenv("REMEMBER_TEST_FAIL_WRITE"), 0);
        if (r.exit_code != 0) {
            failed++;
            assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "5"});
            assert_file_is(db, ".device_id", id);
        } else {
            done = true;
        }
        cmd_result_free(&r);
    }
    ASSERT_TRUE(failed > 0);
    ASSERT_TRUE(done);
    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "6"});
    assert_file_is(db, ".d/device_id", id);
    assert_file_is(db, ".device_id", NULL);
    free(id);
    free(db);
}
// NOLINTEND(concurrency-mt-unsafe)
#endif

void register_schema_v6_cli_tests(void)
{
    RUN_TEST(schema_v6_cli_prints_setting_invalid_for_a_left_out_prefix);
    RUN_TEST(schema_v6_cli_a_write_to_a_newer_brain_prints_schema_too_new);
    RUN_TEST(schema_v6_cli_parallel_first_opens_of_a_v5_brain_all_succeed);
#ifdef REMEMBER_TEST_VFS
    RUN_TEST(schema_v6_cli_a_failing_write_keeps_v5_and_the_old_device_id);
#endif
}
