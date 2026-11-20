#ifndef REMEMBER_PLUGIN_SETTINGS_H
#define REMEMBER_PLUGIN_SETTINGS_H

#include "plugin_manifest.h"
#include "settings.h"

/* The settings keys of installed plugins (specs/SETTINGS.md "Plugin keys"). A plugin gets
   plugin.<name>.enabled and one plugin.<name>.<key> per manifest setting. Every key comes
   from the parsed plugin.json; no other file is read. */

/* The built-in plugin. Its folder holds no manifest, so no installed folder may take the name. */
#define PLUGIN_HYBRID_NAME "hybrid"

/* The largest plugin.json the reader opens (decision:2026-11-16-plan30-plugin-json-reader-cap). */
enum { PLUGIN_JSON_MAX = 65536 };

typedef enum PluginSettingsStatus {
    PLUGIN_SETTINGS_OK = 0,
    PLUGIN_SETTINGS_REFUSED, /* the registry cannot hold a setting of the manifest */
    PLUGIN_SETTINGS_DIR,     /* the plugins folder exists and cannot be read */
    PLUGIN_SETTINGS_OOM
} PluginSettingsStatus;

/* Register plugin.<m->name>.enabled and a key for each of m->settings. All or nothing: when a
   setting's default or an enum value is not a value the type accepts, or memory runs out, no
   key of m stays in reg. A manifest named hybrid registers nothing, and the name is not a
   duplicate: the caller checks both (plugin_settings_load does). */
PluginSettingsStatus plugin_settings_register(const PluginManifest *m, SettingsRegistry *reg);

/* Remove plugin.<name>.* (every key of that plugin) from reg. plugin.<name>x.* stays. */
void plugin_settings_unregister(const char *name, SettingsRegistry *reg);

/* plugin.hybrid.enabled: the built-in plugin has no manifest. */
PluginSettingsStatus plugin_settings_register_hybrid(SettingsRegistry *reg);

/* List <plugins_dir>/<folder>/plugin.json in byte order of the folder name, parse each, and
   register the manifests that parse with no error. A folder with no plugin.json, a file that is
   not a regular file or is over PLUGIN_JSON_MAX, an unreadable file, and a manifest with an
   error register nothing and are not an error. A missing plugins folder is no plugin. A name
   that an earlier folder holds becomes <name>_<folder> cut to 40 characters, or nothing when
   that is not a name or is taken too (decision:2026-11-16-plan30-duplicate-plugin-name).
   *sys_errno is set when the status is PLUGIN_SETTINGS_DIR. */
PluginSettingsStatus plugin_settings_load(const char *plugins_dir, SettingsRegistry *reg,
                                          int *sys_errno);

#endif /* REMEMBER_PLUGIN_SETTINGS_H */
