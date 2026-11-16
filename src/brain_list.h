#ifndef REMEMBER_BRAIN_LIST_H
#define REMEMBER_BRAIN_LIST_H

#include <stdbool.h>
#include <stddef.h>

/*
 * <data dir>/brains.json (specs/BRAIN-LIST.md). Remember is the only writer.
 * This module reads and writes the file; it never opens SQLite.
 */

/* The most bytes of brains.json that a read takes; a larger file is unreadable
   (decision:2026-11-15-plan26-coder-grill). */
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
    BRAIN_LIST_UNREADABLE,
    /* The other path of this brain_id could not be checked (busy, EACCES, EIO): a copy and a
       move cannot be told apart, so nothing changed (decision:2026-11-16-plan26-review-grill
       G2). The caller fails the command; the next open checks again. */
    BRAIN_LIST_UNDECIDED,
    /* Another process held the lock past the lock timeout: nothing was read or written (G5). */
    BRAIN_LIST_BUSY
} BrainListCode;

typedef struct BrainRow {
    char *path;
    char *brain_id;
    char *added_at;
} BrainRow;

typedef struct BrainList {
    BrainRow *rows;
    size_t count;
    /* brain_list_read: the first member, root then rows in file order, that the reader does
       not take, or NULL. The caller warns it with the C-35 file line (G6 B). */
    char *unknown_field;
} BrainList;

/*
 * What brain_list_record asks of its caller. Both run under the file lock.
 * still: 1 when path is a brain file whose brain_id is brain_id, 0 when it is proven not,
 * -1 when it cannot tell (store_path_has_brain_id).
 * mint: writes a new brain_id into out; 0, or -1 on failure. It runs only when
 * the record is a copy, so a plain open mints nothing.
 */
typedef struct BrainProbe {
    int (*still)(void *ctx, const char *path, const char *brain_id);
    int (*mint)(void *ctx, char *out, size_t cap);
    void *ctx;
} BrainProbe;

/* True when path has no brain: stat proves nothing is there, or not a regular file. */
typedef bool (*BrainMissingFn)(void *ctx, const char *path);

void brain_list_free(BrainList *list);

/* A missing file is an empty list and BRAIN_LIST_UNCHANGED. */
BrainListCode brain_list_read(const char *json_path, BrainList *out);

/*
 * Record the brain just opened at real_path, with the id it holds. now is the
 * added_at of a new row (RFC 3339 UTC). A copy (the same id at another path
 * that still has that brain) takes the id probe->mint wrote into out.minted, and the
 * row for real_path gets it. Returns the change made; BRAIN_LIST_IO when the
 * file could not be written, with the previous file in place; BRAIN_LIST_UNDECIDED
 * when probe->still could not tell, and BRAIN_LIST_BUSY when the lock stayed held past
 * in.lock_timeout_ms, both with nothing written. *out.unknown_field (when
 * out.unknown_field is not NULL) gets the first unknown member of a file that was read,
 * or NULL; the caller frees it.
 */
typedef struct BrainRecordIn {
    const char *real_path;
    const char *brain_id;
    const char *now;
    int lock_timeout_ms; /* the longest wait for the lock; then BRAIN_LIST_BUSY */
    char pad_[4];        /* explicit tail padding (kept -Wpadded-clean) */
} BrainRecordIn;

typedef struct BrainRecordOut {
    char *minted;
    size_t minted_cap;
    char **unknown_field;
} BrainRecordOut;

BrainListCode brain_list_record(const char *json_path, BrainRecordIn in, const BrainProbe *probe,
                                BrainRecordOut out);

/*
 * Drop every row whose path missing() says has no brain. lock_timeout_ms bounds the wait for
 * the lock, as in BrainRecordIn; then BRAIN_LIST_BUSY. removed (may be NULL)
 * receives the dropped rows; free it with brain_list_free. unknown_field (may be NULL)
 * gets the first unknown member of a file that was read, or NULL; the caller frees it.
 */
BrainListCode brain_list_prune(const char *json_path, BrainMissingFn missing, void *ctx,
                               int lock_timeout_ms, BrainList *removed, char **unknown_field);

#ifdef REMEMBER_TEST_HOOKS
/* The n-th allocation in this module from now fails (0 is the next one); -1 turns it off. */
void brain_list_test_fail_alloc_after(int n);
#endif

#endif /* REMEMBER_BRAIN_LIST_H */
