#include "appio.h"

#include <stdio.h>

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

void app_set_streams(FILE *out, FILE *err)
{
    g_out = out;
    g_err = err;
}
