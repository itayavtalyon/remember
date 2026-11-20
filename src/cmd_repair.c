#include "cli.h"
#include "commands.h"
#include "commands_common.h"
#include "diag.h"

#include "appio.h"
#include "exit_codes.h"
#include "output.h"
#include "store.h"

#include <stdbool.h>
#include <stddef.h>

int cmd_repair_reject_args(int rest_argc, const char **rest_argv)
{
    if (rest_argc == 1 &&
        cli_subcommand(CLI_CMD_REPAIR, rest_argc, rest_argv) == CLI_SUB_REPAIR_SEARCH_INDEX) {
        return 0;
    }
    err_msg(DIAG_USAGE, "usage: repair search-index");
    return -1;
}

/* The caller ran cmd_repair_reject_args before the open. */
int cmd_repair(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    size_t entries = 0U;
    StoreStatus st = STORE_OK;

    (void)rest_argc;
    (void)rest_argv;
    st = store_repair_search_index(s, &entries);
    if (st != STORE_OK) {
        err_store(s, st);
        return REMEMBER_ERR;
    }
    if (output_rebuilt(app_out(), json, entries) != 0) {
        err_msg(DIAG_INTERNAL, "failed to write output");
        return REMEMBER_ERR;
    }
    return REMEMBER_OK;
}
