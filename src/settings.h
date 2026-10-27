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
   numeric range. default_text is NULL when the key is unset until written. */
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

#endif /* REMEMBER_SETTINGS_H */
