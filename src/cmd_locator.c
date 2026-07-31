#include "commands.h"
#include "commands_common.h"

#include "appio.h"
#include "exit_codes.h"
#include "normalize.h"
#include "output.h"
#include "store.h"

#include <errno.h>
#include <stdbool.h>
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
            (void)fprintf(app_err(), "remember: unknown option '%s'\n", arg);
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
        if (output_get_envelope(app_out(), &entry) != 0) {
            store_entry_free(&entry);
            err_msg("failed to write output");
            return REMEMBER_ERR;
        }
    } else {
        /* Body via output.c — never raw fputs (terminal control neutralization). */
        if (output_body_human(app_out(), entry.body) != 0) {
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
        if (output_action_envelope(app_out(), "deleted", &entry) != 0) {
            store_entry_free(&entry);
            err_msg("failed to write output");
            return REMEMBER_ERR;
        }
    }
    /* Human delete: silent success (design: no id ack required for delete). */
    store_entry_free(&entry);
    return REMEMBER_OK;
}

/* ---- update: locator + opt-in --text / --tag / --clear-tags ------------- */

typedef struct {
    LocatorParse loc;
    const char *text_raw;
    bool set_text;
    bool clear_tags;
    const char **tag_raw;
    size_t ntag_raw;
} UpdateParse;

static void update_parse_free(UpdateParse *p)
{
    free((void *)p->tag_raw);
    p->tag_raw = NULL;
    p->ntag_raw = 0U;
}

/* Returns: 1 end-opts, 0 handled flag, 2 positional, -1 error. */
static int handle_update_flag(const char *arg, int *i, int rest_argc, const char **rest_argv,
                              UpdateParse *out, size_t *tag_cap, const char **err)
{
    if (strcmp(arg, "--") == 0) {
        return 1;
    }
    if (strcmp(arg, "--key") == 0) {
        return take_value(i, rest_argc, rest_argv, &out->loc.key_raw, err,
                          "missing value for --key");
    }
    if (strcmp(arg, "--text") == 0) {
        if (take_value(i, rest_argc, rest_argv, &out->text_raw, err, "missing value for --text") !=
            0) {
            return -1;
        }
        out->set_text = true;
        return 0;
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
    if (strcmp(arg, "--clear-tags") == 0) {
        out->clear_tags = true;
        return 0;
    }
    if (strcmp(arg, "--source") == 0) {
        *err = "--source is only valid on add";
        return -1;
    }
    if (arg[0] == '-' && arg[1] != '\0') {
        (void)fprintf(app_err(), "remember: unknown option '%s'\n", arg);
        *err = "";
        return -1;
    }
    return 2;
}

static int parse_update_args(int rest_argc, const char **rest_argv, UpdateParse *out,
                             const char **err)
{
    int i;
    int end_opts = 0;
    size_t tag_cap = 0U;

    out->loc.key_raw = NULL;
    out->loc.id_raw = NULL;
    out->text_raw = NULL;
    out->set_text = false;
    out->clear_tags = false;
    out->tag_raw = NULL;
    out->ntag_raw = 0U;
    *err = NULL;

    for (i = 0; i < rest_argc; i++) {
        const char *arg = rest_argv[i];

        if (!end_opts) {
            int kind = handle_update_flag(arg, &i, rest_argc, rest_argv, out, &tag_cap, err);
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
        if (out->loc.id_raw != NULL) {
            *err = "too many arguments";
            return -1;
        }
        out->loc.id_raw = arg;
    }
    return 0;
}

static int update_validate_changes(const UpdateParse *p, const char **err)
{
    if (p->clear_tags && p->ntag_raw > 0U) {
        *err = "cannot combine --tag and --clear-tags";
        return -1;
    }
    if (!p->set_text && !p->clear_tags && p->ntag_raw == 0U) {
        *err = "update requires --text, --tag, or --clear-tags";
        return -1;
    }
    return 0;
}

static int emit_update_result(bool json, const Entry *entry)
{
    if (json) {
        if (output_action_envelope(app_out(), "updated", entry) != 0) {
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

/* Normalize optional body/tags from parse; returns -1 after err_msg on failure. */
static int update_prepare_payload(const UpdateParse *parsed, char **body, size_t *body_len,
                                  char *hash, char ***tags_norm, size_t *ntags, bool *set_tags)
{
    const char *err = NULL;

    *set_tags = false;
    if (parsed->clear_tags) {
        *set_tags = true;
    }
    if (parsed->ntag_raw > 0U) {
        *set_tags = true;
    }

    if (parsed->set_text) {
        if (load_body(parsed->text_raw, body, body_len, &err) != 0) {
            err_msg(err);
            return -1;
        }
        body_hash_hex(*body, *body_len, hash);
    }
    if (parsed->ntag_raw > 0U) {
        if (normalize_tags((const char *const *)parsed->tag_raw, parsed->ntag_raw, tags_norm, ntags,
                           &err) != 0) {
            err_msg(err);
            return -1;
        }
    }
    return 0;
}

int cmd_update(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    UpdateParse parsed;
    const char *err = NULL;
    char key_norm[REMEMBER_TOKEN_MAX + 1];
    const char *key_or_null = NULL;
    long long id = 0;
    char **tags_norm = NULL;
    size_t ntags = 0U;
    char *body = NULL;
    size_t body_len = 0U;
    char hash[REMEMBER_SHA256_HEX_LEN + 1];
    bool set_tags = false;
    const char *body_hash = NULL;
    Entry entry;
    StoreStatus st;
    long long conflict_id = 0;
    int rc = REMEMBER_ERR;

    memset(&entry, 0, sizeof(entry));
    memset(key_norm, 0, sizeof(key_norm));
    memset(hash, 0, sizeof(hash));

    if (parse_update_args(rest_argc, rest_argv, &parsed, &err) != 0) {
        if (err != NULL && err[0] != '\0') {
            err_msg(err);
        }
        update_parse_free(&parsed);
        return REMEMBER_ERR;
    }
    if (locator_validate(&parsed.loc) != 0 || update_validate_changes(&parsed, &err) != 0) {
        if (err != NULL) {
            err_msg(err);
        }
        update_parse_free(&parsed);
        return REMEMBER_ERR;
    }
    if (locator_resolve(&parsed.loc, key_norm, sizeof(key_norm), &key_or_null, &id) != 0) {
        update_parse_free(&parsed);
        return REMEMBER_ERR;
    }
    if (update_prepare_payload(&parsed, &body, &body_len, hash, &tags_norm, &ntags, &set_tags) !=
        0) {
        goto cleanup;
    }
    if (parsed.set_text) {
        body_hash = hash;
    }

    st = store_update(s, id, key_or_null, parsed.set_text, body, body_hash, set_tags,
                      (const char *const *)tags_norm, ntags, &entry, &conflict_id);
    if (st == STORE_ERR_CONFLICT) {
        (void)fprintf(app_err(), "remember: body hash conflicts with entry %lld\n", conflict_id);
        goto cleanup;
    }
    if (st != STORE_OK) {
        rc = store_status_to_exit(st);
        goto cleanup;
    }
    if (emit_update_result(json, &entry) != 0) {
        goto cleanup;
    }
    rc = REMEMBER_OK;

cleanup:
    update_parse_free(&parsed);
    free(body);
    free_tag_list(tags_norm, ntags);
    store_entry_free(&entry);
    return rc;
}
