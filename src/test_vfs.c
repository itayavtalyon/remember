#include "test_vfs.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/*
 * Wraps the unix VFS and fails reads and writes on demand. Unix methods other
 * than xOpen ignore their VFS pointer, so those stay the parent's. The unix
 * file stays at the front of the buffer (its own methods expect that layout).
 * A tail after it holds a copy of those methods with xRead and xWrite replaced.
 * Every other method — truncate, sector size, locks — stays the unix function,
 * which is what SQLite actually calls.
 *
 * REMEMBER_TEST_ENOSPC=1 fails every write with SQLITE_FULL.
 * REMEMBER_TEST_FAIL_WRITE=N lets N writes succeed, then fails the rest with
 * SQLITE_IOERR_WRITE. REMEMBER_TEST_FAIL_READ=N does the same for reads with
 * SQLITE_IOERR_READ. REMEMBER_TEST_NOMEM=1 returns SQLITE_IOERR_NOMEM from a
 * write, and from a read of a non-empty file. A zero-length read is the probe
 * SQLite does before it writes a new brain's header; failing that probe would
 * make the write-side fault unreachable. SQLite rewrites IOERR_NOMEM to
 * SQLITE_NOMEM before the store sees it (decision:plan16-nomem-sqlite-name);
 * D6 still maps it to database.
 */

/* Parent method table plus the copy whose read/write slots we replace. */
typedef struct TestTail {
    const sqlite3_io_methods *parent_io;
    sqlite3_io_methods io;
} TestTail;

/* Mutable for the process: install reads the env once, then each read or
 * write bumps a counter. There is no per-connection place to keep them. */
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
static bool g_enospc = false;
static bool g_nomem = false;
static int g_fail_write_after = -1;
static int g_writes = 0;
static int g_fail_read_after = -1;
static int g_reads = 0;
static int g_parent_file_size = 0;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

enum { DECIMAL_BASE = 10 };

/* Non-negative integer, or -1 when the variable is unset or not that integer. */
static int env_count(const char *name)
{
    const char *text = NULL;
    char *end = NULL;
    long value = 0;

    /* Single-threaded CLI; getenv has no portable thread-safe variant. */
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    text = getenv(name);
    if (text == NULL || text[0] == '\0') {
        return -1;
    }
    errno = 0;
    value = strtol(text, &end, DECIMAL_BASE);
    if (end == text || *end != '\0' || errno != 0 || value < 0 || value > INT_MAX) {
        return -1;
    }
    return (int)value;
}

/* The string "1" turns the flag on. Anything else, including unset, leaves it off. */
static bool env_is_one(const char *name)
{
    const char *text = NULL;

    /* Single-threaded CLI; getenv has no portable thread-safe variant. */
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    text = getenv(name);
    if (text == NULL) {
        return false;
    }
    if (strcmp(text, "1") == 0) {
        return true;
    }
    return false;
}

static TestTail *tail_of(sqlite3_file *file)
{
    char *bytes = NULL;
    TestTail *tail = NULL;

    assert(file != NULL);
    assert(g_parent_file_size > 0);
    /* Parent szOsFile is the size of unixFile, which is pointer-aligned, so
     * the tail placed after it is aligned too. */
    assert(g_parent_file_size % (int)_Alignof(TestTail) == 0);
    bytes = (char *)file + g_parent_file_size;
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-align"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
#endif
    tail = (TestTail *)bytes;
#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    return tail;
}

static int vfs_read(sqlite3_file *file, void *buf, int amt, sqlite3_int64 offset)
{
    const sqlite3_io_methods *methods = tail_of(file)->parent_io;
    sqlite3_int64 size = 0;

    assert(methods != NULL && methods->xRead != NULL);
    if (g_nomem) {
        /* An empty file has not been read yet. Let that short read through so
         * the header write can take the NOMEM fault. A file that already has
         * bytes fails here, before the read system call. */
        if (methods->xFileSize != NULL && methods->xFileSize(file, &size) == SQLITE_OK &&
            size == 0) {
            return methods->xRead(file, buf, amt, offset);
        }
        return SQLITE_IOERR_NOMEM;
    }
    if (g_fail_read_after >= 0) {
        if (g_reads >= g_fail_read_after) {
            return SQLITE_IOERR_READ;
        }
        g_reads++;
    }
    return methods->xRead(file, buf, amt, offset);
}

static int vfs_write(sqlite3_file *file, const void *buf, int amt, sqlite3_int64 offset)
{
    const sqlite3_io_methods *methods = tail_of(file)->parent_io;

    assert(methods != NULL && methods->xWrite != NULL);
    if (g_nomem) {
        return SQLITE_IOERR_NOMEM;
    }
    if (g_enospc) {
        return SQLITE_FULL;
    }
    if (g_fail_write_after >= 0) {
        if (g_writes >= g_fail_write_after) {
            return SQLITE_IOERR_WRITE;
        }
        g_writes++;
    }
    return methods->xWrite(file, buf, amt, offset);
}

static int vfs_open(sqlite3_vfs *vfs, sqlite3_filename name, sqlite3_file *file, int flags,
                    int *out_flags)
{
    sqlite3_vfs *parent = (sqlite3_vfs *)vfs->pAppData;
    TestTail *tail = NULL;
    int rc = 0;

    assert(parent != NULL);
    assert(file != NULL);
    rc = parent->xOpen(parent, name, file, flags, out_flags);
    if (rc != SQLITE_OK || file->pMethods == NULL) {
        return rc;
    }
    tail = tail_of(file);
    tail->parent_io = file->pMethods;
    tail->io = *file->pMethods;
    tail->io.xRead = vfs_read;
    tail->io.xWrite = vfs_write;
    file->pMethods = &tail->io;
    return SQLITE_OK;
}

void remember_test_vfs_install(void)
{
    sqlite3_vfs *parent = sqlite3_vfs_find("unix");
    static sqlite3_vfs shim;
    int rc = 0;

    assert(parent != NULL);
    g_enospc = env_is_one("REMEMBER_TEST_ENOSPC");
    g_nomem = env_is_one("REMEMBER_TEST_NOMEM");
    g_fail_write_after = env_count("REMEMBER_TEST_FAIL_WRITE");
    g_writes = 0;
    g_fail_read_after = env_count("REMEMBER_TEST_FAIL_READ");
    g_reads = 0;
    g_parent_file_size = parent->szOsFile;
    shim = *parent;
    shim.pNext = NULL;
    shim.zName = "remember-test";
    shim.pAppData = parent;
    shim.szOsFile = parent->szOsFile + (int)sizeof(TestTail);
    shim.xOpen = vfs_open;
    /* Register is the point of this function. assert checks it; it must still run. */
    rc = sqlite3_vfs_register(&shim, 1);
    assert(rc == SQLITE_OK);
    (void)rc;
}
