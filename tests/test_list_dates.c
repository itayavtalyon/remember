#include "commands_common.h"
#include "register.h"
#include "test.h"

#include <errno.h>
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

enum {
    YMD_LEN = 10,
    YEAR_LEN = 4,
    MONTH_OFF = 5,
    DAY_OFF = 8,
    FIELD_LEN = 2,
    TM_YEAR_BASE = 1900,
    DECIMAL_BASE = 10
};

static int take_digits(const char *text, size_t count, int *out)
{
    char buf[YEAR_LEN + 1];
    char *end = NULL;
    long value = 0;

    if (count > YEAR_LEN) {
        return -1;
    }
    memcpy(buf, text, count);
    buf[count] = '\0';
    errno = 0;
    value = strtol(buf, &end, DECIMAL_BASE);
    if (errno != 0 || end == buf || *end != '\0') {
        return -1;
    }
    *out = (int)value;
    return 0;
}

/* Oracle for E5.2b: local midnight, then UTC. Not the function under test. */
static void expected_local_midnight(const char *ymd, char out[ISO_TS_BUFSIZE])
{
    int year = 0;
    int month = 0;
    int day = 0;
    struct tm local;
    struct tm utc;
    time_t sec = 0;

    memset(out, 0, ISO_TS_BUFSIZE);
    if (ymd == NULL || strlen(ymd) != YMD_LEN || take_digits(ymd, YEAR_LEN, &year) != 0 ||
        take_digits(ymd + MONTH_OFF, FIELD_LEN, &month) != 0 ||
        take_digits(ymd + DAY_OFF, FIELD_LEN, &day) != 0) {
        return;
    }
    memset(&local, 0, sizeof(local));
    local.tm_year = year - TM_YEAR_BASE;
    local.tm_mon = month - 1;
    local.tm_mday = day;
    local.tm_hour = 0;
    local.tm_min = 0;
    local.tm_sec = 0;
    local.tm_isdst = -1;
    sec = mktime(&local);
    if (gmtime_r(&sec, &utc) == NULL) {
        return;
    }
    (void)snprintf(out, ISO_TS_BUFSIZE, "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
                   utc.tm_year + TM_YEAR_BASE, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min,
                   utc.tm_sec);
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
    const char *old = getenv("TZ");
    int had = old != NULL;
    char *saved = NULL;
    char out[ISO_TS_BUFSIZE];
    char want[ISO_TS_BUFSIZE];

    if (had) {
        saved = strdup(old);
        ASSERT_TRUE(saved != NULL);
        if (saved == NULL) {
            return;
        }
    }
    ASSERT_EQ_INT(setenv("TZ", "Asia/Jerusalem", 1), 0);
    tzset();
    expected_local_midnight("2026-06-15", want);
    out[0] = '\0';
    ASSERT_EQ_INT(parse_filter_ts("2026-06-15", out), 0);
    ASSERT_STREQ(out, want);
    ASSERT_STR_NOT_CONTAINS(out, "T00:00:00.000Z");
    if (had) {
        ASSERT_EQ_INT(setenv("TZ", saved, 1), 0);
    } else {
        ASSERT_EQ_INT(unsetenv("TZ"), 0);
    }
    free(saved);
    tzset();
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

TEST(filter_ts_date_is_los_angeles_local_midnight)
{
    char *saved = NULL;
    int had = 0;
    char out[ISO_TS_BUFSIZE];
    char want[ISO_TS_BUFSIZE];

    ASSERT_EQ_INT(use_tz("America/Los_Angeles", &saved, &had), 0);
    expected_local_midnight("2026-06-15", want);
    out[0] = '\0';
    ASSERT_EQ_INT(parse_filter_ts("2026-06-15", out), 0);
    ASSERT_STREQ(out, want);
    ASSERT_STR_NOT_CONTAINS(out, "T00:00:00.000Z");
    restore_tz(saved, had);
}

TEST(filter_ts_jerusalem_dst_end_is_local_midnight)
{
    char *saved = NULL;
    int had = 0;
    char out[ISO_TS_BUFSIZE];
    char want[ISO_TS_BUFSIZE];

    ASSERT_EQ_INT(use_tz("Asia/Jerusalem", &saved, &had), 0);
    expected_local_midnight("2026-10-25", want);
    out[0] = '\0';
    ASSERT_EQ_INT(parse_filter_ts("2026-10-25", out), 0);
    ASSERT_STREQ(out, want);
    ASSERT_STR_NOT_CONTAINS(out, "2026-10-25T00:00:00.000Z");
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

void register_list_dates_tests(void)
{
    RUN_TEST(filter_ts_canonical_is_copied);
    RUN_TEST(filter_ts_seconds_become_millis);
    RUN_TEST(filter_ts_date_is_jerusalem_local_midnight);
    RUN_TEST(filter_ts_date_is_los_angeles_local_midnight);
    RUN_TEST(filter_ts_jerusalem_dst_end_is_local_midnight);
    RUN_TEST(filter_ts_leap_day_utc_is_that_midnight);
    RUN_TEST(filter_ts_rejects_impossible_month);
    RUN_TEST(filter_ts_rejects_february_30);
    RUN_TEST(filter_ts_rejects_non_leap_february_29);
    RUN_TEST(filter_ts_rejects_malformed_timestamps);
    RUN_TEST(filter_ts_rejects_null_input);
    RUN_TEST(filter_ts_rejects_null_output);
}
// NOLINTEND(concurrency-mt-unsafe)
