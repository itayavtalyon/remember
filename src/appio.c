#include "appio.h"

#include "cli.h"
#include "output.h"

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* stdout/stderr are not constant expressions, so resolve the default lazily in
   the accessors rather than initializing the globals to them. Deliberately mutable:
   app_set_streams() is the test seam that redirects output for capture. */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static FILE *g_out = NULL;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static FILE *g_err = NULL;

FILE *app_out(void)
{
    return g_out != NULL ? g_out : stdout;
}

FILE *app_err(void)
{
    return g_err != NULL ? g_err : stderr;
}

int app_err_fileno(void)
{
    return fileno(app_err());
}

void app_set_streams(AppStreams streams)
{
    g_out = streams.out;
    g_err = streams.err;
}

typedef struct {
    char *next_cursor; /* owned; NULL with active means JSON null */
    bool active;
    /* NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers) */
    char pad_[7]; /* explicit tail padding (kept -Wpadded-clean) */
} ListPage;

/* NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) */
static ListPage g_list_page;

static char *dup_cstr(const char *s)
{
    size_t n = strlen(s);
    char *copy = (char *)malloc(n + 1U);

    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, s, n + 1U);
    return copy;
}

static bool g_document;

void app_mark_document(void)
{
    g_document = true;
}

bool app_take_document(void)
{
    bool was = g_document;

    g_document = false;
    return was;
}

void app_clear_list_page(void)
{
    free(g_list_page.next_cursor);
    g_list_page.next_cursor = NULL;
    g_list_page.active = false;
}

int app_set_list_page(const char *next_cursor)
{
    char *copy = NULL;

    if (next_cursor != NULL) {
        copy = dup_cstr(next_cursor);
        if (copy == NULL) {
            return -1;
        }
    }
    free(g_list_page.next_cursor);
    g_list_page.next_cursor = copy;
    g_list_page.active = true;
    return 0;
}

bool app_take_list_page(char **out_cursor)
{
    bool active = g_list_page.active;

    *out_cursor = g_list_page.next_cursor;
    g_list_page.next_cursor = NULL;
    g_list_page.active = false;
    return active;
}

/* One second. A gap of exactly this long allows the next line. */
enum { PROGRESS_MIN_INTERVAL_MS = 1000 };

typedef struct {
    AppPresentation mode;
    long long progress_last_ms;
    bool progress_seen;
    /* NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers) */
    char pad_[7]; /* explicit tail padding (kept -Wpadded-clean) */
} PresentationState;

/* NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) */
static PresentationState g_state;

void app_set_presentation(AppPresentation presentation)
{
    g_state.mode = presentation;
    g_state.progress_seen = false;
    g_state.progress_last_ms = 0;
}

static bool stream_is_tty(FILE *stream)
{
    int descriptor = -1;

    if (stream == NULL) {
        return false;
    }
    descriptor = fileno(stream);
    if (descriptor < 0) {
        return false;
    }
    return isatty(descriptor) == 1;
}

/* cppcheck-suppress staticFunction ; public API (appio.h); tests and plan 25 */
bool app_use_color(FILE *stream)
{
    const char *no_color = NULL;
    const char *term = NULL;

    if (g_state.mode.color == CLI_COLOR_NEVER) {
        return false;
    }
    if (g_state.mode.color == CLI_COLOR_ALWAYS) {
        return true;
    }
    if (!stream_is_tty(stream)) {
        return false;
    }
    /* An empty NO_COLOR is set. Only an unset variable allows auto color. */
    // NOLINTNEXTLINE(concurrency-mt-unsafe) -- single-threaded CLI; no C11 thread-safe getenv
    no_color = getenv("NO_COLOR");
    if (no_color != NULL) {
        return false;
    }
    /* Only the exact string "dumb". Unset, empty, and "Dumb" are not dumb. */
    // NOLINTNEXTLINE(concurrency-mt-unsafe) -- single-threaded CLI; no C11 thread-safe getenv
    term = getenv("TERM");
    if (term != NULL && strcmp(term, "dumb") == 0) {
        return false;
    }
    return true;
}

static bool progress_too_soon(long long now_ms)
{
    if (!g_state.progress_seen) {
        return false;
    }
    if (now_ms < g_state.progress_last_ms) {
        return true;
    }
    return now_ms - g_state.progress_last_ms < PROGRESS_MIN_INTERVAL_MS;
}

/* cppcheck-suppress staticFunction ; public API (appio.h); tests and plan 25 */
void app_progress(const char *line, long long now_ms)
{
    if (g_state.mode.quiet) {
        return;
    }
    if (g_state.mode.json) {
        return;
    }
    if (line == NULL || line[0] == '\0') {
        return;
    }
    if (!stream_is_tty(app_err())) {
        return;
    }
    if (progress_too_soon(now_ms)) {
        return;
    }
    /* Advisory. A failed write leaves the clock alone so the next line can
       retry. It does not fail the command. */
    if (output_text_human(app_err(), line, false) != 0) {
        return;
    }
    if (fputc('\n', app_err()) == EOF) {
        return;
    }
    if (fflush(app_err()) != 0) {
        return;
    }
    g_state.progress_seen = true;
    g_state.progress_last_ms = now_ms;
}

/* cppcheck-suppress staticFunction ; public API (appio.h); tests and plan 25 */
int app_prompt_line(FILE *in, const char *prompt, char *buf, size_t cap)
{
    const char *got = NULL;
    size_t length = 0U;

    if (buf != NULL && cap > 0U) {
        buf[0] = '\0';
    }
    if (in == NULL || buf == NULL || cap < 2U || cap > (size_t)INT_MAX) {
        return -1;
    }
    /* The terminal's own echo shows the typed characters. Do not turn echo
       off, and do not write the typed line a second time. */
    if (prompt != NULL && prompt[0] != '\0') {
        if (output_text_human(app_err(), prompt, false) != 0) {
            return -1;
        }
        if (fflush(app_err()) != 0) {
            return -1;
        }
    }
    got = fgets(buf, (int)cap, in);
    if (got == NULL) {
        buf[0] = '\0';
        return -1;
    }
    length = strlen(buf);
    if (length == 0U || buf[length - 1U] != '\n') {
        buf[0] = '\0';
        return -1;
    }
    buf[length - 1U] = '\0';
    return 0;
}
