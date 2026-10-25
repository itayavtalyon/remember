# Copy pending — sentences the design logs do not lock

Hand this file to the copy editor. It is not a plan. An implementer does not invent a sentence from it.

A row is missing when a command must print words and no Design section states those words. A proposed sentence in this file would be a decision. This file does not propose one.

Quoted lines below are already locked. They are voice examples. Do not change them.

When a row is approved, paste the approved sentence into the plan named in the row, clear that row, and change the plan status only when every row that blocks that plan is gone. Until then the plan stays pending and the stage named in the row does not start.

## How to cut a plain line into JSON

`error` requires `code`, `message`, `fix`, and `retryable`. `fix` is a non-empty string (log 008 JE-2.6). A warning item has `code` and `message` and no `fix` (JE-2.7).

SETTINGS-SCHEME §5.2 shows one split, for `setting_out_of_range`:

- message: `plugin.hybrid.rerank.budget can be 100ms to 30s; you gave 45s.`
- fix: `Set a value from 100ms to 30s.`

That key no longer exists. The split shape is the only approved cut, and it is approved only for that example.

Log 007 ST-3.6 already splits one code:

- message: `the sync credential helper "<id>" did not return a token.`
- fix: `Check the helper, or set remember.sync.credential_helper to env and export REMEMBER_SYNC_TOKEN.`

Every other locked line in the logs is one sentence. The logs do not say which words are `message` and which words are `fix`. The existing `src/diag.c` pattern (problem in `message`, next action in `fix`) is code, not an approved cut.

**C-01. One-sentence errors.** For each locked one-sentence error below, write `message` and `fix`. Do not leave `fix` empty. Blocks plans 19, 20, 21, 23, and 25 wherever that code is printed with `--json`.

Locked one-sentence errors to split:

| Code | Locked plain line | Where it fires |
|---|---|---|
| `schema_too_new` | `this brain was written by a newer remember. You can read it. Upgrade to write to it.` | A write to a brain whose `min_reader_version` is newer than the engine (007 ST-10.2). Exit 1. |
| `setting_from_hook` | `a hook or plugin run can't change settings. Change them in your own terminal.` | `settings set`, `settings reset`, or `settings import` when `REMEMBER_HOOK_DEPTH` is 1 or more. Fails before key lookup (007 ST-7.10). Exit 1. `detail` has `key` and `hook`. |
| `plugin_run_forbidden` | `a hook or plugin run can't run "<command>". Run it in your own terminal.` | Any command that is not on the H-5.5 allow list, when `REMEMBER_HOOK_DEPTH` is 1 or more (010 H-5.5). Exit 1. |
| positional locator | `"abc" isn't a memory id. Use a UUID, a number from 'remember list', or --key.` | A positional locator that is neither a UUID nor digits (008 JE-3.4). Exit is `invalid_argument`. |
| purge of a live row | `"<key>" is live. Delete it first, then purge it.` | `remember purge` of a live row. The id is used when the row has no key (010 H-16.7). Exit 3 `wrong_bin`. |
| Trash wrong list | `remember: "<key>" is in the Trash. Add --trash to use it.` | Exit 3 when the row is in Deleted and the command did not pass `--trash` (010 H-16.9a). |
| expired wrong list | `remember: "<key>" has expired. Add --expired to use it.` | Exit 3 when the row is expired and the command did not pass `--expired` (010 H-16.9a). |

Locked warnings are one `message`. They need no `fix`. Do not rewrite them:

| Code | Locked plain line |
|---|---|
| `flag_meaning_changed` | `remember: warning: --trash now means the Trash (deleted memories). For expired memories, use --expired.` Detail `{"flag":"--trash","use":"--expired"}`. Once per command that uses `--trash`. Removed in the release after 0.3.0 (010 H-16.9a). |
| `plugin_loop_halted` | `remember: warning: "<name>" was stopped after 100 hook runs in a row set each other off. The write was saved. To run it once more, run 'remember plugin rerun <name> <event-id>' within 7 days. To clear this warning without rerunning, run 'remember plugin status --ack <name>'.` (010 H-5.6) |
| `plugin_filter_cold_start` for hybrid/rerank only | `remember: warning: hybrid/rerank is still starting, so results are in keyword order.` (010 H-12.8) |
| `brain_missing` | `brain "<path>" is in the list and is not on disk. Run 'remember brain prune' to remove it.` (010 H-2.3) |
| `setting_unknown` did-you-mean example | `remember: unknown setting "candidate_pool". Did you mean "remember.search.candidate_pool"? Run 'remember settings list' to see all settings.` (007 Examples). SETTINGS-SCHEME §5.1 uses the same shape with a different input: `unknown setting "remember.search.candidates". Did you mean "remember.search.candidate_pool"? Run 'remember settings list' to see all settings.` |
| budget or grace `setting_unknown` fix | `Budgets and cold-start grace are set by the plugin's manifest. Raise remember.plugins.chain_budget instead.` (010 H-12.4, M-24). This is the `fix`. The `message` is still the `setting_unknown` line. |

Help lines already locked (010 H-16.9a, H-16.11, H-16.10):

```
--trash     Use the Trash: memories you deleted. Restore them, or purge them for good.
--expired   Use expired memories: ones past their expiry time. They are not in the Trash.
--hooks     Run matching hooks for this write, also ones you turned off for local writes.
--no-hooks  Run no hooks for this write.
```

`--color` and `--quiet` are global flags beside `--db`, `--json`, `--strict`, `--busy-timeout`, and `--no-create`. The logs do not give a longer help sentence for `--color` or `--quiet`. **C-02.** Write those two help lines. Block: plan 25, the global-flag stage.

Trust dialog and the TTY prompt are locked (010 H-9.4). The error sentences for a missing callback and a false return are not.

- Body, folder plugin: `Allow "<name>" to read your memories? It can send them off this Mac.`
- Body, hook: `Allow hook "<name>" to run <path>?`
- TTY prompt: `Type "<name>" to approve:`

## Settings (plan 20)

SETTINGS-SCHEME §5.1 is the copy source log 007 ST-7.12 names, minus `setting_ambiguous`, `setting_read_only`, `time_budget_capped`, and `setting_overridden`. Several §5.1 examples name keys that 007 deleted (`plugin.hybrid.model`, `plugin.hybrid.rerank.budget`, `remember.hooks.<key>`, `remember.search.hybrid`). Do not print a deleted key. Write a live sentence.

| ID | Code | Where it fires | What the user must learn | Voice nearby | Blocks |
|---|---|---|---|---|---|
| C-03 | `setting_unknown` with no candidate | Exit 2. ST-1.5: when no key matches, the message has no did-you-mean. | The key is unknown, and there is no suggestion. | The did-you-mean line above. | Plan 20 set/get/reset/explain. |
| C-04 | `setting_unknown` plus the uninstalled-plugin hint | ST-5.10: a key of a plugin that is not installed is `setting_unknown`, with the hint `plugin <name> isn't installed`. | The plugin name is not installed. | Did-you-mean line. The hint words are locked. The sentence they sit in is not. | Plan 20, after plan 25 generates plugin keys. |
| C-05 | `setting_invalid_value` | Exit 1. A duration, size, boolean, integer, helper id, or bucket prefix that fails its grammar. `detail`: `key`, `value`, `type`, `allowed`. | The value is the wrong shape, and what shape is legal. | §5.1 examples, which name deleted keys: `plugin.hybrid.model can't be "huge". Use one of: small, base.` and `plugin.hybrid.rerank.budget needs a duration with a unit, like 800ms or 2s.` and `remember.buckets.prefixes: "Project:" must be lowercase and end in ":", like project:.` The prefix row also conflicts with 006 E12.4. See DESIGN-PENDING D-01 before writing the prefix sentence. | Plan 20. |
| C-06 | `setting_out_of_range` | Exit 1. A number or duration outside the registry range, including a flag (`--candidate-pool`, `--limit`). `detail`: `key`, `value`, `min`, `max`. | The allowed range, and the value that was given. | §5.1: `plugin.hybrid.rerank.budget can be 100ms to 30s; you gave 45s.` and `remember.buckets.prefixes can hold up to 8 prefixes; you gave 9.` Verification 7 says `remember.plugins.chain_budget` of `45s` uses this code. The live sentence is not written. The §5.2 fix `Set a value from 100ms to 30s.` matches the deleted key's range, not every key. | Plan 20. |
| C-07 | `setting_scope_mismatch` for a brain-only key | Exit 1. `set --scope user` on `remember.buckets.prefixes`. `detail` names the allowed layers (ST-2.4). | This key can be stored only on the brain. | §5.1 user-only sentence, which is locked for `remember.plugins.idle_timeout`: `remember.plugins.idle_timeout is a user setting; it can't be set for one brain. Run the command again with --scope user, or without --scope.` | Plan 20. |
| C-08 | `setting_import_invalid` | **Deleted 2026-10-25 (D-16, 007 ST-8.4):** no command emits this code. A file that is not a settings document is `import_parse`. **Superseded 2026-10-25 (007 ST-8.4):** an unknown key is the warning `setting_import_skipped` and a bad value is the warning `setting_invalid`, and import continues, so no row fails the import. Old text: Exit 1. One unknown key or one bad value. Nothing is written (ST-8.4). `detail.errors[]`. | How many values failed, which failure is first, and that nothing changed. | §5.1: `settings.json wasn't imported: 2 values failed (first: plugin.hybrid.rerank.budget "fast" needs a duration). Nothing was changed. Fix the file or run with --dry-run to see all problems.` The example key is deleted. | Plan 20 import stage. |
| C-09 | `quota_exceeded` for user.db | Exit 1. The 257th user.db settings row, or a value over 16 KiB. `detail.quota` is `settings_limit`. | The cap, and that reset frees a slot. | Brain sentence, locked: `this brain has 256 settings, the most it can hold. Reset settings you don't need with 'remember settings reset'.` The user.db sentence is not written. The 16 KiB sentence is not written. | Plan 20. |
| C-10 | reset success | `reset` of a row that exists. SS §6.1 example: `remember.search.candidate_pool reset for this brain. Now: 200 (from your user setting).` ST-7.12 does not cite §6.1. A brain reset writes a tombstone. A user reset deletes the row. The example does not say which. | Which layer was cleared, and what value is effective now. | The §6.1 example. Also `(unset)` is locked for a layer with no row (ST-7.4, G-34.7). | Plan 20. |
| C-11 | `reset --namespace` or `reset --all` without `--yes` | ST-7.8 requires `--yes`. The refusal sentence is not in Design. SS §6.1 has `this resets 6 settings for this brain. Run again with --yes to confirm, or see them with 'remember settings list --changed --scope brain'.` ST-7.12 does not adopt §6.1. | How many rows would change, and the exact command to confirm. | The §6.1 line. | Plan 20. |
| C-12 | settings import skipped-row line | ST-8.5: import names each skipped row and writes the rest. Three skip kinds: uninstalled plugin, other scope when `--scope` is set, layer the target does not allow. | Which key was skipped, and which of the three reasons. | None locked. | Plan 20 import stage. |
| C-13 | human lines for `settings list`, `settings history`, `settings explain`, and `settings set` success | JSON field names that Design names are in plan 20. The plain layout is specified only for `get` (007 Examples). | What a person reads on success. | `get` example: `remember.search.candidate_pool = 400` / `from brain, set by human on 21 Oct at 12:04` / `user: (unset) · default: 200`. The date shape `21 Oct at 12:04` appears only in that example. | Plan 20 command stage. |

**C-14a.** **Answered 2026-10-25 (Story Weaver, 007 ST-7.15):** `remember: warning: skipped row <row> in the settings file because it isn't a setting (each row needs a "key").` With no row number: `remember: warning: skipped a row in the settings file because it isn't a setting (each row needs a "key").` Old text: `setting_invalid` with reason `bad_shape` (007 ST-8.4): a settings row that is not an object or has no `key`. The ST-7.15 line needs `<key>` and `<value>`, which this row may not have. Write the plain line. Block: plan 20, the import stage, for that sentence only.

`setting_invalid` (ST-2.7, a synced value that is skipped and not written) has no plain line. **C-14.** **Answered 2026-10-25 (D-15, 007 ST-7.15):** `remember: warning: skipped "<key>": "<value>" isn't a valid value.` Remember adds ` This brain wasn't changed.` only when the batch has one row and the command writes nothing else. Old text: Write it. Say that the brain was not changed. Block: plan 20, the sync-apply stage. Cloud uses the same skip.

## Plugins and hooks (plan 25)

Dialog bodies and the TTY prompt are locked. See the trust block above. These rows are the other words.

| ID | Code | Where it fires | What the user must learn | Blocks |
|---|---|---|---|---|
| C-15 | `plugin_approval_required` | Exit 1. No confirm callback, or the CLI has no terminal (H-9.4). Nothing is recorded. | Approval is required, and nothing was installed. | Plan 25 trust stage. |
| C-16 | `plugin_approval_denied` | Exit 1. The callback returned false, or the typed text was not the name (H-9.4). Nothing is recorded. | The install did not happen. | Plan 25 trust stage. |
| C-17 | `plugin_untrusted` | Warning. Hash mismatch before a run (H-9.5). `disabled_reason` becomes `untrusted`. The plugin does not run. | Which plugin, and that `remember plugin trust NAME` is the way to approve the new files. | Plan 25. |
| C-18 | `plugin_exists` | Exit 1. `plugin add` of a built-in name (H-10.2). | That name is built in. `hybrid` is the built-in name this release reserves. | Plan 25 install stage. |
| C-19 | `plugin_missing` | Warning. A brain has `plugin.<name>.enabled` true and the plugin is not installed (H-10.6). Status lists it as `missing`. | The name, and that the brain row is still there. | Plan 25. |
| C-20 | `plugin_preflight_failed` | Warning. Exit 0, or 4 with `--strict` (H-6.2). The write is saved. The recipient state is `failed`. | Which plugin, and why the check failed (missing file, not executable, or hash mismatch). | Plan 25. The 010 Examples transcript shows one line for a missing script: `remember: warning: hook "wake_coder" did not start: /Users/itay/bin/fk-wake doesn't exist. Run 'remember plugin show wake_coder'.` Design does not say that this line is the template for the other two failures. |
| C-21 | `plugin_dispatch_failed` | Warning. The dispatcher cannot start (H-6.3). Exit 0, or 4 with `--strict`. | The write was saved, and the runs did not start. | Plan 25. |
| C-22 | `plugin_not_rerunnable` | Exit 1. `plugin rerun` when that event has no `failed` or `halted` recipient for that plugin (H-5.7). | There is nothing to rerun. | Plan 25. |
| C-23 | `plugin_unsupported` | `hook add`, folder `plugin add`, and dispatch when `REMEMBER_PROCESSES=0` (H-0.7). | This build cannot start a program. | Plan 25. |
| C-24 | `plugin_backfill_incomplete` | Error, exit 1, not a warning (H-13.6, S-09). `plugin reindex` or `plugin enable` backfill that does not finish. A second run resumes. | The backfill stopped, and the command to resume. | Plan 25. Needs `message` and `fix` (C-01). |
| C-25 | `plugin_dev_mode` | Warning on searches while `plugin add --dev` is in use (H-9.7, PP R2.19). | Results can include an unreleased plugin. | Plan 25. |
| C-26 | `plugin_disabled_crash_loop` | Warning. 3 crashes in 10 minutes (H-11.8). `enabled` does not change. | Which brain, which plugin, and that `plugin enable` clears it. | Plan 25. |
| C-27 | `plugin_filter_timeout` | Warning. A filter sleeps past its budget (010 verification 21). Status `timeout`. Input passes on. Exit 0, or 4 with `--strict`. | Which filter was skipped, and that the order is unchanged from its input. | Plan 25. |
| C-28 | `plugin_filter_failed` | Warning. A filter returns a failure that is not a timeout (H-12.5, spec §8.2). Status `failed`. Input passes on. | Which filter failed, and that its input was kept. | Plan 25. Confirm the code is `plugin_filter_failed` in DESIGN-PENDING D-08 before writing the line. |
| C-29 | `plugin_filter_cold_start` for a filter other than hybrid/rerank | The locked line names `hybrid/rerank` only (H-12.8). Any other cold filter uses the same code (H-12.4). | The filter name, and that results are in keyword order. | Plan 25. |
| C-30 | `plugin_action_timeout` | Spec §8.2 lists the code. 010 H-4.10 states the SIGTERM / SIGKILL behavior and does not give the words. | Which action timed out. | Plan 25. **Printed only by `remember plugin test <name>`, `remember plugin rerun <name> <event-id>`, and the backfill in `plugin reindex` and `plugin enable` (D-17, 010 H-6.4).** Old text: only if the command prints this code. If Design never tells the CLI to print it, say so in the answer and the plan will not print it. |
| C-31 | `plugin_action_failed` | Spec §8.2 lists the code. H-6.4 says a failed run shows only in `plugin log` and the app toast. | Whether the CLI prints this at all. **Answered (D-17): only the C-30 commands print it.** | Same as C-30. |
| C-32 | `plugin_additions_capped` | Spec §8.2. The additions cap is a protocol rule (PP OQ-19). The plain line is not in 010. | How many additions were dropped. | Plan 25 search stage, if the CLI prints it. |
| C-33 | `plugin_note_truncated` | Spec §8.2. No 010 sentence. | The note was shortened. | Plan 25, if the CLI prints it. |
| C-34 | `plugin_request_trimmed` | Spec §8.2. A search request over 16 MiB is trimmed, exit 0 (NQ-12). No plain line. | The request was trimmed, and the search still ran. | Plan 25. |
| C-35 | `unknown_field` | Warning, once per file or stream, everywhere except `plugin.json` `run`, `permissions`, and `filter` (008 JE-4.1). Those three are `plugin_manifest_invalid`. | The field name, and that the rest of the file was still read. | Plans 21, 23, and 25. |
| C-36 | `plugin_manifest_invalid` fix that suggests `_` | A hyphen in a manifest name or entry name (007 ST-1.3, M-17). The code is locked. The fix must suggest the `_` form. The sentence is not written. Example pairs: `url-expand` → `url_expand`, `log-ids` → `log_ids`. | The illegal name, and the legal name. | Plan 25 manifest stage. Also the manifest setting key `enabled` (ST-5.9) needs its own sentence. |
| C-37 | `plugin remove` kept-data line | H-10.8: remove deletes the install and the trust record, keeps every `<brain>.d/plugins/<name>/`, and says so. `--delete-data` also deletes those folders. There is no prompt (H-Q22). | Data was kept, or data was deleted, and there was no question. | Plan 25 remove stage. |
| C-38 | `brain_id_cloned` | Warning. JE-3.10: a copied brain gets a new `brain_id` on open. The code is locked. The sentence is not. | This file is a copy, and it now has a new id. The old id is the one still on the other path. | Plan 25 brain-list stage. |
| C-39 | app toast | H-18.5: `Saved. Hook “router” failed (exit 3).` with **Show Log**. This is remember-mac, not a C plan. Listed so the editor can write it with the rest. | The write was saved, which hook failed, and where the log is. | remember-mac, not plan 25. |

`plugin_chain_capped` is not in this list until DESIGN-PENDING D-02 chooses whether a warning exists. Do not write a sentence for it before that.

## Release (plan 26)

**C-40.** Replace 006 E12.10. That sentence tells the skill to run `remember config get bucket-prefixes`. That command does not exist after plan 20. The skill must tell a reader that bucket prefixes are the brain setting `remember.buckets.prefixes`, default `project:` and `status:`, and that `remember settings get remember.buckets.prefixes` shows the effective list. Write the skill sentence. Block: plan 26 skill stage.

**C-41.** `SKILL.md` and README What's New for 0.3.0, after the feature plans have their lines. Each feature plan writes its own What's New line (006 EN-04). This row is the skill page that replaces `--json-errors`, `--offset`, `--sync-id`, `--source`, `purge-trash`, and the `--trash` alias. Write it only from behavior the plans mark locked. Block: plan 26 skill stage.

## Not missing

Do not send these back as new copy. They are locked in Design:

- Credential helper message and fix (ST-3.6).
- `schema_too_new` plain line (ST-10.2). The JSON cut is C-01. The words are not open.
- `setting_from_hook` plain line (ST-7.10).
- `plugin_run_forbidden` plain line (H-5.5).
- Positional locator plain line (JE-3.4).
- `--trash` warning, both wrong-list lines, both help lines (H-16.9a).
- Purge-live line (H-16.7).
- Loop-halted warning (H-5.6).
- Hybrid/rerank cold-start line (H-12.8).
- `brain_missing` (H-2.3).
- M-24 budget/grace fix (H-12.4).
- Did-you-mean shape in the 007 Examples block.
- `(unset)` for an empty layer (ST-7.4).
- Human `get` example in 007 Examples.
- Trust bodies and `Type "<name>" to approve:` (H-9.4).
- Brain quota sentence in SETTINGS-SCHEME §5.1.
- User-only scope sentence for `remember.plugins.idle_timeout` in §5.1.
