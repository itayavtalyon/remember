#include "fileio.h"

#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const unsigned NIBBLE_BITS = 4U;
static const unsigned NIBBLE_MASK = 0xFU;

enum {
    RANDOM_BYTES = 8,
    HEX_LEN = 2 * RANDOM_BYTES,
    DECIMAL_BASE = 10,
    EXPORT_FILE_MODE = 0600,
    EXPORT_DIR_MODE = 0700,
    READ_FIRST_CAP = 8192
};

#ifdef REMEMBER_TEST_CLI
/* Test CLI only: REMEMBER_TEST_EXPORT_FAIL=<site>[#N][:<errno>] fails the Nth
   call at that site (N defaults to 1) with errno (default EIO). Sites:
   random, alloc, join, fdopen, write (after the document is written), close,
   rename (rename#2 is the second D17 rename). */
static bool fault(const char *site)
{
    static unsigned hit;
    // NOLINTNEXTLINE(concurrency-mt-unsafe) single-threaded CLI
    const char *v = getenv("REMEMBER_TEST_EXPORT_FAIL");
    size_t n = strlen(site);
    unsigned want = 1U;
    const char *rest = NULL;
    char *end = NULL;

    if (v == NULL || strncmp(v, site, n) != 0) {
        return false;
    }
    rest = v + n;
    if (rest[0] == '#') {
        want = (unsigned)strtoul(rest + 1, &end, DECIMAL_BASE);
        rest = end;
    }
    if (rest[0] != '\0' && rest[0] != ':') {
        return false;
    }
    hit++;
    if (hit != want) {
        return false;
    }
    errno = (rest[0] == ':') ? (int)strtol(rest + 1, NULL, DECIMAL_BASE) : EIO;
    return true;
}
#define FAULT(site) fault(site)

/* Test CLI only: REMEMBER_TEST_EXPORT_CRASH=1 stops the process between the
   two D17 renames, as a crash would. exit, not _exit, so a coverage build
   still writes its profile; no atexit handler touches the files. */
static void test_crash_between_renames(void)
{
    // NOLINTNEXTLINE(concurrency-mt-unsafe) single-threaded CLI
    if (getenv("REMEMBER_TEST_EXPORT_CRASH") != NULL) {
        exit(1); // NOLINT(concurrency-mt-unsafe) single-threaded CLI
    }
}
#else
/* Release builds have no fault sites. */
#define FAULT(site) false

static void test_crash_between_renames(void) {}
#endif

static int random_hex(char out[HEX_LEN + 1], int *sys_errno)
{
    static const char hex[] = "0123456789abcdef";
    unsigned char b[RANDOM_BYTES];
    size_t i = 0;
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    ssize_t got = (fd < 0 || FAULT("random")) ? -1 : read(fd, b, sizeof b);
    int read_errno = errno;

    if (fd >= 0) {
        (void)close(fd);
    }
    if (got != (ssize_t)sizeof b) {
        *sys_errno = (got < 0) ? read_errno : EIO;
        return -1;
    }
    for (i = 0; i < sizeof b; i++) {
        out[2U * i] = hex[b[i] >> NIBBLE_BITS];
        out[(2U * i) + 1U] = hex[b[i] & NIBBLE_MASK];
    }
    out[HEX_LEN] = '\0';
    return 0;
}

char *fileio_sibling_name(const char *path, const char *tag, int *sys_errno)
{
    char rnd[HEX_LEN + 1];
    size_t n = 0;
    char *out = NULL;

    if (random_hex(rnd, sys_errno) != 0) {
        return NULL;
    }
    n = strlen(path) + strlen(tag) + sizeof rnd + 2U;
    if (!FAULT("alloc")) {
        out = (char *)malloc(n);
    }
    if (out == NULL) {
        *sys_errno = ENOMEM;
        return NULL;
    }
    (void)snprintf(out, n, "%s.%s-%s", path, tag, rnd);
    return out;
}

bool fileio_exists(const char *path)
{
    struct stat st;

    return lstat(path, &st) == 0;
}

bool fileio_same_file(const char *a, const char *b)
{
    struct stat first;
    struct stat second;

    if (stat(a, &first) != 0 || stat(b, &second) != 0 || first.st_dev != second.st_dev) {
        return false;
    }
    return first.st_ino == second.st_ino;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the file, then the bytes it starts with
bool fileio_starts_with(const char *path, const char *prefix)
{
    /* macOS fopen has no 'e' (O_CLOEXEC) mode; CLI is short-lived. */
    // NOLINTNEXTLINE(android-cloexec-fopen)
    FILE *f = fopen(path, "rb");
    const char *p = prefix;

    if (f == NULL) {
        return false;
    }
    while (*p != '\0') {
        int ch = fgetc(f);

        if (ch == EOF || ch != (unsigned char)*p) {
            break;
        }
        p++;
    }
    (void)fclose(f);
    return *p == '\0';
}

bool fileio_is_dir(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode) != 0;
}

int fileio_write_new(const char *path, FileWriteFn fn, const void *ctx, int *sys_errno)
{
    FILE *f = NULL;
    /* O_CREAT | O_EXCL refuses any existing path, a symlink included. */
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, EXPORT_FILE_MODE);

    if (fd < 0) {
        *sys_errno = errno;
        return -1;
    }
    if (!FAULT("fdopen")) {
        f = fdopen(fd, "wb");
    }
    if (f == NULL) {
        *sys_errno = errno;
        (void)close(fd);
        (void)unlink(path);
        return -1;
    }
    errno = 0;
    if (fn(f, ctx) != 0 || fflush(f) != 0 || fsync(fd) != 0 || FAULT("write")) {
        *sys_errno = (errno != 0) ? errno : EIO;
        (void)fclose(f);
        (void)unlink(path);
        return -1;
    }
    if (fclose(f) != 0 || FAULT("close")) {
        *sys_errno = errno;
        (void)unlink(path);
        return -1;
    }
    return 0;
}

int fileio_rename(const char *from, const char *to, int *sys_errno)
{
    if (FAULT("rename") || rename(from, to) != 0) {
        *sys_errno = errno;
        return -1;
    }
    return 0;
}

char *fileio_join(const char *dir, const char *name, int *sys_errno)
{
    size_t n = strlen(dir) + strlen(name) + 2U;
    char *out = NULL;

    if (!FAULT("join")) {
        out = (char *)malloc(n);
    }

    if (out == NULL) {
        *sys_errno = ENOMEM;
        return NULL;
    }
    (void)snprintf(out, n, "%s/%s", dir, name);
    return out;
}

int fileio_mkdir(const char *path, int *sys_errno)
{
    if (mkdir(path, EXPORT_DIR_MODE) != 0) {
        *sys_errno = errno;
        return -1;
    }
    return 0;
}

#ifdef REMEMBER_TEST_CLI
static int race_text(FILE *out, const void *ctx)
{
    (void)ctx;
    return fputs("keep\n", out) < 0 ? -1 : 0;
}

/* Test CLI only: REMEMBER_TEST_EXPORT_RACE=1 puts a folder holding race.txt
   at path just before the Markdown folder goes there, as another process
   could after the existence check. */
static void test_race_before_swap(const char *path)
{
    int unused = 0;
    char *file = NULL;

    // NOLINTNEXTLINE(concurrency-mt-unsafe) single-threaded CLI
    if (getenv("REMEMBER_TEST_EXPORT_RACE") == NULL) {
        return;
    }
    (void)mkdir(path, EXPORT_DIR_MODE);
    file = fileio_join(path, "race.txt", &unused);
    if (file != NULL) {
        (void)fileio_write_new(file, race_text, NULL, &unused);
    }
    free(file);
}
#else
static void test_race_before_swap(const char *path)
{
    (void)path;
}
#endif

int fileio_replace_dir(const char *tmp, const char *path, bool force, int *sys_errno)
{
    char *old = NULL;

    test_race_before_swap(path);
    /* Without force nothing at path moves: a folder that appeared since the
       existence check makes the rename fail. */
    if (!force || !fileio_exists(path)) {
        return fileio_rename(tmp, path, sys_errno);
    }
    old = fileio_sibling_name(path, "old", sys_errno);
    if (old == NULL || fileio_rename(path, old, sys_errno) != 0) {
        free(old);
        return -1;
    }
    test_crash_between_renames();
    if (fileio_rename(tmp, path, sys_errno) != 0) {
        (void)rename(old, path);
        free(old);
        return -1;
    }
    fileio_remove_tree(old);
    free(old);
    return 0;
}

static int remove_one(const char *path, const struct stat *st, int type, struct FTW *ftw)
{
    (void)st;
    (void)type;
    (void)ftw;
    (void)remove(path);
    return 0;
}

void fileio_remove_tree(const char *path)
{
    enum { FTW_FDS = 16 };

    /* ponytail: a file that will not go is left behind as a stray sibling,
       the same state a crash leaves (D17). */
    // NOLINTNEXTLINE(concurrency-mt-unsafe) single-threaded CLI
    (void)nftw(path, remove_one, FTW_FDS, FTW_DEPTH | FTW_PHYS);
}

int fileio_read_all(const char *path, char **out_bytes, size_t *out_len, int *sys_errno)
{
    FILE *f = NULL;
    char *buf = NULL;
    size_t cap = 0;
    size_t n = 0;
    int closed = 0;

    if (out_bytes == NULL || out_len == NULL || sys_errno == NULL) {
        return -1;
    }
    *out_bytes = NULL;
    *out_len = 0;
    errno = 0;
    /* macOS fopen has no 'e' (O_CLOEXEC) mode; CLI is short-lived. */
    // NOLINTNEXTLINE(android-cloexec-fopen)
    f = fopen(path, "rb");
    if (f == NULL) {
        goto fail;
    }
    for (;;) {
        size_t want = 0;
        size_t got = 0;

        if (n + 1U >= cap) {
            size_t next = (cap == 0U) ? (size_t)READ_FIRST_CAP : (cap * 2U);
            char *grown = (char *)realloc(buf, next);

            if (grown == NULL) {
                goto fail; /* realloc sets ENOMEM */
            }
            buf = grown;
            cap = next;
        }
        want = cap - n - 1U; /* keep one byte for the NUL */
        got = fread(buf + n, 1U, want, f);
        n += got;
        if (got < want) {
            break; /* end of file or an error: never read the stream again */
        }
    }
    if (ferror(f) != 0) {
        goto fail;
    }
    closed = fclose(f);
    f = NULL;
    if (closed != 0) {
        goto fail;
    }
    buf[n] = '\0';
    *out_bytes = buf;
    *out_len = n;
    return 0;

fail:
    /* errno was 0 before fopen; a failure that leaves it 0 (a stream error) is EIO. */
    // NOLINTNEXTLINE(clang-analyzer-unix.Errno)
    *sys_errno = (errno != 0) ? errno : EIO;
    if (f != NULL) {
        (void)fclose(f);
    }
    free(buf);
    return -1;
}
