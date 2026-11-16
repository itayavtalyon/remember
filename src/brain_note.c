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
#include <sys/stat.h>

enum { DATA_DIR_ERR_CAP = 64 };

int brain_json_path(char *out, size_t cap)
{
    char dir[REMEMBER_PATH_MAX];
    char err[DATA_DIR_ERR_CAP];

    if (util_resolve_data_dir(dir, sizeof(dir), err, sizeof(err)) != 0) {
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

/* The probe waits the open's busy timeout, and keeps why it could not tell. */
typedef struct {
    int busy_timeout_ms;
    StoreError why;
} StillCtx;

static int still_has_brain(void *ctx, const char *path, const char *brain_id)
{
    StillCtx *still = (StillCtx *)ctx;

    return store_path_has_brain_id(path, brain_id, still->busy_timeout_ms, &still->why);
}

static int mint_id(void *ctx, char *out, size_t cap)
{
    (void)ctx;
    return store_mint_uuid(out, cap);
}

/* OPEN: the message lines of G5 and G2 are placeholders until Story Weaver approves them
   (decision:open-2026-11-15-brains-json-unreadable-copy). */
void brain_note_busy_error(void)
{
    err_msg(DIAG_BUSY, "brains.json is locked by another remember");
}

int brain_note_open(Store *store)
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

    if (!store_is_writable(store) || brain_json_path(json, sizeof(json)) != 0 ||
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
        err_msg(code_for_store_class(still.why.error_class),
                "cannot tell whether this brain is a copy of another brain in brains.json");
        return -1;
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
