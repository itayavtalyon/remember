#include "register.h"

/* The tests below drive the CLI; the store test binary has no CLI. */
#ifdef REMEMBER_PLUGINS_DIAG_TEST

#include "harness.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Counts, ids and times below are the fixtures under test. */
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

/* Plan 32 (specs/EVENTS.md "Expiry"). Decisions: decision:purge-command,
   decision:2026-11-18-plan32-grill-purge-delete-forms,
   decision:2026-11-18-plan32-grill-sweep-triggers, decision:expiry-local-marker (log 010 H-1.4
   to H-1.6, H-16.7, H-16.8). The sweep runs after every command, so a test that wants a row the
   sweep has not seen backdates it with SQL: the real clock only moves forward. */

static const char k_past[] = "2020-01-01T00:00:00.000Z";
static const char k_past_two[] = "2021-01-01T00:00:00.000Z";

#define RUN(db, ...)                                                                               \
    run_args((db), (const char *const[]){__VA_ARGS__},                                             \
             sizeof((const char *const[]){__VA_ARGS__}) / sizeof(const char *))

static CmdResult run_args(const char *db, const char *const *args, size_t count)
{
    return run_remember(db, args, count, NULL);
}

static void assert_ok(CmdResult r)
{
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the brain, the query, then its answer
static void assert_query(const char *db, const char *sql, const char *want)
{
    char *got = harness_sqlite_query_line(db, sql);

    ASSERT_TRUE(got != NULL);
    ASSERT_STREQ(got != NULL ? got : "", want);
    free(got);
}

/* The event types of the outbox rows, in the order they were written. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the brain, then the row types
static void assert_rows(const char *db, const char *want)
{
    assert_query(db,
                 "SELECT COALESCE(group_concat(type, ','), 'none') FROM "
                 "(SELECT type FROM outbox ORDER BY rowid);",
                 want);
}

static void exec_sql(const char *db, const char *sql)
{
    free(harness_sqlite_query_line(db, sql));
}

static char *given_brain_with_enabled_plugin(void)
{
    char *db = make_temp_db_path();

    ASSERT_TRUE(db != NULL);
    harness_install_hooker(ACTION(ALL_TYPES, ""));
    assert_ok(RUN(db, "settings", "set", "plugin.hooker.enabled", "true", "--scope", "brain"));
    return db;
}

/* A memory that will expire in a day (local id 1, 2, ... in the order they are added). */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the brain, then the body
static void add_expiring(const char *db, const char *body)
{
    assert_ok(RUN(db, "add", body, "--ttl", "1d"));
}

/* The row has been due since `at`, and no sweep has seen it. */
static void backdate(const char *db, int id, const char *at)
{
    char sql[256];

    (void)snprintf(sql, sizeof(sql),
                   "UPDATE entries SET expires_at = '%s', expired_at = NULL WHERE local_id = %d;",
                   at, id);
    exec_sql(db, sql);
}

static void assert_marked(const char *db, int id, bool marked)
{
    char sql[128];

    (void)snprintf(sql, sizeof(sql),
                   "SELECT expired_at IS NOT NULL FROM entries WHERE local_id = %d;", id);
    if (marked) {
        assert_query(db, sql, "1");
    } else {
        assert_query(db, sql, "0");
    }
}

/* ---- the sweep after a command ------------------------------------------------------------ */

TEST(sweep_emits_once_and_revival_clears)
{
    char *db = given_brain_with_enabled_plugin();

    add_expiring(db, "ephemeral");
    assert_rows(db, "memory.created");
    backdate(db, 1, k_past);
    assert_ok(RUN(db, "list"));
    assert_rows(db, "memory.created,memory.expired");
    assert_marked(db, 1, true);
    assert_query(db,
                 "SELECT json_extract(payload, '$.occurred_at') FROM outbox WHERE type = "
                 "'memory.expired';",
                 k_past);
    assert_query(db,
                 "SELECT json_extract(payload, '$.detected_at') > json_extract(payload, "
                 "'$.occurred_at') FROM outbox WHERE type = 'memory.expired';",
                 "1");
    assert_query(db,
                 "SELECT json_extract(payload, '$.cause.command') || ' ' || json_extract(payload, "
                 "'$.cause.origin') FROM outbox WHERE type = 'memory.expired';",
                 "sweep local");
    assert_query(db,
                 "SELECT json_extract(payload, '$.memory.bin') FROM outbox WHERE type = "
                 "'memory.expired';",
                 "expired");
    /* A second command emits nothing for that row. */
    assert_ok(RUN(db, "list"));
    assert_ok(RUN(db, "tags"));
    assert_rows(db, "memory.created,memory.expired");
    /* A later expiry on the row clears the marker in the write that sets it. */
    assert_ok(RUN(db, "update", "1", "--expired", "--ttl", "2d"));
    assert_marked(db, 1, false);
    assert_rows(db, "memory.created,memory.expired,memory.updated");
    /* Due again: one more event, with the new time. */
    backdate(db, 1, k_past_two);
    assert_ok(RUN(db, "list"));
    assert_rows(db, "memory.created,memory.expired,memory.updated,memory.expired");
    assert_query(db,
                 "SELECT json_extract(payload, '$.occurred_at') FROM outbox ORDER BY rowid DESC "
                 "LIMIT 1;",
                 k_past_two);
    free(db);
}

TEST(an_expiry_cleared_or_moved_later_clears_the_marker_and_an_earlier_one_does_not)
{
    char *db = given_brain_with_enabled_plugin();

    add_expiring(db, "ephemeral");
    backdate(db, 1, k_past_two);
    assert_ok(RUN(db, "list"));
    assert_marked(db, 1, true);
    /* Earlier than before and still due: the row stays marked, no second event. */
    exec_sql(db, "UPDATE entries SET expires_at = '2019-01-01T00:00:00.000Z' WHERE local_id = 1;");
    assert_ok(RUN(db, "list"));
    assert_rows(db, "memory.created,memory.expired");
    /* No expiry any more. */
    assert_ok(RUN(db, "update", "1", "--expired", "--clear-expires"));
    assert_marked(db, 1, false);
    free(db);
}

TEST(the_sweep_changes_neither_updated_at_nor_the_vector_nor_either_change_seq)
{
    char *db = given_brain_with_enabled_plugin();
    char *before = NULL;
    char *after = NULL;
    const char *probe = "SELECT updated_at || ' ' || version_vector || ' ' || change_seq || ' ' || "
                        "(SELECT change_seq FROM brain_meta) FROM entries WHERE local_id = 1;";

    add_expiring(db, "ephemeral");
    backdate(db, 1, k_past);
    before = harness_sqlite_query_line(db, probe);
    assert_ok(RUN(db, "list"));
    assert_marked(db, 1, true);
    after = harness_sqlite_query_line(db, probe);
    ASSERT_TRUE(before != NULL && after != NULL);
    ASSERT_STREQ(after != NULL ? after : "", before != NULL ? before : "");
    free(before);
    free(after);
    free(db);
}

TEST(sweep_no_plugins_no_row)
{
    char *db = make_temp_db_path();

    add_expiring(db, "ephemeral");
    backdate(db, 1, k_past);
    assert_ok(RUN(db, "list"));
    assert_marked(db, 1, true);
    assert_rows(db, "none");
    free(db);
}

TEST(the_sweep_leaves_live_deleted_and_unexpiring_rows_alone)
{
    char *db = given_brain_with_enabled_plugin();

    add_expiring(db, "live one");
    add_expiring(db, "deleted one");
    assert_ok(RUN(db, "add", "no expiry"));
    assert_ok(RUN(db, "delete", "2"));
    /* A deleted row can carry a due time (a hand-edited or synced brain): never swept. */
    exec_sql(db, "UPDATE entries SET expires_at = '2020-01-01T00:00:00.000Z' WHERE local_id = 2;");
    assert_ok(RUN(db, "list"));
    assert_marked(db, 1, false);
    assert_marked(db, 2, false);
    assert_marked(db, 3, false);
    assert_rows(db, "memory.created,memory.created,memory.created,memory.deleted");
    free(db);
}

TEST(one_sweep_is_one_batch_apart_from_the_command_that_ran_before_it)
{
    char *db = given_brain_with_enabled_plugin();

    add_expiring(db, "one");
    add_expiring(db, "two");
    backdate(db, 1, k_past);
    backdate(db, 2, k_past);
    assert_ok(RUN(db, "list"));
    assert_rows(db, "memory.created,memory.created,memory.expired,memory.expired");
    assert_query(db,
                 "SELECT count(DISTINCT json_extract(payload, '$.batch_id')) FROM outbox WHERE "
                 "type = 'memory.expired';",
                 "1");
    assert_query(db, "SELECT count(DISTINCT json_extract(payload, '$.batch_id')) FROM outbox;",
                 "3");
    free(db);
}

TEST(a_memory_added_already_due_is_created_by_the_command_and_expired_by_the_sweep_after_it)
{
    char *db = given_brain_with_enabled_plugin();

    assert_ok(RUN(db, "add", "old news", "--expires", "2001-01-01"));
    assert_rows(db, "memory.created,memory.expired");
    assert_query(db, "SELECT json_extract(payload, '$.cause.command') FROM outbox ORDER BY rowid;",
                 "add");
    assert_query(db,
                 "SELECT json_extract(payload, '$.cause.command') FROM outbox ORDER BY rowid "
                 "DESC LIMIT 1;",
                 "sweep");
    free(db);
}

TEST(every_command_that_opens_the_brain_read_write_sweeps_after_it_commits)
{
    static const char *const k_commands[][3] = {
        {"list", NULL, NULL},         {"tags", NULL, NULL},
        {"search", "anything", NULL}, {"add", "more", NULL},
        {"settings", "list", NULL},   {"device", NULL, NULL},
        {"conflicts", NULL, NULL},    {"repair", "search-index", NULL},
    };
    char *db = given_brain_with_enabled_plugin();

    add_expiring(db, "ephemeral");
    for (size_t i = 0; i < sizeof(k_commands) / sizeof(k_commands[0]); i++) {
        const char *args[3] = {k_commands[i][0], k_commands[i][1], k_commands[i][2]};
        size_t n = k_commands[i][1] == NULL ? 1U : 2U;
        CmdResult r;

        backdate(db, 1, k_past);
        r = run_remember(db, args, n, NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
        assert_marked(db, 1, true);
    }
    free(db);
}

TEST(a_command_that_fails_still_sweeps_after_it)
{
    char *db = given_brain_with_enabled_plugin();
    CmdResult r;

    add_expiring(db, "ephemeral");
    backdate(db, 1, k_past);
    r = RUN(db, "get", "999");
    ASSERT_EQ_INT(r.exit_code, 2);
    cmd_result_free(&r);
    assert_marked(db, 1, true);
    free(db);
}

TEST(a_command_that_opens_no_brain_does_not_sweep)
{
    char *db = given_brain_with_enabled_plugin();

    add_expiring(db, "ephemeral");
    backdate(db, 1, k_past);
    assert_ok(RUN(db, "--version"));
    assert_ok(RUN(db, "help"));
    assert_marked(db, 1, false);
    free(db);
}

TEST(read_only_open_skips_sweep)
{
    char *db = given_brain_with_enabled_plugin();
    CmdResult r;

    add_expiring(db, "ephemeral");
    backdate(db, 1, k_past);
    exec_sql(db, "PRAGMA user_version = 99;");
    r = RUN(db, "list");
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.err, "read-only");
    ASSERT_STR_NOT_CONTAINS(r.err, "expiry");
    cmd_result_free(&r);
    assert_marked(db, 1, false);
    assert_rows(db, "memory.created");
    r = run_remember(db, (const char *const[]){"--strict", "list"}, 2U, NULL);
    ASSERT_EQ_INT(r.exit_code, 4); /* the newer-brain warning, and nothing the sweep added */
    cmd_result_free(&r);
    assert_marked(db, 1, false);
    free(db);
}

TEST(a_sweep_that_fails_warns_and_leaves_the_exit_code_of_the_command)
{
    char *db = given_brain_with_enabled_plugin();
    CmdResult r;

    add_expiring(db, "ephemeral");
    backdate(db, 1, k_past);
    exec_sql(db, "CREATE TRIGGER no_expiry BEFORE UPDATE OF expired_at ON entries BEGIN SELECT "
                 "RAISE(ABORT, 'no'); END;");
    r = RUN(db, "list", "--expired");
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "ephemeral");
    ASSERT_STR_PREFIX(r.err, "remember: warning: ");
    cmd_result_free(&r);
    assert_marked(db, 1, false);
    assert_rows(db, "memory.created");
    r = run_remember(db, (const char *const[]){"--strict", "list", "--expired"}, 3U, NULL);
    ASSERT_EQ_INT(r.exit_code, 4);
    cmd_result_free(&r);
    r = run_remember(db, (const char *const[]){"--json", "list", "--expired"}, 3U, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "\"warnings\":[{\"code\":\"sweep_failed\"");
    cmd_result_free(&r);
    /* The next command retries: with the fault gone it sweeps. */
    exec_sql(db, "DROP TRIGGER no_expiry;");
    assert_ok(RUN(db, "list"));
    assert_marked(db, 1, true);
    assert_rows(db, "memory.created,memory.expired");
    free(db);
}

TEST(an_add_that_revives_an_expired_memory_clears_the_mark_and_the_next_expiry_emits_again)
{
    char *db = given_brain_with_enabled_plugin();

    assert_ok(RUN(db, "add", "ephemeral", "--key", "k:one", "--ttl", "1d"));
    backdate(db, 1, k_past);
    assert_ok(RUN(db, "tags"));
    assert_marked(db, 1, true);
    assert_ok(RUN(db, "add", "ephemeral", "--key", "k:one", "--ttl", "2d"));
    assert_marked(db, 1, false);
    backdate(db, 1, k_past_two);
    assert_ok(RUN(db, "tags"));
    assert_marked(db, 1, true);
    assert_query(db, "SELECT count(*) FROM outbox WHERE type = 'memory.expired';", "2");
    free(db);
}

TEST(restore_of_a_due_row_emits_restored_then_expired)
{
    char *db = given_brain_with_enabled_plugin();

    add_expiring(db, "ephemeral");
    assert_ok(RUN(db, "delete", "1"));
    /* The row was marked before it was deleted, and now carries a due time again. */
    exec_sql(db, "UPDATE entries SET expires_at = '2020-01-01T00:00:00.000Z', expired_at = "
                 "'2020-06-01T00:00:00.000Z' WHERE local_id = 1;");
    assert_ok(RUN(db, "update", "1", "--deleted", "--undelete"));
    assert_rows(db, "memory.created,memory.deleted,memory.restored,memory.expired");
    assert_query(db, "SELECT expired_at > '2021' FROM entries WHERE local_id = 1;", "1");
    assert_ok(RUN(db, "list"));
    assert_rows(db, "memory.created,memory.deleted,memory.restored,memory.expired");
    free(db);
}

/* ---- remember sweep -------------------------------------------------------------------------- */

TEST(the_sweep_command_marks_and_prints_how_many_and_a_second_run_prints_zero)
{
    char *db = given_brain_with_enabled_plugin();
    CmdResult r;

    add_expiring(db, "one");
    add_expiring(db, "two");
    add_expiring(db, "three");
    backdate(db, 1, k_past);
    backdate(db, 3, k_past);
    r = RUN(db, "brain", "sweep");
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.out, "2\n");
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
    assert_marked(db, 1, true);
    assert_marked(db, 2, false);
    assert_rows(db, "memory.created,memory.created,memory.created,memory.expired,memory.expired");
    /* `brain sweep` keeps cause.command "sweep" (decision:2026-11-18-sweep-under-brain). */
    assert_query(db,
                 "SELECT group_concat(json_extract(payload, '$.cause.command'), ',') FROM outbox "
                 "WHERE type = 'memory.expired';",
                 "sweep,sweep");
    r = RUN(db, "brain", "sweep");
    ASSERT_STREQ(r.out, "0\n");
    cmd_result_free(&r);
    r = RUN(db, "--json", "brain", "sweep");
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "\"type\":\"brain.sweep.result\",\"data\":{\"expired\":0}");
    cmd_result_free(&r);
    free(db);
}

TEST(there_is_no_top_level_sweep_command_only_brain_sweep)
{
    char *db = given_brain_with_enabled_plugin();
    CmdResult r = RUN(db, "sweep");

    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "sweep");
    cmd_result_free(&r);
    r = RUN(db, "--json", "brain", "sweep");
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"type\":\"brain.sweep.result\"");
    cmd_result_free(&r);
    free(db);
}

TEST(the_sweep_command_takes_no_argument_and_no_flag_but_all)
{
    char *db = given_brain_with_enabled_plugin();
    CmdResult r = RUN(db, "brain", "sweep", "now");

    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "remember: ");
    cmd_result_free(&r);
    r = RUN(db, "brain", "sweep", "--hooks");
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "unknown option '--hooks'");
    cmd_result_free(&r);
    r = RUN(db, "brain", "sweep", "--all", "--all");
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = RUN(db, "--json", "brain", "sweep", "now");
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.out, "\"fix\":\"Run 'remember brain --help' to see how to use it.\"");
    cmd_result_free(&r);
    free(db);
}

/* One brain with an enabled plugin and one due row (id 1), listed in brains.json. */
static char *given_listed_brain_with_a_due_row(void)
{
    char *db = given_brain_with_enabled_plugin();

    add_expiring(db, "ephemeral");
    backdate(db, 1, k_past);
    return db;
}

TEST(sweep_all_reads_brains_json_and_warns_missing)
{
    char *one = given_listed_brain_with_a_due_row();
    char *two = given_listed_brain_with_a_due_row();
    char *gone = given_listed_brain_with_a_due_row();
    char *unlisted = NULL;
    CmdResult r;

    /* A brain that was never opened here is not in brains.json, so --all never reaches it. */
    unlisted = make_temp_db_path();
    exec_sql(unlisted, "CREATE TABLE x(a);");
    ASSERT_EQ_INT(remove(gone), 0);
    r = RUN(one, "brain", "sweep", "--all");
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.out, "2\n");
    ASSERT_STR_PREFIX(r.err, "remember: warning: brain \"");
    ASSERT_STR_CONTAINS(r.err, gone);
    ASSERT_STR_CONTAINS(r.err, "is in the list but isn't on disk.");
    cmd_result_free(&r);
    assert_marked(one, 1, true);
    assert_marked(two, 1, true);
    assert_rows(one, "memory.created,memory.expired");
    assert_rows(two, "memory.created,memory.expired");
    r = RUN(one, "--json", "brain", "list");
    ASSERT_STR_CONTAINS(r.out, gone); /* the entry stays until prune */
    cmd_result_free(&r);
    r = RUN(one, "--json", "brain", "sweep", "--all");
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"type\":\"brain.sweep.result\",\"data\":{\"expired\":0,\"brains\":[");
    ASSERT_STR_CONTAINS(r.out, "\"warnings\":[{\"code\":\"brain_missing\"");
    {
        /* the missing brain is listed with 0, in brains.json order */
        char want[512];

        /* brains.json holds real paths: /private/tmp/... for a /tmp/... temp folder on macOS */
        (void)snprintf(want, sizeof(want), "%s\",\"expired\":0}", strstr(gone, "/remember-test-"));
        ASSERT_STR_CONTAINS(r.out, want);
    }
    cmd_result_free(&r);
    r = RUN(one, "--strict", "brain", "sweep", "--all");
    ASSERT_EQ_INT(r.exit_code, 4);
    cmd_result_free(&r);
    free(unlisted);
    free(gone);
    free(two);
    free(one);
}

TEST(sweep_all_skips_a_newer_brain_and_a_brain_that_cannot_be_swept_warns)
{
    char *one = given_listed_brain_with_a_due_row();
    char *newer = given_listed_brain_with_a_due_row();
    char *broken = given_listed_brain_with_a_due_row();
    CmdResult r;

    exec_sql(newer, "PRAGMA user_version = 99;");
    exec_sql(broken, "CREATE TRIGGER no_expiry BEFORE UPDATE OF expired_at ON entries BEGIN SELECT "
                     "RAISE(ABORT, 'no'); END;");
    r = RUN(one, "brain", "sweep", "--all");
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.out, "1\n");
    ASSERT_STR_CONTAINS(r.err, "warning: ");
    ASSERT_STR_CONTAINS(r.err, broken);
    ASSERT_STR_NOT_CONTAINS(r.err, newer);
    cmd_result_free(&r);
    assert_marked(one, 1, true);
    assert_marked(newer, 1, false);
    assert_marked(broken, 1, false);
    free(broken);
    free(newer);
    free(one);
}

/* ---- remember purge LOCATOR -------------------------------------------------------------------
 */

TEST(purge_live_exits_3_wrong_bin)
{
    char *db = make_temp_db_path();
    CmdResult r;
    char *uuid = NULL;

    assert_ok(RUN(db, "add", "keyed", "--key", "k:one"));
    assert_ok(RUN(db, "add", "keyless"));
    r = RUN(db, "purge", "--key", "k:one");
    ASSERT_EQ_INT(r.exit_code, 3);
    ASSERT_STREQ(r.err, "remember: \"k:one\" isn't deleted, so it can't be purged. Run 'remember "
                        "delete k:one' first.\n");
    cmd_result_free(&r);
    /* A row with no key is named by its id. */
    uuid = harness_sqlite_query_line(db, "SELECT id FROM entries WHERE local_id = 2;");
    ASSERT_TRUE(uuid != NULL);
    r = RUN(db, "purge", "2");
    ASSERT_EQ_INT(r.exit_code, 3);
    ASSERT_STR_CONTAINS(r.err, uuid != NULL ? uuid : "");
    ASSERT_STR_CONTAINS(r.err, " isn't deleted, so it can't be purged. Run 'remember delete ");
    cmd_result_free(&r);
    r = RUN(db, "--json", "purge", "--key", "k:one");
    ASSERT_EQ_INT(r.exit_code, 3);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "\"error\":{\"code\":\"wrong_bin\",\"message\":\"\\\"k:one\\\" "
                               "isn't deleted, so it can't be purged.\",\"fix\":\"Run 'remember "
                               "delete k:one' first.\"");
    cmd_result_free(&r);
    assert_query(db, "SELECT count(*) FROM entries;", "2");
    assert_query(db, "SELECT count(*) FROM tombstones;", "0");
    free(uuid);
    free(db);
}

TEST(purge_deleted_writes_one_tombstone)
{
    char *db = given_brain_with_enabled_plugin();
    CmdResult r;
    char *uuid = NULL;
    char *seq = NULL;

    assert_ok(RUN(db, "add", "to the trash", "--key", "k:one"));
    assert_ok(RUN(db, "add", "stays"));
    assert_ok(RUN(db, "delete", "1"));
    uuid = harness_sqlite_query_line(db, "SELECT id FROM entries WHERE local_id = 1;");
    seq = harness_sqlite_query_line(db, "SELECT change_seq + 1 FROM brain_meta;");
    ASSERT_TRUE(uuid != NULL && seq != NULL);
    r = RUN(db, "purge", "1");
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
    assert_query(db, "SELECT count(*) FROM entries;", "1");
    assert_query(db, "SELECT count(*) FROM entries_fts WHERE entries_fts MATCH 'trash';", "0");
    {
        char sql[256];

        (void)snprintf(
            sql, sizeof(sql),
            "SELECT count(*) || ' ' || (id = '%s') || ' ' || (change_seq = %s) || ' ' || "
            "(change_seq = (SELECT change_seq FROM brain_meta)) FROM tombstones;",
            uuid, seq);
        assert_query(db, sql, "1 1 1 1");
    }
    free(uuid);
    free(seq);
    free(db);
}

TEST(purge_names_the_row_by_id_key_or_uuid_and_a_missing_row_is_not_found)
{
    char *db = make_temp_db_path();
    CmdResult r;

    assert_ok(RUN(db, "add", "one", "--key", "k:one"));
    assert_ok(RUN(db, "add", "two"));
    assert_ok(RUN(db, "delete", "1"));
    assert_ok(RUN(db, "delete", "2"));
    r = RUN(db, "purge", "--key", "k:one");
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    {
        char *uuid = harness_sqlite_query_line(db, "SELECT id FROM entries WHERE local_id = 2;");

        ASSERT_TRUE(uuid != NULL);
        r = RUN(db, "purge", uuid != NULL ? uuid : "");
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
        free(uuid);
    }
    assert_query(db, "SELECT count(*) FROM entries;", "0");
    assert_query(db, "SELECT count(*) FROM tombstones;", "2");
    r = RUN(db, "purge", "1");
    ASSERT_EQ_INT(r.exit_code, 2);
    cmd_result_free(&r);
    r = RUN(db, "purge", "--key", "gone:key");
    ASSERT_EQ_INT(r.exit_code, 2);
    cmd_result_free(&r);
    free(db);
}

/* Runs purge with REMEMBER_TEST_PURGE_RACE set: the test CLI runs race_sql on a second connection
   between purge's read of the row and its write, as another process could. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the brain, the SQL, then the key
static CmdResult purge_with_race(const char *db, const char *race_sql, const char *key)
{
    CmdResult r;

    ASSERT_EQ_INT(setenv("REMEMBER_TEST_PURGE_RACE", race_sql, 1), 0);
    r = RUN(db, "purge", "--key", key);
    ASSERT_EQ_INT(unsetenv("REMEMBER_TEST_PURGE_RACE"), 0);
    return r;
}

TEST(purge_never_wipes_another_memory_that_took_the_local_id_of_the_one_it_read)
{
    char *db = make_temp_db_path();
    CmdResult r;

    assert_ok(RUN(db, "add", "keep"));
    assert_ok(RUN(db, "add", "old", "--key", "k:old"));
    assert_ok(RUN(db, "add", "victim", "--key", "k:victim"));
    assert_ok(RUN(db, "delete", "2"));
    assert_ok(RUN(db, "delete", "3"));
    /* Another process purges k:old, and the Trash row k:victim now holds its local id 2 (SQLite
       hands the highest rowid out again once it is free). */
    r = purge_with_race(db,
                        "DELETE FROM entries WHERE local_id = 2; "
                        "UPDATE entries SET local_id = 2 WHERE local_id = 3;",
                        "k:old");
    ASSERT_EQ_INT(r.exit_code, 2);
    cmd_result_free(&r);
    assert_query(db, "SELECT count(*) FROM entries WHERE key = 'k:victim';", "1");
    free(db);
}

TEST(purge_of_a_row_restored_since_it_was_read_is_the_wrong_bin_line_and_keeps_it)
{
    char *db = make_temp_db_path();
    CmdResult r;

    assert_ok(RUN(db, "add", "one", "--key", "k:one"));
    assert_ok(RUN(db, "delete", "1"));
    r = purge_with_race(db, "UPDATE entries SET deleted_at = NULL WHERE local_id = 1;", "k:one");
    ASSERT_EQ_INT(r.exit_code, 3);
    ASSERT_STREQ(r.err, "remember: \"k:one\" isn't deleted, so it can't be purged. Run 'remember "
                        "delete k:one' first.\n");
    cmd_result_free(&r);
    assert_query(db, "SELECT count(*) FROM entries;", "1");
    free(db);
}

TEST(purge_of_a_row_that_moved_from_the_trash_to_expired_since_it_was_read_purges_it)
{
    char *db = make_temp_db_path();
    CmdResult r;

    assert_ok(RUN(db, "add", "one", "--key", "k:one"));
    assert_ok(RUN(db, "delete", "1"));
    r = purge_with_race(db,
                        "UPDATE entries SET deleted_at = NULL, expires_at = "
                        "'2020-01-01T00:00:00.000Z' WHERE local_id = 1;",
                        "k:one");
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
    assert_query(db, "SELECT count(*) FROM entries;", "0");
    free(db);
}

TEST(purge_of_an_expired_row_the_sweep_marked_emits_expired_then_purged)
{
    char *db = given_brain_with_enabled_plugin();

    add_expiring(db, "marked");
    backdate(db, 1, k_past);
    assert_ok(RUN(db, "list"));
    assert_rows(db, "memory.created,memory.expired");
    assert_ok(RUN(db, "purge", "1"));
    assert_rows(db, "memory.created,memory.expired,memory.purged");
    assert_query(db, "SELECT count(*) FROM entries;", "0");
    free(db);
}

TEST(purge_of_an_unmarked_expired_row_emits_only_purged)
{
    char *db = given_brain_with_enabled_plugin();

    add_expiring(db, "unmarked");
    backdate(db, 1, k_past); /* no sweep has seen it */
    assert_ok(RUN(db, "purge", "1"));
    assert_rows(db, "memory.created,memory.purged");
    assert_query(db, "SELECT count(*) FROM entries;", "0");
    assert_query(db, "SELECT count(*) FROM tombstones;", "1");
    free(db);
}

TEST(purge_event_is_one_purged_with_the_last_state_of_the_row)
{
    char *db = given_brain_with_enabled_plugin();

    assert_ok(RUN(db, "add", "last words", "--key", "k:one", "--tag", "t1"));
    assert_ok(RUN(db, "delete", "1"));
    exec_sql(db, "DELETE FROM outbox;");
    assert_ok(RUN(db, "purge", "1"));
    assert_rows(db, "memory.purged");
    assert_query(db,
                 "SELECT json_extract(payload, '$.memory.body') || ' ' || json_extract(payload, "
                 "'$.cause.command') || ' ' || json_extract(payload, '$.memory.bin') FROM outbox;",
                 "last words purge deleted");
    free(db);
}

TEST(purge_takes_one_locator_and_no_bin_flag)
{
    static const char *const k_bad[][3] = {
        {"purge", "--expired", NULL}, {"purge", "--deleted", NULL}, {"purge", "--trash", NULL},
        {"purge", "1", "--expired"},  {"purge", "1", "--deleted"},  {"purge", "1", "--trash"},
    };
    char *db = make_temp_db_path();
    CmdResult r;

    assert_ok(RUN(db, "add", "body"));
    assert_ok(RUN(db, "delete", "1"));
    for (size_t i = 0; i < sizeof(k_bad) / sizeof(k_bad[0]); i++) {
        const char *args[3] = {k_bad[i][0], k_bad[i][1], k_bad[i][2]};

        r = run_remember(db, args, k_bad[i][2] == NULL ? 2U : 3U, NULL);
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STR_CONTAINS(r.err, "unknown option");
        cmd_result_free(&r);
    }
    r = RUN(db, "purge");
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "missing id or --key");
    cmd_result_free(&r);
    r = RUN(db, "purge", "1", "2");
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "too many arguments");
    cmd_result_free(&r);
    r = RUN(db, "purge", "1", "--key", "k");
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    assert_query(db, "SELECT count(*) FROM entries;", "1");
    free(db);
}

TEST(purge_json_reports_one_purged_entry)
{
    char *db = make_temp_db_path();
    CmdResult r;

    assert_ok(RUN(db, "add", "body"));
    assert_ok(RUN(db, "delete", "1"));
    r = RUN(db, "--json", "purge", "1");
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"type\":\"memory.purge.result\",\"data\":{\"action\":\"purged\","
                               "\"count\":1,\"entries\":[{");
    cmd_result_free(&r);
    free(db);
}

/* ---- delete --expired -------------------------------------------------------------------------
 */

TEST(delete_deleted_unknown)
{
    char *db = make_temp_db_path();
    CmdResult r;

    assert_ok(RUN(db, "add", "body"));
    assert_ok(RUN(db, "delete", "1"));
    r = RUN(db, "delete", "--deleted");
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "remember: unknown option '--deleted'\n");
    cmd_result_free(&r);
    r = RUN(db, "delete", "1", "--deleted");
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "unknown option '--deleted'");
    cmd_result_free(&r);
    assert_query(db, "SELECT count(*) FROM entries;", "1");
    free(db);
}

TEST(delete_expired_moves_all_due_emits_deleted_only)
{
    char *db = given_brain_with_enabled_plugin();
    CmdResult r;

    add_expiring(db, "due one");
    add_expiring(db, "due two");
    add_expiring(db, "later");
    assert_ok(RUN(db, "add", "no expiry"));
    assert_ok(RUN(db, "add", "already in the trash"));
    assert_ok(RUN(db, "delete", "5"));
    backdate(db, 1, k_past);
    backdate(db, 2, k_past_two);
    /* a row already in the Trash can carry a due time (hand-edited or synced): not moved again */
    exec_sql(db, "UPDATE entries SET expires_at = '2020-01-01T00:00:00.000Z' WHERE local_id = 5;");
    exec_sql(db, "DELETE FROM outbox;");
    r = RUN(db, "delete", "--expired");
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.out, "2\n");
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
    /* deleted only: no expired for rows that moved to Deleted, none from the sweep after. */
    assert_rows(db, "memory.deleted,memory.deleted");
    assert_query(db, "SELECT count(DISTINCT json_extract(payload, '$.batch_id')) FROM outbox;",
                 "1");
    assert_query(db,
                 "SELECT group_concat(local_id) FROM (SELECT local_id FROM entries WHERE "
                 "deleted_at IS NOT NULL ORDER BY local_id);",
                 "1,2,5");
    assert_query(db, "SELECT count(*) FROM entries WHERE deleted_at IS NULL;", "2");
    assert_query(db, "SELECT count(*) FROM entries;", "5");
    r = RUN(db, "--json", "delete", "--expired");
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"type\":\"memory.delete.result\",\"data\":{\"action\":\"deleted\","
                               "\"count\":0,\"entries\":[]}");
    cmd_result_free(&r);
    free(db);
}

TEST(delete_expired_has_no_cap)
{
    char *db = given_brain_with_enabled_plugin();
    CmdResult r;

    assert_ok(RUN(db, "add", "seed"));
    exec_sql(db, "WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 1200) "
                 "INSERT INTO entries(id, body, body_hash, created_by, updated_by, created_at, "
                 "updated_at, expires_at, version_vector, change_seq) SELECT "
                 "printf('01a14521-0000-7000-8000-%012x', i), 'due ' || i, printf('%064x', i), "
                 "'{\"kind\":\"human\",\"id\":null}', '{\"kind\":\"human\",\"id\":null}', "
                 "'2026-01-01T00:00:00.000Z', '2026-01-01T00:00:00.000Z', "
                 "'2020-01-01T00:00:00.000Z', '{\"01a0d90a-e71c-7a0a-bb2c-3b377482fa51\":1}', 0 "
                 "FROM n;");
    exec_sql(db, "DELETE FROM outbox;");
    r = RUN(db, "delete", "--expired");
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.out, "1200\n");
    cmd_result_free(&r);
    assert_query(db, "SELECT count(*) FROM entries WHERE deleted_at IS NULL;", "1");
    assert_query(db, "SELECT count(*) FROM outbox WHERE type = 'memory.deleted';", "1200");
    assert_query(db, "SELECT count(*) FROM outbox WHERE type <> 'memory.deleted';", "0");
    free(db);
}

TEST(delete_expired_takes_no_locator)
{
    char *db = make_temp_db_path();
    CmdResult r;

    add_expiring(db, "one");
    backdate(db, 1, k_past);
    r = RUN(db, "delete", "1", "--expired");
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "remember: delete --expired takes no id or --key\n");
    cmd_result_free(&r);
    r = RUN(db, "delete", "--expired", "--key", "k");
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = RUN(db, "delete", "--expired", "--trash");
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    assert_query(db, "SELECT count(*) FROM entries WHERE deleted_at IS NULL;", "1");
    free(db);
}

TEST(plain_delete_of_an_expired_row_moves_it_to_the_trash_as_before)
{
    char *db = given_brain_with_enabled_plugin();

    add_expiring(db, "one");
    backdate(db, 1, k_past);
    assert_ok(RUN(db, "delete", "1"));
    assert_query(db, "SELECT deleted_at IS NOT NULL FROM entries WHERE local_id = 1;", "1");
    assert_rows(db, "memory.created,memory.deleted");
    free(db);
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

#endif /* REMEMBER_PLUGINS_DIAG_TEST */

void register_sweep_tests(void)
{
#ifdef REMEMBER_PLUGINS_DIAG_TEST
    RUN_TEST(sweep_emits_once_and_revival_clears);
    RUN_TEST(an_expiry_cleared_or_moved_later_clears_the_marker_and_an_earlier_one_does_not);
    RUN_TEST(the_sweep_changes_neither_updated_at_nor_the_vector_nor_either_change_seq);
    RUN_TEST(sweep_no_plugins_no_row);
    RUN_TEST(the_sweep_leaves_live_deleted_and_unexpiring_rows_alone);
    RUN_TEST(one_sweep_is_one_batch_apart_from_the_command_that_ran_before_it);
    RUN_TEST(
        a_memory_added_already_due_is_created_by_the_command_and_expired_by_the_sweep_after_it);
    RUN_TEST(every_command_that_opens_the_brain_read_write_sweeps_after_it_commits);
    RUN_TEST(a_command_that_fails_still_sweeps_after_it);
    RUN_TEST(a_command_that_opens_no_brain_does_not_sweep);
    RUN_TEST(read_only_open_skips_sweep);
    RUN_TEST(a_sweep_that_fails_warns_and_leaves_the_exit_code_of_the_command);
    RUN_TEST(an_add_that_revives_an_expired_memory_clears_the_mark_and_the_next_expiry_emits_again);
    RUN_TEST(restore_of_a_due_row_emits_restored_then_expired);
    RUN_TEST(the_sweep_command_marks_and_prints_how_many_and_a_second_run_prints_zero);
    RUN_TEST(there_is_no_top_level_sweep_command_only_brain_sweep);
    RUN_TEST(the_sweep_command_takes_no_argument_and_no_flag_but_all);
    RUN_TEST(sweep_all_reads_brains_json_and_warns_missing);
    RUN_TEST(sweep_all_skips_a_newer_brain_and_a_brain_that_cannot_be_swept_warns);
    RUN_TEST(purge_live_exits_3_wrong_bin);
    RUN_TEST(purge_deleted_writes_one_tombstone);
    RUN_TEST(purge_names_the_row_by_id_key_or_uuid_and_a_missing_row_is_not_found);
    RUN_TEST(purge_never_wipes_another_memory_that_took_the_local_id_of_the_one_it_read);
    RUN_TEST(purge_of_a_row_restored_since_it_was_read_is_the_wrong_bin_line_and_keeps_it);
    RUN_TEST(purge_of_a_row_that_moved_from_the_trash_to_expired_since_it_was_read_purges_it);
    RUN_TEST(purge_of_an_expired_row_the_sweep_marked_emits_expired_then_purged);
    RUN_TEST(purge_of_an_unmarked_expired_row_emits_only_purged);
    RUN_TEST(purge_event_is_one_purged_with_the_last_state_of_the_row);
    RUN_TEST(purge_takes_one_locator_and_no_bin_flag);
    RUN_TEST(purge_json_reports_one_purged_entry);
    RUN_TEST(delete_deleted_unknown);
    RUN_TEST(delete_expired_moves_all_due_emits_deleted_only);
    RUN_TEST(delete_expired_has_no_cap);
    RUN_TEST(delete_expired_takes_no_locator);
    RUN_TEST(plain_delete_of_an_expired_row_moves_it_to_the_trash_as_before);
#endif
}
