#include "harness.h"
#include "register.h"
#include "store.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Fixture sizes and RFC UUID layout; fopen in tests is short-lived. */
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-identifier-length,android-cloexec-fopen)

static const char k_hash_a[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static const char k_hash_b[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
static const char k_now[] = "2026-06-15T12:00:00.000Z";
static const char k_later[] = "2026-06-16T12:00:00.000Z";

enum { LIST_PAGE = 20 };

static void assert_query_is(const char *db, QueryExpect check)
{
    char *row = harness_sqlite_query_line(db, check.sql);

    ASSERT_TRUE(row != NULL);
    ASSERT_STREQ(row != NULL ? row : "", check.want);
    free(row);
}

/* FTS via the store port — do not inspect entries_fts with the system sqlite3
 * CLI (GHA macOS sqlite is older than the vendored amalgamation). */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void assert_search_total(Store *s, const char *query, StoreBin bin, int want)
{
    SearchQuery sq;
    PageResult page;
    size_t i = 0;

    memset(&sq, 0, sizeof(sq));
    sq.query = query;
    sq.filters.limit = (size_t)LIST_PAGE;
    sq.filters.bin = bin;
    page = store_search(s, &sq, k_now);
    ASSERT_EQ_STATUS(page.st, STORE_OK);
    ASSERT_EQ_INT((int)page.total, want);
    for (i = 0; i < page.count; i++) {
        store_entry_free(&page.entries[i]);
    }
    free(page.entries);
}

/* TEXT PK scan + LIMIT 1 in sidecar recovery: extra must sort after a v7
 * local id so a later reopen does not REPLACE the table back to COUNT==1. */
static void insert_extra_device(const char *db)
{
    free(harness_sqlite_query_line(db, "INSERT INTO devices(device_id, first_seen, last_seen) "
                                       "VALUES('ffffffff-ffff-7fff-bfff-ffffffffffff',"
                                       "'2020-01-01T00:00:00.000Z','2020-01-01T00:00:00.000Z');"));
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

static char *read_first_line(const char *path)
{
    FILE *f = NULL;
    char buf[80];
    size_t n = 0;
    char *out = NULL;

    f = fopen(path, "r");
    if (f == NULL) {
        return NULL;
    }
    if (fgets(buf, (int)sizeof(buf), f) == NULL) {
        (void)fclose(f);
        return NULL;
    }
    (void)fclose(f);
    n = strlen(buf);
    while (n > 0U && (buf[n - 1U] == '\n' || buf[n - 1U] == '\r')) {
        buf[--n] = '\0';
    }
    out = (char *)malloc(n + 1U);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, buf, n + 1U);
    return out;
}

static int is_uuid_v7(const char *s)
{
    static const char *hex = "0123456789abcdef";
    size_t i = 0;

    if (s == NULL || strlen(s) != 36U) {
        return 0;
    }
    if (s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-') {
        return 0;
    }
    if (s[14] != '7') {
        return 0;
    }
    if (strchr("89ab", s[19]) == NULL) {
        return 0;
    }
    for (i = 0; i < 36U; i++) {
        if (s[i] == '-') {
            continue;
        }
        if (strchr(hex, s[i]) == NULL) {
            return 0;
        }
    }
    return 1;
}

TEST(store_open_creates_v4_sidecar_and_device)
{
    char *db = make_temp_db_path();
    char *side = NULL;
    char *uuid = NULL;
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    struct stat st;

    ASSERT_TRUE(db != NULL);
    err[0] = '\0';
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    ASSERT_STREQ(err, "");
    store_close(s);

    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "4"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM devices;", .want = "1"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM conflicts;", .want = "0"});
    /* One named unique index on sync_id, no redundant inline-UNIQUE autoindex. */
    assert_query_is(
        db, (QueryExpect){.sql = "SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND "
                                 "tbl_name='entries' AND name LIKE 'sqlite_autoindex%';",
                          .want = "0"});
    assert_query_is(
        db, (QueryExpect){.sql = "SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND "
                                 "name='ux_entries_sync_id';",
                          .want = "1"});

    side = sidecar_path(db);
    ASSERT_TRUE(side != NULL);
    ASSERT_EQ_INT(stat(side, &st), 0);
    ASSERT_EQ_STATUS((st.st_mode & (unsigned)PERM_BITS_MASK), DB_FILE_PERMS);
    uuid = read_first_line(side);
    ASSERT_TRUE(is_uuid_v7(uuid));
    {
        char sql[128];
        (void)snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM devices WHERE device_id='%s';",
                       uuid != NULL ? uuid : "");
        assert_query_is(db, (QueryExpect){.sql = sql, .want = "1"});
    }
    free(uuid);
    free(side);
    free(db);
}

TEST(store_open_two_dbs_in_one_dir_distinct_sidecars)
{
    char tmpl[] = "/tmp/remember-two-db-XXXXXX";
    char *dir = NULL;
    char path_a[160];
    char path_b[160];
    char err[ERR_BUFSIZE];
    Store *sa = NULL;
    Store *sb = NULL;
    char *ua = NULL;
    char *ub = NULL;

    dir = mkdtemp(tmpl);
    ASSERT_TRUE(dir != NULL);
    if (dir == NULL) {
        return;
    }
    (void)snprintf(path_a, sizeof(path_a), "%s/a.db", dir);
    (void)snprintf(path_b, sizeof(path_b), "%s/b.db", dir);
    sa = store_open(path_a, err, sizeof(err));
    sb = store_open(path_b, err, sizeof(err));
    ASSERT_TRUE(sa != NULL);
    ASSERT_TRUE(sb != NULL);
    store_close(sa);
    store_close(sb);

    {
        char side_a[176];
        char side_b[176];
        (void)snprintf(side_a, sizeof(side_a), "%s.device_id", path_a);
        (void)snprintf(side_b, sizeof(side_b), "%s.device_id", path_b);
        ua = read_first_line(side_a);
        ub = read_first_line(side_b);
    }
    ASSERT_TRUE(is_uuid_v7(ua));
    ASSERT_TRUE(is_uuid_v7(ub));
    ASSERT_TRUE(ua != NULL && ub != NULL && strcmp(ua, ub) != 0);
    free(ua);
    free(ub);
    (void)remove(path_a);
    (void)remove(path_b);
    {
        char side_a[176];
        char side_b[176];
        (void)snprintf(side_a, sizeof(side_a), "%s.device_id", path_a);
        (void)snprintf(side_b, sizeof(side_b), "%s.device_id", path_b);
        (void)remove(side_a);
        (void)remove(side_b);
    }
    (void)rmdir(dir);
}

TEST(store_open_migrates_v3_to_v4_preserves_rows)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    char *side = NULL;
    char *dev = NULL;

    ASSERT_TRUE(db != NULL);
    free(harness_sqlite_query_line(
        db, "CREATE TABLE entries(id INTEGER PRIMARY KEY, key TEXT, body TEXT NOT NULL, "
            "body_hash TEXT NOT NULL, source TEXT NOT NULL, created_at TEXT NOT NULL, "
            "updated_at TEXT NOT NULL, expires_at TEXT);"
            "INSERT INTO entries(id, body, body_hash, source, created_at, updated_at) VALUES"
            "(1,'alpha','h1','human','2026-01-01T00:00:00.000Z','2026-01-01T00:00:00.000Z'),"
            "(2,'beta','h2','human','2026-01-01T00:00:00.000Z','2026-01-01T00:00:00.000Z');"
            "PRAGMA user_version=3;"));
    free(harness_sqlite_query_line(
        db, "CREATE TABLE entry_links ("
            "  from_id INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE,"
            "  to_id INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE,"
            "  kind TEXT NOT NULL, created_at TEXT NOT NULL, updated_at TEXT NOT NULL);"
            "INSERT INTO entry_links(from_id, to_id, kind, created_at, updated_at) VALUES"
            "(1,2,'cites','2026-01-01T00:00:00.000Z','2026-01-01T00:00:00.000Z');"));
    /* tags + entry_tags prove the FK-off rebuild's DROP does not cascade child rows. */
    free(harness_sqlite_query_line(
        db, "CREATE TABLE tags(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE);"
            "CREATE TABLE entry_tags(entry_id INTEGER NOT NULL REFERENCES entries(id) "
            "ON DELETE CASCADE, tag_id INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE, "
            "PRIMARY KEY(entry_id, tag_id));"
            "INSERT INTO tags(id, name) VALUES(1,'t');"
            "INSERT INTO entry_tags(entry_id, tag_id) VALUES(1,1);"));
    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "3"});

    err[0] = '\0';
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    ASSERT_STREQ(err, "");
    store_close(s);

    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "4"});
    assert_query_is(db,
                    (QueryExpect){.sql = "SELECT body FROM entries WHERE id=1;", .want = "alpha"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM entry_links;", .want = "1"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM entry_tags;", .want = "1"});
    assert_query_is(
        db, (QueryExpect){.sql = "SELECT COUNT(DISTINCT sync_id) FROM entries;", .want = "2"});
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM devices;", .want = "1"});
    /* Rebuilt columns match a fresh create: NOT NULL, and one (named) sync_id index. */
    assert_query_is(
        db, (QueryExpect){
                .sql = "SELECT \"notnull\" FROM pragma_table_info('entries') WHERE name='sync_id';",
                .want = "1"});
    assert_query_is(
        db,
        (QueryExpect){
            .sql =
                "SELECT \"notnull\" FROM pragma_table_info('entries') WHERE name='version_vector';",
            .want = "1"});
    assert_query_is(
        db, (QueryExpect){.sql = "SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND "
                                 "tbl_name='entries' AND name LIKE 'sqlite_autoindex%';",
                          .want = "0"});

    side = sidecar_path(db);
    dev = read_first_line(side);
    ASSERT_TRUE(is_uuid_v7(dev));
    {
        char sql[160];
        (void)snprintf(sql, sizeof(sql),
                       "SELECT COUNT(*) FROM entries WHERE version_vector = '{\"%s\":1}';",
                       dev != NULL ? dev : "");
        assert_query_is(db, (QueryExpect){.sql = sql, .want = "2"});
    }
    free(dev);
    free(side);
    free(db);
}

TEST(store_add_mints_v7_sync_id_and_get_by_sync_id)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    char sync[64];

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "hello", k_hash_a, "pref:editor", NULL, 0U, "human", NULL, k_now, &act, &e),
        STORE_OK);
    ASSERT_TRUE(is_uuid_v7(e.sync_id));
    ASSERT_TRUE(e.deleted_at == NULL);
    ASSERT_TRUE(e.version_vector != NULL && e.version_vector[0] == '{');
    (void)snprintf(sync, sizeof(sync), "%s", e.sync_id);
    store_entry_free(&e);

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get_by_sync_id(s, sync, STORE_BIN_LIVE, k_now, &e), STORE_OK);
    ASSERT_STREQ(e.body, "hello");
    ASSERT_STREQ(e.sync_id, sync);
    store_entry_free(&e);
    store_close(s);
    free(db);
}

TEST(store_sync_id_stable_across_update)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    char sync[64];
    long long conflict = 0;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_add(s, "one", k_hash_a, "k", NULL, 0U, "human", NULL, k_now, &act, &e),
                     STORE_OK);
    (void)snprintf(sync, sizeof(sync), "%s", e.sync_id);
    {
        char vv_before[80];
        (void)snprintf(vv_before, sizeof(vv_before), "%s", e.version_vector);
        store_entry_free(&e);

        memset(&e, 0, sizeof(e));
        ASSERT_EQ_STATUS(store_update(s, 1, NULL, true, "two", k_hash_b, false, NULL, 0U, false,
                                      NULL, false, STORE_BIN_LIVE, k_now, &e, &conflict),
                         STORE_OK);
        ASSERT_STREQ(e.sync_id, sync);
        /* A plain body edit advances the version vector (criteria #12). */
        ASSERT_TRUE(e.version_vector != NULL);
        if (e.version_vector != NULL) {
            ASSERT_TRUE(strcmp(e.version_vector, vv_before) != 0);
        }
    }
    store_entry_free(&e);
    store_close(s);
    free(db);
}

TEST(store_get_does_not_bump_last_seen)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    char *t1 = NULL;
    char *t2 = NULL;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_add(s, "x", k_hash_a, NULL, NULL, 0U, "human", NULL, k_now, &act, &e),
                     STORE_OK);
    store_entry_free(&e);
    store_close(s);

    t1 = harness_sqlite_query_line(db, "SELECT last_seen FROM devices;");
    ASSERT_TRUE(t1 != NULL);

    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_LIVE, k_now, &e), STORE_OK);
    store_entry_free(&e);
    store_close(s);

    t2 = harness_sqlite_query_line(db, "SELECT last_seen FROM devices;");
    ASSERT_STREQ(t1, t2);
    free(t1);
    free(t2);
    free(db);
}

TEST(store_lost_sidecar_rewritten_from_devices)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    char *side = NULL;
    char *orig = NULL;
    char *restored = NULL;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    store_close(s);

    side = sidecar_path(db);
    orig = read_first_line(side);
    ASSERT_TRUE(is_uuid_v7(orig));
    ASSERT_EQ_INT(remove(side), 0);

    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    store_close(s);

    restored = read_first_line(side);
    ASSERT_STREQ(orig, restored);
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM devices;", .want = "1"});
    free(orig);
    free(restored);
    free(side);
    free(db);
}

TEST(store_get_any_excludes_deleted)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    char sync[64];

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "gone", k_hash_a, "delme", NULL, 0U, "human", NULL, k_now, &act, &e),
        STORE_OK);
    (void)snprintf(sync, sizeof(sync), "%s", e.sync_id);
    store_entry_free(&e);
    free(harness_sqlite_query_line(
        db, "UPDATE entries SET deleted_at='2026-06-15T12:00:00.000Z' WHERE id=1;"));

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get_any(s, 1, &e), STORE_ERR_NOT_FOUND);
    store_entry_free(&e);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get_any_by_key(s, "delme", &e), STORE_ERR_NOT_FOUND);
    store_entry_free(&e);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get_any_by_sync_id(s, sync, &e), STORE_ERR_NOT_FOUND);
    store_entry_free(&e);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get_row(s, 1, &e), STORE_OK);
    ASSERT_TRUE(e.deleted_at != NULL);
    store_entry_free(&e);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get_row_by_key(s, "delme", &e), STORE_OK);
    ASSERT_TRUE(e.deleted_at != NULL);
    store_entry_free(&e);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get_row_by_sync_id(s, sync, &e), STORE_OK);
    ASSERT_TRUE(e.deleted_at != NULL);
    store_entry_free(&e);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_LIVE, k_now, &e), STORE_ERR_DELETED);
    store_entry_free(&e);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_DELETED, k_now, &e), STORE_OK);
    ASSERT_STREQ(e.body, "gone");
    store_entry_free(&e);
    store_close(s);
    free(db);
}

TEST(store_link_unlink_rekey_bump_vv)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry ea;
    Entry eb;
    StoreAddAction act = STORE_ADD_CREATED;
    StoreLinkAction lact = STORE_LINK_CREATED;
    StoreNeighbor stub;
    StoreNeighbor *gone = NULL;
    size_t n = 0U;
    StoreEdgeKind related = STORE_EDGE_RELATED;
    char vv_a0[80];
    char vv_b0[80];
    char sync[64];
    long long conflict = 0;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&ea, 0, sizeof(ea));
    memset(&eb, 0, sizeof(eb));
    ASSERT_EQ_STATUS(
        store_add(s, "alpha", k_hash_a, "ka", NULL, 0U, "human", NULL, k_now, &act, &ea), STORE_OK);
    ASSERT_EQ_STATUS(
        store_add(s, "beta", k_hash_b, NULL, NULL, 0U, "human", NULL, k_now, &act, &eb), STORE_OK);
    (void)snprintf(vv_a0, sizeof(vv_a0), "%s", ea.version_vector);
    (void)snprintf(vv_b0, sizeof(vv_b0), "%s", eb.version_vector);
    (void)snprintf(sync, sizeof(sync), "%s", ea.sync_id);
    store_entry_free(&ea);
    store_entry_free(&eb);

    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(
        store_link(s, (StoreEdge){.from_id = 1, .to_id = 2}, related, k_later, &lact, &stub),
        STORE_OK);
    store_neighbor_free(&stub);

    memset(&ea, 0, sizeof(ea));
    memset(&eb, 0, sizeof(eb));
    ASSERT_EQ_STATUS(store_get_any(s, 1, &ea), STORE_OK);
    ASSERT_EQ_STATUS(store_get_any(s, 2, &eb), STORE_OK);
    ASSERT_TRUE(strcmp(ea.version_vector, vv_a0) != 0);
    ASSERT_TRUE(strcmp(eb.version_vector, vv_b0) != 0);
    (void)snprintf(vv_a0, sizeof(vv_a0), "%s", ea.version_vector);
    (void)snprintf(vv_b0, sizeof(vv_b0), "%s", eb.version_vector);
    store_entry_free(&ea);
    store_entry_free(&eb);

    ASSERT_EQ_STATUS(store_unlink(s, 1, 2, &related, k_later, &gone, &n), STORE_OK);
    ASSERT_EQ_INT((int)n, 1);
    store_neighbors_free(gone, n);
    memset(&ea, 0, sizeof(ea));
    memset(&eb, 0, sizeof(eb));
    ASSERT_EQ_STATUS(store_get_any(s, 1, &ea), STORE_OK);
    ASSERT_EQ_STATUS(store_get_any(s, 2, &eb), STORE_OK);
    ASSERT_TRUE(strcmp(ea.version_vector, vv_a0) != 0);
    ASSERT_TRUE(strcmp(eb.version_vector, vv_b0) != 0);
    (void)snprintf(vv_a0, sizeof(vv_a0), "%s", ea.version_vector);
    store_entry_free(&ea);
    store_entry_free(&eb);

    /* no-op unlink does not bump */
    ASSERT_EQ_STATUS(store_unlink(s, 1, 2, &related, k_now, &gone, &n), STORE_OK);
    ASSERT_EQ_INT((int)n, 0);
    memset(&ea, 0, sizeof(ea));
    ASSERT_EQ_STATUS(store_get_any(s, 1, &ea), STORE_OK);
    ASSERT_STREQ(ea.version_vector, vv_a0);
    store_entry_free(&ea);

    memset(&ea, 0, sizeof(ea));
    ASSERT_EQ_STATUS(store_rekey(s, 1, (RekeyKeys){.key_or_null = NULL, .new_key_or_null = "kb"},
                                 STORE_BIN_LIVE, k_later, &ea, &conflict),
                     STORE_OK);
    ASSERT_STREQ(ea.sync_id, sync);
    ASSERT_TRUE(strcmp(ea.version_vector, vv_a0) != 0);
    store_entry_free(&ea);

    /* live row still resolves by sync_id via get_any */
    memset(&ea, 0, sizeof(ea));
    ASSERT_EQ_STATUS(store_get_any_by_sync_id(s, sync, &ea), STORE_OK);
    ASSERT_STREQ(ea.key, "kb");
    store_entry_free(&ea);
    store_close(s);
    free(db);
}

TEST(store_invalid_sidecar_refuses_open)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    char *side = NULL;
    FILE *f = NULL;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    store_close(s);

    side = sidecar_path(db);
    ASSERT_TRUE(side != NULL);
    f = fopen(side, "w");
    ASSERT_TRUE(f != NULL);
    if (f != NULL) {
        (void)fputs("not-a-uuid\n", f);
        (void)fclose(f);
    }
    err[0] = '\0';
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s == NULL);
    ASSERT_STR_CONTAINS(err, "invalid device_id sidecar");
    free(side);
    free(db);
}

TEST(store_sidecar_wins_over_devices_mismatch)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    char *side = NULL;
    char *orig = NULL;
    char *after = NULL;
    static const char k_other[] = "01900000-0000-7000-8000-000000000001";

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    store_close(s);

    side = sidecar_path(db);
    orig = read_first_line(side);
    ASSERT_TRUE(is_uuid_v7(orig));
    {
        char sql[160];
        (void)snprintf(sql, sizeof(sql), "UPDATE devices SET device_id='%s';", k_other);
        free(harness_sqlite_query_line(db, sql));
    }

    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    store_close(s);

    after = read_first_line(side);
    ASSERT_STREQ(orig, after);
    {
        char sql[160];
        (void)snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM devices WHERE device_id='%s';",
                       orig != NULL ? orig : "");
        assert_query_is(db, (QueryExpect){.sql = sql, .want = "1"});
    }
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM devices;", .want = "1"});
    free(orig);
    free(after);
    free(side);
    free(db);
}

TEST(store_empty_devices_inserted_from_sidecar)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    char *side = NULL;
    char *orig = NULL;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    store_close(s);

    side = sidecar_path(db);
    orig = read_first_line(side);
    free(harness_sqlite_query_line(db, "DELETE FROM devices;"));
    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM devices;", .want = "0"});

    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    store_close(s);

    assert_query_is(db, (QueryExpect){.sql = "SELECT COUNT(*) FROM devices;", .want = "1"});
    {
        char sql[160];
        (void)snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM devices WHERE device_id='%s';",
                       orig != NULL ? orig : "");
        assert_query_is(db, (QueryExpect){.sql = sql, .want = "1"});
    }
    free(orig);
    free(side);
    free(db);
}

TEST(store_bin_matrix_deleted_vs_expired)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_add(s, "exp", k_hash_a, NULL, NULL, 0U, "human",
                               "2020-01-01T00:00:00.000Z", k_now, &act, &e),
                     STORE_OK);
    store_entry_free(&e);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_DELETED, k_now, &e), STORE_ERR_EXPIRED);
    store_entry_free(&e);

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "del", k_hash_b, "dkey", NULL, 0U, "human", NULL, k_now, &act, &e), STORE_OK);
    store_entry_free(&e);
    free(harness_sqlite_query_line(
        db, "UPDATE entries SET deleted_at='2026-06-15T12:00:00.000Z' WHERE id=2;"));
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get(s, 2, STORE_BIN_EXPIRED, k_now, &e), STORE_ERR_DELETED);
    store_entry_free(&e);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_get_by_sync_id(s, "00000000-0000-7000-8000-000000000000", STORE_BIN_LIVE, k_now, &e),
        STORE_ERR_NOT_FOUND);
    store_entry_free(&e);
    store_close(s);
    free(db);
}

TEST(store_purge_trash_skips_deleted)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    Entry *gone = NULL;
    size_t n = 0;
    StoreAddAction act = STORE_ADD_CREATED;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_add(s, "keep-deleted", k_hash_a, NULL, NULL, 0U, "human",
                               "2020-01-01T00:00:00.000Z", k_now, &act, &e),
                     STORE_OK);
    store_entry_free(&e);
    free(harness_sqlite_query_line(
        db, "UPDATE entries SET deleted_at='2026-06-15T12:00:00.000Z' WHERE id=1;"));
    ASSERT_EQ_STATUS(store_purge_trash(s, k_now, &gone, &n), STORE_OK);
    ASSERT_EQ_INT((int)n, 0);
    free(gone);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_DELETED, k_now, &e), STORE_OK);
    store_entry_free(&e);
    store_close(s);
    free(db);
}

TEST(store_empty_sidecar_refuses_open)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    char *side = NULL;
    FILE *f = NULL;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    store_close(s);
    side = sidecar_path(db);
    ASSERT_TRUE(side != NULL);
    f = fopen(side, "w");
    ASSERT_TRUE(f != NULL);
    if (f != NULL) {
        (void)fclose(f);
    }
    err[0] = '\0';
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s == NULL);
    ASSERT_STR_CONTAINS(err, "invalid device_id sidecar");
    free(side);
    free(db);
}

TEST(store_list_tags_deleted_bin)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    const char *tags_in[] = {"gone"};
    TagCount *tc = NULL;
    size_t ntags = 0;
    ListQuery q;
    PageResult page;
    size_t i = 0;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "deleted-row", k_hash_a, NULL, tags_in, 1U, "human", NULL, k_now, &act, &e),
        STORE_OK);
    store_entry_free(&e);
    free(harness_sqlite_query_line(
        db, "UPDATE entries SET deleted_at='2026-06-15T12:00:00.000Z' WHERE id=1;"));

    memset(&q, 0, sizeof(q));
    q.limit = LIST_PAGE;
    q.bin = STORE_BIN_DELETED;
    page = store_list(s, &q, k_now);
    ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
    ASSERT_EQ_INT((int)page.total, 1);
    for (i = 0; i < page.count; i++) {
        store_entry_free(&page.entries[i]);
    }
    free(page.entries);

    ASSERT_EQ_STATUS(store_tags(s, STORE_BIN_DELETED, k_now, &tc, &ntags), STORE_OK);
    ASSERT_EQ_INT((int)ntags, 1);
    store_tags_free(tc, ntags);
    store_close(s);
    free(db);
}

TEST(store_soft_delete_live_keeps_body_fts_edges)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    Entry peer;
    StoreAddAction act = STORE_ADD_CREATED;
    StoreLinkAction lact = STORE_LINK_CREATED;
    StoreNeighbor stub;
    StoreNeighbor *rows = NULL;
    size_t n = 0;
    char sync[80];
    char vv_before[80];
    const char *tags[] = {"keep"};

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    memset(&peer, 0, sizeof(peer));
    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(
        store_add(s, "keep-me", k_hash_a, "ksoft", tags, 1U, "human", NULL, k_now, &act, &e),
        STORE_OK);
    (void)snprintf(sync, sizeof(sync), "%s", e.sync_id);
    (void)snprintf(vv_before, sizeof(vv_before), "%s", e.version_vector);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(
        store_add(s, "peer", k_hash_b, NULL, NULL, 0U, "human", NULL, k_now, &act, &peer),
        STORE_OK);
    ASSERT_EQ_STATUS(store_link(s, (StoreEdge){.from_id = 1, .to_id = peer.id}, STORE_EDGE_RELATED,
                                k_now, &lact, &stub),
                     STORE_OK);
    store_neighbor_free(&stub);
    store_entry_free(&peer);

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_soft_delete_by_id(s, 1, k_now, &e), STORE_OK);
    ASSERT_STREQ(e.body, "keep-me");
    ASSERT_STREQ(e.key, "ksoft");
    ASSERT_STREQ(e.sync_id, sync);
    ASSERT_TRUE(e.deleted_at != NULL);
    ASSERT_STREQ(e.deleted_at, k_now);
    ASSERT_TRUE(e.expires_at == NULL);
    ASSERT_EQ_INT((int)e.ntags, 1);
    ASSERT_TRUE(e.version_vector != NULL);
    if (e.version_vector != NULL) {
        ASSERT_TRUE(strcmp(e.version_vector, vv_before) != 0);
    }
    store_entry_free(&e);

    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_LIVE, k_now, &e), STORE_ERR_DELETED);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_DELETED, k_now, &e), STORE_OK);
    ASSERT_STREQ(e.sync_id, sync);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(store_get_any(s, 1, &e), STORE_ERR_NOT_FOUND);
    store_entry_free(&e);

    assert_query_is(db,
                    (QueryExpect){.sql = "SELECT count(*) FROM entries WHERE id=1;", .want = "1"});
    assert_search_total(s, "keep", STORE_BIN_LIVE, 0);
    assert_search_total(s, "keep", STORE_BIN_DELETED, 1);
    ASSERT_EQ_STATUS(store_list_neighbors(s, 1, NULL, STORE_NEIGHBOR_ALL, k_now, &rows, &n),
                     STORE_OK);
    ASSERT_EQ_INT((int)n, 1);
    store_neighbors_free(rows, n);
    store_close(s);
    free(db);
}

TEST(store_soft_delete_expired_clears_expires)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_add(s, "exp", k_hash_a, "kexp", NULL, 0U, "human",
                               "2020-01-01T00:00:00.000Z", k_now, &act, &e),
                     STORE_OK);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(store_soft_delete_by_key(s, "kexp", k_now, &e), STORE_OK);
    ASSERT_TRUE(e.deleted_at != NULL);
    ASSERT_TRUE(e.expires_at == NULL);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_EXPIRED, k_now, &e), STORE_ERR_DELETED);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(store_soft_delete_by_id(s, 1, k_now, &e), STORE_ERR_DELETED);
    store_entry_free(&e);
    store_close(s);
    free(db);
}

TEST(store_hard_delete_expired_cascades_fts)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    StoreLinkAction lact = STORE_LINK_CREATED;
    StoreNeighbor stub;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    memset(&stub, 0, sizeof(stub));
    ASSERT_EQ_STATUS(store_add(s, "exp", k_hash_a, NULL, NULL, 0U, "human",
                               "2020-01-01T00:00:00.000Z", k_now, &act, &e),
                     STORE_OK);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(store_add(s, "peer", k_hash_b, NULL, NULL, 0U, "human", NULL, k_now, &act, &e),
                     STORE_OK);
    ASSERT_EQ_STATUS(store_link(s, (StoreEdge){.from_id = 1, .to_id = e.id}, STORE_EDGE_RELATED,
                                k_now, &lact, &stub),
                     STORE_OK);
    store_neighbor_free(&stub);
    store_entry_free(&e);

    ASSERT_EQ_STATUS(store_hard_delete_by_id(s, 1, STORE_BIN_EXPIRED, k_now, &e), STORE_OK);
    ASSERT_STREQ(e.body, "exp");
    store_entry_free(&e);
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_EXPIRED, k_now, &e), STORE_ERR_NOT_FOUND);
    store_entry_free(&e);
    assert_query_is(db,
                    (QueryExpect){.sql = "SELECT count(*) FROM entries WHERE id=1;", .want = "0"});
    assert_search_total(s, "exp", STORE_BIN_EXPIRED, 0);
    assert_query_is(db, (QueryExpect){.sql = "SELECT count(*) FROM entry_links;", .want = "0"});
    store_close(s);
    free(db);
}

TEST(store_hard_delete_hatch_two_devices)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_add(s, "x", k_hash_a, NULL, NULL, 0U, "human",
                               "2020-01-01T00:00:00.000Z", k_now, &act, &e),
                     STORE_OK);
    store_entry_free(&e);
    insert_extra_device(db);
    ASSERT_EQ_STATUS(store_hard_delete_by_id(s, 1, STORE_BIN_EXPIRED, k_now, &e),
                     STORE_ERR_NOT_SINGLE_DEVICE);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_EXPIRED, k_now, &e), STORE_OK);
    store_entry_free(&e);
    store_close(s);
    free(db);
}

TEST(store_purge_deleted_and_hatch)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    Entry *gone = NULL;
    size_t n = 0;
    StoreAddAction act = STORE_ADD_CREATED;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_add(s, "del", k_hash_a, "kd", NULL, 0U, "human", NULL, k_now, &act, &e),
                     STORE_OK);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(store_soft_delete_by_id(s, 1, k_now, &e), STORE_OK);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(store_add(s, "live", k_hash_b, NULL, NULL, 0U, "human", NULL, k_now, &act, &e),
                     STORE_OK);
    store_entry_free(&e);

    ASSERT_EQ_STATUS(store_purge(s, STORE_BIN_LIVE, k_now, &gone, &n), STORE_ERR_INTERNAL);
    ASSERT_EQ_STATUS(store_purge(s, STORE_BIN_DELETED, k_now, &gone, &n), STORE_OK);
    ASSERT_EQ_INT((int)n, 1);
    ASSERT_STREQ(gone[0].body, "del");
    store_entry_free(&gone[0]);
    free(gone);
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_DELETED, k_now, &e), STORE_ERR_NOT_FOUND);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(store_get(s, 2, STORE_BIN_LIVE, k_now, &e), STORE_OK);
    store_entry_free(&e);
    insert_extra_device(db);
    ASSERT_EQ_STATUS(store_add(s, "exp", k_hash_a, NULL, NULL, 0U, "human",
                               "2020-01-01T00:00:00.000Z", k_now, &act, &e),
                     STORE_OK);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(store_purge(s, STORE_BIN_EXPIRED, k_now, &gone, &n),
                     STORE_ERR_NOT_SINGLE_DEVICE);
    store_close(s);
    free(db);
}

TEST(store_add_revives_deleted_same_sync_id)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    char sync_k[80];
    char sync_h[80];
    const char *tags[] = {"t"};

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "keyed", k_hash_a, "slot", tags, 1U, "human", NULL, k_now, &act, &e),
        STORE_OK);
    (void)snprintf(sync_k, sizeof(sync_k), "%s", e.sync_id);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(store_soft_delete_by_key(s, "slot", k_now, &e), STORE_OK);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(
        store_add(s, "keyed-new", k_hash_b, "slot", NULL, 0U, "agent", NULL, k_now, &act, &e),
        STORE_OK);
    ASSERT_EQ_INT((int)act, (int)STORE_ADD_UPDATED);
    ASSERT_STREQ(e.sync_id, sync_k);
    ASSERT_TRUE(e.deleted_at == NULL);
    ASSERT_STREQ(e.body, "keyed-new");
    store_entry_free(&e);
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_LIVE, k_now, &e), STORE_OK);
    store_entry_free(&e);

    ASSERT_EQ_STATUS(
        store_add(s, "hashbody", k_hash_a, NULL, NULL, 0U, "human", NULL, k_now, &act, &e),
        STORE_OK);
    (void)snprintf(sync_h, sizeof(sync_h), "%s", e.sync_id);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(store_soft_delete_by_id(s, 2, k_now, &e), STORE_OK);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(
        store_add(s, "hashbody", k_hash_a, NULL, tags, 1U, "tool", NULL, k_now, &act, &e),
        STORE_OK);
    ASSERT_EQ_INT((int)act, (int)STORE_ADD_MERGED);
    ASSERT_STREQ(e.sync_id, sync_h);
    ASSERT_TRUE(e.deleted_at == NULL);
    store_entry_free(&e);
    store_close(s);
    free(db);
}

TEST(store_vv_increment_inserts_missing_device_key)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "empty-vv", k_hash_a, NULL, NULL, 0U, "human", NULL, k_now, &act, &e),
        STORE_OK);
    store_entry_free(&e);
    free(harness_sqlite_query_line(db, "UPDATE entries SET version_vector='{}' WHERE id=1;"));
    ASSERT_EQ_STATUS(store_soft_delete_by_id(s, 1, k_now, &e), STORE_OK);
    ASSERT_TRUE(e.version_vector != NULL);
    if (e.version_vector != NULL) {
        ASSERT_STR_CONTAINS(e.version_vector, "\":1}");
        ASSERT_TRUE(e.version_vector[0] == '{');
    }
    store_entry_free(&e);

    ASSERT_EQ_STATUS(
        store_add(s, "other-vv", k_hash_b, NULL, NULL, 0U, "human", NULL, k_now, &act, &e),
        STORE_OK);
    store_entry_free(&e);
    free(harness_sqlite_query_line(db,
                                   "UPDATE entries SET version_vector="
                                   "'{\"00000000-0000-7000-8000-000000000001\":3}' WHERE id=2;"));
    ASSERT_EQ_STATUS(store_soft_delete_by_id(s, 2, k_now, &e), STORE_OK);
    ASSERT_TRUE(e.version_vector != NULL);
    if (e.version_vector != NULL) {
        ASSERT_STR_CONTAINS(e.version_vector, "00000000-0000-7000-8000-000000000001\":3");
        ASSERT_STR_CONTAINS(e.version_vector, "\":1}");
    }
    store_entry_free(&e);

    ASSERT_EQ_STATUS(
        store_add(s, "bad-vv", k_hash_a, "badvv", NULL, 0U, "human", NULL, k_now, &act, &e),
        STORE_OK);
    store_entry_free(&e);
    free(harness_sqlite_query_line(db, "UPDATE entries SET version_vector='{' WHERE id=3;"));
    ASSERT_EQ_STATUS(store_soft_delete_by_id(s, 3, k_now, &e), STORE_ERR_INTERNAL);
    store_entry_free(&e);
    store_close(s);
    free(db);
}

TEST(store_update_undelete_deleted)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    long long conflict = 0;
    char sync[80];
    char vv_before[80];

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_add(s, "gone", k_hash_a, "u", NULL, 0U, "human", NULL, k_now, &act, &e),
                     STORE_OK);
    (void)snprintf(sync, sizeof(sync), "%s", e.sync_id);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(store_soft_delete_by_id(s, 1, k_now, &e), STORE_OK);
    (void)snprintf(vv_before, sizeof(vv_before), "%s", e.version_vector);
    store_entry_free(&e);

    ASSERT_EQ_STATUS(store_update(s, 1, NULL, false, NULL, NULL, false, NULL, 0U, false, NULL, true,
                                  STORE_BIN_LIVE, k_now, &e, &conflict),
                     STORE_ERR_DELETED);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(store_update(s, 1, NULL, false, NULL, NULL, false, NULL, 0U, false, NULL, true,
                                  STORE_BIN_DELETED, k_now, &e, &conflict),
                     STORE_OK);
    ASSERT_TRUE(e.deleted_at == NULL);
    ASSERT_STREQ(e.sync_id, sync);
    ASSERT_TRUE(e.version_vector != NULL);
    if (e.version_vector != NULL) {
        ASSERT_TRUE(strcmp(e.version_vector, vv_before) != 0);
    }
    store_entry_free(&e);
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_LIVE, k_now, &e), STORE_OK);
    store_entry_free(&e);
    store_close(s);
    free(db);
}

#ifdef REMEMBER_TEST_HOOKS
TEST(store_devices_replace_exec_fail_undoes_savepoint)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    store_close(s);
    free(harness_sqlite_query_line(db, "DELETE FROM devices;"));
    /* pragmas (2) + SAVEPOINT (1) succeed; DELETE inside devices_replace fails. */
    store_test_fail_exec_after(3);
    err[0] = '\0';
    s = store_open(db, err, sizeof(err));
    store_test_fail_exec_after(-1);
    ASSERT_TRUE(s == NULL);
    free(db);
}
#endif

void register_store_sync_tests(void)
{
    RUN_TEST(store_open_creates_v4_sidecar_and_device);
    RUN_TEST(store_open_two_dbs_in_one_dir_distinct_sidecars);
    RUN_TEST(store_open_migrates_v3_to_v4_preserves_rows);
    RUN_TEST(store_add_mints_v7_sync_id_and_get_by_sync_id);
    RUN_TEST(store_sync_id_stable_across_update);
    RUN_TEST(store_get_does_not_bump_last_seen);
    RUN_TEST(store_lost_sidecar_rewritten_from_devices);
    RUN_TEST(store_get_any_excludes_deleted);
    RUN_TEST(store_link_unlink_rekey_bump_vv);
    RUN_TEST(store_invalid_sidecar_refuses_open);
    RUN_TEST(store_sidecar_wins_over_devices_mismatch);
    RUN_TEST(store_empty_devices_inserted_from_sidecar);
    RUN_TEST(store_bin_matrix_deleted_vs_expired);
    RUN_TEST(store_purge_trash_skips_deleted);
    RUN_TEST(store_empty_sidecar_refuses_open);
    RUN_TEST(store_list_tags_deleted_bin);
    RUN_TEST(store_soft_delete_live_keeps_body_fts_edges);
    RUN_TEST(store_soft_delete_expired_clears_expires);
    RUN_TEST(store_hard_delete_expired_cascades_fts);
    RUN_TEST(store_hard_delete_hatch_two_devices);
    RUN_TEST(store_purge_deleted_and_hatch);
    RUN_TEST(store_add_revives_deleted_same_sync_id);
    RUN_TEST(store_vv_increment_inserts_missing_device_key);
    RUN_TEST(store_update_undelete_deleted);
#ifdef REMEMBER_TEST_HOOKS
    RUN_TEST(store_devices_replace_exec_fail_undoes_savepoint);
#endif
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers,readability-identifier-length,android-cloexec-fopen)
