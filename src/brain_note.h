#ifndef REMEMBER_BRAIN_NOTE_H
#define REMEMBER_BRAIN_NOTE_H

#include "store.h"

#include <stdbool.h>
#include <stddef.h>

/* <data dir>/brains.json (decision:2026-11-11-brains-json-data-dir). 0, or -1
   when the data dir will not resolve or the path does not fit cap. */
int brain_json_path(char *out, size_t cap);

/*
 * decision:g2-05 and decision:g2-08 for one open. A read-write open records the
 * brain in brains.json. A brain that is a copy of a listed one (same id, the
 * other path still has it) takes a new id, and the C-38 warning is written. A
 * read-only open does nothing. A file that cannot be written leaves the previous
 * one, prints nothing, and does not change the open. STORE_OK, or the failure
 * of replacing a copy's id in the brain.
 */
StoreStatus brain_note_open(Store *store);

/* BrainMissingFn: true when path is not a regular file (stat follows a symlink and
   never opens the path, so a FIFO cannot block). */
bool brain_path_is_missing(void *ctx, const char *path);

#endif /* REMEMBER_BRAIN_NOTE_H */
