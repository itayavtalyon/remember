#include "test_alloc.h"

#include <stdbool.h>
#include <stdlib.h>

/* This TU is the hook: it calls the libc malloc. */
#undef malloc

/* Process-global by design: the setter is the seam the tests drive. */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static int g_fail_alloc_after = -1;

bool test_countdown_fires(int *counter)
{
    if (*counter == 0) {
        *counter = -1;
        return true;
    }
    if (*counter > 0) {
        (*counter)--;
    }
    return false;
}

void test_alloc_fail_after(int n)
{
    g_fail_alloc_after = n;
}

/* Apple clang 21 asks whether a function that returns malloc's result is an
   allocator wrapper. It is, on purpose; no attribute silences the note. Clang 18
   (Linux CI) does not know that warning, hence the first pragma. */
#ifdef __clang__
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wallocator-wrappers"
#endif
void *test_alloc_malloc(size_t n)
{
    if (test_countdown_fires(&g_fail_alloc_after)) {
        return NULL;
    }
    return malloc(n);
}
