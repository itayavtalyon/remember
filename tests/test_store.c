#include "harness.h"
#include "register.h"
#include "store.h"
#include "test.h"

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

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
    Store *s;

    ASSERT_TRUE(nested != NULL && parent != NULL);
    if (nested == NULL || parent == NULL) {
        goto cleanup;
    }
    ASSERT_EQ_INT(chmod(parent, 0500), 0);

    err[0] = '\0';
    s = store_open(nested, err, sizeof(err));
    ASSERT_TRUE(s == NULL);
    ASSERT_STR_CONTAINS(err, "cannot create database directory");

    /* Restore, or the temp-dir sweep cannot remove it. */
    ASSERT_EQ_INT(chmod(parent, 0700), 0);

cleanup:
    free(nested);
    free(parent);
    free(db);
}

TEST(store_open_path_too_long_fails)
{
    size_t n = (size_t)PATH_MAX + 16U;
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
}
