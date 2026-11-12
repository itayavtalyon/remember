# 30 — Generated plugin settings keys

**Area:** the settings registry's boolean type and the generated `plugin.<name>.*` keys (`src/settings.c`, `src/settings.h`, `src/cmd_settings.c`, `src/plugin_settings.c` new).
**Needs:** plan 25 (the manifest parser). No other plan.
**Blocked on:** nothing. The starting code is on branch `plan/25-plugins` (G1 A).

## Goal

Each installed plugin gets `plugin.<name>.enabled` (boolean, scopes user and brain, default false), and each manifest `settings` item gets `plugin.<name>.<key>`. `remember settings` lists, gets, sets and resets them like any key.

## Source

- `decision:2026-11-11-plugin-enabled-bool`: "`plugin.<name>.enabled` stores JSON `true` or `false`, and the registry gains a boolean type."
- Log 010 H-16 (generated keys), log 007 ST-1.1 (segment rule), ST-1.3 (`-` becomes `_`), ST-5.7 to ST-5.10, ST-9.6.
- Old plan 25 "Logs and CLI", last paragraph: "Generate `plugin.<name>.enabled` for each installed plugin, scopes user and brain, default false. Generate other keys from the manifest `settings` list."
- 007 test 8 (manifest key `enabled` is invalid; C-36 reserved-key lines) is already plan 25. 007 test 23 moves to plan 33.
- `decision:2026-11-11-plan25-branch` (G1 A).
- Spec: `specs/SETTINGS.md` gains section "Plugin keys".

## Out of scope

`plugin enable` and `plugin disable` (plan 34), trust (plan 33), the untrusted `settings set` failure (plan 33).

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md). Tests in `tests/test_settings.c` (suite `settings`) and `tests/test_plugin_settings.c` (new, suite `plugins`).

Starting code: the branch's uncommitted changes to `src/settings.c`, `src/settings.h`, `src/cmd_settings.c` and `tests/test_settings.c`. Rebase them onto origin/main after plans 25 and 26 merge. Move the key generation into `src/plugin_settings.c` with `plugin_settings_register(const PluginManifest *m, SettingsRegistry *r)` and `plugin_settings_unregister(const char *name, SettingsRegistry *r)`.

## What must be true

- The registry type enum gains `SETTING_TYPE_BOOL`. Stored values are the JSON literals `true` and `false`. `settings set plugin.x.enabled yes` fails with `invalid_value` (the existing C-01 row for a bad value).
- On every brain open, the engine lists `<plugins_dir>/*/plugin.json`, parses each with plan 25, and registers `plugin.<name>.enabled` (type bool, scopes user and brain, default false) and one key per manifest `settings` item with the item's own type, scopes and default. A manifest that fails to parse registers nothing and is not an error here.
- The built-in `hybrid` registers `plugin.hybrid.enabled` on every open, with no manifest.
- A stored row whose plugin is not installed behaves exactly as log 007 ST-9.6 states for a key that is not registered; this plan adds no new rule for it.
- `settings explain plugin.x.enabled` names the source plugin.

## Review points

- The generated keys come from the parsed manifest only; no other file is read.
- No key is generated for the reserved name `hybrid` other than `plugin.hybrid.enabled`.

## Definition of Done

- [ ] `bool_type_accepts_true_false_only`.
- [ ] `installed_plugin_has_enabled_key_default_false`.
- [ ] `enabled_scopes_user_and_brain`.
- [ ] `manifest_setting_generates_key_with_its_type`.
- [ ] `invalid_manifest_registers_nothing`.
- [ ] `uninstalled_plugin_row_follows_st_9_6`.
- [ ] `hybrid_has_only_enabled_key`.
- [ ] `specs/SETTINGS.md` "Plugin keys" written; `decision:2026-11-11-plugin-enabled-bool` is `state:implemented`.
- [ ] The gate passes on the committed tree.

## Implementation Notes

## Review Notes
