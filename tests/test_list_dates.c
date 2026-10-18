#include "commands_common.h"
#include "harness.h"
#include "register.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Single-threaded tests. getenv, setenv, and unsetenv have no portable reentrant form. */
// NOLINTBEGIN(concurrency-mt-unsafe)

/*
 * Plan 17 stage 1. parse_filter_ts accepts the three filter forms and
 * writes the canonical UTC stamp. It does not print.
 */

static int use_tz(const char *zone, char **saved, int *had)
{
    const char *old = getenv("TZ");

    *had = old != NULL;
    *saved = NULL;
    if (*had) {
        *saved = strdup(old);
        if (*saved == NULL) {
            return -1;
        }
    }
    if (setenv("TZ", zone, 1) != 0) {
        free(*saved);
        *saved = NULL;
        *had = 0;
        return -1;
    }
    tzset();
    return 0;
}

static void restore_tz(char *saved, int had)
{
    if (had && saved != NULL) {
        (void)setenv("TZ", saved, 1);
    } else if (!had) {
        (void)unsetenv("TZ");
    }
    free(saved);
    tzset();
}

TEST(filter_ts_canonical_is_copied)
{
    char out[ISO_TS_BUFSIZE];

    out[0] = '\0';
    ASSERT_EQ_INT(parse_filter_ts("2026-10-12T10:00:00.123Z", out), 0);
    ASSERT_STREQ(out, "2026-10-12T10:00:00.123Z");
}

TEST(filter_ts_seconds_become_millis)
{
    char out[ISO_TS_BUFSIZE];

    out[0] = '\0';
    ASSERT_EQ_INT(parse_filter_ts("2026-10-12T10:00:00Z", out), 0);
    ASSERT_STREQ(out, "2026-10-12T10:00:00.000Z");
}

TEST(filter_ts_date_is_jerusalem_local_midnight)
{
    char *saved = NULL;
    int had = 0;
    char out[ISO_TS_BUFSIZE];

    ASSERT_EQ_INT(use_tz("Asia/Jerusalem", &saved, &had), 0);
    out[0] = '\0';
    ASSERT_EQ_INT(parse_filter_ts("2026-06-15", out), 0);
    ASSERT_STREQ(out, "2026-06-14T21:00:00.000Z"); /* IDT, UTC+3 */
    restore_tz(saved, had);
}

TEST(filter_ts_rejects_impossible_month)
{
    char out[ISO_TS_BUFSIZE];

    out[0] = 'x';
    out[1] = '\0';
    ASSERT_EQ_INT(parse_filter_ts("2026-13-01T00:00:00.000Z", out), -1);
    ASSERT_STREQ(out, "x");
}

TEST(filter_ts_rejects_february_30)
{
    char out[ISO_TS_BUFSIZE];

    out[0] = 'x';
    out[1] = '\0';
    ASSERT_EQ_INT(parse_filter_ts("2026-02-30", out), -1);
    ASSERT_STREQ(out, "x");
}

static void assert_ts_rejected(const char *raw)
{
    char out[ISO_TS_BUFSIZE];

    out[0] = 'x';
    out[1] = '\0';
    ASSERT_EQ_INT(parse_filter_ts(raw, out), -1);
    ASSERT_STREQ(out, "x");
}

TEST(filter_ts_date_is_los_angeles_local_midnight)
{
    char *saved = NULL;
    int had = 0;
    char out[ISO_TS_BUFSIZE];

    ASSERT_EQ_INT(use_tz("America/Los_Angeles", &saved, &had), 0);
    out[0] = '\0';
    ASSERT_EQ_INT(parse_filter_ts("2026-06-15", out), 0);
    ASSERT_STREQ(out, "2026-06-15T07:00:00.000Z"); /* PDT, UTC-7 */
    restore_tz(saved, had);
}

TEST(filter_ts_jerusalem_dst_end_is_local_midnight)
{
    char *saved = NULL;
    int had = 0;
    char out[ISO_TS_BUFSIZE];

    ASSERT_EQ_INT(use_tz("Asia/Jerusalem", &saved, &had), 0);
    out[0] = '\0';
    ASSERT_EQ_INT(parse_filter_ts("2026-10-25", out), 0);
    ASSERT_STREQ(out, "2026-10-24T21:00:00.000Z"); /* DST ends 02:00; still UTC+3 */
    restore_tz(saved, had);
}

/* Havana skips 00:00-00:59 on 2026-03-08. The day starts at 01:00 local (UTC-4). */
TEST(filter_ts_skipped_midnight_is_the_first_local_instant)
{
    char *saved = NULL;
    int had = 0;
    char out[ISO_TS_BUFSIZE];

    ASSERT_EQ_INT(use_tz("America/Havana", &saved, &had), 0);
    out[0] = '\0';
    ASSERT_EQ_INT(parse_filter_ts("2026-03-08", out), 0);
    ASSERT_STREQ(out, "2026-03-08T05:00:00.000Z");
    restore_tz(saved, had);
}

/* Samoa skipped 2011-12-30 entirely. That day has no local start. */
TEST(filter_ts_rejects_a_day_the_zone_skipped)
{
    char *saved = NULL;
    int had = 0;

    ASSERT_EQ_INT(use_tz("Pacific/Apia", &saved, &had), 0);
    assert_ts_rejected("2011-12-30");
    restore_tz(saved, had);
}

/* decision:plan17-date-floor-1900: macOS mktime cannot convert these, so every
   platform rejects them. Before the floor, macOS returned 1969-12-31T23:59:59.000Z. */
TEST(filter_ts_rejects_a_date_before_1900)
{
    assert_ts_rejected("1899-12-31");
    assert_ts_rejected("0001-01-01");
}

TEST(filter_ts_date_1900_01_01_is_accepted)
{
    char *saved = NULL;
    int had = 0;
    char out[ISO_TS_BUFSIZE];

    ASSERT_EQ_INT(use_tz("UTC", &saved, &had), 0);
    out[0] = '\0';
    ASSERT_EQ_INT(parse_filter_ts("1900-01-01", out), 0);
    ASSERT_STREQ(out, "1900-01-01T00:00:00.000Z");
    restore_tz(saved, had);
}

TEST(filter_ts_leap_day_utc_is_that_midnight)
{
    char *saved = NULL;
    int had = 0;
    char out[ISO_TS_BUFSIZE];

    ASSERT_EQ_INT(use_tz("UTC", &saved, &had), 0);
    out[0] = '\0';
    ASSERT_EQ_INT(parse_filter_ts("2024-02-29", out), 0);
    ASSERT_STREQ(out, "2024-02-29T00:00:00.000Z");
    out[0] = '\0';
    ASSERT_EQ_INT(parse_filter_ts("2000-02-29", out), 0);
    ASSERT_STREQ(out, "2000-02-29T00:00:00.000Z");
    restore_tz(saved, had);
}

TEST(filter_ts_rejects_non_leap_february_29)
{
    assert_ts_rejected("2026-02-29");
    assert_ts_rejected("1900-02-29");
    assert_ts_rejected("2026-02-29T00:00:00.000Z");
    assert_ts_rejected("2026-02-29T00:00:00Z");
}

TEST(filter_ts_rejects_malformed_timestamps)
{
    assert_ts_rejected("2026-1-01");
    assert_ts_rejected("2026-10-12T10:00Z");
    assert_ts_rejected("2026-10-12T10:00:00.1Z");
    assert_ts_rejected("2026-10-12T10:00:00.0000Z");
    assert_ts_rejected("2026-10-12T10:00:00+02:00");
    assert_ts_rejected("2026-13-01");
    assert_ts_rejected("");
    assert_ts_rejected("now");
    assert_ts_rejected("2026-02-30T00:00:00.000Z");
    assert_ts_rejected("2026-02-30T00:00:00Z");
    assert_ts_rejected("2026-10-12T24:00:00.000Z");
    assert_ts_rejected("2026-10-12T10:60:00.000Z");
    assert_ts_rejected("2026-10-12T10:00:60.000Z");
    assert_ts_rejected("2026-10-12T10:00:60Z");
    assert_ts_rejected("2026-04-31");
    assert_ts_rejected("2026-01-32");
    assert_ts_rejected("2026-10-12T10:00:00.000z");
}

TEST(filter_ts_rejects_null_input)
{
    char out[ISO_TS_BUFSIZE];

    out[0] = 'x';
    out[1] = '\0';
    ASSERT_EQ_INT(parse_filter_ts(NULL, out), -1);
    ASSERT_STREQ(out, "x");
}

TEST(filter_ts_rejects_null_output)
{
    ASSERT_EQ_INT(parse_filter_ts("2026-10-12T10:00:00.000Z", NULL), -1);
}

/* Plan 17 stage 3. The CLI passes canonical bounds and a sort to the store. */

enum { STAMP_SQL_BUF = 192, STAMP_WANT_BUF = 64 };

/* Pin both timestamps on an existing id. Returns 0, or -1. */
static int stamp_id(const char *db, long id, const char *created, const char *updated)
{
    char sql[STAMP_SQL_BUF];
    char want[STAMP_WANT_BUF];
    char *got = NULL;
    int stamped = 0;

    if (snprintf(sql, sizeof(sql),
                 "UPDATE entries SET created_at='%s', updated_at='%s' WHERE id=%ld;", created,
                 updated, id) < 0 ||
        snprintf(want, sizeof(want), "%s %s", created, updated) < 0) {
        return -1;
    }
    free(harness_sqlite_query_line(db, sql));
    if (snprintf(sql, sizeof(sql),
                 "SELECT created_at || ' ' || updated_at FROM entries WHERE id=%ld;", id) < 0) {
        return -1;
    }
    got = harness_sqlite_query_line(db, sql);
    stamped = got != NULL && strcmp(got, want) == 0;
    ASSERT_STREQ(got, want);
    free(got);
    if (!stamped) {
        return -1;
    }
    return 0;
}

/* Add one memory and pin both timestamps. Returns the id, or -1. */
/* body, created, and updated are adjacent timestamps and text on purpose. */
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
static long given_stamped(const char *db, const char *body, const char *created,
                          const char *updated)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    const char *add[] = {"add", body};
    CmdResult r;
    long id = 0;

    r = run_remember(db, add, sizeof(add) / sizeof(add[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    if (r.exit_code != 0) {
        cmd_result_free(&r);
        return -1;
    }
    id = parse_id_stdout(r.out);
    cmd_result_free(&r);
    ASSERT_TRUE(id > 0);
    if (id <= 0 || stamp_id(db, id, created, updated) != 0) {
        return -1;
    }
    return id;
}

TEST(list_sort_created_keeps_creation_order)
{
    char *db = make_temp_db_path();
    const char *created_sort[] = {"list", "--sort", "created"};
    CmdResult r;
    long oldest = 0;
    long middle = 0;
    long newest = 0;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    oldest = given_stamped(db, "oldest", "2026-01-01T00:00:00.000Z", "2026-04-01T00:00:00.000Z");
    middle = given_stamped(db, "middle", "2026-02-01T00:00:00.000Z", "2026-02-01T00:00:00.000Z");
    newest = given_stamped(db, "newest", "2026-03-01T00:00:00.000Z", "2026-03-01T00:00:00.000Z");
    ASSERT_EQ_INT(oldest, 1);
    ASSERT_EQ_INT(middle, 2);
    ASSERT_EQ_INT(newest, 3);
    if (newest != 3) {
        free(db);
        return;
    }
    r = run_remember(db, created_sort, sizeof(created_sort) / sizeof(created_sort[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, "3 |  |  | newest | 2026-03-01T00:00:00.000Z | \n"
                        "2 |  |  | middle | 2026-02-01T00:00:00.000Z | \n"
                        "1 |  |  | oldest | 2026-04-01T00:00:00.000Z | \n");
    cmd_result_free(&r);
    free(db);
}

TEST(list_sort_updated_puts_the_edit_first)
{
    char *db = make_temp_db_path();
    const char *updated[] = {"list", "--sort", "updated"};
    const char *plain[] = {"list"};
    const char *want = "1 |  |  | oldest | 2026-04-01T00:00:00.000Z | \n"
                       "3 |  |  | newest | 2026-03-01T00:00:00.000Z | \n"
                       "2 |  |  | middle | 2026-02-01T00:00:00.000Z | \n";
    CmdResult r;
    long oldest = 0;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    oldest = given_stamped(db, "oldest", "2026-01-01T00:00:00.000Z", "2026-04-01T00:00:00.000Z");
    (void)given_stamped(db, "middle", "2026-02-01T00:00:00.000Z", "2026-02-01T00:00:00.000Z");
    (void)given_stamped(db, "newest", "2026-03-01T00:00:00.000Z", "2026-03-01T00:00:00.000Z");
    ASSERT_EQ_INT(oldest, 1);
    if (oldest != 1) {
        free(db);
        return;
    }
    r = run_remember(db, plain, sizeof(plain) / sizeof(plain[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.out, want);
    cmd_result_free(&r);
    r = run_remember(db, updated, sizeof(updated) / sizeof(updated[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, want);
    cmd_result_free(&r);
    free(db);
}

/* The database path and the rejected token are different sentences. */
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
static void assert_sort_rejected(const char *db, const char *token)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    const char *args[] = {"list", "--sort", token};
    CmdResult r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);

    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "remember: invalid --sort (use updated or created)\n");
    ASSERT_STREQ(r.out, "");
    cmd_result_free(&r);
}

TEST(list_sort_rejects_a_bad_value)
{
    char *db = make_temp_db_path();
    const char *json[] = {"--json-errors", "list", "--sort", "bogus"};
    const char *huge = "uuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuu";
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    assert_sort_rejected(db, "bogus");
    assert_sort_rejected(db, "Created");
    assert_sort_rejected(db, "");
    assert_sort_rejected(db, " created");
    assert_sort_rejected(db, "created ");
    assert_sort_rejected(db, "-1");
    assert_sort_rejected(db, huge);
    r = run_remember(db, json, sizeof(json) / sizeof(json[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, "{\"version\":1,\"error\":{\"code\":\"usage\",\"message\":\"invalid --sort "
                        "(use updated or created)\"}}\n");
    cmd_result_free(&r);
    free(db);
}

TEST(list_sort_rejects_a_duplicate)
{
    char *db = make_temp_db_path();
    const char *args[] = {"list", "--sort", "created", "--sort", "updated"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "remember: duplicate --sort\n");
    ASSERT_STREQ(r.out, "");
    cmd_result_free(&r);
    free(db);
}

TEST(list_sort_requires_a_value)
{
    char *db = make_temp_db_path();
    const char *args[] = {"list", "--sort"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "remember: missing value for --sort\n");
    ASSERT_STREQ(r.out, "");
    cmd_result_free(&r);
    free(db);
}

TEST(list_since_is_inclusive_and_until_is_exclusive)
{
    char *db = make_temp_db_path();
    const char *args[] = {"list", "--since", "2026-06-15T00:00:00.000Z", "--until",
                          "2026-06-16T00:00:00.000Z"};
    CmdResult r;
    long at_since = 0;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    (void)given_stamped(db, "before", "2026-06-14T23:59:59.999Z", "2026-06-14T23:59:59.999Z");
    at_since =
        given_stamped(db, "at-since", "2026-06-15T00:00:00.000Z", "2026-06-15T00:00:00.000Z");
    (void)given_stamped(db, "at-until", "2026-06-16T00:00:00.000Z", "2026-06-16T00:00:00.000Z");
    ASSERT_EQ_INT(at_since, 2);
    if (at_since != 2) {
        free(db);
        return;
    }
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, "2 |  |  | at-since | 2026-06-15T00:00:00.000Z | \n");
    cmd_result_free(&r);
    free(db);
}

TEST(list_equal_bounds_are_empty)
{
    char *db = make_temp_db_path();
    const char *args[] = {"--json",  "list",
                          "--since", "2026-06-15T00:00:00.000Z",
                          "--until", "2026-06-15T00:00:00.000Z"};
    CmdResult r;
    long edge = 0;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    edge = given_stamped(db, "edge", "2026-06-15T00:00:00.000Z", "2026-06-15T00:00:00.000Z");
    ASSERT_EQ_INT(edge, 1);
    if (edge != 1) {
        free(db);
        return;
    }
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, "{\"version\":1,\"offset\":0,\"limit\":20,\"count\":0,\"total\":0,"
                        "\"entries\":[]}\n");
    cmd_result_free(&r);
    free(db);
}

/* zone, the two instants, and the expected line are adjacent on purpose. */
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
static void assert_date_since(const char *zone, const char *included, const char *excluded,
                              const char *want)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    char *db = make_temp_db_path();
    char *saved = NULL;
    int had = 0;
    const char *args[] = {"list", "--since", "2026-06-15"};
    CmdResult r;
    long id = 0;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    (void)given_stamped(db, "too-early", excluded, excluded);
    id = given_stamped(db, "on-day", included, included);
    ASSERT_EQ_INT(id, 2);
    if (id != 2) {
        free(db);
        return;
    }
    ASSERT_EQ_INT(use_tz(zone, &saved, &had), 0);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    restore_tz(saved, had);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, want);
    cmd_result_free(&r);
    free(db);
}

TEST(list_date_since_is_jerusalem_local_midnight)
{
    assert_date_since("Asia/Jerusalem", "2026-06-14T21:00:00.000Z", "2026-06-14T20:59:59.999Z",
                      "2 |  |  | on-day | 2026-06-14T21:00:00.000Z | \n");
}

TEST(list_date_since_is_los_angeles_local_midnight)
{
    assert_date_since("America/Los_Angeles", "2026-06-15T07:00:00.000Z", "2026-06-15T06:59:59.999Z",
                      "2 |  |  | on-day | 2026-06-15T07:00:00.000Z | \n");
}

TEST(list_seconds_form_matches_canonical)
{
    char *db = make_temp_db_path();
    const char *seconds[] = {"list", "--since", "2026-06-15T12:00:00Z"};
    const char *canonical[] = {"list", "--since", "2026-06-15T12:00:00.000Z"};
    const char *want = "2 |  |  | at-second | 2026-06-15T12:00:00.000Z | \n";
    CmdResult r;
    long at = 0;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    (void)given_stamped(db, "before", "2026-06-15T11:59:59.999Z", "2026-06-15T11:59:59.999Z");
    at = given_stamped(db, "at-second", "2026-06-15T12:00:00.000Z", "2026-06-15T12:00:00.000Z");
    ASSERT_EQ_INT(at, 2);
    if (at != 2) {
        free(db);
        return;
    }
    r = run_remember(db, seconds, sizeof(seconds) / sizeof(seconds[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.out, want);
    cmd_result_free(&r);
    r = run_remember(db, canonical, sizeof(canonical) / sizeof(canonical[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, want);
    cmd_result_free(&r);
    free(db);
}

enum { TS_ERR_BUF = 256, HUGE_TS_LEN = 300, HUGE_ERR_BUF = 340 };

/* The database path, the flag, and the rejected token are adjacent on purpose. */
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
static void assert_timestamp_rejected(const char *db, const char *flag, const char *token)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    const char *args[] = {"list", flag, token};
    char want[TS_ERR_BUF];
    CmdResult r;

    ASSERT_TRUE(snprintf(want, sizeof(want), "remember: invalid timestamp: %s\n", token) > 0);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, want);
    ASSERT_STREQ(r.out, "");
    cmd_result_free(&r);
}

TEST(list_rejects_a_bad_timestamp)
{
    char *db = make_temp_db_path();
    const char *json[] = {"--json-errors", "list", "--since", "now"};
    char huge[HUGE_TS_LEN + 1];
    char want[HUGE_ERR_BUF];
    const char *huge_args[] = {"list", "--since", huge};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    assert_timestamp_rejected(db, "--since", "2026-1-01");
    assert_timestamp_rejected(db, "--until", "2026-10-12T10:00Z");
    assert_timestamp_rejected(db, "--since", "2026-10-12T10:00:00.1Z");
    assert_timestamp_rejected(db, "--since", "2026-10-12T10:00:00.0000Z");
    assert_timestamp_rejected(db, "--since", "2026-10-12T10:00:00+02:00");
    assert_timestamp_rejected(db, "--since", "2026-13-01");
    assert_timestamp_rejected(db, "--since", "2026-02-30");
    assert_timestamp_rejected(db, "--since", "2026-02-29");
    assert_timestamp_rejected(db, "--since", "");
    assert_timestamp_rejected(db, "--since", "now");
    assert_timestamp_rejected(db, "--since", "1899-12-31");
    assert_timestamp_rejected(db, "--since", " 2026-01-01");
    assert_timestamp_rejected(db, "--until", "2026-01-01 ");
    memset(huge, '9', sizeof(huge) - 1U);
    huge[sizeof(huge) - 1U] = '\0';
    ASSERT_TRUE(snprintf(want, sizeof(want), "remember: invalid timestamp: %s\n", huge) > 0);
    r = run_remember(db, huge_args, sizeof(huge_args) / sizeof(huge_args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, want);
    cmd_result_free(&r);
    r = run_remember(db, json, sizeof(json) / sizeof(json[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, "{\"version\":1,\"error\":{\"code\":\"usage\",\"message\":\"invalid "
                        "timestamp: now\"}}\n");
    cmd_result_free(&r);
    free(db);
}

TEST(list_bound_flags_reject_duplicates_and_missing_values)
{
    char *db = make_temp_db_path();
    const char *dup_since[] = {"list", "--since", "2026-01-01", "--since", "2026-02-01"};
    const char *dup_until[] = {"list", "--until", "2026-01-01", "--until", "2026-02-01"};
    const char *miss_since[] = {"list", "--since"};
    const char *miss_until[] = {"list", "--until"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, dup_since, sizeof(dup_since) / sizeof(dup_since[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "remember: duplicate --since\n");
    ASSERT_STREQ(r.out, "");
    cmd_result_free(&r);
    r = run_remember(db, dup_until, sizeof(dup_until) / sizeof(dup_until[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "remember: duplicate --until\n");
    ASSERT_STREQ(r.out, "");
    cmd_result_free(&r);
    r = run_remember(db, miss_since, sizeof(miss_since) / sizeof(miss_since[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "remember: missing value for --since\n");
    ASSERT_STREQ(r.out, "");
    cmd_result_free(&r);
    r = run_remember(db, miss_until, sizeof(miss_until) / sizeof(miss_until[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "remember: missing value for --until\n");
    ASSERT_STREQ(r.out, "");
    cmd_result_free(&r);
    free(db);
}

TEST(list_bounds_compose_with_tag_source_and_key)
{
    char *db = make_temp_db_path();
    const char *kept[] = {"add", "--tag", "keep", "--source", "human", "--key", "slot", "kept"};
    const char *other[] = {"add", "other"};
    const char *early[] = {"add", "--tag", "keep", "--source", "human", "--key", "early", "early"};
    const char *args[] = {"list",
                          "--since",
                          "2026-03-01T00:00:00.000Z",
                          "--until",
                          "2026-05-01T00:00:00.000Z",
                          "--tag",
                          "keep",
                          "--source",
                          "human",
                          "--key",
                          "slot"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, kept, sizeof(kept) / sizeof(kept[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, other, sizeof(other) / sizeof(other[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, early, sizeof(early) / sizeof(early[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    free(harness_sqlite_query_line(db,
                                   "UPDATE entries SET created_at='2026-04-01T00:00:00.000Z', "
                                   "updated_at='2026-04-01T00:00:00.000Z' WHERE id IN (1, 2);"));
    free(harness_sqlite_query_line(db, "UPDATE entries SET created_at='2026-01-01T00:00:00.000Z', "
                                       "updated_at='2026-01-01T00:00:00.000Z' WHERE id=3;"));
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, "1 | slot | keep | kept | 2026-04-01T00:00:00.000Z | \n");
    cmd_result_free(&r);
    free(db);
}

TEST(search_ignores_sort_for_order)
{
    char *db = make_temp_db_path();
    const char *first[] = {"add", "--key", "a", "alpha"};
    const char *second[] = {"add", "--key", "b", "alpha"};
    const char *plain[] = {"search", "alpha"};
    const char *created[] = {"search", "alpha", "--sort", "created"};
    const char *want = "2 | b |  | alpha | 2026-06-01T00:00:00.000Z | \n"
                       "1 | a |  | alpha | 2026-01-01T00:00:00.000Z | \n";
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, first, sizeof(first) / sizeof(first[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, second, sizeof(second) / sizeof(second[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    ASSERT_EQ_INT(stamp_id(db, 1, "2026-06-01T00:00:00.000Z", "2026-01-01T00:00:00.000Z"), 0);
    ASSERT_EQ_INT(stamp_id(db, 2, "2026-01-01T00:00:00.000Z", "2026-06-01T00:00:00.000Z"), 0);
    r = run_remember(db, plain, sizeof(plain) / sizeof(plain[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.out, want);
    cmd_result_free(&r);
    r = run_remember(db, created, sizeof(created) / sizeof(created[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, want);
    cmd_result_free(&r);
    free(db);
}

TEST(search_since_filters_updated_at)
{
    char *db = make_temp_db_path();
    const char *first[] = {"add", "--key", "a", "alpha"};
    const char *second[] = {"add", "--key", "b", "alpha"};
    const char *args[] = {"search",  "alpha",
                          "--sort",  "created",
                          "--since", "2026-03-01T00:00:00.000Z",
                          "--until", "2026-07-01T00:00:00.000Z"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, first, sizeof(first) / sizeof(first[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, second, sizeof(second) / sizeof(second[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    ASSERT_EQ_INT(stamp_id(db, 1, "2026-06-01T00:00:00.000Z", "2026-01-01T00:00:00.000Z"), 0);
    ASSERT_EQ_INT(stamp_id(db, 2, "2026-01-01T00:00:00.000Z", "2026-06-01T00:00:00.000Z"), 0);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, "2 | b |  | alpha | 2026-06-01T00:00:00.000Z | \n");
    cmd_result_free(&r);
    free(db);
}

TEST(search_sort_bogus_is_usage)
{
    char *db = make_temp_db_path();
    const char *args[] = {"search", "alpha", "--sort", "bogus"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "remember: invalid --sort (use updated or created)\n");
    ASSERT_STREQ(r.out, "");
    cmd_result_free(&r);
    free(db);
}

TEST(search_crossed_bounds_are_empty)
{
    char *db = make_temp_db_path();
    const char *args[] = {"--json",
                          "search",
                          "alpha",
                          "--since",
                          "2026-06-16T00:00:00.000Z",
                          "--until",
                          "2026-06-15T00:00:00.000Z"};
    CmdResult r;
    long row = 0;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    row = given_stamped(db, "alpha", "2026-06-15T12:00:00.000Z", "2026-06-15T12:00:00.000Z");
    ASSERT_EQ_INT(row, 1);
    if (row != 1) {
        free(db);
        return;
    }
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, "{\"version\":1,\"offset\":0,\"limit\":20,\"count\":0,\"total\":0,"
                        "\"entries\":[]}\n");
    cmd_result_free(&r);
    free(db);
}

TEST(list_and_search_help_name_sort_and_bounds)
{
    char *db = make_temp_db_path();
    const char *list[] = {"help", "list"};
    const char *search[] = {"help", "search"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, list, sizeof(list) / sizeof(list[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "--sort SORT     updated or created (default updated)");
    ASSERT_STR_CONTAINS(r.out, "--since TS      Inclusive lower bound on the sort field");
    ASSERT_STR_CONTAINS(r.out, "--until TS      Exclusive upper bound on the sort field");
    ASSERT_STR_CONTAINS(r.out, "--since is inclusive and --until is exclusive.");
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
    r = run_remember(db, search, sizeof(search) / sizeof(search[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "--sort SORT     updated or created; accepted, then ignored");
    ASSERT_STR_CONTAINS(r.out, "--since TS      Inclusive lower bound on updated_at");
    ASSERT_STR_CONTAINS(r.out, "--until TS      Exclusive upper bound on updated_at");
    ASSERT_STR_CONTAINS(r.out, "Search ignores --sort.");
    ASSERT_STR_CONTAINS(r.out, "TS is YYYY-MM-DD (local start of that day), YYYY-MM-DDTHH:MM:SSZ,");
    ASSERT_STR_CONTAINS(r.out, "or YYYY-MM-DDTHH:MM:SS.mmmZ.");
    cmd_result_free(&r);
    free(db);
}

void register_list_dates_tests(void)
{
    RUN_TEST(filter_ts_canonical_is_copied);
    RUN_TEST(filter_ts_seconds_become_millis);
    RUN_TEST(filter_ts_date_is_jerusalem_local_midnight);
    RUN_TEST(filter_ts_date_is_los_angeles_local_midnight);
    RUN_TEST(filter_ts_jerusalem_dst_end_is_local_midnight);
    RUN_TEST(filter_ts_skipped_midnight_is_the_first_local_instant);
    RUN_TEST(filter_ts_rejects_a_day_the_zone_skipped);
    RUN_TEST(filter_ts_rejects_a_date_before_1900);
    RUN_TEST(filter_ts_date_1900_01_01_is_accepted);
    RUN_TEST(filter_ts_leap_day_utc_is_that_midnight);
    RUN_TEST(filter_ts_rejects_impossible_month);
    RUN_TEST(filter_ts_rejects_february_30);
    RUN_TEST(filter_ts_rejects_non_leap_february_29);
    RUN_TEST(filter_ts_rejects_malformed_timestamps);
    RUN_TEST(filter_ts_rejects_null_input);
    RUN_TEST(filter_ts_rejects_null_output);
    RUN_TEST(list_sort_created_keeps_creation_order);
    RUN_TEST(list_sort_updated_puts_the_edit_first);
    RUN_TEST(list_sort_rejects_a_bad_value);
    RUN_TEST(list_sort_rejects_a_duplicate);
    RUN_TEST(list_sort_requires_a_value);
    RUN_TEST(list_since_is_inclusive_and_until_is_exclusive);
    RUN_TEST(list_equal_bounds_are_empty);
    RUN_TEST(list_date_since_is_jerusalem_local_midnight);
    RUN_TEST(list_date_since_is_los_angeles_local_midnight);
    RUN_TEST(list_seconds_form_matches_canonical);
    RUN_TEST(list_rejects_a_bad_timestamp);
    RUN_TEST(list_bound_flags_reject_duplicates_and_missing_values);
    RUN_TEST(list_bounds_compose_with_tag_source_and_key);
    RUN_TEST(search_ignores_sort_for_order);
    RUN_TEST(search_since_filters_updated_at);
    RUN_TEST(search_sort_bogus_is_usage);
    RUN_TEST(search_crossed_bounds_are_empty);
    RUN_TEST(list_and_search_help_name_sort_and_bounds);
}
// NOLINTEND(concurrency-mt-unsafe)
