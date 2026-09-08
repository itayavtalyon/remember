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

/* Locator (get / delete): exactly one of id | --key. */

typedef struct {
    const char *key_raw;
    const char *id_raw;
    bool trash;
} LocatorParse;

/* Shared by get/delete: --key, positional id, reject --source and unknowns. */
static int parse_locator_args(int rest_argc, const char **rest_argv, LocatorParse *out)
{
    int i = 0;
    int end_opts = 0;

    out->key_raw = NULL;
    out->id_raw = NULL;
    out->trash = false;

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
        if (!end_opts && strcmp(arg, "--trash") == 0) {
            out->trash = true;
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
    if (parse_entry_id(p->id_raw, out_id) != 0) {
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
    char now[32];
    const char *key = NULL;
    long long id = 0;
    StoreStatus st = STORE_OK;
    int rc = 0;

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
    if (utc_now(now, sizeof(now)) != 0) {
        err_msg("internal error");
        return REMEMBER_ERR;
    }

    if (key != NULL) {
        st = store_get_by_key(s, key, parsed.trash, now, &entry);
    } else {
        st = store_get(s, id, parsed.trash, now, &entry);
    }
    rc = store_status_to_exit(st);
    if (rc != REMEMBER_OK) {
        return rc;
    }

    {
        StoreNeighbor *links = NULL;
        size_t nlinks = 0U;
        int wr = 0;

        st = store_list_neighbors(s, entry.id, NULL, STORE_NEIGHBOR_ALL, now, &links, &nlinks);
        if (st != STORE_OK) {
            store_entry_free(&entry);
            err_msg(store_status_message(st));
            return REMEMBER_ERR;
        }
        if (json) {
            wr = output_get_envelope(app_out(), &entry, links, nlinks, now);
        } else {
            wr = output_body_human(app_out(), entry.body);
            if (wr == 0) {
                wr = output_related_human(app_out(), links, nlinks, now);
            }
        }
        store_neighbors_free(links, nlinks);
        store_entry_free(&entry);
        if (wr != 0) {
            err_msg("failed to write output");
            return REMEMBER_ERR;
        }
    }
    return REMEMBER_OK;
}

int cmd_delete(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    LocatorParse parsed;
    Entry entry;
    char key_norm[REMEMBER_TOKEN_MAX + 1];
    char now[32];
    const char *key = NULL;
    long long id = 0;
    StoreStatus st = STORE_OK;
    int rc = 0;

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
    if (utc_now(now, sizeof(now)) != 0) {
        err_msg("internal error");
        return REMEMBER_ERR;
    }

    if (key != NULL) {
        st = store_delete_by_key(s, key, parsed.trash, now, &entry);
    } else {
        st = store_delete_by_id(s, id, parsed.trash, now, &entry);
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
    /* True for `--text=VALUE` (VALUE may be "-"); false for `--text -` (stdin). */
    bool text_literal;
    bool clear_tags;
    const char **tag_raw;
    size_t ntag_raw;
    const char *ttl_raw;
    const char *expires_raw;
    bool clear_expires;
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
    /* `--text=-` (or any `--text=VALUE`) is always a literal body, including "-". */
    if (strncmp(arg, "--text=", 7) == 0) {
        out->text_raw = arg + 7;
        out->set_text = true;
        out->text_literal = true;
        return 0;
    }
    if (strcmp(arg, "--text") == 0) {
        if (take_value(i, rest_argc, rest_argv, &out->text_raw, err, "missing value for --text") !=
            0) {
            return -1;
        }
        out->set_text = true;
        out->text_literal = false; /* bare "-" still means stdin */
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
    if (strcmp(arg, "--trash") == 0) {
        out->loc.trash = true;
        return 0;
    }
    if (strcmp(arg, "--ttl") == 0) {
        return take_value(i, rest_argc, rest_argv, &out->ttl_raw, err, "missing value for --ttl");
    }
    if (strcmp(arg, "--expires") == 0) {
        return take_value(i, rest_argc, rest_argv, &out->expires_raw, err,
                          "missing value for --expires");
    }
    if (strcmp(arg, "--clear-expires") == 0) {
        out->clear_expires = true;
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
    int i = 0;
    int end_opts = 0;
    size_t tag_cap = 0U;

    out->loc.key_raw = NULL;
    out->loc.id_raw = NULL;
    out->text_raw = NULL;
    out->set_text = false;
    out->text_literal = false;
    out->clear_tags = false;
    out->tag_raw = NULL;
    out->ntag_raw = 0U;
    out->ttl_raw = NULL;
    out->expires_raw = NULL;
    out->clear_expires = false;
    out->loc.trash = false;
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
    if (p->clear_expires && (p->ttl_raw != NULL || p->expires_raw != NULL)) {
        *err = "cannot combine --clear-expires with --ttl or --expires";
        return -1;
    }
    if (p->ttl_raw != NULL && p->expires_raw != NULL) {
        *err = "cannot combine --ttl and --expires";
        return -1;
    }
    if (!p->set_text && !p->clear_tags && p->ntag_raw == 0U && !p->clear_expires &&
        p->ttl_raw == NULL && p->expires_raw == NULL) {
        *err = "update requires --text, --tag, --clear-tags, --ttl, --expires, or --clear-expires";
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
        {
            int dash_is_stdin = 1;
            if (parsed->text_literal) {
                dash_is_stdin = 0;
            }
            if (load_body(parsed->text_raw, dash_is_stdin, body, body_len, &err) != 0) {
                err_msg(err);
                return -1;
            }
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
    char now[32];
    char expires_iso[32];
    const char *expires_at = NULL;
    bool set_expires = false;
    bool set_tags = false;
    const char *body_hash = NULL;
    Entry entry;
    StoreStatus st = STORE_OK;
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

    if (utc_now(now, sizeof(now)) != 0) {
        err_msg("internal error");
        goto cleanup;
    }
    if (parsed.clear_expires) {
        set_expires = true;
        expires_at = NULL;
    } else if (resolve_expiry_flags(parsed.ttl_raw, parsed.expires_raw, now, expires_iso,
                                    sizeof(expires_iso), &expires_at, &err) != 0) {
        err_msg(err);
        goto cleanup;
    } else if (expires_at != NULL) {
        set_expires = true;
    }
    st = store_update(s, id, key_or_null, parsed.set_text, body, body_hash, set_tags,
                      (const char *const *)tags_norm, ntags, set_expires, expires_at,
                      parsed.loc.trash, now, &entry, &conflict_id);
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

typedef struct {
    LocatorParse loc;
    const char *to_key_raw;
    bool clear_key;
} RekeyParse;

static int handle_rekey_flag(const char *arg, int *i, int rest_argc, const char **rest_argv,
                             RekeyParse *out)
{
    const char *err = NULL;

    if (strcmp(arg, "--") == 0) {
        return 2;
    }
    if (strcmp(arg, "--key") == 0) {
        if (take_value(i, rest_argc, rest_argv, &out->loc.key_raw, &err,
                       "missing value for --key") != 0) {
            err_msg(err);
            return -1;
        }
        return 1;
    }
    if (strcmp(arg, "--to-key") == 0) {
        if (take_value(i, rest_argc, rest_argv, &out->to_key_raw, &err,
                       "missing value for --to-key") != 0) {
            err_msg(err);
            return -1;
        }
        return 1;
    }
    if (strcmp(arg, "--clear-key") == 0) {
        out->clear_key = true;
        return 1;
    }
    if (strcmp(arg, "--trash") == 0) {
        out->loc.trash = true;
        return 1;
    }
    if (strcmp(arg, "--source") == 0) {
        err_msg("--source is only valid on add");
        return -1;
    }
    if (arg[0] == '-' && arg[1] != '\0') {
        (void)fprintf(app_err(), "remember: unknown option '%s'\n", arg);
        return -1;
    }
    return 0;
}

static int parse_rekey_args(int rest_argc, const char **rest_argv, RekeyParse *out)
{
    int i = 0;
    int end_opts = 0;

    memset(out, 0, sizeof(*out));
    for (i = 0; i < rest_argc; i++) {
        const char *arg = rest_argv[i];

        if (!end_opts) {
            int kind = handle_rekey_flag(arg, &i, rest_argc, rest_argv, out);
            if (kind == 2) {
                end_opts = 1;
                continue;
            }
            if (kind == 1) {
                continue;
            }
            if (kind < 0) {
                return -1;
            }
        }
        if (out->loc.id_raw != NULL) {
            err_msg("too many arguments");
            return -1;
        }
        out->loc.id_raw = arg;
    }
    return 0;
}

int cmd_rekey(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    RekeyParse parsed;
    char key_norm[REMEMBER_TOKEN_MAX + 1];
    char new_norm[REMEMBER_TOKEN_MAX + 1];
    const char *key = NULL;
    const char *new_key = NULL;
    long long id = 0;
    char now[32];
    Entry entry;
    StoreStatus st = STORE_OK;
    long long conflict_id = 0;
    int rc = 0;

    memset(&entry, 0, sizeof(entry));
    if (parse_rekey_args(rest_argc, rest_argv, &parsed) != 0) {
        return REMEMBER_ERR;
    }
    if (locator_validate(&parsed.loc) != 0) {
        return REMEMBER_ERR;
    }
    if (parsed.clear_key && parsed.to_key_raw != NULL) {
        err_msg("cannot combine --to-key and --clear-key");
        return REMEMBER_ERR;
    }
    if (!parsed.clear_key && parsed.to_key_raw == NULL) {
        err_msg("rekey requires --to-key or --clear-key");
        return REMEMBER_ERR;
    }
    if (locator_resolve(&parsed.loc, key_norm, sizeof(key_norm), &key, &id) != 0) {
        return REMEMBER_ERR;
    }
    if (!parsed.clear_key) {
        NormStatus ns = normalize_key(parsed.to_key_raw, new_norm, sizeof(new_norm));
        if (ns != NORM_OK) {
            err_msg(norm_token_message(ns, "key"));
            return REMEMBER_ERR;
        }
        new_key = new_norm;
    }
    if (utc_now(now, sizeof(now)) != 0) {
        err_msg("internal error");
        return REMEMBER_ERR;
    }
    st = store_rekey(s, id, key, new_key, parsed.loc.trash, now, &entry, &conflict_id);
    if (st == STORE_ERR_CONFLICT) {
        if (new_key != NULL) {
            (void)fprintf(app_err(), "remember: key conflicts with entry %lld\n", conflict_id);
        } else {
            (void)fprintf(app_err(), "remember: body hash conflicts with entry %lld\n",
                          conflict_id);
        }
        store_entry_free(&entry);
        return REMEMBER_ERR;
    }
    rc = store_status_to_exit(st);
    if (rc != REMEMBER_OK) {
        store_entry_free(&entry);
        return rc;
    }
    if (json) {
        if (output_action_envelope(app_out(), "updated", &entry) != 0) {
            store_entry_free(&entry);
            err_msg("failed to write output");
            return REMEMBER_ERR;
        }
    } else if (output_id_human(app_out(), entry.id) != 0) {
        store_entry_free(&entry);
        err_msg("failed to write output");
        return REMEMBER_ERR;
    }
    store_entry_free(&entry);
    return REMEMBER_OK;
}
