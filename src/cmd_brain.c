#include "cli.h"
#include "commands.h"
#include "commands_common.h"
#include "diag.h"

#include "appio.h"
#include "brain_list.h"
#include "brain_note.h"
#include "exit_codes.h"
#include "output.h"
#include "store.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

enum BrainVerb {
    BRAIN_VERB_NONE,
    BRAIN_VERB_NEW_ID,
    BRAIN_VERB_LIST,
    BRAIN_VERB_PRUNE,
    BRAIN_VERB_SWEEP
};

static enum BrainVerb brain_verb(int rest_argc, const char **rest_argv)
{
    CliSub sub = cli_subcommand(CLI_CMD_BRAIN, rest_argc, rest_argv);

    if (sub == CLI_SUB_BRAIN_NEW_ID) {
        return BRAIN_VERB_NEW_ID;
    }
    if (sub == CLI_SUB_BRAIN_LIST) {
        return BRAIN_VERB_LIST;
    }
    if (sub == CLI_SUB_BRAIN_SWEEP) {
        return BRAIN_VERB_SWEEP;
    }
    return sub == CLI_SUB_BRAIN_PRUNE ? BRAIN_VERB_PRUNE : BRAIN_VERB_NONE;
}

/* `list` and `prune` read and write brains.json only. They never open a brain,
   so they cannot create one or record one. */
bool cmd_brain_uses_store(int rest_argc, const char **rest_argv)
{
    return brain_verb(rest_argc, rest_argv) == BRAIN_VERB_NEW_ID;
}

/* The envelope type of each verb. */
const char *cmd_brain_reply(int rest_argc, const char **rest_argv)
{
    enum BrainVerb verb = brain_verb(rest_argc, rest_argv);

    if (verb == BRAIN_VERB_LIST) {
        return "brain.list";
    }
    if (verb == BRAIN_VERB_SWEEP) {
        return "brain.sweep";
    }
    return verb == BRAIN_VERB_PRUNE ? "brain.prune" : "brain.new_id";
}

/* 0 for `new-id`, `list` or `prune` with no argument. Anything else is usage and
   must not open: a bare `brain` would otherwise create a brain file. */
int cmd_brain_reject_args(int rest_argc, const char **rest_argv)
{
    enum BrainVerb verb = brain_verb(rest_argc, rest_argv);

    if (verb == BRAIN_VERB_NONE) {
        (void)diag_error_fix(DIAG_USAGE,
                             "'remember brain' needs a command: list, prune, sweep, or new-id.",
                             "Run 'remember brain --help' to see what each does.");
        return -1;
    }
    if (rest_argc > 1) {
        /* rest_argv[0] is one of the three verbs brain_verb matched. */
        err_fmt(DIAG_USAGE, "brain %s takes no arguments", rest_argv[0]);
        return -1;
    }
    return 0;
}

/* C-35 for a member brains.json holds that the reader does not take (G6 B). */
static int warn_unknown(const char *file, const char *name)
{
    if (name == NULL) {
        return 0;
    }
    return diag_unknown_field((DiagUnknownField){.path = file, .name = name});
}

/* brains.json cannot be read: the file is left alone (G5 A). */
static int report_unreadable(const char *file)
{
    brain_note_unreadable_error(file);
    return REMEMBER_ERR;
}

static int report_no_data_dir(const char *why)
{
    brain_note_no_data_dir(why);
    return REMEMBER_ERR;
}

static int brain_list_run(bool json)
{
    char file[REMEMBER_PATH_MAX];
    char why[BRAIN_WHY_CAP];
    BrainList list = {0};
    size_t i = 0;
    int rc = REMEMBER_OK;

    if (brain_json_path(file, sizeof(file), why, sizeof(why)) != 0) {
        return report_no_data_dir(why);
    }
    if (brain_list_read(file, &list) != BRAIN_LIST_UNCHANGED) {
        return report_unreadable(file);
    }
    if (warn_unknown(file, list.unknown_field) != 0) {
        rc = REMEMBER_ERR;
    }
    /* The data goes to the envelope's buffer; a failed warning above is not overwritten. */
    if (json && rc == REMEMBER_OK) {
        rc = output_brain_list_data(app_out(), &list) == 0 ? REMEMBER_OK : REMEMBER_ERR;
    }
    for (i = 0; i < list.count && rc == REMEMBER_OK; i++) {
        const char *path = list.rows[i].path;

        if (!json && output_brain_row_human(app_out(), &list.rows[i]) != 0) {
            rc = REMEMBER_ERR;
        }
        if (brain_path_is_missing(NULL, path) && diag_brain_missing(path) != 0) {
            rc = REMEMBER_ERR;
        }
    }
    brain_list_free(&list);
    if (rc != REMEMBER_OK) {
        err_msg(DIAG_INTERNAL, "failed to write output");
    }
    return rc;
}

static int brain_prune_run(bool json, int busy_timeout_ms)
{
    char file[REMEMBER_PATH_MAX];
    char why[BRAIN_WHY_CAP];
    BrainList removed = {0};
    BrainListCode code = BRAIN_LIST_IO;
    char *unknown = NULL;
    int rc = REMEMBER_OK;

    if (brain_json_path(file, sizeof(file), why, sizeof(why)) != 0) {
        return report_no_data_dir(why);
    }
    code = brain_list_prune(file, brain_path_is_missing, NULL, busy_timeout_ms, &removed, &unknown);
    if (warn_unknown(file, unknown) != 0) {
        rc = REMEMBER_ERR;
    }
    free(unknown);
    if (code == BRAIN_LIST_UNREADABLE) {
        return report_unreadable(file);
    }
    if (code == BRAIN_LIST_BUSY) {
        brain_note_busy_error();
        return REMEMBER_ERR;
    }
    if (code != BRAIN_LIST_UNCHANGED && code != BRAIN_LIST_PRUNED) {
        brain_note_update_error(file);
        return REMEMBER_ERR;
    }
    if (json && rc == REMEMBER_OK) {
        rc = output_brain_prune_data(app_out(), &removed) == 0 ? REMEMBER_OK : REMEMBER_ERR;
    }
    brain_list_free(&removed);
    if (rc != REMEMBER_OK) {
        err_msg(DIAG_INTERNAL, "failed to write output");
    }
    return rc;
}

/* busy_timeout_ms bounds prune's wait for the brains.json lock (G5); list takes no lock. */
int cmd_brain_file(bool json, int busy_timeout_ms, int rest_argc, const char **rest_argv)
{
    return brain_verb(rest_argc, rest_argv) == BRAIN_VERB_LIST
               ? brain_list_run(json)
               : brain_prune_run(json, busy_timeout_ms);
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
    /* The open recorded the old id; record the new one, under the same lock and wait. */
    if (brain_note_open(s, false) != 0) {
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
