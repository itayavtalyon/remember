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
 * one, prints nothing, and does not change the open. 0, or -1 with the error
 * written: the lock stayed held past the open's busy timeout (G5), a copy could not
 * be told from a move (G2), or a copy's new id could not be stored (G4)
 * (decision:2026-11-16-plan26-review-grill). Each changes nothing.
 */
int brain_note_open(Store *store);

/* The error of a brains.json lock held past the busy timeout (G5): busy. */
void brain_note_busy_error(void);

/* BrainMissingFn: true when stat proves nothing is there (ENOENT, ENOTDIR, ELOOP,
   ENAMETOOLONG) or the path is not a regular file. Any other stat failure (EACCES, EIO) is
   false. stat follows a symlink and never opens the path, so a FIFO cannot block. */
bool brain_path_is_missing(void *ctx, const char *path);

#endif /* REMEMBER_BRAIN_NOTE_H */
