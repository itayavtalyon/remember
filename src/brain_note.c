#include "brain_note.h"

#include "brain_list.h"
#include "commands_common.h"
#include "diag.h"
#include "fileio.h"
#include "store.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { DATA_DIR_ERR_CAP = 64 };

int brain_json_path(char *out, size_t cap, char *why, size_t why_cap)
{
    char dir[REMEMBER_PATH_MAX];
    char err[DATA_DIR_ERR_CAP] = "";

    if (util_resolve_data_dir(dir, sizeof(dir), err, sizeof(err)) != 0) {
        if (why != NULL && why_cap > 0U) {
            (void)snprintf(why, why_cap, "%s", err);
        }
        return -1;
    }
    return (size_t)snprintf(out, cap, "%s/brains.json", dir) < cap ? 0 : -1;
}

bool brain_path_is_missing(void *ctx, const char *path)
{
    struct stat st;

    (void)ctx;
    if (stat(path, &st) != 0) {
        /* prune must not drop the row of a brain it cannot see. */
        return fileio_errno_means_absent(errno);
    }
    return S_ISREG(st.st_mode) == 0;
}

/* The probe waits the open's busy timeout, and keeps why it could not tell, and which path. */
typedef struct {
    char path[REMEMBER_PATH_MAX];
    StoreError why;
    int busy_timeout_ms;
    char pad_[4]; /* explicit tail padding (kept -Wpadded-clean) */
} StillCtx;

static int still_has_brain(void *ctx, const char *path, const char *brain_id)
{
    StillCtx *still = (StillCtx *)ctx;

    (void)snprintf(still->path, sizeof(still->path), "%s", path);
    return store_path_has_brain_id(path, brain_id, still->busy_timeout_ms, &still->why);
}

static int mint_id(void *ctx, char *out, size_t cap)
{
    (void)ctx;
    return store_mint_uuid(out, cap);
}

/* The message lines are approved copy (decision:open-2026-11-15-brains-json-unreadable-copy).
   Takes msg; a failed allocation leaves msg or fix NULL. */
static void emit_error(DiagCode code, char *msg, const char *fix)
{
    if (msg == NULL || fix == NULL) {
        err_msg(DIAG_OUT_OF_MEMORY, "out of memory");
    } else {
        (void)diag_error_fix(code, msg, fix);
    }
    free(msg);
}

/* As emit_error, for a fix that names a path. Takes fix too. */
static void emit_error_path(DiagCode code, char *msg, char *fix)
{
    emit_error(code, msg, fix);
    free(fix);
}

void brain_note_busy_error(void)
{
    (void)diag_error_fix(DIAG_BUSY, "brains.json is busy: another remember command is using it.",
                         "Try again in a moment.");
}

void brain_note_no_data_dir(const char *why)
{
    emit_error(DIAG_CANNOT_OPEN, fmt_alloc("can't find the data folder: %s.", why),
               "Fix that, then run the command again.");
}

void brain_note_unreadable_error(const char *file)
{
    emit_error(DIAG_CANNOT_OPEN, fmt_alloc("can't read brains.json (%s).", file),
               "Check that the file can be read, then run the command again.");
}

void brain_note_update_error(const char *file)
{
    emit_error(DIAG_IO_ERROR, fmt_alloc("can't update brains.json (%s).", file),
               "Check that its folder can be written to, then run the command again.");
}

int brain_note_open(Store *store, bool warn_unreadable)
{
    StillCtx still = {.busy_timeout_ms = store_busy_timeout_ms(store)};
    const BrainProbe probe = {.still = still_has_brain, .mint = mint_id, .ctx = &still};
    char json[REMEMBER_PATH_MAX];
    char real[REMEMBER_PATH_MAX];
    char id[STORE_DEVICE_ID_LEN + 1];
    char minted[STORE_DEVICE_ID_LEN + 1] = {0};
    char now[STORE_DEVICE_ID_LEN];
    char *unknown = NULL;
    BrainListCode code = BRAIN_LIST_IO;
    StoreStatus st = STORE_OK;

    if (!store_is_writable(store) || brain_json_path(json, sizeof(json), NULL, 0U) != 0 ||
        store_brain_id(store, id, sizeof(id)) != STORE_OK ||
        realpath(store_path(store), real) == NULL || utc_now(now, sizeof(now)) != 0) {
        return 0;
    }
    code = brain_list_record(json,
                             (BrainRecordIn){.real_path = real,
                                             .brain_id = id,
                                             .now = now,
                                             .lock_timeout_ms = still.busy_timeout_ms},
                             &probe,
                             (BrainRecordOut){.minted = minted,
                                              .minted_cap = sizeof(minted),
                                              .unknown_field = &unknown});
    // cppcheck-suppress knownConditionTrueFalse ; brain_list_record writes it via unknown_field
    if (unknown != NULL) {
        /* A failed warning write does not change the open (C-35 keeps the read going). */
        (void)diag_unknown_field((DiagUnknownField){.path = json, .name = unknown});
        free(unknown);
    }
    if (code == BRAIN_LIST_BUSY) {
        brain_note_busy_error();
        return -1;
    }
    if (code == BRAIN_LIST_UNDECIDED) {
        /* The code is the cause's own: busy, cannot_open, io_error, corrupt (G2). */
        DiagCode cause = code_for_store_class(still.why.error_class);

        emit_error_path(cause,
                        fmt_alloc("can't tell whether %s is a copy of this brain or the same brain "
                                  "moved.",
                                  still.path),
                        fmt_alloc("Run the command again once %s can be read.", still.path));
        return -1;
    }
    if (code == BRAIN_LIST_UNREADABLE) {
        /* Nothing is recorded and no copy is checked until the user fixes the file; the command
           goes on with its own exit code (decision:2026-11-12-brains-json-unreadable-warns). */
        if (warn_unreadable) {
            (void)diag_brains_json_unreadable(json);
        }
        return 0;
    }
    if (code != BRAIN_LIST_CLONED) {
        return 0;
    }
    st = store_brain_set_id(store, minted);
    if (st != STORE_OK) {
        err_store(store, st);
        return -1;
    }
    (void)diag_brain_id_cloned(real, minted);
    return 0;
}

int brain_note_id_is_taken(const Store *store, const char *brain_id)
{
    char json[REMEMBER_PATH_MAX];
    BrainList list = {0};
    StoreError why = {.error_class = STORE_ERROR_NONE};
    int taken = 0;
    size_t i = 0;

    /* A list that cannot be read lists nothing, as it does for the open. */
    if (brain_json_path(json, sizeof(json), NULL, 0U) != 0 ||
        brain_list_read(json, &list) != BRAIN_LIST_UNCHANGED) {
        brain_list_free(&list);
        return 0;
    }
    for (i = 0; taken == 0 && i < list.count; i++) {
        if (strcmp(list.rows[i].brain_id, brain_id) == 0) {
            taken = store_path_has_brain_id(list.rows[i].path, brain_id,
                                            store_busy_timeout_ms(store), &why);
            if (taken < 0) {
                emit_error_path(
                    code_for_store_class(why.error_class),
                    fmt_alloc("can't check whether %s still holds this file's brain.",
                              list.rows[i].path),
                    fmt_alloc("Try again once %s is free and can be read.", list.rows[i].path));
            }
        }
    }
    brain_list_free(&list);
    return taken;
}
