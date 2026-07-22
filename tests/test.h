#ifndef REMEMBER_TEST_H
#define REMEMBER_TEST_H

#include <stdbool.h>

/*
 * Minimal collect-all test framework. Asserts are thin macros over functions
 * (in test.c) so test bodies stay branch-free: control flow and I/O live in one
 * place, keeping the same lint bar as src/ (no per-assert if/printf in callers).
 */

extern int g_tests_run;
extern int g_tests_failed;
extern int g_asserts_run;
extern int g_asserts_failed;
extern const char *g_current_test;

void tst_run(const char *name, void (*fn)(void));
void tst_assert_true(bool cond, const char *expr, const char *file, int line);
void tst_assert_eq_int(long long actual, long long expected, const char *file, int line);
void tst_assert_streq(const char *a, const char *b, const char *file, int line);
void tst_assert_contains(const char *haystack, const char *needle, bool want, const char *file,
                         int line);

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) tst_run(#name, test_##name)

#define ASSERT_TRUE(cond) tst_assert_true((cond), #cond, __FILE__, __LINE__)
#define ASSERT_FALSE(cond) tst_assert_true(!(cond), "!(" #cond ")", __FILE__, __LINE__)
#define ASSERT_EQ_INT(actual, expected)                                                            \
    tst_assert_eq_int((long long)(actual), (long long)(expected), __FILE__, __LINE__)
#define ASSERT_STREQ(a, b) tst_assert_streq((a), (b), __FILE__, __LINE__)
#define ASSERT_STR_CONTAINS(haystack, needle)                                                      \
    tst_assert_contains((haystack), (needle), true, __FILE__, __LINE__)
#define ASSERT_STR_NOT_CONTAINS(haystack, needle)                                                  \
    tst_assert_contains((haystack), (needle), false, __FILE__, __LINE__)

#endif /* REMEMBER_TEST_H */
