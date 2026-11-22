#include "harness.h"
#include "plugin_manifest.h"
#include "plugin_settings.h"
#include "register.h"
#include "settings.h"
#include "test.h"
#include "test_alloc.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The numbers are the limits and ranges under test: 511 and 65536 bytes, the manifest ranges. */
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

/* Plan 30 (specs/SETTINGS.md "Plugin keys"). Decisions:
   decision:2026-11-11-plugin-enabled-bool, decision:2026-11-16-plan30-registry-explicit,
   decision:2026-11-16-plan30-duplicate-plugin-name,
   decision:2026-11-16-plan30-plugin-json-reader-cap,
   decision:2026-11-16-plan30-enabled-description, decision:2026-11-16-plan30-plugin-string-values.
 */

#define ACTIONS                                                                                    \
    "\"actions\":[{\"name\":\"go\",\"events\":[\"memory.created\"],\"run\":{\"command\":[\"/bin/"  \
    "echo\"]}}]"

/* A manifest named name. settings is the text of the "settings" array, or NULL for none. */
static void given_doc(char *doc, size_t cap, const char *name, const char *settings)
{
    int n = snprintf(doc, cap,
                     "{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"%s\","
                     "\"plugin_version\":\"1.0.0\",\"protocol\":1," ACTIONS "%s%s%s}",
                     name, settings != NULL ? ",\"settings\":[" : "",
                     settings != NULL ? settings : "", settings != NULL ? "]" : "");

    ASSERT_TRUE(n > 0 && (size_t)n < cap);
}

/* <fresh folder>/plugins, heap. */
static char *given_plugins_dir(void)
{
    char *db = make_temp_db_path();
    char *root = db != NULL ? dir_of_path(db) : NULL;
    char *dir = NULL;

    ASSERT_TRUE(root != NULL);
    if (root == NULL) {
        free(db);
        return NULL;
    }
    dir = (char *)malloc(strlen(root) + sizeof("/plugins"));
    ASSERT_TRUE(dir != NULL);
    if (dir != NULL) {
        (void)snprintf(dir, strlen(root) + sizeof("/plugins"), "%s/plugins", root);
    }
    free(db);
    free(root);
    return dir;
}

static void path_in(char *out, size_t cap, const char *base, const char *folder, const char *file)
{
    int n = snprintf(out, cap, "%s/%s/%s", base, folder, file);

    ASSERT_TRUE(n > 0 && (size_t)n < cap);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) plugins dir, folder, then the file text
static void given_plugin(const char *plugins, const char *folder, const char *doc)
{
    char path[1024];

    path_in(path, sizeof(path), plugins, folder, "plugin.json");
    harness_write_file(path, doc);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) plugins dir, folder, name, settings text
static void given_named_plugin(const char *plugins, const char *folder, const char *name,
                               const char *settings)
{
    char doc[4096];

    given_doc(doc, sizeof(doc), name, settings);
    given_plugin(plugins, folder, doc);
}

static SettingsRegistry *given_registry(void)
{
    SettingsRegistry *reg = settings_registry_new();

    ASSERT_TRUE(reg != NULL);
    return reg;
}

static SettingsRegistry *given_loaded(const char *plugins)
{
    SettingsRegistry *reg = given_registry();
    int err = 0;

    ASSERT_EQ_STATUS(plugin_settings_load(plugins, reg, &err), PLUGIN_SETTINGS_OK);
    return reg;
}

/* One plugin p with one setting, loaded through the real reader. */
static SettingsRegistry *given_one_setting(const char *setting)
{
    char *plugins = given_plugins_dir();
    SettingsRegistry *reg = NULL;

    given_named_plugin(plugins, "p", "p", setting);
    reg = given_loaded(plugins);
    free(plugins);
    return reg;
}

static const SettingSpec *spec_of(const SettingsRegistry *reg, const char *key)
{
    const SettingSpec *spec = setting_by_key(reg, key);

    ASSERT_TRUE(spec != NULL);
    return spec;
}

static size_t plugin_keys(const SettingsRegistry *reg)
{
    size_t i = 0;
    size_t n = 0;

    for (i = 0; i < settings_registry_count(reg); i++) {
        if (strncmp(settings_registry_at(reg, i)->key, "plugin.", 7U) == 0) {
            n++;
        }
    }
    return n;
}

static void assert_default(const SettingSpec *spec, const char *want)
{
    char json[SETTING_VALUE_CAP];

    ASSERT_TRUE(spec != NULL);
    if (spec == NULL) {
        return;
    }
    ASSERT_TRUE(setting_default_json(spec, json, sizeof(json)));
    ASSERT_STREQ(json, want);
}

static SettingStatus prepare_list(const SettingsRegistry *reg, const char *key,
                                  const char *const *items, size_t nitems, char *json)
{
    SettingLayer layer = SETTING_LAYER_AUTO;

    return setting_prepare(reg, key, items, nitems, SETTING_LAYER_AUTO, json, SETTING_VALUE_CAP,
                           NULL, &layer, NULL, 0U);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) key, then the typed value
static SettingStatus prepare_one(const SettingsRegistry *reg, const char *key, const char *value,
                                 char *json)
{
    const char *const items[] = {value};

    return prepare_list(reg, key, items, 1U, json);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) key, typed value, then the stored JSON
static void assert_set(const SettingsRegistry *reg, const char *key, const char *value,
                       const char *want_json)
{
    char json[SETTING_VALUE_CAP];

    ASSERT_EQ_STATUS(prepare_one(reg, key, value, json), SETTING_OK);
    ASSERT_STREQ(json, want_json);
}

static void assert_refused(const SettingsRegistry *reg, const char *key, const char *value,
                           SettingStatus want)
{
    char json[SETTING_VALUE_CAP];

    ASSERT_EQ_STATUS(prepare_one(reg, key, value, json), want);
    ASSERT_STREQ(json, "");
}

static const char k_bool[] = "{\"key\":\"loud\",\"description\":\"Be loud.\",\"type\":\"boolean\","
                             "\"default\":true}";
static const char k_enum[] = "{\"key\":\"mode\",\"description\":\"A mode.\",\"type\":\"enum\","
                             "\"default\":\"fast\",\"range\":{\"values\":[\"fast\",\"slow\"]}}";
static const char k_size[] = "{\"key\":\"cap\",\"description\":\"A cap.\",\"type\":\"size\","
                             "\"default\":\"1MiB\",\"range\":{\"min\":\"1KiB\",\"max\":\"4MiB\"}}";
static const char k_list[] =
    "{\"key\":\"words\",\"description\":\"Words.\",\"type\":\"string_list\","
    "\"default\":[\"a\",\"b\"],\"range\":{\"max_items\":3}}";

/* ---- the boolean type ------------------------------------------------------------------ */

TEST(bool_type_accepts_true_false_only)
{
    SettingsRegistry *reg = given_one_setting(NULL);

    assert_set(reg, "plugin.p.enabled", "true", "true");
    assert_set(reg, "plugin.p.enabled", "false", "false");
    assert_refused(reg, "plugin.p.enabled", "yes", SETTING_ERR_INVALID);
    assert_refused(reg, "plugin.p.enabled", "True", SETTING_ERR_INVALID);
    assert_refused(reg, "plugin.p.enabled", "TRUE", SETTING_ERR_INVALID);
    assert_refused(reg, "plugin.p.enabled", "1", SETTING_ERR_INVALID);
    assert_refused(reg, "plugin.p.enabled", "0", SETTING_ERR_INVALID);
    assert_refused(reg, "plugin.p.enabled", "", SETTING_ERR_INVALID);
    assert_refused(reg, "plugin.p.enabled", " true", SETTING_ERR_INVALID);
    assert_refused(reg, "plugin.p.enabled", "true ", SETTING_ERR_INVALID);
    assert_refused(reg, "plugin.p.enabled", "\"true\"", SETTING_ERR_INVALID);
    assert_refused(reg, "plugin.p.enabled", "null", SETTING_ERR_INVALID);
    settings_registry_free(reg);
}

TEST(a_boolean_takes_one_value_only)
{
    SettingsRegistry *reg = given_one_setting(NULL);
    const char *const two[] = {"true", "false"};
    char json[SETTING_VALUE_CAP];

    ASSERT_EQ_STATUS(prepare_list(reg, "plugin.p.enabled", two, 2U, json), SETTING_ERR_INVALID);
    ASSERT_EQ_STATUS(prepare_list(reg, "plugin.p.enabled", NULL, 0U, json), SETTING_ERR_INVALID);
    settings_registry_free(reg);
}

/* ---- the enabled key ------------------------------------------------------------------- */

TEST(installed_plugin_has_enabled_key_default_false)
{
    SettingsRegistry *reg = given_one_setting(NULL);
    const SettingSpec *spec = spec_of(reg, "plugin.p.enabled");

    ASSERT_EQ_INT(spec->type, SETTING_BOOLEAN);
    assert_default(spec, "false");
    settings_registry_free(reg);
}

TEST(enabled_scopes_user_and_brain)
{
    SettingsRegistry *reg = given_one_setting(NULL);
    const SettingSpec *spec = spec_of(reg, "plugin.p.enabled");
    SettingLayer layer = SETTING_LAYER_AUTO;
    char json[SETTING_VALUE_CAP];
    const char *const one[] = {"true"};

    ASSERT_TRUE(spec->allow_user);
    ASSERT_TRUE(spec->allow_brain);
    ASSERT_EQ_STATUS(setting_prepare(reg, "plugin.p.enabled", one, 1U, SETTING_LAYER_AUTO, json,
                                     sizeof(json), NULL, &layer, NULL, 0U),
                     SETTING_OK);
    ASSERT_EQ_INT(layer, SETTING_LAYER_USER);
    ASSERT_EQ_STATUS(setting_prepare(reg, "plugin.p.enabled", one, 1U, SETTING_LAYER_BRAIN, json,
                                     sizeof(json), NULL, &layer, NULL, 0U),
                     SETTING_OK);
    ASSERT_EQ_INT(layer, SETTING_LAYER_BRAIN);
    settings_registry_free(reg);
}

TEST(the_enabled_description_names_the_plugin)
{
    SettingsRegistry *reg = given_one_setting(NULL);

    ASSERT_STREQ(spec_of(reg, "plugin.p.enabled")->description,
                 "Turns the \"p\" plugin on or off.");
    settings_registry_free(reg);
}

TEST(a_hyphen_typed_in_the_plugin_segment_finds_the_underscore_key)
{
    char *plugins = given_plugins_dir();
    SettingsRegistry *reg = NULL;

    given_named_plugin(plugins, "url_expand", "url_expand", NULL);
    reg = given_loaded(plugins);
    ASSERT_TRUE(setting_by_key(reg, "plugin.url-expand.enabled") ==
                setting_by_key(reg, "plugin.url_expand.enabled"));
    ASSERT_TRUE(setting_by_key(reg, "plugin.url-expand.enabled") != NULL);
    settings_registry_free(reg);
    free(plugins);
}

TEST(a_plugin_that_is_not_installed_has_no_key)
{
    SettingsRegistry *reg = given_one_setting(NULL);

    ASSERT_TRUE(setting_by_key(reg, "plugin.gone.enabled") == NULL);
    ASSERT_TRUE(setting_by_key(settings_registry_builtin(), "plugin.p.enabled") == NULL);
    settings_registry_free(reg);
}

/* ---- the keys of a manifest's settings ------------------------------------------------- */

TEST(manifest_setting_generates_key_with_its_type)
{
    char all[2048];
    SettingsRegistry *reg = NULL;

    (void)snprintf(
        all, sizeof(all), "%s,%s,%s,%s,%s,%s,%s", k_bool,
        "{\"key\":\"limit\",\"description\":\"A limit.\",\"type\":\"integer\",\"default\":5}",
        k_enum,
        "{\"key\":\"wait\",\"description\":\"A wait.\",\"type\":\"duration\",\"default\":\"1s\"}",
        k_size,
        "{\"key\":\"label\",\"description\":\"A label.\",\"type\":\"string\",\"default\":\"\"}",
        k_list);
    reg = given_one_setting(all);
    ASSERT_EQ_INT(spec_of(reg, "plugin.p.loud")->type, SETTING_BOOLEAN);
    ASSERT_EQ_INT(spec_of(reg, "plugin.p.limit")->type, SETTING_INTEGER);
    ASSERT_EQ_INT(spec_of(reg, "plugin.p.mode")->type, SETTING_ENUM);
    ASSERT_EQ_INT(spec_of(reg, "plugin.p.wait")->type, SETTING_DURATION);
    ASSERT_EQ_INT(spec_of(reg, "plugin.p.cap")->type, SETTING_SIZE);
    ASSERT_EQ_INT(spec_of(reg, "plugin.p.label")->type, SETTING_TEXT);
    ASSERT_EQ_INT(spec_of(reg, "plugin.p.words")->type, SETTING_TEXT_LIST);
    ASSERT_EQ_INT((long long)plugin_keys(reg), 8);
    settings_registry_free(reg);
}

TEST(a_manifest_setting_carries_its_description)
{
    SettingsRegistry *reg = given_one_setting(k_bool);

    ASSERT_STREQ(spec_of(reg, "plugin.p.loud")->description, "Be loud.");
    settings_registry_free(reg);
}

TEST(a_manifest_setting_scope_is_user_by_default_and_brain_or_both_when_it_says_so)
{
    char three[1024];
    SettingsRegistry *reg = NULL;

    (void)snprintf(three, sizeof(three),
                   "{\"key\":\"a\",\"description\":\"A.\",\"type\":\"boolean\",\"default\":false},"
                   "{\"key\":\"b\",\"description\":\"B.\",\"type\":\"boolean\",\"default\":false,"
                   "\"scope\":\"brain\"},"
                   "{\"key\":\"c\",\"description\":\"C.\",\"type\":\"boolean\",\"default\":false,"
                   "\"scope\":\"both\"}");
    reg = given_one_setting(three);
    ASSERT_TRUE(spec_of(reg, "plugin.p.a")->allow_user);
    ASSERT_FALSE(spec_of(reg, "plugin.p.a")->allow_brain);
    ASSERT_FALSE(spec_of(reg, "plugin.p.b")->allow_user);
    ASSERT_TRUE(spec_of(reg, "plugin.p.b")->allow_brain);
    ASSERT_TRUE(spec_of(reg, "plugin.p.c")->allow_user);
    ASSERT_TRUE(spec_of(reg, "plugin.p.c")->allow_brain);
    settings_registry_free(reg);
}

TEST(a_boolean_setting_default_and_values)
{
    SettingsRegistry *reg = given_one_setting(k_bool);

    assert_default(spec_of(reg, "plugin.p.loud"), "true");
    assert_set(reg, "plugin.p.loud", "false", "false");
    assert_refused(reg, "plugin.p.loud", "on", SETTING_ERR_INVALID);
    settings_registry_free(reg);
}

TEST(an_integer_setting_has_its_range)
{
    SettingsRegistry *reg = given_one_setting(
        "{\"key\":\"limit\",\"description\":\"L.\",\"type\":\"integer\",\"default\":5,"
        "\"range\":{\"min\":1,\"max\":10}}");
    const SettingSpec *spec = spec_of(reg, "plugin.p.limit");

    assert_default(spec, "5");
    ASSERT_EQ_INT(spec->min_value, 1);
    ASSERT_EQ_INT(spec->max_value, 10);
    assert_set(reg, "plugin.p.limit", "10", "10");
    assert_refused(reg, "plugin.p.limit", "11", SETTING_ERR_RANGE);
    assert_refused(reg, "plugin.p.limit", "0", SETTING_ERR_RANGE);
    assert_refused(reg, "plugin.p.limit", "-1", SETTING_ERR_INVALID);
    settings_registry_free(reg);
}

TEST(an_integer_range_of_exactly_zero_allows_only_zero)
{
    SettingsRegistry *reg = given_one_setting(
        "{\"key\":\"zero\",\"description\":\"Z.\",\"type\":\"integer\",\"default\":0,"
        "\"range\":{\"min\":0,\"max\":0}}");

    assert_set(reg, "plugin.p.zero", "0", "0");
    assert_refused(reg, "plugin.p.zero", "1", SETTING_ERR_RANGE);
    settings_registry_free(reg);
}

TEST(a_range_with_only_a_max_has_a_minimum_of_zero)
{
    SettingsRegistry *reg = given_one_setting(
        "{\"key\":\"limit\",\"description\":\"L.\",\"type\":\"integer\",\"default\":5,"
        "\"range\":{\"max\":10}}");

    assert_set(reg, "plugin.p.limit", "0", "0");
    assert_refused(reg, "plugin.p.limit", "11", SETTING_ERR_RANGE);
    settings_registry_free(reg);
}

TEST(a_range_with_only_a_max_of_zero_allows_only_zero)
{
    SettingsRegistry *reg = given_one_setting(
        "{\"key\":\"zero\",\"description\":\"Z.\",\"type\":\"integer\",\"default\":0,"
        "\"range\":{\"max\":0}}");

    assert_set(reg, "plugin.p.zero", "0", "0");
    assert_refused(reg, "plugin.p.zero", "1", SETTING_ERR_RANGE);
    settings_registry_free(reg);
}

TEST(a_range_with_only_a_min_has_no_upper_bound)
{
    SettingsRegistry *reg = given_one_setting(
        "{\"key\":\"limit\",\"description\":\"L.\",\"type\":\"integer\",\"default\":5,"
        "\"range\":{\"min\":3}}");

    assert_set(reg, "plugin.p.limit", "9223372036854775807", "9223372036854775807");
    assert_refused(reg, "plugin.p.limit", "2", SETTING_ERR_RANGE);
    settings_registry_free(reg);
}

TEST(a_duration_setting_is_stored_in_canonical_form_inside_its_range)
{
    SettingsRegistry *reg = given_one_setting(
        "{\"key\":\"wait\",\"description\":\"W.\",\"type\":\"duration\",\"default\":\"1000ms\","
        "\"range\":{\"min\":\"100ms\",\"max\":\"5s\"}}");
    const SettingSpec *spec = spec_of(reg, "plugin.p.wait");

    assert_default(spec, "\"1s\"");
    ASSERT_EQ_INT(spec->min_value, 100);
    ASSERT_EQ_INT(spec->max_value, 5000);
    assert_set(reg, "plugin.p.wait", "2000ms", "\"2s\"");
    assert_refused(reg, "plugin.p.wait", "6s", SETTING_ERR_RANGE);
    assert_refused(reg, "plugin.p.wait", "50ms", SETTING_ERR_RANGE);
    assert_refused(reg, "plugin.p.wait", "soon", SETTING_ERR_INVALID);
    settings_registry_free(reg);
}

TEST(a_size_setting_is_stored_in_canonical_form_inside_its_range)
{
    SettingsRegistry *reg = given_one_setting(k_size);
    const SettingSpec *spec = spec_of(reg, "plugin.p.cap");

    assert_default(spec, "\"1MiB\"");
    ASSERT_EQ_INT(spec->min_value, 1024);
    ASSERT_EQ_INT(spec->max_value, 4194304);
    assert_set(reg, "plugin.p.cap", "2048KiB", "\"2MiB\"");
    assert_set(reg, "plugin.p.cap", "1024B", "\"1KiB\"");
    assert_refused(reg, "plugin.p.cap", "5MiB", SETTING_ERR_RANGE);
    assert_refused(reg, "plugin.p.cap", "512B", SETTING_ERR_RANGE);
    assert_refused(reg, "plugin.p.cap", "4MB", SETTING_ERR_INVALID);
    assert_refused(reg, "plugin.p.cap", "1.5B", SETTING_ERR_INVALID);
    settings_registry_free(reg);
}

TEST(an_enum_setting_takes_one_of_its_values_exactly)
{
    SettingsRegistry *reg = given_one_setting(k_enum);
    const SettingSpec *spec = spec_of(reg, "plugin.p.mode");

    assert_default(spec, "\"fast\"");
    ASSERT_EQ_INT((long long)spec->nvalues, 2);
    ASSERT_STREQ(spec->values[0], "fast");
    ASSERT_STREQ(spec->values[1], "slow");
    assert_set(reg, "plugin.p.mode", "slow", "\"slow\"");
    assert_refused(reg, "plugin.p.mode", "Slow", SETTING_ERR_INVALID);
    assert_refused(reg, "plugin.p.mode", "slo", SETTING_ERR_INVALID);
    assert_refused(reg, "plugin.p.mode", "", SETTING_ERR_INVALID);
    settings_registry_free(reg);
}

TEST(a_string_setting_takes_text_with_quotes_and_backslashes_escaped_in_the_stored_json)
{
    SettingsRegistry *reg = given_one_setting(
        "{\"key\":\"label\",\"description\":\"L.\",\"type\":\"string\",\"default\":\"hi\"}");

    assert_default(spec_of(reg, "plugin.p.label"), "\"hi\"");
    assert_set(reg, "plugin.p.label", "", "\"\"");
    assert_set(reg, "plugin.p.label", "say \"x\" in a\\b", "\"say \\\"x\\\" in a\\\\b\"");
    assert_set(reg, "plugin.p.label", "caf\xc3\xa9 \xe2\x82\xac", "\"caf\xc3\xa9 \xe2\x82\xac\"");
    settings_registry_free(reg);
}

TEST(a_string_setting_refuses_a_control_byte_del_bad_utf8_and_more_than_511_bytes)
{
    SettingsRegistry *reg = given_one_setting(
        "{\"key\":\"label\",\"description\":\"L.\",\"type\":\"string\",\"default\":\"hi\"}");
    char exact[SETTING_TEXT_MAX + 1];
    char over[SETTING_TEXT_MAX + 2];

    memset(exact, 'a', SETTING_TEXT_MAX);
    exact[SETTING_TEXT_MAX] = '\0';
    memset(over, 'a', SETTING_TEXT_MAX + 1U);
    over[SETTING_TEXT_MAX + 1U] = '\0';
    assert_refused(reg, "plugin.p.label", "a\nb", SETTING_ERR_INVALID);
    assert_refused(reg, "plugin.p.label", "a\tb", SETTING_ERR_INVALID);
    assert_refused(reg, "plugin.p.label", "a\x1b[31m", SETTING_ERR_INVALID);
    assert_refused(reg, "plugin.p.label", "a\x7f", SETTING_ERR_INVALID);
    assert_refused(reg, "plugin.p.label", "a\x80", SETTING_ERR_INVALID);
    assert_refused(reg, "plugin.p.label", "\xc3", SETTING_ERR_INVALID);
    assert_refused(reg, "plugin.p.label", "\xed\xa0\x80", SETTING_ERR_INVALID);
    assert_refused(reg, "plugin.p.label", over, SETTING_ERR_INVALID);
    ASSERT_EQ_STATUS(prepare_one(reg, "plugin.p.label", exact, (char[SETTING_VALUE_CAP]){0}),
                     SETTING_OK);
    settings_registry_free(reg);
}

TEST(a_string_list_setting_stores_a_compact_array_and_holds_at_most_max_items)
{
    SettingsRegistry *reg = given_one_setting(k_list);
    const SettingSpec *spec = spec_of(reg, "plugin.p.words");
    const char *const three[] = {"x", "y\"z", "w"};
    const char *const four[] = {"1", "2", "3", "4"};
    char json[SETTING_VALUE_CAP];

    assert_default(spec, "[\"a\",\"b\"]");
    ASSERT_EQ_INT(spec->max_items, 3);
    ASSERT_EQ_STATUS(prepare_list(reg, "plugin.p.words", three, 3U, json), SETTING_OK);
    ASSERT_STREQ(json, "[\"x\",\"y\\\"z\",\"w\"]");
    ASSERT_EQ_STATUS(prepare_list(reg, "plugin.p.words", four, 4U, json), SETTING_ERR_RANGE);
    ASSERT_STREQ(json, "");
    ASSERT_EQ_STATUS(prepare_list(reg, "plugin.p.words", NULL, 0U, json), SETTING_OK);
    ASSERT_STREQ(json, "[]");
    settings_registry_free(reg);
}

TEST(a_string_list_item_that_is_not_text_refuses_the_whole_list)
{
    SettingsRegistry *reg = given_one_setting(k_list);
    const char *const bad[] = {"ok", "no\nway"};
    char json[SETTING_VALUE_CAP];

    ASSERT_EQ_STATUS(prepare_list(reg, "plugin.p.words", bad, 2U, json), SETTING_ERR_INVALID);
    ASSERT_STREQ(json, "");
    settings_registry_free(reg);
}

/* G1 A (decision:2026-11-17-plan30-review-grill): a stored row is live only when a set of its
   own value would store it, so a row an older manifest wrote can be told apart. */
TEST(a_stored_value_is_ok_only_in_the_form_a_set_would_store)
{
    SettingsRegistry *list = given_one_setting(k_list);
    SettingsRegistry *flag = given_one_setting(k_bool);
    const SettingSpec *words = spec_of(list, "plugin.p.words");
    const SettingSpec *loud = spec_of(flag, "plugin.p.loud");

    ASSERT_TRUE(setting_stored_ok(words, "[\"a\",\"b\",\"c\"]"));
    ASSERT_FALSE(setting_stored_ok(words, "[\"a\",\"b\",\"c\",\"d\"]"));
    ASSERT_FALSE(setting_stored_ok(words, "\"a\""));
    ASSERT_FALSE(setting_stored_ok(words, "[\"a\"]x"));
    ASSERT_TRUE(setting_stored_ok(loud, "true"));
    ASSERT_FALSE(setting_stored_ok(loud, "\"true\""));
    ASSERT_FALSE(setting_stored_ok(loud, "1"));
    settings_registry_free(list);
    settings_registry_free(flag);
}

TEST(a_string_list_with_no_max_items_is_held_by_the_stored_value_cap)
{
    SettingsRegistry *reg = given_one_setting(
        "{\"key\":\"words\",\"description\":\"W.\",\"type\":\"string_list\",\"default\":[]}");
    const char *items[100];
    char big[200];
    char json[SETTING_VALUE_CAP];
    size_t i = 0;

    memset(big, 'w', sizeof(big) - 1U);
    big[sizeof(big) - 1U] = '\0';
    for (i = 0; i < 100U; i++) {
        items[i] = big;
    }
    assert_default(spec_of(reg, "plugin.p.words"), "[]");
    ASSERT_EQ_STATUS(prepare_list(reg, "plugin.p.words", items, 100U, json), SETTING_ERR_QUOTA);
    ASSERT_STREQ(json, "");
    settings_registry_free(reg);
}

/* The stored JSON of a list is at most SETTING_VALUE_MAX (16384) bytes, brackets and commas too. */
TEST(a_string_list_of_exactly_16384_stored_bytes_fits_and_one_more_does_not)
{
    SettingsRegistry *reg = given_one_setting(
        "{\"key\":\"words\",\"description\":\"W.\",\"type\":\"string_list\",\"default\":[]}");
    const char *items[81];
    char big[201];
    char last[160];
    char json[SETTING_VALUE_CAP];
    size_t i = 0;

    memset(big, 'w', 200U);
    big[200] = '\0';
    for (i = 0; i < 80U; i++) {
        items[i] = big;
    }
    /* 80 items of 200 bytes: 80 * 202 quoted + 79 commas + 2 brackets = 16241. One more item adds
       a comma and its quoted length: 140 bytes of text makes 16384, 141 makes 16385. */
    memset(last, 'x', 140U);
    last[140] = '\0';
    items[80] = last;
    ASSERT_EQ_STATUS(prepare_list(reg, "plugin.p.words", items, 81U, json), SETTING_OK);
    ASSERT_EQ_INT((long long)strlen(json), 16384);
    last[140] = 'x';
    last[141] = '\0';
    ASSERT_EQ_STATUS(prepare_list(reg, "plugin.p.words", items, 81U, json), SETTING_ERR_QUOTA);
    ASSERT_STREQ(json, "");
    settings_registry_free(reg);
}

/* ---- text helpers ---------------------------------------------------------------------- */

TEST(text_unquote_reads_only_what_the_writer_writes)
{
    char out[64];

    ASSERT_TRUE(setting_text_unquote("\"a\\\"b\\\\c\"", out, sizeof(out)));
    ASSERT_STREQ(out, "a\"b\\c");
    ASSERT_TRUE(setting_text_unquote("\"\"", out, sizeof(out)));
    ASSERT_STREQ(out, "");
    ASSERT_FALSE(setting_text_unquote("\"a\\nb\"", out, sizeof(out)));
    ASSERT_STREQ(out, "");
    ASSERT_FALSE(setting_text_unquote("\"a\\u0041\"", out, sizeof(out)));
    ASSERT_FALSE(setting_text_unquote("\"a\nb\"", out, sizeof(out)));
    ASSERT_FALSE(setting_text_unquote("\"a\"b\"", out, sizeof(out)));
    ASSERT_FALSE(setting_text_unquote("\"a\\\"", out, sizeof(out)));
    ASSERT_FALSE(setting_text_unquote("a", out, sizeof(out)));
    ASSERT_FALSE(setting_text_unquote("\"abcdef\"", out, 4U));
    ASSERT_FALSE(setting_text_unquote(NULL, out, sizeof(out)));
}

TEST(a_default_that_does_not_fit_the_buffer_has_no_default_json)
{
    SettingsRegistry *reg = given_one_setting("{\"key\":\"label\",\"description\":\"L.\",\"type\":"
                                              "\"string\",\"default\":\"say \\\"x\\\"\"}");
    const SettingSpec *spec = spec_of(reg, "plugin.p.label");
    char small[4] = "zzz";
    char exact[16];

    ASSERT_FALSE(setting_default_json(spec, small, sizeof(small)));
    ASSERT_STREQ(small, "");
    ASSERT_TRUE(setting_default_json(spec, exact, sizeof(exact)));
    ASSERT_STREQ(exact, "\"say \\\"x\\\"\"");
    ASSERT_FALSE(setting_default_json(spec, exact, 9U));
    settings_registry_free(reg);
}

TEST(an_enum_value_too_long_to_store_is_a_quota_error_and_a_list_with_no_items_pointer_is_invalid)
{
    SettingsRegistry *reg = given_registry();
    char *long_value = (char *)malloc(20000U);
    const char *values[1];
    SettingSpec enum_row = {
        .key = "plugin.q.mode", .type = SETTING_ENUM, .nvalues = 1U, .allow_user = true};
    SettingSpec list_row = {.key = "plugin.q.list", .type = SETTING_TEXT_LIST, .allow_user = true};
    char json[SETTING_VALUE_CAP];

    ASSERT_TRUE(long_value != NULL);
    if (long_value == NULL) {
        settings_registry_free(reg);
        return;
    }
    memset(long_value, 'v', 19999U);
    long_value[19999] = '\0';
    values[0] = long_value;
    enum_row.values = values;
    ASSERT_TRUE(settings_registry_add(reg, &enum_row));
    ASSERT_TRUE(settings_registry_add(reg, &list_row));
    ASSERT_EQ_STATUS(prepare_one(reg, "plugin.q.mode", long_value, json), SETTING_ERR_QUOTA);
    ASSERT_EQ_STATUS(
        setting_canon_value(spec_of(reg, "plugin.q.list"), NULL, 2U, json, sizeof(json), NULL, 0U),
        SETTING_ERR_INVALID);
    free(long_value);
    settings_registry_free(reg);
}

TEST(text_list_join_gives_the_items_with_one_space_between)
{
    char out[64];

    ASSERT_TRUE(setting_text_list_join("[\"a\",\"b\\\"c\"]", out, sizeof(out)));
    ASSERT_STREQ(out, "a b\"c");
    ASSERT_TRUE(setting_text_list_join("[]", out, sizeof(out)));
    ASSERT_STREQ(out, "");
    ASSERT_TRUE(setting_text_list_join("[\"\"]", out, sizeof(out)));
    ASSERT_STREQ(out, "");
    ASSERT_FALSE(setting_text_list_join("[\"a\",]", out, sizeof(out)));
    ASSERT_STREQ(out, "");
    ASSERT_FALSE(setting_text_list_join("[\"a\" \"b\"]", out, sizeof(out)));
    ASSERT_FALSE(setting_text_list_join("[1]", out, sizeof(out)));
    ASSERT_FALSE(setting_text_list_join("[\"a\"]x", out, sizeof(out)));
    ASSERT_FALSE(setting_text_list_join("\"a\"", out, sizeof(out)));
    ASSERT_FALSE(setting_text_list_join("[\"abcdef\"]", out, 4U));
}

/* ---- what registers nothing ------------------------------------------------------------ */

TEST(invalid_manifest_registers_nothing)
{
    char *plugins = given_plugins_dir();
    SettingsRegistry *reg = NULL;
    char doc[4096];
    char *protocol = NULL;

    given_named_plugin(plugins, "hyphen", "bad-name", NULL);
    given_named_plugin(plugins, "enabledkey", "ek",
                       "{\"key\":\"enabled\",\"description\":\"E.\",\"type\":\"boolean\","
                       "\"default\":true}");
    given_plugin(plugins, "notjson", "this is not json");
    given_plugin(plugins, "empty", "");
    given_doc(doc, sizeof(doc), "proto", NULL);
    protocol = strstr(doc, "\"protocol\":1");
    ASSERT_TRUE(protocol != NULL);
    if (protocol != NULL) {
        protocol[11] = '2';
    }
    given_plugin(plugins, "proto", doc);
    given_named_plugin(plugins, "zgood", "good", NULL);
    reg = given_loaded(plugins);
    ASSERT_EQ_INT((long long)plugin_keys(reg), 1);
    ASSERT_TRUE(setting_by_key(reg, "plugin.good.enabled") != NULL);
    ASSERT_TRUE(setting_by_key(reg, "plugin.bad_name.enabled") == NULL);
    ASSERT_TRUE(setting_by_key(reg, "plugin.ek.enabled") == NULL);
    ASSERT_TRUE(setting_by_key(reg, "plugin.proto.enabled") == NULL);
    settings_registry_free(reg);
    free(plugins);
}

TEST(a_manifest_with_one_bad_setting_registers_none_of_its_keys)
{
    SettingsRegistry *reg = given_one_setting(
        "{\"key\":\"ok\",\"description\":\"Fine.\",\"type\":\"boolean\",\"default\":true},"
        "{\"key\":\"mode\",\"description\":\"M.\",\"type\":\"enum\",\"default\":\"a\","
        "\"range\":{\"values\":[\"a\",\"b\"]},\"scope\":\"sideways\"}");

    ASSERT_EQ_INT((long long)plugin_keys(reg), 0);
    settings_registry_free(reg);
}

TEST(a_default_the_registry_refuses_registers_no_key_of_the_plugin)
{
    SettingsRegistry *reg = given_one_setting(
        "{\"key\":\"ok\",\"description\":\"Fine.\",\"type\":\"boolean\",\"default\":true},"
        "{\"key\":\"label\",\"description\":\"L.\",\"type\":\"string\","
        "\"default\":\"a\\u0001b\"}");

    ASSERT_EQ_INT((long long)plugin_keys(reg), 0);
    settings_registry_free(reg);
}

TEST(an_enum_value_the_registry_refuses_registers_no_key_of_the_plugin)
{
    SettingsRegistry *reg = given_one_setting(
        "{\"key\":\"mode\",\"description\":\"M.\",\"type\":\"enum\",\"default\":\"a\\nb\","
        "\"range\":{\"values\":[\"a\\nb\"]}}");

    ASSERT_EQ_INT((long long)plugin_keys(reg), 0);
    settings_registry_free(reg);
}

TEST(a_register_that_is_refused_leaves_the_registry_as_it_was)
{
    PluginManifest *man = NULL;
    SettingsRegistry *reg = given_registry();
    char doc[4096];
    size_t before = settings_registry_count(reg);

    given_doc(doc, sizeof(doc), "p",
              "{\"key\":\"ok\",\"description\":\"F.\",\"type\":\"boolean\",\"default\":true},"
              "{\"key\":\"label\",\"description\":\"L.\",\"type\":\"string\","
              "\"default\":\"a\\u0001b\"}");
    ASSERT_EQ_STATUS(plugin_manifest_parse(doc, strlen(doc), &man), PLUGIN_PARSE_OK);
    ASSERT_EQ_STATUS(plugin_settings_register(man, reg), PLUGIN_SETTINGS_REFUSED);
    ASSERT_EQ_INT((long long)settings_registry_count(reg), (long long)before);
    plugin_manifest_free(man);
    settings_registry_free(reg);
}

TEST(a_refused_register_rolls_back_only_its_own_keys)
{
    PluginManifest *good = NULL;
    PluginManifest *bad = NULL;
    SettingsRegistry *reg = given_registry();
    char doc[4096];

    given_doc(doc, sizeof(doc), "pq", k_bool);
    ASSERT_EQ_STATUS(plugin_manifest_parse(doc, strlen(doc), &good), PLUGIN_PARSE_OK);
    given_doc(doc, sizeof(doc), "p",
              "{\"key\":\"label\",\"description\":\"L.\",\"type\":\"string\","
              "\"default\":\"a\\u0001b\"}");
    ASSERT_EQ_STATUS(plugin_manifest_parse(doc, strlen(doc), &bad), PLUGIN_PARSE_OK);
    ASSERT_EQ_STATUS(plugin_settings_register(good, reg), PLUGIN_SETTINGS_OK);
    ASSERT_EQ_STATUS(plugin_settings_register(bad, reg), PLUGIN_SETTINGS_REFUSED);
    ASSERT_EQ_INT((long long)plugin_keys(reg), 2);
    ASSERT_TRUE(setting_by_key(reg, "plugin.pq.loud") != NULL);
    plugin_manifest_free(good);
    plugin_manifest_free(bad);
    settings_registry_free(reg);
}

TEST(register_under_memory_failure_leaves_no_key_of_the_plugin)
{
    PluginManifest *man = NULL;
    char doc[4096];
    int n = 0;
    int ok_seen = 0;

    given_doc(doc, sizeof(doc), "p", k_enum);
    ASSERT_EQ_STATUS(plugin_manifest_parse(doc, strlen(doc), &man), PLUGIN_PARSE_OK);
    for (n = 0; n < 40; n++) {
        SettingsRegistry *reg = given_registry();
        PluginSettingsStatus st = PLUGIN_SETTINGS_OK;
        size_t before = settings_registry_count(reg);

        test_alloc_fail_after(n);
        st = plugin_settings_register(man, reg);
        test_alloc_fail_after(-1);
        if (st == PLUGIN_SETTINGS_OK) {
            ok_seen++;
            ASSERT_EQ_INT((long long)plugin_keys(reg), 2);
        } else {
            ASSERT_EQ_INT(st, PLUGIN_SETTINGS_OOM);
            ASSERT_EQ_INT((long long)settings_registry_count(reg), (long long)before);
        }
        settings_registry_free(reg);
    }
    ASSERT_TRUE(ok_seen > 0);
    plugin_manifest_free(man);
}

/* A manifest the parser would never hand over: register checks a default for itself. */
static PluginManifest *given_hand_made(PluginSettingType type, const char *default_text)
{
    PluginManifest *man = (PluginManifest *)calloc(1U, sizeof(*man));

    ASSERT_TRUE(man != NULL);
    if (man == NULL) {
        return NULL;
    }
    (void)snprintf(man->name, sizeof(man->name), "%s", "hand");
    man->nsettings = 1U;
    (void)snprintf(man->settings[0].key, sizeof(man->settings[0].key), "%s", "w");
    (void)snprintf(man->settings[0].description, sizeof(man->settings[0].description), "%s", "W.");
    (void)snprintf(man->settings[0].default_text, sizeof(man->settings[0].default_text), "%s",
                   default_text);
    man->settings[0].type = type;
    man->settings[0].allow_user = true;
    return man;
}

TEST(a_string_list_default_that_is_not_an_array_of_strings_is_refused_by_register)
{
    const char *bad[] = {"not json", "{}", "[1]", "[\"a\", 2]"};
    size_t i = 0;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        PluginManifest *man = given_hand_made(PLUGIN_SETTING_STRING_LIST, bad[i]);
        SettingsRegistry *reg = given_registry();

        ASSERT_EQ_STATUS(plugin_settings_register(man, reg), PLUGIN_SETTINGS_REFUSED);
        ASSERT_EQ_INT((long long)plugin_keys(reg), 0);
        settings_registry_free(reg);
        free(man);
    }
}

TEST(a_string_list_default_over_the_item_cap_is_refused_by_register)
{
    char many[2048];
    size_t at = 0;
    size_t i = 0;
    PluginManifest *man = NULL;
    SettingsRegistry *reg = given_registry();

    many[at++] = '[';
    for (i = 0; i < 513U; i++) {
        if (i > 0U) {
            many[at++] = ',';
        }
        many[at++] = '"';
        many[at++] = '"';
    }
    many[at++] = ']';
    many[at] = '\0';
    man = given_hand_made(PLUGIN_SETTING_STRING_LIST, many);
    if (man != NULL) {
        ASSERT_EQ_STATUS(plugin_settings_register(man, reg), PLUGIN_SETTINGS_REFUSED);
    }
    free(man);
    settings_registry_free(reg);
}

TEST(reading_a_list_default_under_memory_failure_is_oom_where_jsonread_runs_out)
{
    int n = 0;
    int oom_seen = 0;

    /* A number is the one jsonread value that allocates with malloc, the call the hook fails. */
    for (n = 0; n < 40; n++) {
        PluginManifest *man = given_hand_made(PLUGIN_SETTING_STRING_LIST, "[1]");
        SettingsRegistry *reg = given_registry();
        PluginSettingsStatus st = PLUGIN_SETTINGS_OK;

        test_alloc_fail_after(n);
        st = plugin_settings_register(man, reg);
        test_alloc_fail_after(-1);
        ASSERT_TRUE(st == PLUGIN_SETTINGS_OOM || st == PLUGIN_SETTINGS_REFUSED);
        ASSERT_EQ_INT((long long)plugin_keys(reg), 0);
        if (st == PLUGIN_SETTINGS_OOM) {
            oom_seen++;
        }
        settings_registry_free(reg);
        free(man);
    }
    ASSERT_TRUE(oom_seen > 1);
}

TEST(load_under_memory_failure_returns_oom_or_ok_and_leaks_nothing)
{
    char *plugins = given_plugins_dir();
    char folder[16];
    int n = 0;
    int oom_seen = 0;
    int ok_seen = 0;

    for (n = 0; n < 20; n++) {
        (void)snprintf(folder, sizeof(folder), "f%02d", n);
        given_named_plugin(plugins, folder, folder, k_enum);
    }
    given_named_plugin(plugins, "list", "list", k_list);
    for (n = 0; n < 2000 && ok_seen == 0; n++) {
        SettingsRegistry *reg = given_registry();
        int err = 0;
        PluginSettingsStatus st = PLUGIN_SETTINGS_OK;

        test_alloc_fail_after(n);
        st = plugin_settings_load(plugins, reg, &err);
        test_alloc_fail_after(-1);
        if (st == PLUGIN_SETTINGS_OOM) {
            oom_seen++;
        } else {
            ASSERT_EQ_INT(st, PLUGIN_SETTINGS_OK);
            ASSERT_EQ_INT((long long)plugin_keys(reg), 42);
            ok_seen++;
        }
        settings_registry_free(reg);
    }
    ASSERT_TRUE(oom_seen > 0);
    ASSERT_EQ_INT(ok_seen, 1);
    /* The same with the manifests kept (plan 31): a failed keep unregisters that plugin's keys. */
    oom_seen = 0;
    ok_seen = 0;
    for (n = 0; n < 4000 && ok_seen == 0; n++) {
        SettingsRegistry *reg = given_registry();
        PluginManifestList mans;
        int err = 0;
        PluginSettingsStatus st = PLUGIN_SETTINGS_OK;

        test_alloc_fail_after(n);
        st = plugin_settings_load_manifests(plugins, reg, &mans, &err);
        test_alloc_fail_after(-1);
        if (st == PLUGIN_SETTINGS_OOM) {
            ASSERT_EQ_INT((long long)mans.count, 0);
            oom_seen++;
        } else {
            ASSERT_EQ_INT(st, PLUGIN_SETTINGS_OK);
            ASSERT_EQ_INT((long long)mans.count, 21);
            ok_seen++;
        }
        plugin_manifest_list_free(&mans);
        settings_registry_free(reg);
    }
    ASSERT_TRUE(oom_seen > 0);
    ASSERT_EQ_INT(ok_seen, 1);
    free(plugins);
}

/* ---- the built-in plugin --------------------------------------------------------------- */

TEST(hybrid_has_only_enabled_key)
{
    SettingsRegistry *reg = given_registry();
    const SettingSpec *spec = NULL;

    ASSERT_EQ_STATUS(plugin_settings_register_hybrid(reg), PLUGIN_SETTINGS_OK);
    ASSERT_EQ_INT((long long)plugin_keys(reg), 1);
    spec = spec_of(reg, "plugin.hybrid.enabled");
    ASSERT_EQ_INT(spec->type, SETTING_BOOLEAN);
    ASSERT_TRUE(spec->allow_user);
    ASSERT_TRUE(spec->allow_brain);
    assert_default(spec, "false");
    ASSERT_STREQ(spec->description, "Turns the \"hybrid\" plugin on or off.");
    settings_registry_free(reg);
}

TEST(registering_hybrid_twice_is_refused_and_keeps_one_key)
{
    SettingsRegistry *reg = given_registry();

    ASSERT_EQ_STATUS(plugin_settings_register_hybrid(reg), PLUGIN_SETTINGS_OK);
    ASSERT_EQ_STATUS(plugin_settings_register_hybrid(reg), PLUGIN_SETTINGS_REFUSED);
    ASSERT_EQ_INT((long long)plugin_keys(reg), 1);
    settings_registry_free(reg);
}

TEST(an_installed_manifest_named_hybrid_adds_no_key)
{
    char *plugins = given_plugins_dir();
    SettingsRegistry *reg = given_registry();
    int err = 0;

    ASSERT_EQ_STATUS(plugin_settings_register_hybrid(reg), PLUGIN_SETTINGS_OK);
    given_named_plugin(plugins, "hybrid", "hybrid", k_bool);
    given_named_plugin(plugins, "h2", "hybrid", NULL);
    ASSERT_EQ_STATUS(plugin_settings_load(plugins, reg, &err), PLUGIN_SETTINGS_OK);
    ASSERT_EQ_INT((long long)plugin_keys(reg), 1);
    ASSERT_TRUE(setting_by_key(reg, "plugin.hybrid.loud") == NULL);
    ASSERT_TRUE(setting_by_key(reg, "plugin.hybrid_h2.enabled") == NULL);
    settings_registry_free(reg);
    free(plugins);
}

TEST(a_manifest_named_hybrid_is_refused_by_register_and_leaves_the_registry_alone)
{
    PluginManifest *man = NULL;
    SettingsRegistry *reg = given_registry();
    char doc[4096];

    given_doc(doc, sizeof(doc), "hybrid", k_bool);
    ASSERT_EQ_STATUS(plugin_manifest_parse(doc, strlen(doc), &man), PLUGIN_PARSE_OK);
    ASSERT_EQ_STATUS(plugin_settings_register(man, reg), PLUGIN_SETTINGS_REFUSED);
    ASSERT_EQ_INT((long long)plugin_keys(reg), 0);
    plugin_manifest_free(man);
    settings_registry_free(reg);
}

TEST(registering_a_name_that_is_already_registered_is_refused_and_keeps_the_first)
{
    PluginManifest *man = NULL;
    SettingsRegistry *reg = given_registry();
    char doc[4096];

    given_doc(doc, sizeof(doc), "p", k_bool);
    ASSERT_EQ_STATUS(plugin_manifest_parse(doc, strlen(doc), &man), PLUGIN_PARSE_OK);
    ASSERT_EQ_STATUS(plugin_settings_register(man, reg), PLUGIN_SETTINGS_OK);
    ASSERT_EQ_STATUS(plugin_settings_register(man, reg), PLUGIN_SETTINGS_REFUSED);
    ASSERT_EQ_INT((long long)plugin_keys(reg), 2);
    plugin_manifest_free(man);
    settings_registry_free(reg);
}

/* ---- unregister ------------------------------------------------------------------------ */

TEST(unregister_removes_every_key_of_the_plugin_and_no_other)
{
    char *plugins = given_plugins_dir();
    SettingsRegistry *reg = NULL;
    size_t builtin = settings_registry_count(settings_registry_builtin());

    given_named_plugin(plugins, "a", "a", k_bool);
    given_named_plugin(plugins, "ab", "ab", k_bool);
    given_named_plugin(plugins, "b", "b", NULL);
    reg = given_loaded(plugins);
    ASSERT_EQ_INT((long long)plugin_keys(reg), 5);
    plugin_settings_unregister("a", reg);
    ASSERT_TRUE(setting_by_key(reg, "plugin.a.enabled") == NULL);
    ASSERT_TRUE(setting_by_key(reg, "plugin.a.loud") == NULL);
    ASSERT_TRUE(setting_by_key(reg, "plugin.ab.enabled") != NULL);
    ASSERT_TRUE(setting_by_key(reg, "plugin.ab.loud") != NULL);
    ASSERT_TRUE(setting_by_key(reg, "plugin.b.enabled") != NULL);
    ASSERT_EQ_INT((long long)settings_registry_count(reg), (long long)builtin + 3);
    plugin_settings_unregister("a", reg);
    plugin_settings_unregister("never_installed", reg);
    ASSERT_EQ_INT((long long)plugin_keys(reg), 3);
    settings_registry_free(reg);
    free(plugins);
}

TEST(unregister_keeps_the_compiled_rows)
{
    SettingsRegistry *reg = given_one_setting(NULL);
    size_t builtin = settings_registry_count(settings_registry_builtin());

    plugin_settings_unregister("p", reg);
    plugin_settings_unregister("remember", reg);
    ASSERT_EQ_INT((long long)settings_registry_count(reg), (long long)builtin);
    ASSERT_TRUE(setting_by_key(reg, "remember.search.max_results") != NULL);
    settings_registry_free(reg);
}

TEST(a_spec_pointer_of_another_plugin_survives_an_unregister)
{
    char *plugins = given_plugins_dir();
    SettingsRegistry *reg = NULL;
    const SettingSpec *keep = NULL;

    given_named_plugin(plugins, "a", "a", NULL);
    given_named_plugin(plugins, "b", "b", NULL);
    reg = given_loaded(plugins);
    keep = spec_of(reg, "plugin.b.enabled");
    plugin_settings_unregister("a", reg);
    ASSERT_STREQ(keep->key, "plugin.b.enabled");
    settings_registry_free(reg);
    free(plugins);
}

/* ---- the folder reader ----------------------------------------------------------------- */

TEST(a_missing_plugins_folder_is_no_plugin)
{
    char *plugins = given_plugins_dir();
    SettingsRegistry *reg = given_loaded(plugins);

    ASSERT_EQ_INT((long long)plugin_keys(reg), 0);
    settings_registry_free(reg);
    free(plugins);
}

TEST(a_plugins_path_that_is_a_file_is_no_plugin)
{
    char *plugins = given_plugins_dir();
    SettingsRegistry *reg = NULL;

    harness_write_file(plugins, "not a folder");
    reg = given_loaded(plugins);
    ASSERT_EQ_INT((long long)plugin_keys(reg), 0);
    settings_registry_free(reg);
    free(plugins);
}

TEST(a_plugins_folder_that_cannot_be_read_is_a_dir_error_with_the_errno)
{
    char *plugins = given_plugins_dir();
    SettingsRegistry *reg = given_registry();
    int err = 0;

    given_named_plugin(plugins, "a", "a", NULL);
    ASSERT_EQ_INT(chmod(plugins, 0), 0);
    if (access(plugins, R_OK) != 0) {
        ASSERT_EQ_STATUS(plugin_settings_load(plugins, reg, &err), PLUGIN_SETTINGS_DIR);
        ASSERT_EQ_INT(err, EACCES);
        ASSERT_EQ_INT((long long)plugin_keys(reg), 0);
    }
    (void)chmod(plugins, 0700);
    settings_registry_free(reg);
    free(plugins);
}

TEST(a_folder_with_no_plugin_json_and_a_stray_file_register_nothing)
{
    char *plugins = given_plugins_dir();
    char stray[1024];
    char empty_folder[1024];
    SettingsRegistry *reg = NULL;

    path_in(stray, sizeof(stray), plugins, "readme.txt", "x");
    harness_write_file(stray, "x");
    path_in(empty_folder, sizeof(empty_folder), plugins, "nothing", "other.txt");
    harness_write_file(empty_folder, "x");
    given_named_plugin(plugins, "real", "real", NULL);
    reg = given_loaded(plugins);
    ASSERT_EQ_INT((long long)plugin_keys(reg), 1);
    settings_registry_free(reg);
    free(plugins);
}

TEST(a_plugin_json_of_exactly_64_kib_is_read_and_one_byte_more_is_not)
{
    char *plugins = given_plugins_dir();
    SettingsRegistry *reg = NULL;
    char *doc = (char *)malloc((size_t)PLUGIN_JSON_MAX + 2U);
    size_t used = 0;

    ASSERT_TRUE(doc != NULL);
    if (doc == NULL) {
        free(plugins);
        return;
    }
    given_doc(doc, 4096, "fits", NULL);
    used = strlen(doc);
    memset(doc + used, ' ', (size_t)PLUGIN_JSON_MAX - used);
    doc[PLUGIN_JSON_MAX] = '\0';
    given_plugin(plugins, "fits", doc);
    given_doc(doc, 4096, "toobig", NULL);
    used = strlen(doc);
    memset(doc + used, ' ', (size_t)PLUGIN_JSON_MAX + 1U - used);
    doc[PLUGIN_JSON_MAX + 1] = '\0';
    given_plugin(plugins, "toobig", doc);
    reg = given_loaded(plugins);
    ASSERT_TRUE(setting_by_key(reg, "plugin.fits.enabled") != NULL);
    ASSERT_TRUE(setting_by_key(reg, "plugin.toobig.enabled") == NULL);
    settings_registry_free(reg);
    free(doc);
    free(plugins);
}

TEST(a_plugin_json_that_is_not_a_regular_file_registers_nothing_and_does_not_hang)
{
    char *plugins = given_plugins_dir();
    char dir_path[1024];
    char fifo_path[1024];
    SettingsRegistry *reg = NULL;

    path_in(dir_path, sizeof(dir_path), plugins, "asdir", "plugin.json");
    harness_write_file(dir_path, "x");
    ASSERT_EQ_INT(unlink(dir_path), 0);
    ASSERT_EQ_INT(mkdir(dir_path, 0700), 0);
    path_in(fifo_path, sizeof(fifo_path), plugins, "asfifo", "plugin.json");
    harness_write_file(fifo_path, "x");
    ASSERT_EQ_INT(unlink(fifo_path), 0);
    ASSERT_EQ_INT(mkfifo(fifo_path, 0600), 0);
    given_named_plugin(plugins, "real", "real", NULL);
    reg = given_loaded(plugins);
    ASSERT_EQ_INT((long long)plugin_keys(reg), 1);
    settings_registry_free(reg);
    free(plugins);
}

TEST(a_plugin_json_symlinked_to_a_regular_file_is_read)
{
    char *plugins = given_plugins_dir();
    char target[1024];
    char link_path[1024];
    char doc[4096];
    SettingsRegistry *reg = NULL;

    given_doc(doc, sizeof(doc), "linked", NULL);
    path_in(target, sizeof(target), plugins, "src", "manifest.json");
    harness_write_file(target, doc);
    path_in(link_path, sizeof(link_path), plugins, "dev", "plugin.json");
    harness_write_file(link_path, "x");
    ASSERT_EQ_INT(unlink(link_path), 0);
    ASSERT_EQ_INT(symlink(target, link_path), 0);
    reg = given_loaded(plugins);
    ASSERT_TRUE(setting_by_key(reg, "plugin.linked.enabled") != NULL);
    settings_registry_free(reg);
    free(plugins);
}

TEST(a_plugin_json_that_cannot_be_opened_registers_nothing)
{
    char *plugins = given_plugins_dir();
    char path[1024];
    SettingsRegistry *reg = NULL;

    given_named_plugin(plugins, "locked", "locked", NULL);
    path_in(path, sizeof(path), plugins, "locked", "plugin.json");
    ASSERT_EQ_INT(chmod(path, 0), 0);
    reg = given_loaded(plugins);
    if (access(path, R_OK) != 0) {
        ASSERT_TRUE(setting_by_key(reg, "plugin.locked.enabled") == NULL);
    }
    (void)chmod(path, 0600);
    settings_registry_free(reg);
    free(plugins);
}

TEST(a_plugin_json_with_a_nul_byte_registers_nothing)
{
    char *plugins = given_plugins_dir();
    char doc[4096];
    char path[1024];
    SettingsRegistry *reg = NULL;
    size_t n = 0;

    given_doc(doc, sizeof(doc), "nul", NULL);
    n = strlen(doc);
    doc[n - 1U] = '\0';
    path_in(path, sizeof(path), plugins, "nul", "plugin.json");
    harness_write_bytes_at(path, doc, n, (HarnessSite){__FILE__, __LINE__});
    reg = given_loaded(plugins);
    ASSERT_EQ_INT((long long)plugin_keys(reg), 0);
    settings_registry_free(reg);
    free(plugins);
}

TEST(the_keys_come_from_the_manifest_name_not_the_folder_name)
{
    char *plugins = given_plugins_dir();
    SettingsRegistry *reg = NULL;

    given_named_plugin(plugins, "some-folder", "wake", NULL);
    reg = given_loaded(plugins);
    ASSERT_TRUE(setting_by_key(reg, "plugin.wake.enabled") != NULL);
    ASSERT_EQ_INT((long long)plugin_keys(reg), 1);
    settings_registry_free(reg);
    free(plugins);
}

/* ---- two folders with one name --------------------------------------------------------- */

TEST(the_first_folder_in_byte_order_keeps_the_name_and_a_later_one_gets_name_underscore_folder)
{
    char *plugins = given_plugins_dir();
    SettingsRegistry *reg = NULL;

    given_named_plugin(plugins, "b", "foo", k_bool);
    given_named_plugin(plugins, "a", "foo", NULL);
    reg = given_loaded(plugins);
    ASSERT_TRUE(setting_by_key(reg, "plugin.foo.enabled") != NULL);
    ASSERT_TRUE(setting_by_key(reg, "plugin.foo.loud") == NULL);
    ASSERT_TRUE(setting_by_key(reg, "plugin.foo_b.enabled") != NULL);
    ASSERT_TRUE(setting_by_key(reg, "plugin.foo_b.loud") != NULL);
    ASSERT_STREQ(spec_of(reg, "plugin.foo_b.enabled")->description,
                 "Turns the \"foo_b\" plugin on or off.");
    settings_registry_free(reg);
    free(plugins);
}

TEST(a_folder_name_with_a_hyphen_reads_as_an_underscore_in_the_second_name)
{
    char *plugins = given_plugins_dir();
    SettingsRegistry *reg = NULL;

    given_named_plugin(plugins, "a", "foo", NULL);
    given_named_plugin(plugins, "b-c", "foo", NULL);
    reg = given_loaded(plugins);
    ASSERT_TRUE(setting_by_key(reg, "plugin.foo_b_c.enabled") != NULL);
    settings_registry_free(reg);
    free(plugins);
}

TEST(a_second_name_past_40_characters_is_cut_to_40)
{
    char *plugins = given_plugins_dir();
    SettingsRegistry *reg = NULL;
    char name[41];
    char want[96];

    memset(name, 'n', 38U);
    name[38] = '\0';
    given_named_plugin(plugins, "a", name, NULL);
    given_named_plugin(plugins, "bcd", name, NULL);
    reg = given_loaded(plugins);
    (void)snprintf(want, sizeof(want), "plugin.%s_b.enabled", name);
    ASSERT_TRUE(setting_by_key(reg, want) != NULL);
    ASSERT_TRUE(
        setting_by_key(reg, "plugin.nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn_bcd.enabled") == NULL);
    settings_registry_free(reg);
    free(plugins);
}

/* Review: a folder name longer than the join buffer (NAME_MAX is 255) was refused, not cut. */
TEST(a_second_name_from_a_100_character_folder_is_cut_to_40)
{
    char *plugins = given_plugins_dir();
    SettingsRegistry *reg = NULL;
    char folder[101];

    memset(folder, 'f', 100U);
    folder[100] = '\0';
    given_named_plugin(plugins, "a", "p", NULL);
    given_named_plugin(plugins, folder, "p", NULL);
    reg = given_loaded(plugins);
    ASSERT_TRUE(setting_by_key(reg, "plugin.p_ffffffffffffffffffffffffffffffffffffff.enabled") !=
                NULL);
    settings_registry_free(reg);
    free(plugins);
}

TEST(a_second_name_that_is_not_a_name_registers_nothing)
{
    char *plugins = given_plugins_dir();
    SettingsRegistry *reg = NULL;

    given_named_plugin(plugins, "a", "foo", NULL);
    given_named_plugin(plugins, "b.c", "foo", NULL);
    given_named_plugin(plugins, "bB", "foo", NULL);
    reg = given_loaded(plugins);
    ASSERT_TRUE(setting_by_key(reg, "plugin.foo.enabled") != NULL);
    ASSERT_EQ_INT((long long)plugin_keys(reg), 1);
    settings_registry_free(reg);
    free(plugins);
}

TEST(two_second_names_that_cut_to_one_segment_register_only_the_first)
{
    char *plugins = given_plugins_dir();
    SettingsRegistry *reg = NULL;
    char name[41];

    memset(name, 'n', 38U);
    name[38] = '\0';
    given_named_plugin(plugins, "a", name, k_bool);
    given_named_plugin(plugins, "bcd", name, k_bool);
    given_named_plugin(plugins, "bce", name, NULL);
    reg = given_loaded(plugins);
    ASSERT_EQ_INT((long long)plugin_keys(reg), 4);
    ASSERT_TRUE(setting_by_key(reg, "plugin.nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn_b.loud") !=
                NULL);
    settings_registry_free(reg);
    free(plugins);
}

TEST(a_real_name_that_an_earlier_second_name_took_is_renamed_too)
{
    char *plugins = given_plugins_dir();
    SettingsRegistry *reg = NULL;

    given_named_plugin(plugins, "a", "foo", NULL);
    given_named_plugin(plugins, "c", "foo", NULL);
    given_named_plugin(plugins, "c2", "foo_c", NULL);
    reg = given_loaded(plugins);
    ASSERT_TRUE(setting_by_key(reg, "plugin.foo.enabled") != NULL);
    ASSERT_TRUE(setting_by_key(reg, "plugin.foo_c.enabled") != NULL);
    ASSERT_TRUE(setting_by_key(reg, "plugin.foo_c_c2.enabled") != NULL);
    ASSERT_EQ_INT((long long)plugin_keys(reg), 3);
    settings_registry_free(reg);
    free(plugins);
}

/* ---- the registry itself --------------------------------------------------------------- */

TEST(the_builtin_registry_is_the_six_compiled_rows)
{
    const SettingsRegistry *reg = settings_registry_builtin();

    ASSERT_EQ_INT((long long)settings_registry_count(reg), 6);
    ASSERT_STREQ(settings_registry_at(reg, 0U)->key, "remember.buckets.prefixes");
    ASSERT_TRUE(settings_registry_at(reg, 6U) == NULL);
    ASSERT_TRUE(settings_registry_at(NULL, 0U) == NULL);
}

TEST(a_new_registry_starts_as_the_compiled_rows_and_add_refuses_a_repeated_key)
{
    SettingsRegistry *reg = given_registry();
    SettingSpec row = {.key = "plugin.q.enabled", .type = SETTING_BOOLEAN, .allow_user = true};

    ASSERT_EQ_INT((long long)settings_registry_count(reg), 6);
    ASSERT_TRUE(settings_registry_add(reg, &row));
    ASSERT_FALSE(settings_registry_add(reg, &row));
    row.key = "remember.search.max_results";
    ASSERT_FALSE(settings_registry_add(reg, &row));
    ASSERT_EQ_INT((long long)settings_registry_count(reg), 7);
    ASSERT_STREQ(settings_registry_at(reg, 6U)->key, "plugin.q.enabled");
    settings_registry_free(reg);
}

TEST(a_row_added_is_a_copy_of_the_callers_strings)
{
    SettingsRegistry *reg = given_registry();
    char key[] = "plugin.q.enabled";
    char text[] = "Whether q is on.";
    const char *values[] = {"one", "two"};
    SettingSpec row = {.key = key,
                       .description = text,
                       .values = values,
                       .nvalues = 2U,
                       .type = SETTING_ENUM,
                       .allow_user = true};

    ASSERT_TRUE(settings_registry_add(reg, &row));
    key[0] = 'X';
    text[0] = 'X';
    ASSERT_EQ_INT(key[0], 'X');
    ASSERT_EQ_INT(text[0], 'X');
    values[0] = "gone";
    ASSERT_STREQ(spec_of(reg, "plugin.q.enabled")->description, "Whether q is on.");
    ASSERT_STREQ(spec_of(reg, "plugin.q.enabled")->values[0], "one");
    ASSERT_STREQ(spec_of(reg, "plugin.q.enabled")->values[1], "two");
    settings_registry_free(reg);
}

TEST(add_under_memory_failure_fails_whole_and_leaves_the_registry_as_it_was)
{
    SettingsRegistry *reg = given_registry();
    const char *values[] = {"one", "two"};
    SettingSpec row = {.key = "plugin.q.mode",
                       .default_text = "one",
                       .description = "A mode.",
                       .values = values,
                       .nvalues = 2U,
                       .type = SETTING_ENUM,
                       .allow_user = true};
    int n = 0;
    int failed = 0;

    for (n = 0; n < 8; n++) {
        test_alloc_fail_after(n);
        if (!settings_registry_add(reg, &row)) {
            failed++;
            ASSERT_EQ_INT((long long)settings_registry_count(reg), 6);
        }
        test_alloc_fail_after(-1);
        if (settings_registry_count(reg) == 7U) {
            ASSERT_TRUE(settings_registry_remove_prefix(reg, "plugin.q.") == 1U);
        }
    }
    ASSERT_TRUE(failed > 0);
    settings_registry_free(reg);
}

TEST(a_row_with_more_than_eight_enum_values_is_refused_and_eight_fit)
{
    SettingsRegistry *reg = given_registry();
    const char *values[9] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};
    SettingSpec row = {.key = "plugin.q.mode",
                       .values = values,
                       .nvalues = 9U,
                       .type = SETTING_ENUM,
                       .allow_user = true};

    ASSERT_FALSE(settings_registry_add(reg, &row));
    row.nvalues = 8U;
    ASSERT_TRUE(settings_registry_add(reg, &row));
    ASSERT_STREQ(spec_of(reg, "plugin.q.mode")->values[7], "8");
    settings_registry_free(reg);
}

TEST(remove_prefix_counts_what_it_removed)
{
    SettingsRegistry *reg = given_registry();
    SettingSpec a = {.key = "plugin.q.a", .type = SETTING_BOOLEAN};
    SettingSpec b = {.key = "plugin.q.b", .type = SETTING_BOOLEAN};
    SettingSpec c = {.key = "plugin.qq.a", .type = SETTING_BOOLEAN};

    ASSERT_TRUE(settings_registry_add(reg, &a));
    ASSERT_TRUE(settings_registry_add(reg, &b));
    ASSERT_TRUE(settings_registry_add(reg, &c));
    ASSERT_EQ_INT((long long)settings_registry_remove_prefix(reg, "plugin.q."), 2);
    ASSERT_EQ_INT((long long)settings_registry_remove_prefix(reg, "plugin.q."), 0);
    ASSERT_EQ_INT((long long)settings_registry_remove_prefix(reg, "remember."), 0);
    ASSERT_EQ_INT((long long)settings_registry_count(reg), 7);
    settings_registry_free(reg);
}

TEST(plugin_name_reads_the_segment_after_plugin_dot_and_nothing_else)
{
    char name[41];

    ASSERT_TRUE(setting_plugin_name("plugin.foo.enabled", name, sizeof(name)));
    ASSERT_STREQ(name, "foo");
    ASSERT_TRUE(setting_plugin_name("plugin.fo-o.a.b", name, sizeof(name)));
    ASSERT_STREQ(name, "fo_o");
    ASSERT_FALSE(setting_plugin_name("pluginx.foo.enabled", name, sizeof(name)));
    ASSERT_FALSE(setting_plugin_name("pluginxy.foo.enabled", name, sizeof(name)));
    ASSERT_FALSE(setting_plugin_name("plugin.foo", name, sizeof(name)));
    ASSERT_FALSE(setting_plugin_name("remember.search.max_results", name, sizeof(name)));
    ASSERT_FALSE(setting_plugin_name("plugin.foo.enabled", name, 3U));
    ASSERT_FALSE(setting_plugin_name("not a key", name, sizeof(name)));
}

TEST(a_key_that_only_starts_with_plugin_is_not_a_missing_plugin)
{
    char name[41];

    ASSERT_FALSE(setting_plugin_missing(settings_registry_builtin(), "pluginx.foo.enabled", name,
                                        sizeof(name)));
    ASSERT_TRUE(setting_plugin_missing(settings_registry_builtin(), "plugin.foo.enabled", name,
                                       sizeof(name)));
    ASSERT_STREQ(name, "foo");
}

TEST(a_suggestion_looks_at_the_plugin_keys_too)
{
    SettingsRegistry *reg = given_one_setting(k_bool);

    ASSERT_STREQ(setting_suggestion(reg, "plugin.p.lod"), "plugin.p.loud");
    ASSERT_TRUE(setting_suggestion(settings_registry_builtin(), "plugin.p.lod") == NULL);
    settings_registry_free(reg);
}

void register_plugin_settings_tests(void)
{
    RUN_TEST(bool_type_accepts_true_false_only);
    RUN_TEST(a_boolean_takes_one_value_only);
    RUN_TEST(installed_plugin_has_enabled_key_default_false);
    RUN_TEST(enabled_scopes_user_and_brain);
    RUN_TEST(the_enabled_description_names_the_plugin);
    RUN_TEST(a_hyphen_typed_in_the_plugin_segment_finds_the_underscore_key);
    RUN_TEST(a_plugin_that_is_not_installed_has_no_key);
    RUN_TEST(manifest_setting_generates_key_with_its_type);
    RUN_TEST(a_manifest_setting_carries_its_description);
    RUN_TEST(a_manifest_setting_scope_is_user_by_default_and_brain_or_both_when_it_says_so);
    RUN_TEST(a_boolean_setting_default_and_values);
    RUN_TEST(an_integer_setting_has_its_range);
    RUN_TEST(an_integer_range_of_exactly_zero_allows_only_zero);
    RUN_TEST(a_range_with_only_a_max_has_a_minimum_of_zero);
    RUN_TEST(a_range_with_only_a_max_of_zero_allows_only_zero);
    RUN_TEST(a_range_with_only_a_min_has_no_upper_bound);
    RUN_TEST(a_duration_setting_is_stored_in_canonical_form_inside_its_range);
    RUN_TEST(a_size_setting_is_stored_in_canonical_form_inside_its_range);
    RUN_TEST(an_enum_setting_takes_one_of_its_values_exactly);
    RUN_TEST(a_string_setting_takes_text_with_quotes_and_backslashes_escaped_in_the_stored_json);
    RUN_TEST(a_string_setting_refuses_a_control_byte_del_bad_utf8_and_more_than_511_bytes);
    RUN_TEST(a_string_list_setting_stores_a_compact_array_and_holds_at_most_max_items);
    RUN_TEST(a_string_list_item_that_is_not_text_refuses_the_whole_list);
    RUN_TEST(a_stored_value_is_ok_only_in_the_form_a_set_would_store);
    RUN_TEST(a_string_list_with_no_max_items_is_held_by_the_stored_value_cap);
    RUN_TEST(a_string_list_of_exactly_16384_stored_bytes_fits_and_one_more_does_not);
    RUN_TEST(text_unquote_reads_only_what_the_writer_writes);
    RUN_TEST(a_default_that_does_not_fit_the_buffer_has_no_default_json);
    RUN_TEST(
        an_enum_value_too_long_to_store_is_a_quota_error_and_a_list_with_no_items_pointer_is_invalid);
    RUN_TEST(text_list_join_gives_the_items_with_one_space_between);
    RUN_TEST(invalid_manifest_registers_nothing);
    RUN_TEST(a_manifest_with_one_bad_setting_registers_none_of_its_keys);
    RUN_TEST(a_default_the_registry_refuses_registers_no_key_of_the_plugin);
    RUN_TEST(an_enum_value_the_registry_refuses_registers_no_key_of_the_plugin);
    RUN_TEST(a_register_that_is_refused_leaves_the_registry_as_it_was);
    RUN_TEST(a_refused_register_rolls_back_only_its_own_keys);
    RUN_TEST(register_under_memory_failure_leaves_no_key_of_the_plugin);
    RUN_TEST(a_string_list_default_that_is_not_an_array_of_strings_is_refused_by_register);
    RUN_TEST(a_string_list_default_over_the_item_cap_is_refused_by_register);
    RUN_TEST(reading_a_list_default_under_memory_failure_is_oom_where_jsonread_runs_out);
    RUN_TEST(load_under_memory_failure_returns_oom_or_ok_and_leaks_nothing);
    RUN_TEST(hybrid_has_only_enabled_key);
    RUN_TEST(registering_hybrid_twice_is_refused_and_keeps_one_key);
    RUN_TEST(an_installed_manifest_named_hybrid_adds_no_key);
    RUN_TEST(a_manifest_named_hybrid_is_refused_by_register_and_leaves_the_registry_alone);
    RUN_TEST(registering_a_name_that_is_already_registered_is_refused_and_keeps_the_first);
    RUN_TEST(unregister_removes_every_key_of_the_plugin_and_no_other);
    RUN_TEST(unregister_keeps_the_compiled_rows);
    RUN_TEST(a_spec_pointer_of_another_plugin_survives_an_unregister);
    RUN_TEST(a_missing_plugins_folder_is_no_plugin);
    RUN_TEST(a_plugins_path_that_is_a_file_is_no_plugin);
    RUN_TEST(a_plugins_folder_that_cannot_be_read_is_a_dir_error_with_the_errno);
    RUN_TEST(a_folder_with_no_plugin_json_and_a_stray_file_register_nothing);
    RUN_TEST(a_plugin_json_of_exactly_64_kib_is_read_and_one_byte_more_is_not);
    RUN_TEST(a_plugin_json_that_is_not_a_regular_file_registers_nothing_and_does_not_hang);
    RUN_TEST(a_plugin_json_symlinked_to_a_regular_file_is_read);
    RUN_TEST(a_plugin_json_that_cannot_be_opened_registers_nothing);
    RUN_TEST(a_plugin_json_with_a_nul_byte_registers_nothing);
    RUN_TEST(the_keys_come_from_the_manifest_name_not_the_folder_name);
    RUN_TEST(
        the_first_folder_in_byte_order_keeps_the_name_and_a_later_one_gets_name_underscore_folder);
    RUN_TEST(a_folder_name_with_a_hyphen_reads_as_an_underscore_in_the_second_name);
    RUN_TEST(a_second_name_past_40_characters_is_cut_to_40);
    RUN_TEST(a_second_name_from_a_100_character_folder_is_cut_to_40);
    RUN_TEST(a_second_name_that_is_not_a_name_registers_nothing);
    RUN_TEST(two_second_names_that_cut_to_one_segment_register_only_the_first);
    RUN_TEST(a_real_name_that_an_earlier_second_name_took_is_renamed_too);
    RUN_TEST(the_builtin_registry_is_the_six_compiled_rows);
    RUN_TEST(a_new_registry_starts_as_the_compiled_rows_and_add_refuses_a_repeated_key);
    RUN_TEST(a_row_added_is_a_copy_of_the_callers_strings);
    RUN_TEST(add_under_memory_failure_fails_whole_and_leaves_the_registry_as_it_was);
    RUN_TEST(a_row_with_more_than_eight_enum_values_is_refused_and_eight_fit);
    RUN_TEST(remove_prefix_counts_what_it_removed);
    RUN_TEST(plugin_name_reads_the_segment_after_plugin_dot_and_nothing_else);
    RUN_TEST(a_key_that_only_starts_with_plugin_is_not_a_missing_plugin);
    RUN_TEST(a_suggestion_looks_at_the_plugin_keys_too);
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
