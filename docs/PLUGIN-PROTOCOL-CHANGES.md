# PLUGIN-PROTOCOL.md changelog

Before 1.0, breaking changes are clean breaks with a changelog note and no aliases. This file is that note.

## Revision 7 (2026-10-25)

Document revision of protocol 1 (log 009 P-Q4). No wire change. Applies Itay's copy-pending answers (`fk-review/copy-pending/ITAY-ANSWERS.md`), section 0.8:
- C-20: a pre-flight hash mismatch warns `plugin_untrusted`. `plugin_preflight_failed` covers a missing or non-executable file only (R8.15, §8.2).
- D-02: a capped chain warns `plugin_chain_capped` and sets status `skipped`, reason `chain_capped` (§8.2).
- C-30/C-31: `plugin_action_timeout` and `plugin_action_failed` print only in a command that runs the action synchronously (for example `plugin run`); writes never wait (R8.16).
- D-08: `plugin_filter_failed` is confirmed as the non-timeout filter code.
- C-23: §8.2 adds `plugin_unsupported` as a warning for dispatch in a `REMEMBER_PROCESSES=0` build (R8.15).

Validator: unchanged, and it still passes (no example changed).

## Revision 6 (2026-10-22)

Applies the 24-row amendment table of log 009 (Design), as Itay chose on 2026-10-22 (M-06 A, `decision:2026-10-22-M-06`, 750), with the merge answers M-17 (757) and M-19 N2/N6/N7 (759) and the Interface review's warning-code fix. Still protocol 1, because no plugin shipped (log 009 P-Q4). Protocol 1 is frozen after this revision (PR-5). Spec section 0.7 lists the decisions.

Breaking changes (clean breaks, no aliases):
- One format: every message is `remember-message` version 1 (S-07). `remember-plugin-message`, `remember-cli-message` and `remember-api-message` are gone; a search response is a CLI response when it has no `request_id` (R8.1).
- `$id` is `https://itayavtalyon.github.io/remember/schema/plugin/v1/<file>.json` (G-06).
- Names: `plugin_name` and `entry_name` are both `^[a-z][a-z0-9_]{0,39}$` (M-17); a hyphen in a manifest is `plugin_manifest_invalid` with a fix that suggests `_` (R2.22). Examples `url-expand`, `log-ids` are `url_expand`, `log_ids`.
- `source` is not a memory field or a `needs` item (G-33). The action `filter` block has `created_by`, `updated_by` and `origins` in place of `source` (R2.21, G-11, G2-10), and an unknown field in it is an error (R9.19).
- Six event types; `memory.linked` and `memory.unlinked` are gone (S-10).
- Every event has real `plugin`, `entry`, `data_dir` and `settings` (S-01); `cause.hook` is `plugin/<name>/<entry>`.
- `data_dir` is `<brain>.d/plugins/<name>/data/` (G-28).
- Generated keys: only `plugin.<name>.enabled` (S-06). Budget, cold-start grace (new manifest field `cold_start_grace`, 1 ms to 30 s, default 10 s, shown in `plugin status` `entries[]`; 2026-10-22, M-24 A, #765) and priority come from the manifest only. The CF-7 entry-name collision rule is deleted.
- Search has no `next_cursor`/`has_more`; `data` adds `total`, `keys`, `tags` (S-08).
- Exit codes: any warning gives 4 under `--strict` (G2-03); 4 has no other meaning; an unfinished backfill exits 1 with `plugin_backfill_incomplete` (S-09). The §8.2 Strict column is deleted.
- No exit-75 retry (S-15). A crash loop sets `disabled_reason` `crash_loop` in `index.json`, not a setting (S-02).

Rule changes:
- Warning codes (§8.2): `hook_preflight_failed` → `plugin_preflight_failed`, `hook_dispatch_failed` → `plugin_dispatch_failed`; added `plugin_loop_halted` (M-19 N6, was `plugin_chain_halted` in log 010), `plugin_untrusted`, `plugin_dev_mode`. `time_budget_capped` is removed (2026-10-22, M-22 A, #763: as S-06 says, a budget above the time left in the chain cap is clipped silently). No `hook_` code remains.
- Trust per code hash, confirmed at `plugin add`, update, `hook add` and `plugin trust`; `plugin enable` asks nothing (S-05). `disabled_reason` adds `untrusted`.
- R3.23 / R6.1: `REMEMBER_HOOK_DEPTH`, `REMEMBER_HOOK_NAME`, and cause passing on writes (G2-06). R6.5: socket `<runtime>/s/<16 hex>.sock`. R7.4: catch-up sends purge tombstones (G-09). R9.28: `plugin log --follow --json` lines.
- §10: the log 010 §16 commands (`show`, `trust`, `rerun`, `remove --delete-data`, `status --ack`, `test NAME|DIR`); `enable|disable` are shortcuts for `settings set` (M-19 N2).
- The daemon branches stay (738).

Validator (`scripts/validate_plugin_spec.py`, updated for revision 6): 13 schemas, 29 examples, 81 negative tests, 0 code-check negative tests, 14 positive edge tests, 0 failures (after the Interface final-review fixes of 2026-10-22: `cause.source` removed, ST-1.1 setting-key patterns).

## Amendment to revision 5 (2026-10-22)

- `[decision:2026-10-22-plugin-settings-read]` (740): new R2.16a. Plugins read settings read-only: their own `plugin.<name>.*` keys and the `remember.*` keys listed in `permissions.needs` (for example `remember.search.max_results`), in the existing `settings` list (R3.10). Plugins never write settings (`setting_from_hook`). No wire change, no new message.
- `[decision:2026-10-22-daemon-v1]` (738): the daemon branches of R6.9, R7.3, R7.5a and R9.6a stay (#517, #518); S-11 is reversed. Log 009's revision 6 table no longer deletes them.

## Revision 5 (2026-10-19)

This revision applies Itay's decisions on the Interface review of revision 4 (`fk-review/PLUGIN-PROTOCOL-REVIEW.md`, PP-01 to PP-18) and the FX answers that touch the protocol. Section 0.6 lists them with their Remember DB ids: 516, 543, 563, 566, 567, 571, 574, 575, 577, 596, 597, 599, 600–608, and 517/518 for the daemon. Section 0.5 says that no open items remain.

### Breaking changes (no aliases, no fallbacks)

- `[decision:protocol-backfill-seq]` (599), `[decision:change-seq]` (543): `backfill_needed` is now `{applied_through_seq}`, an integer `change_seq`. `0` asks for every memory. A `catch_up` backfill names `since_seq` (integer) in place of `since` (a vector). No version vector appears in any plugin message: `common.json#/$defs/memory` forbids `version_vector` and allows `change_seq`. Backfill entries require `change_seq` in place of `version_vector`. `version_vector` is no longer a `needs` field. A plugin keeps the version with the greater `change_seq` (R6.14, R7.2, R7.4, R7.11, NQ-7, NQ-8).
- PP-07 (604): `backfill_done`, `backfill_pause`, `backfill_resume` and `backfill_done.result` are now `backfill.done`, `backfill.pause`, `backfill.resume` and `backfill.done.result` (section 1.2, R1.8, R7.12–R7.14, envelope schema).
- PP-08 (604): `event.result` has no `result` field and no `message`. A success is `data {event_id}`. A failure is `error {code, message, fix}`. A well-formed failure is not a strike (R3.18, R3.21, R3.22, R9.10).
- PP-14 (604): every request and event carries `data.entry` as the routing key. `data.filter` (in `filter.search.query` and `filter.search.results`) and `data.plugin.action` (in `event`) are gone. Event data requires `entry` (`null` for a hook) (R3.6, R6.4).
- `[decision:hook-origins-filter]` (577): event `cause` requires `origin` (`local`, `sync` or `import`) (R3.5a).
- `[decision:no-fortkeeps-settings-namespace]` (571): `global_setting_key` accepts only `remember.*` (R2.16).
- `[decision:brain-sidecar-dir]` (566): `data_dir` is `<brain>.d/plugins/<name>/`. The hooks log and hooks config live in `<brain>.d/` (R3.9, R3.15, R9.19, R9.21, R9.28).

### Rule changes

- PP-01 (516), PP-02 and FX-30 (574): a search or write that a plugin or hook degrades exits 0 with a warning. With `--strict` or `REMEMBER_STRICT=1`, strict warnings give exit code 4. Exit code 4 by default stays only for a command's own job (`plugin reindex`, the `plugin enable` backfill). Section 8.2's column is now "Strict". OQ-1, OQ-6, SET-2 and NQ-3 carry "superseded in part" notes (R1.14, R5.9, R8.13–R8.16, R9.2, R9.16).
- PP-03 (600): new R9.6a (warm-up). R9.2 states that a cold filter gets at most the chain cap. New manifest field `idle_timeout` (R2.20).
- PP-04 (601): new section 2.4 "Minimal plugin" with a hello filter and a once-process action. The hello script now sends `index_version`, because R6.13 requires it.
- PP-05 (602): new section 10 "Plugin CLI" with the command table and the `plugin-status.json` schema. `plugin status` always exits 0 when it can report (NQ-3, R3.21 amended).
- PP-06 (603): new R2.19, `plugin add --dev`, and the warning `plugin_dev_mode` (exit code 0).
- PP-09 (605): new section 2.1a glossary and R6.2a (protocol mismatch, `disabled_reason: protocol_mismatch`). `ready.plugin` and `ready.plugin_version` are optional hints.
- PP-10 (606): new R2.5a (`detail {path, rule}`, every error at once). R2.4 gives the `plugin_protocol_unsupported` text.
- PP-11 (607): new R2.3a (needs at `plugin enable`, changed needs only, the plain-words boundary).
- PP-12 (604): new R6.15 (persistent stderr log).
- PP-13 (604): R6.9: Remember sends `shutdown` reason `idle`. `REMEMBER_PLUGIN_IDLE_TIMEOUT` is information only (R6.1).
- PP-15 (608): R8.12: human output, `--explain`, the cold-start line, the MCP tool text. The prefix is `remember: warning:` (#409, MX-7).
- PP-16 (604): R8.8 names `<plugin>/<entry>` = `plugin.<plugin>.<entry>`.
- PP-17 (575): search always carries `next_cursor` and `has_more` (R5.8, R8.3). No schema change.
- PP-18 (604): R9.28 states the one shared hooks log and points authors to `remember plugin log`.
- 597: `setting_capped` is now `time_budget_capped` (R2.13, section 8.2). Removed 2026-10-22 (M-22 A, #763).
- 596: `plugin.<name>.enabled` defaults to `false` for every plugin (R2.15).
- 517, 518: the daemon runs pending backfills, keeps filters warm and sends idle `shutdown` when it runs (R6.9, R7.3, R7.5a, NQ-16, CF-10).
- 567: R9.29 names the C-ABI entry `plugin_handle(msg_json) -> reply_json` on a worker thread.
- 563: `common.json` adds the `actor` object and optional `created_by`, `updated_by` and `change_seq` on memories. They are `needs` fields.
- The source list cites logs 007–010, not the old `007-event-hooks.md`.

### Validation

- Run: `/workspace/.venv-jsonschema/bin/python tools/validate_spec.py PLUGIN-PROTOCOL.md`.
- New schema: `plugin-status.json` (section 10), with one example.
- New examples: the hello and log-ids manifests (section 2.4) and a failed `event.result`.
- Changed negative tests: a backfill entry without `change_seq`, a `catch_up` without `since_seq`, a `backfill_needed` without `applied_through_seq`.
- New negative tests: an old `since` vector, a `version_vector` in a backfill entry or an event memory, the old `applied_through` vector, a negative `applied_through_seq`, `event.result` with `result`, the old types `backfill_done` and `backfill_pause`, `data.filter` in place of `data.entry`, a filter request or an event without `entry`, `data.plugin.action`, a `cause` without `origin` or with an unknown origin, a `fortkeeps.*` need, a `version_vector` need, a plugin status without `last_problems`, an unknown `disabled_reason`.
- New example checks: no `version_vector` and no `data.filter` in any plugin message; no old backfill type names, `applied_through`, `since` or `result` values in any example.
- New positive edge tests: `applied_through_seq: 0`, a manifest `idle_timeout`, a plugin status with `protocol_mismatch`.
- Result: 13 schemas valid, every `$ref` resolves, 29 examples valid, 75 negative tests rejected, 1 code-check negative test rejected, 12 positive edge tests accepted, 0 failures.

## Revision 4 (2026-10-18)

This revision applies Itay's decisions on every item that revision 3 left open in section 0.5: NQ-17 (Remember DB id 510), SET-2 (509, which resolves CF-8), CF-7 (511), CF-9 (512), CF-10 (513) and CF-6 (514). It also applies the new decision `[decision:protocol-index-version-report]` (515). Section 0.4 now holds them in a second table with their Remember DB ids. Section 0.5 says that no open items remain.

### Breaking changes (no aliases, no fallbacks)

- SET-2 (resolves CF-8): the filter chain cap now covers everything, including a cold filter's start-up grace. Before, the grace sat outside the cap. Remember waits for a cold filter at most the grace or the time left in the chain cap, whichever is shorter, and sends that time as `budget_ms`. On a cold filter's first search, Remember falls back to the BM25 results (the filter's input passes through unchanged) with status `timeout`, reason `cold_grace`, `cold: true`, warning `plugin_filter_cold_start` and exit code 4. The filter keeps loading, and it is not a strike (R4.7, R8.15, R9.2, R9.4, R9.6, R9.9, section 8.2).
- `[decision:protocol-index-version-report]` (515): a persistent plugin now reports its current `index_version` (`{data_version, model}`, `model: null` when it has none) at the top level of the data of every `ready` and every `ping.result`. Both schemas now require it. A `ready` or `ping.result` without it, or with a `data_version` that differs from the manifest, is malformed output (R6.2, R6.7, R6.13, R9.14).
- `backfill_needed` is now `{applied_through}` only. `backfill_needed.index_version` is gone: the top-level `index_version` is the one place for it, so the two can never disagree. A plugin that needs every memory again with an unchanged index sends `applied_through: {}` (R6.14). An `index_version` inside `backfill_needed` is an unknown field.
- `ready` data no longer has `data_version`. The value is `index_version.data_version`.
- CF-7: the per-entry budget key `plugin.<name>.<entry>.budget` is locked. No `plugin.<name>.budget` key exists. A filter entry name must not equal one of that plugin's setting keys (a declared `settings` key, or the generated `enabled`, `priority`, `cold_start_grace`). Remember rejects such a manifest with `plugin_manifest_invalid` (R2.11). `manifest.json` now rejects a filter entry named after a generated key. A clash with a declared setting key is checked in code (section 2.2).

### Rule changes

- NQ-17: the `warnings` of a plugin reply go only to the plugin log (`<brain>.hooks.log`) and to `remember plugin status`. They never go into the `warnings` of a CLI or API response, so they never change the exit code (section 1.2, R9.21).
- CF-9: `plugin_request_trimmed` exits 0 (locked) (R5.12, section 8.2).
- CF-10: a backfill requested with `backfill_needed` during a search or a write is only marked pending. The next `remember plugin reindex`, `remember plugin enable`, or launch of the Fortkeeps app runs it. `remember plugin status` shows it as pending (R6.12, R7.3).
- CF-6: Remember stores the plugin's `index_version` after each full backfill. A different `index_version` in a later `ready` or `ping.result` reply marks the index `stale`, and a rebuild is due under the normal desktop and phone rebuild rules `[decision:plugins-rebuild-policy]`. With no stored `index_version`, the index is `missing` and a full backfill is due. The "for Itay to confirm" marks are gone (R6.13, R7.5).
- 515 fixes the R7.5 guess: Remember no longer stores "the manifest `data_version` with `model: null`" for a backfill that the plugin did not ask for. Before a full backfill, Remember pings the process and keeps the reported `index_version`. After `backfill_done.result`, it stores that value. A different value reported during or after the stream marks the index `stale`. A stopped stream stores nothing (R7.5).
- R2.14: the manifest `data_version` now defaults to `1`, and Remember compares it with the `data_version` of the stored `index_version`, so it can mark the index `stale` before it starts the process.
- R7.3: a `missing` or `stale` index found during a search or a write is pending too, the same as a `backfill_needed` request (CF-10).
- Backfill `reason` has a new value `index_version`: the plugin reported a different `index_version`. `data_version` stays for a changed manifest.
- Examples: the timeout warning in section 8.4 now tells the user to set `plugin.hybrid.rerank.budget`, not `plugin.hybrid.budget`. The `filter.search.results` request in section 5.3 now carries `plugin.hybrid.rerank.budget` in `settings`. Section 8.4 has a new CLI example of a cold-start fallback to BM25.

### Validation

- Run: `/workspace/.venv-jsonschema/bin/python tools/validate_spec.py PLUGIN-PROTOCOL.md`.
- New negative tests: a filter entry named `enabled` or `cold_start_grace` (CF-7), a setting key `cold_start_grace`, and a cold-fallback filter item without `reason` (SET-2).
- New manifest code check (R2.11, CF-7, not expressible in JSON Schema): it runs on every manifest example and rejects a filter entry whose name equals a declared setting key.
- New negative tests for 515: `ready` without `index_version`, `ping.result` without `index_version`, an `index_version` without `model`, and a `backfill_needed` without `applied_through`.
- New code check for 515: every `ready` and `ping.result` example reports the manifest `data_version`.
- New positive edge tests: a plugin reply that carries `warnings` is a valid message (NQ-17), a filter entry named `budget` is allowed (its key is `plugin.<name>.budget.budget`), an `index_version` with `model: null`, and a `backfill_needed` with an empty `applied_through`.
- Result: 12 schemas valid, every `$ref` resolves, 25 examples valid, 57 negative tests rejected, 1 code-check negative test rejected, 10 positive edge tests accepted, 0 failures.

## Revision 3 (2026-10-18)

This revision applies Itay's decisions on Q-A to Q-D, NQ-10 to NQ-16, and the conflict points CF-5 and CF-6.

### Breaking changes (no aliases, no fallbacks)

- Q-A: the message type `error` is gone. A failed reply keeps its `<request type>.result` type, echoes `request_id`, and carries `error` in place of `data`. The envelope schema rejects `type: "error"`. A reply to a request of an unknown type is `<that type>.result` with code `unknown_message_type`.
- NQ-14: the budget is per filter entry. The generated key `plugin.<name>.budget` is now `plugin.<name>.<entry>.budget` (proposal CF-7, needs an SS update). A filter entry name must not equal a manifest setting key.

### Rule changes

- Q-B: three format names, each with its own version, are now locked: `remember-cli-message` v2, `remember-api-message` v1, `remember-plugin-message` v1. NQ-15 is settled by Q-B.
- Q-C: export files (`remember-export`, `remember-settings-export`) and `plugin.json` are plain documents, not envelopes.
- Q-D: `REMEMBER_TEST_FIXED_IDS=1` works only in test builds (R1.16).
- NQ-10: a plugin's `unknown_field` warnings go only to the plugin log and `remember plugin status`, never into command output (R9.21).
- NQ-11: malformed and oversize output are now strikes, the same as a crash. Only a well-formed failed reply is exempt (R1.14, R9.9, R9.10, R9.15, R9.16).
- NQ-12: a `filter.search.results` request over 16 MiB is trimmed from the bottom of the candidate pool until it fits. New warning `plugin_request_trimmed` (R5.12; exit code 0 proposed, CF-9).
- NQ-13: Cloud webhooks use `remember-plugin-message` v1.
- NQ-14: every filter entry's warm time counts toward the chain cap (R9.1, R9.4). The cold-start grace stays outside the cap (proposal CF-8).
- NQ-16: the command or app that starts a backfill runs it in the foreground under a per-plugin, per-brain lock. It saves progress after each answered batch, and a later run resumes from there. No daemon (R7.3, R7.5a, R7.5b). A plugin request seen during a search or write only marks the backfill as pending (proposal CF-10).
- CF-5: `remember plugin reindex` stays the command. CF-6 (the `index_version` comparison) is kept and marked for Itay to confirm (R6.13, R7.5).
- Section 0.4 now holds these decisions. Section 0.5 lists the open items: NQ-17, CF-6, CF-7, CF-8, CF-9, CF-10.

### Validation

- The validator now also rejects `type: "error"`, a failed reply without `request_id`, an unknown `.result` type that carries `data` or a code other than `unknown_message_type`, and an unknown request type.
- Result: 12 schemas valid, every `$ref` resolves, 24 examples valid, 49 negative tests rejected, 6 positive edge tests accepted, 0 failures.

## Revision 2 (2026-10-18, afternoon)

This revision applies Itay's envelope decisions (E1 to E7), his decisions on NQ-1 to NQ-9, and the Interface defects D24 to D28 and D30 in JSON-STYLE.md.

### Breaking changes (no aliases, no fallbacks)

- Envelope: `{format, version, type, request_id?, data?, error?, warnings?, next_cursor?, has_more?}`. The envelope `id` is gone. `body` is now `data` (D25, D30). `format`, `version` and `type` are required.
- Requests that expect a reply carry `request_id`, and replies echo it. Notifications (`ready`, `shutdown`, `backfill_pause`, `backfill_resume`) have none (E6, NQ-5).
- A failed reply puts the error object in the envelope `error` field, not in `data`. Its type stays `error` for now (open: Q-A, D27).
- Reply types are now `<request type>.result`: `pong` is now `ping.result`, `event_ack` is now `event.result`, and `backfill_ack` is now `backfill.result` and `backfill_done.result` (E4).
- Event: the `remember-event` document is gone. The `event` message `data` is `{event_id, event_type, occurred_at, detected_at, batch_id, cause, brain, memory, changes, truncated, truncated_fields, limits, plugin, data_dir, settings}`. Every field is always present. `entries[0]` is now `memory`, `event.id` is now `event_id`, and `event.type` is now `event_type` (D26, HK §2.1). The schema `remember-event.json` is now `event-data.json`.
- Search response: a full envelope, `format: "remember-cli-message"` version 2 (CLI) or `"remember-api-message"` version 1 (HTTP and MCP, with `request_id`), with `type: "memory.search.result"` (D28, NQ-1). `next_cursor` and `has_more` moved from `data` to the envelope (D24, E7).
- `time_range` now has `field` (`updated_at` or `created_at`, default `updated_at`) (NQ-6).
- `backfill_needed` is now `{index_version{data_version, model}, applied_through}`. `reason`, `last_applied_version` and `message` are gone (NQ-7).
- `backfill` data now has `mode` (`full` or `catch_up`) and, for `catch_up`, `since` (NQ-7).

### Rule changes

- Section 0 now has three decision tables (OQ-1 to OQ-30, E1 to E7 with J1 to J9, NQ-1 to NQ-9) and one open list (Q-A to Q-D, NQ-10 to NQ-17).
- NQ-2: `plugin_action_timeout` and `plugin_action_failed` are now locked codes (exit code 4).
- NQ-3: a missed action reply returns exit code 4 only from a command that waits (a backfill such as `remember plugin reindex`, or `remember plugin status`). A write records it in the plugin log and in plugin status. It is still a strike.
- NQ-4: `plugin_additions_capped` and `plugin_note_truncated` are now locked warnings with exit code 0.
- NQ-8: backfill plugins always get `updated_at` and `version_vector` (locked).
- NQ-9: filters that the user turned off are not listed. Filters that a crash loop disabled are listed as `skipped` (locked).
- Remember records the `index_version` after each completed full backfill and compares the next `backfill_needed` with it.
- New open question NQ-17: what Remember does with `warnings` in a plugin reply.

### Validation

- `tools/validate_spec.py` now checks the new envelope rules: the key order, no envelope `id`, no `body`, and none of the old type names.
- Result: 12 schemas valid, every `$ref` resolves, 24 examples valid, 44 negative tests rejected, 6 positive edge tests accepted, 0 failures.

## Revision 1 (2026-10-18)

This revision applies Itay's locked decisions OQ-1 to OQ-30, the JSON decisions of 2026-10-18, and the updated JSON-STYLE.md. Before 1.0, breaking changes are clean breaks with a changelog note and no aliases. This file is that note.

### Breaking changes (no aliases, no fallbacks)

- The memory UUID `sync_id` is now `id` everywhere. A reference to a memory from another object is `memory_id` (cause chain, search result items, provenance scores, hook log). `failed` in a backfill ack is now `failed_memory_ids`. No reader accepts `sync_id`.
- No memory in an event or a plugin message carries `local_id`. Only CLI output carries it.
- The envelope `format` is now `remember-plugin-message`. `remember-plugin` is the manifest format only.
- The envelope is `{format, version, type, id, body}` on every transport, plus `request_id` on replies. `reply_to` and `sent_at` are gone. A reply now points to its request with `request_id`.
- Once-processes and hooks now receive an `event` envelope on stdin, with the `remember-event` in `body`. Before, they received a bare `remember-event`.
- Message types lost the `plugin.` prefix: `event`, `event_ack`, `backfill`, `backfill_ack`, `backfill_done`, `backfill_pause`, `backfill_resume`. `plugin.backfill_request` is gone. A plugin asks for a backfill with `backfill_needed` in `ready` or `pong`.
- `settings` in every input is now a list of `{key, value}` items, not an object keyed by setting key (JS §1.1). Duration and size values have `value_ms` and `value_bytes` twins.
- Manifest: it now starts with `format: "remember-plugin"` and `version: 1`. The plugin SemVer moved from `version` to `plugin_version`. Each action and filter has a strict `run` section (`process`, `command`, `timeout`) and a strict `permissions` section (`needs`). `backfill` is an object (`enabled`, `max_batch_entries`, `max_batch_size`), not a boolean. `needs` may list global setting keys.
- The `search.query` input and output carry a query object (`text`, `tags`, `bins`, `time_range`), not a string.
- `search.results`: candidates carry `scores` (labelled by filter) in place of `score`. Result items are `{memory_id, score}`. The request has `max_additions`.
- The backfill batch list is `entries`, not `memories`.
- Provenance: `ms` is now `duration_ms`. `version` is now `plugin_version`. Items that are not `applied` have a `reason`. `applied` result filters carry `scores`.
- The search response is `{version, data[, warnings]}`. Paging fields, `query`, `ranked_by` and `filters` are inside `data`. `warnings` is absent when empty.
- Warning items are `{code, message, detail?}`. `fix` and `retryable` are no longer warning fields.
- `pong.busy` is now `pong.active_requests`.
- In 007-event-hooks.md: `sync_id` is now `id`, `--sync-id` is now `--id`, `REMEMBER_SYNC_ID` is now `REMEMBER_ID`, and colon-style setting keys (`remember:hooks/sync-events`) are now dotted keys (`remember.hooks.sync_events`). No aliases.

### Rule changes

- Section 0 is now a decisions table (OQ-1 to OQ-30, plus J1 to J9) and a list of new open questions (NQ-1 to NQ-16).
- Malformed output and oversize messages are no longer strikes (OQ-22: only timeouts and crashes). Persistent action ack timeouts and backfill ack timeouts are strikes (OQ-12).
- Once-process stdout is ignored. stderr goes to the log (OQ-13).
- `search.query` may rewrite the filters as well as the text (OQ-16). The response shows the original and the rewritten query.
- Scores use each plugin's own scale. Remember passes them through, labelled by filter (OQ-18).
- Additions are capped at the candidate pool size, with the warning `plugin_additions_capped` (OQ-19).
- A long note is truncated with "…" and the warning `plugin_note_truncated`. The plugin output schemas no longer limit the note length (OQ-21).
- Filters that never ran appear as `skipped` with reason `chain_capped` or `disabled` (OQ-29). An intentional pass-through is `skipped` with reason `pass_through` (OQ-20).
- Unknown fields: an error in the manifest `run` and `permissions` sections and in `hooks.json`. The warning `unknown_field` everywhere else. Before, receivers ignored unknown fields silently.
- The manifest may ask for smaller backfill batches, never bigger (OQ-25). Remember rejects a backfill manifest with no persistent entry (OQ-24).
- During a backfill, the plugin keeps the newer version of a memory. Backfill plugins always get `updated_at` and `version_vector` (OQ-26, NQ-8).
- Pause and resume: after a pause Remember closes the connection. Resume opens a new connection (OQ-27).
- Exit code 4 is set only by the OQ-28 codes and the proposed `plugin_action_*` codes (NQ-2, NQ-4).
- UUIDs: readers accept upper or lower case and any UUID version. Writers emit lower case. Remember mints v7.

### Validation

- `tools/validate_spec.py` extracts every schema and example from the spec and checks them with Python `jsonschema` 4.26 (Draft 2020-12). Run: `python tools/validate_spec.py PLUGIN-PROTOCOL.md` (needs `jsonschema` and `referencing`).
- Result: 12 schemas valid, every `$ref` resolves, 23 example documents valid, 24 negative tests rejected, 3 positive edge tests accepted, 0 failures.
- Every example now has a marker that names its schema. The envelope schema selects the body schema by `type`, so each example checks its body too.
