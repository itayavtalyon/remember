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

static int emit_purge_result(bool json, const Entry *entries, size_t count, const char *now)
{
    if (json) {
        if (output_deleted_list(app_out(), entries, count, now) != 0) {
            err_msg("failed to write output");
            return -1;
        }
        return 0;
    }
    if (fprintf(app_out(), "%zu\n", count) < 0) {
        err_msg("failed to write output");
        return -1;
    }
    return 0;
}

static int run_purge_bin(Store *s, bool json, StoreBin bin)
{
    char now[ISO_TS_BUFSIZE];
    Entry *entries = NULL;
    size_t count = 0U;
    StoreStatus st = STORE_OK;
    size_t i = 0;
    int rc = REMEMBER_ERR;

    if (utc_now(now, sizeof(now)) != 0) {
        err_msg("internal error");
        return REMEMBER_ERR;
    }
    st = store_purge(s, bin, now, &entries, &count);
    if (st != STORE_OK) {
        err_msg(store_status_message(st));
        goto cleanup;
    }
    if (emit_purge_result(json, entries, count, now) != 0) {
        goto cleanup;
    }
    rc = REMEMBER_OK;

cleanup:
    for (i = 0; i < count; i++) {
        store_entry_free(&entries[i]);
    }
    free(entries);
    return rc;
}

int cmd_purge(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    CmdBinOpts bins;
    StoreBin bin = STORE_BIN_LIVE;
    int i = 0;

    memset(&bins, 0, sizeof(bins));
    for (i = 0; i < rest_argc; i++) {
        const char *arg = rest_argv[i];

        if (cmd_bin_take_flag(arg, &bins) != 0) {
            continue;
        }
        if (arg != NULL && arg[0] == '-' && arg[1] != '\0') {
            (void)fprintf(app_err(), "remember: unknown option '%s'\n", arg);
            return REMEMBER_ERR;
        }
        err_msg("purge takes no positional arguments");
        return REMEMBER_ERR;
    }
    if (cmd_bin_resolve(&bins, &bin) != 0) {
        return REMEMBER_ERR;
    }
    if (bin == STORE_BIN_LIVE) {
        err_msg("purge requires --expired or --deleted");
        return REMEMBER_ERR;
    }
    return run_purge_bin(s, json, bin);
}

int cmd_purge_trash(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    (void)fprintf(app_err(), "remember: purge-trash is deprecated; use purge --expired\n");
    if (rest_argc > 0) {
        if (rest_argv != NULL && rest_argv[0] != NULL && rest_argv[0][0] == '-' &&
            rest_argv[0][1] != '\0') {
            (void)fprintf(app_err(), "remember: unknown option '%s'\n", rest_argv[0]);
        } else {
            err_msg("purge-trash takes no arguments");
        }
        return REMEMBER_ERR;
    }
    return run_purge_bin(s, json, STORE_BIN_EXPIRED);
}
