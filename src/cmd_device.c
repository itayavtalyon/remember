#include "commands.h"
#include "commands_common.h"
#include "diag.h"

#include "appio.h"
#include "exit_codes.h"
#include "output.h"
#include "store.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

enum { DEVICE_ERR_BUF = 256 };

static int reject_device_args(int rest_argc, const char **rest_argv, bool *make_new)
{
    int i = 0;

    *make_new = false;
    if (rest_argc > 0 && rest_argv == NULL) {
        err_msg(DIAG_INTERNAL, "internal error");
        return -1;
    }
    for (i = 0; i < rest_argc; i++) {
        const char *arg = rest_argv[i];

        if (arg[0] == '-' && arg[1] != '\0') {
            err_fmt(DIAG_USAGE, "unknown option '%s'", arg);
            return -1;
        }
        if (i == 0 && strcmp(arg, "new") == 0) {
            *make_new = true;
            continue;
        }
        if (*make_new) {
            err_msg(DIAG_USAGE, "device new takes no arguments");
        } else {
            err_msg(DIAG_USAGE, "device takes 'new' or no arguments");
        }
        return -1;
    }
    return 0;
}

static int emit_current_device(bool json, const char *device_id)
{
    int rc = 0;

    if (device_id == NULL) {
        err_msg(DIAG_INTERNAL, "internal error");
        return REMEMBER_ERR;
    }
    if (json) {
        rc = output_device_envelope(app_out(), device_id);
    } else {
        rc = output_device_human(app_out(), device_id);
    }
    if (rc != 0) {
        err_msg(DIAG_INTERNAL, "failed to write output");
        return REMEMBER_ERR;
    }
    return REMEMBER_OK;
}

static int emit_new_device(Store *s, bool json)
{
    char previous[STORE_DEVICE_ID_LEN + 1];
    char err[DEVICE_ERR_BUF];
    bool had_prev = false;
    StoreStatus st = STORE_OK;
    int rc = 0;

    err[0] = '\0';
    previous[0] = '\0';
    st = store_device_new(s, previous, sizeof(previous), &had_prev, err, sizeof(err));
    if (st != STORE_OK) {
        err_msg(err[0] != '\0' ? code_for_store_text(err) : code_for_store_status(st),
                err[0] != '\0' ? err : store_status_message(st));
        return REMEMBER_ERR;
    }
    if (json) {
        const char *shown = NULL;

        if (had_prev) {
            shown = previous;
        }
        rc = output_device_new_envelope(app_out(), store_device_id(s), shown);
    } else {
        rc = output_device_human(app_out(), store_device_id(s));
    }
    if (rc != 0) {
        err_msg(DIAG_INTERNAL, "failed to write output");
        return REMEMBER_ERR;
    }
    return REMEMBER_OK;
}

bool cmd_device_mints(int rest_argc, const char **rest_argv)
{
    if (rest_argc != 1 || rest_argv == NULL) {
        return false;
    }
    if (strcmp(rest_argv[0], "new") != 0) {
        return false;
    }
    return true;
}

int cmd_device(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    bool make_new = false;

    if (reject_device_args(rest_argc, rest_argv, &make_new) != 0) {
        return REMEMBER_ERR;
    }
    if (!make_new) {
        return emit_current_device(json, store_device_id(s));
    }
    return emit_new_device(s, json);
}
