#include "commands.h"
#include "commands_common.h"

#include "appio.h"
#include "exit_codes.h"
#include "normalize.h"
#include "output.h"
#include "store.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *from_id_raw;
    const char *from_key_raw;
    const char *from_sync_id_raw;
    const char *to_id_raw;
    const char *to_key_raw;
    const char *to_sync_id_raw;
    const char *kind_raw;
    const char *pos[2];
    CmdBinOpts bins;
    int npos;
    char pad_[4]; /* explicit tail padding (kept -Wpadded-clean) */
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

    if (strcmp(arg, "--") == 0) {
        return 2;
    }
    if (strcmp(arg, "--from") == 0) {
        TakeValue taken = take_value(i, rest_argc, rest_argv, "missing value for --from");
        if (taken.rc != 0) {
            err_msg(taken.err);
            return -1;
        }
        out->from_id_raw = taken.value;
        return 1;
    }
    if (strcmp(arg, "--from-key") == 0) {
        TakeValue taken = take_value(i, rest_argc, rest_argv, "missing value for --from-key");
        if (taken.rc != 0) {
            err_msg(taken.err);
            return -1;
        }
        out->from_key_raw = taken.value;
        return 1;
    }
    if (strcmp(arg, "--from-sync-id") == 0) {
        TakeValue taken = take_value(i, rest_argc, rest_argv, "missing value for --from-sync-id");
        if (taken.rc != 0) {
            err_msg(taken.err);
            return -1;
        }
        out->from_sync_id_raw = taken.value;
        return 1;
    }
    if (strcmp(arg, "--to") == 0) {
        TakeValue taken = take_value(i, rest_argc, rest_argv, "missing value for --to");
        if (taken.rc != 0) {
            err_msg(taken.err);
            return -1;
        }
        out->to_id_raw = taken.value;
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
    if (strcmp(arg, "--to-sync-id") == 0) {
        TakeValue taken = take_value(i, rest_argc, rest_argv, "missing value for --to-sync-id");
        if (taken.rc != 0) {
            err_msg(taken.err);
            return -1;
        }
        out->to_sync_id_raw = taken.value;
        return 1;
    }
    if (strcmp(arg, "--kind") == 0) {
        TakeValue taken = take_value(i, rest_argc, rest_argv, "missing value for --kind");
        if (taken.rc != 0) {
            err_msg(taken.err);
            return -1;
        }
        out->kind_raw = taken.value;
        return 1;
    }
    if (cmd_bin_take_flag(arg, &out->bins) != 0) {
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
    int i = 0;
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

/* One endpoint locator: exactly one of id / key / sync_id. */
typedef struct {
    const char *id_raw;
    const char *key_raw;
    const char *sync_id_raw;
} EndRef;

static int end_form_count(EndRef ref)
{
    int n = 0;

    if (ref.id_raw != NULL) {
        n++;
    }
    if (ref.key_raw != NULL) {
        n++;
    }
    if (ref.sync_id_raw != NULL) {
        n++;
    }
    return n;
}

static int pair_apply_sugar(PairParse *p)
{
    EndRef from = {
        .id_raw = p->from_id_raw, .key_raw = p->from_key_raw, .sync_id_raw = p->from_sync_id_raw};
    EndRef to = {
        .id_raw = p->to_id_raw, .key_raw = p->to_key_raw, .sync_id_raw = p->to_sync_id_raw};

    if (p->npos == 2) {
        if (end_form_count(from) != 0 || end_form_count(to) != 0) {
            err_msg("provide --from/--to flags or two ids, not both");
            return -1;
        }
        p->from_id_raw = p->pos[0];
        p->to_id_raw = p->pos[1];
    } else if (p->npos != 0) {
        err_msg("missing --from/--to (or two ids)");
        return -1;
    }
    from = (EndRef){
        .id_raw = p->from_id_raw, .key_raw = p->from_key_raw, .sync_id_raw = p->from_sync_id_raw};
    to = (EndRef){
        .id_raw = p->to_id_raw, .key_raw = p->to_key_raw, .sync_id_raw = p->to_sync_id_raw};
    if (end_form_count(from) != 1 || end_form_count(to) != 1) {
        err_msg("each end needs exactly one of id, --key, or --sync-id");
        return -1;
    }
    return 0;
}

static int resolve_end(Store *s, EndRef ref, bool include_deleted, long long *out_id)
{
    Entry e;
    StoreStatus st = STORE_OK;

    memset(&e, 0, sizeof(e));
    if (ref.sync_id_raw != NULL) {
        if (store_sync_id_is_canonical(ref.sync_id_raw) == 0) {
            err_msg("invalid sync-id");
            return REMEMBER_ERR;
        }
        if (include_deleted) {
            st = store_get_row_by_sync_id(s, ref.sync_id_raw, &e);
        } else {
            st = store_get_any_by_sync_id(s, ref.sync_id_raw, &e);
        }
    } else if (ref.key_raw != NULL) {
        char key_norm[REMEMBER_TOKEN_MAX + 1];
        NormStatus ns = normalize_key(ref.key_raw, key_norm, sizeof(key_norm));
        if (ns != NORM_OK) {
            err_msg(norm_token_message(ns, "key"));
            return REMEMBER_ERR;
        }
        if (include_deleted) {
            st = store_get_row_by_key(s, key_norm, &e);
        } else {
            st = store_get_any_by_key(s, key_norm, &e);
        }
    } else {
        if (parse_entry_id(ref.id_raw, out_id) != 0) {
            err_msg("invalid id");
            return REMEMBER_ERR;
        }
        if (include_deleted) {
            st = store_get_row(s, *out_id, &e);
        } else {
            st = store_get_any(s, *out_id, &e);
        }
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
    char now[ISO_TS_BUFSIZE];
    long long from_id = 0;
    long long to_id = 0;
    int rc = 0;
    StoreStatus st = STORE_OK;
    StoreLinkAction act = STORE_LINK_CREATED;
    StoreNeighbor stub;
    StoreNeighbor *gone = NULL;
    size_t n = 0U;
    StoreBin bin = STORE_BIN_LIVE;
    bool include_deleted = false;

    memset(&stub, 0, sizeof(stub));
    if (parse_pair_args(rest_argc, rest_argv, &p) != 0 || pair_apply_sugar(&p) != 0) {
        return REMEMBER_ERR;
    }
    if (cmd_bin_resolve(&p.bins, &bin) != 0) {
        return REMEMBER_ERR;
    }
    if (p.bins.expired || p.bins.trash_alias) {
        err_msg("graph commands use --deleted for deleted ends (not --expired)");
        return REMEMBER_ERR;
    }
    include_deleted = (bin == STORE_BIN_DELETED);
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
    rc = resolve_end(s,
                     (EndRef){.id_raw = p.from_id_raw,
                              .key_raw = p.from_key_raw,
                              .sync_id_raw = p.from_sync_id_raw},
                     include_deleted, &from_id);
    if (rc != REMEMBER_OK) {
        return rc;
    }
    rc = resolve_end(
        s,
        (EndRef){.id_raw = p.to_id_raw, .key_raw = p.to_key_raw, .sync_id_raw = p.to_sync_id_raw},
        include_deleted, &to_id);
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
    st = store_link(s, (StoreEdge){.from_id = from_id, .to_id = to_id}, kind, now, &act, &stub);
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
    const char *sync_id_raw;
    const char *kind_raw;
    bool outgoing;
    bool incoming;
    CmdBinOpts bins;
    /* NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers) */
    char pad_[6]; /* explicit tail padding (kept -Wpadded-clean) */
} RelatedParse;

static int handle_related_flag(const char *arg, int *i, int rest_argc, const char **rest_argv,
                               RelatedParse *out)
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
        out->key_raw = taken.value;
        return 1;
    }
    if (strcmp(arg, "--sync-id") == 0) {
        TakeValue taken = take_value(i, rest_argc, rest_argv, "missing value for --sync-id");
        if (taken.rc != 0) {
            err_msg(taken.err);
            return -1;
        }
        out->sync_id_raw = taken.value;
        return 1;
    }
    if (strcmp(arg, "--kind") == 0) {
        TakeValue taken = take_value(i, rest_argc, rest_argv, "missing value for --kind");
        if (taken.rc != 0) {
            err_msg(taken.err);
            return -1;
        }
        out->kind_raw = taken.value;
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
    if (cmd_bin_take_flag(arg, &out->bins) != 0) {
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
    int i = 0;
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

static int related_load_subject(Store *s, const RelatedParse *p, bool include_deleted,
                                Entry *subject)
{
    EndRef ref = {.id_raw = p->id_raw, .key_raw = p->key_raw, .sync_id_raw = p->sync_id_raw};
    StoreStatus st = STORE_OK;

    memset(subject, 0, sizeof(*subject));
    if (ref.sync_id_raw != NULL) {
        if (store_sync_id_is_canonical(ref.sync_id_raw) == 0) {
            err_msg("invalid sync-id");
            return REMEMBER_ERR;
        }
        if (include_deleted) {
            st = store_get_row_by_sync_id(s, ref.sync_id_raw, subject);
        } else {
            st = store_get_any_by_sync_id(s, ref.sync_id_raw, subject);
        }
    } else if (ref.key_raw != NULL) {
        char key_norm[REMEMBER_TOKEN_MAX + 1];
        NormStatus ns = normalize_key(ref.key_raw, key_norm, sizeof(key_norm));
        if (ns != NORM_OK) {
            err_msg(norm_token_message(ns, "key"));
            return REMEMBER_ERR;
        }
        if (include_deleted) {
            st = store_get_row_by_key(s, key_norm, subject);
        } else {
            st = store_get_any_by_key(s, key_norm, subject);
        }
    } else {
        long long id = 0;
        if (parse_entry_id(ref.id_raw, &id) != 0) {
            err_msg("invalid id");
            return REMEMBER_ERR;
        }
        if (include_deleted) {
            st = store_get_row(s, id, subject);
        } else {
            st = store_get_any(s, id, subject);
        }
    }
    return store_status_to_exit(st);
}

int cmd_related(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    RelatedParse p;
    StoreEdgeKind kind = STORE_EDGE_RELATED;
    const StoreEdgeKind *kind_ptr = NULL;
    StoreNeighborDir dir = STORE_NEIGHBOR_ALL;
    char now[ISO_TS_BUFSIZE];
    Entry subject;
    StoreNeighbor *rows = NULL;
    size_t n = 0U;
    StoreStatus st = STORE_OK;
    StoreBin bin = STORE_BIN_LIVE;
    bool include_deleted = false;
    int forms = 0;
    int rc = 0;

    memset(&subject, 0, sizeof(subject));
    if (parse_related_args(rest_argc, rest_argv, &p) != 0) {
        return REMEMBER_ERR;
    }
    if (cmd_bin_resolve(&p.bins, &bin) != 0) {
        return REMEMBER_ERR;
    }
    if (p.bins.expired || p.bins.trash_alias) {
        err_msg("graph commands use --deleted for deleted ends (not --expired)");
        return REMEMBER_ERR;
    }
    include_deleted = (bin == STORE_BIN_DELETED);
    if (p.outgoing && p.incoming) {
        err_msg("cannot combine --outgoing and --incoming");
        return REMEMBER_ERR;
    }
    if (p.key_raw != NULL) {
        forms++;
    }
    if (p.id_raw != NULL) {
        forms++;
    }
    if (p.sync_id_raw != NULL) {
        forms++;
    }
    if (forms > 1) {
        err_msg("provide exactly one of id, --key, or --sync-id");
        return REMEMBER_ERR;
    }
    if (forms == 0) {
        err_msg("missing id, --key, or --sync-id");
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
    rc = related_load_subject(s, &p, include_deleted, &subject);
    if (rc != REMEMBER_OK) {
        return rc;
    }
    st = store_list_neighbors(s, subject.id, kind_ptr, dir, now, &rows, &n);
    rc = store_status_to_exit(st);
    if (rc != REMEMBER_OK) {
        store_entry_free(&subject);
        return rc;
    }
    if (bin != STORE_BIN_DELETED) {
        cmd_neighbors_drop_deleted(rows, &n, now);
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
