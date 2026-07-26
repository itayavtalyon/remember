#include "commands.h"
#include "commands_common.h"

#include "exit_codes.h"
#include "normalize.h"
#include "output.h"
#include "store.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Locator (get / delete): exactly one of id | --key. */

typedef struct {
    const char *key_raw;
    const char *id_raw;
} LocatorParse;

static int parse_id_token(const char *id_raw, long long *out_id)
{
    char *end = NULL;
    long long id;

    errno = 0;
    id = strtoll(id_raw, &end, 10);
    if (end == id_raw || (end != NULL && *end != '\0') || errno == ERANGE || id < 1) {
        return -1;
    }
    *out_id = id;
    return 0;
}

/* Shared by get/delete: --key, positional id, reject --source and unknowns. */
static int parse_locator_args(int rest_argc, const char **rest_argv, LocatorParse *out)
{
    int i;
    int end_opts = 0;

    out->key_raw = NULL;
    out->id_raw = NULL;

    for (i = 0; i < rest_argc; i++) {
        const char *arg = rest_argv[i];

        if (!end_opts && strcmp(arg, "--") == 0) {
            end_opts = 1;
            continue;
        }
        if (!end_opts && strcmp(arg, "--key") == 0) {
            if (i + 1 >= rest_argc) {
                err_msg("missing value for --key");
                return -1;
            }
            out->key_raw = rest_argv[++i];
            continue;
        }
        if (!end_opts && strcmp(arg, "--source") == 0) {
            err_msg("--source is only valid on add");
            return -1;
        }
        if (!end_opts && arg[0] == '-' && arg[1] != '\0') {
            (void)fprintf(stderr, "remember: unknown option '%s'\n", arg);
            return -1;
        }
        if (out->id_raw != NULL) {
            err_msg("too many arguments");
            return -1;
        }
        out->id_raw = arg;
    }
    return 0;
}

static int locator_validate(const LocatorParse *p)
{
    if (p->key_raw != NULL && p->id_raw != NULL) {
        err_msg("provide either id or --key, not both");
        return -1;
    }
    if (p->key_raw == NULL && p->id_raw == NULL) {
        err_msg("missing id or --key");
        return -1;
    }
    return 0;
}

/* Normalize key or parse id; on success either *out_key is set or *out_id. */
static int locator_resolve(const LocatorParse *p, char *key_norm, size_t key_norm_sz,
                           const char **out_key, long long *out_id)
{
    *out_key = NULL;
    *out_id = 0;
    if (p->key_raw != NULL) {
        NormStatus ns = normalize_key(p->key_raw, key_norm, key_norm_sz);
        if (ns != NORM_OK) {
            err_msg(norm_token_message(ns, "key"));
            return -1;
        }
        *out_key = key_norm;
        return 0;
    }
    if (parse_id_token(p->id_raw, out_id) != 0) {
        err_msg("invalid id");
        return -1;
    }
    return 0;
}

int cmd_get(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    LocatorParse parsed;
    Entry entry;
    char key_norm[REMEMBER_TOKEN_MAX + 1];
    const char *key = NULL;
    long long id = 0;
    StoreStatus st;
    int rc;

    memset(&entry, 0, sizeof(entry));
    if (parse_locator_args(rest_argc, rest_argv, &parsed) != 0) {
        return REMEMBER_ERR;
    }
    if (locator_validate(&parsed) != 0) {
        return REMEMBER_ERR;
    }
    if (locator_resolve(&parsed, key_norm, sizeof(key_norm), &key, &id) != 0) {
        return REMEMBER_ERR;
    }

    if (key != NULL) {
        st = store_get_by_key(s, key, &entry);
    } else {
        st = store_get(s, id, &entry);
    }
    rc = store_status_to_exit(st);
    if (rc != REMEMBER_OK) {
        return rc;
    }

    if (json) {
        if (output_get_envelope(stdout, &entry) != 0) {
            store_entry_free(&entry);
            err_msg("failed to write output");
            return REMEMBER_ERR;
        }
    } else {
        /* Body via output.c — never raw fputs (terminal control neutralization). */
        if (output_body_human(stdout, entry.body) != 0) {
            store_entry_free(&entry);
            err_msg("failed to write output");
            return REMEMBER_ERR;
        }
    }
    store_entry_free(&entry);
    return REMEMBER_OK;
}

int cmd_delete(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    LocatorParse parsed;
    Entry entry;
    char key_norm[REMEMBER_TOKEN_MAX + 1];
    const char *key = NULL;
    long long id = 0;
    StoreStatus st;
    int rc;

    memset(&entry, 0, sizeof(entry));
    if (parse_locator_args(rest_argc, rest_argv, &parsed) != 0) {
        return REMEMBER_ERR;
    }
    if (locator_validate(&parsed) != 0) {
        return REMEMBER_ERR;
    }
    if (locator_resolve(&parsed, key_norm, sizeof(key_norm), &key, &id) != 0) {
        return REMEMBER_ERR;
    }

    if (key != NULL) {
        st = store_delete_by_key(s, key, &entry);
    } else {
        st = store_delete_by_id(s, id, &entry);
    }
    rc = store_status_to_exit(st);
    if (rc != REMEMBER_OK) {
        return rc;
    }

    if (json) {
        if (output_action_envelope(stdout, "deleted", &entry) != 0) {
            store_entry_free(&entry);
            err_msg("failed to write output");
            return REMEMBER_ERR;
        }
    }
    /* Human delete: silent success (design: no id ack required for delete). */
    store_entry_free(&entry);
    return REMEMBER_OK;
}
