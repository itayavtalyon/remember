#include "sweep.h"

#include "commands_common.h"
#include "diag.h"

#include "appio.h"
#include "brain_list.h"
#include "brain_note.h"
#include "exit_codes.h"
#include "output.h"
#include "store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int sweep_parse(int rest_argc, const char **rest_argv, bool *all)
{
    int i = 0;

    *all = false;
    for (i = 0; i < rest_argc; i++) {
        const char *arg = rest_argv[i];

        if (strcmp(arg, "--all") == 0 && !*all) {
            *all = true;
        } else if (arg[0] == '-' && arg[1] != '\0' && strcmp(arg, "--all") != 0) {
            err_fmt(DIAG_USAGE, "unknown option '%s'", arg);
            return -1;
        } else {
            err_msg(DIAG_USAGE, "brain sweep takes no arguments but --all");
            return -1;
        }
    }
    return 0;
}

StoreStatus sweep_now(Store *s, size_t *marked)
{
    char now[ISO_TS_BUFSIZE];

    *marked = 0U;
    if (!store_is_writable(s)) {
        return STORE_OK;
    }
    if (utc_now(now, sizeof(now)) != 0) {
        return STORE_ERR_INTERNAL;
    }
    return store_sweep(s, now, marked);
}

/* The data of brain.sweep.result: the total, and for --all each brain's count. */
static int write_result(bool json, size_t total, const BrainList *list, const size_t *counts)
{
    if (!json) {
        return fprintf(app_out(), "%zu\n", total) < 0 ? -1 : 0;
    }
    if (fprintf(app_out(), "{\"expired\":%zu", total) < 0) {
        return -1;
    }
    if (list != NULL) {
        if (fputs(",\"brains\":[", app_out()) < 0) {
            return -1;
        }
        for (size_t i = 0; i < list->count; i++) {
            if ((i > 0U && fputc(',', app_out()) == EOF) || fputs("{\"path\":", app_out()) < 0 ||
                output_json_string(app_out(), list->rows[i].path) != 0 ||
                fprintf(app_out(), ",\"expired\":%zu}", counts[i]) < 0) {
                return -1;
            }
        }
        if (fputc(']', app_out()) == EOF) {
            return -1;
        }
    }
    return fputc('}', app_out()) == EOF ? -1 : 0;
}

/* `brain sweep`. The arguments were parsed by sweep_parse before the brain opened (a usage error
   opens nothing). */
int cmd_sweep(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    size_t marked = 0U;
    StoreStatus st = STORE_OK;

    (void)rest_argc;
    (void)rest_argv;
    st = sweep_now(s, &marked);
    if (st != STORE_OK) {
        err_store(s, st);
        return REMEMBER_ERR;
    }
    if (write_result(json, marked, NULL, NULL) != 0) {
        err_msg(DIAG_INTERNAL, "failed to write output");
        return REMEMBER_ERR;
    }
    return REMEMBER_OK;
}

int cmd_sweep_all(bool json, SweepBrainFn sweep_one, const void *ctx)
{
    char file[REMEMBER_PATH_MAX];
    char why[BRAIN_WHY_CAP];
    BrainList list = {0};
    size_t *counts = NULL;
    size_t total = 0U;
    size_t i = 0;
    int rc = REMEMBER_OK;

    if (brain_json_path(file, sizeof(file), why, sizeof(why)) != 0) {
        brain_note_no_data_dir(why);
        return REMEMBER_ERR;
    }
    if (brain_list_read(file, &list) != BRAIN_LIST_UNCHANGED) {
        brain_list_free(&list);
        brain_note_unreadable_error(file);
        return REMEMBER_ERR;
    }
    counts = (size_t *)calloc(list.count + 1U, sizeof(*counts));
    if (counts == NULL) {
        brain_list_free(&list);
        err_msg(DIAG_OUT_OF_MEMORY, "out of memory");
        return REMEMBER_ERR;
    }
    for (i = 0; i < list.count; i++) {
        if (brain_path_is_missing(NULL, list.rows[i].path)) {
            if (diag_brain_missing(list.rows[i].path) != 0) {
                rc = REMEMBER_ERR;
            }
        } else if (sweep_one(ctx, list.rows[i].path, &counts[i]) == 0) {
            total += counts[i];
        }
    }
    if (rc == REMEMBER_OK && write_result(json, total, &list, counts) != 0) {
        err_msg(DIAG_INTERNAL, "failed to write output");
        rc = REMEMBER_ERR;
    }
    free(counts);
    brain_list_free(&list);
    return rc;
}
