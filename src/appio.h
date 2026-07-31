#ifndef REMEMBER_APPIO_H
#define REMEMBER_APPIO_H

#include <stdio.h>

/*
 * Process-global output/error streams for the CLI pipeline.
 *
 * They default to stdout/stderr; remember_run swaps them so an in-process caller
 * (the native GUI, which links the core instead of spawning the process) can
 * capture the exact same bytes the CLI would print.
 *
 * ponytail: process-global, not threaded through every command signature. The
 * CLI is single-threaded (SQLITE_THREADSAFE=0) and the GUI serializes calls
 * through an actor, so one global pair is enough. Thread a FILE* through the
 * command layer only if concurrent in-process callers ever appear.
 */

/* Current output stream (never NULL — falls back to stdout). */
FILE *app_out(void);

/* Current error stream (never NULL — falls back to stderr). */
FILE *app_err(void);

/* Swap the streams. NULL restores the stdout/stderr default. */
void app_set_streams(FILE *out, FILE *err);

#endif /* REMEMBER_APPIO_H */
