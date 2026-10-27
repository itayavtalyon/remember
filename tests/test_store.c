#include "harness.h"
#include "register.h"
#include "store.h"
#include "test.h"

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* Test-fixture sizes/limits (see harness.h for ERR_BUFSIZE/DIR_PERMS). */
enum {
    LIST_LIMIT = 20,       /* default page limit across list/search tests */
    SQL_BUFSIZE = 160,     /* scratch SQL buffer */
    TINY_ERR_BUFSIZE = 8,  /* deliberately small buffer for truncation test */
    PATH_OVER_MARGIN = 16, /* bytes past REMEMBER_PATH_MAX for the too-long-path test */
    MANY_TAGS = 99,        /* tag count for the tag-stress test */
    STALE_VV_CAP = 128,    /* version_vector copy for the stale-guard test */
    STAMP_TS_CAP = 32      /* updated_at copy; a canonical stamp is 24 plus NUL */
};

/*
 * Unit suite for the store port (step 02). Black-box against store.h;
 * schema details inspected via the sqlite3 CLI helper when available.
 */

/* "<base><leaf>" on the heap; NULL on allocation failure. */
static char *join_path(const char *base, const char *leaf)
{
    size_t n = 0;
    char *out = NULL;

    if (base == NULL || leaf == NULL) {
        return NULL;
    }
    n = strlen(base) + strlen(leaf) + 1U;
    out = (char *)malloc(n);
    if (out == NULL) {
        return NULL;
    }
    (void)snprintf(out, n, "%s%s", base, leaf);
    return out;
}

/* Assert that a one-line sqlite3 CLI query returns exactly want. */
static void assert_query_is(const char *db, QueryExpect check)
{
    char *row = harness_sqlite_query_line(db, check.sql);

    ASSERT_TRUE(row != NULL);
    ASSERT_STREQ(row != NULL ? row : "", check.want);
    free(row);
}

TEST(store_open_creates_user_version_6)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;

    ASSERT_TRUE(db != NULL);
    err[0] = '\0';
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    ASSERT_STREQ(err, "");
    store_close(s);

    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "6"});
    assert_query_is(
        db, (QueryExpect){
                .sql = "SELECT COUNT(*) FROM pragma_table_info('entries') WHERE name='expires_at';",
                .want = "1"});
    /* ponytail: no expires_at index — full scan is fine at personal scale. */
    assert_query_is(
        db,
        (QueryExpect){.sql = "SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND "
                             "(IFNULL(sql, '') LIKE '%expires_at%' OR name LIKE '%expires_at%');",
                      .want = "0"});
    free(db);
}

TEST(store_open_migrates_v1_to_v6)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;

    ASSERT_TRUE(db != NULL);
    /* Compact v1 shape (no expires_at) + one row. sqlite3 CLI is the inspect tool. */
    free(harness_sqlite_query_line(
        db, "CREATE TABLE entries(id INTEGER PRIMARY KEY, key TEXT, body TEXT NOT NULL, "
            "body_hash TEXT NOT NULL, source TEXT NOT NULL, created_at TEXT NOT NULL, "
            "updated_at TEXT NOT NULL);"
            "INSERT INTO entries(body, body_hash, source, created_at, updated_at) "
            "VALUES('old row','h','human','2026-01-01T00:00:00.000Z',"
            "'2026-01-01T00:00:00.000Z');"
            /* Real v1 brains already had these. The v5 delta does not create them. */
            "CREATE TABLE tags (\n"
            "  id INTEGER PRIMARY KEY,\n"
            "  name TEXT NOT NULL UNIQUE\n"
            ");\n"
            "CREATE TABLE entry_tags (\n"
            "  entry_id INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE,\n"
            "  tag_id INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,\n"
            "  PRIMARY KEY (entry_id, tag_id)\n"
            ");\n"
            "PRAGMA user_version=1;"));
    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "1"});

    err[0] = '\0';
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    ASSERT_STREQ(err, "");
    store_close(s);

    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "6"});
    assert_query_is(
        db, (QueryExpect){
                .sql = "SELECT COUNT(*) FROM pragma_table_info('entries') WHERE name='expires_at';",
                .want = "1"});
    assert_query_is(
        db, (QueryExpect){.sql = "SELECT body FROM entries WHERE local_id=1;", .want = "old row"});
    assert_query_is(
        db, (QueryExpect){.sql = "SELECT IFNULL(expires_at, 'NULL') FROM entries WHERE local_id=1;",
                          .want = "NULL"});
    assert_query_is(
        db,
        (QueryExpect){.sql = "SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND "
                             "(IFNULL(sql, '') LIKE '%expires_at%' OR name LIKE '%expires_at%');",
                      .want = "0"});
    free(db);
}

TEST(store_open_reopens_existing)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s1 = NULL;
    Store *s2 = NULL;

    ASSERT_TRUE(db != NULL);
    s1 = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s1 != NULL);
    store_close(s1);

    err[0] = '\0';
    s2 = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s2 != NULL);
    store_close(s2);
    free(db);
}

TEST(store_open_refuses_user_version_too_new)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    store_close(s);

    free(harness_sqlite_query_line(db, "PRAGMA user_version=99;"));
    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "99"});

    err[0] = '\0';
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s == NULL);
    ASSERT_STR_CONTAINS(err, "newer");
    free(db);
}

/* Negative versions are never written by us; refuse instead of treating as 0. */
TEST(store_open_refuses_negative_user_version)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    store_close(s);

    free(harness_sqlite_query_line(db, "PRAGMA user_version=-1;"));
    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "-1"});

    err[0] = '\0';
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s == NULL);
    ASSERT_STR_CONTAINS(err, "unsupported");
    free(db);
}

TEST(store_open_creates_parent_dir_0700)
{
    char *db = make_temp_db_path();
    char *parent = dir_of_path(db);
    char *nested = join_path(parent != NULL ? parent : "", "/nested/t.db");
    char *created = NULL;
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    struct stat st;

    ASSERT_TRUE(nested != NULL);
    if (nested == NULL) {
        goto cleanup;
    }

    err[0] = '\0';
    s = store_open(nested, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    store_close(s);

    created = dir_of_path(nested);
    ASSERT_TRUE(created != NULL);
    if (created == NULL) {
        goto cleanup;
    }
    ASSERT_EQ_INT(stat(created, &st), 0);
    ASSERT_EQ_STATUS((st.st_mode & (unsigned)PERM_BITS_MASK), DIR_PERMS);

cleanup:
    free(created);
    free(nested);
    free(parent);
    free(db);
}

TEST(store_open_db_file_mode_0600)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    struct stat st;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    store_close(s);

    ASSERT_EQ_INT(stat(db, &st), 0);
    ASSERT_EQ_STATUS((st.st_mode & (unsigned)PERM_BITS_MASK), DB_FILE_PERMS);
    free(db);
}

TEST(store_open_has_expected_schema)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    store_close(s);

    assert_query_is(
        db, (QueryExpect){
                .sql = "SELECT name FROM sqlite_master WHERE type='table' AND name='entries';",
                .want = "entries"});
    assert_query_is(
        db,
        (QueryExpect){.sql = "SELECT name FROM sqlite_master WHERE type='table' AND name='tags';",
                      .want = "tags"});
    assert_query_is(
        db, (QueryExpect){
                .sql = "SELECT name FROM sqlite_master WHERE type='table' AND name='entry_tags';",
                .want = "entry_tags"});
    assert_query_is(
        db, (QueryExpect){
                .sql = "SELECT name FROM sqlite_master WHERE type='table' AND name='entries_fts';",
                .want = "entries_fts"});
    assert_query_is(
        db,
        (QueryExpect){
            .sql = "SELECT name FROM sqlite_master WHERE type='index' AND name='ux_entries_key';",
            .want = "ux_entries_key"});
    assert_query_is(
        db,
        (QueryExpect){
            .sql =
                "SELECT name FROM sqlite_master WHERE type='index' AND name='ux_entries_bodyhash';",
            .want = "ux_entries_bodyhash"});

    /* Partial unique indexes: the identity axes from the design. */
    assert_query_is(
        db,
        (QueryExpect){
            .sql = "SELECT CASE WHEN sql LIKE '%WHERE key IS NOT NULL%' THEN 'ok' ELSE 'bad' END "
                   "FROM sqlite_master WHERE name='ux_entries_key';",
            .want = "ok"});
    assert_query_is(
        db, (QueryExpect){
                .sql = "SELECT CASE WHEN sql LIKE '%WHERE key IS NULL%' THEN 'ok' ELSE 'bad' END "
                       "FROM sqlite_master WHERE name='ux_entries_bodyhash';",
                .want = "ok"});
    /* FTS5 tokenizer string (design Round 7). */
    assert_query_is(
        db, (QueryExpect){
                .sql = "SELECT CASE WHEN sql LIKE '%unicode61 remove_diacritics 2%' THEN 'ok' ELSE "
                       "'bad' END FROM sqlite_master WHERE name='entries_fts';",
                .want = "ok"});
    /* Cascade FKs: step 05's orphan-tag GC depends on them. */
    assert_query_is(
        db, (QueryExpect){
                .sql = "SELECT CASE WHEN sql LIKE '%ON DELETE CASCADE%' THEN 'ok' ELSE 'bad' END "
                       "FROM sqlite_master WHERE name='entry_tags';",
                .want = "ok"});
    assert_query_is(
        db, (QueryExpect){
                .sql = "SELECT name FROM sqlite_master WHERE type='table' AND name='entry_links';",
                .want = "entry_links"});
    {
        char *devices = harness_sqlite_query_line(
            db, "SELECT name FROM sqlite_master WHERE type='table' AND name='devices';");
        ASSERT_TRUE(devices == NULL);
        free(devices);
    }
    assert_query_is(
        db, (QueryExpect){
                .sql = "SELECT name FROM sqlite_master WHERE type='table' AND name='conflicts';",
                .want = "conflicts"});
    assert_query_is(
        db, (QueryExpect){
                .sql = "SELECT COUNT(*) FROM pragma_table_info('entries') WHERE name='id' AND "
                       "type='TEXT';",
                .want = "1"});
    assert_query_is(
        db, (QueryExpect){
                .sql = "SELECT COUNT(*) FROM pragma_table_info('entries') WHERE name='deleted_at';",
                .want = "1"});
    assert_query_is(
        db,
        (QueryExpect){
            .sql = "SELECT COUNT(*) FROM pragma_table_info('entries') WHERE name='version_vector';",
            .want = "1"});
    free(db);
}

/*
 * Regression: schema create must be one transaction. `tags` already exists at
 * user_version 0, so the DDL fails part-way — after which `entries` must be gone.
 * Without the transaction it survives, and every later open dies permanently on
 * "table entries already exists".
 */
TEST(store_open_rolls_back_partial_create)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    const Store *s = NULL;

    ASSERT_TRUE(db != NULL);
    free(harness_sqlite_query_line(db, "CREATE TABLE tags(x); PRAGMA user_version=0;"));

    err[0] = '\0';
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s == NULL);
    ASSERT_STR_CONTAINS(err, "already exists");

    assert_query_is(db,
                    (QueryExpect){.sql = "SELECT count(*) FROM sqlite_master WHERE name='entries';",
                                  .want = "0"});
    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "0"});
    free(db);
}

/*
 * Concurrent first open: every racer must succeed. The loser of the create race
 * re-reads user_version under the write lock instead of running CREATE against
 * an already-populated database.
 */
TEST(store_open_concurrent_create_all_succeed)
{
    enum { RACERS = 4 };
    char *db = make_temp_db_path();
    pid_t pids[RACERS];
    int failures = 0;
    int i = 0;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    /* Children must not inherit buffered parent output or the atexit sweep. */
    (void)fflush(stdout);
    (void)fflush(stderr);

    for (i = 0; i < RACERS; i++) {
        pids[i] = fork();
        if (pids[i] == 0) {
            char cerr[ERR_BUFSIZE];
            Store *cs = store_open(db, cerr, sizeof(cerr));
            store_close(cs);
            _exit(cs != NULL ? 0 : 1);
        }
    }
    for (i = 0; i < RACERS; i++) {
        int status = 0;
        if (pids[i] < 0 || waitpid(pids[i], &status, 0) < 0 || !WIFEXITED(status) ||
            WEXITSTATUS(status) != 0) {
            failures++;
        }
    }
    ASSERT_EQ_INT(failures, 0);
    assert_query_is(db, (QueryExpect){.sql = "PRAGMA user_version;", .want = "6"});
    free(db);
}

TEST(store_open_null_path_fails)
{
    char err[ERR_BUFSIZE];
    const Store *s = NULL;

    err[0] = '\0';
    s = store_open(NULL, err, sizeof(err));
    ASSERT_TRUE(s == NULL);
    ASSERT_STR_CONTAINS(err, "empty");
}

TEST(store_open_empty_path_fails)
{
    char err[ERR_BUFSIZE];
    const Store *s = NULL;

    err[0] = '\0';
    s = store_open("", err, sizeof(err));
    ASSERT_TRUE(s == NULL);
    ASSERT_STR_CONTAINS(err, "empty");
}

/* err/errlen are optional: a caller that does not want the message passes none. */
TEST(store_open_tolerates_missing_err_buffer)
{
    char *db = make_temp_db_path();
    char *parent = dir_of_path(db);
    char *blocker = join_path(parent != NULL ? parent : "", "/notadir");
    char *nested = join_path(blocker != NULL ? blocker : "", "/t.db");
    char err[ERR_BUFSIZE];
    int fd = 0;

    ASSERT_TRUE(nested != NULL);
    if (nested == NULL) {
        goto cleanup;
    }
    fd = open(blocker, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, DB_FILE_PERMS);
    ASSERT_TRUE(fd >= 0);
    (void)close(fd);

    ASSERT_TRUE(store_open("", NULL, 0) == NULL);
    ASSERT_TRUE(store_open(nested, NULL, 0) == NULL);
    ASSERT_TRUE(store_open(nested, err, 0) == NULL);

cleanup:
    free(nested);
    free(blocker);
    free(parent);
    free(db);
}

/* Messages longer than the buffer are truncated, never overrun. */
TEST(store_open_truncates_error_to_buffer)
{
    char err[TINY_ERR_BUFSIZE];
    const Store *s = NULL;

    memset(err, 'x', sizeof(err));
    s = store_open("", err, sizeof(err));
    ASSERT_TRUE(s == NULL);
    ASSERT_STREQ(err, "empty d");
}

TEST(store_open_parent_component_file_fails)
{
    char *db = make_temp_db_path();
    char *parent = dir_of_path(db);
    char *blocker = join_path(parent != NULL ? parent : "", "/notadir");
    char *nested = join_path(blocker != NULL ? blocker : "", "/t.db");
    char err[ERR_BUFSIZE];
    const Store *s = NULL;
    int fd = 0;

    ASSERT_TRUE(nested != NULL);
    if (nested == NULL) {
        goto cleanup;
    }
    fd = open(blocker, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, DB_FILE_PERMS);
    ASSERT_TRUE(fd >= 0);
    (void)close(fd);

    err[0] = '\0';
    s = store_open(nested, err, sizeof(err));
    ASSERT_TRUE(s == NULL);
    ASSERT_STR_CONTAINS(err, "not a directory");

cleanup:
    free(nested);
    free(blocker);
    free(parent);
    free(db);
}

/* mkdir failing for a reason other than EEXIST (parent is not writable). */
TEST(store_open_unwritable_parent_fails)
{
    char *db = make_temp_db_path();
    char *parent = dir_of_path(db);
    char *nested = join_path(parent != NULL ? parent : "", "/ro/deep/t.db");
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    int locked = 0;

    ASSERT_TRUE(nested != NULL && parent != NULL);
    if (nested == NULL || parent == NULL) {
        goto cleanup;
    }
    /* Root ignores dir write bits (Docker method A). GHA non-root runs this. */
    if (geteuid() == 0) {
        goto cleanup;
    }
    ASSERT_EQ_INT(chmod(parent, 0500), 0);
    locked = 1;

    err[0] = '\0';
    s = store_open(nested, err, sizeof(err));
    ASSERT_TRUE(s == NULL);
    ASSERT_STR_CONTAINS(err, "cannot create database directory");

cleanup:
    if (s != NULL) {
        store_close(s);
    }
    /* Restore, or the temp-dir sweep cannot remove it. */
    if (locked && parent != NULL) {
        (void)chmod(parent, DIR_PERMS);
    }
    free(nested);
    free(parent);
    free(db);
}

TEST(store_open_path_too_long_fails)
{
    size_t n = (size_t)REMEMBER_PATH_MAX + PATH_OVER_MARGIN;
    char *path = (char *)malloc(n + 1U);
    char err[ERR_BUFSIZE];
    const Store *s = NULL;

    ASSERT_TRUE(path != NULL);
    if (path == NULL) {
        return;
    }
    memset(path, 'a', n);
    path[0] = '/';
    path[n] = '\0';

    err[0] = '\0';
    s = store_open(path, err, sizeof(err));
    ASSERT_TRUE(s == NULL);
    ASSERT_STR_CONTAINS(err, "too long");
    free(path);
}

TEST(store_open_rejects_non_database_file)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    const Store *s = NULL;
    FILE *f = NULL;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    /* glibc "e" (O_CLOEXEC) mode is not portable to macOS; this fd is closed
       immediately after writing, well before any fork/exec. */
    // NOLINTNEXTLINE(android-cloexec-fopen)
    f = fopen(db, "wb");
    ASSERT_TRUE(f != NULL);
    if (f == NULL) {
        free(db);
        return;
    }
    (void)fputs("definitely not a sqlite database, just some plain text bytes\n", f);
    (void)fclose(f);

    err[0] = '\0';
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s == NULL);
    ASSERT_STR_CONTAINS(err, "not a database");
    free(db);
}

TEST(store_close_null_is_safe)
{
    store_close(NULL);
    ASSERT_TRUE(1);
}

/* Dummy 64-char hex (store does not re-hash). */
static const char k_hash_a[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static const char k_hash_b[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
static const char k_hash_c[] = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
static const char k_hash_d[] = "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
static const char k_hash_e[] = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
static const char k_hash_f[] = "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
static const char k_now[] = "2026-06-15T12:00:00.000Z";
static const char k_later[] = "2026-06-15T13:00:00.000Z";
static const char k_latest[] = "2026-06-15T14:00:00.000Z";

/* Stamps a later add must move together, or leave alone. */
typedef struct {
    char version_vector[STALE_VV_CAP];
    char updated_at[STAMP_TS_CAP];
} RowStamp;

/* Open a fresh brain and add one live row. Copies the row's VV and updated_at. */
static Store *given_live_add(char **db_out, const char *body, const char *hash, const char *key,
                             RowStamp *stamp)
{
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    int n = 0;

    *db_out = make_temp_db_path();
    if (*db_out == NULL || stamp == NULL) {
        return NULL;
    }
    s = store_open(*db_out, err, sizeof(err));
    if (s == NULL) {
        return NULL;
    }
    memset(&e, 0, sizeof(e));
    memset(stamp, 0, sizeof(*stamp));
    if (store_add(s, body, hash, key, NULL, 0U, "human", NULL, k_now, &act, &e) != STORE_OK ||
        e.version_vector == NULL || e.updated_at == NULL) {
        store_entry_free(&e);
        store_close(s);
        return NULL;
    }
    n = snprintf(stamp->version_vector, sizeof(stamp->version_vector), "%s", e.version_vector);
    if (n > 0 && (size_t)n < sizeof(stamp->version_vector)) {
        n = snprintf(stamp->updated_at, sizeof(stamp->updated_at), "%s", e.updated_at);
    }
    store_entry_free(&e);
    if (n < 0 || (size_t)n >= sizeof(stamp->updated_at) || stamp->version_vector[0] == '\0') {
        store_close(s);
        return NULL;
    }
    return s;
}

/* When: update row 1's body to "new", guarded by vector. Returns the store status. */
static StoreStatus when_guarded_body_update(Store *s, const char *vector, const char *now)
{
    Entry e;
    long long conflict = 0;
    StoreStatus st = STORE_OK;

    memset(&e, 0, sizeof(e));
    st = store_update(s, 1,
                      &(StoreUpdate){
                          .set_body = true,
                          .body = "new",
                          .body_hash = k_hash_b,
                          .if_version_vector = vector,
                      },
                      now, &e, &conflict);
    store_entry_free(&e);
    return st;
}

/* Then: row 1's body reads body. */
static void then_body_is(Store *s, const char *body)
{
    Entry e;

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_LIVE, k_now, &e), STORE_OK);
    ASSERT_STREQ(e.body, body);
    store_entry_free(&e);
}

static void free_page(PageResult page)
{
    size_t i = 0;

    for (i = 0; i < page.count; i++) {
        store_entry_free(&page.entries[i]);
    }
    free(page.entries);
}

static void set_updated_at(const char *db, long long id, const char *updated)
{
    char sql[SQL_BUFSIZE];

    (void)snprintf(sql, sizeof(sql), "UPDATE entries SET updated_at='%s' WHERE local_id=%lld;",
                   updated, id);
    free(harness_sqlite_query_line(db, sql));
}

/* Given: one memory whose created_at and updated_at are `created`. Source defaults to human. */
typedef struct {
    const char *body;
    const char *hash;
    const char *key;
    const char *const *tags;
    size_t ntags;
    const char *source;
    const char *created;
} GivenEntry;

static long long given_entry(Store *s, GivenEntry g)
{
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    long long id = 0;

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_add(s, g.body, g.hash, g.key, g.tags, g.ntags,
                               g.source != NULL ? g.source : "human", NULL, g.created, &act, &e),
                     STORE_OK);
    id = e.id;
    store_entry_free(&e);
    return id;
}

TEST(store_get_loads_expires_at)
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
        store_add(s, "ttl row", k_hash_a, NULL, NULL, 0U, "human", NULL, k_now, &act, &e),
        STORE_OK);
    ASSERT_TRUE(e.expires_at == NULL);
    store_entry_free(&e);

    free(harness_sqlite_query_line(
        db, "UPDATE entries SET expires_at='2026-12-31T23:59:59.000Z' WHERE local_id=1;"));

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_LIVE, k_now, &e), STORE_OK);
    ASSERT_STREQ(e.expires_at != NULL ? e.expires_at : "", "2026-12-31T23:59:59.000Z");
    store_entry_free(&e);
    store_close(s);
    free(db);
}

TEST(store_get_expired_without_trash_is_expired)
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
    ASSERT_EQ_STATUS(store_add(s, "gone", k_hash_a, NULL, NULL, 0U, "human", NULL, k_now, &act, &e),
                     STORE_OK);
    store_entry_free(&e);
    free(harness_sqlite_query_line(db, "UPDATE entries SET expires_at='2020-01-01T00:00:00.000Z' "
                                       "WHERE local_id=1;"));

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_LIVE, k_now, &e), STORE_ERR_EXPIRED);
    ASSERT_STREQ(store_status_message(STORE_ERR_EXPIRED), "expired");
    store_entry_free(&e);
    store_close(s);
    free(db);
}

typedef struct {
    const char *db;
    const char *iso;
} ExpiresUpdate;

static void sql_set_expires(ExpiresUpdate upd)
{
    char sql[SQL_BUFSIZE];

    (void)snprintf(sql, sizeof(sql), "UPDATE entries SET expires_at='%s' WHERE local_id=1;",
                   upd.iso);
    free(harness_sqlite_query_line(upd.db, sql));
}

TEST(store_get_expired_with_trash_ok)
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
    ASSERT_EQ_STATUS(store_add(s, "gone", k_hash_a, NULL, NULL, 0U, "human", NULL, k_now, &act, &e),
                     STORE_OK);
    store_entry_free(&e);
    sql_set_expires((ExpiresUpdate){.db = db, .iso = "2020-01-01T00:00:00.000Z"});

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_EXPIRED, k_now, &e), STORE_OK);
    ASSERT_EQ_INT(e.id, 1);
    store_entry_free(&e);
    store_close(s);
    free(db);
}

TEST(store_get_active_with_trash_is_not_expired)
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
    ASSERT_EQ_STATUS(store_add(s, "live", k_hash_a, NULL, NULL, 0U, "human", NULL, k_now, &act, &e),
                     STORE_OK);
    store_entry_free(&e);

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_EXPIRED, k_now, &e), STORE_ERR_NOT_EXPIRED);
    ASSERT_STREQ(store_status_message(STORE_ERR_NOT_EXPIRED), "not_expired");
    store_entry_free(&e);
    store_close(s);
    free(db);
}

TEST(store_get_missing_stays_not_found)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get(s, 99, STORE_BIN_LIVE, k_now, &e), STORE_ERR_NOT_FOUND);
    ASSERT_EQ_STATUS(store_get(s, 99, STORE_BIN_EXPIRED, k_now, &e), STORE_ERR_NOT_FOUND);
    store_close(s);
    free(db);
}

TEST(store_expires_at_equal_now_is_trash)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    ListQuery q;
    Entry *rows = NULL;
    size_t count = 0U;
    size_t total = 0U;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_add(s, "edge", k_hash_a, NULL, NULL, 0U, "human", NULL, k_now, &act, &e),
                     STORE_OK);
    store_entry_free(&e);
    sql_set_expires((ExpiresUpdate){.db = db, .iso = k_now});

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_LIVE, k_now, &e), STORE_ERR_EXPIRED);
    store_entry_free(&e);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_EXPIRED, k_now, &e), STORE_OK);
    store_entry_free(&e);

    memset(&q, 0, sizeof(q));
    q.limit = LIST_LIMIT;
    {
        PageResult page = store_list(s, &q, k_now);
        ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
        total = page.total;
    }
    ASSERT_EQ_INT((int)total, 0);
    q.bin = STORE_BIN_EXPIRED;
    {
        PageResult page = store_list(s, &q, k_now);
        ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
        rows = page.entries;
        count = page.count;
        total = page.total;
    }
    ASSERT_EQ_INT((int)total, 1);
    {
        size_t i = 0;
        for (i = 0; i < count; i++) {
            store_entry_free(&rows[i]);
        }
        free(rows);
    }
    store_close(s);
    free(db);
}

TEST(store_list_and_search_bins)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    const char *tags[] = {"wip"};
    ListQuery q;
    SearchQuery sq;
    Entry *rows = NULL;
    size_t total = 0U;
    TagCount *tc = NULL;
    size_t ntags = 0U;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "live helix", k_hash_a, NULL, tags, 1U, "human", NULL, k_now, &act, &e),
        STORE_OK);
    store_entry_free(&e);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "dead helix", k_hash_b, NULL, tags, 1U, "human", NULL, k_now, &act, &e),
        STORE_OK);
    store_entry_free(&e);
    free(harness_sqlite_query_line(db, "UPDATE entries SET expires_at='2020-01-01T00:00:00.000Z' "
                                       "WHERE local_id=2;"));

    memset(&q, 0, sizeof(q));
    q.limit = LIST_LIMIT;
    {
        PageResult page = store_list(s, &q, k_now);
        ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
        rows = page.entries;
        total = page.total;
    }
    ASSERT_EQ_INT((int)total, 1);
    if (rows != NULL && total == 1U) {
        ASSERT_STREQ(rows[0].body, "live helix");
        store_entry_free(&rows[0]);
    }
    free(rows);
    rows = NULL;
    q.bin = STORE_BIN_EXPIRED;
    {
        PageResult page = store_list(s, &q, k_now);
        ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
        rows = page.entries;
        total = page.total;
    }
    ASSERT_EQ_INT((int)total, 1);
    if (rows != NULL && total == 1U) {
        ASSERT_STREQ(rows[0].body, "dead helix");
        store_entry_free(&rows[0]);
    }
    free(rows);
    rows = NULL;

    memset(&sq, 0, sizeof(sq));
    sq.query = "helix";
    sq.filters.limit = LIST_LIMIT;
    {
        PageResult page = store_search(s, &sq, k_now).page;
        ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
        rows = page.entries;
        total = page.total;
    }
    ASSERT_EQ_INT((int)total, 1);
    if (rows != NULL) {
        ASSERT_STREQ(rows[0].body, "live helix");
        store_entry_free(&rows[0]);
    }
    free(rows);
    rows = NULL;
    sq.filters.bin = STORE_BIN_EXPIRED;
    {
        PageResult page = store_search(s, &sq, k_now).page;
        ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
        rows = page.entries;
        total = page.total;
    }
    ASSERT_EQ_INT((int)total, 1);
    if (rows != NULL) {
        ASSERT_STREQ(rows[0].body, "dead helix");
        store_entry_free(&rows[0]);
    }
    free(rows);

    ASSERT_EQ_STATUS(store_tags(s, STORE_BIN_LIVE, k_now, &tc, &ntags), STORE_OK);
    ASSERT_EQ_INT((int)ntags, 1);
    ASSERT_EQ_INT(tc[0].count, 1);
    store_tags_free(tc, ntags);
    tc = NULL;
    ASSERT_EQ_STATUS(store_tags(s, STORE_BIN_EXPIRED, k_now, &tc, &ntags), STORE_OK);
    ASSERT_EQ_INT((int)ntags, 1);
    ASSERT_EQ_INT(tc[0].count, 1);
    store_tags_free(tc, ntags);

    store_close(s);
    free(db);
}

TEST(store_update_and_delete_wrong_bin)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    long long conflict = 0;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_add(s, "row", k_hash_a, NULL, NULL, 0U, "human", NULL, k_now, &act, &e),
                     STORE_OK);
    store_entry_free(&e);
    sql_set_expires((ExpiresUpdate){.db = db, .iso = "2020-01-01T00:00:00.000Z"});

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_update(s, 1,
                                  &(StoreUpdate){
                                      .set_body = true,
                                      .body = "x",
                                      .body_hash = k_hash_b,
                                  },
                                  k_now, &e, &conflict),
                     STORE_ERR_EXPIRED);
    store_entry_free(&e);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_hard_delete_by_id(s, 1, STORE_BIN_LIVE, k_now, &e), STORE_ERR_INTERNAL);
    store_entry_free(&e);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_soft_delete_by_id(s, 1, k_now, &e), STORE_OK);
    ASSERT_TRUE(e.deleted_at != NULL);
    ASSERT_TRUE(e.expires_at == NULL);
    store_entry_free(&e);

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_add(s, "live", k_hash_c, NULL, NULL, 0U, "human", NULL, k_now, &act, &e),
                     STORE_OK);
    store_entry_free(&e);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_update(s, 2,
                                  &(StoreUpdate){
                                      .set_body = true,
                                      .body = "y",
                                      .body_hash = k_hash_a,
                                      .bin = STORE_BIN_EXPIRED,
                                  },
                                  k_now, &e, &conflict),
                     STORE_ERR_NOT_EXPIRED);
    store_entry_free(&e);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_hard_delete_by_id(s, 2, STORE_BIN_EXPIRED, k_now, &e),
                     STORE_ERR_NOT_EXPIRED);
    store_entry_free(&e);
    store_close(s);
    free(db);
}

TEST(store_update_trash_clear_expires_restores)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    long long conflict = 0;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_add(s, "row", k_hash_a, NULL, NULL, 0U, "human", NULL, k_now, &act, &e),
                     STORE_OK);
    store_entry_free(&e);
    sql_set_expires((ExpiresUpdate){.db = db, .iso = "2020-01-01T00:00:00.000Z"});

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_update(s, 1,
                                  &(StoreUpdate){
                                      .set_expires = true,
                                      .bin = STORE_BIN_EXPIRED,
                                  },
                                  k_now, &e, &conflict),
                     STORE_OK);
    ASSERT_TRUE(e.expires_at == NULL);
    store_entry_free(&e);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_LIVE, k_now, &e), STORE_OK);
    store_entry_free(&e);
    store_close(s);
    free(db);
}

TEST(store_update_trash_future_expires_leaves_trash)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    long long conflict = 0;
    const char *future = "2029-01-01T00:00:00.000Z";

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_add(s, "row", k_hash_a, NULL, NULL, 0U, "human", NULL, k_now, &act, &e),
                     STORE_OK);
    store_entry_free(&e);
    sql_set_expires((ExpiresUpdate){.db = db, .iso = "2020-01-01T00:00:00.000Z"});

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_update(s, 1,
                                  &(StoreUpdate){
                                      .set_expires = true,
                                      .expires_at = future,
                                      .bin = STORE_BIN_EXPIRED,
                                  },
                                  k_now, &e, &conflict),
                     STORE_OK);
    ASSERT_STREQ(e.expires_at != NULL ? e.expires_at : "", future);
    store_entry_free(&e);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_LIVE, k_now, &e), STORE_OK);
    store_entry_free(&e);
    store_close(s);
    free(db);
}

TEST(store_add_keyless_revives_expired)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    const char *tags[] = {"wip"};

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "same body", k_hash_a, NULL, NULL, 0U, "human", NULL, k_now, &act, &e),
        STORE_OK);
    ASSERT_EQ_INT(e.id, 1);
    store_entry_free(&e);
    sql_set_expires((ExpiresUpdate){.db = db, .iso = "2020-01-01T00:00:00.000Z"});

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "same body", k_hash_a, NULL, tags, 1U, "human", NULL, k_now, &act, &e),
        STORE_OK);
    ASSERT_EQ_INT(e.id, 1);
    ASSERT_EQ_INT((int)act, (int)STORE_ADD_MERGED);
    ASSERT_TRUE(e.expires_at == NULL);
    ASSERT_EQ_INT((int)e.ntags, 1);
    store_entry_free(&e);
    store_close(s);
    free(db);
}

TEST(store_add_keyed_revives_expired)
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
    ASSERT_EQ_STATUS(store_add(s, "v1", k_hash_a, "slot", NULL, 0U, "human", NULL, k_now, &act, &e),
                     STORE_OK);
    store_entry_free(&e);
    sql_set_expires((ExpiresUpdate){.db = db, .iso = "2020-01-01T00:00:00.000Z"});

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_add(s, "v2", k_hash_b, "slot", NULL, 0U, "human", NULL, k_now, &act, &e),
                     STORE_OK);
    ASSERT_EQ_INT(e.id, 1);
    ASSERT_EQ_INT((int)act, (int)STORE_ADD_UPDATED);
    ASSERT_STREQ(e.body, "v2");
    ASSERT_TRUE(e.expires_at == NULL);
    store_entry_free(&e);
    store_close(s);
    free(db);
}

TEST(store_add_keyed_change_bumps_version_vector)
{
    char *db = NULL;
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    RowStamp before;
    char expect[STALE_VV_CAP];
    const char *device = NULL;
    int n = 0;

    memset(&before, 0, sizeof(before));
    s = given_live_add(&db, "v1", k_hash_a, "slot", &before);
    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    device = store_device_id(s);
    ASSERT_TRUE(device != NULL);
    if (device == NULL) {
        store_close(s);
        free(db);
        return;
    }
    n = snprintf(expect, sizeof(expect), "{\"%s\":1}", device);
    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(expect));
    ASSERT_STREQ(before.version_vector, expect);
    ASSERT_STREQ(before.updated_at, k_now);
    n = snprintf(expect, sizeof(expect), "{\"%s\":2}", device);
    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(expect));

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "v2", k_hash_b, "slot", NULL, 0U, "human", NULL, k_later, &act, &e), STORE_OK);
    if (e.version_vector == NULL || e.updated_at == NULL) {
        store_entry_free(&e);
        store_close(s);
        free(db);
        return;
    }
    ASSERT_EQ_INT(e.id, 1);
    ASSERT_EQ_INT((int)act, (int)STORE_ADD_UPDATED);
    ASSERT_STREQ(e.version_vector, expect);
    ASSERT_STREQ(e.updated_at, k_later);
    store_entry_free(&e);
    store_close(s);
    free(db);
}

TEST(store_add_keyed_same_body_moves_neither_stamp)
{
    char *db = NULL;
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    RowStamp before;

    memset(&before, 0, sizeof(before));
    s = given_live_add(&db, "v1", k_hash_a, "slot", &before);
    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "v1", k_hash_a, "slot", NULL, 0U, "human", NULL, k_later, &act, &e), STORE_OK);
    if (e.version_vector == NULL || e.updated_at == NULL) {
        store_entry_free(&e);
        store_close(s);
        free(db);
        return;
    }
    ASSERT_EQ_INT(e.id, 1);
    ASSERT_EQ_INT((int)act, (int)STORE_ADD_UPDATED);
    ASSERT_STREQ(e.version_vector, before.version_vector);
    ASSERT_STREQ(e.updated_at, before.updated_at);
    store_entry_free(&e);
    store_close(s);
    free(db);
}

TEST(store_add_keyed_new_tag_bumps_both_stamps)
{
    char *db = NULL;
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    RowStamp before;
    char expect[STALE_VV_CAP];
    const char *device = NULL;
    const char *tags[] = {"wip"};
    int n = 0;

    memset(&before, 0, sizeof(before));
    s = given_live_add(&db, "v1", k_hash_a, "slot", &before);
    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    device = store_device_id(s);
    ASSERT_TRUE(device != NULL);
    if (device == NULL) {
        store_close(s);
        free(db);
        return;
    }
    n = snprintf(expect, sizeof(expect), "{\"%s\":2}", device);
    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(expect));

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "v1", k_hash_a, "slot", tags, 1U, "human", NULL, k_later, &act, &e), STORE_OK);
    if (e.version_vector == NULL || e.updated_at == NULL) {
        store_entry_free(&e);
        store_close(s);
        free(db);
        return;
    }
    ASSERT_EQ_INT(e.id, 1);
    ASSERT_EQ_INT((int)act, (int)STORE_ADD_UPDATED);
    ASSERT_STREQ(e.version_vector, expect);
    ASSERT_STREQ(e.updated_at, k_later);
    store_entry_free(&e);
    store_close(s);
    free(db);
}

TEST(store_add_keyless_new_tag_bumps_both_stamps)
{
    char *db = NULL;
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    RowStamp before;
    char expect[STALE_VV_CAP];
    const char *device = NULL;
    const char *tags[] = {"wip"};
    int n = 0;

    memset(&before, 0, sizeof(before));
    s = given_live_add(&db, "same", k_hash_a, NULL, &before);
    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    device = store_device_id(s);
    ASSERT_TRUE(device != NULL);
    if (device == NULL) {
        store_close(s);
        free(db);
        return;
    }
    n = snprintf(expect, sizeof(expect), "{\"%s\":2}", device);
    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(expect));

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "same", k_hash_a, NULL, tags, 1U, "human", NULL, k_later, &act, &e), STORE_OK);
    if (e.version_vector == NULL || e.updated_at == NULL) {
        store_entry_free(&e);
        store_close(s);
        free(db);
        return;
    }
    ASSERT_EQ_INT(e.id, 1);
    ASSERT_EQ_INT((int)act, (int)STORE_ADD_MERGED);
    ASSERT_STREQ(e.version_vector, expect);
    ASSERT_STREQ(e.updated_at, k_later);
    store_entry_free(&e);
    store_close(s);
    free(db);
}

TEST(store_add_keyless_same_body_moves_neither_stamp)
{
    char *db = NULL;
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    RowStamp before;

    memset(&before, 0, sizeof(before));
    s = given_live_add(&db, "same", k_hash_a, NULL, &before);
    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "same", k_hash_a, NULL, NULL, 0U, "human", NULL, k_later, &act, &e), STORE_OK);
    if (e.version_vector == NULL || e.updated_at == NULL) {
        store_entry_free(&e);
        store_close(s);
        free(db);
        return;
    }
    ASSERT_EQ_INT(e.id, 1);
    ASSERT_EQ_INT((int)act, (int)STORE_ADD_MERGED);
    ASSERT_STREQ(e.version_vector, before.version_vector);
    ASSERT_STREQ(e.updated_at, before.updated_at);
    store_entry_free(&e);
    store_close(s);
    free(db);
}

TEST(store_add_keyless_revive_bumps_both_stamps_once)
{
    char *db = NULL;
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    RowStamp before;
    char expect[STALE_VV_CAP];
    const char *device = NULL;
    int n = 0;

    memset(&before, 0, sizeof(before));
    s = given_live_add(&db, "same", k_hash_a, NULL, &before);
    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    sql_set_expires((ExpiresUpdate){.db = db, .iso = "2020-01-01T00:00:00.000Z"});
    device = store_device_id(s);
    ASSERT_TRUE(device != NULL);
    if (device == NULL) {
        store_close(s);
        free(db);
        return;
    }
    n = snprintf(expect, sizeof(expect), "{\"%s\":2}", device);
    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(expect));

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "same", k_hash_a, NULL, NULL, 0U, "human", NULL, k_later, &act, &e), STORE_OK);
    if (e.version_vector == NULL || e.updated_at == NULL) {
        store_entry_free(&e);
        store_close(s);
        free(db);
        return;
    }
    ASSERT_EQ_INT(e.id, 1);
    ASSERT_EQ_INT((int)act, (int)STORE_ADD_MERGED);
    ASSERT_TRUE(e.expires_at == NULL);
    ASSERT_STREQ(e.version_vector, expect);
    ASSERT_STREQ(e.updated_at, k_later);
    store_entry_free(&e);
    store_close(s);
    free(db);
}

TEST(store_add_keyed_revive_bumps_both_stamps_once)
{
    char *db = NULL;
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    char expect[STALE_VV_CAP];
    const char *device = NULL;
    int n = 0;
    RowStamp before;

    memset(&before, 0, sizeof(before));
    s = given_live_add(&db, "v1", k_hash_a, "slot", &before);
    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    sql_set_expires((ExpiresUpdate){.db = db, .iso = "2020-01-01T00:00:00.000Z"});
    device = store_device_id(s);
    ASSERT_TRUE(device != NULL);
    if (device == NULL) {
        store_close(s);
        free(db);
        return;
    }
    n = snprintf(expect, sizeof(expect), "{\"%s\":2}", device);
    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(expect));

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "v2", k_hash_b, "slot", NULL, 0U, "human", NULL, k_later, &act, &e), STORE_OK);
    if (e.version_vector == NULL || e.updated_at == NULL) {
        store_entry_free(&e);
        store_close(s);
        free(db);
        return;
    }
    ASSERT_EQ_INT(e.id, 1);
    ASSERT_EQ_INT((int)act, (int)STORE_ADD_UPDATED);
    ASSERT_STREQ(e.body, "v2");
    ASSERT_TRUE(e.expires_at == NULL);
    ASSERT_STREQ(e.version_vector, expect);
    ASSERT_STREQ(e.updated_at, k_later);
    store_entry_free(&e);
    store_close(s);
    free(db);
}

TEST(store_add_keyless_deleted_revive_bumps_once)
{
    char *db = NULL;
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    char expect[STALE_VV_CAP];
    const char *device = NULL;
    int n = 0;
    RowStamp before;

    memset(&before, 0, sizeof(before));
    memset(&e, 0, sizeof(e));
    s = given_live_add(&db, "same", k_hash_a, NULL, &before);
    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    ASSERT_EQ_STATUS(store_soft_delete_by_id(s, 1, k_now, &e), STORE_OK);
    store_entry_free(&e);
    device = store_device_id(s);
    ASSERT_TRUE(device != NULL);
    if (device == NULL) {
        store_close(s);
        free(db);
        return;
    }
    /* Create is 1. Soft-delete is one write, so the revive is the next counter. */
    n = snprintf(expect, sizeof(expect), "{\"%s\":3}", device);
    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(expect));

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "same", k_hash_a, NULL, NULL, 0U, "human", NULL, k_later, &act, &e), STORE_OK);
    if (e.version_vector == NULL || e.updated_at == NULL) {
        store_entry_free(&e);
        store_close(s);
        free(db);
        return;
    }
    ASSERT_EQ_INT(e.id, 1);
    ASSERT_EQ_INT((int)act, (int)STORE_ADD_MERGED);
    ASSERT_TRUE(e.deleted_at == NULL);
    ASSERT_STREQ(e.version_vector, expect);
    ASSERT_STREQ(e.updated_at, k_later);
    store_entry_free(&e);
    store_close(s);
    free(db);
}

TEST(store_add_same_tag_again_moves_neither_stamp)
{
    char *db = NULL;
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    RowStamp after_tag;
    const char *tags[] = {"wip"};

    memset(&after_tag, 0, sizeof(after_tag));
    s = given_live_add(&db, "v1", k_hash_a, "slot", &after_tag);
    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "v1", k_hash_a, "slot", tags, 1U, "human", NULL, k_later, &act, &e), STORE_OK);
    if (e.version_vector == NULL || e.updated_at == NULL) {
        store_entry_free(&e);
        store_close(s);
        free(db);
        return;
    }
    ASSERT_TRUE(snprintf(after_tag.version_vector, sizeof(after_tag.version_vector), "%s",
                         e.version_vector) > 0);
    ASSERT_TRUE(snprintf(after_tag.updated_at, sizeof(after_tag.updated_at), "%s", e.updated_at) >
                0);
    store_entry_free(&e);

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "v1", k_hash_a, "slot", tags, 1U, "human", NULL, k_latest, &act, &e),
        STORE_OK);
    if (e.version_vector == NULL || e.updated_at == NULL) {
        store_entry_free(&e);
        store_close(s);
        free(db);
        return;
    }
    ASSERT_EQ_INT(e.id, 1);
    ASSERT_STREQ(e.version_vector, after_tag.version_vector);
    ASSERT_STREQ(e.updated_at, after_tag.updated_at);
    store_entry_free(&e);
    store_close(s);
    free(db);
}

TEST(store_add_past_expires_born_in_trash)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    const char *past = "2020-01-01T00:00:00.000Z";

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "already gone", k_hash_a, NULL, NULL, 0U, "human", past, k_now, &act, &e),
        STORE_OK);
    ASSERT_STREQ(e.expires_at != NULL ? e.expires_at : "", past);
    store_entry_free(&e);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_LIVE, k_now, &e), STORE_ERR_EXPIRED);
    store_entry_free(&e);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_EXPIRED, k_now, &e), STORE_OK);
    store_entry_free(&e);
    store_close(s);
    free(db);
}

TEST(store_purge_trash_deletes_only_expired)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    const char *tags[] = {"tmp"};
    Entry *gone = NULL;
    size_t n = 0U;
    ListQuery q;
    Entry *rows = NULL;
    size_t total = 0U;
    TagCount *tc = NULL;
    size_t ntags = 0U;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "keep me", k_hash_a, NULL, NULL, 0U, "human", NULL, k_now, &act, &e),
        STORE_OK);
    store_entry_free(&e);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "trash me", k_hash_b, NULL, tags, 1U, "human", NULL, k_now, &act, &e),
        STORE_OK);
    store_entry_free(&e);
    free(harness_sqlite_query_line(db, "UPDATE entries SET expires_at='2020-01-01T00:00:00.000Z' "
                                       "WHERE local_id=2;"));

    ASSERT_EQ_STATUS(store_purge_trash(s, k_now, &gone, &n), STORE_OK);
    ASSERT_EQ_INT((int)n, 1);
    ASSERT_STREQ(gone[0].body, "trash me");
    store_entry_free(&gone[0]);
    free(gone);

    memset(&q, 0, sizeof(q));
    q.limit = LIST_LIMIT;
    {
        PageResult page = store_list(s, &q, k_now);
        ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
        rows = page.entries;
        total = page.total;
    }
    ASSERT_EQ_INT((int)total, 1);
    ASSERT_STREQ(rows[0].body, "keep me");
    store_entry_free(&rows[0]);
    free(rows);
    q.bin = STORE_BIN_EXPIRED;
    rows = NULL;
    {
        PageResult page = store_list(s, &q, k_now);
        ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
        total = page.total;
    }
    ASSERT_EQ_INT((int)total, 0);

    ASSERT_EQ_STATUS(store_tags(s, STORE_BIN_LIVE, k_now, &tc, &ntags), STORE_OK);
    ASSERT_EQ_INT((int)ntags, 0);
    store_tags_free(tc, ntags);

    ASSERT_EQ_STATUS(store_purge_trash(s, k_now, &gone, &n), STORE_OK);
    ASSERT_EQ_INT((int)n, 0);
    ASSERT_TRUE(gone == NULL);

    store_close(s);
    free(db);
}

/* E5.1: editing the oldest row must not change creation order. */
TEST(store_list_sort_created_keeps_creation_order)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    ListQuery q;
    PageResult page;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    (void)given_entry(
        s, (GivenEntry){.body = "oldest", .hash = k_hash_a, .created = "2026-01-01T00:00:00.000Z"});
    (void)given_entry(
        s, (GivenEntry){.body = "middle", .hash = k_hash_b, .created = "2026-02-01T00:00:00.000Z"});
    (void)given_entry(
        s, (GivenEntry){.body = "newest", .hash = k_hash_c, .created = "2026-03-01T00:00:00.000Z"});
    set_updated_at(db, 1, "2026-04-01T00:00:00.000Z");

    memset(&q, 0, sizeof(q));
    q.limit = LIST_LIMIT;
    q.sort = STORE_SORT_CREATED;
    page = store_list(s, &q, k_now);
    ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
    ASSERT_EQ_INT((int)page.count, 3);
    ASSERT_EQ_INT((int)page.total, 3);
    ASSERT_STREQ(page.entries[0].body, "newest");
    ASSERT_STREQ(page.entries[1].body, "middle");
    ASSERT_STREQ(page.entries[2].body, "oldest");
    free_page(page);
    store_close(s);
    free(db);
}

/* E5.2: since includes the boundary instant; until excludes it. */
TEST(store_list_since_inclusive_until_exclusive)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    ListQuery q;
    PageResult page;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    (void)given_entry(
        s, (GivenEntry){.body = "before", .hash = k_hash_a, .created = "2026-06-01T00:00:00.000Z"});
    (void)given_entry(
        s,
        (GivenEntry){.body = "at_since", .hash = k_hash_b, .created = "2026-06-15T12:00:00.000Z"});
    (void)given_entry(
        s,
        (GivenEntry){.body = "at_until", .hash = k_hash_c, .created = "2026-06-20T00:00:00.000Z"});

    memset(&q, 0, sizeof(q));
    q.limit = LIST_LIMIT;
    q.since = "2026-06-15T12:00:00.000Z";
    q.until = "2026-06-20T00:00:00.000Z";
    page = store_list(s, &q, k_now);
    ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
    ASSERT_EQ_INT((int)page.count, 1);
    ASSERT_EQ_INT((int)page.total, 1);
    ASSERT_STREQ(page.entries[0].body, "at_since");
    free_page(page);
    store_close(s);
    free(db);
}

/* E5.1: zero and updated both follow the edit, not creation time. */
TEST(store_list_updated_sort_puts_the_edit_first)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    ListQuery q;
    PageResult page;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    (void)given_entry(
        s, (GivenEntry){.body = "oldest", .hash = k_hash_a, .created = "2026-01-01T00:00:00.000Z"});
    (void)given_entry(
        s, (GivenEntry){.body = "middle", .hash = k_hash_b, .created = "2026-02-01T00:00:00.000Z"});
    (void)given_entry(
        s, (GivenEntry){.body = "newest", .hash = k_hash_c, .created = "2026-03-01T00:00:00.000Z"});
    set_updated_at(db, 1, "2026-04-01T00:00:00.000Z");

    memset(&q, 0, sizeof(q));
    q.limit = LIST_LIMIT;
    page = store_list(s, &q, k_now);
    ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
    ASSERT_STREQ(page.entries[0].body, "oldest");
    ASSERT_STREQ(page.entries[1].body, "newest");
    ASSERT_STREQ(page.entries[2].body, "middle");
    free_page(page);

    q.sort = STORE_SORT_UPDATED;
    page = store_list(s, &q, k_now);
    ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
    ASSERT_STREQ(page.entries[0].body, "oldest");
    free_page(page);
    store_close(s);
    free(db);
}

/* E5.1: equal created_at breaks toward the higher id, not the later edit. */
TEST(store_list_created_tie_breaks_by_id)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    ListQuery q;
    PageResult page;
    long long low_id = 0;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    low_id = given_entry(
        s, (GivenEntry){.body = "low-id", .hash = k_hash_a, .created = "2026-01-01T00:00:00.000Z"});
    (void)given_entry(
        s,
        (GivenEntry){.body = "high-id", .hash = k_hash_b, .created = "2026-01-01T00:00:00.000Z"});
    set_updated_at(db, low_id, "2026-09-01T00:00:00.000Z");

    memset(&q, 0, sizeof(q));
    q.limit = LIST_LIMIT;
    q.sort = STORE_SORT_CREATED;
    page = store_list(s, &q, k_now);
    ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
    ASSERT_EQ_INT((int)page.count, 2);
    ASSERT_STREQ(page.entries[0].body, "high-id");
    ASSERT_STREQ(page.entries[1].body, "low-id");
    free_page(page);
    store_close(s);
    free(db);
}

/* E5.2: since/until use created_at or updated_at with the sort. */
TEST(store_list_since_follows_sort_column)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    ListQuery q;
    PageResult page;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    (void)given_entry(
        s, (GivenEntry){.body = "moved", .hash = k_hash_a, .created = "2026-01-01T00:00:00.000Z"});
    set_updated_at(db, 1, "2026-08-01T00:00:00.000Z");

    memset(&q, 0, sizeof(q));
    q.limit = LIST_LIMIT;
    q.since = "2026-03-01T00:00:00.000Z";
    q.sort = STORE_SORT_CREATED;
    page = store_list(s, &q, k_now);
    ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
    ASSERT_EQ_INT((int)page.count, 0);
    ASSERT_EQ_INT((int)page.total, 0);
    free_page(page);

    q.sort = STORE_SORT_UPDATED;
    page = store_list(s, &q, k_now);
    ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
    ASSERT_EQ_INT((int)page.count, 1);
    ASSERT_EQ_INT((int)page.total, 1);
    ASSERT_STREQ(page.entries[0].body, "moved");
    free_page(page);
    store_close(s);
    free(db);
}

/* D11: since >= until is an empty page, not an error. */
TEST(store_list_crossed_bounds_are_empty)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    ListQuery q;
    PageResult page;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    (void)given_entry(
        s, (GivenEntry){.body = "row", .hash = k_hash_a, .created = "2026-06-15T12:00:00.000Z"});

    memset(&q, 0, sizeof(q));
    q.limit = LIST_LIMIT;
    q.since = "2026-06-15T12:00:00.000Z";
    q.until = "2026-06-15T12:00:00.000Z";
    page = store_list(s, &q, k_now);
    ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
    ASSERT_EQ_INT((int)page.count, 0);
    ASSERT_EQ_INT((int)page.total, 0);
    free_page(page);

    q.since = "2026-07-01T00:00:00.000Z";
    q.until = "2026-06-01T00:00:00.000Z";
    page = store_list(s, &q, k_now);
    ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
    ASSERT_EQ_INT((int)page.count, 0);
    ASSERT_EQ_INT((int)page.total, 0);
    free_page(page);
    store_close(s);
    free(db);
}

/* E5.3: search since/until stay on updated_at when sort is created. */
TEST(store_search_since_filters_updated_at)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    SearchQuery sq;
    PageResult page;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    (void)given_entry(s, (GivenEntry){.body = "edited alpha",
                                      .hash = k_hash_a,
                                      .created = "2026-01-01T00:00:00.000Z"});
    set_updated_at(db, 1, "2026-08-01T00:00:00.000Z");
    (void)given_entry(s, (GivenEntry){.body = "backdated alpha",
                                      .hash = k_hash_b,
                                      .created = "2026-09-01T00:00:00.000Z"});
    set_updated_at(db, 2, "2026-06-01T00:00:00.000Z");

    memset(&sq, 0, sizeof(sq));
    sq.query = "alpha";
    sq.filters.limit = LIST_LIMIT;
    sq.filters.sort = STORE_SORT_CREATED;
    sq.filters.since = "2026-07-15T00:00:00.000Z";
    page = store_search(s, &sq, k_now).page;
    ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
    ASSERT_EQ_INT((int)page.count, 1);
    ASSERT_EQ_INT((int)page.total, 1);
    if (page.count == 1U) {
        ASSERT_STREQ(page.entries[0].body, "edited alpha");
    }
    free_page(page);

    sq.filters.since = "2026-08-01T00:00:00.000Z";
    sq.filters.until = "2026-08-01T00:00:00.000Z";
    page = store_search(s, &sq, k_now).page;
    ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
    ASSERT_EQ_INT((int)page.count, 0);
    ASSERT_EQ_INT((int)page.total, 0);
    free_page(page);

    sq.filters.until = "2026-08-01T00:00:00.001Z";
    page = store_search(s, &sq, k_now).page;
    ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
    ASSERT_EQ_INT((int)page.count, 1);
    ASSERT_EQ_INT((int)page.total, 1);
    if (page.count == 1U) {
        ASSERT_STREQ(page.entries[0].body, "edited alpha");
    }
    free_page(page);
    store_close(s);
    free(db);
}

/* E5.3: search keeps bm25 then updated_at order even when the query asks for created. */
TEST(store_search_ignores_sort_for_order)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    SearchQuery sq;
    PageResult page;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    (void)given_entry(
        s, (GivenEntry){.body = "alpha", .hash = k_hash_a, .created = "2026-01-01T00:00:00.000Z"});
    set_updated_at(db, 1, "2026-09-01T00:00:00.000Z");
    (void)given_entry(
        s, (GivenEntry){.body = "alpha", .hash = k_hash_b, .created = "2026-05-01T00:00:00.000Z"});

    memset(&sq, 0, sizeof(sq));
    sq.query = "alpha";
    sq.filters.limit = LIST_LIMIT;
    sq.filters.sort = STORE_SORT_CREATED;
    page = store_search(s, &sq, k_now).page;
    ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
    ASSERT_EQ_INT((int)page.count, 2);
    if (page.count == 2U) {
        ASSERT_EQ_INT((int)page.entries[0].id, 1);
        ASSERT_EQ_INT((int)page.entries[1].id, 2);
    }
    free_page(page);
    store_close(s);
    free(db);
}

/* Search rank ties on bm25 and updated_at break toward the higher id. */
TEST(store_search_ties_break_by_id)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    SearchQuery sq;
    PageResult page;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    (void)given_entry(
        s, (GivenEntry){.body = "alpha", .hash = k_hash_a, .created = "2026-01-01T00:00:00.000Z"});
    (void)given_entry(
        s, (GivenEntry){.body = "alpha", .hash = k_hash_b, .created = "2026-01-01T00:00:00.000Z"});

    memset(&sq, 0, sizeof(sq));
    sq.query = "alpha";
    sq.filters.limit = LIST_LIMIT;
    page = store_search(s, &sq, k_now).page;
    ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
    ASSERT_EQ_INT((int)page.count, 2);
    if (page.count == 2U) {
        ASSERT_EQ_INT((int)page.entries[0].id, 2);
        ASSERT_EQ_INT((int)page.entries[1].id, 1);
    }
    free_page(page);
    store_close(s);
    free(db);
}

/* Time bounds AND with tag, source, key, bin, and paging. total is the filtered set. */
TEST(store_list_bounds_compose_with_filters)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    const char *tag_t[] = {"t"};
    const char *tag_z[] = {"z"};
    ListQuery q;
    PageResult page;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    (void)given_entry(s, (GivenEntry){.body = "in-a",
                                      .hash = k_hash_a,
                                      .tags = tag_t,
                                      .ntags = 1U,
                                      .created = "2026-06-10T00:00:00.000Z"});
    (void)given_entry(s, (GivenEntry){.body = "in-b",
                                      .hash = k_hash_b,
                                      .key = "slot",
                                      .tags = tag_t,
                                      .ntags = 1U,
                                      .created = "2026-06-12T00:00:00.000Z"});
    (void)given_entry(s, (GivenEntry){.body = "other-tag",
                                      .hash = k_hash_c,
                                      .tags = tag_z,
                                      .ntags = 1U,
                                      .created = "2026-06-12T00:00:00.000Z"});
    (void)given_entry(s, (GivenEntry){.body = "other-source",
                                      .hash = k_hash_d,
                                      .tags = tag_t,
                                      .ntags = 1U,
                                      .source = "agent",
                                      .created = "2026-06-12T00:00:00.000Z"});
    (void)given_entry(s, (GivenEntry){.body = "too-early",
                                      .hash = k_hash_e,
                                      .tags = tag_t,
                                      .ntags = 1U,
                                      .created = "2026-05-01T00:00:00.000Z"});
    (void)given_entry(s, (GivenEntry){.body = "expired-in",
                                      .hash = k_hash_f,
                                      .tags = tag_t,
                                      .ntags = 1U,
                                      .created = "2026-06-11T00:00:00.000Z"});
    free(harness_sqlite_query_line(
        db, "UPDATE entries SET expires_at='2020-01-01T00:00:00.000Z' WHERE local_id=6;"));

    memset(&q, 0, sizeof(q));
    q.tags = tag_t;
    q.ntags = 1U;
    q.source = "human";
    q.limit = 1U;
    q.offset = 0U;
    q.since = "2026-06-01T00:00:00.000Z";
    q.until = "2026-07-01T00:00:00.000Z";
    page = store_list(s, &q, k_now);
    ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
    ASSERT_EQ_INT((int)page.count, 1);
    ASSERT_EQ_INT((int)page.total, 2);
    ASSERT_STREQ(page.entries[0].body, "in-b");
    free_page(page);

    q.offset = 1U;
    page = store_list(s, &q, k_now);
    ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
    ASSERT_EQ_INT((int)page.count, 1);
    ASSERT_EQ_INT((int)page.total, 2);
    ASSERT_STREQ(page.entries[0].body, "in-a");
    free_page(page);

    q.offset = 2U;
    page = store_list(s, &q, k_now);
    ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
    ASSERT_EQ_INT((int)page.count, 0);
    ASSERT_EQ_INT((int)page.total, 2);
    free_page(page);

    memset(&q, 0, sizeof(q));
    q.key = "slot";
    q.limit = LIST_LIMIT;
    q.since = "2026-06-01T00:00:00.000Z";
    q.until = "2026-07-01T00:00:00.000Z";
    page = store_list(s, &q, k_now);
    ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
    ASSERT_EQ_INT((int)page.count, 1);
    ASSERT_EQ_INT((int)page.total, 1);
    ASSERT_STREQ(page.entries[0].body, "in-b");
    free_page(page);

    memset(&q, 0, sizeof(q));
    q.tags = tag_t;
    q.ntags = 1U;
    q.source = "human";
    q.bin = STORE_BIN_EXPIRED;
    q.limit = LIST_LIMIT;
    q.since = "2026-06-01T00:00:00.000Z";
    q.until = "2026-07-01T00:00:00.000Z";
    page = store_list(s, &q, k_now);
    ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
    ASSERT_EQ_INT((int)page.count, 1);
    ASSERT_EQ_INT((int)page.total, 1);
    ASSERT_STREQ(page.entries[0].body, "expired-in");
    free_page(page);
    store_close(s);
    free(db);
}

TEST(store_list_unknown_sort_is_internal)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    ListQuery q;
    PageResult page;
    enum { UNKNOWN_STORE_SORT = 7 };

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&q, 0, sizeof(q));
    q.limit = LIST_LIMIT;
    /* NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) */
    q.sort = (StoreSort)UNKNOWN_STORE_SORT;
    q.since = "2026-01-01T00:00:00.000Z";
    page = store_list(s, &q, k_now);
    ASSERT_EQ_INT((int)page.st, (int)STORE_ERR_INTERNAL);
    ASSERT_TRUE(page.entries == NULL);
    ASSERT_EQ_INT((int)page.count, 0);
    ASSERT_EQ_INT((int)page.total, 0);
    store_close(s);
    free(db);
}

TEST(store_list_filters_and_paging)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    const char *tags_ab[] = {"a", "b"};
    const char *tags_a[] = {"a"};
    ListQuery q;
    Entry *rows = NULL;
    size_t count = 0U;
    size_t total = 0U;
    size_t i = 0;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "alpha", k_hash_a, NULL, tags_a, 1U, "human", NULL, k_now, &act, &e),
        STORE_OK);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(
        store_add(s, "beta", k_hash_b, NULL, tags_a, 1U, "agent", NULL, k_now, &act, &e), STORE_OK);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(
        store_add(s, "gamma", k_hash_c, "slot", tags_ab, 2U, "tool", NULL, k_now, &act, &e),
        STORE_OK);
    store_entry_free(&e);

    memset(&q, 0, sizeof(q));
    q.tags = tags_ab;
    q.ntags = 2U;
    q.limit = LIST_LIMIT;
    q.offset = 0U;
    {
        PageResult page = store_list(s, &q, k_now);
        ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
        rows = page.entries;
        count = page.count;
        total = page.total;
    }
    ASSERT_EQ_INT((int)count, 1);
    ASSERT_EQ_INT((int)total, 1);
    ASSERT_TRUE(rows != NULL && rows[0].body != NULL);
    ASSERT_STREQ(rows[0].body, "gamma");
    for (i = 0; i < count; i++) {
        store_entry_free(&rows[i]);
    }
    free(rows);
    rows = NULL;

    memset(&q, 0, sizeof(q));
    q.source = "agent";
    q.limit = LIST_LIMIT;
    {
        PageResult page = store_list(s, &q, k_now);
        ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
        rows = page.entries;
        count = page.count;
    }
    ASSERT_EQ_INT((int)count, 1);
    ASSERT_STREQ(rows[0].body, "beta");
    for (i = 0; i < count; i++) {
        store_entry_free(&rows[i]);
    }
    free(rows);
    rows = NULL;

    memset(&q, 0, sizeof(q));
    q.key = "slot";
    q.limit = LIST_LIMIT;
    {
        PageResult page = store_list(s, &q, k_now);
        ASSERT_EQ_INT((int)page.st, (int)STORE_OK);
        rows = page.entries;
        count = page.count;
        total = page.total;
    }
    ASSERT_EQ_INT((int)total, 1);
    ASSERT_STREQ(rows[0].body, "gamma");
    for (i = 0; i < count; i++) {
        store_entry_free(&rows[i]);
    }
    free(rows);

    store_close(s);
    free(db);
}

TEST(store_update_if_version_vector_mismatch_writes_nothing)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    char vv_before[STALE_VV_CAP];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    const char *tags[] = {"keep"};
    long long conflict = 0;
    int vv_n = 0;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }

    memset(&e, 0, sizeof(e));
    memset(vv_before, 0, sizeof(vv_before));
    ASSERT_EQ_STATUS(store_add(s, "old", k_hash_a, NULL, tags, 1U, "human", NULL, k_now, &act, &e),
                     STORE_OK);
    if (e.body == NULL || e.updated_at == NULL || e.version_vector == NULL || e.tags == NULL ||
        e.ntags != 1U || e.tags[0] == NULL) {
        store_entry_free(&e);
        store_close(s);
        free(db);
        return;
    }
    ASSERT_STREQ(e.body, "old");
    ASSERT_STREQ(e.updated_at, k_now);
    ASSERT_STREQ(e.tags[0], "keep");
    vv_n = snprintf(vv_before, sizeof(vv_before), "%s", e.version_vector);
    ASSERT_TRUE(vv_n > 0 && (size_t)vv_n < sizeof(vv_before));
    store_entry_free(&e);

    memset(&e, 0, sizeof(e));
    {
        const char *stamp = "{\"other-device\":1}";
        StoreStatus st = store_update(s, 1,
                                      &(StoreUpdate){
                                          .set_body = true,
                                          .body = "new",
                                          .body_hash = k_hash_b,
                                          .if_version_vector = stamp,
                                      },
                                      k_now, &e, &conflict);
        ASSERT_EQ_INT((int)st, (int)STORE_ERR_STALE);
        if (st != STORE_ERR_STALE) {
            store_entry_free(&e);
            store_close(s);
            free(db);
            return;
        }
    }
    ASSERT_STREQ(store_status_message(STORE_ERR_STALE), "memory changed since it was read");
    store_entry_free(&e);

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_LIVE, k_now, &e), STORE_OK);
    if (e.body == NULL || e.updated_at == NULL || e.version_vector == NULL || e.tags == NULL ||
        e.ntags != 1U || e.tags[0] == NULL) {
        store_entry_free(&e);
        store_close(s);
        free(db);
        return;
    }
    ASSERT_STREQ(e.body, "old");
    ASSERT_STREQ(e.updated_at, k_now);
    ASSERT_STREQ(e.version_vector, vv_before);
    ASSERT_STREQ(e.tags[0], "keep");
    ASSERT_TRUE(e.deleted_at == NULL);
    store_entry_free(&e);

    /* The stale return rolls the write transaction back, so a later update works. */
    memset(&e, 0, sizeof(e));
    {
        StoreStatus again = store_update(s, 1,
                                         &(StoreUpdate){
                                             .set_body = true,
                                             .body = "new",
                                             .body_hash = k_hash_b,
                                         },
                                         k_now, &e, &conflict);
        ASSERT_EQ_INT((int)again, (int)STORE_OK);
        if (again != STORE_OK || e.body == NULL) {
            store_entry_free(&e);
            store_close(s);
            free(db);
            return;
        }
    }
    ASSERT_STREQ(e.body, "new");
    store_entry_free(&e);

    store_close(s);
    free(db);
}

TEST(store_update_equal_version_vector_writes)
{
    char *db = NULL;
    RowStamp before;
    Store *s = given_live_add(&db, "old", k_hash_a, NULL, &before);

    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    ASSERT_EQ_STATUS(when_guarded_body_update(s, before.version_vector, k_later), STORE_OK);
    then_body_is(s, "new");
    store_close(s);
    free(db);
}

/* Whitespace is not part of the vector. Byte equality would refuse this. */
TEST(store_update_version_vector_ignores_whitespace)
{
    char *db = NULL;
    char vector[STALE_VV_CAP];
    RowStamp before;
    Store *s = given_live_add(&db, "old", k_hash_a, NULL, &before);

    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    ASSERT_TRUE(snprintf(vector, sizeof(vector), "{ \"%s\" : 1 }", store_device_id(s)) > 0);
    ASSERT_EQ_STATUS(when_guarded_body_update(s, vector, k_later), STORE_OK);
    then_body_is(s, "new");
    store_close(s);
    free(db);
}

TEST(store_update_empty_version_vector_is_bad)
{
    char *db = NULL;
    RowStamp before;
    Store *s = given_live_add(&db, "old", k_hash_a, NULL, &before);

    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    ASSERT_EQ_STATUS(when_guarded_body_update(s, "", k_now), STORE_ERR_BAD_VV);
    then_body_is(s, "old");
    store_close(s);
    free(db);
}

/* An explicit 0 for a device the row does not mention is the same map.
   Key order does not matter. */
TEST(store_update_version_vector_missing_device_is_zero)
{
    char *db = NULL;
    char vector[STALE_VV_CAP];
    RowStamp before;
    Store *s = given_live_add(&db, "old", k_hash_a, NULL, &before);

    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    ASSERT_TRUE(snprintf(vector, sizeof(vector), "{\"other\":0,\"%s\":1}", store_device_id(s)) > 0);
    ASSERT_EQ_STATUS(when_guarded_body_update(s, vector, k_later), STORE_OK);
    then_body_is(s, "new");
    store_close(s);
    free(db);
}

/* The caller saw a device the row never had: the given side differs. */
TEST(store_update_extra_device_is_stale)
{
    char *db = NULL;
    char vector[STALE_VV_CAP];
    RowStamp before;
    Store *s = given_live_add(&db, "old", k_hash_a, NULL, &before);

    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    ASSERT_TRUE(snprintf(vector, sizeof(vector), "{\"%s\":1,\"other\":1}", store_device_id(s)) > 0);
    ASSERT_EQ_STATUS(when_guarded_body_update(s, vector, k_now), STORE_ERR_STALE);
    then_body_is(s, "old");
    store_close(s);
    free(db);
}

/* The row has a counter the caller never saw: only the row side differs. */
TEST(store_update_empty_object_is_stale)
{
    char *db = NULL;
    RowStamp before;
    Store *s = given_live_add(&db, "old", k_hash_a, NULL, &before);

    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    ASSERT_EQ_STATUS(when_guarded_body_update(s, "{}", k_now), STORE_ERR_STALE);
    then_body_is(s, "old");
    store_close(s);
    free(db);
}

TEST(store_update_changed_counter_is_stale)
{
    char *db = NULL;
    char vector[STALE_VV_CAP];
    RowStamp before;
    Store *s = given_live_add(&db, "old", k_hash_a, NULL, &before);

    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    ASSERT_TRUE(snprintf(vector, sizeof(vector), "{\"%s\":2}", store_device_id(s)) > 0);
    ASSERT_EQ_STATUS(when_guarded_body_update(s, vector, k_now), STORE_ERR_STALE);
    then_body_is(s, "old");
    store_close(s);
    free(db);
}

/* Overwrite row 1's stored vector, bypassing the store's own writers.
   db path first, like harness_sqlite_query_line. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void given_row_version_vector(const char *db, const char *vector)
{
    char sql[SQL_BUFSIZE];

    ASSERT_TRUE(snprintf(sql, sizeof(sql),
                         "UPDATE entries SET version_vector='%s' WHERE local_id=1;", vector) > 0);
    free(harness_sqlite_query_line(db, sql));
}

TEST(store_update_malformed_version_vector_writes_nothing)
{
    char *db = NULL;
    RowStamp before;
    Store *s = given_live_add(&db, "old", k_hash_a, NULL, &before);
    /* 2^63 is not an int64. SQLite still reports type integer and saturates the cast. */
    const char *bad[] = {"[]",          "{\"d\":-1}",
                         "{\"d\":1.5}", "{\"d\":\"1\"}",
                         "{\"\":1}",    "{\"d\":9223372036854775808}"};
    size_t i = 0;

    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        ASSERT_EQ_STATUS(when_guarded_body_update(s, bad[i], k_now), STORE_ERR_BAD_VV);
    }
    then_body_is(s, "old");
    store_close(s);
    free(db);
}

/* decision:plan17a-duplicate-vv-key: agreeing copies would match the row as
   that one clock, so a repeated key is malformed even when the copies agree. */
TEST(store_update_repeated_device_key_is_bad)
{
    char *db = NULL;
    char agree[STALE_VV_CAP];
    char differ[STALE_VV_CAP];
    RowStamp before;
    Store *s = given_live_add(&db, "old", k_hash_a, NULL, &before);
    const char *device = NULL;

    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    device = store_device_id(s);
    ASSERT_TRUE(device != NULL);
    if (device == NULL) {
        store_close(s);
        free(db);
        return;
    }
    ASSERT_TRUE(snprintf(agree, sizeof(agree), "{\"%s\":1,\"%s\":1}", device, device) > 0);
    ASSERT_TRUE(snprintf(differ, sizeof(differ), "{\"%s\":1,\"%s\":2}", device, device) > 0);
    ASSERT_EQ_STATUS(when_guarded_body_update(s, agree, k_later), STORE_ERR_BAD_VV);
    ASSERT_EQ_STATUS(when_guarded_body_update(s, differ, k_later), STORE_ERR_BAD_VV);
    then_body_is(s, "old");
    store_close(s);
    free(db);
}

/* A stored vector with a repeated key cannot authorize a write. */
TEST(store_update_stored_repeated_device_key_is_stale)
{
    char *db = NULL;
    char agree[STALE_VV_CAP];
    char differ[STALE_VV_CAP];
    char given[STALE_VV_CAP];
    RowStamp before;
    Store *s = given_live_add(&db, "old", k_hash_a, NULL, &before);
    const char *device = NULL;

    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    device = store_device_id(s);
    ASSERT_TRUE(device != NULL);
    if (device == NULL) {
        store_close(s);
        free(db);
        return;
    }
    ASSERT_TRUE(snprintf(agree, sizeof(agree), "{\"%s\":1,\"%s\":1}", device, device) > 0);
    ASSERT_TRUE(snprintf(differ, sizeof(differ), "{\"%s\":1,\"%s\":2}", device, device) > 0);
    ASSERT_TRUE(snprintf(given, sizeof(given), "{\"%s\":1}", device) > 0);
    given_row_version_vector(db, agree);
    ASSERT_EQ_STATUS(when_guarded_body_update(s, given, k_later), STORE_ERR_STALE);
    given_row_version_vector(db, differ);
    ASSERT_EQ_STATUS(when_guarded_body_update(s, given, k_later), STORE_ERR_STALE);
    then_body_is(s, "old");
    store_close(s);
    free(db);
}

/* Two different devices are not a repeated key. */
TEST(store_update_two_device_version_vector_matches)
{
    char *db = NULL;
    char vector[STALE_VV_CAP];
    RowStamp before;
    Store *s = given_live_add(&db, "old", k_hash_a, NULL, &before);

    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    ASSERT_TRUE(snprintf(vector, sizeof(vector), "{\"%s\":1,\"other\":2}", store_device_id(s)) > 0);
    given_row_version_vector(db, vector);
    ASSERT_EQ_STATUS(when_guarded_body_update(s, vector, k_later), STORE_OK);
    then_body_is(s, "new");
    store_close(s);
    free(db);
}

/* 2^63 must not pass as int64 max. CAST saturates both to the same clock. */
TEST(store_update_int64_overflow_does_not_match_max)
{
    char *db = NULL;
    char stored[STALE_VV_CAP];
    char attack[STALE_VV_CAP];
    RowStamp before;
    Store *s = given_live_add(&db, "old", k_hash_a, NULL, &before);
    const char *device = NULL;

    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    device = store_device_id(s);
    ASSERT_TRUE(device != NULL);
    if (device == NULL) {
        store_close(s);
        free(db);
        return;
    }
    ASSERT_TRUE(snprintf(stored, sizeof(stored), "{\"%s\":9223372036854775808}", device) > 0);
    given_row_version_vector(db, stored);
    ASSERT_TRUE(snprintf(attack, sizeof(attack), "{\"%s\":9223372036854775807}", device) > 0);
    ASSERT_EQ_STATUS(when_guarded_body_update(s, attack, k_now), STORE_ERR_STALE);
    then_body_is(s, "old");
    store_close(s);
    free(db);
}

/* The largest int64 is a real counter. The overflow check must not reject it. */
TEST(store_update_int64_max_matches_itself)
{
    char *db = NULL;
    char vector[STALE_VV_CAP];
    RowStamp before;
    Store *s = given_live_add(&db, "old", k_hash_a, NULL, &before);
    const char *device = NULL;

    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    device = store_device_id(s);
    ASSERT_TRUE(device != NULL);
    if (device == NULL) {
        store_close(s);
        free(db);
        return;
    }
    ASSERT_TRUE(snprintf(vector, sizeof(vector), "{\"%s\":9223372036854775807}", device) > 0);
    given_row_version_vector(db, vector);
    ASSERT_EQ_STATUS(when_guarded_body_update(s, vector, k_later), STORE_OK);
    then_body_is(s, "new");
    store_close(s);
    free(db);
}

/* W2 shares W1's timestamp. The vector, not the clock, is what makes W1 stale. */
TEST(store_update_same_timestamp_is_still_stale)
{
    char *db = NULL;
    RowStamp first;
    Store *s = given_live_add(&db, "first", k_hash_a, NULL, &first);
    Entry e;
    long long conflict = 0;

    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    /* The second write reuses the add's millisecond, so only the VV can tell them apart. */
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_update(s, 1,
                                  &(StoreUpdate){
                                      .set_body = true,
                                      .body = "second",
                                      .body_hash = k_hash_c,
                                  },
                                  k_now, &e, &conflict),
                     STORE_OK);
    ASSERT_STREQ(e.updated_at, first.updated_at);
    ASSERT_TRUE(e.version_vector != NULL && strcmp(e.version_vector, first.version_vector) != 0);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(when_guarded_body_update(s, first.version_vector, k_now), STORE_ERR_STALE);
    then_body_is(s, "second");
    store_close(s);
    free(db);
}

/* decision:plan17-stale-after-bin: a wrong bin wins over a mismatched stamp. */
TEST(store_update_wrong_bin_wins_over_stale)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    const char *stamp = "2020-01-01T00:00:00.000Z";
    long long conflict = 0;
    StoreStatus st = STORE_OK;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_add(s, "old", k_hash_a, NULL, NULL, 0U, "human", NULL, k_now, &act, &e),
                     STORE_OK);
    store_entry_free(&e);
    free(harness_sqlite_query_line(db, "UPDATE entries SET expires_at='2020-01-01T00:00:00.000Z' "
                                       "WHERE local_id=1;"));

    memset(&e, 0, sizeof(e));
    st = store_update(s, 1,
                      &(StoreUpdate){
                          .set_body = true,
                          .body = "new",
                          .body_hash = k_hash_b,
                          .if_version_vector = stamp,
                      },
                      k_now, &e, &conflict);
    ASSERT_EQ_INT((int)st, (int)STORE_ERR_EXPIRED);
    store_entry_free(&e);

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_EXPIRED, k_now, &e), STORE_OK);
    if (e.body == NULL || e.updated_at == NULL) {
        store_entry_free(&e);
        store_close(s);
        free(db);
        return;
    }
    ASSERT_STREQ(e.body, "old");
    ASSERT_STREQ(e.updated_at, k_now);
    store_entry_free(&e);

    store_close(s);
    free(db);
}

TEST(store_update_body_and_tags)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    const char *tags_ab[] = {"a", "b"};
    const char *tags_z[] = {"z"};
    long long conflict = 0;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "old", k_hash_a, NULL, tags_ab, 2U, "human", NULL, k_now, &act, &e), STORE_OK);
    store_entry_free(&e);

    /* Body-only: tags unchanged. */
    ASSERT_EQ_STATUS(store_update(s, 1,
                                  &(StoreUpdate){
                                      .set_body = true,
                                      .body = "new",
                                      .body_hash = k_hash_b,
                                  },
                                  k_now, &e, &conflict),
                     STORE_OK);
    ASSERT_STREQ(e.body, "new");
    ASSERT_EQ_INT((int)e.ntags, 2);
    ASSERT_STREQ(e.source, "human");
    store_entry_free(&e);

    /* Tags-only replace + clear path via empty set. */
    ASSERT_EQ_STATUS(store_update(s, 1,
                                  &(StoreUpdate){
                                      .set_tags = true,
                                      .tags = tags_z,
                                      .ntags = 1U,
                                  },
                                  k_now, &e, &conflict),
                     STORE_OK);
    ASSERT_STREQ(e.body, "new");
    ASSERT_EQ_INT((int)e.ntags, 1);
    ASSERT_STREQ(e.tags[0], "z");
    store_entry_free(&e);

    ASSERT_EQ_STATUS(store_update(s, 1, &(StoreUpdate){.set_tags = true}, k_now, &e, &conflict),
                     STORE_OK);
    ASSERT_EQ_INT((int)e.ntags, 0);
    store_entry_free(&e);

    /* Keyed locator; no body-hash conflict against keyless peer. */
    ASSERT_EQ_STATUS(store_add(s, "peer", k_hash_c, NULL, NULL, 0U, "agent", NULL, k_now, &act, &e),
                     STORE_OK);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(
        store_add(s, "slotv1", k_hash_a, "slot", NULL, 0U, "tool", NULL, k_now, &act, &e),
        STORE_OK);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(store_update(s, 0,
                                  &(StoreUpdate){
                                      .key_or_null = "slot",
                                      .set_body = true,
                                      .body = "peer",
                                      .body_hash = k_hash_c,
                                  },
                                  k_now, &e, &conflict),
                     STORE_OK);
    ASSERT_STREQ(e.body, "peer");
    ASSERT_STREQ(e.key, "slot");
    store_entry_free(&e);

    /* Keyless conflict: cannot take another keyless hash. */
    ASSERT_EQ_STATUS(
        store_add(s, "solo", k_hash_a, NULL, NULL, 0U, "unknown", NULL, k_now, &act, &e), STORE_OK);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(store_update(s, 1,
                                  &(StoreUpdate){
                                      .set_body = true,
                                      .body = "peer",
                                      .body_hash = k_hash_c,
                                  },
                                  k_now, &e, &conflict),
                     STORE_ERR_CONFLICT);
    ASSERT_TRUE(conflict != 0);

    store_close(s);
    free(db);
}

TEST(store_delete_by_id_gcs_orphan_tags)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    const char *tags[] = {"solo"};
    char *count = NULL;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "only", k_hash_a, NULL, tags, 1U, "unknown", NULL, k_now, &act, &e), STORE_OK);
    ASSERT_EQ_INT((int)e.id, 1);
    store_entry_free(&e);

    ASSERT_EQ_STATUS(store_soft_delete_by_id(s, 1, k_now, &e), STORE_OK);
    ASSERT_STREQ(e.body, "only");
    store_entry_free(&e);
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_LIVE, k_now, &e), STORE_ERR_DELETED);
    store_entry_free(&e);

    count = harness_sqlite_query_line(db, "SELECT count(*) FROM tags WHERE name='solo';");
    ASSERT_TRUE(count != NULL);
    ASSERT_STREQ(count, "1");
    free(count);

    ASSERT_EQ_STATUS(store_hard_delete_by_id(s, 1, STORE_BIN_DELETED, k_now, &e), STORE_OK);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_DELETED, k_now, &e), STORE_ERR_NOT_FOUND);
    store_close(s);

    count = harness_sqlite_query_line(db, "SELECT count(*) FROM tags WHERE name='solo';");
    ASSERT_TRUE(count != NULL);
    ASSERT_STREQ(count, "0");
    free(count);
    free(db);
}

TEST(store_delete_by_key_missing)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_soft_delete_by_key(s, "nope", k_now, &e), STORE_ERR_NOT_FOUND);
    store_close(s);
    free(db);
}

TEST(store_tags_empty_db)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    TagCount *tags = NULL;
    size_t n = MANY_TAGS;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    ASSERT_EQ_STATUS(store_tags(s, STORE_BIN_LIVE, k_now, &tags, &n), STORE_OK);
    ASSERT_EQ_INT((int)n, 0);
    ASSERT_TRUE(tags == NULL);
    store_tags_free(tags, n);
    store_close(s);
    free(db);
}

TEST(store_tags_counts_and_sorted)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    const char *tags_az[] = {"zebra", "apple"};
    const char *tags_a[] = {"apple"};
    TagCount *tags = NULL;
    size_t n = 0U;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(
        store_add(s, "first", k_hash_a, NULL, tags_az, 2U, "human", NULL, k_now, &act, &e),
        STORE_OK);
    store_entry_free(&e);
    ASSERT_EQ_STATUS(
        store_add(s, "second", k_hash_b, NULL, tags_a, 1U, "human", NULL, k_now, &act, &e),
        STORE_OK);
    store_entry_free(&e);

    ASSERT_EQ_STATUS(store_tags(s, STORE_BIN_LIVE, k_now, &tags, &n), STORE_OK);
    ASSERT_EQ_INT((int)n, 2);
    ASSERT_TRUE(tags != NULL && n == 2U);
    if (tags == NULL || n != 2U) {
        store_tags_free(tags, n);
        store_close(s);
        free(db);
        return;
    }
    /* Sorted by name COLLATE BINARY: apple (in 2 entries) before zebra (in 1). */
    ASSERT_STREQ(tags[0].name, "apple");
    ASSERT_EQ_INT((int)tags[0].count, 2);
    ASSERT_STREQ(tags[1].name, "zebra");
    ASSERT_EQ_INT((int)tags[1].count, 1);
    store_tags_free(tags, n);
    store_close(s);
    free(db);
}

#ifdef REMEMBER_TEST_HOOKS
TEST(store_open_oom_on_store_struct)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    const Store *s = NULL;
    ASSERT_TRUE(db != NULL);
    store_test_fail_alloc_after(0); /* first malloc/calloc fails */
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s == NULL);
    ASSERT_STR_CONTAINS(err, "memory");
    free(db);
}

TEST(store_add_prepare_fail)
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
    /* Fail the first prepare inside store_add (lookup by body_hash). */
    store_test_fail_prepare_after(0);
    ASSERT_EQ_STATUS(
        store_add(s, "body", k_hash_a, NULL, NULL, 0U, "unknown", NULL, k_now, &act, &e),
        STORE_ERR_SQLITE);
    store_close(s);
    free(db);
}

TEST(store_get_prepare_fail)
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
    ASSERT_EQ_STATUS(store_add(s, "g", k_hash_b, NULL, NULL, 0U, "unknown", NULL, k_now, &act, &e),
                     STORE_OK);
    store_entry_free(&e);
    store_test_fail_prepare_after(0);
    ASSERT_EQ_STATUS(store_get(s, 1, STORE_BIN_LIVE, k_now, &e), STORE_ERR_SQLITE);
    store_close(s);
    free(db);
}

TEST(store_list_prepare_fail)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    ListQuery q;
    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&q, 0, sizeof(q));
    q.limit = LIST_LIMIT;
    store_test_fail_prepare_after(0);
    ASSERT_EQ_STATUS(store_list(s, &q, k_now).st, STORE_ERR_SQLITE);
    store_close(s);
    free(db);
}

TEST(store_search_prepare_and_step_fail)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    SearchQuery q;
    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_add(s, "searchable body helix", k_hash_a, NULL, NULL, 0U, "unknown",
                               NULL, k_now, &act, &e),
                     STORE_OK);
    store_entry_free(&e);
    memset(&q, 0, sizeof(q));
    q.query = "helix";
    q.filters.limit = LIST_LIMIT;

    /* The parse probe is statement 0 (test_store_search.c). Fail COUNT prepare. */
    store_test_fail_prepare_after(1);
    ASSERT_EQ_STATUS(store_search(s, &q, k_now).page.st, STORE_ERR_SQLITE);

    /* COUNT prepare ok; fail SELECT prepare. */
    store_test_fail_prepare_after(2);
    ASSERT_EQ_STATUS(store_search(s, &q, k_now).page.st, STORE_ERR_SQLITE);

    /* Fail the COUNT step. */
    store_test_fail_step_after(1);
    ASSERT_EQ_STATUS(store_search(s, &q, k_now).page.st, STORE_ERR_SQLITE);

    /* COUNT step ok; fail first SELECT step. */
    store_test_fail_step_after(2);
    ASSERT_EQ_STATUS(store_search(s, &q, k_now).page.st, STORE_ERR_SQLITE);

    store_close(s);
    free(db);
}

TEST(store_delete_prepare_fail)
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
    ASSERT_EQ_STATUS(store_add(s, "d", k_hash_c, NULL, NULL, 0U, "unknown", NULL, k_now, &act, &e),
                     STORE_OK);
    store_entry_free(&e);
    /* hatch COUNT prepare fails immediately */
    store_test_fail_prepare_after(0);
    ASSERT_EQ_STATUS(store_hard_delete_by_id(s, 1, STORE_BIN_EXPIRED, k_now, &e), STORE_ERR_SQLITE);
    store_close(s);
    free(db);
}

/* Fault-injection fuzz sweep: the store_test_fail_*(i % N) strides and the small
   buffer/iteration sizes below are intentionally arbitrary tuning knobs chosen to
   reach distinct failure points; naming each would obscure, not clarify. */
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
TEST(store_add_tag_alloc_fail)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    const char *tags[] = {"t1", "t2", "t3", "t4", "t5"};
    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    /* Allow several allocs then fail mid-tag load/add path. */
    store_test_fail_alloc_after(8);
    (void)store_add(s, "tagged", k_hash_a, NULL, tags, 5U, "unknown", NULL, k_now, &act, &e);
    store_entry_free(&e);
    store_test_fail_alloc_after(-1);
    store_close(s);
    free(db);
}

/* Free a store_list/store_search page (or no-op if *rows is NULL). */
static void sweep_free_page(Entry **rows, size_t count)
{
    size_t j = 0;

    if (*rows == NULL) {
        return;
    }
    for (j = 0; j < count; j++) {
        store_entry_free(&(*rows)[j]);
    }
    free(*rows);
    *rows = NULL;
}

static void sweep_list_faults(Store *s, const ListQuery *q, int i)
{
    Entry *rows = NULL;
    size_t count = 0U;

    store_test_fail_prepare_after(i % 8);
    {
        PageResult page = store_list(s, q, k_now);
        rows = page.entries;
        count = page.count;
    }
    sweep_free_page(&rows, count);
    store_test_fail_step_after(i % 5);
    {
        PageResult page = store_list(s, q, k_now);
        rows = page.entries;
        count = page.count;
    }
    sweep_free_page(&rows, count);
}

static void sweep_search_faults(Store *s, const char *const *tags, int i)
{
    Entry *rows = NULL;
    size_t count = 0U;
    SearchQuery sq;

    memset(&sq, 0, sizeof(sq));
    sq.query = "sweep";
    sq.filters.limit = 10U;
    sq.filters.offset = 0U;
    sq.filters.tags = tags;
    sq.filters.ntags = 1U;
    store_test_fail_prepare_after(i % 4);
    {
        PageResult page = store_search(s, &sq, k_now).page;
        rows = page.entries;
        count = page.count;
    }
    sweep_free_page(&rows, count);
    store_test_fail_step_after(i % 3);
    {
        PageResult page = store_search(s, &sq, k_now).page;
        rows = page.entries;
        count = page.count;
    }
    sweep_free_page(&rows, count);
}

/*
 * Sweep fault injection across mutators so OOM / prepare-error lines in
 * store_sqlite.c are actually executed (needed for 100% line coverage).
 */
TEST(store_fault_injection_sweep)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    int i = 0;
    ASSERT_TRUE(db != NULL);

    for (i = 0; i < 60; i++) {
        Store *s = NULL;
        Entry e;
        StoreAddAction act = STORE_ADD_CREATED;
        ListQuery q;
        char body[32];
        char hash[65];
        const char *tags[] = {"a", "b", "c"};

        store_test_fail_alloc_after(-1);
        store_test_fail_prepare_after(-1);
        store_test_fail_step_after(-1);
        store_test_fail_exec_after(-1);
        s = store_open(db, err, sizeof(err));
        if (s == NULL) {
            /* open may fail under exec/prepare injection on next iteration */
            store_test_fail_exec_after(i % 5);
            (void)store_open(db, err, sizeof(err));
            store_test_fail_exec_after(-1);
            continue;
        }
        memset(&e, 0, sizeof(e));
        (void)snprintf(body, sizeof(body), "sweep body %d", i);
        (void)snprintf(hash, sizeof(hash), "%064d", i % 1000);

        store_test_fail_prepare_after(i % 12);
        (void)store_add(s, body, hash, (i % 5 == 0) ? "k" : NULL, tags, 3U, "agent", NULL, k_now,
                        &act, &e);
        store_entry_free(&e);

        store_test_fail_alloc_after(i % 15);
        (void)store_add(s, body, hash, NULL, tags, 3U, "tool", NULL, k_now, &act, &e);
        store_entry_free(&e);

        store_test_fail_step_after(i % 10);
        (void)store_add(s, body, hash, NULL, tags, 1U, "human", NULL, k_now, &act, &e);
        store_entry_free(&e);

        memset(&q, 0, sizeof(q));
        q.limit = LIST_LIMIT;
        q.tags = tags;
        q.ntags = (size_t)(1U + (size_t)(i % 3));
        q.source = (i % 4 == 0) ? "agent" : NULL;
        q.key = (i % 7 == 0) ? "k" : NULL;
        sweep_list_faults(s, &q, i);
        sweep_search_faults(s, tags, i);

        store_test_fail_prepare_after(i % 6);
        (void)store_get(s, 1, STORE_BIN_LIVE, k_now, &e);
        store_entry_free(&e);
        store_test_fail_step_after(i % 4);
        (void)store_get(s, 1, STORE_BIN_LIVE, k_now, &e);
        store_entry_free(&e);
        store_test_fail_prepare_after(i % 6);
        (void)store_get_by_key(s, "k", STORE_BIN_LIVE, k_now, &e);
        store_entry_free(&e);

        store_test_fail_prepare_after(i % 10);
        (void)store_soft_delete_by_id(s, 1, k_now, &e);
        store_entry_free(&e);
        store_test_fail_step_after(i % 8);
        (void)store_hard_delete_by_id(s, 1, STORE_BIN_DELETED, k_now, &e);
        store_entry_free(&e);
        store_test_fail_exec_after(i % 6);
        (void)store_soft_delete_by_key(s, "k", k_now, &e);
        store_entry_free(&e);
        store_test_fail_alloc_after(i % 10);
        (void)store_hard_delete_by_key(s, "k", STORE_BIN_EXPIRED, k_now, &e);
        store_entry_free(&e);

        {
            long long conflict = 0;
            const char *utags[] = {"u"};
            store_test_fail_prepare_after(i % 9);
            (void)store_update(s, 1,
                               &(StoreUpdate){
                                   .set_body = true,
                                   .body = body,
                                   .body_hash = hash,
                                   .set_tags = true,
                                   .tags = utags,
                                   .ntags = 1U,
                               },
                               k_now, &e, &conflict);
            store_entry_free(&e);
            store_test_fail_step_after(i % 7);
            (void)store_update(s, 0, &(StoreUpdate){.key_or_null = "k", .set_tags = true}, k_now,
                               &e, &conflict);
            store_entry_free(&e);
            store_test_fail_alloc_after(i % 12);
            (void)store_update(s, 1,
                               &(StoreUpdate){
                                   .set_body = true,
                                   .body = body,
                                   .body_hash = hash,
                               },
                               k_now, &e, &conflict);
            store_entry_free(&e);
        }

        store_test_fail_alloc_after(-1);
        store_test_fail_prepare_after(-1);
        store_test_fail_step_after(-1);
        store_test_fail_exec_after(-1);
        store_close(s);
    }
    free(db);
    ASSERT_TRUE(1);
}
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

TEST(store_tags_prepare_fail)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    TagCount *tags = NULL;
    size_t n = 0U;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    store_test_fail_prepare_after(0);
    ASSERT_EQ_STATUS(store_tags(s, STORE_BIN_LIVE, k_now, &tags, &n), STORE_ERR_SQLITE);
    ASSERT_TRUE(tags == NULL);
    store_tags_free(tags, n);
    store_close(s);
    free(db);
}

TEST(store_tags_alloc_fail)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    const char *tags_in[] = {"apple"};
    TagCount *tags = NULL;
    size_t n = 0U;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_add(s, "x", k_hash_a, NULL, tags_in, 1U, "human", NULL, k_now, &act, &e),
                     STORE_OK);
    store_entry_free(&e);
    store_test_fail_alloc_after(0); /* first dup_str in store_tags fails */
    ASSERT_EQ_STATUS(store_tags(s, STORE_BIN_LIVE, k_now, &tags, &n), STORE_ERR_OOM);
    ASSERT_TRUE(tags == NULL);
    store_tags_free(tags, n);
    store_close(s);
    free(db);
}

TEST(store_tags_realloc_fail)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    const char *tags_in[] = {"apple"};
    TagCount *tags = NULL;
    size_t n = 0U;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_add(s, "x", k_hash_a, NULL, tags_in, 1U, "human", NULL, k_now, &act, &e),
                     STORE_OK);
    store_entry_free(&e);
    /* dup_str succeeds, the growth realloc (first append, cap 0->8) then fails. */
    store_test_fail_alloc_after(1);
    ASSERT_EQ_STATUS(store_tags(s, STORE_BIN_LIVE, k_now, &tags, &n), STORE_ERR_OOM);
    ASSERT_TRUE(tags == NULL);
    store_tags_free(tags, n);
    store_close(s);
    free(db);
}

TEST(store_tags_step_fail)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    TagCount *tags = NULL;
    size_t n = 0U;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    store_test_fail_step_after(0); /* first step errors -> rc != SQLITE_DONE path */
    ASSERT_EQ_STATUS(store_tags(s, STORE_BIN_LIVE, k_now, &tags, &n), STORE_ERR_SQLITE);
    ASSERT_TRUE(tags == NULL);
    store_tags_free(tags, n);
    store_close(s);
    free(db);
}

#endif /* REMEMBER_TEST_HOOKS */

void register_store_tests(void)
{
    RUN_TEST(store_open_creates_user_version_6);
    RUN_TEST(store_open_migrates_v1_to_v6);
    RUN_TEST(store_open_reopens_existing);
    RUN_TEST(store_open_refuses_user_version_too_new);
    RUN_TEST(store_open_refuses_negative_user_version);
    RUN_TEST(store_open_creates_parent_dir_0700);
    RUN_TEST(store_open_db_file_mode_0600);
    RUN_TEST(store_open_has_expected_schema);
    RUN_TEST(store_open_rolls_back_partial_create);
    RUN_TEST(store_open_concurrent_create_all_succeed);
    RUN_TEST(store_open_null_path_fails);
    RUN_TEST(store_open_empty_path_fails);
    RUN_TEST(store_open_tolerates_missing_err_buffer);
    RUN_TEST(store_open_truncates_error_to_buffer);
    RUN_TEST(store_open_parent_component_file_fails);
    RUN_TEST(store_open_unwritable_parent_fails);
    RUN_TEST(store_open_path_too_long_fails);
    RUN_TEST(store_open_rejects_non_database_file);
    RUN_TEST(store_close_null_is_safe);
    RUN_TEST(store_get_loads_expires_at);
    RUN_TEST(store_get_expired_without_trash_is_expired);
    RUN_TEST(store_get_expired_with_trash_ok);
    RUN_TEST(store_get_active_with_trash_is_not_expired);
    RUN_TEST(store_get_missing_stays_not_found);
    RUN_TEST(store_expires_at_equal_now_is_trash);
    RUN_TEST(store_list_and_search_bins);
    RUN_TEST(store_update_and_delete_wrong_bin);
    RUN_TEST(store_update_trash_clear_expires_restores);
    RUN_TEST(store_update_trash_future_expires_leaves_trash);
    RUN_TEST(store_add_keyless_revives_expired);
    RUN_TEST(store_add_keyed_revives_expired);
    RUN_TEST(store_add_keyed_change_bumps_version_vector);
    RUN_TEST(store_add_keyed_same_body_moves_neither_stamp);
    RUN_TEST(store_add_keyed_new_tag_bumps_both_stamps);
    RUN_TEST(store_add_keyless_new_tag_bumps_both_stamps);
    RUN_TEST(store_add_keyless_same_body_moves_neither_stamp);
    RUN_TEST(store_add_keyless_revive_bumps_both_stamps_once);
    RUN_TEST(store_add_keyed_revive_bumps_both_stamps_once);
    RUN_TEST(store_add_keyless_deleted_revive_bumps_once);
    RUN_TEST(store_add_same_tag_again_moves_neither_stamp);
    RUN_TEST(store_add_past_expires_born_in_trash);
    RUN_TEST(store_purge_trash_deletes_only_expired);
    RUN_TEST(store_list_sort_created_keeps_creation_order);
    RUN_TEST(store_list_since_inclusive_until_exclusive);
    RUN_TEST(store_list_updated_sort_puts_the_edit_first);
    RUN_TEST(store_list_created_tie_breaks_by_id);
    RUN_TEST(store_list_since_follows_sort_column);
    RUN_TEST(store_list_crossed_bounds_are_empty);
    RUN_TEST(store_search_since_filters_updated_at);
    RUN_TEST(store_search_ignores_sort_for_order);
    RUN_TEST(store_search_ties_break_by_id);
    RUN_TEST(store_list_bounds_compose_with_filters);
    RUN_TEST(store_list_unknown_sort_is_internal);
    RUN_TEST(store_list_filters_and_paging);
    RUN_TEST(store_update_if_version_vector_mismatch_writes_nothing);
    RUN_TEST(store_update_equal_version_vector_writes);
    RUN_TEST(store_update_version_vector_ignores_whitespace);
    RUN_TEST(store_update_empty_version_vector_is_bad);
    RUN_TEST(store_update_version_vector_missing_device_is_zero);
    RUN_TEST(store_update_extra_device_is_stale);
    RUN_TEST(store_update_empty_object_is_stale);
    RUN_TEST(store_update_changed_counter_is_stale);
    RUN_TEST(store_update_malformed_version_vector_writes_nothing);
    RUN_TEST(store_update_repeated_device_key_is_bad);
    RUN_TEST(store_update_stored_repeated_device_key_is_stale);
    RUN_TEST(store_update_two_device_version_vector_matches);
    RUN_TEST(store_update_int64_overflow_does_not_match_max);
    RUN_TEST(store_update_int64_max_matches_itself);
    RUN_TEST(store_update_same_timestamp_is_still_stale);
    RUN_TEST(store_update_wrong_bin_wins_over_stale);
    RUN_TEST(store_update_body_and_tags);
    RUN_TEST(store_delete_by_id_gcs_orphan_tags);
    RUN_TEST(store_delete_by_key_missing);
    RUN_TEST(store_tags_empty_db);
    RUN_TEST(store_tags_counts_and_sorted);
    register_store_link_tests();
    register_store_sync_tests();
    register_store_search_tests();
#ifdef REMEMBER_TEST_HOOKS
    RUN_TEST(store_open_oom_on_store_struct);
    RUN_TEST(store_tags_prepare_fail);
    RUN_TEST(store_tags_alloc_fail);
    RUN_TEST(store_tags_realloc_fail);
    RUN_TEST(store_tags_step_fail);
    RUN_TEST(store_add_prepare_fail);
    RUN_TEST(store_get_prepare_fail);
    RUN_TEST(store_list_prepare_fail);
    RUN_TEST(store_search_prepare_and_step_fail);
    RUN_TEST(store_delete_prepare_fail);
    RUN_TEST(store_add_tag_alloc_fail);
    RUN_TEST(store_fault_injection_sweep);
#endif
}
