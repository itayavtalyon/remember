#include "settings.h"

#include "normalize.h"
#include "unicode.h"

#include <limits.h>
#include <stdint.h>
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
    KEY_MAX_SEGMENTS = 4,
    ASCII_UNIT = 0x20,
    ASCII_DEL = 0x7F,
    PLUGIN_DOT_LEN = 7 /* "plugin." */
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

enum { SETTING_STATIC_COUNT = sizeof(k_registry) / sizeof(k_registry[0]), REGISTRY_GROW = 8 };

/* The generated rows. Each row is one allocation (the spec, the enum value pointers, then the
   strings), so a pointer to one row stays good while other rows come and go. */
struct SettingsRegistry {
    struct GenRow **rows;
    size_t n;
    size_t cap;
};

typedef struct GenRow {
    SettingSpec spec;
    const char *values[SETTING_ENUM_VALUES_MAX];
    char text[]; /* the strings that spec and values point at */
} GenRow;

static const SettingsRegistry k_builtin_registry = {.rows = NULL, .n = 0U, .cap = 0U};

/* cppcheck-suppress staticFunction ; public API (settings.h); used by tests */
const SettingsRegistry *settings_registry_builtin(void)
{
    return &k_builtin_registry;
}

/* cppcheck-suppress staticFunction ; public API (settings.h); used by tests */
SettingsRegistry *settings_registry_new(void)
{
    SettingsRegistry *reg = (SettingsRegistry *)malloc(sizeof(*reg));

    if (reg != NULL) {
        memset(reg, 0, sizeof(*reg));
    }
    return reg;
}

/* cppcheck-suppress staticFunction ; public API (settings.h); used by tests */
void settings_registry_free(SettingsRegistry *reg)
{
    size_t i = 0;

    if (reg == NULL) {
        return;
    }
    for (i = 0; i < reg->n; i++) {
        free(reg->rows[i]);
    }
    free((void *)reg->rows);
    free(reg);
}

/* cppcheck-suppress staticFunction ; public API (settings.h); used by tests */
size_t settings_registry_count(const SettingsRegistry *reg)
{
    return reg == NULL ? 0U : (size_t)SETTING_STATIC_COUNT + reg->n;
}

/* cppcheck-suppress staticFunction ; public API (settings.h); used by tests */
const SettingSpec *settings_registry_at(const SettingsRegistry *reg, size_t i)
{
    if (i >= settings_registry_count(reg)) {
        return NULL;
    }
    if (i < (size_t)SETTING_STATIC_COUNT) {
        return &k_registry[i];
    }
    return &reg->rows[i - (size_t)SETTING_STATIC_COUNT]->spec;
}

/* ponytail: a linear scan, so building a registry is quadratic in its rows. 1000 plugins of 17
   keys each cost about 0.5 s per command (plan 30 review); sort the generated rows and search
   them by halves if anyone installs that many. */
static const SettingSpec *registry_find(const SettingsRegistry *reg, const char *folded)
{
    size_t n = settings_registry_count(reg);
    size_t i = 0;

    for (i = 0; i < n; i++) {
        const SettingSpec *spec = settings_registry_at(reg, i);

        if (strcmp(spec->key, folded) == 0) {
            return spec;
        }
    }
    return NULL;
}

static size_t text_bytes(const char *text)
{
    return text == NULL ? 0U : strlen(text) + 1U;
}

/* Copy text into *at and move *at past it. NULL stays NULL. */
static const char *take_text(char **at, const char *text)
{
    const char *out = NULL;
    size_t n = 0;

    if (text == NULL) {
        return NULL;
    }
    n = strlen(text) + 1U;
    memcpy(*at, text, n);
    out = *at;
    *at += n;
    return out;
}

static GenRow *row_copy(const SettingSpec *row)
{
    size_t chars = text_bytes(row->key) + text_bytes(row->default_text) +
                   text_bytes(row->description) + text_bytes(row->related);
    size_t nvalues = row->values != NULL ? row->nvalues : 0U;
    size_t i = 0;
    GenRow *gen = NULL;
    char *at = NULL;

    if (nvalues > (size_t)SETTING_ENUM_VALUES_MAX) {
        return NULL;
    }
    for (i = 0; i < nvalues; i++) {
        chars += text_bytes(row->values[i]);
    }
    gen = (GenRow *)malloc(sizeof(*gen) + chars);
    if (gen == NULL) {
        return NULL;
    }
    for (i = 0; i < (size_t)SETTING_ENUM_VALUES_MAX; i++) {
        gen->values[i] = NULL;
    }
    gen->spec = *row;
    at = gen->text;
    gen->spec.key = take_text(&at, row->key);
    gen->spec.default_text = take_text(&at, row->default_text);
    gen->spec.description = take_text(&at, row->description);
    gen->spec.related = take_text(&at, row->related);
    gen->spec.values = nvalues > 0U ? gen->values : NULL;
    gen->spec.nvalues = nvalues;
    for (i = 0; i < nvalues; i++) {
        gen->values[i] = take_text(&at, row->values[i]);
    }
    return gen;
}

static bool registry_grow(SettingsRegistry *reg)
{
    size_t ncap = 0;
    GenRow **rows = NULL;

    if (reg->n < reg->cap) {
        return true;
    }
    ncap = reg->cap + (size_t)REGISTRY_GROW;
    // NOLINTNEXTLINE(bugprone-sizeof-expression) rows holds pointers; each element is one
    rows = (GenRow **)malloc(ncap * sizeof(*rows));
    if (rows == NULL) {
        return false;
    }
    if (reg->n > 0U) {
        // NOLINTNEXTLINE(bugprone-sizeof-expression) rows holds pointers; each element is one
        memcpy((void *)rows, (const void *)reg->rows, reg->n * sizeof(*rows));
    }
    free((void *)reg->rows);
    reg->rows = rows;
    reg->cap = ncap;
    return true;
}

/* cppcheck-suppress staticFunction ; public API (settings.h); used by tests */
bool settings_registry_add(SettingsRegistry *reg, const SettingSpec *row)
{
    GenRow *gen = NULL;

    if (reg == NULL || row == NULL || row->key == NULL || registry_find(reg, row->key) != NULL ||
        !registry_grow(reg)) {
        return false;
    }
    gen = row_copy(row);
    if (gen == NULL) {
        return false;
    }
    reg->rows[reg->n++] = gen;
    return true;
}

/* cppcheck-suppress staticFunction ; public API (settings.h); used by tests */
size_t settings_registry_remove_prefix(SettingsRegistry *reg, const char *prefix)
{
    size_t keep = 0;
    size_t i = 0;
    size_t gone = 0;
    size_t len = strlen(prefix);

    if (reg == NULL) {
        return 0U;
    }
    for (i = 0; i < reg->n; i++) {
        if (strncmp(reg->rows[i]->spec.key, prefix, len) == 0) {
            free(reg->rows[i]);
            gone++;
        } else {
            reg->rows[keep++] = reg->rows[i];
        }
    }
    reg->n = keep;
    return gone;
}

/* cppcheck-suppress staticFunction ; public API (settings.h); used by tests */
bool setting_quoted_plain(const char *text, char *inner, size_t inner_cap)
{
    size_t n = strlen(text);
    size_t i = 0;

    if (n < 2U || text[0] != '"' || text[n - 1U] != '"') {
        return false;
    }
    if (n - 2U >= inner_cap) {
        return false;
    }
    for (i = 1; i + 1U < n; i++) {
        unsigned char c = (unsigned char)text[i];

        if (c < (unsigned char)ASCII_UNIT || c == (unsigned char)ASCII_DEL || c == '"' ||
            c == '\\') {
            return false;
        }
    }
    memcpy(inner, text + 1, n - 2U);
    inner[n - 2U] = '\0';
    return true;
}

/* cppcheck-suppress staticFunction ; public API (settings.h); used by tests */
const SettingSpec *setting_by_key(const SettingsRegistry *reg, const char *typed)
{
    char folded[SETTING_KEY_MAX + 1];

    if (!setting_key_fold(typed, folded, sizeof(folded))) {
        return NULL;
    }
    return registry_find(reg, folded);
}

/* cppcheck-suppress staticFunction ; public API (settings.h); used by the settings commands */
bool setting_plugin_name(const char *typed, char *name, size_t cap)
{
    char folded[SETTING_KEY_MAX + 1];
    const char *dot = NULL;
    size_t n = 0;

    if (!setting_key_fold(typed, folded, sizeof(folded)) ||
        strncmp(folded, "plugin.", (size_t)PLUGIN_DOT_LEN) != 0) {
        return false;
    }
    dot = strchr(folded + PLUGIN_DOT_LEN, '.');
    if (dot == NULL) {
        return false;
    }
    n = (size_t)(dot - (folded + PLUGIN_DOT_LEN));
    if (n == 0U || n >= cap) {
        return false;
    }
    memcpy(name, folded + PLUGIN_DOT_LEN, n);
    name[n] = '\0';
    return true;
}

/* cppcheck-suppress staticFunction ; public API (settings.h); used by the settings commands */
bool setting_plugin_missing(const SettingsRegistry *reg, const char *typed, char *name, size_t cap)
{
    char enabled[SETTING_KEY_MAX + 1];

    if (!setting_plugin_name(typed, name, cap)) {
        return false;
    }
    (void)snprintf(enabled, sizeof(enabled), "plugin.%s.enabled", name);
    return registry_find(reg, enabled) == NULL;
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

/* A JSON string of src. Only a quote and a backslash need an escape: every caller passes text
   with no control byte (a canonical duration or size, a helper id, a setting_text_ok text). */
static bool json_quote(const char *src, char *out, size_t cap)
{
    size_t at = 0;
    size_t i = 0;

    if (src == NULL || out == NULL || cap == 0U) {
        return false;
    }
    out[at++] = '"';
    for (i = 0; src[i] != '\0'; i++) {
        size_t need = (src[i] == '"' || src[i] == '\\') ? 2U : 1U;

        if (at + need + 2U > cap) {
            out[0] = '\0';
            return false;
        }
        if (need == 2U) {
            out[at++] = '\\';
        }
        out[at++] = src[i];
    }
    out[at++] = '"';
    out[at] = '\0';
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
    if (spec->type == SETTING_DURATION || spec->type == SETTING_SIZE ||
        spec->type == SETTING_ENUM || spec->type == SETTING_TEXT || spec->type == SETTING_STRING) {
        if (!json_quote(spec->default_text, out, cap)) {
            return;
        }
        *has = true;
        return;
    }
    /* An integer, a boolean, and the compact array of a text list are stored as they read. */
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

/* A row with no range has both bounds 0 and has_range false. A range of exactly 0 to 0 is a
   range, and has_range says so. */
static bool in_range(const SettingSpec *spec, long long value)
{
    if (!spec->has_range && spec->min_value == 0 && spec->max_value == 0) {
        return true;
    }
    if (value < spec->min_value || value > spec->max_value) {
        return false;
    }
    return true;
}

static SettingStatus canon_integer(const SettingSpec *spec, const char *src, char *json,
                                   size_t json_cap)
{
    long long v = 0;

    if (!setting_integer(src, &v)) {
        return SETTING_ERR_INVALID;
    }
    if (!in_range(spec, v)) {
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
    if (!in_range(spec, ms)) {
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

/* ---- plugin setting types (decision:2026-11-16-plan30-plugin-string-values) ---- */

/* cppcheck-suppress staticFunction ; public API (settings.h); used by tests */
bool setting_text_ok(const char *text)
{
    size_t len = 0;
    size_t at = 0;

    if (text == NULL) {
        return false;
    }
    len = strlen(text);
    if (len > (size_t)SETTING_TEXT_MAX) {
        return false;
    }
    while (at < len) {
        uint32_t cp = 0;
        size_t used = 0;

        if (unicode_decode(text + at, len - at, &cp, &used) != UNICODE_OK ||
            cp < (uint32_t)ASCII_UNIT || cp == (uint32_t)ASCII_DEL) {
            return false;
        }
        at += used;
    }
    return true;
}

/* One quoted string of a stored value, from s[*pos]. Only \" and \\ are escapes. *pos ends past
   the closing quote. dst holds the decoded text, which must pass setting_text_ok. */
static bool take_string(const char *s, size_t *pos, char *dst, size_t dst_cap)
{
    size_t at = *pos;
    size_t used = 0;

    if (s[at] != '"') {
        return false;
    }
    at++;
    while (s[at] != '"') {
        char c = s[at];

        if (c == '\0') {
            return false;
        }
        if (c == '\\') {
            at++;
            c = s[at];
            if (c != '"' && c != '\\') {
                return false;
            }
        }
        if (used + 1U >= dst_cap) {
            return false;
        }
        dst[used++] = c;
        at++;
    }
    dst[used] = '\0';
    *pos = at + 1U;
    return setting_text_ok(dst);
}

/* cppcheck-suppress staticFunction ; public API (settings.h); used by tests */
bool setting_text_unquote(const char *json, char *out, size_t out_cap)
{
    size_t pos = 0;

    if (out != NULL && out_cap > 0U) {
        out[0] = '\0';
    }
    if (json == NULL || out == NULL || out_cap == 0U || !take_string(json, &pos, out, out_cap) ||
        json[pos] != '\0') {
        if (out != NULL && out_cap > 0U) {
            out[0] = '\0';
        }
        return false;
    }
    return true;
}

/* cppcheck-suppress staticFunction ; public API (settings.h); used by tests */
bool setting_text_list_join(const char *json, char *out, size_t out_cap)
{
    char item[SETTING_TEXT_MAX + 1];
    size_t pos = 1;
    size_t used = 0;
    bool first = true;

    if (out == NULL || out_cap == 0U) {
        return false;
    }
    out[0] = '\0';
    if (json == NULL || json[0] != '[') {
        return false;
    }
    while (json[pos] != ']') {
        size_t n = 0;
        size_t gap = 0;

        if (!first) {
            if (json[pos] != ',') {
                out[0] = '\0';
                return false;
            }
            pos++;
            gap = 1U;
        }
        if (!take_string(json, &pos, item, sizeof(item))) {
            out[0] = '\0';
            return false;
        }
        n = strlen(item);
        if (used + n + gap >= out_cap) {
            out[0] = '\0';
            return false;
        }
        if (!first) {
            out[used++] = ' ';
        }
        memcpy(out + used, item, n + 1U);
        used += n;
        first = false;
    }
    if (json[pos + 1U] != '\0') {
        out[0] = '\0';
        return false;
    }
    return true;
}

static SettingStatus canon_boolean(const char *src, char *json, size_t json_cap)
{
    if (strcmp(src, "true") != 0 && strcmp(src, "false") != 0) {
        return SETTING_ERR_INVALID;
    }
    (void)snprintf(json, json_cap, "%s", src);
    return SETTING_OK;
}

static SettingStatus canon_enum(const SettingSpec *spec, const char *src, char *json,
                                size_t json_cap)
{
    size_t i = 0;

    for (i = 0; spec->values != NULL && i < spec->nvalues; i++) {
        if (strcmp(spec->values[i], src) == 0) {
            if (!json_quote(src, json, json_cap)) {
                return SETTING_ERR_QUOTA;
            }
            return SETTING_OK;
        }
    }
    return SETTING_ERR_INVALID;
}

static SettingStatus canon_size(const SettingSpec *spec, const char *src, char *json,
                                size_t json_cap)
{
    char canon[NORM_MEASURE_CANON_CAP];
    long long bytes = 0;

    if (!norm_size_canonical(src, &bytes, canon, sizeof(canon))) {
        return SETTING_ERR_INVALID;
    }
    if (!in_range(spec, bytes)) {
        return SETTING_ERR_RANGE;
    }
    (void)json_quote(canon, json, json_cap);
    return SETTING_OK;
}

static SettingStatus canon_text(const char *src, char *json, size_t json_cap)
{
    if (!setting_text_ok(src)) {
        return SETTING_ERR_INVALID;
    }
    /* A text of at most SETTING_TEXT_MAX bytes quotes to under SETTING_VALUE_CAP. */
    (void)json_quote(src, json, json_cap);
    return SETTING_OK;
}

static SettingStatus canon_text_list(const SettingSpec *spec, const char *const *items,
                                     size_t nitems, char *json, size_t json_cap)
{
    char one[(2U * (size_t)SETTING_TEXT_MAX) + 3U];
    size_t pos = 0;
    size_t i = 0;

    if (spec->max_items > 0 && nitems > (size_t)spec->max_items) {
        return SETTING_ERR_RANGE;
    }
    if (nitems > 0U && items == NULL) {
        return SETTING_ERR_INVALID;
    }
    json[pos++] = '[';
    for (i = 0; i < nitems; i++) {
        size_t n = 0;

        if (items[i] == NULL || !setting_text_ok(items[i])) {
            json[0] = '\0';
            return SETTING_ERR_INVALID;
        }
        (void)json_quote(items[i], one, sizeof(one));
        n = strlen(one);
        if (pos + n + 2U > (size_t)SETTING_VALUE_MAX || pos + n + 2U >= json_cap) {
            json[0] = '\0';
            return SETTING_ERR_QUOTA;
        }
        if (i > 0U) {
            json[pos++] = ',';
        }
        memcpy(json + pos, one, n);
        pos += n;
    }
    json[pos++] = ']';
    json[pos] = '\0';
    return SETTING_OK;
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

/* Fold every item into a block the caller frees. On a refusal *item is the
   failing index and *other the earlier item it repeats, or SIZE_MAX when the
   item doesn't fold. */
// NOLINTBEGIN(bugprone-easily-swappable-parameters) the failing item, then the earlier one
static SettingStatus prefix_fold_all(const char *const *items, size_t nitems, char **block_out,
                                     size_t *item, size_t *other)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    enum { PREFIX_JSON_MIN = 5 };
    char *block = NULL;
    size_t i = 0;

    *block_out = NULL;
    *item = 0;
    *other = SIZE_MAX;
    if (nitems > (size_t)SETTING_VALUE_MAX / (size_t)PREFIX_JSON_MIN) {
        return SETTING_ERR_QUOTA;
    }
    if (nitems == 0U) {
        return SETTING_OK;
    }
    block = (char *)malloc(nitems * (size_t)REMEMBER_BUCKET_PREFIX_BUF);
    if (block == NULL) {
        return SETTING_ERR_STORE;
    }
    *block_out = block;
    for (i = 0; i < nitems; i++) {
        char *folded = block + (i * (size_t)REMEMBER_BUCKET_PREFIX_BUF);

        *item = i;
        if (items[i] == NULL ||
            bucket_prefix_fold(items[i], folded, REMEMBER_BUCKET_PREFIX_BUF) != NORM_OK ||
            prefix_already_folded(block, i, folded, other)) {
            return SETTING_ERR_INVALID;
        }
    }
    return SETTING_OK;
}

SettingStatus setting_prefix_issue(const char *const *items, size_t nitems, size_t *item,
                                   size_t *other)
{
    char *block = NULL;
    SettingStatus st = prefix_fold_all(items, nitems, &block, item, other);

    free(block);
    return st;
}

static SettingStatus canon_prefixes(const char *const *items, size_t nitems, char *json,
                                    size_t json_cap, char *err, size_t errlen)
{
    char *block = NULL;
    size_t i = 0;
    size_t pos = 0;
    size_t bad = 0;
    size_t earlier = 0;
    SettingStatus st = prefix_fold_all(items, nitems, &block, &bad, &earlier);

    json[pos++] = '[';
    for (i = 0; st == SETTING_OK && i < nitems; i++) {
        st = append_prefix_json(json, &pos, json_cap,
                                block + (i * (size_t)REMEMBER_BUCKET_PREFIX_BUF), i > 0U);
    }
    free(block);
    if (st == SETTING_ERR_STORE) {
        copy_msg(err, errlen, "out of memory");
    }
    if (st != SETTING_OK) {
        json[0] = '\0';
        return st;
    }
    json[pos++] = ']';
    json[pos] = '\0';
    if (pos > (size_t)SETTING_VALUE_MAX) {
        json[0] = '\0';
        return SETTING_ERR_QUOTA;
    }
    return SETTING_OK;
}

/* cppcheck-suppress staticFunction ; public API (settings.h); used by tests */
SettingStatus setting_canon_value(const SettingSpec *spec, const char *const *items, size_t nitems,
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
    if (spec->type == SETTING_TEXT_LIST) {
        return canon_text_list(spec, items, nitems, json, json_cap);
    }
    if (items == NULL || nitems != 1U || items[0] == NULL) {
        return SETTING_ERR_INVALID;
    }
    switch (spec->type) {
    case SETTING_INTEGER:
        return canon_integer(spec, items[0], json, json_cap);
    case SETTING_DURATION:
        return canon_duration(spec, items[0], json, json_cap);
    case SETTING_BOOLEAN:
        return canon_boolean(items[0], json, json_cap);
    case SETTING_ENUM:
        return canon_enum(spec, items[0], json, json_cap);
    case SETTING_SIZE:
        return canon_size(spec, items[0], json, json_cap);
    case SETTING_TEXT:
        return canon_text(items[0], json, json_cap);
    case SETTING_STRING:
    case SETTING_STRING_LIST:
    case SETTING_TEXT_LIST:
    default:
        return canon_helper(items[0], json, json_cap);
    }
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
SettingStatus setting_choose_layer(const SettingsRegistry *reg, const char *key,
                                   SettingLayer requested, const SettingSpec **spec_out,
                                   SettingLayer *layer_out, char *err, size_t errlen)
{
    const SettingSpec *spec = NULL;

    if (err != NULL && errlen > 0U) {
        err[0] = '\0';
    }
    if (spec_out != NULL) {
        *spec_out = NULL;
    }
    spec = setting_by_key(reg, key);
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
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
SettingStatus setting_prepare(const SettingsRegistry *reg, const char *key,
                              const char *const *items, size_t nitems, SettingLayer requested,
                              char *json, size_t json_cap, const SettingSpec **spec_out,
                              SettingLayer *layer_out, char *err, size_t errlen)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    const SettingSpec *spec = NULL;
    SettingStatus st = SETTING_OK;

    st = setting_choose_layer(reg, key, requested, &spec, layer_out, err, errlen);
    if (st != SETTING_OK) {
        return st;
    }
    if (spec_out != NULL) {
        *spec_out = spec;
    }
    return setting_canon_value(spec, items, nitems, json, json_cap, err, errlen);
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
const char *setting_suggestion(const SettingsRegistry *reg, const char *typed)
{
    size_t nregs = settings_registry_count(reg);
    const char *hit = NULL;
    size_t hits = 0;
    size_t i = 0;
    size_t n = 0;
    bool comparable = false;

    if (typed == NULL || typed[0] == '\0') {
        return NULL;
    }
    for (i = 0; i < nregs; i++) {
        const char *dot = strrchr(settings_registry_at(reg, i)->key, '.');

        if (dot != NULL && strcmp(dot + 1, typed) == 0) {
            hits++;
            hit = settings_registry_at(reg, i)->key;
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
        size_t nb = strlen(settings_registry_at(reg, i)->key);
        int dist = 0;

        if (!comparable || n > nb + 2U || nb > n + 2U) {
            continue;
        }
        dist = edit_distance(typed, settings_registry_at(reg, i)->key);
        if (dist <= 2) {
            hits++;
            hit = settings_registry_at(reg, i)->key;
        }
    }
    if (hits == 1U) {
        return hit;
    }
    return NULL;
}
