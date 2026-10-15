#include "commands.h"
#include "commands_common.h"
#include "diag.h"

#include "appio.h"
#include "exit_codes.h"
#include "output.h"
#include "store.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

int cmd_tags(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    TagCount *tags = NULL;
    size_t count = 0U;
    StoreStatus st = STORE_OK;
    int rc = REMEMBER_ERR;
    char now[ISO_TS_BUFSIZE];
    CmdBinOpts bins;
    StoreBin bin = STORE_BIN_LIVE;
    int i = 0;

    memset(&bins, 0, sizeof(bins));
    for (i = 0; i < rest_argc; i++) {
        const char *arg = rest_argv[i];
        if (cmd_bin_take_flag(arg, &bins) != 0) {
            continue;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            err_fmt(DIAG_USAGE, "unknown option '%s'", arg);
            return REMEMBER_ERR;
        }
        err_msg(DIAG_USAGE, "tags takes no arguments");
        return REMEMBER_ERR;
    }
    if (utc_now(now, sizeof(now)) != 0) {
        err_msg(DIAG_INTERNAL, "internal error");
        return REMEMBER_ERR;
    }
    if (cmd_bin_resolve(&bins, &bin) != 0) {
        return REMEMBER_ERR;
    }

    st = store_tags(s, bin, now, &tags, &count);
    if (st != STORE_OK) {
        err_store(st);
        return REMEMBER_ERR;
    }

    if (json) {
        if (output_tags_envelope(app_out(), tags, count) != 0) {
            err_msg(DIAG_INTERNAL, "failed to write output");
            goto cleanup;
        }
    } else {
        if (output_tags_human(app_out(), tags, count) != 0) {
            err_msg(DIAG_INTERNAL, "failed to write output");
            goto cleanup;
        }
    }
    rc = REMEMBER_OK;

cleanup:
    store_tags_free(tags, count);
    return rc;
}
