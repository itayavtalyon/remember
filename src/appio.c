#include "appio.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
