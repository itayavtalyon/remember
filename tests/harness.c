#include "harness.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* Same fallback as store_sqlite.c: PATH_MAX is POSIX, not ISO C. */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

const char *g_remember_bin = NULL;

void cmd_result_free(CmdResult *r)
{
    if (r == NULL) {
        return;
    }
    free(r->out);
    free(r->err);
    r->out = NULL;
    r->err = NULL;
    r->exit_code = 0;
}

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} Buf;

static bool buf_append(Buf *b, const char *src, size_t n)
{
    if (b->len + n + 1U > b->cap) {
        size_t ncap = (b->cap != 0U) ? b->cap : 4096U;
        char *nd;
        while (ncap < b->len + n + 1U) {
            ncap *= 2U;
        }
        nd = realloc(b->data, ncap);
        if (nd == NULL) {
            return false;
        }
        b->data = nd;
        b->cap = ncap;
    }
    memcpy(b->data + b->len, src, n);
    b->len += n;
    return true;
}

/*
 * Drain both fds to EOF concurrently via poll(). Reading them sequentially would
 * deadlock if the child fills one pipe (e.g. a large stderr) while we block on
 * the other. Each output is heap-allocated and NUL-terminated (never NULL).
 */
static void drain_two(int fd0, int fd1, char **out0, char **out1)
{
    Buf bufs[2] = {{NULL, 0, 0}, {NULL, 0, 0}};
    int fds[2];
    int open_count = 2;

    fds[0] = fd0;
    fds[1] = fd1;

    while (open_count > 0) {
        struct pollfd pfds[2];
        int slot[2];
        int nfds = 0;
        int i;

        for (i = 0; i < 2; i++) {
            if (fds[i] >= 0) {
                pfds[nfds].fd = fds[i];
                pfds[nfds].events = POLLIN;
                pfds[nfds].revents = 0;
                slot[nfds] = i;
                nfds++;
            }
        }
        if (poll(pfds, (nfds_t)nfds, -1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        for (i = 0; i < nfds; i++) {
            int idx = slot[i];
            char tmp[4096];
            ssize_t n;

            if ((pfds[i].revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
                continue;
            }
            n = read(fds[idx], tmp, sizeof(tmp));
            if (n > 0) {
                (void)buf_append(&bufs[idx], tmp, (size_t)n);
            } else if (n == 0 || errno != EINTR) {
                fds[idx] = -1;
                open_count--;
            }
        }
    }

    for (int i = 0; i < 2; i++) {
        if (bufs[i].data == NULL) {
            bufs[i].data = calloc(1U, 1U);
        } else {
            bufs[i].data[bufs[i].len] = '\0';
        }
    }
    *out0 = bufs[0].data;
    *out1 = bufs[1].data;
}

static CmdResult harness_error(const char *msg)
{
    CmdResult r;

    r.exit_code = 127;
    r.out = strdup("");
    r.err = strdup(msg);
    return r;
}

static void close_pipe(int p[2])
{
    if (p[0] >= 0) {
        close(p[0]);
    }
    if (p[1] >= 0) {
        close(p[1]);
    }
}

/* argv: bin, --db, path, args..., NULL. Elements alias their inputs.
 * execv wants char *const[]; the process does not write through these pointers. */
static char **build_child_argv(const char *db_path, const char *const *args, size_t nargs)
{
    size_t argc = 3U + nargs;
    char **argv = (char **)calloc(argc + 1U, sizeof(*argv));
    size_t i;

    if (argv == NULL) {
        return NULL;
    }
    /* NOLINTBEGIN(clang-diagnostic-cast-qual) */
    argv[0] = (char *)g_remember_bin;
    argv[1] = (char *)"--db";
    argv[2] = (char *)db_path;
    for (i = 0; i < nargs; i++) {
        argv[3U + i] = (char *)args[i];
    }
    /* NOLINTEND(clang-diagnostic-cast-qual) */
    argv[argc] = NULL;
    return argv;
}

/* Child side: redirect stdio to the pipes and exec. Never returns. */
static void child_run(int out_pipe[2], int err_pipe[2], int in_pipe[2], const char *stdin_data,
                      char *const argv[])
{
    if (dup2(out_pipe[1], STDOUT_FILENO) < 0 || dup2(err_pipe[1], STDERR_FILENO) < 0) {
        _exit(127);
    }
    close(out_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[0]);
    close(err_pipe[1]);

    if (stdin_data != NULL) {
        if (dup2(in_pipe[0], STDIN_FILENO) < 0) {
            _exit(127);
        }
        close(in_pipe[0]);
        close(in_pipe[1]);
    } else {
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            (void)dup2(devnull, STDIN_FILENO);
            close(devnull);
        }
    }
    execv(g_remember_bin, argv);
    _exit(127);
}

static void write_all(int fd, const char *data)
{
    size_t left = strlen(data);
    const char *p = data;

    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        p += (size_t)w;
        left -= (size_t)w;
    }
}

static int wait_status(pid_t pid)
{
    int status = 0;

    if (waitpid(pid, &status, 0) < 0) {
        return 127;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 127;
}

CmdResult run_remember(const char *db_path, const char *const *args, size_t nargs,
                       const char *stdin_data)
{
    CmdResult result = {0, NULL, NULL};
    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    int in_pipe[2] = {-1, -1};
    char **argv;
    pid_t pid;

    if (g_remember_bin == NULL || db_path == NULL) {
        return harness_error("harness: missing binary or db path");
    }
    /* Every failure below goes through cleanup: a half-open pipe pair would
       otherwise leak two fds per call. */
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
        result = harness_error("harness: pipe failed");
        goto cleanup;
    }
    if (stdin_data != NULL && pipe(in_pipe) != 0) {
        result = harness_error("harness: stdin pipe failed");
        goto cleanup;
    }

    argv = build_child_argv(db_path, args, nargs);
    if (argv == NULL) {
        result = harness_error("harness: oom");
        goto cleanup;
    }

    pid = fork();
    if (pid < 0) {
        result = harness_error("harness: fork failed");
        free((void *)argv);
        goto cleanup;
    }
    if (pid == 0) {
        child_run(out_pipe, err_pipe, in_pipe, stdin_data, argv);
    }

    /* parent */
    free((void *)argv);
    close(out_pipe[1]);
    out_pipe[1] = -1;
    close(err_pipe[1]);
    err_pipe[1] = -1;
    if (stdin_data != NULL) {
        close(in_pipe[0]);
        in_pipe[0] = -1;
        write_all(in_pipe[1], stdin_data);
        close(in_pipe[1]);
        in_pipe[1] = -1;
    }

    drain_two(out_pipe[0], err_pipe[0], &result.out, &result.err);
    close(out_pipe[0]);
    close(err_pipe[0]);
    if (result.out == NULL) {
        result.out = strdup("");
    }
    if (result.err == NULL) {
        result.err = strdup("");
    }
    result.exit_code = wait_status(pid);
    return result;

cleanup:
    close_pipe(out_pipe);
    close_pipe(err_pipe);
    close_pipe(in_pipe);
    return result;
}

/* ---- temp-dir registry: clean up every mkdtemp at process exit ----------- */

static char **g_temp_dirs;
static size_t g_temp_count;
static size_t g_temp_cap;
static bool g_atexit_registered;

/* Depth-first: tests nest databases under the temp dir (see parent-dir tests),
   so a single-level unlink sweep would leave the whole tree behind in /tmp. */
static void remove_temp_dir(const char *dir)
{
    DIR *d = opendir(dir);

    if (d != NULL) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            char path[4096];
            struct stat st;
            int n;
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
                continue;
            }
            n = snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
            if (n <= 0 || (size_t)n >= sizeof(path)) {
                continue;
            }
            if (lstat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
                remove_temp_dir(path);
            } else {
                (void)unlink(path);
            }
        }
        (void)closedir(d);
    }
    (void)rmdir(dir);
}

static void cleanup_temp_dirs(void)
{
    size_t i;
    for (i = 0; i < g_temp_count; i++) {
        remove_temp_dir(g_temp_dirs[i]);
        free(g_temp_dirs[i]);
    }
    free((void *)g_temp_dirs);
    g_temp_dirs = NULL;
    g_temp_count = 0;
    g_temp_cap = 0;
}

static void register_temp_dir(const char *dir)
{
    char *copy;

    if (!g_atexit_registered) {
        (void)atexit(cleanup_temp_dirs);
        g_atexit_registered = true;
    }
    if (g_temp_count == g_temp_cap) {
        size_t ncap = (g_temp_cap != 0U) ? g_temp_cap * 2U : 16U;
        char **grown = (char **)realloc((void *)g_temp_dirs, ncap * sizeof(*grown));
        if (grown == NULL) {
            return;
        }
        g_temp_dirs = grown;
        g_temp_cap = ncap;
    }
    copy = strdup(dir);
    if (copy == NULL) {
        return;
    }
    g_temp_dirs[g_temp_count] = copy;
    g_temp_count++;
}

char *make_temp_db_path(void)
{
    char tmpl[] = "/tmp/remember-test-XXXXXX";
    char *dir;
    char *path;
    size_t n;

    dir = mkdtemp(tmpl);
    if (dir == NULL) {
        return NULL;
    }
    register_temp_dir(dir);
    n = strlen(dir) + strlen("/test.db") + 1U;
    path = malloc(n);
    if (path == NULL) {
        return NULL;
    }
    (void)snprintf(path, n, "%s/test.db", dir);
    return path;
}

void trim_trailing_newlines(char *s)
{
    size_t n;
    if (s == NULL) {
        return;
    }
    n = strlen(s);
    while (n > 0 && (s[n - 1U] == '\n' || s[n - 1U] == '\r')) {
        s[n - 1U] = '\0';
        n--;
    }
}

long parse_id_stdout(const char *out)
{
    char *copy;
    char *end = NULL;
    long id;

    if (out == NULL) {
        return -1;
    }
    copy = strdup(out);
    if (copy == NULL) {
        return -1;
    }
    trim_trailing_newlines(copy);
    if (copy[0] == '\0') {
        free(copy);
        return -1;
    }
    errno = 0;
    id = strtol(copy, &end, 10);
    if (errno != 0 || end == copy || (end != NULL && *end != '\0')) {
        free(copy);
        return -1;
    }
    free(copy);
    return id;
}

bool str_is_blank(const char *s)
{
    if (s == NULL) {
        return true;
    }
    while (*s != '\0') {
        if (*s != ' ' && *s != '\t' && *s != '\n' && *s != '\r') {
            return false;
        }
        s++;
    }
    return true;
}

/*
 * Append s to dst as a single-quoted shell word, escaping embedded quotes as
 * '\''. Double-quoting would be shorter but leaves $, ` and \ live, which SQL
 * predicates ('%$%', LIKE patterns) hit sooner or later.
 * Returns false if dst would overflow.
 */
static bool shell_quote_append(char *dst, size_t cap, size_t *len, const char *s)
{
    size_t i = *len;

    if (i + 1U >= cap) {
        return false;
    }
    dst[i++] = '\'';
    for (; *s != '\0'; s++) {
        if (*s == '\'') {
            if (i + 4U >= cap) {
                return false;
            }
            memcpy(dst + i, "'\\''", 4U);
            i += 4U;
            continue;
        }
        if (i + 1U >= cap) {
            return false;
        }
        dst[i++] = *s;
    }
    if (i + 1U >= cap) {
        return false;
    }
    dst[i++] = '\'';
    dst[i] = '\0';
    *len = i;
    return true;
}

char *harness_sqlite_query_line(const char *db_path, const char *sql)
{
    char cmd[1024];
    FILE *fp;
    char line[512];
    char *out;
    size_t len;

    if (db_path == NULL || sql == NULL) {
        return NULL;
    }
    /* Requires sqlite3 CLI on PATH (dev dependency for inspect-only tests). */
    len = (size_t)snprintf(cmd, sizeof(cmd), "sqlite3 ");
    if (!shell_quote_append(cmd, sizeof(cmd), &len, db_path)) {
        return NULL;
    }
    if (len + 1U >= sizeof(cmd)) {
        return NULL;
    }
    cmd[len] = ' ';
    len++;
    cmd[len] = '\0';
    if (!shell_quote_append(cmd, sizeof(cmd), &len, sql)) {
        return NULL;
    }
    /* Test-only DB inspection; command is built from test-controlled paths/SQL,
       never external input. */
    /* NOLINTNEXTLINE(cert-env33-c,bugprone-command-processor) */
    fp = popen(cmd, "r");
    if (fp == NULL) {
        return NULL;
    }
    if (fgets(line, sizeof(line), fp) == NULL) {
        (void)pclose(fp);
        return NULL;
    }
    (void)pclose(fp);
    trim_trailing_newlines(line);
    out = strdup(line);
    return out;
}

char *dir_of_path(const char *file_path)
{
    char *copy;
    char *slash;

    if (file_path == NULL) {
        return NULL;
    }
    copy = strdup(file_path);
    if (copy == NULL) {
        return NULL;
    }
    slash = strrchr(copy, '/');
    if (slash == NULL) {
        free(copy);
        return strdup(".");
    }
    if (slash == copy) {
        slash[1] = '\0';
        return copy;
    }
    *slash = '\0';
    return copy;
}
