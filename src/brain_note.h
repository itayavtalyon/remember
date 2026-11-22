#ifndef REMEMBER_BRAIN_NOTE_H
#define REMEMBER_BRAIN_NOTE_H

#include "store.h"

#include <stdbool.h>
#include <stddef.h>

/* <data dir>/brains.json (decision:2026-11-11-brains-json-data-dir). 0, or -1
   when the data dir will not resolve or the path does not fit cap. why (may be NULL) gets the
   resolver's own reason, such as "HOME is not set", when the data dir will not resolve. */
int brain_json_path(char *out, size_t cap, char *why, size_t why_cap);

/* A buffer for that reason: the resolver's reasons are short. */
enum { BRAIN_WHY_CAP = 64 };

/*
 * decision:g2-05 and decision:g2-08 for one open. A read-write open records the
 * brain in brains.json. A brain that is a copy of a listed one (same id, the
 * other path still has it) takes a new id, and the C-38 warning is written. A
 * read-only open does nothing. A file that cannot be written leaves the previous
 * one, prints nothing, and does not change the open. A file that cannot be read is left
 * alone, records nothing, and prints the brains_json_unreadable warning when warn_unreadable
 * (the open does; a later record in the same command passes false). 0, or -1 with the error
 * written: the lock stayed held past the open's busy timeout (G5), a copy could not
 * be told from a move (G2), or a copy's new id could not be stored (G4)
 * (decision:2026-11-16-plan26-review-grill). Each changes nothing.
 */
int brain_note_open(Store *store, bool warn_unreadable);

/*
 * Plan 23: 1 when brains.json lists a brain with brain_id that still has it (an import file
 * from it would make a copy), 0 when none does or the list cannot be read, -1 with the error
 * written when a listed brain cannot be checked (busy, EACCES, EIO).
 */
int brain_note_id_is_taken(const Store *store, const char *brain_id);

/* The error of a brains.json lock held past the busy timeout (G5): busy. */
void brain_note_busy_error(void);

/* The errors of the brains.json commands (decision:open-2026-11-15-brains-json-unreadable-copy):
   the data folder will not resolve (why is the resolver's reason), brains.json cannot be read,
   and brains.json cannot be written. file is the path of brains.json. Each writes the error. */
void brain_note_no_data_dir(const char *why);
void brain_note_unreadable_error(const char *file);
void brain_note_update_error(const char *file);

/* BrainMissingFn: true when stat proves nothing is there (ENOENT, ENOTDIR, ELOOP,
   ENAMETOOLONG) or the path is not a regular file. Any other stat failure (EACCES, EIO) is
   false. stat follows a symlink and never opens the path, so a FIFO cannot block. */
bool brain_path_is_missing(void *ctx, const char *path);

#endif /* REMEMBER_BRAIN_NOTE_H */
