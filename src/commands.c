#include "commands.h"

#include "exit_codes.h"
#include "normalize.h"
#include "output.h"
#include "store.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- normalize → user-facing messages ------------------------------------ */

static const char *norm_body_message(NormStatus st)
{
    switch (st) {
    case NORM_OK:
        return "ok";
    case NORM_ERR_EMPTY:
        return "empty body after trim";
    case NORM_ERR_TOO_LONG:
        return "body exceeds 64 KiB";
    case NORM_ERR_INVALID_UTF8:
        return "invalid UTF-8 in body";
    case NORM_ERR_OOM:
        return "out of memory";
    case NORM_ERR_INVALID_CHAR:
    case NORM_ERR_INTERNAL:
    default:
        return "invalid body";
    }
}

static const char *norm_token_message(NormStatus st, const char *kind)
{
    int is_key = (kind != NULL && strcmp(kind, "key") == 0);

    switch (st) {
    case NORM_OK:
        return "ok";
    case NORM_ERR_EMPTY:
        return is_key ? "empty key" : "empty tag";
    case NORM_ERR_TOO_LONG:
        return is_key ? "key exceeds 64 bytes" : "tag exceeds 64 bytes";
    case NORM_ERR_INVALID_UTF8:
        return is_key ? "invalid UTF-8 in key" : "invalid UTF-8 in tag";
    case NORM_ERR_INVALID_CHAR:
        return is_key ? "invalid key" : "invalid tag";
    case NORM_ERR_OOM:
        return "out of memory";
    case NORM_ERR_INTERNAL:
    default:
        return is_key ? "invalid key" : "invalid tag";
    }
}

static void err_msg(const char *msg)
{
    (void)fprintf(stderr, "remember: %s\n", msg);
}

static int source_is_valid(const char *s)
{
    return s != NULL && (strcmp(s, "human") == 0 || strcmp(s, "agent") == 0 ||
                         strcmp(s, "tool") == 0 || strcmp(s, "unknown") == 0);
}

static const char *action_name(StoreAddAction a)
{
    switch (a) {
    case STORE_ADD_CREATED:
        return "created";
    case STORE_ADD_MERGED:
        return "merged";
    case STORE_ADD_UPDATED:
        return "updated";
    default:
        return "created";
    }
}

/* ---- add arg parse ------------------------------------------------------- */

typedef struct {
    const char *source;
    const char *key_raw;
    const char *body_raw;
    const char **tag_raw;
    size_t ntag_raw;
} AddParse;

static void add_parse_free(AddParse *p)
{
    free((void *)p->tag_raw);
    p->tag_raw = NULL;
    p->ntag_raw = 0U;
}

static int add_parse_push_tag(AddParse *p, size_t *cap, const char *t)
{
    if (p->ntag_raw == *cap) {
        size_t ncap = (*cap == 0U) ? 4U : (*cap * 2U);
        const char **grown = (const char **)realloc((void *)p->tag_raw, ncap * sizeof(*grown));
        if (grown == NULL) {
            return -1;
        }
        p->tag_raw = grown;
        *cap = ncap;
    }
    p->tag_raw[p->ntag_raw] = t;
    p->ntag_raw++;
    return 0;
}

/* Take the next token as an option value; advances *i. */
static int take_value(int *i, int rest_argc, const char **rest_argv, const char **out,
                      const char **err, const char *missing_msg)
{
    if (*i + 1 >= rest_argc) {
        *err = missing_msg;
        return -1;
    }
    *i += 1;
    *out = rest_argv[*i];
    return 0;
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
        if (add_parse_push_tag(out, tag_cap, t) != 0) {
            *err = "out of memory";
            return -1;
        }
        return 0;
    }
    if (arg[0] == '-' && arg[1] != '\0') {
        (void)fprintf(stderr, "remember: unknown option '%s'\n", arg);
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
    }
    return 0;
}

static int load_body(const char *body_raw, char **out_body, size_t *out_len, const char **err)
{
    char *stdin_body = NULL;
    size_t stdin_len = 0U;
    NormStatus ns;

    *out_body = NULL;
    *out_len = 0U;
    *err = NULL;

    if (strcmp(body_raw, "-") == 0) {
        /* Read with a generous hard cap; body_trim_copy enforces the real
           post-trim 64 KiB limit, so stdin and argv reject identically. */
        int rr = util_read_stdin(&stdin_body, &stdin_len, REMEMBER_STDIN_MAX);
        if (rr == -2) {
            *err = "stdin input too large";
            return -1;
        }
        if (rr != 0 || stdin_body == NULL) {
            *err = "failed to read body from stdin";
            return -1;
        }
        ns = body_trim_copy(stdin_body, stdin_len, out_body, out_len);
        free(stdin_body);
    } else {
        ns = body_trim_copy(body_raw, strlen(body_raw), out_body, out_len);
    }
    if (ns != NORM_OK) {
        *err = norm_body_message(ns);
        return -1;
    }
    return 0;
}

static int normalize_tags(const char *const *tag_raw, size_t ntag_raw, char ***out_tags,
                          size_t *out_ntags, const char **err)
{
    size_t t;
    char **tags;

    *out_tags = NULL;
    *out_ntags = 0U;
    *err = NULL;
    if (ntag_raw == 0U) {
        return 0;
    }

    tags = (char **)calloc(ntag_raw, sizeof(*tags));
    if (tags == NULL) {
        *err = "out of memory";
        return -1;
    }
    for (t = 0; t < ntag_raw; t++) {
        char buf[REMEMBER_TOKEN_MAX + 1];
        NormStatus ns = normalize_tag(tag_raw[t], buf, sizeof(buf));
        if (ns != NORM_OK) {
            size_t j;
            for (j = 0; j < t; j++) {
                free(tags[j]);
            }
            free((void *)tags);
            *err = norm_token_message(ns, "tag");
            return -1;
        }
        tags[t] = strdup(buf);
        if (tags[t] == NULL) {
            size_t j;
            for (j = 0; j < t; j++) {
                free(tags[j]);
            }
            free((void *)tags);
            *err = "out of memory";
            return -1;
        }
    }
    *out_tags = tags;
    *out_ntags = ntag_raw;
    return 0;
}

static void free_tag_list(char **tags, size_t ntags)
{
    size_t t;
    if (tags == NULL) {
        return;
    }
    for (t = 0; t < ntags; t++) {
        free(tags[t]);
    }
    free((void *)tags);
}

static int emit_add_result(bool json, StoreAddAction action, const Entry *entry)
{
    if (json) {
        if (output_action_envelope(stdout, action_name(action), entry) != 0) {
            err_msg("failed to write output");
            return -1;
        }
        return 0;
    }
    if (output_id_human(stdout, entry->id) != 0) {
        err_msg("failed to write output");
        return -1;
    }
    return 0;
}

/* ---- add ----------------------------------------------------------------- */

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
    if (load_body(parsed.body_raw, &body, &body_len, &err) != 0) {
        err_msg(err);
        goto cleanup;
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

/* ---- get ----------------------------------------------------------------- */

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

typedef struct {
    const char *key_raw;
    const char *id_raw;
} GetParse;

static int parse_get_args(int rest_argc, const char **rest_argv, GetParse *out)
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

static int load_get_entry(Store *s, const GetParse *p, Entry *entry)
{
    StoreStatus st;

    if (p->key_raw != NULL && p->id_raw != NULL) {
        err_msg("provide either id or --key, not both");
        return REMEMBER_ERR;
    }
    if (p->key_raw == NULL && p->id_raw == NULL) {
        err_msg("missing id or --key");
        return REMEMBER_ERR;
    }

    if (p->key_raw != NULL) {
        char key_norm[REMEMBER_TOKEN_MAX + 1];
        NormStatus ns = normalize_key(p->key_raw, key_norm, sizeof(key_norm));
        if (ns != NORM_OK) {
            err_msg(norm_token_message(ns, "key"));
            return REMEMBER_ERR;
        }
        st = store_get_by_key(s, key_norm, entry);
    } else {
        long long id = 0;
        if (parse_id_token(p->id_raw, &id) != 0) {
            err_msg("invalid id");
            return REMEMBER_ERR;
        }
        st = store_get(s, id, entry);
    }

    if (st == STORE_ERR_NOT_FOUND) {
        err_msg(store_status_message(st));
        return REMEMBER_NOT_FOUND;
    }
    if (st != STORE_OK) {
        err_msg(store_status_message(st));
        return REMEMBER_ERR;
    }
    return REMEMBER_OK;
}

int cmd_get(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    GetParse parsed;
    Entry entry;
    int rc;

    memset(&entry, 0, sizeof(entry));
    if (parse_get_args(rest_argc, rest_argv, &parsed) != 0) {
        return REMEMBER_ERR;
    }
    rc = load_get_entry(s, &parsed, &entry);
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
        /* Same flow as list: body goes through output.c, never raw fputs, so a
           control/ESC byte in a stored body cannot drive the terminal. */
        (void)output_body_human(stdout, entry.body);
    }
    store_entry_free(&entry);
    return REMEMBER_OK;
}

/* ---- list (minimal; filters in step 05) ---------------------------------- */

int cmd_list(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    Entry *entries = NULL;
    size_t count = 0U;
    size_t total = 0U;
    size_t i;
    StoreStatus st;
    int j;
    int end_opts = 0;
    const size_t limit = 100U;
    const size_t offset = 0U;
    int rc = REMEMBER_ERR;

    for (j = 0; j < rest_argc; j++) {
        const char *arg = rest_argv[j];

        if (!end_opts && strcmp(arg, "--") == 0) {
            end_opts = 1;
            continue;
        }
        if (!end_opts && arg[0] == '-' && arg[1] != '\0') {
            (void)fprintf(stderr, "remember: unknown option '%s'\n", arg);
            return REMEMBER_ERR;
        }
        err_msg("unexpected argument");
        return REMEMBER_ERR;
    }

    st = store_list(s, limit, offset, &entries, &count, &total);
    if (st != STORE_OK) {
        err_msg(store_status_message(st));
        return REMEMBER_ERR;
    }

    if (json) {
        if (output_list_envelope(stdout, offset, limit, count, total, entries) != 0) {
            err_msg("failed to write output");
            goto cleanup;
        }
    } else {
        for (i = 0; i < count; i++) {
            (void)output_entry_human_line(stdout, &entries[i]);
        }
    }
    rc = REMEMBER_OK;

cleanup:
    for (i = 0; i < count; i++) {
        store_entry_free(&entries[i]);
    }
    free(entries);
    return rc;
}
