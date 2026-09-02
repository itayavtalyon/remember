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
    const char *from_id_raw;
    const char *from_key_raw;
    const char *to_id_raw;
    const char *to_key_raw;
    const char *kind_raw;
    const char *pos[2];
    int npos;
} PairParse;

/* raw is never NULL: callers only parse when a --kind token was given. */
static int parse_kind_token(const char *raw, StoreEdgeKind *out)
{
    if (strcmp(raw, "related") == 0) {
        *out = STORE_EDGE_RELATED;
        return 0;
    }
    if (strcmp(raw, "supersedes") == 0) {
        *out = STORE_EDGE_SUPERSEDES;
        return 0;
    }
    if (strcmp(raw, "cites") == 0) {
        *out = STORE_EDGE_CITES;
        return 0;
    }
    err_msg("unknown kind (related, supersedes, cites)");
    return -1;
}

/* 1 = consumed flag, 2 = end-of-options, 0 = positional, -1 = error. */
static int handle_pair_flag(const char *arg, int *i, int rest_argc, const char **rest_argv,
                            PairParse *out)
{
    const char *err = NULL;

    if (strcmp(arg, "--") == 0) {
        return 2;
    }
    if (strcmp(arg, "--from") == 0) {
        if (take_value(i, rest_argc, rest_argv, &out->from_id_raw, &err,
                       "missing value for --from") != 0) {
            err_msg(err);
            return -1;
        }
        return 1;
    }
    if (strcmp(arg, "--from-key") == 0) {
        if (take_value(i, rest_argc, rest_argv, &out->from_key_raw, &err,
                       "missing value for --from-key") != 0) {
            err_msg(err);
            return -1;
        }
        return 1;
    }
    if (strcmp(arg, "--to") == 0) {
        if (take_value(i, rest_argc, rest_argv, &out->to_id_raw, &err, "missing value for --to") !=
            0) {
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
    if (strcmp(arg, "--kind") == 0) {
        if (take_value(i, rest_argc, rest_argv, &out->kind_raw, &err, "missing value for --kind") !=
            0) {
            err_msg(err);
            return -1;
        }
        return 1;
    }
    if (arg[0] == '-' && arg[1] != '\0') {
        (void)fprintf(app_err(), "remember: unknown option '%s'\n", arg);
        return -1;
    }
    return 0;
}

static int parse_pair_args(int rest_argc, const char **rest_argv, PairParse *out)
{
    int i;
    int end_opts = 0;

    memset(out, 0, sizeof(*out));
    for (i = 0; i < rest_argc; i++) {
        const char *arg = rest_argv[i];

        if (!end_opts) {
            int kind = handle_pair_flag(arg, &i, rest_argc, rest_argv, out);
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
        if (out->npos >= 2) {
            err_msg("too many arguments");
            return -1;
        }
        out->pos[out->npos++] = arg;
    }
    return 0;
}

static int pair_apply_sugar(PairParse *p)
{
    int from_n = 0;
    int to_n = 0;

    if (p->npos == 2) {
        if (p->from_id_raw != NULL || p->from_key_raw != NULL || p->to_id_raw != NULL ||
            p->to_key_raw != NULL) {
            err_msg("provide --from/--to flags or two ids, not both");
            return -1;
        }
        p->from_id_raw = p->pos[0];
        p->to_id_raw = p->pos[1];
    } else if (p->npos != 0) {
        err_msg("missing --from/--to (or two ids)");
        return -1;
    }
    if (p->from_id_raw != NULL) {
        from_n++;
    }
    if (p->from_key_raw != NULL) {
        from_n++;
    }
    if (p->to_id_raw != NULL) {
        to_n++;
    }
    if (p->to_key_raw != NULL) {
        to_n++;
    }
    if (from_n != 1 || to_n != 1) {
        err_msg("each end needs exactly one of id or --key");
        return -1;
    }
    return 0;
}

static int resolve_end(Store *s, const char *id_raw, const char *key_raw, long long *out_id)
{
    Entry e;
    StoreStatus st;

    memset(&e, 0, sizeof(e));
    if (key_raw != NULL) {
        char key_norm[REMEMBER_TOKEN_MAX + 1];
        NormStatus ns = normalize_key(key_raw, key_norm, sizeof(key_norm));
        if (ns != NORM_OK) {
            err_msg(norm_token_message(ns, "key"));
            return REMEMBER_ERR;
        }
        st = store_get_any_by_key(s, key_norm, &e);
    } else {
        if (parse_entry_id(id_raw, out_id) != 0) {
            err_msg("invalid id");
            return REMEMBER_ERR;
        }
        st = store_get_any(s, *out_id, &e);
    }
    if (st != STORE_OK) {
        return store_status_to_exit(st);
    }
    *out_id = e.id;
    store_entry_free(&e);
    return REMEMBER_OK;
}

static int emit_links_result(bool json, const char *action, long long subject_id,
                             const StoreNeighbor *links, size_t count, const char *now)
{
    if (json) {
        if (output_links_write_envelope(app_out(), action, links, count, now) != 0) {
            err_msg("failed to write output");
            return -1;
        }
        return 0;
    }
    if (output_id_human(app_out(), subject_id) != 0) {
        err_msg("failed to write output");
        return -1;
    }
    return 0;
}

static int run_pair(Store *s, bool json, int rest_argc, const char **rest_argv, int is_unlink)
{
    PairParse p;
    StoreEdgeKind kind = STORE_EDGE_RELATED;
    const StoreEdgeKind *kind_ptr = NULL;
    char now[32];
    long long from_id = 0;
    long long to_id = 0;
    int rc;
    StoreStatus st;
    StoreLinkAction act;
    StoreNeighbor stub;
    StoreNeighbor *gone = NULL;
    size_t n = 0U;

    memset(&stub, 0, sizeof(stub));
    if (parse_pair_args(rest_argc, rest_argv, &p) != 0 || pair_apply_sugar(&p) != 0) {
        return REMEMBER_ERR;
    }
    if (p.kind_raw != NULL) {
        if (parse_kind_token(p.kind_raw, &kind) != 0) {
            return REMEMBER_ERR;
        }
        kind_ptr = &kind;
    } else if (!is_unlink) {
        kind = STORE_EDGE_RELATED;
        kind_ptr = &kind;
    }
    if (utc_now(now, sizeof(now)) != 0) {
        err_msg("internal error");
        return REMEMBER_ERR;
    }
    rc = resolve_end(s, p.from_id_raw, p.from_key_raw, &from_id);
    if (rc != REMEMBER_OK) {
        return rc;
    }
    rc = resolve_end(s, p.to_id_raw, p.to_key_raw, &to_id);
    if (rc != REMEMBER_OK) {
        return rc;
    }
    if (is_unlink) {
        st = store_unlink(s, from_id, to_id, kind_ptr, now, &gone, &n);
        rc = store_status_to_exit(st);
        if (rc != REMEMBER_OK) {
            store_neighbors_free(gone, n);
            return rc;
        }
        if (emit_links_result(json, "deleted", from_id, gone, n, now) != 0) {
            store_neighbors_free(gone, n);
            return REMEMBER_ERR;
        }
        store_neighbors_free(gone, n);
        return REMEMBER_OK;
    }
    st = store_link(s, from_id, to_id, kind, now, &act, &stub);
    rc = store_status_to_exit(st);
    if (rc != REMEMBER_OK) {
        return rc;
    }
    if (emit_links_result(json, (act == STORE_LINK_MERGED) ? "merged" : "created", from_id, &stub,
                          1U, now) != 0) {
        store_neighbor_free(&stub);
        return REMEMBER_ERR;
    }
    store_neighbor_free(&stub);
    return REMEMBER_OK;
}

int cmd_link(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    return run_pair(s, json, rest_argc, rest_argv, 0);
}

int cmd_unlink(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    return run_pair(s, json, rest_argc, rest_argv, 1);
}

typedef struct {
    const char *id_raw;
    const char *key_raw;
    const char *kind_raw;
    bool outgoing;
    bool incoming;
} RelatedParse;

static int handle_related_flag(const char *arg, int *i, int rest_argc, const char **rest_argv,
                               RelatedParse *out)
{
    const char *err = NULL;

    if (strcmp(arg, "--") == 0) {
        return 2;
    }
    if (strcmp(arg, "--key") == 0) {
        if (take_value(i, rest_argc, rest_argv, &out->key_raw, &err, "missing value for --key") !=
            0) {
            err_msg(err);
            return -1;
        }
        return 1;
    }
    if (strcmp(arg, "--kind") == 0) {
        if (take_value(i, rest_argc, rest_argv, &out->kind_raw, &err, "missing value for --kind") !=
            0) {
            err_msg(err);
            return -1;
        }
        return 1;
    }
    if (strcmp(arg, "--outgoing") == 0) {
        out->outgoing = true;
        return 1;
    }
    if (strcmp(arg, "--incoming") == 0) {
        out->incoming = true;
        return 1;
    }
    if (arg[0] == '-' && arg[1] != '\0') {
        (void)fprintf(app_err(), "remember: unknown option '%s'\n", arg);
        return -1;
    }
    return 0;
}

static int parse_related_args(int rest_argc, const char **rest_argv, RelatedParse *out)
{
    int i;
    int end_opts = 0;

    memset(out, 0, sizeof(*out));
    for (i = 0; i < rest_argc; i++) {
        const char *arg = rest_argv[i];

        if (!end_opts) {
            int kind = handle_related_flag(arg, &i, rest_argc, rest_argv, out);
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
        if (out->id_raw != NULL) {
            err_msg("too many arguments");
            return -1;
        }
        out->id_raw = arg;
    }
    return 0;
}

static int related_load_subject(Store *s, const RelatedParse *p, Entry *subject)
{
    StoreStatus st;

    memset(subject, 0, sizeof(*subject));
    if (p->key_raw != NULL) {
        char key_norm[REMEMBER_TOKEN_MAX + 1];
        NormStatus ns = normalize_key(p->key_raw, key_norm, sizeof(key_norm));
        if (ns != NORM_OK) {
            err_msg(norm_token_message(ns, "key"));
            return REMEMBER_ERR;
        }
        st = store_get_any_by_key(s, key_norm, subject);
    } else {
        long long id = 0;
        if (parse_entry_id(p->id_raw, &id) != 0) {
            err_msg("invalid id");
            return REMEMBER_ERR;
        }
        st = store_get_any(s, id, subject);
    }
    return store_status_to_exit(st);
}

int cmd_related(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    RelatedParse p;
    StoreEdgeKind kind;
    const StoreEdgeKind *kind_ptr = NULL;
    StoreNeighborDir dir = STORE_NEIGHBOR_ALL;
    char now[32];
    Entry subject;
    StoreNeighbor *rows = NULL;
    size_t n = 0U;
    StoreStatus st;
    int rc;

    memset(&subject, 0, sizeof(subject));
    if (parse_related_args(rest_argc, rest_argv, &p) != 0) {
        return REMEMBER_ERR;
    }
    if (p.outgoing && p.incoming) {
        err_msg("cannot combine --outgoing and --incoming");
        return REMEMBER_ERR;
    }
    if (p.key_raw != NULL && p.id_raw != NULL) {
        err_msg("provide either id or --key, not both");
        return REMEMBER_ERR;
    }
    if (p.key_raw == NULL && p.id_raw == NULL) {
        err_msg("missing id or --key");
        return REMEMBER_ERR;
    }
    if (p.kind_raw != NULL) {
        if (parse_kind_token(p.kind_raw, &kind) != 0) {
            return REMEMBER_ERR;
        }
        kind_ptr = &kind;
    }
    if (p.outgoing) {
        dir = STORE_NEIGHBOR_OUTGOING;
    } else if (p.incoming) {
        dir = STORE_NEIGHBOR_INCOMING;
    }
    if (utc_now(now, sizeof(now)) != 0) {
        err_msg("internal error");
        return REMEMBER_ERR;
    }
    rc = related_load_subject(s, &p, &subject);
    if (rc != REMEMBER_OK) {
        return rc;
    }
    st = store_list_neighbors(s, subject.id, kind_ptr, dir, now, &rows, &n);
    rc = store_status_to_exit(st);
    if (rc != REMEMBER_OK) {
        store_entry_free(&subject);
        return rc;
    }
    if (json) {
        rc = output_related_envelope(app_out(), subject.id, subject.key, rows, n, now);
    } else {
        rc = output_related_human(app_out(), rows, n, now);
    }
    store_neighbors_free(rows, n);
    store_entry_free(&subject);
    if (rc != 0) {
        err_msg("failed to write output");
        return REMEMBER_ERR;
    }
    return REMEMBER_OK;
}
