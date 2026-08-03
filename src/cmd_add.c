#include "commands.h"
#include "commands_common.h"

#include "appio.h"
#include "exit_codes.h"
#include "normalize.h"
#include "output.h"
#include "store.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *source;
    const char *key_raw;
    const char *body_raw;
    const char **tag_raw;
    size_t ntag_raw;
    /* True when the body token followed `--` — then "-" is a literal body. */
    bool body_literal;
} AddParse;

static void add_parse_free(AddParse *p)
{
    free((void *)p->tag_raw);
    p->tag_raw = NULL;
    p->ntag_raw = 0U;
}

static int handle_add_flag(const char *arg, int *i, int rest_argc, const char **rest_argv,
                           AddParse *out, size_t *tag_cap, const char **err)
{
    if (strcmp(arg, "--") == 0) {
        return 1; /* end opts */
    }
    if (strcmp(arg, "--source") == 0) {
        return take_value(i, rest_argc, rest_argv, &out->source, err, "missing value for --source");
    }
    if (strcmp(arg, "--key") == 0) {
        return take_value(i, rest_argc, rest_argv, &out->key_raw, err, "missing value for --key");
    }
    if (strcmp(arg, "--tag") == 0) {
        const char *t = NULL;
        if (take_value(i, rest_argc, rest_argv, &t, err, "missing value for --tag") != 0) {
            return -1;
        }
        if (push_cstr_ptr(&out->tag_raw, &out->ntag_raw, tag_cap, t) != 0) {
            *err = "out of memory";
            return -1;
        }
        return 0;
    }
    if (arg[0] == '-' && arg[1] != '\0') {
        (void)fprintf(app_err(), "remember: unknown option '%s'\n", arg);
        *err = "";
        return -1;
    }
    return 2; /* positional */
}

static int parse_add_args(int rest_argc, const char **rest_argv, AddParse *out, const char **err)
{
    int i;
    int end_opts = 0;
    size_t tag_cap = 0U;

    out->source = "unknown";
    out->key_raw = NULL;
    out->body_raw = NULL;
    out->tag_raw = NULL;
    out->ntag_raw = 0U;
    out->body_literal = false;
    *err = NULL;

    for (i = 0; i < rest_argc; i++) {
        const char *arg = rest_argv[i];

        if (!end_opts) {
            int kind = handle_add_flag(arg, &i, rest_argc, rest_argv, out, &tag_cap, err);
            if (kind == 1) {
                end_opts = 1;
                continue;
            }
            if (kind == 0) {
                continue;
            }
            if (kind < 0) {
                return -1;
            }
        }
        if (out->body_raw != NULL) {
            *err = "too many arguments";
            return -1;
        }
        out->body_raw = arg;
        /* After `--`, even "-" is a one-character memory, not stdin. */
        out->body_literal = (end_opts != 0);
    }
    return 0;
}

static int emit_add_result(bool json, StoreAddAction action, const Entry *entry)
{
    if (json) {
        if (output_action_envelope(app_out(), action_name(action), entry) != 0) {
            err_msg("failed to write output");
            return -1;
        }
        return 0;
    }
    if (output_id_human(app_out(), entry->id) != 0) {
        err_msg("failed to write output");
        return -1;
    }
    return 0;
}

int cmd_add(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    AddParse parsed;
    const char *err = NULL;
    char key_norm[REMEMBER_TOKEN_MAX + 1];
    char **tags_norm = NULL;
    size_t ntags = 0U;
    char *body = NULL;
    size_t body_len = 0U;
    char hash[REMEMBER_SHA256_HEX_LEN + 1];
    const char *key_or_null = NULL;
    Entry entry;
    StoreAddAction action = STORE_ADD_CREATED;
    StoreStatus st;
    int rc = REMEMBER_ERR;

    memset(&entry, 0, sizeof(entry));
    memset(key_norm, 0, sizeof(key_norm));

    if (parse_add_args(rest_argc, rest_argv, &parsed, &err) != 0) {
        if (err != NULL && err[0] != '\0') {
            err_msg(err);
        }
        add_parse_free(&parsed);
        return REMEMBER_ERR;
    }

    if (!source_is_valid(parsed.source)) {
        err_msg("invalid source (use human, agent, tool, or unknown)");
        goto cleanup;
    }
    if (parsed.body_raw == NULL) {
        err_msg("missing body");
        goto cleanup;
    }
    {
        int dash_is_stdin = 1;
        if (parsed.body_literal) {
            dash_is_stdin = 0;
        }
        if (load_body(parsed.body_raw, dash_is_stdin, &body, &body_len, &err) != 0) {
            err_msg(err);
            goto cleanup;
        }
    }
    if (parsed.key_raw != NULL) {
        NormStatus ns = normalize_key(parsed.key_raw, key_norm, sizeof(key_norm));
        if (ns != NORM_OK) {
            err_msg(norm_token_message(ns, "key"));
            goto cleanup;
        }
        key_or_null = key_norm;
    }
    if (normalize_tags((const char *const *)parsed.tag_raw, parsed.ntag_raw, &tags_norm, &ntags,
                       &err) != 0) {
        err_msg(err);
        goto cleanup;
    }

    body_hash_hex(body, body_len, hash);
    st = store_add(s, body, hash, key_or_null, (const char *const *)tags_norm, ntags, parsed.source,
                   &action, &entry);
    if (st != STORE_OK) {
        err_msg(store_status_message(st));
        goto cleanup;
    }
    if (emit_add_result(json, action, &entry) != 0) {
        goto cleanup;
    }
    rc = REMEMBER_OK;

cleanup:
    add_parse_free(&parsed);
    free(body);
    free_tag_list(tags_norm, ntags);
    store_entry_free(&entry);
    return rc;
}
