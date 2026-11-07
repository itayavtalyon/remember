#ifndef REMEMBER_APPIO_H
#define REMEMBER_APPIO_H

#include "cli.h"

#include <stdbool.h>
#include <stddef.h>
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

/* File descriptor of the error stream, or -1. For a child to inherit. */
int app_err_fileno(void);

/* Output/error stream pair. Passed by value so callers name each end and cannot
   transpose the two FILE* arguments. */
typedef struct {
    FILE *out;
    FILE *err;
} AppStreams;

/* Swap the streams. NULL fields restore the stdout/stderr default. */
void app_set_streams(AppStreams streams);

/* How this process presents progress, prompts, and color. One struct so the
   two switches cannot be swapped at the call. The next remember_run replaces it. */
typedef struct {
    CliColor color;
    bool quiet;
    bool json;
    /* NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers) */
    char pad_[2]; /* explicit tail padding (kept -Wpadded-clean) */
} AppPresentation;

/* Store the mode and restart the progress clock. */
void app_set_presentation(AppPresentation presentation);

/* never is off. always is on, even when stream is NULL or not a terminal.
   auto is on only for a terminal, when NO_COLOR is unset and TERM is not "dumb".
   An empty NO_COLOR is set. */
bool app_use_color(FILE *stream);

/* One whole progress line on the error stream, plus a newline. Only when that
   stream is a terminal, at most once per 1000 ms, with no spinner. quiet and
   --json write nothing. now_ms is the caller's clock. Never touches stdout. */
void app_progress(const char *line, long long now_ms);

/* Write prompt on the error stream, then read one typed line from in.
   The terminal echoes the typed characters; this does not hide them or write
   them again. Returns 0 and drops the trailing newline, or -1. */
int app_prompt_line(FILE *in, const char *prompt, char *buf, size_t cap);

/*
 * List paging for the JSON envelope. The command sets the slot; remember_run
 * takes it. A set slot with a NULL cursor is the last page (JSON null).
 * An unset slot omits next_cursor and has_more. The cursor string is copied.
 */
void app_clear_list_page(void);
int app_set_list_page(const char *next_cursor);
bool app_take_list_page(char **out_cursor);

#endif /* REMEMBER_APPIO_H */
