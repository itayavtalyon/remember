#include "commands.h"
#include "commands_common.h"

#include "appio.h"
#include "exit_codes.h"
#include "output.h"
#include "store.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

int cmd_tags(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    TagCount *tags = NULL;
    size_t count = 0U;
    StoreStatus st;
    int rc = REMEMBER_ERR;
    char now[32];
    bool trash = false;
    int i;

    for (i = 0; i < rest_argc; i++) {
        const char *arg = rest_argv[i];
        if (strcmp(arg, "--trash") == 0) {
            trash = true;
            continue;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            (void)fprintf(app_err(), "remember: unknown option '%s'\n", arg);
            return REMEMBER_ERR;
        }
        err_msg("tags takes no arguments");
        return REMEMBER_ERR;
    }
    if (utc_now(now, sizeof(now)) != 0) {
        err_msg("internal error");
        return REMEMBER_ERR;
    }

    st = store_tags(s, trash, now, &tags, &count);
    if (st != STORE_OK) {
        err_msg(store_status_message(st));
        return REMEMBER_ERR;
    }

    if (json) {
        if (output_tags_envelope(app_out(), tags, count) != 0) {
            err_msg("failed to write output");
            goto cleanup;
        }
    } else {
        if (output_tags_human(app_out(), tags, count) != 0) {
            err_msg("failed to write output");
            goto cleanup;
        }
    }
    rc = REMEMBER_OK;

cleanup:
    store_tags_free(tags, count);
    return rc;
}
