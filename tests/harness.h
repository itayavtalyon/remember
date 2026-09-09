#ifndef REMEMBER_HARNESS_H
#define REMEMBER_HARNESS_H

#include <stdbool.h>
#include <stddef.h>

/* Shared test constants (enum so a header include never warns "unused"). */
enum {
    ERR_BUFSIZE = 256,     /* store_open error-message buffer */
    PERM_BITS_MASK = 0777, /* st_mode permission bits (cast to unsigned at bitwise use) */
    DIR_PERMS = 0700,      /* store directory mode */
    DB_FILE_PERMS = 0600   /* db-file / blocker-file mode */
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
    int exit_code;
    char *out; /* stdout, heap, may be empty string */
    char *err; /* stderr, heap, may be empty string */
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
 * Run `sqlite3 <db> <sql>` and return first line of stdout (heap; caller frees).
 * Returns NULL if sqlite3 missing or command failed.
 */
char *harness_sqlite_query_line(const char *db_path, const char *sql);

/* Directory containing path (heap; caller frees). */
char *dir_of_path(const char *file_path);

#endif /* REMEMBER_HARNESS_H */
