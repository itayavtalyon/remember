#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

/* Empty counts as unset. Single-threaded CLI; getenv has no portable
   thread-safe variant in C11/POSIX. */
static const char *env_nonempty(const char *name)
{
    const char *value = NULL;

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    value = getenv(name);
    if (value != NULL && value[0] == '\0') {
        return NULL;
    }
    return value;
}

static int copy_regular_db_path(const char *chosen, char *buf, size_t buflen, char *err,
                                size_t errlen)
{
    size_t n = 0;

    if (path_is_ephemeral(chosen)) {
        set_err(err, errlen, "database path must be a regular file (not :memory: or file: URI)");
        return -1;
    }
    n = strlen(chosen);
    if (n >= buflen) {
        set_err(err, errlen, "database path is too long");
        return -1;
    }
    memcpy(buf, chosen, n + 1U);
    return 0;
}

/* Bytes of default-db read; a longer file is too long. */
enum { DEFAULT_DB_READ_MAX = PATH_MAX + 1 };

/* Strips ASCII whitespace from both ends in place; returns the first kept byte. */
static char *trim_ascii_space(char *s)
{
    static const char space[] = " \t\n\v\f\r";
    size_t n = strlen(s);
    size_t lead = strspn(s, space);

    while (n > lead && strchr(space, s[n - 1U]) != NULL) {
        n--;
    }
    s[n] = '\0';
    return s + lead;
}

/* root and suffix are different roles (directory, then the fixed tail). */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static int format_db_path(char *buf, size_t buflen, char *err, size_t errlen, const char *root,
                          const char *suffix)
{
    int n = snprintf(buf, buflen, "%s%s", root, suffix);

    if (n < 0 || (size_t)n >= buflen) {
        set_err(err, errlen, "database path is too long");
        return -1;
    }
    return 0;
}

static const char *home_dir(char *err, size_t errlen, const char *missing)
{
    const char *home = env_nonempty("HOME");

    if (home == NULL) {
        set_err(err, errlen, missing);
        return NULL;
    }
    return home;
}

/* Reads ~/.remember/default-db into content (DEFAULT_DB_READ_MAX + 1 bytes).
   Returns 1 when a file was read, 0 when there is none (ENOENT, ENOTDIR), -1 on
   error. A file that exists but cannot be opened or read, is not a regular
   file, or holds a NUL byte is "cannot read default-db", with no fallback
   (decision:2026-11-13-default-db-unreadable, decision:2026-11-14-plan27-review-grill).
   O_NONBLOCK keeps a FIFO from blocking the open. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static int read_default_db(const char *home, char *content, char *err, size_t errlen)
{
    char file[PATH_MAX];
    struct stat st;
    FILE *f = NULL;
    size_t n = 0;
    int fd = -1;
    int failed = 1;

    if (format_db_path(file, sizeof(file), err, errlen, home, "/.remember/default-db") != 0) {
        return -1;
    }
    fd = open(file, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0 && (errno == ENOENT || errno == ENOTDIR)) {
        return 0;
    }
    if (fd >= 0 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
        f = fdopen(fd, "r");
    }
    if (f != NULL) {
        n = fread(content, 1U, DEFAULT_DB_READ_MAX, f);
        failed = ferror(f) != 0 || memchr(content, '\0', n) != NULL;
        (void)fclose(f);
    } else if (fd >= 0) {
        (void)close(fd);
    }
    if (failed != 0) {
        set_err(err, errlen, "cannot read default-db");
        return -1;
    }
    if (n == (size_t)DEFAULT_DB_READ_MAX) {
        set_err(err, errlen, "database path is too long");
        return -1;
    }
    content[n] = '\0';
    return 1;
}

/* Puts the absolute path of ~/.remember/default-db into buf. Returns 1 when it
   did, 0 when the file gives none (missing, empty, relative), -1 on error. The
   line goes through copy_regular_db_path before the absolute check, so :memory:
   and a file: URI fail as they do for --db. */
static int use_default_db(const char *home, char *buf, size_t buflen, char *err, size_t errlen)
{
    char content[DEFAULT_DB_READ_MAX + 1];
    const char *line = NULL;
    int found = read_default_db(home, content, err, errlen);

    if (found <= 0) {
        return found;
    }
    line = trim_ascii_space(content);
    if (copy_regular_db_path(line, buf, buflen, err, errlen) != 0) {
        return -1;
    }
    return buf[0] == '/' ? 1 : 0;
}

int util_resolve_db_path(const char *cli_db, char *buf, size_t buflen, char *err, size_t errlen)
{
    const char *chosen = NULL;
    const char *home = NULL;
    int found = 0;

    if (buf == NULL || buflen == 0U) {
        set_err(err, errlen, "internal error: path buffer missing");
        return -1;
    }

    if (cli_db != NULL && cli_db[0] != '\0') {
        chosen = cli_db;
    } else {
        chosen = env_nonempty("REMEMBER_DB");
    }
    if (chosen != NULL) {
        return copy_regular_db_path(chosen, buf, buflen, err, errlen);
    }
    home = home_dir(err, errlen, "HOME is not set; pass --db PATH");
    if (home == NULL) {
        return -1;
    }
    found = use_default_db(home, buf, buflen, err, errlen);
    if (found != 0) {
        return found < 0 ? -1 : 0;
    }
    return format_db_path(buf, buflen, err, errlen, home, "/.remember/remember.db");
}

/* The data dir (decision:g-01), or user.db in it: $XDG_DATA_HOME/remember when
   XDG_DATA_HOME is set and non-empty, else $HOME/.remember. xdg_leaf and home_leaf are
   what follows the base: the folder, or the folder and user.db. */
static int resolve_data_path(const char *xdg_leaf, const char *home_leaf, char *buf, size_t buflen,
                             char *err, size_t errlen)
{
    const char *chosen = NULL;
    const char *home = NULL;

    if (buf == NULL || buflen == 0U) {
        set_err(err, errlen, "internal error: path buffer missing");
        return -1;
    }
    chosen = env_nonempty("XDG_DATA_HOME");
    if (chosen != NULL) {
        return format_db_path(buf, buflen, err, errlen, chosen, xdg_leaf);
    }
    home = home_dir(err, errlen, "HOME is not set");
    if (home == NULL) {
        return -1;
    }
    return format_db_path(buf, buflen, err, errlen, home, home_leaf);
}

/* cppcheck-suppress staticFunction ; public API (util.h); used by tests */
int util_resolve_user_db_path(char *buf, size_t buflen, char *err, size_t errlen)
{
    const char *chosen = env_nonempty("REMEMBER_USER_DB");

    if (buf != NULL && buflen > 0U && chosen != NULL) {
        return copy_regular_db_path(chosen, buf, buflen, err, errlen);
    }
    return resolve_data_path("/remember/user.db", "/.remember/user.db", buf, buflen, err, errlen);
}

int util_resolve_data_dir(char *buf, size_t buflen, char *err, size_t errlen)
{
    return resolve_data_path("/remember", "/.remember", buf, buflen, err, errlen);
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

/* Shared by the size options and --busy-timeout so the decimal rules stay one place. */
enum { DECIMAL_BASE = 10 };

int util_parse_nonneg(const char *raw, unsigned long long *out)
{
    char *end = NULL;
    unsigned long long value = 0;

    /* ASCII digits only (decision:size-option-digits-only). A leading digit stops
       strtoull from skipping space or taking a sign (" -1" would wrap to
       ULLONG_MAX); the end check below rejects anything after the digits. */
    if (raw == NULL || !isdigit((unsigned char)raw[0])) {
        return -1;
    }
    errno = 0;
    value = strtoull(raw, &end, DECIMAL_BASE);
    if ((end != NULL && *end != '\0') || errno == ERANGE) {
        return -1;
    }
    *out = value;
    return 0;
}
