#include "commands.h"
#include "commands_common.h"
#include "diag.h"

#include "appio.h"
#include "exit_codes.h"
#include "output.h"
#include "store.h"

#include <stdbool.h>
#include <string.h>

/* 0 for exactly `new-id`. Anything else is usage and must not open: a bare
   `brain` would otherwise create a brain file. */
int cmd_brain_reject_args(int rest_argc, const char **rest_argv)
{
    if (rest_argc == 1 && rest_argv != NULL && strcmp(rest_argv[0], "new-id") == 0) {
        return 0;
    }
    if (rest_argc > 1 && rest_argv != NULL && strcmp(rest_argv[0], "new-id") == 0) {
        err_msg(DIAG_USAGE, "brain new-id takes no arguments");
        return -1;
    }
    err_msg(DIAG_USAGE, "brain takes 'new-id'");
    return -1;
}

int cmd_brain(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    StoreBrainId ids = {0};
    StoreStatus st = STORE_OK;
    int rc = 0;

    (void)rest_argc;
    (void)rest_argv;
    st = store_brain_new_id(s, &ids);
    if (st != STORE_OK) {
        err_store(s, st);
        return REMEMBER_ERR;
    }
    if (json) {
        rc = output_brain_new_id_data(app_out(), &ids);
    } else {
        rc = output_device_human(app_out(), ids.id);
    }
    if (rc != 0) {
        err_msg(DIAG_INTERNAL, "failed to write output");
        return REMEMBER_ERR;
    }
    return REMEMBER_OK;
}
