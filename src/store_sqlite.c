#include "store.h"

#include "normalize.h"
#include "search_terms.h"
#include "sqlite3.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* A prefix the v5 -> v6 step left out (store_open_warning). */
typedef struct {
    char *value;
    const char *reason;
} MigrationWarning;

struct Store {
    sqlite3 *db;
    StoreError last; /* last captured SQLite failure (zero when none) */
    char *path;
    char *device_id;
    MigrationWarning *warnings; /* this open's v6 step; nwarnings items */
    size_t nwarnings;
    bool replace_invalid_sidecar; /* device new only: invalid counts as missing */
    /* NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers) */
    char pad_[7]; /* explicit tail padding (kept -Wpadded-clean) */
};

/* Directory mode for the DB parent (rwx for owner only). */
enum { DB_DIR_MODE = 0700 };
/* ISO-8601 UTC timestamp helper: minimum buffer and the ".mmmZ" fraction suffix. */
enum { ISO_TS_MIN_BUFLEN = 25, ISO_FRAC_SUFFIX_LEN = 5 };
enum { NANOS_PER_MS = 1000000 };
/* Initial element capacity for the geometric-growth dynamic arrays below. */
enum { GROW_MIN_CAP = 8 };

/* A (wall-clock now, target expires_at) timestamp pair, named so the two
   const char * arguments cannot be transposed at a call site. */
typedef struct {
    const char *now;
    const char *expires_at;
} EntryTimes;

/* A SQL "col = ?" equality clause: the column name and its bound text value. */
typedef struct {
    const char *col;
    const char *value;
} SqlEq;

/* A neighbor-query spec: the subject row id and which direction(s) to include. */
typedef struct {
    long long subject_id;
    StoreNeighborDir dir;
    char pad_[4]; /* explicit tail padding (kept -Wpadded-clean) */
} NeighborQuery;

/* ---- optional fault injection (coverage / unit tests) --------------------
 * Deliberately mutable process-global counters: the store_test_fail_* setters are
 * the seam that drives OOM/SQLite-error paths from tests. Compiled only under
 * REMEMBER_TEST_HOOKS. */
#ifdef REMEMBER_TEST_HOOKS
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static int g_fail_alloc_after = -1;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static int g_fail_prepare_after = -1;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static int g_fail_step_after = -1;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static int g_fail_exec_after = -1;

void store_test_fail_alloc_after(int n)
{
    g_fail_alloc_after = n;
}

void store_test_fail_prepare_after(int n)
{
    g_fail_prepare_after = n;
}

void store_test_fail_step_after(int n)
{
    g_fail_step_after = n;
}

void store_test_fail_exec_after(int n)
{
    g_fail_exec_after = n;
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static int g_fail_exec_schema_after = -1;

void store_test_fail_exec_schema_after(int n)
{
    g_fail_exec_schema_after = n;
}

/* Next sidecar rename fails once, after the temp file is written. */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static int g_fail_sidecar_rename = 0;

void store_test_fail_sidecar_rename(int n)
{
    g_fail_sidecar_rename = n;
}

/* Capture libc allocators before macros redirect malloc/calloc/realloc. */
static void *real_malloc(size_t n)
{
    return malloc(n);
}
static void *real_calloc(size_t nm, size_t sz)
{
    return calloc(nm, sz);
}
static void *real_realloc(void *p, size_t n)
{
    return realloc(p, n);
}

static void *hook_malloc(size_t n)
{
    if (g_fail_alloc_after >= 0) {
        if (g_fail_alloc_after == 0) {
            g_fail_alloc_after = -1;
            return NULL;
        }
        g_fail_alloc_after--;
    }
    return real_malloc(n);
}

static void *hook_calloc(size_t nm, size_t sz)
{
    if (g_fail_alloc_after >= 0) {
        if (g_fail_alloc_after == 0) {
            g_fail_alloc_after = -1;
            return NULL;
        }
        g_fail_alloc_after--;
    }
    return real_calloc(nm, sz);
}

static void *hook_realloc(void *p, size_t n)
{
    if (g_fail_alloc_after >= 0) {
        if (g_fail_alloc_after == 0) {
            g_fail_alloc_after = -1;
            return NULL;
        }
        g_fail_alloc_after--;
    }
    return real_realloc(p, n);
}

#define malloc(n) hook_malloc(n)
#define calloc(a, b) hook_calloc((a), (b))
#define realloc(p, n) hook_realloc((p), (n))

static int real_prepare(sqlite3 *db, const char *sql, int nByte, sqlite3_stmt **ppStmt,
                        const char **pzTail)
{
    return sqlite3_prepare_v2(db, sql, nByte, ppStmt, pzTail);
}

static int store_prepare(sqlite3 *db, const char *sql, int nByte, sqlite3_stmt **ppStmt,
                         const char **pzTail)
{
    if (g_fail_prepare_after >= 0) {
        if (g_fail_prepare_after == 0) {
            g_fail_prepare_after = -1;
            return SQLITE_NOMEM;
        }
        g_fail_prepare_after--;
    }
    return real_prepare(db, sql, nByte, ppStmt, pzTail);
}
#define sqlite3_prepare_v2(db, sql, n, pp, pt) store_prepare((db), (sql), (n), (pp), (pt))

static int real_step(sqlite3_stmt *stmt)
{
    return sqlite3_step(stmt);
}

static int store_step(sqlite3_stmt *stmt)
{
    if (g_fail_step_after >= 0) {
        if (g_fail_step_after == 0) {
            g_fail_step_after = -1;
            return SQLITE_ERROR;
        }
        g_fail_step_after--;
    }
    return real_step(stmt);
}
#define sqlite3_step(stmt) store_step(stmt)

static int real_exec(sqlite3 *db, const char *sql, int (*cb)(void *, int, char **, char **),
                     void *arg, char **errmsg)
{
    return sqlite3_exec(db, sql, cb, arg, errmsg);
}

static int store_exec(sqlite3 *db, const char *sql, int (*cb)(void *, int, char **, char **),
                      void *arg, char **errmsg)
{
    if (g_fail_exec_after >= 0) {
        if (g_fail_exec_after == 0) {
            g_fail_exec_after = -1;
            return SQLITE_ERROR;
        }
        g_fail_exec_after--;
    }
    if (g_fail_exec_schema_after >= 0) {
        if (g_fail_exec_schema_after == 0) {
            g_fail_exec_schema_after = -1;
            return SQLITE_SCHEMA;
        }
        g_fail_exec_schema_after--;
    }
    return real_exec(db, sql, cb, arg, errmsg);
}
#define sqlite3_exec(db, sql, cb, arg, errmsg) store_exec((db), (sql), (cb), (arg), (errmsg))
#endif /* REMEMBER_TEST_HOOKS */

/* The v5 shape. Every brain climbs to it and then takes the one v6 step, a
   fresh one included, so a migrated brain is byte-identical to a fresh one.
   The v1/2/3 rebuild (rebuild_entries_notnull) reuses these columns: ALTER ADD
   COLUMN cannot add NOT NULL, hence the rebuild. */
#define K_ENTRIES_V5_COLUMNS                                                                       \
    "  id INTEGER PRIMARY KEY,\n"                                                                  \
    "  key TEXT,\n"                                                                                \
    "  body TEXT NOT NULL,\n"                                                                      \
    "  body_hash TEXT NOT NULL,\n"                                                                 \
    "  source TEXT NOT NULL,\n"                                                                    \
    "  created_at TEXT NOT NULL,\n"                                                                \
    "  updated_at TEXT NOT NULL,\n"                                                                \
    "  expires_at TEXT,\n"                                                                         \
    "  sync_id TEXT NOT NULL,\n"                                                                   \
    "  deleted_at TEXT,\n"                                                                         \
    "  version_vector TEXT NOT NULL\n"
#define K_ENTRIES_V5_INDEXES                                                                       \
    "CREATE UNIQUE INDEX ux_entries_key ON entries(key) WHERE key IS NOT NULL;\n"                  \
    "CREATE UNIQUE INDEX ux_entries_bodyhash ON entries(body_hash) WHERE key IS NULL;\n"           \
    "CREATE UNIQUE INDEX ux_entries_sync_id ON entries(sync_id);\n"

/* FTS column order is key, body, tags (E1.3). Fresh create and v4→v5 share this. */
#define K_FTS_SQL                                                                                  \
    "CREATE VIRTUAL TABLE entries_fts USING fts5(\n"                                               \
    "  key,\n"                                                                                     \
    "  body,\n"                                                                                    \
    "  tags,\n"                                                                                    \
    "  tokenize = 'unicode61 remove_diacritics 2'\n"                                               \
    ");\n"
#define K_TAGS_SQL                                                                                 \
    "CREATE TABLE tags (\n"                                                                        \
    "  id INTEGER PRIMARY KEY,\n"                                                                  \
    "  name TEXT NOT NULL UNIQUE\n"                                                                \
    ");\n"
#define K_ENTRY_TAGS_V5_SQL                                                                        \
    "CREATE TABLE entry_tags (\n"                                                                  \
    "  entry_id INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE,\n"                      \
    "  tag_id INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,\n"                           \
    "  PRIMARY KEY (entry_id, tag_id)\n"                                                           \
    ");\n"
#define K_CONFLICTS_COLUMNS                                                                        \
    "  id            INTEGER PRIMARY KEY,\n"                                                       \
    "  sync_id       TEXT NOT NULL,\n"                                                             \
    "  reason        TEXT NOT NULL,\n"                                                             \
    "  local_json    TEXT NOT NULL,\n"                                                             \
    "  incoming_json TEXT NOT NULL,\n"                                                             \
    "  created_at    TEXT NOT NULL,\n"                                                             \
    "  origin        TEXT\n"
#define K_CONFLICTS_SQL "CREATE TABLE conflicts (\n" K_CONFLICTS_COLUMNS ");\n"
#define K_BUCKET_PREFIXES_SQL                                                                      \
    "CREATE TABLE bucket_prefixes (\n"                                                             \
    "  prefix TEXT PRIMARY KEY,\n"                                                                 \
    "  added_at TEXT NOT NULL,\n"                                                                  \
    "  removed_at TEXT,\n"                                                                         \
    "  device_id TEXT NOT NULL\n"                                                                  \
    ");\n"
#define K_SETTINGS_SQL                                                                             \
    "CREATE TABLE settings (\n"                                                                    \
    "  name TEXT PRIMARY KEY,\n"                                                                   \
    "  value TEXT NOT NULL,\n"                                                                     \
    "  updated_at TEXT NOT NULL\n"                                                                 \
    ");\n"

/* DDL only; version bump is applied after a successful body (same transaction). */
static const char k_schema_sql[] =
    "CREATE TABLE entries (\n" K_ENTRIES_V5_COLUMNS
    ");\n" K_ENTRIES_V5_INDEXES K_TAGS_SQL K_ENTRY_TAGS_V5_SQL K_FTS_SQL;

/* Applied on v0 create (after k_schema_sql) and on v1/v2 migrate. */
static const char k_links_sql[] =
    "CREATE TABLE entry_links (\n"
    "  from_id    INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE,\n"
    "  to_id      INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE,\n"
    "  kind       TEXT NOT NULL CHECK (kind IN ('related', 'supersedes', 'cites')),\n"
    "  created_at TEXT NOT NULL,\n"
    "  updated_at TEXT NOT NULL,\n"
    "  CHECK (from_id != to_id)\n"
    ");\n"
    "CREATE UNIQUE INDEX entry_links_edge\n"
    "  ON entry_links(from_id, to_id, kind);\n"
    "CREATE INDEX entry_links_to ON entry_links(to_id);\n";

static const char k_sync_sql[] = K_CONFLICTS_SQL K_BUCKET_PREFIXES_SQL K_SETTINGS_SQL;

/* ---- schema v6 (007 §10, 008 §8, 010 §17) ---------------------------------
 * The integer foreign keys (entry_tags, entry_links, entries_fts.rowid) stay on
 * local_id, the old integer id. id is the old sync_id. */
#define K_ENTRIES_V6_SQL                                                                           \
    "CREATE TABLE entries (\n"                                                                     \
    "  local_id INTEGER PRIMARY KEY,\n"                                                            \
    "  id TEXT NOT NULL UNIQUE,\n"                                                                 \
    "  key TEXT,\n"                                                                                \
    "  body TEXT NOT NULL,\n"                                                                      \
    "  body_hash TEXT NOT NULL,\n"                                                                 \
    "  created_by TEXT NOT NULL,\n"                                                                \
    "  updated_by TEXT NOT NULL,\n"                                                                \
    "  created_at TEXT NOT NULL,\n"                                                                \
    "  updated_at TEXT NOT NULL,\n"                                                                \
    "  expires_at TEXT,\n"                                                                         \
    "  deleted_at TEXT,\n"                                                                         \
    "  version_vector TEXT NOT NULL,\n"                                                            \
    "  change_seq INTEGER NOT NULL\n"                                                              \
    ");\n"
#define K_ENTRIES_V6_INDEXES                                                                       \
    "CREATE UNIQUE INDEX ux_entries_key ON entries(key) WHERE key IS NOT NULL;\n"                  \
    "CREATE UNIQUE INDEX ux_entries_bodyhash ON entries(body_hash) WHERE key IS NULL;\n"
#define K_ENTRY_TAGS_V6_SQL                                                                        \
    "CREATE TABLE entry_tags (\n"                                                                  \
    "  entry_id INTEGER NOT NULL REFERENCES entries(local_id) ON DELETE CASCADE,\n"                \
    "  tag_id INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,\n"                           \
    "  PRIMARY KEY (entry_id, tag_id)\n"                                                           \
    ");\n"
#define K_LINKS_V6_SQL                                                                             \
    "CREATE TABLE entry_links (\n"                                                                 \
    "  id             TEXT NOT NULL UNIQUE,\n"                                                     \
    "  from_id        INTEGER NOT NULL REFERENCES entries(local_id) ON DELETE CASCADE,\n"          \
    "  to_id          INTEGER NOT NULL REFERENCES entries(local_id) ON DELETE CASCADE,\n"          \
    "  kind           TEXT NOT NULL CHECK (kind IN ('related', 'supersedes', 'cites')),\n"         \
    "  created_at     TEXT NOT NULL,\n"                                                            \
    "  updated_at     TEXT NOT NULL,\n"                                                            \
    "  version_vector TEXT NOT NULL,\n"                                                            \
    "  deleted_at     TEXT,\n"                                                                     \
    "  change_seq     INTEGER NOT NULL,\n"                                                         \
    "  CHECK (from_id != to_id)\n"                                                                 \
    ");\n"
#define K_LINKS_V6_INDEXES                                                                         \
    "CREATE UNIQUE INDEX entry_links_edge\n"                                                       \
    "  ON entry_links(from_id, to_id, kind);\n"                                                    \
    "CREATE INDEX entry_links_to ON entry_links(to_id);\n"

/* The one source -> actor map (008 §8 step 1): kind copies source, unknown
   becomes human, id is null. The migration and every new write use it.
   operand is a column or a bound parameter. */
#define K_ACTOR_FROM_SOURCE(operand)                                                               \
    "json_object('kind', CASE " operand " WHEN 'unknown' THEN 'human' ELSE " operand               \
    " END, 'id', NULL)"
/* Entry.source until plan 21 drops it: the creating actor's kind. */
#define K_SOURCE_COL "created_by ->> '$.kind'"
#define K_SOURCE_OF_E "e." K_SOURCE_COL
/* The entry SELECT that fill_entry_from_row reads, bare and under alias e. */
#define K_ENTRY_COLS                                                                               \
    "local_id, key, body, " K_SOURCE_COL ", created_at, updated_at, expires_at, id, deleted_at, "  \
    "version_vector"
#define K_ENTRY_COLS_E                                                                             \
    "e.local_id, e.key, e.body, " K_SOURCE_OF_E ", e.created_at, e.updated_at, e.expires_at, "     \
    "e.id, e.deleted_at, e.version_vector"

/* 008 §8 steps 1 to 4. Rename the old tables away, then CREATE with the v6 DDL,
   so sqlite_master holds the plain text. A link id is a placeholder here; the
   caller mints one UUID v7 per link before anything reads it. */
static const char k_v6_rebuild_sql[] =
    "ALTER TABLE entry_links RENAME TO entry_links_v5;\n"
    "ALTER TABLE entry_tags RENAME TO entry_tags_v5;\n"
    "ALTER TABLE entries RENAME TO entries_v5;\n" K_ENTRIES_V6_SQL
    "INSERT INTO entries(local_id, id, key, body, body_hash, created_by, updated_by, created_at, "
    "updated_at, expires_at, deleted_at, version_vector, change_seq) "
    "SELECT id, sync_id, key, body, body_hash, " K_ACTOR_FROM_SOURCE(
        "source") ", " K_ACTOR_FROM_SOURCE("source") ", created_at, updated_at, expires_at, "
                                                     "deleted_at, version_vector, 0 "
                                                     "FROM entries_v5;\n" K_ENTRY_TAGS_V6_SQL
                                                     "INSERT INTO entry_tags(entry_id, tag_id) "
                                                     "SELECT entry_id, tag_id FROM "
                                                     "entry_tags_v5;\n" K_LINKS_V6_SQL
                                                     "INSERT INTO entry_links(rowid, id, from_id, "
                                                     "to_id, kind, created_at, updated_at, "
                                                     "version_vector, deleted_at, change_seq) "
                                                     "SELECT rowid, 'v5:' || rowid, from_id, "
                                                     "to_id, kind, "
                                                     "created_at, updated_at, '{}', NULL, 0 FROM "
                                                     "entry_links_v5;\n"
                                                     "DROP TABLE entry_links_v5;\n"
                                                     "DROP TABLE entry_tags_v5;\n"
                                                     "DROP TABLE "
                                                     "entries_v5;"
                                                     "\n" K_ENTRIES_V6_INDEXES K_LINKS_V6_INDEXES
                                                     "CREATE TABLE brain_meta (\n"
                                                     "  brain_id           TEXT NOT NULL,\n"
                                                     "  change_seq         INTEGER NOT NULL,\n"
                                                     "  min_reader_version TEXT NOT NULL\n"
                                                     ");\n"
                                                     "CREATE TABLE tombstones (\n"
                                                     "  id         TEXT PRIMARY KEY,\n"
                                                     "  change_seq INTEGER NOT NULL,\n"
                                                     "  purged_at  TEXT NOT NULL\n"
                                                     ");\n";

/* 007 §10 settings step 1: the ST-4.1 tables under temporary names. */
static const char k_v6_settings_tables_sql[] = "CREATE TABLE settings_v6 (\n"
                                               "  key            TEXT PRIMARY KEY,\n"
                                               "  value          TEXT NOT NULL,\n"
                                               "  updated_by     TEXT NOT NULL,\n"
                                               "  updated_at     TEXT NOT NULL,\n"
                                               "  device_id      TEXT NOT NULL,\n"
                                               "  version_vector TEXT NOT NULL,\n"
                                               "  deleted_at     TEXT,\n"
                                               "  change_seq     INTEGER NOT NULL\n"
                                               ");\n"
                                               "CREATE TABLE settings_history_v6 (\n"
                                               "  key TEXT NOT NULL, old_value TEXT, new_value "
                                               "TEXT, updated_by TEXT NOT NULL, at TEXT NOT NULL\n"
                                               ");\n";

/* 007 §10 settings steps 3 and 4. */
static const char k_v6_settings_swap_sql[] = "DROP TABLE settings;\n"
                                             "DROP TABLE bucket_prefixes;\n"
                                             "ALTER TABLE settings_v6 RENAME TO settings;\n"
                                             "ALTER TABLE settings_history_v6 RENAME TO "
                                             "settings_history;\n";

/* 010 §17 steps 1 and 2. Step 3 binds the migration's now. */
static const char k_v6_outbox_sql[] =
    "CREATE TABLE outbox(event_id TEXT PRIMARY KEY, type TEXT NOT NULL, payload TEXT NOT NULL, "
    "recipients TEXT NOT NULL, created_at TEXT NOT NULL);\n"
    "ALTER TABLE entries ADD COLUMN expired_at TEXT;\n";

/* 006 E12.7 without the 8-item cap (007 §10 step 2, D-01): the live order
   items first, in order, then the other live prefixes by added_at, then
   prefix. An order value that is not a JSON array counts as no order. */
static const char k_e12_effective_sql[] =
    "WITH live AS (SELECT prefix, added_at FROM bucket_prefixes "
    "WHERE removed_at IS NULL OR added_at >= removed_at), "
    "ord AS (SELECT j.value AS prefix, MIN(j.key) AS pos FROM settings s, "
    "json_each(CASE WHEN json_valid(s.value) AND json_type(s.value) = 'array' THEN s.value "
    "ELSE '[]' END) j WHERE s.name = 'bucket_prefix_order' AND j.type = 'text' "
    "GROUP BY j.value) "
    "SELECT l.prefix FROM live l LEFT JOIN ord o ON o.prefix = l.prefix "
    "ORDER BY o.pos IS NULL, o.pos, l.added_at, l.prefix;";

enum { SCHEMA_VERSION = 6, UUID_BYTES = 16, UUID_STR_LEN = STORE_DEVICE_ID_LEN };
/* The v6 step stores this in brain_meta (007 ST-10.2, G-20). */
static const char k_min_reader_version[] = "0.3.0";
/* import --from-db accepts 4 and 5 (a v6 source is plan 23). Above that, the
   open message. */
enum { IMPORT_MIN_USER_VERSION = 4, IMPORT_MAX_USER_VERSION = 5 };
static const char k_newer_database[] = "database is newer than this remember";
/* RFC 4122 layout for canonical 8-4-4-4-12 and v7 mint. */
enum {
    UUID_VER_BYTE = 6,
    UUID_VAR_BYTE = 8,
    UUID_HEX_DASH_0 = 4,
    UUID_HEX_DASH_1 = 6,
    UUID_HEX_DASH_2 = 8,
    UUID_HEX_DASH_3 = 10,
    UUID_NIBBLE_SHIFT = 4U,
    UUID_NIBBLE_MASK = 0x0fU,
    UUID_V7_HI = 0x70U,
    UUID_VAR_MASK = 0x3fU,
    UUID_VAR_RFC = 0x80U,
    MS_PER_SEC = 1000ULL,
    UUID_MS_SHIFT_0 = 40U,
    UUID_MS_SHIFT_1 = 32U,
    UUID_MS_SHIFT_2 = 24U,
    UUID_MS_SHIFT_3 = 16U,
    UUID_MS_SHIFT_4 = 8U,
    SIDECAR_LINE_BUF = 64,
    SIDECAR_FILE_MODE = 0600,
    SIDECAR_PATH_PAD = 16,
    SIDECAR_TMP_EXTRA = 4, /* ".tmp" */
    VV_JSON_BUFLEN = 80,
    VV_OUT_MAX = 4096,   /* import may merge many foreign device keys */
    VV_JSON_MIN_OUT = 8, /* shortest compact object we emit: {"d":1} */
    VV_INT_BASE = 10,
    VV_NEEDLE_LEN = UUID_STR_LEN + 4 /* "\"<uuid>\":" */
};

static void set_err(char *err, size_t errlen, const char *msg)
{
    size_t n = 0;

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

/* ---- structured SQLite errors (plan 16 stage 3, E4.7) ----------------------
 * One capture helper reads extended code, message, and errno at the point of
 * failure — before any finalize/rollback can clobber them. The class table is
 * first-match-wins in E4.7 order with D6 (NOMEM, IOERR_NOMEM → database)
 * before the IOERR row. Anything the table misses falls back to its primary
 * code (low byte), so every IOERR_* is io_error; anything else is database. */

/* Primary result code: the low byte of an extended code. */
enum { SQLITE_PRIMARY_CODE_MASK = 0xFF };

/* Only the codes whose class differs from their primary code's row; every
   other code (all BUSY_*, LOCKED_*, READONLY_*, CANTOPEN_*, CORRUPT_*, IOERR_*)
   lands on its primary in classify_extended. */
static const struct {
    int extended;
    StoreErrorClass error_class;
} k_error_classes[] = {
    /* D6: allocation failures are database, never io_error. */
    {SQLITE_NOMEM, STORE_ERROR_DATABASE},
    {SQLITE_IOERR_NOMEM, STORE_ERROR_DATABASE},
    /* A moved read-only database cannot be reopened: cannot_open, not read_only. */
    {SQLITE_READONLY_DBMOVED, STORE_ERROR_CANNOT_OPEN},
    /* A short read means the file ends mid-page: damage, not io_error. */
    {SQLITE_IOERR_SHORT_READ, STORE_ERROR_CORRUPT},
};

static StoreErrorClass classify_extended(int extended)
{
    size_t i = 0U;

    for (i = 0U; i < sizeof(k_error_classes) / sizeof(k_error_classes[0]); i++) {
        if (k_error_classes[i].extended == extended) {
            return k_error_classes[i].error_class;
        }
    }
    /* Primary code: every E4.7 row except the exceptions above. */
    // NOLINTNEXTLINE(hicpp-signed-bitwise)
    switch (extended & SQLITE_PRIMARY_CODE_MASK) {
    case SQLITE_BUSY:
    case SQLITE_LOCKED:
    case SQLITE_PROTOCOL:
        return STORE_ERROR_BUSY;
    case SQLITE_READONLY:
        return STORE_ERROR_READ_ONLY;
    case SQLITE_FULL:
        return STORE_ERROR_DISK_FULL;
    case SQLITE_CANTOPEN:
    case SQLITE_PERM:
        return STORE_ERROR_CANNOT_OPEN;
    case SQLITE_NOTADB:
        return STORE_ERROR_NOT_A_DATABASE;
    case SQLITE_CORRUPT:
        return STORE_ERROR_CORRUPT;
    case SQLITE_IOERR:
    case SQLITE_NOLFS:
        return STORE_ERROR_IO_ERROR;
    default:
        /* SCHEMA on exec is retried (concurrent migration); a prepare/step
           that still sees SCHEMA, NOMEM, MISUSE, and everything the E4.7
           table does not name report database. */
        return STORE_ERROR_DATABASE;
    }
}

/* Extended names E4.8 prints: every code the E4.7 table names, the primaries,
   and the common database fallbacks. Anything else resolves via its primary
   code, then to SQLITE_ERROR. The name sits beside its code, so they cannot
   drift. */
static const struct {
    const char *name;
    int code;
    /* NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers) */
    char pad_[4]; /* explicit tail padding (kept -Wpadded-clean) */
} k_sqlite_names[] = {
    {"SQLITE_OK", SQLITE_OK, {0}},
    {"SQLITE_ERROR", SQLITE_ERROR, {0}},
    {"SQLITE_INTERNAL", SQLITE_INTERNAL, {0}},
    {"SQLITE_PERM", SQLITE_PERM, {0}},
    {"SQLITE_ABORT", SQLITE_ABORT, {0}},
    {"SQLITE_BUSY", SQLITE_BUSY, {0}},
    {"SQLITE_LOCKED", SQLITE_LOCKED, {0}},
    {"SQLITE_NOMEM", SQLITE_NOMEM, {0}},
    {"SQLITE_READONLY", SQLITE_READONLY, {0}},
    {"SQLITE_INTERRUPT", SQLITE_INTERRUPT, {0}},
    {"SQLITE_IOERR", SQLITE_IOERR, {0}},
    {"SQLITE_CORRUPT", SQLITE_CORRUPT, {0}},
    {"SQLITE_NOTFOUND", SQLITE_NOTFOUND, {0}},
    {"SQLITE_FULL", SQLITE_FULL, {0}},
    {"SQLITE_CANTOPEN", SQLITE_CANTOPEN, {0}},
    {"SQLITE_PROTOCOL", SQLITE_PROTOCOL, {0}},
    {"SQLITE_EMPTY", SQLITE_EMPTY, {0}},
    {"SQLITE_SCHEMA", SQLITE_SCHEMA, {0}},
    {"SQLITE_TOOBIG", SQLITE_TOOBIG, {0}},
    {"SQLITE_CONSTRAINT", SQLITE_CONSTRAINT, {0}},
    {"SQLITE_MISMATCH", SQLITE_MISMATCH, {0}},
    {"SQLITE_MISUSE", SQLITE_MISUSE, {0}},
    {"SQLITE_NOLFS", SQLITE_NOLFS, {0}},
    {"SQLITE_AUTH", SQLITE_AUTH, {0}},
    {"SQLITE_FORMAT", SQLITE_FORMAT, {0}},
    {"SQLITE_RANGE", SQLITE_RANGE, {0}},
    {"SQLITE_NOTADB", SQLITE_NOTADB, {0}},
    {"SQLITE_BUSY_RECOVERY", SQLITE_BUSY_RECOVERY, {0}},
    {"SQLITE_BUSY_SNAPSHOT", SQLITE_BUSY_SNAPSHOT, {0}},
    {"SQLITE_BUSY_TIMEOUT", SQLITE_BUSY_TIMEOUT, {0}},
    {"SQLITE_LOCKED_SHAREDCACHE", SQLITE_LOCKED_SHAREDCACHE, {0}},
    {"SQLITE_LOCKED_VTAB", SQLITE_LOCKED_VTAB, {0}},
    {"SQLITE_READONLY_RECOVERY", SQLITE_READONLY_RECOVERY, {0}},
    {"SQLITE_READONLY_CANTLOCK", SQLITE_READONLY_CANTLOCK, {0}},
    {"SQLITE_READONLY_ROLLBACK", SQLITE_READONLY_ROLLBACK, {0}},
    {"SQLITE_READONLY_DBMOVED", SQLITE_READONLY_DBMOVED, {0}},
    {"SQLITE_READONLY_CANTINIT", SQLITE_READONLY_CANTINIT, {0}},
    {"SQLITE_READONLY_DIRECTORY", SQLITE_READONLY_DIRECTORY, {0}},
    {"SQLITE_CANTOPEN_NOTEMPDIR", SQLITE_CANTOPEN_NOTEMPDIR, {0}},
    {"SQLITE_CANTOPEN_ISDIR", SQLITE_CANTOPEN_ISDIR, {0}},
    {"SQLITE_CANTOPEN_FULLPATH", SQLITE_CANTOPEN_FULLPATH, {0}},
    {"SQLITE_CANTOPEN_CONVPATH", SQLITE_CANTOPEN_CONVPATH, {0}},
    {"SQLITE_CANTOPEN_DIRTYWAL", SQLITE_CANTOPEN_DIRTYWAL, {0}},
    {"SQLITE_CANTOPEN_SYMLINK", SQLITE_CANTOPEN_SYMLINK, {0}},
    {"SQLITE_CORRUPT_VTAB", SQLITE_CORRUPT_VTAB, {0}},
    {"SQLITE_CORRUPT_SEQUENCE", SQLITE_CORRUPT_SEQUENCE, {0}},
    {"SQLITE_CORRUPT_INDEX", SQLITE_CORRUPT_INDEX, {0}},
    {"SQLITE_IOERR_READ", SQLITE_IOERR_READ, {0}},
    {"SQLITE_IOERR_SHORT_READ", SQLITE_IOERR_SHORT_READ, {0}},
    {"SQLITE_IOERR_WRITE", SQLITE_IOERR_WRITE, {0}},
    {"SQLITE_IOERR_FSYNC", SQLITE_IOERR_FSYNC, {0}},
    {"SQLITE_IOERR_TRUNCATE", SQLITE_IOERR_TRUNCATE, {0}},
    {"SQLITE_IOERR_FSTAT", SQLITE_IOERR_FSTAT, {0}},
    {"SQLITE_IOERR_UNLOCK", SQLITE_IOERR_UNLOCK, {0}},
    {"SQLITE_IOERR_RDLOCK", SQLITE_IOERR_RDLOCK, {0}},
    {"SQLITE_IOERR_DELETE", SQLITE_IOERR_DELETE, {0}},
    {"SQLITE_IOERR_BLOCKED", SQLITE_IOERR_BLOCKED, {0}},
    {"SQLITE_IOERR_NOMEM", SQLITE_IOERR_NOMEM, {0}},
    {"SQLITE_IOERR_ACCESS", SQLITE_IOERR_ACCESS, {0}},
    {"SQLITE_IOERR_CHECKRESERVEDLOCK", SQLITE_IOERR_CHECKRESERVEDLOCK, {0}},
    {"SQLITE_IOERR_LOCK", SQLITE_IOERR_LOCK, {0}},
    {"SQLITE_IOERR_CLOSE", SQLITE_IOERR_CLOSE, {0}},
    {"SQLITE_IOERR_SHMOPEN", SQLITE_IOERR_SHMOPEN, {0}},
    {"SQLITE_IOERR_SHMSIZE", SQLITE_IOERR_SHMSIZE, {0}},
    {"SQLITE_IOERR_SHMLOCK", SQLITE_IOERR_SHMLOCK, {0}},
    {"SQLITE_IOERR_SHMMAP", SQLITE_IOERR_SHMMAP, {0}},
    {"SQLITE_IOERR_SEEK", SQLITE_IOERR_SEEK, {0}},
    {"SQLITE_IOERR_MMAP", SQLITE_IOERR_MMAP, {0}},
    {"SQLITE_IOERR_GETTEMPPATH", SQLITE_IOERR_GETTEMPPATH, {0}},
    {"SQLITE_IOERR_CONVPATH", SQLITE_IOERR_CONVPATH, {0}},
    {"SQLITE_IOERR_VNODE", SQLITE_IOERR_VNODE, {0}},
    {"SQLITE_IOERR_AUTH", SQLITE_IOERR_AUTH, {0}},
    {"SQLITE_IOERR_DATA", SQLITE_IOERR_DATA, {0}},
    {"SQLITE_IOERR_CORRUPTFS", SQLITE_IOERR_CORRUPTFS, {0}},
};

static const char *extended_name(int extended)
{
    size_t i = 0U;
    size_t count = sizeof(k_sqlite_names) / sizeof(k_sqlite_names[0]);

    for (i = 0U; i < count; i++) {
        if (k_sqlite_names[i].code == extended) {
            return k_sqlite_names[i].name;
        }
    }
    for (i = 0U; i < count; i++) {
        // NOLINTNEXTLINE(hicpp-signed-bitwise)
        if (k_sqlite_names[i].code == (extended & SQLITE_PRIMARY_CODE_MASK)) {
            return k_sqlite_names[i].name;
        }
    }
    return "SQLITE_ERROR";
}

/* EACCES/ENOENT/EISDIR come from sqlite3_system_errno; CANTOPEN_ISDIR (526)
 * names a directory even when the OS gives no errno. Anything else is other. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static StoreOpenReason derive_reason(int extended, int sys_errno)
{
    if (extended == SQLITE_CANTOPEN_ISDIR) {
        return STORE_REASON_IS_DIRECTORY;
    }
    switch (sys_errno) {
    case EACCES:
        return STORE_REASON_PERMISSION;
    case ENOENT:
        return STORE_REASON_MISSING;
    case EISDIR:
        return STORE_REASON_IS_DIRECTORY;
    default:
        return STORE_REASON_OTHER;
    }
}

/*
 * Snapshot the SQLite failure on db into out. Call immediately after the
 * failing call: any later SQLite call on db (finalize, ROLLBACK, even
 * sqlite3_errmsg for the legacy text) may clobber it. NULL db (open-time OOM)
 * records NOMEM. NULL out captures nothing.
 */
/* Copy an engine message into the fixed field: NULL becomes "", over-long
   messages truncate (unit-tested via store_test_copy_message). */
static void copy_sqlite_message(StoreError *out, const char *msg)
{
    size_t msg_n = 0U;

    if (msg == NULL) {
        msg = "";
    }
    msg_n = strlen(msg);
    if (msg_n >= sizeof(out->sqlite_message)) {
        msg_n = sizeof(out->sqlite_message) - 1U;
    }
    memcpy(out->sqlite_message, msg, msg_n);
    out->sqlite_message[msg_n] = '\0';
}

static void snapshot_sqlite_error(sqlite3 *db, StoreError *out);

/* E4.10: the failing statement touched the search index when its SQL names it. */
static bool sql_names_entries_fts(const char *sql)
{
    if (sql == NULL) {
        return false;
    }
    return strstr(sql, "entries_fts") != NULL;
}

static bool db_stmts_name_entries_fts(sqlite3 *db)
{
    sqlite3_stmt *stmt = NULL;

    if (db == NULL) {
        return false;
    }
    while ((stmt = sqlite3_next_stmt(db, stmt)) != NULL) {
        if (sql_names_entries_fts(sqlite3_sql(stmt))) {
            return true;
        }
    }
    return false;
}

/* E4.10 base tables. The search index is not one of them. Names are fixed, so the
   SQL is fixed too: nothing here is long enough to need a formatted buffer. */
/* tags and entry_tags too (Itay 2026-10-16, amends E4.10): they are memories,
   not index, so damage there must never read as fts_only. */
static const char *const k_base_checks[] = {
    "PRAGMA quick_check(entries);",          "PRAGMA quick_check(tags);",
    "PRAGMA quick_check(entry_tags);",       "PRAGMA quick_check(entry_links);",
    "PRAGMA quick_check(conflicts);",        "PRAGMA quick_check(settings);",
    "PRAGMA quick_check(settings_history);", "PRAGMA quick_check(brain_meta);",
    "PRAGMA quick_check(tombstones);",       "PRAGMA quick_check(outbox);",
};

/* A base-table check that did not return an engine code still reports corrupt. */
static void assign_corrupt(StoreError *out, const char *message)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->error_class = STORE_ERROR_CORRUPT;
    out->sqlite_code = SQLITE_CORRUPT;
    out->sqlite_extended = SQLITE_CORRUPT;
    (void)snprintf(out->sqlite_name, sizeof(out->sqlite_name), "SQLITE_CORRUPT");
    copy_sqlite_message(out, message);
}

/* Keep the engine's code and message. A failed base-table check is corrupt, never fts_only. */
static void record_engine_check_failure(sqlite3 *db, StoreError *fail)
{
    if (fail == NULL) {
        return;
    }
    snapshot_sqlite_error(db, fail);
    fail->error_class = STORE_ERROR_CORRUPT;
    fail->fts_only = false;
}

/* One "ok" row means the table passed. Any other row, or a failed statement, does not.
   fail NULL asks only for the bool; a non-NULL fail receives the corrupt error. */
static int quick_check_table(sqlite3 *db, const char *sql, StoreError *fail)
{
    sqlite3_stmt *stmt = NULL;
    char row_text[STORE_SQLITE_MSG_LEN];
    int rc = 0;

    row_text[0] = '\0';
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
    }
    if (rc == SQLITE_ROW) {
        const char *text = (const char *)sqlite3_column_text(stmt, 0);

        if (text != NULL && strcmp(text, "ok") == 0 && sqlite3_step(stmt) == SQLITE_DONE) {
            (void)sqlite3_finalize(stmt);
            return 0;
        }
        if (text != NULL) {
            (void)snprintf(row_text, sizeof(row_text), "%s", text);
        }
    } else if (rc != SQLITE_DONE) {
        record_engine_check_failure(db, fail);
        (void)sqlite3_finalize(stmt);
        return -1;
    }
    (void)sqlite3_finalize(stmt);
    assign_corrupt(fail, row_text[0] != '\0' ? row_text : "database disk image is malformed");
    return -1;
}

/* 0 when every base table is ok. fail receives the first corrupt error, or NULL to ignore it. */
static int check_base_tables(sqlite3 *db, StoreError *fail)
{
    size_t i = 0;

    for (i = 0; i < sizeof(k_base_checks) / sizeof(k_base_checks[0]); i++) {
        if (quick_check_table(db, k_base_checks[i], fail) != 0) {
            return -1;
        }
    }
    return 0;
}

/* Check passes and the statement touched FTS, or the code is CORRUPT_VTAB. */
static bool fts_only_for(int extended, bool touched, bool base_ok)
{
    if (extended == SQLITE_CORRUPT_VTAB) {
        return true;
    }
    if (!base_ok) {
        touched = false;
    }
    return touched;
}

static void note_corrupt_fts_only(sqlite3 *db, StoreError *out)
{
    bool touched = false;
    bool base_ok = false;

    if (out->error_class != STORE_ERROR_CORRUPT) {
        return;
    }
    touched = db_stmts_name_entries_fts(db);
    base_ok = check_base_tables(db, NULL) == 0;
    out->fts_only = fts_only_for(out->sqlite_extended, touched, base_ok);
}

static void snapshot_sqlite_error(sqlite3 *db, StoreError *out)
{
    const char *name = NULL;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (db == NULL) {
        out->error_class = STORE_ERROR_DATABASE;
        out->sqlite_code = SQLITE_NOMEM;
        out->sqlite_extended = SQLITE_NOMEM;
        (void)snprintf(out->sqlite_name, sizeof(out->sqlite_name), "SQLITE_NOMEM");
        (void)snprintf(out->sqlite_message, sizeof(out->sqlite_message), "out of memory");
        return;
    }
    out->sqlite_extended = sqlite3_extended_errcode(db);
    // NOLINTNEXTLINE(hicpp-signed-bitwise)
    out->sqlite_code = out->sqlite_extended & SQLITE_PRIMARY_CODE_MASK;
    out->error_class = classify_extended(out->sqlite_extended);
    name = extended_name(out->sqlite_extended);
    (void)snprintf(out->sqlite_name, sizeof(out->sqlite_name), "%s", name);
    copy_sqlite_message(out, sqlite3_errmsg(db));
    out->sys_errno = sqlite3_system_errno(db);
    if (out->error_class == STORE_ERROR_CANNOT_OPEN) {
        out->reason = derive_reason(out->sqlite_extended, out->sys_errno);
    }
}

static void capture_sqlite_error(sqlite3 *db, StoreError *out)
{
    snapshot_sqlite_error(db, out);
    /* After the snapshot: quick_check is another SQLite call and would clobber it. */
    if (out != NULL && db != NULL) {
        note_corrupt_fts_only(db, out);
    }
}

/* A failure on the import --from-db handle: same capture, flagged so the CLI
   names the source file instead of the brain (Itay 2026-10-15). */
static void capture_source_error(sqlite3 *src, StoreError *out)
{
    capture_sqlite_error(src, out);
    if (out != NULL) {
        out->import_source = true;
    }
}

#ifdef REMEMBER_TEST_HOOKS
/* Read-only probes for the E4.7 mapping table tests (store.h). */
StoreErrorClass store_test_classify_extended(int extended)
{
    return classify_extended(extended);
}

const char *store_test_sqlite_name(int extended)
{
    return extended_name(extended);
}

StoreOpenReason store_test_derive_reason(int extended, int sys_errno)
{
    return derive_reason(extended, sys_errno);
}

void store_test_copy_message(StoreError *out, const char *msg)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    copy_sqlite_message(out, msg);
}

int store_test_defensive(const Store *s)
{
    int enabled = -1;

    if (s == NULL || s->db == NULL) {
        return -1;
    }
    (void)sqlite3_db_config(s->db, SQLITE_DBCONFIG_DEFENSIVE, -1, &enabled);
    return enabled;
}

void store_test_capture_null(StoreError *out)
{
    capture_sqlite_error(NULL, out);
}
#endif /* REMEMBER_TEST_HOOKS */

/* Create every parent of path with mode 0700 (mkdir -p style). */
static int ensure_parent_dirs(const char *path, char *err, size_t errlen)
{
    char buf[REMEMBER_PATH_MAX];
    size_t len = strlen(path);
    size_t i = 0;

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
        if (mkdir(buf, DB_DIR_MODE) != 0) {
            struct stat st;
            if (errno != EEXIST) {
                /* Single-threaded CLI; strerror_r's signature is not portable (XSI vs GNU). */
                // NOLINTNEXTLINE(concurrency-mt-unsafe)
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

/* E4.15: a concurrent migration can rewrite the schema under us; the failing
   exec reports SQLITE_SCHEMA. Run the same statement once more; a second
   failure surfaces below (classified database by the mapping). Re-running is
   safe: single statements are atomic, and the multi-statement migration
   scripts trip on the first already-applied DDL (loud error, never a
   re-applied data write). */
static int exec_sql(sqlite3 *db, const char *sql, char *err, size_t errlen, StoreError *serr)
{
    int rc = SQLITE_OK;

    rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
    if (rc == SQLITE_SCHEMA) {
        rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
    }
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
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

static int read_user_version(sqlite3 *db, int *out, char *err, size_t errlen, StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    int rc = 0;

    rc = sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        set_errf(err, errlen, "cannot read user_version", sqlite3_errmsg(db));
        return -1;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        /* Where a non-database file surfaces: "file is not a database". */
        capture_sqlite_error(db, serr);
        set_errf(err, errlen, "cannot read user_version", sqlite3_errmsg(db));
        (void)sqlite3_finalize(stmt);
        return -1;
    }
    *out = sqlite3_column_int(stmt, 0);
    (void)sqlite3_finalize(stmt);
    return 0;
}

/* Default busy wait when the caller passes no options (plan 16 stage 5 owns
 * the --busy-timeout flag; the store applies the value as given). */

/* "PRAGMA busy_timeout = <int>;" fits with room to spare. */
enum { BUSY_TIMEOUT_SQL_CAP = 64 };

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
/* Defensive mode (Itay 2026-10-16): plain SQL on this connection cannot
   corrupt the file (writable_schema, schema_version writes,
   journal_mode=OFF, writes to virtual-table shadow tables). On for every
   connection; only repair's schema surgery lifts it, briefly. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static int set_defensive(sqlite3 *db, int enable, char *err, size_t errlen, StoreError *serr)
{
    if (sqlite3_db_config(db, SQLITE_DBCONFIG_DEFENSIVE, enable, (int *)NULL) != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        set_errf(err, errlen, "sqlite", sqlite3_errmsg(db));
        return -1;
    }
    return 0;
}

static int apply_pragmas(sqlite3 *db, int busy_timeout_ms, char *err, size_t errlen,
                         StoreError *serr)
{
    char timeout_sql[BUSY_TIMEOUT_SQL_CAP];

    if (set_defensive(db, 1, err, errlen, serr) != 0) {
        return -1;
    }
    if (exec_sql(db, "PRAGMA foreign_keys = ON;", err, errlen, serr) != 0) {
        return -1;
    }
    (void)snprintf(timeout_sql, sizeof(timeout_sql), "PRAGMA busy_timeout = %d;", busy_timeout_ms);
    if (exec_sql(db, timeout_sql, err, errlen, serr) != 0) {
        return -1;
    }
    return 0;
}

/* Canonical lowercase 8-4-4-4-12. Device sidecar and entry sync_id share this. */
/* RFC 4122 v7 packing: shifts and nibble masks are the layout, not business
   constants clang-tidy can usefully name. */
// NOLINTBEGIN(hicpp-signed-bitwise,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
static int mint_uuid_v7(char out[UUID_STR_LEN + 1])
{
    struct timespec ts;
    unsigned char b[UUID_BYTES];
    static const char hex[] = "0123456789abcdef";
    uint64_t ms = 0;
    int i = 0;
    int o = 0;

    if (out == NULL) {
        return -1;
    }
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) {
        return -1;
    }
    if (ts.tv_sec < 0) {
        return -1;
    }
    ms = ((uint64_t)ts.tv_sec * MS_PER_SEC) + (uint64_t)(ts.tv_nsec / NANOS_PER_MS);
    sqlite3_randomness((int)sizeof(b), b);
    b[0] = (unsigned char)(ms >> UUID_MS_SHIFT_0);
    b[1] = (unsigned char)(ms >> UUID_MS_SHIFT_1);
    b[2] = (unsigned char)(ms >> UUID_MS_SHIFT_2);
    b[3] = (unsigned char)(ms >> UUID_MS_SHIFT_3);
    b[4] = (unsigned char)(ms >> UUID_MS_SHIFT_4);
    b[5] = (unsigned char)ms;
    b[UUID_VER_BYTE] = (unsigned char)((b[UUID_VER_BYTE] & UUID_NIBBLE_MASK) | UUID_V7_HI);
    b[UUID_VAR_BYTE] = (unsigned char)((b[UUID_VAR_BYTE] & UUID_VAR_MASK) | UUID_VAR_RFC);
    for (i = 0; i < UUID_BYTES; i++) {
        if (i == UUID_HEX_DASH_0 || i == UUID_HEX_DASH_1 || i == UUID_HEX_DASH_2 ||
            i == UUID_HEX_DASH_3) {
            out[o++] = '-';
        }
        out[o++] = hex[b[i] >> UUID_NIBBLE_SHIFT];
        out[o++] = hex[b[i] & UUID_NIBBLE_MASK];
    }
    out[UUID_STR_LEN] = '\0';
    return 0;
}
// NOLINTEND(hicpp-signed-bitwise,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

static int sidecar_path(const char *db_path, char *out, size_t outlen)
{
    int n = 0;

    if (db_path == NULL || out == NULL) {
        return -1;
    }
    n = snprintf(out, outlen, "%s.device_id", db_path);
    if (n < 0 || (size_t)n >= outlen) {
        return -1;
    }
    return 0;
}

/* 0 = one canonical line, 1 = missing, -1 = invalid or unreadable. */
static int read_sidecar(const char *path, char uuid[UUID_STR_LEN + 1])
{
    FILE *f = NULL;
    char buf[SIDECAR_LINE_BUF];
    size_t n = 0;
    int extra = 0;

    /* macOS fopen has no 'e' (O_CLOEXEC) mode; CLI is short-lived. */
    // NOLINTNEXTLINE(android-cloexec-fopen)
    f = fopen(path, "r");
    if (f == NULL) {
        return (errno == ENOENT) ? 1 : -1;
    }
    if (fgets(buf, (int)sizeof(buf), f) == NULL) {
        (void)fclose(f);
        return -1;
    }
    n = strlen(buf);
    while (n > 0U && (buf[n - 1U] == '\n' || buf[n - 1U] == '\r')) {
        buf[--n] = '\0';
    }
    if (!uuid_v7_is_canonical(buf)) {
        (void)fclose(f);
        return -1;
    }
    /* A second line, even empty, is not the one-line sidecar.
       EOF from a read error is not a proven end of file. */
    extra = fgetc(f);
    if (extra != EOF || ferror(f) != 0) {
        (void)fclose(f);
        return -1;
    }
    (void)fclose(f);
    memcpy(uuid, buf, (size_t)UUID_STR_LEN + 1U);
    return 0;
}

/* Temp name then final path. The test hook fails the rename once. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static int rename_sidecar_tmp(const char *tmp, const char *path)
{
#ifdef REMEMBER_TEST_HOOKS
    if (g_fail_sidecar_rename > 0) {
        g_fail_sidecar_rename = 0;
        return -1;
    }
#endif
    return rename(tmp, path);
}

/* path then uuid: sidecar file vs contents; call sites pass them in that order.
   Writes a temp file and renames it over path so a crash cannot leave a partial sidecar. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static int write_sidecar(const char *path, const char *uuid, char *err, size_t errlen)
{
    char tmp[REMEMBER_PATH_MAX + SIDECAR_PATH_PAD + SIDECAR_TMP_EXTRA];
    int fd = -1;
    char line[UUID_STR_LEN + 2];
    ssize_t wrote = 0;
    size_t n = 0;
    int sn = 0;

    n = (size_t)snprintf(line, sizeof(line), "%s\n", uuid);
    if (n != (size_t)UUID_STR_LEN + 1U) {
        set_err(err, errlen, "cannot write device_id sidecar");
        return -1;
    }
    sn = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (sn < 0 || (size_t)sn >= sizeof(tmp)) {
        set_err(err, errlen, "cannot write device_id sidecar");
        return -1;
    }
    /* O_* are signed-int flag macros. */
    // NOLINTNEXTLINE(hicpp-signed-bitwise,android-cloexec-open)
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, SIDECAR_FILE_MODE);
    if (fd < 0) {
        set_err(err, errlen, "cannot write device_id sidecar");
        return -1;
    }
    if (fchmod(fd, SIDECAR_FILE_MODE) != 0) {
        goto cleanup;
    }
    wrote = write(fd, line, n);
    if (wrote < 0 || (size_t)wrote != n) {
        goto cleanup;
    }
    /* Durable before the rename publishes it: an empty sidecar after power loss
       would be "invalid" and never re-minted (E14.4). */
    if (fsync(fd) != 0) {
        goto cleanup;
    }
    {
        int closed = close(fd);

        fd = -1;
        if (closed != 0) {
            goto cleanup;
        }
    }
    if (rename_sidecar_tmp(tmp, path) != 0) {
        goto cleanup;
    }
    return 0;

cleanup:
    if (fd >= 0) {
        (void)close(fd);
    }
    (void)unlink(tmp);
    set_err(err, errlen, "cannot write device_id sidecar");
    return -1;
}

static char *dup_str(const char *s);

static int bind_device_id(Store *s, const char *uuid, char *err, size_t errlen)
{
    s->device_id = dup_str(uuid);
    if (s->device_id == NULL) {
        set_err(err, errlen, "out of memory");
        return -1;
    }
    return 0;
}

/* Sidecar is the only device id. Valid: use it. Missing: mint and write. Invalid:
   fail, unless this handle is a `device new` open (Itay 2026-10-13), which mints over it. */
static int ensure_device_identity(Store *s, char *err, size_t errlen)
{
    char side[REMEMBER_PATH_MAX + SIDECAR_PATH_PAD];
    char file_uuid[UUID_STR_LEN + 1];
    int file_rc = 0;

    if (sidecar_path(s->path, side, sizeof(side)) != 0) {
        set_err(err, errlen, "database path is too long");
        return -1;
    }
    file_rc = read_sidecar(side, file_uuid);
    if (file_rc < 0 && !s->replace_invalid_sidecar) {
        set_err(err, errlen, "invalid device_id sidecar");
        return -1;
    }
    if (file_rc != 0) {
        if (mint_uuid_v7(file_uuid) != 0) {
            set_err(err, errlen, "cannot mint device_id");
            return -1;
        }
        if (write_sidecar(side, file_uuid, err, errlen) != 0) {
            return -1;
        }
    }
    return bind_device_id(s, file_uuid, err, errlen);
}

/* Already-current open. A valid sidecar binds without a lock. Otherwise take the
   write lock migrate also takes, re-read, and mint only if still missing, so
   parallel opens all bind the one id the file keeps (Itay 2026-10-13). */
static int bind_current_identity(Store *s, char *err, size_t errlen)
{
    char side[REMEMBER_PATH_MAX + SIDECAR_PATH_PAD];
    char uuid[UUID_STR_LEN + 1];

    if (sidecar_path(s->path, side, sizeof(side)) == 0 && read_sidecar(side, uuid) == 0) {
        return bind_device_id(s, uuid, err, errlen);
    }
    if (exec_sql(s->db, "BEGIN IMMEDIATE;", err, errlen, &s->last) != 0) {
        return -1;
    }
    if (ensure_device_identity(s, err, errlen) != 0 ||
        exec_sql(s->db, "COMMIT;", err, errlen, &s->last) != 0) {
        rollback_quiet(s->db);
        return -1;
    }
    return 0;
}

const char *store_device_id(const Store *s)
{
    if (s == NULL) {
        return NULL;
    }
    return s->device_id;
}

static void fill_device_previous(char *prev_out, bool *had_prev, int file_rc, const char *previous)
{
    if (file_rc == 0) {
        memcpy(prev_out, previous, (size_t)STORE_DEVICE_ID_LEN + 1U);
        *had_prev = true;
        return;
    }
    prev_out[0] = '\0';
    *had_prev = false;
}

StoreStatus store_device_new(Store *s, char *prev_out, size_t prev_len, bool *had_prev, char *err,
                             size_t errlen)
{
    char side[REMEMBER_PATH_MAX + SIDECAR_PATH_PAD];
    char previous[UUID_STR_LEN + 1];
    char minted[UUID_STR_LEN + 1];
    char *next = NULL;
    int file_rc = 0;

    if (s == NULL || prev_out == NULL || had_prev == NULL ||
        prev_len < (size_t)STORE_DEVICE_ID_LEN + 1U) {
        set_err(err, errlen, "internal error");
        return STORE_ERR_INTERNAL;
    }
    memset(&s->last, 0, sizeof(s->last));
    if (sidecar_path(s->path, side, sizeof(side)) != 0) {
        set_err(err, errlen, "database path is too long");
        return STORE_ERR_INTERNAL;
    }
    if (exec_sql(s->db, "BEGIN IMMEDIATE;", err, errlen, &s->last) != 0) {
        return STORE_ERR_SQLITE;
    }
    /* device new always mints: an invalid file is replaced like a missing one. */
    file_rc = read_sidecar(side, previous);
    if (mint_uuid_v7(minted) != 0) {
        rollback_quiet(s->db);
        set_err(err, errlen, "cannot mint device_id");
        return STORE_ERR_INTERNAL;
    }
    /* Dup before the rename so an allocation failure leaves the old file. */
    next = dup_str(minted);
    if (next == NULL) {
        rollback_quiet(s->db);
        set_err(err, errlen, "out of memory");
        return STORE_ERR_OOM;
    }
    if (write_sidecar(side, minted, err, errlen) != 0) {
        free(next);
        rollback_quiet(s->db);
        return STORE_ERR_INTERNAL;
    }
    free(s->device_id);
    s->device_id = next;
    if (exec_sql(s->db, "COMMIT;", err, errlen, &s->last) != 0) {
        /* The transaction wrote nothing. The sidecar is already the new id. */
        rollback_quiet(s->db);
    }
    fill_device_previous(prev_out, had_prev, file_rc, previous);
    return STORE_OK;
}

static int backfill_sync_ids(sqlite3 *db, const char *device_id, char *err, size_t errlen,
                             StoreError *serr)
{
    sqlite3_stmt *sel = NULL;
    sqlite3_stmt *upd = NULL;
    int rc = 0;
    char uuid[UUID_STR_LEN + 1];
    char vv_json[VV_JSON_BUFLEN];

    rc = sqlite3_prepare_v2(db,
                            "SELECT id FROM entries WHERE sync_id IS NULL OR version_vector IS "
                            "NULL;",
                            -1, &sel, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        set_errf(err, errlen, "sqlite", sqlite3_errmsg(db));
        return -1;
    }
    rc = sqlite3_prepare_v2(
        db, "UPDATE entries SET sync_id = ?1, version_vector = ?2 WHERE id = ?3;", -1, &upd, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(sel);
        set_errf(err, errlen, "sqlite", sqlite3_errmsg(db));
        return -1;
    }
    (void)snprintf(vv_json, sizeof(vv_json), "{\"%s\":1}", device_id);
    while ((rc = sqlite3_step(sel)) == SQLITE_ROW) {
        long long id = sqlite3_column_int64(sel, 0);
        if (mint_uuid_v7(uuid) != 0) {
            (void)sqlite3_finalize(sel);
            (void)sqlite3_finalize(upd);
            set_err(err, errlen, "cannot mint sync_id");
            return -1;
        }
        (void)sqlite3_reset(upd);
        (void)sqlite3_bind_text(upd, 1, uuid, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_text(upd, 2, vv_json, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_int64(upd, 3, id);
        if (sqlite3_step(upd) != SQLITE_DONE) {
            capture_sqlite_error(db, serr);
            (void)sqlite3_finalize(sel);
            (void)sqlite3_finalize(upd);
            set_errf(err, errlen, "sqlite", sqlite3_errmsg(db));
            return -1;
        }
    }
    if (rc != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(sel);
        (void)sqlite3_finalize(upd);
        set_errf(err, errlen, "sqlite", sqlite3_errmsg(db));
        return -1;
    }
    (void)sqlite3_finalize(sel);
    (void)sqlite3_finalize(upd);
    return 0;
}

/* Gate an already-created database: 6 is current, anything else is refused. */
static int check_version(int version, char *err, size_t errlen)
{
    if (version == SCHEMA_VERSION) {
        return 0;
    }
    if (version > SCHEMA_VERSION) {
        set_err(err, errlen, k_newer_database);
    } else {
        set_err(err, errlen, "unsupported database version");
    }
    return -1;
}

static int migrate_v3_to_v4_columns(sqlite3 *db, char *err, size_t errlen, StoreError *serr)
{
    if (exec_sql(db, "ALTER TABLE entries ADD COLUMN sync_id TEXT;", err, errlen, serr) != 0) {
        return -1;
    }
    if (exec_sql(db, "ALTER TABLE entries ADD COLUMN deleted_at TEXT;", err, errlen, serr) != 0) {
        return -1;
    }
    if (exec_sql(db, "ALTER TABLE entries ADD COLUMN version_vector TEXT;", err, errlen, serr) !=
        0) {
        return -1;
    }
    if (exec_sql(db, k_sync_sql, err, errlen, serr) != 0) {
        return -1;
    }
    return 0;
}

static int migrate_body_to_v4(sqlite3 *db, int version, char *err, size_t errlen, StoreError *serr)
{
    if (version == 0) {
        if (exec_sql(db, k_schema_sql, err, errlen, serr) != 0) {
            return -1;
        }
        if (exec_sql(db, k_links_sql, err, errlen, serr) != 0) {
            return -1;
        }
        return exec_sql(db, k_sync_sql, err, errlen, serr);
    }
    if (version == 1) {
        if (exec_sql(db, "ALTER TABLE entries ADD COLUMN expires_at TEXT;", err, errlen, serr) !=
            0) {
            return -1;
        }
        if (exec_sql(db, k_links_sql, err, errlen, serr) != 0) {
            return -1;
        }
        return migrate_v3_to_v4_columns(db, err, errlen, serr);
    }
    if (version == 2) {
        if (exec_sql(db, k_links_sql, err, errlen, serr) != 0) {
            return -1;
        }
        return migrate_v3_to_v4_columns(db, err, errlen, serr);
    }
    if (version == 3) {
        return migrate_v3_to_v4_columns(db, err, errlen, serr);
    }
    return 1;
}

/* Fail fast if the schema left any dangling foreign-key reference (run after a
   rebuild, while foreign_keys is OFF). One returned row = at least one violation. */
static int foreign_keys_ok(sqlite3 *db, char *err, size_t errlen, StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    int rc = 0;

    rc = sqlite3_prepare_v2(db, "PRAGMA foreign_key_check;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        set_errf(err, errlen, "sqlite", sqlite3_errmsg(db));
        return -1;
    }
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        (void)sqlite3_finalize(stmt);
        set_err(err, errlen, "migration left a dangling reference");
        return -1;
    }
    if (rc != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        set_errf(err, errlen, "sqlite", sqlite3_errmsg(db));
        (void)sqlite3_finalize(stmt);
        return -1;
    }
    (void)sqlite3_finalize(stmt);
    return 0;
}

/* Rebuild entries so v1/2/3-migrated rows get NOT NULL sync_id/version_vector and
   a single named index per column (parity with a fresh create). Runs with
   foreign_keys OFF so DROP does not cascade entry_links/entry_tags; the caller's
   foreign_keys_ok() verifies afterward. sync_id/version_vector are already
   backfilled, so the NOT NULL copy succeeds. */
static int rebuild_entries_notnull(sqlite3 *db, char *err, size_t errlen, StoreError *serr)
{
    static const char sql[] =
        "CREATE TABLE entries_v4 (\n" K_ENTRIES_V5_COLUMNS ");\n"
        "INSERT INTO entries_v4 SELECT id, key, body, body_hash, source, created_at, updated_at, "
        "expires_at, sync_id, deleted_at, version_vector FROM entries;\n"
        "DROP TABLE entries;\n"
        "ALTER TABLE entries_v4 RENAME TO entries;\n" K_ENTRIES_V5_INDEXES;
    return exec_sql(db, sql, err, errlen, serr);
}

static StoreStatus fts_resync(sqlite3 *db, long long entry_id, StoreError *serr);

/* One integer column. bind NULL means the statement has no parameter. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static int select_int(sqlite3 *db, const char *sql, const char *bind, int *out, char *err,
                      size_t errlen, StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    int rc = 0;

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        set_errf(err, errlen, "sqlite", sqlite3_errmsg(db));
        return -1;
    }
    if (bind != NULL) {
        (void)sqlite3_bind_text(stmt, 1, bind, -1, SQLITE_STATIC);
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        set_errf(err, errlen, "sqlite", sqlite3_errmsg(db));
        return -1;
    }
    *out = sqlite3_column_int(stmt, 0);
    (void)sqlite3_finalize(stmt);
    return 0;
}

/* One text column of the first row into *out (heap), or NULL when there is no
   row or the value is NULL. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static int select_text(sqlite3 *db, const char *sql, char **out, char *err, size_t errlen,
                       StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    const unsigned char *text = NULL;
    int rc = 0;

    *out = NULL;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        set_errf(err, errlen, "sqlite", sqlite3_errmsg(db));
        return -1;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        set_errf(err, errlen, "sqlite", sqlite3_errmsg(db));
        (void)sqlite3_finalize(stmt);
        return -1;
    }
    text = (rc == SQLITE_ROW) ? sqlite3_column_text(stmt, 0) : NULL;
    if (text != NULL) {
        *out = dup_str((const char *)text);
        if (*out == NULL) {
            (void)sqlite3_finalize(stmt);
            set_err(err, errlen, "out of memory");
            return -1;
        }
    }
    (void)sqlite3_finalize(stmt);
    return 0;
}

/* Create with the canonical DDL only when the table is absent, so an existing
   object's sqlite_master text stays identical to a fresh database. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static int create_table_if_missing(sqlite3 *db, const char *name, const char *sql, char *err,
                                   size_t errlen, StoreError *serr)
{
    int n = 0;

    if (select_int(db, "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name=?1;", name,
                   &n, err, errlen, serr) != 0) {
        return -1;
    }
    if (n > 0) {
        return 0;
    }
    return exec_sql(db, sql, err, errlen, serr);
}

static int rebuild_conflicts_with_origin(sqlite3 *db, char *err, size_t errlen, StoreError *serr)
{
    /* Rename away, then CREATE with the fresh DDL. Renaming the new table quotes
       its name in sqlite_master and would drift from a fresh database. */
    static const char sql[] =
        "ALTER TABLE conflicts RENAME TO conflicts_v4;\n" K_CONFLICTS_SQL
        "INSERT INTO conflicts(id, sync_id, reason, local_json, incoming_json, created_at, "
        "origin) SELECT id, sync_id, reason, local_json, incoming_json, created_at, NULL FROM "
        "conflicts_v4;\n"
        "DROP TABLE conflicts_v4;\n";
    return exec_sql(db, sql, err, errlen, serr);
}

static int ensure_conflicts_origin(sqlite3 *db, char *err, size_t errlen, StoreError *serr)
{
    int origin = 0;

    if (create_table_if_missing(db, "conflicts", K_CONFLICTS_SQL, err, errlen, serr) != 0) {
        return -1;
    }
    if (select_int(db, "SELECT COUNT(*) FROM pragma_table_info('conflicts') WHERE name='origin';",
                   NULL, &origin, err, errlen, serr) != 0) {
        return -1;
    }
    if (origin > 0) {
        return 0;
    }
    return rebuild_conflicts_with_origin(db, err, errlen, serr);
}

static int repopulate_fts(sqlite3 *db, char *err, size_t errlen, StoreError *serr)
{
    sqlite3_stmt *sel = NULL;
    int rc = 0;

    rc = sqlite3_prepare_v2(db, "SELECT local_id FROM entries;", -1, &sel, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        set_errf(err, errlen, "sqlite", sqlite3_errmsg(db));
        return -1;
    }
    while ((rc = sqlite3_step(sel)) == SQLITE_ROW) {
        StoreStatus st = fts_resync(db, sqlite3_column_int64(sel, 0), serr);
        if (st != STORE_OK) {
            (void)sqlite3_finalize(sel);
            set_err(err, errlen,
                    (st == STORE_ERR_OOM) ? "out of memory" : "cannot rebuild search index");
            return -1;
        }
    }
    if (rc != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(sel);
        set_errf(err, errlen, "sqlite", sqlite3_errmsg(db));
        return -1;
    }
    (void)sqlite3_finalize(sel);
    return 0;
}

/* v5 objects on a database that already has entries, tags, and entry_tags.
   Drop devices last: the row is not copied into the sidecar. */
static int apply_v5_delta(sqlite3 *db, char *err, size_t errlen, StoreError *serr)
{
    if (exec_sql(db, "DROP TABLE IF EXISTS entries_fts;", err, errlen, serr) != 0) {
        return -1;
    }
    /* Filled after the v6 rebuild (migrate_to_current): the rows are the same,
       and repopulate reads the v6 names. */
    if (exec_sql(db, K_FTS_SQL, err, errlen, serr) != 0) {
        return -1;
    }
    if (create_table_if_missing(db, "bucket_prefixes", K_BUCKET_PREFIXES_SQL, err, errlen, serr) !=
        0) {
        return -1;
    }
    if (create_table_if_missing(db, "settings", K_SETTINGS_SQL, err, errlen, serr) != 0) {
        return -1;
    }
    if (ensure_conflicts_origin(db, err, errlen, serr) != 0) {
        return -1;
    }
    return exec_sql(db, "DROP TABLE IF EXISTS devices;", err, errlen, serr);
}

/* ---- v5 -> v6 (007 §10) ---------------------------------------------------- */

/* One UUID v7 per link (008 §8 step 2). The rebuild left 'v5:<rowid>'. */
static int mint_link_ids(sqlite3 *db, char *err, size_t errlen, StoreError *serr)
{
    sqlite3_stmt *sel = NULL;
    sqlite3_stmt *upd = NULL;
    char uuid[UUID_STR_LEN + 1];
    int rc = 0;
    int result = -1;

    if (sqlite3_prepare_v2(db, "SELECT rowid FROM entry_links;", -1, &sel, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(db, "UPDATE entry_links SET id = ?1 WHERE rowid = ?2;", -1, &upd,
                           NULL) != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        set_errf(err, errlen, "sqlite", sqlite3_errmsg(db));
        goto cleanup;
    }
    while ((rc = sqlite3_step(sel)) == SQLITE_ROW) {
        if (mint_uuid_v7(uuid) != 0) {
            set_err(err, errlen, "cannot mint link id");
            goto cleanup;
        }
        (void)sqlite3_reset(upd);
        (void)sqlite3_bind_text(upd, 1, uuid, -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_int64(upd, 2, sqlite3_column_int64(sel, 0));
        if (sqlite3_step(upd) != SQLITE_DONE) {
            capture_sqlite_error(db, serr);
            set_errf(err, errlen, "sqlite", sqlite3_errmsg(db));
            goto cleanup;
        }
    }
    if (rc != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        set_errf(err, errlen, "sqlite", sqlite3_errmsg(db));
        goto cleanup;
    }
    result = 0;

cleanup:
    (void)sqlite3_finalize(sel);
    (void)sqlite3_finalize(upd);
    return result;
}

/* Run one statement whose ?N are the given texts, in order. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static int exec_bound(sqlite3 *db, const char *sql, const char *const *binds, int nbinds, char *err,
                      size_t errlen, StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    int i = 0;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        set_errf(err, errlen, "sqlite", sqlite3_errmsg(db));
        return -1;
    }
    for (i = 0; i < nbinds; i++) {
        (void)sqlite3_bind_text(stmt, i + 1, binds[i], -1, SQLITE_STATIC);
    }
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        set_errf(err, errlen, "sqlite", sqlite3_errmsg(db));
        (void)sqlite3_finalize(stmt);
        return -1;
    }
    (void)sqlite3_finalize(stmt);
    return 0;
}

/* 008 §8 step 3: the one brain_meta row. */
static int insert_brain_meta(sqlite3 *db, char *err, size_t errlen, StoreError *serr)
{
    char brain_id[UUID_STR_LEN + 1];
    const char *binds[2];

    if (mint_uuid_v7(brain_id) != 0) {
        set_err(err, errlen, "cannot mint brain_id");
        return -1;
    }
    binds[0] = brain_id;
    binds[1] = k_min_reader_version;
    return exec_bound(db,
                      "INSERT INTO brain_meta(brain_id, change_seq, min_reader_version) "
                      "VALUES (?1, 0, ?2);",
                      binds, 2, err, errlen, serr);
}

static const char k_prefixes_key[] = "remember.buckets.prefixes";

static void free_open_warnings(Store *s)
{
    size_t i = 0;

    for (i = 0; i < s->nwarnings; i++) {
        free(s->warnings[i].value);
    }
    free(s->warnings);
    s->warnings = NULL;
    s->nwarnings = 0U;
}

/* Keep the prefix that was left out for store_open_warning (007 ST-7.15). */
static int add_open_warning(Store *s, const char *prefix, const char *reason)
{
    MigrationWarning *grown = NULL;
    char *value = dup_str(prefix);

    if (value == NULL) {
        return -1;
    }
    grown = (MigrationWarning *)realloc(s->warnings, (s->nwarnings + 1U) * sizeof(*grown));
    if (grown == NULL) {
        free(value);
        return -1;
    }
    s->warnings = grown;
    s->warnings[s->nwarnings].value = value;
    s->warnings[s->nwarnings].reason = reason;
    s->nwarnings++;
    return 0;
}

/* Valid prefixes become the JSON array in *json; each other prefix warns.
   Valid prefixes hold no '"' or '\\', so each item is written as is, and a
   quoted search finds a folded duplicate. Sets *nvalid and *nlive. */
static int collect_prefixes(Store *s, char **json, size_t *nvalid, size_t *nlive, char *err,
                            size_t errlen)
{
    sqlite3 *db = s->db;
    sqlite3_stmt *sel = NULL;
    FILE *fp = NULL;
    size_t len = 0U;
    int rc = 0;
    int result = -1;

    *json = NULL;
    fp = open_memstream(json, &len);
    if (fp == NULL || fputc('[', fp) == EOF) {
        set_err(err, errlen, "out of memory");
        goto cleanup;
    }
    if (sqlite3_prepare_v2(db, k_e12_effective_sql, -1, &sel, NULL) != SQLITE_OK) {
        capture_sqlite_error(db, &s->last);
        set_errf(err, errlen, "sqlite", sqlite3_errmsg(db));
        goto cleanup;
    }
    while ((rc = sqlite3_step(sel)) == SQLITE_ROW) {
        const char *prefix = (const char *)sqlite3_column_text(sel, 0);
        char folded[REMEMBER_BUCKET_PREFIX_BUF];
        char quoted[REMEMBER_BUCKET_PREFIX_BUF + 2];
        const char *reason = NULL;

        (*nlive)++;
        if (bucket_prefix_fold(prefix != NULL ? prefix : "", folded, sizeof(folded)) != NORM_OK) {
            reason = "bad_prefix_shape";
        } else {
            (void)snprintf(quoted, sizeof(quoted), "\"%s\"", folded);
            if (fflush(fp) != 0) {
                set_err(err, errlen, "out of memory");
                goto cleanup;
            }
            if (strstr(*json, quoted) != NULL) {
                reason = "duplicate_prefix";
            }
        }
        if (reason != NULL) {
            if (add_open_warning(s, prefix != NULL ? prefix : "", reason) != 0) {
                set_err(err, errlen, "out of memory");
                goto cleanup;
            }
            continue;
        }
        if (fprintf(fp, "%s%s", *nvalid > 0U ? "," : "", quoted) < 0) {
            set_err(err, errlen, "out of memory");
            goto cleanup;
        }
        (*nvalid)++;
    }
    if (rc != SQLITE_DONE) {
        capture_sqlite_error(db, &s->last);
        set_errf(err, errlen, "sqlite", sqlite3_errmsg(db));
        goto cleanup;
    }
    if (fputc(']', fp) == EOF) {
        set_err(err, errlen, "out of memory");
        goto cleanup;
    }
    rc = fclose(fp);
    fp = NULL;
    if (rc != 0) {
        set_err(err, errlen, "out of memory");
        goto cleanup;
    }
    result = 0;

cleanup:
    (void)sqlite3_finalize(sel);
    if (fp != NULL) {
        (void)fclose(fp);
    }
    if (result != 0) {
        free(*json);
        *json = NULL;
    }
    return result;
}

/* 007 §10 settings step 2: one remember.buckets.prefixes row, or none. */
static int migrate_prefix_row(Store *s, const char *now, char *err, size_t errlen)
{
    sqlite3 *db = s->db;
    char *json = NULL;
    char *order_at = NULL;
    char vv[VV_JSON_BUFLEN];
    const char *binds[5];
    size_t nvalid = 0U;
    size_t nlive = 0U;
    int rows = 0;
    int result = -1;

    if (select_int(db, "SELECT COUNT(*) FROM bucket_prefixes;", NULL, &rows, err, errlen,
                   &s->last) != 0) {
        return -1;
    }
    if (rows == 0) {
        return 0; /* ST-10.1 */
    }
    if (collect_prefixes(s, &json, &nvalid, &nlive, err, errlen) != 0) {
        return -1;
    }
    if (nlive > 0U && nvalid == 0U) {
        free(json);
        return 0; /* every prefix failed: no row */
    }
    if (select_text(db, "SELECT updated_at FROM settings WHERE name = 'bucket_prefix_order';",
                    &order_at, err, errlen, &s->last) != 0) {
        goto cleanup;
    }
    (void)snprintf(vv, sizeof(vv), "{\"%s\":1}", s->device_id);
    binds[0] = k_prefixes_key;
    binds[1] = json;
    binds[2] = order_at != NULL ? order_at : now;
    binds[3] = s->device_id;
    binds[4] = vv;
    if (exec_bound(db,
                   "INSERT INTO settings_v6(key, value, updated_by, updated_at, device_id, "
                   "version_vector, deleted_at, change_seq) VALUES (?1, ?2, "
                   "'{\"kind\":\"human\",\"id\":null}', ?3, ?4, ?5, NULL, 1);",
                   binds, 5, err, errlen, &s->last) != 0 ||
        exec_sql(db, "UPDATE brain_meta SET change_seq = 1;", err, errlen, &s->last) != 0) {
        goto cleanup;
    }
    result = 0;

cleanup:
    free(json);
    free(order_at);
    return result;
}

/* PRAGMA integrity_check answers one row, "ok", when the file is sound. */
static int integrity_ok(sqlite3 *db, char *err, size_t errlen, StoreError *serr)
{
    char *answer = NULL;
    int ok = 0;

    if (select_text(db, "PRAGMA integrity_check;", &answer, err, errlen, serr) != 0) {
        return -1;
    }
    ok = (answer != NULL && strcmp(answer, "ok") == 0);
    free(answer);
    if (!ok) {
        set_err(err, errlen, "migration failed the integrity check");
        return -1;
    }
    return 0;
}

/* Inside the caller's transaction, on a v5-shaped brain with a bound device id:
   the 008 rebuild, the settings steps, the 010 steps, then both checks. The
   caller sets user_version only after this returns 0. */
static int migrate_v5_to_v6(Store *s, bool refill_fts, char *err, size_t errlen)
{
    sqlite3 *db = s->db;
    StoreError *serr = &s->last;
    char now[ISO_TS_MIN_BUFLEN];
    const char *binds[1];

    if (utc_now(now, sizeof(now)) != 0) {
        set_err(err, errlen, "cannot read the clock");
        return -1;
    }
    if (exec_sql(db, k_v6_rebuild_sql, err, errlen, serr) != 0 ||
        mint_link_ids(db, err, errlen, serr) != 0 ||
        insert_brain_meta(db, err, errlen, serr) != 0) {
        return -1;
    }
    if (refill_fts && repopulate_fts(db, err, errlen, serr) != 0) {
        return -1;
    }
    if (exec_sql(db, k_v6_settings_tables_sql, err, errlen, serr) != 0 ||
        migrate_prefix_row(s, now, err, errlen) != 0 ||
        exec_sql(db, k_v6_settings_swap_sql, err, errlen, serr) != 0) {
        return -1;
    }
    binds[0] = now;
    if (exec_sql(db, k_v6_outbox_sql, err, errlen, serr) != 0 ||
        exec_bound(db,
                   "UPDATE entries SET expired_at = expires_at WHERE deleted_at IS NULL AND "
                   "expires_at <= ?1;",
                   binds, 1, err, errlen, serr) != 0) {
        return -1;
    }
    if (foreign_keys_ok(db, err, errlen, serr) != 0) {
        return -1;
    }
    return integrity_ok(db, err, errlen, serr);
}

/* v0 creates the v5 DDL directly. v1/v2/v3 still climb through the v4 column rebuild. */
static int migrate_legacy(Store *s, int version, char *err, size_t errlen)
{
    sqlite3 *db = s->db;
    StoreError *serr = &s->last;

    if (migrate_body_to_v4(db, version, err, errlen, serr) != 0) {
        return -1;
    }
    if (ensure_device_identity(s, err, errlen) != 0) {
        return -1;
    }
    if (backfill_sync_ids(db, s->device_id, err, errlen, serr) != 0) {
        return -1;
    }
    /* v0 is already NOT NULL from k_schema_sql; only migrated rows rebuild. */
    if (version != 0 && rebuild_entries_notnull(db, err, errlen, serr) != 0) {
        return -1;
    }
    return foreign_keys_ok(db, err, errlen, serr);
}

/*
 * Create or migrate to schema 6 in one write transaction: climb to the v5
 * shape, then the one v6 step, then user_version = 6 (007 §10 step 2). Assumes
 * foreign_keys is OFF (ensure_schema toggles it) so the rebuilds' DROPs do not
 * cascade. Returns 0 = created/migrated + committed (identity bound), 1 = a
 * concurrent remember already migrated (caller binds identity), -1 = failure.
 * Device id comes from the sidecar, never from a devices row.
 */
static int migrate_to_current(Store *s, char *err, size_t errlen)
{
    sqlite3 *db = s->db;
    StoreError *serr = &s->last;
    int version = 0;

    if (exec_sql(db, "BEGIN IMMEDIATE;", err, errlen, serr) != 0) {
        return -1;
    }
    if (read_user_version(db, &version, err, errlen, serr) != 0) {
        goto cleanup_fail;
    }
    if (version == SCHEMA_VERSION) {
        rollback_quiet(db);
        return 1;
    }
    /* Another writer committed an illegal version before BEGIN IMMEDIATE.
       Same refusal as the outer gate. */
    if (version < 0 || version > SCHEMA_VERSION) {
        rollback_quiet(db);
        return check_version(version, err, errlen);
    }
    if (version <= 3) {
        if (migrate_legacy(s, version, err, errlen) != 0) {
            goto cleanup_fail;
        }
    }
    if (version <= 4 && apply_v5_delta(db, err, errlen, serr) != 0) {
        goto cleanup_fail;
    }
    if (s->device_id == NULL && ensure_device_identity(s, err, errlen) != 0) {
        goto cleanup_fail;
    }
    if (migrate_v5_to_v6(s, version <= 4, err, errlen) != 0) {
        goto cleanup_fail;
    }
    if (exec_sql(db, "PRAGMA user_version = 6;", err, errlen, serr) != 0) {
        goto cleanup_fail;
    }
    if (exec_sql(db, "COMMIT;", err, errlen, serr) != 0) {
        goto cleanup_fail;
    }
    return 0;

cleanup_fail:
    rollback_quiet(db);
    return -1;
}

/*
 * Bring an open database to schema version 5 and bind local device identity.
 *
 * The v1/2/3 migration rebuilds entries, so foreign_keys is toggled OFF around
 * it (the PRAGMA is a no-op inside a transaction) and restored ON afterward.
 * The already-current and concurrent-adopt paths share bind_identity so the
 * bind is covered by every normal reopen.
 */
static int ensure_schema(Store *s, char *err, size_t errlen)
{
    int version = 0;
    int rc = 0;
    sqlite3 *db = s->db;
    StoreError *serr = &s->last;

    if (read_user_version(db, &version, err, errlen, serr) != 0) {
        return -1;
    }
    if (version == SCHEMA_VERSION) {
        goto bind_identity; /* already current; foreign_keys stays ON */
    }
    if (version < 0 || version > SCHEMA_VERSION - 1) {
        return check_version(version, err, errlen);
    }
    if (exec_sql(db, "PRAGMA foreign_keys = OFF;", err, errlen, serr) != 0) {
        return -1;
    }
    rc = migrate_to_current(s, err, errlen);
    (void)exec_sql(db, "PRAGMA foreign_keys = ON;", NULL, 0, NULL);
    if (rc < 0) {
        return -1;
    }
    if (rc == 0) {
        return 0; /* migrated + committed; identity already bound */
    }

bind_identity:
    return bind_current_identity(s, err, errlen);
}

Store *store_open_opts(const char *path, const StoreOpenOpts *opts, char *err, size_t errlen,
                       StoreError *out_error)
{
    Store *s = NULL;
    sqlite3 *db = NULL;
    size_t plen = 0;
    int timeout_ms = STORE_DEFAULT_BUSY_TIMEOUT_MS;
    bool no_create = false;
    bool replace_invalid_sidecar = false;
    /* SQLITE_OPEN_* are sqlite's own signed-int flag macros; the API takes int. */
    // NOLINTNEXTLINE(hicpp-signed-bitwise)
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;

    if (out_error != NULL) {
        memset(out_error, 0, sizeof(*out_error));
    }
    if (path == NULL || path[0] == '\0') {
        set_err(err, errlen, "empty database path");
        return NULL;
    }
    if (opts != NULL) {
        timeout_ms = opts->busy_timeout_ms;
        no_create = opts->no_create;
        replace_invalid_sidecar = opts->replace_invalid_sidecar;
    }
    if (no_create) {
        /* D8: no folder, no file, no sidecar. mkdir is skipped entirely and
           the CREATE-less open below fails naturally with the real CANTOPEN
           codes (a missing path reports reason missing). */
        // NOLINTNEXTLINE(hicpp-signed-bitwise)
        flags = SQLITE_OPEN_READWRITE;
    } else if (ensure_parent_dirs(path, err, errlen) != 0) {
        return NULL;
    }

    s = (Store *)calloc(1, sizeof(*s));
    if (s == NULL) {
        set_err(err, errlen, "out of memory");
        return NULL;
    }
    plen = strlen(path);
    s->path = (char *)malloc(plen + 1U);
    if (s->path == NULL) {
        set_err(err, errlen, "out of memory");
        free(s);
        return NULL;
    }
    memcpy(s->path, path, plen + 1U);
    s->replace_invalid_sidecar = replace_invalid_sidecar;

    if (sqlite3_open_v2(path, &db, flags, NULL) != SQLITE_OK) {
        /* sqlite3_errmsg is NULL-safe: reports OOM when the handle is NULL. */
        capture_sqlite_error(db, &s->last);
        set_errf(err, errlen, "cannot open database", sqlite3_errmsg(db));
        goto cleanup_fail;
    }
    s->db = db;
    if (apply_pragmas(db, timeout_ms, err, errlen, &s->last) != 0) {
        goto cleanup_fail;
    }
    if (ensure_schema(s, err, errlen) != 0) {
        goto cleanup_fail;
    }

    if (err != NULL && errlen > 0U) {
        err[0] = '\0';
    }
    return s;

cleanup_fail:
    if (out_error != NULL) {
        *out_error = s->last;
    }
    (void)sqlite3_close_v2(db);
    free(s->path);
    free(s->device_id);
    free_open_warnings(s);
    free(s);
    return NULL;
}

Store *store_open(const char *path, char *err, size_t errlen)
{
    return store_open_opts(path, NULL, err, errlen, NULL);
}

const StoreError *store_last_error(const Store *s)
{
    if (s == NULL) {
        return NULL;
    }
    return &s->last;
}

const char *store_path(const Store *s)
{
    if (s == NULL) {
        return NULL;
    }
    return s->path;
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
    free(s->path);
    free(s->device_id);
    free_open_warnings(s);
    s->path = NULL;
    s->device_id = NULL;
    free(s);
}

size_t store_open_warning_count(const Store *s)
{
    if (s == NULL) {
        return 0U;
    }
    return s->nwarnings;
}

StoreSettingWarning store_open_warning(const Store *s, size_t i)
{
    if (s == NULL || i >= s->nwarnings) {
        return (StoreSettingWarning){0};
    }
    return (StoreSettingWarning){
        .key = k_prefixes_key, .value = s->warnings[i].value, .reason = s->warnings[i].reason};
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
    case STORE_ERR_CONFLICT:
        return "body hash conflict";
    case STORE_ERR_EXPIRED:
        return "expired";
    case STORE_ERR_NOT_EXPIRED:
        return "not_expired";
    case STORE_ERR_DELETED:
        return "deleted";
    case STORE_ERR_NOT_DELETED:
        return "not_deleted";
    case STORE_ERR_NOT_SINGLE_DEVICE:
        return "hard delete requires exactly one registered device";
    case STORE_ERR_SELF_LINK:
        return "self-link";
    case STORE_ERR_CYCLE:
        return "supersedes cycle";
    case STORE_ERR_SOURCE_TOO_OLD:
        return "source database is older than this remember";
    case STORE_ERR_SOURCE_TOO_NEW:
        return k_newer_database;
    case STORE_ERR_UNIQUE_TAKEN:
        return "cannot free unique key or body hash without inventing a key";
    case STORE_ERR_STALE:
        return "memory changed since it was read";
    case STORE_ERR_BAD_VV:
        return "invalid version vector";
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
        size_t i = 0;
        for (i = 0; i < e->ntags; i++) {
            free(e->tags[i]);
        }
        free((void *)e->tags);
    }
    free(e->source);
    free(e->created_at);
    free(e->updated_at);
    free(e->expires_at);
    free(e->sync_id);
    free(e->deleted_at);
    free(e->version_vector);
    e->key = NULL;
    e->body = NULL;
    e->tags = NULL;
    e->ntags = 0U;
    e->source = NULL;
    e->created_at = NULL;
    e->updated_at = NULL;
    e->expires_at = NULL;
    e->sync_id = NULL;
    e->deleted_at = NULL;
    e->version_vector = NULL;
    e->id = 0;
}

static char *dup_str(const char *s)
{
    size_t n = 0;
    char *p = NULL;

    if (s == NULL) {
        return NULL;
    }
    n = strlen(s);
    p = (char *)malloc(n + 1U);
    if (p == NULL) {
        return NULL;
    }
    memcpy(p, s, n + 1U);
    return p;
}

/* ISO-8601 UTC millisecond precision, e.g. 2026-07-24T12:00:00.123Z (24 chars +
 * NUL). The 3-digit fraction is fixed-width so lexicographic order equals
 * chronological order (list/search sort on these strings), and sub-second
 * resolution keeps updated_at monotonic across writes within one second.
 * timespec_get with TIME_UTC is ISO C11 (no POSIX feature macros); gmtime (not
 * gmtime_r) is fine for a single-threaded CLI. */
int utc_now(char *buf, size_t buflen)
{
    struct timespec ts;
    struct tm tm;
    size_t n = 0;
    int ms = 0;

    if (buf == NULL || buflen < (size_t)ISO_TS_MIN_BUFLEN) {
        return -1;
    }
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) {
        return -1;
    }
    /* gmtime_r: reentrant; gmtime uses a shared static buffer (concurrency-mt-unsafe). */
    if (gmtime_r(&ts.tv_sec, &tm) == NULL) {
        return -1;
    }
    n = strftime(buf, buflen, "%Y-%m-%dT%H:%M:%S", &tm);
    if (n == 0U) {
        return -1;
    }
    /* C11: timespec_get(TIME_UTC) yields tv_nsec in [0, 999999999], so
       nsec/1e6 is always in [0, 999] — no clamp branches (coverage-dead). */
    ms = (int)(ts.tv_nsec / NANOS_PER_MS);
    if (snprintf(buf + n, buflen - n, ".%03dZ", ms) != ISO_FRAC_SUFFIX_LEN) {
        return -1;
    }
    return 0;
}

/*
 * Load tags for entry_id into *out_tags / *out_ntags (heap). Sorted by name.
 * On failure leaves *out_tags NULL / *out_ntags 0.
 */
static StoreStatus load_tags(sqlite3 *db, long long entry_id, char ***out_tags, size_t *out_ntags,
                             StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    char **tags = NULL;
    size_t n = 0U;
    size_t cap = 0U;
    int rc = 0;

    *out_tags = NULL;
    *out_ntags = 0U;

    rc = sqlite3_prepare_v2(db,
                            "SELECT t.name FROM tags t "
                            "JOIN entry_tags et ON et.tag_id = t.id "
                            "WHERE et.entry_id = ?1 "
                            "ORDER BY t.name COLLATE BINARY;",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_int64(stmt, 1, entry_id);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 0);
        char *copy = NULL;
        char **grown = NULL;

        if (name == NULL) {
            continue;
        }
        copy = dup_str(name);
        if (copy == NULL) {
            size_t i = 0;
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
                    size_t i = 0;
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
    if (rc != SQLITE_DONE) {
        size_t i = 0;
        capture_sqlite_error(db, serr);
        for (i = 0; i < n; i++) {
            free(tags[i]);
        }
        free((void *)tags);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(stmt);
    *out_tags = tags;
    *out_ntags = n;
    return STORE_OK;
}

/* Fill *out from a SELECT of K_ENTRY_COLS: local_id, key, body, source (the
 * creating actor's kind), created_at, updated_at, expires_at, id (the UUID),
 * deleted_at, version_vector (key/expires_at/deleted_at may be NULL). */
static StoreStatus fill_entry_from_row(sqlite3 *db, sqlite3_stmt *stmt, Entry *out,
                                       StoreError *serr)
{
    /* Column order of the entry SELECT this row comes from. */
    enum {
        COL_ID = 0,
        COL_KEY = 1,
        COL_BODY = 2,
        COL_SOURCE = 3,
        COL_CREATED_AT = 4,
        COL_UPDATED_AT = 5,
        COL_EXPIRES_AT = 6,
        COL_SYNC_ID = 7,
        COL_DELETED_AT = 8,
        COL_VERSION_VECTOR = 9
    };
    StoreStatus st = STORE_OK;
    const unsigned char *key_u = NULL;
    const unsigned char *body_u = NULL;
    const unsigned char *source_u = NULL;
    const unsigned char *created_u = NULL;
    const unsigned char *updated_u = NULL;
    const unsigned char *expires_u = NULL;
    const unsigned char *sync_u = NULL;
    const unsigned char *deleted_u = NULL;
    const unsigned char *vv_u = NULL;

    memset(out, 0, sizeof(*out));
    out->id = sqlite3_column_int64(stmt, COL_ID);
    key_u = sqlite3_column_text(stmt, COL_KEY);
    body_u = sqlite3_column_text(stmt, COL_BODY);
    source_u = sqlite3_column_text(stmt, COL_SOURCE);
    created_u = sqlite3_column_text(stmt, COL_CREATED_AT);
    updated_u = sqlite3_column_text(stmt, COL_UPDATED_AT);
    expires_u = sqlite3_column_text(stmt, COL_EXPIRES_AT);
    sync_u = sqlite3_column_text(stmt, COL_SYNC_ID);
    deleted_u = sqlite3_column_text(stmt, COL_DELETED_AT);
    vv_u = sqlite3_column_text(stmt, COL_VERSION_VECTOR);

    if (body_u == NULL || source_u == NULL || created_u == NULL || updated_u == NULL ||
        sync_u == NULL || vv_u == NULL) {
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
    out->sync_id = dup_str((const char *)sync_u);
    out->version_vector = dup_str((const char *)vv_u);
    if (expires_u != NULL) {
        out->expires_at = dup_str((const char *)expires_u);
        if (out->expires_at == NULL) {
            store_entry_free(out);
            return STORE_ERR_OOM;
        }
    }
    if (deleted_u != NULL) {
        out->deleted_at = dup_str((const char *)deleted_u);
        if (out->deleted_at == NULL) {
            store_entry_free(out);
            return STORE_ERR_OOM;
        }
    }
    if (out->body == NULL || out->source == NULL || out->created_at == NULL ||
        out->updated_at == NULL || out->sync_id == NULL || out->version_vector == NULL) {
        store_entry_free(out);
        return STORE_ERR_OOM;
    }
    st = load_tags(db, out->id, &out->tags, &out->ntags, serr);
    if (st != STORE_OK) {
        store_entry_free(out);
        return st;
    }
    return STORE_OK;
}

static StoreStatus load_entry_by_id(sqlite3 *db, long long id, Entry *out, StoreError *serr);
static StoreStatus load_entry_by_key(sqlite3 *db, const char *key, Entry *out, StoreError *serr);
static StoreStatus load_entry_by_sync_id(sqlite3 *db, const char *sync_id, Entry *out,
                                         StoreError *serr);

/* Three timestamp strings; order is the bin contract (deleted, then expiry). */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
StoreBin store_bin_of(const char *deleted_at, const char *expires_at, const char *now)
{
    if (deleted_at != NULL) {
        return STORE_BIN_DELETED;
    }
    if (expires_at != NULL && now != NULL && strcmp(expires_at, now) <= 0) {
        return STORE_BIN_EXPIRED;
    }
    return STORE_BIN_LIVE;
}

static StoreBin entry_bin(const Entry *e, const char *now)
{
    return store_bin_of(e->deleted_at, e->expires_at, now);
}

/* Round 7 exit-3 matrix. Prefer the row's actual bin when two flags are wrong. */
static StoreStatus bin_status(const Entry *e, StoreBin want, const char *now)
{
    StoreBin have = STORE_BIN_LIVE;

    if (e == NULL || now == NULL) {
        return STORE_ERR_INTERNAL;
    }
    have = entry_bin(e, now);
    if (have == want) {
        return STORE_OK;
    }
    if (want == STORE_BIN_LIVE) {
        return (have == STORE_BIN_EXPIRED) ? STORE_ERR_EXPIRED : STORE_ERR_DELETED;
    }
    if (want == STORE_BIN_EXPIRED) {
        return (have == STORE_BIN_LIVE) ? STORE_ERR_NOT_EXPIRED : STORE_ERR_DELETED;
    }
    return (have == STORE_BIN_LIVE) ? STORE_ERR_NOT_DELETED : STORE_ERR_EXPIRED;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static StoreStatus load_then_check_bin(sqlite3 *db, long long id, const char *key_or_null,
                                       const char *sync_id_or_null, StoreBin bin, const char *now,
                                       Entry *out, StoreError *serr)
{
    StoreStatus st = STORE_OK;

    if (now == NULL || out == NULL) {
        return STORE_ERR_INTERNAL;
    }
    if (sync_id_or_null != NULL) {
        st = load_entry_by_sync_id(db, sync_id_or_null, out, serr);
    } else if (key_or_null != NULL) {
        st = load_entry_by_key(db, key_or_null, out, serr);
    } else {
        st = load_entry_by_id(db, id, out, serr);
    }
    if (st != STORE_OK) {
        return st;
    }
    st = bin_status(out, bin, now);
    if (st != STORE_OK) {
        store_entry_free(out);
    }
    return st;
}

static StoreStatus write_expires_at(sqlite3 *db, long long id, const char *expires_at,
                                    StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    int rc = 0;

    rc = sqlite3_prepare_v2(db, "UPDATE entries SET expires_at = ?1 WHERE local_id = ?2;", -1,
                            &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    if (expires_at == NULL) {
        (void)sqlite3_bind_null(stmt, 1);
    } else {
        (void)sqlite3_bind_text(stmt, 1, expires_at, -1, SQLITE_STATIC);
    }
    (void)sqlite3_bind_int64(stmt, 2, id);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(stmt);
    return STORE_OK;
}

static StoreStatus clear_deleted_at(sqlite3 *db, long long id, StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    int rc = 0;

    rc = sqlite3_prepare_v2(db, "UPDATE entries SET deleted_at = NULL WHERE local_id = ?1;", -1,
                            &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_int64(stmt, 1, id);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(stmt);
    return STORE_OK;
}

/* Increment version_vector[device_id] (missing key = 0). Compact JSON, no spaces. */
static int vv_increment(const char *in, const char *device_id, char *out, size_t outlen)
{
    char needle[VV_NEEDLE_LEN];
    const char *hit = NULL;
    int needle_n = 0;
    int written = 0;

    if (in == NULL || device_id == NULL || out == NULL || outlen < (size_t)VV_JSON_MIN_OUT ||
        in[0] != '{') {
        return -1;
    }
    needle_n = snprintf(needle, sizeof(needle), "\"%s\":", device_id);
    if (needle_n < 0 || (size_t)needle_n >= sizeof(needle)) {
        return -1;
    }
    hit = strstr(in, needle);
    if (hit != NULL) {
        const char *digits = hit + (size_t)needle_n;
        char *end = NULL;
        unsigned long long next_clock = 0;
        size_t pre = 0;

        errno = 0;
        next_clock = strtoull(digits, &end, VV_INT_BASE);
        if (end == digits || errno != 0 || next_clock == ULLONG_MAX ||
            next_clock >= ULLONG_MAX - 1ULL) {
            return -1;
        }
        next_clock++;
        pre = (size_t)(digits - in);
        written = snprintf(out, outlen, "%.*s%llu%s", (int)pre, in, next_clock, end);
        if (written < 0 || (size_t)written >= outlen) {
            return -1;
        }
        return 0;
    }
    {
        const char *brace = strrchr(in, '}');
        size_t len = strlen(in);

        if (brace == NULL || brace[1] != '\0') {
            return -1;
        }
        if (len <= 2U) {
            written = snprintf(out, outlen, "{\"%s\":1}", device_id);
        } else {
            written = snprintf(out, outlen, "%.*s,\"%s\":1}", (int)(brace - in), in, device_id);
        }
        if (written < 0 || (size_t)written >= outlen) {
            return -1;
        }
        return 0;
    }
}

static StoreStatus apply_vv_bump(sqlite3 *db, long long id, const char *device_id,
                                 const char *vv_in, StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    char vv_out[VV_OUT_MAX];
    int rc = 0;

    if (device_id == NULL || vv_in == NULL) {
        return STORE_ERR_INTERNAL;
    }
    if (vv_increment(vv_in, device_id, vv_out, sizeof(vv_out)) != 0) {
        return STORE_ERR_INTERNAL;
    }
    rc = sqlite3_prepare_v2(db, "UPDATE entries SET version_vector = ?1 WHERE local_id = ?2;", -1,
                            &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_text(stmt, 1, vv_out, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_int64(stmt, 2, id);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(stmt);
    return STORE_OK;
}

static StoreStatus load_entry_by_id(sqlite3 *db, long long id, Entry *out, StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    int rc = 0;
    StoreStatus st = STORE_OK;

    rc = sqlite3_prepare_v2(db,
                            "SELECT " K_ENTRY_COLS " "
                            "FROM entries WHERE local_id = ?1;",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_int64(stmt, 1, id);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_NOT_FOUND;
    }
    if (rc != SQLITE_ROW) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    st = fill_entry_from_row(db, stmt, out, serr);
    (void)sqlite3_finalize(stmt);
    return st;
}

static StoreStatus load_entry_by_key(sqlite3 *db, const char *key, Entry *out, StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    int rc = 0;
    StoreStatus st = STORE_OK;

    if (key == NULL) {
        return STORE_ERR_INTERNAL;
    }
    rc = sqlite3_prepare_v2(db,
                            "SELECT " K_ENTRY_COLS " "
                            "FROM entries WHERE key = ?1;",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_NOT_FOUND;
    }
    if (rc != SQLITE_ROW) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    st = fill_entry_from_row(db, stmt, out, serr);
    (void)sqlite3_finalize(stmt);
    return st;
}

static StoreStatus load_entry_by_sync_id(sqlite3 *db, const char *sync_id, Entry *out,
                                         StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    int rc = 0;
    StoreStatus st = STORE_OK;

    if (sync_id == NULL) {
        return STORE_ERR_INTERNAL;
    }
    rc = sqlite3_prepare_v2(db,
                            "SELECT " K_ENTRY_COLS " "
                            "FROM entries WHERE id = ?1;",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_text(stmt, 1, sync_id, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_NOT_FOUND;
    }
    if (rc != SQLITE_ROW) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    st = fill_entry_from_row(db, stmt, out, serr);
    (void)sqlite3_finalize(stmt);
    return st;
}

/* Space-joined tag names for FTS (heap; caller frees). Empty tags → "". */
static char *join_tags_space(const char *const *tags, size_t ntags)
{
    size_t i = 0;
    size_t total = 0U;
    char *buf = NULL;
    size_t pos = 0U;

    if (ntags == 0U || tags == NULL) {
        buf = (char *)malloc(1U);
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
        buf = (char *)malloc(1U);
        if (buf != NULL) {
            buf[0] = '\0';
        }
        return buf;
    }
    buf = (char *)malloc(total);
    if (buf == NULL) {
        return NULL;
    }
    for (i = 0; i < ntags; i++) {
        size_t len = 0;
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

static StoreStatus fold_fts_field(const char *src, char **out)
{
    const char *text = (src != NULL) ? src : "";
    NormStatus ns = fold_hebrew_marks(text, strlen(text), out);

    /* NULL out/src are not passed here, so the only failure is OOM. */
    if (ns == NORM_OK) {
        return STORE_OK;
    }
    return STORE_ERR_OOM;
}

/* Fold key, body, and tags, then replace the FTS row. key may be NULL. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static StoreStatus write_folded_fts(sqlite3 *db, long long entry_id, const char *key,
                                    const char *body, const char *tags_text, StoreError *serr)
{
    sqlite3_stmt *del = NULL;
    sqlite3_stmt *ins = NULL;
    char *key_fold = NULL;
    char *body_fold = NULL;
    char *tags_fold = NULL;
    StoreStatus st = STORE_ERR_SQLITE;
    int rc = 0;

    st = fold_fts_field(key, &key_fold);
    if (st != STORE_OK) {
        goto cleanup;
    }
    st = fold_fts_field(body, &body_fold);
    if (st != STORE_OK) {
        goto cleanup;
    }
    st = fold_fts_field(tags_text, &tags_fold);
    if (st != STORE_OK) {
        goto cleanup;
    }
    st = STORE_ERR_SQLITE;

    rc = sqlite3_prepare_v2(db, "DELETE FROM entries_fts WHERE rowid = ?1;", -1, &del, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        goto cleanup;
    }
    (void)sqlite3_bind_int64(del, 1, entry_id);
    if (sqlite3_step(del) != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        goto cleanup;
    }

    rc = sqlite3_prepare_v2(
        db, "INSERT INTO entries_fts(rowid, key, body, tags) VALUES (?1, ?2, ?3, ?4);", -1, &ins,
        NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        goto cleanup;
    }
    (void)sqlite3_bind_int64(ins, 1, entry_id);
    /* NOLINTNEXTLINE(performance-no-int-to-ptr) -- SQLITE_TRANSIENT is (destructor)-1 */
    (void)sqlite3_bind_text(ins, 2, key_fold, -1, SQLITE_TRANSIENT);
    /* NOLINTNEXTLINE(performance-no-int-to-ptr) -- SQLITE_TRANSIENT is (destructor)-1 */
    (void)sqlite3_bind_text(ins, 3, body_fold, -1, SQLITE_TRANSIENT);
    /* NOLINTNEXTLINE(performance-no-int-to-ptr) -- SQLITE_TRANSIENT is (destructor)-1 */
    (void)sqlite3_bind_text(ins, 4, tags_fold, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(ins) != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
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
    free(key_fold);
    free(body_fold);
    free(tags_fold);
    return st;
}

/* Rewrite FTS row for entry_id from current entries + entry_tags. */
static StoreStatus fts_resync(sqlite3 *db, long long entry_id, StoreError *serr)
{
    sqlite3_stmt *sel = NULL;
    char **tags = NULL;
    size_t ntags = 0U;
    char *tags_text = NULL;
    const unsigned char *body_u = NULL;
    StoreStatus st = STORE_ERR_SQLITE;
    int rc = 0;

    rc = sqlite3_prepare_v2(db, "SELECT key, body FROM entries WHERE local_id = ?1;", -1, &sel,
                            NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_int64(sel, 1, entry_id);
    rc = sqlite3_step(sel);
    if (rc != SQLITE_ROW) {
        if (rc != SQLITE_DONE) {
            capture_sqlite_error(db, serr);
        }
        (void)sqlite3_finalize(sel);
        return (rc == SQLITE_DONE) ? STORE_ERR_NOT_FOUND : STORE_ERR_SQLITE;
    }
    body_u = sqlite3_column_text(sel, 1);
    if (body_u == NULL) {
        (void)sqlite3_finalize(sel);
        return STORE_ERR_SQLITE;
    }

    st = load_tags(db, entry_id, &tags, &ntags, serr);
    if (st != STORE_OK) {
        (void)sqlite3_finalize(sel);
        return st;
    }
    tags_text = join_tags_space((const char *const *)tags, ntags);
    if (tags_text == NULL) {
        st = STORE_ERR_OOM;
        goto cleanup;
    }
    st = write_folded_fts(db, entry_id, (const char *)sqlite3_column_text(sel, 0),
                          (const char *)body_u, tags_text, serr);

cleanup:
    (void)sqlite3_finalize(sel);
    {
        size_t i = 0;
        for (i = 0; i < ntags; i++) {
            free(tags[i]);
        }
    }
    free((void *)tags);
    free(tags_text);
    return st;
}

/* Ensure tag name exists; return its id in *out_tag_id. */
static StoreStatus ensure_tag(sqlite3 *db, const char *name, long long *out_tag_id,
                              StoreError *serr)
{
    sqlite3_stmt *ins = NULL;
    sqlite3_stmt *sel = NULL;
    int rc = 0;

    rc = sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO tags(name) VALUES (?1);", -1, &ins, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_text(ins, 1, name, -1, SQLITE_STATIC);
    if (sqlite3_step(ins) != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(ins);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(ins);

    rc = sqlite3_prepare_v2(db, "SELECT id FROM tags WHERE name = ?1;", -1, &sel, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_text(sel, 1, name, -1, SQLITE_STATIC);
    rc = sqlite3_step(sel);
    if (rc != SQLITE_ROW) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(sel);
        return STORE_ERR_SQLITE;
    }
    *out_tag_id = sqlite3_column_int64(sel, 0);
    (void)sqlite3_finalize(sel);
    return STORE_OK;
}

static StoreStatus link_tag(sqlite3 *db, long long entry_id, long long tag_id, StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    int rc = 0;

    rc = sqlite3_prepare_v2(
        db, "INSERT OR IGNORE INTO entry_tags(entry_id, tag_id) VALUES (?1, ?2);", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_int64(stmt, 1, entry_id);
    (void)sqlite3_bind_int64(stmt, 2, tag_id);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(stmt);
    return STORE_OK;
}

static StoreStatus union_tags(sqlite3 *db, long long entry_id, const char *const *tags,
                              size_t ntags, StoreError *serr)
{
    size_t i = 0;

    if (tags == NULL || ntags == 0U) {
        return STORE_OK;
    }
    for (i = 0; i < ntags; i++) {
        long long tag_id = 0;
        StoreStatus st = STORE_OK;

        if (tags[i] == NULL) {
            continue;
        }
        st = ensure_tag(db, tags[i], &tag_id, serr);
        if (st != STORE_OK) {
            return st;
        }
        st = link_tag(db, entry_id, tag_id, serr);
        if (st != STORE_OK) {
            return st;
        }
    }
    return STORE_OK;
}

static StoreStatus find_keyless_by_hash(sqlite3 *db, const char *body_hash, long long *out_id,
                                        StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    int rc = 0;

    rc = sqlite3_prepare_v2(
        db, "SELECT local_id FROM entries WHERE body_hash = ?1 AND key IS NULL;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_text(stmt, 1, body_hash, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_NOT_FOUND;
    }
    if (rc != SQLITE_ROW) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    *out_id = sqlite3_column_int64(stmt, 0);
    (void)sqlite3_finalize(stmt);
    return STORE_OK;
}

static StoreStatus find_id_by_key(sqlite3 *db, const char *key, long long *out_id, StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    int rc = 0;

    rc = sqlite3_prepare_v2(db, "SELECT local_id FROM entries WHERE key = ?1;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_NOT_FOUND;
    }
    if (rc != SQLITE_ROW) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    *out_id = sqlite3_column_int64(stmt, 0);
    (void)sqlite3_finalize(stmt);
    return STORE_OK;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
static StoreStatus insert_entry(sqlite3 *db, const char *body, const char *body_hash,
                                const char *key_or_null, const char *source, const char *now,
                                const char *expires_at, const char *device_id, long long *out_id,
                                StoreError *serr)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    /* ?N bind positions of the INSERT below. */
    enum {
        BIND_KEY = 1,
        BIND_BODY = 2,
        BIND_BODY_HASH = 3,
        BIND_SOURCE = 4,
        BIND_CREATED_AT = 5,
        BIND_UPDATED_AT = 6,
        BIND_EXPIRES_AT = 7,
        BIND_SYNC_ID = 8,
        BIND_VV = 9
    };
    sqlite3_stmt *stmt = NULL;
    int rc = 0;
    char uuid[UUID_STR_LEN + 1];
    char vv_json[VV_JSON_BUFLEN];

    if (device_id == NULL) {
        return STORE_ERR_INTERNAL;
    }
    if (mint_uuid_v7(uuid) != 0) {
        return STORE_ERR_INTERNAL;
    }
    (void)snprintf(vv_json, sizeof(vv_json), "{\"%s\":1}", device_id);

    rc = sqlite3_prepare_v2(db,
                            "INSERT INTO entries(key, body, body_hash, created_by, updated_by, "
                            "created_at, updated_at, expires_at, id, version_vector, change_seq) "
                            "VALUES (?1, ?2, ?3, " K_ACTOR_FROM_SOURCE(
                                "?4") ", " K_ACTOR_FROM_SOURCE("?4") ", ?5, ?6, ?7, ?8, ?9, 0);",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    if (key_or_null == NULL) {
        (void)sqlite3_bind_null(stmt, BIND_KEY);
    } else {
        (void)sqlite3_bind_text(stmt, BIND_KEY, key_or_null, -1, SQLITE_STATIC);
    }
    (void)sqlite3_bind_text(stmt, BIND_BODY, body, -1, SQLITE_STATIC);
    (void)sqlite3_bind_text(stmt, BIND_BODY_HASH, body_hash, -1, SQLITE_STATIC);
    (void)sqlite3_bind_text(stmt, BIND_SOURCE, source, -1, SQLITE_STATIC);
    (void)sqlite3_bind_text(stmt, BIND_CREATED_AT, now, -1, SQLITE_STATIC);
    (void)sqlite3_bind_text(stmt, BIND_UPDATED_AT, now, -1, SQLITE_STATIC);
    if (expires_at == NULL) {
        (void)sqlite3_bind_null(stmt, BIND_EXPIRES_AT);
    } else {
        (void)sqlite3_bind_text(stmt, BIND_EXPIRES_AT, expires_at, -1, SQLITE_STATIC);
    }
    (void)sqlite3_bind_text(stmt, BIND_SYNC_ID, uuid, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(stmt, BIND_VV, vv_json, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    *out_id = sqlite3_last_insert_rowid(db);
    (void)sqlite3_finalize(stmt);
    return STORE_OK;
}

static StoreStatus touch_updated_at(sqlite3 *db, long long id, const char *now, StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    int rc = 0;

    rc = sqlite3_prepare_v2(db, "UPDATE entries SET updated_at = ?1 WHERE local_id = ?2;", -1,
                            &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_text(stmt, 1, now, -1, SQLITE_STATIC);
    (void)sqlite3_bind_int64(stmt, 2, id);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(stmt);
    return STORE_OK;
}

static StoreStatus replace_body(sqlite3 *db, long long id, const char *body, const char *body_hash,
                                const char *now, StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    int rc = 0;

    rc = sqlite3_prepare_v2(
        db, "UPDATE entries SET body = ?1, body_hash = ?2, updated_at = ?3 WHERE local_id = ?4;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_text(stmt, 1, body, -1, SQLITE_STATIC);
    (void)sqlite3_bind_text(stmt, 2, body_hash, -1, SQLITE_STATIC);
    (void)sqlite3_bind_text(stmt, 3, now, -1, SQLITE_STATIC);
    (void)sqlite3_bind_int64(stmt, 4, id);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(stmt);
    return STORE_OK;
}

/* If the existing row is expired or deleted, restore to live (incoming expiry or none). */
static StoreStatus maybe_revive(sqlite3 *db, long long id, EntryTimes times, const char *device_id,
                                StoreError *serr)
{
    Entry current;
    StoreStatus st = STORE_OK;
    StoreBin have = STORE_BIN_LIVE;

    memset(&current, 0, sizeof(current));
    st = load_entry_by_id(db, id, &current, serr);
    if (st != STORE_OK) {
        return st;
    }
    have = entry_bin(&current, times.now);
    if (have == STORE_BIN_LIVE) {
        store_entry_free(&current);
        return STORE_OK;
    }
    st = write_expires_at(db, id, times.expires_at, serr);
    if (st != STORE_OK) {
        goto done;
    }
    if (have == STORE_BIN_DELETED) {
        st = clear_deleted_at(db, id, serr);
        if (st != STORE_OK) {
            goto done;
        }
    }
    st = apply_vv_bump(db, id, device_id, current.version_vector, serr);

done:
    store_entry_free(&current);
    return st;
}

/* What an add onto an existing row already wrote, so the stamp does not bump twice. */
typedef struct {
    bool updated_at_written; /* replace_body already set updated_at */
    bool revived;            /* maybe_revive already bumped the version vector */
    bool tags_added;
} ExistingAddChange;

/* Bump updated_at and version_vector[local] together, and only when the row
   changed. A no-op add leaves both where they were. now and device_id are
   different strings; the suppression is the same one insert_entry uses. */
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
static StoreStatus stamp_existing_add(sqlite3 *db, long long id, const char *now,
                                      const char *device_id, const char *vv_before,
                                      ExistingAddChange change, StoreError *serr)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    if (!change.updated_at_written && !change.revived && !change.tags_added) {
        return STORE_OK;
    }
    if (!change.updated_at_written) {
        StoreStatus st = touch_updated_at(db, id, now, serr);

        if (st != STORE_OK) {
            return st;
        }
    }
    if (change.revived) {
        return STORE_OK;
    }
    return apply_vv_bump(db, id, device_id, vv_before, serr);
}

typedef enum { REUSE_KEEP_BODY = 0, REUSE_REPLACE_CHANGED_BODY } ReuseBody;

/* Keyed passes REUSE_REPLACE_CHANGED_BODY so a different body counts. Keyless
   identity is the hash, so it passes REUSE_KEEP_BODY and does not write the body. */
static StoreStatus reuse_existing_row(sqlite3 *db, long long id, const char *body,
                                      const char *body_hash, const char *const *tags, size_t ntags,
                                      const char *now, const char *expires_at,
                                      const char *device_id, ReuseBody body_policy,
                                      StoreError *serr)
{
    Entry current;
    ExistingAddChange change = {false, false, false};
    char vv_before[VV_OUT_MAX];
    StoreStatus st = STORE_OK;
    int vv_n = 0;
    int changes_before = 0;

    memset(&current, 0, sizeof(current));
    memset(vv_before, 0, sizeof(vv_before));
    st = load_entry_by_id(db, id, &current, serr);
    if (st != STORE_OK) {
        return st;
    }
    switch (body_policy) {
    case REUSE_REPLACE_CHANGED_BODY:
        if (current.body == NULL || strcmp(current.body, body) != 0) {
            change.updated_at_written = true;
        }
        break;
    case REUSE_KEEP_BODY:
        break;
    default:
        store_entry_free(&current);
        return STORE_ERR_INTERNAL;
    }
    change.revived = (entry_bin(&current, now) != STORE_BIN_LIVE);
    if (current.version_vector != NULL) {
        vv_n = snprintf(vv_before, sizeof(vv_before), "%s", current.version_vector);
    }
    store_entry_free(&current);
    if (vv_n < 0 || (size_t)vv_n >= sizeof(vv_before) || vv_before[0] == '\0') {
        return STORE_ERR_INTERNAL;
    }
    if (change.updated_at_written) {
        st = replace_body(db, id, body, body_hash, now, serr);
        if (st != STORE_OK) {
            return st;
        }
    }
    st = maybe_revive(db, id, (EntryTimes){.now = now, .expires_at = expires_at}, device_id, serr);
    if (st != STORE_OK) {
        return st;
    }
    changes_before = sqlite3_total_changes(db);
    st = union_tags(db, id, tags, ntags, serr);
    if (st != STORE_OK) {
        return st;
    }
    change.tags_added = (sqlite3_total_changes(db) != changes_before);
    return stamp_existing_add(db, id, now, device_id, vv_before, change, serr);
}

/* Keyed upsert: replace body + union tags, or insert. Sets out_id and out_action. */
static StoreStatus add_keyed(sqlite3 *db, const char *body, const char *body_hash, const char *key,
                             const char *const *tags, size_t ntags, const char *source,
                             const char *now, const char *expires_at, const char *device_id,
                             long long *out_id, StoreAddAction *out_action, StoreError *serr)
{
    StoreStatus st = STORE_OK;
    long long id = 0;

    st = find_id_by_key(db, key, &id, serr);
    if (st == STORE_OK) {
        st = reuse_existing_row(db, id, body, body_hash, tags, ntags, now, expires_at, device_id,
                                REUSE_REPLACE_CHANGED_BODY, serr);
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
    st = insert_entry(db, body, body_hash, key, source, now, expires_at, device_id, &id, serr);
    if (st != STORE_OK) {
        return st;
    }
    st = union_tags(db, id, tags, ntags, serr);
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
                               const char *now, const char *expires_at, const char *device_id,
                               long long *out_id, StoreAddAction *out_action, StoreError *serr)
{
    StoreStatus st = STORE_OK;
    long long id = 0;

    st = find_keyless_by_hash(db, body_hash, &id, serr);
    if (st == STORE_OK) {
        st = reuse_existing_row(db, id, body, body_hash, tags, ntags, now, expires_at, device_id,
                                REUSE_KEEP_BODY, serr);
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
    st = insert_entry(db, body, body_hash, NULL, source, now, expires_at, device_id, &id, serr);
    if (st != STORE_OK) {
        return st;
    }
    st = union_tags(db, id, tags, ntags, serr);
    if (st != STORE_OK) {
        return st;
    }
    *out_id = id;
    *out_action = STORE_ADD_CREATED;
    return STORE_OK;
}

StoreStatus store_add(Store *s, const char *body, const char *body_hash, const char *key_or_null,
                      const char *const *tags, size_t ntags, const char *source,
                      const char *expires_at, const char *now, StoreAddAction *out_action,
                      Entry *out_entry)
{
    long long id = 0;
    StoreAddAction action = STORE_ADD_CREATED;
    StoreStatus st = STORE_OK;
    char err_unused[1];

    if (s == NULL || s->db == NULL || s->device_id == NULL || body == NULL || body_hash == NULL ||
        source == NULL || now == NULL || out_action == NULL || out_entry == NULL) {
        return STORE_ERR_INTERNAL;
    }
    if (ntags > 0U && tags == NULL) {
        return STORE_ERR_INTERNAL;
    }
    memset(&s->last, 0, sizeof(s->last));
    if (exec_sql(s->db, "BEGIN IMMEDIATE;", err_unused, 0U, &s->last) != 0) {
        return STORE_ERR_SQLITE;
    }

    if (key_or_null != NULL) {
        st = add_keyed(s->db, body, body_hash, key_or_null, tags, ntags, source, now, expires_at,
                       s->device_id, &id, &action, &s->last);
    } else {
        st = add_keyless(s->db, body, body_hash, tags, ntags, source, now, expires_at, s->device_id,
                         &id, &action, &s->last);
    }
    if (st != STORE_OK) {
        goto fail;
    }

    st = fts_resync(s->db, id, &s->last);
    if (st != STORE_OK) {
        goto fail;
    }
    if (exec_sql(s->db, "COMMIT;", err_unused, 0U, &s->last) != 0) {
        st = STORE_ERR_SQLITE;
        goto fail;
    }

    /* The write is already durable past COMMIT; a failure here (e.g. OOM while
       snapshotting the row for output) reports error but does not lose data. */
    st = load_entry_by_id(s->db, id, out_entry, &s->last);
    if (st != STORE_OK) {
        return st;
    }
    *out_action = action;
    return STORE_OK;

fail:
    rollback_quiet(s->db);
    return st;
}

StoreStatus store_get(Store *s, long long id, StoreBin bin, const char *now, Entry *out_entry)
{
    if (s == NULL || s->db == NULL || out_entry == NULL) {
        return STORE_ERR_INTERNAL;
    }
    memset(&s->last, 0, sizeof(s->last));
    return load_then_check_bin(s->db, id, NULL, NULL, bin, now, out_entry, &s->last);
}

StoreStatus store_get_by_key(Store *s, const char *key, StoreBin bin, const char *now,
                             Entry *out_entry)
{
    if (s == NULL || s->db == NULL || key == NULL || out_entry == NULL) {
        return STORE_ERR_INTERNAL;
    }
    memset(&s->last, 0, sizeof(s->last));
    return load_then_check_bin(s->db, 0, key, NULL, bin, now, out_entry, &s->last);
}

StoreStatus store_get_by_sync_id(Store *s, const char *sync_id, StoreBin bin, const char *now,
                                 Entry *out_entry)
{
    if (s == NULL || s->db == NULL || sync_id == NULL || out_entry == NULL) {
        return STORE_ERR_INTERNAL;
    }
    memset(&s->last, 0, sizeof(s->last));
    return load_then_check_bin(s->db, 0, NULL, sync_id, bin, now, out_entry, &s->last);
}

void store_tags_free(TagCount *tags, size_t count)
{
    size_t i = 0;

    if (tags == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        free(tags[i].name);
    }
    free(tags);
}

StoreStatus store_tags(Store *s, StoreBin bin, const char *now, TagCount **out_tags,
                       size_t *out_count)
{
    sqlite3_stmt *stmt = NULL;
    TagCount *rows = NULL;
    size_t n = 0U;
    size_t cap = 0U;
    int rc = 0;
    const char *sql = NULL;

    if (s == NULL || s->db == NULL || now == NULL || out_tags == NULL || out_count == NULL) {
        return STORE_ERR_INTERNAL;
    }
    *out_tags = NULL;
    *out_count = 0U;

    /* INNER JOIN: a tag with no links (never expected — orphans are GC'd) is
       simply absent, which is the desired "only tags in use" result. Bin filter
       is on entries so counts match list/search. */
    if (bin == STORE_BIN_EXPIRED) {
        sql = "SELECT t.name, COUNT(et.entry_id) FROM tags t "
              "JOIN entry_tags et ON et.tag_id = t.id "
              "JOIN entries e ON e.local_id = et.entry_id "
              "WHERE e.deleted_at IS NULL AND e.expires_at IS NOT NULL AND e.expires_at <= ?1 "
              "GROUP BY t.id ORDER BY t.name COLLATE BINARY;";
    } else if (bin == STORE_BIN_DELETED) {
        sql = "SELECT t.name, COUNT(et.entry_id) FROM tags t "
              "JOIN entry_tags et ON et.tag_id = t.id "
              "JOIN entries e ON e.local_id = et.entry_id "
              "WHERE e.deleted_at IS NOT NULL "
              "GROUP BY t.id ORDER BY t.name COLLATE BINARY;";
    } else {
        sql = "SELECT t.name, COUNT(et.entry_id) FROM tags t "
              "JOIN entry_tags et ON et.tag_id = t.id "
              "JOIN entries e ON e.local_id = et.entry_id "
              "WHERE e.deleted_at IS NULL AND (e.expires_at IS NULL OR e.expires_at > ?1) "
              "GROUP BY t.id ORDER BY t.name COLLATE BINARY;";
    }
    memset(&s->last, 0, sizeof(s->last));
    rc = sqlite3_prepare_v2(s->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(s->db, &s->last);
        return STORE_ERR_SQLITE;
    }
    if (bin != STORE_BIN_DELETED) {
        (void)sqlite3_bind_text(stmt, 1, now, -1, SQLITE_STATIC);
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 0);
        char *copy = NULL;

        if (name == NULL) {
            continue;
        }
        copy = dup_str(name);
        if (copy == NULL) {
            store_tags_free(rows, n);
            (void)sqlite3_finalize(stmt);
            return STORE_ERR_OOM;
        }
        if (n == cap) {
            size_t ncap = (cap == 0U) ? (size_t)GROW_MIN_CAP : cap * 2U;
            TagCount *grown = (TagCount *)realloc((void *)rows, ncap * sizeof(*grown));
            if (grown == NULL) {
                free(copy);
                store_tags_free(rows, n);
                (void)sqlite3_finalize(stmt);
                return STORE_ERR_OOM;
            }
            rows = grown;
            cap = ncap;
        }
        rows[n].name = copy;
        rows[n].count = sqlite3_column_int64(stmt, 1);
        n++;
    }
    if (rc != SQLITE_DONE) {
        capture_sqlite_error(s->db, &s->last);
        (void)sqlite3_finalize(stmt);
        store_tags_free(rows, n);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(stmt);
    *out_tags = rows;
    *out_count = n;
    return STORE_OK;
}

static void free_entry_rows(Entry *rows, size_t n)
{
    size_t i = 0;
    for (i = 0; i < n; i++) {
        store_entry_free(&rows[i]);
    }
    free(rows);
}

static StoreStatus list_append_row(sqlite3 *db, sqlite3_stmt *sel, Entry **rows, size_t *n,
                                   size_t *cap, StoreError *serr)
{
    Entry e;
    StoreStatus st = fill_entry_from_row(db, sel, &e, serr);
    Entry *grown = NULL;

    if (st != STORE_OK) {
        return st;
    }
    if (*n == *cap) {
        size_t ncap = (*cap == 0U) ? (size_t)GROW_MIN_CAP : (*cap * 2U);
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
                          const char **bind_text, size_t bind_cap, SqlEq clause)
{
    int n = 0;

    if (clause.value == NULL) {
        return 0;
    }
    if ((size_t)*nbinds >= bind_cap) {
        return -1;
    }
    n = snprintf(sql + *pos, sql_cap - *pos, " AND e.%s = ?%d", clause.col, *nbinds + 1);
    if (n < 0 || (size_t)n >= sql_cap - *pos) {
        return -1;
    }
    *pos += (size_t)n;
    bind_text[(*nbinds)++] = clause.value;
    return 0;
}

static int list_append_tag_exists(char *sql, size_t sql_cap, size_t *pos, int *nbinds,
                                  const char **bind_text, size_t bind_cap, const char *tag)
{
    int n = 0;

    if (tag == NULL) {
        return 0;
    }
    if ((size_t)*nbinds >= bind_cap) {
        return -1;
    }
    n = snprintf(sql + *pos, sql_cap - *pos,
                 " AND EXISTS (SELECT 1 FROM entry_tags et"
                 " JOIN tags tg ON tg.id = et.tag_id"
                 " WHERE et.entry_id = e.local_id AND tg.name = ?%d)",
                 *nbinds + 1);
    if (n < 0 || (size_t)n >= sql_cap - *pos) {
        return -1;
    }
    *pos += (size_t)n;
    bind_text[(*nbinds)++] = tag;
    return 0;
}

/*
 * Append shared list/search filter ANDs (source, key, tag EXISTS) at *pos / *nbinds.
 * Tags are AND'd via EXISTS subqueries (one per tag). Returns -1 if truncated.
 */
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
static int list_append_bin(char *sql, size_t sql_cap, size_t *pos, int *nbinds,
                           const char **bind_text, size_t bind_cap, StoreBin bin, const char *now)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    int n = 0;

    if (bin == STORE_BIN_DELETED) {
        n = snprintf(sql + *pos, sql_cap - *pos, " AND e.deleted_at IS NOT NULL");
        if (n < 0 || (size_t)n >= sql_cap - *pos) {
            return -1;
        }
        *pos += (size_t)n;
        return 0;
    }
    if (now == NULL) {
        return -1;
    }
    if ((size_t)*nbinds >= bind_cap) {
        return -1;
    }
    if (bin == STORE_BIN_EXPIRED) {
        n = snprintf(
            sql + *pos, sql_cap - *pos,
            " AND e.deleted_at IS NULL AND e.expires_at IS NOT NULL AND e.expires_at <= ?%d",
            *nbinds + 1);
    } else {
        n = snprintf(sql + *pos, sql_cap - *pos,
                     " AND e.deleted_at IS NULL AND (e.expires_at IS NULL OR e.expires_at > ?%d)",
                     *nbinds + 1);
    }
    if (n < 0 || (size_t)n >= sql_cap - *pos) {
        return -1;
    }
    *pos += (size_t)n;
    bind_text[(*nbinds)++] = now;
    return 0;
}

/*
 * The only map from a list sort to a column name. List order and list
 * since/until both use it. Search ignores ListQuery.sort and always uses
 * updated_at (E5.3).
 * NULL is an unknown sort (programmer error).
 */
static const char *list_sort_column(StoreSort sort)
{
    switch (sort) {
    case STORE_SORT_UPDATED:
        return "updated_at";
    case STORE_SORT_CREATED:
        return "created_at";
    default:
        return NULL;
    }
}

/* One bound. inclusive is >= (since); the other side is < (until). NULL skips.
   column is a fixed name (list_sort_column, or updated_at for search). The
   timestamp is bound, never pasted. */
static int list_append_cmp(char *sql, size_t sql_cap, size_t *pos, int *nbinds,
                           const char **bind_text, size_t bind_cap, const char *column,
                           bool inclusive, const char *value)
{
    int n = 0;

    if (value == NULL) {
        return 0;
    }
    if ((size_t)*nbinds >= bind_cap) {
        return -1;
    }
    if (inclusive) {
        n = snprintf(sql + *pos, sql_cap - *pos, " AND e.%s >= ?%d", column, *nbinds + 1);
    } else {
        n = snprintf(sql + *pos, sql_cap - *pos, " AND e.%s < ?%d", column, *nbinds + 1);
    }
    if (n < 0 || (size_t)n >= sql_cap - *pos) {
        return -1;
    }
    *pos += (size_t)n;
    bind_text[(*nbinds)++] = value;
    return 0;
}

static int list_append_filters(const ListQuery *q, char *sql, size_t sql_cap, size_t *pos,
                               int *nbinds, const char **bind_text, size_t bind_cap,
                               const char *time_column)
{
    size_t t = 0;

    if (q == NULL || sql == NULL || pos == NULL || nbinds == NULL || bind_text == NULL) {
        return -1;
    }
    if (list_append_eq(sql, sql_cap, pos, nbinds, bind_text, bind_cap,
                       (SqlEq){.col = K_SOURCE_COL, .value = q->source}) != 0 ||
        list_append_eq(sql, sql_cap, pos, nbinds, bind_text, bind_cap,
                       (SqlEq){.col = "key", .value = q->key}) != 0) {
        return -1;
    }
    for (t = 0; t < q->ntags; t++) {
        const char *tag = (q->tags != NULL) ? q->tags[t] : NULL;
        if (list_append_tag_exists(sql, sql_cap, pos, nbinds, bind_text, bind_cap, tag) != 0) {
            return -1;
        }
    }
    if (list_append_cmp(sql, sql_cap, pos, nbinds, bind_text, bind_cap, time_column, true,
                        q->since) != 0) {
        return -1;
    }
    return list_append_cmp(sql, sql_cap, pos, nbinds, bind_text, bind_cap, time_column, false,
                           q->until);
}

/*
 * Build list WHERE clause fragments and bind params.
 * sql_out must be large enough (caller-sized); returns -1 if truncated.
 */
static int list_build_where(const ListQuery *q, const char *now, const char *time_column, char *sql,
                            size_t sql_cap, int *out_nbinds, const char **bind_text,
                            size_t bind_cap)
{
    size_t pos = 0U;
    int nbinds = 0;
    int n = 0;

    if (q == NULL || now == NULL || time_column == NULL || sql == NULL || sql_cap == 0U ||
        out_nbinds == NULL || bind_text == NULL) {
        return -1;
    }
    n = snprintf(sql, sql_cap, " WHERE 1=1");
    if (n < 0 || (size_t)n >= sql_cap) {
        return -1;
    }
    pos = (size_t)n;

    if (list_append_filters(q, sql, sql_cap, &pos, &nbinds, bind_text, bind_cap, time_column) !=
        0) {
        return -1;
    }
    if (list_append_bin(sql, sql_cap, &pos, &nbinds, bind_text, bind_cap, q->bin, now) != 0) {
        return -1;
    }
    *out_nbinds = nbinds;
    return 0;
}

/*
 * Search WHERE: FTS MATCH on entries_fts (table name required: FTS5 rejects
 * aliases for MATCH/bm25), then the same entry filters as list. Slot 1 is q
 * (the ORDER BY reads it), slot 2 the MATCH text; filters continue from 3.
 * The COUNT statement does not read ?1, but binding it is still in range
 * because ?2 makes the parameter count at least 2.
 */
typedef struct {
    const char *q;     /* ?1 */
    const char *match; /* ?2 */
} SearchBinds;

static int search_build_where(const ListQuery *f, SearchBinds b, const char *now, char *sql,
                              size_t sql_cap, int *out_nbinds, const char **bind_text,
                              size_t bind_cap)
{
    size_t pos = 0U;
    int nbinds = 0;
    int n = 0;

    n = snprintf(sql, sql_cap, " WHERE entries_fts MATCH ?2");
    if (n < 0 || (size_t)n >= sql_cap) {
        return -1;
    }
    pos = (size_t)n;
    bind_text[0] = b.q;
    bind_text[1] = b.match;
    nbinds = 2;

    /* Search ignores ListQuery.sort: since/until are on updated_at (E5.3). */
    if (list_append_filters(f, sql, sql_cap, &pos, &nbinds, bind_text, bind_cap, "updated_at") !=
        0) {
        return -1;
    }
    if (list_append_bin(sql, sql_cap, &pos, &nbinds, bind_text, bind_cap, f->bin, now) != 0) {
        return -1;
    }
    *out_nbinds = nbinds;
    return 0;
}

/* Error map for list and search statements: every failure is a database error. */
static StoreStatus store_status_plain(sqlite3 *db, StoreError *serr)
{
    capture_sqlite_error(db, serr);
    return STORE_ERR_SQLITE;
}

static StoreStatus list_bind_texts(sqlite3_stmt *stmt, const char **bind_text, int nbinds,
                                   StoreError *serr)
{
    int i = 0;
    for (i = 0; i < nbinds; i++) {
        if (sqlite3_bind_text(stmt, i + 1, bind_text[i], -1, SQLITE_STATIC) != SQLITE_OK) {
            capture_sqlite_error(sqlite3_db_handle(stmt), serr);
            return STORE_ERR_SQLITE;
        }
    }
    return STORE_OK;
}

/* Enough for source + key + many tag EXISTS clauses. */
enum { LIST_SQL_CAP = 8192 };
enum { LIST_BIND_CAP = 64 };
_Static_assert(LIST_BIND_CAP >= 2, "search binds q and the MATCH text ahead of the filters");

/*
 * Bind filters + limit/offset, run the COUNT statement then the paged SELECT the
 * caller built, and collect rows. Split into two statements (COUNT then paged
 * SELECT) rather than one query: a single COUNT(*) OVER() would ride on the
 * result rows, so it reports no total whenever the page is empty (offset past the
 * end), breaking the "total is the unpaged count" contract. Callers run this
 * inside one read transaction so both statements see a single snapshot.
 * Any failed prepare/step is a database error. limit/offset bind at
 * ?nbinds+1 / ?nbinds+2.
 */
static PageResult run_count_and_page(sqlite3 *db, const char *count_sql, const char *select_sql,
                                     const char **bind_text, int nbinds, size_t limit,
                                     size_t offset, StoreError *serr)
{
    sqlite3_stmt *count_stmt = NULL;
    sqlite3_stmt *sel = NULL;
    Entry *rows = NULL;
    size_t n = 0U;
    size_t cap = 0U;
    size_t total = 0U;
    int rc = 0;
    StoreStatus st = STORE_OK;

    rc = sqlite3_prepare_v2(db, count_sql, -1, &count_stmt, NULL);
    if (rc != SQLITE_OK) {
        return (PageResult){.st = store_status_plain(db, serr)};
    }
    st = list_bind_texts(count_stmt, bind_text, nbinds, serr);
    if (st != STORE_OK) {
        (void)sqlite3_finalize(count_stmt);
        return (PageResult){.st = st};
    }
    rc = sqlite3_step(count_stmt);
    if (rc != SQLITE_ROW) {
        st = store_status_plain(db, serr);
        (void)sqlite3_finalize(count_stmt);
        return (PageResult){.st = st};
    }
    total = (size_t)sqlite3_column_int64(count_stmt, 0);
    (void)sqlite3_finalize(count_stmt);

    rc = sqlite3_prepare_v2(db, select_sql, -1, &sel, NULL);
    if (rc != SQLITE_OK) {
        return (PageResult){.st = store_status_plain(db, serr)};
    }
    st = list_bind_texts(sel, bind_text, nbinds, serr);
    if (st != STORE_OK) {
        (void)sqlite3_finalize(sel);
        return (PageResult){.st = st};
    }
    if (sqlite3_bind_int64(sel, nbinds + 1, (sqlite3_int64)limit) != SQLITE_OK ||
        sqlite3_bind_int64(sel, nbinds + 2, (sqlite3_int64)offset) != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(sel);
        return (PageResult){.st = STORE_ERR_SQLITE};
    }

    while ((rc = sqlite3_step(sel)) == SQLITE_ROW) {
        st = list_append_row(db, sel, &rows, &n, &cap, serr);
        if (st != STORE_OK) {
            free_entry_rows(rows, n);
            (void)sqlite3_finalize(sel);
            return (PageResult){.st = st};
        }
    }
    if (rc != SQLITE_DONE) {
        st = store_status_plain(db, serr);
        free_entry_rows(rows, n);
        (void)sqlite3_finalize(sel);
        return (PageResult){.st = st};
    }
    (void)sqlite3_finalize(sel);

    return (PageResult){.st = STORE_OK, .entries = rows, .count = n, .total = total};
}

/*
 * List: page ordered by the sort column DESC, then id DESC. count_sql/select_sql
 * pad LIST_SQL_CAP for the fixed SELECT column list + ORDER BY + LIMIT/OFFSET
 * that frame where_sql.
 */
static PageResult list_query_exec(sqlite3 *db, const ListQuery *q, const char *now,
                                  StoreError *serr)
{
    /* Headroom beyond where_sql for the COUNT / SELECT framing (columns,
       ORDER BY, LIMIT/OFFSET) wrapped around it. */
    enum { COUNT_FRAME_MARGIN = 64, SELECT_FRAME_MARGIN = 160 };
    char where_sql[LIST_SQL_CAP];
    char count_sql[(size_t)LIST_SQL_CAP + (size_t)COUNT_FRAME_MARGIN];
    char select_sql[(size_t)LIST_SQL_CAP + (size_t)SELECT_FRAME_MARGIN];
    const char *bind_text[LIST_BIND_CAP];
    const char *column = list_sort_column(q->sort);
    int nbinds = 0;
    int sn = 0;

    if (column == NULL) {
        return (PageResult){.st = STORE_ERR_INTERNAL};
    }
    if (list_build_where(q, now, column, where_sql, sizeof(where_sql), &nbinds, bind_text,
                         LIST_BIND_CAP) != 0) {
        return (PageResult){.st = STORE_ERR_INTERNAL};
    }
    sn = snprintf(count_sql, sizeof(count_sql), "SELECT COUNT(*) FROM entries e%s;", where_sql);
    if (sn < 0 || (size_t)sn >= sizeof(count_sql)) {
        return (PageResult){.st = STORE_ERR_INTERNAL};
    }
    sn = snprintf(select_sql, sizeof(select_sql),
                  "SELECT " K_ENTRY_COLS_E " "
                  "FROM entries e%s ORDER BY e.%s DESC, e.local_id DESC "
                  "LIMIT ?%d OFFSET ?%d;",
                  where_sql, column, nbinds + 1, nbinds + 2);
    if (sn < 0 || (size_t)sn >= sizeof(select_sql)) {
        return (PageResult){.st = STORE_ERR_INTERNAL};
    }
    return run_count_and_page(db, count_sql, select_sql, bind_text, nbinds, q->limit, q->offset,
                              serr);
}

PageResult store_list(Store *s, const ListQuery *q, const char *now)
{
    char err_unused[1];
    PageResult page = {.st = STORE_OK};

    if (s == NULL || s->db == NULL || q == NULL || now == NULL) {
        return (PageResult){.st = STORE_ERR_INTERNAL};
    }

    /* One read transaction: COUNT and the paged SELECT see the same snapshot, so
       total and the page cannot disagree if a writer commits mid-list. */
    memset(&s->last, 0, sizeof(s->last));
    if (exec_sql(s->db, "BEGIN;", err_unused, 0U, &s->last) != 0) {
        return (PageResult){.st = STORE_ERR_SQLITE};
    }
    /* cppcheck-suppress redundantInitialization */
    page = list_query_exec(s->db, q, now, &s->last);
    if (page.st != STORE_OK) {
        rollback_quiet(s->db);
        return page;
    }
    if (exec_sql(s->db, "COMMIT;", err_unused, 0U, &s->last) != 0) {
        /* Unwind the page we built so the caller sees a clean failure. */
        rollback_quiet(s->db);
        free_entry_rows(page.entries, page.count);
        return (PageResult){.st = STORE_ERR_SQLITE};
    }
    return page;
}

/*
 * COUNT + paged FTS SELECT. Same two-statement total contract as
 * list_query_exec. E2.4 order: exact key, key prefix of q, weighted bm25
 * (key 10, body 1, tags 4; lower is better), then updated_at DESC, id DESC.
 * IS, not =, so a NULL key ranks as a miss instead of below one.
 */
static PageResult search_query_exec(sqlite3 *db, const ListQuery *f, SearchBinds b, const char *now,
                                    StoreError *serr)
{
    /* Headroom beyond where_sql for the COUNT / SELECT framing wrapped around it. */
    enum { COUNT_FRAME_MARGIN = 128, SELECT_FRAME_MARGIN = 512 };
    char where_sql[LIST_SQL_CAP];
    char count_sql[(size_t)LIST_SQL_CAP + (size_t)COUNT_FRAME_MARGIN];
    char select_sql[(size_t)LIST_SQL_CAP + (size_t)SELECT_FRAME_MARGIN];
    const char *bind_text[LIST_BIND_CAP];
    int nbinds = 0;
    int sn = 0;

    if (search_build_where(f, b, now, where_sql, sizeof(where_sql), &nbinds, bind_text,
                           LIST_BIND_CAP) != 0) {
        return (PageResult){.st = STORE_ERR_INTERNAL};
    }
    sn = snprintf(count_sql, sizeof(count_sql),
                  "SELECT COUNT(*) FROM entries e "
                  "JOIN entries_fts ON entries_fts.rowid = e.local_id%s;",
                  where_sql);
    if (sn < 0 || (size_t)sn >= sizeof(count_sql)) {
        return (PageResult){.st = STORE_ERR_INTERNAL};
    }
    sn = snprintf(select_sql, sizeof(select_sql),
                  "SELECT " K_ENTRY_COLS_E " "
                  "FROM entries e "
                  "JOIN entries_fts ON entries_fts.rowid = e.local_id%s "
                  "ORDER BY e.key IS ?1 DESC, substr(e.key, 1, length(?1)) IS ?1 DESC, "
                  "bm25(entries_fts, 10.0, 1.0, 4.0), e.updated_at DESC, e.local_id DESC "
                  "LIMIT ?%d OFFSET ?%d;",
                  where_sql, nbinds + 1, nbinds + 2);
    if (sn < 0 || (size_t)sn >= sizeof(select_sql)) {
        return (PageResult){.st = STORE_ERR_INTERNAL};
    }
    return run_count_and_page(db, count_sql, select_sql, bind_text, nbinds, f->limit, f->offset,
                              serr);
}

/* "t1"* "t2"* ...: each token quoted as a prefix, joined by one space. */
enum { SEARCH_P_CAP = (SEARCH_TOKENS_MAX * (SEARCH_TOKEN_BYTES_MAX + 4)) + 1 };

/* snprintf returns the length it would have written. Adding that after a short
   write makes the next size wrap past the buffer. */
static int search_prefix_part(const SearchTokens *t, char p[SEARCH_P_CAP])
{
    size_t pos = 0U;
    size_t i = 0U;

    if (t->count > (size_t)SEARCH_TOKENS_MAX) {
        return -1;
    }
    p[0] = '\0';
    for (i = 0U; i < t->count; i++) {
        size_t room = (size_t)SEARCH_P_CAP - pos;
        int wrote = snprintf(p + pos, room, "%s\"%s\"*", (i > 0U) ? " " : "", t->tokens[i]);

        if (wrote < 0 || (size_t)wrote >= room) {
            return -1;
        }
        pos += (size_t)wrote;
    }
    return 0;
}

/* E2.1 (3)-(5): (P) OR (R), or whichever is not empty. r is "" when R is
   empty or does not parse. Empty out means no query (an empty result). */
static void search_match(const char *p, const char *r, char *out, size_t cap)
{
    if (p[0] != '\0' && r[0] != '\0') {
        (void)snprintf(out, cap, "(%s) OR (%s)", p, r);
    } else {
        (void)snprintf(out, cap, "%s%s", p, r);
    }
}

/*
 * E2.1a: does r parse alone? Runs on the caller's connection and read
 * transaction. It does not parse only on SQLITE_ERROR with a D1 message:
 * *warning (NULL on entry) is then a heap copy of that message. Any other
 * failure is a real error, captured into serr.
 */
static StoreStatus search_probe_raw(sqlite3 *db, const char *r, char **warning, StoreError *serr)
{
    static const char *const no_parse[] = {"fts5:", "no such column", "unknown special query",
                                           "unterminated string"};
    sqlite3_stmt *stmt = NULL;
    StoreStatus st = STORE_OK;
    const char *msg = NULL;
    size_t i = 0U;
    int rc = 0;

    rc = sqlite3_prepare_v2(db, "SELECT 1 FROM entries_fts WHERE entries_fts MATCH ?1 LIMIT 1;", -1,
                            &stmt, NULL);
    if (rc != SQLITE_OK) {
        return store_status_plain(db, serr);
    }
    st = list_bind_texts(stmt, &r, 1, serr);
    if (st != STORE_OK) {
        goto cleanup;
    }
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW || rc == SQLITE_DONE) {
        goto cleanup;
    }
    msg = sqlite3_errmsg(db);
    for (i = 0U; rc == SQLITE_ERROR && i < sizeof(no_parse) / sizeof(no_parse[0]); i++) {
        if (strncmp(msg, no_parse[i], strlen(no_parse[i])) == 0) {
            *warning = dup_str(msg);
            st = (*warning == NULL) ? STORE_ERR_OOM : STORE_OK;
            goto cleanup;
        }
    }
    st = store_status_plain(db, serr);

cleanup:
    (void)sqlite3_finalize(stmt);
    return st;
}

static StoreStatus dup_col_req(sqlite3_stmt *stmt, int col, char **out);

static void search_key_item_free(SearchKeyItem *item)
{
    free(item->key);
    free(item->sync_id);
    free(item->label);
    free(item->source);
    free(item->updated_at);
}

/*
 * Facets (E2.5d, D13): one query per kind over the active bin and filters,
 * fetched once per search; search_terms.c classifies and orders the rows.
 * Key rows: key, sync_id, source, updated_at, body. Tag rows: name, count.
 */
typedef enum { FACET_KEYS = 0, FACET_TAGS } FacetKind;

static const char *const k_facet_head[] = {
    [FACET_KEYS] = "SELECT e.key, e.id, " K_SOURCE_OF_E ", e.updated_at, e.body FROM entries e",
    [FACET_TAGS] =
        "SELECT tg.name, COUNT(*) FROM entries e"
        " JOIN entry_tags et ON et.entry_id = e.local_id JOIN tags tg ON tg.id = et.tag_id",
};
static const char *const k_facet_tail[] = {
    [FACET_KEYS] = " AND e.key IS NOT NULL;",
    [FACET_TAGS] = " GROUP BY tg.name;",
};

/* What every facet query of one search shares. */
typedef struct {
    sqlite3 *db;
    const SearchQuery *sq;
    const char *now;
    const SearchTokens *tokens;
    const char *q;
    StoreError *serr;
} FacetScope;

/* A text-matching key or tag. facet comes first: the search_terms comparators
   read each element as a SearchFacet. facet.text is item.key; a tag fills
   item.key only. */
typedef struct {
    SearchFacet facet;
    SearchKeyItem item;
} FacetCandidate;

typedef struct {
    FacetCandidate *items;
    size_t count;
    size_t cap;
} FacetCandidates;

static void facet_candidates_free(FacetCandidates *c)
{
    size_t i = 0U;

    for (i = 0U; i < c->count; i++) {
        search_key_item_free(&c->items[i].item);
    }
    free(c->items);
}

/* Copy one row into c when its text matches. */
static StoreStatus facet_append_row(const FacetScope *scope, sqlite3_stmt *stmt, FacetKind kind,
                                    FacetCandidates *c)
{
    FacetCandidate k = {0};
    long long count = (kind == FACET_TAGS) ? sqlite3_column_int64(stmt, 1) : 0;
    SearchStatus sst = search_facet_classify(
        scope->tokens, scope->q, (const char *)sqlite3_column_text(stmt, 0), count, &k.facet);

    /* Only a hand-edited store holds text that is not UTF-8: a non-match
       (decision:plan18-facet-invalid-utf8-no-match). */
    if (sst == SEARCH_ERR_INVALID_UTF8 || (sst == SEARCH_OK && k.facet.tier == SEARCH_TIER_NONE)) {
        return STORE_OK;
    }
    if (sst != SEARCH_OK) {
        return (sst == SEARCH_ERR_OOM) ? STORE_ERR_OOM : STORE_ERR_INTERNAL;
    }
    if (dup_col_req(stmt, 0, &k.item.key) != STORE_OK) {
        goto oom;
    }
    /* The columns are NOT NULL, so a NULL copy is out of memory. With a key the
       label never decodes the body: it too fails only when a copy does. */
    if (kind == FACET_KEYS && (dup_col_req(stmt, 1, &k.item.sync_id) != STORE_OK ||
                               dup_col_req(stmt, 2, &k.item.source) != STORE_OK ||
                               dup_col_req(stmt, 3, &k.item.updated_at) != STORE_OK ||
                               search_label((const char *)sqlite3_column_text(stmt, 4), k.item.key,
                                            &k.item.label) != SEARCH_OK)) {
        goto oom;
    }
    if (c->count == c->cap) {
        size_t ncap = (c->cap == 0U) ? (size_t)GROW_MIN_CAP : (c->cap * 2U);
        FacetCandidate *grown = (FacetCandidate *)realloc((void *)c->items, ncap * sizeof(*grown));
        if (grown == NULL) {
            goto oom;
        }
        c->items = grown;
        c->cap = ncap;
    }
    k.facet.text = k.item.key;
    c->items[c->count++] = k;
    return STORE_OK;

oom:
    search_key_item_free(&k.item);
    return STORE_ERR_OOM;
}

/* Run kind's query and keep the rows that text-match, sorted (E2.6, D28). */
static StoreStatus facet_fetch(const FacetScope *scope, FacetKind kind, FacetCandidates *c)
{
    /* Headroom beyond where_sql for the longest head + tail. */
    enum { FRAME_MARGIN = 256 };
    char where_sql[LIST_SQL_CAP];
    char sql[(size_t)LIST_SQL_CAP + (size_t)FRAME_MARGIN];
    const char *bind_text[LIST_BIND_CAP];
    sqlite3_stmt *stmt = NULL;
    StoreStatus st = STORE_OK;
    int nbinds = 0;
    int sn = 0;
    int rc = 0;

    if (list_build_where(&scope->sq->filters, scope->now, "updated_at", where_sql,
                         sizeof(where_sql), &nbinds, bind_text, LIST_BIND_CAP) != 0) {
        return STORE_ERR_INTERNAL;
    }
    sn = snprintf(sql, sizeof(sql), "%s%s%s", k_facet_head[kind], where_sql, k_facet_tail[kind]);
    if (sn < 0 || (size_t)sn >= sizeof(sql)) {
        return STORE_ERR_INTERNAL;
    }
    if (sqlite3_prepare_v2(scope->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return store_status_plain(scope->db, scope->serr);
    }
    st = list_bind_texts(stmt, bind_text, nbinds, scope->serr);
    while (st == STORE_OK && (rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        st = facet_append_row(scope, stmt, kind, c);
    }
    if (st == STORE_OK && rc != SQLITE_DONE) {
        st = store_status_plain(scope->db, scope->serr);
    }
    (void)sqlite3_finalize(stmt);
    /* items is NULL when nothing matched, and qsort needs a valid pointer. */
    if (st == STORE_OK && c->count > 0U) {
        qsort(c->items, c->count, sizeof(*c->items),
              (kind == FACET_KEYS) ? search_key_order : search_tag_order);
    }
    return st;
}

/* Move the first limit keys of c into out; total counts all. A moved item is
   zeroed in c, so freeing c afterwards stays correct. */
static StoreStatus facet_take_keys(FacetCandidates *c, size_t limit, SearchKeyFacet *out)
{
    size_t keep = (c->count < limit) ? c->count : limit;
    size_t i = 0U;

    out->total = c->count;
    if (keep == 0U) {
        return STORE_OK;
    }
    out->items = (SearchKeyItem *)malloc(keep * sizeof(*out->items));
    if (out->items == NULL) {
        return STORE_ERR_OOM;
    }
    for (i = 0U; i < keep; i++) {
        out->items[i] = c->items[i].item;
        memset(&c->items[i].item, 0, sizeof(c->items[i].item));
    }
    out->count = keep;
    return STORE_OK;
}

/* Same for tags: the tag text moves, count comes from the facet. */
static StoreStatus facet_take_tags(FacetCandidates *c, size_t limit, SearchTagFacet *out)
{
    size_t keep = (c->count < limit) ? c->count : limit;
    size_t i = 0U;

    out->total = c->count;
    if (keep == 0U) {
        return STORE_OK;
    }
    out->items = (SearchTagItem *)malloc(keep * sizeof(*out->items));
    if (out->items == NULL) {
        return STORE_ERR_OOM;
    }
    for (i = 0U; i < keep; i++) {
        out->items[i] =
            (SearchTagItem){.tag = c->items[i].item.key, .count = (size_t)c->items[i].facet.count};
        c->items[i].item.key = NULL;
    }
    out->count = keep;
    return STORE_OK;
}

/* Both facets of res, on the search's read transaction. */
static StoreStatus search_facets(const FacetScope *scope, SearchResult *res)
{
    FacetCandidates keys = {0};
    FacetCandidates tags = {0};
    StoreStatus st = facet_fetch(scope, FACET_KEYS, &keys);

    if (st == STORE_OK) {
        st = facet_take_keys(&keys, scope->sq->facet_limit, &res->keys);
    }
    if (st == STORE_OK) {
        st = facet_fetch(scope, FACET_TAGS, &tags);
    }
    if (st == STORE_OK) {
        st = facet_take_tags(&tags, scope->sq->facet_limit, &res->tags);
    }
    facet_candidates_free(&keys);
    facet_candidates_free(&tags);
    return st;
}

void store_search_result_free(SearchResult *res)
{
    size_t i = 0U;

    if (res == NULL) {
        return;
    }
    free_entry_rows(res->page.entries, res->page.count);
    for (i = 0U; i < res->keys.count; i++) {
        search_key_item_free(&res->keys.items[i]);
    }
    free(res->keys.items);
    for (i = 0U; i < res->tags.count; i++) {
        free(res->tags.items[i].tag);
    }
    free(res->tags.items);
    free(res->warning);
    memset(res, 0, sizeof(*res));
}

SearchResult store_search(Store *s, const SearchQuery *q, const char *now)
{
    char err_unused[1];
    SearchResult res = {.page = {.st = STORE_OK}, .warning = NULL};
    SearchTokens tokens;
    SearchStatus sst = SEARCH_OK;
    char p[SEARCH_P_CAP];
    char *r = NULL;
    char *q_low = NULL;
    char *match = NULL;
    size_t start = 0U;
    size_t end = 0U;
    size_t match_cap = 0U;

    if (s == NULL || s->db == NULL || q == NULL || q->query == NULL || now == NULL) {
        return (SearchResult){.page = {.st = STORE_ERR_INTERNAL}};
    }
    end = strlen(q->query);
    ascii_ws_trim(q->query, &start, &end);
    r = (char *)malloc(end - start + 1U);
    q_low = (char *)malloc(end - start + 1U);
    match_cap = sizeof(p) + (end - start) + sizeof("() OR ()");
    match = (char *)malloc(match_cap);
    if (r == NULL || q_low == NULL || match == NULL) {
        res.page.st = STORE_ERR_OOM;
        goto cleanup;
    }
    memcpy(r, q->query + start, end - start);
    r[end - start] = '\0';
    sst = search_tokens_build(q->query, &tokens);
    if (sst == SEARCH_OK) {
        sst = search_q(r, q_low, end - start + 1U);
    }
    /* A failed prefix build is the same internal failure. || skips the build
       when tokens were left untouched. */
    if (sst != SEARCH_OK || search_prefix_part(&tokens, p) != 0) {
        res.page.st = (sst == SEARCH_ERR_OOM) ? STORE_ERR_OOM : STORE_ERR_INTERNAL;
        goto cleanup;
    }

    memset(&s->last, 0, sizeof(s->last));
    if (exec_sql(s->db, "BEGIN;", err_unused, 0U, &s->last) != 0) {
        res.page.st = STORE_ERR_SQLITE;
        goto cleanup;
    }
    if (r[0] != '\0') {
        res.page.st = search_probe_raw(s->db, r, &res.warning, &s->last);
    }
    if (res.page.st == STORE_OK) {
        search_match(p, (res.warning == NULL) ? r : "", match, match_cap);
        if (match[0] != '\0') {
            res.page = search_query_exec(s->db, &q->filters,
                                         (SearchBinds){.q = q_low, .match = match}, now, &s->last);
        }
    }
    if (res.page.st == STORE_OK) {
        FacetScope scope = {
            .db = s->db, .sq = q, .now = now, .tokens = &tokens, .q = q_low, .serr = &s->last};

        res.page.st = search_facets(&scope, &res);
    }
    if (res.page.st != STORE_OK) {
        rollback_quiet(s->db);
        goto cleanup;
    }
    if (exec_sql(s->db, "COMMIT;", err_unused, 0U, &s->last) != 0) {
        rollback_quiet(s->db);
        res.page.st = STORE_ERR_SQLITE;
    }

cleanup:
    free(r);
    free(q_low);
    free(match);
    if (res.page.st != STORE_OK) {
        StoreStatus st = res.page.st;

        store_search_result_free(&res);
        res.page.st = st;
    }
    return res;
}

static const char k_fts_rebuild_sql[] = "INSERT INTO entries_fts(entries_fts) VALUES('rebuild');";
static const char k_fts_integrity_sql[] =
    "INSERT INTO entries_fts(entries_fts) VALUES('integrity-check');";

static StoreStatus count_all_entries(sqlite3 *db, size_t *out, StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    int rc = 0;

    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM entries;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    *out = (size_t)sqlite3_column_int64(stmt, 0);
    (void)sqlite3_finalize(stmt);
    return STORE_OK;
}

/* Fast path: FTS5 'rebuild' in one transaction, then integrity-check (E4.14).
   Fixes logical damage; page-level damage makes it fail (it frees those pages). */
static int rebuild_in_place(sqlite3 *db, StoreError *serr)
{
    char err_unused[1];

    if (exec_sql(db, "BEGIN;", err_unused, 0U, serr) != 0) {
        return -1;
    }
    if (exec_sql(db, k_fts_rebuild_sql, err_unused, 0U, serr) != 0 ||
        exec_sql(db, "COMMIT;", err_unused, 0U, serr) != 0) {
        rollback_quiet(db);
        return -1;
    }
    return exec_sql(db, k_fts_integrity_sql, err_unused, 0U, serr);
}

/* Page-level damage in an FTS shadow table defeats 'rebuild' and DROP alike:
   both must free the damaged pages. The base tables already passed
   quick_check, so unlink the index from the schema without touching its
   pages, recreate it from entries (Hebrew fold) in the same transaction, then
   VACUUM away the orphaned pages (Itay 2026-10-16: repair fixes whatever it
   can). */
static int replace_damaged_fts(sqlite3 *db, StoreError *serr)
{
    static const char unlink_sql[] = "PRAGMA writable_schema = ON;"
                                     "DELETE FROM sqlite_schema WHERE tbl_name = 'entries_fts' OR "
                                     "tbl_name LIKE 'entries\\_fts\\_%' ESCAPE '\\';"
                                     "PRAGMA writable_schema = RESET;";
    char err_unused[1];

    /* writable_schema needs defensive mode off; it is back on before VACUUM. */
    if (set_defensive(db, 0, err_unused, 0U, serr) != 0) {
        return -1;
    }
    if (exec_sql(db, "BEGIN IMMEDIATE;", err_unused, 0U, serr) != 0 ||
        exec_sql(db, unlink_sql, err_unused, 0U, serr) != 0 ||
        exec_sql(db, K_FTS_SQL, err_unused, 0U, serr) != 0 ||
        repopulate_fts(db, err_unused, 0U, serr) != 0 ||
        exec_sql(db, "COMMIT;", err_unused, 0U, serr) != 0) {
        rollback_quiet(db);
        /* The schema was reloaded mid-transaction; reload the rolled-back one. */
        (void)sqlite3_exec(db, "PRAGMA writable_schema = RESET;", NULL, NULL, NULL);
        (void)set_defensive(db, 1, err_unused, 0U, NULL);
        return -1;
    }
    if (set_defensive(db, 1, err_unused, 0U, serr) != 0) {
        return -1;
    }
    if (exec_sql(db, "VACUUM;", err_unused, 0U, serr) != 0) {
        return -1;
    }
    return exec_sql(db, k_fts_integrity_sql, err_unused, 0U, serr);
}

/* E4.14: base tables first (refuse on base damage), then the 'rebuild' fast
   path, then the schema-surgery rebuild for page-level index damage. */
StoreStatus store_repair_search_index(Store *s, size_t *out_entries)
{
    if (s == NULL || s->db == NULL || out_entries == NULL) {
        return STORE_ERR_INTERNAL;
    }
    memset(&s->last, 0, sizeof(s->last));
    if (check_base_tables(s->db, &s->last) != 0) {
        return STORE_ERR_SQLITE;
    }
    if (rebuild_in_place(s->db, &s->last) != 0 && replace_damaged_fts(s->db, &s->last) != 0) {
        /* Repair could not fix it: never point the user back at repair. */
        s->last.fts_only = false;
        return STORE_ERR_SQLITE;
    }
    memset(&s->last, 0, sizeof(s->last));
    return count_all_entries(s->db, out_entries, &s->last);
}

/* Remove FTS row for entry_id (no-op if already gone). */
static StoreStatus fts_delete(sqlite3 *db, long long entry_id, StoreError *serr)
{
    sqlite3_stmt *del = NULL;
    int rc = 0;

    rc = sqlite3_prepare_v2(db, "DELETE FROM entries_fts WHERE rowid = ?1;", -1, &del, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_int64(del, 1, entry_id);
    if (sqlite3_step(del) != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(del);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(del);
    return STORE_OK;
}

/* Drop tags with no remaining entry_tags rows (after CASCADE). */
static StoreStatus gc_orphan_tags(sqlite3 *db, StoreError *serr)
{
    char err_unused[1];

    if (exec_sql(db,
                 "DELETE FROM tags WHERE NOT EXISTS "
                 "(SELECT 1 FROM entry_tags et WHERE et.tag_id = tags.id);",
                 err_unused, 0U, serr) != 0) {
        return STORE_ERR_SQLITE;
    }
    return STORE_OK;
}

static StoreStatus bin_status_soft_delete(const Entry *e, const char *now)
{
    if (e == NULL || now == NULL) {
        return STORE_ERR_INTERNAL;
    }
    if (entry_bin(e, now) == STORE_BIN_DELETED) {
        return STORE_ERR_DELETED;
    }
    return STORE_OK;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static StoreStatus load_for_soft_delete(sqlite3 *db, long long id, const char *key_or_null,
                                        const char *now, Entry *out, StoreError *serr)
{
    StoreStatus st = STORE_OK;

    if (key_or_null != NULL) {
        st = load_entry_by_key(db, key_or_null, out, serr);
    } else {
        st = load_entry_by_id(db, id, out, serr);
    }
    if (st != STORE_OK) {
        return st;
    }
    st = bin_status_soft_delete(out, now);
    if (st != STORE_OK) {
        store_entry_free(out);
    }
    return st;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static StoreStatus apply_soft_delete(sqlite3 *db, long long id, const char *now,
                                     const char *device_id, const char *vv_in, StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    char vv_out[VV_OUT_MAX];
    int rc = 0;

    if (vv_increment(vv_in, device_id, vv_out, sizeof(vv_out)) != 0) {
        return STORE_ERR_INTERNAL;
    }
    rc = sqlite3_prepare_v2(db,
                            "UPDATE entries SET deleted_at = ?1, expires_at = NULL, "
                            "updated_at = ?1, version_vector = ?2 WHERE local_id = ?3;",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_text(stmt, 1, now, -1, SQLITE_STATIC);
    (void)sqlite3_bind_text(stmt, 2, vv_out, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_int64(stmt, 3, id);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(stmt);
    return STORE_OK;
}

static StoreStatus soft_delete_tx(Store *s, long long id_or_zero, const char *key_or_null,
                                  const char *now, Entry *out)
{
    char err_unused[1];
    StoreStatus st = STORE_OK;
    long long id = id_or_zero;
    Entry cur;

    if (s == NULL || s->db == NULL || s->device_id == NULL || now == NULL || out == NULL) {
        return STORE_ERR_INTERNAL;
    }
    memset(out, 0, sizeof(*out));
    memset(&cur, 0, sizeof(cur));
    memset(&s->last, 0, sizeof(s->last));
    if (exec_sql(s->db, "BEGIN IMMEDIATE;", err_unused, 0U, &s->last) != 0) {
        return STORE_ERR_SQLITE;
    }
    st = load_for_soft_delete(s->db, id, key_or_null, now, &cur, &s->last);
    if (st != STORE_OK) {
        goto fail;
    }
    id = cur.id;
    st = apply_soft_delete(s->db, id, now, s->device_id, cur.version_vector, &s->last);
    store_entry_free(&cur);
    if (st != STORE_OK) {
        goto fail;
    }
    st = load_entry_by_id(s->db, id, out, &s->last);
    if (st != STORE_OK) {
        goto fail;
    }
    if (exec_sql(s->db, "COMMIT;", err_unused, 0U, &s->last) != 0) {
        st = STORE_ERR_SQLITE;
        store_entry_free(out);
        goto fail;
    }
    return STORE_OK;

fail:
    rollback_quiet(s->db);
    return st;
}

static StoreStatus hard_delete_tx(Store *s, long long id_or_zero, const char *key_or_null,
                                  StoreBin bin, const char *now, Entry *out_deleted)
{
    char err_unused[1];
    StoreStatus st = STORE_OK;
    sqlite3_stmt *del = NULL;
    long long id = id_or_zero;
    int rc = 0;

    memset(out_deleted, 0, sizeof(*out_deleted));
    if (bin == STORE_BIN_LIVE) {
        return STORE_ERR_INTERNAL;
    }
    memset(&s->last, 0, sizeof(s->last));
    if (exec_sql(s->db, "BEGIN IMMEDIATE;", err_unused, 0U, &s->last) != 0) {
        return STORE_ERR_SQLITE;
    }

    st = load_then_check_bin(s->db, id, key_or_null, NULL, bin, now, out_deleted, &s->last);
    if (st != STORE_OK) {
        goto fail;
    }
    id = out_deleted->id;

    st = fts_delete(s->db, id, &s->last);
    if (st != STORE_OK) {
        goto fail_free;
    }

    rc = sqlite3_prepare_v2(s->db, "DELETE FROM entries WHERE local_id = ?1;", -1, &del, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(s->db, &s->last);
        st = STORE_ERR_SQLITE;
        goto fail_free;
    }
    (void)sqlite3_bind_int64(del, 1, id);
    if (sqlite3_step(del) != SQLITE_DONE) {
        capture_sqlite_error(s->db, &s->last);
        (void)sqlite3_finalize(del);
        st = STORE_ERR_SQLITE;
        goto fail_free;
    }
    (void)sqlite3_finalize(del);

    if (sqlite3_changes(s->db) == 0) {
        st = STORE_ERR_NOT_FOUND;
        goto fail_free;
    }

    st = gc_orphan_tags(s->db, &s->last);
    if (st != STORE_OK) {
        goto fail_free;
    }

    if (exec_sql(s->db, "COMMIT;", err_unused, 0U, &s->last) != 0) {
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

StoreStatus store_soft_delete_by_id(Store *s, long long id, const char *now, Entry *out)
{
    if (s == NULL || s->db == NULL || now == NULL || out == NULL) {
        return STORE_ERR_INTERNAL;
    }
    return soft_delete_tx(s, id, NULL, now, out);
}

StoreStatus store_soft_delete_by_key(Store *s, const char *key, const char *now, Entry *out)
{
    if (s == NULL || s->db == NULL || key == NULL || now == NULL || out == NULL) {
        return STORE_ERR_INTERNAL;
    }
    return soft_delete_tx(s, 0, key, now, out);
}

StoreStatus store_hard_delete_by_id(Store *s, long long id, StoreBin bin, const char *now,
                                    Entry *out)
{
    if (s == NULL || s->db == NULL || now == NULL || out == NULL) {
        return STORE_ERR_INTERNAL;
    }
    return hard_delete_tx(s, id, NULL, bin, now, out);
}

StoreStatus store_hard_delete_by_key(Store *s, const char *key, StoreBin bin, const char *now,
                                     Entry *out)
{
    if (s == NULL || s->db == NULL || key == NULL || now == NULL || out == NULL) {
        return STORE_ERR_INTERNAL;
    }
    return hard_delete_tx(s, 0, key, bin, now, out);
}

/* Drop all entry_tags for entry_id, then link the new set (ntags may be 0). */
static StoreStatus replace_tags(sqlite3 *db, long long entry_id, const char *const *tags,
                                size_t ntags, StoreError *serr)
{
    sqlite3_stmt *del = NULL;
    StoreStatus st = STORE_OK;
    int rc = 0;

    rc = sqlite3_prepare_v2(db, "DELETE FROM entry_tags WHERE entry_id = ?1;", -1, &del, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_int64(del, 1, entry_id);
    if (sqlite3_step(del) != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(del);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(del);

    st = union_tags(db, entry_id, tags, ntags, serr);
    if (st != STORE_OK) {
        return st;
    }
    return gc_orphan_tags(db, serr);
}

static StoreStatus update_check_args(const Store *s, long long id, const char *key_or_null,
                                     bool set_body, const char *body, const char *body_hash,
                                     bool set_tags, const char *const *tags, size_t ntags,
                                     bool set_expires, bool undelete, const char *now,
                                     const Entry *out_entry)
{
    if (s == NULL || s->db == NULL || now == NULL || out_entry == NULL) {
        return STORE_ERR_INTERNAL;
    }
    if (!set_body && !set_tags && !set_expires && !undelete) {
        return STORE_ERR_INTERNAL;
    }
    if (set_body && (body == NULL || body_hash == NULL)) {
        return STORE_ERR_INTERNAL;
    }
    if (set_tags && ntags > 0U && tags == NULL) {
        return STORE_ERR_INTERNAL;
    }
    if (key_or_null == NULL && id < 1) {
        return STORE_ERR_INTERNAL;
    }
    return STORE_OK;
}

/* Keyless body-hash uniqueness: conflict if another keyless row owns body_hash. */
static StoreStatus update_check_body_conflict(sqlite3 *db, long long entry_id, bool is_keyless,
                                              const char *body_hash, long long *out_conflict_id,
                                              StoreError *serr)
{
    long long other_id = 0;
    StoreStatus st = STORE_OK;

    if (!is_keyless) {
        return STORE_OK;
    }
    st = find_keyless_by_hash(db, body_hash, &other_id, serr);
    if (st == STORE_ERR_NOT_FOUND) {
        return STORE_OK;
    }
    if (st != STORE_OK) {
        return st;
    }
    if (other_id == entry_id) {
        return STORE_OK;
    }
    if (out_conflict_id != NULL) {
        *out_conflict_id = other_id;
    }
    return STORE_ERR_CONFLICT;
}

static StoreStatus version_vector_guard(sqlite3 *db, const char *row_vv, const char *given,
                                        StoreError *serr);

/* Apply body and/or tag changes and always refresh updated_at. */
static StoreStatus update_apply_changes(sqlite3 *db, long long entry_id, bool is_keyless,
                                        bool set_body, const char *body, const char *body_hash,
                                        bool set_tags, const char *const *tags, size_t ntags,
                                        bool set_expires, EntryTimes times, bool undelete,
                                        const char *device_id, const char *vv_in,
                                        long long *out_conflict_id, StoreError *serr)
{
    StoreStatus st = STORE_OK;

    if (set_body) {
        st = update_check_body_conflict(db, entry_id, is_keyless, body_hash, out_conflict_id, serr);
        if (st != STORE_OK) {
            return st;
        }
        st = replace_body(db, entry_id, body, body_hash, times.now, serr);
    } else {
        st = touch_updated_at(db, entry_id, times.now, serr);
    }
    if (st != STORE_OK) {
        return st;
    }
    if (set_tags) {
        st = replace_tags(db, entry_id, tags, ntags, serr);
        if (st != STORE_OK) {
            return st;
        }
    }
    if (set_expires) {
        st = write_expires_at(db, entry_id, times.expires_at, serr);
        if (st != STORE_OK) {
            return st;
        }
    }
    if (undelete) {
        st = clear_deleted_at(db, entry_id, serr);
        if (st != STORE_OK) {
            return st;
        }
    }
    /* Every update mutates at least one field (update_check_args), so bump the
       version vector exactly once here — covers body/tags/expiry and undelete. */
    st = apply_vv_bump(db, entry_id, device_id, vv_in, serr);
    if (st != STORE_OK) {
        return st;
    }
    return fts_resync(db, entry_id, serr);
}

StoreStatus store_update(Store *s, long long id, const StoreUpdate *u, const char *now,
                         Entry *out_entry, long long *out_conflict_id)
{
    char err_unused[1];
    StoreStatus st = STORE_OK;
    Entry current;
    long long entry_id = 0;
    bool is_keyless = false;

    if (out_conflict_id != NULL) {
        *out_conflict_id = 0;
    }
    if (u == NULL) {
        return STORE_ERR_INTERNAL;
    }
    st = update_check_args(s, id, u->key_or_null, u->set_body, u->body, u->body_hash, u->set_tags,
                           u->tags, u->ntags, u->set_expires, u->undelete, now, out_entry);
    if (st != STORE_OK) {
        return st;
    }

    memset(&current, 0, sizeof(current));
    memset(out_entry, 0, sizeof(*out_entry));
    memset(&s->last, 0, sizeof(s->last));

    if (exec_sql(s->db, "BEGIN IMMEDIATE;", err_unused, 0U, &s->last) != 0) {
        return STORE_ERR_SQLITE;
    }

    st = load_then_check_bin(s->db, id, u->key_or_null, NULL, u->bin, now, &current, &s->last);
    if (st != STORE_OK) {
        rollback_quiet(s->db);
        return st;
    }
    entry_id = current.id;
    is_keyless = (current.key == NULL);
    /* P4: current.version_vector was read in this write transaction, before any change. */
    if (u->if_version_vector != NULL) {
        st = version_vector_guard(s->db, current.version_vector, u->if_version_vector, &s->last);
        if (st != STORE_OK) {
            store_entry_free(&current);
            rollback_quiet(s->db);
            return st;
        }
    }

    st = update_apply_changes(s->db, entry_id, is_keyless, u->set_body, u->body, u->body_hash,
                              u->set_tags, u->tags, u->ntags, u->set_expires,
                              (EntryTimes){.now = now, .expires_at = u->expires_at}, u->undelete,
                              s->device_id, current.version_vector, out_conflict_id, &s->last);
    if (st != STORE_OK) {
        store_entry_free(&current);
        rollback_quiet(s->db);
        return st;
    }

    if (exec_sql(s->db, "COMMIT;", err_unused, 0U, &s->last) != 0) {
        store_entry_free(&current);
        rollback_quiet(s->db);
        return STORE_ERR_SQLITE;
    }
    store_entry_free(&current);

    /* Durable past COMMIT; snapshot failure does not lose the write. */
    return load_entry_by_id(s->db, entry_id, out_entry, &s->last);
}

StoreStatus store_purge(Store *s, StoreBin bin, const char *now, Entry **out_entries,
                        size_t *out_count)
{
    sqlite3_stmt *sel = NULL;
    sqlite3_stmt *del = NULL;
    char err_unused[1];
    StoreStatus st = STORE_OK;
    Entry *rows = NULL;
    size_t n = 0U;
    size_t cap = 0U;
    size_t i = 0;
    int rc = 0;
    const char *sel_sql = NULL;
    const char *del_sql = NULL;
    bool bind_now = false;

    if (s == NULL || s->db == NULL || now == NULL || out_entries == NULL || out_count == NULL) {
        return STORE_ERR_INTERNAL;
    }
    if (bin == STORE_BIN_EXPIRED) {
        sel_sql = "SELECT " K_ENTRY_COLS " "
                  "FROM entries WHERE deleted_at IS NULL AND expires_at IS NOT NULL "
                  "AND expires_at <= ?1 ORDER BY updated_at DESC, local_id DESC;";
        del_sql = "DELETE FROM entries WHERE deleted_at IS NULL AND expires_at IS NOT NULL AND "
                  "expires_at <= ?1;";
        bind_now = true;
    } else if (bin == STORE_BIN_DELETED) {
        sel_sql = "SELECT " K_ENTRY_COLS " "
                  "FROM entries WHERE deleted_at IS NOT NULL "
                  "ORDER BY updated_at DESC, local_id DESC;";
        del_sql = "DELETE FROM entries WHERE deleted_at IS NOT NULL;";
    } else {
        return STORE_ERR_INTERNAL;
    }
    *out_entries = NULL;
    *out_count = 0U;
    memset(&s->last, 0, sizeof(s->last));

    if (exec_sql(s->db, "BEGIN IMMEDIATE;", err_unused, 0U, &s->last) != 0) {
        return STORE_ERR_SQLITE;
    }

    rc = sqlite3_prepare_v2(s->db, sel_sql, -1, &sel, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(s->db, &s->last);
        rollback_quiet(s->db);
        return STORE_ERR_SQLITE;
    }
    if (bind_now) {
        (void)sqlite3_bind_text(sel, 1, now, -1, SQLITE_STATIC);
    }
    while ((rc = sqlite3_step(sel)) == SQLITE_ROW) {
        st = list_append_row(s->db, sel, &rows, &n, &cap, &s->last);
        if (st != STORE_OK) {
            (void)sqlite3_finalize(sel);
            free_entry_rows(rows, n);
            rollback_quiet(s->db);
            return st;
        }
    }
    if (rc != SQLITE_DONE) {
        capture_sqlite_error(s->db, &s->last);
        (void)sqlite3_finalize(sel);
        sel = NULL;
        free_entry_rows(rows, n);
        rollback_quiet(s->db);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(sel);
    sel = NULL;

    for (i = 0; i < n; i++) {
        st = fts_delete(s->db, rows[i].id, &s->last);
        if (st != STORE_OK) {
            free_entry_rows(rows, n);
            rollback_quiet(s->db);
            return st;
        }
    }

    rc = sqlite3_prepare_v2(s->db, del_sql, -1, &del, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(s->db, &s->last);
        free_entry_rows(rows, n);
        rollback_quiet(s->db);
        return STORE_ERR_SQLITE;
    }
    if (bind_now) {
        (void)sqlite3_bind_text(del, 1, now, -1, SQLITE_STATIC);
    }
    if (sqlite3_step(del) != SQLITE_DONE) {
        capture_sqlite_error(s->db, &s->last);
        (void)sqlite3_finalize(del);
        free_entry_rows(rows, n);
        rollback_quiet(s->db);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(del);

    st = gc_orphan_tags(s->db, &s->last);
    if (st != STORE_OK) {
        free_entry_rows(rows, n);
        rollback_quiet(s->db);
        return st;
    }

    if (exec_sql(s->db, "COMMIT;", err_unused, 0U, &s->last) != 0) {
        free_entry_rows(rows, n);
        rollback_quiet(s->db);
        return STORE_ERR_SQLITE;
    }

    *out_entries = rows;
    *out_count = n;
    return STORE_OK;
}

StoreStatus store_purge_trash(Store *s, const char *now, Entry **out_entries, size_t *out_count)
{
    return store_purge(s, STORE_BIN_EXPIRED, now, out_entries, out_count);
}

/* ---- entry_links / rekey ------------------------------------------------- */

static const char *edge_kind_token(StoreEdgeKind k)
{
    switch (k) {
    case STORE_EDGE_RELATED:
        return "related";
    case STORE_EDGE_SUPERSEDES:
        return "supersedes";
    case STORE_EDGE_CITES:
        return "cites";
    default:
        return NULL;
    }
}

static int parse_edge_kind(const char *s, StoreEdgeKind *out)
{
    if (s == NULL || out == NULL) {
        return -1;
    }
    if (strcmp(s, "related") == 0) {
        *out = STORE_EDGE_RELATED;
        return 0;
    }
    if (strcmp(s, "supersedes") == 0) {
        *out = STORE_EDGE_SUPERSEDES;
        return 0;
    }
    if (strcmp(s, "cites") == 0) {
        *out = STORE_EDGE_CITES;
        return 0;
    }
    return -1;
}

void store_neighbor_free(StoreNeighbor *n)
{
    if (n == NULL) {
        return;
    }
    free(n->edge_updated_at);
    free(n->neighbor_key);
    free(n->neighbor_body);
    free(n->neighbor_expires_at);
    free(n->neighbor_sync_id);
    free(n->neighbor_deleted_at);
    memset(n, 0, sizeof(*n));
}

void store_neighbors_free(StoreNeighbor *rows, size_t count)
{
    size_t i = 0;
    if (rows == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        store_neighbor_free(&rows[i]);
    }
    free(rows);
}

static StoreStatus require_entry_id(sqlite3 *db, long long id, StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    int rc = 0;

    rc = sqlite3_prepare_v2(db, "SELECT 1 FROM entries WHERE local_id = ?1;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_int64(stmt, 1, id);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_NOT_FOUND;
    }
    if (rc != SQLITE_ROW) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(stmt);
    return STORE_OK;
}

/* a != b always: callers reject self-links before reaching here. Touching the
   same row twice would be harmless anyway. Bumps updated_at and version_vector. */
static StoreStatus bump_endpoints(Store *s, long long a, long long b, const char *now)
{
    Entry ea;
    Entry eb;
    StoreStatus st = STORE_OK;

    memset(&ea, 0, sizeof(ea));
    memset(&eb, 0, sizeof(eb));
    st = load_entry_by_id(s->db, a, &ea, &s->last);
    if (st != STORE_OK) {
        return st;
    }
    st = load_entry_by_id(s->db, b, &eb, &s->last);
    if (st != STORE_OK) {
        store_entry_free(&ea);
        return st;
    }
    st = touch_updated_at(s->db, a, now, &s->last);
    if (st == STORE_OK) {
        st = apply_vv_bump(s->db, a, s->device_id, ea.version_vector, &s->last);
    }
    if (st == STORE_OK) {
        st = touch_updated_at(s->db, b, now, &s->last);
    }
    if (st == STORE_OK) {
        st = apply_vv_bump(s->db, b, s->device_id, eb.version_vector, &s->last);
    }
    store_entry_free(&ea);
    store_entry_free(&eb);
    return st;
}

static StoreStatus fill_stub(sqlite3 *db, long long subject_id, StoreEdge row, StoreEdgeKind kind,
                             const char *edge_updated, StoreNeighbor *out, StoreError *serr)
{
    Entry e;
    long long nid = (row.from_id == subject_id) ? row.to_id : row.from_id;
    StoreStatus st = STORE_OK;

    memset(out, 0, sizeof(*out));
    memset(&e, 0, sizeof(e));
    st = load_entry_by_id(db, nid, &e, serr);
    if (st != STORE_OK) {
        return st;
    }
    out->subject_id = subject_id;
    out->from_id = row.from_id;
    out->to_id = row.to_id;
    out->kind = kind;
    out->edge_updated_at = dup_str(edge_updated);
    out->neighbor_id = e.id;
    out->neighbor_key = e.key;
    e.key = NULL;
    out->neighbor_body = e.body;
    e.body = NULL;
    out->neighbor_expires_at = e.expires_at;
    e.expires_at = NULL;
    out->neighbor_sync_id = e.sync_id;
    e.sync_id = NULL;
    out->neighbor_deleted_at = e.deleted_at;
    e.deleted_at = NULL;
    store_entry_free(&e);
    if (out->edge_updated_at == NULL || out->neighbor_body == NULL ||
        out->neighbor_sync_id == NULL) {
        store_neighbor_free(out);
        return STORE_ERR_OOM;
    }
    return STORE_OK;
}

static StoreStatus supersedes_reaches(sqlite3 *db, long long start, long long target, bool *out,
                                      StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    int rc = 0;

    *out = false;
    rc = sqlite3_prepare_v2(db,
                            "WITH RECURSIVE chain(id) AS ("
                            "  SELECT to_id FROM entry_links"
                            "   WHERE from_id = ?1 AND kind = 'supersedes'"
                            "  UNION" /* set UNION, not UNION ALL: a dense DAG must not explode */
                            "  SELECT e.to_id FROM entry_links e"
                            "   JOIN chain c ON e.from_id = c.id"
                            "   WHERE e.kind = 'supersedes'"
                            ") SELECT 1 FROM chain WHERE id = ?2 LIMIT 1;",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_int64(stmt, 1, start);
    (void)sqlite3_bind_int64(stmt, 2, target);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(stmt);
    if (rc == SQLITE_ROW) {
        *out = true;
    }
    return STORE_OK;
}

static StoreStatus find_edge(sqlite3 *db, StoreEdge edge, StoreEdgeKind kind, bool *present,
                             StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    int rc = 0;
    const char *tok = edge_kind_token(kind);

    *present = false;
    if (tok == NULL) {
        return STORE_ERR_INTERNAL;
    }
    rc = sqlite3_prepare_v2(
        db, "SELECT 1 FROM entry_links WHERE from_id = ?1 AND to_id = ?2 AND kind = ?3;", -1, &stmt,
        NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_int64(stmt, 1, edge.from_id);
    (void)sqlite3_bind_int64(stmt, 2, edge.to_id);
    (void)sqlite3_bind_text(stmt, 3, tok, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(stmt);
    if (rc == SQLITE_ROW) {
        *present = true;
    }
    return STORE_OK;
}

static StoreStatus insert_edge(sqlite3 *db, StoreEdge edge, StoreEdgeKind kind, const char *now,
                               StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    int rc = 0;
    const char *tok = edge_kind_token(kind);
    char link_id[UUID_STR_LEN + 1];

    if (tok == NULL || mint_uuid_v7(link_id) != 0) {
        return STORE_ERR_INTERNAL;
    }
    rc = sqlite3_prepare_v2(db,
                            "INSERT INTO entry_links(id, from_id, to_id, kind, created_at, "
                            "updated_at, version_vector, change_seq)"
                            " VALUES (?5, ?1, ?2, ?3, ?4, ?4, '{}', 0);",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_int64(stmt, 1, edge.from_id);
    (void)sqlite3_bind_int64(stmt, 2, edge.to_id);
    (void)sqlite3_bind_text(stmt, 3, tok, -1, SQLITE_STATIC);
    (void)sqlite3_bind_text(stmt, 4, now, -1, SQLITE_STATIC);
    (void)sqlite3_bind_text(stmt, 5, link_id, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(stmt);
    return STORE_OK;
}

static StoreStatus touch_edge(sqlite3 *db, StoreEdge edge, StoreEdgeKind kind, const char *now,
                              StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    int rc = 0;
    const char *tok = edge_kind_token(kind);

    if (tok == NULL) {
        return STORE_ERR_INTERNAL;
    }
    rc = sqlite3_prepare_v2(db,
                            "UPDATE entry_links SET updated_at = ?1"
                            " WHERE from_id = ?2 AND to_id = ?3 AND kind = ?4;",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_text(stmt, 1, now, -1, SQLITE_STATIC);
    (void)sqlite3_bind_int64(stmt, 2, edge.from_id);
    (void)sqlite3_bind_int64(stmt, 3, edge.to_id);
    (void)sqlite3_bind_text(stmt, 4, tok, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(stmt);
    return STORE_OK;
}

StoreStatus store_get_any(Store *s, long long id, Entry *out_entry)
{
    StoreStatus st = STORE_OK;

    if (s == NULL || s->db == NULL || out_entry == NULL) {
        return STORE_ERR_INTERNAL;
    }
    memset(out_entry, 0, sizeof(*out_entry));
    memset(&s->last, 0, sizeof(s->last));
    st = load_entry_by_id(s->db, id, out_entry, &s->last);
    if (st != STORE_OK) {
        return st;
    }
    if (out_entry->deleted_at != NULL) {
        store_entry_free(out_entry);
        return STORE_ERR_NOT_FOUND;
    }
    return STORE_OK;
}

StoreStatus store_get_any_by_key(Store *s, const char *key, Entry *out_entry)
{
    StoreStatus st = STORE_OK;

    if (s == NULL || s->db == NULL || key == NULL || out_entry == NULL) {
        return STORE_ERR_INTERNAL;
    }
    memset(out_entry, 0, sizeof(*out_entry));
    memset(&s->last, 0, sizeof(s->last));
    st = load_entry_by_key(s->db, key, out_entry, &s->last);
    if (st != STORE_OK) {
        return st;
    }
    if (out_entry->deleted_at != NULL) {
        store_entry_free(out_entry);
        return STORE_ERR_NOT_FOUND;
    }
    return STORE_OK;
}

StoreStatus store_get_any_by_sync_id(Store *s, const char *sync_id, Entry *out_entry)
{
    StoreStatus st = STORE_OK;

    if (s == NULL || s->db == NULL || sync_id == NULL || out_entry == NULL) {
        return STORE_ERR_INTERNAL;
    }
    memset(out_entry, 0, sizeof(*out_entry));
    memset(&s->last, 0, sizeof(s->last));
    st = load_entry_by_sync_id(s->db, sync_id, out_entry, &s->last);
    if (st != STORE_OK) {
        return st;
    }
    if (out_entry->deleted_at != NULL) {
        store_entry_free(out_entry);
        return STORE_ERR_NOT_FOUND;
    }
    return STORE_OK;
}

StoreStatus store_get_row(Store *s, long long id, Entry *out_entry)
{
    if (s == NULL || s->db == NULL || out_entry == NULL) {
        return STORE_ERR_INTERNAL;
    }
    memset(out_entry, 0, sizeof(*out_entry));
    memset(&s->last, 0, sizeof(s->last));
    return load_entry_by_id(s->db, id, out_entry, &s->last);
}

StoreStatus store_get_row_by_key(Store *s, const char *key, Entry *out_entry)
{
    if (s == NULL || s->db == NULL || key == NULL || out_entry == NULL) {
        return STORE_ERR_INTERNAL;
    }
    memset(out_entry, 0, sizeof(*out_entry));
    memset(&s->last, 0, sizeof(s->last));
    return load_entry_by_key(s->db, key, out_entry, &s->last);
}

StoreStatus store_get_row_by_sync_id(Store *s, const char *sync_id, Entry *out_entry)
{
    if (s == NULL || s->db == NULL || sync_id == NULL || out_entry == NULL) {
        return STORE_ERR_INTERNAL;
    }
    memset(out_entry, 0, sizeof(*out_entry));
    memset(&s->last, 0, sizeof(s->last));
    return load_entry_by_sync_id(s->db, sync_id, out_entry, &s->last);
}

int store_sync_id_is_canonical(const char *s)
{
    if (uuid_v7_is_canonical(s)) {
        return 1;
    }
    return 0;
}

StoreStatus store_link(Store *s, StoreEdge edge, StoreEdgeKind kind, const char *now,
                       StoreLinkAction *out_action, StoreNeighbor *out_stub)
{
    char err_unused[1];
    long long from_id = edge.from_id;
    long long to_id = edge.to_id;
    long long stored_from = from_id;
    long long stored_to = to_id;
    bool present = false;
    bool cycle = false;
    StoreStatus st = STORE_OK;

    if (s == NULL || s->db == NULL || now == NULL || out_action == NULL || out_stub == NULL) {
        return STORE_ERR_INTERNAL;
    }
    memset(out_stub, 0, sizeof(*out_stub));
    if (from_id == to_id) {
        return STORE_ERR_SELF_LINK;
    }
    if (edge_kind_token(kind) == NULL) {
        return STORE_ERR_INTERNAL;
    }
    if (kind == STORE_EDGE_RELATED) {
        stored_from = (from_id < to_id) ? from_id : to_id;
        stored_to = (from_id < to_id) ? to_id : from_id;
    }

    memset(&s->last, 0, sizeof(s->last));
    if (exec_sql(s->db, "BEGIN IMMEDIATE;", err_unused, 0U, &s->last) != 0) {
        return STORE_ERR_SQLITE;
    }
    st = require_entry_id(s->db, from_id, &s->last);
    if (st == STORE_OK) {
        st = require_entry_id(s->db, to_id, &s->last);
    }
    if (st != STORE_OK) {
        rollback_quiet(s->db);
        return st;
    }
    st = find_edge(s->db, (StoreEdge){.from_id = stored_from, .to_id = stored_to}, kind, &present,
                   &s->last);
    if (st != STORE_OK) {
        rollback_quiet(s->db);
        return st;
    }
    if (kind == STORE_EDGE_SUPERSEDES && !present) {
        st = supersedes_reaches(s->db, to_id, from_id, &cycle, &s->last);
        if (st != STORE_OK) {
            rollback_quiet(s->db);
            return st;
        }
        if (cycle) {
            rollback_quiet(s->db);
            return STORE_ERR_CYCLE;
        }
    }
    if (present) {
        st = touch_edge(s->db, (StoreEdge){.from_id = stored_from, .to_id = stored_to}, kind, now,
                        &s->last);
        *out_action = STORE_LINK_MERGED;
    } else {
        st = insert_edge(s->db, (StoreEdge){.from_id = stored_from, .to_id = stored_to}, kind, now,
                         &s->last);
        *out_action = STORE_LINK_CREATED;
    }
    if (st == STORE_OK) {
        st = bump_endpoints(s, from_id, to_id, now);
    }
    if (st != STORE_OK) {
        rollback_quiet(s->db);
        return st;
    }
    /* Read the stub inside the transaction (as store_unlink does) so a
       neighbor purged between COMMIT and the read cannot turn a committed
       link into a spurious not-found. */
    st = fill_stub(s->db, from_id, (StoreEdge){.from_id = stored_from, .to_id = stored_to}, kind,
                   now, out_stub, &s->last);
    if (st != STORE_OK) {
        rollback_quiet(s->db);
        return st;
    }
    if (exec_sql(s->db, "COMMIT;", err_unused, 0U, &s->last) != 0) {
        store_neighbor_free(out_stub);
        rollback_quiet(s->db);
        return STORE_ERR_SQLITE;
    }
    return STORE_OK;
}

static StoreStatus collect_unlink_matches(sqlite3 *db, sqlite3_stmt *sel, long long subject,
                                          StoreNeighbor **out, size_t *out_n, StoreError *serr)
{
    StoreNeighbor *rows = NULL;
    size_t n = 0U;
    size_t cap = 0U;
    int rc = 0;

    *out = NULL;
    *out_n = 0U;
    while ((rc = sqlite3_step(sel)) == SQLITE_ROW) {
        StoreNeighbor stub;
        StoreEdgeKind kind = STORE_EDGE_RELATED;
        long long from_id = sqlite3_column_int64(sel, 0);
        long long to_id = sqlite3_column_int64(sel, 1);
        const char *ktok = (const char *)sqlite3_column_text(sel, 2);
        const char *upd = (const char *)sqlite3_column_text(sel, 3);
        StoreNeighbor *grown = NULL;

        if (parse_edge_kind(ktok, &kind) != 0) {
            store_neighbors_free(rows, n);
            return STORE_ERR_SQLITE;
        }
        memset(&stub, 0, sizeof(stub));
        {
            StoreStatus st = fill_stub(db, subject, (StoreEdge){.from_id = from_id, .to_id = to_id},
                                       kind, upd, &stub, serr);
            if (st != STORE_OK) {
                store_neighbors_free(rows, n);
                return st;
            }
        }
        if (n == cap) {
            size_t ncap = (cap == 0U) ? 4U : (cap * 2U);
            grown = (StoreNeighbor *)realloc(rows, ncap * sizeof(*grown));
            if (grown == NULL) {
                store_neighbor_free(&stub);
                store_neighbors_free(rows, n);
                return STORE_ERR_OOM;
            }
            rows = grown;
            cap = ncap;
        }
        rows[n] = stub;
        n++;
    }
    if (rc != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        store_neighbors_free(rows, n);
        return STORE_ERR_SQLITE;
    }
    *out = rows;
    *out_n = n;
    return STORE_OK;
}

static int bind_edge_ends(sqlite3_stmt *stmt, long long from_id, long long to_id, const char *tok)
{
    (void)sqlite3_bind_int64(stmt, 1, from_id);
    (void)sqlite3_bind_int64(stmt, 2, to_id);
    if (tok != NULL) {
        (void)sqlite3_bind_text(stmt, 3, tok, -1, SQLITE_STATIC);
    }
    return 0;
}

StoreStatus store_unlink(Store *s, long long from_id, long long to_id, const StoreEdgeKind *kind,
                         const char *now, StoreNeighbor **out_stubs, size_t *out_count)
{
    char err_unused[1];
    sqlite3_stmt *sel = NULL;
    sqlite3_stmt *del = NULL;
    StoreStatus st = STORE_OK;
    int rc = 0;
    const char *tok = NULL;
    long long stored_from = from_id;
    long long stored_to = to_id;

    if (s == NULL || s->db == NULL || now == NULL || out_stubs == NULL || out_count == NULL) {
        return STORE_ERR_INTERNAL;
    }
    *out_stubs = NULL;
    *out_count = 0U;
    if (from_id == to_id) {
        return STORE_ERR_SELF_LINK;
    }
    if (kind != NULL) {
        tok = edge_kind_token(*kind);
        if (tok == NULL) {
            return STORE_ERR_INTERNAL;
        }
        if (*kind == STORE_EDGE_RELATED) {
            stored_from = (from_id < to_id) ? from_id : to_id;
            stored_to = (from_id < to_id) ? to_id : from_id;
        }
    }

    memset(&s->last, 0, sizeof(s->last));
    if (exec_sql(s->db, "BEGIN IMMEDIATE;", err_unused, 0U, &s->last) != 0) {
        return STORE_ERR_SQLITE;
    }

    rc = sqlite3_prepare_v2(
        s->db,
        kind == NULL ? "SELECT from_id, to_id, kind, updated_at FROM entry_links"
                       " WHERE (from_id = ?1 AND to_id = ?2) OR (from_id = ?2 AND to_id = ?1);"
                     : "SELECT from_id, to_id, kind, updated_at FROM entry_links"
                       " WHERE from_id = ?1 AND to_id = ?2 AND kind = ?3;",
        -1, &sel, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(s->db, &s->last);
        rollback_quiet(s->db);
        return STORE_ERR_SQLITE;
    }
    (void)bind_edge_ends(sel, stored_from, stored_to, tok);
    st = collect_unlink_matches(s->db, sel, from_id, out_stubs, out_count, &s->last);
    (void)sqlite3_finalize(sel);
    if (st != STORE_OK) {
        rollback_quiet(s->db);
        return st;
    }

    rc = sqlite3_prepare_v2(
        s->db,
        kind == NULL ? "DELETE FROM entry_links WHERE (from_id = ?1 AND to_id = ?2)"
                       " OR (from_id = ?2 AND to_id = ?1);"
                     : "DELETE FROM entry_links WHERE from_id = ?1 AND to_id = ?2 AND kind = ?3;",
        -1, &del, NULL);
    if (rc != SQLITE_OK) {
        store_neighbors_free(*out_stubs, *out_count);
        *out_stubs = NULL;
        *out_count = 0U;
        capture_sqlite_error(s->db, &s->last);
        rollback_quiet(s->db);
        return STORE_ERR_SQLITE;
    }
    (void)bind_edge_ends(del, stored_from, stored_to, tok);
    if (sqlite3_step(del) != SQLITE_DONE) {
        capture_sqlite_error(s->db, &s->last);
        (void)sqlite3_finalize(del);
        store_neighbors_free(*out_stubs, *out_count);
        *out_stubs = NULL;
        *out_count = 0U;
        rollback_quiet(s->db);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(del);

    if (*out_count > 0U) {
        st = bump_endpoints(s, from_id, to_id, now);
        if (st != STORE_OK) {
            store_neighbors_free(*out_stubs, *out_count);
            *out_stubs = NULL;
            *out_count = 0U;
            rollback_quiet(s->db);
            return st;
        }
    }

    if (exec_sql(s->db, "COMMIT;", err_unused, 0U, &s->last) != 0) {
        store_neighbors_free(*out_stubs, *out_count);
        *out_stubs = NULL;
        *out_count = 0U;
        rollback_quiet(s->db);
        return STORE_ERR_SQLITE;
    }
    return STORE_OK;
}

static StoreStatus dup_col_opt(sqlite3_stmt *stmt, int col, char **out)
{
    *out = NULL;
    if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
        return STORE_OK;
    }
    *out = dup_str((const char *)sqlite3_column_text(stmt, col));
    return (*out == NULL) ? STORE_ERR_OOM : STORE_OK;
}

static StoreStatus dup_col_req(sqlite3_stmt *stmt, int col, char **out)
{
    *out = dup_str((const char *)sqlite3_column_text(stmt, col));
    return (*out == NULL) ? STORE_ERR_OOM : STORE_OK;
}

/* Column order of the neighbor SELECT feeding neighbors_from_stmt. */
enum {
    NCOL_SUBJECT_ID = 0,
    NCOL_FROM_ID = 1,
    NCOL_TO_ID = 2,
    NCOL_KIND = 3,
    NCOL_EDGE_UPDATED_AT = 4,
    NCOL_NEIGHBOR_ID = 5,
    NCOL_NEIGHBOR_KEY = 6,
    NCOL_NEIGHBOR_BODY = 7,
    NCOL_NEIGHBOR_EXPIRES_AT = 8,
    NCOL_NEIGHBOR_SYNC_ID = 9,
    NCOL_NEIGHBOR_DELETED_AT = 10
};

static StoreStatus neighbor_row_from_stmt(sqlite3_stmt *stmt, StoreNeighbor *row)
{
    StoreEdgeKind kind = STORE_EDGE_RELATED;
    const char *ktok = (const char *)sqlite3_column_text(stmt, NCOL_KIND);

    memset(row, 0, sizeof(*row));
    if (parse_edge_kind(ktok, &kind) != 0) {
        return STORE_ERR_SQLITE;
    }
    row->subject_id = sqlite3_column_int64(stmt, NCOL_SUBJECT_ID);
    row->from_id = sqlite3_column_int64(stmt, NCOL_FROM_ID);
    row->to_id = sqlite3_column_int64(stmt, NCOL_TO_ID);
    row->kind = kind;
    row->neighbor_id = sqlite3_column_int64(stmt, NCOL_NEIGHBOR_ID);
    if (dup_col_req(stmt, NCOL_EDGE_UPDATED_AT, &row->edge_updated_at) != STORE_OK ||
        dup_col_opt(stmt, NCOL_NEIGHBOR_KEY, &row->neighbor_key) != STORE_OK ||
        dup_col_req(stmt, NCOL_NEIGHBOR_BODY, &row->neighbor_body) != STORE_OK ||
        dup_col_opt(stmt, NCOL_NEIGHBOR_EXPIRES_AT, &row->neighbor_expires_at) != STORE_OK ||
        dup_col_req(stmt, NCOL_NEIGHBOR_SYNC_ID, &row->neighbor_sync_id) != STORE_OK ||
        dup_col_opt(stmt, NCOL_NEIGHBOR_DELETED_AT, &row->neighbor_deleted_at) != STORE_OK) {
        store_neighbor_free(row);
        return STORE_ERR_OOM;
    }
    return STORE_OK;
}

static StoreStatus neighbor_append(StoreNeighbor **rows, size_t *n, size_t *cap, StoreNeighbor *row)
{
    if (*n == *cap) {
        size_t ncap = (*cap == 0U) ? (size_t)GROW_MIN_CAP : (*cap * 2U);
        StoreNeighbor *grown = (StoreNeighbor *)realloc(*rows, ncap * sizeof(*grown));
        if (grown == NULL) {
            store_neighbor_free(row);
            store_neighbors_free(*rows, *n);
            *rows = NULL;
            *n = 0U;
            *cap = 0U;
            return STORE_ERR_OOM;
        }
        *rows = grown;
        *cap = ncap;
    }
    (*rows)[*n] = *row;
    memset(row, 0, sizeof(*row));
    (*n)++;
    return STORE_OK;
}

static StoreStatus neighbors_from_stmt(sqlite3 *db, sqlite3_stmt *stmt, StoreNeighbor **out,
                                       size_t *out_n, StoreError *serr)
{
    StoreNeighbor *rows = NULL;
    size_t n = 0U;
    size_t cap = 0U;
    int rc = 0;

    *out = NULL;
    *out_n = 0U;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        StoreNeighbor row;
        StoreStatus st = neighbor_row_from_stmt(stmt, &row);
        if (st != STORE_OK) {
            store_neighbors_free(rows, n);
            return st;
        }
        st = neighbor_append(&rows, &n, &cap, &row);
        if (st != STORE_OK) {
            return st;
        }
    }
    if (rc != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        store_neighbors_free(rows, n);
        return STORE_ERR_SQLITE;
    }
    *out = rows;
    *out_n = n;
    return STORE_OK;
}

static int build_neighbors_sql(char *sql, size_t cap, NeighborQuery query, const char *tok)
{
    size_t pos = 0U;
    int n = 0;

    n = snprintf(
        sql, cap,
        "SELECT %lld, l.from_id, l.to_id, l.kind, l.updated_at,"
        " n.local_id, n.key, n.body, n.expires_at, n.id, n.deleted_at"
        " FROM entry_links l"
        " JOIN entries n ON n.local_id = CASE WHEN l.from_id = ?1 THEN l.to_id ELSE l.from_id END"
        " WHERE (l.from_id = ?1 OR l.to_id = ?1)",
        query.subject_id);
    if (n < 0 || (size_t)n >= cap) {
        return -1;
    }
    pos = (size_t)n;
    if (query.dir == STORE_NEIGHBOR_OUTGOING || query.dir == STORE_NEIGHBOR_INCOMING) {
        n = snprintf(sql + pos, cap - pos,
                     query.dir == STORE_NEIGHBOR_OUTGOING
                         ? " AND (l.kind = 'related' OR l.from_id = ?1)"
                         : " AND (l.kind = 'related' OR l.to_id = ?1)");
        if (n < 0 || (size_t)n >= cap - pos) {
            return -1;
        }
        pos += (size_t)n;
    }
    if (tok != NULL) {
        n = snprintf(sql + pos, cap - pos, " AND l.kind = ?2");
        if (n < 0 || (size_t)n >= cap - pos) {
            return -1;
        }
        pos += (size_t)n;
    }
    n = snprintf(sql + pos, cap - pos, " ORDER BY l.updated_at DESC, n.local_id DESC;");
    if (n < 0 || (size_t)n >= cap - pos) {
        return -1;
    }
    return 0;
}

StoreStatus store_list_neighbors(Store *s, long long subject_id, const StoreEdgeKind *kind,
                                 StoreNeighborDir dir, const char *now, StoreNeighbor **out,
                                 size_t *out_count)
{
    enum { NEIGHBORS_SQL_CAP = 768 };
    sqlite3_stmt *stmt = NULL;
    char sql[NEIGHBORS_SQL_CAP];
    int rc = 0;
    const char *tok = NULL;
    StoreStatus st = STORE_OK;

    if (s == NULL || s->db == NULL || now == NULL || out == NULL || out_count == NULL) {
        return STORE_ERR_INTERNAL;
    }
    *out = NULL;
    *out_count = 0U;
    if (kind != NULL) {
        tok = edge_kind_token(*kind);
        if (tok == NULL) {
            return STORE_ERR_INTERNAL;
        }
    }
    if (build_neighbors_sql(sql, sizeof(sql), (NeighborQuery){.subject_id = subject_id, .dir = dir},
                            tok) != 0) {
        return STORE_ERR_INTERNAL;
    }

    memset(&s->last, 0, sizeof(s->last));
    rc = sqlite3_prepare_v2(s->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(s->db, &s->last);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_int64(stmt, 1, subject_id);
    if (tok != NULL) {
        (void)sqlite3_bind_text(stmt, 2, tok, -1, SQLITE_STATIC);
    }
    st = neighbors_from_stmt(s->db, stmt, out, out_count, &s->last);
    (void)sqlite3_finalize(stmt);
    return st;
}

StoreStatus store_list_neighbors_for(Store *s, const long long *ids, size_t nids, const char *now,
                                     StoreNeighbor **out, size_t *out_count)
{
    enum { NEIGHBORS_FOR_SQL_CAP = 8192 };
    sqlite3_stmt *stmt = NULL;
    char sql[NEIGHBORS_FOR_SQL_CAP];
    size_t pos = 0U;
    size_t i = 0;
    int n = 0;
    int rc = 0;
    StoreStatus st = STORE_OK;

    if (s == NULL || s->db == NULL || now == NULL || out == NULL || out_count == NULL ||
        (nids > 0U && ids == NULL)) {
        return STORE_ERR_INTERNAL;
    }
    *out = NULL;
    *out_count = 0U;
    if (nids == 0U) {
        return STORE_OK;
    }

    n = snprintf(sql, sizeof(sql),
                 "SELECT s.local_id, l.from_id, l.to_id, l.kind, l.updated_at,"
                 " n.local_id, n.key, n.body, n.expires_at, n.id, n.deleted_at"
                 " FROM entries s"
                 " JOIN entry_links l ON l.from_id = s.local_id OR l.to_id = s.local_id"
                 " JOIN entries n ON n.local_id = CASE WHEN l.from_id = s.local_id THEN l.to_id "
                 "ELSE l.from_id END"
                 " WHERE s.local_id IN (");
    if (n < 0 || (size_t)n >= sizeof(sql)) {
        return STORE_ERR_INTERNAL;
    }
    pos = (size_t)n;
    for (i = 0; i < nids; i++) {
        n = snprintf(sql + pos, sizeof(sql) - pos, "%s?%d", (i == 0U) ? "" : ",", (int)i + 1);
        if (n < 0 || (size_t)n >= sizeof(sql) - pos) {
            return STORE_ERR_INTERNAL;
        }
        pos += (size_t)n;
    }
    n = snprintf(sql + pos, sizeof(sql) - pos, ") ORDER BY l.updated_at DESC, n.local_id DESC;");
    if (n < 0 || (size_t)n >= sizeof(sql) - pos) {
        return STORE_ERR_INTERNAL;
    }

    memset(&s->last, 0, sizeof(s->last));
    rc = sqlite3_prepare_v2(s->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(s->db, &s->last);
        return STORE_ERR_SQLITE;
    }
    for (i = 0; i < nids; i++) {
        (void)sqlite3_bind_int64(stmt, (int)i + 1, ids[i]);
    }
    st = neighbors_from_stmt(s->db, stmt, out, out_count, &s->last);
    (void)sqlite3_finalize(stmt);
    return st;
}

static StoreStatus load_body_hash(sqlite3 *db, long long id, char **out_hash, StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    int rc = 0;

    *out_hash = NULL;
    rc = sqlite3_prepare_v2(db, "SELECT body_hash FROM entries WHERE local_id = ?1;", -1, &stmt,
                            NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_int64(stmt, 1, id);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        if (rc != SQLITE_DONE) {
            capture_sqlite_error(db, serr);
        }
        (void)sqlite3_finalize(stmt);
        return (rc == SQLITE_DONE) ? STORE_ERR_NOT_FOUND : STORE_ERR_SQLITE;
    }
    *out_hash = dup_str((const char *)sqlite3_column_text(stmt, 0));
    (void)sqlite3_finalize(stmt);
    return (*out_hash == NULL) ? STORE_ERR_OOM : STORE_OK;
}

static StoreStatus write_key(sqlite3 *db, long long id, const char *key_or_null, StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    int rc = 0;

    rc =
        sqlite3_prepare_v2(db, "UPDATE entries SET key = ?1 WHERE local_id = ?2;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    if (key_or_null == NULL) {
        (void)sqlite3_bind_null(stmt, 1);
    } else {
        (void)sqlite3_bind_text(stmt, 1, key_or_null, -1, SQLITE_STATIC);
    }
    (void)sqlite3_bind_int64(stmt, 2, id);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(stmt);
    return STORE_OK;
}

static StoreStatus rekey_set(sqlite3 *db, long long entry_id, const char *current_key,
                             const char *new_key, long long *out_conflict_id, StoreError *serr)
{
    long long other = 0;
    StoreStatus st = STORE_OK;

    if (current_key != NULL && strcmp(current_key, new_key) == 0) {
        return STORE_OK;
    }
    st = find_id_by_key(db, new_key, &other, serr);
    if (st == STORE_OK && other != entry_id) {
        if (out_conflict_id != NULL) {
            *out_conflict_id = other;
        }
        return STORE_ERR_CONFLICT;
    }
    if (st != STORE_OK && st != STORE_ERR_NOT_FOUND) {
        return st;
    }
    return write_key(db, entry_id, new_key, serr);
}

static StoreStatus rekey_clear(sqlite3 *db, long long entry_id, const char *current_key,
                               long long *out_conflict_id, StoreError *serr)
{
    char *hash = NULL;
    long long other = 0;
    StoreStatus st = STORE_OK;

    if (current_key == NULL) {
        return STORE_OK;
    }
    st = load_body_hash(db, entry_id, &hash, serr);
    if (st != STORE_OK) {
        return st;
    }
    st = find_keyless_by_hash(db, hash, &other, serr);
    free(hash);
    if (st == STORE_OK && other != entry_id) {
        if (out_conflict_id != NULL) {
            *out_conflict_id = other;
        }
        return STORE_ERR_CONFLICT;
    }
    if (st != STORE_OK && st != STORE_ERR_NOT_FOUND) {
        return st;
    }
    return write_key(db, entry_id, NULL, serr);
}

StoreStatus store_rekey(Store *s, long long id, RekeyKeys keys, StoreBin bin, const char *now,
                        Entry *out_entry, long long *out_conflict_id)
{
    const char *key_or_null = keys.key_or_null;
    const char *new_key_or_null = keys.new_key_or_null;
    char err_unused[1];
    Entry current;
    StoreStatus st = STORE_OK;
    long long entry_id = 0;

    if (out_conflict_id != NULL) {
        *out_conflict_id = 0;
    }
    if (s == NULL || s->db == NULL || now == NULL || out_entry == NULL) {
        return STORE_ERR_INTERNAL;
    }
    if (key_or_null == NULL && id < 1) {
        return STORE_ERR_INTERNAL;
    }
    if (new_key_or_null != NULL && new_key_or_null[0] == '\0') {
        return STORE_ERR_INTERNAL;
    }
    memset(&current, 0, sizeof(current));
    memset(out_entry, 0, sizeof(*out_entry));
    memset(&s->last, 0, sizeof(s->last));

    if (exec_sql(s->db, "BEGIN IMMEDIATE;", err_unused, 0U, &s->last) != 0) {
        return STORE_ERR_SQLITE;
    }
    st = load_then_check_bin(s->db, id, key_or_null, NULL, bin, now, &current, &s->last);
    if (st != STORE_OK) {
        rollback_quiet(s->db);
        return st;
    }
    entry_id = current.id;

    if (new_key_or_null != NULL) {
        st = rekey_set(s->db, entry_id, current.key, new_key_or_null, out_conflict_id, &s->last);
    } else {
        st = rekey_clear(s->db, entry_id, current.key, out_conflict_id, &s->last);
    }
    if (st == STORE_OK) {
        st = touch_updated_at(s->db, entry_id, now, &s->last);
    }
    if (st == STORE_OK) {
        st = apply_vv_bump(s->db, entry_id, s->device_id, current.version_vector, &s->last);
    }
    if (st == STORE_OK) {
        st = fts_resync(s->db, entry_id, &s->last);
    }
    store_entry_free(&current);
    if (st != STORE_OK) {
        rollback_quiet(s->db);
        return st;
    }
    if (exec_sql(s->db, "COMMIT;", err_unused, 0U, &s->last) != 0) {
        rollback_quiet(s->db);
        return STORE_ERR_SQLITE;
    }
    return load_entry_by_id(s->db, entry_id, out_entry, &s->last);
}

/* ---- sync import / conflicts (plan 14 stage 5, 005 Round 7) ------------- */

typedef struct {
    char *device_id;
    unsigned long long clock;
} VvPair;

static void free_tag_list(char **tags, size_t n)
{
    size_t i = 0;

    if (tags == NULL) {
        return;
    }
    for (i = 0; i < n; i++) {
        free(tags[i]);
    }
    free((void *)tags);
}

static void vv_pairs_free(VvPair *pairs, size_t n)
{
    size_t i = 0;

    if (pairs == NULL) {
        return;
    }
    for (i = 0; i < n; i++) {
        free(pairs[i].device_id);
    }
    free(pairs);
}

/* Parse compact VV object via JSON1 into heap pairs. Empty {} → n=0. */
static StoreStatus vv_parse_pairs(sqlite3 *db, const char *vv_json, VvPair **out, size_t *out_n,
                                  StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    VvPair *pairs = NULL;
    size_t n = 0U;
    size_t cap = 0U;
    int rc = 0;

    *out = NULL;
    *out_n = 0U;
    if (vv_json == NULL || vv_json[0] != '{') {
        return STORE_ERR_INTERNAL;
    }
    rc = sqlite3_prepare_v2(db, "SELECT key, CAST(value AS INTEGER) FROM json_each(?1);", -1, &stmt,
                            NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_text(stmt, 1, vv_json, -1, SQLITE_STATIC);
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *key = (const char *)sqlite3_column_text(stmt, 0);
        char *copy = NULL;
        VvPair *grown = NULL;

        if (key == NULL || key[0] == '\0') {
            continue;
        }
        copy = dup_str(key);
        if (copy == NULL) {
            vv_pairs_free(pairs, n);
            (void)sqlite3_finalize(stmt);
            return STORE_ERR_OOM;
        }
        if (n == cap) {
            size_t ncap = (cap == 0U) ? (size_t)GROW_MIN_CAP : cap * 2U;
            grown = (VvPair *)realloc(pairs, ncap * sizeof(*pairs));
            if (grown == NULL) {
                free(copy);
                vv_pairs_free(pairs, n);
                (void)sqlite3_finalize(stmt);
                return STORE_ERR_OOM;
            }
            pairs = grown;
            cap = ncap;
        }
        pairs[n].device_id = copy;
        pairs[n].clock = (unsigned long long)sqlite3_column_int64(stmt, 1);
        n++;
    }
    if (rc != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        vv_pairs_free(pairs, n);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(stmt);
    *out = pairs;
    *out_n = n;
    return STORE_OK;
}

static unsigned long long vv_clock_of(const VvPair *pairs, size_t n, const char *device_id)
{
    size_t i = 0;

    for (i = 0; i < n; i++) {
        if (strcmp(pairs[i].device_id, device_id) == 0) {
            return pairs[i].clock;
        }
    }
    return 0ULL;
}

/*
 * -1 local dominates, 1 incoming dominates, 0 concurrent or equal.
 * Missing device key = 0. Dominates iff every counter >= and at least one >.
 */
static StoreStatus vv_compare(sqlite3 *db, const char *local_vv, const char *incoming_vv, int *out,
                              StoreError *serr)
{
    VvPair *loc = NULL;
    VvPair *inc = NULL;
    size_t nloc = 0U;
    size_t ninc = 0U;
    size_t i = 0;
    int any_loc_gt = 0;
    int any_inc_gt = 0;
    StoreStatus st = STORE_OK;

    if (out == NULL) {
        return STORE_ERR_INTERNAL;
    }
    *out = 0;
    st = vv_parse_pairs(db, local_vv, &loc, &nloc, serr);
    if (st != STORE_OK) {
        return st;
    }
    st = vv_parse_pairs(db, incoming_vv, &inc, &ninc, serr);
    if (st != STORE_OK) {
        vv_pairs_free(loc, nloc);
        return st;
    }
    for (i = 0; i < nloc; i++) {
        unsigned long long a = loc[i].clock;
        unsigned long long b = vv_clock_of(inc, ninc, loc[i].device_id);

        if (a > b) {
            any_loc_gt = 1;
        } else if (a < b) {
            any_inc_gt = 1;
        }
    }
    for (i = 0; i < ninc; i++) {
        unsigned long long b = inc[i].clock;
        unsigned long long a = vv_clock_of(loc, nloc, inc[i].device_id);

        if (b > a) {
            any_inc_gt = 1;
        } else if (b < a) {
            any_loc_gt = 1;
        }
    }
    vv_pairs_free(loc, nloc);
    vv_pairs_free(inc, ninc);
    if (any_inc_gt && !any_loc_gt) {
        *out = 1;
    } else if (any_loc_gt && !any_inc_gt) {
        *out = -1;
    } else {
        *out = 0;
    }
    return STORE_OK;
}

/* A guard token is one JSON object of device id to non-negative integer.
   json_type and json_each error on malformed JSON; CASE evaluates only the
   branch it needs, so json_valid guards them. An integer that does not fit in
   int64 fails `value != CAST(value AS INTEGER)`: SQLite reports it as integer
   and saturates the cast, so 2^63 would compare equal to 2^63-1. A repeated
   key is malformed too: vv_clock_of reads the first copy, so agreeing copies
   pass as one clock and disagreeing ones do not match themselves. */
static StoreStatus vv_reject_malformed(sqlite3 *db, const char *given, StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    int rc = 0;
    int bad = 1;

    rc = sqlite3_prepare_v2(db,
                            "SELECT CASE WHEN json_valid(?1) = 0 THEN 1 "
                            "WHEN json_type(?1) != 'object' THEN 1 "
                            "ELSE EXISTS (SELECT 1 FROM json_each(?1) "
                            "WHERE type != 'integer' OR atom < 0 OR ifnull(key, '') = '' "
                            "OR value != CAST(value AS INTEGER)) "
                            "OR EXISTS (SELECT key FROM json_each(?1) "
                            "GROUP BY key HAVING COUNT(*) > 1) END;",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_text(stmt, 1, given, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        bad = sqlite3_column_int(stmt, 0);
    }
    (void)sqlite3_finalize(stmt);
    if (rc != SQLITE_ROW) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    return bad != 0 ? STORE_ERR_BAD_VV : STORE_OK;
}

/* Equal when every device counter matches. A missing device counts as 0.
   vv_compare returns 0 for both equal and concurrent, so it cannot guard. */
static StoreStatus version_vector_guard(sqlite3 *db, const char *row_vv, const char *given,
                                        StoreError *serr)
{
    VvPair *row = NULL;
    VvPair *got = NULL;
    size_t nrow = 0U;
    size_t ngot = 0U;
    size_t i = 0U;
    StoreStatus st = STORE_OK;

    st = vv_reject_malformed(db, given, serr);
    if (st != STORE_OK) {
        return st;
    }
    /* A stored counter that does not fit int64 must not saturate into a match. */
    st = vv_reject_malformed(db, row_vv, serr);
    if (st == STORE_ERR_BAD_VV) {
        return STORE_ERR_STALE;
    }
    if (st != STORE_OK) {
        return st;
    }
    st = vv_parse_pairs(db, row_vv, &row, &nrow, serr);
    if (st != STORE_OK) {
        return st;
    }
    st = vv_parse_pairs(db, given, &got, &ngot, serr);
    if (st != STORE_OK) {
        vv_pairs_free(row, nrow);
        return st;
    }
    for (i = 0; i < nrow; i++) {
        if (row[i].clock != vv_clock_of(got, ngot, row[i].device_id)) {
            vv_pairs_free(row, nrow);
            vv_pairs_free(got, ngot);
            return STORE_ERR_STALE;
        }
    }
    for (i = 0; i < ngot; i++) {
        if (got[i].clock != vv_clock_of(row, nrow, got[i].device_id)) {
            vv_pairs_free(row, nrow);
            vv_pairs_free(got, ngot);
            return STORE_ERR_STALE;
        }
    }
    vv_pairs_free(row, nrow);
    vv_pairs_free(got, ngot);
    return STORE_OK;
}

static int vv_device_in_pairs(const VvPair *pairs, size_t count, const char *device_id)
{
    size_t i = 0;

    for (i = 0; i < count; i++) {
        if (strcmp(pairs[i].device_id, device_id) == 0) {
            return 1;
        }
    }
    return 0;
}

static int vv_append_field(char *out, size_t outlen, size_t *pos, int *first, const char *device_id,
                           unsigned long long clock)
{
    int written = 0;

    written =
        snprintf(out + *pos, outlen - *pos, "%s\"%s\":%llu", *first ? "" : ",", device_id, clock);
    if (written < 0 || (size_t)written >= outlen - *pos) {
        return -1;
    }
    *pos += (size_t)written;
    *first = 0;
    return 0;
}

/* Pairwise max of two VVs into out[outlen]. Compact JSON, no spaces. */
static StoreStatus vv_pairwise_max(sqlite3 *db, const char *a_vv, const char *b_vv, char *out,
                                   size_t outlen, StoreError *serr)
{
    VvPair *pairs_a = NULL;
    VvPair *pairs_b = NULL;
    size_t count_a = 0U;
    size_t count_b = 0U;
    size_t i = 0;
    size_t pos = 0U;
    int first = 1;
    StoreStatus st = STORE_OK;

    if (out == NULL || outlen < (size_t)VV_JSON_MIN_OUT) {
        return STORE_ERR_INTERNAL;
    }
    st = vv_parse_pairs(db, a_vv, &pairs_a, &count_a, serr);
    if (st != STORE_OK) {
        return st;
    }
    st = vv_parse_pairs(db, b_vv, &pairs_b, &count_b, serr);
    if (st != STORE_OK) {
        vv_pairs_free(pairs_a, count_a);
        return st;
    }
    out[0] = '{';
    pos = 1U;
    for (i = 0; i < count_a; i++) {
        unsigned long long clock_a = pairs_a[i].clock;
        unsigned long long clock_b = vv_clock_of(pairs_b, count_b, pairs_a[i].device_id);
        unsigned long long merged = (clock_a > clock_b) ? clock_a : clock_b;

        if (vv_append_field(out, outlen, &pos, &first, pairs_a[i].device_id, merged) != 0) {
            vv_pairs_free(pairs_a, count_a);
            vv_pairs_free(pairs_b, count_b);
            return STORE_ERR_INTERNAL;
        }
    }
    for (i = 0; i < count_b; i++) {
        /* Skip if already emitted from a's loop, including a-device with clock 0. */
        if (vv_device_in_pairs(pairs_a, count_a, pairs_b[i].device_id)) {
            continue;
        }
        if (vv_append_field(out, outlen, &pos, &first, pairs_b[i].device_id, pairs_b[i].clock) !=
            0) {
            vv_pairs_free(pairs_a, count_a);
            vv_pairs_free(pairs_b, count_b);
            return STORE_ERR_INTERNAL;
        }
    }
    vv_pairs_free(pairs_a, count_a);
    vv_pairs_free(pairs_b, count_b);
    if (pos + 1U >= outlen) {
        return STORE_ERR_INTERNAL;
    }
    out[pos] = '}';
    out[pos + 1U] = '\0';
    return STORE_OK;
}

static StoreStatus write_version_vector(sqlite3 *db, long long id, const char *vv_json,
                                        StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    int rc = 0;

    rc = sqlite3_prepare_v2(db, "UPDATE entries SET version_vector = ?1 WHERE local_id = ?2;", -1,
                            &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_text(stmt, 1, vv_json, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_int64(stmt, 2, id);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(stmt);
    return STORE_OK;
}

/* Union a and b then bump local device; write VV (+ optional updated_at). */
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
static StoreStatus vv_union_bump(sqlite3 *db, long long id, const char *a_vv, const char *b_vv,
                                 const char *device_id, const char *now_or_null, StoreError *serr)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    char maxed[VV_OUT_MAX];
    char bumped[VV_OUT_MAX];
    StoreStatus st = STORE_OK;

    st = vv_pairwise_max(db, a_vv, b_vv, maxed, sizeof(maxed), serr);
    if (st != STORE_OK) {
        return st;
    }
    if (vv_increment(maxed, device_id, bumped, sizeof(bumped)) != 0) {
        return STORE_ERR_INTERNAL;
    }
    st = write_version_vector(db, id, bumped, serr);
    if (st != STORE_OK) {
        return st;
    }
    if (now_or_null != NULL) {
        return touch_updated_at(db, id, now_or_null, serr);
    }
    return STORE_OK;
}

enum { ASCII_C0_MAX = 0x20 }; /* first printable ASCII */

static int json_putc_escape(FILE *fp, unsigned char c)
{
    switch (c) {
    case '"':
        return fputs("\\\"", fp);
    case '\\':
        return fputs("\\\\", fp);
    case '\b':
        return fputs("\\b", fp);
    case '\f':
        return fputs("\\f", fp);
    case '\n':
        return fputs("\\n", fp);
    case '\r':
        return fputs("\\r", fp);
    case '\t':
        return fputs("\\t", fp);
    default:
        if (c < (unsigned char)ASCII_C0_MAX) {
            return fprintf(fp, "\\u%04x", (unsigned)c);
        }
        return fputc((int)c, fp);
    }
}

static int json_write_string(FILE *fp, const char *s)
{
    static const char k_empty[] = "";
    const unsigned char *p = NULL;
    const char *text = (s != NULL) ? s : k_empty;

    if (fputc('"', fp) == EOF) {
        return -1;
    }
    for (p = (const unsigned char *)text; *p != '\0'; p++) {
        if (json_putc_escape(fp, *p) < 0) {
            return -1;
        }
    }
    return (fputc('"', fp) == EOF) ? -1 : 0;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
static int json_write_nullable(FILE *fp, const char *name, const char *value)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    if (fprintf(fp, ",\"%s\":", name) < 0) {
        return -1;
    }
    if (value == NULL) {
        return fputs("null", fp) < 0 ? -1 : 0;
    }
    return json_write_string(fp, value);
}

static const char *bin_token_store(StoreBin bin)
{
    switch (bin) {
    case STORE_BIN_LIVE:
        return "live";
    case STORE_BIN_EXPIRED:
        return "expired";
    case STORE_BIN_DELETED:
        return "deleted";
    default:
        return NULL;
    }
}

/* Entry-like JSON without links (005 field order). Caller frees *out.
 * ponytail: RFC 8259 escape is duplicated with output.c; extract if snapshots drift. */
static StoreStatus entry_snapshot_json(const Entry *e, const char *now, char **out)
{
    FILE *fp = NULL;
    char *buf = NULL;
    size_t buflen = 0U;
    const char *bin = NULL;
    size_t i = 0;

    *out = NULL;
    if (e == NULL || now == NULL || e->sync_id == NULL ||
        !vv_json_is_canonical(e->version_vector)) {
        return STORE_ERR_INTERNAL;
    }
    bin = bin_token_store(store_bin_of(e->deleted_at, e->expires_at, now));
    if (bin == NULL) {
        return STORE_ERR_INTERNAL;
    }
    fp = open_memstream(&buf, &buflen);
    if (fp == NULL) {
        return STORE_ERR_OOM;
    }
    if (fprintf(fp, "{\"id\":%lld,\"sync_id\":", e->id) < 0 ||
        json_write_string(fp, e->sync_id) != 0 || json_write_nullable(fp, "key", e->key) != 0 ||
        fprintf(fp, ",\"body\":") < 0 || json_write_string(fp, e->body) != 0 ||
        fputs(",\"tags\":[", fp) < 0) {
        (void)fclose(fp);
        free(buf);
        return STORE_ERR_OOM;
    }
    for (i = 0; i < e->ntags; i++) {
        if (i > 0U && fputc(',', fp) == EOF) {
            (void)fclose(fp);
            free(buf);
            return STORE_ERR_OOM;
        }
        if (json_write_string(fp, e->tags[i]) != 0) {
            (void)fclose(fp);
            free(buf);
            return STORE_ERR_OOM;
        }
    }
    if (fputs("]", fp) < 0 || fprintf(fp, ",\"source\":") < 0 ||
        json_write_string(fp, e->source != NULL ? e->source : "unknown") != 0 ||
        fprintf(fp, ",\"created_at\":") < 0 || json_write_string(fp, e->created_at) != 0 ||
        fprintf(fp, ",\"updated_at\":") < 0 || json_write_string(fp, e->updated_at) != 0 ||
        json_write_nullable(fp, "expires_at", e->expires_at) != 0 ||
        json_write_nullable(fp, "deleted_at", e->deleted_at) != 0 || fprintf(fp, ",\"bin\":") < 0 ||
        json_write_string(fp, bin) != 0 ||
        fprintf(fp, ",\"version_vector\":%s}", e->version_vector) < 0) {
        (void)fclose(fp);
        free(buf);
        return STORE_ERR_OOM;
    }
    if (fclose(fp) != 0) {
        free(buf);
        return STORE_ERR_OOM;
    }
    *out = buf;
    return STORE_OK;
}

const char *store_conflict_reason_str(StoreConflictReason reason)
{
    switch (reason) {
    case STORE_CONFLICT_CONCURRENT_VV:
        return "concurrent_vv";
    case STORE_CONFLICT_KEY_CLASH:
        return "key_clash";
    case STORE_CONFLICT_HASH_CLASH:
        return "hash_clash";
    default:
        return NULL;
    }
}

static StoreStatus conflict_reason_parse(const char *s, StoreConflictReason *out)
{
    if (s == NULL || out == NULL) {
        return STORE_ERR_INTERNAL;
    }
    if (strcmp(s, "concurrent_vv") == 0) {
        *out = STORE_CONFLICT_CONCURRENT_VV;
        return STORE_OK;
    }
    if (strcmp(s, "key_clash") == 0) {
        *out = STORE_CONFLICT_KEY_CLASH;
        return STORE_OK;
    }
    if (strcmp(s, "hash_clash") == 0) {
        *out = STORE_CONFLICT_HASH_CLASH;
        return STORE_OK;
    }
    return STORE_ERR_INTERNAL;
}

static StoreStatus insert_conflict(sqlite3 *db, const char *sync_id, StoreConflictReason reason,
                                   const char *local_json, const char *incoming_json,
                                   const char *now, StoreError *serr)
{
    enum { BIND_SYNC = 1, BIND_REASON = 2, BIND_LOCAL = 3, BIND_INCOMING = 4, BIND_CREATED = 5 };
    sqlite3_stmt *stmt = NULL;
    const char *r = store_conflict_reason_str(reason);
    int rc = 0;

    if (r == NULL || sync_id == NULL || local_json == NULL || incoming_json == NULL ||
        now == NULL) {
        return STORE_ERR_INTERNAL;
    }
    rc = sqlite3_prepare_v2(db,
                            "INSERT INTO conflicts(sync_id, reason, local_json, incoming_json, "
                            "created_at) VALUES (?1, ?2, ?3, ?4, ?5);",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_text(stmt, BIND_SYNC, sync_id, -1, SQLITE_STATIC);
    (void)sqlite3_bind_text(stmt, BIND_REASON, r, -1, SQLITE_STATIC);
    (void)sqlite3_bind_text(stmt, BIND_LOCAL, local_json, -1, SQLITE_STATIC);
    (void)sqlite3_bind_text(stmt, BIND_INCOMING, incoming_json, -1, SQLITE_STATIC);
    (void)sqlite3_bind_text(stmt, BIND_CREATED, now, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(stmt);
    return STORE_OK;
}

/* Refresh snapshots on an existing open conflict (same sync_id + reason). */
static StoreStatus update_conflict_snapshots(sqlite3 *db, long long id, const char *local_json,
                                             const char *incoming_json, StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    int rc = 0;

    rc = sqlite3_prepare_v2(
        db, "UPDATE conflicts SET local_json = ?1, incoming_json = ?2 WHERE id = ?3;", -1, &stmt,
        NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_text(stmt, 1, local_json, -1, SQLITE_STATIC);
    (void)sqlite3_bind_text(stmt, 2, incoming_json, -1, SQLITE_STATIC);
    (void)sqlite3_bind_int64(stmt, 3, id);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(stmt);
    return STORE_OK;
}

/* Find open conflict by sync_id + reason. NOT_FOUND if none. */
static StoreStatus find_open_conflict(sqlite3 *db, const char *sync_id, StoreConflictReason reason,
                                      long long *out_id, StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    const char *r = store_conflict_reason_str(reason);
    int rc = 0;

    if (r == NULL || sync_id == NULL || out_id == NULL) {
        return STORE_ERR_INTERNAL;
    }
    *out_id = 0;
    rc = sqlite3_prepare_v2(db,
                            "SELECT id FROM conflicts WHERE sync_id = ?1 AND reason = ?2 "
                            "ORDER BY id ASC LIMIT 1;",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_text(stmt, 1, sync_id, -1, SQLITE_STATIC);
    (void)sqlite3_bind_text(stmt, 2, r, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_NOT_FOUND;
    }
    if (rc != SQLITE_ROW) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    *out_id = sqlite3_column_int64(stmt, 0);
    (void)sqlite3_finalize(stmt);
    return STORE_OK;
}

/* Insert or refresh an open conflict by (sync_id, reason). *out_new is 1 only on insert. */
static StoreStatus upsert_conflict(sqlite3 *db, const char *sync_id, StoreConflictReason reason,
                                   const char *local_json, const char *incoming_json,
                                   const char *now, int *out_new, StoreError *serr)
{
    long long existing = 0;
    StoreStatus st = STORE_OK;

    if (out_new != NULL) {
        *out_new = 0;
    }
    st = find_open_conflict(db, sync_id, reason, &existing, serr);
    if (st == STORE_OK) {
        return update_conflict_snapshots(db, existing, local_json, incoming_json, serr);
    }
    if (st != STORE_ERR_NOT_FOUND) {
        return st;
    }
    st = insert_conflict(db, sync_id, reason, local_json, incoming_json, now, serr);
    if (st == STORE_OK && out_new != NULL) {
        *out_new = 1;
    }
    return st;
}

/*
 * Record or refresh a concurrent_vv conflict after pairwise-maxing local VV
 * (decision:sync-import-reimport-idempotent — re-import must not add another row).
 * *out_new_conflict is 1 only when a new conflicts row was inserted.
 */
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
static StoreStatus record_concurrent_vv(sqlite3 *db, const Entry *local, const Entry *incoming_view,
                                        const char *incoming_vv, const char *now,
                                        int *out_new_conflict, StoreError *serr)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    char maxed[VV_OUT_MAX];
    char *local_json = NULL;
    char *incoming_json = NULL;
    StoreStatus st = STORE_OK;
    Entry refreshed;

    if (out_new_conflict != NULL) {
        *out_new_conflict = 0;
    }
    st = vv_pairwise_max(db, local->version_vector, incoming_vv, maxed, sizeof(maxed), serr);
    if (st != STORE_OK) {
        return st;
    }
    st = write_version_vector(db, local->id, maxed, serr);
    if (st != STORE_OK) {
        return st;
    }

    memset(&refreshed, 0, sizeof(refreshed));
    st = load_entry_by_id(db, local->id, &refreshed, serr);
    if (st != STORE_OK) {
        return st;
    }
    st = entry_snapshot_json(&refreshed, now, &local_json);
    store_entry_free(&refreshed);
    if (st != STORE_OK) {
        return st;
    }
    st = entry_snapshot_json(incoming_view, now, &incoming_json);
    if (st != STORE_OK) {
        free(local_json);
        return st;
    }

    st = upsert_conflict(db, local->sync_id, STORE_CONFLICT_CONCURRENT_VV, local_json,
                         incoming_json, now, out_new_conflict, serr);
    free(local_json);
    free(incoming_json);
    return st;
}

static StoreStatus delete_conflict_row(sqlite3 *db, long long id, StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    int rc = 0;

    rc = sqlite3_prepare_v2(db, "DELETE FROM conflicts WHERE id = ?1;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_int64(stmt, 1, id);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(stmt);
    return STORE_OK;
}

/* Insert preserving sync_id / VV / timestamps / deleted_at (no mint, no bump). */
static StoreStatus insert_entry_imported(sqlite3 *db, const char *key_or_null, const char *body,
                                         const char *body_hash, const char *source,
                                         const char *created_at, const char *updated_at,
                                         const char *expires_at, const char *sync_id,
                                         const char *deleted_at, const char *vv_json,
                                         long long *out_id, StoreError *serr)
{
    enum {
        BIND_KEY = 1,
        BIND_BODY = 2,
        BIND_BODY_HASH = 3,
        BIND_SOURCE = 4,
        BIND_CREATED_AT = 5,
        BIND_UPDATED_AT = 6,
        BIND_EXPIRES_AT = 7,
        BIND_SYNC_ID = 8,
        BIND_DELETED_AT = 9,
        BIND_VV = 10
    };
    sqlite3_stmt *stmt = NULL;
    int rc = 0;

    if (body == NULL || body_hash == NULL || source == NULL || created_at == NULL ||
        updated_at == NULL || sync_id == NULL || vv_json == NULL || out_id == NULL) {
        return STORE_ERR_INTERNAL;
    }
    rc = sqlite3_prepare_v2(
        db,
        "INSERT INTO entries(key, body, body_hash, created_by, updated_by, "
        "created_at, updated_at, expires_at, id, deleted_at, version_vector, "
        "change_seq) VALUES (?1, ?2, ?3, " K_ACTOR_FROM_SOURCE("?4") ", " K_ACTOR_FROM_SOURCE(
            "?4") ", ?5, ?6, ?7, ?8, ?9, ?10, 0);",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    if (key_or_null == NULL) {
        (void)sqlite3_bind_null(stmt, BIND_KEY);
    } else {
        (void)sqlite3_bind_text(stmt, BIND_KEY, key_or_null, -1, SQLITE_STATIC);
    }
    (void)sqlite3_bind_text(stmt, BIND_BODY, body, -1, SQLITE_STATIC);
    (void)sqlite3_bind_text(stmt, BIND_BODY_HASH, body_hash, -1, SQLITE_STATIC);
    (void)sqlite3_bind_text(stmt, BIND_SOURCE, source, -1, SQLITE_STATIC);
    (void)sqlite3_bind_text(stmt, BIND_CREATED_AT, created_at, -1, SQLITE_STATIC);
    (void)sqlite3_bind_text(stmt, BIND_UPDATED_AT, updated_at, -1, SQLITE_STATIC);
    if (expires_at == NULL) {
        (void)sqlite3_bind_null(stmt, BIND_EXPIRES_AT);
    } else {
        (void)sqlite3_bind_text(stmt, BIND_EXPIRES_AT, expires_at, -1, SQLITE_STATIC);
    }
    (void)sqlite3_bind_text(stmt, BIND_SYNC_ID, sync_id, -1, SQLITE_STATIC);
    if (deleted_at == NULL) {
        (void)sqlite3_bind_null(stmt, BIND_DELETED_AT);
    } else {
        (void)sqlite3_bind_text(stmt, BIND_DELETED_AT, deleted_at, -1, SQLITE_STATIC);
    }
    (void)sqlite3_bind_text(stmt, BIND_VV, vv_json, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    *out_id = sqlite3_last_insert_rowid(db);
    (void)sqlite3_finalize(stmt);
    return STORE_OK;
}

static int keys_equal(const char *a, const char *b)
{
    if (a == NULL && b == NULL) {
        return 1;
    }
    if (a == NULL || b == NULL) {
        return 0;
    }
    return strcmp(a, b) == 0;
}

static int delete_state_equal(const char *a, const char *b)
{
    return (a == NULL) == (b == NULL);
}

static int tags_equal(char *const *a, size_t count_a, char *const *b, size_t count_b)
{
    size_t i = 0;

    if (count_a != count_b) {
        return 0;
    }
    for (i = 0; i < count_a; i++) {
        if (a[i] == NULL || b[i] == NULL || strcmp(a[i], b[i]) != 0) {
            return 0;
        }
    }
    return 1;
}

/* Identical content = body+tags+key+delete state (not expires/source/links). */
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
static int content_identical(const Entry *local, const char *body, const char *key,
                             const char *deleted_at, char *const *tags, size_t ntags)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    if (local == NULL || body == NULL || local->body == NULL) {
        return 0;
    }
    if (strcmp(local->body, body) != 0) {
        return 0;
    }
    if (!keys_equal(local->key, key)) {
        return 0;
    }
    if (!delete_state_equal(local->deleted_at, deleted_at)) {
        return 0;
    }
    return tags_equal(local->tags, local->ntags, tags, ntags);
}

/* True if key is taken by a row whose sync_id != except_sync_id (NULL = any). */
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
static StoreStatus key_taken_by_other(sqlite3 *db, const char *key, const char *except_sync_id,
                                      long long *out_id, int *taken, StoreError *serr)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    long long id = 0;
    StoreStatus st = STORE_OK;
    Entry e;

    *taken = 0;
    if (out_id != NULL) {
        *out_id = 0;
    }
    if (key == NULL) {
        return STORE_OK;
    }
    st = find_id_by_key(db, key, &id, serr);
    if (st == STORE_ERR_NOT_FOUND) {
        return STORE_OK;
    }
    if (st != STORE_OK) {
        return st;
    }
    memset(&e, 0, sizeof(e));
    st = load_entry_by_id(db, id, &e, serr);
    if (st != STORE_OK) {
        return st;
    }
    if (except_sync_id != NULL && e.sync_id != NULL && strcmp(e.sync_id, except_sync_id) == 0) {
        store_entry_free(&e);
        return STORE_OK;
    }
    store_entry_free(&e);
    *taken = 1;
    if (out_id != NULL) {
        *out_id = id;
    }
    return STORE_OK;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
static StoreStatus hash_taken_by_other(sqlite3 *db, const char *body_hash,
                                       const char *except_sync_id, long long *out_id, int *taken,
                                       StoreError *serr)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    long long id = 0;
    StoreStatus st = STORE_OK;
    Entry e;

    *taken = 0;
    if (out_id != NULL) {
        *out_id = 0;
    }
    if (body_hash == NULL) {
        return STORE_OK;
    }
    st = find_keyless_by_hash(db, body_hash, &id, serr);
    if (st == STORE_ERR_NOT_FOUND) {
        return STORE_OK;
    }
    if (st != STORE_OK) {
        return st;
    }
    memset(&e, 0, sizeof(e));
    st = load_entry_by_id(db, id, &e, serr);
    if (st != STORE_OK) {
        return st;
    }
    if (except_sync_id != NULL && e.sync_id != NULL && strcmp(e.sync_id, except_sync_id) == 0) {
        store_entry_free(&e);
        return STORE_OK;
    }
    store_entry_free(&e);
    *taken = 1;
    if (out_id != NULL) {
        *out_id = id;
    }
    return STORE_OK;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
static StoreStatus apply_incoming_fields(sqlite3 *db, long long id, const char *key_or_null,
                                         const char *body, const char *body_hash,
                                         const char *source, const char *expires_at,
                                         const char *deleted_at, const char *now, char *const *tags,
                                         size_t ntags, StoreError *serr)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    enum {
        BIND_KEY = 1,
        BIND_BODY = 2,
        BIND_BODY_HASH = 3,
        BIND_EXPIRES_AT = 4,
        BIND_DELETED_AT = 5,
        BIND_UPDATED_AT = 6,
        BIND_ID = 7
    };
    sqlite3_stmt *stmt = NULL;
    StoreStatus st = STORE_OK;
    int rc = 0;

    (void)source; /* Round 7 apply list omits source; leave local source. */
    rc =
        sqlite3_prepare_v2(db,
                           "UPDATE entries SET key = ?1, body = ?2, body_hash = ?3, "
                           "expires_at = ?4, deleted_at = ?5, updated_at = ?6 WHERE local_id = ?7;",
                           -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    if (key_or_null == NULL) {
        (void)sqlite3_bind_null(stmt, BIND_KEY);
    } else {
        (void)sqlite3_bind_text(stmt, BIND_KEY, key_or_null, -1, SQLITE_STATIC);
    }
    (void)sqlite3_bind_text(stmt, BIND_BODY, body, -1, SQLITE_STATIC);
    (void)sqlite3_bind_text(stmt, BIND_BODY_HASH, body_hash, -1, SQLITE_STATIC);
    if (expires_at == NULL) {
        (void)sqlite3_bind_null(stmt, BIND_EXPIRES_AT);
    } else {
        (void)sqlite3_bind_text(stmt, BIND_EXPIRES_AT, expires_at, -1, SQLITE_STATIC);
    }
    if (deleted_at == NULL) {
        (void)sqlite3_bind_null(stmt, BIND_DELETED_AT);
    } else {
        (void)sqlite3_bind_text(stmt, BIND_DELETED_AT, deleted_at, -1, SQLITE_STATIC);
    }
    (void)sqlite3_bind_text(stmt, BIND_UPDATED_AT, now, -1, SQLITE_STATIC);
    (void)sqlite3_bind_int64(stmt, BIND_ID, id);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(stmt);
    st = replace_tags(db, id, (const char *const *)tags, ntags, serr);
    if (st != STORE_OK) {
        return st;
    }
    return fts_resync(db, id, serr);
}

static StoreStatus record_clash(sqlite3 *db, long long local_id, const Entry *incoming_view,
                                const char *now, StoreConflictReason reason, const char *sync_id,
                                int *out_new_conflict, StoreError *serr)
{
    Entry local;
    char *local_json = NULL;
    char *incoming_json = NULL;
    StoreStatus st = STORE_OK;

    if (out_new_conflict != NULL) {
        *out_new_conflict = 0;
    }
    memset(&local, 0, sizeof(local));
    st = load_entry_by_id(db, local_id, &local, serr);
    if (st != STORE_OK) {
        return st;
    }
    st = entry_snapshot_json(&local, now, &local_json);
    if (st != STORE_OK) {
        store_entry_free(&local);
        return st;
    }
    st = entry_snapshot_json(incoming_view, now, &incoming_json);
    store_entry_free(&local);
    if (st != STORE_OK) {
        free(local_json);
        return st;
    }
    st = upsert_conflict(db, sync_id, reason, local_json, incoming_json, now, out_new_conflict,
                         serr);
    free(local_json);
    free(incoming_json);
    return st;
}

typedef struct {
    long long foreign_id;
    char *key;
    char *body;
    char *body_hash;
    char *source;
    char *created_at;
    char *updated_at;
    char *expires_at;
    char *sync_id;
    char *deleted_at;
    char *version_vector;
    char **tags;
    size_t ntags;
} ImportRow;

static void import_row_free(ImportRow *r)
{
    if (r == NULL) {
        return;
    }
    free(r->key);
    free(r->body);
    free(r->body_hash);
    free(r->source);
    free(r->created_at);
    free(r->updated_at);
    free(r->expires_at);
    free(r->sync_id);
    free(r->deleted_at);
    free(r->version_vector);
    free_tag_list(r->tags, r->ntags);
    memset(r, 0, sizeof(*r));
}

static StoreStatus import_row_from_stmt(sqlite3 *src, sqlite3_stmt *stmt, ImportRow *out,
                                        StoreError *serr)
{
    enum {
        COL_ID = 0,
        COL_KEY = 1,
        COL_BODY = 2,
        COL_BODY_HASH = 3,
        COL_SOURCE = 4,
        COL_CREATED_AT = 5,
        COL_UPDATED_AT = 6,
        COL_EXPIRES_AT = 7,
        COL_SYNC_ID = 8,
        COL_DELETED_AT = 9,
        COL_VV = 10
    };
    StoreStatus st = STORE_OK;
    const unsigned char *u = NULL;

    memset(out, 0, sizeof(*out));
    out->foreign_id = sqlite3_column_int64(stmt, COL_ID);
    if (sqlite3_column_type(stmt, COL_KEY) != SQLITE_NULL) {
        out->key = dup_str((const char *)sqlite3_column_text(stmt, COL_KEY));
        if (out->key == NULL) {
            return STORE_ERR_OOM;
        }
    }
    u = sqlite3_column_text(stmt, COL_BODY);
    out->body = dup_str(u != NULL ? (const char *)u : "");
    u = sqlite3_column_text(stmt, COL_BODY_HASH);
    out->body_hash = dup_str(u != NULL ? (const char *)u : "");
    u = sqlite3_column_text(stmt, COL_SOURCE);
    out->source = dup_str(u != NULL ? (const char *)u : "unknown");
    u = sqlite3_column_text(stmt, COL_CREATED_AT);
    out->created_at = dup_str(u != NULL ? (const char *)u : "");
    u = sqlite3_column_text(stmt, COL_UPDATED_AT);
    out->updated_at = dup_str(u != NULL ? (const char *)u : "");
    if (sqlite3_column_type(stmt, COL_EXPIRES_AT) != SQLITE_NULL) {
        out->expires_at = dup_str((const char *)sqlite3_column_text(stmt, COL_EXPIRES_AT));
        if (out->expires_at == NULL) {
            import_row_free(out);
            return STORE_ERR_OOM;
        }
    }
    u = sqlite3_column_text(stmt, COL_SYNC_ID);
    out->sync_id = dup_str(u != NULL ? (const char *)u : "");
    if (sqlite3_column_type(stmt, COL_DELETED_AT) != SQLITE_NULL) {
        out->deleted_at = dup_str((const char *)sqlite3_column_text(stmt, COL_DELETED_AT));
        if (out->deleted_at == NULL) {
            import_row_free(out);
            return STORE_ERR_OOM;
        }
    }
    u = sqlite3_column_text(stmt, COL_VV);
    out->version_vector = dup_str(u != NULL ? (const char *)u : "{}");
    if (out->body == NULL || out->body_hash == NULL || out->source == NULL ||
        out->created_at == NULL || out->updated_at == NULL || out->sync_id == NULL ||
        out->version_vector == NULL) {
        import_row_free(out);
        return STORE_ERR_OOM;
    }
    st = load_tags(src, out->foreign_id, &out->tags, &out->ntags, serr);
    if (st != STORE_OK) {
        import_row_free(out);
        return st;
    }
    return STORE_OK;
}

static void import_row_as_entry(const ImportRow *r, Entry *e)
{
    memset(e, 0, sizeof(*e));
    e->id = r->foreign_id; /* snapshot id from foreign; cosmetic in conflict JSON */
    e->sync_id = r->sync_id;
    e->key = r->key;
    e->body = r->body;
    e->tags = r->tags;
    e->ntags = r->ntags;
    e->source = r->source;
    e->created_at = r->created_at;
    e->updated_at = r->updated_at;
    e->expires_at = r->expires_at;
    e->deleted_at = r->deleted_at;
    e->version_vector = r->version_vector;
}

static StoreStatus import_insert_new(sqlite3 *dst, const ImportRow *row, const char *now,
                                     StoreImportCounts *counts, StoreError *serr)
{
    int taken = 0;
    long long occ = 0;
    long long new_id = 0;
    StoreStatus st = STORE_OK;
    Entry view;

    if (row->key != NULL) {
        st = key_taken_by_other(dst, row->key, NULL, &occ, &taken, serr);
        if (st != STORE_OK) {
            return st;
        }
        if (taken) {
            int is_new = 0;

            import_row_as_entry(row, &view);
            st = record_clash(dst, occ, &view, now, STORE_CONFLICT_KEY_CLASH, row->sync_id, &is_new,
                              serr);
            if (st == STORE_OK && is_new) {
                counts->conflicts++;
            }
            return st;
        }
    } else {
        st = hash_taken_by_other(dst, row->body_hash, NULL, &occ, &taken, serr);
        if (st != STORE_OK) {
            return st;
        }
        if (taken) {
            int is_new = 0;

            import_row_as_entry(row, &view);
            st = record_clash(dst, occ, &view, now, STORE_CONFLICT_HASH_CLASH, row->sync_id,
                              &is_new, serr);
            if (st == STORE_OK && is_new) {
                counts->conflicts++;
            }
            return st;
        }
    }
    st = insert_entry_imported(dst, row->key, row->body, row->body_hash, row->source,
                               row->created_at, row->updated_at, row->expires_at, row->sync_id,
                               row->deleted_at, row->version_vector, &new_id, serr);
    if (st != STORE_OK) {
        return st;
    }
    st = union_tags(dst, new_id, (const char *const *)row->tags, row->ntags, serr);
    if (st != STORE_OK) {
        return st;
    }
    st = fts_resync(dst, new_id, serr);
    if (st != STORE_OK) {
        return st;
    }
    counts->inserted++;
    return STORE_OK;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static StoreStatus import_merge_present(sqlite3 *dst, const char *device_id, const ImportRow *row,
                                        const Entry *local, const char *now,
                                        StoreImportCounts *counts, StoreError *serr)
{
    int cmp = 0;
    StoreStatus st = STORE_OK;
    int taken = 0;
    long long occ = 0;
    Entry view;

    st = vv_compare(dst, local->version_vector, row->version_vector, &cmp, serr);
    if (st != STORE_OK) {
        return st;
    }
    if (cmp > 0) {
        /* Incoming dominates — check unique before apply. */
        if (row->key != NULL) {
            st = key_taken_by_other(dst, row->key, row->sync_id, &occ, &taken, serr);
        } else {
            st = hash_taken_by_other(dst, row->body_hash, row->sync_id, &occ, &taken, serr);
        }
        if (st != STORE_OK) {
            return st;
        }
        if (taken) {
            int is_new = 0;

            import_row_as_entry(row, &view);
            st = record_clash(dst, occ, &view, now,
                              row->key != NULL ? STORE_CONFLICT_KEY_CLASH
                                               : STORE_CONFLICT_HASH_CLASH,
                              row->sync_id, &is_new, serr);
            if (st == STORE_OK && is_new) {
                counts->conflicts++;
            }
            return st;
        }
        st = apply_incoming_fields(dst, local->id, row->key, row->body, row->body_hash, row->source,
                                   row->expires_at, row->deleted_at, now, row->tags, row->ntags,
                                   serr);
        if (st != STORE_OK) {
            return st;
        }
        st = vv_union_bump(dst, local->id, local->version_vector, row->version_vector, device_id,
                           NULL, serr);
        if (st != STORE_OK) {
            return st;
        }
        counts->updated++;
        return STORE_OK;
    }
    if (cmp < 0 ||
        content_identical(local, row->body, row->key, row->deleted_at, row->tags, row->ntags)) {
        /* Local dominates, or concurrent/equal with identical content. */
        char maxed[VV_OUT_MAX];

        st = vv_pairwise_max(dst, local->version_vector, row->version_vector, maxed, sizeof(maxed),
                             serr);
        if (st != STORE_OK) {
            return st;
        }
        st = write_version_vector(dst, local->id, maxed, serr);
        if (st != STORE_OK) {
            return st;
        }
        counts->unchanged++;
        return STORE_OK;
    }
    /* Concurrent + different → conflict (pairwise-max + idempotent row). */
    {
        int is_new = 0;

        import_row_as_entry(row, &view);
        st = record_concurrent_vv(dst, local, &view, row->version_vector, now, &is_new, serr);
        if (st == STORE_OK && is_new) {
            counts->conflicts++;
        }
        return st;
    }
}

/* A peer token is kept only as a local write would store it
   (decision:import-control-bytes-followup). */
static bool import_token_canonical(const char *token)
{
    char buf[REMEMBER_TOKEN_MAX + 1];

    if (normalize_token(token, buf, sizeof(buf)) != NORM_OK || strcmp(buf, token) != 0) {
        return false;
    }
    return true;
}

static bool import_stamp_or_null(const char *stamp)
{
    if (stamp != NULL && !utc_stamp_is_canonical(stamp)) {
        return false;
    }
    return true;
}

/* Every peer field import stores has the shape a local write produces
   (decision:plan18c-validate-peer-fields); anything else skips the row. */
static bool import_row_valid(const ImportRow *row)
{
    char hash[REMEMBER_SHA256_HEX_LEN + 1];
    size_t i = 0;

    if (!uuid_v7_is_canonical(row->sync_id) || !source_is_valid(row->source) ||
        !utc_stamp_is_canonical(row->created_at) || !utc_stamp_is_canonical(row->updated_at) ||
        !import_stamp_or_null(row->expires_at) || !import_stamp_or_null(row->deleted_at) ||
        !vv_json_is_canonical(row->version_vector)) {
        return false;
    }
    if (row->key != NULL && !import_token_canonical(row->key)) {
        return false;
    }
    for (i = 0; i < row->ntags; i++) {
        if (!import_token_canonical(row->tags[i])) {
            return false;
        }
    }
    body_hash_hex(row->body, strlen(row->body), hash);
    if (strcmp(hash, row->body_hash) != 0) {
        return false;
    }
    return true;
}

static StoreStatus import_one_entry(sqlite3 *dst, const char *device_id, const ImportRow *row,
                                    const char *now, StoreImportCounts *counts, StoreError *serr)
{
    Entry local;
    StoreStatus st = STORE_OK;

    if (!import_row_valid(row)) {
        counts->rejected++;
        return STORE_OK;
    }
    memset(&local, 0, sizeof(local));
    st = load_entry_by_sync_id(dst, row->sync_id, &local, serr);
    if (st == STORE_ERR_NOT_FOUND) {
        return import_insert_new(dst, row, now, counts, serr);
    }
    if (st != STORE_OK) {
        return st;
    }
    st = import_merge_present(dst, device_id, row, &local, now, counts, serr);
    store_entry_free(&local);
    return st;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static StoreStatus import_remap_links(sqlite3 *dst, sqlite3 *src, StoreError *serr)
{
    sqlite3_stmt *sel = NULL;
    sqlite3_stmt *ins = NULL;
    sqlite3_stmt *sync_of = NULL;
    int rc = 0;

    rc = sqlite3_prepare_v2(src,
                            "SELECT from_id, to_id, kind, created_at, updated_at FROM entry_links;",
                            -1, &sel, NULL);
    if (rc != SQLITE_OK) {
        capture_source_error(src, serr);
        return STORE_ERR_SQLITE;
    }
    rc = sqlite3_prepare_v2(src, "SELECT sync_id FROM entries WHERE id = ?1;", -1, &sync_of, NULL);
    if (rc != SQLITE_OK) {
        capture_source_error(src, serr);
        (void)sqlite3_finalize(sel);
        return STORE_ERR_SQLITE;
    }
    rc = sqlite3_prepare_v2(
        dst,
        "INSERT OR IGNORE INTO entry_links(id, from_id, to_id, kind, created_at, "
        "updated_at, version_vector, change_seq) "
        "VALUES (?6, ?1, ?2, ?3, ?4, ?5, '{}', 0);",
        -1, &ins, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(dst, serr);
        (void)sqlite3_finalize(sel);
        (void)sqlite3_finalize(sync_of);
        return STORE_ERR_SQLITE;
    }

    while ((rc = sqlite3_step(sel)) == SQLITE_ROW) {
        enum {
            BIND_FROM = 1,
            BIND_TO = 2,
            BIND_KIND = 3,
            BIND_CREATED = 4,
            BIND_UPDATED = 5,
            BIND_LINK_ID = 6
        };
        long long f_id = sqlite3_column_int64(sel, 0);
        long long t_id = sqlite3_column_int64(sel, 1);
        const char *kind = (const char *)sqlite3_column_text(sel, 2);
        const char *created = (const char *)sqlite3_column_text(sel, 3);
        const char *updated = (const char *)sqlite3_column_text(sel, 4);
        const char *f_sync = NULL;
        const char *t_sync = NULL;
        Entry from_entry;
        Entry to_entry;
        StoreStatus st = STORE_OK;

        (void)sqlite3_reset(sync_of);
        (void)sqlite3_clear_bindings(sync_of);
        (void)sqlite3_bind_int64(sync_of, 1, f_id);
        if (sqlite3_step(sync_of) != SQLITE_ROW) {
            continue;
        }
        f_sync = (const char *)sqlite3_column_text(sync_of, 0);
        if (f_sync == NULL) {
            continue;
        }
        {
            char *from_sync = dup_str(f_sync);
            char *ts = NULL;
            char link_id[UUID_STR_LEN + 1];

            (void)sqlite3_reset(sync_of);
            (void)sqlite3_clear_bindings(sync_of);
            (void)sqlite3_bind_int64(sync_of, 1, t_id);
            if (sqlite3_step(sync_of) != SQLITE_ROW) {
                free(from_sync);
                continue;
            }
            t_sync = (const char *)sqlite3_column_text(sync_of, 0);
            if (t_sync == NULL) {
                free(from_sync);
                continue;
            }
            ts = dup_str(t_sync);
            if (from_sync == NULL || ts == NULL) {
                free(from_sync);
                free(ts);
                (void)sqlite3_finalize(sel);
                (void)sqlite3_finalize(sync_of);
                (void)sqlite3_finalize(ins);
                return STORE_ERR_OOM;
            }
            memset(&from_entry, 0, sizeof(from_entry));
            memset(&to_entry, 0, sizeof(to_entry));
            st = load_entry_by_sync_id(dst, from_sync, &from_entry, serr);
            if (st == STORE_OK) {
                st = load_entry_by_sync_id(dst, ts, &to_entry, serr);
            }
            free(from_sync);
            free(ts);
            if (st == STORE_ERR_NOT_FOUND) {
                store_entry_free(&from_entry);
                store_entry_free(&to_entry);
                continue;
            }
            if (st != STORE_OK) {
                store_entry_free(&from_entry);
                store_entry_free(&to_entry);
                (void)sqlite3_finalize(sel);
                (void)sqlite3_finalize(sync_of);
                (void)sqlite3_finalize(ins);
                return st;
            }
            if (mint_uuid_v7(link_id) != 0) {
                store_entry_free(&from_entry);
                store_entry_free(&to_entry);
                (void)sqlite3_finalize(sel);
                (void)sqlite3_finalize(sync_of);
                (void)sqlite3_finalize(ins);
                return STORE_ERR_INTERNAL;
            }
            (void)sqlite3_reset(ins);
            (void)sqlite3_clear_bindings(ins);
            (void)sqlite3_bind_text(ins, BIND_LINK_ID, link_id, -1, SQLITE_TRANSIENT);
            (void)sqlite3_bind_int64(ins, BIND_FROM, from_entry.id);
            (void)sqlite3_bind_int64(ins, BIND_TO, to_entry.id);
            (void)sqlite3_bind_text(ins, BIND_KIND, kind != NULL ? kind : "related", -1,
                                    SQLITE_TRANSIENT);
            (void)sqlite3_bind_text(ins, BIND_CREATED, created != NULL ? created : "", -1,
                                    SQLITE_TRANSIENT);
            (void)sqlite3_bind_text(ins, BIND_UPDATED, updated != NULL ? updated : "", -1,
                                    SQLITE_TRANSIENT);
            if (sqlite3_step(ins) != SQLITE_DONE) {
                capture_sqlite_error(dst, serr);
                store_entry_free(&from_entry);
                store_entry_free(&to_entry);
                (void)sqlite3_finalize(sel);
                (void)sqlite3_finalize(sync_of);
                (void)sqlite3_finalize(ins);
                return STORE_ERR_SQLITE;
            }
            store_entry_free(&from_entry);
            store_entry_free(&to_entry);
        }
    }
    if (rc != SQLITE_DONE) {
        capture_source_error(src, serr);
    }
    (void)sqlite3_finalize(sel);
    (void)sqlite3_finalize(sync_of);
    (void)sqlite3_finalize(ins);
    return (rc == SQLITE_DONE) ? STORE_OK : STORE_ERR_SQLITE;
}

/* One existence count. 0 is absent. A sqlite error is -1. devices is never queried. */
static int probe_count(sqlite3 *src, const char *sql, StoreError *serr)
{
    int n = 0;

    if (select_int(src, sql, NULL, &n, NULL, 0U, serr) != 0) {
        return -1;
    }
    (void)n; /* 0 is absent and 1 is present. This stage does not copy the rows. */
    return 0;
}

/* Read-only. Missing objects are absent, not an error. Row contents stay in the
   source (bucket_prefixes and settings are plan 19; conflicts.origin is plan 21). */
static int probe_import_source(sqlite3 *src, StoreError *serr)
{
    if (probe_count(src,
                    "SELECT COUNT(*) FROM pragma_table_info('conflicts') WHERE "
                    "name='origin';",
                    serr) != 0) {
        return -1;
    }
    if (probe_count(src,
                    "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND "
                    "name='bucket_prefixes';",
                    serr) != 0) {
        return -1;
    }
    if (probe_count(src,
                    "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND "
                    "name='settings';",
                    serr) != 0) {
        return -1;
    }
    return 0;
}

/* Accept 4 and 5. Older keeps the source-too-old text. Newer uses the open text.
   Probe only after the version is one this build understands. */
static StoreStatus import_source_gate(sqlite3 *src, int version, StoreError *serr)
{
    if (version < IMPORT_MIN_USER_VERSION) {
        return STORE_ERR_SOURCE_TOO_OLD;
    }
    if (version > IMPORT_MAX_USER_VERSION) {
        return STORE_ERR_SOURCE_TOO_NEW;
    }
    if (probe_import_source(src, serr) != 0) {
        return STORE_ERR_SQLITE;
    }
    return STORE_OK;
}

StoreStatus store_import(Store *dst, const char *src_path, const char *now,
                         StoreImportCounts *out_counts)
{
    sqlite3 *src = NULL;
    sqlite3_stmt *sel = NULL;
    char err_unused[1];
    int rc = 0;
    int version = 0;
    StoreStatus st = STORE_OK;

    if (out_counts != NULL) {
        memset(out_counts, 0, sizeof(*out_counts));
    }
    if (dst == NULL || dst->db == NULL || dst->device_id == NULL || src_path == NULL ||
        now == NULL || out_counts == NULL) {
        return STORE_ERR_INTERNAL;
    }
    memset(&dst->last, 0, sizeof(dst->last));
    rc = sqlite3_open_v2(src_path, &src, SQLITE_OPEN_READONLY, NULL);
    if (rc != SQLITE_OK) {
        capture_source_error(src, &dst->last);
        if (src != NULL) {
            (void)sqlite3_close(src);
        }
        return STORE_ERR_SQLITE;
    }
    if (read_user_version(src, &version, err_unused, 0U, &dst->last) != 0) {
        dst->last.import_source = true;
        (void)sqlite3_close(src);
        return STORE_ERR_SQLITE;
    }
    st = import_source_gate(src, version, &dst->last);
    if (st != STORE_OK) {
        /* The gate reads only the source. */
        dst->last.import_source = true;
        (void)sqlite3_close(src);
        return st;
    }

    if (exec_sql(dst->db, "BEGIN IMMEDIATE;", err_unused, 0U, &dst->last) != 0) {
        (void)sqlite3_close(src);
        return STORE_ERR_SQLITE;
    }

    rc = sqlite3_prepare_v2(src,
                            "SELECT id, key, body, body_hash, source, created_at, updated_at, "
                            "expires_at, sync_id, deleted_at, version_vector FROM entries;",
                            -1, &sel, NULL);
    if (rc != SQLITE_OK) {
        capture_source_error(src, &dst->last);
        st = STORE_ERR_SQLITE;
        goto fail;
    }
    while ((rc = sqlite3_step(sel)) == SQLITE_ROW) {
        ImportRow row;

        st = import_row_from_stmt(src, sel, &row, &dst->last);
        if (st != STORE_OK) {
            goto fail;
        }
        st = import_one_entry(dst->db, dst->device_id, &row, now, out_counts, &dst->last);
        import_row_free(&row);
        if (st != STORE_OK) {
            goto fail;
        }
    }
    if (rc != SQLITE_DONE) {
        capture_source_error(src, &dst->last);
        st = STORE_ERR_SQLITE;
        goto fail;
    }
    (void)sqlite3_finalize(sel);
    sel = NULL;

    st = import_remap_links(dst->db, src, &dst->last);
    if (st != STORE_OK) {
        goto fail;
    }

    if (exec_sql(dst->db, "COMMIT;", err_unused, 0U, &dst->last) != 0) {
        st = STORE_ERR_SQLITE;
        goto fail;
    }
    (void)sqlite3_close(src);
    return STORE_OK;

fail:
    if (sel != NULL) {
        (void)sqlite3_finalize(sel);
    }
    rollback_quiet(dst->db);
    (void)sqlite3_close(src);
    return st;
}

void store_conflicts_free(StoreConflict *rows, size_t count)
{
    size_t i = 0;

    if (rows == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        free(rows[i].sync_id);
        free(rows[i].local_json);
        free(rows[i].incoming_json);
        free(rows[i].created_at);
    }
    free(rows);
}

StoreStatus store_conflicts_list(Store *s, StoreConflict **out_rows, size_t *out_count)
{
    sqlite3_stmt *stmt = NULL;
    StoreConflict *rows = NULL;
    size_t n = 0U;
    size_t cap = 0U;
    int rc = 0;

    if (s == NULL || s->db == NULL || out_rows == NULL || out_count == NULL) {
        return STORE_ERR_INTERNAL;
    }
    *out_rows = NULL;
    *out_count = 0U;
    memset(&s->last, 0, sizeof(s->last));
    rc = sqlite3_prepare_v2(s->db,
                            "SELECT id, sync_id, reason, local_json, incoming_json, created_at, "
                            "CASE WHEN json_valid(local_json) AND json_valid(incoming_json) "
                            "THEN json_type(local_json) = 'object' AND "
                            "json_type(incoming_json) = 'object' ELSE 0 END "
                            "FROM conflicts ORDER BY id ASC;",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(s->db, &s->last);
        return STORE_ERR_SQLITE;
    }
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        enum {
            COL_ID = 0,
            COL_SYNC_ID = 1,
            COL_REASON = 2,
            COL_LOCAL = 3,
            COL_INCOMING = 4,
            COL_CREATED_AT = 5,
            COL_SNAPSHOTS_ARE_OBJECTS = 6
        };
        StoreConflict *grown = NULL;
        StoreConflict row;
        StoreStatus st = STORE_OK;
        const char *reason = (const char *)sqlite3_column_text(stmt, COL_REASON);

        memset(&row, 0, sizeof(row));
        row.id = sqlite3_column_int64(stmt, COL_ID);
        row.sync_id = dup_str((const char *)sqlite3_column_text(stmt, COL_SYNC_ID));
        row.local_json = dup_str((const char *)sqlite3_column_text(stmt, COL_LOCAL));
        row.incoming_json = dup_str((const char *)sqlite3_column_text(stmt, COL_INCOMING));
        row.created_at = dup_str((const char *)sqlite3_column_text(stmt, COL_CREATED_AT));
        st = conflict_reason_parse(reason, &row.reason);
        /* output prints the snapshots raw: refuse one that is not a JSON object
           (decision:plan18c-validate-peer-fields). */
        if (st == STORE_OK && sqlite3_column_int(stmt, COL_SNAPSHOTS_ARE_OBJECTS) == 0) {
            st = STORE_ERR_INTERNAL;
        }
        if (st != STORE_OK || row.sync_id == NULL || row.local_json == NULL ||
            row.incoming_json == NULL || row.created_at == NULL) {
            free(row.sync_id);
            free(row.local_json);
            free(row.incoming_json);
            free(row.created_at);
            store_conflicts_free(rows, n);
            (void)sqlite3_finalize(stmt);
            return (st != STORE_OK) ? st : STORE_ERR_OOM;
        }
        if (n == cap) {
            size_t ncap = (cap == 0U) ? (size_t)GROW_MIN_CAP : cap * 2U;
            grown = (StoreConflict *)realloc(rows, ncap * sizeof(*rows));
            if (grown == NULL) {
                free(row.sync_id);
                free(row.local_json);
                free(row.incoming_json);
                free(row.created_at);
                store_conflicts_free(rows, n);
                (void)sqlite3_finalize(stmt);
                return STORE_ERR_OOM;
            }
            rows = grown;
            cap = ncap;
        }
        rows[n] = row;
        n++;
    }
    if (rc != SQLITE_DONE) {
        capture_sqlite_error(s->db, &s->last);
        (void)sqlite3_finalize(stmt);
        store_conflicts_free(rows, n);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(stmt);
    *out_rows = rows;
    *out_count = n;
    return STORE_OK;
}

/* ---- conflict accept helpers ------------------------------------------- */

enum { SNAPSHOT_FIELDS_PAD = 7 };

typedef struct {
    char *sync_id;
    char *key;
    char *body;
    char *source;
    char *created_at;
    char *updated_at;
    char *expires_at;
    char *deleted_at;
    char *version_vector;
    char **tags;
    size_t ntags;
    char body_hash[REMEMBER_SHA256_HEX_LEN + 1];
    char pad_[SNAPSHOT_FIELDS_PAD];
} SnapshotFields;

static void snapshot_fields_free(SnapshotFields *f)
{
    if (f == NULL) {
        return;
    }
    free(f->sync_id);
    free(f->key);
    free(f->body);
    free(f->source);
    free(f->created_at);
    free(f->updated_at);
    free(f->expires_at);
    free(f->deleted_at);
    free(f->version_vector);
    free_tag_list(f->tags, f->ntags);
    memset(f, 0, sizeof(*f));
}

enum { JSON_EXTRACT_SQL_BUFLEN = 128 };

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
static char *json_extract_text(sqlite3 *db, const char *json, const char *path, int *is_null,
                               StoreError *serr)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    sqlite3_stmt *stmt = NULL;
    char *out = NULL;
    char sql[JSON_EXTRACT_SQL_BUFLEN];
    int rc = 0;

    *is_null = 0;
    if (snprintf(sql, sizeof(sql), "SELECT json_extract(?1, '%s');", path) < 0) {
        return NULL;
    }
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return NULL;
    }
    (void)sqlite3_bind_text(stmt, 1, json, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        if (rc != SQLITE_DONE) {
            capture_sqlite_error(db, serr);
        }
        (void)sqlite3_finalize(stmt);
        return NULL;
    }
    if (sqlite3_column_type(stmt, 0) == SQLITE_NULL) {
        *is_null = 1;
        (void)sqlite3_finalize(stmt);
        return NULL;
    }
    out = dup_str((const char *)sqlite3_column_text(stmt, 0));
    (void)sqlite3_finalize(stmt);
    return out;
}

static StoreStatus snapshot_parse_tags(sqlite3 *db, const char *json, char ***out_tags,
                                       size_t *out_n, StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    char **tags = NULL;
    size_t n = 0U;
    size_t cap = 0U;
    int rc = 0;

    *out_tags = NULL;
    *out_n = 0U;
    rc = sqlite3_prepare_v2(db,
                            "SELECT value FROM json_each(?1, '$.tags') "
                            "ORDER BY CAST(key AS INTEGER);",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_text(stmt, 1, json, -1, SQLITE_STATIC);
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        char *copy = dup_str((const char *)sqlite3_column_text(stmt, 0));
        char **grown = NULL;

        if (copy == NULL) {
            free_tag_list(tags, n);
            (void)sqlite3_finalize(stmt);
            return STORE_ERR_OOM;
        }
        if (n == cap) {
            size_t ncap = (cap == 0U) ? (size_t)GROW_MIN_CAP : cap * 2U;
            grown = (char **)realloc((void *)tags, ncap * sizeof(*tags));
            if (grown == NULL) {
                free(copy);
                free_tag_list(tags, n);
                (void)sqlite3_finalize(stmt);
                return STORE_ERR_OOM;
            }
            tags = grown;
            cap = ncap;
        }
        tags[n++] = copy;
    }
    if (rc != SQLITE_DONE) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        free_tag_list(tags, n);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_finalize(stmt);
    *out_tags = tags;
    *out_n = n;
    return STORE_OK;
}

static StoreStatus snapshot_parse(sqlite3 *db, const char *json, SnapshotFields *out,
                                  StoreError *serr)
{
    int is_null = 0;
    StoreStatus st = STORE_OK;

    memset(out, 0, sizeof(*out));
    out->sync_id = json_extract_text(db, json, "$.sync_id", &is_null, serr);
    if (out->sync_id == NULL) {
        return STORE_ERR_INTERNAL;
    }
    out->key = json_extract_text(db, json, "$.key", &is_null, serr);
    if (is_null) {
        free(out->key);
        out->key = NULL;
    } else if (out->key == NULL) {
        snapshot_fields_free(out);
        return STORE_ERR_OOM;
    }
    out->body = json_extract_text(db, json, "$.body", &is_null, serr);
    out->source = json_extract_text(db, json, "$.source", &is_null, serr);
    out->created_at = json_extract_text(db, json, "$.created_at", &is_null, serr);
    out->updated_at = json_extract_text(db, json, "$.updated_at", &is_null, serr);
    out->expires_at = json_extract_text(db, json, "$.expires_at", &is_null, serr);
    if (is_null) {
        free(out->expires_at);
        out->expires_at = NULL;
    }
    out->deleted_at = json_extract_text(db, json, "$.deleted_at", &is_null, serr);
    if (is_null) {
        free(out->deleted_at);
        out->deleted_at = NULL;
    }
    /* version_vector is an object — extract as JSON text */
    {
        sqlite3_stmt *stmt = NULL;
        int rc =
            sqlite3_prepare_v2(db, "SELECT json_extract(?1, '$.version_vector');", -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            capture_sqlite_error(db, serr);
            snapshot_fields_free(out);
            return STORE_ERR_SQLITE;
        }
        (void)sqlite3_bind_text(stmt, 1, json, -1, SQLITE_STATIC);
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW) {
            if (rc != SQLITE_DONE) {
                capture_sqlite_error(db, serr);
            }
            (void)sqlite3_finalize(stmt);
            snapshot_fields_free(out);
            return STORE_ERR_INTERNAL;
        }
        if (sqlite3_column_type(stmt, 0) == SQLITE_NULL) {
            (void)sqlite3_finalize(stmt);
            snapshot_fields_free(out);
            return STORE_ERR_INTERNAL;
        }
        out->version_vector = dup_str((const char *)sqlite3_column_text(stmt, 0));
        (void)sqlite3_finalize(stmt);
        if (out->version_vector == NULL) {
            snapshot_fields_free(out);
            return STORE_ERR_OOM;
        }
    }
    if (out->body == NULL || out->source == NULL || out->created_at == NULL ||
        out->updated_at == NULL) {
        snapshot_fields_free(out);
        return STORE_ERR_OOM;
    }
    body_hash_hex(out->body, strlen(out->body), out->body_hash);
    /* Ownership of out fields transfers to caller on STORE_OK. */
    // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
    st = snapshot_parse_tags(db, json, &out->tags, &out->ntags, serr);
    if (st != STORE_OK) {
        snapshot_fields_free(out);
        return st;
    }
    return STORE_OK;
}

static StoreStatus accept_load_conflict(sqlite3 *db, long long id, char **sync_id,
                                        char **local_json, char **incoming_json,
                                        StoreConflictReason *reason, StoreError *serr)
{
    sqlite3_stmt *stmt = NULL;
    StoreStatus st = STORE_OK;
    int rc = 0;

    *sync_id = NULL;
    *local_json = NULL;
    *incoming_json = NULL;
    rc = sqlite3_prepare_v2(db,
                            "SELECT sync_id, reason, local_json, incoming_json FROM conflicts "
                            "WHERE id = ?1;",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        capture_sqlite_error(db, serr);
        return STORE_ERR_SQLITE;
    }
    (void)sqlite3_bind_int64(stmt, 1, id);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_NOT_FOUND;
    }
    if (rc != SQLITE_ROW) {
        capture_sqlite_error(db, serr);
        (void)sqlite3_finalize(stmt);
        return STORE_ERR_SQLITE;
    }
    *sync_id = dup_str((const char *)sqlite3_column_text(stmt, 0));
    st = conflict_reason_parse((const char *)sqlite3_column_text(stmt, 1), reason);
    *local_json = dup_str((const char *)sqlite3_column_text(stmt, 2));
    *incoming_json = dup_str((const char *)sqlite3_column_text(stmt, 3));
    (void)sqlite3_finalize(stmt);
    if (st != STORE_OK) {
        free(*sync_id);
        free(*local_json);
        free(*incoming_json);
        *sync_id = NULL;
        *local_json = NULL;
        *incoming_json = NULL;
        return st;
    }
    if (*sync_id == NULL || *local_json == NULL || *incoming_json == NULL) {
        free(*sync_id);
        free(*local_json);
        free(*incoming_json);
        *sync_id = NULL;
        *local_json = NULL;
        *incoming_json = NULL;
        return STORE_ERR_OOM;
    }
    return STORE_OK;
}

/* Insert snapshot; if key taken and make_keyless_ok, strip key. Hash collision → UNIQUE_TAKEN. */
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
static StoreStatus insert_snapshot_row(sqlite3 *db, const SnapshotFields *snap,
                                       int preserve_sync_id, const char *device_id, const char *now,
                                       int make_keyless_ok, long long *out_id, StoreError *serr)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    const char *key = snap->key;
    char minted[UUID_STR_LEN + 1];
    char vv_new[VV_JSON_BUFLEN];
    const char *sync_id = snap->sync_id;
    const char *vv_json = snap->version_vector;
    int taken = 0;
    StoreStatus st = STORE_OK;

    if (!preserve_sync_id) {
        if (mint_uuid_v7(minted) != 0) {
            return STORE_ERR_INTERNAL;
        }
        sync_id = minted;
        (void)snprintf(vv_new, sizeof(vv_new), "{\"%s\":1}", device_id);
        vv_json = vv_new;
    }
    if (key != NULL) {
        st = key_taken_by_other(db, key, sync_id, NULL, &taken, serr);
        if (st != STORE_OK) {
            return st;
        }
        if (taken) {
            if (!make_keyless_ok) {
                return STORE_ERR_INTERNAL;
            }
            key = NULL;
        }
    }
    if (key == NULL) {
        st = hash_taken_by_other(db, snap->body_hash, sync_id, NULL, &taken, serr);
        if (st != STORE_OK) {
            return st;
        }
        if (taken) {
            /* decision:sync-import-hash-demote — never invent key=sync_id. */
            return STORE_ERR_UNIQUE_TAKEN;
        }
    }
    st = insert_entry_imported(db, key, snap->body, snap->body_hash, snap->source, snap->created_at,
                               now, snap->expires_at, sync_id, snap->deleted_at, vv_json, out_id,
                               serr);
    if (st != STORE_OK) {
        return st;
    }
    st = union_tags(db, *out_id, (const char *const *)snap->tags, snap->ntags, serr);
    if (st != STORE_OK) {
        return st;
    }
    return fts_resync(db, *out_id, serr);
}

static StoreStatus accept_out_one(sqlite3 *db, long long id, Entry **out_entries, size_t *out_count,
                                  StoreError *serr)
{
    Entry *arr = NULL;
    StoreStatus st = STORE_OK;

    arr = (Entry *)calloc(1U, sizeof(Entry));
    if (arr == NULL) {
        return STORE_ERR_OOM;
    }
    st = load_entry_by_id(db, id, &arr[0], serr);
    if (st != STORE_OK) {
        free(arr);
        return st;
    }
    *out_entries = arr;
    *out_count = 1U;
    return STORE_OK;
}

static StoreStatus accept_out_two(sqlite3 *db, long long a, long long b, Entry **out_entries,
                                  size_t *out_count, StoreError *serr)
{
    Entry *arr = NULL;
    StoreStatus st = STORE_OK;

    arr = (Entry *)calloc(2U, sizeof(Entry));
    if (arr == NULL) {
        return STORE_ERR_OOM;
    }
    st = load_entry_by_id(db, a, &arr[0], serr);
    if (st != STORE_OK) {
        free(arr);
        return st;
    }
    st = load_entry_by_id(db, b, &arr[1], serr);
    if (st != STORE_OK) {
        store_entry_free(&arr[0]);
        free(arr);
        return st;
    }
    *out_entries = arr;
    *out_count = 2U;
    return STORE_OK;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
static StoreStatus accept_keep_local(sqlite3 *db, const char *device_id, const char *now,
                                     const SnapshotFields *local, const SnapshotFields *incoming,
                                     Entry **out_entries, size_t *out_count, StoreError *serr)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    Entry e;
    StoreStatus st = STORE_OK;

    memset(&e, 0, sizeof(e));
    st = load_entry_by_sync_id(db, local->sync_id, &e, serr);
    if (st != STORE_OK) {
        return st;
    }
    st = vv_union_bump(db, e.id, e.version_vector, incoming->version_vector, device_id, now, serr);
    if (st != STORE_OK) {
        store_entry_free(&e);
        return st;
    }
    {
        long long id = e.id;
        store_entry_free(&e);
        return accept_out_one(db, id, out_entries, out_count, serr);
    }
}

/* Clear key so another row can claim it. If that would collide on body_hash,
 * fail (decision:sync-import-hash-demote — never invent key=sync_id). */
static StoreStatus demote_occupant_key(sqlite3 *db, long long occupant_id, StoreError *serr)
{
    char *hash = NULL;
    Entry e;
    int taken = 0;
    StoreStatus st = STORE_OK;

    memset(&e, 0, sizeof(e));
    st = load_entry_by_id(db, occupant_id, &e, serr);
    if (st != STORE_OK) {
        return st;
    }
    st = load_body_hash(db, occupant_id, &hash, serr);
    if (st != STORE_OK) {
        store_entry_free(&e);
        return st;
    }
    st = hash_taken_by_other(db, hash, e.sync_id, NULL, &taken, serr);
    if (st != STORE_OK) {
        free(hash);
        store_entry_free(&e);
        return st;
    }
    if (taken) {
        free(hash);
        store_entry_free(&e);
        return STORE_ERR_UNIQUE_TAKEN;
    }
    st = write_key(db, occupant_id, NULL, serr);
    if (st == STORE_OK) {
        st = fts_resync(db, occupant_id, serr);
    }
    free(hash);
    store_entry_free(&e);
    return st;
}

/* Free a key/hash slot held by a different sync_id (Round 7: never SQLITE unique). */
static StoreStatus demote_other_if_unique_taken(sqlite3 *db, const SnapshotFields *incoming,
                                                StoreError *serr)
{
    int taken = 0;
    long long occ = 0;
    StoreStatus st = STORE_OK;

    if (incoming == NULL || incoming->sync_id == NULL) {
        return STORE_ERR_INTERNAL;
    }
    if (incoming->key != NULL) {
        st = key_taken_by_other(db, incoming->key, incoming->sync_id, &occ, &taken, serr);
        if (st != STORE_OK) {
            return st;
        }
        if (taken) {
            return demote_occupant_key(db, occ, serr);
        }
        return STORE_OK;
    }
    st = hash_taken_by_other(db, incoming->body_hash, incoming->sync_id, &occ, &taken, serr);
    if (st != STORE_OK) {
        return st;
    }
    if (taken) {
        /* Occupant is keyless with this hash — cannot invent a key for them. */
        return STORE_ERR_UNIQUE_TAKEN;
    }
    return STORE_OK;
}

static StoreStatus accept_keep_incoming(sqlite3 *db, const char *device_id, const char *now,
                                        StoreConflictReason reason, const SnapshotFields *local,
                                        const SnapshotFields *incoming, Entry **out_entries,
                                        size_t *out_count, StoreError *serr)
{
    StoreStatus st = STORE_OK;
    Entry e;
    long long id = 0;

    memset(&e, 0, sizeof(e));
    if (reason == STORE_CONFLICT_CONCURRENT_VV) {
        st = load_entry_by_sync_id(db, incoming->sync_id, &e, serr);
        if (st != STORE_OK) {
            return st;
        }
        id = e.id;
        /* Incoming may rekey onto a slot another local row holds. */
        st = demote_other_if_unique_taken(db, incoming, serr);
        if (st != STORE_OK) {
            store_entry_free(&e);
            return st;
        }
        st = apply_incoming_fields(db, id, incoming->key, incoming->body, incoming->body_hash,
                                   incoming->source, incoming->expires_at, incoming->deleted_at,
                                   now, incoming->tags, incoming->ntags, serr);
        if (st != STORE_OK) {
            store_entry_free(&e);
            return st;
        }
        st = vv_union_bump(db, id, e.version_vector, incoming->version_vector, device_id, NULL,
                           serr);
        store_entry_free(&e);
        if (st != STORE_OK) {
            return st;
        }
        return accept_out_one(db, id, out_entries, out_count, serr);
    }

    /* Clash: incoming takes the contested slot; demote local occupant. */
    st = load_entry_by_sync_id(db, local->sync_id, &e, serr);
    if (st != STORE_OK) {
        return st;
    }
    if (reason == STORE_CONFLICT_KEY_CLASH) {
        st = demote_occupant_key(db, e.id, serr);
    } else if (reason == STORE_CONFLICT_HASH_CLASH) {
        /* Local is keyless owning the hash — cannot invent a key; refuse. */
        st = STORE_ERR_UNIQUE_TAKEN;
    }
    store_entry_free(&e);
    if (st != STORE_OK) {
        return st;
    }

    st = insert_snapshot_row(db, incoming, 1, device_id, now, 0, &id, serr);
    if (st != STORE_OK) {
        return st;
    }
    st = apply_vv_bump(db, id, device_id, incoming->version_vector, serr);
    if (st != STORE_OK) {
        return st;
    }
    memset(&e, 0, sizeof(e));
    st = load_entry_by_sync_id(db, local->sync_id, &e, serr);
    if (st != STORE_OK) {
        return st;
    }
    st = vv_union_bump(db, e.id, e.version_vector, incoming->version_vector, device_id, now, serr);
    store_entry_free(&e);
    if (st != STORE_OK) {
        return st;
    }
    return accept_out_one(db, id, out_entries, out_count, serr);
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
static StoreStatus accept_keep_both(sqlite3 *db, const char *device_id, const char *now,
                                    StoreConflictReason reason, const SnapshotFields *local,
                                    const SnapshotFields *incoming, Entry **out_entries,
                                    size_t *out_count, StoreError *serr)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    Entry e;
    long long local_id = 0;
    long long new_id = 0;
    StoreStatus st = STORE_OK;
    int remint = (reason == STORE_CONFLICT_CONCURRENT_VV);

    memset(&e, 0, sizeof(e));
    st = load_entry_by_sync_id(db, local->sync_id, &e, serr);
    if (st != STORE_OK) {
        return st;
    }
    local_id = e.id;
    st = vv_union_bump(db, local_id, e.version_vector, incoming->version_vector, device_id, now,
                       serr);
    store_entry_free(&e);
    if (st != STORE_OK) {
        return st;
    }

    st = insert_snapshot_row(db, incoming, remint ? 0 : 1, device_id, now, 1, &new_id, serr);
    if (st != STORE_OK) {
        return st;
    }
    if (!remint) {
        /* Preserve incoming VV then bump. */
        memset(&e, 0, sizeof(e));
        st = load_entry_by_id(db, new_id, &e, serr);
        if (st != STORE_OK) {
            return st;
        }
        st = apply_vv_bump(db, new_id, device_id, e.version_vector, serr);
        store_entry_free(&e);
        if (st != STORE_OK) {
            return st;
        }
    }
    return accept_out_two(db, local_id, new_id, out_entries, out_count, serr);
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
StoreStatus store_conflict_accept(Store *s, long long conflict_id, StoreConflictKeep keep,
                                  const char *now, Entry **out_entries, size_t *out_count)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    char err_unused[1];
    char *sync_id = NULL;
    char *local_json = NULL;
    char *incoming_json = NULL;
    StoreConflictReason reason = STORE_CONFLICT_CONCURRENT_VV;
    SnapshotFields local;
    SnapshotFields incoming;
    StoreStatus st = STORE_OK;

    if (s == NULL || s->db == NULL || s->device_id == NULL || now == NULL || out_entries == NULL ||
        out_count == NULL) {
        return STORE_ERR_INTERNAL;
    }
    *out_entries = NULL;
    *out_count = 0U;
    memset(&local, 0, sizeof(local));
    memset(&incoming, 0, sizeof(incoming));
    memset(&s->last, 0, sizeof(s->last));

    if (exec_sql(s->db, "BEGIN IMMEDIATE;", err_unused, 0U, &s->last) != 0) {
        return STORE_ERR_SQLITE;
    }
    st = accept_load_conflict(s->db, conflict_id, &sync_id, &local_json, &incoming_json, &reason,
                              &s->last);
    if (st != STORE_OK) {
        goto fail;
    }
    (void)sync_id;
    st = snapshot_parse(s->db, local_json, &local, &s->last);
    if (st != STORE_OK) {
        goto fail;
    }
    st = snapshot_parse(s->db, incoming_json, &incoming, &s->last);
    if (st != STORE_OK) {
        goto fail;
    }

    if (keep == STORE_KEEP_LOCAL) {
        st = accept_keep_local(s->db, s->device_id, now, &local, &incoming, out_entries, out_count,
                               &s->last);
    } else if (keep == STORE_KEEP_INCOMING) {
        st = accept_keep_incoming(s->db, s->device_id, now, reason, &local, &incoming, out_entries,
                                  out_count, &s->last);
    } else if (keep == STORE_KEEP_BOTH) {
        st = accept_keep_both(s->db, s->device_id, now, reason, &local, &incoming, out_entries,
                              out_count, &s->last);
    } else {
        st = STORE_ERR_INTERNAL;
    }
    if (st != STORE_OK) {
        goto fail;
    }
    st = delete_conflict_row(s->db, conflict_id, &s->last);
    if (st != STORE_OK) {
        goto fail;
    }
    if (exec_sql(s->db, "COMMIT;", err_unused, 0U, &s->last) != 0) {
        st = STORE_ERR_SQLITE;
        goto fail;
    }
    free(sync_id);
    free(local_json);
    free(incoming_json);
    snapshot_fields_free(&local);
    snapshot_fields_free(&incoming);
    return STORE_OK;

fail:
    if (*out_entries != NULL) {
        size_t i = 0;
        for (i = 0; i < *out_count; i++) {
            store_entry_free(&(*out_entries)[i]);
        }
        free(*out_entries);
        *out_entries = NULL;
        *out_count = 0U;
    }
    free(sync_id);
    free(local_json);
    free(incoming_json);
    snapshot_fields_free(&local);
    snapshot_fields_free(&incoming);
    rollback_quiet(s->db);
    return st;
}
