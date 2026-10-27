#include "normalize.h"
#include "register.h"
#include "settings.h"
#include "test.h"

#include <stdio.h>
#include <string.h>

/* The numbers are the plan's examples (1500ms, 1.5KiB, the registry bounds).
   A name for each one hides that table. */
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

enum { SEGMENT_MAX = 40 };

enum { CANON = NORM_MEASURE_CANON_CAP };

static void assert_duration(const char *src, long long ms, const char *canon)
{
    char out[CANON];
    long long got = -1;

    ASSERT_TRUE(norm_duration_canonical(src, &got, out, sizeof(out)));
    ASSERT_EQ_INT(got, ms);
    ASSERT_STREQ(out, canon);
}

static void assert_size(const char *src, long long bytes, const char *canon)
{
    char out[CANON];
    long long got = -1;

    ASSERT_TRUE(norm_size_canonical(src, &got, out, sizeof(out)));
    ASSERT_EQ_INT(got, bytes);
    ASSERT_STREQ(out, canon);
}

TEST(duration_canonical_examples)
{
    assert_duration("1500ms", 1500, "1.5s");
    assert_duration("90s", 90000, "1.5m");
    assert_duration("61s", 61000, "61s");
    assert_duration("100ms", 100, "100ms");
    assert_duration("1.5s", 1500, "1.5s");
    assert_duration("2.50s", 2500, "2.5s");
    assert_duration("2.00s", 2000, "2s");
    assert_duration("0ms", 0, "0ms");
    assert_duration("0.5s", 500, "500ms");
    assert_duration("01s", 1000, "1s");
    assert_duration("1000ms", 1000, "1s");
    assert_duration("60000ms", 60000, "1m");
    assert_duration("3600000ms", 3600000, "1h");
    assert_duration("1.001s", 1001, "1.001s");
    assert_duration("1.010s", 1010, "1.01s");
    assert_duration("1ms", 1, "1ms");
    assert_duration("1m", 60000, "1m");
    assert_duration("10m", 600000, "10m");
    assert_duration("90m", 5400000, "1.5h");
    assert_duration("1.5h", 5400000, "1.5h");
    assert_duration("61m", 3660000, "61m");
    assert_duration("0s", 0, "0ms");
    assert_duration("0h", 0, "0ms");
    assert_duration("1.000s", 1000, "1s");
}

TEST(duration_rejects_what_is_not_a_whole_millisecond)
{
    char out[CANON];
    long long got = 1;
    char tiny[CANON - 1];

    memset(out, 'x', sizeof(out) - 1U);
    out[sizeof(out) - 1U] = '\0';
    ASSERT_FALSE(norm_duration_canonical(NULL, &got, out, sizeof(out)));
    ASSERT_STREQ(out, "");
    ASSERT_EQ_INT(got, 1);
    memset(out, 'x', sizeof(out) - 1U);
    out[sizeof(out) - 1U] = '\0';
    ASSERT_FALSE(norm_duration_canonical("1s", NULL, out, sizeof(out)));
    ASSERT_STREQ(out, "");
    ASSERT_EQ_INT(got, 1);
    ASSERT_FALSE(norm_duration_canonical("1s", &got, NULL, sizeof(out)));
    memset(tiny, 'Q', sizeof(tiny));
    ASSERT_FALSE(norm_duration_canonical("1s", &got, tiny, sizeof(tiny)));
    ASSERT_EQ_INT((unsigned char)tiny[0], 'Q');
    ASSERT_FALSE(norm_duration_canonical("0.5ms", &got, out, sizeof(out)));
    ASSERT_STREQ(out, "");
    ASSERT_FALSE(norm_duration_canonical("1.s", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("1.", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical(".5s", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("1.5", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("1.5x", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("1mx", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("s", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("99999999999999999999s", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("9223372036854775807s", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("0.999999999999999h", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("9223372036854775.9s", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("1.1234567890123456s", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("+1s", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("1 s", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("1S", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("1.2.3s", &got, out, sizeof(out)));
    ASSERT_EQ_INT(got, 1);
}

TEST(size_canonical_examples)
{
    assert_size("1536B", 1536, "1.5KiB");
    assert_size("1024B", 1024, "1KiB");
    assert_size("512B", 512, "512B");
    assert_size("1025B", 1025, "1025B");
    assert_size("1048576B", 1048576, "1MiB");
    assert_size("1.5KiB", 1536, "1.5KiB");
    assert_size("1.5MiB", 1572864, "1.5MiB");
    assert_size("0.5KiB", 512, "512B");
    assert_size("0B", 0, "0B");
    assert_size("2.00KiB", 2048, "2KiB");
    assert_size("1024KiB", 1048576, "1MiB");
    assert_size("1536KiB", 1572864, "1.5MiB");
    assert_size("0KiB", 0, "0B");
    assert_size("0MiB", 0, "0B");
}

TEST(size_rejects_a_fraction_of_a_byte)
{
    char out[CANON];
    long long got = 4;

    ASSERT_FALSE(norm_size_canonical("0.5B", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_size_canonical("1X", &got, out, sizeof(out)));
    ASSERT_STREQ(out, "");
    ASSERT_EQ_INT(got, 4);
}

TEST(integer_grammar)
{
    long long v = -1;

    ASSERT_TRUE(setting_integer("0", &v));
    ASSERT_EQ_INT(v, 0);
    ASSERT_TRUE(setting_integer("200", &v));
    ASSERT_EQ_INT(v, 200);
    ASSERT_TRUE(setting_integer("9223372036854775807", &v));
    ASSERT_EQ_INT(v, 9223372036854775807LL);
    ASSERT_FALSE(setting_integer("9223372036854775808", &v));
    ASSERT_FALSE(setting_integer("00", &v));
    ASSERT_FALSE(setting_integer("01", &v));
    ASSERT_FALSE(setting_integer("-1", &v));
    ASSERT_FALSE(setting_integer("+1", &v));
    ASSERT_FALSE(setting_integer("1.0", &v));
    ASSERT_FALSE(setting_integer("1a", &v));
    ASSERT_FALSE(setting_integer("", &v));
    ASSERT_FALSE(setting_integer(NULL, &v));
    ASSERT_FALSE(setting_integer("1", NULL));
    ASSERT_EQ_INT(v, 9223372036854775807LL);
}

TEST(key_grammar_folds_hyphens_and_requires_a_full_key)
{
    char out[SETTING_KEY_MAX + 1];
    char segment[SEGMENT_MAX + 2];
    char key[SETTING_KEY_MAX + 2];
    char small[SETTING_KEY_MAX];
    int n = 0;

    ASSERT_TRUE(setting_key_fold("remember.buckets.prefixes", out, sizeof(out)));
    ASSERT_STREQ(out, "remember.buckets.prefixes");
    ASSERT_TRUE(setting_key_fold("remember.search.candidate-pool", out, sizeof(out)));
    ASSERT_STREQ(out, "remember.search.candidate_pool");
    ASSERT_TRUE(setting_key_fold("remember.search.a1_b", out, sizeof(out)));
    ASSERT_TRUE(setting_key_fold("remember.a.b.c", out, sizeof(out)));

    memset(segment, 'b', (size_t)SEGMENT_MAX);
    segment[SEGMENT_MAX] = '\0';
    n = snprintf(key, sizeof(key), "remember.%s", segment);
    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(key));
    ASSERT_TRUE(setting_key_fold(key, out, sizeof(out)));

    memset(out, 'x', sizeof(out));
    ASSERT_FALSE(setting_key_fold("remember", out, sizeof(out)));
    ASSERT_STREQ(out, "");
    ASSERT_FALSE(setting_key_fold("remember.a.b.c.d", out, sizeof(out)));
    ASSERT_FALSE(setting_key_fold("Remember.buckets.prefixes", out, sizeof(out)));
    ASSERT_FALSE(setting_key_fold("remember..x", out, sizeof(out)));
    ASSERT_FALSE(setting_key_fold("remember.", out, sizeof(out)));
    ASSERT_FALSE(setting_key_fold(".remember.x", out, sizeof(out)));
    ASSERT_FALSE(setting_key_fold("remember.1abc", out, sizeof(out)));
    memset(out, 'x', sizeof(out));
    ASSERT_FALSE(setting_key_fold("remember.abC", out, sizeof(out)));
    ASSERT_STREQ(out, "");
    memset(out, 'x', sizeof(out));
    ASSERT_FALSE(setting_key_fold("remember.ab~", out, sizeof(out)));
    ASSERT_STREQ(out, "");
    memset(out, 'x', sizeof(out));
    ASSERT_FALSE(setting_key_fold("remember.ab:", out, sizeof(out)));
    ASSERT_STREQ(out, "");
    memset(out, 'x', sizeof(out));
    ASSERT_FALSE(setting_key_fold("remember.~a", out, sizeof(out)));
    ASSERT_STREQ(out, "");
    ASSERT_FALSE(setting_key_fold("remember.-a", out, sizeof(out)));
    ASSERT_FALSE(setting_key_fold("", out, sizeof(out)));
    ASSERT_FALSE(setting_key_fold(NULL, out, sizeof(out)));
    ASSERT_FALSE(setting_key_fold("remember.x", NULL, sizeof(out)));
    ASSERT_FALSE(setting_key_fold("remember.x", small, sizeof(small)));
    {
        char one = 'Q';

        ASSERT_FALSE(setting_key_fold("remember.x", &one, 0));
        ASSERT_EQ_INT(one, 'Q');
    }

    memset(segment, 'b', (size_t)SEGMENT_MAX + 1U);
    segment[SEGMENT_MAX + 1U] = '\0';
    n = snprintf(key, sizeof(key), "remember.%s", segment);
    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(key));
    ASSERT_FALSE(setting_key_fold(key, out, sizeof(out)));

    /* 96 fits: remember. + 40 + . + 40 + . + 5. One more character is 97. */
    n = snprintf(key, sizeof(key), "remember.%.*s.%.*s.ccccc", SEGMENT_MAX, segment, SEGMENT_MAX,
                 segment);
    ASSERT_EQ_INT(n, 96);
    ASSERT_TRUE(setting_key_fold(key, out, sizeof(out)));
    ASSERT_STREQ(out, key);
    n = snprintf(key, sizeof(key), "remember.%.*s.%.*s.cccccc", SEGMENT_MAX, segment, SEGMENT_MAX,
                 segment);
    ASSERT_EQ_INT(n, 97);
    ASSERT_FALSE(setting_key_fold(key, out, sizeof(out)));
}

TEST(registry_lists_the_six_keys)
{
    size_t n = 0;
    const SettingSpec *all = setting_registry(&n);
    const SettingSpec *pool = NULL;
    const SettingSpec *prefixes = NULL;
    const SettingSpec *budget = NULL;
    const SettingSpec *max_results = NULL;
    const SettingSpec *idle = NULL;
    const SettingSpec *helper = NULL;

    ASSERT_EQ_INT((long long)n, 6);
    ASSERT_TRUE(setting_registry(NULL) == all);
    pool = setting_by_key("remember.search.candidate-pool");
    ASSERT_TRUE(pool != NULL);
    if (pool == NULL) {
        return;
    }
    ASSERT_EQ_INT(pool->type, SETTING_INTEGER);
    ASSERT_STREQ(pool->default_text, "200");
    ASSERT_EQ_INT(pool->min_value, 20);
    ASSERT_EQ_INT(pool->max_value, 2000);
    ASSERT_TRUE(pool->allow_brain);
    ASSERT_TRUE(pool->allow_user);
    prefixes = setting_by_key("remember.buckets.prefixes");
    ASSERT_TRUE(prefixes != NULL);
    if (prefixes == NULL) {
        return;
    }
    ASSERT_EQ_INT(prefixes->type, SETTING_STRING_LIST);
    ASSERT_TRUE(prefixes->default_text == NULL);
    ASSERT_TRUE(prefixes->allow_brain);
    ASSERT_FALSE(prefixes->allow_user);
    budget = setting_by_key("remember.plugins.chain_budget");
    ASSERT_TRUE(budget != NULL);
    if (budget == NULL) {
        return;
    }
    ASSERT_EQ_INT(budget->type, SETTING_DURATION);
    ASSERT_STREQ(budget->default_text, "1.5s");
    ASSERT_EQ_INT(budget->min_value, 100);
    ASSERT_EQ_INT(budget->max_value, 30000);
    ASSERT_FALSE(budget->allow_brain);
    ASSERT_TRUE(budget->allow_user);
    max_results = setting_by_key("remember.search.max_results");
    ASSERT_TRUE(max_results != NULL);
    if (max_results == NULL) {
        return;
    }
    ASSERT_EQ_INT(max_results->type, SETTING_INTEGER);
    ASSERT_STREQ(max_results->default_text, "20");
    ASSERT_EQ_INT(max_results->min_value, 1);
    ASSERT_EQ_INT(max_results->max_value, 1000);
    ASSERT_TRUE(max_results->allow_brain);
    ASSERT_TRUE(max_results->allow_user);
    idle = setting_by_key("remember.plugins.idle_timeout");
    ASSERT_TRUE(idle != NULL);
    if (idle == NULL) {
        return;
    }
    ASSERT_EQ_INT(idle->type, SETTING_DURATION);
    ASSERT_STREQ(idle->default_text, "10m");
    ASSERT_EQ_INT(idle->min_value, 60000);
    ASSERT_EQ_INT(idle->max_value, 86400000);
    ASSERT_FALSE(idle->allow_brain);
    ASSERT_TRUE(idle->allow_user);
    helper = setting_by_key("remember.sync.credential_helper");
    ASSERT_TRUE(helper != NULL);
    if (helper == NULL) {
        return;
    }
    ASSERT_EQ_INT(helper->type, SETTING_STRING);
    ASSERT_TRUE(helper->default_text == NULL);
    ASSERT_EQ_INT(helper->min_value, 0);
    ASSERT_EQ_INT(helper->max_value, 0);
    ASSERT_FALSE(helper->allow_brain);
    ASSERT_TRUE(helper->allow_user);
    ASSERT_TRUE(setting_by_key("remember.search.other") == NULL);
    ASSERT_TRUE(setting_by_key("candidate_pool") == NULL);
}

void register_settings_tests(void)
{
    RUN_TEST(duration_canonical_examples);
    RUN_TEST(duration_rejects_what_is_not_a_whole_millisecond);
    RUN_TEST(size_canonical_examples);
    RUN_TEST(size_rejects_a_fraction_of_a_byte);
    RUN_TEST(integer_grammar);
    RUN_TEST(key_grammar_folds_hyphens_and_requires_a_full_key);
    RUN_TEST(registry_lists_the_six_keys);
}
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
