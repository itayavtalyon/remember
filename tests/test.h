#ifndef REMEMBER_TEST_H
#define REMEMBER_TEST_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Collect-all failures so the suite reports every gap at once. */

extern int g_tests_run;
extern int g_tests_failed;
extern int g_asserts_run;
extern int g_asserts_failed;
extern const char *g_current_test;

#define TEST(name) static void test_##name(void)

#define RUN_TEST(name)                                                         \
    do {                                                                       \
        int _fails_before = g_asserts_failed;                                  \
        g_current_test = #name;                                                \
        g_tests_run++;                                                         \
        printf("RUN  %s\n", #name);                                            \
        test_##name();                                                         \
        if (g_asserts_failed > _fails_before) {                                \
            g_tests_failed++;                                                  \
            printf("FAIL %s (%d assert(s))\n", #name,                          \
                   g_asserts_failed - _fails_before);                          \
        } else {                                                               \
            printf("PASS %s\n", #name);                                        \
        }                                                                      \
    } while (0)

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                       \
        g_asserts_run++;                                                       \
        if (!(cond)) {                                                         \
            g_asserts_failed++;                                                \
            fprintf(stderr, "FAIL %s (%s:%d): ASSERT_TRUE(%s)\n",              \
                    g_current_test ? g_current_test : "?", __FILE__, __LINE__, \
                    #cond);                                                    \
        }                                                                      \
    } while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ_INT(a, b)                                                    \
    do {                                                                       \
        g_asserts_run++;                                                       \
        long long _a = (long long)(a);                                         \
        long long _b = (long long)(b);                                         \
        if (_a != _b) {                                                        \
            g_asserts_failed++;                                                \
            fprintf(stderr,                                                    \
                    "FAIL %s (%s:%d): ASSERT_EQ_INT(%lld, %lld)\n",            \
                    g_current_test ? g_current_test : "?", __FILE__, __LINE__, \
                    _a, _b);                                                   \
        }                                                                      \
    } while (0)

#define ASSERT_STREQ(a, b)                                                     \
    do {                                                                       \
        g_asserts_run++;                                                       \
        const char *_a = (a);                                                  \
        const char *_b = (b);                                                  \
        if (_a == NULL || _b == NULL || strcmp(_a, _b) != 0) {                 \
            g_asserts_failed++;                                                \
            fprintf(stderr,                                                    \
                    "FAIL %s (%s:%d): ASSERT_STREQ(\"%s\", \"%s\")\n",         \
                    g_current_test ? g_current_test : "?", __FILE__, __LINE__, \
                    _a ? _a : "(null)", _b ? _b : "(null)");                   \
        }                                                                      \
    } while (0)

#define ASSERT_STR_CONTAINS(haystack, needle)                                  \
    do {                                                                       \
        g_asserts_run++;                                                       \
        const char *_h = (haystack);                                           \
        const char *_n = (needle);                                             \
        if (_h == NULL || _n == NULL || strstr(_h, _n) == NULL) {              \
            g_asserts_failed++;                                                \
            fprintf(stderr,                                                    \
                    "FAIL %s (%s:%d): ASSERT_STR_CONTAINS(\"%s\", \"%s\")\n",  \
                    g_current_test ? g_current_test : "?", __FILE__, __LINE__, \
                    _h ? _h : "(null)", _n ? _n : "(null)");                   \
        }                                                                      \
    } while (0)

#define ASSERT_STR_NOT_CONTAINS(haystack, needle)                              \
    do {                                                                       \
        g_asserts_run++;                                                       \
        const char *_h = (haystack);                                           \
        const char *_n = (needle);                                             \
        if (_h != NULL && _n != NULL && strstr(_h, _n) != NULL) {              \
            g_asserts_failed++;                                                \
            fprintf(stderr,                                                    \
                    "FAIL %s (%s:%d): ASSERT_STR_NOT_CONTAINS(\"%s\", "        \
                    "\"%s\")\n",                                               \
                    g_current_test ? g_current_test : "?", __FILE__, __LINE__, \
                    _h, _n);                                                   \
        }                                                                      \
    } while (0)

#endif /* REMEMBER_TEST_H */
