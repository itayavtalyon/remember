#ifndef REMEMBER_PLUGIN_MANIFEST_H
#define REMEMBER_PLUGIN_MANIFEST_H

#include <stdbool.h>
#include <stddef.h>

/* plugin.json (specs/PLUGIN-MANIFEST.md, PLUGIN-PROTOCOL R2). A pure parser:
   it reads the bytes the caller passes and touches no file. Names are
   ^[a-z][a-z0-9_]{0,39}$. A hyphen is plugin_manifest_invalid and prints C-36. */

/* The caps below that the schema does not state are the limits of
   decision:2026-11-13-plugin-manifest-limits (L1 to L14). A document past one is
   plugin_manifest_invalid, and its sentence names the limit. The schema's own limits are the name
   (40), the description (200 characters), a setting key (96), and the ranges of priority, budget
   and batch size. */
enum {
    PLUGIN_NAME_CAP = 40,
    PLUGIN_TEXT_CHARS = 200,
    PLUGIN_TEXT_CAP = (4 * PLUGIN_TEXT_CHARS) + 1, /* 200 characters of UTF-8, and a NUL */
    PLUGIN_VER_CAP = 64,
    PLUGIN_ARG_CAP = 256,
    PLUGIN_ARGS_MAX = 8,
    PLUGIN_NEEDS_MAX = 16,
    PLUGIN_NEED_CAP = 97, /* the schema allows 96 characters, and a NUL */
    PLUGIN_EVENTS_MAX = 8,
    PLUGIN_EVENT_CAP = 32,
    PLUGIN_LIST_MAX = 16, /* actions, and filters, each */
    PLUGIN_ENTRIES_MAX = 2 * PLUGIN_LIST_MAX,
    PLUGIN_SETTINGS_MAX = 16,
    PLUGIN_PATH_CAP = 96,
    PLUGIN_VALUES_MAX = 8,
    PLUGIN_VALUE_CAP = 64,
    PLUGIN_TAGS_MAX = 8,
    PLUGIN_TAG_CAP = 64,
    PLUGIN_ACTORS_MAX = 5,
    PLUGIN_ACTOR_CAP = 16,
    PLUGIN_BINS_MAX = 3,
    PLUGIN_BIN_CAP = 16,
    PLUGIN_CAUSE_MAX = 8,
    PLUGIN_CAUSE_CAP = 80,
    PLUGIN_ORIGINS_MAX = 3,
    PLUGIN_ORIGIN_CAP = 8,
    PLUGIN_POINT_CAP = 32,
    PLUGIN_DEFAULT_CAP = 512,
    /* Explicit tail padding, so the structs stay -Wpadded clean. */
    PLUGIN_ENTRY_TAIL = 11,
    PLUGIN_DECL_TAIL = 13,
    PLUGIN_MANIFEST_TAIL = 6,
    PLUGIN_RUN_PAD = 6
};

typedef enum PluginParseStatus {
    PLUGIN_PARSE_OK = 0,
    PLUGIN_PARSE_INVALID,     /* plugin_manifest_invalid */
    PLUGIN_PARSE_UNSUPPORTED, /* plugin_protocol_unsupported */
    PLUGIN_PARSE_SYNTAX,      /* not a JSON object */
    PLUGIN_PARSE_OOM
} PluginParseStatus;

/* Heap strings sized to what they hold: a hostile key or value is echoed
   whole, never cut (standard:c). message and fix hold no control byte: every
   piece of plugin-controlled text in them is JSON-escaped, DEL and C1 included
   (R25-2 Q2). path is the raw RFC 6901 pointer and can hold any byte but NUL:
   the JSON writer escapes it, and a plain printer that shows it must mask it.
   code and rule point at literals. supported is NULL, or the numbers this
   remember reads (detail.supported). */
typedef struct PluginError {
    const char *code;
    const char *rule;
    const int *supported;
    char *path;
    char *message;
    char *fix;
    size_t nsupported;
    size_t at; /* byte offset in plugin.json of what the error names; errors come in this order */
} PluginError;

/* An unknown field on an open object (R2.6). The caller reports each through
   diag_unknown_field (C-35). path is the JSON Pointer of the object that holds it.
   path and field are raw bytes from the file. */
typedef struct PluginUnknown {
    char *path;
    char *field;
} PluginUnknown;

typedef struct PluginRun {
    char argv[PLUGIN_ARGS_MAX][PLUGIN_ARG_CAP];
    size_t argc;
    long long timeout_ms;
    bool persistent;
    bool has_timeout;
    char pad_[PLUGIN_RUN_PAD];
} PluginRun;

typedef struct PluginEntry {
    PluginRun run;
    long long budget_ms;
    long long grace_ms;
    size_t nevents;
    size_t nneeds;
    size_t n_tags_any;
    size_t n_tags_all;
    size_t n_created_by;
    size_t n_updated_by;
    size_t n_bins;
    size_t n_not_caused_by;
    size_t n_origins;
    int priority;
    bool is_filter;
    bool all_events;
    bool has_match_before;
    bool match_before;
    char pad_[4];
    char name[PLUGIN_NAME_CAP + 1];
    char events[PLUGIN_EVENTS_MAX][PLUGIN_EVENT_CAP];
    char point[PLUGIN_POINT_CAP];
    char needs[PLUGIN_NEEDS_MAX][PLUGIN_NEED_CAP];
    char tags_any[PLUGIN_TAGS_MAX][PLUGIN_TAG_CAP];
    char tags_all[PLUGIN_TAGS_MAX][PLUGIN_TAG_CAP];
    char created_by[PLUGIN_ACTORS_MAX][PLUGIN_ACTOR_CAP];
    char updated_by[PLUGIN_ACTORS_MAX][PLUGIN_ACTOR_CAP];
    char bins[PLUGIN_BINS_MAX][PLUGIN_BIN_CAP];
    char not_caused_by[PLUGIN_CAUSE_MAX][PLUGIN_CAUSE_CAP];
    char origins[PLUGIN_ORIGINS_MAX][PLUGIN_ORIGIN_CAP];
    char tail_[PLUGIN_ENTRY_TAIL];
} PluginEntry;

/* The order is the order of the schema's `type` enum. */
typedef enum PluginSettingType {
    PLUGIN_SETTING_BOOLEAN = 0,
    PLUGIN_SETTING_INTEGER,
    PLUGIN_SETTING_ENUM,
    PLUGIN_SETTING_DURATION,
    PLUGIN_SETTING_SIZE,
    PLUGIN_SETTING_STRING,
    PLUGIN_SETTING_STRING_LIST
} PluginSettingType;

/* default_text is the default as plain text, or the source JSON of a
   string_list. min_value and max_value are milliseconds for a duration and
   bytes for a size. Plan 30 turns this into a registry row. */
typedef struct PluginSettingDecl {
    long long min_value;
    long long max_value;
    size_t nvalues;
    long long max_items;
    PluginSettingType type;
    bool has_default;
    bool has_min;
    bool has_max;
    bool allow_user;
    bool allow_brain;
    char key[PLUGIN_NAME_CAP + 1];
    char description[PLUGIN_TEXT_CAP];
    char default_text[PLUGIN_DEFAULT_CAP];
    char values[PLUGIN_VALUES_MAX][PLUGIN_VALUE_CAP];
    char tail_[PLUGIN_DECL_TAIL];
} PluginSettingDecl;

typedef struct PluginManifest {
    PluginEntry entries[PLUGIN_ENTRIES_MAX]; /* actions first, then filters */
    PluginSettingDecl settings[PLUGIN_SETTINGS_MAX];
    PluginError *errors;     /* every error, in document order (R2.5a) */
    PluginUnknown *warnings; /* every unknown field on an open object */
    size_t nentries;
    size_t nsettings;
    size_t nerrors;
    size_t nwarnings;
    size_t errors_cap;
    size_t warnings_cap;
    long long idle_timeout_ms;
    long long max_batch_bytes;
    long long data_version;
    int max_batch_entries;
    bool has_idle_timeout;
    bool backfill;
    bool has_description;
    bool oom;
    char name[PLUGIN_NAME_CAP + 1];
    char plugin_version[PLUGIN_VER_CAP];
    char description[PLUGIN_TEXT_CAP];
    char tail_[PLUGIN_MANIFEST_TAIL];
} PluginManifest;

/* *out is a heap manifest the caller frees, including when the status is
   not OK. *out is NULL only on PLUGIN_PARSE_OOM. text is one JSON value. */
PluginParseStatus plugin_manifest_parse(const char *text, size_t len, PluginManifest **out);

void plugin_manifest_free(PluginManifest *manifest);

#endif /* REMEMBER_PLUGIN_MANIFEST_H */
