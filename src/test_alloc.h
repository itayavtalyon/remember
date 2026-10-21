#ifndef REMEMBER_TEST_ALLOC_H
#define REMEMBER_TEST_ALLOC_H

#include <stdbool.h>
#include <stdlib.h> // IWYU pragma: keep (declares malloc before the macro below)

/*
 * Test-only fault seam shared by the pure TUs (plan 18b,
 * decision:plan18b-shared-alloc-hook). test_alloc.c is linked only into
 * REMEMBER_TEST_HOOKS targets, so a Release build has none of these symbols.
 */

/* One-shot countdown: a negative *counter is off; 0 fires now and turns it off;
   n > 0 lets n more calls pass first. */
bool test_countdown_fires(int *counter);

/* The allocation after n more succeed returns NULL (0 = the next one); -1 turns it off. */
void test_alloc_fail_after(int n);
void *test_alloc_malloc(size_t n);

/* Include this header last: under REMEMBER_TEST_HOOKS every later malloc in the
   TU goes through the shared counter. */
#ifdef REMEMBER_TEST_HOOKS
#define malloc(n) test_alloc_malloc(n)
#endif

#endif /* REMEMBER_TEST_ALLOC_H */
