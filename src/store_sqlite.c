#include "store.h"

#include "sqlite3.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/*
 * PATH_MAX is POSIX, not ISO C. With -std=c11 -pedantic, glibc may omit it from
 * <limits.h> unless feature-test macros are set. Fall back to a sane stack cap.
 */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

struct Store {
    sqlite3 *db;
};

/* DDL only; version bump is applied after a successful body (same transaction). */
static const char k_schema_sql[] =
    "CREATE TABLE entries (\n"
    "  id INTEGER PRIMARY KEY,\n"
    "  key TEXT,\n"
    "  body TEXT NOT NULL,\n"
    "  body_hash TEXT NOT NULL,\n"
    "  source TEXT NOT NULL,\n"
    "  created_at TEXT NOT NULL,\n"
    "  updated_at TEXT NOT NULL\n"
    ");\n"
    "CREATE UNIQUE INDEX ux_entries_key ON entries(key) WHERE key IS NOT NULL;\n"
    "CREATE UNIQUE INDEX ux_entries_bodyhash ON entries(body_hash) WHERE key IS NULL;\n"
    "CREATE TABLE tags (\n"
    "  id INTEGER PRIMARY KEY,\n"
    "  name TEXT NOT NULL UNIQUE\n"
    ");\n"
    "CREATE TABLE entry_tags (\n"
    "  entry_id INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE,\n"
    "  tag_id INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,\n"
    "  PRIMARY KEY (entry_id, tag_id)\n"
    ");\n"
    "CREATE VIRTUAL TABLE entries_fts USING fts5(\n"
    "  body,\n"
    "  tags,\n"
    "  tokenize = 'unicode61 remove_diacritics 2'\n"
    ");\n";

static void set_err(char *err, size_t errlen, const char *msg)
{
    size_t n;

    if (err == NULL || errlen == 0U) {
        return;
    }
    n = strlen(msg);
    if (n >= errlen) {
        n = errlen - 1U;
    }
    memcpy(err, msg, n);
    err[n] = '\0';
}

/* detail is always a library/libc string (never NULL) — see call sites. */
static void set_errf(char *err, size_t errlen, const char *prefix, const char *detail)
{
    if (err == NULL || errlen == 0U) {
        return;
    }
    (void)snprintf(err, errlen, "%s: %s", prefix, detail);
}

/* Create every parent of path with mode 0700 (mkdir -p style). */
static int ensure_parent_dirs(const char *path, char *err, size_t errlen)
{
    char buf[PATH_MAX];
    size_t len = strlen(path);
    size_t i;

    if (len >= sizeof(buf)) {
        set_err(err, errlen, "database path is too long");
        return -1;
    }
    memcpy(buf, path, len + 1U);

    /* Walk components; the final segment is the file itself, so stop before it. */
    for (i = 1; i < len; i++) {
        if (buf[i] != '/') {
            continue;
        }
        buf[i] = '\0';
        if (mkdir(buf, 0700) != 0) {
            struct stat st;
            if (errno != EEXIST) {
                set_errf(err, errlen, "cannot create database directory", strerror(errno));
                return -1;
            }
            /* EEXIST alone is not enough: the component may be a plain file. */
            if (stat(buf, &st) != 0 || !S_ISDIR(st.st_mode)) {
                set_err(err, errlen, "database path component is not a directory");
                return -1;
            }
        }
        buf[i] = '/';
    }
    return 0;
}

static int exec_sql(sqlite3 *db, const char *sql, char *err, size_t errlen)
{
    if (sqlite3_exec(db, sql, NULL, NULL, NULL) != SQLITE_OK) {
        set_errf(err, errlen, "sqlite", sqlite3_errmsg(db));
        return -1;
    }
    return 0;
}

/* Best-effort ROLLBACK; does not overwrite *err (caller's failure message). */
static void rollback_quiet(sqlite3 *db)
{
    (void)sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
}

static int read_user_version(sqlite3 *db, int *out, char *err, size_t errlen)
{
    sqlite3_stmt *stmt = NULL;
    int rc;

    rc = sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        set_errf(err, errlen, "cannot read user_version", sqlite3_errmsg(db));
        return -1;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        /* Where a non-database file surfaces: "file is not a database". */
        set_errf(err, errlen, "cannot read user_version", sqlite3_errmsg(db));
        (void)sqlite3_finalize(stmt);
        return -1;
    }
    *out = sqlite3_column_int(stmt, 0);
    (void)sqlite3_finalize(stmt);
    return 0;
}

static int apply_pragmas(sqlite3 *db, char *err, size_t errlen)
{
    if (exec_sql(db, "PRAGMA foreign_keys = ON;", err, errlen) != 0) {
        return -1;
    }
    if (exec_sql(db, "PRAGMA busy_timeout = 5000;", err, errlen) != 0) {
        return -1;
    }
    return 0;
}

/* Gate an already-created database: 1 is current, anything else is refused. */
static int check_version(int version, char *err, size_t errlen)
{
    if (version == 1) {
        return 0;
    }
    if (version > 1) {
        set_err(err, errlen, "database is newer than this remember");
    } else {
        set_err(err, errlen, "unsupported database version");
    }
    return -1;
}

/*
 * Bring an open database to schema version 1.
 *
 * Create runs as one transaction (BEGIN → DDL → user_version=1 → COMMIT) so a
 * failure part-way cannot leave objects behind at user_version 0, which would
 * make every later open fail with "table entries already exists".
 *
 * The version is read a second time under the write lock: the first read happens
 * before we hold it, so a concurrent remember may have created the schema while
 * we waited, and our CREATEs would then collide.
 */
static int ensure_schema(sqlite3 *db, char *err, size_t errlen)
{
    int version = 0;

    if (read_user_version(db, &version, err, errlen) != 0) {
        return -1;
    }
    if (version != 0) {
        return check_version(version, err, errlen);
    }

    if (exec_sql(db, "BEGIN IMMEDIATE;", err, errlen) != 0) {
        return -1;
    }
    if (read_user_version(db, &version, err, errlen) != 0) {
        goto cleanup_fail;
    }
    if (version != 0) {
        rollback_quiet(db);
        return check_version(version, err, errlen);
    }
    if (exec_sql(db, k_schema_sql, err, errlen) != 0) {
        goto cleanup_fail;
    }
    if (exec_sql(db, "PRAGMA user_version = 1;", err, errlen) != 0) {
        goto cleanup_fail;
    }
    if (exec_sql(db, "COMMIT;", err, errlen) != 0) {
        goto cleanup_fail;
    }
    return 0;

cleanup_fail:
    rollback_quiet(db);
    return -1;
}

Store *store_open(const char *path, char *err, size_t errlen)
{
    Store *s = NULL;
    sqlite3 *db = NULL;

    if (path == NULL || path[0] == '\0') {
        set_err(err, errlen, "empty database path");
        return NULL;
    }

    if (ensure_parent_dirs(path, err, errlen) != 0) {
        return NULL;
    }

    s = calloc(1, sizeof(*s));
    if (s == NULL) {
        set_err(err, errlen, "out of memory");
        return NULL;
    }

    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
        /* sqlite3_errmsg is NULL-safe: reports OOM when the handle is NULL. */
        set_errf(err, errlen, "cannot open database", sqlite3_errmsg(db));
        goto cleanup_fail;
    }
    if (apply_pragmas(db, err, errlen) != 0) {
        goto cleanup_fail;
    }
    if (ensure_schema(db, err, errlen) != 0) {
        goto cleanup_fail;
    }

    s->db = db;
    if (err != NULL && errlen > 0U) {
        err[0] = '\0';
    }
    return s;

cleanup_fail:
    (void)sqlite3_close_v2(db);
    free(s);
    return NULL;
}

void store_close(Store *s)
{
    if (s == NULL) {
        return;
    }
    /* _v2 so a forgotten prepared statement defers the close instead of
       leaking the connection (sqlite3_close would return SQLITE_BUSY). */
    (void)sqlite3_close_v2(s->db);
    s->db = NULL;
    free(s);
}
