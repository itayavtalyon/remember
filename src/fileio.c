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
    EXPORT_DIR_MODE = 0700
};

#ifdef REMEMBER_TEST_CLI
/* Test CLI only: REMEMBER_TEST_EXPORT_FAIL=<site>[#N][:<errno>] fails the Nth
   call at that site (N defaults to 1) with errno (default EIO). Sites:
   random, alloc, join, fdopen, write (after the document is written), close,
   rename, swap (the second D17 rename). */
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

int fileio_replace_dir(const char *tmp, const char *path, int *sys_errno)
{
    char *old = NULL;

    if (!fileio_exists(path)) {
        return fileio_rename(tmp, path, sys_errno);
    }
    old = fileio_sibling_name(path, "old", sys_errno);
    if (old == NULL || fileio_rename(path, old, sys_errno) != 0) {
        free(old);
        return -1;
    }
    test_crash_between_renames();
    if (FAULT("swap") || fileio_rename(tmp, path, sys_errno) != 0) {
        *sys_errno = errno;
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
