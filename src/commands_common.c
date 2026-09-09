#include "commands_common.h"

#include "appio.h"
#include "exit_codes.h"
#include "normalize.h"
#include "store.h"
#include "util.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Radix for all decimal integer parsing. */
enum { DECIMAL_BASE = 10 };

/* Seconds in each TTL unit (m/h/d/w). Enumerators are int; they promote without
   loss where the TTL/overflow math below is unsigned long long or long long. */
enum {
    SECS_PER_MIN = 60,
    SECS_PER_HOUR = 3600,
    SECS_PER_DAY = 86400,
    SECS_PER_WEEK = 604800,
};

/* Time-of-day bounds and struct tm base offsets. */
enum {
    HOUR_MAX = 23,
    MINUTE_MAX = 59,
    SECOND_MAX = 59,
    MS_MAX = 999,
    YEAR_MAX = 9999,
    TM_YEAR_BASE = 1900,
};

/* Howard Hinnant civil<->days algorithm constants (see days_from_civil). */
enum {
    DAYS_PER_ERA = 146097,     /* days in a 400-year Gregorian era */
    DAYS_CIVIL_EPOCH = 719468, /* days from 0000-03-01 to 1970-01-01 */
    YEARS_PER_ERA = 400,
    YEARS_PER_CENTURY = 100,
    DAYS_PER_COMMON_YEAR = 365,
    DOY_NUMERATOR_MUL = 153,  /* month->day-of-year: (153*mp + 2)/5 */
    DOY_DENOMINATOR = 5,
    MONTH_JANFEB_OFFSET = 9,  /* month-index shift for Jan/Feb */
};

/* Canonical UTC stamp layout: "YYYY-MM-DDTHH:MM:SS.mmmZ" (24 chars + NUL).
   *_OFF_* are field byte offsets; *_SEP_* are the separator byte offsets. */
enum {
    ISO_LEN = 24,
    ISO_BUFSIZE = 25,       /* includes NUL; minimum outlen for a canonical stamp */
    ISO_DATE_LEN = 10,      /* "YYYY-MM-DD" prefix */
    ISO_SECONDS_LEN = 19,   /* "YYYY-MM-DDTHH:MM:SS" */
    ISO_MINZ_LEN = 20,      /* seconds precision + 'Z' */
    ISO_FRACZ_MIN_LEN = 22, /* seconds + '.' + >=1 frac digit + 'Z' */
    ISO_OFF_MONTH = 5,
    ISO_SEP_MONTH_DAY = 7,
    ISO_OFF_DAY = 8,
    ISO_SEP_DATE_TIME = 10,
    ISO_OFF_HOUR = 11,
    ISO_SEP_HOUR_MIN = 13,
    ISO_OFF_MIN = 14,
    ISO_SEP_MIN_SEC = 16,
    ISO_OFF_SEC = 17,
    ISO_OFF_DOT = 19,
    ISO_OFF_FRAC = 20,
    ISO_OFF_Z = 23,
};

int parse_entry_id(const char *raw, long long *out_id)
{
    char *end = NULL;
    long long id = 0;

    if (raw == NULL || out_id == NULL) {
        return -1;
    }
    errno = 0;
    id = strtoll(raw, &end, DECIMAL_BASE);
    if (end == raw || (end != NULL && *end != '\0') || errno == ERANGE || id < 1) {
        return -1;
    }
    *out_id = id;
    return 0;
}

const char *norm_body_message(NormStatus st)
{
    switch (st) {
    case NORM_OK:
        return "ok";
    case NORM_ERR_EMPTY:
        return "empty body after trim";
    case NORM_ERR_TOO_LONG:
        return "body exceeds 64 KiB";
    case NORM_ERR_INVALID_UTF8:
        return "invalid UTF-8 in body";
    case NORM_ERR_OOM:
        return "out of memory";
    case NORM_ERR_INVALID_CHAR:
    case NORM_ERR_INTERNAL:
    default:
        return "invalid body";
    }
}

const char *norm_token_message(NormStatus st, const char *kind)
{
    int is_key = (kind != NULL && strcmp(kind, "key") == 0);

    switch (st) {
    case NORM_OK:
        return "ok";
    case NORM_ERR_EMPTY:
        return is_key ? "empty key" : "empty tag";
    case NORM_ERR_TOO_LONG:
        return is_key ? "key exceeds 64 bytes" : "tag exceeds 64 bytes";
    case NORM_ERR_INVALID_UTF8:
        return is_key ? "invalid UTF-8 in key" : "invalid UTF-8 in tag";
    case NORM_ERR_INVALID_CHAR:
        return is_key ? "invalid key" : "invalid tag";
    case NORM_ERR_OOM:
        return "out of memory";
    case NORM_ERR_INTERNAL:
    default:
        return is_key ? "invalid key" : "invalid tag";
    }
}

void err_msg(const char *msg)
{
    (void)fprintf(app_err(), "remember: %s\n", msg);
}

int source_is_valid(const char *s)
{
    return s != NULL &&
           (strcmp(s, "human") == 0 || strcmp(s, "agent") == 0 || strcmp(s, "tool") == 0 ||
            strcmp(s, "share") == 0 || strcmp(s, "unknown") == 0);
}

const char *action_name(StoreAddAction a)
{
    switch (a) {
    case STORE_ADD_CREATED:
        return "created";
    case STORE_ADD_MERGED:
        return "merged";
    case STORE_ADD_UPDATED:
        return "updated";
    default:
        return "created";
    }
}

int push_cstr_ptr(const char ***arr, size_t *n, size_t *cap, const char *t)
{
    if (*n == *cap) {
        size_t ncap = (*cap == 0U) ? 4U : (*cap * 2U);
        const char **grown = (const char **)realloc((void *)*arr, ncap * sizeof(*grown));
        if (grown == NULL) {
            return -1;
        }
        *arr = grown;
        *cap = ncap;
    }
    (*arr)[*n] = t;
    (*n)++;
    return 0;
}

int take_value(int *i, int rest_argc, const char **rest_argv, const char **out, const char **err,
               const char *missing_msg)
{
    if (*i + 1 >= rest_argc) {
        *err = missing_msg;
        return -1;
    }
    *i += 1;
    *out = rest_argv[*i];
    return 0;
}

/* ISO C11: avoid strdup (POSIX; hidden under -std=c11 without feature macros). */
static char *dup_cstr(const char *s)
{
    size_t n = 0;
    char *p = NULL;

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

int normalize_tags(const char *const *tag_raw, size_t ntag_raw, char ***out_tags, size_t *out_ntags,
                   const char **err)
{
    size_t t = 0;
    char **tags = NULL;

    *out_tags = NULL;
    *out_ntags = 0U;
    *err = NULL;
    if (ntag_raw == 0U) {
        return 0;
    }

    tags = (char **)calloc(ntag_raw, sizeof(*tags));
    if (tags == NULL) {
        *err = "out of memory";
        return -1;
    }
    for (t = 0; t < ntag_raw; t++) {
        char buf[REMEMBER_TOKEN_MAX + 1];
        NormStatus ns = normalize_tag(tag_raw[t], buf, sizeof(buf));
        if (ns != NORM_OK) {
            size_t j = 0;
            for (j = 0; j < t; j++) {
                free(tags[j]);
            }
            free((void *)tags);
            *err = norm_token_message(ns, "tag");
            return -1;
        }
        tags[t] = dup_cstr(buf);
        if (tags[t] == NULL) {
            size_t j = 0;
            for (j = 0; j < t; j++) {
                free(tags[j]);
            }
            free((void *)tags);
            *err = "out of memory";
            return -1;
        }
    }
    *out_tags = tags;
    *out_ntags = ntag_raw;
    return 0;
}

void free_tag_list(char **tags, size_t ntags)
{
    size_t t = 0;
    if (tags == NULL) {
        return;
    }
    for (t = 0; t < ntags; t++) {
        free(tags[t]);
    }
    free((void *)tags);
}

int store_status_to_exit(StoreStatus st)
{
    if (st == STORE_ERR_NOT_FOUND) {
        err_msg(store_status_message(st));
        return REMEMBER_NOT_FOUND;
    }
    if (st == STORE_ERR_EXPIRED || st == STORE_ERR_NOT_IN_TRASH) {
        err_msg(store_status_message(st));
        return REMEMBER_WRONG_BIN;
    }
    if (st != STORE_OK) {
        err_msg(store_status_message(st));
        return REMEMBER_ERR;
    }
    return REMEMBER_OK;
}

int load_body(const char *body_raw, int dash_is_stdin, char **out_body, size_t *out_len,
              const char **err)
{
    char *stdin_body = NULL;
    size_t stdin_len = 0U;
    NormStatus ns = NORM_OK;

    *out_body = NULL;
    *out_len = 0U;
    *err = NULL;

    if (body_raw == NULL) {
        *err = "missing body";
        return -1;
    }
    if (dash_is_stdin != 0 && strcmp(body_raw, "-") == 0) {
        /* Read with a generous hard cap; body_trim_copy enforces the real
           post-trim 64 KiB limit, so stdin and argv reject identically. */
        int rr = util_read_stdin(&stdin_body, &stdin_len, REMEMBER_STDIN_MAX);
        if (rr == -2) {
            *err = "stdin input too large";
            return -1;
        }
        if (rr != 0 || stdin_body == NULL) {
            *err = "failed to read body from stdin";
            return -1;
        }
        ns = body_trim_copy(stdin_body, stdin_len, out_body, out_len);
        free(stdin_body);
    } else {
        ns = body_trim_copy(body_raw, strlen(body_raw), out_body, out_len);
    }
    if (ns != NORM_OK) {
        *err = norm_body_message(ns);
        return -1;
    }
    return 0;
}

static int digit(char c)
{
    return c >= '0' && c <= '9';
}

/* A broken-down civil UTC time. Passed by value so the six int components
   cannot be transposed at a call site. */
typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int min;
    int sec;
} CivilTime;

/* Civil to Unix days (Howard Hinnant). Uses only the date fields of t. */
static long long days_from_civil(CivilTime t)
{
    int era = 0;
    unsigned yoe = 0;
    unsigned doy = 0;
    unsigned doe = 0;
    const int m = t.month;
    const int d = t.day;
    int yy = t.year;

    yy -= (m <= 2) ? 1 : 0;
    era = (yy >= 0 ? yy : (yy - (YEARS_PER_ERA - 1))) / YEARS_PER_ERA;
    yoe = (unsigned)(yy - (era * YEARS_PER_ERA));
    doy = ((((DOY_NUMERATOR_MUL * (unsigned)(m + ((m > 2) ? -3 : MONTH_JANFEB_OFFSET))) + 2U) /
            DOY_DENOMINATOR) +
           ((unsigned)d - 1U));
    doe = (((yoe * DAYS_PER_COMMON_YEAR) + (yoe / 4U)) - (yoe / YEARS_PER_CENTURY)) + doy;
    return (((long long)era * DAYS_PER_ERA) + (long long)doe) - DAYS_CIVIL_EPOCH;
}

static int unix_from_civil(CivilTime t, long long *out)
{
    long long days = 0;
    long long sec = 0;
    const int h = t.hour;
    const int mi = t.min;
    const int se = t.sec;

    days = days_from_civil(t);
    if (days > LLONG_MAX / SECS_PER_DAY || days < LLONG_MIN / SECS_PER_DAY) {
        return -1;
    }
    sec = days * SECS_PER_DAY;
    if (h < 0 || h > HOUR_MAX || mi < 0 || mi > MINUTE_MAX || se < 0 || se > SECOND_MAX) {
        return -1;
    }
    sec += ((long long)h * SECS_PER_HOUR) + ((long long)mi * SECS_PER_MIN) + (long long)se;
    *out = sec;
    return 0;
}

static int format_iso_mmmz(int y, int mo, int d, int h, int mi, int se, int ms, char *out,
                           size_t outlen)
{
    int n = 0;

    if (out == NULL || outlen < ISO_BUFSIZE) {
        return -1;
    }
    if (y < 1 || y > YEAR_MAX) {
        return -1;
    }
    n = snprintf(out, outlen, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", y, mo, d, h, mi, se, ms);
    if (n != ISO_LEN) {
        return -1;
    }
    return 0;
}

static int parse_n_digits(const char *s, size_t n, int *out)
{
    size_t i = 0;
    int v = 0;

    for (i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return -1;
        }
        v = (v * DECIMAL_BASE) + (int)(s[i] - '0');
    }
    *out = v;
    return 0;
}

static int parse_iso_mmmz(const char *s, int *y, int *mo, int *d, int *h, int *mi, int *se, int *ms)
{
    if (s == NULL || strlen(s) != (size_t)ISO_LEN) {
        return -1;
    }
    if (s[4] != '-' || s[ISO_SEP_MONTH_DAY] != '-' || s[ISO_SEP_DATE_TIME] != 'T' ||
        s[ISO_SEP_HOUR_MIN] != ':' || s[ISO_SEP_MIN_SEC] != ':' || s[ISO_OFF_DOT] != '.' ||
        s[ISO_OFF_Z] != 'Z') {
        return -1;
    }
    if (parse_n_digits(s, 4U, y) != 0 || parse_n_digits(s + ISO_OFF_MONTH, 2U, mo) != 0 ||
        parse_n_digits(s + ISO_OFF_DAY, 2U, d) != 0 || parse_n_digits(s + ISO_OFF_HOUR, 2U, h) != 0 ||
        parse_n_digits(s + ISO_OFF_MIN, 2U, mi) != 0 || parse_n_digits(s + ISO_OFF_SEC, 2U, se) != 0 ||
        parse_n_digits(s + ISO_OFF_FRAC, 3U, ms) != 0) {
        return -1;
    }
    return 0;
}

/* A point in time as whole seconds since the Unix epoch plus a millisecond
   remainder. Named so the two convertible components cannot be transposed. */
typedef struct {
    long long unix_sec;
    int ms;
} Instant;

static int unix_to_iso_ms(Instant at, char *out, size_t outlen)
{
    time_t tt = 0;
    struct tm tm;

    if (at.ms < 0 || at.ms > MS_MAX) {
        return -1;
    }
    tt = (time_t)at.unix_sec;
    if ((long long)tt != at.unix_sec) {
        return -1;
    }
    /* gmtime_r: reentrant; gmtime uses a shared static buffer (concurrency-mt-unsafe). */
    if (gmtime_r(&tt, &tm) == NULL) {
        return -1;
    }
    return format_iso_mmmz(tm.tm_year + TM_YEAR_BASE, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                           tm.tm_min, tm.tm_sec, at.ms, out, outlen);
}

static int match_mask(const char *s, const char *mask)
{
    size_t i = 0;

    for (i = 0; mask[i] != '\0'; i++) {
        if (mask[i] == 'd') {
            if (!digit(s[i])) {
                return 0;
            }
        } else if (s[i] != mask[i]) {
            return 0;
        }
    }
    return s[i] == '\0';
}

static int match_mask_n(const char *s, const char *mask, size_t n)
{
    size_t i = 0;

    for (i = 0; i < n; i++) {
        if (mask[i] == '\0') {
            return 0;
        }
        if (mask[i] == 'd') {
            if (!digit(s[i])) {
                return 0;
            }
        } else if (s[i] != mask[i]) {
            return 0;
        }
    }
    return 1;
}

int parse_ttl_to_expires(const char *token, const char *now, char *out, size_t outlen,
                         const char **err)
{
    const char *p = NULL;
    unsigned long long n = 0ULL;
    unsigned long long mul = 0;
    long long add_sec = 0;
    long long unix_sec = 0;
    int y = 0;
    int mo = 0;
    int d = 0;
    int h = 0;
    int mi = 0;
    int se = 0;
    int ms = 0;

    *err = "invalid --ttl";
    if (token == NULL || now == NULL || out == NULL) {
        return -1;
    }
    p = token;
    if (*p < '1' || *p > '9') {
        return -1;
    }
    while (*p >= '0' && *p <= '9') {
        if (n > (ULLONG_MAX - (unsigned long long)(*p - '0')) / DECIMAL_BASE) {
            *err = "invalid --ttl";
            return -1;
        }
        n = (n * DECIMAL_BASE) + (unsigned long long)(*p - '0');
        p++;
    }
    if (*p == '\0' || p[1] != '\0') {
        return -1;
    }
    switch (*p) {
    case 'm':
        mul = SECS_PER_MIN;
        break;
    case 'h':
        mul = SECS_PER_HOUR;
        break;
    case 'd':
        mul = SECS_PER_DAY;
        break;
    case 'w':
        mul = SECS_PER_WEEK;
        break;
    default:
        return -1;
    }
    if (n > ULLONG_MAX / mul) {
        return -1;
    }
    n *= mul;
    if (n > (unsigned long long)LLONG_MAX) {
        return -1;
    }
    add_sec = (long long)n;
    if (parse_iso_mmmz(now, &y, &mo, &d, &h, &mi, &se, &ms) != 0) {
        *err = "internal error";
        return -1;
    }
    if (unix_from_civil((CivilTime){.year = y, .month = mo, .day = d, .hour = h, .min = mi, .sec = se},
                        &unix_sec) != 0) {
        return -1;
    }
    if (add_sec > 0 && unix_sec > LLONG_MAX - add_sec) {
        return -1;
    }
    unix_sec += add_sec;
    if (unix_to_iso_ms((Instant){.unix_sec = unix_sec, .ms = ms}, out, outlen) != 0) {
        return -1;
    }
    return 0;
}

static int expires_date_only(const char *token, char *out, size_t outlen)
{
    int y = 0;
    int mo = 0;
    int d = 0;
    struct tm t;
    time_t sec = 0;
    struct tm utc;

    if (strlen(token) != (size_t)ISO_DATE_LEN || !match_mask(token, "dddd-dd-dd")) {
        return -1;
    }
    if (parse_n_digits(token, 4U, &y) != 0 || parse_n_digits(token + ISO_OFF_MONTH, 2U, &mo) != 0 ||
        parse_n_digits(token + ISO_OFF_DAY, 2U, &d) != 0) {
        return -1;
    }
    memset(&t, 0, sizeof(t));
    t.tm_year = y - TM_YEAR_BASE;
    t.tm_mon = mo - 1;
    t.tm_mday = d;
    t.tm_hour = HOUR_MAX;
    t.tm_min = MINUTE_MAX;
    t.tm_sec = SECOND_MAX;
    t.tm_isdst = -1;
    sec = mktime(&t);
    if (sec == (time_t)-1) {
        return -1;
    }
    /* gmtime_r: reentrant; gmtime uses a shared static buffer (concurrency-mt-unsafe). */
    if (gmtime_r(&sec, &utc) == NULL) {
        return -1;
    }
    return format_iso_mmmz(utc.tm_year + TM_YEAR_BASE, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour,
                           utc.tm_min, utc.tm_sec, MS_MAX, out, outlen);
}

static int expires_utc_z(const char *token, char *out, size_t outlen)
{
    size_t n = strlen(token);
    char canon[ISO_BUFSIZE];
    int y = 0;
    int mo = 0;
    int d = 0;
    int h = 0;
    int mi = 0;
    int se = 0;
    int ms = 0;

    if (n < (size_t)ISO_MINZ_LEN || token[n - 1U] != 'Z') {
        return -1;
    }
    if (!match_mask_n(token, "dddd-dd-ddTdd:dd:dd", (size_t)ISO_SECONDS_LEN)) {
        return -1;
    }
    if (n == (size_t)ISO_MINZ_LEN && token[ISO_OFF_DOT] == 'Z') {
        memcpy(canon, token, (size_t)ISO_SECONDS_LEN);
        memcpy(canon + ISO_OFF_DOT, ".000Z", sizeof(".000Z"));
    } else if (n >= (size_t)ISO_FRACZ_MIN_LEN && token[ISO_OFF_DOT] == '.') {
        size_t i = 0;
        size_t frac_n = n - (size_t)(ISO_OFF_FRAC + 1); /* frac digits; n>=22 => frac_n>=1 */
        for (i = (size_t)ISO_OFF_FRAC; i < n - 1U; i++) {
            if (!digit(token[i])) {
                return -1;
            }
        }
        memcpy(canon, token, (size_t)ISO_OFF_FRAC); /* through the dot */
        if (frac_n >= 3U) {
            memcpy(canon + ISO_OFF_FRAC, token + ISO_OFF_FRAC, 3U);
        } else {
            memcpy(canon + ISO_OFF_FRAC, token + ISO_OFF_FRAC, frac_n);
            memset(canon + (size_t)ISO_OFF_FRAC + frac_n, '0', 3U - frac_n);
        }
        canon[ISO_OFF_Z] = 'Z';
        canon[ISO_LEN] = '\0';
    } else {
        return -1;
    }
    if (parse_iso_mmmz(canon, &y, &mo, &d, &h, &mi, &se, &ms) != 0) {
        return -1;
    }
    if (outlen < (size_t)ISO_BUFSIZE) {
        return -1;
    }
    memcpy(out, canon, (size_t)ISO_BUFSIZE);
    return 0;
}

int parse_expires_to_iso(const char *token, char *out, size_t outlen, const char **err)
{
    *err = "invalid --expires";
    if (token == NULL || out == NULL) {
        return -1;
    }
    if (strlen(token) == (size_t)ISO_DATE_LEN) {
        if (expires_date_only(token, out, outlen) != 0) {
            return -1;
        }
        return 0;
    }
    if (expires_utc_z(token, out, outlen) != 0) {
        return -1;
    }
    return 0;
}

int resolve_expiry_flags(const char *ttl_raw, const char *expires_raw, const char *now, char *out,
                         size_t outlen, const char **out_expires, const char **err)
{
    *out_expires = NULL;
    if (ttl_raw != NULL && expires_raw != NULL) {
        *err = "cannot combine --ttl and --expires";
        return -1;
    }
    if (ttl_raw != NULL) {
        if (parse_ttl_to_expires(ttl_raw, now, out, outlen, err) != 0) {
            return -1;
        }
        *out_expires = out;
        return 0;
    }
    if (expires_raw != NULL) {
        if (parse_expires_to_iso(expires_raw, out, outlen, err) != 0) {
            return -1;
        }
        *out_expires = out;
        return 0;
    }
    return 0;
}
