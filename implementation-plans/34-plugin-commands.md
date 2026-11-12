# 34 — Plugin and hook commands

**Area:** `src/cmd_plugin.c` (new) and the plugin install folder: `hook add`, `plugin add|list|show|enable|disable|trust|remove|log|status` and the plugin search path.
**Needs:** plans 30 and 33. No other plan.
**Blocked on:** nothing.

## Goal

The user installs, lists, enables, disables, trusts and removes plugins and hooks from the CLI, with the old plan 25 synopsis.

## Source

- Log 010 H-10 (install), H-11 (enable), H-15 (logs), H-16 (CLI); `decision:s-05`, `decision:s-14`.
- Old plan 25 "Trust and install" paragraphs 6 to 11 and "Logs and CLI".
- `decision:2026-11-11-d17-sync-commands` (G6 A): only `plugin rerun` and `plugin test` print `plugin_action_timeout` and `plugin_action_failed`.
- 007 tests 10 and 30: `plugin enable` and `plugin disable --scope brain`; `hook add url-expand` creates `url_expand`.
- Copy: C-18, C-19, C-25, C-35, C-36, C-37 and the D-14 `skipped_source` line in `COPY-PENDING.md`.
- Spec: `specs/PLUGIN-CLI.md` (new).

## Out of scope

`plugin rerun` and `remember dispatch` (plan 35). `plugin reindex` and the backfill (plan 36). `plugin test` (plan 38). The daemon plugin dir argument (plan 40).

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md). Tests in `tests/test_plugin_cli.c` (new, suite `plugins`).

## What must be true

Synopsis (this plan's commands only):

- `remember hook add NAME --command PATH [ARG…] --event EVENT [--event …] [--tag-any P]… [--tag-all P]… [--created-by KIND]… [--updated-by KIND]… [--bin BIN]… [--origin ORIGIN]… [--not-caused-by NAME]… [--timeout 60s] [--cwd PATH] [--disabled]`
- `remember plugin add DIR [--dev]`
- `remember plugin list | show NAME | status [NAME] [--ack NAME]`
- `remember plugin enable NAME [--scope brain] | disable NAME [--scope brain]`
- `remember plugin trust NAME`
- `remember plugin remove NAME [--delete-data]`
- `remember plugin log NAME [--follow] [--since DURATION]`

Search path: `plugin_search_dirs()` returns, in order, the daemon's plugin dir argument when set (plan 40 fills it; empty here), then `RememberEnv.plugins_dir`. Built-ins (`hybrid`) are always present.

Install: once per user into `<plugins_dir>/<name>/`. `plugin add DIR` copies the folder after a true confirm (plan 33). An installed name updates: confirm, copy, new hash, then `shutdown` reason `upgrade` (sent by plan 36 when processes exist; a no-op here). `plugin add` of a built-in name fails with `plugin_exists` and C-18. `plugin add --dev <dir>` uses the folder in place, re-hashes at each start, needs a terminal, and marks searches with `plugin_dev_mode` (C-25).

`hook add`: writes `plugin.json` with one once-action named `run`. The name is the typed name with `-` replaced by `_`, and it must match a settings segment. The action holds the command, the events, the filter, `timeout` default `60s` range `1s`–`1h`, and `working_directory` default `~`. After a true confirm, write `plugin.<name>.enabled` true on the user layer. `--disabled` writes no enabled row. `--command` takes the rest of argv until the next `hook add` flag; `--` passes arguments that start with `-`. `--origin` default is `local`. `--not-caused-by` is a plugin name (every entry) or `plugin/<name>/<entry>`.

`plugin enable` and `plugin disable` are `settings set plugin.<name>.enabled true|false` with no prompt. `enable` also clears `crash_loop` for this brain (the field exists in `index.json` from plan 36; here the clear is a write of `disabled_reason` null when it is `crash_loop`).

A brain where `enabled` resolves true for a plugin that is not installed lists `missing` and warns `plugin_missing` (C-19).

`plugin remove NAME` deletes the install and the trust record and keeps every `<brain>.d/plugins/<name>/` (C-37). `--delete-data` also deletes those folders for brains in `brains.json`. No prompt. Reserved built-in `hybrid`: trusted, no filter entry, `plugin add hybrid` is `plugin_exists`.

Unknown manifest field at `plugin add` warns `unknown_field` with the C-35 file line. `plugin.json` members `run`, `permissions`, and `filter` are `plugin_manifest_invalid`, not C-35. A hyphen in a manifest name is `plugin_manifest_invalid` with C-36.

`plugin log`: `<brain>.d/hooks.log` JSON Lines, mode 0600, rotated at 5 MB, 3 files kept, entries older than 30 days deleted. `--follow --json` emits one `remember-message` per line: `plugin.run` for a run record and for a `skipped_source` record, `plugin.stderr` with `data` `{plugin, line, at}`. The human `skipped_source` line is the D-14 text in old plan 25 "Status".

`plugin status` exits 0 whenever it can report. `plugin status --ack NAME` clears the `plugin_loop_halted` warning for NAME. JSON types follow `<noun>.<verb>.result`. A hook appears in `plugin.list.result` like any plugin, with `timeout` and `timeout_ms` on its action.

Exit codes: 0 ok (including a degraded command with a warning), 1 usage or error, 2 not found, 3 wrong bin, 4 a warning under `--strict`.

`REMEMBER_PROCESSES=0`: `hook add` and folder `plugin add` return `plugin_unsupported` (C-23).

- **Warnings carry a `fix` (E3 B, `decision:2026-11-12-e3-warnings-have-fix-field`).** Every warning this plan prints has a `message` and a non-empty `fix`: `plugin_missing`, `plugin_loop_halted`, `plugin_unsupported`, `plugin_dev_mode`, `plugin_action_timeout`, `plugin_action_failed` and `unknown_field`. The wording is copy item E3B-1, entries 2, 12, 15, 16, 21, 22 and 26, in `decision:2026-11-12-copy-batch-e3b-warning-splits`: the approved sentence stays word for word and only its next step moves into `fix`; the plain line is `remember: warning: <message> <fix>`. `code`, `detail`, exit codes and `--strict` do not change. Itay approved E3B-1 as a whole on 2026-11-12, including every `fix` that is Story Weaver wording (`decision:2026-11-12-copy-batch-e3b-warning-splits`).

## Review points

- `plugin enable` never prompts.
- No `hooks.json`, no hook registry separate from plugins.

## Definition of Done

- [ ] `hook_add_writes_enabled_user_layer` and `hook_add_disabled_no_row` (010 test 36).
- [ ] `hook_add_url_expand_creates_url_expand` (007 test 30).
- [ ] `plugin_enable_disable_scope_brain` (007 test 10).
- [ ] `plugin_add_builtin_name_c18` (010 test 32, second half).
- [ ] `plugin_add_update_confirms_and_rehashes` (010 test 32, first half, without the shutdown).
- [ ] `plugin_missing_c19`.
- [ ] `plugin_remove_keeps_data_c37` and `remove_delete_data`.
- [ ] `unknown_field_c35` and `hyphen_name_c36`.
- [ ] `plugin_status_json_exit_0` (010 test 27).
- [ ] `hooks_log_rotation_5mb_3_files_30_days`.
- [ ] `log_follow_json_skipped_source_is_plugin_run`.
- [ ] `processes_off_hook_add_unsupported` (010 test 17, command half).
- [ ] `specs/PLUGIN-CLI.md` written.
- [ ] The gate passes on the committed tree.

## Implementation Notes

## Review Notes
