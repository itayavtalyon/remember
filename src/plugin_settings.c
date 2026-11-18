#include "plugin_settings.h"

#include "fileio.h"
#include "jsonread.h"
#include "plugin_manifest.h"
#include "settings.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Last: under REMEMBER_TEST_HOOKS every malloc below goes through the shared
   fault hook (decision:plan18b-shared-alloc-hook). */
#ifdef REMEMBER_TEST_HOOKS
#include "test_alloc.h" // IWYU pragma: keep
#endif

enum {
    PLUGIN_SEGMENT_MAX = 40, /* a settings key segment (ST-1.1) */
    NAMES_GROW = 16,
    PREFIX_CAP = SETTING_KEY_MAX + 1,
    DESCRIPTION_CAP = PLUGIN_NAME_CAP + 32
};

static bool segment_char(char c)
{
    if (c >= 'a' && c <= 'z') {
        return true;
    }
    if (c >= '0' && c <= '9') {
        return true;
    }
    return c == '_';
}

/* [a-z][a-z0-9_]{0,39}, the name of a plugin as a key segment. */
static bool segment_ok(const char *name)
{
    size_t i = 0;
    size_t n = strlen(name);

    if (n == 0U || n > (size_t)PLUGIN_SEGMENT_MAX || name[0] < 'a' || name[0] > 'z') {
        return false;
    }
    for (i = 1; i < n; i++) {
        if (!segment_char(name[i])) {
            return false;
        }
    }
    return true;
}

static bool key_of(char *out, size_t cap, const char *name, const char *tail)
{
    int n = snprintf(out, cap, "plugin.%s.%s", name, tail);

    if (n <= 0) {
        return false;
    }
    return (size_t)n < cap;
}

static PluginSettingsStatus add_enabled(SettingsRegistry *reg, const char *name)
{
    char key[SETTING_KEY_MAX + 1];
    char text[DESCRIPTION_CAP];
    SettingSpec row;

    memset(&row, 0, sizeof(row));
    (void)snprintf(text, sizeof(text), "Whether \"%s\" is turned on.", name);
    if (!key_of(key, sizeof(key), name, "enabled") || setting_by_key(reg, key) != NULL) {
        return PLUGIN_SETTINGS_REFUSED;
    }
    row.key = key;
    row.default_text = "false";
    row.description = text;
    row.type = SETTING_BOOLEAN;
    row.allow_user = true;
    row.allow_brain = true;
    /* The key was free just above, so a refusal is memory. */
    if (!settings_registry_add(reg, &row)) {
        return PLUGIN_SETTINGS_OOM;
    }
    return PLUGIN_SETTINGS_OK;
}

typedef struct {
    SettingSpec row;
    const char *values[PLUGIN_VALUES_MAX];
    char default_text[SETTING_VALUE_CAP];
    char key[SETTING_KEY_MAX + 1];
    char pad_[3]; /* explicit tail padding (kept -Wpadded-clean) */
} Built;

static SettingType type_of(PluginSettingType type)
{
    switch (type) {
    case PLUGIN_SETTING_BOOLEAN:
        return SETTING_BOOLEAN;
    case PLUGIN_SETTING_INTEGER:
        return SETTING_INTEGER;
    case PLUGIN_SETTING_ENUM:
        return SETTING_ENUM;
    case PLUGIN_SETTING_DURATION:
        return SETTING_DURATION;
    case PLUGIN_SETTING_SIZE:
        return SETTING_SIZE;
    case PLUGIN_SETTING_STRING:
        return SETTING_TEXT;
    case PLUGIN_SETTING_STRING_LIST:
    default:
        return SETTING_TEXT_LIST;
    }
}

/* A missing bound of a range is the type's own limit: 0 below, and the largest number above. */
static void fill_range(SettingSpec *row, const PluginSettingDecl *decl)
{
    row->has_range = false;
    row->min_value = 0;
    row->max_value = 0;
    if (!decl->has_min && !decl->has_max) {
        return;
    }
    row->has_range = true;
    if (decl->has_min) {
        row->min_value = decl->min_value;
    }
    row->max_value = LLONG_MAX;
    if (decl->has_max) {
        row->max_value = decl->max_value;
    }
}

/* The canonical JSON of the default of decl. A string_list default is the source JSON of an
   array, which is read here and checked item by item like a typed value. */
static PluginSettingsStatus default_json(const SettingSpec *row, const PluginSettingDecl *decl,
                                         char *json)
{
    const char *scalar[1] = {decl->default_text};
    const char *items[PLUGIN_DEFAULT_CAP];
    JsonValue *tree = NULL;
    size_t nitems = 1;
    const char *const *use = scalar;
    SettingStatus got = SETTING_OK;

    if (row->type == SETTING_TEXT_LIST) {
        size_t i = 0;
        JsonReadStatus read = jsonread_parse(decl->default_text, strlen(decl->default_text), &tree);

        if (read == JSONREAD_ERR_OOM) {
            return PLUGIN_SETTINGS_OOM;
        }
        if (read != JSONREAD_OK || tree->type != JSON_ARRAY ||
            tree->u.array.n > (size_t)PLUGIN_DEFAULT_CAP) {
            jsonread_free(tree);
            return PLUGIN_SETTINGS_REFUSED;
        }
        nitems = tree->u.array.n;
        for (i = 0; i < nitems; i++) {
            items[i] = tree->u.array.items[i]->type == JSON_STRING
                           ? tree->u.array.items[i]->u.string.bytes
                           : NULL;
        }
        use = items;
    }
    got = setting_canon_value(row, use, nitems, json, SETTING_VALUE_CAP, NULL, 0U);
    jsonread_free(tree);
    if (got != SETTING_OK) {
        return PLUGIN_SETTINGS_REFUSED;
    }
    return PLUGIN_SETTINGS_OK;
}

/* The default as plain text: a quoted type is stored quoted and the registry quotes it again. */
static bool plain_default(const char *json, char *out, size_t cap)
{
    if (json[0] == '"') {
        return setting_text_unquote(json, out, cap);
    }
    if (snprintf(out, cap, "%s", json) <= 0) {
        return false;
    }
    return true;
}

static PluginSettingsStatus add_setting(SettingsRegistry *reg, const char *name,
                                        const PluginSettingDecl *decl)
{
    /* One of these is about 20 KiB: the heap, not the stack. */
    Built *built = (Built *)malloc(sizeof(*built));
    PluginSettingsStatus st = PLUGIN_SETTINGS_REFUSED;
    char *json = (char *)malloc(SETTING_VALUE_CAP);
    size_t i = 0;

    if (built == NULL || json == NULL) {
        free(built);
        free(json);
        return PLUGIN_SETTINGS_OOM;
    }
    memset(built, 0, sizeof(*built));
    built->row.type = type_of(decl->type);
    built->row.description = decl->description;
    built->row.allow_user = decl->allow_user;
    built->row.allow_brain = decl->allow_brain;
    if (built->row.type == SETTING_TEXT_LIST) {
        built->row.max_items =
            decl->max_items > (long long)INT_MAX ? INT_MAX : (int)decl->max_items;
    }
    if (built->row.type == SETTING_ENUM) {
        built->row.nvalues = decl->nvalues;
        built->row.values = built->values;
        for (i = 0; i < decl->nvalues; i++) {
            built->values[i] = decl->values[i];
        }
    }
    if (built->row.type == SETTING_INTEGER || built->row.type == SETTING_DURATION ||
        built->row.type == SETTING_SIZE) {
        fill_range(&built->row, decl);
    }
    built->row.key = built->key;
    if (key_of(built->key, sizeof(built->key), name, decl->key)) {
        st = default_json(&built->row, decl, json);
    }
    for (i = 0; st == PLUGIN_SETTINGS_OK && built->row.type == SETTING_ENUM && i < decl->nvalues;
         i++) {
        if (!setting_text_ok(decl->values[i])) {
            st = PLUGIN_SETTINGS_REFUSED;
        }
    }
    if (st != PLUGIN_SETTINGS_OK) {
        goto done;
    }
    built->row.default_text = built->default_text;
    /* json is canon output, so plain_default cannot fail; st is its answer if it does. */
    st = PLUGIN_SETTINGS_REFUSED;
    if (!plain_default(json, built->default_text, sizeof(built->default_text))) {
        goto done;
    }
    /* The parser refuses two settings with one key, so a refusal is memory. */
    st = PLUGIN_SETTINGS_OOM;
    if (settings_registry_add(reg, &built->row)) {
        st = PLUGIN_SETTINGS_OK;
    }
done:
    free(json);
    free(built);
    return st;
}

/* cppcheck-suppress staticFunction ; public API (plugin_settings.h); plan 34 calls it, the tests do
 * now */
PluginSettingsStatus plugin_settings_register(const PluginManifest *m, SettingsRegistry *reg)
{
    char key[SETTING_KEY_MAX + 1];
    PluginSettingsStatus st = PLUGIN_SETTINGS_OK;
    size_t i = 0;

    if (strcmp(m->name, PLUGIN_HYBRID_NAME) == 0 || !key_of(key, sizeof(key), m->name, "enabled") ||
        setting_by_key(reg, key) != NULL) {
        return PLUGIN_SETTINGS_REFUSED;
    }
    st = add_enabled(reg, m->name);
    for (i = 0; st == PLUGIN_SETTINGS_OK && i < m->nsettings; i++) {
        st = add_setting(reg, m->name, &m->settings[i]);
    }
    if (st != PLUGIN_SETTINGS_OK) {
        char prefix[PREFIX_CAP];

        (void)snprintf(prefix, sizeof(prefix), "plugin.%s.", m->name);
        (void)settings_registry_remove_prefix(reg, prefix);
    }
    return st;
}

/* cppcheck-suppress staticFunction ; public API (plugin_settings.h); used by tests */
void plugin_settings_unregister(const char *name, SettingsRegistry *reg)
{
    char prefix[PREFIX_CAP];

    if (snprintf(prefix, sizeof(prefix), "plugin.%s.", name) > 0) {
        (void)settings_registry_remove_prefix(reg, prefix);
    }
}

PluginSettingsStatus plugin_settings_register_hybrid(SettingsRegistry *reg)
{
    return add_enabled(reg, PLUGIN_HYBRID_NAME);
}

/* ---- the folder reader ----------------------------------------------------------------- */

static int by_byte_order(const void *left, const void *right)
{
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

static void names_free(char **names, size_t n)
{
    size_t i = 0;

    if (names == NULL) {
        return;
    }
    for (i = 0; i < n; i++) {
        free(names[i]);
    }
    free((void *)names);
}

/* The folder names under dir, sorted. *names is NULL when there are none. */
static PluginSettingsStatus list_folders(const char *dir, char ***names, size_t *n, int *sys_errno)
{
    DIR *d = opendir(dir);
    const struct dirent *ent = NULL;
    size_t cap = 0;

    *names = NULL;
    *n = 0;
    if (d == NULL) {
        if (fileio_errno_means_absent(errno)) {
            return PLUGIN_SETTINGS_OK;
        }
        *sys_errno = errno;
        return PLUGIN_SETTINGS_DIR;
    }
    // NOLINTNEXTLINE(concurrency-mt-unsafe) single-threaded; readdir has no portable reentrant form
    while ((ent = readdir(d)) != NULL) {
        char *copy = NULL;

        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        if (*n == cap) {
            char **grown = (char **)malloc((cap + (size_t)NAMES_GROW) * sizeof(*grown));

            if (grown == NULL) {
                names_free(*names, *n);
                *names = NULL;
                (void)closedir(d);
                return PLUGIN_SETTINGS_OOM;
            }
            if (*n > 0U) {
                memcpy((void *)grown, (const void *)*names, *n * sizeof(*grown));
            }
            free((void *)*names);
            *names = grown;
            cap += (size_t)NAMES_GROW;
        }
        copy = (char *)malloc(strlen(ent->d_name) + 1U);
        if (copy == NULL) {
            names_free(*names, *n);
            *names = NULL;
            (void)closedir(d);
            return PLUGIN_SETTINGS_OOM;
        }
        memcpy(copy, ent->d_name, strlen(ent->d_name) + 1U);
        (*names)[(*n)++] = copy;
    }
    (void)closedir(d);
    if (*n > 1U) {
        qsort((void *)*names, *n, sizeof(**names), by_byte_order);
    }
    return PLUGIN_SETTINGS_OK;
}

/* *bytes gets <dir>/<folder>/plugin.json, or NULL when there is none to use. Running out of
   memory is OOM, never "no plugin.json": the plugin would vanish quietly. */
static PluginSettingsStatus read_manifest(const char *dir, const char *folder, char **bytes,
                                          size_t *len)
{
    int err = 0;
    char *folder_path = fileio_join(dir, folder, &err);
    char *file_path = folder_path != NULL ? fileio_join(folder_path, "plugin.json", &err) : NULL;

    *bytes = NULL;
    free(folder_path);
    if (file_path == NULL) {
        return PLUGIN_SETTINGS_OOM; /* fileio_join fails only with ENOMEM */
    }
    if (fileio_read_regular(file_path, (size_t)PLUGIN_JSON_MAX, bytes, len, &err) != 0) {
        *bytes = NULL;
    }
    free(file_path);
    return err == ENOMEM ? PLUGIN_SETTINGS_OOM : PLUGIN_SETTINGS_OK;
}

/* name for a manifest whose own name an earlier folder holds: <name>_<folder>, with a hyphen of
   the folder read as an underscore, cut to 40 characters. False when that is not a name. */
static bool second_name(char *out, size_t cap, const char *name, const char *folder)
{
    size_t i = 0;

    /* A longer join is cut here; only the first 40 characters are kept below. */
    (void)snprintf(out, cap, "%s_%s", name, folder);
    for (i = 0; out[i] != '\0'; i++) {
        if (out[i] == '-') {
            out[i] = '_';
        }
    }
    if (strlen(out) > (size_t)PLUGIN_SEGMENT_MAX) {
        out[PLUGIN_SEGMENT_MAX] = '\0';
    }
    return segment_ok(out);
}

static bool name_taken(const SettingsRegistry *reg, const char *name)
{
    char key[SETTING_KEY_MAX + 1];

    if (!key_of(key, sizeof(key), name, "enabled")) {
        return true;
    }
    return setting_by_key(reg, key) != NULL;
}

/* The manifests plugin_settings_load_manifests keeps: those whose keys are registered. */
typedef struct {
    PluginManifest **items;
    size_t count;
    size_t cap;
} KeptManifests;

/* Take ownership of man into keep. False when memory runs out; man is then still the caller's. */
static bool keep_manifest(KeptManifests *keep, PluginManifest *man)
{
    if (keep->count == keep->cap) {
        size_t cap = keep->cap + (size_t)NAMES_GROW;
        PluginManifest **grown = (PluginManifest **)malloc(cap * sizeof(void *));

        if (grown == NULL) {
            return false;
        }
        if (keep->count > 0U) {
            memcpy((void *)grown, (const void *)keep->items, keep->count * sizeof(void *));
        }
        free((void *)keep->items);
        keep->items = grown;
        keep->cap = cap;
    }
    keep->items[keep->count++] = man;
    return true;
}

/* One folder. OOM is the only failure that stops the reader. keep may be NULL. */
static PluginSettingsStatus load_folder(const char *dir, const char *folder, SettingsRegistry *reg,
                                        KeptManifests *keep)
{
    PluginManifest *man = NULL;
    size_t len = 0;
    char *bytes = NULL;
    PluginParseStatus parsed = PLUGIN_PARSE_SYNTAX;
    PluginSettingsStatus st = read_manifest(dir, folder, &bytes, &len);

    if (bytes == NULL) {
        return st;
    }
    parsed = plugin_manifest_parse(bytes, len, &man);
    free(bytes);
    if (parsed == PLUGIN_PARSE_OOM) {
        return PLUGIN_SETTINGS_OOM;
    }
    /* The built-in name is checked here as well as in register: a duplicate would be renamed. */
    if (parsed == PLUGIN_PARSE_OK && strcmp(man->name, PLUGIN_HYBRID_NAME) != 0) {
        if (name_taken(reg, man->name)) {
            char renamed[PLUGIN_NAME_CAP + PLUGIN_NAME_CAP + 2];

            if (second_name(renamed, sizeof(renamed), man->name, folder) &&
                !name_taken(reg, renamed)) {
                (void)snprintf(man->name, sizeof(man->name), "%s", renamed);
            } else {
                plugin_manifest_free(man);
                return PLUGIN_SETTINGS_OK;
            }
        }
        st = plugin_settings_register(man, reg);
        if (st == PLUGIN_SETTINGS_REFUSED) {
            st = PLUGIN_SETTINGS_OK;
        } else if (st == PLUGIN_SETTINGS_OK && keep != NULL) {
            if (!keep_manifest(keep, man)) {
                plugin_settings_unregister(man->name, reg);
                st = PLUGIN_SETTINGS_OOM;
            } else {
                return st; /* kept: the caller frees it */
            }
        }
    }
    plugin_manifest_free(man);
    return st;
}

PluginSettingsStatus plugin_settings_load_manifests(const char *plugins_dir, SettingsRegistry *reg,
                                                    PluginManifestList *out, int *sys_errno)
{
    char **names = NULL;
    size_t n = 0;
    size_t i = 0;
    KeptManifests keep = {.items = NULL, .count = 0U, .cap = 0U};
    PluginSettingsStatus st = list_folders(plugins_dir, &names, &n, sys_errno);

    for (i = 0; st == PLUGIN_SETTINGS_OK && i < n; i++) {
        st = load_folder(plugins_dir, names[i], reg, out != NULL ? &keep : NULL);
    }
    names_free(names, n);
    if (out != NULL) {
        out->items = keep.items;
        out->count = keep.count;
    }
    if (st != PLUGIN_SETTINGS_OK && out != NULL) {
        plugin_manifest_list_free(out);
    }
    return st;
}

PluginSettingsStatus plugin_settings_load(const char *plugins_dir, SettingsRegistry *reg,
                                          int *sys_errno)
{
    return plugin_settings_load_manifests(plugins_dir, reg, NULL, sys_errno);
}

void plugin_manifest_list_free(PluginManifestList *list)
{
    size_t i = 0;

    for (i = 0; i < list->count; i++) {
        plugin_manifest_free(list->items[i]);
    }
    free((void *)list->items);
    list->items = NULL;
    list->count = 0;
}
