#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_err(char *err, size_t errlen, const char *msg)
{
    size_t n;

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
    if (strncmp(path, "file:", 5) == 0) {
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
    size_t i;

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
    const char *chosen;
    const char *home;
    int n;

    if (buf == NULL || buflen == 0U) {
        set_err(err, errlen, "internal error: path buffer missing");
        return -1;
    }

    if (cli_db != NULL && cli_db[0] != '\0') {
        chosen = cli_db;
    } else {
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
            size_t ncap = (cap == 0U) ? 4096U : cap * 2U;
            char *nb;
            if (ncap < len + 2U) {
                ncap = len + 2U;
            }
            nb = realloc(buf, ncap);
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
        buf = malloc(1U);
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
