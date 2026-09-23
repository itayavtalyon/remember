#include "commands.h"
#include "commands_common.h"

#include "appio.h"
#include "exit_codes.h"
#include "output.h"
#include "store.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_import(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    const char *from_db = NULL;
    char now[ISO_TS_BUFSIZE];
    StoreImportCounts counts;
    StoreStatus st = STORE_OK;
    int i = 0;

    memset(&counts, 0, sizeof(counts));
    for (i = 0; i < rest_argc; i++) {
        const char *arg = rest_argv[i];

        if (arg != NULL && strcmp(arg, "--from-db") == 0) {
            TakeValue taken = take_value(&i, rest_argc, rest_argv, "--from-db requires a path");
            if (taken.rc != 0) {
                err_msg(taken.err);
                return REMEMBER_ERR;
            }
            if (from_db != NULL) {
                err_msg("duplicate --from-db");
                return REMEMBER_ERR;
            }
            from_db = taken.value;
            continue;
        }
        if (arg != NULL && arg[0] == '-' && arg[1] != '\0') {
            (void)fprintf(app_err(), "remember: unknown option '%s'\n", arg);
            return REMEMBER_ERR;
        }
        err_msg("import takes no positional arguments");
        return REMEMBER_ERR;
    }
    if (from_db == NULL) {
        err_msg("import requires --from-db PATH");
        return REMEMBER_ERR;
    }
    if (utc_now(now, sizeof(now)) != 0) {
        err_msg("internal error");
        return REMEMBER_ERR;
    }
    st = store_import(s, from_db, now, &counts);
    if (st != STORE_OK) {
        return store_status_to_exit(st);
    }
    if (json) {
        if (output_import_envelope(app_out(), &counts) != 0) {
            err_msg("failed to write output");
            return REMEMBER_ERR;
        }
    } else if (fprintf(app_out(), "inserted %zu updated %zu unchanged %zu conflicts %zu\n",
                       counts.inserted, counts.updated, counts.unchanged, counts.conflicts) < 0) {
        err_msg("failed to write output");
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
        err_msg("conflicts takes no arguments");
        return REMEMBER_ERR;
    }
    (void)rest_argv;
    st = store_conflicts_list(s, &rows, &count);
    if (st != STORE_OK) {
        return store_status_to_exit(st);
    }
    if (json) {
        if (output_conflicts_envelope(app_out(), rows, count) != 0) {
            err_msg("failed to write output");
            goto cleanup;
        }
    } else {
        for (i = 0; i < count; i++) {
            const char *reason = store_conflict_reason_str(rows[i].reason);
            if (fprintf(app_out(), "%lld\t%s\t%s\n", rows[i].id,
                        rows[i].sync_id != NULL ? rows[i].sync_id : "",
                        reason != NULL ? reason : "?") < 0) {
                err_msg("failed to write output");
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
        err_msg(taken.err);
        return REMEMBER_ERR;
    }
    if (*have_id) {
        err_msg("duplicate --id");
        return REMEMBER_ERR;
    }
    if (parse_entry_id(taken.value, conflict_id) != 0) {
        err_msg("invalid conflict id");
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
        err_msg(taken.err);
        return REMEMBER_ERR;
    }
    if (*have_keep) {
        err_msg("duplicate --keep");
        return REMEMBER_ERR;
    }
    if (parse_keep(taken.value, keep) != 0) {
        err_msg("--keep must be local, incoming, or both");
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
        if (arg != NULL && arg[0] == '-' && arg[1] != '\0') {
            (void)fprintf(app_err(), "remember: unknown option '%s'\n", arg);
            return REMEMBER_ERR;
        }
        err_msg("conflict accept takes no positional arguments");
        return REMEMBER_ERR;
    }
    if (!have_id || !have_keep) {
        err_msg("conflict accept requires --id and --keep");
        return REMEMBER_ERR;
    }
    return 0;
}

static int conflict_accept_emit(bool json, const Entry *entries, size_t count, const char *now)
{
    size_t i = 0;

    if (json) {
        if (output_accepted_envelope(app_out(), entries, count, now) != 0) {
            err_msg("failed to write output");
            return REMEMBER_ERR;
        }
        return REMEMBER_OK;
    }
    for (i = 0; i < count; i++) {
        if (output_id_human(app_out(), entries[i].id) != 0) {
            err_msg("failed to write output");
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
        err_msg("usage: conflict accept --id N --keep local|incoming|both");
        return REMEMBER_ERR;
    }
    if (conflict_accept_parse(rest_argc, rest_argv, &conflict_id, &keep) != 0) {
        return REMEMBER_ERR;
    }
    if (utc_now(now, sizeof(now)) != 0) {
        err_msg("internal error");
        return REMEMBER_ERR;
    }
    st = store_conflict_accept(s, conflict_id, keep, now, &entries, &count);
    if (st != STORE_OK) {
        return store_status_to_exit(st);
    }
    rc = conflict_accept_emit(json, entries, count, now);

    for (i = 0; i < count; i++) {
        store_entry_free(&entries[i]);
    }
    free(entries);
    return rc;
}
