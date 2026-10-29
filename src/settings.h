#ifndef REMEMBER_SETTINGS_H
#define REMEMBER_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

/* A full key is at most 96 characters (007). */
enum { SETTING_KEY_MAX = 96 };

typedef enum SettingType {
    SETTING_INTEGER = 0,
    SETTING_DURATION,
    SETTING_STRING,
    SETTING_STRING_LIST
} SettingType;

/* One compiled-in registry row. min/max are milliseconds for a duration
   and the integer itself for an integer. Both 0 means the type has no
   numeric range. default_text is NULL when the row has no single stored
   default. Prefixes still resolve to project: and status: with no row
   written until set. */
typedef struct SettingSpec {
    const char *key;
    const char *default_text;
    long long min_value;
    long long max_value;
    SettingType type;
    bool allow_brain;
    bool allow_user;
    char pad_[2]; /* explicit tail padding (kept -Wpadded-clean) */
} SettingSpec;

/* Rewrite '-' to '_' and require namespace '.' segment { '.' segment }
   with 1 to 3 segments after the namespace. Each segment is
   [a-z][a-z0-9_]{0,39}. False clears out when out_cap > 0. */
bool setting_key_fold(const char *src, char *out, size_t out_cap);

/* ^(0|[1-9][0-9]*)$ and the value fits in long long. */
bool setting_integer(const char *src, long long *out);

const SettingSpec *setting_registry(size_t *count);

/* Fold, then the registry row. NULL when the text is not one of those keys. */
const SettingSpec *setting_by_key(const char *typed);

/* Stored JSON is at most 16 KiB (007 ST-4.4). CAP is that plus a NUL and
   enough bytes for the buffer to stay 4-byte aligned. */
enum {
    SETTING_VALUE_MAX = 16384,
    SETTING_VALUE_CAP = 16388,
    SETTING_ROW_QUOTA = 256,
    SETTING_HISTORY_KEEP = 50
};

typedef enum SettingStatus {
    SETTING_OK = 0,
    SETTING_ERR_UNKNOWN, /* setting_unknown */
    SETTING_ERR_INVALID, /* setting_invalid_value */
    SETTING_ERR_RANGE,   /* setting_out_of_range */
    SETTING_ERR_SCOPE,   /* setting_scope_mismatch */
    SETTING_ERR_QUOTA,   /* quota_exceeded */
    SETTING_ERR_STORE    /* brain or user.db failure; err holds that sentence */
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

/* value is the effective JSON. A layer the key does not allow is empty and
   its live flag is false. A tombstone is not live. */
typedef struct {
    char value[SETTING_VALUE_CAP];
    char user_value[SETTING_VALUE_CAP];
    char brain_value[SETTING_VALUE_CAP];
    SettingStatus status;
    SettingFrom from;
    bool user_live;
    bool brain_live;
    char pad_[2];
} SettingRead;

/* Locked error code, or "" when the status has no settings code. */
const char *setting_status_code(SettingStatus st);

/* [a-z][a-z0-9_-]{0,39}. The credential helper id grammar (G2-01). */
bool helper_id_ok(const char *src);

/* Fold the key and choose the layer a set or reset writes. */
SettingStatus setting_choose_layer(const char *key, SettingLayer requested,
                                   const SettingSpec **spec_out, SettingLayer *layer_out, char *err,
                                   size_t errlen);

/* Fold the key, check the layer, and write the canonical JSON value.
   A prefix failure writes its locked sentence into err. Other refusals
   leave err empty when the sentence is still COPY-PENDING. */
SettingStatus setting_prepare(const char *key, const char *const *items, size_t nitems,
                              SettingLayer requested, char *json, size_t json_cap,
                              const SettingSpec **spec_out, SettingLayer *layer_out, char *err,
                              size_t errlen);

/* Last layer with a value wins. A NULL layer pointer is no row. Copies the
   winning JSON into out. An unset key with no default leaves out empty. */
SettingFrom setting_pick(const SettingSpec *spec, const char *user_json, const char *brain_json,
                         const char *flag_json, char *out, size_t out_cap);

#endif /* REMEMBER_SETTINGS_H */
