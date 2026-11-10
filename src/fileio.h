#ifndef REMEMBER_FILEIO_H
#define REMEMBER_FILEIO_H

#include <stdbool.h>
#include <stdio.h>

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

/* True when path is a folder (stat follows a symlink). */
bool fileio_is_dir(const char *path);

/* rename(2). */
int fileio_rename(const char *from, const char *to, int *sys_errno);

/* "<dir>/<name>", heap; NULL (ENOMEM) when memory runs out. */
char *fileio_join(const char *dir, const char *name, int *sys_errno);

/* mkdir(2) with mode 0700 (decision:plan22-export-file-mode). */
int fileio_mkdir(const char *path, int *sys_errno);

/*
 * Put the folder tmp at path (D17). When path exists, rename it to
 * "<path>.old-<16 hex>", rename tmp onto path, then delete the old folder.
 * A crash between the renames leaves the old or the new folder intact plus a
 * stray sibling, never a half-written path. When the second rename fails,
 * the old folder goes back to path.
 */
int fileio_replace_dir(const char *tmp, const char *path, int *sys_errno);

/* Delete path and everything under it; symlinks are removed, not followed. */
void fileio_remove_tree(const char *path);

#endif /* REMEMBER_FILEIO_H */
