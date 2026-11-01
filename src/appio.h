#ifndef REMEMBER_APPIO_H
#define REMEMBER_APPIO_H

#include <stdbool.h>
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

/* Output/error stream pair. Passed by value so callers name each end and cannot
   transpose the two FILE* arguments. */
typedef struct {
    FILE *out;
    FILE *err;
} AppStreams;

/* Swap the streams. NULL fields restore the stdout/stderr default. */
void app_set_streams(AppStreams streams);

/*
 * List paging for the JSON envelope. The command sets the slot; remember_run
 * takes it. A set slot with a NULL cursor is the last page (JSON null).
 * An unset slot omits next_cursor and has_more. The cursor string is copied.
 */
void app_clear_list_page(void);
int app_set_list_page(const char *next_cursor);
bool app_take_list_page(char **out_cursor);

#endif /* REMEMBER_APPIO_H */
