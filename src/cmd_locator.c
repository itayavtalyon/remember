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

/* Locator (get / delete): exactly one of id | --key | --sync-id. */

typedef struct {
    const char *key_raw;
    const char *id_raw;
    const char *sync_id_raw;
    CmdBinOpts bins;
} LocatorParse;

/* Shared by get/delete: --key / --sync-id, positional id, reject --source and unknowns. */
static int locator_take_option(const char *arg, int *i, int rest_argc, const char **rest_argv,
                               LocatorParse *out)
{
    if (strcmp(arg, "--key") == 0) {
        if (*i + 1 >= rest_argc) {
            err_msg("missing value for --key");
            return -1;
        }
        out->key_raw = rest_argv[++(*i)];
        return 1;
    }
    if (strcmp(arg, "--sync-id") == 0) {
        if (*i + 1 >= rest_argc) {
            err_msg("missing value for --sync-id");
            return -1;
        }
        out->sync_id_raw = rest_argv[++(*i)];
        return 1;
    }
    if (cmd_bin_take_flag(arg, &out->bins) != 0) {
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

static int parse_locator_args(int rest_argc, const char **rest_argv, LocatorParse *out)
{
    int i = 0;
    int end_opts = 0;

    out->key_raw = NULL;
    out->id_raw = NULL;
    out->sync_id_raw = NULL;
    memset(&out->bins, 0, sizeof(out->bins));

    for (i = 0; i < rest_argc; i++) {
        const char *arg = rest_argv[i];

        if (!end_opts && strcmp(arg, "--") == 0) {
            end_opts = 1;
            continue;
        }
        if (!end_opts) {
            int kind = locator_take_option(arg, &i, rest_argc, rest_argv, out);
            if (kind < 0) {
                return -1;
            }
            if (kind > 0) {
                continue;
            }
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
    int n = 0;

    if (p->key_raw != NULL) {
        n++;
    }
    if (p->id_raw != NULL) {
        n++;
    }
    if (p->sync_id_raw != NULL) {
        n++;
    }
    if (n > 1) {
        err_msg("provide exactly one of id, --key, or --sync-id");
        return -1;
    }
    if (n == 0) {
        err_msg("missing id, --key, or --sync-id");
        return -1;
    }
    return 0;
}

/* Which form the locator uses after validate. */
typedef enum { LOC_ID = 0, LOC_KEY, LOC_SYNC_ID } LocatorKind;

typedef struct {
    long long id;
    const char *key;     /* points into key_norm when kind==KEY */
    const char *sync_id; /* points into parse when kind==SYNC_ID */
    LocatorKind kind;
    char pad_[4]; /* explicit tail padding (kept -Wpadded-clean) */
} LocatorResolved;

/* Normalize key / parse id / check sync_id canonical. */
static int locator_resolve(const LocatorParse *p, char *key_norm, size_t key_norm_sz,
                           LocatorResolved *out)
{
    out->kind = LOC_ID;
    out->id = 0;
    out->key = NULL;
    out->sync_id = NULL;
    if (p->key_raw != NULL) {
        NormStatus ns = normalize_key(p->key_raw, key_norm, key_norm_sz);
        if (ns != NORM_OK) {
            err_msg(norm_token_message(ns, "key"));
            return -1;
        }
        out->kind = LOC_KEY;
        out->key = key_norm;
        return 0;
    }
    if (p->sync_id_raw != NULL) {
        if (store_sync_id_is_canonical(p->sync_id_raw) == 0) {
            err_msg("invalid sync-id");
            return -1;
        }
        out->kind = LOC_SYNC_ID;
        out->sync_id = p->sync_id_raw;
        return 0;
    }
    if (parse_entry_id(p->id_raw, &out->id) != 0) {
        err_msg("invalid id");
        return -1;
    }
    out->kind = LOC_ID;
    return 0;
}

int cmd_get(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    LocatorParse parsed;
    LocatorResolved loc;
    Entry entry;
    char key_norm[REMEMBER_TOKEN_MAX + 1];
    char now[ISO_TS_BUFSIZE];
    StoreStatus st = STORE_OK;
    StoreBin bin = STORE_BIN_LIVE;
    int rc = 0;

    memset(&entry, 0, sizeof(entry));
    memset(&loc, 0, sizeof(loc));
    if (parse_locator_args(rest_argc, rest_argv, &parsed) != 0) {
        return REMEMBER_ERR;
    }
    if (locator_validate(&parsed) != 0) {
        return REMEMBER_ERR;
    }
    if (cmd_bin_resolve(&parsed.bins, &bin) != 0) {
        return REMEMBER_ERR;
    }
    if (locator_resolve(&parsed, key_norm, sizeof(key_norm), &loc) != 0) {
        return REMEMBER_ERR;
    }
    if (utc_now(now, sizeof(now)) != 0) {
        err_msg("internal error");
        return REMEMBER_ERR;
    }

    if (loc.kind == LOC_KEY) {
        st = store_get_by_key(s, loc.key, bin, now, &entry);
    } else if (loc.kind == LOC_SYNC_ID) {
        st = store_get_by_sync_id(s, loc.sync_id, bin, now, &entry);
    } else {
        st = store_get(s, loc.id, bin, now, &entry);
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
        if (bin != STORE_BIN_DELETED) {
            cmd_neighbors_drop_deleted(links, &nlinks, now);
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

/* Soft or hard delete after locator_resolve. */
static StoreStatus delete_resolved(Store *s, const LocatorResolved *loc, StoreBin bin,
                                   const char *now, Entry *entry)
{
    if (loc->kind == LOC_SYNC_ID) {
        Entry found;
        memset(&found, 0, sizeof(found));
        if (bin == STORE_BIN_LIVE) {
            StoreStatus st = store_get_row_by_sync_id(s, loc->sync_id, &found);
            if (st == STORE_OK && found.deleted_at != NULL) {
                store_entry_free(&found);
                return STORE_ERR_DELETED;
            }
            if (st == STORE_OK) {
                long long sid = found.id;
                store_entry_free(&found);
                return store_soft_delete_by_id(s, sid, now, entry);
            }
            return st;
        }
        {
            StoreStatus st = store_get_by_sync_id(s, loc->sync_id, bin, now, &found);
            if (st == STORE_OK) {
                long long hid = found.id;
                store_entry_free(&found);
                return store_hard_delete_by_id(s, hid, bin, now, entry);
            }
            return st;
        }
    }
    if (loc->kind == LOC_KEY) {
        if (bin == STORE_BIN_LIVE) {
            return store_soft_delete_by_key(s, loc->key, now, entry);
        }
        return store_hard_delete_by_key(s, loc->key, bin, now, entry);
    }
    if (bin == STORE_BIN_LIVE) {
        return store_soft_delete_by_id(s, loc->id, now, entry);
    }
    return store_hard_delete_by_id(s, loc->id, bin, now, entry);
}

int cmd_delete(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    LocatorParse parsed;
    LocatorResolved loc;
    Entry entry;
    char key_norm[REMEMBER_TOKEN_MAX + 1];
    char now[ISO_TS_BUFSIZE];
    StoreStatus st = STORE_OK;
    StoreBin bin = STORE_BIN_LIVE;
    int rc = 0;

    memset(&entry, 0, sizeof(entry));
    memset(&loc, 0, sizeof(loc));
    if (parse_locator_args(rest_argc, rest_argv, &parsed) != 0) {
        return REMEMBER_ERR;
    }
    if (locator_validate(&parsed) != 0) {
        return REMEMBER_ERR;
    }
    if (cmd_bin_resolve(&parsed.bins, &bin) != 0) {
        return REMEMBER_ERR;
    }
    if (locator_resolve(&parsed, key_norm, sizeof(key_norm), &loc) != 0) {
        return REMEMBER_ERR;
    }
    if (utc_now(now, sizeof(now)) != 0) {
        err_msg("internal error");
        return REMEMBER_ERR;
    }

    st = delete_resolved(s, &loc, bin, now, &entry);
    rc = store_status_to_exit(st);
    if (rc != REMEMBER_OK) {
        return rc;
    }

    if (json) {
        if (output_action_envelope(app_out(), "deleted", &entry, now) != 0) {
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
    const char **tag_raw;
    const char *ttl_raw;
    const char *expires_raw;
    size_t ntag_raw;
    bool set_text;
    /* True for `--text=VALUE` (VALUE may be "-"); false for `--text -` (stdin). */
    bool text_literal;
    bool clear_tags;
    bool clear_expires;
    bool undelete;
    char pad_[3]; /* explicit tail padding (kept -Wpadded-clean) */
} UpdateParse;

static void update_parse_free(UpdateParse *p)
{
    free((void *)p->tag_raw);
    p->tag_raw = NULL;
    p->ntag_raw = 0U;
}

static int handle_update_ttl_flag(const char *arg, int *i, int rest_argc, const char **rest_argv,
                                  UpdateParse *out, const char **err);

/* Returns: 1 end-opts, 0 handled flag, 2 positional, -1 error. */
static int handle_update_flag(const char *arg, int *i, int rest_argc, const char **rest_argv,
                              UpdateParse *out, size_t *tag_cap, const char **err)
{
    if (strcmp(arg, "--") == 0) {
        return 1;
    }
    if (strcmp(arg, "--key") == 0) {
        TakeValue taken = take_value(i, rest_argc, rest_argv, "missing value for --key");
        if (taken.rc != 0) {
            *err = taken.err;
            return taken.rc;
        }
        out->loc.key_raw = taken.value;
        return 0;
    }
    if (strcmp(arg, "--sync-id") == 0) {
        TakeValue taken = take_value(i, rest_argc, rest_argv, "missing value for --sync-id");
        if (taken.rc != 0) {
            *err = taken.err;
            return taken.rc;
        }
        out->loc.sync_id_raw = taken.value;
        return 0;
    }
    /* `--text=-` (or any `--text=VALUE`) is always a literal body, including "-". */
    if (strncmp(arg, "--text=", sizeof("--text=") - 1U) == 0) {
        out->text_raw = arg + (sizeof("--text=") - 1U);
        out->set_text = true;
        out->text_literal = true;
        return 0;
    }
    if (strcmp(arg, "--text") == 0) {
        TakeValue taken = take_value(i, rest_argc, rest_argv, "missing value for --text");
        if (taken.rc != 0) {
            *err = taken.err;
            return -1;
        }
        out->text_raw = taken.value;
        out->set_text = true;
        out->text_literal = false; /* bare "-" still means stdin */
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
    if (strcmp(arg, "--clear-tags") == 0) {
        out->clear_tags = true;
        return 0;
    }
    if (cmd_bin_take_flag(arg, &out->loc.bins) != 0) {
        return 0;
    }
    return handle_update_ttl_flag(arg, i, rest_argc, rest_argv, out, err);
}

static int handle_update_ttl_flag(const char *arg, int *i, int rest_argc, const char **rest_argv,
                                  UpdateParse *out, const char **err)
{
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
    if (strcmp(arg, "--clear-expires") == 0) {
        out->clear_expires = true;
        return 0;
    }
    if (strcmp(arg, "--undelete") == 0) {
        out->undelete = true;
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
    out->loc.sync_id_raw = NULL;
    out->text_raw = NULL;
    out->set_text = false;
    out->text_literal = false;
    out->clear_tags = false;
    out->tag_raw = NULL;
    out->ntag_raw = 0U;
    out->ttl_raw = NULL;
    out->expires_raw = NULL;
    out->clear_expires = false;
    out->undelete = false;
    memset(&out->loc.bins, 0, sizeof(out->loc.bins));
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
    if (p->undelete && (p->ttl_raw != NULL || p->expires_raw != NULL || p->clear_expires)) {
        *err = "cannot combine --undelete with --ttl, --expires, or --clear-expires";
        return -1;
    }
    if (!p->set_text && !p->clear_tags && p->ntag_raw == 0U && !p->clear_expires &&
        p->ttl_raw == NULL && p->expires_raw == NULL && !p->undelete) {
        *err = "update requires --text, --tag, --clear-tags, --ttl, --expires, --clear-expires, or "
               "--undelete";
        return -1;
    }
    return 0;
}

static int emit_update_result(bool json, const Entry *entry, const char *now)
{
    if (json) {
        if (output_action_envelope(app_out(), "updated", entry, now) != 0) {
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

/* Resolve --clear-expires / --ttl / --expires into (set_expires, expires_at).
   0 ok; -1 usage error (message already printed). expires_at points into buf. */
static int update_resolve_expiry(const UpdateParse *parsed, const char *now, char *buf,
                                 size_t buflen, bool *out_set, const char **out_expires)
{
    /* Init satisfies clang-tidy init-variables; keeping the declaration before the
       early clear-expires return keeps clang's -Wdeclaration-after-statement happy,
       so cppcheck's redundantInitialization is the one that must yield here. */
    ExpiryResult exp = {.rc = 0};

    *out_set = false;
    *out_expires = NULL;
    if (parsed->clear_expires) {
        *out_set = true;
        return 0;
    }
    /* cppcheck-suppress redundantInitialization */
    exp = resolve_expiry_flags(
        (ExpiryFlags){.ttl_raw = parsed->ttl_raw, .expires_raw = parsed->expires_raw}, now, buf,
        buflen);
    if (exp.rc != 0) {
        err_msg(exp.err);
        return -1;
    }
    *out_expires = exp.expires;
    *out_set = (exp.expires != NULL);
    return 0;
}

/* Map a resolved locator to store_update/store_rekey (id or key_or_null).
   For SYNC_ID, loads the row and sets *out_id. Returns process exit or 0. */
static int locator_bind_write(Store *s, const LocatorResolved *loc, StoreBin bin, const char *now,
                              long long *out_id, const char **out_key)
{
    *out_id = 0;
    *out_key = NULL;
    if (loc->kind == LOC_KEY) {
        *out_key = loc->key;
        return 0;
    }
    if (loc->kind == LOC_SYNC_ID) {
        Entry found;
        StoreStatus st = STORE_OK;
        memset(&found, 0, sizeof(found));
        st = store_get_by_sync_id(s, loc->sync_id, bin, now, &found);
        if (st != STORE_OK) {
            return store_status_to_exit(st);
        }
        *out_id = found.id;
        store_entry_free(&found);
        return 0;
    }
    *out_id = loc->id;
    return 0;
}

int cmd_update(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    UpdateParse parsed;
    LocatorResolved loc;
    const char *err = NULL;
    char key_norm[REMEMBER_TOKEN_MAX + 1];
    const char *key_or_null = NULL;
    long long id = 0;
    char **tags_norm = NULL;
    size_t ntags = 0U;
    char *body = NULL;
    size_t body_len = 0U;
    char hash[REMEMBER_SHA256_HEX_LEN + 1];
    char now[ISO_TS_BUFSIZE];
    char expires_iso[ISO_TS_BUFSIZE];
    const char *expires_at = NULL;
    bool set_expires = false;
    bool set_tags = false;
    const char *body_hash = NULL;
    Entry entry;
    StoreStatus st = STORE_OK;
    StoreBin bin = STORE_BIN_LIVE;
    long long conflict_id = 0;
    int rc = REMEMBER_ERR;
    int bind_rc = 0;

    memset(&entry, 0, sizeof(entry));
    memset(&loc, 0, sizeof(loc));
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
    if (cmd_bin_resolve(&parsed.loc.bins, &bin) != 0) {
        update_parse_free(&parsed);
        return REMEMBER_ERR;
    }
    if (locator_resolve(&parsed.loc, key_norm, sizeof(key_norm), &loc) != 0) {
        update_parse_free(&parsed);
        return REMEMBER_ERR;
    }
    if (utc_now(now, sizeof(now)) != 0) {
        err_msg("internal error");
        update_parse_free(&parsed);
        return REMEMBER_ERR;
    }
    bind_rc = locator_bind_write(s, &loc, bin, now, &id, &key_or_null);
    if (bind_rc != 0) {
        update_parse_free(&parsed);
        return bind_rc;
    }
    if (update_prepare_payload(&parsed, &body, &body_len, hash, &tags_norm, &ntags, &set_tags) !=
        0) {
        goto cleanup;
    }
    if (parsed.set_text) {
        body_hash = hash;
    }
    if (update_resolve_expiry(&parsed, now, expires_iso, sizeof(expires_iso), &set_expires,
                              &expires_at) != 0) {
        goto cleanup;
    }
    st = store_update(s, id, key_or_null, parsed.set_text, body, body_hash, set_tags,
                      (const char *const *)tags_norm, ntags, set_expires, expires_at,
                      parsed.undelete, bin, now, &entry, &conflict_id);
    if (st == STORE_ERR_CONFLICT) {
        (void)fprintf(app_err(), "remember: body hash conflicts with entry %lld\n", conflict_id);
        goto cleanup;
    }
    if (st != STORE_OK) {
        rc = store_status_to_exit(st);
        goto cleanup;
    }
    if (emit_update_result(json, &entry, now) != 0) {
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
    /* NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers) */
    char pad_[7]; /* explicit tail padding (kept -Wpadded-clean) */
} RekeyParse;

static int handle_rekey_flag(const char *arg, int *i, int rest_argc, const char **rest_argv,
                             RekeyParse *out)
{

    if (strcmp(arg, "--") == 0) {
        return 2;
    }
    if (strcmp(arg, "--key") == 0) {
        TakeValue taken = take_value(i, rest_argc, rest_argv, "missing value for --key");
        if (taken.rc != 0) {
            err_msg(taken.err);
            return -1;
        }
        out->loc.key_raw = taken.value;
        return 1;
    }
    if (strcmp(arg, "--sync-id") == 0) {
        TakeValue taken = take_value(i, rest_argc, rest_argv, "missing value for --sync-id");
        if (taken.rc != 0) {
            err_msg(taken.err);
            return -1;
        }
        out->loc.sync_id_raw = taken.value;
        return 1;
    }
    if (strcmp(arg, "--to-key") == 0) {
        TakeValue taken = take_value(i, rest_argc, rest_argv, "missing value for --to-key");
        if (taken.rc != 0) {
            err_msg(taken.err);
            return -1;
        }
        out->to_key_raw = taken.value;
        return 1;
    }
    if (strcmp(arg, "--clear-key") == 0) {
        out->clear_key = true;
        return 1;
    }
    if (cmd_bin_take_flag(arg, &out->loc.bins) != 0) {
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
    LocatorResolved loc;
    char key_norm[REMEMBER_TOKEN_MAX + 1];
    char new_norm[REMEMBER_TOKEN_MAX + 1];
    const char *key = NULL;
    const char *new_key = NULL;
    long long id = 0;
    char now[ISO_TS_BUFSIZE];
    Entry entry;
    StoreStatus st = STORE_OK;
    StoreBin bin = STORE_BIN_LIVE;
    long long conflict_id = 0;
    int rc = 0;

    memset(&entry, 0, sizeof(entry));
    memset(&loc, 0, sizeof(loc));
    if (parse_rekey_args(rest_argc, rest_argv, &parsed) != 0) {
        return REMEMBER_ERR;
    }
    if (locator_validate(&parsed.loc) != 0) {
        return REMEMBER_ERR;
    }
    if (cmd_bin_resolve(&parsed.loc.bins, &bin) != 0) {
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
    if (locator_resolve(&parsed.loc, key_norm, sizeof(key_norm), &loc) != 0) {
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
    {
        int bind_rc = locator_bind_write(s, &loc, bin, now, &id, &key);
        if (bind_rc != 0) {
            return bind_rc;
        }
    }
    st = store_rekey(s, id, (RekeyKeys){.key_or_null = key, .new_key_or_null = new_key}, bin, now,
                     &entry, &conflict_id);
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
        if (output_action_envelope(app_out(), "updated", &entry, now) != 0) {
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
