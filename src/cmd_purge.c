#include "commands.h"
#include "commands_common.h"

#include "appio.h"
#include "exit_codes.h"
#include "output.h"
#include "store.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

int cmd_purge_trash(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    char now[32];
    Entry *entries = NULL;
    size_t count = 0U;
    StoreStatus st;
    size_t i;
    int rc = REMEMBER_ERR;

    if (rest_argc > 0) {
        if (rest_argv != NULL && rest_argv[0] != NULL && rest_argv[0][0] == '-' &&
            rest_argv[0][1] != '\0') {
            (void)fprintf(app_err(), "remember: unknown option '%s'\n", rest_argv[0]);
        } else {
            err_msg("purge-trash takes no arguments");
        }
        return REMEMBER_ERR;
    }
    if (utc_now(now, sizeof(now)) != 0) {
        err_msg("internal error");
        return REMEMBER_ERR;
    }
    st = store_purge_trash(s, now, &entries, &count);
    if (st != STORE_OK) {
        err_msg(store_status_message(st));
        goto cleanup;
    }
    if (json) {
        if (output_deleted_list(app_out(), entries, count) != 0) {
            err_msg("failed to write output");
            goto cleanup;
        }
    } else if (fprintf(app_out(), "%zu\n", count) < 0) {
        err_msg("failed to write output");
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
