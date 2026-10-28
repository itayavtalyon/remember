#include "settings.h"

#include <limits.h>
#include <string.h>

enum { SETTING_SEGMENT_MAX = 40, DECIMAL_BASE = 10, KEY_MIN_SEGMENTS = 2, KEY_MAX_SEGMENTS = 4 };

static bool key_body_char(char c)
{
    /* Relational ops yield int. Return an actual bool (clang-tidy). */
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
        return true;
    }
    return false;
}

static bool key_segments_ok(const char *folded, size_t n)
{
    size_t i = 0;
    size_t seg_len = 0;
    int segs = 0;
    bool at_start = true;

    for (i = 0; i < n; i++) {
        char c = folded[i];

        if (c == '.') {
            if (seg_len == 0U) {
                return false;
            }
            segs++;
            seg_len = 0U;
            at_start = true;
            continue;
        }
        if (at_start) {
            if (c < 'a' || c > 'z') {
                return false;
            }
            at_start = false;
        } else if (!key_body_char(c)) {
            return false;
        }
        if (seg_len == (size_t)SETTING_SEGMENT_MAX) {
            return false;
        }
        seg_len++;
    }
    if (seg_len == 0U) {
        return false;
    }
    segs++;
    if (segs >= KEY_MIN_SEGMENTS && segs <= KEY_MAX_SEGMENTS) {
        return true;
    }
    return false;
}

static const SettingSpec k_registry[] = {
    {.key = "remember.buckets.prefixes", .type = SETTING_STRING_LIST, .allow_brain = true},
    {.key = "remember.sync.credential_helper", .type = SETTING_STRING, .allow_user = true},
    {.key = "remember.search.candidate_pool",
     .type = SETTING_INTEGER,
     .default_text = "200",
     .min_value = 20,
     .max_value = 2000,
     .allow_brain = true,
     .allow_user = true},
    {.key = "remember.search.max_results",
     .type = SETTING_INTEGER,
     .default_text = "20",
     .min_value = 1,
     .max_value = 1000,
     .allow_brain = true,
     .allow_user = true},
    {.key = "remember.plugins.chain_budget",
     .type = SETTING_DURATION,
     .default_text = "1.5s",
     .min_value = 100,
     .max_value = 30000,
     .allow_user = true},
    {.key = "remember.plugins.idle_timeout",
     .type = SETTING_DURATION,
     .default_text = "10m",
     .min_value = 60000,
     .max_value = 86400000,
     .allow_user = true},
};

/* cppcheck-suppress staticFunction ; public API (settings.h); used by tests */
bool setting_key_fold(const char *src, char *out, size_t out_cap)
{
    size_t n = 0;
    size_t i = 0;

    if (out != NULL && out_cap > 0U) {
        out[0] = '\0';
    }
    if (src == NULL || out == NULL || out_cap < (size_t)SETTING_KEY_MAX + 1U) {
        return false;
    }
    n = strlen(src);
    if (n == 0U || n > (size_t)SETTING_KEY_MAX) {
        return false;
    }
    for (i = 0; i < n; i++) {
        out[i] = (char)((src[i] == '-') ? '_' : src[i]);
    }
    out[n] = '\0';
    if (!key_segments_ok(out, n)) {
        out[0] = '\0';
        return false;
    }
    return true;
}

/* cppcheck-suppress staticFunction ; public API (settings.h); used by tests */
bool setting_integer(const char *src, long long *out)
{
    const char *p = NULL;
    long long v = 0;

    if (src == NULL || out == NULL || src[0] == '\0') {
        return false;
    }
    if (src[0] == '0') {
        if (src[1] != '\0') {
            return false;
        }
        *out = 0;
        return true;
    }
    p = src;
    while (*p >= '0' && *p <= '9') {
        int dig = *p - '0';

        if (v > (LLONG_MAX - dig) / DECIMAL_BASE) {
            return false;
        }
        v = (v * DECIMAL_BASE) + dig;
        p++;
    }
    if (*p != '\0') {
        return false;
    }
    *out = v;
    return true;
}

/* cppcheck-suppress staticFunction ; public API (settings.h); used by tests */
const SettingSpec *setting_registry(size_t *count)
{
    if (count != NULL) {
        *count = sizeof(k_registry) / sizeof(k_registry[0]);
    }
    return k_registry;
}

/* cppcheck-suppress staticFunction ; public API (settings.h); used by tests */
const SettingSpec *setting_by_key(const char *typed)
{
    char folded[SETTING_KEY_MAX + 1];
    size_t n = 0;
    size_t i = 0;
    const SettingSpec *specs = setting_registry(&n);

    if (!setting_key_fold(typed, folded, sizeof(folded))) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        if (strcmp(specs[i].key, folded) == 0) {
            return &specs[i];
        }
    }
    return NULL;
}
