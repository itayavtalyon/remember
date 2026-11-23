#include "plugin_trust.h"

#include "fileio.h"
#include "jsonread.h"
#include "normalize.h"
#include "output.h"
#include "plugin_manifest.h"
#include "plugin_settings.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h> // IWYU pragma: keep (ssize_t; IWYU 0.23 wants it, 0.21 calls it unused)
#include <unistd.h>

/* Last: under REMEMBER_TEST_HOOKS every malloc below goes through the shared fault hook
   (decision:plan18b-shared-alloc-hook). */
#ifdef REMEMBER_TEST_HOOKS
#include "test_alloc.h" // IWYU pragma: keep
#endif

enum {
    PATH_CAP = 4096, /* a joined path; a longer one cannot be hashed */
    CHUNK = 16384,
    DECIMAL_BASE = 10,
    WALK_GROW = 16,
    NONE_AT = -1,
    LOCK_OOM = -3 /* lock_trust: beside FILEIO_LOCK_IO and FILEIO_LOCK_BUSY */
};

static const char k_file_name[] = "trust.json";
static const char k_lock_suffix[] = ".lock";
static const char k_tmp_suffix[] = ".tmp";
static const char k_hook_cmd[] = "/usr/bin/env";

/* H-9.1: the interpreters whose realpath and SHA-256 a record also holds. */
static const char *const k_interpreters[] = {"sh",   "bash", "zsh",  "python", "python3",
                                             "node", "ruby", "perl", "deno",   "bun"};
static const char *const k_shells[] = {"sh", "bash", "zsh"};

/* ---- one hashed file ----------------------------------------------------------------------- */

typedef enum HashRc {
    H_OK = 0,
    H_ABSENT,      /* nothing is there */
    H_NOT_REGULAR, /* something is there and it is not a regular file */
    H_BAD,         /* it cannot be hashed: unreadable, over the cap, a failed read */
    H_OOM
} HashRc;

typedef struct FileHash {
    long long size;
    long long mtime;
    char hex[PLUGIN_TRUST_HEX + 1];
    bool exec;
    // NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
    char pad_[6]; /* explicit tail padding (kept -Wpadded-clean) */
} FileHash;

/* H_ABSENT when err says nothing is at the path, H_BAD when it says nothing about it. */
static HashRc absent_rc(int err)
{
    if (fileio_errno_means_absent(err)) {
        return H_ABSENT;
    }
    return H_BAD;
}

/* Hash what fd holds. fd stays open; it must be a regular file of at most the cap. */
static HashRc hash_fd(int fd, FileHash *out)
{
    struct stat st;
    unsigned char chunk[CHUNK];
    long long total = 0;
    HashRc rc = H_OK;
    void *mem = NULL;
    HashStream *stream = NULL;

    if (fstat(fd, &st) != 0) {
        return H_BAD;
    }
    if (!S_ISREG(st.st_mode)) {
        return H_NOT_REGULAR;
    }
    if (st.st_size > (off_t)PLUGIN_TRUST_HASHED_FILE_MAX) {
        return H_BAD;
    }
    mem = malloc(hash_stream_size());
    stream = hash_stream_init(mem);
    if (stream == NULL) {
        return H_OOM;
    }
    for (;;) {
        ssize_t got = read(fd, chunk, sizeof(chunk));

        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got < 0 ||
            (got > 0 && total + (long long)got > (long long)PLUGIN_TRUST_HASHED_FILE_MAX)) {
            rc = H_BAD;
            break;
        }
        if (got == 0) {
            break;
        }
        total += (long long)got;
        hash_stream_update(stream, chunk, (size_t)got);
    }
    if (rc == H_OK) {
        hash_stream_hex(stream, out->hex);
        out->size = total;
        out->mtime = (long long)st.st_mtime;
        out->exec = (st.st_mode & S_IXUSR) != 0;
    }
    free(mem);
    return rc;
}

/* Hash the regular file at path, never following a symlink at the last name. */
static HashRc hash_path(const char *path, FileHash *out)
{
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    HashRc rc = H_OK;

    if (fd < 0) {
        return absent_rc(errno);
    }
    rc = hash_fd(fd, out);
    (void)close(fd);
    return rc;
}

/* ---- small text helpers -------------------------------------------------------------------- */

static char *dup_text(const char *text)
{
    size_t n = strlen(text) + 1U;
    char *copy = (char *)malloc(n);

    if (copy != NULL) {
        memcpy(copy, text, n);
    }
    return copy;
}

/* "<dir>/<name>", heap, NULL when memory runs out or the result is not a path. */
static char *join_path(const char *dir, const char *name)
{
    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);
    char *out = NULL;

    if (dir_len + name_len + 2U > (size_t)PATH_CAP) {
        return NULL;
    }
    out = (char *)malloc(dir_len + name_len + 2U);
    if (out != NULL) {
        /* NUL-terminated by the copy of name below. */
        // NOLINTNEXTLINE(bugprone-not-null-terminated-result)
        memcpy(out, dir, dir_len);
        out[dir_len] = '/';
        memcpy(out + dir_len + 1U, name, name_len + 1U);
    }
    return out;
}

static const char *base_of(const char *path)
{
    const char *slash = strrchr(path, '/');

    return slash != NULL ? slash + 1 : path;
}

static bool in_list(const char *name, const char *const *list, size_t count)
{
    size_t i = 0;

    for (i = 0; i < count; i++) {
        if (strcmp(name, list[i]) == 0) {
            return true;
        }
    }
    return false;
}

#define IN_LIST(name, list) in_list((name), (list), sizeof(list) / sizeof((list)[0]))

/* ---- the plugin folder ---------------------------------------------------------------------- */

typedef struct WalkLine {
    char *rel;
    char hex[PLUGIN_TRUST_HEX + 1];
    bool exec;
    // NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
    char pad_[6]; /* explicit tail padding (kept -Wpadded-clean) */
} WalkLine;

typedef struct Walk {
    WalkLine *lines;
    size_t count;
    size_t cap;
    size_t dirs;
} Walk;

static void walk_free(Walk *walk)
{
    size_t i = 0;

    for (i = 0; i < walk->count; i++) {
        free(walk->lines[i].rel);
    }
    free(walk->lines);
}

static HashRc walk_add(Walk *walk, char *rel, const FileHash *hash)
{
    WalkLine *line = NULL;

    if (walk->count >= (size_t)PLUGIN_TRUST_FOLDER_FILES_MAX) {
        free(rel);
        return H_BAD;
    }
    if (walk->count == walk->cap) {
        size_t next = walk->cap == 0U ? (size_t)WALK_GROW : walk->cap * 2U;
        WalkLine *grown = (WalkLine *)malloc(next * sizeof(*grown));

        if (grown == NULL) {
            free(rel);
            return H_OOM;
        }
        if (walk->count > 0U) {
            memcpy(grown, walk->lines, walk->count * sizeof(*grown));
        }
        free(walk->lines);
        walk->lines = grown;
        walk->cap = next;
    }
    line = &walk->lines[walk->count];
    line->rel = rel;
    line->exec = hash->exec;
    memcpy(line->hex, hash->hex, sizeof(line->hex));
    walk->count++;
    return H_OK;
}

/* One file of the folder: opened under dirfd without following a link, hashed. */
static HashRc walk_file(int dirfd, const char *name, char *rel, Walk *walk)
{
    FileHash hash;
    // NOLINTNEXTLINE(clang-analyzer-unix.StdCLibraryFunctions) -- dirfd of an open DIR is not -1
    int fd = openat(dirfd, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    HashRc rc = H_BAD;

    if (fd < 0) {
        free(rel);
        return H_BAD;
    }
    rc = hash_fd(fd, &hash);
    (void)close(fd);
    if (rc != H_OK) {
        free(rel);
        return rc == H_OOM ? H_OOM : H_BAD;
    }
    return walk_add(walk, rel, &hash);
}

static HashRc walk_dir(int fd, const char *rel, size_t depth, Walk *walk);

// NOLINTNEXTLINE(misc-no-recursion) -- depth is capped at PLUGIN_TRUST_DEPTH_MAX
static HashRc walk_entry(int dirfd, const char *name, const char *rel, size_t depth, Walk *walk)
{
    struct stat st;
    char *child = rel[0] == '\0' ? dup_text(name) : join_path(rel, name);
    int sub = -1;

    if (child == NULL) {
        return H_OOM;
    }
    if (fstatat(dirfd, name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
        free(child);
        return H_BAD;
    }
    if (S_ISREG(st.st_mode)) {
        return walk_file(dirfd, name, child, walk);
    }
    if (!S_ISDIR(st.st_mode) || depth + 1U > (size_t)PLUGIN_TRUST_DEPTH_MAX ||
        ++walk->dirs > (size_t)PLUGIN_TRUST_FOLDER_DIRS_MAX) {
        free(child);
        return H_BAD;
    }
    // NOLINTNEXTLINE(clang-analyzer-unix.StdCLibraryFunctions) -- dirfd of an open DIR is not -1
    sub = openat(dirfd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (sub >= 0) {
        HashRc rc = walk_dir(sub, child, depth + 1U, walk);

        free(child);
        return rc;
    }
    free(child);
    return H_BAD;
}

/* Every entry under fd (taken over: closed here) is a regular file or a folder; a link, a FIFO, a
   device or a socket makes the folder unhashable (decision:2026-11-22-plan33-folder-hash). */
// NOLINTNEXTLINE(misc-no-recursion) -- depth is capped at PLUGIN_TRUST_DEPTH_MAX
static HashRc walk_dir(int fd, const char *rel, size_t depth, Walk *walk)
{
    DIR *dir = fdopendir(fd);
    const struct dirent *ent = NULL;
    HashRc rc = H_OK;

    if (dir == NULL) {
        (void)close(fd);
        return H_BAD;
    }
    errno = 0;
    // NOLINTNEXTLINE(concurrency-mt-unsafe) -- single-threaded CLI; readdir_r is deprecated
    while (rc == H_OK && (ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
            rc = walk_entry(dirfd(dir), ent->d_name, rel, depth, walk);
        }
        errno = 0;
    }
    /* A listing that stopped on an error is a partial folder. */
    if (rc == H_OK && errno != 0) {
        rc = H_BAD;
    }
    (void)closedir(dir);
    return rc;
}

static int cmp_lines(const void *left, const void *right)
{
    return strcmp(((const WalkLine *)left)->rel, ((const WalkLine *)right)->rel);
}

/* folder_sha256: one record per regular file in the byte order of its relative path: 'F', the
   executable bit as 'x' or '-', the 64 hex digits of its content hash, the path, and a NUL. A NUL
   cannot be in a name, so no two folders give one list. */
static HashRc hash_folder(const char *folder, char out_hex[PLUGIN_TRUST_HEX + 1])
{
    Walk walk = {.lines = NULL, .count = 0U, .cap = 0U, .dirs = 0U};
    int fd = open(folder, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    void *mem = NULL;
    HashStream *stream = NULL;
    HashRc rc = H_BAD;
    size_t i = 0;

    if (fd < 0) {
        return H_BAD;
    }
    rc = walk_dir(fd, "", 0U, &walk);
    if (rc != H_OK) {
        walk_free(&walk);
        return rc;
    }
    mem = malloc(hash_stream_size());
    stream = hash_stream_init(mem);
    if (stream == NULL) {
        walk_free(&walk);
        return H_OOM;
    }
    if (walk.count > 1U) {
        qsort(walk.lines, walk.count, sizeof(*walk.lines), cmp_lines);
    }
    for (i = 0; i < walk.count; i++) {
        const WalkLine *line = &walk.lines[i];
        char head[2] = {'F', '-'};

        if (line->exec) {
            head[1] = 'x';
        }
        hash_stream_update(stream, head, sizeof(head));
        hash_stream_update(stream, line->hex, PLUGIN_TRUST_HEX);
        hash_stream_update(stream, line->rel, strlen(line->rel) + 1U);
    }
    hash_stream_hex(stream, out_hex);
    free(mem);
    walk_free(&walk);
    return H_OK;
}

/* ---- the record ----------------------------------------------------------------------------- */

typedef struct FileRec {
    char *path;
    long long size;
    long long mtime;
    char sha[PLUGIN_TRUST_HEX + 1];
    // NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
    char pad_[7]; /* explicit tail padding (kept -Wpadded-clean) */
} FileRec;

typedef struct TrustRecord {
    char *name;
    char *path;
    char *trusted_at;
    char **needs;
    char *interp_path;
    size_t nneeds;
    size_t nfiles;
    PluginTrustKind kind;
    bool dev;
    bool oom; /* a reader ran out of memory while taking this entry */
    // NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
    char pad_[2]; /* explicit padding (kept -Wpadded-clean) */
    FileRec files[PLUGIN_TRUST_FILES_MAX];
    char folder_sha[PLUGIN_TRUST_HEX + 1];
    char manifest_sha[PLUGIN_TRUST_HEX + 1];
    char interp_sha[PLUGIN_TRUST_HEX + 1];
    // NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
    char tail_[5]; /* explicit tail padding */
} TrustRecord;

static void record_clear(TrustRecord *rec)
{
    size_t i = 0;

    for (i = 0; i < rec->nfiles; i++) {
        free(rec->files[i].path);
    }
    for (i = 0; i < rec->nneeds; i++) {
        free(rec->needs[i]);
    }
    free((void *)rec->needs);
    free(rec->name);
    free(rec->path);
    free(rec->trusted_at);
    free(rec->interp_path);
    memset(rec, 0, sizeof(*rec));
}

/* The interpreter of a command: the argv item that names it (0 for command[0] itself, 1 for
   /usr/bin/env followed by one), or NONE_AT. */
static int interpreter_at(const PluginTrustCommand *cmd)
{
    if (strcmp(cmd->argv[0], k_hook_cmd) == 0) {
        return (cmd->argc > 1U && IN_LIST(cmd->argv[1], k_interpreters)) ? 1 : NONE_AT;
    }
    return IN_LIST(base_of(cmd->argv[0]), k_interpreters) ? 0 : NONE_AT;
}

/* The text of "<shell> -c <inline>", or NULL. */
static const char *inline_text(const PluginTrustCommand *cmd)
{
    int at = interpreter_at(cmd);
    size_t shell_at = at == 1 ? 1U : 0U;
    const char *shell = NULL;

    if (at == NONE_AT || cmd->argc <= shell_at + 2U) {
        return NULL;
    }
    shell = at == 1 ? cmd->argv[1] : base_of(cmd->argv[0]);
    if (!IN_LIST(shell, k_shells) || strcmp(cmd->argv[shell_at + 1U], "-c") != 0) {
        return NULL;
    }
    return cmd->argv[shell_at + 2U];
}

/* manifest_sha256: the bytes of <folder>/plugin.json, then for each command that runs
   "<shell> -c <inline>" a NUL, "sh-c", a NUL, the inline text and a NUL. */
static HashRc hash_manifest(const PluginTrustSubject *subject, char out_hex[PLUGIN_TRUST_HEX + 1])
{
    static const char k_tag[] = "\0sh-c";
    char *path = join_path(subject->folder, "plugin.json");
    char *bytes = NULL;
    size_t len = 0;
    int sys_errno = 0;
    void *mem = NULL;
    HashStream *stream = NULL;
    size_t i = 0;

    if (path == NULL) {
        return H_OOM;
    }
    if (fileio_read_regular(path, (size_t)PLUGIN_JSON_MAX, &bytes, &len, &sys_errno) != 0) {
        free(path);
        return sys_errno == ENOMEM ? H_OOM : H_BAD;
    }
    free(path);
    mem = malloc(hash_stream_size());
    stream = hash_stream_init(mem);
    if (stream == NULL) {
        free(bytes);
        return H_OOM;
    }
    hash_stream_update(stream, bytes, len);
    free(bytes);
    for (i = 0; i < subject->ncommands; i++) {
        const char *text = inline_text(&subject->commands[i]);

        if (text != NULL) {
            hash_stream_update(stream, k_tag, sizeof(k_tag));
            hash_stream_update(stream, text, strlen(text) + 1U);
        }
    }
    hash_stream_hex(stream, out_hex);
    free(mem);
    return H_OK;
}

/* The realpath of path, heap. H_OK, H_ABSENT (nothing there), H_BAD or H_OOM. The buffer is a
   PATH_CAP one: a path that does not fit is not a path remember hashes. */
static HashRc real_of(const char *path, char **out)
{
    char buf[PATH_CAP];

    *out = NULL;
    if (realpath(path, buf) == NULL) {
        return absent_rc(errno);
    }
    *out = dup_text(buf);
    return *out != NULL ? H_OK : H_OOM;
}

/* A record's paths are JSON text, so each must be UTF-8 (jsonread refuses the rest). */
static bool text_is_json_safe(const char *text)
{
    return utf8_is_valid(text, strlen(text));
}

/* The realpath of item (a path relative to folder, or absolute) when a regular file is there;
 *rc says why not: H_ABSENT (text, not a file), H_NOT_REGULAR (a folder, a device), H_BAD. */
static char *resolve_file(const char *folder, const char *item, FileHash *hash, HashRc *rc)
{
    char *joined = item[0] == '/' ? dup_text(item) : join_path(folder, item);
    char *real = NULL;
    int fd = -1;

    *rc = H_OOM;
    if (joined == NULL) {
        return NULL;
    }
    *rc = real_of(joined, &real);
    free(joined);
    if (real == NULL) {
        return NULL;
    }
    fd = open(real, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        *rc = absent_rc(errno);
        free(real);
        return NULL;
    }
    *rc = hash_fd(fd, hash);
    (void)close(fd);
    if (*rc != H_OK || !text_is_json_safe(real)) {
        *rc = *rc == H_OK ? H_BAD : *rc;
        free(real);
        return NULL;
    }
    return real;
}

typedef struct Inside {
    const char *real_folder;
    size_t len;
} Inside;

/* True when real is the folder or under it: the folder hash covers it. */
static bool is_inside(const Inside *in, const char *real)
{
    if (strncmp(real, in->real_folder, in->len) != 0) {
        return false;
    }
    if (real[in->len] == '/') {
        return true;
    }
    return real[in->len] == '\0';
}

static bool have_path(const TrustRecord *rec, const char *real)
{
    size_t i = 0;

    for (i = 0; i < rec->nfiles; i++) {
        if (strcmp(rec->files[i].path, real) == 0) {
            return true;
        }
    }
    return false;
}

/* One argv item: a regular file outside the folder is a record row (G-29, at most 8). Text, a
   folder, a device and a file inside the folder are not. */
static HashRc add_argv_file(TrustRecord *rec, const Inside *in, const char *folder,
                            const char *item)
{
    FileHash hash;
    HashRc rc = H_OK;
    char *real = item[0] == '\0' ? NULL : resolve_file(folder, item, &hash, &rc);

    if (real == NULL) {
        return (item[0] == '\0' || rc == H_ABSENT || rc == H_NOT_REGULAR) ? H_OK : rc;
    }
    if (is_inside(in, real) || have_path(rec, real)) {
        free(real);
        return H_OK;
    }
    if (rec->nfiles >= (size_t)PLUGIN_TRUST_FILES_MAX) {
        free(real);
        return H_BAD;
    }
    rec->files[rec->nfiles] = (FileRec){.path = real, .size = hash.size, .mtime = hash.mtime};
    memcpy(rec->files[rec->nfiles].sha, hash.hex, sizeof(hash.hex));
    rec->nfiles++;
    return H_OK;
}

/* The first executable regular file called name in a ':' separated search path; empty entries
   are skipped. NULL with *rc H_ABSENT when there is none, or H_OOM. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the PATH, then the name
static char *search_in_path(const char *search_path, const char *name, HashRc *rc)
{
    const char *start = search_path;
    struct stat st;

    *rc = H_ABSENT;
    while (start != NULL && *start != '\0') {
        const char *end = strchr(start, ':');
        size_t count = end != NULL ? (size_t)(end - start) : strlen(start);
        char *dir = count > 0U ? (char *)malloc(count + 1U) : NULL;

        if (count > 0U && dir == NULL) {
            *rc = H_OOM;
            return NULL;
        }
        if (dir != NULL) {
            char *joined = NULL;
            char *real = NULL;

            memcpy(dir, start, count);
            dir[count] = '\0';
            joined = join_path(dir, name);
            free(dir);
            if (joined == NULL) {
                *rc = H_OOM;
                return NULL;
            }
            *rc = real_of(joined, &real);
            free(joined);
            if (*rc == H_OOM) {
                return NULL;
            }
            /* env skips a folder, a device and a file it cannot run. */
            if (real != NULL && stat(real, &st) == 0 && S_ISREG(st.st_mode) &&
                access(real, X_OK) == 0) {
                return real;
            }
            free(real);
        }
        start = end != NULL ? end + 1 : NULL;
    }
    return NULL;
}

/* Hash the file at real, a realpath search_in_path found. */
static HashRc hash_real(const char *real, FileHash *hash)
{
    HashRc rc = hash_path(real, hash);

    return rc == H_OK || rc == H_OOM ? rc : H_BAD;
}

/* The interpreter of one command, when it has one and the file is there: its realpath and hash.
   A second interpreter is not the first one: unhashable (decision:2026-11-22-plan33-many-entries).
   A file the record already holds as an argv file is not hashed again. */
static HashRc add_interpreter(TrustRecord *rec, const PluginTrustSubject *subject,
                              const PluginTrustCommand *cmd)
{
    int at = interpreter_at(cmd);
    char *real = NULL;
    FileHash hash;
    HashRc rc = H_OK;

    if (at == NONE_AT) {
        return H_OK;
    }
    if (at == 0) {
        real = resolve_file(subject->folder, cmd->argv[0], &hash, &rc);
        if (real == NULL) {
            return (rc == H_ABSENT || rc == H_NOT_REGULAR) ? H_OK : rc;
        }
    } else {
        real = search_in_path(subject->search_path, cmd->argv[1], &rc);
        if (real == NULL) {
            return rc == H_OOM ? H_OOM : H_OK;
        }
        rc = H_BAD;
        if (text_is_json_safe(real)) {
            rc = hash_real(real, &hash);
        }
        if (rc != H_OK) {
            free(real);
            return rc;
        }
    }
    if (rec->interp_path != NULL) {
        bool same = strcmp(rec->interp_path, real) == 0;

        free(real);
        if (!same) {
            return H_BAD;
        }
        return H_OK;
    }
    memcpy(rec->interp_sha, hash.hex, sizeof(rec->interp_sha));
    rec->interp_path = real;
    return H_OK;
}

static PluginTrustStatus status_of(HashRc rc)
{
    if (rc == H_OOM) {
        return PLUGIN_TRUST_OOM;
    }
    return rc == H_OK ? PLUGIN_TRUST_OK : PLUGIN_TRUST_UNHASHABLE;
}

static bool subject_is_usable(const PluginTrustSubject *subject)
{
    if (subject->name == NULL || subject->name[0] == '\0' || subject->folder == NULL ||
        subject->ncommands > (size_t)PLUGIN_TRUST_COMMANDS_MAX ||
        (subject->ncommands > 0U && subject->commands == NULL) ||
        (subject->nneeds > 0U && subject->needs == NULL)) {
        return false;
    }
    if (subject->kind == PLUGIN_TRUST_KIND_HOOK && subject->ncommands != 1U) {
        return false;
    }
    for (size_t i = 0; i < subject->ncommands; i++) {
        const PluginTrustCommand *cmd = &subject->commands[i];

        if (cmd->argc == 0U || cmd->argc > (size_t)PLUGIN_ARGS_MAX || cmd->argv == NULL) {
            return false;
        }
        for (size_t j = 0; j < cmd->argc; j++) {
            if (cmd->argv[j] == NULL) {
                return false;
            }
        }
    }
    return true;
}

/* The strings of the record come from the caller and end in a JSON file: each must be UTF-8. */
static HashRc take_strings(TrustRecord *rec, const PluginTrustSubject *subject)
{
    const char *path =
        subject->kind == PLUGIN_TRUST_KIND_HOOK ? subject->commands[0].argv[0] : subject->folder;
    size_t i = 0;

    if (!text_is_json_safe(subject->name) || !text_is_json_safe(path)) {
        return H_BAD;
    }
    rec->name = dup_text(subject->name);
    rec->path = dup_text(path);
    rec->needs =
        subject->nneeds > 0U ? (char **)malloc(subject->nneeds * sizeof(*rec->needs)) : NULL;
    if (rec->name == NULL || rec->path == NULL || (subject->nneeds > 0U && rec->needs == NULL)) {
        return H_OOM;
    }
    for (i = 0; i < subject->nneeds; i++) {
        if (subject->needs[i] == NULL || !text_is_json_safe(subject->needs[i])) {
            return H_BAD;
        }
        rec->needs[i] = dup_text(subject->needs[i]);
        if (rec->needs[i] == NULL) {
            return H_OOM;
        }
        rec->nneeds++;
    }
    return H_OK;
}

/* The record of the code as it is now (no trusted_at). The status is OK, UNHASHABLE or OOM. */
static PluginTrustStatus build_record(const PluginTrustSubject *subject, TrustRecord *rec)
{
    char *real_folder = NULL;
    Inside inside = {.real_folder = NULL, .len = 0U};
    HashRc rc = H_BAD;

    memset(rec, 0, sizeof(*rec));
    if (!subject_is_usable(subject)) {
        return PLUGIN_TRUST_UNHASHABLE;
    }
    rec->kind = subject->kind;
    rec->dev = subject->dev;
    rc = real_of(subject->folder, &real_folder);
    if (real_folder == NULL) {
        return status_of(rc == H_ABSENT ? H_BAD : rc);
    }
    inside.real_folder = real_folder;
    inside.len = strlen(real_folder);
    rc = take_strings(rec, subject);
    if (rc == H_OK) {
        rc = hash_folder(subject->folder, rec->folder_sha);
    }
    if (rc == H_OK) {
        rc = hash_manifest(subject, rec->manifest_sha);
    }
    for (size_t i = 0; rc == H_OK && i < subject->ncommands; i++) {
        const PluginTrustCommand *cmd = &subject->commands[i];

        for (size_t j = 0; rc == H_OK && j < cmd->argc; j++) {
            rc = add_argv_file(rec, &inside, subject->folder, cmd->argv[j]);
        }
        if (rc == H_OK) {
            rc = add_interpreter(rec, subject, cmd);
        }
    }
    free(real_folder);
    if (rc != H_OK) {
        record_clear(rec);
    }
    return status_of(rc);
}

/* ---- trust.json: read ------------------------------------------------------------------------ */

typedef struct TrustDoc {
    TrustRecord *entries;
    size_t count;
    size_t cap;
} TrustDoc;

static void doc_clear(TrustDoc *doc)
{
    size_t i = 0;

    for (i = 0; i < doc->count; i++) {
        record_clear(&doc->entries[i]);
    }
    free(doc->entries);
    memset(doc, 0, sizeof(*doc));
}

/* The one member named key. A key given twice is NULL, like a missing one: first-wins here and
   last-wins in most other readers would read two different files. */
static const JsonValue *member(const JsonValue *obj, const char *key)
{
    const JsonValue *found = NULL;
    size_t i = 0;

    for (i = 0; i < obj->u.object.n; i++) {
        if (strcmp(obj->u.object.members[i].key, key) == 0) {
            if (found != NULL) {
                return NULL;
            }
            found = obj->u.object.members[i].value;
        }
    }
    return found;
}

/* obj is an object with exactly these members, each once and no other: the file is the decided
   document and nothing else (decision:2026-11-22-plan33-trust-json-unreadable). */
static bool has_exactly(const JsonValue *obj, const char *const *keys, size_t count)
{
    size_t i = 0;

    if (obj == NULL || obj->type != JSON_OBJECT || obj->u.object.n != count) {
        return false;
    }
    for (i = 0; i < count; i++) {
        if (member(obj, keys[i]) == NULL) {
            return false;
        }
    }
    return true;
}

static const char *string_of(const JsonValue *obj, const char *key)
{
    const JsonValue *value = member(obj, key);

    return (value != NULL && value->type == JSON_STRING) ? value->u.string.bytes : NULL;
}

static bool is_hex_digest(const char *text)
{
    size_t i = 0;

    if (text == NULL || strlen(text) != (size_t)PLUGIN_TRUST_HEX) {
        return false;
    }
    for (i = 0; i < (size_t)PLUGIN_TRUST_HEX; i++) {
        if (!((text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f'))) {
            return false;
        }
    }
    return true;
}

static const char *hex_of(const JsonValue *obj, const char *key)
{
    const char *text = string_of(obj, key);

    if (!is_hex_digest(text)) {
        return NULL;
    }
    return text;
}

/* A whole number written as digits (and a leading minus) that fits a long long. */
static bool int_of(const JsonValue *obj, const char *key, long long *out)
{
    const JsonValue *value = member(obj, key);
    const char *text = NULL;
    char *end = NULL;
    size_t i = 0;

    if (value == NULL || value->type != JSON_NUMBER) {
        return false;
    }
    text = value->u.number.bytes;
    i = text[0] == '-' ? 1U : 0U;
    if (text[i] == '\0') {
        return false;
    }
    for (; text[i] != '\0'; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
    }
    errno = 0;
    *out = strtoll(text, &end, DECIMAL_BASE);
    return errno == 0 && *end == '\0';
}

/* Copy text into *slot. false when there is no text, or (rec->oom) when memory ran out. */
static bool take_text(TrustRecord *rec, char **slot, const char *text)
{
    *slot = text != NULL ? dup_text(text) : NULL;
    if (text != NULL && *slot == NULL) {
        rec->oom = true;
    }
    return *slot != NULL;
}

static bool take_files(TrustRecord *rec, const JsonValue *files)
{
    static const char *const k_keys[] = {"path", "sha256", "size", "mtime"};
    size_t i = 0;

    if (files == NULL || files->type != JSON_ARRAY ||
        files->u.array.n > (size_t)PLUGIN_TRUST_FILES_MAX) {
        return false;
    }
    for (i = 0; i < files->u.array.n; i++) {
        const JsonValue *row = files->u.array.items[i];
        FileRec *slot = &rec->files[i];
        const char *sha = NULL;

        if (!has_exactly(row, k_keys, 4U)) {
            return false;
        }
        sha = hex_of(row, "sha256");
        if (sha == NULL || !int_of(row, "size", &slot->size) || slot->size < 0 ||
            !int_of(row, "mtime", &slot->mtime) || string_of(row, "path") == NULL ||
            string_of(row, "path")[0] == '\0') {
            return false;
        }
        memcpy(slot->sha, sha, sizeof(slot->sha));
        if (!take_text(rec, &slot->path, string_of(row, "path"))) {
            return false;
        }
        rec->nfiles++;
    }
    return true;
}

static bool take_interpreter(TrustRecord *rec, const JsonValue *interp)
{
    static const char *const k_keys[] = {"path", "sha256"};
    const char *sha = NULL;

    if (interp != NULL && interp->type == JSON_NULL) {
        return true;
    }
    if (!has_exactly(interp, k_keys, 2U)) {
        return false;
    }
    sha = hex_of(interp, "sha256");
    if (sha == NULL || string_of(interp, "path") == NULL || string_of(interp, "path")[0] == '\0') {
        return false;
    }
    memcpy(rec->interp_sha, sha, sizeof(rec->interp_sha));
    return take_text(rec, &rec->interp_path, string_of(interp, "path"));
}

static bool take_needs(TrustRecord *rec, const JsonValue *needs)
{
    size_t i = 0;

    if (needs == NULL || needs->type != JSON_ARRAY ||
        needs->u.array.n > (size_t)PLUGIN_TRUST_NEEDS_MAX) {
        return false;
    }
    if (needs->u.array.n == 0U) {
        return true;
    }
    rec->needs = (char **)malloc(needs->u.array.n * sizeof(*rec->needs));
    if (rec->needs == NULL) {
        rec->oom = true;
        return false;
    }
    for (i = 0; i < needs->u.array.n; i++) {
        const JsonValue *item = needs->u.array.items[i];

        if (item->type != JSON_STRING || !take_text(rec, &rec->needs[i], item->u.string.bytes)) {
            return false;
        }
        rec->nneeds++;
    }
    return true;
}

/* One entry of the file into rec. false: not an entry (the file is unreadable). */
static bool take_entry(TrustRecord *rec, const JsonValue *row)
{
    static const char *const k_keys[] = {
        "kind",  "name",        "path",  "folder_sha256", "manifest_sha256",
        "files", "interpreter", "needs", "trusted_at",    "dev"};
    const char *kind = NULL;
    const char *folder = NULL;
    const char *manifest = NULL;
    const JsonValue *dev = NULL;

    if (!has_exactly(row, k_keys, sizeof(k_keys) / sizeof(k_keys[0]))) {
        return false;
    }
    kind = string_of(row, "kind");
    folder = hex_of(row, "folder_sha256");
    manifest = hex_of(row, "manifest_sha256");
    dev = member(row, "dev");
    if (kind == NULL || (strcmp(kind, "plugin") != 0 && strcmp(kind, "hook") != 0) ||
        folder == NULL || manifest == NULL || dev == NULL || dev->type != JSON_BOOL) {
        return false;
    }
    rec->kind = strcmp(kind, "hook") == 0 ? PLUGIN_TRUST_KIND_HOOK : PLUGIN_TRUST_KIND_PLUGIN;
    rec->dev = dev->u.boolean;
    memcpy(rec->folder_sha, folder, sizeof(rec->folder_sha));
    memcpy(rec->manifest_sha, manifest, sizeof(rec->manifest_sha));
    if (!take_text(rec, &rec->name, string_of(row, "name")) || rec->name[0] == '\0' ||
        !take_text(rec, &rec->path, string_of(row, "path")) ||
        !take_text(rec, &rec->trusted_at, string_of(row, "trusted_at"))) {
        return false;
    }
    if (!take_files(rec, member(row, "files")) ||
        !take_interpreter(rec, member(row, "interpreter"))) {
        return false;
    }
    return take_needs(rec, member(row, "needs"));
}

/* True when one of the first limit entries has this name. */
static bool name_is_taken(const TrustDoc *doc, const char *name, size_t limit)
{
    size_t i = 0;

    for (i = 0; i < limit; i++) {
        if (strcmp(doc->entries[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

/* Room for one more entry. */
static bool doc_reserve(TrustDoc *doc, size_t count)
{
    TrustRecord *grown = NULL;

    if (count <= doc->cap) {
        return true;
    }
    grown = (TrustRecord *)malloc(count * sizeof(*grown));
    if (grown == NULL) {
        return false;
    }
    if (doc->count > 0U) {
        memcpy(grown, doc->entries, doc->count * sizeof(*grown));
    }
    free(doc->entries);
    doc->entries = grown;
    doc->cap = count;
    return true;
}

/* The whole file as the decided document, or UNREADABLE (a name given twice included). */
static PluginTrustStatus parse_doc(const char *bytes, size_t len, TrustDoc *doc)
{
    static const char *const k_root[] = {"format", "version", "entries"};
    JsonValue *root = NULL;
    JsonReadStatus parsed = JSONREAD_OK;
    const JsonValue *entries = NULL;
    const char *format = NULL;
    const JsonValue *version = NULL;
    PluginTrustStatus status = PLUGIN_TRUST_UNREADABLE;

    parsed = jsonread_parse(bytes, len, &root);
    if (parsed != JSONREAD_OK) {
        return parsed == JSONREAD_ERR_OOM ? PLUGIN_TRUST_OOM : PLUGIN_TRUST_UNREADABLE;
    }
    if (has_exactly(root, k_root, 3U)) {
        entries = member(root, "entries");
    }
    format = entries != NULL ? string_of(root, "format") : NULL;
    version = entries != NULL ? member(root, "version") : NULL;
    if (entries != NULL && entries->type == JSON_ARRAY && format != NULL &&
        strcmp(format, "remember-trust") == 0 && version != NULL && version->type == JSON_NUMBER &&
        strcmp(version->u.number.bytes, "1") == 0 &&
        entries->u.array.n <= (size_t)PLUGIN_TRUST_ENTRIES_MAX) {
        status = PLUGIN_TRUST_OOM;
        if (doc_reserve(doc, entries->u.array.n + 1U)) {
            status = PLUGIN_TRUST_OK;
        }
        for (size_t i = 0; status == PLUGIN_TRUST_OK && i < entries->u.array.n; i++) {
            TrustRecord *rec = &doc->entries[doc->count];

            memset(rec, 0, sizeof(*rec));
            doc->count++;
            if (!take_entry(rec, entries->u.array.items[i])) {
                status = PLUGIN_TRUST_UNREADABLE;
                if (rec->oom) {
                    status = PLUGIN_TRUST_OOM;
                }
            } else if (name_is_taken(doc, rec->name, doc->count - 1U)) {
                status = PLUGIN_TRUST_UNREADABLE;
            }
        }
    }
    jsonread_free(root);
    return status;
}

/* ---- trust.json: write ----------------------------------------------------------------------- */

static int put(FILE *out, const char *text)
{
    return fputs(text, out) < 0 ? -1 : 0;
}

static int put_json(FILE *out, const char *text)
{
    return output_json_string(out, text);
}

static int write_files(FILE *out, const TrustRecord *rec)
{
    size_t i = 0;

    if (put(out, "[") != 0) {
        return -1;
    }
    for (i = 0; i < rec->nfiles; i++) {
        const FileRec *file = &rec->files[i];

        if ((i > 0U && put(out, ",") != 0) || put(out, "{\"path\":") != 0 ||
            put_json(out, file->path) != 0 || put(out, ",\"sha256\":") != 0 ||
            put_json(out, file->sha) != 0 ||
            fprintf(out, ",\"size\":%lld,\"mtime\":%lld}", file->size, file->mtime) < 0) {
            return -1;
        }
    }
    return put(out, "]");
}

static int write_interpreter(FILE *out, const TrustRecord *rec)
{
    if (rec->interp_path == NULL) {
        return put(out, "null");
    }
    if (put(out, "{\"path\":") != 0 || put_json(out, rec->interp_path) != 0 ||
        put(out, ",\"sha256\":") != 0 || put_json(out, rec->interp_sha) != 0) {
        return -1;
    }
    return put(out, "}");
}

static int write_entry(FILE *out, const TrustRecord *rec)
{
    const char *kind = rec->kind == PLUGIN_TRUST_KIND_HOOK ? "hook" : "plugin";
    size_t i = 0;

    if (put(out, "{\"kind\":") != 0 || put_json(out, kind) != 0 || put(out, ",\"name\":") != 0 ||
        put_json(out, rec->name) != 0 || put(out, ",\"path\":") != 0 ||
        put_json(out, rec->path) != 0 || put(out, ",\"folder_sha256\":") != 0 ||
        put_json(out, rec->folder_sha) != 0 || put(out, ",\"manifest_sha256\":") != 0 ||
        put_json(out, rec->manifest_sha) != 0 || put(out, ",\"files\":") != 0 ||
        write_files(out, rec) != 0 || put(out, ",\"interpreter\":") != 0 ||
        write_interpreter(out, rec) != 0 || put(out, ",\"needs\":[") != 0) {
        return -1;
    }
    for (i = 0; i < rec->nneeds; i++) {
        if ((i > 0U && put(out, ",") != 0) || put_json(out, rec->needs[i]) != 0) {
            return -1;
        }
    }
    if (put(out, "],\"trusted_at\":") != 0 || put_json(out, rec->trusted_at) != 0) {
        return -1;
    }
    if (rec->dev) {
        return put(out, ",\"dev\":true}");
    }
    return put(out, ",\"dev\":false}");
}

static int write_doc(FILE *out, const void *ctx)
{
    const TrustDoc *doc = (const TrustDoc *)ctx;
    size_t i = 0;

    if (put(out, "{\"format\":\"remember-trust\",\"version\":1,\"entries\":[") != 0) {
        return -1;
    }
    for (i = 0; i < doc->count; i++) {
        if ((i > 0U && put(out, ",") != 0) || write_entry(out, &doc->entries[i]) != 0) {
            return -1;
        }
    }
    return put(out, "]}\n");
}

/* <file><suffix>, heap. */
static char *with_suffix(const char *file, const char *suffix, size_t suffix_size)
{
    size_t n = strlen(file);
    char *out = (char *)malloc(n + suffix_size);

    if (out != NULL) {
        /* suffix_size includes the NUL of the suffix, copied by the next line. */
        // NOLINTNEXTLINE(bugprone-not-null-terminated-result)
        memcpy(out, file, n);
        memcpy(out + n, suffix, suffix_size);
    }
    return out;
}

// cppcheck-suppress staticFunction ; public API (plugin_trust.h); plans 34 and 35 call it
char *plugin_trust_file_path(const char *data_dir)
{
    return join_path(data_dir, k_file_name);
}

/* An absent file is an empty document. Anything else that cannot be used is UNREADABLE. */
static PluginTrustStatus load_doc(const char *file, TrustDoc *doc)
{
    char *bytes = NULL;
    size_t len = 0;
    int sys_errno = 0;
    PluginTrustStatus status = PLUGIN_TRUST_OK;

    memset(doc, 0, sizeof(*doc));
    if (fileio_read_regular(file, (size_t)PLUGIN_TRUST_FILE_MAX, &bytes, &len, &sys_errno) != 0) {
        if (sys_errno == ENOENT || sys_errno == ENOTDIR) {
            return PLUGIN_TRUST_OK;
        }
        return sys_errno == ENOMEM ? PLUGIN_TRUST_OOM : PLUGIN_TRUST_UNREADABLE;
    }
    status = parse_doc(bytes, len, doc);
    free(bytes);
    if (status != PLUGIN_TRUST_OK) {
        doc_clear(doc);
    }
    return status;
}

/* The temp name is fixed. The lock is held, so no other writer is using it; a stale one, a
   symlink included, is unlinked, and O_EXCL in fileio_write_new refuses anything that appears. */
static PluginTrustStatus store_doc(const char *file, const TrustDoc *doc)
{
    char *tmp = with_suffix(file, k_tmp_suffix, sizeof(k_tmp_suffix));
    int sys_errno = 0;
    PluginTrustStatus status = PLUGIN_TRUST_IO;

    if (tmp == NULL) {
        return PLUGIN_TRUST_OOM;
    }
    (void)unlink(tmp);
    if (fileio_write_new(tmp, write_doc, doc, &sys_errno) == 0) {
        if (fileio_rename(tmp, file, &sys_errno) == 0) {
            status = PLUGIN_TRUST_OK;
        } else {
            (void)unlink(tmp);
        }
    }
    free(tmp);
    return status;
}

static size_t doc_find(const TrustDoc *doc, const char *name)
{
    size_t i = 0;

    for (i = 0; i < doc->count; i++) {
        if (strcmp(doc->entries[i].name, name) == 0) {
            return i;
        }
    }
    return doc->count;
}

/* The lock on <file>.lock, the folder made first. */
static int lock_trust(const char *file)
{
    char *lock_path = with_suffix(file, k_lock_suffix, sizeof(k_lock_suffix));
    int sys_errno = 0;
    int fd = FILEIO_LOCK_IO;

    if (lock_path == NULL) {
        return LOCK_OOM;
    }
    if (fileio_mkdir_parents(lock_path, &sys_errno) == 0) {
        fd = fileio_lock_open(lock_path, PLUGIN_TRUST_LOCK_TIMEOUT_MS);
    }
    free(lock_path);
    return fd;
}

/* The slot for the entry at index at: the old one, cleared, or a new one after the last (room was
   reserved). NULL when there is no room. */
static TrustRecord *doc_slot(TrustDoc *doc, size_t at)
{
    if (at < doc->count) {
        record_clear(&doc->entries[at]);
        return &doc->entries[at];
    }
    if (at >= doc->cap) {
        return NULL;
    }
    doc->count++;
    return &doc->entries[at];
}

/* Under the lock: read the file again, put rec in place of the entry of its name (or last), write.
   rec is moved into the file's list. */
static PluginTrustStatus commit_record(const char *data_dir, TrustRecord *rec)
{
    char *file = plugin_trust_file_path(data_dir);
    TrustDoc doc;
    PluginTrustStatus status = PLUGIN_TRUST_OOM;
    size_t at = 0;
    int lock = FILEIO_LOCK_IO;

    memset(&doc, 0, sizeof(doc));
    if (file == NULL) {
        return PLUGIN_TRUST_OOM;
    }
    lock = lock_trust(file);
    if (lock < 0) {
        free(file);
        if (lock == LOCK_OOM) {
            return PLUGIN_TRUST_OOM;
        }
        return lock == FILEIO_LOCK_BUSY ? PLUGIN_TRUST_BUSY : PLUGIN_TRUST_IO;
    }
    status = load_doc(file, &doc);
    if (status == PLUGIN_TRUST_OK) {
        at = doc_find(&doc, rec->name);
        if (at == doc.count && doc.count >= (size_t)PLUGIN_TRUST_ENTRIES_MAX) {
            status = PLUGIN_TRUST_UNREADABLE; /* a full file takes no new name */
        } else if (!doc_reserve(&doc, doc.count + 1U)) {
            status = PLUGIN_TRUST_OOM;
        }
    }
    if (status == PLUGIN_TRUST_OK) {
        TrustRecord *slot = doc_slot(&doc, at);

        status = PLUGIN_TRUST_OOM;
        if (slot != NULL) {
            *slot = *rec;
            memset(rec, 0, sizeof(*rec));
            status = store_doc(file, &doc);
        }
    }
    doc_clear(&doc);
    (void)close(lock);
    free(file);
    return status;
}

/* ---- check, confirm, allow --------------------------------------------------------------------
 */

static bool is_builtin(const PluginTrustSubject *subject)
{
    if (subject->kind != PLUGIN_TRUST_KIND_PLUGIN || subject->name == NULL) {
        return false;
    }
    return strcmp(subject->name, PLUGIN_HYBRID_NAME) == 0;
}

static bool same_code(const TrustRecord *stored, const TrustRecord *now)
{
    size_t i = 0;

    if (stored->kind != now->kind || strcmp(stored->folder_sha, now->folder_sha) != 0 ||
        strcmp(stored->manifest_sha, now->manifest_sha) != 0 || stored->nfiles != now->nfiles ||
        (stored->interp_path == NULL) != (now->interp_path == NULL)) {
        return false;
    }
    for (i = 0; i < now->nfiles; i++) {
        if (strcmp(stored->files[i].path, now->files[i].path) != 0 ||
            strcmp(stored->files[i].sha, now->files[i].sha) != 0) {
            return false;
        }
    }
    if (now->interp_path == NULL) {
        return true;
    }
    if (strcmp(stored->interp_path, now->interp_path) != 0) {
        return false;
    }
    return strcmp(stored->interp_sha, now->interp_sha) == 0;
}

// cppcheck-suppress staticFunction ; public API (plugin_trust.h); plans 34 and 35 call it
PluginTrustStatus plugin_trust_check(const char *data_dir, const PluginTrustSubject *subject)
{
    char *file = NULL;
    TrustDoc doc;
    TrustRecord now;
    PluginTrustStatus status = PLUGIN_TRUST_OK;

    if (is_builtin(subject)) {
        return PLUGIN_TRUST_OK;
    }
    file = plugin_trust_file_path(data_dir);
    if (file == NULL) {
        return PLUGIN_TRUST_OOM;
    }
    status = load_doc(file, &doc);
    free(file);
    if (status != PLUGIN_TRUST_OK) {
        return status;
    }
    status = build_record(subject, &now);
    if (status == PLUGIN_TRUST_OK) {
        size_t at = doc_find(&doc, subject->name);

        status = PLUGIN_TRUST_UNTRUSTED;
        if (at < doc.count && same_code(&doc.entries[at], &now)) {
            status = PLUGIN_TRUST_OK;
        }
        record_clear(&now);
    }
    doc_clear(&doc);
    return status;
}

// cppcheck-suppress staticFunction ; public API (plugin_trust.h); plans 34 and 35 call it
PluginTrustStatus plugin_trust_confirm(const RememberEnv *env, PluginTrustKind kind,
                                       const char *name, const char *body)
{
    if (env == NULL || env->confirm == NULL) {
        return PLUGIN_TRUST_NO_CALLBACK;
    }
    if (!env->confirm(env->callback_ctx, kind == PLUGIN_TRUST_KIND_HOOK ? "hook" : "plugin", name,
                      body)) {
        return PLUGIN_TRUST_DENIED;
    }
    return PLUGIN_TRUST_OK;
}

static bool old_has_need(const TrustRecord *old, const char *need)
{
    size_t i = 0;

    for (i = 0; old != NULL && i < old->nneeds; i++) {
        if (strcmp(old->needs[i], need) == 0) {
            return true;
        }
    }
    return false;
}

/* The dialog body (H-9.4, H-9.6). A hook: one sentence. A folder: the question, then one need per
   line; for an update only the needs the old record lacks (the wording of the lines is
   decision:open-2026-11-22-plan33-trust-body-lines). */
static char *build_body(const PluginTrustSubject *subject, const TrustRecord *old)
{
    size_t i = 0;
    size_t len = 0;
    char *body = NULL;
    char *at = NULL;

    if (subject->kind == PLUGIN_TRUST_KIND_HOOK) {
        const char *path = subject->commands[0].argv[0];
        int need = snprintf(NULL, 0, "Allow hook \"%s\" to run %s?", subject->name, path);

        body = (char *)malloc((size_t)need + 1U);
        if (body != NULL) {
            (void)snprintf(body, (size_t)need + 1U, "Allow hook \"%s\" to run %s?", subject->name,
                           path);
        }
        return body;
    }
    len = strlen(subject->name) +
          sizeof("Allow \"\" to read your memories? It can send them off this Mac.");
    for (i = 0; i < subject->nneeds; i++) {
        if (!old_has_need(old, subject->needs[i])) {
            len += strlen(subject->needs[i]) + 1U;
        }
    }
    body = (char *)malloc(len);
    if (body == NULL) {
        return NULL;
    }
    at = body + snprintf(body, len,
                         "Allow \"%s\" to read your memories? It can send them off this Mac.",
                         subject->name);
    for (i = 0; i < subject->nneeds; i++) {
        if (!old_has_need(old, subject->needs[i])) {
            at += snprintf(at, len - (size_t)(at - body), "\n%s", subject->needs[i]);
        }
    }
    return body;
}

PluginTrustStatus plugin_trust_allow(const char *data_dir, const RememberEnv *env,
                                     const PluginTrustSubject *subject, const char *now)
{
    TrustRecord rec;
    TrustDoc doc;
    char *file = NULL;
    char *body = NULL;
    PluginTrustStatus status = PLUGIN_TRUST_OK;

    if (is_builtin(subject)) {
        return PLUGIN_TRUST_OK;
    }
    status = build_record(subject, &rec);
    file = status == PLUGIN_TRUST_OK ? plugin_trust_file_path(data_dir) : NULL;
    if (status != PLUGIN_TRUST_OK || file == NULL) {
        return status != PLUGIN_TRUST_OK ? status : PLUGIN_TRUST_OOM;
    }
    status = load_doc(file, &doc);
    free(file);
    if (status == PLUGIN_TRUST_OK) {
        size_t at = doc_find(&doc, subject->name);

        body = build_body(subject, at < doc.count ? &doc.entries[at] : NULL);
        doc_clear(&doc);
        status = body != NULL ? plugin_trust_confirm(env, subject->kind, subject->name, body)
                              : PLUGIN_TRUST_OOM;
        free(body);
    }
    if (status == PLUGIN_TRUST_OK && !take_text(&rec, &rec.trusted_at, now)) {
        status = PLUGIN_TRUST_OOM;
    }
    if (status == PLUGIN_TRUST_OK) {
        status = commit_record(data_dir, &rec);
    }
    record_clear(&rec);
    return status;
}

/* ---- an installed plugin ----------------------------------------------------------------------
 */

_Static_assert((size_t)PLUGIN_ENTRIES_MAX <= (size_t)PLUGIN_TRUST_COMMANDS_MAX,
               "every entry of a manifest is a command a subject can hold");

// cppcheck-suppress staticFunction ; public API (plugin_trust.h); plans 34 and 35 call it
void plugin_trust_installed_free(PluginTrustInstalled *installed)
{
    free((void *)installed->argv_pool);
    free(installed->commands);
    free((void *)installed->needs);
    free(installed->folder);
    plugin_manifest_free(installed->manifest);
    memset(installed, 0, sizeof(*installed));
}

static bool has_need(const char **needs, size_t count, const char *need)
{
    size_t i = 0;

    for (i = 0; i < count; i++) {
        if (needs[i] != NULL && strcmp(needs[i], need) == 0) {
            return true;
        }
    }
    return false;
}

/* The manifest's commands and distinct needs into the owned arrays. false: memory ran out. */
static bool fill_subject(PluginTrustInstalled *installed)
{
    const PluginManifest *man = installed->manifest;
    size_t count = man->nentries;

    installed->commands = (PluginTrustCommand *)malloc(count * sizeof(*installed->commands));
    installed->argv_pool = (const char **)malloc(count * (size_t)PLUGIN_ARGS_MAX * sizeof(char *));
    installed->needs =
        (const char **)malloc(((count * (size_t)PLUGIN_NEEDS_MAX) + 1U) * sizeof(char *));
    if (installed->commands == NULL || installed->argv_pool == NULL || installed->needs == NULL) {
        return false;
    }
    memset((void *)installed->needs, 0, ((count * (size_t)PLUGIN_NEEDS_MAX) + 1U) * sizeof(char *));
    installed->subject.commands = installed->commands;
    for (size_t i = 0; i < count; i++) {
        const PluginEntry *entry = &man->entries[i];
        const char **argv = installed->argv_pool + (i * (size_t)PLUGIN_ARGS_MAX);

        for (size_t j = 0; j < entry->run.argc; j++) {
            argv[j] = entry->run.argv[j];
        }
        installed->commands[i] = (PluginTrustCommand){.argv = argv, .argc = entry->run.argc};
        installed->subject.ncommands = i + 1U;
        for (size_t j = 0; j < entry->nneeds; j++) {
            if (!has_need(installed->needs, installed->subject.nneeds, entry->needs[j])) {
                installed->needs[installed->subject.nneeds++] = entry->needs[j];
            }
        }
    }
    installed->subject.needs = installed->needs;
    return true;
}

// cppcheck-suppress staticFunction ; public API (plugin_trust.h); plans 34 and 35 call it
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the data dir, the name, the kind, the PATH
PluginTrustStatus plugin_trust_load_installed(const char *data_dir, const char *name,
                                              PluginTrustKind kind, const char *search_path,
                                              PluginTrustInstalled *out)
{
    char *plugins = join_path(data_dir, "plugins");
    char *json_path = NULL;
    char *bytes = NULL;
    size_t len = 0;
    int sys_errno = 0;
    PluginParseStatus parsed = PLUGIN_PARSE_SYNTAX;

    memset(out, 0, sizeof(*out));
    out->folder = plugins != NULL ? join_path(plugins, name) : NULL;
    free(plugins);
    json_path = out->folder != NULL ? join_path(out->folder, "plugin.json") : NULL;
    if (json_path == NULL) {
        plugin_trust_installed_free(out);
        return PLUGIN_TRUST_OOM;
    }
    if (fileio_read_regular(json_path, (size_t)PLUGIN_JSON_MAX, &bytes, &len, &sys_errno) != 0) {
        free(json_path);
        plugin_trust_installed_free(out);
        return sys_errno == ENOMEM ? PLUGIN_TRUST_OOM : PLUGIN_TRUST_UNHASHABLE;
    }
    free(json_path);
    parsed = plugin_manifest_parse(bytes, len, &out->manifest);
    free(bytes);
    if (parsed == PLUGIN_PARSE_OOM || out->manifest == NULL || out->manifest->oom) {
        plugin_trust_installed_free(out);
        return PLUGIN_TRUST_OOM;
    }
    if (parsed != PLUGIN_PARSE_OK || out->manifest->nerrors > 0U ||
        strcmp(out->manifest->name, name) != 0 || out->manifest->nentries == 0U) {
        plugin_trust_installed_free(out);
        return PLUGIN_TRUST_UNHASHABLE;
    }
    out->subject.name = out->manifest->name;
    out->subject.folder = out->folder;
    out->subject.search_path = search_path;
    out->subject.kind = kind;
    if (!fill_subject(out)) {
        plugin_trust_installed_free(out);
        return PLUGIN_TRUST_OOM;
    }
    return PLUGIN_TRUST_OK;
}

PluginTrustStatus plugin_trust_check_installed(const char *data_dir, const char *name,
                                               const char *search_path)
{
    char *file = plugin_trust_file_path(data_dir);
    TrustDoc doc;
    PluginTrustInstalled installed;
    PluginTrustStatus status = PLUGIN_TRUST_OK;
    size_t at = 0;

    if (strcmp(name, PLUGIN_HYBRID_NAME) == 0) {
        free(file);
        return PLUGIN_TRUST_OK;
    }
    if (file == NULL) {
        return PLUGIN_TRUST_OOM;
    }
    status = load_doc(file, &doc);
    free(file);
    if (status != PLUGIN_TRUST_OK) {
        return status;
    }
    at = doc_find(&doc, name);
    if (at == doc.count) {
        doc_clear(&doc);
        return PLUGIN_TRUST_UNTRUSTED;
    }
    status =
        plugin_trust_load_installed(data_dir, name, doc.entries[at].kind, search_path, &installed);
    doc_clear(&doc);
    if (status == PLUGIN_TRUST_OK) {
        status = plugin_trust_check(data_dir, &installed.subject);
    }
    plugin_trust_installed_free(&installed);
    return status;
}
