#include "util.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static void set_err(char *err, size_t errlen, const char *msg)
{
    size_t n = 0;

    if (err == NULL || errlen == 0U) {
        return;
    }
    n = strlen(msg);
    if (n >= errlen) {
        n = errlen - 1U;
    }
    memcpy(err, msg, n);
    err[n] = '\0';
}

static int path_is_ephemeral(const char *path)
{
    if (path == NULL) {
        return 1;
    }
    if (strcmp(path, ":memory:") == 0) {
        return 1;
    }
    if (strncmp(path, "file:", sizeof("file:") - 1U) == 0) {
        return 1;
    }
    return 0;
}

int util_path_looks_synced(const char *path)
{
    static const char *const markers[] = {
        "com~apple~CloudDocs",
        "Dropbox",
        "Google Drive",
        NULL,
    };
    size_t i = 0;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    for (i = 0; markers[i] != NULL; i++) {
        if (strstr(path, markers[i]) != NULL) {
            return 1;
        }
    }
    return 0;
}

int util_resolve_db_path(const char *cli_db, char *buf, size_t buflen, char *err, size_t errlen)
{
    const char *chosen = NULL;
    const char *home = NULL;
    int n = 0;

    if (buf == NULL || buflen == 0U) {
        set_err(err, errlen, "internal error: path buffer missing");
        return -1;
    }

    if (cli_db != NULL && cli_db[0] != '\0') {
        chosen = cli_db;
    } else {
        /* Single-threaded CLI; getenv has no portable thread-safe variant in C11/POSIX. */
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        chosen = getenv("REMEMBER_DB");
        if (chosen != NULL && chosen[0] == '\0') {
            chosen = NULL;
        }
    }

    if (chosen != NULL) {
        if (path_is_ephemeral(chosen)) {
            set_err(err, errlen,
                    "database path must be a regular file (not :memory: or file: URI)");
            return -1;
        }
        if (strlen(chosen) >= buflen) {
            set_err(err, errlen, "database path is too long");
            return -1;
        }
        memcpy(buf, chosen, strlen(chosen) + 1U);
        return 0;
    }

    /* Single-threaded CLI; getenv has no portable thread-safe variant in C11/POSIX. */
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        set_err(err, errlen, "HOME is not set; pass --db PATH");
        return -1;
    }
    n = snprintf(buf, buflen, "%s/.remember/remember.db", home);
    if (n < 0 || (size_t)n >= buflen) {
        set_err(err, errlen, "database path is too long");
        return -1;
    }
    return 0;
}

int util_read_stdin(char **out, size_t *out_len, size_t max_len)
{
    enum { STDIN_INITIAL_CAP = 4096 }; /* first read buffer; doubles thereafter */
    char *buf = NULL;
    size_t len = 0U;
    size_t cap = 0U;

    if (out == NULL) {
        return -1;
    }
    *out = NULL;
    if (out_len != NULL) {
        *out_len = 0U;
    }

    for (;;) {
        int c = fgetc(stdin);
        if (c == EOF) {
            if (ferror(stdin)) {
                free(buf);
                return -1;
            }
            break;
        }
        if (len >= max_len) {
            free(buf);
            return -2;
        }
        if (len + 1U >= cap) {
            size_t ncap = (cap == 0U) ? (size_t)STDIN_INITIAL_CAP : cap * 2U;
            char *nb = NULL;
            if (ncap < len + 2U) {
                ncap = len + 2U;
            }
            nb = (char *)realloc(buf, ncap);
            if (nb == NULL) {
                free(buf);
                return -1;
            }
            buf = nb;
            cap = ncap;
        }
        buf[len] = (char)c;
        len++;
    }

    if (buf == NULL) {
        buf = (char *)malloc(1U);
        if (buf == NULL) {
            return -1;
        }
        len = 0U;
    }
    buf[len] = '\0';
    *out = buf;
    if (out_len != NULL) {
        *out_len = len;
    }
    return 0;
}

/* Join path onto cwd when relative (heap, caller frees). NULL on OOM/error.
 * getcwd into a stack buffer: POSIX does not define getcwd(NULL, 0). */
static char *join_with_cwd(const char *path)
{
    char cwd[PATH_MAX];
    char *joined = NULL;
    size_t need = 0U;

    if (path[0] == '/') {
        need = strlen(path) + 1U;
        joined = (char *)malloc(need);
        if (joined == NULL) {
            return NULL;
        }
        memcpy(joined, path, need);
        return joined;
    }
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        return NULL;
    }
    need = strlen(cwd) + 1U + strlen(path) + 1U;
    joined = (char *)malloc(need);
    if (joined == NULL) {
        return NULL;
    }
    (void)snprintf(joined, need, "%s/%s", cwd, path);
    return joined;
}

/*
 * Lexical normalize: split on '/', drop "" and ".", pop on ".." (clamped at
 * the root). Writes into buf; -1 when the result does not fit.
 */
static int normalize_into(const char *joined, char *buf, size_t buflen)
{
    /* Offsets of kept components inside joined; depth is the stack pointer. */
    enum { MAX_PARTS = 1024 };
    size_t starts[MAX_PARTS];
    size_t lens[MAX_PARTS];
    size_t depth = 0U;
    size_t i = 0U;
    size_t n = strlen(joined);
    size_t w = 0U;
    size_t d = 0U;

    for (i = 0U; i <= n;) {
        size_t j = i;

        while (j < n && joined[j] != '/') {
            j++;
        }
        if (j > i) {
            size_t len = j - i;
            if (len == 1U && joined[i] == '.') {
                /* skip */
            } else if (len == 2U && joined[i] == '.' && joined[i + 1U] == '.') {
                if (depth > 0U) {
                    depth--;
                }
            } else if (depth < (size_t)MAX_PARTS) {
                starts[depth] = i;
                lens[depth] = len;
                depth++;
            } else {
                return -1;
            }
        }
        i = j + 1U;
    }
    /* "/" plus one '/' per component plus bytes; depth==0 is the root. */
    for (d = 0U; d < depth; d++) {
        if (w + 1U + lens[d] + 1U > buflen) {
            return -1;
        }
        buf[w] = '/';
        w++;
        memcpy(buf + w, joined + starts[d], lens[d]);
        w += lens[d];
    }
    if (depth == 0U) {
        if (buflen < 2U) {
            return -1;
        }
        buf[w] = '/';
        w++;
    }
    if (w >= buflen) {
        return -1;
    }
    buf[w] = '\0';
    return 0;
}

int util_absolute_path(const char *path, char *buf, size_t buflen)
{
    char *joined = NULL;
    int rc = -1;

    if (path == NULL || path[0] == '\0' || buf == NULL || buflen == 0U) {
        return -1;
    }
    joined = join_with_cwd(path);
    if (joined == NULL) {
        return -1;
    }
    rc = normalize_into(joined, buf, buflen);
    free(joined);
    return rc;
}

/* Shared by --limit/--offset and --busy-timeout so the decimal rules stay one place. */
enum { DECIMAL_BASE = 10 };

int util_parse_nonneg(const char *raw, unsigned long long *out)
{
    char *end = NULL;
    unsigned long long value = 0;

    if (raw == NULL || raw[0] == '\0' || raw[0] == '-') {
        return -1;
    }
    errno = 0;
    value = strtoull(raw, &end, DECIMAL_BASE);
    if (end == raw || (end != NULL && *end != '\0') || errno == ERANGE) {
        return -1;
    }
    *out = value;
    return 0;
}
