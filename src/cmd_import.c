#include "commands.h"
#include "commands_common.h"
#include "diag.h"

#include "appio.h"
#include "exit_codes.h"
#include "import_read.h"
#include "output.h"
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

/* The import reply: the JSON data object, or one plain line of counts. */
static int import_write(bool json, const StoreImportResult *r)
{
    if (json) {
        return output_import_data(app_out(), r);
    }
    if (fprintf(app_out(),
                "inserted %zu updated %zu unchanged %zu conflicts %zu rejected %zu skipped %zu\n",
                r->inserted, r->updated, r->unchanged, r->nconflicts, r->nrejected, r->skipped) <
        0) {
        return -1;
    }
    return 0;
}

static int import_report_parse(const ImportDoc *doc)
{
    const char *msg = (doc->parse_message != NULL) ? doc->parse_message : "isn't a remember export.";
    const char *fix =
        (doc->parse_fix != NULL) ? doc->parse_fix : diag_code_fix(DIAG_IMPORT_PARSE);

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

static int import_from_json(Store *s, bool json, const char *path, const char *origin)
{
    ImportDoc doc;
    StoreImportResult result;
    char now[ISO_TS_BUFSIZE];
    int sys_errno = 0;
    StoreStatus st = STORE_OK;
    int rc = 0;

    memset(&doc, 0, sizeof(doc));
    memset(&result, 0, sizeof(result));
    if (import_read_json(path, &doc, &sys_errno) != 0) {
        return import_report_io(path, sys_errno);
    }
    if (doc.parse_message != NULL) {
        rc = import_report_parse(&doc);
        import_doc_free(&doc);
        return rc;
    }
    if (utc_now(now, sizeof(now)) != 0) {
        import_doc_free(&doc);
        err_msg(DIAG_INTERNAL, "internal error");
        return REMEMBER_ERR;
    }
    st = store_import_doc(s, &doc, now, origin, &result);
    import_doc_free(&doc);
    if (st != STORE_OK) {
        return store_status_to_exit(s, st);
    }
    rc = import_write(json, &result);
    store_import_result_free(&result);
    if (rc != 0) {
        err_msg(DIAG_INTERNAL, "failed to write output");
        return REMEMBER_ERR;
    }
    return REMEMBER_OK;
}

int cmd_import(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    const char *from_db = NULL;
    const char *from_json = NULL;
    const char *from_md = NULL;
    const char *from_csv = NULL;
    const char *origin_flag = NULL;
    const char *origin = NULL;
    int n_sources = 0;
    char now[ISO_TS_BUFSIZE];
    StoreImportResult result;
    int rc = 0;
    StoreStatus st = STORE_OK;
    int i = 0;

    memset(&result, 0, sizeof(result));
    for (i = 0; i < rest_argc; i++) {
        const char *arg = rest_argv[i];

        if (arg != NULL && strcmp(arg, "--origin") == 0) {
            TakeValue taken = take_value(&i, rest_argc, rest_argv, "--origin requires a label");
            if (taken.rc != 0) {
                err_msg(DIAG_USAGE, taken.err);
                return REMEMBER_ERR;
            }
            if (origin_flag != NULL) {
                err_msg(DIAG_USAGE, "duplicate --origin");
                return REMEMBER_ERR;
            }
            if (strlen(taken.value) > (size_t)ORIGIN_MAX) {
                err_msg(DIAG_USAGE, "origin is too long (max 64 bytes)");
                return REMEMBER_ERR;
            }
            origin_flag = taken.value;
            continue;
        }
        if (arg != NULL && strcmp(arg, "--from-db") == 0) {
            TakeValue taken = take_value(&i, rest_argc, rest_argv, "--from-db requires a path");
            if (taken.rc != 0) {
                err_msg(DIAG_USAGE, taken.err);
                return REMEMBER_ERR;
            }
            if (from_db != NULL) {
                err_msg(DIAG_USAGE, "duplicate --from-db");
                return REMEMBER_ERR;
            }
            from_db = taken.value;
            continue;
        }
        if (arg != NULL && strcmp(arg, "--from-json") == 0) {
            TakeValue taken = take_value(&i, rest_argc, rest_argv, "--from-json requires a path");
            if (taken.rc != 0) {
                err_msg(DIAG_USAGE, taken.err);
                return REMEMBER_ERR;
            }
            if (from_json != NULL) {
                err_msg(DIAG_USAGE, "duplicate --from-json");
                return REMEMBER_ERR;
            }
            from_json = taken.value;
            continue;
        }
        if (arg != NULL && strcmp(arg, "--from-md") == 0) {
            TakeValue taken = take_value(&i, rest_argc, rest_argv, "--from-md requires a path");
            if (taken.rc != 0) {
                err_msg(DIAG_USAGE, taken.err);
                return REMEMBER_ERR;
            }
            if (from_md != NULL) {
                err_msg(DIAG_USAGE, "duplicate --from-md");
                return REMEMBER_ERR;
            }
            from_md = taken.value;
            continue;
        }
        if (arg != NULL && strcmp(arg, "--from-csv") == 0) {
            TakeValue taken = take_value(&i, rest_argc, rest_argv, "--from-csv requires a path");
            if (taken.rc != 0) {
                err_msg(DIAG_USAGE, taken.err);
                return REMEMBER_ERR;
            }
            if (from_csv != NULL) {
                err_msg(DIAG_USAGE, "duplicate --from-csv");
                return REMEMBER_ERR;
            }
            from_csv = taken.value;
            continue;
        }
        if (arg != NULL && cmd_is_hooks_flag(arg)) {
            continue;
        }
        if (arg != NULL && arg[0] == '-' && arg[1] != '\0') {
            err_fmt(DIAG_USAGE, "unknown option '%s'", arg);
            return REMEMBER_ERR;
        }
        err_msg(DIAG_USAGE, "import takes no positional arguments");
        return REMEMBER_ERR;
    }
    n_sources = (from_db != NULL ? 1 : 0) + (from_json != NULL ? 1 : 0) + (from_md != NULL ? 1 : 0) +
                (from_csv != NULL ? 1 : 0);
    if (n_sources != 1) {
        err_msg(DIAG_USAGE,
                "import requires exactly one of --from-json, --from-md, --from-csv, --from-db");
        return REMEMBER_ERR;
    }
    if (from_json != NULL) {
        origin = (origin_flag != NULL) ? origin_flag : import_basename(from_json);
        return import_from_json(s, json, from_json, origin);
    }
    if (from_md != NULL || from_csv != NULL) {
        /* Stage 2 wires JSON only; Markdown and CSV land in later stages. */
        err_msg(DIAG_USAGE, from_md != NULL ? "--from-md is not implemented yet"
                                             : "--from-csv is not implemented yet");
        return REMEMBER_ERR;
    }
    if (utc_now(now, sizeof(now)) != 0) {
        err_msg(DIAG_INTERNAL, "internal error");
        return REMEMBER_ERR;
    }
    origin = (origin_flag != NULL) ? origin_flag : import_basename(from_db);
    st = store_import(s, from_db, now, origin, &result);
    if (st == STORE_ERR_SQLITE && store_last_error(s)->import_source) {
        /* The source failed, not the brain: name the --from-db file. */
        err_store_sqlite(store_last_error(s), from_db);
        return REMEMBER_ERR;
    }
    if (st != STORE_OK) {
        return store_status_to_exit(s, st);
    }
    rc = import_write(json, &result);
    store_import_result_free(&result);
    if (rc != 0) {
        err_msg(DIAG_INTERNAL, "failed to write output");
        return REMEMBER_ERR;
    }
    return REMEMBER_OK;
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
