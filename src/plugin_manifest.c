#include "plugin_manifest.h"

#include "jsonread.h"
#include "normalize.h"
#include "settings.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef REMEMBER_TEST_HOOKS
#include "test_alloc.h" // IWYU pragma: keep
#endif

enum {
    BUDGET_MIN_MS = 1,
    BUDGET_MAX_MS = 30000,
    BUDGET_DEFAULT_MS = 800,
    GRACE_DEFAULT_MS = 10000,
    PRIORITY_DEFAULT = 500,
    PRIORITY_MAX = 1000,
    TIMEOUT_DEFAULT_MS = 60000,
    BATCH_ENTRIES_MAX = 100,
    BATCH_BYTES_MAX = 4 * 1024 * 1024,
    GROW_FIRST = 8,
    ENUM_PHRASE_CAP = 512,
    WHAT_CAP = 64,
    WHOLE_TEXT_CAP = 72,
    WHOLE_DIGITS_MAX = 64,
    WHOLE_FITS_DIGITS = 19, /* LLONG_MAX has 19 digits */
    EXPONENT_MAX = 100000,
    LIST_WHAT_CAP = ENUM_PHRASE_CAP + WHAT_CAP,
    NEED_WHAT_CAP = ENUM_PHRASE_CAP + 96,
    GLOBAL_KEY_SEGMENTS_MIN = 2,
    GLOBAL_KEY_SEGMENTS_MAX = 3,
    SEMVER_DOTS = 2,
    UTF8_CONT_MASK = 0xC0,
    UTF8_CONT = 0x80,
    UTF8_C1_LEAD = 0xC2,
    UTF8_C1_FIRST = 0x80,
    UTF8_C1_LAST = 0x9F,
    UTF8_SPACE = 0x20,
    UTF8_DEL = 0x7F,
    HEX_MASK = 0xF,
    HEX_SHIFT = 4,
    U_ESCAPE_LEN = 6, /* backslash, u, 0, 0 and two hex digits */
    U_ESCAPE_PREFIX = 4,
    DECIMAL = 10,
    WHOLE_SCAN_PAD = 5
};

/* detail.supported (R25-1, G2). The range text is built from these arrays. */
static const int k_protocols[] = {1};
static const int k_formats[] = {1};

static const char k_unsupported[] = "plugin_protocol_unsupported";
static const char k_invalid[] = "plugin_manifest_invalid";
static const char k_budget_rule[] = "budget_range";
static const char k_global_prefix[] = "remember.";

static const char *const k_root_known[] = {
    "format",      "version",      "name",         "plugin_version", "protocol",
    "description", "data_version", "idle_timeout", "backfill",       "actions",
    "filters",     "settings",     "run",          "permissions",    "filter"};
static const char *const k_root_misplaced[] = {"run", "permissions", "filter"};
static const char k_llong_max[] = "9223372036854775807";
static const char *const k_action_known[] = {"name", "events", "filter", "run", "permissions"};
static const char *const k_filter_known[] = {
    "name", "point", "priority", "budget", "cold_start_grace", "run", "permissions"};
static const char *const k_action_run_known[] = {"process", "command", "timeout"};
static const char *const k_filter_run_known[] = {"process", "command"};
static const char *const k_perm_known[] = {"needs"};
static const char *const k_filter_block_known[] = {"tags", "created_by",    "updated_by",
                                                   "bins", "not_caused_by", "origins"};
static const char *const k_tags_known[] = {"any", "all", "match_before"};
static const char *const k_backfill_known[] = {"enabled", "max_batch_entries", "max_batch_size"};
static const char *const k_setting_known[] = {"key",     "description", "type",
                                              "default", "range",       "scope"};
static const char *const k_range_known[] = {"min", "max", "values", "max_items"};

static const char *const k_events[] = {"memory.created",  "memory.updated", "memory.deleted",
                                       "memory.restored", "memory.purged",  "memory.expired",
                                       "memory.*"};
static const char *const k_origins[] = {"local", "sync", "import"};
static const char *const k_points[] = {"search.query", "search.results"};
static const char *const k_actors[] = {"human", "agent", "tool", "share", "import"};
static const char *const k_bins[] = {"live", "expired", "deleted", "purged"};
static const char *const k_action_process[] = {"once", "persistent"};
static const char *const k_filter_process[] = {"persistent"};
static const char *const k_scopes[] = {"user", "brain", "both"};
/* Indexed by PluginSettingType. */
static const char *const k_setting_types[] = {"boolean", "integer", "enum",       "duration",
                                              "size",    "string",  "string_list"};
static const char *const k_memory_fields[] = {
    "key",        "body",       "tags",       "bin",        "created_at", "updated_at",
    "expires_at", "deleted_at", "change_seq", "created_by", "updated_by", "links"};
static const char *const k_duration_units[] = {"ms", "s", "m", "h"};
static const char *const k_size_units[] = {"B", "KiB", "MiB"};

#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

typedef struct Names {
    const char *const *list;
    size_t count;
} Names;

#define NAMES(array) ((Names){(array), COUNT(array)})

static bool names_have(Names names, const char *text)
{
    size_t i = 0;

    for (i = 0; i < names.count; i++) {
        if (strcmp(names.list[i], text) == 0) {
            return true;
        }
    }
    return false;
}

static bool segment_ok(const char *text, size_t len)
{
    size_t i = 0;

    if (len == 0U || len > (size_t)PLUGIN_NAME_CAP || text[0] < 'a' || text[0] > 'z') {
        return false;
    }
    for (i = 1; i < len; i++) {
        char ch = text[i];

        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_')) {
            return false;
        }
    }
    return true;
}

/* ^[a-z][a-z0-9_]{0,39}$ */
static bool name_ok(const char *text)
{
    return segment_ok(text, strlen(text));
}

/* "remember." and two or three name segments. The 96-character cap is the size of the needs
   buffer (PLUGIN_NEED_CAP), so a longer string never gets here. */
static bool global_key_ok(const char *text)
{
    const char *seg = text + sizeof(k_global_prefix) - 1U;
    int segments = 0;

    if (strncmp(text, k_global_prefix, sizeof(k_global_prefix) - 1U) != 0) {
        return false;
    }
    for (;;) {
        const char *dot = strchr(seg, '.');
        size_t n = dot == NULL ? strlen(seg) : (size_t)(dot - seg);

        if (!segment_ok(seg, n)) {
            return false;
        }
        segments++;
        if (dot == NULL) {
            break;
        }
        seg = dot + 1;
    }
    if (segments < GLOBAL_KEY_SEGMENTS_MIN || segments > GLOBAL_KEY_SEGMENTS_MAX) {
        return false;
    }
    return true;
}

static bool key_is(const JsonMember *member, const char *name)
{
    return strcmp(member->key, name) == 0;
}

/* The first member named key. check_members reports a repeated key. */
static const JsonValue *member_value(const JsonValue *obj, const char *key)
{
    size_t i = 0;

    for (i = 0; i < obj->u.object.n; i++) {
        if (key_is(&obj->u.object.members[i], key)) {
            return obj->u.object.members[i].value;
        }
    }
    return NULL;
}

static char *dup_text(const char *text)
{
    size_t n = strlen(text);
    char *out = (char *)malloc(n + 1U);

    if (out != NULL) {
        memcpy(out, text, n + 1U);
    }
    return out;
}

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wallocator-wrappers"
#endif
static char *vfmt_alloc(const char *fmt, va_list args) __attribute__((format(printf, 1, 0)));

/* A heap string exactly as long as the sentence (standard:c). NULL on OOM.
   ponytail: the second vfmt helper (commands_common.c fmt_valloc is static and CLI-layer), and
   cmd_settings, commands_common, diag and import_read measure with snprintf(NULL, 0) inline. One
   shared text helper would replace them all; that is its own change. */
static char *vfmt_alloc(const char *fmt, va_list args)
{
    /* NOLINTNEXTLINE(cppcoreguidelines-init-variables) */
    va_list again;
    int n = 0;
    char *out = NULL;

    va_copy(again, args);
    n = vsnprintf(NULL, 0, fmt, args);
    out = (char *)malloc((size_t)n + 1U);
    if (out != NULL) {
        (void)vsnprintf(out, (size_t)n + 1U, fmt, again);
    }
    va_end(again);
    return out;
}

static char *fmt_alloc(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static char *fmt_alloc(const char *fmt, ...)
{
    /* NOLINTNEXTLINE(cppcoreguidelines-init-variables) */
    va_list args;
    char *out = NULL;

    va_start(args, fmt);
    out = vfmt_alloc(fmt, args);
    va_end(args);
    return out;
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif

/* items with room for one more; NULL (items untouched) on OOM. */
static void *room_for_one(void *items, size_t count, size_t *cap, size_t size)
{
    size_t next = *cap == 0U ? (size_t)GROW_FIRST : *cap * 2U;
    void *bigger = NULL;

    if (count < *cap) {
        return items;
    }
    bigger = malloc(next * size);
    if (bigger == NULL) {
        return NULL;
    }
    if (count > 0U) {
        memcpy(bigger, items, count * size);
    }
    free(items);
    *cap = next;
    return bigger;
}

typedef struct Report {
    const char *code;
    const char *rule;
    const char *path;
    const int *supported;
    size_t nsupported;
    size_t place; /* source offset + 1 of what the error names; 0: sort_errors finds it from path */
} Report;

/* Takes message and fix (heap). Any OOM marks the manifest; parse then returns OOM. */
static void add_error(PluginManifest *man, Report report, char *message, char *fix)
{
    char *path_copy = dup_text(report.path);
    PluginError *errors = NULL;

    if (message != NULL && fix != NULL && path_copy != NULL) {
        errors = (PluginError *)room_for_one(man->errors, man->nerrors, &man->errors_cap,
                                             sizeof(*errors));
    }
    if (errors == NULL) {
        free(message);
        free(fix);
        free(path_copy);
        man->oom = true;
        return;
    }
    man->errors = errors;
    errors[man->nerrors] = (PluginError){.code = report.code,
                                         .rule = report.rule,
                                         .supported = report.supported,
                                         .path = path_copy,
                                         .message = message,
                                         .fix = fix,
                                         .nsupported = report.nsupported,
                                         .at = report.place == 0U ? SIZE_MAX : report.place - 1U};
    man->nerrors += 1U;
}

static void add_invalid(PluginManifest *man, const char *rule, const char *path, char *message,
                        char *fix)
{
    add_error(man, (Report){.code = k_invalid, .rule = rule, .path = path}, message, fix);
}

static char *json_escaped(const char *text);

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): a path, then a printf format
static char *where_message(const char *path, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* "plugin.json <path>: <sentence>" (R25-2). An empty path is the top level. The pointer is
   escaped like any plugin-controlled text (Q2): a key under an open object can be anything. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): a path, then a printf format
static char *where_message(const char *path, const char *fmt, ...)
{
    /* NOLINTNEXTLINE(cppcoreguidelines-init-variables) */
    va_list args;
    char *body = NULL;
    char *escaped = json_escaped(path);
    char *out = NULL;

    va_start(args, fmt);
    body = vfmt_alloc(fmt, args);
    va_end(args);
    if (body != NULL && escaped != NULL) {
        out = fmt_alloc("plugin.json%s%s: %s", path[0] == '\0' ? "" : " ", escaped, body);
    }
    free(body);
    free(escaped);
    return out;
}

/* R25-2 sentence 5: a value of the wrong type or outside its range. */
static void err_must_be(PluginManifest *man, const char *rule, const char *path, const char *what)
{
    add_invalid(man, rule, path, where_message(path, "must be %s.", what),
                fmt_alloc("Use %s.", what));
}

/* R25-2 sentence 3: path names the object that lacks the member. */
static void err_missing(PluginManifest *man, const char *rule, const char *path, const char *field)
{
    add_invalid(man, rule, path, where_message(path, "\"%s\" is missing.", field),
                fmt_alloc("Add \"%s\".", field));
}

/* The letter of the short escape of a byte (the one after the backslash), or NUL. */
static char short_escape(unsigned code)
{
    static const char raw[] = "\"\\\n\r\t\b\f";
    static const char letter[] = "\"\\nrtbf";
    const char *hit = code == 0U ? NULL : strchr(raw, (int)code);

    if (hit == NULL) {
        return '\0';
    }
    return letter[hit - raw];
}

/* The second byte of a C1 control (U+0080 to U+009F in UTF-8), or 0. */
static unsigned c1_code(const unsigned char *p)
{
    if (p[0] != (unsigned)UTF8_C1_LEAD || p[1] < (unsigned)UTF8_C1_FIRST ||
        p[1] > (unsigned)UTF8_C1_LAST) {
        return 0U;
    }
    return p[1];
}

/* Q2 (decision:2026-11-14-copy-r25-2-manifest-approved): plugin-controlled text prints with JSON
   string escaping, so a line stays one line and no escape sequence reaches a terminal. The bytes
   that change are ", \\, the C0 controls, DEL, and the C1 controls (U+0080 to U+009F). Text in a
   pointer stays as RFC 6901 wrote it: the JSON writer escapes it. Writes into out when it is
   not NULL, and returns the length either way. */
static size_t escape_text(const char *text, char *out)
{
    static const char hex[] = "0123456789abcdef";
    const unsigned char *p = (const unsigned char *)text;
    size_t n = 0;

    for (; *p != 0U; p++) {
        unsigned code = c1_code(p);
        char letter = short_escape(*p);

        if (letter != '\0') {
            if (out != NULL) {
                out[n] = '\\';
                out[n + 1U] = letter;
            }
            n += 2U;
            continue;
        }
        if (code == 0U) {
            code = *p;
            if (code >= (unsigned)UTF8_SPACE && code != (unsigned)UTF8_DEL) {
                if (out != NULL) {
                    out[n] = (char)code;
                }
                n += 1U;
                continue;
            }
        } else {
            p++; /* the C1 lead byte is done; code is its second byte */
        }
        if (out != NULL) {
            (void)memcpy(out + n, "\\u00", (size_t)U_ESCAPE_PREFIX);
            out[n + (size_t)U_ESCAPE_PREFIX] =
                hex[(code >> (unsigned)HEX_SHIFT) & (unsigned)HEX_MASK];
            out[n + (size_t)U_ESCAPE_PREFIX + 1U] = hex[code & (unsigned)HEX_MASK];
        }
        n += (size_t)U_ESCAPE_LEN;
    }
    if (out != NULL) {
        out[n] = '\0';
    }
    return n;
}

/* A heap copy of text, escaped. NULL on out of memory. */
static char *json_escaped(const char *text)
{
    char *out = (char *)malloc(escape_text(text, NULL) + 1U);

    if (out != NULL) {
        (void)escape_text(text, out);
    }
    return out;
}

/* R25-2 sentence 8. detail_path is the pointer of the repeated thing. A repeated member or item is
   kept once. A repeated entry name or setting key is renamed. */
// NOLINTBEGIN(bugprone-easily-swappable-parameters): the list, then the repeated pointer, then text
static void err_twice(PluginManifest *man, const char *rule, const char *where_path,
                      const char *detail_path, const char *what, bool rename, size_t place)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    char *escaped = json_escaped(what);
    char *fix = NULL;

    if (escaped == NULL) {
        man->oom = true;
        return;
    }
    if (rename) {
        fix = dup_text("Rename one of them.");
    } else {
        fix = fmt_alloc("Keep one \"%s\".", escaped);
    }
    add_error(man, (Report){.code = k_invalid, .rule = rule, .path = detail_path, .place = place},
              where_message(where_path, "\"%s\" appears more than once.", escaped), fix);
    free(escaped);
}

static void add_unknown(PluginManifest *man, const char *path, const char *field)
{
    char *path_copy = dup_text(path);
    char *field_copy = dup_text(field);
    PluginUnknown *warnings = NULL;

    if (path_copy != NULL && field_copy != NULL) {
        warnings = (PluginUnknown *)room_for_one(man->warnings, man->nwarnings, &man->warnings_cap,
                                                 sizeof(*warnings));
    }
    if (warnings == NULL) {
        free(path_copy);
        free(field_copy);
        man->oom = true;
        return;
    }
    man->warnings = warnings;
    warnings[man->nwarnings] = (PluginUnknown){.path = path_copy, .field = field_copy};
    man->nwarnings += 1U;
}

/* RFC 6901: "~" is "~0" and "/" is "~1" inside one reference token. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the parent pointer, then one key
static char *pointer_join(const char *parent, const char *key)
{
    size_t plen = strlen(parent);
    size_t n = plen + 2U;
    const char *k = NULL;
    char *out = NULL;
    char *w = NULL;

    for (k = key; *k != '\0'; k++) {
        n += (*k == '~' || *k == '/') ? 2U : 1U;
    }
    out = (char *)malloc(n);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, parent, plen + 1U);
    w = out + plen;
    *w++ = '/';
    for (k = key; *k != '\0'; k++) {
        if (*k == '~' || *k == '/') {
            *w++ = '~';
            *w++ = (*k == '~') ? '0' : '1';
        } else {
            *w++ = *k;
        }
    }
    *w = '\0';
    return out;
}

static void child_path(char *buf, const char *path, const char *key)
{
    (void)snprintf(buf, (size_t)PLUGIN_PATH_CAP, "%s/%s", path, key);
}

static void index_path(char *buf, const char *path, size_t index)
{
    (void)snprintf(buf, (size_t)PLUGIN_PATH_CAP, "%s/%zu", path, index);
}

/* One edit away: one substitution, insertion or deletion. */
static bool one_edit(const char *a, const char *b)
{
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    const char *shorter = a;
    const char *longer = b;
    size_t len_short = len_a;
    size_t i = 0;

    if (len_a > len_b) {
        shorter = b;
        longer = a;
        len_short = len_b;
    }
    while (i < len_short && shorter[i] == longer[i]) {
        i++;
    }
    if (len_a == len_b) {
        if (i == len_short) {
            return false;
        }
        return strcmp(shorter + i + 1, longer + i + 1) == 0;
    }
    return strcmp(shorter + i, longer + i + 1) == 0;
}

typedef struct KeyRef {
    const char *key;
    size_t len;
    size_t index;
} KeyRef;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the qsort comparator signature
static int key_order(const void *left, const void *right)
{
    const KeyRef *a = (const KeyRef *)left;
    const KeyRef *b = (const KeyRef *)right;
    int c = memcmp(a->key, b->key, a->len < b->len ? a->len : b->len);

    if (c != 0) {
        return c;
    }
    if (a->len != b->len) {
        return a->len < b->len ? -1 : 1;
    }
    return a->index < b->index ? -1 : 1;
}

/* repeated[i] is true for every member after the first with the same key.
   Sorted, so a hostile object with many keys stays O(n log n). */
static bool *repeated_members(const JsonValue *obj)
{
    size_t n = obj->u.object.n;
    KeyRef *refs = (KeyRef *)malloc((n == 0U ? 1U : n) * sizeof(*refs));
    bool *repeated = (bool *)malloc(n == 0U ? 1U : n);
    size_t i = 0;

    if (refs == NULL || repeated == NULL) {
        free(refs);
        free(repeated);
        return NULL;
    }
    memset(repeated, 0, n == 0U ? 1U : n);
    for (i = 0; i < n; i++) {
        refs[i] = (KeyRef){.key = obj->u.object.members[i].key,
                           .len = obj->u.object.members[i].key_len,
                           .index = i};
    }
    qsort(refs, n, sizeof(*refs), key_order);
    for (i = 1; i < n; i++) {
        if (refs[i].len == refs[i - 1U].len &&
            memcmp(refs[i].key, refs[i - 1U].key, refs[i].len) == 0) {
            repeated[refs[i].index] = true;
        }
    }
    free(refs);
    return repeated;
}

/* R25-2 sentence 7: unknown field on a closed object. The fix names the one known field of that
   object that is one edit away, when exactly one is (decision:plugin-manifest-errors). */
static void err_unknown(PluginManifest *man, const char *rule, const char *path, const char *field,
                        Names known, size_t place)
{
    char *child = pointer_join(path, field);
    char *escaped = json_escaped(field);
    char *fix = NULL;
    const char *near = NULL;
    size_t i = 0;
    size_t near_count = 0;

    for (i = 0; i < known.count; i++) {
        if (one_edit(field, known.list[i])) {
            near = known.list[i];
            near_count++;
        }
    }
    if (near_count == 1U) {
        fix = fmt_alloc("Did you mean \"%s\"?", near);
    } else if (escaped != NULL) {
        fix = fmt_alloc("Remove \"%s\".", escaped);
    }
    if (child == NULL || escaped == NULL || fix == NULL) {
        free(fix);
        man->oom = true;
    } else {
        add_error(man, (Report){.code = k_invalid, .rule = rule, .path = child, .place = place},
                  where_message(path, "unknown field \"%s\".", escaped), fix);
    }
    free(child);
    free(escaped);
}

/* Every object the schema names goes through here once: an unknown key is an error on a closed
   object (unknown_rule is its rule) or a C-35 warning on an open one (unknown_rule is NULL, R2.6).
   A repeated key is check_repeats' error, so it is skipped here. */
static void check_members(PluginManifest *man, const JsonValue *obj, const char *path, Names known,
                          const char *unknown_rule)
{
    bool *repeated = repeated_members(obj);
    size_t i = 0;

    if (repeated == NULL) {
        man->oom = true;
        return;
    }
    for (i = 0; i < obj->u.object.n; i++) {
        const char *key = obj->u.object.members[i].key;

        if (repeated[i] || names_have(known, key)) {
            continue;
        }
        if (unknown_rule != NULL) {
            err_unknown(man, unknown_rule, path, key, known,
                        obj->u.object.members[i].value->src_at + 1U);
        } else {
            add_unknown(man, path, key);
        }
    }
    free(repeated);
}

/* R25-2 sentence 8 for a repeated key, in every object of the file: a parser that kept the other
   value would read another manifest. Each repeat is placed at its own member. jsonread caps the
   depth at 64, so the recursion is bounded. */
static void check_repeats(PluginManifest *man, const JsonValue *value, const char *path)
{
    size_t n = value->type == JSON_OBJECT ? value->u.object.n : value->u.array.n;
    bool *repeated = NULL;
    size_t i = 0;

    if (value->type == JSON_OBJECT) {
        repeated = repeated_members(value);
        if (repeated == NULL) {
            man->oom = true;
            return;
        }
    }
    for (i = 0; i < n; i++) {
        const JsonMember *member = value->type == JSON_OBJECT ? &value->u.object.members[i] : NULL;
        const JsonValue *child = member != NULL ? member->value : value->u.array.items[i];
        char *child_path_ = NULL;

        if (member != NULL && !repeated[i] && child->type != JSON_OBJECT &&
            child->type != JSON_ARRAY) {
            continue;
        }
        child_path_ =
            member != NULL ? pointer_join(path, member->key) : fmt_alloc("%s/%zu", path, i);
        if (child_path_ == NULL) {
            man->oom = true;
        } else {
            if (member != NULL && repeated[i]) {
                err_twice(man, "R2.2", path, child_path_, member->key, false, child->src_at + 1U);
            }
            if (child->type == JSON_OBJECT || child->type == JSON_ARRAY) {
                check_repeats(man, child, child_path_);
            }
        }
        free(child_path_);
    }
    free(repeated);
}

/* A JSON object whose members are checked, or NULL after an error. */
static const JsonValue *object_at(PluginManifest *man, const JsonValue *value, const char *path,
                                  const char *rule, Names known, const char *unknown_rule)
{
    if (value->type != JSON_OBJECT) {
        err_must_be(man, rule, path, "an object");
        return NULL;
    }
    check_members(man, value, path, known, unknown_rule);
    return value;
}

/* The member, or NULL. A required member that is absent is reported. */
static const JsonValue *member_of(PluginManifest *man, const JsonValue *obj, const char *path,
                                  const char *key, const char *rule, bool required)
{
    const JsonValue *value = member_value(obj, key);

    if (value == NULL && required) {
        err_missing(man, rule, path, key);
    }
    return value;
}

/* A string that fits dst (cap includes the NUL). */
static bool string_into(const JsonValue *value, char *dst, size_t cap)
{
    if (value->type != JSON_STRING || value->u.string.len >= cap) {
        return false;
    }
    memcpy(dst, value->u.string.bytes, value->u.string.len + 1U);
    return true;
}

typedef enum WholeKind {
    WHOLE_NO = 0, /* not a whole number, or too long to print */
    WHOLE_FITS,   /* a whole number that fits a long long */
    WHOLE_BIG     /* a whole number past a long long: text holds its digits */
} WholeKind;

typedef struct WholeScan {
    long long exponent;
    size_t count; /* significant digits kept */
    size_t frac;  /* digits after the point */
    bool negative;
    bool exponent_negative;
    char digits[WHOLE_DIGITS_MAX + 1];
    char pad_[WHOLE_SCAN_PAD];
} WholeScan;

/* The exponent after "e" or "E", clamped. */
static long long scan_exponent(const char *p, bool *negative)
{
    long long exponent = 0;

    *negative = *p == '-';
    if (*p == '-' || *p == '+') {
        p++;
    }
    for (; *p != '\0'; p++) {
        exponent = (exponent * DECIMAL) + (*p - '0');
        if (exponent > EXPONENT_MAX) {
            exponent = EXPONENT_MAX;
        }
    }
    return exponent;
}

/* Reads sign, digits and exponent. False when the number keeps more digits than fit. */
static bool whole_scan(const char *token, WholeScan *scan)
{
    const char *p = token;
    bool dotted = false;
    size_t zeros = 0; /* zeros after the point, kept only when a digit follows them */

    scan->negative = *p == '-';
    if (scan->negative) {
        p++;
    }
    for (; *p != '\0' && *p != 'e' && *p != 'E'; p++) {
        if (*p == '.') {
            dotted = true;
            continue;
        }
        if (dotted) {
            scan->frac++;
        }
        if (scan->count == 0U && *p == '0') {
            continue;
        }
        if (dotted && *p == '0') {
            zeros++;
            continue;
        }
        if (scan->count + zeros >= (size_t)WHOLE_DIGITS_MAX) {
            return false;
        }
        memset(scan->digits + scan->count, '0', zeros);
        scan->count += zeros;
        zeros = 0;
        scan->digits[scan->count++] = *p;
    }
    scan->frac -= zeros; /* 1.000 is 1: trailing zeros after the point change nothing */
    if (*p != '\0') {
        scan->exponent = scan_exponent(p + 1, &scan->exponent_negative);
    }
    return true;
}

/* Applies the exponent and the digits after the point. False when the value is not whole, or
   needs more than WHOLE_DIGITS_MAX digits. */
static bool whole_shift(WholeScan *scan)
{
    long long scale = scan->exponent - (long long)scan->frac;
    size_t cut = 0;

    if (scan->exponent_negative) {
        scale = -scan->exponent - (long long)scan->frac;
    }
    if (scale >= 0) {
        if ((size_t)scale > (size_t)WHOLE_DIGITS_MAX - scan->count) {
            return false;
        }
        memset(scan->digits + scan->count, '0', (size_t)scale);
        scan->count += (size_t)scale;
        return true;
    }
    cut = (size_t)(-scale);
    if (cut > scan->count) {
        return false;
    }
    for (size_t i = 0; i < cut; i++) {
        if (scan->digits[scan->count - 1U - i] != '0') {
            return false;
        }
    }
    scan->count -= cut;
    return true;
}

/* Q1 (decision:2026-11-14-copy-r25-2-manifest-approved): a JSON number is a whole number when its
   value is one, so 1.0, 1e0 and -0 are whole and 1.5 is not. text (WHOLE_TEXT_CAP bytes) gets the
   value as plain digits, never the source text. A number with more than WHOLE_DIGITS_MAX digits
   once its exponent is applied is WHOLE_NO, so a hostile exponent costs nothing. */
static WholeKind whole_of(const char *token, long long *value, char *text)
{
    WholeScan scan = {0};
    const char *sign = "";

    if (!whole_scan(token, &scan)) {
        return WHOLE_NO;
    }
    if (scan.count == 0U) { /* zero, whatever its exponent */
        *value = 0;
        (void)snprintf(text, (size_t)WHOLE_TEXT_CAP, "0");
        return WHOLE_FITS;
    }
    if (!whole_shift(&scan)) {
        return WHOLE_NO;
    }
    scan.digits[scan.count] = '\0';
    if (scan.negative) {
        sign = "-";
    }
    if (scan.count > (size_t)WHOLE_FITS_DIGITS ||
        (scan.count == (size_t)WHOLE_FITS_DIGITS && strcmp(scan.digits, k_llong_max) > 0)) {
        (void)snprintf(text, (size_t)WHOLE_TEXT_CAP, "%s%s", sign, scan.digits);
        return WHOLE_BIG;
    }
    *value = strtoll(scan.digits, NULL, DECIMAL);
    if (scan.negative) {
        *value = -*value;
    }
    (void)snprintf(text, (size_t)WHOLE_TEXT_CAP, "%lld", *value);
    return WHOLE_FITS;
}

/* A whole number (Q1) from lo to hi. */
static bool whole_in(const JsonValue *value, long long lo, long long hi, long long *out)
{
    char text[WHOLE_TEXT_CAP];
    long long number = 0;

    if (value->type != JSON_NUMBER ||
        whole_of(value->u.number.bytes, &number, text) != WHOLE_FITS || number < lo ||
        number > hi) {
        return false;
    }
    *out = number;
    return true;
}

/* Digits, then one of the schema's units. The settings grammar reads the value. */
static bool measure_shape(const char *text, Names units)
{
    size_t digits = strspn(text, "0123456789");

    if (digits == 0U) {
        return false;
    }
    return names_have(units, text + digits);
}

static bool duration_in(const JsonValue *value, long long lo, long long hi, long long *out)
{
    char canon[NORM_MEASURE_CANON_CAP];
    char text[NORM_MEASURE_CANON_CAP];
    long long ms = 0;

    if (!string_into(value, text, sizeof(text)) || !measure_shape(text, NAMES(k_duration_units)) ||
        !norm_duration_canonical(text, &ms, canon, sizeof(canon)) || ms < lo || ms > hi) {
        return false;
    }
    *out = ms;
    return true;
}

static bool size_in(const JsonValue *value, long long hi, long long *out)
{
    char canon[NORM_MEASURE_CANON_CAP];
    char text[NORM_MEASURE_CANON_CAP];
    long long bytes = 0;

    if (!string_into(value, text, sizeof(text)) || !measure_shape(text, NAMES(k_size_units)) ||
        !norm_size_canonical(text, &bytes, canon, sizeof(canon)) || bytes > hi) {
        return false;
    }
    *out = bytes;
    return true;
}

typedef struct WholeRange {
    long long lo;
    long long hi;
} WholeRange;

/* Member readers. Each reports a missing (when required) or a wrong value
   once, and returns true only when it stored a value. */
static bool read_bool(PluginManifest *man, const JsonValue *obj, const char *path, const char *key,
                      const char *rule, bool required, bool *out)
{
    const JsonValue *value = member_of(man, obj, path, key, rule, required);
    char child[PLUGIN_PATH_CAP];

    if (value == NULL) {
        return false;
    }
    child_path(child, path, key);
    if (value->type != JSON_BOOL) {
        err_must_be(man, rule, child, "true or false");
        return false;
    }
    *out = value->u.boolean;
    return true;
}

static bool read_whole(PluginManifest *man, const JsonValue *obj, const char *path, const char *key,
                       const char *rule, WholeRange range, const char *what, long long *out)
{
    const JsonValue *value = member_of(man, obj, path, key, rule, false);
    char child[PLUGIN_PATH_CAP];

    if (value == NULL) {
        return false;
    }
    child_path(child, path, key);
    if (!whole_in(value, range.lo, range.hi, out)) {
        err_must_be(man, rule, child, what);
        return false;
    }
    return true;
}

static bool read_duration(PluginManifest *man, const JsonValue *obj, const char *path,
                          const char *key, const char *rule, const char *what, long long lo,
                          long long hi, long long *out)
{
    const JsonValue *value = member_of(man, obj, path, key, rule, false);
    char child[PLUGIN_PATH_CAP];

    if (value == NULL) {
        return false;
    }
    child_path(child, path, key);
    if (!duration_in(value, lo, hi, out)) {
        err_must_be(man, rule, child, what);
        return false;
    }
    return true;
}

static bool read_text(PluginManifest *man, const JsonValue *obj, const char *path, const char *key,
                      const char *rule, bool required, char *dst, size_t cap, const char *what)
{
    const JsonValue *value = member_of(man, obj, path, key, rule, required);
    char child[PLUGIN_PATH_CAP];

    if (value == NULL) {
        return false;
    }
    child_path(child, path, key);
    if (!string_into(value, dst, cap)) {
        err_must_be(man, rule, child, what);
        return false;
    }
    return true;
}

static size_t code_points(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    size_t count = 0;

    for (; *p != 0U; p++) {
        if ((*p & (unsigned)UTF8_CONT_MASK) != (unsigned)UTF8_CONT) {
            count++;
        }
    }
    return count;
}

/* A description: at most 200 characters (the schema counts characters, not bytes). */
static bool read_description(PluginManifest *man, const JsonValue *obj, const char *path,
                             bool required, char *dst)
{
    static const char what[] = "a string of at most 200 characters";

    if (!read_text(man, obj, path, "description", "R2.5", required, dst, PLUGIN_TEXT_CAP, what)) {
        return false;
    }
    if (code_points(dst) > (size_t)PLUGIN_TEXT_CHARS) {
        char child[PLUGIN_PATH_CAP];

        child_path(child, path, "description");
        err_must_be(man, "R2.5", child, what);
        dst[0] = '\0';
        return false;
    }
    return true;
}

/* The text before item i of count: nothing, " or " (two items), ", or " (last), or ", ". */
static const char *enum_separator(size_t i, size_t count)
{
    if (i == 0U) {
        return "";
    }
    if (count == 2U) {
        return " or ";
    }
    if (i + 1U == count) {
        return ", or ";
    }
    return ", ";
}

/* `"a", "b", or "c"` built from the table that checks the value. */
static void enum_phrase(char *buf, Names names)
{
    size_t i = 0;

    buf[0] = '\0';
    for (i = 0; i < names.count; i++) {
        size_t used = strlen(buf);

        (void)snprintf(buf + used, (size_t)ENUM_PHRASE_CAP - used, "%s\"%s\"",
                       enum_separator(i, names.count), names.list[i]);
    }
}

/* One text item of a list of at most max strings, or of an enum list.
   An empty list is valid (the schema has no minItems there). False when the list itself is
   refused (not a list, or over its limit), so no item was read. */
// NOLINTBEGIN(bugprone-easily-swappable-parameters): same-typed pointers and sizes, by their order
static bool read_list(PluginManifest *man, const char *rule, const JsonValue *value,
                      const char *path, char *rows, size_t row_cap, size_t max_rows, size_t *count,
                      const Names *allowed, bool at_least_one)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    char what[LIST_WHAT_CAP];
    char item_what[ENUM_PHRASE_CAP];
    size_t i = 0;

    *count = 0;
    if (allowed != NULL) {
        enum_phrase(item_what, *allowed);
        if (at_least_one) {
            (void)snprintf(what, sizeof(what), "a list of 1 to %zu of %s", max_rows, item_what);
        } else {
            (void)snprintf(what, sizeof(what), "a list of at most %zu of %s", max_rows, item_what);
        }
    } else {
        (void)snprintf(what, sizeof(what), "a list of at most %zu strings", max_rows);
        (void)snprintf(item_what, sizeof(item_what), "a string of at most %zu bytes", row_cap - 1U);
    }
    if (value->type != JSON_ARRAY || (at_least_one && value->u.array.n == 0U) ||
        value->u.array.n > max_rows) {
        err_must_be(man, rule, path, what);
        return false;
    }
    for (i = 0; i < value->u.array.n; i++) {
        char child[PLUGIN_PATH_CAP];
        char *dst = rows + (*count * row_cap);

        index_path(child, path, i);
        if (!string_into(value->u.array.items[i], dst, row_cap) ||
            (allowed != NULL && !names_have(*allowed, dst))) {
            err_must_be(man, rule, child, item_what);
        } else {
            *count += 1U;
        }
    }
    return true;
}

/* Names ---------------------------------------------------------------- */

/* C-36 hyphen, or R25-2 sentence 6. fold is the name with each "-" as "_". */
// NOLINTBEGIN(bugprone-easily-swappable-parameters): the pointer of the name, then the name
static void err_name(PluginManifest *man, const char *path, const char *bad)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    char *fold = dup_text(bad);
    char *escaped = json_escaped(bad);
    char *escaped_fold = NULL;
    char *p = fold;

    if (fold == NULL || escaped == NULL) {
        free(fold);
        free(escaped);
        man->oom = true;
        return;
    }
    while ((p = strchr(p, '-')) != NULL) {
        *p = '_';
    }
    escaped_fold = json_escaped(fold);
    if (escaped_fold == NULL) {
        man->oom = true;
    } else if (strchr(bad, '-') != NULL) {
        add_invalid(man, "R2.22", path,
                    fmt_alloc("\"%s\" in plugin.json can't contain \"-\".", escaped),
                    fmt_alloc("Use \"%s\".", escaped_fold));
    } else {
        add_invalid(man, "R2.22", path, where_message(path, "\"%s\" isn't a valid name.", escaped),
                    dup_text("Use a lowercase letter, then lowercase letters, digits, or \"_\", up "
                             "to 40 characters."));
    }
    free(escaped_fold);
    free(escaped);
    free(fold);
}

/* A name member. dst gets it when it is valid. */
static bool read_name(PluginManifest *man, const JsonValue *obj, const char *path, const char *key,
                      const char *rule, bool required, char *dst)
{
    const JsonValue *value = member_of(man, obj, path, key, rule, required);
    char child[PLUGIN_PATH_CAP];

    if (value == NULL) {
        return false;
    }
    child_path(child, path, key);
    if (value->type != JSON_STRING) {
        err_must_be(man, "R2.22", child, "a string");
        return false;
    }
    if (!name_ok(value->u.string.bytes)) {
        err_name(man, child, value->u.string.bytes);
        return false;
    }
    memcpy(dst, value->u.string.bytes, value->u.string.len + 1U);
    return true;
}

/* Permissions, run, command -------------------------------------------- */

static void parse_needs(PluginManifest *man, const JsonValue *value, const char *path,
                        PluginEntry *entry)
{
    char what[ENUM_PHRASE_CAP];
    char item_what[NEED_WHAT_CAP];
    size_t i = 0;
    size_t j = 0;

    enum_phrase(what, NAMES(k_memory_fields));
    (void)snprintf(item_what, sizeof(item_what),
                   "a memory field (%s), or a \"remember.\" setting key", what);
    (void)snprintf(what, sizeof(what), "a list of at most %d items", PLUGIN_NEEDS_MAX);
    if (value->type != JSON_ARRAY || value->u.array.n > (size_t)PLUGIN_NEEDS_MAX) {
        err_must_be(man, "R2.16", path, what);
        return;
    }
    for (i = 0; i < value->u.array.n; i++) {
        char child[PLUGIN_PATH_CAP];
        char *dst = entry->needs[entry->nneeds];

        index_path(child, path, i);
        if (!string_into(value->u.array.items[i], dst, PLUGIN_NEED_CAP) ||
            !(names_have(NAMES(k_memory_fields), dst) || global_key_ok(dst))) {
            err_must_be(man, "R2.16", child, item_what);
            continue;
        }
        for (j = 0; j < entry->nneeds; j++) {
            if (strcmp(entry->needs[j], dst) == 0) {
                err_twice(man, "R2.16", path, child, dst, false, 0);
                break;
            }
        }
        if (j == entry->nneeds) {
            entry->nneeds += 1U;
        }
    }
}

static void parse_permissions(PluginManifest *man, const JsonValue *value, const char *path,
                              PluginEntry *entry)
{
    const JsonValue *obj = object_at(man, value, path, "R2.5", NAMES(k_perm_known), "R2.5");
    const JsonValue *needs = obj == NULL ? NULL : member_value(obj, "needs");

    if (needs != NULL) {
        char child[PLUGIN_PATH_CAP];

        child_path(child, path, "needs");
        parse_needs(man, needs, child, entry);
    }
}

/* G4: a relative command can't leave the plugin folder. */
static bool has_dotdot(const char *text)
{
    const char *seg = text;

    for (;;) {
        const char *slash = strchr(seg, '/');
        size_t n = slash == NULL ? strlen(seg) : (size_t)(slash - seg);

        if (n == 2U && seg[0] == '.' && seg[1] == '.') {
            return true;
        }
        if (slash == NULL) {
            return false;
        }
        seg = slash + 1;
    }
}

static void parse_command(PluginManifest *man, const JsonValue *value, const char *path,
                          PluginRun *run)
{
    char what[WHAT_CAP];
    size_t i = 0;
    bool ok = true;

    (void)snprintf(what, sizeof(what), "a list of 1 to %d strings", PLUGIN_ARGS_MAX);
    if (value->type != JSON_ARRAY || value->u.array.n == 0U ||
        value->u.array.n > (size_t)PLUGIN_ARGS_MAX) {
        err_must_be(man, "R2.8", path, what);
        return;
    }
    (void)snprintf(what, sizeof(what), "a string of 1 to %d bytes", PLUGIN_ARG_CAP - 1);
    for (i = 0; i < value->u.array.n; i++) {
        char child[PLUGIN_PATH_CAP];

        index_path(child, path, i);
        if (!string_into(value->u.array.items[i], run->argv[i], PLUGIN_ARG_CAP) ||
            run->argv[i][0] == '\0') {
            err_must_be(man, "R2.8", child, what);
            ok = false;
        } else if (i == 0U && run->argv[0][0] != '/' && has_dotdot(run->argv[0])) {
            add_invalid(man, "R2.8", child,
                        where_message(child, "a relative command can't contain \"..\"."),
                        dup_text("Use a path inside the plugin folder, or an absolute path."));
            ok = false;
        }
    }
    if (ok) {
        run->argc = value->u.array.n;
    }
}

static void parse_run(PluginManifest *man, const JsonValue *value, const char *path, PluginRun *run,
                      bool filter)
{
    Names known = NAMES(k_action_run_known);
    Names kinds = NAMES(k_action_process);
    const JsonValue *obj = NULL;
    const JsonValue *command = NULL;
    const JsonValue *process = NULL;
    char child[PLUGIN_PATH_CAP];

    if (filter) {
        known = NAMES(k_filter_run_known);
        kinds = NAMES(k_filter_process);
    }
    obj = object_at(man, value, path, "R2.5", known, "R2.5");
    if (obj == NULL) {
        return;
    }
    command = member_of(man, obj, path, "command", "R2.8", true);
    if (command != NULL) {
        child_path(child, path, "command");
        parse_command(man, command, child, run);
    }
    process = member_of(man, obj, path, "process", "R2.9", filter);
    if (process != NULL) {
        char text[PLUGIN_EVENT_CAP];

        child_path(child, path, "process");
        if (!string_into(process, text, sizeof(text)) || !names_have(kinds, text)) {
            char what[ENUM_PHRASE_CAP];

            enum_phrase(what, kinds);
            err_must_be(man, "R2.9", child, what);
        } else {
            run->persistent = strcmp(text, "persistent") == 0;
        }
    }
    run->timeout_ms = TIMEOUT_DEFAULT_MS;
    if (!filter && read_duration(man, obj, path, "timeout", "R2.5", "a duration like \"10m\"", 0,
                                 LLONG_MAX, &run->timeout_ms)) {
        run->has_timeout = true;
    }
}

/* The filter block of an action (R2.21) ------------------------------- */

/* One optional R2.21 list of obj: absent is fine, present is read by read_list. */
// NOLINTBEGIN(bugprone-easily-swappable-parameters): read_list's own order
static void filter_list(PluginManifest *man, const JsonValue *obj, const char *path,
                        const char *key, char *rows, size_t row_cap, size_t max_rows, size_t *count,
                        const Names *allowed)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    const JsonValue *item = member_value(obj, key);
    char child[PLUGIN_PATH_CAP];

    if (item != NULL) {
        child_path(child, path, key);
        (void)read_list(man, "R2.21", item, child, rows, row_cap, max_rows, count, allowed, false);
    }
}

static void parse_filter_block(PluginManifest *man, const JsonValue *value, const char *path,
                               PluginEntry *entry)
{
    const JsonValue *obj =
        object_at(man, value, path, "R2.21", NAMES(k_filter_block_known), "R2.21");
    const JsonValue *tags = NULL;
    char child[PLUGIN_PATH_CAP];
    const Names actors = NAMES(k_actors);
    const Names bins = NAMES(k_bins);
    const Names origins = NAMES(k_origins);

    if (obj == NULL) {
        return;
    }
    tags = member_value(obj, "tags");
    child_path(child, path, "tags");
    if (tags != NULL) {
        tags = object_at(man, tags, child, "R2.21", NAMES(k_tags_known), NULL);
    }
    if (tags != NULL) {
        filter_list(man, tags, child, "any", &entry->tags_any[0][0], PLUGIN_TAG_CAP,
                    PLUGIN_TAGS_MAX, &entry->n_tags_any, NULL);
        filter_list(man, tags, child, "all", &entry->tags_all[0][0], PLUGIN_TAG_CAP,
                    PLUGIN_TAGS_MAX, &entry->n_tags_all, NULL);
        entry->has_match_before =
            read_bool(man, tags, child, "match_before", "R2.21", false, &entry->match_before);
    }
    filter_list(man, obj, path, "created_by", &entry->created_by[0][0], PLUGIN_ACTOR_CAP,
                PLUGIN_ACTORS_MAX, &entry->n_created_by, &actors);
    filter_list(man, obj, path, "updated_by", &entry->updated_by[0][0], PLUGIN_ACTOR_CAP,
                PLUGIN_ACTORS_MAX, &entry->n_updated_by, &actors);
    filter_list(man, obj, path, "bins", &entry->bins[0][0], PLUGIN_BIN_CAP, PLUGIN_BINS_MAX,
                &entry->n_bins, &bins);
    filter_list(man, obj, path, "not_caused_by", &entry->not_caused_by[0][0], PLUGIN_CAUSE_CAP,
                PLUGIN_CAUSE_MAX, &entry->n_not_caused_by, NULL);
    filter_list(man, obj, path, "origins", &entry->origins[0][0], PLUGIN_ORIGIN_CAP,
                PLUGIN_ORIGINS_MAX, &entry->n_origins, &origins);
}

static void parse_events(PluginManifest *man, const JsonValue *value, const char *path,
                         PluginEntry *entry)
{
    const Names events = NAMES(k_events);
    size_t i = 0;
    bool others = false;

    (void)read_list(man, "R2.21", value, path, &entry->events[0][0], PLUGIN_EVENT_CAP,
                    PLUGIN_EVENTS_MAX, &entry->nevents, &events, true);

    for (i = 0; i < entry->nevents; i++) {
        if (strcmp(entry->events[i], "memory.*") == 0) {
            entry->all_events = true;
        } else {
            others = true;
        }
    }
    if (entry->all_events && others) {
        add_invalid(man, "R2.21", path,
                    where_message(path, "\"memory.*\" can't be listed with other events."),
                    dup_text("List \"memory.*\" alone."));
    }
}

static void parse_action(PluginManifest *man, const JsonValue *value, const char *path,
                         PluginEntry *entry)
{
    const JsonValue *obj = object_at(man, value, path, "R2.5", NAMES(k_action_known), NULL);
    const JsonValue *item = NULL;
    char child[PLUGIN_PATH_CAP];

    if (obj == NULL) {
        return;
    }
    (void)read_name(man, obj, path, "name", "R2.22", true, entry->name);
    item = member_of(man, obj, path, "events", "R2.21", true);
    if (item != NULL) {
        child_path(child, path, "events");
        parse_events(man, item, child, entry);
    }
    item = member_value(obj, "filter");
    if (item != NULL) {
        child_path(child, path, "filter");
        parse_filter_block(man, item, child, entry);
    }
    if (item == NULL || item->type != JSON_OBJECT || member_value(item, "origins") == NULL) {
        (void)snprintf(entry->origins[0], PLUGIN_ORIGIN_CAP, "%s", k_origins[0]);
        entry->n_origins = 1U;
    }
    item = member_of(man, obj, path, "run", "R2.5", true);
    if (item != NULL) {
        child_path(child, path, "run");
        parse_run(man, item, child, &entry->run, false);
    }
    item = member_value(obj, "permissions");
    if (item != NULL) {
        child_path(child, path, "permissions");
        parse_permissions(man, item, child, entry);
    }
}

static void parse_filter(PluginManifest *man, const JsonValue *value, const char *path,
                         PluginEntry *entry)
{
    const JsonValue *obj = object_at(man, value, path, "R2.5", NAMES(k_filter_known), NULL);
    const JsonValue *item = NULL;
    const Names points = NAMES(k_points);
    char child[PLUGIN_PATH_CAP];
    long long number = 0;

    entry->is_filter = true;
    entry->priority = PRIORITY_DEFAULT;
    entry->budget_ms = BUDGET_DEFAULT_MS;
    entry->grace_ms = GRACE_DEFAULT_MS;
    if (obj == NULL) {
        return;
    }
    (void)read_name(man, obj, path, "name", "R2.22", true, entry->name);
    item = member_of(man, obj, path, "point", "R2.5", true);
    if (item != NULL) {
        child_path(child, path, "point");
        if (!string_into(item, entry->point, sizeof(entry->point)) ||
            !names_have(points, entry->point)) {
            char what[ENUM_PHRASE_CAP];

            enum_phrase(what, points);
            err_must_be(man, "R2.5", child, what);
        }
    }
    if (read_whole(man, obj, path, "priority", "R2.5", (WholeRange){0, PRIORITY_MAX},
                   "a whole number from 0 to 1000", &number)) {
        entry->priority = (int)number;
    }
    (void)read_duration(man, obj, path, "budget", k_budget_rule, "a duration from 1ms to 30s",
                        BUDGET_MIN_MS, BUDGET_MAX_MS, &entry->budget_ms);
    (void)read_duration(man, obj, path, "cold_start_grace", "R2.13", "a duration from 1ms to 30s",
                        BUDGET_MIN_MS, BUDGET_MAX_MS, &entry->grace_ms);
    item = member_of(man, obj, path, "run", "R2.5", true);
    if (item != NULL) {
        child_path(child, path, "run");
        parse_run(man, item, child, &entry->run, true);
    }
    item = member_value(obj, "permissions");
    if (item != NULL) {
        child_path(child, path, "permissions");
        parse_permissions(man, item, child, entry);
    }
}

/* Settings ------------------------------------------------------------- */

/* What a default of each type must be (PluginSettingType order). */
static const char *const k_default_what[] = {"true or false",
                                             "a whole number",
                                             "one of the \"values\" in \"range\"",
                                             "a duration like \"10m\"",
                                             "a size like \"4MiB\"",
                                             "a string of at most 511 bytes",
                                             "a list of strings of at most 511 bytes as written"};

static bool default_valid(const JsonValue *value, PluginSettingType type)
{
    long long ignored = 0;

    if (type == PLUGIN_SETTING_BOOLEAN) {
        return value->type == JSON_BOOL;
    }
    if (type == PLUGIN_SETTING_INTEGER) {
        return whole_in(value, 0, LLONG_MAX, &ignored);
    }
    if (type == PLUGIN_SETTING_DURATION) {
        return duration_in(value, 0, LLONG_MAX, &ignored);
    }
    if (type == PLUGIN_SETTING_SIZE) {
        return size_in(value, LLONG_MAX, &ignored);
    }
    if (type == PLUGIN_SETTING_STRING_LIST) {
        if (value->type != JSON_ARRAY || value->src_len >= (size_t)PLUGIN_DEFAULT_CAP) {
            return false;
        }
        for (size_t i = 0; i < value->u.array.n; i++) {
            if (value->u.array.items[i]->type != JSON_STRING) {
                return false;
            }
        }
        return true;
    }
    if (value->type != JSON_STRING) {
        return false;
    }
    return value->u.string.len < (size_t)PLUGIN_DEFAULT_CAP;
}

/* The default of one setting, as text: the value, or the source JSON of a list. */
// NOLINTBEGIN(bugprone-easily-swappable-parameters): same-typed pointers and sizes, by their order
static void parse_default(PluginManifest *man, const JsonValue *value, const char *path,
                          const char *text, PluginSettingDecl *decl)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    decl->has_default = default_valid(value, decl->type);
    if (!decl->has_default) {
        err_must_be(man, "R2.11", path, k_default_what[decl->type]);
    } else if (decl->type == PLUGIN_SETTING_BOOLEAN) {
        const char *word = "false";

        if (value->u.boolean) {
            word = "true";
        }
        (void)snprintf(decl->default_text, sizeof(decl->default_text), "%s", word);
    } else if (decl->type == PLUGIN_SETTING_STRING_LIST) {
        memcpy(decl->default_text, text + value->src_at, value->src_len);
    } else if (value->type == JSON_STRING) {
        memcpy(decl->default_text, value->u.string.bytes, value->u.string.len);
    } else {
        long long number = 0;

        (void)whole_in(value, 0, LLONG_MAX, &number);
        (void)snprintf(decl->default_text, sizeof(decl->default_text), "%lld", number);
    }
}

static bool measure_member(PluginManifest *man, const JsonValue *range, const char *range_path,
                           const char *key, PluginSettingType type, long long *out)
{
    const JsonValue *value = member_value(range, key);
    char child[PLUGIN_PATH_CAP];
    bool valid = false;
    const char *what = "a whole number";

    if (value == NULL) {
        return false;
    }
    child_path(child, range_path, key);
    if (type == PLUGIN_SETTING_INTEGER) {
        valid = whole_in(value, 0, LLONG_MAX, out);
    } else if (type == PLUGIN_SETTING_DURATION) {
        valid = duration_in(value, 0, LLONG_MAX, out);
        what = "a duration like \"10m\"";
    } else {
        valid = size_in(value, LLONG_MAX, out);
        what = "a size like \"4MiB\"";
    }
    if (!valid) {
        err_must_be(man, "R2.11", child, what);
    }
    return valid;
}

static bool default_number(const PluginSettingDecl *decl, long long *out)
{
    char canon[NORM_MEASURE_CANON_CAP];

    if (decl->type == PLUGIN_SETTING_INTEGER) {
        return setting_integer(decl->default_text, out);
    }
    if (decl->type == PLUGIN_SETTING_DURATION) {
        return norm_duration_canonical(decl->default_text, out, canon, sizeof(canon));
    }
    return norm_size_canonical(decl->default_text, out, canon, sizeof(canon));
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters): same-typed pointers and sizes, by their order
static void parse_numeric_range(PluginManifest *man, const JsonValue *range, const char *range_path,
                                const char *setting_path, PluginSettingDecl *decl)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    long long number = 0;

    decl->has_min = measure_member(man, range, range_path, "min", decl->type, &decl->min_value);
    decl->has_max = measure_member(man, range, range_path, "max", decl->type, &decl->max_value);
    if (decl->has_min && decl->has_max && decl->min_value > decl->max_value) {
        add_invalid(man, "R2.11", range_path,
                    where_message(range_path, "\"min\" is greater than \"max\"."),
                    dup_text("Set \"min\" to \"max\" or lower."));
        return;
    }
    if (decl->has_default && default_number(decl, &number) &&
        ((decl->has_min && number < decl->min_value) ||
         (decl->has_max && number > decl->max_value))) {
        char child[PLUGIN_PATH_CAP];

        child_path(child, setting_path, "default");
        err_must_be(man, "R2.11", child, "a value inside \"range\"");
    }
}

static void parse_enum_range(PluginManifest *man, const JsonValue *range, const char *range_path,
                             const char *setting_path, PluginSettingDecl *decl)
{
    const JsonValue *values = member_of(man, range, range_path, "values", "R2.11", true);
    char child[PLUGIN_PATH_CAP];
    size_t i = 0;
    bool listed = false;

    if (values != NULL) {
        child_path(child, range_path, "values");
        listed = read_list(man, "R2.11", values, child, &decl->values[0][0], PLUGIN_VALUE_CAP,
                           PLUGIN_VALUES_MAX, &decl->nvalues, NULL, false);
    }
    while (i < decl->nvalues && strcmp(decl->values[i], decl->default_text) != 0) {
        i++;
    }
    if (listed && decl->has_default && i == decl->nvalues) {
        child_path(child, setting_path, "default");
        err_must_be(man, "R2.11", child, "one of the \"values\" in \"range\"");
    }
}

static void parse_range(PluginManifest *man, const JsonValue *value, const char *path,
                        PluginSettingDecl *decl)
{
    char child[PLUGIN_PATH_CAP];
    const JsonValue *range = NULL;

    child_path(child, path, "range");
    if (value == NULL) {
        if (decl->type == PLUGIN_SETTING_ENUM) {
            err_missing(man, "R2.11", path, "range");
        }
        return;
    }
    range = object_at(man, value, child, "R2.11", NAMES(k_range_known), NULL);
    if (range == NULL) {
        return;
    }
    if (decl->type == PLUGIN_SETTING_ENUM) {
        parse_enum_range(man, range, child, path, decl);
    } else if (decl->type == PLUGIN_SETTING_STRING_LIST) {
        long long items = 0;

        if (read_whole(man, range, child, "max_items", "R2.11", (WholeRange){1, LLONG_MAX},
                       "a whole number of 1 or more", &items)) {
            decl->max_items = items;
        }
    } else if (decl->type != PLUGIN_SETTING_BOOLEAN && decl->type != PLUGIN_SETTING_STRING) {
        parse_numeric_range(man, range, child, path, decl);
    }
}

static void parse_scope(PluginManifest *man, const JsonValue *value, const char *path,
                        PluginSettingDecl *decl)
{
    const Names scopes = NAMES(k_scopes);
    char text[PLUGIN_ACTOR_CAP];

    if (value == NULL) {
        return;
    }
    if (!string_into(value, text, sizeof(text)) || !names_have(scopes, text)) {
        char what[ENUM_PHRASE_CAP];
        char child[PLUGIN_PATH_CAP];

        child_path(child, path, "scope");
        enum_phrase(what, scopes);
        err_must_be(man, "R2.12", child, what);
        return;
    }
    decl->allow_user = strcmp(text, "brain") != 0;
    decl->allow_brain = strcmp(text, "user") != 0;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters): same-typed pointers and sizes, by their order
static void parse_setting(PluginManifest *man, const JsonValue *value, const char *path,
                          const char *text, const char *plugin_name, PluginSettingDecl *decl)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    const JsonValue *obj = object_at(man, value, path, "R2.11", NAMES(k_setting_known), NULL);
    const JsonValue *item = NULL;
    const Names types = NAMES(k_setting_types);
    char child[PLUGIN_PATH_CAP];
    bool typed = false;

    decl->allow_user = true;
    if (obj == NULL) {
        return;
    }
    item = member_of(man, obj, path, "key", "R2.11", true);
    if (item != NULL && item->type == JSON_STRING && strcmp(item->u.string.bytes, "enabled") == 0) {
        char *escaped = plugin_name == NULL ? NULL : json_escaped(plugin_name);

        child_path(child, path, "key");
        add_invalid(
            man, "R2.11", child,
            plugin_name == NULL
                ? dup_text("this plugin declares a setting named \"enabled\", and that name is "
                           "reserved.")
                : (escaped == NULL ? NULL
                                   : fmt_alloc("\"%s\" declares a setting named \"enabled\", and "
                                               "that name is reserved.",
                                               escaped)),
            dup_text("Rename that setting in plugin.json."));
        free(escaped);
    } else {
        (void)read_name(man, obj, path, "key", "R2.11", false, decl->key);
    }
    (void)read_description(man, obj, path, true, decl->description);
    item = member_of(man, obj, path, "type", "R2.11", true);
    if (item != NULL) {
        size_t i = 0;

        child_path(child, path, "type");
        for (i = 0; item->type == JSON_STRING && i < types.count; i++) {
            if (strcmp(types.list[i], item->u.string.bytes) == 0) {
                decl->type = (PluginSettingType)i;
                typed = true;
            }
        }
        if (!typed) {
            char what[ENUM_PHRASE_CAP];

            enum_phrase(what, types);
            err_must_be(man, "R2.11", child, what);
        }
    }
    item = member_of(man, obj, path, "default", "R2.11", true);
    if (typed && item != NULL) {
        child_path(child, path, "default");
        parse_default(man, item, child, text, decl);
    }
    if (typed) {
        parse_range(man, member_value(obj, "range"), path, decl);
    }
    parse_scope(man, member_value(obj, "scope"), path, decl);
}

/* Document ------------------------------------------------------------- */

static void parse_entry_list(PluginManifest *man, const JsonValue *value, const char *path,
                             bool filter)
{
    size_t i = 0;

    if (value == NULL) {
        return;
    }
    if (value->type != JSON_ARRAY || value->u.array.n == 0U ||
        value->u.array.n > (size_t)PLUGIN_LIST_MAX) {
        char what[WHAT_CAP];

        (void)snprintf(what, sizeof(what), "a list of 1 to %d entries", PLUGIN_LIST_MAX);
        err_must_be(man, "R2.5", path, what);
        return;
    }
    for (i = 0; i < value->u.array.n; i++) {
        char child[PLUGIN_PATH_CAP];

        index_path(child, path, i);
        if (filter) {
            parse_filter(man, value->u.array.items[i], child, &man->entries[man->nentries]);
        } else {
            parse_action(man, value->u.array.items[i], child, &man->entries[man->nentries]);
        }
        man->nentries += 1U;
    }
}

static bool commands_equal(const PluginRun *left, const PluginRun *right)
{
    size_t i = 0;

    if (left->argc != right->argc) {
        return false;
    }
    for (i = 0; i < left->argc; i++) {
        if (strcmp(left->argv[i], right->argv[i]) != 0) {
            return false;
        }
    }
    return true;
}

/* R25-2 sentence 8: a name used twice in one list. Each later use is reported. */
static void check_repeated_entry_names(PluginManifest *man, const char *list, size_t first,
                                       size_t count)
{
    for (size_t i = 1; i < count; i++) {
        const char *name = man->entries[first + i].name;

        for (size_t j = 0; j < i && name[0] != '\0'; j++) {
            if (strcmp(name, man->entries[first + j].name) == 0) {
                char path[PLUGIN_PATH_CAP];

                (void)snprintf(path, sizeof(path), "%s/%zu/name", list, i);
                err_twice(man, "R2.22", list, path, name, true, 0);
                break;
            }
        }
    }
}

static void check_repeated_setting_keys(PluginManifest *man)
{
    for (size_t i = 1; i < man->nsettings; i++) {
        const char *key = man->settings[i].key;

        for (size_t j = 0; j < i && key[0] != '\0'; j++) {
            if (strcmp(key, man->settings[j].key) == 0) {
                char path[PLUGIN_PATH_CAP];

                (void)snprintf(path, sizeof(path), "/settings/%zu/key", i);
                err_twice(man, "R2.22", "/settings", path, key, true, 0);
                break;
            }
        }
    }
}

/* The pointer of entry i: actions first, then filters. */
static void entry_pointer(char *buf, const PluginManifest *man, size_t i, size_t nactions)
{
    if (man->entries[i].is_filter) {
        (void)snprintf(buf, (size_t)PLUGIN_PATH_CAP, "/filters/%zu", i - nactions);
    } else {
        (void)snprintf(buf, (size_t)PLUGIN_PATH_CAP, "/actions/%zu", i);
    }
}

/* R2.10 (sentence 12: every persistent entry that differs from the first) and R2.17. */
static void check_persistent(PluginManifest *man, size_t nactions)
{
    const PluginRun *first = NULL;
    char first_path[PLUGIN_PATH_CAP];
    bool any_persistent = false;
    size_t i = 0;

    for (i = 0; i < man->nentries; i++) {
        if (!man->entries[i].run.persistent) {
            continue;
        }
        any_persistent = true;
        if (man->entries[i].run.argc == 0U) {
            continue; /* its command has its own error, so a comparison says nothing */
        }
        if (first == NULL) {
            first = &man->entries[i].run;
            entry_pointer(first_path, man, i, nactions);
        } else if (!commands_equal(first, &man->entries[i].run)) {
            char path[PLUGIN_PATH_CAP];
            char entry[PLUGIN_PATH_CAP];

            entry_pointer(entry, man, i, nactions);
            (void)snprintf(path, sizeof(path), "%s/run/command", entry);
            add_invalid(man, "R2.10", path,
                        where_message(path,
                                      "this entry runs a different command than %s. All "
                                      "persistent entries share one process.",
                                      first_path),
                        dup_text("Give every persistent entry the same command."));
        }
    }
    if (man->backfill && !any_persistent && man->nentries == nactions) {
        add_invalid(
            man, "R2.17", "/backfill/enabled",
            where_message("/backfill/enabled", "backfill is on, and no entry is persistent."),
            dup_text("Set \"process\" to \"persistent\" in an action's \"run\", or set \"enabled\" "
                     "to false."));
    }
}

static void parse_backfill(PluginManifest *man, const JsonValue *value)
{
    const JsonValue *obj = NULL;
    long long number = 0;

    man->max_batch_entries = BATCH_ENTRIES_MAX;
    man->max_batch_bytes = BATCH_BYTES_MAX;
    if (value == NULL) {
        return;
    }
    obj = object_at(man, value, "/backfill", "R2.17", NAMES(k_backfill_known), NULL);
    if (obj == NULL) {
        return;
    }
    (void)read_bool(man, obj, "/backfill", "enabled", "R2.17", true, &man->backfill);
    if (read_whole(man, obj, "/backfill", "max_batch_entries", "R2.18",
                   (WholeRange){1, BATCH_ENTRIES_MAX}, "a whole number from 1 to 100", &number)) {
        man->max_batch_entries = (int)number;
    }
    value = member_value(obj, "max_batch_size");
    if (value != NULL && !size_in(value, BATCH_BYTES_MAX, &man->max_batch_bytes)) {
        err_must_be(man, "R2.18", "/backfill/max_batch_size", "a size of at most 4MiB");
    }
}

/* The plugin name for R25-1, C-36 and sentences 9, 10 and 16: any string, as the file wrote it,
   valid or not. NULL when name is missing or not a string. */
static const char *usable_name(const JsonValue *root)
{
    const JsonValue *name = member_value(root, "name");

    return name != NULL && name->type == JSON_STRING ? name->u.string.bytes : NULL;
}

/* R25-1 for protocol, G2 for version. name is NULL when the file has no usable name. */
static void check_supported(PluginManifest *man, const JsonValue *root, const char *key,
                            const int *supported, size_t count, const char *name)
{
    const JsonValue *value = member_of(man, root, "", key, "R2.4", true);
    const char *verb = "uses plugin.json format";
    const char *noun = "format";
    char path[PLUGIN_PATH_CAP];
    char shown[WHOLE_TEXT_CAP];
    char *escaped = NULL;
    char *subject = NULL;
    char *message = NULL;
    char *fix = NULL;
    long long number = 0;
    WholeKind kind = WHOLE_NO;
    size_t i = 0;

    if (value == NULL) {
        return;
    }
    child_path(path, "", key);
    if (value->type == JSON_NUMBER) {
        kind = whole_of(value->u.number.bytes, &number, shown);
    }
    if (kind == WHOLE_NO) {
        err_must_be(man, "R2.4", path, "a whole number");
        return;
    }
    for (i = 0; i < count; i++) {
        if (kind == WHOLE_FITS && number == supported[i]) {
            return;
        }
    }
    if (strcmp(key, "protocol") == 0) {
        verb = "was written for plugin protocol";
        noun = "protocol";
    }
    /* ponytail: one protocol and one format exist, so the range is that number. The "<a> to <b>"
       form of R25-1 needs a second protocol to test it; add it with that protocol. */
    escaped = name == NULL ? NULL : json_escaped(name);
    if (name == NULL) {
        subject = dup_text("this plugin");
        fix = dup_text("Update remember, or install an older version of the plugin.");
    } else if (escaped != NULL) {
        subject = fmt_alloc("\"%s\"", escaped);
        fix = fmt_alloc("Update remember, or install an older version of \"%s\".", escaped);
    }
    if (subject != NULL) {
        message = where_message(path, "%s %s %s, and this remember reads %s %d.", subject, verb,
                                shown, noun, supported[0]);
    }
    free(subject);
    free(escaped);
    add_error(man,
              (Report){.code = k_unsupported,
                       .rule = "R2.4",
                       .path = path,
                       .supported = supported,
                       .nsupported = count},
              message, fix);
}

static bool semver_ok(const char *text)
{
    size_t i = 0;
    int dots = 0;
    bool digit = false;

    for (i = 0; text[i] != '\0'; i++) {
        char ch = text[i];

        if (ch >= '0' && ch <= '9') {
            digit = true;
        } else if (ch == '.' && digit) {
            dots++;
            digit = false;
        } else {
            break;
        }
    }
    if (dots != SEMVER_DOTS || !digit) {
        return false;
    }
    if (text[i] == '\0') {
        return true;
    }
    if ((text[i] != '-' && text[i] != '+') || text[i + 1U] == '\0') {
        return false;
    }
    return strspn(text + i + 1U,
                  "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz.-") ==
           strlen(text + i + 1U);
}

/* R25-2 sentence 5, with the two phrases of plugin_version (L12). */
static void parse_plugin_version(PluginManifest *man, const JsonValue *root)
{
    const JsonValue *value = member_of(man, root, "", "plugin_version", "R2.2", true);

    if (value == NULL) {
        return;
    }
    if (value->type == JSON_STRING && value->u.string.len >= sizeof(man->plugin_version)) {
        err_must_be(man, "R2.2", "/plugin_version", "a version of at most 63 bytes");
    } else if (value->type != JSON_STRING || !semver_ok(value->u.string.bytes)) {
        err_must_be(man, "R2.2", "/plugin_version", "a version like \"1.0.0\"");
    } else {
        memcpy(man->plugin_version, value->u.string.bytes, value->u.string.len + 1U);
    }
}

static void parse_identity(PluginManifest *man, const JsonValue *root)
{
    const char *name_text = usable_name(root);
    char text[PLUGIN_VER_CAP];

    if (read_text(man, root, "", "format", "R2.2", true, text, sizeof(text),
                  "\"remember-plugin\"") &&
        strcmp(text, "remember-plugin") != 0) {
        err_must_be(man, "R2.2", "/format", "\"remember-plugin\"");
    }
    check_supported(man, root, "version", k_formats, COUNT(k_formats), name_text);
    check_supported(man, root, "protocol", k_protocols, COUNT(k_protocols), name_text);
    (void)read_name(man, root, "", "name", "R2.22", true, man->name);
    parse_plugin_version(man, root);
    man->has_description = read_description(man, root, "", false, man->description);
    man->data_version = 1;
    (void)read_whole(man, root, "", "data_version", "R2.14", (WholeRange){1, LLONG_MAX},
                     "a whole number of 1 or more", &man->data_version);
    man->has_idle_timeout =
        read_duration(man, root, "", "idle_timeout", "R2.20", "a duration like \"10m\"", 0,
                      LLONG_MAX, &man->idle_timeout_ms);
}

/* R25-2 sentence 7b: run, permissions and filter are known fields in the wrong place. */
static void reject_misplaced(PluginManifest *man, const JsonValue *root)
{
    bool seen[COUNT(k_root_misplaced)] = {false};
    size_t i = 0;

    for (i = 0; i < root->u.object.n; i++) {
        const char *key = root->u.object.members[i].key;
        char *child = NULL;
        char *fix = NULL;
        size_t j = 0;

        while (j < COUNT(k_root_misplaced) && strcmp(k_root_misplaced[j], key) != 0) {
            j++;
        }
        if (j == COUNT(k_root_misplaced) || seen[j]) {
            continue; /* not misplaced, or a repeat, which check_repeats reports */
        }
        seen[j] = true;
        child = pointer_join("", key);
        if (strcmp(key, "filter") == 0) {
            fix = dup_text("Move \"filter\" into an action.");
        } else {
            fix = fmt_alloc("Move \"%s\" into an action or a filter.", key);
        }
        if (child == NULL || fix == NULL) {
            free(fix);
            man->oom = true;
        } else {
            add_invalid(man, strcmp(key, "filter") == 0 ? "R2.21" : "R2.5", child,
                        where_message("", "\"%s\" can't be at the top level.", key), fix);
        }
        free(child);
    }
}

/* One reference token of a pointer (n bytes, "~0" and "~1" undone) equals key. */
static bool token_is(const char *token, size_t n, const char *key)
{
    size_t i = 0;

    for (i = 0; i < n; i++, key++) {
        char c = token[i];

        if (c == '~' && i + 1U < n) {
            c = token[++i] == '1' ? '/' : '~';
        }
        if (*key != c) {
            return false;
        }
    }
    return *key == '\0';
}

/* The source offset of the deepest value pointer reaches. A repeated key is its first member, the
   one member_value reads. */
static size_t doc_at(const JsonValue *root, const char *pointer)
{
    const JsonValue *at = root;
    const char *p = pointer;

    while (*p == '/') {
        const char *token = ++p;
        const JsonValue *next = NULL;
        size_t n = strcspn(token, "/");
        size_t i = 0;

        p += n;
        if (at->type == JSON_OBJECT) {
            for (i = 0; next == NULL && i < at->u.object.n; i++) {
                if (token_is(token, n, at->u.object.members[i].key)) {
                    next = at->u.object.members[i].value;
                }
            }
        } else if (at->type == JSON_ARRAY && n > 0U && strspn(token, "0123456789") == n) {
            unsigned long long index = strtoull(token, NULL, DECIMAL);

            next = index < at->u.array.n ? at->u.array.items[index] : NULL;
        }
        if (next == NULL) {
            break;
        }
        at = next;
    }
    return at->src_at;
}

typedef struct Placed {
    size_t at;
    size_t index;
} Placed;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the qsort comparator signature
static int placed_order(const void *left, const void *right)
{
    const Placed *a = (const Placed *)left;
    const Placed *b = (const Placed *)right;

    if (a->at != b->at) {
        return a->at < b->at ? -1 : 1;
    }
    return a->index < b->index ? -1 : 1;
}

/* R25-2: "The order is document order." Each error sits at the offset of what it names; errors at
   one offset keep the order they were found in.
   ponytail: an error without a place walks its pointer, linear in each object's members. Only a
   bounded number of errors (one per schema field, entry or list item) lack a place; the unbounded
   ones (repeats, unknown fields) are placed when found. */
static void sort_errors(PluginManifest *man, const JsonValue *root)
{
    size_t n = man->nerrors;
    Placed *order = NULL;
    PluginError *sorted = NULL;
    size_t i = 0;

    if (n < 2U) {
        return;
    }
    order = (Placed *)malloc(n * sizeof(*order));
    sorted = (PluginError *)malloc(n * sizeof(*sorted));
    if (order == NULL || sorted == NULL) {
        free(order);
        free(sorted);
        man->oom = true;
        return;
    }
    for (i = 0; i < n; i++) {
        if (man->errors[i].at == SIZE_MAX) {
            man->errors[i].at = doc_at(root, man->errors[i].path);
        }
        order[i] = (Placed){.at = man->errors[i].at, .index = i};
    }
    qsort(order, n, sizeof(*order), placed_order);
    for (i = 0; i < n; i++) {
        sorted[i] = man->errors[order[i].index];
    }
    free(order);
    free(man->errors);
    man->errors = sorted;
    man->errors_cap = n;
}

static PluginParseStatus status_of(const PluginManifest *man)
{
    size_t i = 0;

    for (i = 0; i < man->nerrors; i++) {
        if (man->errors[i].code == k_unsupported) {
            return PLUGIN_PARSE_UNSUPPORTED;
        }
    }
    return man->nerrors > 0U ? PLUGIN_PARSE_INVALID : PLUGIN_PARSE_OK;
}

static void parse_settings(PluginManifest *man, const JsonValue *value, const char *text,
                           const char *plugin_name)
{
    size_t i = 0;

    if (value == NULL) {
        return;
    }
    if (value->type != JSON_ARRAY || value->u.array.n > (size_t)PLUGIN_SETTINGS_MAX) {
        char what[WHAT_CAP];

        (void)snprintf(what, sizeof(what), "a list of at most %d settings", PLUGIN_SETTINGS_MAX);
        err_must_be(man, "R2.11", "/settings", what);
        return;
    }
    for (i = 0; i < value->u.array.n; i++) {
        char child[PLUGIN_PATH_CAP];

        index_path(child, "/settings", i);
        parse_setting(man, value->u.array.items[i], child, text, plugin_name,
                      &man->settings[man->nsettings]);
        man->nsettings += 1U;
    }
}

/* Takes root and man: frees root, and frees man on OOM. */
static PluginParseStatus parse_document(PluginManifest *man, JsonValue *root, const char *text,
                                        PluginManifest **out)
{
    const JsonValue *actions = member_value(root, "actions");
    const JsonValue *filters = member_value(root, "filters");
    size_t nactions = 0;

    check_repeats(man, root, "");
    check_members(man, root, "", NAMES(k_root_known), NULL);
    reject_misplaced(man, root);
    parse_identity(man, root);
    parse_backfill(man, member_value(root, "backfill"));
    if (actions == NULL && filters == NULL) {
        add_invalid(man, "R2.5", "",
                    where_message("", "\"actions\" and \"filters\" are both missing."),
                    dup_text("Add \"actions\" or \"filters\"."));
    }
    parse_entry_list(man, actions, "/actions", false);
    nactions = man->nentries;
    parse_entry_list(man, filters, "/filters", true);
    parse_settings(man, member_value(root, "settings"), text, usable_name(root));
    check_repeated_entry_names(man, "/actions", 0, nactions);
    check_repeated_entry_names(man, "/filters", nactions, man->nentries - nactions);
    check_repeated_setting_keys(man);
    check_persistent(man, nactions);
    sort_errors(man, root);
    jsonread_free(root);
    if (man->oom) {
        plugin_manifest_free(man);
        return PLUGIN_PARSE_OOM;
    }
    *out = man;
    return status_of(man);
}

/* cppcheck-suppress staticFunction ; public API (plugin_manifest.h), used by the plugin commands */
PluginParseStatus plugin_manifest_parse(const char *text, size_t len, PluginManifest **out)
{
    JsonValue *root = NULL;
    JsonReadStatus read = JSONREAD_OK;
    PluginManifest *man = NULL;
    bool is_object = false;

    if (out == NULL) {
        return PLUGIN_PARSE_SYNTAX;
    }
    *out = NULL;
    man = (PluginManifest *)malloc(sizeof(*man));
    if (man == NULL) {
        return PLUGIN_PARSE_OOM;
    }
    memset(man, 0, sizeof(*man));
    read = text == NULL ? JSONREAD_ERR_SYNTAX : jsonread_parse(text, len, &root);
    if (read == JSONREAD_OK && root->type == JSON_OBJECT) {
        return parse_document(man, root, text, out);
    }
    is_object = read == JSONREAD_OK;
    jsonread_free(root);
    if (read == JSONREAD_ERR_OOM) {
        plugin_manifest_free(man);
        return PLUGIN_PARSE_OOM;
    }
    if (is_object) {
        add_invalid(man, "R2.2", "", dup_text("plugin.json must hold one JSON object."),
                    dup_text("Make the top level of plugin.json an object."));
    } else if (read == JSONREAD_ERR_NESTING) {
        add_invalid(man, "R2.2", "", dup_text("plugin.json is nested more than 64 levels deep."),
                    dup_text("Reduce the nesting in plugin.json to 64 levels or fewer."));
    } else {
        add_invalid(man, "R2.2", "", dup_text("plugin.json isn't valid JSON."),
                    dup_text("Fix the JSON syntax in plugin.json."));
    }
    if (man->oom) {
        plugin_manifest_free(man);
        return PLUGIN_PARSE_OOM;
    }
    *out = man;
    return PLUGIN_PARSE_SYNTAX;
}

/* cppcheck-suppress staticFunction ; public API (plugin_manifest.h) */
void plugin_manifest_free(PluginManifest *manifest)
{
    size_t i = 0;

    if (manifest == NULL) {
        return;
    }
    for (i = 0; i < manifest->nerrors; i++) {
        free(manifest->errors[i].path);
        free(manifest->errors[i].message);
        free(manifest->errors[i].fix);
    }
    for (i = 0; i < manifest->nwarnings; i++) {
        free(manifest->warnings[i].path);
        free(manifest->warnings[i].field);
    }
    free(manifest->errors);
    free(manifest->warnings);
    free(manifest);
}
