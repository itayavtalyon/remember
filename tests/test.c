#include "test.h"

#include <stdio.h>
#include <string.h>

int g_tests_run;
int g_tests_failed;
int g_asserts_run;
int g_asserts_failed;
const char *g_current_test;

static const char *current_test(void)
{
    return (g_current_test != NULL) ? g_current_test : "?";
}

static const char *or_null(const char *s)
{
    return (s != NULL) ? s : "(null)";
}

void tst_run(const char *name, void (*fn)(void))
{
    int fails_before = g_asserts_failed;

    g_current_test = name;
    g_tests_run++;
    (void)printf("RUN  %s\n", name);
    fn();
    if (g_asserts_failed > fails_before) {
        g_tests_failed++;
        (void)printf("FAIL %s (%d assert(s))\n", name, g_asserts_failed - fails_before);
    } else {
        (void)printf("PASS %s\n", name);
    }
}

void tst_assert_true(bool cond, const char *expr, const char *file, int line)
{
    g_asserts_run++;
    if (!cond) {
        g_asserts_failed++;
        (void)fprintf(stderr, "FAIL %s (%s:%d): ASSERT_TRUE(%s)\n", current_test(), file, line,
                      expr);
    }
}

void tst_assert_eq_int(long long actual, long long expected, const char *file, int line)
{
    g_asserts_run++;
    if (actual != expected) {
        g_asserts_failed++;
        (void)fprintf(stderr, "FAIL %s (%s:%d): ASSERT_EQ_INT(actual=%lld, expected=%lld)\n",
                      current_test(), file, line, actual, expected);
    }
}

void tst_assert_streq(const char *a, const char *b, const char *file, int line)
{
    g_asserts_run++;
    if (a == NULL || b == NULL || strcmp(a, b) != 0) {
        g_asserts_failed++;
        (void)fprintf(stderr, "FAIL %s (%s:%d): ASSERT_STREQ(\"%s\", \"%s\")\n", current_test(),
                      file, line, or_null(a), or_null(b));
    }
}

static bool substring_present(const char *haystack, const char *needle)
{
    if (haystack == NULL || needle == NULL) {
        return false;
    }
    if (strstr(haystack, needle) != NULL) {
        return true;
    }
    return false;
}

void tst_assert_contains(const char *haystack, const char *needle, bool want, const char *file,
                         int line)
{
    bool found = substring_present(haystack, needle);
    const char *kind = "STR_CONTAINS";

    if (!want) {
        kind = "STR_NOT_CONTAINS";
    }
    g_asserts_run++;
    if (found != want) {
        g_asserts_failed++;
        (void)fprintf(stderr, "FAIL %s (%s:%d): ASSERT_%s(\"%s\", \"%s\")\n", current_test(), file,
                      line, kind, or_null(haystack), or_null(needle));
    }
}
