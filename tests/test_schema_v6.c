#include "harness.h"
#include "normalize.h"
#include "register.h"
#include "store.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Plan 19: the one v5 -> v6 migration (007 §10, 008 §8, 010 §17).
 * A v5 fixture is a brain this build created, rewritten to the v5 shape by
 * harness_make_v5_source, with the rows each test needs added by SQL.
 */

enum { V6_SQL_BUF = 1024, V6_FAULT_TRIES = 200, V6_SEARCH_LIMIT = 20 };

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
    q.source = "unknown";
    page = store_list(s, &q, k_now);
    ASSERT_EQ_INT((int)page.total, 0);
    free_page(page);
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
            ASSERT_TRUE(err[0] != '\0');
            assert_still_v5(db);
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
#ifdef REMEMBER_TEST_HOOKS
    RUN_TEST(schema_v6_exec_fault_anywhere_keeps_v5);
    RUN_TEST(schema_v6_prepare_fault_anywhere_keeps_v5);
    RUN_TEST(schema_v6_step_fault_anywhere_keeps_v5);
    RUN_TEST(schema_v6_alloc_fault_anywhere_keeps_v5);
    RUN_TEST(schema_v6_dangling_reference_keeps_v5);
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

void register_schema_v6_cli_tests(void)
{
    RUN_TEST(schema_v6_cli_prints_setting_invalid_for_a_left_out_prefix);
}
