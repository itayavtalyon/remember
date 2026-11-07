#include "cmd_settings.h"

#include "appio.h"
#include "commands.h"
#include "commands_common.h"
#include "diag.h"
#include "exit_codes.h"
#include "normalize.h"
#include "output.h"
#include "settings.h"
#include "store.h"
#include "sync_credential.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* glibc hides timegm under _POSIX_C_SOURCE. Apple clang declares it; Apple gcc does not. */
#if !defined(__APPLE__) || !defined(__clang__)
extern time_t timegm(struct tm *utc);
#endif

/* ST-3.3. One deadline, not a second timeout. */
enum { CREDENTIAL_TIMEOUT_MS = 30000 };

/* "remember-credential-" + helper id (40) + NUL. */
enum { CREDENTIAL_NAME_CAP = 64 };

enum { DRAIN_CAP = 64 };

enum { MS_PER_SEC = 1000, NS_PER_MS = 1000000, POLL_MAX_MS = 1000000 };

/* Short pause while the helper is still running, and a last wait after SIGKILL. */
enum { PAUSE_NS = 20000000, REAP_MS = 1000 };

/* POSIX environ. posix_spawnp has no const envp. */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern char **environ;

#ifdef REMEMBER_TEST_HOOKS
/* Test-only deadline override. Production stays CREDENTIAL_TIMEOUT_MS. */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static int g_timeout_override = 0;

/* cppcheck-suppress staticFunction ; public API (cmd_settings.h); tests */
void cli_credential_test_set_timeout_ms(int ms)
{
    g_timeout_override = ms;
}
#endif

/* cppcheck-suppress staticFunction ; public API (cmd_settings.h); used by tests */
int cli_credential_timeout_ms(void)
{
#ifdef REMEMBER_TEST_HOOKS
    if (g_timeout_override > 0) {
        return g_timeout_override;
    }
#endif
    return CREDENTIAL_TIMEOUT_MS;
}

static long long mono_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return ((long long)ts.tv_sec * (long long)MS_PER_SEC) +
           ((long long)ts.tv_nsec / (long long)NS_PER_MS);
}

static int remaining_ms(long long deadline)
{
    long long now = mono_ms();
    long long left = deadline - now;

    if (left <= 0) {
        return 0;
    }
    if (left > (long long)POLL_MAX_MS) {
        return POLL_MAX_MS;
    }
    return (int)left;
}

/* EINTR is a retry. Any other negative rc is a real failure. */
static bool interrupted(int rc)
{
    if (rc < 0 && errno == EINTR) {
        return true;
    }
    return false;
}

/* n is bytes already read, below cap; cap is the buffer size. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void trim_line(char *buf, size_t n, size_t cap)
{
    size_t start = 0U;
    size_t end = n;

    ascii_ws_trim(buf, &start, &end);
    memmove(buf, buf + start, end - start);
    /* n < cap, so this writes the NUL and clears the moved-from tail. */
    sync_credential_wipe(buf + (end - start), cap - (end - start));
}

/* pid is the child. deadline is a CLOCK_MONOTONIC millisecond stamp. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static int wait_child(pid_t pid, long long deadline)
{
    for (;;) {
        int status = 0;
        pid_t got = waitpid(pid, &status, WNOHANG);

        if (got == pid) {
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                return 0;
            }
            break;
        }
        if (interrupted((int)got)) {
            continue;
        }
        if (got < 0) {
            break;
        }
        if (remaining_ms(deadline) == 0) {
            (void)killpg(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
            break;
        }
        {
            struct timespec pause = {.tv_sec = 0, .tv_nsec = PAUSE_NS};

            (void)nanosleep(&pause, NULL);
        }
    }
    return -2;
}

/* fd is the pipe. deadline is a CLOCK_MONOTONIC millisecond stamp. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static bool poll_ready(int fd, long long deadline)
{
    for (;;) {
        struct pollfd pfd;
        int rc = 0;

        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        rc = poll(&pfd, 1, remaining_ms(deadline));
        if (interrupted(rc)) {
            continue;
        }
        if (rc > 0) {
            return true;
        }
        return false;
    }
}

/* fd is the pipe. deadline is a CLOCK_MONOTONIC millisecond stamp. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static int drain_stdout(int fd, long long deadline)
{
    unsigned char drain[DRAIN_CAP];

    memset(drain, 0, sizeof(drain));
    for (;;) {
        ssize_t extra = 0;

        if (!poll_ready(fd, deadline)) {
            sync_credential_wipe(drain, sizeof(drain));
            return -2;
        }
        extra = read(fd, drain, sizeof(drain));
        sync_credential_wipe(drain, sizeof(drain));
        if (interrupted((int)extra)) {
            continue;
        }
        if (extra <= 0) {
            return extra < 0 ? -2 : 0;
        }
    }
}

/* Read the first stdout line into buf. 0 finished the line, -2 timed out or the read failed.
   refused: the line does not fit, or holds a NUL that would cut the token. */
static int read_first_line(int read_fd, char *buf, size_t cap, bool *refused, long long deadline)
{
    size_t n = 0U;
    bool line_done = false;

    *refused = false;
    while (!line_done) {
        unsigned char byte = 0;
        ssize_t got = 0;

        if (!poll_ready(read_fd, deadline)) {
            goto fail;
        }
        got = read(read_fd, &byte, 1);
        if (interrupted((int)got)) {
            continue;
        }
        if (got < 0) {
            goto fail;
        }
        if (got == 0 || byte == '\n') {
            line_done = true;
            continue;
        }
        if (byte != '\0' && n + 1U < cap) {
            buf[n] = (char)byte;
            n++;
            continue;
        }
        *refused = true;
    }
    /* A refused line is not terminated; the caller wipes it. */
    if (!*refused) {
        trim_line(buf, n, cap);
    }
    return drain_stdout(read_fd, deadline);
fail:
    return -2;
}

static int spawn_helper(char *name, pid_t *pid, int errfd, int *read_fd)
{
    int pipes[2] = {-1, -1};
    posix_spawn_file_actions_t actions = {0};
    posix_spawnattr_t attr = {0};
    char verb[] = "get";
    char *argv[3];
    int spawned = -2;
    bool actions_ok = false;
    bool attr_ok = false;

    argv[0] = name;
    argv[1] = verb;
    argv[2] = NULL;
    /* pipe2 is Linux-only. File actions close both ends in the child. */
    // NOLINTNEXTLINE(android-cloexec-pipe)
    if (pipe(pipes) != 0) {
        goto out;
    }
    if (posix_spawn_file_actions_init(&actions) != 0) {
        goto out;
    }
    actions_ok = true;
    if (posix_spawnattr_init(&attr) != 0) {
        goto out;
    }
    attr_ok = true;
    if (posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0) != 0 ||
        posix_spawn_file_actions_adddup2(&actions, pipes[1], STDOUT_FILENO) != 0 ||
        posix_spawn_file_actions_adddup2(&actions, errfd, STDERR_FILENO) != 0 ||
        posix_spawn_file_actions_addclose(&actions, pipes[0]) != 0 ||
        posix_spawn_file_actions_addclose(&actions, pipes[1]) != 0 ||
        posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP) != 0 ||
        posix_spawnattr_setpgroup(&attr, 0) != 0) {
        goto out;
    }
    {
        int rc = posix_spawnp(pid, name, &actions, &attr, argv, environ);

        if (rc == 0) {
            spawned = 0;
        } else if (rc == ENOENT) {
            spawned = -1;
        }
    }
out:
    if (actions_ok) {
        posix_spawn_file_actions_destroy(&actions);
    }
    if (attr_ok) {
        posix_spawnattr_destroy(&attr);
    }
    if (pipes[1] >= 0) {
        (void)close(pipes[1]);
    }
    if (spawned != 0) {
        if (pipes[0] >= 0) {
            (void)close(pipes[0]);
        }
        return spawned;
    }
    *read_fd = pipes[0];
    return 0;
}

/* cppcheck-suppress staticFunction ; public API (cmd_settings.h); used by tests */
int cli_credential_token(void *ctx, const char *helper_id, char *buf, size_t buflen)
{
    char name[CREDENTIAL_NAME_CAP];
    long long deadline = 0;
    pid_t pid = 0;
    int read_fd = -1;
    int errfd = 0;
    int spawned = 0;
    int wrote = 0;
    bool refused = false;

    (void)ctx;
    if (buf == NULL || buflen == 0U || helper_id == NULL || !helper_id_ok(helper_id)) {
        return -1;
    }
    sync_credential_wipe(buf, buflen);
    wrote = snprintf(name, sizeof(name), "remember-credential-%s", helper_id);
    if (wrote < 0 || (size_t)wrote >= sizeof(name)) {
        return -1;
    }
    errfd = app_err_fileno();
    if (errfd < 0) {
        return -2;
    }
    deadline = mono_ms() + (long long)cli_credential_timeout_ms();
    spawned = spawn_helper(name, &pid, errfd, &read_fd);
    if (spawned != 0) {
        return spawned;
    }
    if (read_first_line(read_fd, buf, buflen, &refused, deadline) != 0 || refused ||
        buf[0] == '\0') {
        sync_credential_wipe(buf, buflen);
        (void)killpg(pid, SIGKILL);
        (void)close(read_fd);
        (void)wait_child(pid, mono_ms() + (long long)REAP_MS);
        return -2;
    }
    (void)close(read_fd);
    if (wait_child(pid, deadline) != 0) {
        sync_credential_wipe(buf, buflen);
        return -2;
    }
    return 0;
}

/* C-01. The message stays the setting_unknown line. */
static const char k_manifest_fix[] =
    "Budgets and cold-start grace are set by the plugin's manifest. Raise "
    "remember.plugins.chain_budget instead.";

/* U+00B7. The locked 007 example uses it between the other layers. */
static const char k_layer_sep[] = " \xC2\xB7 ";

/* Calendar and stamp widths. The numbers are the grammar, so they live here once. */
// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
enum {
    KIND_CHARS = 8,
    PLUGIN_DOT_LEN = 7,
    GRACE_SUFFIX_LEN = 17,
    ASCII_UNIT = 0x20,
    ASCII_DEL = 0x7F,
    STAMP_FIELDS = 7,
    MONTH_MAX = 12,
    HOUR_MAX = 23,
    MINUTE_MAX = 59,
    SECOND_MAX = 59,
    TM_EPOCH_YEAR = 1900,
    UNSET_CHARS = 8,
    DATE_TEXT_CAP = 64,
    STORE_ERR_CAP = 256,
    LEAP_DAY = 29,
    FEB = 2,
    LEAP_EVERY = 4,
    CENTURY = 100,
    LEAP_CENTURY = 400
};
// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

typedef struct {
    const char *key;
    SettingLayer scope;
    bool scope_set;
    bool value_only;
    char pad_[2];
} GetOpts;

typedef struct {
    const char *json;
    const char *actor;
    const char *stamp;
    SettingFrom from;
    bool changed;
    char pad_[3];
} Shown;

typedef struct {
    char kind[KIND_CHARS];
    char id[ACTOR_ID_MAX + 1U];
    bool has_id;
    bool ok;
} ActorParts;

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int sec;
} StampParts;

typedef struct {
    FILE *out;
    const char *name;
    const char *json;
    const char *actor;
    const char *stamp;
    SettingType type;
    bool comma;
    bool live;
    char pad_[2];
} LayerOut;

static const char *type_token(SettingType type)
{
    switch (type) {
    case SETTING_INTEGER:
        return "integer";
    case SETTING_DURATION:
        return "duration";
    case SETTING_STRING:
        return "string";
    case SETTING_STRING_LIST:
    default:
        return "string_list";
    }
}

static const char *from_token(SettingFrom from)
{
    switch (from) {
    case SETTING_FROM_USER:
        return "user";
    case SETTING_FROM_BRAIN:
        return "brain";
    case SETTING_FROM_FLAG:
        return "flag";
    case SETTING_FROM_DEFAULT:
    default:
        return "default";
    }
}

static bool plugin_manifest_key(const char *key)
{
    size_t n = 0;

    n = strlen(key);
    if (strncmp(key, "plugin.", PLUGIN_DOT_LEN) != 0) {
        return false;
    }
    if (strcmp(key + (n - (size_t)PLUGIN_DOT_LEN), ".budget") == 0) {
        return true;
    }
    if (n >= (size_t)GRACE_SUFFIX_LEN &&
        strcmp(key + (n - (size_t)GRACE_SUFFIX_LEN), ".cold_start_grace") == 0) {
        return true;
    }
    return false;
}

static int report_unknown(const char *key)
{
    const char *suggest = setting_suggestion(key);
    const char *fix = diag_code_fix(DIAG_SETTING_UNKNOWN);
    char *msg = NULL;
    int n = 0;

    if (plugin_manifest_key(key)) {
        fix = k_manifest_fix;
    }
    if (suggest != NULL) {
        n = snprintf(NULL, 0, "unknown setting \"%s\". Did you mean \"%s\"?", key, suggest);
    } else {
        n = snprintf(NULL, 0, "unknown setting \"%s\".", key);
    }
    if (n < 0) {
        err_msg(DIAG_INTERNAL, "internal error");
        return REMEMBER_ERR;
    }
    msg = (char *)malloc((size_t)n + 1U);
    if (msg == NULL) {
        err_msg(DIAG_OUT_OF_MEMORY, "out of memory");
        return REMEMBER_ERR;
    }
    if (suggest != NULL) {
        (void)snprintf(msg, (size_t)n + 1U, "unknown setting \"%s\". Did you mean \"%s\"?", key,
                       suggest);
    } else {
        (void)snprintf(msg, (size_t)n + 1U, "unknown setting \"%s\".", key);
    }
    (void)diag_error_fix(DIAG_SETTING_UNKNOWN, msg, fix);
    free(msg);
    return REMEMBER_NOT_FOUND;
}

static int take_scope(int *index, int argc, const char **argv, GetOpts *opt)
{
    const char *val = NULL;

    if (opt->scope_set) {
        err_msg(DIAG_USAGE, "duplicate --scope");
        return -1;
    }
    if (*index + 1 >= argc) {
        err_msg(DIAG_USAGE, "missing value for --scope");
        return -1;
    }
    *index += 1;
    val = argv[*index];
    if (strcmp(val, "user") == 0) {
        opt->scope = SETTING_LAYER_USER;
    } else if (strcmp(val, "brain") == 0) {
        opt->scope = SETTING_LAYER_BRAIN;
    } else {
        err_msg(DIAG_USAGE, "invalid --scope (use brain or user)");
        return -1;
    }
    opt->scope_set = true;
    return 0;
}

static int parse_get(int argc, const char **argv, GetOpts *opt)
{
    int i = 0;
    bool end_opts = false;

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (!end_opts && strcmp(arg, "--") == 0) {
            end_opts = true;
            continue;
        }
        if (!end_opts && strcmp(arg, "--scope") == 0) {
            if (take_scope(&i, argc, argv, opt) != 0) {
                return -1;
            }
            continue;
        }
        if (!end_opts && strcmp(arg, "--value") == 0) {
            if (opt->value_only) {
                err_msg(DIAG_USAGE, "duplicate --value");
                return -1;
            }
            opt->value_only = true;
            continue;
        }
        if (!end_opts && arg[0] == '-' && arg[1] != '\0') {
            err_fmt(DIAG_USAGE, "unknown option '%s'", arg);
            return -1;
        }
        if (opt->key != NULL) {
            err_msg(DIAG_USAGE, "settings get takes one key");
            return -1;
        }
        opt->key = arg;
    }
    if (opt->key == NULL) {
        err_msg(DIAG_USAGE, "settings get requires a key");
        return -1;
    }
    return 0;
}

static bool quoted_plain(const char *text, char *inner, size_t inner_cap)
{
    size_t n = strlen(text);
    size_t i = 0;

    if (n < 2U || text[0] != '"' || text[n - 1U] != '"') {
        return false;
    }
    if (n - 2U >= inner_cap) {
        return false;
    }
    for (i = 1; i + 1U < n; i++) {
        unsigned char c = (unsigned char)text[i];

        if (c < (unsigned char)ASCII_UNIT || c == (unsigned char)ASCII_DEL || c == '"' ||
            c == '\\') {
            return false;
        }
    }
    memcpy(inner, text + 1, n - 2U);
    inner[n - 2U] = '\0';
    return true;
}

static bool array_plain(const char *text)
{
    size_t n = strlen(text);
    size_t i = 1;

    if (n < 2U || text[0] != '[' || text[n - 1U] != ']') {
        return false;
    }
    if (n == 2U) {
        return true;
    }
    while (i + 1U < n) {
        if (text[i] != '"') {
            return false;
        }
        i++;
        while (i + 1U < n && text[i] != '"') {
            unsigned char c = (unsigned char)text[i];

            if (c < (unsigned char)ASCII_UNIT || c == (unsigned char)ASCII_DEL || c == '\\') {
                return false;
            }
            i++;
        }
        if (i >= n || text[i] != '"') {
            return false;
        }
        i++;
        if (i + 1U == n && text[i] == ']') {
            return true;
        }
        if (i >= n || text[i] != ',') {
            return false;
        }
        i++;
    }
    return false;
}

static bool json_text_ok(SettingType type, const char *text)
{
    char inner[SETTING_VALUE_CAP];
    long long ms = 0;
    long long n = 0;

    if (text == NULL || text[0] == '\0') {
        return false;
    }
    if (type == SETTING_INTEGER) {
        return setting_integer(text, &n);
    }
    if (type == SETTING_STRING) {
        return quoted_plain(text, inner, sizeof(inner));
    }
    if (type == SETTING_DURATION) {
        char canon[NORM_MEASURE_CANON_CAP];

        if (!quoted_plain(text, inner, sizeof(inner))) {
            return false;
        }
        return norm_duration_canonical(inner, &ms, canon, sizeof(canon));
    }
    return array_plain(text);
}

static bool values_ok(const SettingSpec *spec, const SettingRead *read, Shown shown)
{
    char def[SETTING_VALUE_CAP];

    if (shown.json != NULL && !json_text_ok(spec->type, shown.json)) {
        return false;
    }
    if (spec->allow_user && read->user_live && !json_text_ok(spec->type, read->user_value)) {
        return false;
    }
    if (spec->allow_brain && read->brain_live && !json_text_ok(spec->type, read->brain_value)) {
        return false;
    }
    if (setting_default_json(spec, def, sizeof(def)) && !json_text_ok(spec->type, def)) {
        return false;
    }
    return true;
}

static Shown shown_of(const SettingSpec *spec, const SettingRead *read, GetOpts opt)
{
    Shown shown;

    memset(&shown, 0, sizeof(shown));
    if (opt.scope_set) {
        shown.from = opt.scope == SETTING_LAYER_USER ? SETTING_FROM_USER : SETTING_FROM_BRAIN;
        if (opt.scope == SETTING_LAYER_USER && spec->allow_user && read->user_live) {
            shown.json = read->user_value;
            shown.actor = read->user_by;
            shown.stamp = read->user_at;
            shown.changed = true;
        } else if (opt.scope == SETTING_LAYER_BRAIN && spec->allow_brain && read->brain_live) {
            shown.json = read->brain_value;
            shown.actor = read->brain_by;
            shown.stamp = read->brain_at;
            shown.changed = true;
        }
        return shown;
    }
    shown.from = read->from;
    if ((spec->allow_user && read->user_live) || (spec->allow_brain && read->brain_live)) {
        shown.changed = true;
    }
    if (read->value[0] != '\0') {
        shown.json = read->value;
    }
    if (read->from == SETTING_FROM_USER) {
        shown.actor = read->user_by;
        shown.stamp = read->user_at;
    } else if (read->from == SETTING_FROM_BRAIN) {
        shown.actor = read->brain_by;
        shown.stamp = read->brain_at;
    }
    return shown;
}

static ActorParts actor_parts(const char *json)
{
    static const char k_null_tail[] = "\",\"id\":null}";
    static const char k_id_open[] = "\",\"id\":\"";
    ActorParts part;
    const char *kind = NULL;
    const char *end = NULL;
    size_t n = 0;

    memset(&part, 0, sizeof(part));
    if (json == NULL || !actor_json_is_canonical(json)) {
        return part;
    }
    /* Canonical JSON has a stored kind and either a null id or a closed string id. */
    kind = json + strlen("{\"kind\":\"");
    end = strchr(kind, '"');
    n = (size_t)(end - kind);
    memcpy(part.kind, kind, n);
    part.kind[n] = '\0';
    if (strcmp(end, k_null_tail) == 0) {
        part.ok = true;
        return part;
    }
    kind = end + (sizeof(k_id_open) - 1U);
    end = strchr(kind, '"');
    n = (size_t)(end - kind);
    memcpy(part.id, kind, n);
    part.id[n] = '\0';
    part.has_id = true;
    part.ok = true;
    return part;
}

/* February 29 only in a leap year. A shape-valid stamp can still be 31 Feb. */
// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
static int days_in_month(int year, int month)
{
    static const int k_days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int days = k_days[month];

    if (month == FEB && year % LEAP_EVERY == 0 &&
        (year % CENTURY != 0 || year % LEAP_CENTURY == 0)) {
        days = LEAP_DAY;
    }
    return days;
}
// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

static bool stamp_fields(const char *stamp, StampParts *part)
{
    int ms = 0;

    memset(part, 0, sizeof(*part));
    if (!utc_stamp_is_canonical(stamp)) {
        return false;
    }
    /* The shape check already required a digit in every numeric place. */
    // NOLINTNEXTLINE(bugprone-unchecked-string-to-number-conversion,cert-err34-c)
    if (sscanf(stamp, "%d-%d-%dT%d:%d:%d.%dZ", &part->year, &part->month, &part->day, &part->hour,
               &part->minute, &part->sec, &ms) != STAMP_FIELDS) {
        return false;
    }
    if (part->month < 1 || part->month > MONTH_MAX || part->day < 1 ||
        part->day > days_in_month(part->year, part->month) || part->hour > HOUR_MAX ||
        part->minute > MINUTE_MAX || part->sec > SECOND_MAX) {
        return false;
    }
    return true;
}

static bool when_of_stamp(const char *stamp, SettingWhen *when)
{
    StampParts part;
    struct tm utc;
    struct tm local;
    struct tm now_tm;
    time_t instant = 0;
    time_t now_sec = 0;

    memset(when, 0, sizeof(*when));
    if (!stamp_fields(stamp, &part)) {
        return false;
    }
    memset(&utc, 0, sizeof(utc));
    utc.tm_year = part.year - TM_EPOCH_YEAR;
    utc.tm_mon = part.month - 1;
    utc.tm_mday = part.day;
    utc.tm_hour = part.hour;
    utc.tm_min = part.minute;
    utc.tm_sec = part.sec;
    utc.tm_isdst = 0;
    when->year = part.year;
    when->month = part.month;
    when->day = part.day;
    when->hour = part.hour;
    when->minute = part.minute;
    when->year_now = part.year;
    when->utc_suffix = true;
    instant = timegm(&utc);
    if (instant != (time_t)-1 && localtime_r(&instant, &local) != NULL) {
        when->year = local.tm_year + TM_EPOCH_YEAR;
        when->month = local.tm_mon + 1;
        when->day = local.tm_mday;
        when->hour = local.tm_hour;
        when->minute = local.tm_min;
        when->utc_suffix = false;
    }
    now_sec = time(NULL);
    if (localtime_r(&now_sec, &now_tm) != NULL || gmtime_r(&now_sec, &now_tm) != NULL) {
        when->year_now = now_tm.tm_year + TM_EPOCH_YEAR;
    }
    return true;
}

static int fill_list_display(const char *json, char *out, size_t cap)
{
    size_t n = strlen(json);
    size_t i = 1;
    size_t pos = 0;
    bool any = false;

    if (cap == 0U) {
        return -1;
    }
    out[0] = '\0';
    if (n == 2U) {
        return 0;
    }
    while (i + 1U < n) {
        i++;
        if (any) {
            if (pos + 1U >= cap) {
                return -1;
            }
            out[pos++] = ' ';
        }
        any = true;
        while (json[i] != '"') {
            if (pos + 1U >= cap) {
                return -1;
            }
            out[pos++] = json[i];
            i++;
        }
        i++;
        if (json[i] == ',') {
            i++;
        }
    }
    out[pos] = '\0';
    return 0;
}

static int fill_display(SettingType type, const char *json, char *out, size_t cap)
{
    if (json == NULL || json[0] == '\0') {
        if (cap < (size_t)UNSET_CHARS) {
            return -1;
        }
        memcpy(out, "(unset)", (size_t)UNSET_CHARS);
        return 0;
    }
    if (type == SETTING_INTEGER) {
        size_t n = strlen(json);

        if (n >= cap) {
            return -1;
        }
        memcpy(out, json, n + 1U);
        return 0;
    }
    if (type == SETTING_STRING || type == SETTING_DURATION) {
        char inner[SETTING_VALUE_CAP];

        if (!quoted_plain(json, inner, sizeof(inner))) {
            return -1;
        }
        {
            size_t n = strlen(inner);

            if (n >= cap) {
                return -1;
            }
            memcpy(out, inner, n + 1U);
            return 0;
        }
    }
    return fill_list_display(json, out, cap);
}

static int write_raw_value(FILE *out, SettingType type, const char *json)
{
    char inner[SETTING_VALUE_CAP];
    long long ms = 0;

    if (json == NULL || json[0] == '\0') {
        return fputs("null", out) < 0 ? -1 : 0;
    }
    if (fputs(json, out) < 0) {
        return -1;
    }
    if (type != SETTING_DURATION) {
        return 0;
    }
    {
        char canon[NORM_MEASURE_CANON_CAP];

        if (!quoted_plain(json, inner, sizeof(inner)) ||
            !norm_duration_canonical(inner, &ms, canon, sizeof(canon))) {
            return -1;
        }
    }
    return fprintf(out, ",\"value_ms\":%lld", ms) < 0 ? -1 : 0;
}

static int write_layer(LayerOut layer)
{
    bool stored = false;
    bool who = false;
    bool dated = false;
    StampParts when;

    if (layer.live && layer.json != NULL && layer.json[0] != '\0') {
        stored = true;
    }
    if (stored && layer.actor != NULL && actor_json_is_canonical(layer.actor)) {
        who = true;
    }
    if (who && layer.stamp != NULL && stamp_fields(layer.stamp, &when)) {
        dated = true;
    }

    if (layer.comma && fputc(',', layer.out) == EOF) {
        return -1;
    }
    if (fprintf(layer.out, "\"%s\":", layer.name) < 0) {
        return -1;
    }
    if (!stored) {
        return fputs("null", layer.out) < 0 ? -1 : 0;
    }
    if (fputs("{\"value\":", layer.out) < 0 ||
        write_raw_value(layer.out, layer.type, layer.json) != 0) {
        return -1;
    }
    if (who) {
        if (fputs(",\"updated_by\":", layer.out) < 0 || fputs(layer.actor, layer.out) < 0) {
            return -1;
        }
        if (dated) {
            if (fputs(",\"updated_at\":", layer.out) < 0 ||
                output_json_string(layer.out, layer.stamp) != 0) {
                return -1;
            }
        }
    }
    return fputc('}', layer.out) == EOF ? -1 : 0;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — name, json, actor, stamp, in that order
static LayerOut layer_out(FILE *out, const char *name, SettingType type, const char *json,
                          const char *actor, const char *stamp, bool comma, bool live)
{
    LayerOut layer;

    memset(&layer, 0, sizeof(layer));
    layer.out = out;
    layer.name = name;
    layer.json = json;
    layer.actor = actor;
    layer.stamp = stamp;
    layer.type = type;
    layer.comma = comma;
    layer.live = live;
    return layer;
}

static const char *live_text(bool live, const char *text)
{
    if (live) {
        return text;
    }
    return NULL;
}

static int write_layers(FILE *out, const SettingSpec *spec, const SettingRead *read, GetOpts opt)
{
    char def[SETTING_VALUE_CAP];
    bool has_def = false;
    const char *def_json = NULL;

    has_def = setting_default_json(spec, def, sizeof(def));
    if (has_def) {
        def_json = def;
    }
    if (opt.scope_set) {
        bool live = false;
        const char *json = NULL;
        const char *actor = NULL;
        const char *stamp = NULL;
        const char *name = "brain";

        if (opt.scope == SETTING_LAYER_USER) {
            name = "user";
            if (spec->allow_user && read->user_live) {
                live = true;
                json = read->user_value;
                actor = read->user_by;
                stamp = read->user_at;
            }
        } else if (spec->allow_brain && read->brain_live) {
            live = true;
            json = read->brain_value;
            actor = read->brain_by;
            stamp = read->brain_at;
        }
        return write_layer(layer_out(out, name, spec->type, json, actor, stamp, false, live));
    }
    if (write_layer(layer_out(out, "default", spec->type, def_json, NULL, NULL, false, has_def)) !=
        0) {
        return -1;
    }
    if (spec->allow_user &&
        write_layer(layer_out(out, "user", spec->type, live_text(read->user_live, read->user_value),
                              live_text(read->user_live, read->user_by),
                              live_text(read->user_live, read->user_at), true, read->user_live)) !=
            0) {
        return -1;
    }
    if (spec->allow_brain &&
        write_layer(
            layer_out(out, "brain", spec->type, live_text(read->brain_live, read->brain_value),
                      live_text(read->brain_live, read->brain_by),
                      live_text(read->brain_live, read->brain_at), true, read->brain_live)) != 0) {
        return -1;
    }
    return 0;
}

static int write_setting_object(FILE *out, const SettingSpec *spec, const SettingRead *read,
                                Shown shown, GetOpts opt)
{
    const char *changed = "false";

    if (fputs("{\"setting\":{\"key\":", out) < 0 || output_json_string(out, spec->key) != 0) {
        return -1;
    }
    if (fprintf(out, ",\"type\":\"%s\",\"value\":", type_token(spec->type)) < 0 ||
        write_raw_value(out, spec->type, shown.json) != 0) {
        return -1;
    }
    if (shown.changed) {
        changed = "true";
    }
    if (fprintf(out, ",\"from\":\"%s\",\"changed\":%s,\"layers\":{", from_token(shown.from),
                changed) < 0 ||
        write_layers(out, spec, read, opt) != 0) {
        return -1;
    }
    /* layers, setting, then the data object. */
    return fputs("}}}", out) < 0 ? -1 : 0;
}

/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — actor then stamp, same order as the row */
static int write_from_line(FILE *out, SettingFrom from, const char *actor, const char *stamp)
{
    ActorParts who = actor_parts(actor);
    SettingWhen when;
    char date[DATE_TEXT_CAP];
    bool dated = false;

    if (who.ok && stamp != NULL && when_of_stamp(stamp, &when) &&
        setting_format_when(when, date, sizeof(date))) {
        dated = true;
    }
    if (fprintf(out, "  from %s", from_token(from)) < 0) {
        return -1;
    }
    if (who.ok) {
        if (fprintf(out, ", set by %s", who.kind) < 0) {
            return -1;
        }
        if (who.has_id && fprintf(out, " \"%s\"", who.id) < 0) {
            return -1;
        }
        if (dated && fprintf(out, " on %s", date) < 0) {
            return -1;
        }
    }
    return fputc('\n', out) == EOF ? -1 : 0;
}

static int append_other(char *buf, size_t cap, size_t *pos, bool *any, const char *name,
                        const char *display)
{
    const char *sep = "";
    int n = 0;

    if (*any) {
        sep = k_layer_sep;
    }
    n = snprintf(buf + *pos, cap - *pos, "%s%s: %s", sep, name, display);

    if (n < 0 || (size_t)n >= cap - *pos) {
        return -1;
    }
    *pos += (size_t)n;
    *any = true;
    return 0;
}

/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — json is the value, name is the layer */
static int write_one_other(const SettingSpec *spec, const char *json, const char *name, char *line,
                           size_t cap, size_t *pos, bool *any)
{
    char bit[SETTING_VALUE_CAP];

    if (fill_display(spec->type, json, bit, sizeof(bit)) != 0) {
        return -1;
    }
    return append_other(line, cap, pos, any, name, bit);
}

static int write_others(FILE *out, const SettingSpec *spec, const SettingRead *read,
                        SettingFrom winner)
{
    char def[SETTING_VALUE_CAP];
    char line[SETTING_VALUE_CAP * 3];
    size_t pos = 0;
    bool any = false;
    const char *json = NULL;

    line[0] = '\0';
    if (spec->allow_user && winner != SETTING_FROM_USER) {
        json = live_text(read->user_live, read->user_value);
        if (write_one_other(spec, json, "user", line, sizeof(line), &pos, &any) != 0) {
            return -1;
        }
    }
    if (spec->allow_brain && winner != SETTING_FROM_BRAIN) {
        json = live_text(read->brain_live, read->brain_value);
        if (write_one_other(spec, json, "brain", line, sizeof(line), &pos, &any) != 0) {
            return -1;
        }
    }
    if (winner != SETTING_FROM_DEFAULT) {
        json = NULL;
        if (setting_default_json(spec, def, sizeof(def))) {
            json = def;
        }
        if (write_one_other(spec, json, "default", line, sizeof(line), &pos, &any) != 0) {
            return -1;
        }
    }
    if (line[0] == '\0') {
        return 0;
    }
    return fprintf(out, "  %s\n", line) < 0 ? -1 : 0;
}

static int write_human(FILE *out, const SettingSpec *spec, const SettingRead *read, Shown shown,
                       GetOpts opt)
{
    char display[SETTING_VALUE_CAP];

    if (fill_display(spec->type, shown.json, display, sizeof(display)) != 0) {
        return -1;
    }
    if (opt.scope_set && shown.json == NULL) {
        return fputs("(unset)\n", out) < 0 ? -1 : 0;
    }
    if (opt.value_only) {
        return fprintf(out, "%s\n", display) < 0 ? -1 : 0;
    }
    if (fprintf(out, "%s = %s\n", spec->key, display) < 0 ||
        write_from_line(out, shown.from, shown.actor, shown.stamp) != 0) {
        return -1;
    }
    if (!opt.scope_set) {
        return write_others(out, spec, read, shown.from);
    }
    return 0;
}

static int fail_store(const char *err)
{
    const char *text = "internal error";

    if (err != NULL && err[0] != '\0') {
        text = err;
    }
    err_msg(code_for_store_text(text), text);
    return REMEMBER_ERR;
}

static int cmd_settings_get(Store *store, bool json, int argc, const char **argv)
{
    GetOpts opt;
    SettingRead read;
    Shown shown;
    const SettingSpec *spec = NULL;
    char userdb[REMEMBER_PATH_MAX];
    char err[STORE_ERR_CAP];
    SettingStatus st = SETTING_OK;
    int wrote = 0;

    memset(&opt, 0, sizeof(opt));
    if (parse_get(argc, argv, &opt) != 0) {
        return REMEMBER_ERR;
    }
    spec = setting_by_key(opt.key);
    if (spec == NULL) {
        return report_unknown(opt.key);
    }
    if (util_resolve_user_db_path(userdb, sizeof(userdb), err, sizeof(err)) != 0) {
        return fail_store(err);
    }
    st = store_setting_read(store, userdb, spec->key, NULL, &read, err, sizeof(err));
    if (st == SETTING_ERR_STORE) {
        return fail_store(err);
    }
    if (st != SETTING_OK) {
        err_msg(DIAG_INTERNAL, "internal error");
        return REMEMBER_ERR;
    }
    shown = shown_of(spec, &read, opt);
    if (!values_ok(spec, &read, shown)) {
        err_msg(DIAG_INTERNAL, "internal error");
        return REMEMBER_ERR;
    }
    if (json) {
        wrote = write_setting_object(app_out(), spec, &read, shown, opt);
    } else {
        wrote = write_human(app_out(), spec, &read, shown, opt);
    }
    if (wrote != 0) {
        err_msg(DIAG_INTERNAL, "failed to write output");
        return REMEMBER_ERR;
    }
    return REMEMBER_OK;
}

/* cppcheck-suppress staticFunction ; public API (commands.h) */
const char *cmd_settings_reply(int rest_argc, const char **rest_argv)
{
    if (rest_argc > 0 && rest_argv != NULL && strcmp(rest_argv[0], "get") == 0) {
        return "setting.get";
    }
    return "unknown";
}

int cmd_settings(Store *store, bool json, int rest_argc, const char **rest_argv)
{
    if (rest_argc < 1 || rest_argv == NULL || strcmp(rest_argv[0], "get") != 0) {
        err_msg(DIAG_USAGE, "settings takes 'get'");
        return REMEMBER_ERR;
    }
    return cmd_settings_get(store, json, rest_argc, rest_argv);
}
