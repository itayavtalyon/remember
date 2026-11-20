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
- Grilled 2026-11-16: `decision:2026-11-16-plan30-registry-explicit` (a `SettingsRegistry` object, passed to every lookup), `decision:2026-11-16-plan30-duplicate-plugin-name`, `decision:2026-11-16-plan30-plugin-json-reader-cap` (this plan owns the `plugin.json` reader and its 64 KiB cap, which `decision:2026-11-13-plugin-manifest-limits` L14 left to plan 33), `decision:2026-11-16-plan30-enabled-description`, `decision:2026-11-16-plan30-plugin-string-values`, `decision:2026-11-16-plan30-value-error-copy-drafts`.
- Spec: `specs/SETTINGS.md` gains section "Plugin keys".

## Out of scope

`plugin enable` and `plugin disable` (plan 34), trust (plan 33), the untrusted `settings set` failure (plan 33).

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md). Tests in `tests/test_settings.c` (suite `settings`) and `tests/test_plugin_settings.c` (new, suite `plugins`).

Starting code: the branch's uncommitted changes to `src/settings.c`, `src/settings.h`, `src/cmd_settings.c` and `tests/test_settings.c`. Plan 30 took the WIP's types and canonical forms (boolean, enum, size, text, text list) and its idea of one registry, and replaced its process-global table with the explicit `SettingsRegistry` Itay chose. The key generation is in `src/plugin_settings.c` with `plugin_settings_register(const PluginManifest *m, SettingsRegistry *r)` and `plugin_settings_unregister(const char *name, SettingsRegistry *r)`.

## What must be true

- The registry type enum gains `SETTING_BOOLEAN` (the plan said `SETTING_TYPE_BOOL`; every member of `SettingType` is `SETTING_<TYPE>`). Stored values are the JSON literals `true` and `false`. `settings set plugin.x.enabled yes` fails with `invalid_value` (the existing C-01 row for a bad value).
- On every brain open, the engine lists `<plugins_dir>/*/plugin.json`, parses each with plan 25, and registers `plugin.<name>.enabled` (type bool, scopes user and brain, default false) and one key per manifest `settings` item with the item's own type, scopes and default. A manifest that fails to parse registers nothing and is not an error here. `<plugins_dir>` is `<data dir>/plugins` (`decision:g-01`); `RememberEnv` has no `plugins_dir` field yet. A `plugin.json` over 64 KiB, one that is not a regular file, and one that cannot be read register nothing. A plugins folder that exists and cannot be read fails the command.
- The built-in `hybrid` registers `plugin.hybrid.enabled` on every open, with no manifest.
- A key of a plugin that is not installed behaves as log 007 ST-5.10 states: `setting_unknown`, with C-03's `plugin <name> isn't installed` line. (The plan cited ST-9.6, the rule that plugins read settings and never write them. It says nothing of a missing plugin. The decision wins.) A settings import skips such a row with the C-12 line.
- `settings explain plugin.x.enabled` names the source plugin.

## Review points

- The generated keys come from the parsed manifest only; no other file is read.
- No key is generated for the reserved name `hybrid` other than `plugin.hybrid.enabled`.

## Definition of Done

- [x] `bool_type_accepts_true_false_only`.
- [x] `installed_plugin_has_enabled_key_default_false`.
- [x] `enabled_scopes_user_and_brain`.
- [x] `manifest_setting_generates_key_with_its_type`.
- [x] `invalid_manifest_registers_nothing`.
- [x] `uninstalled_plugin_key_is_unknown_with_the_not_installed_hint` (was `uninstalled_plugin_row_follows_st_9_6`).
- [x] `hybrid_has_only_enabled_key`.
- [x] `specs/SETTINGS.md` "Plugin keys" written; `decision:2026-11-11-plugin-enabled-bool` is `state:implemented`.
- [x] The gate passes on the committed tree (see Gate above).

## Implementation Notes

Coder: Sonnet 5.5 session, 2026-11-16. Branch `claude/amazing-jang-fc3ed8`, one commit on main 37badf7. Not pushed, no PR.

**What was built.** `SettingsRegistry` and the five new types in `src/settings.c` and `src/settings.h`. `src/plugin_settings.c` (new): register, unregister, hybrid, and the `plugin.json` reader. `store_set_settings_registry` and `store_settings_registry` in `src/store_sqlite.c` (the store holds a pointer it does not own). `src/remember_app.c` builds the registry after the open (`load_registry`) and frees it after `store_close`. `src/cmd_settings.c`: the type tokens, display, `_bytes` twins, the unknown-plugin line, the new error lines, and a heap-sized list and explain (the old `REGISTRY_CAP` of 64 rows would have overrun with plugin keys). `src/export_format.c` and `src/cmd_export.c`: the registry reaches the settings export row. `src/store_sqlite.c` import: a boolean row is read by its JSON member type, and `SETTING_SKIP_PLUGIN_NOT_INSTALLED`.

**What the WIP gave.** The type list, the canonical forms, and the single registry idea. Not taken: its process-global table (`plugin_registry_generation`, `plugin_generated_at`), which the manifest parser the WIP was built on no longer has, and its rule that a quote or a backslash is refused in a string.

**Plan corrections (the notes say so, the plan text above is amended).** (1) `SETTING_TYPE_BOOL` is `SETTING_BOOLEAN`, like every member of `SettingType`. (2) ST-9.6 is the rule that plugins never write settings. The rule for a key of a plugin that is not installed is ST-5.10 with C-03, and the import skip is C-12. The decision wins (standard:coding 2). Test `uninstalled_plugin_row_follows_st_9_6` is `uninstalled_plugin_key_is_unknown_with_the_not_installed_hint`. (3) The plugin.json reader and its size cap are this plan's (`decision:2026-11-16-plan30-plugin-json-reader-cap`). `decision:2026-11-13-plugin-manifest-limits` L14 and `33-plugin-trust.md` still say plan 33 owns the cap: the reviewer amends L14 with a dated line and plan 33 loses nothing but a sentence it never had. (4) `RememberEnv` has no `plugins_dir`, and nothing else passes one. `<plugins_dir>` is `<data dir>/plugins`, the data dir of `decision:g-01`, found by `util_resolve_data_dir`. Plan 29's handle API adds the field.

**Decisions filed this plan** (all `state:accepted` when asked, `state:implemented` now): `decision:2026-11-16-plan30-registry-explicit`, `-duplicate-plugin-name`, `-plugin-json-reader-cap`, `-enabled-description`, `-plugin-string-values`, `-value-error-copy-drafts`. Search misses: `remember-miss:plan30-manifest-type-error-copy`, `plan30-plan-st-9-6-cite`, `plan30-plugin-json-cap-owner`, `plan30-plugins-folder-unreadable-copy`.

**Open, not decided (specs/SETTINGS.md marks them).** (a) The copy drafts in `COPY-PENDING.md` are printed and not approved. (b) `cannot read the plugins folder` was not put to Itay: it is the one line no decision words. (c) `can hold 1 to 1 items` and `Give 1 items or fewer` for `max_items` 1. (d) A one-sided range prints the type's own limit (9223372036854775807) as the missing bound. (e) E3B-1 entry 8 gives `plugin_not_installed` a fix, `Install plugin <name>, then import the file again.` Main has no warning `fix` field yet (the E3B-1 session adds it), so the warning prints the message alone. When that merges, `warn_skip` in `src/cmd_settings.c` needs the fix on this branch. *Review: done in 24b4960 after main's #80 merged.*

**The tests.** Red first: `tests/test_plugin_settings.c` and the CLI tests in `tests/test_settings.c` were written against the declared API, and the first build failed (calls with the old arguments in `store_sqlite.c`, and no implementation). Gate suite `plugins` holds `test_plugin_settings.c` (it runs in the black-box binary and in `remember_store_tests`). Each Definition of Done box names its test below.

**Mutants.** About 60 mutants over `settings.c`, `plugin_settings.c`, `store_sqlite.c`, `cmd_settings.c` and `remember_app.c`, each applied alone in a copy of the tree (`/private/tmp/claude-501/p30-mut`, a process-group kill and a timeout on every command; a mutant that did not build was rewritten, not counted). Each is red in `remember_store_tests` or the `settings` suite: boolean accepts `1`, drops `false`; enum prefix match; the range check dropped for integer, duration and size (as `ms < 0` guards, since a plain drop did not build); `in_range` at each end and at the zero range; text at 511 bytes, a space refused, DEL allowed; the quote and the backslash unescaped on write and refused on read; any escape accepted; list `max_items` off by one, a leading comma, the 16384-byte cap; registry `at` bound, duplicate key, remove-prefix prefix, enum value cap; default of an enum or text unquoted; the plugin segment prefix (`plugin` for `plugin.`); the hybrid guard at both sites; the 40-character cut; hyphen to underscore; the taken-name test inverted; each range bound and the range flag; scope user and brain swapped; the 64 KiB cap at minus and plus one; an unreadable folder read as absent; unregister and rollback prefixes; a refused register read as an error; the enabled default; the store's registry default; the boolean import member; the `plugin_not_installed` reason; the unknown-plugin line off; enum phrase for two values; the size twin name; the zero and KiB bound units; the size shape; hybrid registered twice; the `app` hybrid and unreadable-folder branches; the export row's registry. Survivors the first pass found, each fixed by a test, then killed: the plugin segment prefix (`pluginxy.foo.enabled`, `plugin_name_reads_the_segment_after_plugin_dot_and_nothing_else`), the rollback prefix (`a_refused_register_rolls_back_only_its_own_keys`), the list byte cap (`a_string_list_of_exactly_16384_stored_bytes_fits_and_one_more_does_not`), the enum value cap (`a_row_with_more_than_eight_enum_values_is_refused_and_eight_fit`). One survivor was a redundant check: `man->nerrors == 0U` after `parsed == PLUGIN_PARSE_OK` (plugin_manifest.c returns OK only with no error); deleted. Not mutated: the `qsort` of folder names. `readdir` order is not specified, so a macOS run may not tell a sorted list from an unsorted one; it is not mutated, and the Linux run is the one that can.

**Probes past the plan's list (run on the built binary).** A newline, an ESC and a 300-byte value in a boolean, an enum, plain and `--json`: the plain line masks the control byte as `?`, JSON escapes it. A 300-byte plugin segment and an ESC in the segment of an unknown plugin key: plain unknown line, no crash. The reader: no plugins folder, a file where the folder is, a folder with mode 0 (`EACCES`, fails the command), a `plugin.json` that is a directory, a FIFO (no hang), a symlink to a file (read), a NUL byte, a file of exactly 65536 bytes (read) and 65537 (not), a folder with no `plugin.json`, and a stray file next to the folders. Strings: a control byte, DEL, a lone continuation byte, a surrogate, 511 and 512 bytes, multi-byte text, a quote and a backslash. Names: a duplicate, a hyphen in the folder, a name cut at 40, two cuts that collide, a real name an earlier rename took, `hybrid`. SQLite reads a JSON `true` as the number 1 (`settings import` of a boolean row failed with `"1" isn't a valid value` until the member type was used): that is a bug the first CLI run found.

**Simplify candidates (at least three).** (1) `Built` in `plugin_settings.c` is about 16 KiB and heap allocated because `setting_canon_value` needs a `SETTING_VALUE_CAP` buffer: the default could be checked on a smaller stack buffer if the canon took a capacity; not done, it would change a public signature for 16 KiB. (2) `report_text_invalid`, `report_enum_invalid` and `report_list_range` are three 20-line message and fix emitters with three different detail shapes; one helper would need a shape argument and save about 20 lines (not counted by a run). (3) `plain_default` round-trips a default (canonical JSON, unquoted, quoted again by `default_json`): a `default_json` member on `SettingSpec` would remove it and break the compiled rows' plain `default_text`. (4) `setting_plugin_name` and `setting_plugin_missing` are two functions with two callers each: kept, the import warning needs the name with no registry. (5) `settings_registry_builtin()` is a static empty registry so a `Store` with none still answers: every store test relies on it.

**Reviewer's list, item by item.** "The generated keys come from the parsed manifest only; no other file is read": `read_manifest` opens `<plugins_dir>/<folder>/plugin.json` and nothing else. Tests: `the_keys_come_from_the_manifest_name_not_the_folder_name`, `a_folder_with_no_plugin_json_and_a_stray_file_register_nothing`. "No key is generated for the reserved name `hybrid` other than `plugin.hybrid.enabled`": `hybrid_has_only_enabled_key`, `an_installed_manifest_named_hybrid_adds_no_key`, `a_manifest_named_hybrid_is_refused_by_register_and_leaves_the_registry_alone`, `registering_hybrid_twice_is_refused_and_keeps_one_key`. The two guards (in `plugin_settings_register` and in `load_folder`) cover two paths: a direct call, and the rename of a duplicate (without the second, a manifest named `hybrid` would register as `hybrid_<folder>`).

**Gate.** Run on commit 90b45fd (the code tree; the notes were added after, in a commit that changes only this file), 2026-11-17, one step after another under the heavy-job lock: build and `ctest` (step_gate, store_asan_gate, install_skill, spec_drift) 0 failed; `scripts/lint-all.sh` with LLVM 22 (format, cppcheck src and tests, gcc -fanalyzer, clang-tidy, scan-build) clean; `scripts/check-coverage.sh` 100% functions, 100% effective lines, pure-TU branches 100%; `cmake --build build --target leaks-macos`; `scripts/ci-linux.sh` OK on ubuntu 24.04 and 26.04 (tidy 18 and 21, IWYU, LSan unit run); `scripts/codeql-local.sh` OK (52 allowed, one new entry: `cpp/path-injection` in `tests/test_settings.c`, the XDG_DATA_HOME fixtures). Earlier runs found and fixed: cppcheck variable scope and a static-linkage hint, a tidy `implicit conversion`, a coverage hole in the OOM path of the list default and in four `report_*` OOM lines, Linux tidy 18 `sizeof` on pointer arrays and an IWYU include, two codeql `snprintf` size alerts. Logs: `/private/tmp/claude-501/p30-gate/*.log`.

## Review Notes

Second-opinion deep review: Opus 5.5 session, 2026-11-17 and 2026-11-18. Branch `claude/reverent-northcutt-5562b1`, reset to the coder's ref, review commits on top, rebased onto main 07388be. Grills: `decision:2026-11-17-plan30-review-grill` (G1 A, G2 A, Itay's answers).

**Plan check.** Every "What must be true" line and every Definition of Done box holds, with the coder's tests. The decisions the plan cites are met, with one gap: `decision:2026-11-16-plan30-duplicate-plugin-name` says a long renamed name is cut to 40, and `second_name` refused a folder name longer than its join buffer instead (finding 4).

**Findings, each fixed with a test that failed first.**
1. Security: `settings explain` printed a manifest `description` raw. An ESC sequence, a newline that forged a second line and BEL from `plugin.json` reached the terminal. It goes through `output_text_human` now (`explain_masks_control_bytes_in_a_manifest_description`).
2. Security: `setting_text_ok` lets a C1 control (U+0080..U+009F) through, so a CSI (U+009B) in a manifest default, an enum value or a user's text printed raw in plain `get` and `list`. The plain display masks it (`decision:plan18c-mask-c1`) with the new `output_mask_in_place`, which shares `output.c`'s one-line rule. JSON keeps the stored text (`plain_output_masks_a_c1_control_in_a_plugin_text_value`, `output_mask_in_place_masks_c0_del_newline_and_c1_and_keeps_tab_and_utf8`).
3. A failed allocation in `read_manifest` (either path join, or the read buffer) read as "no plugin.json", so the plugin quietly showed as not installed. It fails the command as out of memory now. New fault site `read` in `fileio_read_regular` (`out_of_memory_in_the_plugin_reader_fails_the_command`).
4. A duplicate plugin in a folder named over about 42 bytes registered nothing; it is cut to 40 now (`a_second_name_from_a_100_character_folder_is_cut_to_40`).
5. G1 A: a stored plugin value the row refuses (a plugin update changed the type, dropped an enum value, narrowed a range or lowered `max_items`) made `get`, `explain` and the whole `list` fail with `internal error`. It reads as not set now and falls to the next layer, with a `setting_invalid` warning. The rule covers plugin keys only: applied to compiled keys it turned a credential helper id outside the grammar into a fallback to the `env` token, which four existing tests caught. `setting_stored_ok` in `settings.c`, `user_refused` and `brain_refused` on `SettingRead` (replacing its padding). Tests: `a_stored_value_the_new_manifest_type_refuses_falls_to_the_default_with_a_warning`, `a_stored_enum_value_the_manifest_dropped_falls_to_the_default`, `a_refused_brain_value_falls_to_the_user_layer_and_names_scope_brain`, `list_and_explain_go_on_past_a_refused_stored_value`.
6. Main's #80 (warning `fix`) merged under the branch: `warn_invalid_value` looked the key up in the compiled table, so a bad brain row of a plugin key lost `--scope brain`. The registry is passed through now, and the `plugin_not_installed` skip prints E3B-1 entry 8's fix (open item e) (`import_fix_for_a_bad_plugin_brain_row_names_scope_brain`).
7. G2 A: an unreadable plugins folder keeps failing every command. No change.

**Simplify.** `add_row` inlined. A `ponytail:` ceiling on `registry_find`'s linear scan: 100 plugins of 17 keys cost 10 ms per command, 1000 cost about 0.5 s; a hash or sorted index when that matters. The coder's simplify candidates (1)-(5) stand as written.

**Mutants (each killed).** Review fixes: the explain mask, `output_mask_in_place` for C0 and C1, the `copy_display` mask, the `second_name` cut, the ENOMEM read in `read_manifest`, the registry lookup in the warning. G1 (9): the plugin-only scope widened to every key; the user and the brain refused checks dropped; the live flags kept for a refused layer (each layer); the `max_items` check loosened; the canonical `strcmp` dropped; the list check skipped; trailing bytes after `]` accepted. A first pass counted four `setting_stored_ok` mutants as killed when they were not: the script restored each source with an older mtime than the mutant's object, so the next build kept the last `store_sqlite.c` mutant and its one red test showed under every later mutant. The gate's ctest found the stale binary. Rerun with fresh mtimes, the four survived; `a_stored_value_is_ok_only_in_the_form_a_set_would_store` (in `test_plugin_settings.c`) kills them.

**Exploit hunt (plugin.json is untrusted).** Beyond the coder's probes: terminal control in every manifest string that reaches plain output (findings 1, 2), allocation failure on each reader step (finding 3), a long folder name on a duplicate (finding 4), a manifest update under stored rows (finding 5), 1000 plugins (time only). Not found: a path outside `<data dir>/plugins`, a hang, an overrun.

**Follow-up, not this plan.** `brain_list` swallows an out-of-memory read of `brains.json` (`REMEMBER_TEST_EXPORT_FAIL=read#1:12`: rc 0, no message). Plan 26 code; the same "could not tell" rule as finding 3.

**Filed.** `decision:open-2026-11-17-plan30-plugin-keys-copy` (owner Story Weaver: the six draft lines, and item 7, the read-time refused-value warning reusing the C-14 line). `remember-miss:health-snippet-shape`. Folded into `standard:coding`: help text and descriptions cross the trust boundary; ENOMEM counts as "could not tell"; readers are probed over rows an older input wrote. `standard:c`: mask C1 too.

**Grades.** Coder (Sonnet 5.5): B+. Thorough plan corrections, about 60 mutants, wide reader probes, honest open items. Missed: the description and C1 masking (the probes masked C0 in values, not a manifest's own text, and not C1), ENOMEM read as absence, and the read of a row an older manifest wrote. Pointers folded into `standard:coding` as above. In-session reviewer: B. Its list was checked item by item and holds; it did not probe the untrusted manifest's text on the terminal or a manifest change under stored rows, the two places the real defects were.
