#ifndef REMEMBER_SETTINGS_H
#define REMEMBER_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

/* A full key is at most 96 characters (007). A row holds at most 8 enum values: the manifest's
   own limit (decision:2026-11-13-plugin-manifest-limits L10). */
enum { SETTING_KEY_MAX = 96, SETTING_ENUM_VALUES_MAX = 8 };

/* STRING and STRING_LIST are the two compiled-in types with their own grammar (a helper id, a
   bucket prefix). The last five are the types of a plugin manifest's settings
   (decision:2026-11-11-plugin-enabled-bool, specs/SETTINGS.md "Plugin keys"). */
typedef enum SettingType {
    SETTING_INTEGER = 0,
    SETTING_DURATION,
    SETTING_STRING,
    SETTING_STRING_LIST,
    SETTING_BOOLEAN,  /* stored as the JSON literal true or false */
    SETTING_ENUM,     /* one of values, stored as a JSON string */
    SETTING_SIZE,     /* stored as a JSON string, "4MiB" */
    SETTING_TEXT,     /* a plugin string, stored as a JSON string */
    SETTING_TEXT_LIST /* a plugin string_list, stored as a JSON array of strings */
} SettingType;

/* One registry row. min/max are milliseconds for a duration, bytes for a size and the integer
   itself for an integer. Both 0 and has_range false means the type has no numeric range. For a
   SETTING_ENUM, values holds the nvalues allowed strings. max_items 0 means a list has no item
   cap. default_text is NULL when the row has no single stored default, the plain text of the
   default for a scalar, and the compact JSON array for a SETTING_TEXT_LIST. Prefixes still
   resolve to project: and status: with no row written until set. */
typedef struct SettingSpec {
    const char *key;
    const char *default_text;
    /* The explain line (C-13, decision:plan20-explain-description-drafts). */
    const char *description;
    /* One related key or NULL (decision:plan20-empty-list-related). */
    const char *related;
    const char *const *values;
    long long min_value;
    long long max_value;
    size_t nvalues;
    int max_items;
    SettingType type;
    bool has_range;
    bool allow_brain;
    bool allow_user;
    /* NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers) */
    char pad_[5]; /* explicit tail padding (kept -Wpadded-clean) */
} SettingSpec;

/* The compiled-in rows plus the rows generated for installed plugins. Every function that
   looks a key up takes one (decision:2026-11-16-plan30-registry-explicit); there is no
   process-wide registry. A SettingSpec pointer from a registry is good until that registry
   is changed or freed. */
typedef struct SettingsRegistry SettingsRegistry;

/* The compiled-in rows only. Never changed, never freed. */
const SettingsRegistry *settings_registry_builtin(void);

/* A registry that starts as the compiled-in rows. The caller frees it. NULL when memory runs
   out. */
SettingsRegistry *settings_registry_new(void);
void settings_registry_free(SettingsRegistry *reg);

/* Compiled rows first, then generated rows in the order they were added. */
size_t settings_registry_count(const SettingsRegistry *reg);
const SettingSpec *settings_registry_at(const SettingsRegistry *reg, size_t i);

/* Copy row (every string in it too) onto the end. False when memory runs out, when a row with
   that key is already there, or when it has more than SETTING_ENUM_VALUES_MAX values. */
bool settings_registry_add(SettingsRegistry *reg, const SettingSpec *row);

/* Remove every generated row whose key starts with prefix. A compiled row stays. Returns how
   many went. */
size_t settings_registry_remove_prefix(SettingsRegistry *reg, const char *prefix);

/* Rewrite '-' to '_' and require namespace '.' segment { '.' segment }
   with 1 to 3 segments after the namespace. Each segment is
   [a-z][a-z0-9_]{0,39}. False clears out when out_cap > 0. */
bool setting_key_fold(const char *src, char *out, size_t out_cap);

/* ^(0|[1-9][0-9]*)$ and the value fits in long long. */
bool setting_integer(const char *src, long long *out);

/* Fold, then the registry row. NULL when the text is not one of those keys. */
const SettingSpec *setting_by_key(const SettingsRegistry *reg, const char *typed);

/* Plugin text (SETTING_TEXT, each SETTING_TEXT_LIST item, an enum value): valid UTF-8, no
   control byte or DEL, at most SETTING_TEXT_MAX bytes
   (decision:2026-11-16-plan30-plugin-string-values). */
bool setting_text_ok(const char *text);

/* The text of a stored JSON string written by this program: only the escapes \" and \\ and
   the setting_text_ok rule on the result. False clears out when out_cap > 0. */
bool setting_text_unquote(const char *json, char *out, size_t out_cap);

/* A stored JSON array of those strings, [] or ["a","b"], as its items joined by one space.
   False when the text is anything else; out is then empty. An empty array gives "". */
bool setting_text_list_join(const char *json, char *out, size_t out_cap);

/* Inner bytes of a JSON string with no escapes and no controls.
   False when text is not that shape or does not fit inner_cap. */
bool setting_quoted_plain(const char *text, char *inner, size_t inner_cap);

/* Stored JSON is at most 16 KiB (007 ST-4.4). CAP is that plus a NUL and
   enough bytes for the buffer to stay 4-byte aligned. */
enum {
    SETTING_TEXT_MAX = 511,
    SETTING_VALUE_MAX = 16384,
    SETTING_VALUE_CAP = 16388,
    SETTING_ROW_QUOTA = 256,
    SETTING_HISTORY_KEEP = 50,
    /* One canonical actor (ACTOR_JSON_CAP) and one 24-character UTC stamp. */
    SETTING_ACTOR_CAP = 96,
    SETTING_AT_CAP = 32
};

typedef enum SettingStatus {
    SETTING_OK = 0,
    SETTING_ERR_UNKNOWN, /* setting_unknown */
    SETTING_ERR_INVALID, /* setting_invalid_value */
    SETTING_ERR_RANGE,   /* setting_out_of_range */
    SETTING_ERR_SCOPE,   /* setting_scope_mismatch */
    SETTING_ERR_QUOTA,   /* quota_exceeded */
    SETTING_ERR_STORE,   /* brain or user.db failure; err holds that sentence */
    SETTING_ERR_DOCUMENT /* import_parse: the file is not a settings document */
} SettingStatus;

typedef enum SettingLayer {
    SETTING_LAYER_AUTO = 0, /* user, unless the key allows only the brain */
    SETTING_LAYER_USER,
    SETTING_LAYER_BRAIN
} SettingLayer;

typedef enum SettingFrom {
    SETTING_FROM_DEFAULT = 0,
    SETTING_FROM_USER,
    SETTING_FROM_BRAIN,
    SETTING_FROM_FLAG
} SettingFrom;

/* One set. items are the typed values, one per string_list element.
   updated_by NULL stores {"kind":"human","id":null}. */
typedef struct {
    const char *key;
    const char *const *items;
    const char *updated_by;
    size_t nitems;
    SettingLayer layer;
    char pad_[4];
} SettingWrite;

typedef struct {
    SettingStatus status;
    SettingLayer layer;
    bool changed;
    char pad_[3];
} SettingWriteResult;

/* value is the effective JSON. user_live and brain_live mean a stored row's
   value fit. A tombstone is not live. A live row in a layer the key does not
   allow still has its value here; setting_pick ignores that layer. Stamps are
   empty unless that layer is live. */
typedef struct {
    char value[SETTING_VALUE_CAP];
    char user_value[SETTING_VALUE_CAP];
    char brain_value[SETTING_VALUE_CAP];
    char user_by[SETTING_ACTOR_CAP];
    char brain_by[SETTING_ACTOR_CAP];
    char user_at[SETTING_AT_CAP];
    char brain_at[SETTING_AT_CAP];
    SettingStatus status;
    SettingFrom from;
    bool user_live;
    bool brain_live;
    /* A stored plugin value of an allowed layer that the row refuses (a plugin update changed the
       type, dropped an enum value or narrowed a range): that layer is not live, and the read
       fell to the next (decision:2026-11-17-plan30-review-grill G1 A). Its value stays here. */
    bool user_refused;
    bool brain_refused;
} SettingRead;

/* Local calendar fields for "21 Oct at 10:04". month is 1..12. utc_suffix
   appends " UTC" when the zone could not be read (ST-7.16). */
typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int year_now;
    bool utc_suffix;
    char pad_[3];
} SettingWhen;

/* The plugin segment of a key that names one: typed folds to plugin.<name>.<...>. False when it
   does not, or the name does not fit cap. */
bool setting_plugin_name(const char *typed, char *name, size_t cap);

/* A key of a plugin that is not installed (ST-5.10): the key folds to plugin.<name>.<...>, and the
   registry has no plugin.<name>.enabled. name gets <name> (cap at least SETTING_KEY_MAX + 1). */
bool setting_plugin_missing(const SettingsRegistry *reg, const char *typed, char *name, size_t cap);

/* Locked error code, or "" when the status has no settings code. */
const char *setting_status_code(SettingStatus st);

/* [a-z][a-z0-9_-]{0,39}. The credential helper id grammar (G2-01). */
bool helper_id_ok(const char *src);

/* Fold the key and choose the layer a set or reset writes. */
SettingStatus setting_choose_layer(const SettingsRegistry *reg, const char *key,
                                   SettingLayer requested, const SettingSpec **spec_out,
                                   SettingLayer *layer_out, char *err, size_t errlen);

/* Fold the key, check the layer, and write the canonical JSON value.
   Refusals leave err empty; only a store failure writes it. */
SettingStatus setting_prepare(const SettingsRegistry *reg, const char *key,
                              const char *const *items, size_t nitems, SettingLayer requested,
                              char *json, size_t json_cap, const SettingSpec **spec_out,
                              SettingLayer *layer_out, char *err, size_t errlen);

/* SETTING_ERR_INVALID when a bucket prefix item is refused: *item is its
   index, and *other the earlier item it repeats, or SIZE_MAX when the item
   isn't a prefix at all. The caller words the sentence from its own items. */
SettingStatus setting_prefix_issue(const char *const *items, size_t nitems, size_t *item,
                                   size_t *other);

/* Last layer with a value wins. A NULL layer pointer, or a layer the key
   does not allow, is no row. Copies the winning JSON into out. An unset key
   with no default leaves out empty. */
SettingFrom setting_pick(const SettingSpec *spec, const char *user_json, const char *brain_json,
                         const char *flag_json, char *out, size_t out_cap);

/* Canonical JSON of the compiled default. False leaves out empty. */
bool setting_default_json(const SettingSpec *spec, char *out, size_t cap);

/* "21 Oct at 10:04", with the year when it is not year_now, and " UTC" when
   utc_suffix is set. False when the fields are not a date or buf is short. */
bool setting_format_when(SettingWhen when, char *buf, size_t cap);

/* The one full key ST-1.5 would name, or NULL when there is not exactly one.
   Two or three candidates have no approved sentence. */
const char *setting_suggestion(const SettingsRegistry *reg, const char *typed);

/* True when json is what a set of its own value would store for spec: the type, the range, the
   enum values and the item cap all hold, in canonical form. A bucket prefix list is checked for
   shape and text only (its items are not folded again). */
bool setting_stored_ok(const SettingSpec *spec, const char *json);

/* The canonical JSON of items for spec, as setting_prepare writes it, with no key or layer to
   look up. json_cap must be SETTING_VALUE_CAP. The status is setting_prepare's. */
SettingStatus setting_canon_value(const SettingSpec *spec, const char *const *items, size_t nitems,
                                  char *json, size_t json_cap, char *err, size_t errlen);

#endif /* REMEMBER_SETTINGS_H */
