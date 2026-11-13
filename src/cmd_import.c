#include "commands.h"
#include "commands_common.h"
#include "diag.h"

#include "appio.h"
#include "exit_codes.h"
#include "import_read.h"
#include "output.h"
#include "settings.h"
#include "store.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ORIGIN_MAX = 64 };

/* Basename for conflicts.origin: last path component after the final '/'. */
static const char *import_basename(const char *path)
{
    const char *slash = NULL;

    if (path == NULL || path[0] == '\0') {
        return "";
    }
    slash = strrchr(path, '/');
    return (slash != NULL) ? slash + 1 : path;
}

/* The import reply: the settings skip warnings (ST-8.3a, the same lines as
   settings import), then the JSON data object or one plain line of counts. */
static int import_write(bool json, const StoreImportResult *r)
{
    if (settings_import_warn_skips(&r->settings, false) != 0) {
        return -1;
    }
    if (json) {
        return output_import_data(app_out(), r);
    }
    if (fprintf(app_out(),
                "inserted %zu updated %zu unchanged %zu conflicts %zu rejected %zu skipped %zu\n",
                r->inserted, r->updated, r->unchanged, r->nconflicts, r->nrejected,
                r->skipped) < 0) {
        return -1;
    }
    return 0;
}

typedef struct {
    const char *flag;
    const char **slot;
} ImportSource;

/* When arg is one of the --from-* flags, take its path into that slot and
   return true; *rc is REMEMBER_ERR after a usage error (P7: a repeat is one). */
static bool import_take_source(const char *arg, int *i, int argc, const char **argv,
                               const ImportSource *sources, size_t n, int *rc)
{
    size_t k = 0;

    for (k = 0; k < n; k++) {
        if (strcmp(arg, sources[k].flag) == 0) {
            TakeValue taken = take_value(i, argc, argv, NULL);

            *rc = REMEMBER_ERR;
            if (taken.rc != 0) {
                err_fmt(DIAG_USAGE, "%s requires a path", arg);
            } else if (*sources[k].slot != NULL) {
                err_fmt(DIAG_USAGE, "duplicate %s", arg);
            } else {
                *sources[k].slot = taken.value;
                *rc = 0;
            }
            return true;
        }
    }
    return false;
}

/* The settings skip warnings, then the reply; frees the result. */
static int import_reply(bool json, StoreImportResult *result)
{
    int rc = import_write(json, result);

    store_import_result_free(result);
    if (rc != 0) {
        err_msg(DIAG_INTERNAL, "failed to write output");
        return REMEMBER_ERR;
    }
    return REMEMBER_OK;
}

static int import_report_parse(const ImportDoc *doc)
{
    const char *msg =
        (doc->parse_message != NULL) ? doc->parse_message : "isn't a remember export.";
    const char *fix = (doc->parse_fix != NULL) ? doc->parse_fix : diag_code_fix(DIAG_IMPORT_PARSE);

    (void)diag_error_fix(DIAG_IMPORT_PARSE, msg, fix);
    return REMEMBER_ERR;
}

static int import_report_io(const char *path, int sys_errno)
{
    char *msg = NULL;

    if (sys_errno == ENOMEM) {
        err_msg(DIAG_OUT_OF_MEMORY, "out of memory");
        return REMEMBER_ERR;
    }
    errno = sys_errno;
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    msg = fmt_alloc("can't read \"%s\": %s.", path, strerror(sys_errno));
    if (msg == NULL) {
        err_msg(DIAG_OUT_OF_MEMORY, "out of memory");
        return REMEMBER_ERR;
    }
    (void)diag_error_fix(DIAG_IMPORT_PARSE, msg, diag_code_fix(DIAG_IMPORT_PARSE));
    free(msg);
    return REMEMBER_ERR;
}

/* The parsed import flags: one --from-* path, and the --origin label or NULL. */
typedef struct {
    const char *from_db;
    const char *from_json;
    const char *from_md;
    const char *from_csv;
    const char *origin;
} ImportArgs;

/* --origin LABEL, else the source's basename (conflicts.origin). */
static const char *import_origin(const ImportArgs *a, const char *path)
{
    return (a->origin != NULL) ? a->origin : import_basename(path);
}

/* The store's status as the exit code, else the reply; frees the result. */
static int import_finish(const Store *s, bool json, StoreStatus st, StoreImportResult *result)
{
    if (st != STORE_OK) {
        return store_status_to_exit(s, st);
    }
    return import_reply(json, result);
}

static int import_from_json(Store *s, bool json, const ImportArgs *a, const char *now)
{
    const char *path = a->from_json;
    ImportDoc doc;
    StoreImportResult result;
    int sys_errno = 0;
    StoreStatus st = STORE_OK;

    memset(&doc, 0, sizeof(doc));
    memset(&result, 0, sizeof(result));
    if (import_read_json(path, &doc, &sys_errno) != 0) {
        return import_report_io(path, sys_errno);
    }
    if (doc.parse_message != NULL) {
        int rc = import_report_parse(&doc);

        import_doc_free(&doc);
        return rc;
    }
    if (doc.unknown_field != NULL) {
        (void)diag_unknown_field((DiagUnknownField){.path = path, .name = doc.unknown_field});
    }
    if (doc.settings_doc != NULL) {
        SettingImportReport rep;
        char err[STORE_SQLITE_MSG_LEN];
        SettingStatus sst = SETTING_OK;

        memset(&rep, 0, sizeof(rep));
        err[0] = '\0';
        /* Dry-run validates the nested document before any memory write (ST-8.3a). */
        sst = store_setting_import(s, NULL, doc.settings_doc, doc.settings_len, SETTING_LAYER_BRAIN,
                                   true, &rep, err, sizeof(err));
        if (sst == SETTING_ERR_DOCUMENT) {
            store_setting_import_free(&rep);
            import_doc_free(&doc);
            (void)diag_error_fix(DIAG_IMPORT_PARSE, "\"settings\" isn't a settings document.",
                                 "Fix the settings object, then try again.");
            return REMEMBER_ERR;
        }
        if (sst != SETTING_OK) {
            store_setting_import_free(&rep);
            import_doc_free(&doc);
            err_msg(DIAG_INTERNAL, err[0] != '\0' ? err : "internal error");
            return REMEMBER_ERR;
        }
        store_setting_import_free(&rep);
    }
    st = store_import_doc(s, &doc, now, import_origin(a, path), &result);
    import_doc_free(&doc);
    return import_finish(s, json, st, &result);
}

/* One --origin LABEL at argv[*i]. 0, or REMEMBER_ERR after a usage error. */
static int import_take_origin(int *i, int argc, const char **argv, ImportArgs *a)
{
    TakeValue taken = take_value(i, argc, argv, "--origin requires a label");

    if (taken.rc != 0) {
        err_msg(DIAG_USAGE, taken.err);
        return REMEMBER_ERR;
    }
    if (a->origin != NULL) {
        err_msg(DIAG_USAGE, "duplicate --origin");
        return REMEMBER_ERR;
    }
    if (strlen(taken.value) > (size_t)ORIGIN_MAX) {
        err_msg(DIAG_USAGE, "origin is too long (max 64 bytes)");
        return REMEMBER_ERR;
    }
    a->origin = taken.value;
    return 0;
}

/* The import flags into a. 0, or REMEMBER_ERR after a usage error. */
static int import_parse_args(int argc, const char **argv, ImportArgs *a)
{
    ImportSource sources[] = {{"--from-db", &a->from_db},
                              {"--from-json", &a->from_json},
                              {"--from-md", &a->from_md},
                              {"--from-csv", &a->from_csv}};
    size_t nsources = sizeof sources / sizeof sources[0];
    size_t given = 0;
    size_t k = 0;
    int i = 0;

    for (i = 0; i < argc; i++) {
        const char *arg = (argv[i] != NULL) ? argv[i] : "";
        int taken_rc = 0;

        if (strcmp(arg, "--origin") == 0) {
            taken_rc = import_take_origin(&i, argc, argv, a);
        } else if (import_take_source(arg, &i, argc, argv, sources, nsources, &taken_rc) ||
                   cmd_is_hooks_flag(arg)) {
            /* taken (taken_rc says how) or a hooks flag */
        } else if (arg[0] == '-' && arg[1] != '\0') {
            err_fmt(DIAG_USAGE, "unknown option '%s'", arg);
            return REMEMBER_ERR;
        } else {
            err_msg(DIAG_USAGE, "import takes no positional arguments");
            return REMEMBER_ERR;
        }
        if (taken_rc != 0) {
            return REMEMBER_ERR;
        }
    }
    for (k = 0; k < nsources; k++) {
        if (*sources[k].slot != NULL) {
            given++;
        }
    }
    if (given != 1U) {
        err_msg(DIAG_USAGE,
                "import requires exactly one of --from-json, --from-md, --from-csv, --from-db");
        return REMEMBER_ERR;
    }
    return 0;
}

int cmd_import(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    ImportArgs a = {NULL, NULL, NULL, NULL, NULL};
    char now[ISO_TS_BUFSIZE];
    StoreImportResult result;
    StoreStatus st = STORE_OK;

    if (import_parse_args(rest_argc, rest_argv, &a) != 0) {
        return REMEMBER_ERR;
    }
    if (a.from_md != NULL) {
        /* Markdown and CSV land in plan 23a (decision:plan23-review-g1-md-csv-to-23a). */
        err_msg(DIAG_USAGE, "--from-md is not implemented yet");
        return REMEMBER_ERR;
    }
    if (a.from_csv != NULL) {
        err_msg(DIAG_USAGE, "--from-csv is not implemented yet");
        return REMEMBER_ERR;
    }
    if (utc_now(now, sizeof(now)) != 0) {
        err_msg(DIAG_INTERNAL, "internal error");
        return REMEMBER_ERR;
    }
    if (a.from_json != NULL) {
        return import_from_json(s, json, &a, now);
    }
    memset(&result, 0, sizeof(result));
    st = store_import(s, a.from_db, now, import_origin(&a, a.from_db), &result);
    if (st == STORE_ERR_SQLITE && store_last_error(s)->import_source) {
        /* The source failed, not the brain: name the --from-db file. */
        err_store_sqlite(store_last_error(s), a.from_db);
        return REMEMBER_ERR;
    }
    return import_finish(s, json, st, &result);
}

int cmd_conflicts(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    StoreConflict *rows = NULL;
    size_t count = 0U;
    StoreStatus st = STORE_OK;
    size_t i = 0;
    int rc = REMEMBER_ERR;

    if (rest_argc > 0) {
        err_msg(DIAG_USAGE, "conflicts takes no arguments");
        return REMEMBER_ERR;
    }
    (void)rest_argv;
    st = store_conflicts_list(s, &rows, &count);
    if (st != STORE_OK) {
        return store_status_to_exit(s, st);
    }
    if (json) {
        if (output_conflicts_data(app_out(), rows, count) != 0) {
            err_msg(DIAG_INTERNAL, "failed to write output");
            goto cleanup;
        }
    } else {
        for (i = 0; i < count; i++) {
            const char *reason = store_conflict_reason_str(rows[i].reason);
            /* The sync_id came from a peer: masked (decision:import-control-bytes-followup). */
            if (fprintf(app_out(), "%lld\t", rows[i].id) < 0 ||
                output_text_human(app_out(), rows[i].sync_id, false) != 0 ||
                fprintf(app_out(), "\t%s\n", reason != NULL ? reason : "?") < 0) {
                err_msg(DIAG_INTERNAL, "failed to write output");
                goto cleanup;
            }
        }
    }
    rc = REMEMBER_OK;

cleanup:
    store_conflicts_free(rows, count);
    return rc;
}

static int parse_keep(const char *raw, StoreConflictKeep *out)
{
    if (raw == NULL || out == NULL) {
        return -1;
    }
    if (strcmp(raw, "local") == 0) {
        *out = STORE_KEEP_LOCAL;
        return 0;
    }
    if (strcmp(raw, "incoming") == 0) {
        *out = STORE_KEEP_INCOMING;
        return 0;
    }
    if (strcmp(raw, "both") == 0) {
        *out = STORE_KEEP_BOTH;
        return 0;
    }
    return -1;
}

static int conflict_accept_take_id(int *arg_i, int rest_argc, const char **rest_argv,
                                   long long *conflict_id, bool *have_id)
{
    TakeValue taken = take_value(arg_i, rest_argc, rest_argv, "--id requires a value");

    if (taken.rc != 0) {
        err_msg(DIAG_USAGE, taken.err);
        return REMEMBER_ERR;
    }
    if (*have_id) {
        err_msg(DIAG_USAGE, "duplicate --id");
        return REMEMBER_ERR;
    }
    if (parse_entry_id(taken.value, conflict_id) != 0) {
        err_msg(DIAG_USAGE, "invalid conflict id");
        return REMEMBER_ERR;
    }
    *have_id = true;
    return 0;
}

static int conflict_accept_take_keep(int *arg_i, int rest_argc, const char **rest_argv,
                                     StoreConflictKeep *keep, bool *have_keep)
{
    TakeValue taken =
        take_value(arg_i, rest_argc, rest_argv, "--keep requires local|incoming|both");

    if (taken.rc != 0) {
        err_msg(DIAG_USAGE, taken.err);
        return REMEMBER_ERR;
    }
    if (*have_keep) {
        err_msg(DIAG_USAGE, "duplicate --keep");
        return REMEMBER_ERR;
    }
    if (parse_keep(taken.value, keep) != 0) {
        err_msg(DIAG_USAGE, "--keep must be local, incoming, or both");
        return REMEMBER_ERR;
    }
    *have_keep = true;
    return 0;
}

/* Parse conflict accept --id / --keep. Returns 0 on success, REMEMBER_ERR on usage. */
static int conflict_accept_parse(int rest_argc, const char **rest_argv, long long *conflict_id,
                                 StoreConflictKeep *keep)
{
    bool have_id = false;
    bool have_keep = false;
    int arg_i = 0;

    *conflict_id = 0;
    *keep = STORE_KEEP_LOCAL;
    for (arg_i = 1; arg_i < rest_argc; arg_i++) {
        const char *arg = rest_argv[arg_i];

        if (arg != NULL && strcmp(arg, "--id") == 0) {
            if (conflict_accept_take_id(&arg_i, rest_argc, rest_argv, conflict_id, &have_id) != 0) {
                return REMEMBER_ERR;
            }
            continue;
        }
        if (arg != NULL && strcmp(arg, "--keep") == 0) {
            if (conflict_accept_take_keep(&arg_i, rest_argc, rest_argv, keep, &have_keep) != 0) {
                return REMEMBER_ERR;
            }
            continue;
        }
        if (arg != NULL && cmd_is_hooks_flag(arg)) {
            continue;
        }
        if (arg != NULL && arg[0] == '-' && arg[1] != '\0') {
            err_fmt(DIAG_USAGE, "unknown option '%s'", arg);
            return REMEMBER_ERR;
        }
        err_msg(DIAG_USAGE, "conflict accept takes no positional arguments");
        return REMEMBER_ERR;
    }
    if (!have_id || !have_keep) {
        err_msg(DIAG_USAGE, "conflict accept requires --id and --keep");
        return REMEMBER_ERR;
    }
    return 0;
}

static int conflict_accept_emit(bool json, const Entry *entries, size_t count, const char *now)
{
    size_t i = 0;

    if (json) {
        if (output_entries_data(app_out(), "accepted", entries, count, now) != 0) {
            err_msg(DIAG_INTERNAL, "failed to write output");
            return REMEMBER_ERR;
        }
        return REMEMBER_OK;
    }
    for (i = 0; i < count; i++) {
        if (output_id_human(app_out(), entries[i].id) != 0) {
            err_msg(DIAG_INTERNAL, "failed to write output");
            return REMEMBER_ERR;
        }
    }
    return REMEMBER_OK;
}

int cmd_conflict(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    long long conflict_id = 0;
    StoreConflictKeep keep = STORE_KEEP_LOCAL;
    char now[ISO_TS_BUFSIZE];
    Entry *entries = NULL;
    size_t count = 0U;
    StoreStatus st = STORE_OK;
    size_t i = 0;
    int rc = REMEMBER_ERR;

    if (rest_argc < 1 || rest_argv[0] == NULL || strcmp(rest_argv[0], "accept") != 0) {
        err_msg(DIAG_USAGE, "usage: conflict accept --id N --keep local|incoming|both");
        return REMEMBER_ERR;
    }
    if (conflict_accept_parse(rest_argc, rest_argv, &conflict_id, &keep) != 0) {
        return REMEMBER_ERR;
    }
    if (utc_now(now, sizeof(now)) != 0) {
        err_msg(DIAG_INTERNAL, "internal error");
        return REMEMBER_ERR;
    }
    st = store_conflict_accept(s, conflict_id, keep, now, &entries, &count);
    if (st != STORE_OK) {
        return store_status_to_exit(s, st);
    }
    rc = conflict_accept_emit(json, entries, count, now);

    for (i = 0; i < count; i++) {
        store_entry_free(&entries[i]);
    }
    free(entries);
    return rc;
}
