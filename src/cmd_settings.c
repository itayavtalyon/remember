#include "cmd_settings.h"

#include "appio.h"
#include "settings.h"
#include "sync_credential.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

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

static bool ascii_space(unsigned char c)
{
    if (c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r') {
        return true;
    }
    return false;
}

/* EINTR is a retry. Any other negative rc is a real failure. */
static bool interrupted(int rc)
{
    if (rc < 0 && errno == EINTR) {
        return true;
    }
    return false;
}

/* n is bytes already read; cap is the buffer size. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void trim_line(char *buf, size_t n, size_t cap)
{
    size_t start = 0U;
    size_t end = n;

    while (start < end && ascii_space((unsigned char)buf[start])) {
        start++;
    }
    while (end > start && ascii_space((unsigned char)buf[end - 1U])) {
        end--;
    }
    if (start > 0U) {
        memmove(buf, buf + start, end - start);
    }
    n = end - start;
    if (n < cap) {
        buf[n] = '\0';
    }
    if (cap > n + 1U) {
        sync_credential_wipe(buf + n + 1U, cap - n - 1U);
    }
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

/* Read the first stdout line into buf. 0 finished the line, -2 timed out or the read failed. */
static int read_first_line(int read_fd, char *buf, size_t cap, bool *too_long, long long deadline)
{
    size_t n = 0U;
    bool line_done = false;

    *too_long = false;
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
        if (n + 1U < cap) {
            buf[n] = (char)byte;
            n++;
            continue;
        }
        *too_long = true;
    }
    if (n < cap) {
        buf[n] = '\0';
    }
    if (!*too_long) {
        trim_line(buf, n, cap);
    }
    if (drain_stdout(read_fd, deadline) != 0) {
        goto fail;
    }
    return 0;
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
    bool too_long = false;

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
    if (read_first_line(read_fd, buf, buflen, &too_long, deadline) != 0 || too_long ||
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
