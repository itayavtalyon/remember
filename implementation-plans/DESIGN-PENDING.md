# Design pending — questions the logs do not answer

Hand this file back when a design log is written or when a row is answered in Design. It is not a plan. An implementer does not pick an answer from it.

A row is here when two Design sections disagree, or when a behavior is unnamed. Copy gaps live in [`COPY-PENDING.md`](COPY-PENDING.md). A missing sentence is not a reason to open a new product rule.

Each answer must land in a Design section. A chat reply is not enough for an implementer who has only the repo.

## Already closed for this extract

These are recorded so a later session does not grill them again.

- Itay will finish plan 18 and merge it. Plans 18b and 18c stay as they are on that branch. This extract does not tick plan 18 and does not edit 18, 18b, or 18c.
- Missing sentences go to the copy editor. Plans that still need one of those sentences are pending. See COPY-PENDING.
- The daemon log is not part of this extract. Plans 19–26 implement the CLI and the host path only. When the daemon is not running, the CLI and the host run each job as the logs already write it. The hourly sweep stays the fallback (010 H-18.1).
- Add-wins for bucket prefixes is deleted (007 ST-5.4, decision S-03). Plan 19 in this folder is the schema migration, not the old prefix command.
- Do not re-extract plans 16, 17, 17a, or 18. Log 008's draft step 5 says to re-extract them. That draft is not a plan. Plan 21 updates tests that still cite `--json-errors`. It does not rewrite those plan files.

## Open questions

### D-01. Bucket-prefix error words and code

**Answered 2026-10-25:** 007 ST-5.4. There is no item limit. A bad shape or a duplicate is `setting_invalid_value` with fixed sentences. A sync apply or import skips the row with `setting_invalid`.

007 ST-5.4 says a bad prefix fails with `setting_invalid_value` and the 006 E12.4 message. The E12.4 messages are `invalid bucket prefix: <p>`, `duplicate bucket prefix: <p>`, and `too many bucket prefixes (max 8)`, and E12.4 uses code `usage`.

007 ST-7.12 says the copy is SETTINGS-SCHEME §5.1. That table uses `setting_invalid_value` with `remember.buckets.prefixes: "Project:" must be lowercase and end in ":", like project:.` and uses `setting_out_of_range` with `remember.buckets.prefixes can hold up to 8 prefixes; you gave 9.`

Question: for a bad prefix shape, a duplicate, and a ninth prefix, what is the code, and what is the sentence?

Blocks: plan 20, the prefix-validation stage. Until this is answered, that stage does not print a prefix error. Grammar checks that do not print (accept `project:` and `status:`, reject by code only in a test that does not assert the sentence) may wait with the rest of the stage. Do not choose the sentence in the test.

### D-02. `plugin_chain_capped`

**Answered 2026-10-25:** both. The status is `skipped`/`chain_capped`, and the warning is `plugin_chain_capped` (010 H-12.4, PP revision 7).

010 H-12.4: when the chain reaches the cap, the remaining filters are not run. Their status is `skipped` with reason `chain_capped`. There is no `time_budget_capped` warning. A budget larger than the time left is clipped silently. The clipped value is the `budget_ms` Remember sends.

Plugin protocol §8.2 lists `plugin_chain_capped` as a warning code.

Question: does the user see a `plugin_chain_capped` warning, or only the status reason? If they see a warning, the plain sentence is also missing (add it to COPY-PENDING after the answer).

Blocks: plan 25, the chain-cap stage. Until this is answered, the implementer sets the status reason and does not emit `plugin_chain_capped`.

### D-03. Success JSON for settings commands

**Answered 2026-10-25:** 007 ST-7.11a–7.11b, ST-7.14 and ST-7.16 define one setting object and its wrappers, `action`, the page sizes (`list` 100, `history` 50, at most 1000), the cursor, `changed_at`, and human dates. Plan 20 quotes them.

007 ST-7.11 names the envelope types: `setting.list.result`, `setting.get.result`, `setting.set.result`, `setting.reset.result`, `setting.explain.result`, `setting.history.result`, `setting.import.result`.

Design names these fields and no others:

- `get`: `value`, `from`, `layers`, `updated_by`, `updated_at`. A scoped get with no row: `value` null, `from` the requested scope, `changed` false (ST-7.3, ST-7.4).
- `history` item: `key`, `old_value`, `new_value`, `updated_by`, `at`, `scope` (ST-4.1, ST-7.6).
- `explain`: the registry fields in ST-5.2 (`key`, `type`, range, `default`, `scopes`, `title`, `description`, `effect`, `related`, `since`, `status`).
- Export document: ST-8.1 and ST-8.2.

Design does not name the `data` object for `setting.set.result`, `setting.reset.result`, `setting.list.result`, or `setting.import.result`. `list` and `history` page by cursor (ST-7.11, JE-2.10). The page size is not stated.

Question: write the `data` object for set, reset, list, and import, and write the page size for list and history. Name every field. Say what human `set` prints. Human `get` is the 007 Examples block and is not open.

Blocks: plan 20, the command stage. Storage and resolution do not need this answer.

### D-04. Markdown `settings.json` and the CSV `#settings` row

**Answered 2026-10-25:** 007 ST-8.3 and ST-8.3a. The JSON `settings` member is a nested `remember-settings-export` version 1 document, and a brain with no settings rows writes it with an empty list. Markdown `settings.json` is the same document, omitted when the brain has no rows. CSV has no `#settings` row; the same document goes to `<CSV basename>.settings.json` beside the CSV file, with the same omit rule. Import reads it by ST-8.4 to ST-8.7. 006 E7.11 is dead.

006 E6.b says Markdown `settings.json` is the E12.9 object. 006 E6.c says the CSV `#settings` row carries that object in `body`. E12.9 is the add-wins object. 007 ST-5.4 deletes add-wins. 007 ST-8.3 says `remember export` carries every brain settings row, tombstones included, in the ST-8.2 shape, and never a user row. 008 JE-1.5 says a stored file starts with `format` and `version`. The only settings document Design defines is `remember-settings-export` version 1 (ST-8.1).

008 JE-8.3 replaces the CSV header and does not mention a `#settings` row.

Question:

1. Is Markdown `settings.json` a `remember-settings-export` version 1 document whose `settings` array holds only that brain's brain rows, including tombstones?
2. Does the CSV file still end with a `#settings` row? If it does, what are the exact bytes of that row?
3. In `remember-export` JSON, is `settings` an array of ST-8.2 objects, or a nested `remember-settings-export` document? 006 E6.a used an object. An untouched brain wrote `{}`.

Blocks: plan 22, the settings-bytes stage, and plan 23, the settings-import stage. Memory rows, links, atomic write, and the CSV memory header do not need this answer.

### D-05. Human `add` line in the 010 example

010 Examples show:

```
$ remember add --tag to:coder "ready for review"
Saved.
remember: warning: hook "wake_coder" did not start: ...
```

The shipped human success line for `add` is the id. 010 Design does not say to replace it with `Saved.`

Question: does human `add` stay the id, or does it print `Saved.`?

Blocks: nothing in plans 19–24. Plan 25 must not change the `add` success line until this is answered.

### D-06. Daemon log

Not a question for this extract. Recorded so it is not lost.

010 reverses S-11. The local daemon stays in v1 to serve local HTTP MCP. When it runs, it also owns warm filters, backfills, timed sync, and the queue. H-Q25 owns the socket, the handshake, the crash rule, and stale locks. Those details are not written.

Also unwritten, and not to be designed in plans 19–26: HTTP MCP bind, per-bot tokens, the activity log, and which jobs move from the CLI into the daemon.

Itay's answer for this extract: CLI and host only. No daemon plan. No HTTP MCP server. No per-bot tokens. No activity log.

The Linux daemon unit name `io.github.itayavtalyon.remember.daemon.service` (G-07) belongs to the daemon log. Do not install it.

### D-07. URL expansion log

010 H-14.4 leaves the HTML parser choice to the URL plugin log. H-14.7 leaves the enrichment setting key to that log. 010's draft step 11 says not to start until that log is closed.

Do not implement the URL plugin in plan 25. Do not pick a parser.

### D-08. Filter failure code

**Answered 2026-10-25:** `plugin_filter_failed` (010 H-12.5).

010 H-12.5: a failed or timed-out filter is skipped, and the command warns with a code that names the filter. Verification 21 names `plugin_filter_timeout` for a filter that sleeps past its budget. The spec lists `plugin_filter_failed` as well.

Question: is the non-timeout failure code exactly `plugin_filter_failed`? If not, name the code.

The plain lines are C-27 and C-28. Blocks: plan 25, the filter-failure stage. The timeout code `plugin_filter_timeout` is locked by verification 21. The failure code is not, until this row is answered. Do not emit a failure warning under a guessed code.

### D-09. Hybrid ranking log

Remember #485. The algorithm, the model, and the failure behavior beyond "built-in plugin, default disabled" are not in logs 007–010.

Plan 25 reserves the built-in name `hybrid`. It is trusted. `plugin.<name>.enabled` defaults to false. Plan 25 does not add a filter entry and does not rank. `plugin add hybrid` fails with `plugin_exists` once that stage exists.

Do not implement the ranker in these plans.

### D-10. API surface v1

Remember #583. The full error catalog, the HTTP status for each code, the MCP tool schemas, and the `GET /v1/settings` route schemas are not written.

Engine plans name only codes that 007–010 or the plugin spec already name. They do not assign HTTP status numbers. Cloud and MCP implementers cannot start the catalog from these plans.

Locked for Cloud without that log, quoted in the plans that need them:

- `GET /v1/settings` and `GET /v1/settings/{key}` only. No HTTP call writes a setting (007 ST-9.1, ST-9.2).
- MCP tool `remember_settings` with `action` `list`, `get`, or `explain`.
- `remember_call` accepts `setting.set`, `setting.reset`, and `setting.import`.
- Every surface uses `remember-message` version 1 (008 JE-1.2).
- Memory id is `id`, a lowercase hyphenated UUID v7. No `sync_id`. No `local_id` on HTTP, MCP, events, or export (008 JE-3).
- `remember sync --strict` exits 4 when the server drops unknown fields (Cloud 001, M-01 A).
- Concurrent link add and unlink: later `updated_at`, then larger device id (Cloud 001, M-02 B). A link change updates `updated_at`, `updated_by`, and `change_seq` on both ends and does not change either memory vector (008 JE-5.1).
- Cloud v1 does not offer end-to-end encryption. No opt-in line (Cloud 001, M-11 C).
- The Cloud-built hybrid index stays on the server. Nothing Cloud-built is downloaded into `plugins/<name>/data/` (Cloud 001, M-13 A).
- Account lapse notice: email at lapse, and 30 days and 7 days before deletion. Local data is never affected (Cloud 001, M-10 A).

### D-11. Sync wire

Settings merge is locked (007 ST-2.7). Tombstone confirmation, the change feed, and the sync protocol wire are a later log (008 Future Work, 007 Future Work). Engine plans do not invent the wire. Cloud plans that move bytes on the wire wait for that log. The merge rule itself does not wait.

### D-12. remember-mac log 011

App accessibility tests moved to remember-mac log 011 (010, M-19 N9). That log is not in this repo. Plans 19–26 do not write the Mac app.

Engine contracts the app calls are in plans 20 and 25: `RememberEnv` callbacks (010 H-0.6), `remember_call` for settings writes, the hourly LaunchAgent command line `remember sweep --all` (the app installs it, the engine does not), and Empty Trash as one `purge` per Deleted memory with no Touch ID (010 H-16.9).

### D-13. Tool-undo name parse

007 ST-11.3: next to a tool actor, the app shows `Changed by <name>, 2 hours ago`, where `<name>` is the plugin name from `updated_by.id`. The id shape is `plugin/<name>/<entry>` (008 JE-4.4). Design does not say how `<name>` is parsed when the id has more than one slash, or what the app shows when the id is null.

This is app copy, not a remember C plan. Do not invent the parse in plan 20 or plan 25.

### D-14. `skipped_source` log line

**Answered 2026-10-25:** 010 H-7.2. One line per skipped action and event type in the run log, shown by `remember plugin log <plugin>`: `{"version","at","event":"skipped_source","event_type","command","origin","plugin","action","count"}`.

010 H-7.2: a sync or import command writes one `skipped_source` log line, with the count, when an action's `origins` does not list the event origin. The line's fields are not listed. H-15.3 points at HK §6.3 for a run record and does not list `skipped_source` fields.

Question: what are the JSON fields of that one log line?

Blocks: plan 25, the sync-origin stage. The count behavior is locked. The bytes of the line are not.

## Known limitation, not a question

A persistent plugin that omits `REMEMBER_CAUSE_EVENT_ID` and `REMEMBER_HOOK_CHAIN` counts as depth 1. The chain kill switch at 100 does not fire for that writer (010 H-5.3, G2-06). Plan 25 states this. It does not add a second rule.
