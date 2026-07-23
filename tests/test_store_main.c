#include "register.h"
#include "test.h"

#include <stdio.h>

/*
 * Store unit tests under ASan/UBSan (and LSan on Linux CI).
 * No fork/exec harness — the process under test is instrumented directly.
 *
 * usage: remember_store_tests
 */
int main(void)
{
    register_store_tests();

    (void)printf("\n== store unit (sanitized) summary ==\n");
    (void)printf("tests run:     %d\n", g_tests_run);
    (void)printf("tests failed:  %d\n", g_tests_failed);
    (void)printf("asserts run:   %d\n", g_asserts_run);
    (void)printf("asserts fail:  %d\n", g_asserts_failed);

    if (g_tests_run == 0) {
        (void)fprintf(stderr, "no store tests registered\n");
        return 2;
    }
    return (g_asserts_failed == 0) ? 0 : 1;
}
