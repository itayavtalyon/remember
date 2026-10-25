/* fopencookie is a GNU extension; funopen is in the BSD stdio. glibc's fmemopen
   reserves a byte for the NUL and does not keep a write error sticky, so the two
   platforms would fail at different bytes (plan 18b stage 4 probe). */
#ifdef __linux__
#define _GNU_SOURCE // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) glibc
                    // feature macro
#endif

#include "failing_stream.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

/* The bytes that fit, or false once the limit is passed (and from then on). */
static bool accept_bytes(FailingStream *s, const char *data, size_t n)
{
    if (s->failed) {
        s->late_writes++;
        errno = ENOSPC;
        return false;
    }
    if (n > s->limit - s->len || n >= sizeof s->buf - s->len) {
        s->failed = true;
        errno = ENOSPC;
        return false;
    }
    memcpy(s->buf + s->len, data, n);
    s->len += n;
    s->buf[s->len] = '\0';
    return true;
}

#ifdef __APPLE__
static int write_cookie(void *cookie, const char *data, int n)
{
    if (!accept_bytes((FailingStream *)cookie, data, (size_t)n)) {
        return -1;
    }
    return n;
}

FILE *failing_stream_open(FailingStream *s, size_t limit)
{
    FILE *f = NULL;

    memset(s, 0, sizeof *s);
    s->limit = limit;
    f = funopen(s, NULL, write_cookie, NULL, NULL);
    if (f != NULL) {
        (void)setvbuf(f, NULL, _IONBF, 0);
    }
    return f;
}
#elif defined(__GLIBC__)
/* glibc reads a 0 return as a write error; a cookie write must not return < 0. */
static ssize_t write_cookie(void *cookie, const char *data, size_t n)
{
    if (!accept_bytes((FailingStream *)cookie, data, n)) {
        return 0;
    }
    return (ssize_t)n;
}

FILE *failing_stream_open(FailingStream *s, size_t limit)
{
    cookie_io_functions_t funcs = {
        .read = NULL, .write = write_cookie, .seek = NULL, .close = NULL};
    FILE *f = NULL;

    memset(s, 0, sizeof *s);
    s->limit = limit;
    f = fopencookie(s, "w", funcs);
    if (f != NULL) {
        (void)setvbuf(f, NULL, _IONBF, 0);
    }
    return f;
}
#else
#error "failing_stream needs funopen (BSD/macOS) or fopencookie (glibc)"
#endif
