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
    const char *ttl_raw;
    const char *expires_raw;
    size_t ntag_raw;
    /* True when the body token followed `--` — then "-" is a literal body. */
    bool body_literal;
    /* NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers) */
    char pad_[7]; /* explicit tail padding (kept -Wpadded-clean) */
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
        TakeValue taken = take_value(i, rest_argc, rest_argv, "missing value for --source");
        if (taken.rc != 0) {
            *err = taken.err;
            return taken.rc;
        }
        out->source = taken.value;
        return 0;
    }
    if (strcmp(arg, "--key") == 0) {
        TakeValue taken = take_value(i, rest_argc, rest_argv, "missing value for --key");
        if (taken.rc != 0) {
            *err = taken.err;
            return taken.rc;
        }
        out->key_raw = taken.value;
        return 0;
    }
    if (strcmp(arg, "--tag") == 0) {
        TakeValue taken = take_value(i, rest_argc, rest_argv, "missing value for --tag");
        if (taken.rc != 0) {
            *err = taken.err;
            return -1;
        }
        if (push_cstr_ptr(&out->tag_raw, &out->ntag_raw, tag_cap, taken.value) != 0) {
            *err = "out of memory";
            return -1;
        }
        return 0;
    }
    if (strcmp(arg, "--ttl") == 0) {
        TakeValue taken = take_value(i, rest_argc, rest_argv, "missing value for --ttl");
        if (taken.rc != 0) {
            *err = taken.err;
            return taken.rc;
        }
        out->ttl_raw = taken.value;
        return 0;
    }
    if (strcmp(arg, "--expires") == 0) {
        TakeValue taken = take_value(i, rest_argc, rest_argv, "missing value for --expires");
        if (taken.rc != 0) {
            *err = taken.err;
            return taken.rc;
        }
        out->expires_raw = taken.value;
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
    int i = 0;
    int end_opts = 0;
    size_t tag_cap = 0U;

    out->source = "unknown";
    out->key_raw = NULL;
    out->body_raw = NULL;
    out->tag_raw = NULL;
    out->ntag_raw = 0U;
    out->body_literal = false;
    out->ttl_raw = NULL;
    out->expires_raw = NULL;
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
    char now[ISO_TS_BUFSIZE];
    char expires_iso[ISO_TS_BUFSIZE];
    const char *expires_at = NULL;
    const char *key_or_null = NULL;
    Entry entry;
    StoreAddAction action = STORE_ADD_CREATED;
    StoreStatus st = STORE_OK;
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
        err_msg("invalid source (use human, agent, tool, share, or unknown)");
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
    if (utc_now(now, sizeof(now)) != 0) {
        err_msg("internal error");
        goto cleanup;
    }
    {
        ExpiryResult exp = resolve_expiry_flags(
            (ExpiryFlags){.ttl_raw = parsed.ttl_raw, .expires_raw = parsed.expires_raw}, now,
            expires_iso, sizeof(expires_iso));
        if (exp.rc != 0) {
            err_msg(exp.err);
            goto cleanup;
        }
        expires_at = exp.expires;
    }
    st = store_add(s, body, hash, key_or_null, (const char *const *)tags_norm, ntags, parsed.source,
                   expires_at, now, &action, &entry);
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
