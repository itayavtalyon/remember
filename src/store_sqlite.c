#include "store.h"

#include "sqlite3.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

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
    char buf[REMEMBER_PATH_MAX];
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

/* ---- status / entry helpers ---------------------------------------------- */

const char *store_status_message(StoreStatus st)
{
    switch (st) {
    case STORE_OK:
        return "ok";
    case STORE_ERR_NOT_FOUND:
        return "not found";
    case STORE_ERR_SQLITE:
        return "database error";
    case STORE_ERR_OOM:
        return "out of memory";
    case STORE_ERR_INTERNAL:
        return "internal error";
    default:
        return "store error";
    }
}

void store_entry_free(Entry *e)
{
    if (e == NULL) {
        return;
    }
    free(e->key);
    free(e->body);
    if (e->tags != NULL) {
        size_t i;
        for (i = 0; i < e->ntags; i++) {
            free(e->tags[i]);
        }
        free((void *)e->tags);
    }
    free(e->source);
    free(e->created_at);
    free(e->updated_at);
    e->key = NULL;
    e->body = NULL;
    e->tags = NULL;
    e->ntags = 0U;
    e->source = NULL;
    e->created_at = NULL;
    e->updated_at = NULL;
    e->id = 0;
}

static char *dup_str(const char *s)
{
    size_t n;
    char *p;

    if (s == NULL) {
        return NULL;
    }
    n = strlen(s);
    p = malloc(n + 1U);
    if (p == NULL) {
        return NULL;
    }
    memcpy(p, s, n + 1U);
    return p;
}

/* ISO-8601 UTC second precision, e.g. 2026-07-24T12:00:00Z (20 chars + NUL).
 * Uses gmtime (not gmtime_r): single-threaded CLI; gmtime_r is POSIX-only and
 * vanishes under pure -std=c11 without feature-test macros (Linux IWYU). */
static int utc_now(char *buf, size_t buflen)
{
    time_t t;
    const struct tm *tmp;
    struct tm tm;

    if (buf == NULL || buflen < 21U) {
        return -1;
    }
    t = time(NULL);
    if (t == (time_t)-1) {
        return -1;
    }
    tmp = gmtime(&t);
    if (tmp == NULL) {
        return -1;
    }
    tm = *tmp;
    if (strftime(buf, buflen, "%Y-%m-%dT%H:%M:%SZ", &tm) == 0U) {
        return -1;
    }
    return 0;
}

/*
 * Load tags for entry_id into *out_tags / *out_ntags (heap). Sorted by name.
 * On failure leaves *out_tags NULL / *out_ntags 0.
 */
static StoreStatus load_tags(sqlite3 *db, long long entry_id, char ***out_tags, size_t *out_ntags)
{
    sqlite3_stmt *stmt = NULL;
    char **tags = NULL;
    size_t n = 0U;
    size_t cap = 0U;
    int rc;

    *out_tags = NULL;
    *out_ntags = 0U;

    rc = sqlite3_prepare_v2(db,
                            "SELECT t.name FROM tags t "
                            "JOIN entry_tags et ON et.tag_id = t.id "
                            "WHERE et.entry_id = ?1 "
                            "ORDER BY t.name COLLATE BINARY;",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_int64(stmt, 1, entry_id);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 0);
        char *copy;
        char **grown;

        if (name == NULL) {
            continue;
        }
        copy = dup_str(name);
        if (copy == NULL) {
            size_t i;
            for (i = 0; i < n; i++) {
                free(tags[i]);
            }
            free((void *)tags);
            (void)sqlite3_finalize(stmt);
            return STORE_ERR_OOM;
        }
        if (n == cap) {
            size_t ncap = (cap == 0U) ? 4U : cap * 2U;
            grown = (char **)realloc((void *)tags, ncap * sizeof(*tags));
            if (grown == NULL) {
                free(copy);
                {
                    size_t i;
                    for (i = 0; i < n; i++) {
                        free(tags[i]);
                    }
                }
                free((void *)tags);
                (void)sqlite3_finalize(stmt);
                return STORE_ERR_OOM;
            }
            tags = grown;
            cap = ncap;
        }
        tags[n] = copy;
        n++;
    }
    (void)sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        size_t i;
        for (i = 0; i < n; i++) {
            free(tags[i]);
        }
        free((void *)tags);
        return STORE_ERR_SQLITE;
    }
    *out_tags = tags;
    *out_ntags = n;
    return STORE_OK;
}

/* Fill *out from a SELECT that returns columns:
 * id, key, body, source, created_at, updated_at (key may be NULL). */
static StoreStatus fill_entry_from_row(sqlite3 *db, sqlite3_stmt *stmt, Entry *out)
{
    StoreStatus st;
    const unsigned char *key_u;
    const unsigned char *body_u;
    const unsigned char *source_u;
    const unsigned char *created_u;
    const unsigned char *updated_u;

    memset(out, 0, sizeof(*out));
    out->id = sqlite3_column_int64(stmt, 0);
    key_u = sqlite3_column_text(stmt, 1);
    body_u = sqlite3_column_text(stmt, 2);
    source_u = sqlite3_column_text(stmt, 3);
    created_u = sqlite3_column_text(stmt, 4);
    updated_u = sqlite3_column_text(stmt, 5);

    if (body_u == NULL || source_u == NULL || created_u == NULL || updated_u == NULL) {
        return STORE_ERR_SQLITE;
    }
    if (key_u != NULL) {
        out->key = dup_str((const char *)key_u);
        if (out->key == NULL) {
            store_entry_free(out);
            return STORE_ERR_OOM;
        }
    }
    out->body = dup_str((const char *)body_u);
    out->source = dup_str((const char *)source_u);
    out->created_at = dup_str((const char *)created_u);
    out->updated_at = dup_str((const char *)updated_u);
    if (out->body == NULL || out->source == NULL || out->created_at == NULL ||
        out->updated_at == NULL) {
        store_entry_free(out);
        return STORE_ERR_OOM;
    }
    st = load_tags(db, out->id, &out->tags, &out->ntags);
    if (st != STORE_OK) {
        store_entry_free(out);
        return st;
    }
    return STORE_OK;
}

static StoreStatus load_entry_by_id(sqlite3 *db, long long id, Entry *out)
{
    sqlite3_stmt *stmt = NULL;
    int rc;
    StoreStatus st;

    rc = sqlite3_prepare_v2(db,
                            "SELECT id, key, body, source, created_at, updated_at "
                            "FROM entries WHERE id = ?1;",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_int64(stmt, 1, id);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_NOT_FOUND;
    }
    if (rc != SQLITE_ROW) {
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    st = fill_entry_from_row(db, stmt, out);
    (void)sqlite3_finalize(stmt);
    return st;
}

static StoreStatus load_entry_by_key(sqlite3 *db, const char *key, Entry *out)
{
    sqlite3_stmt *stmt = NULL;
    int rc;
    StoreStatus st;

    if (key == NULL) {
        return STORE_ERR_INTERNAL;
    }
    rc = sqlite3_prepare_v2(db,
                            "SELECT id, key, body, source, created_at, updated_at "
                            "FROM entries WHERE key = ?1;",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_NOT_FOUND;
    }
    if (rc != SQLITE_ROW) {
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    st = fill_entry_from_row(db, stmt, out);
    (void)sqlite3_finalize(stmt);
    return st;
}

/* Space-joined tag names for FTS (heap; caller frees). Empty tags → "". */
static char *join_tags_space(const char *const *tags, size_t ntags)
{
    size_t i;
    size_t total = 0U;
    char *buf;
    size_t pos = 0U;

    if (ntags == 0U || tags == NULL) {
        buf = malloc(1U);
        if (buf != NULL) {
            buf[0] = '\0';
        }
        return buf;
    }
    for (i = 0; i < ntags; i++) {
        if (tags[i] == NULL) {
            continue;
        }
        total += strlen(tags[i]) + 1U; /* + space or NUL */
    }
    if (total == 0U) {
        buf = malloc(1U);
        if (buf != NULL) {
            buf[0] = '\0';
        }
        return buf;
    }
    buf = malloc(total);
    if (buf == NULL) {
        return NULL;
    }
    for (i = 0; i < ntags; i++) {
        size_t len;
        if (tags[i] == NULL) {
            continue;
        }
        len = strlen(tags[i]);
        if (pos > 0U) {
            buf[pos++] = ' ';
        }
        memcpy(buf + pos, tags[i], len);
        pos += len;
    }
    buf[pos] = '\0';
    return buf;
}

/* Rewrite FTS row for entry_id from current entries + entry_tags. */
static StoreStatus fts_resync(sqlite3 *db, long long entry_id)
{
    sqlite3_stmt *sel = NULL;
    sqlite3_stmt *del = NULL;
    sqlite3_stmt *ins = NULL;
    char **tags = NULL;
    size_t ntags = 0U;
    char *tags_text = NULL;
    const unsigned char *body_u;
    StoreStatus st = STORE_ERR_SQLITE;
    int rc;

    rc = sqlite3_prepare_v2(db, "SELECT body FROM entries WHERE id = ?1;", -1, &sel, NULL);
    if (rc != SQLITE_OK) {
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_int64(sel, 1, entry_id);
    rc = sqlite3_step(sel);
    if (rc != SQLITE_ROW) {
        (void)sqlite3_finalize(sel);
        return (rc == SQLITE_DONE) ? STORE_ERR_NOT_FOUND : STORE_ERR_SQLITE;
    }
    body_u = sqlite3_column_text(sel, 0);
    if (body_u == NULL) {
        (void)sqlite3_finalize(sel);
        return STORE_ERR_SQLITE;
    }

    st = load_tags(db, entry_id, &tags, &ntags);
    if (st != STORE_OK) {
        (void)sqlite3_finalize(sel);
        return st;
    }
    tags_text = join_tags_space((const char *const *)tags, ntags);
    if (tags_text == NULL) {
        size_t i;
        for (i = 0; i < ntags; i++) {
            free(tags[i]);
        }
        free((void *)tags);
        (void)sqlite3_finalize(sel);
        return STORE_ERR_OOM;
    }

    rc = sqlite3_prepare_v2(db, "DELETE FROM entries_fts WHERE rowid = ?1;", -1, &del, NULL);
    if (rc != SQLITE_OK) {
        goto cleanup;
    }
    (void)sqlite3_bind_int64(del, 1, entry_id);
    if (sqlite3_step(del) != SQLITE_DONE) {
        goto cleanup;
    }

    rc = sqlite3_prepare_v2(db, "INSERT INTO entries_fts(rowid, body, tags) VALUES (?1, ?2, ?3);",
                            -1, &ins, NULL);
    if (rc != SQLITE_OK) {
        goto cleanup;
    }
    (void)sqlite3_bind_int64(ins, 1, entry_id);
    /* NOLINTNEXTLINE(performance-no-int-to-ptr) -- SQLITE_TRANSIENT is (destructor)-1 */
    (void)sqlite3_bind_text(ins, 2, (const char *)body_u, -1, SQLITE_TRANSIENT);
    /* NOLINTNEXTLINE(performance-no-int-to-ptr) -- SQLITE_TRANSIENT is (destructor)-1 */
    (void)sqlite3_bind_text(ins, 3, tags_text, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(ins) != SQLITE_DONE) {
        goto cleanup;
    }
    st = STORE_OK;

cleanup:
    if (ins != NULL) {
        (void)sqlite3_finalize(ins);
    }
    if (del != NULL) {
        (void)sqlite3_finalize(del);
    }
    (void)sqlite3_finalize(sel);
    {
        size_t i;
        for (i = 0; i < ntags; i++) {
            free(tags[i]);
        }
    }
    free((void *)tags);
    free(tags_text);
    return st;
}

/* Ensure tag name exists; return its id in *out_tag_id. */
static StoreStatus ensure_tag(sqlite3 *db, const char *name, long long *out_tag_id)
{
    sqlite3_stmt *ins = NULL;
    sqlite3_stmt *sel = NULL;
    int rc;

    rc = sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO tags(name) VALUES (?1);", -1, &ins, NULL);
    if (rc != SQLITE_OK) {
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_text(ins, 1, name, -1, SQLITE_STATIC);
    if (sqlite3_step(ins) != SQLITE_DONE) {
        (void)sqlite3_finalize(ins);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(ins);

    rc = sqlite3_prepare_v2(db, "SELECT id FROM tags WHERE name = ?1;", -1, &sel, NULL);
    if (rc != SQLITE_OK) {
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_text(sel, 1, name, -1, SQLITE_STATIC);
    rc = sqlite3_step(sel);
    if (rc != SQLITE_ROW) {
        (void)sqlite3_finalize(sel);
        return STORE_ERR_SQLITE;
    }
    *out_tag_id = sqlite3_column_int64(sel, 0);
    (void)sqlite3_finalize(sel);
    return STORE_OK;
}

static StoreStatus link_tag(sqlite3 *db, long long entry_id, long long tag_id)
{
    sqlite3_stmt *stmt = NULL;
    int rc;

    rc = sqlite3_prepare_v2(
        db, "INSERT OR IGNORE INTO entry_tags(entry_id, tag_id) VALUES (?1, ?2);", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_int64(stmt, 1, entry_id);
    (void)sqlite3_bind_int64(stmt, 2, tag_id);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(stmt);
    return STORE_OK;
}

static StoreStatus union_tags(sqlite3 *db, long long entry_id, const char *const *tags,
                              size_t ntags)
{
    size_t i;

    if (tags == NULL || ntags == 0U) {
        return STORE_OK;
    }
    for (i = 0; i < ntags; i++) {
        long long tag_id = 0;
        StoreStatus st;

        if (tags[i] == NULL) {
            continue;
        }
        st = ensure_tag(db, tags[i], &tag_id);
        if (st != STORE_OK) {
            return st;
        }
        st = link_tag(db, entry_id, tag_id);
        if (st != STORE_OK) {
            return st;
        }
    }
    return STORE_OK;
}

static StoreStatus find_keyless_by_hash(sqlite3 *db, const char *body_hash, long long *out_id)
{
    sqlite3_stmt *stmt = NULL;
    int rc;

    rc = sqlite3_prepare_v2(db, "SELECT id FROM entries WHERE body_hash = ?1 AND key IS NULL;", -1,
                            &stmt, NULL);
    if (rc != SQLITE_OK) {
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_text(stmt, 1, body_hash, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_NOT_FOUND;
    }
    if (rc != SQLITE_ROW) {
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    *out_id = sqlite3_column_int64(stmt, 0);
    (void)sqlite3_finalize(stmt);
    return STORE_OK;
}

static StoreStatus find_id_by_key(sqlite3 *db, const char *key, long long *out_id)
{
    sqlite3_stmt *stmt = NULL;
    int rc;

    rc = sqlite3_prepare_v2(db, "SELECT id FROM entries WHERE key = ?1;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_NOT_FOUND;
    }
    if (rc != SQLITE_ROW) {
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    *out_id = sqlite3_column_int64(stmt, 0);
    (void)sqlite3_finalize(stmt);
    return STORE_OK;
}

static StoreStatus insert_entry(sqlite3 *db, const char *body, const char *body_hash,
                                const char *key_or_null, const char *source, const char *now,
                                long long *out_id)
{
    sqlite3_stmt *stmt = NULL;
    int rc;

    rc = sqlite3_prepare_v2(db,
                            "INSERT INTO entries(key, body, body_hash, source, created_at, "
                            "updated_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6);",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return STORE_ERR_SQLITE;
    }
    if (key_or_null == NULL) {
        (void)sqlite3_bind_null(stmt, 1);
    } else {
        (void)sqlite3_bind_text(stmt, 1, key_or_null, -1, SQLITE_STATIC);
    }
    (void)sqlite3_bind_text(stmt, 2, body, -1, SQLITE_STATIC);
    (void)sqlite3_bind_text(stmt, 3, body_hash, -1, SQLITE_STATIC);
    (void)sqlite3_bind_text(stmt, 4, source, -1, SQLITE_STATIC);
    (void)sqlite3_bind_text(stmt, 5, now, -1, SQLITE_STATIC);
    (void)sqlite3_bind_text(stmt, 6, now, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    *out_id = sqlite3_last_insert_rowid(db);
    (void)sqlite3_finalize(stmt);
    return STORE_OK;
}

static StoreStatus touch_updated_at(sqlite3 *db, long long id, const char *now)
{
    sqlite3_stmt *stmt = NULL;
    int rc;

    rc = sqlite3_prepare_v2(db, "UPDATE entries SET updated_at = ?1 WHERE id = ?2;", -1, &stmt,
                            NULL);
    if (rc != SQLITE_OK) {
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_text(stmt, 1, now, -1, SQLITE_STATIC);
    (void)sqlite3_bind_int64(stmt, 2, id);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(stmt);
    return STORE_OK;
}

static StoreStatus replace_body(sqlite3 *db, long long id, const char *body, const char *body_hash,
                                const char *now)
{
    sqlite3_stmt *stmt = NULL;
    int rc;

    rc = sqlite3_prepare_v2(
        db, "UPDATE entries SET body = ?1, body_hash = ?2, updated_at = ?3 WHERE id = ?4;", -1,
        &stmt, NULL);
    if (rc != SQLITE_OK) {
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_text(stmt, 1, body, -1, SQLITE_STATIC);
    (void)sqlite3_bind_text(stmt, 2, body_hash, -1, SQLITE_STATIC);
    (void)sqlite3_bind_text(stmt, 3, now, -1, SQLITE_STATIC);
    (void)sqlite3_bind_int64(stmt, 4, id);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(stmt);
    return STORE_OK;
}

/* Keyed upsert: replace body + union tags, or insert. Sets out_id and out_action. */
static StoreStatus add_keyed(sqlite3 *db, const char *body, const char *body_hash, const char *key,
                             const char *const *tags, size_t ntags, const char *source,
                             const char *now, long long *out_id, StoreAddAction *out_action)
{
    StoreStatus st;
    long long id = 0;

    st = find_id_by_key(db, key, &id);
    if (st == STORE_OK) {
        st = replace_body(db, id, body, body_hash, now);
        if (st != STORE_OK) {
            return st;
        }
        st = union_tags(db, id, tags, ntags);
        if (st != STORE_OK) {
            return st;
        }
        *out_id = id;
        *out_action = STORE_ADD_UPDATED;
        return STORE_OK;
    }
    if (st != STORE_ERR_NOT_FOUND) {
        return st;
    }
    st = insert_entry(db, body, body_hash, key, source, now, &id);
    if (st != STORE_OK) {
        return st;
    }
    st = union_tags(db, id, tags, ntags);
    if (st != STORE_OK) {
        return st;
    }
    *out_id = id;
    *out_action = STORE_ADD_CREATED;
    return STORE_OK;
}

/* Keyless insert or body-hash merge. */
static StoreStatus add_keyless(sqlite3 *db, const char *body, const char *body_hash,
                               const char *const *tags, size_t ntags, const char *source,
                               const char *now, long long *out_id, StoreAddAction *out_action)
{
    StoreStatus st;
    long long id = 0;

    st = find_keyless_by_hash(db, body_hash, &id);
    if (st == STORE_OK) {
        st = touch_updated_at(db, id, now);
        if (st != STORE_OK) {
            return st;
        }
        st = union_tags(db, id, tags, ntags);
        if (st != STORE_OK) {
            return st;
        }
        *out_id = id;
        *out_action = STORE_ADD_MERGED;
        return STORE_OK;
    }
    if (st != STORE_ERR_NOT_FOUND) {
        return st;
    }
    st = insert_entry(db, body, body_hash, NULL, source, now, &id);
    if (st != STORE_OK) {
        return st;
    }
    st = union_tags(db, id, tags, ntags);
    if (st != STORE_OK) {
        return st;
    }
    *out_id = id;
    *out_action = STORE_ADD_CREATED;
    return STORE_OK;
}

StoreStatus store_add(Store *s, const char *body, const char *body_hash, const char *key_or_null,
                      const char *const *tags, size_t ntags, const char *source,
                      StoreAddAction *out_action, Entry *out_entry)
{
    char now[32];
    long long id = 0;
    StoreAddAction action = STORE_ADD_CREATED;
    StoreStatus st;
    char err_unused[1];

    if (s == NULL || s->db == NULL || body == NULL || body_hash == NULL || source == NULL ||
        out_action == NULL || out_entry == NULL) {
        return STORE_ERR_INTERNAL;
    }
    if (ntags > 0U && tags == NULL) {
        return STORE_ERR_INTERNAL;
    }
    if (utc_now(now, sizeof(now)) != 0) {
        return STORE_ERR_INTERNAL;
    }
    if (exec_sql(s->db, "BEGIN IMMEDIATE;", err_unused, 0U) != 0) {
        return STORE_ERR_SQLITE;
    }

    if (key_or_null != NULL) {
        st = add_keyed(s->db, body, body_hash, key_or_null, tags, ntags, source, now, &id, &action);
    } else {
        st = add_keyless(s->db, body, body_hash, tags, ntags, source, now, &id, &action);
    }
    if (st != STORE_OK) {
        goto fail;
    }

    st = fts_resync(s->db, id);
    if (st != STORE_OK) {
        goto fail;
    }
    if (exec_sql(s->db, "COMMIT;", err_unused, 0U) != 0) {
        st = STORE_ERR_SQLITE;
        goto fail;
    }

    /* The write is already durable past COMMIT; a failure here (e.g. OOM while
       snapshotting the row for output) reports error but does not lose data. */
    st = load_entry_by_id(s->db, id, out_entry);
    if (st != STORE_OK) {
        return st;
    }
    *out_action = action;
    return STORE_OK;

fail:
    rollback_quiet(s->db);
    return st;
}

StoreStatus store_get(Store *s, long long id, Entry *out_entry)
{
    if (s == NULL || s->db == NULL || out_entry == NULL) {
        return STORE_ERR_INTERNAL;
    }
    return load_entry_by_id(s->db, id, out_entry);
}

StoreStatus store_get_by_key(Store *s, const char *key, Entry *out_entry)
{
    if (s == NULL || s->db == NULL || key == NULL || out_entry == NULL) {
        return STORE_ERR_INTERNAL;
    }
    return load_entry_by_key(s->db, key, out_entry);
}

static void free_entry_rows(Entry *rows, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        store_entry_free(&rows[i]);
    }
    free(rows);
}

static StoreStatus list_append_row(sqlite3 *db, sqlite3_stmt *sel, Entry **rows, size_t *n,
                                   size_t *cap)
{
    Entry e;
    StoreStatus st = fill_entry_from_row(db, sel, &e);
    Entry *grown;

    if (st != STORE_OK) {
        return st;
    }
    if (*n == *cap) {
        size_t ncap = (*cap == 0U) ? 8U : (*cap * 2U);
        grown = (Entry *)realloc((void *)*rows, ncap * sizeof(*grown));
        if (grown == NULL) {
            store_entry_free(&e);
            return STORE_ERR_OOM;
        }
        *rows = grown;
        *cap = ncap;
    }
    (*rows)[*n] = e;
    (*n)++;
    return STORE_OK;
}

/* Append " AND col = ?N" and push bind; -1 on OOM/truncation/bind cap. */
static int list_append_eq(char *sql, size_t sql_cap, size_t *pos, int *nbinds,
                          const char **bind_text, size_t bind_cap, const char *col,
                          const char *value)
{
    int n;

    if (value == NULL) {
        return 0;
    }
    if ((size_t)*nbinds >= bind_cap) {
        return -1;
    }
    n = snprintf(sql + *pos, sql_cap - *pos, " AND e.%s = ?%d", col, *nbinds + 1);
    if (n < 0 || (size_t)n >= sql_cap - *pos) {
        return -1;
    }
    *pos += (size_t)n;
    bind_text[(*nbinds)++] = value;
    return 0;
}

static int list_append_tag_exists(char *sql, size_t sql_cap, size_t *pos, int *nbinds,
                                  const char **bind_text, size_t bind_cap, const char *tag)
{
    int n;

    if (tag == NULL) {
        return 0;
    }
    if ((size_t)*nbinds >= bind_cap) {
        return -1;
    }
    n = snprintf(sql + *pos, sql_cap - *pos,
                 " AND EXISTS (SELECT 1 FROM entry_tags et"
                 " JOIN tags tg ON tg.id = et.tag_id"
                 " WHERE et.entry_id = e.id AND tg.name = ?%d)",
                 *nbinds + 1);
    if (n < 0 || (size_t)n >= sql_cap - *pos) {
        return -1;
    }
    *pos += (size_t)n;
    bind_text[(*nbinds)++] = tag;
    return 0;
}

/*
 * Build list WHERE clause fragments and bind params.
 * Tags are AND'd via EXISTS subqueries (one per tag).
 * sql_out must be large enough (caller-sized); returns -1 if truncated.
 */
static int list_build_where(const ListQuery *q, char *sql, size_t sql_cap, int *out_nbinds,
                            const char **bind_text, size_t bind_cap)
{
    size_t pos = 0U;
    int nbinds = 0;
    size_t t;
    int n;

    if (q == NULL || sql == NULL || sql_cap == 0U || out_nbinds == NULL || bind_text == NULL) {
        return -1;
    }
    n = snprintf(sql, sql_cap, " WHERE 1=1");
    if (n < 0 || (size_t)n >= sql_cap) {
        return -1;
    }
    pos = (size_t)n;

    if (list_append_eq(sql, sql_cap, &pos, &nbinds, bind_text, bind_cap, "source", q->source) !=
            0 ||
        list_append_eq(sql, sql_cap, &pos, &nbinds, bind_text, bind_cap, "key", q->key) != 0) {
        return -1;
    }
    for (t = 0; t < q->ntags; t++) {
        const char *tag = (q->tags != NULL) ? q->tags[t] : NULL;
        if (list_append_tag_exists(sql, sql_cap, &pos, &nbinds, bind_text, bind_cap, tag) != 0) {
            return -1;
        }
    }
    *out_nbinds = nbinds;
    return 0;
}

static StoreStatus list_bind_texts(sqlite3_stmt *stmt, const char **bind_text, int nbinds)
{
    int i;
    for (i = 0; i < nbinds; i++) {
        if (sqlite3_bind_text(stmt, i + 1, bind_text[i], -1, SQLITE_STATIC) != SQLITE_OK) {
            return STORE_ERR_SQLITE;
        }
    }
    return STORE_OK;
}

/* Enough for source + key + many tag EXISTS clauses. */
#define LIST_SQL_CAP 8192
#define LIST_BIND_CAP 64

/*
 * Count + page. Split into two statements (COUNT then paged SELECT) rather than
 * one query: a single COUNT(*) OVER() would ride on the result rows, so it
 * reports no total whenever the page is empty (offset past the end), breaking the
 * "total is the unpaged count" contract. store_list runs this inside one read
 * transaction so both statements see a single snapshot.
 */
static StoreStatus list_query_exec(sqlite3 *db, const ListQuery *q, Entry **out_entries,
                                   size_t *out_count, size_t *out_total)
{
    char where_sql[LIST_SQL_CAP];
    char count_sql[LIST_SQL_CAP + 64];
    char select_sql[LIST_SQL_CAP + 160];
    const char *bind_text[LIST_BIND_CAP];
    int nbinds = 0;
    sqlite3_stmt *count_stmt = NULL;
    sqlite3_stmt *sel = NULL;
    Entry *rows = NULL;
    size_t n = 0U;
    size_t cap = 0U;
    size_t total = 0U;
    int rc;
    StoreStatus st;
    int lim_idx;
    int off_idx;

    *out_entries = NULL;
    *out_count = 0U;
    *out_total = 0U;

    if (list_build_where(q, where_sql, sizeof(where_sql), &nbinds, bind_text, LIST_BIND_CAP) != 0) {
        return STORE_ERR_INTERNAL;
    }

    {
        int sn =
            snprintf(count_sql, sizeof(count_sql), "SELECT COUNT(*) FROM entries e%s;", where_sql);
        if (sn < 0 || (size_t)sn >= sizeof(count_sql)) {
            return STORE_ERR_INTERNAL;
        }
        sn = snprintf(select_sql, sizeof(select_sql),
                      "SELECT e.id, e.key, e.body, e.source, e.created_at, e.updated_at "
                      "FROM entries e%s ORDER BY e.updated_at DESC, e.id DESC "
                      "LIMIT ?%d OFFSET ?%d;",
                      where_sql, nbinds + 1, nbinds + 2);
        if (sn < 0 || (size_t)sn >= sizeof(select_sql)) {
            return STORE_ERR_INTERNAL;
        }
    }
    lim_idx = nbinds + 1;
    off_idx = nbinds + 2;

    rc = sqlite3_prepare_v2(db, count_sql, -1, &count_stmt, NULL);
    if (rc != SQLITE_OK) {
        return STORE_ERR_SQLITE;
    }
    st = list_bind_texts(count_stmt, bind_text, nbinds);
    if (st != STORE_OK) {
        (void)sqlite3_finalize(count_stmt);
        return st;
    }
    rc = sqlite3_step(count_stmt);
    if (rc != SQLITE_ROW) {
        (void)sqlite3_finalize(count_stmt);
        return STORE_ERR_SQLITE;
    }
    total = (size_t)sqlite3_column_int64(count_stmt, 0);
    (void)sqlite3_finalize(count_stmt);
    count_stmt = NULL;

    rc = sqlite3_prepare_v2(db, select_sql, -1, &sel, NULL);
    if (rc != SQLITE_OK) {
        return STORE_ERR_SQLITE;
    }
    st = list_bind_texts(sel, bind_text, nbinds);
    if (st != STORE_OK) {
        (void)sqlite3_finalize(sel);
        return st;
    }
    if (sqlite3_bind_int64(sel, lim_idx, (sqlite3_int64)q->limit) != SQLITE_OK ||
        sqlite3_bind_int64(sel, off_idx, (sqlite3_int64)q->offset) != SQLITE_OK) {
        (void)sqlite3_finalize(sel);
        return STORE_ERR_SQLITE;
    }

    while ((rc = sqlite3_step(sel)) == SQLITE_ROW) {
        st = list_append_row(db, sel, &rows, &n, &cap);
        if (st != STORE_OK) {
            free_entry_rows(rows, n);
            (void)sqlite3_finalize(sel);
            return st;
        }
    }
    if (rc != SQLITE_DONE) {
        free_entry_rows(rows, n);
        (void)sqlite3_finalize(sel);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(sel);

    *out_entries = rows;
    *out_count = n;
    *out_total = total;
    return STORE_OK;
}

StoreStatus store_list(Store *s, const ListQuery *q, Entry **out_entries, size_t *out_count,
                       size_t *out_total)
{
    char err_unused[1];
    StoreStatus st;

    if (s == NULL || s->db == NULL || q == NULL || out_entries == NULL || out_count == NULL ||
        out_total == NULL) {
        return STORE_ERR_INTERNAL;
    }
    *out_entries = NULL;
    *out_count = 0U;
    *out_total = 0U;

    /* One read transaction: COUNT and the paged SELECT see the same snapshot, so
     *out_total and the page cannot disagree if a writer commits mid-list. */
    if (exec_sql(s->db, "BEGIN;", err_unused, 0U) != 0) {
        return STORE_ERR_SQLITE;
    }
    st = list_query_exec(s->db, q, out_entries, out_count, out_total);
    if (st != STORE_OK) {
        rollback_quiet(s->db);
        return st;
    }
    if (exec_sql(s->db, "COMMIT;", err_unused, 0U) != 0) {
        /* Unwind the page we built so the caller sees a clean failure. */
        rollback_quiet(s->db);
        free_entry_rows(*out_entries, *out_count);
        *out_entries = NULL;
        *out_count = 0U;
        *out_total = 0U;
        return STORE_ERR_SQLITE;
    }
    return STORE_OK;
}

/* Remove FTS row for entry_id (no-op if already gone). */
static StoreStatus fts_delete(sqlite3 *db, long long entry_id)
{
    sqlite3_stmt *del = NULL;
    int rc;

    rc = sqlite3_prepare_v2(db, "DELETE FROM entries_fts WHERE rowid = ?1;", -1, &del, NULL);
    if (rc != SQLITE_OK) {
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_int64(del, 1, entry_id);
    if (sqlite3_step(del) != SQLITE_DONE) {
        (void)sqlite3_finalize(del);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(del);
    return STORE_OK;
}

/* Drop tags with no remaining entry_tags rows (after CASCADE). */
static StoreStatus gc_orphan_tags(sqlite3 *db)
{
    char err_unused[1];

    if (exec_sql(db,
                 "DELETE FROM tags WHERE NOT EXISTS "
                 "(SELECT 1 FROM entry_tags et WHERE et.tag_id = tags.id);",
                 err_unused, 0U) != 0) {
        return STORE_ERR_SQLITE;
    }
    return STORE_OK;
}

/*
 * Hard-delete one entry under a single write lock: load snapshot, FTS delete,
 * DELETE entry (CASCADE entry_tags), orphan-tag GC, COMMIT. Load is inside the
 * transaction so the JSON echo matches the row that is removed.
 *
 * *out_deleted is zeroed on entry. On STORE_OK it holds a heap snapshot (caller
 * frees). On failure it is left zeroed / freed.
 */
static StoreStatus delete_entry_tx(Store *s, long long id_or_zero, const char *key_or_null,
                                   Entry *out_deleted)
{
    char err_unused[1];
    StoreStatus st;
    sqlite3_stmt *del = NULL;
    long long id = id_or_zero;
    int rc;

    memset(out_deleted, 0, sizeof(*out_deleted));

    if (exec_sql(s->db, "BEGIN IMMEDIATE;", err_unused, 0U) != 0) {
        return STORE_ERR_SQLITE;
    }

    if (key_or_null != NULL) {
        st = load_entry_by_key(s->db, key_or_null, out_deleted);
    } else {
        st = load_entry_by_id(s->db, id, out_deleted);
    }
    if (st != STORE_OK) {
        goto fail;
    }
    id = out_deleted->id;

    st = fts_delete(s->db, id);
    if (st != STORE_OK) {
        goto fail_free;
    }

    rc = sqlite3_prepare_v2(s->db, "DELETE FROM entries WHERE id = ?1;", -1, &del, NULL);
    if (rc != SQLITE_OK) {
        st = STORE_ERR_SQLITE;
        goto fail_free;
    }
    (void)sqlite3_bind_int64(del, 1, id);
    if (sqlite3_step(del) != SQLITE_DONE) {
        (void)sqlite3_finalize(del);
        st = STORE_ERR_SQLITE;
        goto fail_free;
    }
    (void)sqlite3_finalize(del);
    del = NULL;

    /* Should not happen under the write lock after a successful load. */
    if (sqlite3_changes(s->db) == 0) {
        st = STORE_ERR_NOT_FOUND;
        goto fail_free;
    }

    st = gc_orphan_tags(s->db);
    if (st != STORE_OK) {
        goto fail_free;
    }

    if (exec_sql(s->db, "COMMIT;", err_unused, 0U) != 0) {
        st = STORE_ERR_SQLITE;
        goto fail_free;
    }
    return STORE_OK;

fail_free:
    store_entry_free(out_deleted);
fail:
    rollback_quiet(s->db);
    return st;
}

StoreStatus store_delete_by_id(Store *s, long long id, Entry *out_deleted)
{
    if (s == NULL || s->db == NULL || out_deleted == NULL) {
        return STORE_ERR_INTERNAL;
    }
    return delete_entry_tx(s, id, NULL, out_deleted);
}

StoreStatus store_delete_by_key(Store *s, const char *key, Entry *out_deleted)
{
    if (s == NULL || s->db == NULL || key == NULL || out_deleted == NULL) {
        return STORE_ERR_INTERNAL;
    }
    return delete_entry_tx(s, 0, key, out_deleted);
}
