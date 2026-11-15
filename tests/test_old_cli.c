#include "cli.h"
#include "diag.h"
#include "harness.h"
#include "register.h"
#include "remember_app.h"
#include "store.h"
#include "test.h"

#include <limits.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/*
 * Plan 27b: an older remember opens a newer brain read-only. Reads warn on
 * every command; writes fail with schema_too_new and exit 75. The copy is
 * R27b-1 (decision:2026-11-12-copy-batch-final). A newer brain here is a brain
 * this build wrote, with user_version raised past the schema this build knows.
 */

enum {
    OLD_CLI_SCHEMA = 6,
    OLD_CLI_NEWER = 7,
    OLD_CLI_SQL_BUF = 64,
    OLD_CLI_UPDATE_BUF = 512,
    OLD_CLI_MAX_ARGS = 8,
    OLD_CLI_BUSY_MS = 5000
};

/* R27b-1, schema 7 brain and schema 6 build. Literal, never built from the code. */
#define K_NEWER_MESSAGE                                                                            \
    "this brain was made by a newer version of remember, so it is read-only here. You can still "  \
    "read and export it. This brain uses schema 7; this remember reads up to schema 6."
#define K_NEWER_FIX "Upgrade remember to make changes."

static void exec_sql(const char *db, const char *sql)
{
    free(harness_sqlite_query_line(db, sql));
}

/* A brain with one memory ("seed", id 1) whose user_version is `schema`. */
static char *given_brain_at_schema(int schema)
{
    char *db = make_temp_db_path();
    const char *add[] = {"add", "seed"};
    char sql[OLD_CLI_SQL_BUF];
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, add, sizeof(add) / sizeof(add[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    (void)snprintf(sql, sizeof(sql), "PRAGMA user_version=%d;", schema);
    exec_sql(db, sql);
    return db;
}

TEST(old_cli_detect_by_schema_version)
{
    char *newer = given_brain_at_schema(OLD_CLI_NEWER);
    char *equal = given_brain_at_schema(OLD_CLI_SCHEMA);
    char err[ERR_BUFSIZE];
    StoreSchemaGap gap = {.brain_schema = 0};
    Store *s = NULL;

    err[0] = '\0';
    s = store_open(newer, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    ASSERT_STREQ(err, "");
    ASSERT_TRUE(store_newer_brain(s, NULL));
    ASSERT_TRUE(store_newer_brain(s, &gap));
    ASSERT_EQ_INT(gap.brain_schema, OLD_CLI_NEWER);
    ASSERT_EQ_INT(gap.cli_schema, OLD_CLI_SCHEMA);
    store_close(s);

    s = store_open(equal, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    ASSERT_FALSE(store_newer_brain(s, &gap));
    store_close(s);
    free(newer);
    free(equal);
}

/* The store alone refuses a write on a newer schema, so no caller can skip the
   CLI's own check by mistake. */
TEST(old_cli_the_store_refuses_a_write_on_a_newer_schema)
{
    static const char k_now[] = "2026-06-15T12:00:00.000Z";
    static const char k_hash[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    char *db = given_brain_at_schema(OLD_CLI_NEWER);
    char err[ERR_BUFSIZE];
    Store *s = store_open(db, err, sizeof(err));
    StoreAddAction act = STORE_ADD_CREATED;
    StoreBrainId id;
    Entry e;

    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_add(s, "three", k_hash, NULL, NULL, 0U, "human", NULL, k_now, &act, &e),
                     STORE_ERR_SCHEMA_TOO_NEW);
    ASSERT_EQ_STATUS(store_brain_new_id(s, &id), STORE_ERR_SCHEMA_TOO_NEW);
    store_close(s);
    free(db);
}

/* Review 2026-11-13: a newer remember upgrades the brain after this open read
   user_version 5 and before it takes the write lock. The open re-reads the
   version under the lock and must open read only, as the outer gate does, not
   fail with "database is newer". The child waits on a pipe until the parent
   holds the lock (no SQLite handle crosses the fork). */
TEST(old_cli_a_brain_upgraded_during_the_open_opens_read_only)
{
    static const struct timespec k_window = {0, 300000000L}; /* child reaches BEGIN */
    char *db = given_brain_at_schema(OLD_CLI_SCHEMA - 1);
    sqlite3 *raw = NULL;
    char *version = NULL;
    int ready[2] = {-1, -1};
    int status = 0;
    pid_t pid = 0;
    char token = 'g';

    // NOLINTNEXTLINE(android-cloexec-pipe) macOS has no pipe2; the child execs nothing
    ASSERT_EQ_INT(pipe(ready), 0);
    (void)fflush(stdout);
    (void)fflush(stderr);
    pid = fork();
    ASSERT_TRUE(pid >= 0);
    if (pid == 0) {
        char err[ERR_BUFSIZE];
        StoreSchemaGap gap = {.brain_schema = 0};
        Store *s = NULL;
        int code = 1;

        (void)close(ready[1]);
        if (read(ready[0], &token, 1) != 1) {
            _exit(2);
        }
        s = store_open(db, err, sizeof(err));
        if (s != NULL && store_newer_brain(s, &gap) && gap.brain_schema == OLD_CLI_NEWER) {
            code = 0;
        }
        store_close(s);
        _exit(code);
    }
    (void)close(ready[0]);
    ASSERT_EQ_INT(sqlite3_open(db, &raw), SQLITE_OK);
    ASSERT_EQ_INT(sqlite3_busy_timeout(raw, OLD_CLI_BUSY_MS), SQLITE_OK);
    ASSERT_EQ_INT(sqlite3_exec(raw, "BEGIN IMMEDIATE;", NULL, NULL, NULL), SQLITE_OK);
    ASSERT_EQ_INT((int)write(ready[1], &token, 1), 1);
    (void)close(ready[1]);
    (void)nanosleep(&k_window, NULL);
    ASSERT_EQ_INT(sqlite3_exec(raw, "PRAGMA user_version = 7; COMMIT;", NULL, NULL, NULL),
                  SQLITE_OK);
    (void)sqlite3_close(raw);
    ASSERT_EQ_INT((int)waitpid(pid, &status, 0), (int)pid);
    ASSERT_TRUE(WIFEXITED(status) != 0);
    ASSERT_EQ_INT(WEXITSTATUS(status), 0);
    version = harness_sqlite_query_line(db, "PRAGMA user_version;");
    ASSERT_STREQ(version, "7");
    free(version);
    free(db);
}

/* Runs `remember --db <db> <args...>`; the caller frees the result. */
#define RUN_ARGS(db, ...)                                                                          \
    run_remember((db), (const char *const[]){__VA_ARGS__},                                         \
                 sizeof((const char *const[]){__VA_ARGS__}) / sizeof(const char *), NULL)

TEST(old_cli_newer_brain_read_human)
{
    char *db = given_brain_at_schema(OLD_CLI_NEWER);
    CmdResult r = RUN_ARGS(db, "get", "1");

    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.out, "seed\n");
    ASSERT_STREQ(r.err, "remember: warning: " K_NEWER_MESSAGE " " K_NEWER_FIX "\n");
    cmd_result_free(&r);
    free(db);
}

TEST(old_cli_newer_brain_read_json)
{
    char *db = given_brain_at_schema(OLD_CLI_NEWER);
    CmdResult r = RUN_ARGS(db, "--json", "get", "1");

    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "\"body\":\"seed\"");
    ASSERT_STR_CONTAINS(
        r.out,
        ",\"warnings\":[{\"code\":\"schema_too_new_read_only\",\"message\":\"" K_NEWER_MESSAGE
        "\",\"fix\":\"" K_NEWER_FIX "\",\"detail\":{\"brain_schema_version\":\"7\","
        "\"cli_schema_version\":\"6\"}}]}\n");
    cmd_result_free(&r);
    free(db);
}

/* ---- helpers ---------------------------------------------------------------- */

/* The brain file's bytes before a command, to compare after it. */
typedef struct {
    char *bytes;
    size_t len;
} Snapshot;

static Snapshot given_snapshot(const char *db)
{
    Snapshot snap = {NULL, 0U};

    snap.bytes = harness_read_file(db, &snap.len);
    ASSERT_TRUE(snap.bytes != NULL);
    return snap;
}

static void snapshot_free(Snapshot *snap)
{
    free(snap->bytes);
    snap->bytes = NULL;
}

static void assert_unchanged(const char *db, const Snapshot *before)
{
    size_t len = 0U;
    char *after = harness_read_file(db, &len);

    ASSERT_TRUE(after != NULL);
    ASSERT_EQ_INT(len, before->len);
    ASSERT_TRUE(after != NULL && memcmp(after, before->bytes, before->len) == 0);
    free(after);
}

#define K_WRITE_LINE "remember: " K_NEWER_MESSAGE " " K_NEWER_FIX "\n"
#define K_READ_LINE "remember: warning: " K_NEWER_MESSAGE " " K_NEWER_FIX "\n"

TEST(old_cli_newer_brain_write_human)
{
    char *db = given_brain_at_schema(OLD_CLI_NEWER);
    Snapshot before = given_snapshot(db);
    CmdResult r = RUN_ARGS(db, "add", "another");

    ASSERT_EQ_INT(r.exit_code, 75);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, K_WRITE_LINE);
    assert_unchanged(db, &before);
    cmd_result_free(&r);
    snapshot_free(&before);
    free(db);
}

TEST(old_cli_newer_brain_write_json)
{
    char *db = given_brain_at_schema(OLD_CLI_NEWER);
    Snapshot before = given_snapshot(db);
    CmdResult r = RUN_ARGS(db, "--json", "add", "another");

    ASSERT_EQ_INT(r.exit_code, 75);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out,
                 "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.add.result\","
                 "\"error\":{\"code\":\"schema_too_new\",\"message\":\"" K_NEWER_MESSAGE
                 "\",\"fix\":\"" K_NEWER_FIX "\",\"retryable\":false,\"detail\":{"
                 "\"brain_schema_version\":\"7\",\"cli_schema_version\":\"6\"}}}\n");
    assert_unchanged(db, &before);
    cmd_result_free(&r);
    snapshot_free(&before);
    free(db);
}

TEST(old_cli_no_brew_command_in_output)
{
    char *db = given_brain_at_schema(OLD_CLI_NEWER);
    CmdResult read_human = RUN_ARGS(db, "get", "1");
    CmdResult read_json = RUN_ARGS(db, "--json", "get", "1");
    CmdResult write_human = RUN_ARGS(db, "add", "x");
    CmdResult write_json = RUN_ARGS(db, "--json", "add", "x");
    const CmdResult *all[] = {&read_human, &read_json, &write_human, &write_json};
    size_t i = 0;

    for (i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        ASSERT_STR_NOT_CONTAINS(all[i]->out, "brew");
        ASSERT_STR_NOT_CONTAINS(all[i]->err, "brew");
    }
    ASSERT_STREQ(read_human.err, K_READ_LINE);
    ASSERT_STREQ(write_human.err, K_WRITE_LINE);
    ASSERT_STR_CONTAINS(read_json.out,
                        "\"message\":\"" K_NEWER_MESSAGE "\",\"fix\":\"" K_NEWER_FIX "\"");
    ASSERT_STR_CONTAINS(write_json.out,
                        "\"message\":\"" K_NEWER_MESSAGE "\",\"fix\":\"" K_NEWER_FIX "\"");
    cmd_result_free(&read_human);
    cmd_result_free(&read_json);
    cmd_result_free(&write_human);
    cmd_result_free(&write_json);
    free(db);
}

TEST(old_cli_no_cli_outdated_code)
{
    char *db = given_brain_at_schema(OLD_CLI_NEWER);
    CmdResult read_json = RUN_ARGS(db, "--json", "list");
    CmdResult write_json = RUN_ARGS(db, "--json", "delete", "1");
    CmdResult write_human = RUN_ARGS(db, "delete", "1");
    int code = 0;

    ASSERT_STR_NOT_CONTAINS(read_json.out, "cli_outdated");
    ASSERT_STR_NOT_CONTAINS(write_json.out, "cli_outdated");
    ASSERT_STR_NOT_CONTAINS(write_human.err, "cli_outdated");
    for (code = 0; code < DIAG_CODE_COUNT; code++) {
        ASSERT_STR_NOT_CONTAINS(diag_code_name((DiagCode)code), "cli_outdated");
    }
    for (code = 0; code < DIAG_WARN_COUNT; code++) {
        ASSERT_STR_NOT_CONTAINS(diag_warning_name((DiagWarning)code), "cli_outdated");
    }
    cmd_result_free(&read_json);
    cmd_result_free(&write_json);
    cmd_result_free(&write_human);
    free(db);
}

/* ---- every command ----------------------------------------------------------- */

/* args end at the first NULL. */
typedef struct {
    const char *args[OLD_CLI_MAX_ARGS];
} Invocation;

static size_t arg_count(const Invocation *inv)
{
    size_t n = 0;

    while (inv->args[n] != NULL) {
        n++;
    }
    return n;
}

static const Invocation k_writes[] = {
    {{"add", "another", NULL}},
    {{"update", "1", "--text", "changed", NULL}},
    {{"delete", "1", NULL}},
    {{"link", "1", "2", NULL}},
    {{"unlink", "1", "2", NULL}},
    {{"rekey", "1", "--new-key", "k", NULL}},
    {{"purge", "--deleted", NULL}},
    {{"purge", "--expired", NULL}},
    {{"import", "/nonexistent/export.json", NULL}},
    {{"conflict", "accept", "--id", "1", "--keep", "local", NULL}},
    {{"repair", "search-index", NULL}},
    {{"settings", "set", "remember.search.max_results", "50", NULL}},
    {{"settings", "reset", "remember.search.max_results", NULL}},
    {{"settings", "import", "/nonexistent/settings.json", NULL}},
    /* decision:2026-11-13-plan27b-review-grill G3: one class per verb, whatever the
       scope or --dry-run. */
    {{"settings", "set", "remember.search.max_results", "50", "--scope", "user", NULL}},
    {{"settings", "reset", "remember.search.max_results", "--scope", "user", NULL}},
    {{"settings", "import", "--dry-run", "/nonexistent/settings.json", NULL}},
    {{"brain", "new-id", NULL}},
};

TEST(old_cli_every_write_command_is_refused)
{
    char *db = given_brain_at_schema(OLD_CLI_NEWER);
    Snapshot before = given_snapshot(db);
    size_t i = 0;

    for (i = 0; i < sizeof(k_writes) / sizeof(k_writes[0]); i++) {
        CmdResult human = run_remember(db, k_writes[i].args, arg_count(&k_writes[i]), NULL);

        ASSERT_EQ_INT(human.exit_code, 75);
        ASSERT_STREQ(human.out, "");
        ASSERT_STREQ(human.err, K_WRITE_LINE);
        cmd_result_free(&human);
    }
    for (i = 0; i < sizeof(k_writes) / sizeof(k_writes[0]); i++) {
        const char *args[OLD_CLI_MAX_ARGS + 1U] = {"--json"};
        CmdResult json;
        size_t a = 0;

        for (a = 0; a < arg_count(&k_writes[i]); a++) {
            args[a + 1U] = k_writes[i].args[a];
        }
        json = run_remember(db, args, arg_count(&k_writes[i]) + 1U, NULL);
        ASSERT_EQ_INT(json.exit_code, 75);
        ASSERT_STREQ(json.err, "");
        ASSERT_STR_CONTAINS(
            json.out, "\"error\":{\"code\":\"schema_too_new\",\"message\":\"" K_NEWER_MESSAGE "\"");
        cmd_result_free(&json);
    }
    assert_unchanged(db, &before);
    snapshot_free(&before);
    free(db);
}

static const Invocation k_reads[] = {
    {{"get", "1", NULL}},
    {{"list", NULL}},
    {{"search", "seed", NULL}},
    {{"tags", NULL}},
    {{"related", "1", NULL}},
    {{"conflicts", NULL}},
    {{"device", NULL}},
    {{"settings", "get", "remember.search.max_results", NULL}},
    {{"settings", "list", NULL}},
    {{"settings", "history", NULL}},
    {{"settings", "explain", "remember.search.max_results", NULL}},
    {{"settings", "export", NULL}},
};

TEST(old_cli_every_read_command_warns_every_time)
{
    char *db = given_brain_at_schema(OLD_CLI_NEWER);
    /* The settings reads open user.db: keep it beside the brain, not under HOME. */
    char *user_db = harness_side_path(db, ".user.db");
    Snapshot before = given_snapshot(db);
    size_t i = 0;

    // NOLINTNEXTLINE(concurrency-mt-unsafe) single-threaded test runner
    ASSERT_EQ_INT(setenv("REMEMBER_USER_DB", user_db, 1), 0);

    for (i = 0; i < sizeof(k_reads) / sizeof(k_reads[0]); i++) {
        for (int run = 0; run < 2; run++) {
            CmdResult r = run_remember(db, k_reads[i].args, arg_count(&k_reads[i]), NULL);

            ASSERT_EQ_INT(r.exit_code, 0);
            ASSERT_STREQ(r.err, K_READ_LINE);
            cmd_result_free(&r);
        }
    }
    // NOLINTNEXTLINE(concurrency-mt-unsafe) single-threaded test runner
    (void)unsetenv("REMEMBER_USER_DB");
    assert_unchanged(db, &before);
    snapshot_free(&before);
    free(user_db);
    free(db);
}

TEST(old_cli_export_warns_every_time)
{
    char *db = given_brain_at_schema(OLD_CLI_NEWER);
    Snapshot before = given_snapshot(db);
    int run = 0;

    for (run = 0; run < 2; run++) {
        char out_path[REMEMBER_PATH_MAX];
        const char *args[] = {"export", "--format", "json", "--out", out_path};
        CmdResult r;

        (void)snprintf(out_path, sizeof(out_path), "%s.export%d.json", db, run);
        r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STREQ(r.err, K_READ_LINE);
        (void)remove(out_path);
        cmd_result_free(&r);
    }
    assert_unchanged(db, &before);
    snapshot_free(&before);
    free(db);
}

/* decision:2026-11-13-plan27b-review-grill G1: device new writes no brain bytes,
   so it is a read. It mints a new sidecar id, warns, and exits 0. */
TEST(old_cli_device_new_is_a_read_that_warns)
{
    char *db = given_brain_at_schema(OLD_CLI_NEWER);
    char *sidecar = harness_side_path(db, ".d/device_id");
    char *before_id = harness_read_line(sidecar);
    Snapshot before = given_snapshot(db);
    CmdResult r = RUN_ARGS(db, "device", "new");
    char *after_id = harness_read_line(sidecar);

    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, K_READ_LINE);
    ASSERT_TRUE(before_id != NULL && after_id != NULL);
    ASSERT_TRUE(before_id != NULL && after_id != NULL && strcmp(before_id, after_id) != 0);
    assert_unchanged(db, &before);
    cmd_result_free(&r);
    snapshot_free(&before);
    free(after_id);
    free(before_id);
    free(sidecar);
    free(db);
}

/* A newer brain copied without its <brain>.d folder: the read mints the sidecar
   under an empty write transaction and leaves the brain bytes as they were. */
TEST(old_cli_a_read_without_a_sidecar_leaves_the_brain_unchanged)
{
    char *db = given_brain_at_schema(OLD_CLI_NEWER);
    char *dir = harness_side_path(db, ".d");
    char *sidecar = harness_side_path(db, ".d/device_id");
    Snapshot before;
    CmdResult r;
    char *id = NULL;

    harness_remove_tree(dir);
    ASSERT_TRUE(access(sidecar, F_OK) != 0);
    before = given_snapshot(db);
    r = RUN_ARGS(db, "list");
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, K_READ_LINE);
    assert_unchanged(db, &before);
    id = harness_read_line(sidecar);
    ASSERT_TRUE(id != NULL);
    cmd_result_free(&r);
    snapshot_free(&before);
    free(id);
    free(sidecar);
    free(dir);
    free(db);
}

/* The table is the only place that names a command's class. A command in
   CliCommand that the table forgot is refused here. */
TEST(old_cli_every_command_has_a_class)
{
    int cmd = 0;

    for (cmd = 0; cmd <= CLI_CMD_UNKNOWN; cmd++) {
        ASSERT_TRUE(app_command_class((CliCommand)cmd, 0, NULL) != APP_CMD_UNLISTED);
    }
    ASSERT_EQ_INT(app_command_class((CliCommand)((int)CLI_CMD_UNKNOWN + 1), 0, NULL),
                  APP_CMD_UNLISTED);
}

TEST(old_cli_the_class_table_splits_reads_from_writes)
{
    const char *set[] = {"set"};
    const char *get[] = {"get"};

    ASSERT_EQ_INT(app_command_class(CLI_CMD_HELP, 0, NULL), APP_CMD_NO_STORE);
    ASSERT_EQ_INT(app_command_class(CLI_CMD_ADD, 0, NULL), APP_CMD_WRITE);
    ASSERT_EQ_INT(app_command_class(CLI_CMD_GET, 0, NULL), APP_CMD_READ);
    ASSERT_EQ_INT(app_command_class(CLI_CMD_SETTINGS, 1, set), APP_CMD_WRITE);
    ASSERT_EQ_INT(app_command_class(CLI_CMD_SETTINGS, 1, get), APP_CMD_READ);
    ASSERT_EQ_INT(app_command_class(CLI_CMD_SETTINGS, 0, NULL), APP_CMD_READ);
}

/* The post-command sweep (decision:2026-10-31-no-sweep-agent) is not in the
   engine yet (plan 32). What can hold today: a command on a newer brain, read
   or refused write, leaves the file and its folder as they were. */
TEST(old_cli_a_command_leaves_no_journal_and_no_new_bytes)
{
    char *db = given_brain_at_schema(OLD_CLI_NEWER);
    char *journal = harness_side_path(db, "-journal");
    char *wal = harness_side_path(db, "-wal");
    Snapshot before = given_snapshot(db);
    CmdResult read = RUN_ARGS(db, "list");
    CmdResult write = RUN_ARGS(db, "add", "x");

    ASSERT_EQ_INT(read.exit_code, 0);
    ASSERT_EQ_INT(write.exit_code, 75);
    assert_unchanged(db, &before);
    ASSERT_TRUE(access(journal, F_OK) != 0);
    ASSERT_TRUE(access(wal, F_OK) != 0);
    cmd_result_free(&read);
    cmd_result_free(&write);
    snapshot_free(&before);
    free(journal);
    free(wal);
    free(db);
}

/* ---- hostile values -------------------------------------------------------- */

/* R27b-2 (decision:2026-11-14-copy-r27b-2-r27b-3-approved). A brain newer only
   by min_reader_version (its schema_version equals this build's). Literals, never
   built from the code under test. */
#define K_C01_MESSAGE                                                                              \
    "this brain was made by a newer version of remember, so it is read-only here. You can still "  \
    "read and export it."

/* What `remember --version` prints after "remember ": the <version> of the
   line (decision:2026-11-14-r27b-2-3-stop-and-ask, answer 1). */
static void given_cli_version(const char *db, char *out, size_t cap)
{
    static const char k_prefix[] = "remember ";
    const size_t prefix = sizeof(k_prefix) - 1U;
    CmdResult r = RUN_ARGS(db, "version");
    size_t len = strlen(r.out);

    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_TRUE(strncmp(r.out, k_prefix, prefix) == 0);
    ASSERT_TRUE(len > prefix + 1U && r.out[len - 1U] == '\n' && len - prefix <= cap);
    (void)snprintf(out, cap, "%.*s", (int)(len - prefix - 1U), r.out + prefix);
    cmd_result_free(&r);
}

enum { OLD_CLI_LINE_BUF = 1024 };

/* A schema 6 brain whose min_reader_version is `value` (SQL text). */
static char *given_brain_with_min_reader(const char *value_sql)
{
    char *db = given_brain_at_schema(OLD_CLI_SCHEMA);
    char sql[OLD_CLI_UPDATE_BUF];

    (void)snprintf(sql, sizeof(sql), "UPDATE brain_meta SET min_reader_version = %s;", value_sql);
    exec_sql(db, sql);
    return db;
}

/* The four outputs of one brain: read and write, plain and JSON. */
typedef struct {
    CmdResult read_human;
    CmdResult read_json;
    CmdResult write_human;
    CmdResult write_json;
} FourRuns;

static FourRuns given_four_runs(const char *db)
{
    FourRuns f = {RUN_ARGS(db, "get", "1"), RUN_ARGS(db, "--json", "get", "1"),
                  RUN_ARGS(db, "add", "x"), RUN_ARGS(db, "--json", "add", "x")};

    return f;
}

static void four_runs_free(FourRuns *f)
{
    cmd_result_free(&f->read_human);
    cmd_result_free(&f->read_json);
    cmd_result_free(&f->write_human);
    cmd_result_free(&f->write_json);
}

/* The detail object of both JSON runs must be exactly `detail_json`, and the
   message in both exactly `message`. */
static void assert_json_notices(const FourRuns *f, const char *message, const char *detail_json)
{
    char want[OLD_CLI_LINE_BUF];

    (void)snprintf(want, sizeof(want),
                   ",\"warnings\":[{\"code\":\"schema_too_new_read_only\",\"message\":\"%s\","
                   "\"fix\":\"" K_NEWER_FIX "\",\"detail\":%s}]}\n",
                   message, detail_json);
    ASSERT_STR_CONTAINS(f->read_json.out, want);
    ASSERT_STREQ(f->read_json.err, "");
    (void)snprintf(
        want, sizeof(want),
        "\"error\":{\"code\":\"schema_too_new\",\"message\":\"%s\",\"fix\":\"" K_NEWER_FIX
        "\",\"retryable\":false,\"detail\":%s}}\n",
        message, detail_json);
    ASSERT_STR_CONTAINS(f->write_json.out, want);
    ASSERT_STREQ(f->write_json.err, "");
}

static void assert_plain_lines(const FourRuns *f, const char *message)
{
    char want[OLD_CLI_LINE_BUF];

    ASSERT_EQ_INT(f->read_human.exit_code, 0);
    (void)snprintf(want, sizeof(want), "remember: warning: %s " K_NEWER_FIX "\n", message);
    ASSERT_STREQ(f->read_human.err, want);
    ASSERT_EQ_INT(f->write_human.exit_code, 75);
    (void)snprintf(want, sizeof(want), "remember: %s " K_NEWER_FIX "\n", message);
    ASSERT_STREQ(f->write_human.err, want);
    ASSERT_EQ_INT(f->write_json.exit_code, 75);
}

TEST(old_cli_min_reader_variant_1_read_and_write_lines)
{
    char *db = given_brain_with_min_reader("'0.4.0'");
    char version[OLD_CLI_SQL_BUF];
    FourRuns f = given_four_runs(db);
    char message[OLD_CLI_LINE_BUF];

    given_cli_version(db, version, sizeof(version));
    (void)snprintf(message, sizeof(message),
                   K_C01_MESSAGE " This brain needs remember 0.4.0 or later; this is remember %s.",
                   version);
    assert_plain_lines(&f, message);
    ASSERT_STR_CONTAINS(f.read_json.out, message);
    ASSERT_STR_CONTAINS(f.write_json.out, message);
    ASSERT_STR_NOT_CONTAINS(f.read_human.err, "schema 6");
    four_runs_free(&f);
    free(db);
}

/* Variant 2: the value is bytes of unknown form, so no number is printed. */
TEST(old_cli_min_reader_variant_2_for_unparseable_value)
{
    char *db = given_brain_with_min_reader("'not-a-version'");
    FourRuns f = given_four_runs(db);

    assert_plain_lines(&f, K_C01_MESSAGE);
    ASSERT_STR_NOT_CONTAINS(f.read_human.err, "needs remember");
    ASSERT_STR_NOT_CONTAINS(f.write_human.err, "schema");
    ASSERT_STR_NOT_CONTAINS(f.read_human.err, "not-a-version");
    four_runs_free(&f);
    free(db);
}

/* detail: four members, in both variants and for both codes. Variant 2 masks
   the stored value (answer 2): the member stays and holds "". */
TEST(old_cli_min_reader_detail_has_four_members_both_variants_both_codes)
{
    char *db = given_brain_with_min_reader("'0.4.0'");
    char *bad = given_brain_with_min_reader("'4'");
    char version[OLD_CLI_SQL_BUF];
    char detail[OLD_CLI_LINE_BUF];
    FourRuns f = given_four_runs(db);

    given_cli_version(db, version, sizeof(version));
    (void)snprintf(detail, sizeof(detail),
                   "{\"brain_schema_version\":\"6\",\"cli_schema_version\":\"6\","
                   "\"min_reader_version\":\"0.4.0\",\"cli_version\":\"%s\"}",
                   version);
    ASSERT_STR_CONTAINS(f.read_json.out, detail);
    ASSERT_STR_CONTAINS(f.write_json.out, detail);
    four_runs_free(&f);

    f = given_four_runs(bad);
    (void)snprintf(detail, sizeof(detail),
                   "{\"brain_schema_version\":\"6\",\"cli_schema_version\":\"6\","
                   "\"min_reader_version\":\"\",\"cli_version\":\"%s\"}",
                   version);
    assert_json_notices(&f, K_C01_MESSAGE, detail);
    four_runs_free(&f);
    free(bad);
    free(db);
}

/* A hostile value is masked in the JSON detail and never printed in the human
   line: Variant 2 for each (decision:2026-11-14-r27b-2-3-stop-and-ask, answer 2). */
TEST(old_cli_hostile_min_reader_version_is_masked)
{
    static const char *const k_hostile[] = {
        "'not-a-version'",
        "printf('%.300c', 'x')",
        "char(27) || '[31m' || char(10) || 'evil'",
        "'99999999999999999999.0.0'",
        "printf('%.300c', '0') || '4.0.0'", /* parses, but is over-long */
        "'0.4.0 '",
        "''",
    };
    char *db = given_brain_at_schema(OLD_CLI_SCHEMA);
    char version[OLD_CLI_SQL_BUF];
    char detail[OLD_CLI_LINE_BUF];
    size_t i = 0;

    given_cli_version(db, version, sizeof(version));
    free(db);
    (void)snprintf(detail, sizeof(detail),
                   "{\"brain_schema_version\":\"6\",\"cli_schema_version\":\"6\","
                   "\"min_reader_version\":\"\",\"cli_version\":\"%s\"}",
                   version);
    for (i = 0; i < sizeof(k_hostile) / sizeof(k_hostile[0]); i++) {
        FourRuns f;

        db = given_brain_with_min_reader(k_hostile[i]);
        f = given_four_runs(db);
        assert_plain_lines(&f, K_C01_MESSAGE);
        assert_json_notices(&f, K_C01_MESSAGE, detail);
        ASSERT_STR_NOT_CONTAINS(f.read_human.err, "evil");
        ASSERT_STR_NOT_CONTAINS(f.write_json.out, "evil");
        four_runs_free(&f);
        free(db);
    }
}

/* The longest version that prints is 32 bytes; one more is masked. */
TEST(old_cli_min_reader_version_prints_up_to_32_bytes)
{
    static const char k_max[] = "0.4.0000000000000000000000000000";   /* 32 bytes */
    static const char k_over[] = "0.4.00000000000000000000000000000"; /* 33 bytes */
    char sql[OLD_CLI_SQL_BUF];
    char *db = NULL;
    char *over = NULL;
    CmdResult at;
    CmdResult past;

    ASSERT_EQ_INT((int)strlen(k_max), 32);
    ASSERT_EQ_INT((int)strlen(k_over), 33);
    (void)snprintf(sql, sizeof(sql), "'%s'", k_max);
    db = given_brain_with_min_reader(sql);
    (void)snprintf(sql, sizeof(sql), "'%s'", k_over);
    over = given_brain_with_min_reader(sql);
    at = RUN_ARGS(db, "get", "1");
    past = RUN_ARGS(over, "get", "1");
    ASSERT_STR_CONTAINS(at.err,
                        "This brain needs remember 0.4.0000000000000000000000000000 or later;");
    ASSERT_STR_NOT_CONTAINS(past.err, "needs remember");
    ASSERT_STR_NOT_CONTAINS(past.err, "0.4.0");
    cmd_result_free(&at);
    cmd_result_free(&past);
    free(db);
    free(over);
}

/* The store ranks the schema first: a greater schema_version prints R27b-1,
   whatever min_reader_version says, and keeps the two-member detail. */
TEST(old_cli_greater_schema_prints_r27b_1_whatever_min_reader_says)
{
    static const char *const k_values[] = {"'0.4.0'", "'not-a-version'"};
    size_t i = 0;

    for (i = 0; i < sizeof(k_values) / sizeof(k_values[0]); i++) {
        char *db = given_brain_with_min_reader(k_values[i]);
        FourRuns f;

        exec_sql(db, "PRAGMA user_version=7;");
        f = given_four_runs(db);
        assert_plain_lines(&f, K_NEWER_MESSAGE);
        ASSERT_STR_CONTAINS(f.read_json.out, "\"detail\":{\"brain_schema_version\":\"7\","
                                             "\"cli_schema_version\":\"6\"}}]}\n");
        ASSERT_STR_NOT_CONTAINS(f.write_json.out, "min_reader_version");
        ASSERT_STR_NOT_CONTAINS(f.read_human.err, "needs remember");
        four_runs_free(&f);
        free(db);
    }
}

TEST(old_cli_the_largest_schema_prints_as_digits)
{
    char *db = given_brain_at_schema(INT_MAX);
    CmdResult r = RUN_ARGS(db, "get", "1");

    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.err,
                        "This brain uses schema 2147483647; this remember reads up to schema 6.");
    cmd_result_free(&r);
    free(db);
}

TEST(old_cli_same_version_no_warning)
{
    char *db = given_brain_at_schema(OLD_CLI_SCHEMA);
    CmdResult r = RUN_ARGS(db, "get", "1");
    CmdResult json = RUN_ARGS(db, "--json", "get", "1");

    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_NOT_CONTAINS(json.out, "warnings");
    cmd_result_free(&r);
    cmd_result_free(&json);
    free(db);
}

TEST(old_cli_min_reader_version_path)
{
    char *db = given_brain_with_min_reader("'0.4.0'");
    CmdResult read = RUN_ARGS(db, "get", "1");
    CmdResult write = RUN_ARGS(db, "add", "x");

    ASSERT_EQ_INT(read.exit_code, 0);
    ASSERT_STR_CONTAINS(read.err, "remember: warning: " K_C01_MESSAGE " This brain needs remember");
    ASSERT_EQ_INT(write.exit_code, 75);
    ASSERT_STR_CONTAINS(write.err, "remember: " K_C01_MESSAGE " This brain needs remember");
    cmd_result_free(&read);
    cmd_result_free(&write);
    free(db);
}

TEST(old_cli_strict_turns_a_read_warning_into_exit_4_and_keeps_exit_75)
{
    char *db = given_brain_at_schema(OLD_CLI_NEWER);
    CmdResult read = RUN_ARGS(db, "--strict", "get", "1");
    CmdResult write = RUN_ARGS(db, "--strict", "add", "x");

    ASSERT_EQ_INT(read.exit_code, 4);
    ASSERT_STREQ(read.err, K_READ_LINE);
    ASSERT_EQ_INT(write.exit_code, 75);
    ASSERT_STREQ(write.err, K_WRITE_LINE);
    cmd_result_free(&read);
    cmd_result_free(&write);
    free(db);
}

TEST(old_cli_help_and_version_do_not_open_a_brain)
{
    char *db = given_brain_at_schema(OLD_CLI_NEWER);
    CmdResult help = RUN_ARGS(db, "help");
    CmdResult version = RUN_ARGS(db, "version");

    ASSERT_EQ_INT(help.exit_code, 0);
    ASSERT_STREQ(help.err, "");
    ASSERT_EQ_INT(version.exit_code, 0);
    ASSERT_STREQ(version.err, "");
    cmd_result_free(&help);
    cmd_result_free(&version);
    free(db);
}

/* R27b-3 with answer 4 of decision:2026-11-14-r27b-2-3-stop-and-ask: the 5 line
   fits every exit 5 (plugin and hook approvals never take --yes). */
TEST(old_cli_help_exit_codes_list_5_and_75)
{
    static const char k_block[] = "Exit codes:\n"
                                  "  0  success (including empty search/list)\n"
                                  "  1  usage or error\n"
                                  "  2  not found (get/delete/update)\n"
                                  "  3  wrong bin (expired / not_expired / deleted / not_deleted)\n"
                                  "  4  a warning under --strict\n"
                                  "  5  confirmation or approval required\n"
                                  "  75 brain is read-only (made by a newer version of remember)\n";
    char *db = make_temp_db_path();
    const char *const flag_forms[][1] = {{"--help"}, {"-h"}, {"help"}};
    size_t i = 0;

    ASSERT_TRUE(db != NULL);
    for (i = 0; i < sizeof(flag_forms) / sizeof(flag_forms[0]); i++) {
        CmdResult r = run_remember(db, flag_forms[i], 1U, NULL);
        const char *at = strstr(r.out, "Exit codes:\n");

        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_TRUE(at != NULL);
        ASSERT_STREQ(at, k_block);
        cmd_result_free(&r);
    }
    free(db);
}

void register_old_cli_tests(void)
{
    RUN_TEST(old_cli_detect_by_schema_version);
    RUN_TEST(old_cli_the_store_refuses_a_write_on_a_newer_schema);
    RUN_TEST(old_cli_a_brain_upgraded_during_the_open_opens_read_only);
    RUN_TEST(old_cli_newer_brain_read_human);
    RUN_TEST(old_cli_newer_brain_read_json);
    RUN_TEST(old_cli_newer_brain_write_human);
    RUN_TEST(old_cli_newer_brain_write_json);
    RUN_TEST(old_cli_no_brew_command_in_output);
    RUN_TEST(old_cli_no_cli_outdated_code);
    RUN_TEST(old_cli_every_write_command_is_refused);
    RUN_TEST(old_cli_every_read_command_warns_every_time);
    RUN_TEST(old_cli_export_warns_every_time);
    RUN_TEST(old_cli_device_new_is_a_read_that_warns);
    RUN_TEST(old_cli_a_read_without_a_sidecar_leaves_the_brain_unchanged);
    RUN_TEST(old_cli_every_command_has_a_class);
    RUN_TEST(old_cli_the_class_table_splits_reads_from_writes);
    RUN_TEST(old_cli_a_command_leaves_no_journal_and_no_new_bytes);
    RUN_TEST(old_cli_min_reader_variant_1_read_and_write_lines);
    RUN_TEST(old_cli_min_reader_variant_2_for_unparseable_value);
    RUN_TEST(old_cli_min_reader_detail_has_four_members_both_variants_both_codes);
    RUN_TEST(old_cli_hostile_min_reader_version_is_masked);
    RUN_TEST(old_cli_min_reader_version_prints_up_to_32_bytes);
    RUN_TEST(old_cli_greater_schema_prints_r27b_1_whatever_min_reader_says);
    RUN_TEST(old_cli_the_largest_schema_prints_as_digits);
    RUN_TEST(old_cli_same_version_no_warning);
    RUN_TEST(old_cli_min_reader_version_path);
    RUN_TEST(old_cli_strict_turns_a_read_warning_into_exit_4_and_keeps_exit_75);
    RUN_TEST(old_cli_help_and_version_do_not_open_a_brain);
    RUN_TEST(old_cli_help_exit_codes_list_5_and_75);
}
