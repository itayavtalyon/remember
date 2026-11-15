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
#include <string.h>

enum BrainVerb { BRAIN_VERB_NONE, BRAIN_VERB_NEW_ID, BRAIN_VERB_LIST, BRAIN_VERB_PRUNE };

static enum BrainVerb brain_verb(int rest_argc, const char **rest_argv)
{
    if (rest_argc < 1 || rest_argv == NULL) {
        return BRAIN_VERB_NONE;
    }
    if (strcmp(rest_argv[0], "new-id") == 0) {
        return BRAIN_VERB_NEW_ID;
    }
    if (strcmp(rest_argv[0], "list") == 0) {
        return BRAIN_VERB_LIST;
    }
    return strcmp(rest_argv[0], "prune") == 0 ? BRAIN_VERB_PRUNE : BRAIN_VERB_NONE;
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
    return verb == BRAIN_VERB_PRUNE ? "brain.prune" : "brain.new_id";
}

/* 0 for `new-id`, `list` or `prune` with no argument. Anything else is usage and
   must not open: a bare `brain` would otherwise create a brain file. */
int cmd_brain_reject_args(int rest_argc, const char **rest_argv)
{
    enum BrainVerb verb = brain_verb(rest_argc, rest_argv);

    if (verb == BRAIN_VERB_NONE) {
        err_msg(DIAG_USAGE, "brain takes 'new-id'");
        return -1;
    }
    if (rest_argc > 1) {
        const char *message = "brain prune takes no arguments";

        if (verb == BRAIN_VERB_NEW_ID) {
            message = "brain new-id takes no arguments";
        } else if (verb == BRAIN_VERB_LIST) {
            message = "brain list takes no arguments";
        }
        err_msg(DIAG_USAGE, message);
        return -1;
    }
    return 0;
}

/* brains.json cannot be read: the file is left alone (G5 A). */
static int report_unreadable(void)
{
    err_msg(DIAG_CANNOT_OPEN, "cannot read brains.json");
    return REMEMBER_ERR;
}

static int brain_list_run(bool json)
{
    char file[REMEMBER_PATH_MAX];
    BrainList list = {0};
    size_t i = 0;
    int rc = REMEMBER_OK;

    if (brain_json_path(file, sizeof(file)) != 0) {
        err_msg(DIAG_CANNOT_OPEN, "cannot find the data folder");
        return REMEMBER_ERR;
    }
    if (brain_list_read(file, &list) != BRAIN_LIST_UNCHANGED) {
        return report_unreadable();
    }
    if (json) {
        rc = output_brain_list_data(app_out(), &list) == 0 ? REMEMBER_OK : REMEMBER_ERR;
    }
    for (i = 0; i < list.count && rc == REMEMBER_OK; i++) {
        const char *path = list.rows[i].path;

        /* The path is masked: brains.json is a file another program may have written. */
        if (!json &&
            (output_text_human(app_out(), path, false) != 0 || fputc('\n', app_out()) == EOF)) {
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

static int brain_prune_run(bool json)
{
    char file[REMEMBER_PATH_MAX];
    BrainList removed = {0};
    BrainListCode code = BRAIN_LIST_IO;
    int rc = REMEMBER_OK;

    if (brain_json_path(file, sizeof(file)) != 0) {
        err_msg(DIAG_CANNOT_OPEN, "cannot find the data folder");
        return REMEMBER_ERR;
    }
    code = brain_list_prune(file, brain_path_is_missing, NULL, &removed);
    if (code == BRAIN_LIST_UNREADABLE) {
        return report_unreadable();
    }
    if (code != BRAIN_LIST_UNCHANGED && code != BRAIN_LIST_PRUNED) {
        err_msg(DIAG_IO_ERROR, "cannot update brains.json");
        return REMEMBER_ERR;
    }
    rc = json && output_brain_prune_data(app_out(), &removed) != 0 ? REMEMBER_ERR : REMEMBER_OK;
    brain_list_free(&removed);
    if (rc != REMEMBER_OK) {
        err_msg(DIAG_INTERNAL, "failed to write output");
    }
    return rc;
}

int cmd_brain_file(bool json, int rest_argc, const char **rest_argv)
{
    return brain_verb(rest_argc, rest_argv) == BRAIN_VERB_LIST ? brain_list_run(json)
                                                               : brain_prune_run(json);
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
