#ifndef REMEMBER_BRAIN_LIST_H
#define REMEMBER_BRAIN_LIST_H

#include <stdbool.h>
#include <stddef.h>

/*
 * <data dir>/brains.json (specs/BRAIN-LIST.md). Remember is the only writer.
 * This module reads and writes the file; it never opens SQLite.
 */

/* The most bytes of brains.json that a read takes. OPEN: the value is not decided
   (decision:2026-11-12-brains-json-unreadable-warns); see specs/BRAIN-LIST.md. */
enum { BRAIN_LIST_FILE_MAX = 1048576 };

typedef enum BrainListCode {
    BRAIN_LIST_UNCHANGED = 0,
    BRAIN_LIST_ADDED,
    BRAIN_LIST_MOVED,
    BRAIN_LIST_CLONED,
    BRAIN_LIST_UPDATED,
    BRAIN_LIST_PRUNED,
    /* The lock, the temp file, the rename or memory failed. The previous file stays. */
    BRAIN_LIST_IO,
    /* A file is there and cannot be used: not a regular file, over
       BRAIN_LIST_FILE_MAX, unreadable, not the document, or a bad row. It is
       never written over. */
    BRAIN_LIST_UNREADABLE
} BrainListCode;

typedef struct BrainRow {
    char *path;
    char *brain_id;
    char *added_at;
} BrainRow;

typedef struct BrainList {
    BrainRow *rows;
    size_t count;
} BrainList;

/*
 * What brain_list_record asks of its caller. Both run under the file lock.
 * still: true when path is a brain file whose brain_id is brain_id.
 * mint: writes a new brain_id into out; 0, or -1 on failure. It runs only when
 * the record is a copy, so a plain open mints nothing.
 */
typedef struct BrainProbe {
    bool (*still)(void *ctx, const char *path, const char *brain_id);
    int (*mint)(void *ctx, char *out, size_t cap);
    void *ctx;
} BrainProbe;

/* True when path has no brain: nothing there, or not a regular file. */
typedef bool (*BrainMissingFn)(void *ctx, const char *path);

void brain_list_free(BrainList *list);

/* A missing file is an empty list and BRAIN_LIST_UNCHANGED. */
BrainListCode brain_list_read(const char *json_path, BrainList *out);

/*
 * Record the brain just opened at real_path, with the id it holds. now is the
 * added_at of a new row (RFC 3339 UTC). A copy (the same id at another path
 * that still has that brain) takes the id probe->mint wrote into minted, and the
 * row for real_path gets it. Returns the change made; BRAIN_LIST_IO when the
 * file could not be written, with the previous file in place.
 */
typedef struct BrainRecordIn {
    const char *real_path;
    const char *brain_id;
    const char *now;
} BrainRecordIn;

BrainListCode brain_list_record(const char *json_path, BrainRecordIn in, const BrainProbe *probe,
                                char *minted, size_t minted_cap);

/*
 * Drop every row whose path missing() says has no brain. removed (may be NULL)
 * receives the dropped rows; free it with brain_list_free.
 */
BrainListCode brain_list_prune(const char *json_path, BrainMissingFn missing, void *ctx,
                               BrainList *removed);

#ifdef REMEMBER_TEST_HOOKS
/* The n-th allocation in this module from now fails (0 is the next one); -1 turns it off. */
void brain_list_test_fail_alloc_after(int n);
#endif

#endif /* REMEMBER_BRAIN_LIST_H */
