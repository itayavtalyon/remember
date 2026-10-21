#ifndef REMEMBER_FAILING_STREAM_H
#define REMEMBER_FAILING_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

enum { FAILING_STREAM_CAP = 4096, FAILING_STREAM_PAD = 7 };

/*
 * Test-only stream (plan 18b B4). Writes succeed while the total stays within
 * limit bytes; the write that would pass it fails, and so does every later one.
 * The stream is unbuffered, so each stdio call reaches the check. The bytes
 * that went through are kept, NUL-terminated, in buf. late_writes counts the
 * write calls made after the first failure: a writer that stops at its first
 * failed write leaves it at 0.
 */
typedef struct {
    char buf[FAILING_STREAM_CAP];
    size_t len;
    size_t limit;
    size_t late_writes;
    bool failed;
    char pad_[FAILING_STREAM_PAD]; /* explicit tail padding (kept -Wpadded-clean) */
} FailingStream;

/* The caller owns s and fcloses the result; NULL when the platform call fails. */
FILE *failing_stream_open(FailingStream *s, size_t limit);

#endif /* REMEMBER_FAILING_STREAM_H */
