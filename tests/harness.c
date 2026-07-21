#include "harness.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <stdbool.h>

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

static char *read_fd_all(int fd)
{
    size_t cap = 4096;
    size_t len = 0;
    char *buf = malloc(cap);
    if (buf == NULL) {
        return NULL;
    }
    for (;;) {
        if (len + 1024 >= cap) {
            size_t ncap = cap * 2U;
            char *nbuf = realloc(buf, ncap);
            if (nbuf == NULL) {
                free(buf);
                return NULL;
            }
            buf = nbuf;
            cap = ncap;
        }
        ssize_t n = read(fd, buf + len, 1024);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            free(buf);
            return NULL;
        }
        if (n == 0) {
            break;
        }
        len += (size_t)n;
    }
    buf[len] = '\0';
    return buf;
}

CmdResult run_remember(const char *db_path, const char *const *args, size_t nargs,
                       const char *stdin_data)
{
    CmdResult result = {0, NULL, NULL};
    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    int in_pipe[2] = {-1, -1};
    pid_t pid;
    size_t i;
    size_t argc;
    char **argv = NULL;

    if (g_remember_bin == NULL || db_path == NULL) {
        result.exit_code = 127;
        result.err = strdup("harness: missing binary or db path");
        result.out = strdup("");
        return result;
    }

    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
        result.exit_code = 127;
        result.err = strdup("harness: pipe failed");
        result.out = strdup("");
        return result;
    }

    if (stdin_data != NULL) {
        if (pipe(in_pipe) != 0) {
            result.exit_code = 127;
            result.err = strdup("harness: stdin pipe failed");
            result.out = strdup("");
            close(out_pipe[0]);
            close(out_pipe[1]);
            close(err_pipe[0]);
            close(err_pipe[1]);
            return result;
        }
    }

    /* argv: bin, --db, path, args..., NULL */
    argc = 3U + nargs;
    argv = calloc(argc + 1U, sizeof(*argv));
    if (argv == NULL) {
        result.exit_code = 127;
        result.err = strdup("harness: oom");
        result.out = strdup("");
        goto fail_pipes;
    }
    argv[0] = (char *)g_remember_bin;
    argv[1] = (char *)"--db";
    argv[2] = (char *)db_path;
    for (i = 0; i < nargs; i++) {
        argv[3U + i] = (char *)args[i];
    }
    argv[argc] = NULL;

    pid = fork();
    if (pid < 0) {
        result.exit_code = 127;
        result.err = strdup("harness: fork failed");
        result.out = strdup("");
        free(argv);
        goto fail_pipes;
    }

    if (pid == 0) {
        /* child */
        if (dup2(out_pipe[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        if (dup2(err_pipe[1], STDERR_FILENO) < 0) {
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

    /* parent */
    free(argv);
    close(out_pipe[1]);
    close(err_pipe[1]);
    if (stdin_data != NULL) {
        close(in_pipe[0]);
        {
            size_t left = strlen(stdin_data);
            const char *p = stdin_data;
            while (left > 0) {
                ssize_t w = write(in_pipe[1], p, left);
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
        close(in_pipe[1]);
        in_pipe[1] = -1;
    }

    result.out = read_fd_all(out_pipe[0]);
    result.err = read_fd_all(err_pipe[0]);
    close(out_pipe[0]);
    close(err_pipe[0]);
    out_pipe[0] = err_pipe[0] = -1;

    if (result.out == NULL) {
        result.out = strdup("");
    }
    if (result.err == NULL) {
        result.err = strdup("");
    }

    {
        int status = 0;
        if (waitpid(pid, &status, 0) < 0) {
            result.exit_code = 127;
            return result;
        }
        if (WIFEXITED(status)) {
            result.exit_code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            result.exit_code = 128 + WTERMSIG(status);
        } else {
            result.exit_code = 127;
        }
    }
    return result;

fail_pipes:
    if (out_pipe[0] >= 0) {
        close(out_pipe[0]);
    }
    if (out_pipe[1] >= 0) {
        close(out_pipe[1]);
    }
    if (err_pipe[0] >= 0) {
        close(err_pipe[0]);
    }
    if (err_pipe[1] >= 0) {
        close(err_pipe[1]);
    }
    if (in_pipe[0] >= 0) {
        close(in_pipe[0]);
    }
    if (in_pipe[1] >= 0) {
        close(in_pipe[1]);
    }
    return result;
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

char *sqlite3_query_line(const char *db_path, const char *sql)
{
    char cmd[1024];
    FILE *fp;
    char line[512];
    char *out;
    int n;

    if (db_path == NULL || sql == NULL) {
        return NULL;
    }
    /* Requires sqlite3 CLI on PATH (dev dependency for inspect-only tests). */
    n = snprintf(cmd, sizeof(cmd), "sqlite3 '%s' '%s'", db_path, sql);
    if (n < 0 || (size_t)n >= sizeof(cmd)) {
        return NULL;
    }
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
