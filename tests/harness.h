#ifndef REMEMBER_HARNESS_H
#define REMEMBER_HARNESS_H

#include <stdbool.h>
#include <stddef.h>

/* Shared test constants (enum so a header include never warns "unused"). */
enum {
    ERR_BUFSIZE = 256,      /* store_open error-message buffer */
    EXIT_SPAWN_FAIL = 127,  /* child exit / result code when exec or spawn fails */
    PERM_BITS_MASK = 0777,  /* st_mode permission bits (cast to unsigned at bitwise use) */
    DIR_PERMS = 0700,       /* store directory mode */
    DB_FILE_PERMS = 0600,   /* db-file / blocker-file mode */
    ASCII_DEL = 0x7f,       /* DEL control byte (invalid in tokens) */
    UTF8_STRAY_CONT = 0x80, /* lone continuation byte (never a valid lead) */
    UTF8_INVALID_FE = 0xfe, /* never a valid UTF-8 byte */
    UTF8_INVALID_FF = 0xff  /* never a valid UTF-8 byte */
};

/* A SQL query paired with its expected one-line result (assert_query_is). */
typedef struct {
    const char *sql;
    const char *want;
} QueryExpect;

/* Path to the remember binary under test (set from argv in test_main). Write-once
   process-global; no const-init available at startup. */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern const char *g_remember_bin;

typedef struct {
    char *out; /* stdout, heap, may be empty string */
    char *err; /* stderr, heap, may be empty string */
    int exit_code;
    char pad_[4]; /* explicit tail padding (kept -Wpadded-clean) */
} CmdResult;

void cmd_result_free(CmdResult *r);

/*
 * Run: remember --db <db_path> <args...>
 * stdin_data: optional body for commands that read stdin (may be NULL).
 * Returns captured exit/stdout/stderr. Always frees prior contents of *r if any
 * is caller's job — this fills a fresh CmdResult.
 */
CmdResult run_remember(const char *db_path, const char *const *args, size_t nargs,
                       const char *stdin_data);

/* Convenience: run with a freshly created temp db path (caller frees db_path). */
char *make_temp_db_path(void);

/* Trim trailing newline(s) in place for comparing single-line stdout. */
void trim_trailing_newlines(char *s);

/* Parse leading integer id from stdout; returns -1 on failure. */
long parse_id_stdout(const char *out);

/* True if s is NULL or only whitespace. */
bool str_is_blank(const char *s);

/*
 * Run `sqlite3 <db> <sql>` and return the first line of stdout (heap; caller
 * frees), or NULL when the statement printed nothing (an UPDATE, no row).
 * A run that cannot do its job fails the calling test at the caller's line:
 * the command does not fit the buffer, the shell cannot start, or sqlite3
 * exits non-zero (not on PATH, an SQL error). Fixture SQL never fails quietly.
 */
/* Where a harness call came from, so a failure names the caller's line. */
typedef struct {
    const char *file;
    long line;
} HarnessSite;

char *harness_sqlite_query_line_at(const char *db_path, const char *sql, HarnessSite site);
#define harness_sqlite_query_line(db_path, sql)                                                    \
    harness_sqlite_query_line_at((db_path), (sql), (HarnessSite){__FILE__, __LINE__})

/*
 * Rewrite a v6 brain this build wrote into the v5 shape, in place, so
 * `import --from-db` (v4 and v5 sources until plan 23) can read it: entries
 * get the v5 columns back (id = local_id, sync_id = id, source = the creating
 * actor's kind), live links keep from_id/to_id/kind/times, the v6-only tables
 * go, and empty E12 tables come back. A file that is not at v6 is left as is.
 * Fails the calling test like harness_sqlite_query_line.
 */
void harness_make_v5_source_at(const char *db_path, HarnessSite site);
#define harness_make_v5_source(db_path)                                                            \
    harness_make_v5_source_at((db_path), (HarnessSite){__FILE__, __LINE__})

/* Directory containing path (heap; caller frees). */
char *dir_of_path(const char *file_path);

#endif /* REMEMBER_HARNESS_H */
