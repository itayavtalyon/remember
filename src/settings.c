#include "settings.h"

#include "normalize.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Last: under REMEMBER_TEST_HOOKS every malloc below goes through the shared
   fault hook (decision:plan18b-shared-alloc-hook). */
#ifdef REMEMBER_TEST_HOOKS
#include "test_alloc.h" // IWYU pragma: keep
#endif

/* Two anonymous enums. The casts keep -Wanon-enum-enum-conversion quiet. */
// NOLINTNEXTLINE(readability-redundant-casting)
_Static_assert((int)SETTING_ACTOR_CAP == (int)ACTOR_JSON_CAP,
               "actor stamp holds one canonical actor");
// NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
enum { UTC_FORM_CAP = 25, MONTH_MAX = 12, DAY_MAX = 31, HOUR_MAX = 23, MINUTE_MAX = 59 };
/* Two anonymous enums. The cast keeps -Wanon-enum-enum-conversion quiet. */
// NOLINTNEXTLINE(readability-redundant-casting)
_Static_assert((int)SETTING_AT_CAP >= (int)UTC_FORM_CAP,
               "updated_at stamp holds the 24-character UTC form");

enum {
    SETTING_SEGMENT_MAX = 40,
    HELPER_ID_MAX = 40,
    DECIMAL_BASE = 10,
    KEY_MIN_SEGMENTS = 2,
    KEY_MAX_SEGMENTS = 4
};

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
    {.key = "remember.buckets.prefixes",
     .description = "The tag prefixes that group memories into buckets.",
     .type = SETTING_STRING_LIST,
     .allow_brain = true},
    {.key = "remember.sync.credential_helper",
     .description = "The program that gives sync its access token.",
     .type = SETTING_STRING,
     .allow_user = true},
    {.key = "remember.search.candidate_pool",
     .type = SETTING_INTEGER,
     .default_text = "200",
     .description = "How many keyword matches search looks at before filters rank them.",
     .related = "remember.search.max_results",
     .min_value = 20,
     .max_value = 2000,
     .allow_brain = true,
     .allow_user = true},
    {.key = "remember.search.max_results",
     .type = SETTING_INTEGER,
     .default_text = "20",
     .description = "How many results search shows after filters rank them.",
     .related = "remember.search.candidate_pool",
     .min_value = 1,
     .max_value = 1000,
     .allow_brain = true,
     .allow_user = true},
    {.key = "remember.plugins.chain_budget",
     .type = SETTING_DURATION,
     .default_text = "1.5s",
     .description = "How long all plugin filters together may run for one command.",
     .min_value = 100,
     .max_value = 30000,
     .allow_user = true},
    {.key = "remember.plugins.idle_timeout",
     .type = SETTING_DURATION,
     .default_text = "10m",
     .description = "How long a plugin may sit idle before remember stops it.",
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

static const char k_prefix_default[] = "[\"project:\",\"status:\"]";

static void copy_msg(char *err, size_t errlen, const char *msg)
{
    if (err == NULL || errlen == 0U || msg == NULL) {
        return;
    }
    (void)snprintf(err, errlen, "%s", msg);
}

/* cppcheck-suppress staticFunction ; public API (settings.h); used by tests */
const char *setting_status_code(SettingStatus st)
{
    switch (st) {
    case SETTING_OK:
        return "";
    case SETTING_ERR_UNKNOWN:
        return "setting_unknown";
    case SETTING_ERR_INVALID:
        return "setting_invalid_value";
    case SETTING_ERR_RANGE:
        return "setting_out_of_range";
    case SETTING_ERR_SCOPE:
        return "setting_scope_mismatch";
    case SETTING_ERR_QUOTA:
        return "quota_exceeded";
    case SETTING_ERR_DOCUMENT:
        return "import_parse";
    case SETTING_ERR_STORE:
    default:
        /* STORE has no settings code. An unknown status shares that empty token. */
        return "";
    }
}

static bool json_quote(const char *src, char *out, size_t cap)
{
    size_t n = 0;

    if (src == NULL || out == NULL) {
        return false;
    }
    n = strlen(src);
    if (cap < n + 3U) {
        return false;
    }
    out[0] = '"';
    memcpy(out + 1, src, n);
    out[n + 1U] = '"';
    out[n + 2U] = '\0';
    return true;
}

/* cppcheck-suppress staticFunction ; public API (settings.h); credential lookup */
bool helper_id_ok(const char *src)
{
    size_t i = 0;
    size_t n = 0;

    if (src == NULL || src[0] < 'a' || src[0] > 'z') {
        return false;
    }
    n = strlen(src);
    if (n > (size_t)HELPER_ID_MAX) {
        return false;
    }
    for (i = 1; i < n; i++) {
        char c = src[i];

        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            continue;
        }
        return false;
    }
    return true;
}

static void default_json(const SettingSpec *spec, char *out, size_t cap, bool *has)
{
    *has = false;
    if (out != NULL && cap > 0U) {
        out[0] = '\0';
    }
    if (spec == NULL || out == NULL) {
        return;
    }
    if (spec->type == SETTING_STRING_LIST) {
        if (cap <= strlen(k_prefix_default)) {
            return;
        }
        memcpy(out, k_prefix_default, strlen(k_prefix_default) + 1U);
        *has = true;
        return;
    }
    if (spec->default_text == NULL) {
        return;
    }
    if (spec->type == SETTING_DURATION) {
        if (!json_quote(spec->default_text, out, cap)) {
            return;
        }
        *has = true;
        return;
    }
    if (cap <= strlen(spec->default_text)) {
        return;
    }
    memcpy(out, spec->default_text, strlen(spec->default_text) + 1U);
    *has = true;
}

/* cppcheck-suppress staticFunction ; public API (settings.h); used by tests */
bool setting_default_json(const SettingSpec *spec, char *out, size_t cap)
{
    bool has = false;

    default_json(spec, out, cap, &has);
    return has;
}

static SettingStatus canon_integer(const SettingSpec *spec, const char *src, char *json,
                                   size_t json_cap)
{
    long long v = 0;

    if (!setting_integer(src, &v)) {
        return SETTING_ERR_INVALID;
    }
    if (v < spec->min_value || v > spec->max_value) {
        return SETTING_ERR_RANGE;
    }
    /* The caller passes SETTING_VALUE_CAP. A decimal long long fits in it. */
    (void)snprintf(json, json_cap, "%lld", v);
    return SETTING_OK;
}

static SettingStatus canon_duration(const SettingSpec *spec, const char *src, char *json,
                                    size_t json_cap)
{
    char canon[NORM_MEASURE_CANON_CAP];
    long long ms = 0;

    if (!norm_duration_canonical(src, &ms, canon, sizeof(canon))) {
        return SETTING_ERR_INVALID;
    }
    if (ms < spec->min_value || ms > spec->max_value) {
        return SETTING_ERR_RANGE;
    }
    /* Canonical durations fit in SETTING_VALUE_CAP, which the caller passes. */
    (void)json_quote(canon, json, json_cap);
    return SETTING_OK;
}

static SettingStatus canon_helper(const char *src, char *json, size_t json_cap)
{
    if (!helper_id_ok(src)) {
        return SETTING_ERR_INVALID;
    }
    /* A helper id is at most 40 characters. SETTING_VALUE_CAP holds the quotes. */
    (void)json_quote(src, json, json_cap);
    return SETTING_OK;
}

static void prefix_bad_item(const char *item, char *err, size_t errlen)
{
    const char *shown = "";

    if (item != NULL) {
        shown = item;
    }
    if (err != NULL && errlen > 0U) {
        (void)snprintf(err, errlen, "\"%s\" isn't a bucket prefix.", shown);
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the new item, then the earlier one
static void prefix_dup_item(const char *item, const char *earlier, char *err, size_t errlen)
{
    if (err == NULL || errlen == 0U) {
        return;
    }
    if (strcmp(item, earlier) == 0) {
        (void)snprintf(err, errlen, "\"%s\" is in the list twice.", item);
        return;
    }
    (void)snprintf(err, errlen, "\"%s\" and \"%s\" are the same prefix.", item, earlier);
}

static bool prefix_already_folded(const char *block, size_t i, const char *folded, size_t *at)
{
    size_t j = 0;

    for (j = 0; j < i; j++) {
        if (strcmp(block + (j * (size_t)REMEMBER_BUCKET_PREFIX_BUF), folded) == 0) {
            *at = j;
            return true;
        }
    }
    return false;
}

static SettingStatus append_prefix_json(char *json, size_t *pos, size_t json_cap,
                                        const char *folded, bool comma)
{
    size_t flen = strlen(folded);
    size_t need = flen + 2U;
    size_t at = *pos;

    if (comma) {
        need += 1U;
    }
    if (at + need + 1U >= json_cap) {
        return SETTING_ERR_QUOTA;
    }
    if (comma) {
        json[at++] = ',';
    }
    json[at++] = '"';
    memcpy(json + at, folded, flen + 1U);
    json[at + flen] = '"';
    at += flen + 1U;
    *pos = at;
    return SETTING_OK;
}

static SettingStatus canon_prefixes(const char *const *items, size_t nitems, char *json,
                                    size_t json_cap, char *err, size_t errlen)
{
    enum { PREFIX_JSON_MIN = 5 };
    char *block = NULL;
    size_t i = 0;
    size_t pos = 0;

    if (nitems > (size_t)SETTING_VALUE_MAX / (size_t)PREFIX_JSON_MIN) {
        return SETTING_ERR_QUOTA;
    }
    if (nitems > 0U) {
        block = (char *)malloc(nitems * (size_t)REMEMBER_BUCKET_PREFIX_BUF);
        if (block == NULL) {
            copy_msg(err, errlen, "out of memory");
            return SETTING_ERR_STORE;
        }
    }
    json[pos++] = '[';
    for (i = 0; i < nitems; i++) {
        char *folded = block + (i * (size_t)REMEMBER_BUCKET_PREFIX_BUF);
        size_t earlier = 0;
        SettingStatus st = SETTING_OK;
        bool comma = false;

        if (items[i] == NULL ||
            bucket_prefix_fold(items[i], folded, REMEMBER_BUCKET_PREFIX_BUF) != NORM_OK) {
            prefix_bad_item(items[i], err, errlen);
            free(block);
            return SETTING_ERR_INVALID;
        }
        if (prefix_already_folded(block, i, folded, &earlier)) {
            prefix_dup_item(items[i], items[earlier], err, errlen);
            free(block);
            return SETTING_ERR_INVALID;
        }
        if (i > 0U) {
            comma = true;
        }
        st = append_prefix_json(json, &pos, json_cap, folded, comma);
        if (st != SETTING_OK) {
            free(block);
            return st;
        }
    }
    json[pos++] = ']';
    json[pos] = '\0';
    free(block);
    if (pos > (size_t)SETTING_VALUE_MAX) {
        json[0] = '\0';
        return SETTING_ERR_QUOTA;
    }
    return SETTING_OK;
}

static SettingStatus setting_canon(const SettingSpec *spec, const char *const *items, size_t nitems,
                                   char *json, size_t json_cap, char *err, size_t errlen)
{
    if (json != NULL && json_cap > 0U) {
        json[0] = '\0';
    }
    if (spec == NULL || json == NULL || json_cap < (size_t)SETTING_VALUE_CAP) {
        copy_msg(err, errlen, "internal error");
        return SETTING_ERR_STORE;
    }
    if (spec->type == SETTING_STRING_LIST) {
        if (nitems > 0U && items == NULL) {
            return SETTING_ERR_INVALID;
        }
        return canon_prefixes(items, nitems, json, json_cap, err, errlen);
    }
    if (items == NULL || nitems != 1U || items[0] == NULL) {
        return SETTING_ERR_INVALID;
    }
    if (spec->type == SETTING_INTEGER) {
        return canon_integer(spec, items[0], json, json_cap);
    }
    if (spec->type == SETTING_DURATION) {
        return canon_duration(spec, items[0], json, json_cap);
    }
    return canon_helper(items[0], json, json_cap);
}

static SettingStatus setting_layer_of(const SettingSpec *spec, SettingLayer requested,
                                      SettingLayer *out)
{
    SettingLayer layer = requested;

    if (requested == SETTING_LAYER_AUTO) {
        if (spec->allow_user) {
            layer = SETTING_LAYER_USER;
        } else {
            layer = SETTING_LAYER_BRAIN;
        }
    }
    if ((layer == SETTING_LAYER_USER && !spec->allow_user) ||
        (layer == SETTING_LAYER_BRAIN && !spec->allow_brain)) {
        return SETTING_ERR_SCOPE;
    }
    *out = layer;
    return SETTING_OK;
}

/* cppcheck-suppress staticFunction ; public API (settings.h); used by tests */
SettingStatus setting_choose_layer(const char *key, SettingLayer requested,
                                   const SettingSpec **spec_out, SettingLayer *layer_out, char *err,
                                   size_t errlen)
{
    const SettingSpec *spec = NULL;

    if (err != NULL && errlen > 0U) {
        err[0] = '\0';
    }
    if (spec_out != NULL) {
        *spec_out = NULL;
    }
    spec = setting_by_key(key);
    if (spec == NULL) {
        return SETTING_ERR_UNKNOWN;
    }
    if (spec_out != NULL) {
        *spec_out = spec;
    }
    if (layer_out == NULL) {
        copy_msg(err, errlen, "internal error");
        return SETTING_ERR_STORE;
    }
    return setting_layer_of(spec, requested, layer_out);
}

/* cppcheck-suppress staticFunction ; public API (settings.h); used by tests */
/* nitems (size_t) sits beside SettingLayer. That order is the public signature. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
SettingStatus setting_prepare(const char *key, const char *const *items, size_t nitems,
                              SettingLayer requested, char *json, size_t json_cap,
                              const SettingSpec **spec_out, SettingLayer *layer_out, char *err,
                              size_t errlen)
{
    const SettingSpec *spec = NULL;
    SettingStatus st = SETTING_OK;

    st = setting_choose_layer(key, requested, &spec, layer_out, err, errlen);
    if (st != SETTING_OK) {
        return st;
    }
    if (spec_out != NULL) {
        *spec_out = spec;
    }
    return setting_canon(spec, items, nitems, json, json_cap, err, errlen);
}

static bool copy_cap(const char *src, char *out, size_t cap)
{
    size_t n = 0;

    if (out == NULL || cap == 0U || src == NULL) {
        return false;
    }
    n = strlen(src);
    if (n >= cap) {
        return false;
    }
    memcpy(out, src, n + 1U);
    return true;
}

/* cppcheck-suppress staticFunction ; public API (settings.h); used by tests */
SettingFrom setting_pick(const SettingSpec *spec, const char *user_json, const char *brain_json,
                         const char *flag_json, char *out, size_t out_cap)
{
    bool has = false;
    char fallback[SETTING_VALUE_CAP];

    if (out != NULL && out_cap > 0U) {
        out[0] = '\0';
    }
    if (flag_json != NULL) {
        (void)copy_cap(flag_json, out, out_cap);
        return SETTING_FROM_FLAG;
    }
    /* A row in a layer the key does not allow (a synced or hand-edited brain)
       is no value. */
    if (brain_json != NULL && spec->allow_brain) {
        (void)copy_cap(brain_json, out, out_cap);
        return SETTING_FROM_BRAIN;
    }
    if (user_json != NULL && spec->allow_user) {
        (void)copy_cap(user_json, out, out_cap);
        return SETTING_FROM_USER;
    }
    default_json(spec, fallback, sizeof(fallback), &has);
    if (has) {
        (void)copy_cap(fallback, out, out_cap);
    }
    return SETTING_FROM_DEFAULT;
}

/* cppcheck-suppress staticFunction ; public API (settings.h); used by tests */
bool setting_format_when(SettingWhen when, char *buf, size_t cap)
{
    static const char *const k_months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int n = 0;

    if (buf == NULL || cap == 0U) {
        return false;
    }
    buf[0] = '\0';
    if (when.month < 1 || when.month > MONTH_MAX || when.day < 1 || when.day > DAY_MAX ||
        when.hour < 0 || when.hour > HOUR_MAX || when.minute < 0 || when.minute > MINUTE_MAX) {
        return false;
    }
    if (when.year != when.year_now) {
        n = snprintf(buf, cap, "%d %s %d at %02d:%02d", when.day, k_months[when.month - 1],
                     when.year, when.hour, when.minute);
    } else {
        n = snprintf(buf, cap, "%d %s at %02d:%02d", when.day, k_months[when.month - 1], when.hour,
                     when.minute);
    }
    if (n < 0 || (size_t)n >= cap) {
        return false;
    }
    if (when.utc_suffix) {
        int extra = snprintf(buf + n, cap - (size_t)n, " UTC");

        if (extra < 0 || (size_t)n + (size_t)extra >= cap) {
            return false;
        }
    }
    return true;
}

/* Length diff above 2 cannot be within distance 2. Inputs longer than a key
   plus 2 cannot either. Both skip with continue so the distance itself stays
   the value the caller compares. */
static int edit_distance(const char *a, const char *b)
{
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    unsigned int prev[SETTING_KEY_MAX + 3];
    unsigned int cur[SETTING_KEY_MAX + 3];
    size_t i = 0;
    size_t j = 0;

    for (j = 0; j <= len_b; j++) {
        prev[j] = (unsigned int)j;
    }
    for (i = 1; i <= len_a; i++) {
        cur[0] = (unsigned int)i;
        for (j = 1; j <= len_b; j++) {
            unsigned int cost = (a[i - 1U] == b[j - 1U]) ? 0U : 1U;
            unsigned int del = prev[j] + 1U;
            unsigned int ins = cur[j - 1U] + 1U;
            unsigned int sub = prev[j - 1U] + cost;
            unsigned int best = del;

            if (ins < best) {
                best = ins;
            }
            if (sub < best) {
                best = sub;
            }
            cur[j] = best;
        }
        memcpy(prev, cur, (len_b + 1U) * sizeof(prev[0]));
    }
    return (int)prev[len_b];
}

/* cppcheck-suppress staticFunction ; public API (settings.h); used by tests */
const char *setting_suggestion(const char *typed)
{
    size_t nregs = 0;
    const SettingSpec *specs = setting_registry(&nregs);
    const char *hit = NULL;
    size_t hits = 0;
    size_t i = 0;
    size_t n = 0;
    bool comparable = false;

    if (typed == NULL || typed[0] == '\0') {
        return NULL;
    }
    for (i = 0; i < nregs; i++) {
        const char *dot = strrchr(specs[i].key, '.');

        if (dot != NULL && strcmp(dot + 1, typed) == 0) {
            hits++;
            hit = specs[i].key;
        }
    }
    if (hits == 1U) {
        return hit;
    }
    /* ST-1.5 measures the typed bytes. A colon is not a dot. */
    n = strlen(typed);
    comparable = n <= (size_t)SETTING_KEY_MAX + 2U;
    hits = 0U;
    hit = NULL;
    for (i = 0; i < nregs; i++) {
        size_t nb = strlen(specs[i].key);
        int dist = 0;

        if (!comparable || n > nb + 2U || nb > n + 2U) {
            continue;
        }
        dist = edit_distance(typed, specs[i].key);
        if (dist <= 2) {
            hits++;
            hit = specs[i].key;
        }
    }
    if (hits == 1U) {
        return hit;
    }
    return NULL;
}
