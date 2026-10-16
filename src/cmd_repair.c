#include "commands.h"
#include "commands_common.h"
#include "diag.h"

#include "appio.h"
#include "exit_codes.h"
#include "output.h"
#include "store.h"

#include <stdbool.h>
#include <string.h>

static int reject_repair_args(int rest_argc, const char **rest_argv)
{
    if (rest_argc == 1 && rest_argv != NULL && strcmp(rest_argv[0], "search-index") == 0) {
        return 0;
    }
    err_msg(DIAG_USAGE, "usage: repair search-index");
    return -1;
}

int cmd_repair(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    size_t entries = 0U;
    StoreStatus st = STORE_OK;

    if (reject_repair_args(rest_argc, rest_argv) != 0) {
        return REMEMBER_ERR;
    }
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
