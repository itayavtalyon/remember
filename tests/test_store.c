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

/*
 * Unit suite for the store port (step 02). Black-box against store.h;
 * schema details inspected via the sqlite3 CLI helper when available.
 */

/* "<base><leaf>" on the heap; NULL on allocation failure. */
static char *join_path(const char *base, const char *leaf)
{
    size_t n;
    char *out;

    if (base == NULL || leaf == NULL) {
        return NULL;
    }
    n = strlen(base) + strlen(leaf) + 1U;
    out = malloc(n);
    if (out == NULL) {
        return NULL;
    }
    (void)snprintf(out, n, "%s%s", base, leaf);
    return out;
}

/* Assert that a one-line sqlite3 CLI query returns exactly want. */
static void assert_query_is(const char *db, const char *sql, const char *want)
{
    char *row = harness_sqlite_query_line(db, sql);

    ASSERT_TRUE(row != NULL);
    ASSERT_STREQ(row != NULL ? row : "", want);
    free(row);
}

TEST(store_open_creates_user_version_1)
{
    char *db = make_temp_db_path();
    char err[256];
    Store *s;

    ASSERT_TRUE(db != NULL);
    err[0] = '\0';
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    ASSERT_STREQ(err, "");
    store_close(s);

    assert_query_is(db, "PRAGMA user_version;", "1");
    free(db);
}

TEST(store_open_reopens_existing)
{
    char *db = make_temp_db_path();
    char err[256];
    Store *s1;
    Store *s2;

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
    char err[256];
    Store *s;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    store_close(s);

    free(harness_sqlite_query_line(db, "PRAGMA user_version=99;"));
    assert_query_is(db, "PRAGMA user_version;", "99");

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
    char err[256];
    Store *s;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    store_close(s);

    free(harness_sqlite_query_line(db, "PRAGMA user_version=-1;"));
    assert_query_is(db, "PRAGMA user_version;", "-1");

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
    char err[256];
    Store *s;
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
    ASSERT_EQ_INT((int)(st.st_mode & 0777), 0700);

cleanup:
    free(created);
    free(nested);
    free(parent);
    free(db);
}

TEST(store_open_db_file_mode_0600)
{
    char *db = make_temp_db_path();
    char err[256];
    Store *s;
    struct stat st;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    store_close(s);

    ASSERT_EQ_INT(stat(db, &st), 0);
    ASSERT_EQ_INT((int)(st.st_mode & 0777), 0600);
    free(db);
}

TEST(store_open_has_expected_schema)
{
    char *db = make_temp_db_path();
    char err[256];
    Store *s;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    store_close(s);

    assert_query_is(db, "SELECT name FROM sqlite_master WHERE type='table' AND name='entries';",
                    "entries");
    assert_query_is(db, "SELECT name FROM sqlite_master WHERE type='table' AND name='tags';",
                    "tags");
    assert_query_is(db, "SELECT name FROM sqlite_master WHERE type='table' AND name='entry_tags';",
                    "entry_tags");
    assert_query_is(db, "SELECT name FROM sqlite_master WHERE type='table' AND name='entries_fts';",
                    "entries_fts");
    assert_query_is(db,
                    "SELECT name FROM sqlite_master WHERE type='index' AND name='ux_entries_key';",
                    "ux_entries_key");
    assert_query_is(
        db, "SELECT name FROM sqlite_master WHERE type='index' AND name='ux_entries_bodyhash';",
        "ux_entries_bodyhash");

    /* Partial unique indexes: the identity axes from the design. */
    assert_query_is(db,
                    "SELECT CASE WHEN sql LIKE '%WHERE key IS NOT NULL%' THEN 'ok' ELSE 'bad' END "
                    "FROM sqlite_master WHERE name='ux_entries_key';",
                    "ok");
    assert_query_is(db,
                    "SELECT CASE WHEN sql LIKE '%WHERE key IS NULL%' THEN 'ok' ELSE 'bad' END "
                    "FROM sqlite_master WHERE name='ux_entries_bodyhash';",
                    "ok");
    /* FTS5 tokenizer string (design Round 7). */
    assert_query_is(db,
                    "SELECT CASE WHEN sql LIKE '%unicode61 remove_diacritics 2%' THEN 'ok' ELSE "
                    "'bad' END FROM sqlite_master WHERE name='entries_fts';",
                    "ok");
    /* Cascade FKs: step 05's orphan-tag GC depends on them. */
    assert_query_is(db,
                    "SELECT CASE WHEN sql LIKE '%ON DELETE CASCADE%' THEN 'ok' ELSE 'bad' END "
                    "FROM sqlite_master WHERE name='entry_tags';",
                    "ok");
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
    char err[256];
    Store *s;

    ASSERT_TRUE(db != NULL);
    free(harness_sqlite_query_line(db, "CREATE TABLE tags(x); PRAGMA user_version=0;"));

    err[0] = '\0';
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s == NULL);
    ASSERT_STR_CONTAINS(err, "already exists");

    assert_query_is(db, "SELECT count(*) FROM sqlite_master WHERE name='entries';", "0");
    assert_query_is(db, "PRAGMA user_version;", "0");
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
    int i;

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
            char cerr[256];
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
    assert_query_is(db, "PRAGMA user_version;", "1");
    free(db);
}

TEST(store_open_null_path_fails)
{
    char err[256];
    Store *s;

    err[0] = '\0';
    s = store_open(NULL, err, sizeof(err));
    ASSERT_TRUE(s == NULL);
    ASSERT_STR_CONTAINS(err, "empty");
}

TEST(store_open_empty_path_fails)
{
    char err[256];
    Store *s;

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
    char err[256];
    int fd;

    ASSERT_TRUE(nested != NULL);
    if (nested == NULL) {
        goto cleanup;
    }
    fd = open(blocker, O_CREAT | O_WRONLY | O_TRUNC, 0600);
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
    char err[8];
    Store *s;

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
    char err[256];
    Store *s;
    int fd;

    ASSERT_TRUE(nested != NULL);
    if (nested == NULL) {
        goto cleanup;
    }
    fd = open(blocker, O_CREAT | O_WRONLY | O_TRUNC, 0600);
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
    char err[256];
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
        (void)chmod(parent, 0700);
    }
    free(nested);
    free(parent);
    free(db);
}

TEST(store_open_path_too_long_fails)
{
    size_t n = (size_t)REMEMBER_PATH_MAX + 16U;
    char *path = malloc(n + 1U);
    char err[256];
    Store *s;

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
    char err[256];
    Store *s;
    FILE *f;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
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

TEST(store_list_filters_and_paging)
{
    char *db = make_temp_db_path();
    char err[256];
    Store *s;
    Entry e;
    StoreAddAction act;
    const char *tags_ab[] = {"a", "b"};
    const char *tags_a[] = {"a"};
    ListQuery q;
    Entry *rows = NULL;
    size_t count = 0U;
    size_t total = 0U;
    size_t i;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_INT((int)store_add(s, "alpha", k_hash_a, NULL, tags_a, 1U, "human", &act, &e),
                  (int)STORE_OK);
    store_entry_free(&e);
    ASSERT_EQ_INT((int)store_add(s, "beta", k_hash_b, NULL, tags_a, 1U, "agent", &act, &e),
                  (int)STORE_OK);
    store_entry_free(&e);
    ASSERT_EQ_INT((int)store_add(s, "gamma", k_hash_c, "slot", tags_ab, 2U, "tool", &act, &e),
                  (int)STORE_OK);
    store_entry_free(&e);

    memset(&q, 0, sizeof(q));
    q.tags = tags_ab;
    q.ntags = 2U;
    q.limit = 20U;
    q.offset = 0U;
    ASSERT_EQ_INT((int)store_list(s, &q, &rows, &count, &total), (int)STORE_OK);
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
    q.limit = 20U;
    ASSERT_EQ_INT((int)store_list(s, &q, &rows, &count, &total), (int)STORE_OK);
    ASSERT_EQ_INT((int)count, 1);
    ASSERT_STREQ(rows[0].body, "beta");
    for (i = 0; i < count; i++) {
        store_entry_free(&rows[i]);
    }
    free(rows);
    rows = NULL;

    memset(&q, 0, sizeof(q));
    q.key = "slot";
    q.limit = 20U;
    ASSERT_EQ_INT((int)store_list(s, &q, &rows, &count, &total), (int)STORE_OK);
    ASSERT_EQ_INT((int)total, 1);
    ASSERT_STREQ(rows[0].body, "gamma");
    for (i = 0; i < count; i++) {
        store_entry_free(&rows[i]);
    }
    free(rows);

    store_close(s);
    free(db);
}

TEST(store_update_body_and_tags)
{
    char *db = make_temp_db_path();
    char err[256];
    Store *s;
    Entry e;
    StoreAddAction act;
    const char *tags_ab[] = {"a", "b"};
    const char *tags_z[] = {"z"};
    long long conflict = 0;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_INT((int)store_add(s, "old", k_hash_a, NULL, tags_ab, 2U, "human", &act, &e),
                  (int)STORE_OK);
    store_entry_free(&e);

    /* Body-only: tags unchanged. */
    ASSERT_EQ_INT(
        (int)store_update(s, 1, NULL, true, "new", k_hash_b, false, NULL, 0U, &e, &conflict),
        (int)STORE_OK);
    ASSERT_STREQ(e.body, "new");
    ASSERT_EQ_INT((int)e.ntags, 2);
    ASSERT_STREQ(e.source, "human");
    store_entry_free(&e);

    /* Tags-only replace + clear path via empty set. */
    ASSERT_EQ_INT((int)store_update(s, 1, NULL, false, NULL, NULL, true, tags_z, 1U, &e, &conflict),
                  (int)STORE_OK);
    ASSERT_STREQ(e.body, "new");
    ASSERT_EQ_INT((int)e.ntags, 1);
    ASSERT_STREQ(e.tags[0], "z");
    store_entry_free(&e);

    ASSERT_EQ_INT((int)store_update(s, 1, NULL, false, NULL, NULL, true, NULL, 0U, &e, &conflict),
                  (int)STORE_OK);
    ASSERT_EQ_INT((int)e.ntags, 0);
    store_entry_free(&e);

    /* Keyed locator; no body-hash conflict against keyless peer. */
    ASSERT_EQ_INT((int)store_add(s, "peer", k_hash_c, NULL, NULL, 0U, "agent", &act, &e),
                  (int)STORE_OK);
    store_entry_free(&e);
    ASSERT_EQ_INT((int)store_add(s, "slotv1", k_hash_a, "slot", NULL, 0U, "tool", &act, &e),
                  (int)STORE_OK);
    store_entry_free(&e);
    ASSERT_EQ_INT(
        (int)store_update(s, 0, "slot", true, "peer", k_hash_c, false, NULL, 0U, &e, &conflict),
        (int)STORE_OK);
    ASSERT_STREQ(e.body, "peer");
    ASSERT_STREQ(e.key, "slot");
    store_entry_free(&e);

    /* Keyless conflict: cannot take another keyless hash. */
    ASSERT_EQ_INT((int)store_add(s, "solo", k_hash_a, NULL, NULL, 0U, "unknown", &act, &e),
                  (int)STORE_OK);
    store_entry_free(&e);
    ASSERT_EQ_INT(
        (int)store_update(s, 1, NULL, true, "peer", k_hash_c, false, NULL, 0U, &e, &conflict),
        (int)STORE_ERR_CONFLICT);
    ASSERT_TRUE(conflict != 0);

    store_close(s);
    free(db);
}

TEST(store_delete_by_id_gcs_orphan_tags)
{
    char *db = make_temp_db_path();
    char err[256];
    Store *s;
    Entry e;
    StoreAddAction act;
    const char *tags[] = {"solo"};
    char *count;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);

    memset(&e, 0, sizeof(e));
    ASSERT_EQ_INT((int)store_add(s, "only", k_hash_a, NULL, tags, 1U, "unknown", &act, &e),
                  (int)STORE_OK);
    ASSERT_EQ_INT((int)e.id, 1);
    store_entry_free(&e);

    ASSERT_EQ_INT((int)store_delete_by_id(s, 1, &e), (int)STORE_OK);
    ASSERT_STREQ(e.body, "only");
    store_entry_free(&e);

    ASSERT_EQ_INT((int)store_get(s, 1, &e), (int)STORE_ERR_NOT_FOUND);
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
    char err[256];
    Store *s;
    Entry e;

    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_INT((int)store_delete_by_key(s, "nope", &e), (int)STORE_ERR_NOT_FOUND);
    store_close(s);
    free(db);
}

#ifdef REMEMBER_TEST_HOOKS
TEST(store_open_oom_on_store_struct)
{
    char *db = make_temp_db_path();
    char err[256];
    Store *s;
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
    char err[256];
    Store *s;
    Entry e;
    StoreAddAction act;
    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    /* Fail the first prepare inside store_add (lookup by body_hash). */
    store_test_fail_prepare_after(0);
    ASSERT_EQ_INT((int)store_add(s, "body", k_hash_a, NULL, NULL, 0U, "unknown", &act, &e),
                  (int)STORE_ERR_SQLITE);
    store_close(s);
    free(db);
}

TEST(store_get_prepare_fail)
{
    char *db = make_temp_db_path();
    char err[256];
    Store *s;
    Entry e;
    StoreAddAction act;
    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_INT((int)store_add(s, "g", k_hash_b, NULL, NULL, 0U, "unknown", &act, &e),
                  (int)STORE_OK);
    store_entry_free(&e);
    store_test_fail_prepare_after(0);
    ASSERT_EQ_INT((int)store_get(s, 1, &e), (int)STORE_ERR_SQLITE);
    store_close(s);
    free(db);
}

TEST(store_list_prepare_fail)
{
    char *db = make_temp_db_path();
    char err[256];
    Store *s;
    Entry *rows = NULL;
    size_t count = 0U;
    size_t total = 0U;
    ListQuery q;
    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&q, 0, sizeof(q));
    q.limit = 20U;
    store_test_fail_prepare_after(0);
    ASSERT_EQ_INT((int)store_list(s, &q, &rows, &count, &total), (int)STORE_ERR_SQLITE);
    store_close(s);
    free(db);
}

TEST(store_search_prepare_and_step_fail)
{
    char *db = make_temp_db_path();
    char err[256];
    Store *s;
    Entry e;
    Entry *rows = NULL;
    size_t count = 0U;
    size_t total = 0U;
    StoreAddAction act;
    SearchQuery q;
    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_INT(
        (int)store_add(s, "searchable body helix", k_hash_a, NULL, NULL, 0U, "unknown", &act, &e),
        (int)STORE_OK);
    store_entry_free(&e);
    memset(&q, 0, sizeof(q));
    q.query = "helix";
    q.filters.limit = 20U;

    /* Fail COUNT prepare. */
    store_test_fail_prepare_after(0);
    ASSERT_EQ_INT((int)store_search(s, &q, &rows, &count, &total), (int)STORE_ERR_SQLITE);

    /* COUNT prepare ok; fail SELECT prepare. */
    store_test_fail_prepare_after(1);
    ASSERT_EQ_INT((int)store_search(s, &q, &rows, &count, &total), (int)STORE_ERR_SQLITE);

    /* COUNT step ok; fail first SELECT step. */
    store_test_fail_step_after(1);
    ASSERT_EQ_INT((int)store_search(s, &q, &rows, &count, &total), (int)STORE_ERR_SQLITE);

    store_close(s);
    free(db);
}

TEST(store_delete_prepare_fail)
{
    char *db = make_temp_db_path();
    char err[256];
    Store *s;
    Entry e;
    StoreAddAction act;
    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_INT((int)store_add(s, "d", k_hash_c, NULL, NULL, 0U, "unknown", &act, &e),
                  (int)STORE_OK);
    store_entry_free(&e);
    /* load succeeds; fail prepare on DELETE FROM entries */
    store_test_fail_prepare_after(1);
    ASSERT_EQ_INT((int)store_delete_by_id(s, 1, &e), (int)STORE_ERR_SQLITE);
    store_close(s);
    free(db);
}

TEST(store_add_tag_alloc_fail)
{
    char *db = make_temp_db_path();
    char err[256];
    Store *s;
    Entry e;
    StoreAddAction act;
    const char *tags[] = {"t1", "t2", "t3", "t4", "t5"};
    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    /* Allow several allocs then fail mid-tag load/add path. */
    store_test_fail_alloc_after(8);
    (void)store_add(s, "tagged", k_hash_a, NULL, tags, 5U, "unknown", &act, &e);
    store_entry_free(&e);
    store_test_fail_alloc_after(-1);
    store_close(s);
    free(db);
}

/* Free a store_list/store_search page (or no-op if *rows is NULL). */
static void sweep_free_page(Entry **rows, size_t count)
{
    size_t j;

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
    size_t total = 0U;

    store_test_fail_prepare_after(i % 8);
    (void)store_list(s, q, &rows, &count, &total);
    sweep_free_page(&rows, count);
    store_test_fail_step_after(i % 5);
    (void)store_list(s, q, &rows, &count, &total);
    sweep_free_page(&rows, count);
}

static void sweep_search_faults(Store *s, const char *const *tags, int i)
{
    Entry *rows = NULL;
    size_t count = 0U;
    size_t total = 0U;
    SearchQuery sq;

    memset(&sq, 0, sizeof(sq));
    sq.query = "sweep";
    sq.filters.limit = 10U;
    sq.filters.offset = 0U;
    sq.filters.tags = tags;
    sq.filters.ntags = 1U;
    store_test_fail_prepare_after(i % 4);
    (void)store_search(s, &sq, &rows, &count, &total);
    sweep_free_page(&rows, count);
    store_test_fail_step_after(i % 3);
    (void)store_search(s, &sq, &rows, &count, &total);
    sweep_free_page(&rows, count);
}

/*
 * Sweep fault injection across mutators so OOM / prepare-error lines in
 * store_sqlite.c are actually executed (needed for 100% line coverage).
 */
TEST(store_fault_injection_sweep)
{
    char *db = make_temp_db_path();
    char err[256];
    int i;
    ASSERT_TRUE(db != NULL);

    for (i = 0; i < 60; i++) {
        Store *s;
        Entry e;
        StoreAddAction act;
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
        (void)store_add(s, body, hash, (i % 5 == 0) ? "k" : NULL, tags, 3U, "agent", &act, &e);
        store_entry_free(&e);

        store_test_fail_alloc_after(i % 15);
        (void)store_add(s, body, hash, NULL, tags, 3U, "tool", &act, &e);
        store_entry_free(&e);

        store_test_fail_step_after(i % 10);
        (void)store_add(s, body, hash, NULL, tags, 1U, "human", &act, &e);
        store_entry_free(&e);

        memset(&q, 0, sizeof(q));
        q.limit = 20U;
        q.tags = tags;
        q.ntags = (size_t)(1U + (size_t)(i % 3));
        q.source = (i % 4 == 0) ? "agent" : NULL;
        q.key = (i % 7 == 0) ? "k" : NULL;
        sweep_list_faults(s, &q, i);
        sweep_search_faults(s, tags, i);

        store_test_fail_prepare_after(i % 6);
        (void)store_get(s, 1, &e);
        store_entry_free(&e);
        store_test_fail_step_after(i % 4);
        (void)store_get(s, 1, &e);
        store_entry_free(&e);
        store_test_fail_prepare_after(i % 6);
        (void)store_get_by_key(s, "k", &e);
        store_entry_free(&e);

        store_test_fail_prepare_after(i % 10);
        (void)store_delete_by_id(s, 1, &e);
        store_entry_free(&e);
        store_test_fail_step_after(i % 8);
        (void)store_delete_by_id(s, 1, &e);
        store_entry_free(&e);
        store_test_fail_exec_after(i % 6);
        (void)store_delete_by_key(s, "k", &e);
        store_entry_free(&e);
        store_test_fail_alloc_after(i % 10);
        (void)store_delete_by_key(s, "k", &e);
        store_entry_free(&e);

        {
            long long conflict = 0;
            const char *utags[] = {"u"};
            store_test_fail_prepare_after(i % 9);
            (void)store_update(s, 1, NULL, true, body, hash, true, utags, 1U, &e, &conflict);
            store_entry_free(&e);
            store_test_fail_step_after(i % 7);
            (void)store_update(s, 0, "k", false, NULL, NULL, true, NULL, 0U, &e, &conflict);
            store_entry_free(&e);
            store_test_fail_alloc_after(i % 12);
            (void)store_update(s, 1, NULL, true, body, hash, false, NULL, 0U, &e, &conflict);
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
#endif /* REMEMBER_TEST_HOOKS */

void register_store_tests(void)
{
    RUN_TEST(store_open_creates_user_version_1);
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
    RUN_TEST(store_list_filters_and_paging);
    RUN_TEST(store_update_body_and_tags);
    RUN_TEST(store_delete_by_id_gcs_orphan_tags);
    RUN_TEST(store_delete_by_key_missing);
#ifdef REMEMBER_TEST_HOOKS
    RUN_TEST(store_open_oom_on_store_struct);
    RUN_TEST(store_add_prepare_fail);
    RUN_TEST(store_get_prepare_fail);
    RUN_TEST(store_list_prepare_fail);
    RUN_TEST(store_search_prepare_and_step_fail);
    RUN_TEST(store_delete_prepare_fail);
    RUN_TEST(store_add_tag_alloc_fail);
    RUN_TEST(store_fault_injection_sweep);
#endif
}
