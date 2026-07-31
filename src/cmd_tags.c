#include "commands.h"
#include "commands_common.h"

#include "appio.h"
#include "exit_codes.h"
#include "output.h"
#include "store.h"

#include <stdbool.h>
#include <stddef.h>

int cmd_tags(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    TagCount *tags = NULL;
    size_t count = 0U;
    StoreStatus st;
    int rc = REMEMBER_ERR;

    (void)rest_argv;
    if (rest_argc > 0) {
        err_msg("tags takes no arguments");
        return REMEMBER_ERR;
    }

    st = store_tags(s, &tags, &count);
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
