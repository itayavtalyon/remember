#include "brain_note.h"

#include "brain_list.h"
#include "diag.h"
#include "store.h"
#include "util.h"

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
        return true;
    }
    if (S_ISREG(st.st_mode)) {
        return false;
    }
    return true;
}

static bool still_has_brain(void *ctx, const char *path, const char *brain_id)
{
    (void)ctx;
    return store_path_has_brain_id(path, brain_id);
}

static int mint_id(void *ctx, char *out, size_t cap)
{
    (void)ctx;
    return store_mint_uuid(out, cap);
}

StoreStatus brain_note_open(Store *store)
{
    const BrainProbe probe = {.still = still_has_brain, .mint = mint_id, .ctx = NULL};
    char json[REMEMBER_PATH_MAX];
    char real[REMEMBER_PATH_MAX];
    char id[STORE_DEVICE_ID_LEN + 1];
    char minted[STORE_DEVICE_ID_LEN + 1];
    char now[STORE_DEVICE_ID_LEN];
    BrainListCode code = BRAIN_LIST_IO;
    StoreStatus st = STORE_OK;

    if (!store_is_writable(store) || brain_json_path(json, sizeof(json)) != 0 ||
        store_brain_id(store, id, sizeof(id)) != STORE_OK ||
        realpath(store_path(store), real) == NULL || utc_now(now, sizeof(now)) != 0) {
        return STORE_OK;
    }
    code = brain_list_record(json, (BrainRecordIn){.real_path = real, .brain_id = id, .now = now},
                             &probe, minted, sizeof(minted));
    if (code != BRAIN_LIST_CLONED) {
        return STORE_OK;
    }
    st = store_brain_set_id(store, minted);
    if (st != STORE_OK) {
        return st;
    }
    (void)diag_brain_id_cloned(real, minted);
    return STORE_OK;
}
