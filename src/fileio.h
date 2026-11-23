#ifndef REMEMBER_FILEIO_H
#define REMEMBER_FILEIO_H

#include <errno.h>
#include <fcntl.h> // IWYU pragma: export (O_NOFOLLOW below)
#include <stdbool.h>
#include <stdio.h>

/* Darwin hides O_NOFOLLOW when _POSIX_C_SOURCE is set, which is how
   gcc -fanalyzer compiles this tree. The value is the one in <sys/fcntl.h>. */
#if defined(__APPLE__) && !defined(O_NOFOLLOW)
#define O_NOFOLLOW 0x00000100
#endif

/*
 * Export file I/O (plan 22): temp names, writes, renames. A failing call
 * returns -1 and leaves the errno that caused it in *sys_errno.
 */

/* Writes one document to out; 0 or -1. */
typedef int (*FileWriteFn)(FILE *out, const void *ctx);

/* "<path>.<tag>-<16 lowercase hex>", heap; NULL when the random bytes or memory fail. */
char *fileio_sibling_name(const char *path, const char *tag, int *sys_errno);

/* Create path (it must not exist), write it with fn, flush it to disk, close it.
   On failure path is removed. */
int fileio_write_new(const char *path, FileWriteFn fn, const void *ctx, int *sys_errno);

/* True when something is at path (lstat: a dangling symlink counts). */
bool fileio_exists(const char *path);

/* True when a and b are one file: the same device and inode (stat follows a
   symlink). False when either is missing. */
bool fileio_same_file(const char *a, const char *b);

/* True when the file at path begins with the bytes of prefix. */
bool fileio_starts_with(const char *path, const char *prefix);

/* True when path is a folder (stat follows a symlink). */
bool fileio_is_dir(const char *path);

/* rename(2). */
int fileio_rename(const char *from, const char *to, int *sys_errno);

/* "<dir>/<name>", heap; NULL (ENOMEM) when memory runs out. */
char *fileio_join(const char *dir, const char *name, int *sys_errno);

/* mkdir(2) with mode 0700 (decision:plan22-export-file-mode). */
int fileio_mkdir(const char *path, int *sys_errno);

/*
 * Put the folder tmp at path (D17). Without force, rename tmp onto path, which
 * fails when a folder that is not empty is there. With force, when path
 * exists, rename it to "<path>.old-<16 hex>", rename tmp onto path, then
 * delete the old folder.
 * A crash between the renames leaves the old or the new folder intact plus a
 * stray sibling, never a half-written path. When the second rename fails,
 * the old folder goes back to path.
 */
int fileio_replace_dir(const char *tmp, const char *path, bool force, int *sys_errno);

/* Delete path and everything under it; symlinks are removed, not followed. */
void fileio_remove_tree(const char *path);

/*
 * Read the whole file at path into a heap buffer (*out_bytes, *out_len).
 * No size cap (plan 23). On failure *out_bytes is NULL and *sys_errno is set.
 */
int fileio_read_all(const char *path, char **out_bytes, size_t *out_len, int *sys_errno);

/*
 * Read the file at path for a file that another program may have replaced
 * (standard:c): opened with O_NONBLOCK, so a FIFO cannot hang the read; only a
 * regular file; at most max_bytes. A symlink to a regular file is followed. On
 * failure *out_bytes is NULL and *sys_errno is ENOENT (nothing there), EINVAL
 * (not a regular file), EFBIG (more than max_bytes), or the errno that failed.
 * The buffer holds one more byte, a NUL, after *out_len.
 */
int fileio_read_regular(const char *path, size_t max_bytes, char **out_bytes, size_t *out_len,
                        int *sys_errno);

/* fileio_read_regular for a file only its owner may write (decision:2026-11-23-plan33-trust-json-
   owner-mode): also fails, with EPERM, when the file is not owned by the effective user or group
   or others may write it. */
int fileio_read_private(const char *path, size_t max_bytes, char **out_bytes, size_t *out_len,
                        int *sys_errno);

/* True when a failed stat's errno proves nothing is at the path (ENOENT, ENOTDIR, ELOOP,
   ENAMETOOLONG). Any other errno (EACCES, EIO) says nothing about what is there. Inline, so
   the store test binary, which does not link fileio.c, has the one definition too. */
static inline bool fileio_errno_means_absent(int err)
{
    return (err == ENOENT || err == ENOTDIR || err == ELOOP || err == ENAMETOOLONG) != 0;
}

/* mkdir -p for every folder above the last component of file_path, mode 0700.
   file_path is changed while it runs and is the same when it returns. */
int fileio_mkdir_parents(char *file_path, int *sys_errno);

/* lock_path's failures (fileio_lock_open). */
enum { FILEIO_LOCK_IO = -1, FILEIO_LOCK_BUSY = -2 };

/*
 * A POSIX record lock on the whole file at lock_path (made, mode 0600, when missing), advisory,
 * dropped by the OS when the holder dies. The file is opened without following a symlink and
 * must be a regular file. The wait is bounded: it retries every 10 ms until timeout_ms has gone
 * by, then FILEIO_LOCK_BUSY (never an unbounded wait). The folder above lock_path must exist.
 * The descriptor to close when done (closing releases the lock), or FILEIO_LOCK_IO.
 * ponytail: the wait counts its sleeps, so it can run a little past timeout_ms; a clock_gettime
 * deadline if that ever matters.
 */
int fileio_lock_open(const char *lock_path, int timeout_ms);

#endif /* REMEMBER_FILEIO_H */
