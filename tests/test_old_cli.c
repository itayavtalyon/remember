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
    StoreSchemaGap gap = {0, 0};
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
        StoreSchemaGap gap = {0, 0};
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

/* K_NEWER_MESSAGE with both numbers 6: a min_reader_version trigger. */
#define K_SAME_SCHEMA_MESSAGE                                                                      \
    "this brain was made by a newer version of remember, so it is read-only here. You can still "  \
    "read and export it. This brain uses schema 6; this remember reads up to schema 6."

TEST(old_cli_hostile_min_reader_version_is_never_echoed)
{
    static const char *const k_hostile[] = {
        "UPDATE brain_meta SET min_reader_version = 'not-a-version';",
        "UPDATE brain_meta SET min_reader_version = printf('%.300c', 'x');",
        "UPDATE brain_meta SET min_reader_version = char(27) || '[31m' || char(10) || 'evil';",
        "UPDATE brain_meta SET min_reader_version = '99999999999999999999.0.0';",
    };
    size_t i = 0;

    for (i = 0; i < sizeof(k_hostile) / sizeof(k_hostile[0]); i++) {
        char *db = given_brain_at_schema(OLD_CLI_SCHEMA);
        CmdResult human;
        CmdResult json;
        CmdResult write;

        exec_sql(db, k_hostile[i]);
        human = RUN_ARGS(db, "get", "1");
        json = RUN_ARGS(db, "--json", "get", "1");
        write = RUN_ARGS(db, "add", "x");
        ASSERT_EQ_INT(human.exit_code, 0);
        ASSERT_STREQ(human.err, "remember: warning: " K_SAME_SCHEMA_MESSAGE " " K_NEWER_FIX "\n");
        ASSERT_STR_CONTAINS(
            json.out,
            "\"message\":\"" K_SAME_SCHEMA_MESSAGE "\",\"fix\":\"" K_NEWER_FIX
            "\",\"detail\":{\"brain_schema_version\":\"6\",\"cli_schema_version\":\"6\"}}]}\n");
        ASSERT_EQ_INT(write.exit_code, 75);
        ASSERT_STREQ(write.err, "remember: " K_SAME_SCHEMA_MESSAGE " " K_NEWER_FIX "\n");
        cmd_result_free(&human);
        cmd_result_free(&json);
        cmd_result_free(&write);
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
    char *db = given_brain_at_schema(OLD_CLI_SCHEMA);
    CmdResult read;
    CmdResult write;

    exec_sql(db, "UPDATE brain_meta SET min_reader_version = '0.4.0';");
    read = RUN_ARGS(db, "get", "1");
    write = RUN_ARGS(db, "add", "x");
    ASSERT_EQ_INT(read.exit_code, 0);
    ASSERT_STREQ(read.err, "remember: warning: " K_SAME_SCHEMA_MESSAGE " " K_NEWER_FIX "\n");
    ASSERT_EQ_INT(write.exit_code, 75);
    ASSERT_STREQ(write.err, "remember: " K_SAME_SCHEMA_MESSAGE " " K_NEWER_FIX "\n");
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
    RUN_TEST(old_cli_hostile_min_reader_version_is_never_echoed);
    RUN_TEST(old_cli_the_largest_schema_prints_as_digits);
    RUN_TEST(old_cli_same_version_no_warning);
    RUN_TEST(old_cli_min_reader_version_path);
    RUN_TEST(old_cli_strict_turns_a_read_warning_into_exit_4_and_keeps_exit_75);
    RUN_TEST(old_cli_help_and_version_do_not_open_a_brain);
}
