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
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Last: under REMEMBER_TEST_HOOKS every malloc below goes through the shared
   fault hook (decision:plan18b-shared-alloc-hook). */
#ifdef REMEMBER_TEST_HOOKS
#include "test_alloc.h" // IWYU pragma: keep
#endif

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

static int write_setting_core(FILE *out, const SettingSpec *spec, const SettingRead *read,
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
    /* layers, then the setting object. The caller closes the data object. */
    return fputs("}}", out) < 0 ? -1 : 0;
}

static int write_setting_object(FILE *out, const SettingSpec *spec, const SettingRead *read,
                                Shown shown, GetOpts opt)
{
    if (write_setting_core(out, spec, read, shown, opt) != 0) {
        return -1;
    }
    return fputc('}', out) == EOF ? -1 : 0;
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

/* Field order keeps WriteOpts -Wpadded-clean: pointers, sizes, then the layer and flags. */
typedef struct {
    const char *key;
    const char *ns;
    const char **items;
    const char *actor_kind;
    const char *actor_id;
    size_t nitems;
    size_t items_cap;
    SettingLayer scope;
    bool scope_set;
    bool clear;
    bool yes;
    bool all;
} WriteOpts;

/* Stolen list rows. Brain sorts before user when the keys match. */
typedef struct {
    char *key;
    char *old_value;
    SettingLayer layer;
    bool changed;
    char pad_[3];
} ResetItem;

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
enum {
    PREPARE_ERR_CAP = 8192,
    BOUND_TEXT_CAP = 32,
    RESET_GROW = 8,
    MS_PER_MINUTE = 60 * MS_PER_SEC,
    MS_PER_HOUR = 60 * MS_PER_MINUTE
};
// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

static const char k_integer_fix[] =
    "Write it in plain digits, like 200, with no sign, leading zero, \"_\", or exponent.";
static const char k_duration_fix[] = "Use a number with ms, s, m, or h, like 800ms or 2s.";
static const char k_sub_ms_fix[] = "Use a whole number of milliseconds, like 800ms or 1.5s.";
static const char k_helper_fix[] =
    "Use a lowercase name that starts with a letter, up to 40 letters, digits, \"_\" or \"-\", "
    "like \"keychain\".";
static const char k_prefix_fix[] =
    "Use 1 to 32 letters, digits, \"-\", \"_\" or \".\", then one \":\", like \"project:\".";
static const char k_prefix_once[] = "List each prefix once.";
static const char k_brain_scope_fix[] =
    "Run the command again with --scope brain, or without --scope.";
static const char k_user_scope_fix[] =
    "Run the command again with --scope user, or without --scope.";
static const char k_value_quota_fix[] = "Use a shorter value.";
static const char k_user_quota_msg[] = "you have 256 user settings, the most remember can hold.";
static const char k_user_quota_fix[] =
    "Reset settings you don't need with 'remember settings reset --scope user'.";
static const char k_brain_quota_msg[] = "this brain has 256 settings, the most it can hold.";
static const char k_brain_quota_fix[] =
    "Reset settings you don't need with 'remember settings reset'.";
static const char k_hook_msg[] = "a hook or plugin can't change settings.";
static const char k_hook_fix[] = "Change them yourself in a terminal or in Settings.";
static const char k_and_quote[] = " and \"";

/* Sized text for a sentence that includes a key or a value. Call sites pass a literal. */
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wallocator-wrappers"
#endif
#ifdef __GNUC__
__attribute__((format(printf, 1, 2)))
#endif
static char *fmt_alloc(const char *fmt, ...)
{
    /* Same two-pass va_list as err_fmt. The caller frees the text. */
    /* NOLINTNEXTLINE(cppcoreguidelines-init-variables) */
    va_list vargs;
    int wrote = 0;
    int again = 0;
    char *buf = NULL;

    va_start(vargs, fmt);
    /* NOLINTNEXTLINE(clang-analyzer-valist.Uninitialized) -- array va_list mismatch */
    wrote = vsnprintf(NULL, 0, fmt, vargs);
    va_end(vargs);
    if (wrote < 0) {
        return NULL;
    }
    buf = (char *)malloc((size_t)wrote + 1U);
    if (buf == NULL) {
        return NULL;
    }
    va_start(vargs, fmt);
    /* NOLINTNEXTLINE(clang-analyzer-valist.Uninitialized) -- array va_list mismatch */
    again = vsnprintf(buf, (size_t)wrote + 1U, fmt, vargs);
    /* The length mismatch is libc, so va_end stays on the path that always runs. */
    va_end(vargs);
    if (again != wrote) {
        free(buf);
        return NULL;
    }
    return buf;
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif

static char *dup_text(const char *src)
{
    size_t n = 0;
    char *out = NULL;

    n = strlen(src);
    out = (char *)malloc(n + 1U);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, src, n + 1U);
    return out;
}

static int report_oom(void)
{
    err_msg(DIAG_OUT_OF_MEMORY, "out of memory");
    return REMEMBER_ERR;
}

static int emit_fix(DiagCode code, char *msg, const char *fix)
{
    if (msg == NULL) {
        return report_oom();
    }
    (void)diag_error_fix(code, msg, fix);
    free(msg);
    return REMEMBER_ERR;
}

static int emit_detail(DiagCode code, const char *msg, const char *fix,
                       const DiagDetailField *fields, size_t nfields)
{
    (void)diag_error_detail(code, msg, fix, fields, nfields);
    return REMEMBER_ERR;
}

/* ^[0-9]+(\.[0-9]+)?(ms|s|m|h)$ . A match that still fails canonical is sub-millisecond. */
static bool duration_shape(const char *text)
{
    size_t i = 0;

    if (text == NULL || text[0] < '0' || text[0] > '9') {
        return false;
    }
    while (text[i] >= '0' && text[i] <= '9') {
        i++;
    }
    if (text[i] == '.') {
        i++;
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
        while (text[i] >= '0' && text[i] <= '9') {
            i++;
        }
    }
    if (text[i] == 'm' && text[i + 1] == 's' && text[i + 2] == '\0') {
        return true;
    }
    if ((text[i] == 's' || text[i] == 'm' || text[i] == 'h') && text[i + 1] == '\0') {
        return true;
    }
    return false;
}

static int format_bound(const SettingSpec *spec, long long value, char *buf, size_t cap)
{
    long long unit = value;
    const char *suffix = "";
    int wrote = 0;

    if (spec->type != SETTING_INTEGER) {
        if (value % (long long)MS_PER_HOUR == 0 && value / (long long)MS_PER_HOUR != 0) {
            unit = value / (long long)MS_PER_HOUR;
            suffix = "h";
        } else if (value % (long long)MS_PER_MINUTE == 0 && value / (long long)MS_PER_MINUTE != 0) {
            unit = value / (long long)MS_PER_MINUTE;
            suffix = "m";
        } else if (value % (long long)MS_PER_SEC == 0 && value / (long long)MS_PER_SEC != 0) {
            unit = value / (long long)MS_PER_SEC;
            suffix = "s";
        } else {
            suffix = "ms";
        }
    }
    wrote = snprintf(buf, cap, "%lld%s", unit, suffix);
    if (wrote < 0 || (size_t)wrote >= cap) {
        return -1;
    }
    return 0;
}

static char *quoted_span(const char *text, const char **rest)
{
    const char *end = NULL;
    size_t n = 0;
    char *out = NULL;

    if (rest != NULL) {
        *rest = NULL;
    }
    if (text == NULL || text[0] != '"') {
        return NULL;
    }
    end = strchr(text + 1, '"');
    if (end == NULL) {
        return NULL;
    }
    n = (size_t)(end - (text + 1));
    out = (char *)malloc(n + 1U);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, text + 1, n);
    out[n] = '\0';
    if (rest != NULL) {
        *rest = end + 1;
    }
    return out;
}

static int report_prefix(const char *err)
{
    const char *rest = NULL;
    const char *fix = k_prefix_once;
    char *item = NULL;
    char *other = NULL;
    DiagDetailField fields[2];
    size_t nfields = 0;

    if (strstr(err, "isn't a bucket prefix") != NULL) {
        fix = k_prefix_fix;
    }
    item = quoted_span(err, &rest);
    if (item == NULL) {
        return emit_detail(DIAG_SETTING_INVALID_VALUE, err, fix, NULL, 0U);
    }
    fields[0].name = "item";
    fields[0].value = item;
    nfields = 1U;
    if (rest != NULL && strncmp(rest, k_and_quote, strlen(k_and_quote)) == 0) {
        other = quoted_span(rest + (strlen(k_and_quote) - 1U), NULL);
        if (other == NULL) {
            free(item);
            return emit_detail(DIAG_SETTING_INVALID_VALUE, err, fix, NULL, 0U);
        }
        fields[1].name = "other";
        fields[1].value = other;
        nfields = 2U;
    }
    (void)diag_error_detail(DIAG_SETTING_INVALID_VALUE, err, fix, fields, nfields);
    free(item);
    free(other);
    return REMEMBER_ERR;
}

static int report_scalar_invalid(const SettingSpec *spec, const char *value)
{
    char *msg = NULL;

    if (spec->type == SETTING_DURATION && duration_shape(value)) {
        msg =
            fmt_alloc("\"%s\" can't be \"%s\": a duration must be a whole number of milliseconds.",
                      spec->key, value);
        return emit_fix(DIAG_SETTING_INVALID_VALUE, msg, k_sub_ms_fix);
    }
    if (spec->type == SETTING_DURATION) {
        msg = fmt_alloc("\"%s\" needs a duration with a unit; you gave \"%s\".", spec->key, value);
        return emit_fix(DIAG_SETTING_INVALID_VALUE, msg, k_duration_fix);
    }
    if (spec->type == SETTING_INTEGER) {
        msg = fmt_alloc("\"%s\" needs a whole number; you gave \"%s\".", spec->key, value);
        return emit_fix(DIAG_SETTING_INVALID_VALUE, msg, k_integer_fix);
    }
    if (spec->type == SETTING_STRING) {
        msg = fmt_alloc("\"%s\" isn't a helper id.", value);
        return emit_fix(DIAG_SETTING_INVALID_VALUE, msg, k_helper_fix);
    }
    /* string_list errors carry a sentence and are reported before this. */
    return REMEMBER_ERR;
}

static int report_invalid(const SettingSpec *spec, const WriteOpts *opt, const char *err)
{
    if (err != NULL && err[0] != '\0') {
        return report_prefix(err);
    }
    /* C-42-style drafts (decision:plan20-scalar-shape-draft). */
    if (opt->clear) {
        char *fix = fmt_alloc("Set a value, or run 'remember settings reset %s'.", spec->key);
        char *msg = fmt_alloc("\"%s\" isn't a list, so --clear can't empty it.", spec->key);

        if (fix == NULL) {
            free(msg);
            return report_oom();
        }
        (void)emit_fix(DIAG_SETTING_INVALID_VALUE, msg, fix);
        free(fix);
        return REMEMBER_ERR;
    }
    if (opt->nitems > 1U) {
        char *msg = fmt_alloc("\"%s\" takes one value; you gave %zu.", spec->key, opt->nitems);

        return emit_fix(DIAG_SETTING_INVALID_VALUE, msg, "Give one value.");
    }
    if (opt->nitems != 1U || opt->items == NULL || opt->items[0] == NULL) {
        err_msg(DIAG_INTERNAL, "internal error");
        return REMEMBER_ERR;
    }
    return report_scalar_invalid(spec, opt->items[0]);
}

static int report_range(const SettingSpec *spec, const WriteOpts *opt)
{
    char lower[BOUND_TEXT_CAP];
    char upper[BOUND_TEXT_CAP];
    char *msg = NULL;
    char *fix = NULL;

    if (opt->nitems != 1U || opt->items == NULL || opt->items[0] == NULL) {
        err_msg(DIAG_INTERNAL, "internal error");
        return REMEMBER_ERR;
    }
    if (format_bound(spec, spec->min_value, lower, sizeof(lower)) != 0 ||
        format_bound(spec, spec->max_value, upper, sizeof(upper)) != 0) {
        err_msg(DIAG_INTERNAL, "internal error");
        return REMEMBER_ERR;
    }
    msg = fmt_alloc("\"%s\" can be %s to %s; you gave %s.", spec->key, lower, upper, opt->items[0]);
    fix = fmt_alloc("Set a value from %s to %s.", lower, upper);
    if (msg == NULL || fix == NULL) {
        free(msg);
        free(fix);
        return report_oom();
    }
    {
        /* String members, drafts (decision:plan20-error-detail-strings). */
        const DiagDetailField fields[] = {{.name = "key", .value = spec->key},
                                          {.name = "value", .value = opt->items[0]},
                                          {.name = "min", .value = lower},
                                          {.name = "max", .value = upper}};

        (void)emit_detail(DIAG_SETTING_OUT_OF_RANGE, msg, fix, fields,
                          sizeof(fields) / sizeof(fields[0]));
    }
    free(msg);
    free(fix);
    return REMEMBER_ERR;
}

/* detail {key, allowed}: the one layer the key allows (decision:plan20-error-detail-strings). */
static int emit_scope(const SettingSpec *spec, char *msg, const char *fix, const char *allowed)
{
    const DiagDetailField fields[] = {{.name = "key", .value = spec->key},
                                      {.name = "allowed", .value = allowed}};

    if (msg == NULL) {
        return report_oom();
    }
    (void)emit_detail(DIAG_SETTING_SCOPE_MISMATCH, msg, fix, fields,
                      sizeof(fields) / sizeof(fields[0]));
    free(msg);
    return REMEMBER_ERR;
}

static int report_scope(const SettingSpec *spec, const WriteOpts *opt)
{
    SettingLayer requested = SETTING_LAYER_AUTO;

    if (opt->scope_set) {
        requested = opt->scope;
    }
    if (requested == SETTING_LAYER_USER && !spec->allow_user) {
        char *msg =
            fmt_alloc("\"%s\" is a brain setting; it can't be set for your user.", spec->key);

        return emit_scope(spec, msg, k_brain_scope_fix, "brain");
    }
    if (requested == SETTING_LAYER_BRAIN && !spec->allow_brain) {
        /* C-42-style draft (decision:plan20-user-only-scope-draft). */
        char *msg =
            fmt_alloc("\"%s\" is a user setting; it can't be set for one brain.", spec->key);

        return emit_scope(spec, msg, k_user_scope_fix, "user");
    }
    err_msg(DIAG_INTERNAL, "internal error");
    return REMEMBER_ERR;
}

static int report_quota(const char *msg, const char *fix)
{
    DiagDetailField field;

    field.name = "quota";
    field.value = "settings_limit";
    return emit_detail(DIAG_QUOTA_EXCEEDED, msg, fix, &field, 1U);
}

static int report_value_quota(const char *key)
{
    char *msg = NULL;

    msg = fmt_alloc("the value for \"%s\" is over 16 KiB, the most a setting can hold.", key);
    if (msg == NULL) {
        return report_oom();
    }
    (void)report_quota(msg, k_value_quota_fix);
    free(msg);
    return REMEMBER_ERR;
}

static int report_row_quota(SettingLayer layer)
{
    if (layer == SETTING_LAYER_USER) {
        return report_quota(k_user_quota_msg, k_user_quota_fix);
    }
    if (layer == SETTING_LAYER_BRAIN) {
        return report_quota(k_brain_quota_msg, k_brain_quota_fix);
    }
    err_msg(DIAG_INTERNAL, "internal error");
    return REMEMBER_ERR;
}

static int report_prepare(SettingStatus st, const SettingSpec *spec, const WriteOpts *opt,
                          const char *err)
{
    if (spec == NULL) {
        err_msg(DIAG_INTERNAL, "internal error");
        return REMEMBER_ERR;
    }
    if (st == SETTING_ERR_INVALID) {
        return report_invalid(spec, opt, err);
    }
    if (st == SETTING_ERR_RANGE) {
        return report_range(spec, opt);
    }
    if (st == SETTING_ERR_SCOPE) {
        return report_scope(spec, opt);
    }
    if (st == SETTING_ERR_QUOTA) {
        return report_value_quota(spec->key);
    }
    return fail_store(err);
}

/* A hook is rejected after a clean parse and before actor JSON, so --as does not
   become the unwritten invalid_argument line. Usage stays usage. */
static int guard_actor(const WriteOpts *opt, char *actor_buf, const char **updated_by)
{
    CmdActorFlags flags;

    *updated_by = NULL;
    if (hook_depth_blocks()) {
        /* detail {key, hook}; key only when one was typed (decision:plan20-error-detail-strings).
         */
        DiagDetailField fields[2];
        size_t n = 0;

        if (opt->key != NULL) {
            fields[n++] = (DiagDetailField){.name = "key", .value = opt->key};
        }
        fields[n++] = (DiagDetailField){.name = "hook", .value = getenv("REMEMBER_HOOK_DEPTH")};
        return emit_detail(DIAG_SETTING_FROM_HOOK, k_hook_msg, k_hook_fix, fields, n);
    }
    if (opt->actor_kind == NULL && opt->actor_id == NULL) {
        return 0;
    }
    memset(&flags, 0, sizeof(flags));
    flags.kind = opt->actor_kind;
    flags.id = opt->actor_id;
    if (cmd_actor_json(&flags, actor_buf) != 0) {
        return REMEMBER_ERR;
    }
    *updated_by = actor_buf;
    return 0;
}

static int take_write_scope(int *index, int argc, const char **argv, WriteOpts *opt)
{
    GetOpts scope;

    memset(&scope, 0, sizeof(scope));
    scope.scope = opt->scope;
    scope.scope_set = opt->scope_set;
    if (take_scope(index, argc, argv, &scope) != 0) {
        return -1;
    }
    opt->scope = scope.scope;
    opt->scope_set = scope.scope_set;
    return 0;
}

static int take_write_actor(int *index, int argc, const char **argv, WriteOpts *opt)
{
    CmdActorFlags flags;
    const char *err = NULL;
    int took = 0;

    if (strcmp(argv[*index], "--as") == 0 && opt->actor_kind != NULL) {
        err_msg(DIAG_USAGE, "duplicate --as");
        return -1;
    }
    if (strcmp(argv[*index], "--actor") == 0 && opt->actor_id != NULL) {
        err_msg(DIAG_USAGE, "duplicate --actor");
        return -1;
    }
    memset(&flags, 0, sizeof(flags));
    flags.kind = opt->actor_kind;
    flags.id = opt->actor_id;
    took = cmd_take_write_flag(argv[*index], index, argc, argv, &flags, &err);
    if (took < 0) {
        err_msg(DIAG_USAGE, err != NULL ? err : "internal error");
        return -1;
    }
    opt->actor_kind = flags.kind;
    opt->actor_id = flags.id;
    return 0;
}

/* 1: this token was an option. 0: it is positional. -1: usage was already printed. */
static int take_set_option(int *index, int argc, const char **argv, WriteOpts *opt, bool *end_opts)
{
    const char *arg = argv[*index];

    if (*end_opts) {
        return 0;
    }
    if (strcmp(arg, "--") == 0) {
        *end_opts = true;
        return 1;
    }
    if (strcmp(arg, "--scope") == 0) {
        if (take_write_scope(index, argc, argv, opt) != 0) {
            return -1;
        }
        return 1;
    }
    if (strcmp(arg, "--clear") == 0) {
        if (opt->clear) {
            err_msg(DIAG_USAGE, "duplicate --clear");
            return -1;
        }
        opt->clear = true;
        return 1;
    }
    if (strcmp(arg, "--as") == 0 || strcmp(arg, "--actor") == 0 || cmd_is_hooks_flag(arg)) {
        if (take_write_actor(index, argc, argv, opt) != 0) {
            return -1;
        }
        return 1;
    }
    if (arg[0] == '-' && arg[1] != '\0') {
        err_fmt(DIAG_USAGE, "unknown option '%s'", arg);
        return -1;
    }
    return 0;
}

static int reject_set_shape(const WriteOpts *opt)
{
    if (opt->key == NULL) {
        err_msg(DIAG_USAGE, "settings set requires a key");
        return -1;
    }
    if (opt->clear && opt->nitems > 0U) {
        err_msg(DIAG_USAGE, "cannot combine a value with --clear");
        return -1;
    }
    if (!opt->clear && opt->nitems == 0U) {
        err_msg(DIAG_USAGE, "settings set requires a value");
        return -1;
    }
    return 0;
}

static int parse_set(int argc, const char **argv, WriteOpts *opt)
{
    int i = 0;
    bool end_opts = false;

    for (i = 1; i < argc; i++) {
        int took = take_set_option(&i, argc, argv, opt, &end_opts);

        if (took < 0) {
            return -1;
        }
        if (took > 0) {
            continue;
        }
        if (opt->key == NULL) {
            opt->key = argv[i];
            continue;
        }
        if (push_cstr_ptr(&opt->items, &opt->nitems, &opt->items_cap, argv[i]) != 0) {
            err_msg(DIAG_OUT_OF_MEMORY, "out of memory");
            return REMEMBER_ERR;
        }
    }
    return reject_set_shape(opt);
}

static int take_namespace(int *index, int argc, const char **argv, WriteOpts *opt)
{
    if (opt->ns != NULL) {
        err_msg(DIAG_USAGE, "duplicate --namespace");
        return -1;
    }
    if (*index + 1 >= argc) {
        err_msg(DIAG_USAGE, "missing value for --namespace");
        return -1;
    }
    /* A flag is not a namespace: `--namespace --yes` would match nothing. */
    if (argv[*index + 1][0] == '-') {
        err_msg(DIAG_USAGE, "missing value for --namespace");
        return -1;
    }
    *index += 1;
    opt->ns = argv[*index];
    return 0;
}

/* 1: this token was an option. 0: it is positional. -1: usage was already printed. */
static int take_reset_option(int *index, int argc, const char **argv, WriteOpts *opt,
                             bool *end_opts)
{
    const char *arg = argv[*index];

    if (*end_opts) {
        return 0;
    }
    if (strcmp(arg, "--") == 0) {
        *end_opts = true;
        return 1;
    }
    if (strcmp(arg, "--scope") == 0) {
        if (take_write_scope(index, argc, argv, opt) != 0) {
            return -1;
        }
        return 1;
    }
    if (strcmp(arg, "--namespace") == 0) {
        if (take_namespace(index, argc, argv, opt) != 0) {
            return -1;
        }
        return 1;
    }
    if (strcmp(arg, "--yes") == 0) {
        if (opt->yes) {
            err_msg(DIAG_USAGE, "duplicate --yes");
            return -1;
        }
        opt->yes = true;
        return 1;
    }
    if (strcmp(arg, "--all") == 0) {
        if (opt->all) {
            err_msg(DIAG_USAGE, "duplicate --all");
            return -1;
        }
        opt->all = true;
        return 1;
    }
    if (strcmp(arg, "--as") == 0 || strcmp(arg, "--actor") == 0 || cmd_is_hooks_flag(arg)) {
        if (take_write_actor(index, argc, argv, opt) != 0) {
            return -1;
        }
        return 1;
    }
    if (arg[0] == '-' && arg[1] != '\0') {
        err_fmt(DIAG_USAGE, "unknown option '%s'", arg);
        return -1;
    }
    return 0;
}

static int parse_reset(int argc, const char **argv, WriteOpts *opt)
{
    int i = 0;
    bool end_opts = false;

    for (i = 1; i < argc; i++) {
        int took = take_reset_option(&i, argc, argv, opt, &end_opts);

        if (took < 0) {
            return -1;
        }
        if (took > 0) {
            continue;
        }
        if (opt->key != NULL) {
            err_msg(DIAG_USAGE, "settings reset takes one key");
            return -1;
        }
        opt->key = argv[i];
    }
    return 0;
}

/* Checked after the flags parse, in this order, so the first clash wins. */
static int reset_forms(const WriteOpts *opt)
{
    if (opt->all && opt->key != NULL) {
        err_msg(DIAG_USAGE, "cannot combine --all with a key");
        return -1;
    }
    if (opt->ns != NULL && opt->key != NULL) {
        err_msg(DIAG_USAGE, "cannot combine --namespace with a key");
        return -1;
    }
    if (opt->all && opt->ns != NULL) {
        err_msg(DIAG_USAGE, "cannot combine --all and --namespace");
        return -1;
    }
    if (opt->all && !opt->scope_set) {
        err_msg(DIAG_USAGE, "reset --all requires --scope");
        return -1;
    }
    if (opt->yes && opt->key != NULL) {
        err_msg(DIAG_USAGE, "cannot combine --yes with a key");
        return -1;
    }
    if (opt->key == NULL && !opt->all && opt->ns == NULL) {
        err_msg(DIAG_USAGE, "settings reset requires a key");
        return -1;
    }
    return 0;
}

static const char *scope_name(SettingLayer layer)
{
    if (layer == SETTING_LAYER_BRAIN) {
        return "brain";
    }
    return "user";
}

static const char *action_name_of(SettingLayer layer, bool changed)
{
    if (!changed) {
        return "none";
    }
    if (layer == SETTING_LAYER_BRAIN) {
        return "tombstone";
    }
    return "delete";
}

static int write_old_json(FILE *out, const char *old)
{
    if (old == NULL || old[0] == '\0') {
        return fputs("null", out) < 0 ? -1 : 0;
    }
    return fputs(old, out) < 0 ? -1 : 0;
}

static int write_tail(FILE *out, SettingLayer layer, const char *old, bool changed,
                      const char *action)
{
    const char *flag = "false";

    if (changed) {
        flag = "true";
    }

    if (fprintf(out, ",\"scope\":\"%s\",\"old_value\":", scope_name(layer)) < 0 ||
        write_old_json(out, old) != 0 || fprintf(out, ",\"changed\":%s", flag) < 0) {
        return -1;
    }
    if (action != NULL && fprintf(out, ",\"action\":\"%s\"", action) < 0) {
        return -1;
    }
    return fputc('}', out) == EOF ? -1 : 0;
}

static int copy_live(const SettingRead *read, SettingLayer layer, char **out)
{
    bool live = false;
    const char *text = NULL;

    *out = NULL;
    if (layer == SETTING_LAYER_BRAIN) {
        live = read->brain_live;
        text = read->brain_value;
    } else if (layer == SETTING_LAYER_USER) {
        live = read->user_live;
        text = read->user_value;
    }
    if (!live || text == NULL || text[0] == '\0') {
        return 0;
    }
    *out = dup_text(text);
    if (*out == NULL) {
        return -1;
    }
    return 0;
}

static int read_spec(Store *store, const char *userdb, const SettingSpec *spec, SettingRead *read,
                     char *err, size_t errlen)
{
    SettingStatus st = SETTING_OK;

    st = store_setting_read(store, userdb, spec->key, NULL, read, err, errlen);
    if (st == SETTING_OK) {
        return 0;
    }
    if (st == SETTING_ERR_STORE) {
        return fail_store(err);
    }
    err_msg(DIAG_INTERNAL, "internal error");
    return REMEMBER_ERR;
}

static int write_set_result(FILE *out, const SettingSpec *spec, const SettingRead *read,
                            SettingLayer layer, const char *old, bool changed)
{
    GetOpts shown_opt;
    Shown shown;

    memset(&shown_opt, 0, sizeof(shown_opt));
    shown = shown_of(spec, read, shown_opt);
    if (!values_ok(spec, read, shown)) {
        return -1;
    }
    if (write_setting_core(out, spec, read, shown, shown_opt) != 0) {
        return -1;
    }
    return write_tail(out, layer, old, changed, NULL);
}

static int apply_set(Store *store, bool json, const WriteOpts *opt)
{
    const SettingSpec *spec = NULL;
    SettingLayer chosen = SETTING_LAYER_AUTO;
    SettingLayer requested = SETTING_LAYER_AUTO;
    SettingWriteResult wrote;
    SettingWrite input;
    SettingRead read;
    char userdb[REMEMBER_PATH_MAX];
    char err[PREPARE_ERR_CAP];
    char prepared[SETTING_VALUE_CAP];
    char actor_buf[ACTOR_JSON_CAP];
    const char *updated_by = NULL;
    char *old = NULL;
    SettingStatus st = SETTING_OK;
    int rc = REMEMBER_ERR;

    memset(&wrote, 0, sizeof(wrote));
    memset(&input, 0, sizeof(input));
    memset(&read, 0, sizeof(read));
    err[0] = '\0';
    if (guard_actor(opt, actor_buf, &updated_by) != 0) {
        return REMEMBER_ERR;
    }
    if (util_resolve_user_db_path(userdb, sizeof(userdb), err, sizeof(err)) != 0) {
        return fail_store(err);
    }
    spec = setting_by_key(opt->key);
    if (spec == NULL) {
        return report_unknown(opt->key);
    }
    if (opt->scope_set) {
        requested = opt->scope;
    }
    st = setting_prepare(opt->key, opt->items, opt->nitems, requested, prepared, sizeof(prepared),
                         &spec, &chosen, err, sizeof(err));
    if (st != SETTING_OK) {
        return report_prepare(st, spec, opt, err);
    }
    if (read_spec(store, userdb, spec, &read, err, sizeof(err)) != 0) {
        return REMEMBER_ERR;
    }
    if (copy_live(&read, chosen, &old) != 0) {
        return report_oom();
    }
    input.key = spec->key;
    input.items = opt->items;
    input.updated_by = updated_by;
    input.nitems = opt->nitems;
    input.layer = requested;
    err[0] = '\0';
    st = store_setting_set(store, userdb, &input, &wrote, err, sizeof(err));
    if (st == SETTING_ERR_QUOTA) {
        rc = report_row_quota(chosen);
        goto done;
    }
    if (st != SETTING_OK) {
        rc = fail_store(err);
        goto done;
    }
    if (read_spec(store, userdb, spec, &read, err, sizeof(err)) != 0) {
        goto done;
    }
    rc = REMEMBER_OK;
    if (json && write_set_result(app_out(), spec, &read, wrote.layer, old, wrote.changed) != 0) {
        err_msg(DIAG_INTERNAL, "failed to write output");
        rc = REMEMBER_ERR;
    }
done:
    free(old);
    return rc;
}

static int cmd_settings_set(Store *store, bool json, int argc, const char **argv)
{
    WriteOpts opt;
    int rc = 0;

    memset(&opt, 0, sizeof(opt));
    if (parse_set(argc, argv, &opt) != 0) {
        free((void *)opt.items);
        return REMEMBER_ERR;
    }
    rc = apply_set(store, json, &opt);
    free((void *)opt.items);
    return rc;
}

static bool ns_match(const char *key, const char *ns)
{
    size_t nlen = 0;

    if (ns == NULL) {
        return true;
    }
    if (key == NULL) {
        return false;
    }
    nlen = strlen(ns);
    if (strncmp(key, ns, nlen) != 0) {
        return false;
    }
    if (key[nlen] == '\0' || key[nlen] == '.') {
        return true;
    }
    return false;
}

/* qsort passes the two element slots. The caller cannot swap the parameters. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static int cmp_reset_item(const void *left, const void *right)
{
    const ResetItem *a = (const ResetItem *)left;
    const ResetItem *b = (const ResetItem *)right;
    int cmp = 0;

    cmp = strcmp(a->key, b->key);
    if (cmp != 0) {
        return cmp;
    }
    if (a->layer == b->layer || a->layer == SETTING_LAYER_BRAIN) {
        return a->layer == b->layer ? 0 : -1;
    }
    return 1;
}

static void reset_items_free(ResetItem *items, size_t n)
{
    size_t i = 0;

    if (items == NULL) {
        return;
    }
    for (i = 0; i < n; i++) {
        free(items[i].key);
        free(items[i].old_value);
    }
    free(items);
}

static int grow_reset(ResetItem **items, const size_t *n, size_t *cap)
{
    ResetItem *grown = NULL;
    size_t next = 0;

    if (*n < *cap) {
        return 0;
    }
    if (*cap > SIZE_MAX / 2U / sizeof(**items)) {
        return -1;
    }
    next = (*cap == 0U) ? (size_t)RESET_GROW : (*cap * 2U);
    grown = (ResetItem *)realloc(*items, next * sizeof(*grown));
    if (grown == NULL) {
        return -1;
    }
    *items = grown;
    *cap = next;
    return 0;
}

static int append_live(SettingStoredRow *rows, size_t nrows, const char *ns, SettingLayer layer,
                       ResetItem **items, size_t *n, size_t *cap)
{
    size_t i = 0;

    for (i = 0; i < nrows; i++) {
        ResetItem *slot = NULL;

        if (!ns_match(rows[i].key, ns)) {
            continue;
        }
        if (grow_reset(items, n, cap) != 0) {
            return -1;
        }
        slot = &(*items)[*n];
        slot->key = rows[i].key;
        slot->old_value = rows[i].value;
        slot->layer = layer;
        slot->changed = false;
        rows[i].key = NULL;
        rows[i].value = NULL;
        *n += 1U;
    }
    return 0;
}

static int collect_layer(Store *store, const char *userdb, SettingLayer layer, const char *ns,
                         ResetItem **items, size_t *n, size_t *cap, char *err, size_t errlen)
{
    SettingStoredRow *rows = NULL;
    size_t nrows = 0;
    SettingStatus st = SETTING_OK;
    int rc = 0;

    st = store_setting_list_live(store, userdb, layer, &rows, &nrows, err, errlen);
    if (st != SETTING_OK) {
        return -1;
    }
    rc = append_live(rows, nrows, ns, layer, items, n, cap);
    store_setting_rows_free(rows, nrows);
    return rc;
}

static size_t unique_keys(const ResetItem *items, size_t n)
{
    size_t i = 0;
    size_t count = 0;

    for (i = 0; i < n; i++) {
        if (i == 0U || strcmp(items[i].key, items[i - 1U].key) != 0) {
            count++;
        }
    }
    return count;
}

static int add_size(size_t *n, size_t more)
{
    if (*n > SIZE_MAX - more) {
        return -1;
    }
    *n += more;
    return 0;
}

static char *settings_command(int argc, const char **argv)
{
    const char *head = "remember settings";
    const char *tail = " --yes";
    size_t n = 0;
    size_t pos = 0;
    int i = 0;
    char *buf = NULL;

    n = strlen(head);
    if (add_size(&n, strlen(tail)) != 0) {
        return NULL;
    }
    for (i = 0; i < argc; i++) {
        if (add_size(&n, 1U) != 0 || add_size(&n, strlen(argv[i])) != 0) {
            return NULL;
        }
    }
    buf = (char *)malloc(n + 1U);
    if (buf == NULL) {
        return NULL;
    }
    memcpy(buf, head, strlen(head));
    pos = strlen(head);
    for (i = 0; i < argc; i++) {
        size_t len = strlen(argv[i]);

        buf[pos++] = ' ';
        memcpy(buf + pos, argv[i], len);
        pos += len;
    }
    memcpy(buf + pos, tail, strlen(tail) + 1U);
    return buf;
}

static int report_confirm(int argc, const char **argv, size_t count)
{
    char *command = NULL;
    char *msg = NULL;
    char *fix = NULL;

    command = settings_command(argc, argv);
    /* "1 setting" is a C-42-style draft of C-11 (decision:plan20-reset-count-zero-one). */
    msg = command != NULL
              ? fmt_alloc("this resets %zu %s.", count, count == 1U ? "setting" : "settings")
              : NULL;
    fix = (command != NULL && msg != NULL) ? fmt_alloc("Run '%s' to confirm.", command) : NULL;
    free(command);
    if (msg == NULL || fix == NULL) {
        free(msg);
        free(fix);
        return report_oom();
    }
    (void)diag_error_fix(DIAG_CONFIRMATION_REQUIRED, msg, fix);
    free(msg);
    free(fix);
    return REMEMBER_ERR;
}

static int write_c10(const char *key, const char *display)
{
    char *line = NULL;
    int wrote = 0;

    line = fmt_alloc("\"%s\" was reset. It's now %s.\n", key, display);
    if (line == NULL) {
        return report_oom();
    }
    wrote = fputs(line, app_out());
    free(line);
    if (wrote < 0) {
        err_msg(DIAG_INTERNAL, "failed to write output");
        return REMEMBER_ERR;
    }
    return REMEMBER_OK;
}

static int c10_display(const SettingSpec *spec, const SettingRead *read, bool scope_set, char *buf,
                       size_t cap)
{
    GetOpts shown_opt;
    Shown shown;

    if (spec == NULL || scope_set) {
        if (cap < (size_t)UNSET_CHARS) {
            return -1;
        }
        memcpy(buf, "(unset)", (size_t)UNSET_CHARS);
        return 0;
    }
    memset(&shown_opt, 0, sizeof(shown_opt));
    shown = shown_of(spec, read, shown_opt);
    return fill_display(spec->type, shown.json, buf, cap);
}

static int write_unregistered(FILE *out, const ResetItem *item)
{
    const char *scope = scope_name(item->layer);
    const char *action = action_name_of(item->layer, item->changed);
    const char *flag = "false";

    if (item->changed) {
        flag = "true";
    }

    if (fputs("{\"setting\":{\"key\":", out) < 0 || output_json_string(out, item->key) != 0) {
        return -1;
    }
    if (fprintf(out,
                ",\"type\":null,\"value\":null,\"from\":\"default\",\"changed\":false,"
                "\"layers\":{\"%s\":null}}",
                scope) < 0) {
        return -1;
    }
    if (fprintf(out, ",\"scope\":\"%s\",\"old_value\":", scope) < 0 ||
        write_old_json(out, item->old_value) != 0) {
        return -1;
    }
    if (fprintf(out, ",\"changed\":%s,\"action\":\"%s\"}", flag, action) < 0) {
        return -1;
    }
    return 0;
}

static int write_registered_reset(FILE *out, const SettingSpec *spec, const SettingRead *read,
                                  SettingLayer layer, const char *old, bool changed)
{
    GetOpts shown_opt;
    Shown shown;

    memset(&shown_opt, 0, sizeof(shown_opt));
    shown = shown_of(spec, read, shown_opt);
    if (!values_ok(spec, read, shown) ||
        write_setting_core(out, spec, read, shown, shown_opt) != 0) {
        return -1;
    }
    return write_tail(out, layer, old, changed, action_name_of(layer, changed));
}

static int emit_reset_item(Store *store, const char *userdb, bool json, bool scope_set,
                           const ResetItem *item, char *err, size_t errlen)
{
    const SettingSpec *spec = setting_by_key(item->key);
    SettingRead read;
    char display[SETTING_VALUE_CAP];

    if (spec == NULL) {
        if (json) {
            if (write_unregistered(app_out(), item) != 0) {
                err_msg(DIAG_INTERNAL, "failed to write output");
                return REMEMBER_ERR;
            }
            return REMEMBER_OK;
        }
        return write_c10(item->key, "(unset)");
    }
    memset(&read, 0, sizeof(read));
    if (read_spec(store, userdb, spec, &read, err, errlen) != 0) {
        return REMEMBER_ERR;
    }
    if (json) {
        if (write_registered_reset(app_out(), spec, &read, item->layer, item->old_value,
                                   item->changed) != 0) {
            err_msg(DIAG_INTERNAL, "failed to write output");
            return REMEMBER_ERR;
        }
        return REMEMBER_OK;
    }
    if (c10_display(spec, &read, scope_set, display, sizeof(display)) != 0) {
        err_msg(DIAG_INTERNAL, "internal error");
        return REMEMBER_ERR;
    }
    return write_c10(spec->key, display);
}

static int emit_reset_batch(Store *store, const char *userdb, bool json, bool scope_set,
                            const ResetItem *items, size_t n, char *err, size_t errlen)
{
    size_t i = 0;
    FILE *out = app_out();

    if (json && fprintf(out, "{\"count\":%zu,\"settings\":[", n) < 0) {
        err_msg(DIAG_INTERNAL, "failed to write output");
        return REMEMBER_ERR;
    }
    for (i = 0; i < n; i++) {
        if (json && i > 0U && fputc(',', out) == EOF) {
            err_msg(DIAG_INTERNAL, "failed to write output");
            return REMEMBER_ERR;
        }
        if (emit_reset_item(store, userdb, json, scope_set, &items[i], err, errlen) != 0) {
            return REMEMBER_ERR;
        }
    }
    if (json && fputs("]}", out) < 0) {
        err_msg(DIAG_INTERNAL, "failed to write output");
        return REMEMBER_ERR;
    }
    return REMEMBER_OK;
}

static int apply_reset_one(Store *store, bool json, const char *userdb, const WriteOpts *opt,
                           const char *updated_by)
{
    const SettingSpec *spec = NULL;
    SettingLayer chosen = SETTING_LAYER_AUTO;
    SettingLayer requested = SETTING_LAYER_AUTO;
    SettingWriteResult wrote;
    SettingRead read;
    char err[PREPARE_ERR_CAP];
    char *old = NULL;
    SettingStatus st = SETTING_OK;
    int rc = REMEMBER_ERR;

    memset(&wrote, 0, sizeof(wrote));
    memset(&read, 0, sizeof(read));
    err[0] = '\0';
    spec = setting_by_key(opt->key);
    if (spec == NULL) {
        return report_unknown(opt->key);
    }
    if (opt->scope_set) {
        requested = opt->scope;
    }
    st = setting_choose_layer(opt->key, requested, &spec, &chosen, err, sizeof(err));
    if (st != SETTING_OK) {
        return report_prepare(st, spec, opt, err);
    }
    if (read_spec(store, userdb, spec, &read, err, sizeof(err)) != 0) {
        return REMEMBER_ERR;
    }
    if (copy_live(&read, chosen, &old) != 0) {
        return report_oom();
    }
    err[0] = '\0';
    st = store_setting_reset(store, userdb, spec->key, requested, updated_by, &wrote, err,
                             sizeof(err));
    if (st != SETTING_OK) {
        free(old);
        return fail_store(err);
    }
    if (read_spec(store, userdb, spec, &read, err, sizeof(err)) != 0) {
        free(old);
        return REMEMBER_ERR;
    }
    if (json) {
        if (write_registered_reset(app_out(), spec, &read, wrote.layer, old, wrote.changed) != 0) {
            err_msg(DIAG_INTERNAL, "failed to write output");
            rc = REMEMBER_ERR;
        } else {
            rc = REMEMBER_OK;
        }
    } else {
        char display[SETTING_VALUE_CAP];

        if (c10_display(spec, &read, opt->scope_set, display, sizeof(display)) != 0) {
            err_msg(DIAG_INTERNAL, "internal error");
        } else {
            rc = write_c10(spec->key, display);
        }
    }
    free(old);
    return rc;
}

static int apply_reset_bulk(Store *store, bool json, int argc, const char **argv,
                            const char *userdb, const WriteOpts *opt, const char *updated_by)
{
    ResetItem *items = NULL;
    size_t n = 0;
    size_t cap = 0;
    size_t i = 0;
    char err[PREPARE_ERR_CAP];
    int rc = REMEMBER_ERR;

    err[0] = '\0';
    if (opt->scope_set) {
        rc = collect_layer(store, userdb, opt->scope, opt->ns, &items, &n, &cap, err, sizeof(err));
    } else if (collect_layer(store, userdb, SETTING_LAYER_BRAIN, opt->ns, &items, &n, &cap, err,
                             sizeof(err)) == 0) {
        rc = collect_layer(store, userdb, SETTING_LAYER_USER, opt->ns, &items, &n, &cap, err,
                           sizeof(err));
    }
    if (rc != 0) {
        reset_items_free(items, n);
        if (err[0] == '\0') {
            err_msg(DIAG_OUT_OF_MEMORY, "out of memory");
            return REMEMBER_ERR;
        }
        return fail_store(err);
    }
    if (n > 1U) {
        qsort(items, n, sizeof(*items), cmp_reset_item);
    }
    /* Nothing matched: nothing to confirm (decision:plan20-reset-count-zero-one). */
    if (!opt->yes && n > 0U) {
        rc = report_confirm(argc, argv, unique_keys(items, n));
        reset_items_free(items, n);
        return rc;
    }
    for (i = 0; i < n; i++) {
        SettingWriteResult wrote;

        memset(&wrote, 0, sizeof(wrote));
        err[0] = '\0';
        if (store_setting_reset_stored(store, userdb, items[i].key, items[i].layer, updated_by,
                                       &wrote, err, sizeof(err)) != SETTING_OK) {
            /* The two databases are not one transaction. Earlier keys stay reset. */
            reset_items_free(items, n);
            return fail_store(err);
        }
        items[i].changed = wrote.changed;
    }
    rc = emit_reset_batch(store, userdb, json, opt->scope_set, items, n, err, sizeof(err));
    reset_items_free(items, n);
    return rc;
}

static int cmd_settings_reset(Store *store, bool json, int argc, const char **argv)
{
    WriteOpts opt;
    char userdb[REMEMBER_PATH_MAX];
    char err[PREPARE_ERR_CAP];
    char actor_buf[ACTOR_JSON_CAP];
    const char *updated_by = NULL;

    memset(&opt, 0, sizeof(opt));
    err[0] = '\0';
    if (parse_reset(argc, argv, &opt) != 0 || reset_forms(&opt) != 0) {
        return REMEMBER_ERR;
    }
    if (guard_actor(&opt, actor_buf, &updated_by) != 0) {
        return REMEMBER_ERR;
    }
    if (util_resolve_user_db_path(userdb, sizeof(userdb), err, sizeof(err)) != 0) {
        return fail_store(err);
    }
    if (opt.key != NULL) {
        return apply_reset_one(store, json, userdb, &opt, updated_by);
    }
    return apply_reset_bulk(store, json, argc, argv, userdb, &opt, updated_by);
}

static const char *settings_verb(int rest_argc, const char **rest_argv)
{
    if (rest_argc > 0 && rest_argv != NULL && rest_argv[0] != NULL) {
        if (strcmp(rest_argv[0], "get") == 0) {
            return "setting.get";
        }
        if (strcmp(rest_argv[0], "set") == 0) {
            return "setting.set";
        }
        if (strcmp(rest_argv[0], "reset") == 0) {
            return "setting.reset";
        }
    }
    return NULL;
}

/* cppcheck-suppress staticFunction ; public API (commands.h) */
const char *cmd_settings_reply(int rest_argc, const char **rest_argv)
{
    const char *verb = settings_verb(rest_argc, rest_argv);

    if (verb != NULL) {
        return verb;
    }
    return "unknown";
}

int cmd_settings(Store *store, bool json, int rest_argc, const char **rest_argv)
{
    if (settings_verb(rest_argc, rest_argv) == NULL) {
        err_msg(DIAG_USAGE, "settings takes 'get', 'set', or 'reset'");
        return REMEMBER_ERR;
    }
    if (strcmp(rest_argv[0], "get") == 0) {
        return cmd_settings_get(store, json, rest_argc, rest_argv);
    }
    if (strcmp(rest_argv[0], "set") == 0) {
        return cmd_settings_set(store, json, rest_argc, rest_argv);
    }
    return cmd_settings_reset(store, json, rest_argc, rest_argv);
}
