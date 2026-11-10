# Approved copy — 2026-10-25

Nothing in this file is pending. Itay approved `ANSWERS-FINAL.md` (25 Oct 2026, 20:50 UTC+2), including the Story Weaver wording (K-1). An implementer prints these lines and does not invent another.

A plain line includes the printer prefix. JSON `message` is the text after that prefix, up to the fix. `fix` is the last next-action sentence. A warning has no `fix`. Keys and values go in double quotes. Commands go in single quotes.

The design logs quote the plain lines this file names. Where a log and this file disagree on a product rule, the log wins and the row is in [`DESIGN-PENDING.md`](DESIGN-PENDING.md). The sentences themselves are approved.

## C-01

| Code | Plain line | message | fix |
|---|---|---|---|
| `schema_too_new` | `remember: this brain was made by a newer version of remember, so it is read-only here. You can still read and export it. Upgrade remember to make changes.` | `this brain was made by a newer version of remember, so it is read-only here. You can still read and export it.` | `Upgrade remember to make changes.` |
| `setting_from_hook` | `remember: a hook or plugin can't change settings. Change them yourself in a terminal or in Settings.` | `a hook or plugin can't change settings.` | `Change them yourself in a terminal or in Settings.` |
| `plugin_run_forbidden` | `remember: a hook or plugin can't run "<command>". Run it yourself in a terminal.` | `a hook or plugin can't run "<command>".` | `Run it yourself in a terminal.` |
| positional locator (`invalid_argument`) | `remember: "<value>" isn't a memory id. Use a number from 'remember list', a UUID, or --key KEY.` | `"<value>" isn't a memory id.` | `Use a number from 'remember list', a UUID, or --key KEY.` |
| `wrong_bin` | `remember: "<key>" isn't deleted, so it can't be purged. Run 'remember delete <key>' first.` | `"<key>" isn't deleted, so it can't be purged.` | `Run 'remember delete <key>' first.` |
| Trash wrong list | `remember: "<key>" is in the Trash. Add --trash to use it.` | `"<key>" is in the Trash.` | `Add --trash to use it.` |
| expired wrong list | `remember: "<key>" has expired. Add --expired to use it.` | `"<key>" has expired.` | `Add --expired to use it.` |

`<command>` is the refused command. `<value>` is the positional the user typed. `<key>` is the key, or the id when the row has no key.

Unchanged, and still locked:

- `flag_meaning_changed`: `remember: warning: --trash now means the Trash (deleted memories). For expired memories, use --expired.` Detail `{"flag":"--trash","use":"--expired"}`.
- `plugin_loop_halted`: `remember: warning: "<name>" was stopped after 100 hook runs in a row set each other off. The write was saved. To run it once more, run 'remember plugin rerun <name> <event-id>' within 7 days. To clear this warning without rerunning, run 'remember plugin status --ack <name>'.`
- Did-you-mean: `remember: unknown setting "<input>". Did you mean "<full key>"? Run 'remember settings list' to see all settings.`
- Budget or grace `setting_unknown` fix: `Budgets and cold-start grace are set by the plugin's manifest. Raise remember.plugins.chain_budget instead.` The message is the `setting_unknown` line.
- Help: `--trash` is `Use the Trash: memories you deleted. Restore them, or purge them for good.` `--expired` is `Use expired memories: ones past their expiry time. They are not in the Trash.` `--hooks` is `Run matching hooks for this write, also ones you turned off for local writes.` `--no-hooks` is `Run no hooks for this write.`
- Trust bodies: `Allow "<name>" to read your memories? It can send them off this Mac.` and `Allow hook "<name>" to run <path>?`
- TTY prompt: `Type "<name>" to allow it, or press Enter to cancel:`

Also replaced (warnings, no fix):

- hybrid/rerank cold start: `remember: warning: hybrid/rerank is still starting, so these results are in keyword order. Search again in a moment.`
- `brain_missing`: `remember: warning: brain "<path>" is in the list but isn't on disk. If you moved it, open it once from its new place. Otherwise run 'remember brain prune' to remove it from the list.`

Plans: 19 (`schema_too_new`), 20 (`setting_from_hook`), 21 (locator, Trash, expired), 25 (`plugin_run_forbidden`, `wrong_bin`, cold start, `brain_missing`, trust prompt).

## C-02

- `--color     Use color: auto, always, or never. auto uses color only on a terminal, when NO_COLOR is unset and TERM isn't dumb.`
- `--quiet     Turn off progress lines on stderr.`

Plan 25 prints these. Plan 21 lists the flags and uses the same lines.

## C-03

`setting_unknown`, no candidate. Plain: `remember: unknown setting "<key>". Run 'remember settings list' to see all settings.` Message: `unknown setting "<key>".` Fix: `Run 'remember settings list' to see all settings.` Plan 20.

## C-04

`setting_unknown`, plugin not installed. Plain: `remember: unknown setting "<key>": plugin <name> isn't installed. Run 'remember settings list' to see all settings.` Message: `unknown setting "<key>": plugin <name> isn't installed.` Fix: `Run 'remember settings list' to see all settings.` Plan 20, after plan 25 generates plugin keys.

## C-05

`setting_invalid_value`. Plain line is `remember: <message> <fix>`.

| Case | message | fix |
|---|---|---|
| Duration | `"<key>" needs a duration with a unit; you gave "<value>".` | `Use a number with ms, s, m, or h, like 800ms or 2s.` |
| Sub-millisecond duration | `"<key>" can't be "<value>": a duration must be a whole number of milliseconds.` | `Use a whole number of milliseconds, like 800ms or 1.5s.` |
| Size | `"<key>" needs a size with a unit; you gave "<value>".` | `Use a number with B, KiB, or MiB, like 512KiB or 2MiB.` |
| Sub-byte size | `"<key>" can't be "<value>": a size must be a whole number of bytes.` | `Use a whole number of bytes, like 512B or 2KiB.` |
| Boolean | `"<key>" must be true or false; you gave "<value>".` | `Use true or false.` |
| Integer | `"<key>" needs a whole number; you gave "<value>".` | `Write it in plain digits, like 200, with no sign, leading zero, "_", or exponent.` |
| Helper id | `"<value>" isn't a helper id.` | `Use a lowercase name that starts with a letter, up to 40 letters, digits, "_" or "-", like "keychain".` |
| Bad prefix | `"<item>" isn't a bucket prefix.` | `Use 1 to 32 letters, digits, "-", "_" or ".", then one ":", like "project:".` |
| Duplicate prefix | `"<item>" is in the list twice.` | `List each prefix once.` |
| Folded duplicate | `"<item>" and "<other>" are the same prefix.` | `List each prefix once.` |

`<item>` is the prefix as typed, before folding. `<other>` is the other typed form. Detail for a prefix error adds `item`, and `other` when folding made the two match. There is no count limit. The 16 KiB cap is C-09.

`settings set` exits 1. A sync apply uses C-14. The v5 to v6 migration warns `remember: warning: dropped "<item>" from "remember.buckets.prefixes": it isn't a bucket prefix.` Code `setting_invalid`, reason `bad_prefix_shape`.

Import of a bad prefix follows 007 ST-8.4, not a failed file. See DESIGN-PENDING for the code on that skip.

Plan 20. The migration line is plan 19.

## C-06

From a setting: `remember: "<key>" can be <min> to <max>; you gave <value>. Set a value from <min> to <max>.`

From a flag: message `<flag> ("<key>") can be <min> to <max>; you gave <value>.` Fix: `Use a value from <min> to <max>.` Example: `remember: --limit ("remember.search.max_results") can be 1 to 1000; you gave 5000. Use a value from 1 to 1000.` `<flag>` is `--candidate-pool` or `--limit`. `--limit` on `settings list` and `settings history` uses this flag form, range 1–1000. There is no ninth-prefix case. Plan 20.

## C-07

Message: `"<key>" is a brain setting; it can't be set for your user.` Fix: `Run the command again with --scope brain, or without --scope.` Plan 20.

## C-08

An unknown key or a bad value does not fail the file. Those rows skip and the import continues (007 ST-8.4, C-12, C-14). Do not print the old "nothing was changed" line for that case.

`setting_import_invalid` is not given a new sentence here. What remains of that code is DESIGN-PENDING D-16. Plan 20 does not invent one.

## C-09

257th user.db row. Message: `you have 256 user settings, the most remember can hold.` Fix: `Reset settings you don't need with 'remember settings reset --scope user'.`

Over 16 KiB. Message: `the value for "<key>" is over 16 KiB, the most a setting can hold.` Fix: `Use a shorter value.`

The brain sentence stays `this brain has 256 settings, the most it can hold. Reset settings you don't need with 'remember settings reset'.` Plan 20.

## C-10

Plain: `"<key>" was reset. It's now <value>.` `<value>` is the effective value. With `--scope`, an empty layer shows `(unset)`.

JSON `setting.reset.result` `data` is `{setting, scope, old_value, changed, action}`. `action` is `tombstone` (brain), `delete` (user), or `none` (no row, so `changed` is false). `setting.layers.<scope>` is null. Plan 20.

## C-11

Code `confirmation_required`, exit 1, nothing written. Plain: `remember: this resets <count> settings. Run '<command> --yes' to confirm.` Message: `this resets <count> settings.` Fix: `Run '<command> --yes' to confirm.` `<count>` is the number of keys. `<command>` is the command as typed.

With `--yes`, JSON is `setting.reset.result` and `data` is `{count, settings[]}`. Each item is `{setting, scope, old_value, changed, action}`. Plan 20.

## C-12

Warning `setting_import_skipped`. Plain shape: `remember: warning: skipped "<key>": <reason>.` No fix.

- Plugin not installed: `remember: warning: skipped "<key>": plugin <name> isn't installed.`
- Other scope: `remember: warning: skipped "<key>": it's a brain setting, and you ran with --scope user.` and `remember: warning: skipped "<key>": it's a user setting, and you ran with --scope brain.`
- Layer not allowed: `remember: warning: skipped "<key>": it can't be stored for your user.` and `remember: warning: skipped "<key>": it can't be stored for one brain.`
- Unknown key uses the same shape, `remember: warning: skipped "<key>": <reason>.` The words of `<reason>` for `unknown_key` are not in the approved answers. Do not invent them. The copy editor writes that phrase. The other unwritten sentence is the hook-depth `--as`/`--actor` rejection in plans 20 and 21. Its code stays `invalid_argument`.

Detail `reason` for the lines above is `plugin_not_installed`, `other_scope`, `layer_not_allowed`, or `unknown_key`. The import row has `result` `skipped`, the same `reason`, and `action` `none`.

A bad value on import is 007 ST-8.4: warning `setting_invalid`, reason `invalid_value`, and the C-14 line (D-15, answered 2026-10-25). Print C-12 for the four reasons above. Plan 20.

## C-13

Human dates: `21 Oct at 12:04` (local, 24-hour, English). Add the year when it is not this year: `21 Oct 2025 at 12:04`. With no readable zone: `21 Oct at 10:04 UTC`. JSON stays RFC 3339 UTC.

`get` stays:

```text
remember.search.candidate_pool = 400
  from brain, set by human on 21 Oct at 12:04
  user: (unset) · default: 200
```

`set` success: `"<key>" is now <value> for this brain.` or `"<key>" is now <value> for your user.` with `--scope user`. No-op: `"<key>" is already <value>. Nothing changed.` JSON `setting.set.result`: `{setting, scope, old_value, changed}`.

`list`, page 100, max 1000. One line: `<key> = <value>   (<from>)`. `<from>` is `default`, `user`, `brain`, or `flag`. When more pages follow, stderr (not a warning code): `remember: more settings. Run 'remember settings list --cursor <value>' to see them.` JSON: `{count, settings[]}` plus `next_cursor` and `has_more`.

`history`, page 50, max 1000, newest first. Columns: when, scope, key, old → new, actor. The actor is `by <kind>`, plus `"<id>"` when the id is not null. Example: `21 Oct at 12:04  brain  remember.search.candidate_pool  200 → 400  by human`. The more-pages line names `history`. JSON: `{count, changes[]}`. Each change is `{key, scope, old_value, new_value, updated_by, changed_at, action}`. ANSWERS-FINAL says `action` is `tombstone` or `delete` for a reset, and `none` for a set. 007 ST-7.11b says a set is `action` `set`. Plan 20 follows the log. That difference is DESIGN-PENDING D-18. Do not change the plan until the log changes.

`explain`:

```text
remember.search.candidate_pool  (integer, 20–2000, default 200)
  How many keyword matches search looks at before filters rank them.
  Can be set for: user, brain
  Now: 400, from brain, set by human on 21 Oct at 12:04
  Related: remember.search.max_results
```

The description line is the registry's own text. `effect`, `since`, and `status` show only when they are present.

Import success: `Imported "<path>": <count> settings written, <count> unchanged, <count> skipped.` JSON `setting.import.result`: `{count, written, unchanged, skipped, rows[]}`. Each row is `{key, scope, result, reason?, action}`. The log also allows `result` `kept_local`, counted as `unchanged`, and an import row keeps both `result` and `action`. Plan 20 follows the log. Do not drop `kept_local`. Skipped lines print first (C-12). Plan 20.

## C-14

Sync apply, Cloud, migration, and import. Warning, no fix: `remember: warning: skipped "<key>": "<value>" isn't a valid value.` Add ` This brain wasn't changed.` only when the batch has one row and the command writes no other row, memory or link (D-15, 007 ST-7.15). Detail: `key`, `value`, `reason` (for example `bad_prefix_shape`). `settings set` of a bad value is C-05, not this code. Plan 20. Plan 19 prints the migration prefix line in C-05, which is this code with reason `bad_prefix_shape`.

## C-14a

`setting_invalid`, reason `bad_shape` (007 ST-7.15, ST-8.4). Warning, no fix: `remember: warning: skipped row <row> in the settings file because it isn't a setting (each row needs a "key").` `<row>` is the 1-based position in the `settings` list. With no row number: `remember: warning: skipped a row in the settings file because it isn't a setting (each row needs a "key").` Plan 20.

## C-15

`plugin_approval_required`. Message: `"<name>" has to be allowed first, and there's no terminal to ask you in. Nothing was installed.` Fix: `Run '<command>' in your own terminal.` Plan 25.

## C-16

`plugin_approval_denied`. Message: `"<name>" wasn't allowed, so nothing was installed.` Fix: `To try again, run '<command>' and type "<name>" to allow it.` Plan 25.

## C-17

`plugin_untrusted`. `remember: warning: "<name>" changed since you allowed it, so it didn't run. Run 'remember plugin trust <name>' to review the changes and allow it again.` Plan 25.

## C-18

`plugin_exists`. Message: `"<name>" is built in, so you can't add a plugin with that name.` Fix: `Give your plugin another name in its plugin.json.` Plan 25.

## C-19

`plugin_missing`. `remember: warning: plugin "<name>" is turned on for this brain but isn't installed. The brain's setting "plugin.<name>.enabled" is still there.` Plan 25.

## C-20

Missing file: `remember: warning: hook "<name>" did not start: <path> doesn't exist. Run 'remember plugin show <name>'.`

Not executable: `remember: warning: hook "<name>" did not start: <path> isn't executable. Run 'remember plugin show <name>'.`

A hash mismatch reuses C-17. It does not use `plugin_preflight_failed`. The recipient state is `failed`. A folder plugin says `plugin` instead of `hook`. Plan 25.

## C-21

`plugin_dispatch_failed`. `remember: warning: the write was saved, but its hooks and plugins didn't run because the dispatcher couldn't start.` Plan 25.

## C-22

`plugin_not_rerunnable`. Message: `"<name>" has nothing to rerun for event <event-id>.` Fix: `Run 'remember plugin log <name>' to see its runs.` Plan 25.

## C-23

`hook add` and folder `plugin add`, error. Message: `this build of remember can't start programs, so it can't run "<command>".` Fix: `Use a build of remember that can start programs.`

Dispatch, warning: `remember: warning: the write was saved, but this build of remember can't start programs, so its hooks and plugins didn't run.` Plan 25.

## C-24

`plugin_backfill_incomplete`, error. Message: `"<name>" stopped before it finished indexing. Your memories are fine.` Fix: `Run '<command>' again to continue where it stopped.` Plan 25.

## C-25

`plugin_dev_mode`. `remember: warning: results can include "<name>", a plugin you're testing with --dev that isn't released.` Plan 25.

## C-26

`plugin_disabled_crash_loop`. `remember: warning: "<name>" crashed 3 times in 10 minutes, so it is paused in brain "<path>". Run 'remember plugin enable <name>' to start it again.` Plan 25.

## C-27

`plugin_filter_timeout`. `remember: warning: <name> took too long, so it was skipped. These results keep the order they had before it.` `<name>` is `<plugin>/<entry>`. Plan 25.

## C-28

`plugin_filter_failed`. `remember: warning: <name> failed, so it was skipped. These results keep the order they had before it.` `<name>` is `<plugin>/<entry>`. Status `failed`. Plan 25.

## C-29

`plugin_filter_cold_start` for a filter other than hybrid/rerank. `remember: warning: <name> is still starting, so these results are in keyword order. Search again in a moment.` Plan 25. The hybrid/rerank line is C-01.

## C-30

`plugin_action_timeout`. `remember: warning: <name> didn't answer in time, so it was skipped.` `<name>` is `<plugin>/<action>`. Print it only in `remember plugin test <name>`, `remember plugin rerun <name> <event-id>`, and the backfill in `remember plugin reindex` and `remember plugin enable` (D-17, 010 H-6.4). A write never prints it, and its exit code does not change (R8.16). Plan 25.

## C-31

`plugin_action_failed`. `remember: warning: "<name>" failed. Run 'remember plugin log <name>' to see why.` `<name>` is the plugin. Printed only where C-30 is printed (D-17). Plan 25.

## C-32

`plugin_additions_capped`. `remember: warning: <name> added more results than allowed, so <count> were dropped.` `<count>` is `detail.dropped`. Plan 25.

## C-33

`plugin_note_truncated`. `remember: warning: the note from <name> was over 120 characters, so it was shortened.` Plan 25.

## C-34

`plugin_request_trimmed`. `remember: warning: the search request was over 16 MiB, so <count> results were left out of it. The search still ran.` `<count>` is `detail.dropped`. Plan 25.

## C-35

File: `remember: warning: "<path>" has a field remember doesn't know, "<name>". The rest of the file was read.`

Stream: `remember: warning: stdin has a field remember doesn't know, "<name>". The rest of the input was read.`

Plans 21, 23, and 25. A plugin's own `unknown_field` stays in the plugin log and `plugin status`. `plugin.json` members `run`, `permissions`, and `filter` are `plugin_manifest_invalid`, not this warning.

## C-36

Hyphen. Message: `"<name>" in plugin.json can't contain "-".` Fix: `Use "<value>".` Example: `remember: "url-expand" in plugin.json can't contain "-". Use "url_expand".`

Reserved key. Message: `"<name>" declares a setting named "enabled", and that name is reserved.` Fix: `Rename that setting in plugin.json.` Plan 25.

## C-37

Kept, one brain: `Removed "<name>". Its data in 1 brain was kept. To delete that data too, run 'remember plugin remove <name> --delete-data'.` Use `<count> brains` when the count is not 1. No data: `Removed "<name>".`

Deleted: `Removed "<name>" and deleted its data in <count> brains. Your memories are not affected. This can't be undone.` Use `1 brain` when the count is 1. Plan 25.

## C-38

`brain_id_cloned`. `remember: warning: brain "<path>" looks like a copy of another brain, so it got its own new id, <id>. The original keeps the old id. Both now sync and run hooks as separate brains.` Plan 25.

## C-39

remember-mac, not a plan in this repo. Toast: `Saved. Hook “<name>” failed (exit <code>).` Action label: **Show Log** on Mac, **Show log** on iOS and Android. The Plugins row keeps `Last run failed (exit <code>)`. This does not change the CLI `add` success line (D-05). No file in this repo changes for C-39.

## C-40

Skill sentence:

> Tags that start with a bucket prefix group memories into buckets, for example `project:fortkeeps`. Bucket prefixes are the brain setting `remember.buckets.prefixes`, which is `project:` and `status:` by default; run `remember settings get remember.buckets.prefixes` to see the list in use.

Plan 26.

## C-41

README › What's New:

- `--json` now puts success and failure in one `remember-message` version 1 envelope on stdout, and stderr stays empty. `--json-errors` is gone.
- Lists page with a cursor. Search doesn't page. `--offset` is gone.
- A memory's id in output is `id`, a lowercase hyphenated UUID. `--sync-id` is gone. The integer `local_id` appears only in CLI output.
- Actors replace `source`. `--source` is gone completely.
- `--trash` now means the Trash: memories you deleted. For expired memories, use `--expired`. For this release, `--trash` prints: `remember: warning: --trash now means the Trash (deleted memories). For expired memories, use --expired.`
- `purge-trash` is gone. To delete memories in the Trash for good, run `remember purge <locator>` for each one.

SKILL.md:

- Use `--json` to get one `remember-message` version 1 envelope on stdout, for success and failure alike. There is no `--json-errors`.
- To page a list, pass back its `next_cursor`. Search returns its top results and doesn't page. There is no `--offset`.
- Refer to a memory by `id`, a lowercase hyphenated UUID. The number `local_id` is for this device's CLI only. There is no `--sync-id`.
- Writes record actors, not `source`. There is no `--source`.
- `--trash` is the Trash (deleted memories). `--expired` is expired memories.
- To empty the Trash, run `remember purge <locator>` once for each memory in it. There is no `purge-trash`.
- Settings commands: see C-13. For scripts and agents, use `--json`, because human dates are local.

Plan 26, stage 4, after plans 19 through 25 have merged. Each feature plan still writes its own feature line. This page is only the flag and identity changes above.

## plugin_chain_capped

`remember: warning: <name> didn't run because the search reached its time limit, "remember.plugins.chain_budget". These results keep the order they had before it.` Status stays `skipped` with reason `chain_capped`. A clipped budget still gets no warning. Plan 25.

## Still open (plan 22)

**C-44.** Plan 22 export drafts (Itay grill, 2026-11-09). The CLI prints these drafts. They are not approved copy. Copy approves or rewrites them before INDEX row 22 is ticked. C-42 and C-43 are plan 20's rows.

- Human line at a count of 1 (`decision:plan22-export-human-count-agrees`, amends D19): `exported 1 memory, 1 link to <path>`. 0 and 2 or more keep D19: `exported 0 memories, 2 links to <path>`.
- `read_only` on an export file (`decision:plan22-export-file-error-copy`). Message `cannot write "<path>".` Fix `Give yourself write permission on that folder, or export to another folder.`
- `cannot_open`, reason `missing`, on an export file's folder (same decision). Message `the folder for "<path>" doesn't exist.` Fix `Create the folder, or export to another path.`
- `cannot_open`, reason `is_directory`, when `--out` (or a sibling file) is a folder (`decision:plan22-export-out-is-folder`). Message `"<path>" is a folder.` Fix `Pick a file path for --out.`
- `disk_full` and `io_error` on an export file keep the plan 16 text with a closing period: `disk is full.` / `Free some space and try again.` and `the disk reported a read or write error.` / `Check the disk or drive, then try again.`
- Command summary in `remember --help`: `export      Write the brain to a JSON, Markdown, or CSV file`.
- Markdown `README.txt` second line (`decision:plan22-export-bins-header`): `Bins: live 12, expired 0, deleted 3.` It lists only the exported bins.
- `export takes no positional arguments` follows the `import` and `purge` pattern.
- `remember help export` block (drafted 2026-11-10, after the repair block): the usage line; options `--format FMT  json, md, or csv`, `--out PATH  The file to write (a folder for md)`, `--include-expired  Also export expired memories`, `--include-trash  Also export memories in the Trash`, `--exclude-archived  Accepted; no effect until archiving exists`, `--force  Replace PATH when it exists`; then `Live memories by default. A link is exported when both ends are.`, `csv also writes <name>.manifest.json beside the file.`, `A missing brain fails with cannot_open. Export creates nothing.`, and the human/JSON reply lines.

## Still open

**C-42.** Refused `REMEMBER_SYNC_TOKEN` with no helper row (plan 20 stage 4 second opinion, 2026-10-30). With no `remember.sync.credential_helper` row, a variable that is too long for the token buffer, or holds a C0 control or DEL inside it, fails the lookup. The code is not chosen. The lookup returns 2 and prints nothing until this row is answered. The variable is set and cannot be used, and why (too long, or a control byte). The token is never echoed. ST-3.6 names a helper id, so it does not fit. Plan 20 sync caller, or the first stage that runs sync.

**C-43.** Plan 20 stage 6 drafts (Itay grill, 2026-11-08). The CLI prints these drafts. They are not approved copy. Copy approves or rewrites them before INDEX row 20 is ticked.

- `setting_scope_mismatch`, `--scope brain` on a user-only key (`decision:plan20-user-only-scope-draft`). Message `"<key>" is a user setting; it can't be set for one brain.` Fix `Run the command again with --scope user, or without --scope.` It replaces the SETTINGS-SCHEME §5.1 line for `remember.plugins.idle_timeout`, and it covers every user-only key.
- `setting_invalid_value`, two or more values for a key that is not a list (`decision:plan20-scalar-shape-draft`). Message `"<key>" takes one value; you gave <n>.` Fix `Give one value.`
- `setting_invalid_value`, `--clear` on a key that is not a list (same decision). Message `"<key>" isn't a list, so --clear can't empty it.` Fix `Set a value, or run 'remember settings reset <key>'.`
- `confirmation_required` for one key (`decision:plan20-reset-count-zero-one`). Message `this resets 1 setting.` C-11 keeps `this resets <count> settings.` for two or more. A bulk reset that matches no row asks for no confirmation.
- Catalog fallbacks for the six stage 6 codes (`decision:plan21-error-fix-drafts-c42`). The settings commands always pass their own fix, so these print only when a caller passes none. Approved text reused: the C-05 integer fix, the C-07 fix, the C-09 brain fix, and the `setting_from_hook` fix. Drafts: `setting_out_of_range` `Set a value from the minimum to the maximum.` and `confirmation_required` `Run the command again with --yes to confirm.`
- Error `detail` members, all strings (`decision:plan20-error-detail-strings`). `setting_from_hook` `{key, hook}`: `key` is the typed key and is absent when none was typed; `hook` is the `REMEMBER_HOOK_DEPTH` text. `setting_scope_mismatch` `{key, allowed}`: `allowed` is `brain` or `user`. `setting_out_of_range` `{key, value, min, max}`: the typed value and the canonical bounds. A flag form adds `flag` (stage 9).

Plan 20 stage 8 drafts (`settings import`, Itay grill, 2026-11-08). Same status: printed, not approved.

- `setting_import_skipped`, reason `unknown_key` (`decision:plan20-import-unknown-key-draft`). The C-12 shape with the reason words `remember has no setting by that name`: `remember: warning: skipped "<key>": remember has no setting by that name.` A `plugin.*` key is this reason until plan 25.
- `setting_invalid`, reason `bad_field` (`decision:plan20-import-bad-field`). `remember: warning: skipped "<key>": its "<field>" isn't valid.` `<field>` is the first bad member in ST-8.2 order: `scope`, `updated_by`, `updated_at`, `device_id`, `version_vector`, `deleted_at`. Detail `{key, field, reason}`.
- `import_parse`, the file is not a settings document (`decision:plan20-import-parse-draft`). Message `"<path>" isn't a remember settings file.` A file that cannot be read: `can't read "<path>": <system error>.` Both keep the catalog fix `Check the import source, then try again.`
- `import_parse`, a later version (`decision:plan20-import-version-draft`). Message `"<path>" is settings file version <n>; this remember reads version 1.` Fix `Update remember, then try again.`
- `--dry-run` plain line (`decision:plan20-import-dry-run-draft`). `Would import "<path>": <n> settings written, <n> unchanged, <n> skipped. Nothing was changed.`
- Warning `detail` members. `setting_import_skipped` `{key, scope, reason}`: `scope` is null when the row's scope is not `brain` or `user`. `setting_invalid` `{key, value, reason}` for `invalid_value` and `bad_shape`: `value` is the row's value as text, a string without its quotes; `key` and `value` are null when the row has none (ST-8.4).

- `invalid_argument`, a file with user and brain rows and no `--scope` (`decision:plan20-import-mixed-scope-draft`, whole-plan review grill 2026-11-09). Message `"<path>" has user and brain settings; remember imports one scope at a time.` Fix `Run the command again with --scope user, then with --scope brain.`
- `import_parse`, a file over 16 MiB (`decision:plan20-import-size-cap`). No new words: the `can't read "<path>": <system error>.` draft with the system's `File too large`.

Plan 20 stage 9 drafts (`settings list`, `history`, `explain`, Itay grill, 2026-11-08). Same status: printed, not approved.

- Key descriptions for `settings explain` (`decision:plan20-explain-description-drafts`). `remember.buckets.prefixes` `The tag prefixes that group memories into buckets.` `remember.sync.credential_helper` `The program that gives sync its access token.` `remember.search.candidate_pool` `How many keyword matches search looks at before filters rank them.` `remember.search.max_results` `How many results search shows after filters rank them.` `remember.plugins.chain_budget` `How long all plugin filters together may run for one command.` `remember.plugins.idle_timeout` `How long a plugin may sit idle before remember stops it.`
- The explain block (`decision:plan20-explain-human-form`). Header `<key>  (<type>, <min>–<max>, default <default>)`, or `<key>  (<type>, default <default>)` for a key with no range. Then indented lines `<description>`, `Can be set for: <scopes>`, `Now: <value>, from <layer>[, set by <actor> on <date>]`, and `Related: <keys>` when the key has related keys. `--all` puts a blank line between blocks. The JSON entry adds `description`, `scopes`, `default`, `min`, `max` (with `_ms` twins for durations) and `related` (`decision:plan20-explain-json-shape`).
- `setting_out_of_range` for `--limit` on `list` and `history` (`decision:plan20-list-limit-draft`). Message `--limit can be 1 to 1000; you gave <n>.` Fix `Use a value from 1 to 1000.` Detail `{flag, value, min, max}`. A `--limit` that is not an integer stays the usage line `invalid --limit (must be 1..1000)`.
- `invalid_argument` for a bad or reused `--cursor` (`decision:plan20-cursor-fix-draft`). Message `invalid --cursor.` Fix `Run 'remember settings list' again without --cursor.`, or `history` for the history verb. A cursor keeps its filters; another `--scope`, `--changed`, prefix or `--namespace` with it is this error (`decision:plan20-cursor-carries-filters`).
- The more-pages line on stderr (`decision:plan20-history-plain-draft`). `remember: more settings. Run 'remember settings list --cursor <c>' to see them.` and `remember: more changes. Run 'remember settings history --cursor <c>' to see them.`
- History lines (same decision). `<when>  <scope>  <key>  <old> → <new>  by <kind>[ "<id>"]`. A missing side prints `(unset)`. A row whose actor is not valid JSON prints no `by` part.
- An empty list value prints `(empty)` in `list`, `get` and `explain` (`decision:plan20-empty-list-related`).
