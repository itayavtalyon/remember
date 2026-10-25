# Remember plugin and hook protocol, version 1 (draft)

Author: Grok Bot, for Itay. Draft of 2026-10-16. Revisions 1, 2, 3 and 4 of 2026-10-18. Revision 5 of 2026-10-19 (Interface review PP-01 to PP-18 decided, FX answers applied). Amended 2026-10-22 (R2.16a, read-only settings). Revision 6 of 2026-10-22: the log 009 amendment table applied (Itay, M-06), plus the 2026-10-22 merge answers M-17 and M-19 N2/N6/N7 and the Interface warning-code fix; section 0.7. Revision 7 of 2026-10-25: warning-code wording from Itay's copy-pending answers (C-20, C-23, D-02, D-08), still protocol 1; section 0.8. Status: **draft for Itay's approval** (Interface reviewed revision 4 in `fk-review/PLUGIN-PROTOCOL-REVIEW.md`) (decision:plugins-filter-needs requires both). The changes of each revision are in `PLUGIN-PROTOCOL-CHANGES.md`.

Sources of truth, in priority order:
1. Itay's locked decisions. The decisions of 2026-10-18 are in sections 0.1 to 0.4. Older decisions in his Remember DB are cited as `[key]`, for example `[decision:plugins-strikes]`.
2. `fk-review/JSON-STYLE.md` (cited **JS**). Where a decision in section 0 differs from JS, the decision wins.
3. `fk-review/HOOKS-INTERFACE.md` (cited **HK**) and `fk-review/SETTINGS-SCHEME.md` (cited **SS**).
4. Design logs 007 (settings), 008 (JSON envelope), 009 (plugin protocol) and 010 (plugins and hooks). Log 010 replaces the old `design-logs/007-event-hooks.md`, which this spec no longer cites.

Normative words: **must**, **must not**, **should**, **may** (RFC 2119). Each normative rule names its actor: **Remember** (the engine, CLI or in-app engine), the **plugin**, the **dispatcher**, or the **user**.

Before 1.0, a breaking change ships as a clean break with a changelog note. Remember keeps no compatibility aliases and no fallbacks (Itay, 2026-10-18). For example, no reader accepts `sync_id`, `body` or a bare `remember-event`, and no `--json-version 1` or `--sync-id` exists.

---

## 0. Decisions and open questions

### 0.1 Protocol decisions (locked by Itay, 2026-10-18)

Each rule that applies a decision carries the tag **(OQ-n)**.

| OQ | Decision | Summary |
|---|---|---|
| OQ-1 | Exit code 4 stays. Superseded in part by `[decision:plugins-degraded-exit-code]` (#516), section 0.6. | A search or write that a plugin or hook degrades exits 0 with a warning. Exit code 4 by default stays only where a command's own job failed in part. With `--strict`, the strict warnings give exit code 4. |
| OQ-2 | Each manifest setting declares its scope. | `scope` is `user`, `brain` or `both`. The default is `user`. |
| OQ-3 | Use the current names. | Dotted setting keys (for example `remember.plugins.chain_budget`), current scopes. Old names are not aliases. (`remember.hooks.sync_events` is gone: `[decision:hook-origins-filter]` #577.) |
| OQ-4 | One envelope on every transport. | The socket, stdin and stdout use the same envelope (E1). The event goes in `data`. Parsing never depends on the transport. |
| OQ-5 | The protocol version is an integer. | `1` in v1. Only a breaking change bumps it. |
| OQ-6 | Size limits. | 16 MiB for one socket message. 256 KiB for the `data` of one event. An oversize plugin message is a plugin failure: the input passes through, with a warning (exit code 0; 4 with `--strict`, #516). |
| OQ-7 | Socket and handshake. | Remember gives the socket path in `REMEMBER_PLUGIN_SOCKET`. The plugin binds it, then writes an enveloped `ready` to stdout. |
| OQ-8 | One request per connection in v1. | One connection carries one request and its one reply. A backfill stream is the one exception. |
| OQ-9 | One process per plugin per brain. | The process serves all persistent entries of the plugin. |
| OQ-10 | Windows uses AF_UNIX. | The platform abstraction layer hides it. The wire format does not change. |
| OQ-11 | Default data without `needs`. | A hook gets the full memory. A plugin action gets only `id` and `bin`. A filter gets `id` and scores. |
| OQ-12 | Persistent actions ack each event. | The ack is the `event.result` reply. A missing ack after the action timeout is a strike. Exit codes follow NQ-3 as amended by #602. |
| OQ-13 | Once-process output. | Remember ignores stdout. Only the exit code counts. stderr goes to the log. |
| OQ-14 | Path forms. | `brain.path` is shortened with `~`. `data_dir` is absolute. |
| OQ-15 | Settings in the input. | The plugin's own resolved `plugin.<name>.*` settings, plus the global settings it declares in `needs`. The user approves them at install. |
| OQ-16 | What `search.query` may rewrite. | The query text and its filters (tags, time range, bins). The search response shows the original and the rewritten query. |
| OQ-17 | `search.query` budgets. | The same as `search.results`: 800 ms warm, 10 s cold-start grace, 30 s hard cap, and the shared 1.5 s chain cap. |
| OQ-18 | Scores. | Each plugin uses its own scale. Only the order counts. Remember passes the scores through, labelled by filter. |
| OQ-19 | Additions cap. | At most the candidate pool size (default 200). Remember drops the extras with a warning. |
| OQ-20 | Intentional pass-through. | `skipped: true` plus a `note`. It is not a failure and not a strike. |
| OQ-21 | Note length. | At most 120 Unicode characters. Remember truncates a longer note with "…" and adds a warning. |
| OQ-22 | Error replies. | An error reply is not a strike. Only timeouts and crashes are strikes. |
| OQ-23 | Strike scope. | Remember counts strikes per process. |
| OQ-24 | Backfill needs a persistent process. | Remember rejects a manifest that asks for backfill without a persistent entry. |
| OQ-25 | Backfill batch size. | At most 100 memories or 4 MiB. The manifest may ask for smaller batches, never bigger. Remember sends the next batch after the reply. |
| OQ-26 | Live events during a backfill. | Live events keep flowing. The plugin keeps the newer version of a memory. |
| OQ-27 | Pause and resume. | Only Remember sends them, to the plugin. |
| OQ-28 | v1 warning codes. | `plugin_filter_failed`, `plugin_filter_timeout`, `plugin_filter_cold_start`, `plugin_chain_capped`, `plugin_disabled_crash_loop`, `hook_preflight_failed`, `hook_dispatch_failed`. New codes are additive and do not bump the version. *(Superseded 2026-10-22: the `hook_*` codes are `plugin_preflight_failed` and `plugin_dispatch_failed` in revision 6, S-01; see section 8.2.)* |
| OQ-29 | Filters that never ran. | They appear in `filters` with status `skipped` and a `reason`, for example `chain_capped` or `disabled`. |
| OQ-30 | Plugin backfill request. | A `backfill_needed` field in the `ready` data or the `ping.result` data (shape: `{applied_through_seq}`, #599). Remember always opens the connections. |

### 0.2 Envelope and JSON decisions (locked by Itay, 2026-10-18)

These override JS where they differ.

- E1. One envelope on every surface: `{format, version, type, request_id?, data?, error?, warnings?, next_cursor?, has_more?}`. **The envelope has no `id` field.** The old field `body` is now `data`.
- E2. `format`, `version` and `type` are required on every message.
- E3. Three format names, each with its own version: `remember-plugin-message` version 1 (plugins, hooks, Cloud webhooks), `remember-cli-message` version 2 (CLI), `remember-api-message` version 1 (HTTP and MCP) **(Q-B)**. Superseded in revision 6 by `[decision:s-07]`: one format, `remember-message` version 1, on every surface (log 008 JE-1.2).
- E4. A response type is `<request type>.result`, for example `memory.search.result`, `filter.search.query.result`, `ping.result`. A failed reply keeps that type and carries `error` **(Q-A)**.
- E5. Domain ids live in `data`. `get`, `add` and `update` return `data.memory` (one memory). Lists return `data.entries`, and each item has its own `id`. An event is type `event` with `data` `{event_id, event_type, …, memory}`.
- E6. Requests and replies match only by `request_id`. A request that expects a reply carries a `request_id`, and the reply echoes it.
- E7. Paging: `next_cursor` and `has_more` sit in the envelope, only on list responses. `count` and `entries` stay in `data`.
- J1. A success carries `data`. A failure carries `error`. Never both, and neither is ever `null`.
- J2. `warnings` appears only when it is not empty (schema: optional, `minItems: 1`).
- J3. Unknown fields: an error in `hooks.json` and in the `run` and `permissions` sections of `plugin.json`. A warning `unknown_field` everywhere else.
- J4. Ids have no type prefix. Ids are hyphenated UUIDs only.
- J5. An object's own id is `id`. A reference to another object is `<noun>_id`, for example `memory_id`, `event_id`.
- J6. The memory UUID is `id` everywhere, in JSON and in SQLite. The integer row id is `local_id`. `local_id` is CLI-only and never appears in the API, MCP, events or plugin messages.
- J7. With `--json`, warnings go only into `warnings` on stdout.
- J8. Hooks read the memory id from `data.memory.id` and from the environment variable `REMEMBER_ID`. No `REMEMBER_SYNC_ID` exists.
- J9. Before 1.0, breaking changes are clean breaks with a changelog note. There are no aliases and no fallbacks.

### 0.3 Decisions on the revision 1 questions (locked by Itay, 2026-10-18)

| NQ | Decision | Summary |
|---|---|---|
| NQ-1 | CLI JSON version 2. | A clean break, with no fallback. |
| NQ-2 | Action warning codes. | Add `plugin_action_timeout` and `plugin_action_failed`. |
| NQ-3 | Who returns exit code 4 for a missed action ack. Amended by `[decision:plugin-cli-table]` (#602). | Only a command whose own job waits for the ack, for example a backfill (`remember plugin reindex`). `remember plugin status` reports a missed ack in `last_problems` and exits 0. A write records the miss in the plugin log and in plugin status. The miss is still a strike. |
| NQ-4 | Informational warnings. | `plugin_additions_capped` and `plugin_note_truncated` are warnings with exit code 0. |
| NQ-5 | `request_id`. | Requests carry it, and replies echo it. |
| NQ-6 | Time range field. | `time_range.field` is `updated_at` or `created_at`. The default is `updated_at`. |
| NQ-7 | `backfill_needed` shape. | Two fields. `index_version` (`data_version` plus model): a mismatch means a full rebuild. `applied_through` (a version vector): an incremental catch-up. Revision 4: `index_version` moved to the top level of `ready` and `ping.result` data, and `backfill_needed` keeps only `applied_through` (`[decision:protocol-index-version-report]`, section 0.4). Revision 5: `applied_through` is replaced by `applied_through_seq`, an integer `change_seq` (`[decision:protocol-backfill-seq]` #599). |
| NQ-8 | Version fields for backfill plugins. Amended by #599 and `[decision:change-seq]` (#543). | They always get `updated_at` and `change_seq`. No version vector appears in the plugin protocol. |
| NQ-9 | Disabled filters in `filters`. | A filter that the user turned off is not listed. A filter that a crash loop disabled is listed as `skipped`. |

### 0.4 Decisions on the revision 2 and revision 3 questions (locked by Itay, 2026-10-18)

| Item | Decision | Summary |
|---|---|---|
| Q-A | Type of a failed reply. | A failed reply keeps its `<request type>.result` type and carries `error`. No message type `error` exists. |
| Q-B | Format names. Superseded in revision 6 by `[decision:s-07]`. | Three names, each versioned separately: `remember-cli-message` v2, `remember-api-message` v1, `remember-plugin-message` v1. Revision 6: one format, `remember-message` v1. |
| Q-C | Export files. | Plain documents with their own `format` and `version` (`remember-export`, `remember-settings-export`), not envelopes. `plugin.json` is a plain document too. |
| Q-D | `REMEMBER_TEST_FIXED_IDS=1`. | It works only in test builds. A release build ignores it. |
| NQ-10 | `unknown_field` from a plugin. | The warning goes only to the plugin log and to `remember plugin status`, never into command output. |
| NQ-11 | Malformed and oversize output. | Each counts as a strike, the same as a crash. Only a well-formed failed reply is exempt. |
| NQ-12 | A request over 16 MiB. | Remember trims the candidate pool from the bottom (the lowest BM25 candidates go first) until the request fits, and warns `plugin_request_trimmed`. |
| NQ-13 | Cloud webhooks. | They use the same `event` envelope (revision 6: `remember-message` v1). |
| NQ-14 | Budget scope. | The time budget is per filter entry. Every entry counts toward the chain cap. |
| NQ-15 | Format name for hooks. | Settled by Q-B; revision 6: hooks use `remember-message` (S-07). |
| NQ-16 | Who drives a backfill. | The CLI command or app that starts a backfill runs it in the foreground, under a lock. It saves progress and resumes from the last answered batch on a later run. "No daemon in v1" is superseded by `[decision:remember-daemon]` (#517) and `[decision:remember-daemon-scope]` (#518): when the optional daemon runs, it runs backfills; when it does not, the CLI does (R7.5a). |
| CF-5 | Command name. | Keep `remember plugin reindex`. No new command. |

Decisions on the revision 3 open items (locked by Itay, 2026-10-18). The Remember DB id is the id of the decision memory in Itay's Remember DB.

| Item | Remember DB id | Decision | Summary |
|---|---|---|---|
| NQ-17 | 510 | Warnings in plugin replies. | Remember writes the `warnings` of a plugin reply only to the plugin log (the shared hooks log in `<brain>.d/`, R9.28) and to `remember plugin status`. They never go into the `warnings` of a CLI or API response (R9.21). |
| SET-2 | 509 | The chain cap covers everything. Resolves CF-8. | The filter chain cap covers every filter entry, including a cold filter's start-up grace. On a cold filter's first search, Remember falls back to the BM25 results, and the filter keeps loading (R9.2, R9.4, R9.6). The exit code is 0 (4 with `--strict`): "with code 4" is superseded in part by #516. |
| CF-8 | 509 | Cold-start grace and the chain cap. | Resolved by SET-2: the grace is inside the cap, not outside it. |
| CF-7 | 511 | Per-entry budget keys. | Each search filter entry's budget setting key is `plugin.<name>.<entry>.budget`. A filter entry name must not equal one of that plugin's setting keys. Remember rejects such a manifest (R2.11). |
| CF-9 | 512 | Exit code of `plugin_request_trimmed`. | Exit code 0 (R5.12, section 8.2). |
| CF-10 | 513 | A plugin backfill request seen during a search or write. | Remember only marks the backfill as pending. The daemon runs it when it runs (#518). Otherwise the next `remember plugin reindex`, `remember plugin enable`, or launch of the Fortkeeps app runs it. `remember plugin status` shows it as pending (R7.3). |
| CF-6 | 514 | The `index_version` comparison. | Remember stores the plugin's `index_version` after each full backfill. A different `index_version` in a later `ready` or `ping.result` reply marks the index `stale`, and a rebuild is due under the normal desktop and phone rebuild rules (R6.13, R7.5). |
| `decision:protocol-index-version-report` | 515 | The plugin reports its `index_version`. | A persistent plugin reports its current `index_version` (`data_version` plus `model`, `model: null` when it has none) at the top level of the data of every `ready` and every `ping.result`. After a completed full backfill, Remember stores the reported value. A later different value marks the index `stale`. `backfill_needed` keeps only `applied_through` (R6.13, R6.14, R7.5). Revision 5: now `applied_through_seq` (#599). |

### 0.5 Open items

None remain in revision 6. Revisions 4 and 5 had none. The Interface review of revision 4 (PP-01 to PP-18) is decided in section 0.6.

### 0.6 Decisions of revision 5 (locked by Itay, 2026-10-18 and 2026-10-19)

| Item | Remember DB id | Decision | Summary |
|---|---|---|---|
| PP-01 | 516 | `[decision:plugins-degraded-exit-code]` | A search or write that a plugin or hook degrades exits 0 and reports the warning. With `--strict` or `REMEMBER_STRICT=1`, strict warnings give exit code 4. Exit 4 by default stays only where a command's own job failed in part (`plugin reindex`, `plugin enable` backfill) (R8.13–R8.16). |
| PP-02, FX-30 | 574 | [decision:strict-warnings] | Superseded by G2-03 and S-09 (section 0.7): under --strict or REMEMBER_STRICT=1, every warning gives exit code 4, whatever its code. No warning is exempt: that includes plugin_preflight_failed and plugin_dispatch_failed (formerly hook_*), raw_query_ignored, deprecated_flag, flag_meaning_changed, plugin_request_trimmed, plugin_additions_capped and unknown_field in command output. Warnings in a plugin reply never reach command output, so they never change the exit code (R9.21). |
| PP-03 | 600 | `[decision:plugin-cold-start-warmup]` | Remember warms persistent filter processes ahead of time (R9.6a). R9.2 states that a cold filter gets at most the chain cap. `plugin status` shows `cold_start_ms`. The manifest may set `idle_timeout` (R2.20). |
| PP-04 | 601 | `[decision:plugin-minimal-example]` | Section 2.4 "Minimal plugin": a hello persistent filter and a once-process action. Both are conformance fixtures. |
| PP-05 | 602 | `[decision:plugin-cli-table]` | Section 10 "Plugin CLI" with one command table and a fixed `plugin status --json` shape. `plugin status` always exits 0 when it can report. |
| PP-06 | 603 | `[decision:plugin-dev-mode]` | `remember plugin add --dev <dir>` (R2.19). |
| PP-07 | 604 | `[decision:plugin-protocol-small-fixes]` | `backfill.done`, `backfill.pause`, `backfill.resume`, reply `backfill.done.result`. |
| PP-08 | 604 | (same) | `event.result` has no `result` field. Success is `data {event_id}`. Failure is `error {code, message, fix}`. A well-formed failure is not a strike (R3.18, R3.22). |
| PP-09 | 605 | `[decision:plugin-version-glossary]` | Section 2.1a glossary. R6.2a: a `ready.protocol` that differs from the manifest disables the plugin with `disabled_reason: protocol_mismatch`. |
| PP-10 | 606 | `[decision:plugin-manifest-errors]` | `plugin_manifest_invalid` carries `detail {path, rule}` and exact message and fix text. `plugin add` and `plugin test` report every manifest error at once (R2.5a). |
| PP-11 | 607 | `[decision:plugin-permission-prompt]` | `plugin enable` shows `permissions.needs` for every plugin, built-ins included. A new folder hash shows only the changed needs (R2.3a). |
| PP-12 | 604 | `[decision:plugin-protocol-small-fixes]` | Persistent process stderr goes to `<brain>.d/plugins/<name>/stderr.log` (R6.15). |
| PP-13 | 604 | (same) | Remember sends `shutdown` with reason `idle` after `idle_timeout`. Plugins need no timer. `REMEMBER_PLUGIN_IDLE_TIMEOUT` is information only (R6.1, R6.9). |
| PP-14 | 604 | (same) | Every request and event carries `data.entry` as the routing key. It replaces `data.filter` and `data.plugin.action` (R6.4). |
| PP-15 | 608 | `[decision:plugin-provenance-human]` | Keep the JSON, HTTP and MCP provenance shape. Human search prints no extra line when every filter applied. `remember search --explain` prints the filter table. One cold-start line (R8.12). |
| PP-16 | 604 | `[decision:plugin-protocol-small-fixes]` | `<plugin>/<entry>` names the same entry as the settings prefix `plugin.<plugin>.<entry>` (R8.8). |
| PP-17 | 575 | `[decision:search-paging-fields]` | Search is a list response and always carries `next_cursor` and `has_more` (`null` and `false` when complete) (R8.3). |
| PP-18 | 604 | `[decision:plugin-protocol-small-fixes]` | One shared hooks log, documented in section 9.5. Authors use `remember plugin log` (R9.28). |
| backfill seq | 599 | `[decision:protocol-backfill-seq]` | `backfill_needed` is `{applied_through_seq}`. `0` asks for every memory. A `catch_up` backfill names `since_seq`. No version vectors appear anywhere in the plugin protocol (R6.14, R7.4). |
| change_seq | 543 | `[decision:change-seq]` | One monotonic per-brain `change_seq`. Plugins read changes since a seq and never get version vectors. |
| capped warning | 597 | `[decision:warning-time-budget-capped]` | Superseded by S-06 (2026-10-22, M-22 A, `decision:2026-10-22-M-22`): no `time_budget_capped` warning. A budget above the time left in the chain cap is clipped silently (R2.13). |
| sidecar folder | 566 | `[decision:brain-sidecar-dir]` | Every per-brain file lives in `<brain>.d/`. `data_dir` is `<brain>.d/plugins/<name>/` (R3.9). |
| event origin | 577 | `[decision:hook-origins-filter]` | Every event carries `cause.origin` (`local`, `sync` or `import`) and the actor (R3.5a). |
| actors | 563 | `[decision:provenance-actors]` | Memories carry `created_by` and `updated_by` actor objects `{kind, id}` (section 1.3). |
| daemon | 517, 518 | `[decision:remember-daemon]`, `[decision:remember-daemon-scope]` | The optional daemon owns warm filter processes, backfills and timed sync. Without it, the CLI does these jobs (R6.9, R7.3, R7.5a, R9.6a). |
| built-ins on phones | 567 | `[decision:builtin-plugins-phone]` | Built-in plugins are C-ABI libraries with one entry `plugin_handle(msg_json) -> reply_json`, called on a worker thread (R9.29). |
| enabled default | 596 | `[decision:plugin-enabled-default]` | `plugin.<name>.enabled` defaults to `false` for every plugin. `plugin add` writes `true` after the trust confirmation, source `human` (R2.15). |
| no fortkeeps namespace | 571 | `[decision:no-fortkeeps-settings-namespace]` | No `fortkeeps.*` settings exist. `needs` names only `remember.*` global keys (R2.16). |

### 0.7 Decisions of revision 6 (locked by Itay, 2026-10-21 and 2026-10-22)

Revision 6 applies the amendment table of log 009 (Design) and is still protocol 1, because no plugin shipped before it (log 009 P-Q4, PR-5). Protocol 1 is frozen after this revision. Where a row of sections 0.1 to 0.6 differs, this table wins.

| Item | Decision | Summary |
|---|---|---|
| S-07 | `[decision:s-07]` | One format, `remember-message` version 1, on every message (R1.3, section 1.2, R8.1). |
| G2-03, S-09 | `[decision:g2-03]`, `[decision:s-09]` | Under `--strict` or `REMEMBER_STRICT=1`, any warning gives exit code 4. Exit code 4 has no other meaning. A backfill that does not finish exits 1 with `plugin_backfill_incomplete` (R8.13–R8.15, section 8.2). |
| S-05 | `[decision:s-05]` | Trust belongs to the code hash. Confirmation at `plugin add`, an update, `hook add` and `plugin trust`, through the host confirm callback. `plugin enable` asks nothing (R2.3, R2.3a, R2.15). |
| S-06 | `[decision:s-06]` | `plugin.<name>.enabled` is the only generated key. Budgets, cold-start grace and priority come from the manifest only (R2.11, R9.1–R9.3). `time_budget_capped` is removed (2026-10-22, M-22 A, `decision:2026-10-22-M-22`): a budget above the time left in the chain cap is clipped silently (R2.13). `cold_start_grace` is 1 ms to 30 s, default 10 s (2026-10-22, M-24 A, `decision:2026-10-22-M-24`). |
| G-11, G2-10 | `[decision:g-11]`, `[decision:g2-10]` | The action `filter` block, with `origins` (default `["local"]`) and the actor filters (R2.21, R3.5a). |
| G-33 | `[decision:g-33]` | `source` is not a memory field (R2.16, R3.7). |
| S-10 | `[decision:s-10]` | Six event types. A link change is a `memory.updated` on each end (R3.3, R3.16). |
| S-01 | `[decision:s-01]` | Every hook is a plugin. Every event carries real `plugin`, `entry`, `data_dir` and `settings` (R3.6). |
| G-28 | `[decision:g-28]` | `data_dir` is `<brain>.d/plugins/<name>/data/` (R3.9, R6.15). |
| S-15 | `[decision:s-15]` | No exit-75 retry. `remember plugin rerun` reruns a failed run (R3.14). |
| G2-06 | `[decision:g2-06]` | A persistent process passes the cause on each write (R6.1, R3.23). |
| daemon | `[decision:2026-10-22-daemon-v1]` (738) | The daemon branches stay; S-11 is reversed (R6.9, R7.3, R7.5a, R9.6a). |
| settings read | `[decision:2026-10-22-plugin-settings-read]` (740) | Settings are read-only for plugins (R2.16a, R3.10). |
| G-09 | `[decision:g-09]` | A catch-up sends purge tombstones (R7.4). |
| S-08 | `[decision:s-08]` | Search has no `next_cursor` and no `has_more`; it carries `total` and the `keys` and `tags` facets (R8.3). |
| S-02 | `[decision:s-02]` | A crash loop is runtime state in `index.json`, not a setting (R9.13). |
| G-05, G-35 | `[decision:g-05]`, `[decision:g-35]` | The section 10 command table; `disabled_reason` adds `untrusted`. |
| G-06 | `[decision:g-06]` | `$id` is `https://itayavtalyon.github.io/remember/schema/plugin/v1/<file>.json`. |
| M-17 | `decision:2026-10-22-M-17` (757) | One name rule for plugins and entries: `^[a-z][a-z0-9_]{0,39}$`. A hyphen in a manifest is `plugin_manifest_invalid`, with a fix that suggests the `_` form (R2.22). |
| M-19 | `decision:2026-10-22-M-19` (759) | `plugin enable|disable` are shortcuts for `settings set plugin.<name>.enabled` (N2). `plugin_chain_halted` is `plugin_loop_halted` (N6). `plugin rerun` or `plugin status --ack NAME` clears the halt warning (N7). |
| warning codes | Interface review R2 (S-01) | `hook_preflight_failed` and `hook_dispatch_failed` are `plugin_preflight_failed` and `plugin_dispatch_failed`. Section 8.2 adds `plugin_loop_halted`, `plugin_untrusted` and `plugin_dev_mode`. No `hook_` code remains. |
| M-22 | `decision:2026-10-22-M-22` (763) | No `time_budget_capped` warning, as S-06 says. A budget above the time left in the chain cap is clipped silently; the clipped value is the `budget_ms` Remember sends (R2.13, section 8.2). Dated 2026-10-22. |
| M-24 | `decision:2026-10-22-M-24` (765) | `cold_start_grace` defaults to 10 s, with 1 ms minimum and 30 s maximum; `plugin status` `entries[]` shows it (R2.13, R9.2, R9.3, section 10). Dated 2026-10-22. |

### 0.8 Decisions of revision 7 (Itay, 2026-10-25)

Revision 7 is a document revision of protocol 1. No wire shape changes (log 009 P-Q4).

| Item | Decision | Summary |
|---|---|---|
| C-20 | copy-pending answer 5 | A hash mismatch is its own warning, `plugin_untrusted`, not a `plugin_preflight_failed` reason (R8.15, section 8.2, log 010 H-6.2). |
| D-02 | copy-pending answer 2 | A capped chain gives both the `chain_capped` status (R8.9) and the `plugin_chain_capped` warning. |
| D-08 | copy-pending answer 4 | The non-timeout filter failure code is exactly `plugin_filter_failed`. |
| C-30, C-31 | Itay, 2026-10-25 | `plugin_action_timeout` and `plugin_action_failed` print only in a command that runs the action synchronously (for example `plugin run`). A write never waits (R8.16, log 010 H-6.4). |
| C-23 | copy-pending answer 13 | In a `REMEMBER_PROCESSES=0` build, a write that matches a folder-plugin action is saved and warns `plugin_unsupported` (log 010 H-0.7). |

---

## 1. Envelope and framing

### 1.1 Rules

- R1.1. Every message is one JSON object, encoded as UTF-8 without a BOM (JS §1.15).
- R1.2. A sender must not send invalid UTF-8. Remember treats invalid UTF-8 from a plugin as malformed output (section 9.3).
- R1.3. Every message on every transport uses the envelope of section 1.2. This applies to the socket, to stdin, to the `ready` line on stdout, and to Cloud webhooks. A receiver parses a message the same way on every transport **(OQ-4, E1)**. Every message has `format` `remember-message` and `version` 1 `[decision:s-07]`.
- R1.4. A once-process and a hook receive exactly one envelope of type `event` on stdin, followed by `\n`. Then Remember closes stdin. The event is in `data` (section 3) **(OQ-4, E5)**.
- R1.5. A persistent process speaks JSON Lines over a local stream socket. Each message is one line that ends with `\n`. A message must not contain a raw newline. JSON escapes `\n` inside strings (JS §1.17).
- R1.6. A persistent process writes exactly one `ready` line to stdout after it listens on the socket **(OQ-7)**. All other messages travel on the socket.
- R1.7. Remember always opens the connection. One connection carries one request and its one reply **(OQ-8, OQ-30)**. A backfill stream is the one exception (section 7).
- R1.8. A request that expects a reply carries a new `request_id` (UUID v7) from its sender. The reply echoes it. Replies and requests match only by `request_id` **(E6, NQ-5)**. A message that gets no reply (`ready`, `shutdown`, `backfill.pause`, `backfill.resume`) has no `request_id`. An `event` on stdin carries a `request_id` too, so that the dispatcher can log the delivery, but it gets no reply.
- R1.9. The envelope has no `id`. Domain ids live in `data`, for example `data.memory.id`, `data.event_id`, `data.backfill_id` **(E1, E5)**.
- R1.10. A reply carries `data` on success and `error` on failure, never both. A request or a notification always carries `data` (an empty object when there is nothing to send) **(J1)**.
- R1.11. A failed reply keeps the reply type of its request (`<request type>.result`), echoes the `request_id`, and carries `error` in place of `data`. No message type `error` exists **(Q-A)**. When the plugin cannot read a request at all, it closes the connection without a reply. Remember counts that as a strike (log 009 P-Q5).
- R1.12. `warnings` appears only when it is not empty **(J2)**. `next_cursor` and `has_more` never appear on plugin messages, because v1 has no list replies **(E7)**.
- R1.13. A socket message must not exceed 16 MiB. The `data` of an `event` must not exceed 256 KiB (HK §2.2) **(OQ-6)**.
- R1.14. A plugin message over its limit is a plugin failure and a strike. Remember drops the message, passes the input through unchanged, and warns. The exit code is 0, or 4 with `--strict` (#516, #574) **(OQ-6, NQ-11)**. Remember keeps its own messages within the limits: HK §2.2 truncation for events, and trimming of the candidate pool for `filter.search.results` (R5.12) **(NQ-12)**.
- R1.15. All instants are RFC 3339 UTC strings with 3 fraction digits and `Z` (JS §1.3).
- R1.16. All ids are hyphenated UUID strings with no type prefix. Writers emit lower case. Readers accept upper or lower case and normalize to lower case. Remember mints UUID v7 ids (JS §1.2) **(J4)**. In a test build only, `REMEMBER_TEST_FIXED_IDS=1` makes Remember mint a fixed id sequence. A release build ignores it **(Q-D)**.
- R1.17. An object's own id is `id`. A reference to another object is `<noun>_id` **(J5)**. The memory UUID is `id` **(J6)**.
- R1.18. A receiver ignores an unknown field and reports it with the warning `unknown_field` (section 9.4) **(J3)**.
- R1.19. Writers emit the envelope keys in this order: `format`, `version`, `type`, `request_id`, `data` or `error`, `next_cursor`, `has_more`, `warnings` (JS §1.8).

### 1.2 Envelope

| Field | Required | Rule |
|---|---|---|
| `format` | yes | `"remember-message"` `[decision:s-07]` (log 008 JE-1.2). |
| `version` | yes | The protocol version, an integer. `1` in v1 **(OQ-5)**. |
| `type` | yes | The message type. The table below lists the types. A reply type is `<request type>.result` **(E4)**. |
| `request_id` | on requests that expect a reply, and on their replies | R1.8 **(E6)**. |
| `data` | on every message except a failed reply | The payload of the type. Always an object. |
| `error` | only on a failed reply, which keeps its `.result` type (Q-A) | The JS §1.8 error object `{code, message, fix, retryable, detail?, docs?}`. |
| `warnings` | only when not empty | `{code, message, detail?}` items. Warnings in a plugin reply go only to the plugin log and `remember plugin status`, never into a CLI or API response **(NQ-17)**. |
| `next_cursor`, `has_more` | never on plugin messages | E7. |

Message types:

| Type | From | Transport | `request_id` | Reply | Section |
|---|---|---|---|---|---|
| `ready` | plugin | stdout | no | none | 6 |
| `ping` | Remember | socket | yes | `ping.result` | 6 |
| `ping.result` | plugin | socket | echoed | (reply) | 6 |
| `shutdown` | Remember | socket | no | none; the plugin closes | 6 |
| `event` | Remember | stdin or socket | yes | `event.result` on the socket; none on stdin | 3 |
| `event.result` | plugin | socket | echoed | (reply) | 3.5 |
| `filter.search.query` | Remember | socket | yes | `filter.search.query.result` | 4 |
| `filter.search.query.result` | plugin | socket | echoed | (reply) | 4 |
| `filter.search.results` | Remember | socket | yes | `filter.search.results.result` | 5 |
| `filter.search.results.result` | plugin | socket | echoed | (reply) | 5 |
| `backfill` | Remember | socket (stream) | yes | `backfill.result` | 7 |
| `backfill.result` | plugin | socket (stream) | echoed | (reply) | 7 |
| `backfill.done` | Remember | socket (stream) | yes | `backfill.done.result` | 7 |
| `backfill.done.result` | plugin | socket (stream) | echoed | (reply) | 7 |
| `backfill.pause` | Remember | socket (stream) | no | none | 7 |
| `backfill.resume` | Remember | socket (stream) | no | none | 7 |

JS §2 leaves the list of plugin message types to this spec. This table is the list. Every reply type may carry `error` in place of `data` (R1.11).

### 1.3 Schemas

Shared definitions (`common.json`). The other schemas refer to them as `common.json#/$defs/...`.

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://itayavtalyon.github.io/remember/schema/plugin/v1/common.json",
  "title": "Shared definitions for the Remember plugin protocol v1",
  "$defs": {
    "uuid": {"type": "string", "pattern": "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"},
    "timestamp": {"type": "string", "pattern": "^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}\\.\\d{3}Z$"},
    "nullable_timestamp": {"oneOf": [{"$ref": "#/$defs/timestamp"}, {"type": "null"}]},
    "plugin_name": {"type": "string", "pattern": "^[a-z][a-z0-9_]{0,39}$"},
    "entry_name": {"type": "string", "pattern": "^[a-z][a-z0-9_]{0,39}$", "description": "The same rule as plugin_name (M-17, R2.22)."},
    "duration": {"type": "string", "pattern": "^\\d+(ms|s|m|h)$"},
    "size": {"type": "string", "pattern": "^\\d+(B|KiB|MiB)$"},
    "setting_key": {"type": "string", "maxLength": 96, "pattern": "^(remember|plugin)\\.[a-z][a-z0-9_]{0,39}(\\.[a-z][a-z0-9_]{0,39}){1,3}$", "description": "Log 007 ST-1.1, M-17 (2026-10-22)."},
    "global_setting_key": {"type": "string", "maxLength": 96, "pattern": "^remember\\.[a-z][a-z0-9_]{0,39}(\\.[a-z][a-z0-9_]{0,39}){1,2}$", "description": "No fortkeeps.* namespace exists (#571). Log 007 ST-1.1, M-17 (2026-10-22)."},
    "setting_value": {
      "type": "object",
      "required": ["key", "value"],
      "properties": {
        "key": {"$ref": "#/$defs/setting_key"},
        "value": {},
        "value_ms": {"type": "integer", "minimum": 0, "description": "Twin of a duration value (JS §1.4)."},
        "value_bytes": {"type": "integer", "minimum": 0, "description": "Twin of a size value (JS §1.5)."}
      }
    },
    "settings": {"type": "array", "items": {"$ref": "#/$defs/setting_value"}},
    "bin": {"enum": ["live", "expired", "deleted", "purged"]},
    "version_vector": {"type": "object", "additionalProperties": {"type": "integer", "minimum": 0}, "description": "Map from device_id to a counter. The one allowed data-as-key map (JS §1.1). Only in CLI and API responses, never in a plugin message (#599)."},
    "change_seq": {"type": "integer", "minimum": 1, "description": "The per-brain change_seq of the memory's last change (#543)."},
    "actor": {
      "type": "object",
      "required": ["kind", "id"],
      "properties": {
        "kind": {"enum": ["human", "agent", "tool", "share", "sync", "import"]},
        "id": {"type": ["string", "null"], "description": "Token, member or device id, or null (#563)."}
      }
    },
    "link": {
      "type": "object",
      "required": ["id"],
      "properties": {
        "id": {"$ref": "#/$defs/uuid", "description": "The linked memory. A link stub is a short copy of that memory, so it uses id (HK §2.1)."},
        "key": {"type": ["string", "null"]},
        "kind": {"type": "string"},
        "bin": {"$ref": "#/$defs/bin"}
      }
    },
    "memory_fields": {
      "type": "object",
      "required": ["id"],
      "properties": {
        "id": {"$ref": "#/$defs/uuid"},
        "key": {"type": ["string", "null"]},
        "body": {"type": "string"},
        "tags": {"type": "array", "items": {"type": "string"}},
        "created_at": {"$ref": "#/$defs/timestamp"},
        "updated_at": {"$ref": "#/$defs/timestamp"},
        "expires_at": {"$ref": "#/$defs/nullable_timestamp"},
        "deleted_at": {"$ref": "#/$defs/nullable_timestamp"},
        "bin": {"$ref": "#/$defs/bin"},
        "version_vector": {"$ref": "#/$defs/version_vector"},
        "change_seq": {"$ref": "#/$defs/change_seq"},
        "created_by": {"$ref": "#/$defs/actor"},
        "updated_by": {"$ref": "#/$defs/actor"},
        "links": {"type": "array", "items": {"$ref": "#/$defs/link"}}
      }
    },
    "memory": {
      "description": "A memory in an event or a plugin message. Never carries local_id (J6) or a version vector (#599).",
      "allOf": [{"$ref": "#/$defs/memory_fields"}],
      "properties": {"local_id": false, "version_vector": false}
    },
    "query": {
      "type": "object",
      "required": ["text", "tags", "bins", "time_range"],
      "properties": {
        "text": {"type": "string", "minLength": 1, "pattern": "\\S"},
        "tags": {"type": "array", "items": {"type": "string"}, "description": "Tag filter, with the meaning of --tag."},
        "bins": {"type": "array", "minItems": 1, "items": {"enum": ["live", "expired", "deleted"]}},
        "time_range": {
          "type": "object",
          "required": ["field", "after", "before"],
          "properties": {
            "field": {"enum": ["updated_at", "created_at"], "default": "updated_at", "description": "NQ-6."},
            "after": {"$ref": "#/$defs/nullable_timestamp"},
            "before": {"$ref": "#/$defs/nullable_timestamp"}
          }
        }
      }
    },
    "filter_score": {
      "type": "object",
      "required": ["filter", "score"],
      "properties": {"filter": {"type": "string", "description": "<plugin>/<filter>"}, "score": {"type": "number"}}
    },
    "error": {
      "type": "object",
      "required": ["code", "message", "retryable"],
      "properties": {
        "code": {"type": "string", "pattern": "^[a-z][a-z0-9_]*$"},
        "message": {"type": "string", "minLength": 1},
        "fix": {"type": "string"},
        "retryable": {"type": "boolean"},
        "detail": {"type": "object"},
        "docs": {"type": "string"}
      },
      "if": {"properties": {"code": {"not": {"const": "internal"}}}},
      "then": {"required": ["fix"]}
    },
    "warning": {
      "type": "object",
      "required": ["code", "message"],
      "properties": {
        "code": {"type": "string", "pattern": "^[a-z][a-z0-9_]*$", "description": "Open list for readers (JS §1.10). Section 8.2 lists the v1 codes."},
        "message": {"type": "string", "minLength": 1},
        "detail": {"type": "object"}
      }
    },
    "warnings": {"type": "array", "minItems": 1, "items": {"$ref": "#/$defs/warning"}}
  }
}
```

Envelope (`envelope.json`). It checks the envelope rules and selects the `data` schema by `type`.

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://itayavtalyon.github.io/remember/schema/plugin/v1/envelope.json",
  "title": "remember-message envelope v1 (plugin messages)",
  "type": "object",
  "required": ["format", "version", "type"],
  "properties": {
    "format": {"const": "remember-message"},
    "version": {"const": 1},
    "type": {"anyOf": [
      {"$ref": "#/$defs/known_types"},
      {"type": "string", "pattern": "^[a-z][a-z0-9_.]*\\.result$", "description": "Failed reply to a request of an unknown type (R9.22)."}
    ]},
    "request_id": {"$ref": "common.json#/$defs/uuid"},
    "data": {"type": "object"},
    "error": {"$ref": "common.json#/$defs/error"},
    "warnings": {"$ref": "common.json#/$defs/warnings"},
    "next_cursor": false,
    "has_more": false
  },
  "oneOf": [
    {"required": ["data"], "not": {"required": ["error"]}},
    {"required": ["error"], "not": {"required": ["data"]}}
  ],
  "$defs": {
    "known_types": {"enum": [
      "ready", "ping", "ping.result", "shutdown",
      "event", "event.result",
      "filter.search.query", "filter.search.query.result",
      "filter.search.results", "filter.search.results.result",
      "backfill", "backfill.result", "backfill.done", "backfill.done.result",
      "backfill.pause", "backfill.resume"
    ]},
    "requests": {"enum": ["ping", "event", "filter.search.query", "filter.search.results", "backfill", "backfill.done"]},
    "replies": {"enum": ["ping.result", "event.result", "filter.search.query.result", "filter.search.results.result", "backfill.result", "backfill.done.result"]},
    "notifications": {"enum": ["ready", "shutdown", "backfill.pause", "backfill.resume"]}
  },
  "allOf": [
    {"if": {"properties": {"type": {"$ref": "#/$defs/requests"}}}, "then": {"required": ["request_id", "data"]}},
    {"if": {"properties": {"type": {"$ref": "#/$defs/replies"}}}, "then": {"required": ["request_id"]}},
    {"if": {"properties": {"type": {"$ref": "#/$defs/notifications"}}}, "then": {"required": ["data"], "not": {"required": ["request_id"]}}},
    {"if": {"required": ["error"]}, "then": {"required": ["request_id"], "properties": {"type": {"pattern": "\\.result$"}}}},
    {"if": {"properties": {"type": {"not": {"$ref": "#/$defs/known_types"}}}}, "then": {"required": ["error"], "properties": {"error": {"properties": {"code": {"const": "unknown_message_type"}}}}}},
    {"if": {"properties": {"type": {"const": "ready"}}}, "then": {"properties": {"data": {"$ref": "control.json#/$defs/ready"}}}},
    {"if": {"properties": {"type": {"const": "ping"}}}, "then": {"properties": {"data": {"$ref": "control.json#/$defs/ping"}}}},
    {"if": {"properties": {"type": {"const": "ping.result"}}}, "then": {"properties": {"data": {"$ref": "control.json#/$defs/ping_result"}}}},
    {"if": {"properties": {"type": {"const": "shutdown"}}}, "then": {"properties": {"data": {"$ref": "control.json#/$defs/shutdown"}}}},
    {"if": {"properties": {"type": {"const": "event"}}}, "then": {"properties": {"data": {"$ref": "event-data.json"}}}},
    {"if": {"properties": {"type": {"const": "event.result"}}}, "then": {"properties": {"data": {"$ref": "event-result.json"}}}},
    {"if": {"properties": {"type": {"const": "filter.search.query"}}}, "then": {"properties": {"data": {"$ref": "search-query-in.json"}}}},
    {"if": {"properties": {"type": {"const": "filter.search.query.result"}}}, "then": {"properties": {"data": {"$ref": "search-query-out.json"}}}},
    {"if": {"properties": {"type": {"const": "filter.search.results"}}}, "then": {"properties": {"data": {"$ref": "search-results-in.json"}}}},
    {"if": {"properties": {"type": {"const": "filter.search.results.result"}}}, "then": {"properties": {"data": {"$ref": "search-results-out.json"}}}},
    {"if": {"properties": {"type": {"const": "backfill"}}}, "then": {"properties": {"data": {"$ref": "backfill.json#/$defs/backfill"}}}},
    {"if": {"properties": {"type": {"const": "backfill.result"}}}, "then": {"properties": {"data": {"$ref": "backfill.json#/$defs/backfill_result"}}}},
    {"if": {"properties": {"type": {"const": "backfill.done"}}}, "then": {"properties": {"data": {"$ref": "backfill.json#/$defs/backfill_done"}}}},
    {"if": {"properties": {"type": {"const": "backfill.done.result"}}}, "then": {"properties": {"data": {"$ref": "backfill.json#/$defs/backfill_result"}}}},
    {"if": {"properties": {"type": {"const": "backfill.pause"}}}, "then": {"properties": {"data": {"$ref": "backfill.json#/$defs/backfill_pause"}}}},
    {"if": {"properties": {"type": {"const": "backfill.resume"}}}, "then": {"properties": {"data": {"$ref": "backfill.json#/$defs/backfill_resume"}}}}
  ]
}
```

### 1.4 Example

<!-- example: envelope.json -->
```json
{"format":"remember-message","version":1,"type":"ping","request_id":"01a14520-1f00-7a10-8b00-000000000001","data":{}}
```

---

## 2. Manifest: plugin.json

### 2.1 Rules

- R2.1. A plugin is a folder. The folder must contain `plugin.json` at its root `[decision:plugins-manifest]`.
- R2.2. `plugin.json` is a plain document, not a message: it has no envelope, `type` or `request_id` (JS §1.7b). It starts with `"format": "remember-plugin"` and `"version": 1`. `version` is the manifest format version. `plugin_version` is the SemVer of the plugin. `protocol` is the protocol version that the plugin speaks **(OQ-5)**.
- R2.3. The user installs the folder once per user with `remember plugin add <dir>` `[decision:plugins-install-scope]`. Trust belongs to the code hash `[decision:s-05]`. Remember asks for confirmation at `plugin add`, at an update (`plugin add` of an installed name), at `hook add` and at `plugin trust`, through the host confirm callback (log 010 H-9.4). Before the confirmation, Remember shows every `permissions.needs` item: the memory fields and the global settings `[decision:plugins-filter-needs]` **(OQ-15)**.
- R2.3a. Built-ins are trusted. `remember plugin enable` asks nothing `[decision:s-05]`. At an update (a new code hash), Remember shows only the changed needs (for example "now also wants: links"). Unchanged needs re-trust with one confirmation. The prompt says in plain words that a plugin that reads memory text can send it elsewhere, and that Remember cannot stop that. The final wording is by Story Weaver, reviewed by Interface `[decision:plugin-permission-prompt]` **(PP-11)**.
- R2.4. Remember must refuse a manifest whose `version` or `protocol` it does not support. The error code is `plugin_protocol_unsupported`. Its message names the supported protocol range, and its fix says what to do, for example `plugin.json /protocol: protocol 2 isn't supported by remember 0.3.0 (supports 1).` fix `Upgrade remember, or use a plugin build for protocol 1.` `[decision:plugin-manifest-errors]` **(PP-10)**.
- R2.5. Each action and each filter has a `run` section and a `permissions` section. Remember rejects an unknown field in either section with `plugin_manifest_invalid`, which names the field **(J3)**.
- R2.5a. Every `plugin_manifest_invalid` error has `detail {path, rule}`. `path` is a JSON Pointer into `plugin.json`. `rule` is the R-number of this spec. The message and fix follow JS §1.8, for example `plugin.json /filters/0/run: unknown field "proces".` fix `Did you mean "process"?`. `plugin add` and `plugin test` report every manifest error at once, not only the first `[decision:plugin-manifest-errors]` **(PP-10)**.
- R2.6. Remember accepts an unknown field anywhere else in the manifest. It warns with `unknown_field` at `plugin add` (JS §1.13) **(J3)**.
- R2.7. The plugin must not write into its folder at run time. The folder hash must stay valid `[decision:plugins-runtime-dir]`.
- R2.8. `run.command[0]` is a path relative to the plugin folder, or an absolute path. Remember runs the argv without a shell.
- R2.9. An action defaults to `run.process: "once"`. A filter must declare `run.process: "persistent"` `[decision:plugins-process-kinds]`.
- R2.10. Remember starts one process per plugin per brain. That process serves every persistent entry of the plugin. So all persistent entries must declare the same `run.command`. Remember rejects a manifest that breaks this rule with `plugin_manifest_invalid` **(OQ-9)**.
- R2.11. Remember turns each `settings` item into the key `plugin.<name>.<key>` `[decision:settings-scheme]`. Remember generates one key per plugin: `plugin.<name>.enabled` `[decision:s-06]`. A manifest setting key `enabled` makes the manifest invalid. `budget` is not reserved, and no entry-name collision rule exists. Each entry's budget, cold-start grace and priority come from the manifest only. No user setting overrides them. `remember settings set` on a budget or grace key (for example `plugin.hybrid.rerank.cold_start_grace`) fails with `setting_unknown` (exit 2), and the fix line is: `Budgets and cold-start grace are set by the plugin's manifest. Raise remember.plugins.chain_budget instead.` (2026-10-22, M-24 A).
- R2.12. Each setting declares its `scope`: `user`, `brain` or `both`. The default is `user` **(OQ-2)**.
- R2.13. A filter `budget` or `cold_start_grace` above the 30 s hard cap makes the manifest invalid `[decision:s-06]`. A `cold_start_grace` below 1 ms (`0ms`, `0s`) also makes the manifest invalid (2026-10-22, M-24 A, `decision:2026-10-22-M-24`). A manifest budget larger than the time left in the chain cap is clipped silently; the clipped value is the `budget_ms` Remember sends `[decision:plugins-budget-clip]`. There is no `time_budget_capped` warning (2026-10-22, M-22 A, `decision:2026-10-22-M-22`, as S-06 says; supersedes `[decision:warning-time-budget-capped]`) **(OQ-17)**.
- R2.14. `data_version` is an integer. The default is `1`. When it differs from the `data_version` of the stored `index_version` (R7.5), Remember marks the index `stale` `[decision:plugins-sidecar-data]`. Remember can do this before it starts the process.
- R2.15. Built-in plugins and Fortkeeps plugins use the same format `[decision:plugins-manifest]`. Built-in plugins ship disabled `[decision:plugins-default-enabled]`. `plugin.<name>.enabled` defaults to `false` for every plugin, built-in or added. `remember plugin add` writes `true` only after the trust confirmation, as a recorded settings write with source `human` `[decision:plugin-enabled-default]`. `plugin.<name>.enabled` is an ordinary setting with scopes `user` and `brain`. `plugin add` and `hook add` write `true` on the user layer. A successful `plugin trust` clears `disabled_reason` `untrusted` `[decision:s-05]`.
- R2.16. `permissions.needs` lists what Remember sends to the entry. An item is a memory field or a global setting key (`remember.*` only; no `fortkeeps.*` namespace exists, `[decision:no-fortkeeps-settings-namespace]`). Memory fields: `key`, `body`, `tags`, `bin`, `created_at`, `updated_at`, `expires_at`, `deleted_at`, `change_seq`, `created_by`, `updated_by`, `links`. `version_vector` is not a field a plugin can ask for `[decision:protocol-backfill-seq]`. Remember always sends `id`, so `id` is not listed **(OQ-11, OQ-15)**. `source` is not a memory field. A plugin asks for `created_by` or `updated_by` `[decision:g-33]`.
- R2.16a. Settings access is read-only `[decision:2026-10-22-plugin-settings-read]` (2026-10-22). A plugin **may** read the `remember.*` keys it lists in `permissions.needs` (for example `remember.search.max_results`, the search result size) and its own `plugin.<name>.*` keys, through `settings` in each request (R3.10). No message writes a setting. A plugin **must not** call `remember settings set`, `reset` or `import`; Remember fails such a call with `setting_from_hook` (log 007 ST-7.10). No new message or capability is needed.
- R2.17. `backfill.enabled: true` requires at least one persistent entry. Remember rejects a manifest that breaks this rule with `plugin_manifest_invalid` **(OQ-24)**.
- R2.18. `backfill.max_batch_entries` (at most 100) and `backfill.max_batch_size` (at most `4MiB`) may ask for smaller batches. Remember rejects a larger value with `plugin_manifest_invalid` **(OQ-25)**.
- R2.19. `remember plugin add --dev <dir>` installs a dev plugin. The user types the trust confirmation once. Remember re-hashes the folder at each process start instead of pinning one hash. `plugin status` and `plugin list` show `dev`. Each search that a dev plugin shaped carries the warning `plugin_dev_mode` (exit code 0, 4 with `--strict`). `--dev` requires a TTY, so an agent cannot turn it on alone. Agents iterate with `remember plugin test` (section 10) `[decision:plugin-dev-mode]` **(PP-06)**.
- R2.20. The manifest may set `idle_timeout`, a duration. It overrides `remember.plugins.idle_timeout` (default 10 min) for this plugin `[decision:plugin-cold-start-warmup]` **(PP-03)**.
- R2.21. An action may have a `filter` block: `tags.any`, `tags.all`, `match_before`, `created_by`, `updated_by` (actor kinds), `bins`, `not_caused_by`, and `origins` (`local`, `sync`, `import`; default `["local"]`). Remember evaluates it at write time (log 010 H-4.1) `[decision:g-11]`, `[decision:g2-10]`. An unknown field in `filter` makes the manifest invalid (R9.19).
- R2.22. Plugin names and entry names follow one rule, `^[a-z][a-z0-9_]{0,39}$`, the settings segment grammar (log 007 ST-1.1). A manifest setting key uses the same pattern. When a user types a name with a hyphen (for example `remember plugin enable url-expand`), Remember normalizes it to `_`. A hyphen in `plugin.json` is a `plugin_manifest_invalid` error whose fix suggests the `_` form, for example `plugin.json /name: "url-expand" isn't a valid name.` fix `Use "url_expand".` (Itay, 2026-10-22, M-17).

### 2.1a Version fields

`[decision:plugin-version-glossary]` **(PP-09)**:

| Field | Where | Who sets it | What a change does |
|---|---|---|---|
| `version` | every document and message | the format | The manifest format version. Remember rejects an unknown value (R2.4). |
| `protocol` | manifest, `ready` | the plugin author | The wire version. Remember never sends a newer one (R9.24). A `ready.protocol` that differs from the manifest disables the plugin (R6.2a). |
| `plugin_version` | manifest; optional hint in `ready` | the plugin author | Display only. The manifest wins. |
| `data_version` | manifest; inside `index_version` | the plugin author | A change marks the index `stale` (R2.14). |
| `index_version` | `ready`, `ping.result` | the plugin | A mismatch with the stored value means a rebuild (R6.13). |

### 2.2 Schema

The JSON Schema checks the shape, including the name rule (R2.22) and the reserved setting key `enabled` (R2.11). Remember checks R2.10 and the 4 MiB limit of R2.18 in code.

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://itayavtalyon.github.io/remember/schema/plugin/v1/manifest.json",
  "title": "Remember plugin manifest (plugin.json) v1",
  "type": "object",
  "required": ["format", "version", "name", "plugin_version", "protocol"],
  "anyOf": [{"required": ["actions"]}, {"required": ["filters"]}],
  "properties": {
    "format": {"const": "remember-plugin"},
    "version": {"const": 1},
    "name": {"$ref": "common.json#/$defs/plugin_name"},
    "plugin_version": {"type": "string", "pattern": "^\\d+\\.\\d+\\.\\d+([-+][0-9A-Za-z.-]+)?$", "description": "SemVer of the plugin."},
    "protocol": {"const": 1},
    "description": {"type": "string", "maxLength": 200},
    "data_version": {"type": "integer", "minimum": 1, "default": 1},
    "idle_timeout": {"$ref": "common.json#/$defs/duration", "description": "Overrides remember.plugins.idle_timeout for this plugin (R2.20)."},
    "backfill": {"$ref": "#/$defs/backfill"},
    "actions": {"type": "array", "minItems": 1, "items": {"$ref": "#/$defs/action"}},
    "filters": {"type": "array", "minItems": 1, "items": {"$ref": "#/$defs/filter"}},
    "settings": {"type": "array", "items": {"$ref": "#/$defs/setting"}}
  },
  "if": {"properties": {"backfill": {"properties": {"enabled": {"const": true}}, "required": ["enabled"]}}, "required": ["backfill"]},
  "then": {
    "anyOf": [
      {"required": ["filters"]},
      {"required": ["actions"], "properties": {"actions": {"contains": {"properties": {"run": {"properties": {"process": {"const": "persistent"}}, "required": ["process"]}}}}}}
    ]
  },
  "$defs": {
    "command": {"type": "array", "minItems": 1, "items": {"type": "string", "minLength": 1}},
    "actor_kind": {"enum": ["human", "agent", "tool", "share", "import"]},
    "memory_field": {"enum": ["key", "body", "tags", "bin", "created_at", "updated_at", "expires_at", "deleted_at", "change_seq", "created_by", "updated_by", "links"]},
    "needs": {
      "type": "array", "uniqueItems": true,
      "items": {"anyOf": [{"$ref": "#/$defs/memory_field"}, {"$ref": "common.json#/$defs/global_setting_key"}]}
    },
    "permissions": {
      "type": "object",
      "additionalProperties": false,
      "properties": {"needs": {"$ref": "#/$defs/needs"}}
    },
    "event_type": {"enum": ["memory.created", "memory.updated", "memory.deleted", "memory.restored", "memory.purged", "memory.expired", "memory.*"]},
    "action_run": {
      "type": "object",
      "required": ["command"],
      "additionalProperties": false,
      "properties": {
        "process": {"enum": ["once", "persistent"], "default": "once"},
        "command": {"$ref": "#/$defs/command"},
        "timeout": {"$ref": "common.json#/$defs/duration", "default": "60s"}
      }
    },
    "filter_run": {
      "type": "object",
      "required": ["process", "command"],
      "additionalProperties": false,
      "properties": {
        "process": {"const": "persistent"},
        "command": {"$ref": "#/$defs/command"}
      }
    },
    "action": {
      "type": "object",
      "required": ["name", "events", "run"],
      "properties": {
        "name": {"$ref": "common.json#/$defs/entry_name"},
        "events": {"type": "array", "minItems": 1, "items": {"$ref": "#/$defs/event_type"}},
        "filter": {
          "type": "object",
          "additionalProperties": false,
          "properties": {
            "tags": {"type": "object", "properties": {"any": {"type": "array", "items": {"type": "string"}}, "all": {"type": "array", "items": {"type": "string"}}, "match_before": {"type": "boolean"}}},
            "created_by": {"type": "array", "items": {"$ref": "#/$defs/actor_kind"}},
            "updated_by": {"type": "array", "items": {"$ref": "#/$defs/actor_kind"}},
            "bins": {"type": "array", "items": {"$ref": "common.json#/$defs/bin"}},
            "not_caused_by": {"type": "array", "items": {"type": "string"}},
            "origins": {"type": "array", "items": {"enum": ["local", "sync", "import"]}, "default": ["local"], "description": "R2.21 (G-11)."}
          }
        },
        "run": {"$ref": "#/$defs/action_run"},
        "permissions": {"$ref": "#/$defs/permissions"}
      }
    },
    "filter": {
      "type": "object",
      "required": ["name", "point", "run"],
      "properties": {
        "name": {"$ref": "common.json#/$defs/entry_name"},
        "point": {"enum": ["search.query", "search.results"]},
        "priority": {"type": "integer", "minimum": 0, "maximum": 1000, "default": 500, "description": "Lower runs first."},
        "budget": {"$ref": "common.json#/$defs/duration", "default": "800ms", "description": "The entry budget; no user setting overrides it (S-06)."},
        "cold_start_grace": {"allOf": [{"$ref": "common.json#/$defs/duration"}, {"not": {"pattern": "^0+(ms|s|m|h)$"}}], "default": "10s", "description": "The cold-start grace, 1 ms to 30 s (R2.13, M-24 A, 2026-10-22); no user setting overrides it (S-06)."},
        "run": {"$ref": "#/$defs/filter_run"},
        "permissions": {"$ref": "#/$defs/permissions"}
      }
    },
    "backfill": {
      "type": "object",
      "required": ["enabled"],
      "properties": {
        "enabled": {"type": "boolean"},
        "max_batch_entries": {"type": "integer", "minimum": 1, "maximum": 100, "default": 100},
        "max_batch_size": {"$ref": "common.json#/$defs/size", "default": "4MiB", "description": "At most 4MiB (R2.18)."}
      }
    },
    "setting": {
      "type": "object",
      "required": ["key", "description", "type", "default"],
      "properties": {
        "key": {"type": "string", "pattern": "^[a-z][a-z0-9_]{0,39}$", "not": {"const": "enabled"}},
        "description": {"type": "string", "maxLength": 200},
        "type": {"enum": ["boolean", "integer", "enum", "duration", "size", "string", "string_list"]},
        "default": {},
        "range": {
          "type": "object",
          "properties": {
            "min": {}, "max": {},
            "values": {"type": "array", "items": {"type": "string"}},
            "max_items": {"type": "integer", "minimum": 1}
          }
        },
        "scope": {"enum": ["user", "brain", "both"], "default": "user"}
      }
    }
  }
}
```

### 2.3 Example (the built-in hybrid search plugin)

<!-- example: manifest.json -->
```json
{
  "format": "remember-plugin",
  "version": 1,
  "name": "hybrid",
  "plugin_version": "1.0.0",
  "protocol": 1,
  "description": "Semantic re-ranking of keyword search results.",
  "data_version": 3,
  "backfill": {"enabled": true, "max_batch_entries": 50, "max_batch_size": "2MiB"},
  "actions": [
    {
      "name": "index",
      "events": ["memory.created", "memory.updated", "memory.deleted", "memory.restored", "memory.purged", "memory.expired"],
      "run": {"process": "persistent", "command": ["bin/remember-hybrid", "--serve"], "timeout": "30s"},
      "permissions": {"needs": ["key", "body", "tags", "bin", "updated_at", "change_seq"]}
    }
  ],
  "filters": [
    {
      "name": "rerank",
      "point": "search.results",
      "priority": 500,
      "budget": "800ms",
      "run": {"process": "persistent", "command": ["bin/remember-hybrid", "--serve"]},
      "permissions": {"needs": ["remember.search.candidate_pool"]}
    }
  ],
  "settings": [
    {"key": "model", "description": "Which embedding model to use.", "type": "enum", "default": "small", "range": {"values": ["small", "base"]}, "scope": "user"},
    {"key": "min_index_coverage", "description": "Rank only when this share of memories is indexed.", "type": "integer", "default": 50, "range": {"min": 0, "max": 100}, "scope": "both"}
  ]
}
```

### 2.4 Minimal plugin

`[decision:plugin-minimal-example]` **(PP-04)**. Both examples below are conformance-suite fixtures that CI runs, so they stay true. The hello filter has no idle timer: Remember stops it with `shutdown` reason `idle` (R6.9) **(PP-13)**.

`hello/plugin.json` (a persistent filter that puts memories with more query words first):

<!-- example: manifest.json -->
```json
{"format":"remember-plugin","version":1,"name":"hello","plugin_version":"0.1.0","protocol":1,
 "filters":[{"name":"rank","point":"search.results","run":{"process":"persistent","command":["hello.py"]},"permissions":{"needs":["body"]}}]}
```

`hello/hello.py`:

```python
#!/usr/bin/env python3
import json, os, socket
path = os.environ["REMEMBER_PLUGIN_SOCKET"]
srv = socket.socket(socket.AF_UNIX); srv.bind(path); srv.listen()
msg = lambda **kw: {"format": "remember-message", "version": 1, **kw}
iv = {"data_version": 1, "model": None}
print(json.dumps(msg(type="ready", data={"protocol": 1, "pid": os.getpid(), "index_version": iv})), flush=True)
while True:
    conn, _ = srv.accept(); f = conn.makefile("rw")
    req = json.loads(f.readline()); t, d = req["type"], req.get("data", {})
    if t == "shutdown": os.unlink(path); break
    out = msg(type=t + ".result", request_id=req["request_id"])
    if t == "ping": out["data"] = {"pid": os.getpid(), "index_version": iv}
    elif t == "filter.search.results":
        words = d["query"]["text"].lower().split()
        hits = lambda c: sum(w in c.get("body", "").lower() for w in words)
        ranked = sorted(d["candidates"], key=hits, reverse=True)
        out["data"] = {"results": [{"memory_id": c["id"], "score": hits(c)} for c in ranked]}
    else: out["error"] = {"code": "unknown_message_type", "message": f"hello can't handle {t}.",
                          "fix": "Upgrade the hello plugin.", "retryable": False}
    f.write(json.dumps(out) + "\n"); f.flush(); conn.close()
```

Then `remember plugin add ./hello`, `remember plugin enable hello`, and `remember search "export paths" --json | jq .data.ranked_by` prints `"hello/rank"`. The review draft of this script had no `index_version`; R6.13 requires it, so the script sends `{"data_version": 1, "model": null}`.

`log_ids/plugin.json` (a once-process action):

<!-- example: manifest.json -->
```json
{"format":"remember-plugin","version":1,"name":"log_ids","plugin_version":"0.1.0","protocol":1,
 "actions":[{"name":"append","events":["memory.created"],"run":{"command":["append.py"]}}]}
```

`log_ids/append.py` (appends each new memory id to a file in its data folder):

```python
#!/usr/bin/env python3
import json, os, sys
msg = json.loads(sys.stdin.readline())
d = msg["data"]
with open(os.path.join(d["data_dir"], "ids.txt"), "a") as f:
    f.write(d["memory"]["id"] + "\n")
sys.exit(0)
```

---

## 3. Action and hook event input

### 3.1 Rules

- R3.1. Hooks and plugin actions receive the same message `[decision:hooks-plugins-one-mechanism]`: an envelope of type `event`. The event is the `data` of that message (HK §2.1) **(OQ-4, E5)**. The old standalone `remember-event` document is gone (JS §1.7b). The envelope `version` versions the event data too.
- R3.2. Each event carries exactly one memory in `data.memory` (HK §2.1). The memory UUID is `data.memory.id`. A hook also gets it in the environment variable `REMEMBER_ID`. No other name exists for it **(J6, J8)**.
- R3.3. `data.event_type` is one of `memory.created`, `memory.updated`, `memory.deleted`, `memory.restored`, `memory.purged`, `memory.expired` (HK §1). A link change is a `memory.updated` on each end, with `changes.link` `[decision:s-10]`.
- R3.4. `data.event_id` is unique per event. The handler must use it to remove duplicates, because delivery is at least once (HK §3.3.4). The `request_id` is new for each delivery. A handler must not use it to remove duplicates.
- R3.5. `data.cause` carries the cause chain (HK §3A). A chain item refers to its memory as `memory_id` and to its event as `event_id` **(J5)**. Remember does not run a handler when the chain length reaches 100 (HK §3.5).
- R3.5a. `data.cause.origin` is `local`, `sync` or `import`. The actor of the change is `data.memory.updated_by` (and `created_by`), the FX-4 actor objects. An action's `filter` block has `origins`, default `["local"]` (R2.21) `[decision:g-11]`. It replaces the setting `remember.hooks.sync_events`. `cause.hook` is `plugin/<name>/<entry>`. No `cause.plugin` field exists `[decision:hook-origins-filter]`, `[decision:provenance-actors]`.
- R3.6. Every field of the event data is always present (HK §2.1). Every event has real values in `plugin` (name and plugin version), `entry` (the action name, R6.4), `data_dir` and `settings`, because every hook is a plugin (log 010 H-10.3) `[decision:s-01]`.
- R3.7. A hook (the one-action plugin that `hook add` writes) receives every memory field. A plugin action receives `id`, `bin`, and the fields in its `permissions.needs` **(OQ-11)**. For a plugin with backfill enabled, Remember always adds `updated_at` and `change_seq` **(NQ-8, #599)**. No memory in a plugin or hook message carries `version_vector` `[decision:protocol-backfill-seq]`. `changes` is always present for `memory.updated`. `changes.before` holds only the fields that the receiver gets.
- R3.8. No memory in an event carries `local_id` **(J6)**.
- R3.9. `brain.path` is shortened with `~` (HK rule H10 J). `data_dir` is the absolute path `<brain>.d/plugins/<name>/data/` `[decision:g-28]` **(OQ-14)**. Remember keeps its own `index.json` and `stderr.log` in `<brain>.d/plugins/<name>/`, outside `data_dir`. Remember creates the folder before the first run. Remember never syncs, exports or backs up the folder `[decision:plugins-sidecar-data]`.
- R3.10. `settings` is a list of `{key, value}` items, sorted by key. It holds the effective values of the plugin's own `plugin.<name>.*` keys (after the precedence default, user, brain, flag `[decision:settings-scheme]`). It also holds each global setting that the entry lists in `permissions.needs` **(OQ-15)**. A setting key is a value, never a JSON key (JS §1.1). A duration value has a `value_ms` twin, and a size value has a `value_bytes` twin (JS §1.4, §1.5). Settings are read-only for the plugin (R2.16a).
- R3.11. The `data` of an event must not exceed 256 KiB. Over the limit, Remember drops `changes.before.body`, then `memory.links`, then cuts `memory.body` to 16,384 bytes, and lists the cut paths in `truncated_fields` (HK §2.2) **(OQ-6)**.
- R3.12. The plugin must not change a memory to react to an event. The plugin may write a **new** memory, for example a linked memory tagged `source:url` `[decision:url-expansion-linked-memory]`, `[principle:plugins-never-change-writes]`.
- R3.13. Remember never waits for a handler. A handler result never changes the exit code or the output of the write `[decision:hooks-never-wait]` **(NQ-3)**.
- R3.14. A once-process ends with an exit code. `0` means handled. Any other code means failed, and Remember does not retry. No exit code is retried. `remember plugin rerun` reruns a failed run `[decision:s-15]`.
- R3.15. Remember ignores the stdout of a once-process and of a hook. Only the exit code counts. Remember writes stderr to the per-run output file that the hooks log in `<brain>.d/` names (HK §6.3) **(OQ-13)**.
- R3.16. For a link change (a `memory.updated` on each end, `[decision:s-10]`), `changes` holds `{"link": {kind, direction, other{id, key}}}` (HK §1.2).

### 3.2 Schema

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://itayavtalyon.github.io/remember/schema/plugin/v1/event-data.json",
  "title": "Event data (the data of an event message), v1",
  "type": "object",
  "required": ["event_id", "event_type", "occurred_at", "detected_at", "batch_id", "cause", "brain", "memory", "changes", "truncated", "truncated_fields", "limits", "plugin", "entry", "data_dir", "settings"],
  "properties": {
    "event_id": {"$ref": "common.json#/$defs/uuid"},
    "event_type": {"enum": ["memory.created", "memory.updated", "memory.deleted", "memory.restored", "memory.purged", "memory.expired"]},
    "occurred_at": {"$ref": "common.json#/$defs/timestamp"},
    "detected_at": {"$ref": "common.json#/$defs/timestamp"},
    "batch_id": {"oneOf": [{"$ref": "common.json#/$defs/uuid"}, {"type": "null"}]},
    "cause": {
      "type": "object",
      "required": ["command", "origin", "hook", "run_id", "depth", "chain"],
      "properties": {
        "command": {"enum": ["add", "update", "delete", "purge", "rekey", "link", "unlink", "import", "sync", "sweep", "app"]},
        "origin": {"enum": ["local", "sync", "import"], "description": "R3.5a (#577)."},
        "hook": {"type": ["string", "null"], "description": "plugin/<name>/<entry>, or null (R3.5a)."},
        "plugin": false,
        "run_id": {"oneOf": [{"$ref": "common.json#/$defs/uuid"}, {"type": "null"}]},
        "depth": {"type": "integer", "minimum": 0},
        "chain": {"type": "array", "maxItems": 100, "items": {
          "type": "object", "required": ["hook", "event_id", "memory_id", "depth"],
          "properties": {
            "hook": {"type": "string"},
            "event_id": {"$ref": "common.json#/$defs/uuid"},
            "memory_id": {"$ref": "common.json#/$defs/uuid"},
            "depth": {"type": "integer", "minimum": 1}}}}
      }
    },
    "brain": {
      "type": "object", "required": ["path", "device_id"],
      "properties": {"path": {"type": "string", "description": "Shortened with ~ (OQ-14)."}, "device_id": {"$ref": "common.json#/$defs/uuid"}}
    },
    "memory": {"$ref": "common.json#/$defs/memory"},
    "changes": {"type": ["object", "null"]},
    "truncated": {"type": "boolean"},
    "truncated_fields": {"type": "array", "items": {"type": "string"}},
    "limits": {"type": "object", "required": ["max_bytes"], "properties": {"max_bytes": {"const": 262144}}},
    "plugin": {"type": "object", "required": ["name", "plugin_version"],
      "properties": {"name": {"$ref": "common.json#/$defs/plugin_name"}, "plugin_version": {"type": "string"}, "action": false}},
    "entry": {"$ref": "common.json#/$defs/entry_name", "description": "The action entry (routing key, R6.4). Every hook is a plugin (S-01)."},
    "data_dir": {"type": "string", "pattern": "^(/|[A-Za-z]:\\\\)", "description": "Absolute <brain>.d/plugins/<name>/data/ (OQ-14, G-28)."},
    "settings": {"$ref": "common.json#/$defs/settings"}
  }
}
```

### 3.3 Example: once-process on stdin (URL expansion action, `needs: ["body", "tags"]`)

<!-- example: envelope.json -->
```json
{"format":"remember-message","version":1,"type":"event","request_id":"01a14521-0b00-7000-8000-0000000000e8",
 "data":{"event_id":"01a14521-0a00-7c11-9a00-0000000000e1","event_type":"memory.created","occurred_at":"2026-10-16T14:21:03.512Z","detected_at":"2026-10-16T14:21:03.512Z","batch_id":null,
  "cause":{"command":"add","origin":"local","hook":null,"run_id":null,"depth":0,"chain":[]},
  "brain":{"path":"~/.remember/remember.db","device_id":"01a0d90a-e71c-7a0a-bb2c-3b377482fa51"},
  "memory":{"id":"01a14521-09ff-7d20-8c00-00000000aa01","body":"Read later: https://sqlite.org/fts5.html#the_bm25_function","tags":["reading"],"bin":"live"},
  "changes":null,"truncated":false,"truncated_fields":[],"limits":{"max_bytes":262144},
  "plugin":{"name":"url_expand","plugin_version":"0.4.0"},"entry":"fetch",
  "data_dir":"/Users/itay/.remember/remember.db.d/plugins/url_expand/data/",
  "settings":[{"key":"plugin.url_expand.enabled","value":true},{"key":"plugin.url_expand.max_bytes","value":"512KiB","value_bytes":524288}]}}
```

### 3.4 Example: hook on stdin (the `router` hook plugin, full memory, second hop of a chain)

<!-- example: envelope.json -->
```json
{"format":"remember-message","version":1,"type":"event","request_id":"01a14521-0c00-7000-8000-0000000000f1",
 "data":{"event_id":"01a14521-0c00-7c11-9a00-0000000000e2","event_type":"memory.updated","occurred_at":"2026-10-16T14:21:09.004Z","detected_at":"2026-10-16T14:21:09.004Z","batch_id":null,
  "cause":{"command":"update","origin":"local","hook":"plugin/router/run","run_id":"01a14521-0bf0-7000-8000-0000000000c1","depth":1,
   "chain":[{"hook":"plugin/router/run","event_id":"01a14521-0a00-7c11-9a00-0000000000e1","memory_id":"01a14521-09ff-7d20-8c00-00000000aa01","depth":1}]},
  "brain":{"path":"~/.remember/remember.db","device_id":"01a0d90a-e71c-7a0a-bb2c-3b377482fa51"},
  "memory":{"id":"01a14521-09ff-7d20-8c00-00000000aa01","key":null,"body":"Read later: https://sqlite.org/fts5.html#the_bm25_function (expanded)","tags":["reading","to:reviewer"],"created_at":"2026-10-16T14:21:03.512Z","updated_at":"2026-10-16T14:21:09.004Z","expires_at":null,"deleted_at":null,"bin":"live","change_seq":118,"created_by":{"kind":"human","id":null},"updated_by":{"kind":"agent","id":null},"links":[]},
  "changes":{"fields":["tags"],"before":{"tags":["reading"]},"tags_added":["to:reviewer"],"tags_removed":[]},
  "truncated":false,"truncated_fields":[],"limits":{"max_bytes":262144},
  "plugin":{"name":"router","plugin_version":"0.1.0"},"entry":"run","data_dir":"/Users/itay/.remember/remember.db.d/plugins/router/data/","settings":[{"key":"plugin.router.enabled","value":true}]}}
```

### 3.5 Event to a persistent action process

- R3.17. Remember sends the same `event` envelope on the socket.
- R3.18. The plugin must answer with `event.result` before the action `run.timeout`. The reply echoes `request_id`. A success carries `data {event_id}`. A failure carries `error {code, message, fix}` in place of `data` (R1.11). `event.result` has no `result` field `[decision:plugin-protocol-small-fixes]` **(OQ-12, PP-08)**.
- R3.19. A missing reply after the timeout is a `timeout`. It is one strike for the process (section 9.2) **(OQ-12, NQ-3)**.
- R3.20. A write never waits for the reply. When the reply is missing or carries `error`, Remember records it in the plugin log (the shared hooks log, R9.28) and in `remember plugin status` (`last_problems`). A write command keeps its own exit code **(NQ-3)**.
- R3.21. A command whose own job waits for the reply (for example a backfill in `remember plugin reindex`) exits 1 with `plugin_backfill_incomplete` when the job does not finish `[decision:s-09]`. Its warning is `plugin_action_timeout` for a missing reply and `plugin_action_failed` for a failed reply. `remember plugin status` never waits for a reply. It reports a missed ack in `last_problems` and exits 0 `[decision:plugin-cli-table]` **(NQ-2, NQ-3, PP-05)**.
- R3.22. A well-formed failed `event.result` is not a strike **(OQ-22, PP-08)**.
- R3.23. A persistent process gets `REMEMBER_HOOK_DEPTH=1` and `REMEMBER_HOOK_NAME=plugin/<name>` (R6.1). When it writes through the CLI for an event, it sets `REMEMBER_CAUSE_EVENT_ID` and `REMEMBER_HOOK_CHAIN` from that event's `data.cause` `[decision:g2-06]`.

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://itayavtalyon.github.io/remember/schema/plugin/v1/event-result.json",
  "title": "event.result data",
  "type": "object",
  "required": ["event_id"],
  "properties": {
    "event_id": {"$ref": "common.json#/$defs/uuid"},
    "result": false
  }
}
```

<!-- example: envelope.json -->
```json
{"format":"remember-message","version":1,"type":"event.result","request_id":"01a14521-0b00-7000-8000-0000000000e9","data":{"event_id":"01a14521-0a00-7c11-9a00-0000000000e1"}}
```

A failed `event.result` (not a strike, R3.22):

<!-- example: envelope.json -->
```json
{"format":"remember-message","version":1,"type":"event.result","request_id":"01a14521-0b00-7000-8000-0000000000ea","error":{"code":"fetch_failed","message":"The page https://sqlite.org/fts5.html did not answer in 10s.","fix":"Check the network, then run 'remember plugin log url_expand'.","retryable":true}}
```

---
## 4. Filter point search.query

### 4.1 Rules

- R4.1. Remember runs the `search.query` chain before BM25 `[decision:plugins-filter-points]`.
- R4.2. Each filter receives the query that the previous filter returned `[decision:plugins-filter-chain]`. It also receives the user's `original_query`.
- R4.3. A query is an object: `text`, `tags`, `bins` and `time_range`. `time_range.field` is `updated_at` (the default) or `created_at` **(OQ-16, NQ-6)**. A filter may rewrite the text and every filter field. It must return the whole query object. A filter may not change `key` or the result limit.
- R4.4. An empty `text` after trimming is malformed output. An empty `bins` list is malformed output.
- R4.5. When a filter fails or times out, Remember passes its input query to the next filter unchanged.
- R4.6. A filter that leaves the query unchanged on purpose returns `skipped: true` and a `note`. Remember then uses the input query and ignores any `query` in the output. This is not a failure and not a strike **(OQ-20)**.
- R4.7. The budgets are the same as for `search.results`: 800 ms warm, 10 s cold-start grace, a 30 s hard cap, and the shared 1.5 s chain cap. The chain cap covers both points together **(OQ-17)**, and it covers the cold-start grace too (R9.2) **(SET-2)**.
- R4.8. The search response shows the original query and the final rewritten query in `data.query` (section 8) **(OQ-16)**.
- R4.9. A note may have at most 120 Unicode characters. Remember truncates a longer note (section 9.3) **(OQ-21)**.

### 4.2 Schemas

Request data (`filter.search.query`):

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://itayavtalyon.github.io/remember/schema/plugin/v1/search-query-in.json",
  "title": "filter.search.query data",
  "type": "object",
  "required": ["entry", "query", "original_query", "budget_ms", "cold", "settings", "data_dir"],
  "properties": {
    "entry": {"$ref": "common.json#/$defs/entry_name", "description": "Name of the filter entry in the manifest (routing key, R6.4)."},
    "filter": false,
    "query": {"$ref": "common.json#/$defs/query", "description": "Output of the previous filter, or the user's query."},
    "original_query": {"$ref": "common.json#/$defs/query"},
    "budget_ms": {"type": "integer", "minimum": 1},
    "cold": {"type": "boolean", "description": "True on the first call after the process started."},
    "settings": {"$ref": "common.json#/$defs/settings"},
    "data_dir": {"type": "string"}
  }
}
```

Reply data (`filter.search.query.result`). The schema has no length limit on `note`, because a long note is not malformed (R9.16).

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://itayavtalyon.github.io/remember/schema/plugin/v1/search-query-out.json",
  "title": "filter.search.query.result data",
  "type": "object",
  "properties": {
    "query": {"$ref": "common.json#/$defs/query"},
    "skipped": {"type": "boolean", "default": false},
    "note": {"type": "string"}
  },
  "if": {"properties": {"skipped": {"const": true}}, "required": ["skipped"]},
  "then": {"required": ["note"]},
  "else": {"required": ["query"]}
}
```

### 4.3 Example

Request:

<!-- example: envelope.json -->
```json
{"format":"remember-message","version":1,"type":"filter.search.query","request_id":"01a14522-0000-7000-8000-000000000101",
 "data":{"entry":"expand",
  "query":{"text":"k8s ingress last week","tags":[],"bins":["live"],"time_range":{"field":"updated_at","after":null,"before":null}},
  "original_query":{"text":"k8s ingress last week","tags":[],"bins":["live"],"time_range":{"field":"updated_at","after":null,"before":null}},
  "budget_ms":800,"cold":false,
  "settings":[{"key":"plugin.synonyms.enabled","value":true}],"data_dir":"/Users/itay/.remember/remember.db.d/plugins/synonyms/data/"}}
```

Response (the filter rewrites the text and adds a time range):

<!-- example: envelope.json -->
```json
{"format":"remember-message","version":1,"type":"filter.search.query.result","request_id":"01a14522-0000-7000-8000-000000000101",
 "data":{"query":{"text":"(k8s OR kubernetes) ingress","tags":[],"bins":["live"],"time_range":{"field":"updated_at","after":"2026-10-09T00:00:00.000Z","before":null}},"note":"expanded k8s; last week as time range"}}
```

---

## 5. Filter point search.results

### 5.1 Rules

- R5.1. Search runs in three stages `[decision:plugins-filter-needs]`. Stage 1: Remember finds candidate ids and BM25 scores. Stage 2: Remember loads only the fields that the chain needs, and runs the chain. Stage 3: Remember loads the final memories by id.
- R5.2. The candidate pool is the top N BM25 ids. N is `remember.search.candidate_pool`, default 200, range 20 to 2000 (SS settings table) `[decision:plugins-candidate-pool]`.
- R5.3. Each candidate is a memory object with `id` and `bm25`. It also has the fields in the union of `permissions.needs` of all filters in the chain. It has `scores`: one `{filter, score}` item for each earlier filter in this chain that returned scores **(OQ-18)**.
- R5.4. A filter may reorder, drop or add memories. The output lists memories as `{memory_id, score}` items. A filter adds a memory by listing its id in `additions` and in `results` `[decision:plugins-filter-points]`.
- R5.5. The order of `results` is authoritative. `score` is on the plugin's own scale. Remember never sorts or compares by score. Remember passes each score through to the response, labelled by filter **(OQ-18)**.
- R5.6. Remember accepts at most `candidate_pool` additions (default 200). Remember drops the extra additions in list order and warns with `plugin_additions_capped` **(OQ-19, NQ-4)**. The request carries the cap as `max_additions`.
- R5.7. Remember loads added memories itself. Remember drops an id that does not exist or that is not in the requested bins.
- R5.8. After the chain, Remember returns at most `max_results` memories. A call can override the limit `[decision:plugins-candidate-pool]`. Search has no paging, and a search response has no `next_cursor` and no `has_more` `[decision:search-no-paging]`, `[decision:s-08]`.
- R5.9. When a filter fails or times out, Remember passes its input on unchanged. Remember warns with a code that names the filter `[decision:plugins-filter-chain]`. The exit code is 0, or 4 with `--strict` `[decision:plugins-degraded-exit-code]`, `[decision:strict-warnings]`.
- R5.10. A plugin whose index is below its own threshold returns its input unchanged on purpose. It sets `skipped: true` and a `note` `[decision:plugins-partial-index-owned-by-plugin]`. This is not a failure and not a strike. The status is `skipped` with reason `pass_through` **(OQ-20)**.
- R5.11. A note may have at most 120 Unicode characters **(OQ-21)**.
- R5.12. When a `filter.search.results` request would exceed 16 MiB, Remember removes candidates from the bottom of the pool (lowest BM25 first) until the request fits. Then it sends the request and warns `plugin_request_trimmed`, with `detail.sent` and `detail.dropped` **(NQ-12)**. The warning leaves the exit code at 0, because every filter still ran **(CF-9)**.

### 5.2 Schemas

Request data (`filter.search.results`):

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://itayavtalyon.github.io/remember/schema/plugin/v1/search-results-in.json",
  "title": "filter.search.results data",
  "type": "object",
  "required": ["entry", "query", "original_query", "candidates", "max_results", "max_additions", "budget_ms", "cold", "settings", "data_dir"],
  "properties": {
    "entry": {"$ref": "common.json#/$defs/entry_name", "description": "Routing key (R6.4)."},
    "filter": false,
    "query": {"$ref": "common.json#/$defs/query", "description": "The query after the search.query chain."},
    "original_query": {"$ref": "common.json#/$defs/query"},
    "candidates": {
      "type": "array", "maxItems": 2000,
      "items": {
        "allOf": [{"$ref": "common.json#/$defs/memory"}],
        "required": ["id", "bm25", "scores"],
        "properties": {
          "bm25": {"type": "number"},
          "scores": {"type": "array", "items": {"$ref": "common.json#/$defs/filter_score"}, "description": "Scores of earlier filters, labelled by filter."}
        }
      }
    },
    "max_results": {"type": "integer", "minimum": 1},
    "max_additions": {"type": "integer", "minimum": 0},
    "budget_ms": {"type": "integer", "minimum": 1},
    "cold": {"type": "boolean"},
    "settings": {"$ref": "common.json#/$defs/settings"},
    "data_dir": {"type": "string"}
  }
}
```

Reply data (`filter.search.results.result`). The schema has no limit on `additions` or on `note`, because extra additions and long notes are not malformed (R9.16, R9.17).

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://itayavtalyon.github.io/remember/schema/plugin/v1/search-results-out.json",
  "title": "filter.search.results.result data",
  "type": "object",
  "properties": {
    "results": {
      "type": "array",
      "items": {"type": "object", "required": ["memory_id", "score"],
        "properties": {"memory_id": {"$ref": "common.json#/$defs/uuid"}, "score": {"type": "number"}}}
    },
    "additions": {"type": "array", "items": {"$ref": "common.json#/$defs/uuid"}},
    "skipped": {"type": "boolean", "default": false},
    "note": {"type": "string"}
  },
  "if": {"properties": {"skipped": {"const": true}}, "required": ["skipped"]},
  "then": {"required": ["note"]},
  "else": {"required": ["results"]}
}
```

### 5.3 Example

Request (3 of 200 candidates shown; the filter needs no memory fields):

<!-- example: envelope.json -->
```json
{"format":"remember-message","version":1,"type":"filter.search.results","request_id":"01a14523-0000-7000-8000-000000000201",
 "data":{"entry":"rerank",
  "query":{"text":"how do we escape export paths","tags":[],"bins":["live"],"time_range":{"field":"updated_at","after":null,"before":null}},
  "original_query":{"text":"how do we escape export paths","tags":[],"bins":["live"],"time_range":{"field":"updated_at","after":null,"before":null}},
  "candidates":[
   {"id":"01a13a8c-55f0-7c2e-9b1d-4e0c7f3a2b11","bm25":7.41,"scores":[]},
   {"id":"01a13b02-1a10-7e3d-8d2a-1b2c3d4e5f60","bm25":5.02,"scores":[]},
   {"id":"01a13c55-3b21-7a0e-9f11-aa00bb11cc22","bm25":4.88,"scores":[]}],
  "max_results":20,"max_additions":200,"budget_ms":800,"cold":false,
  "settings":[{"key":"plugin.hybrid.enabled","value":true},{"key":"plugin.hybrid.min_index_coverage","value":50},{"key":"plugin.hybrid.model","value":"small"},{"key":"remember.search.candidate_pool","value":200}],
  "data_dir":"/Users/itay/.remember/remember.db.d/plugins/hybrid/data/"}}
```

Response:

<!-- example: envelope.json -->
```json
{"format":"remember-message","version":1,"type":"filter.search.results.result","request_id":"01a14523-0000-7000-8000-000000000201",
 "data":{"results":[
   {"memory_id":"01a13c55-3b21-7a0e-9f11-aa00bb11cc22","score":0.91},
   {"memory_id":"01a13a8c-55f0-7c2e-9b1d-4e0c7f3a2b11","score":0.87},
   {"memory_id":"01a13d10-0000-7abc-8def-0123456789ab","score":0.80}],
  "additions":["01a13d10-0000-7abc-8def-0123456789ab"],
  "note":"hybrid: 92% indexed, model small"}}
```

Pass-through on purpose (index below its threshold):

<!-- example: envelope.json -->
```json
{"format":"remember-message","version":1,"type":"filter.search.results.result","request_id":"01a14523-0000-7000-8000-000000000201","data":{"skipped":true,"note":"hybrid: 31% indexed, below 50%; keyword order kept"}}
```

---

## 6. Control messages

### 6.1 Rules

- R6.1. Remember starts a persistent process with these environment variables: `REMEMBER_PLUGIN_SOCKET`, `REMEMBER_PLUGIN_NAME`, `REMEMBER_PLUGIN_DATA_DIR`, `REMEMBER_PLUGIN_IDLE_TIMEOUT`, `REMEMBER_DB`, `REMEMBER_VERSION` **(OQ-7)**, `REMEMBER_HOOK_DEPTH=1` and `REMEMBER_HOOK_NAME=plugin/<name>` (R3.23) `[decision:g2-06]`. `REMEMBER_PLUGIN_IDLE_TIMEOUT` is information only: Remember drives the idle stop (R6.9) **(PP-13)**.
- R6.2. The plugin binds and listens on the path in `REMEMBER_PLUGIN_SOCKET`. Then it writes one `ready` envelope as one line on stdout. `ready` carries `protocol`, `pid` and `index_version` in `data` `[decision:plugins-process-kinds]` **(OQ-7, `[decision:protocol-index-version-report]`)**. `ready.plugin` and `ready.plugin_version` are optional hints. The manifest wins (section 2.1a).
- R6.2a. When `ready.protocol` differs from the manifest `protocol`, Remember sends `shutdown` with reason `upgrade`, disables the plugin with `disabled_reason: protocol_mismatch`, and reports `plugin_protocol_unsupported` in `remember plugin status`. This counts neither as a strike nor toward a crash loop `[decision:plugin-version-glossary]` **(PP-09)**.
- R6.3. On Windows, the socket is AF_UNIX behind the platform abstraction layer `[principle:platform-abstraction-layer]` **(OQ-10)**.
- R6.4. One process serves every persistent entry of the plugin for one brain. Every request and every event names its entry in `data.entry`, so the process can route it. `data.filter` and `data.plugin.action` are gone `[decision:plugin-protocol-small-fixes]` **(OQ-9, PP-14)**.
- R6.5. The socket is `<runtime>/s/<16 hex>.sock`. Remember stores the process state in one state file, the log 010 H-11.4 document. Remember does not write a separate pid file.
- R6.6. Remember holds an OS advisory lock in the runtime directory while it checks and starts a process. Only one caller starts the process `[decision:plugins-liveness]`.
- R6.7. Remember sends `ping` on its own connection. The plugin must answer `ping.result` within 200 ms. `ping.result` carries `pid` and `index_version` in `data` **(`[decision:protocol-index-version-report]`)**. Remember judges a process alive only when it answers in time. Remember never trusts the state file alone.
- R6.8. When the ping fails, Remember deletes the stale state, socket and lock-holder files, and starts the process cold.
- R6.9. (When the daemon does not run, the CLI and the host run each job, log 010 H-11.6, H-11.7, H-13.4.) Remember stops a process after its idle timeout (`remember.plugins.idle_timeout`, default 10 min, or the manifest `idle_timeout`, R2.20) with no requests. It sends `shutdown` with reason `idle`. The daemon sends it when it runs. Without the daemon, the next Remember run that finds an idle process past its timeout sends it `[decision:plugins-filter-lifecycle]`, `[decision:plugin-protocol-small-fixes]`, `[decision:remember-daemon-scope]` **(PP-13)**. A plugin needs no idle timer. It may stop itself earlier. At a clean stop, the plugin removes its own runtime files.
- R6.10. Remember sends `shutdown` to stop a process, for example after 3 strikes, on `plugin disable`, or on `plugin remove`. The plugin sends no reply. It closes within `grace_ms`. After that, Remember kills the process through the platform layer.
- R6.11. Either side may answer a request with a failed reply. The reply keeps its `<request type>.result` type, echoes the `request_id`, and carries the JS §1.8 error object in `error`. `fix` is required except for the code `internal` **(Q-A)**.
- R6.12. A plugin asks for a backfill with the `backfill_needed` field in its `ready` data or in any `ping.result` data **(OQ-30)**. Remember never accepts a connection from the plugin. A request seen during a search or a write is only marked pending (R7.3) **(CF-10)**.
- R6.13. A persistent plugin reports its current `index_version` at the top level of the data of every `ready` and every `ping.result` **(`[decision:protocol-index-version-report]`)**. `index_version` is `{data_version, model}`. `data_version` must equal the manifest `data_version` (default `1`). `model` is the model id, or `null` when the plugin has no model. A `ready` or `ping.result` without `index_version` is malformed output (R9.14). Remember compares each reported `index_version` with the stored one (R7.5):
  - No stored `index_version`: the index is `missing`, and a full backfill is due.
  - A different `index_version`: the index is `stale`, and a full rebuild is due. The rebuild follows the normal desktop and phone rebuild rules `[decision:plugins-rebuild-policy]` **(CF-6)**.
  - The same `index_version`: the index is current. Remember starts no backfill unless the plugin sends `backfill_needed` (R6.14).
- R6.14. `backfill_needed` has one field, `applied_through_seq`: the integer `change_seq` of the last change that the plugin has applied `[decision:protocol-backfill-seq]`, `[decision:change-seq]` **(NQ-7)**. The plugin sends it when it missed changes, for example after a crash. When the index is current (R6.13), Remember starts a `catch_up` backfill of every change with a `change_seq` greater than `applied_through_seq`. `0` asks for every memory. When the index is `missing` or `stale`, the full backfill covers the request, and Remember ignores `applied_through_seq`. An `index_version` or an `applied_through` inside `backfill_needed` is an unknown field (R9.20), and the required `applied_through_seq` is then missing (R9.14).
- R6.15. Remember sends a persistent process's stderr to `<brain>.d/plugins/<name>/stderr.log`, next to `index.json` and outside `data_dir` (R3.9), rotated like the hooks log (R9.28). `remember plugin log <name>` shows it with the plugin's lines from the hooks log `[decision:plugin-protocol-small-fixes]` **(PP-12)**.

### 6.2 Schemas

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://itayavtalyon.github.io/remember/schema/plugin/v1/control.json",
  "title": "Control message data",
  "$defs": {
    "ready": {"type": "object", "required": ["protocol", "pid", "index_version"],
      "properties": {
        "protocol": {"const": 1},
        "pid": {"type": "integer", "minimum": 1},
        "plugin": {"$ref": "common.json#/$defs/plugin_name"},
        "plugin_version": {"type": "string"},
        "index_version": {"$ref": "#/$defs/index_version", "description": "Always present (R6.13)."},
        "backfill_needed": {"$ref": "#/$defs/backfill_needed"}}},
    "ping": {"type": "object"},
    "ping_result": {"type": "object", "required": ["pid", "index_version"],
      "properties": {
        "pid": {"type": "integer", "minimum": 1},
        "index_version": {"$ref": "#/$defs/index_version", "description": "Always present (R6.13)."},
        "active_requests": {"type": "integer", "minimum": 0},
        "backfill_needed": {"$ref": "#/$defs/backfill_needed"}}},
    "shutdown": {"type": "object", "required": ["reason", "grace_ms"],
      "properties": {
        "reason": {"enum": ["idle", "strikes", "disabled", "removed", "upgrade", "user"]},
        "grace_ms": {"type": "integer", "minimum": 0}}},
    "index_version": {"type": "object", "required": ["data_version", "model"],
      "properties": {
        "data_version": {"type": "integer", "minimum": 1},
        "model": {"type": ["string", "null"], "description": "Model id, or null for a plugin without a model."}}},
    "backfill_needed": {"type": "object", "required": ["applied_through_seq"],
      "properties": {
        "applied_through_seq": {"type": "integer", "minimum": 0, "description": "change_seq of the last applied change; 0 asks for every memory (R6.14, #599)."}}}
  }
}
```

### 6.3 Examples

`ready` (stdout, one line):

<!-- example: envelope.json -->
```json
{"format":"remember-message","version":1,"type":"ready","data":{"protocol":1,"pid":48213,"plugin":"hybrid","plugin_version":"1.0.0","index_version":{"data_version":3,"model":"small"}}}
```

`ping` and `ping.result` (one connection):

<!-- example-lines: envelope.json -->
```json
{"format":"remember-message","version":1,"type":"ping","request_id":"01a14524-0000-7000-8000-000000000302","data":{}}
{"format":"remember-message","version":1,"type":"ping.result","request_id":"01a14524-0000-7000-8000-000000000302","data":{"pid":48213,"index_version":{"data_version":3,"model":"small"},"active_requests":0}}
```

`shutdown`:

<!-- example: envelope.json -->
```json
{"format":"remember-message","version":1,"type":"shutdown","data":{"reason":"strikes","grace_ms":2000}}
```

Failed reply (to the `filter.search.results` request of section 5.3):

<!-- example: envelope.json -->
```json
{"format":"remember-message","version":1,"type":"filter.search.results.result","request_id":"01a14523-0000-7000-8000-000000000201","error":{"code":"model_not_loaded","message":"The embedding model small is missing from the data folder.","fix":"Run 'remember plugin reindex hybrid'.","retryable":false}}
```

---

## 7. Backfill

### 7.1 Rules

- R7.1. Backfill requires a persistent process. Remember rejects a manifest with `backfill.enabled: true` and no persistent entry **(OQ-24)**.
- R7.2. Remember pushes a resumable stream of memories. Each memory has the shape of `common.json#/$defs/memory`, with `id`, `bin`, `updated_at`, `change_seq`, and the union of the plugin's `permissions.needs` `[decision:plugins-backfill]` **(NQ-8, #599)**.
- R7.3. A backfill starts when the user enables the plugin (`remember plugin enable`), when the index is `missing` or `stale` (R2.14, R6.13), or with `remember plugin reindex <name>`. The Fortkeeps app may also start one. A plugin request (`backfill_needed`, R6.12), or a `missing` or `stale` index (R6.13), seen during a search or a write never runs there: Remember only marks the backfill as pending. When the optional daemon runs, it runs the pending backfill. When it does not, the next `remember plugin reindex`, `remember plugin enable`, or launch of the Fortkeeps app runs it `[decision:remember-daemon-scope]`. Until then, `remember plugin status` shows the backfill as pending **(CF-10)**.
- R7.4. A backfill has a `mode`. `full` sends every live memory. `catch_up` sends only the changes with a `change_seq` greater than the plugin's `applied_through_seq`, and names that number in `since_seq` (R6.14) `[decision:protocol-backfill-seq]` **(NQ-7)**. A `catch_up` also sends each purge tombstone with a `change_seq` above `since_seq`, as `{id, bin: "purged", change_seq}`. The plugin deletes that id. A link change reaches a catch-up through the new `change_seq` of both ends `[decision:g-09]`.
- R7.5. Before a `full` backfill, Remember pings the process (R6.7) and keeps the `index_version` that the `ping.result` reports. After `backfill.done.result`, Remember stores that `index_version` **(CF-6, `[decision:protocol-index-version-report]`)**. A `ready` or `ping.result` during or after the stream that reports a different value marks the index `stale` (R6.13). A stopped stream (R7.5b) stores nothing.
- R7.5a. When the optional daemon runs, the CLI hands the backfill to it. When it does not run, the CLI command or app that starts a backfill runs it in the foreground. Either way, the runner holds a per-plugin, per-brain backfill lock in the runtime directory, and a second caller does not start a second stream `[decision:remember-daemon]`, `[decision:remember-daemon-scope]` **(NQ-16)**.
- R7.5b. Remember saves progress (the `backfill_id` and the last answered `seq`) after each answered batch. When the command stops (for example the user presses Ctrl-C or the app quits), a later run resumes from the last answered batch **(NQ-16)**.
- R7.6. Remember opens one connection for the stream. The stream is the one exception to one request per connection (R1.7).
- R7.7. A backfill has a `backfill_id`, a `total`, and a `seq` per batch. `seq` starts at 1. Each batch and `backfill.done` carry a new `request_id`.
- R7.8. A batch holds at most 100 memories or 4 MiB, whichever limit comes first. The manifest may set smaller limits (R2.18) **(OQ-25)**.
- R7.9. The plugin answers each batch with `backfill.result`, which echoes the `request_id`. Remember sends the next batch only after the reply (a window of 1) **(OQ-25)**.
- R7.10. Remember records the last answered `seq` for `remember plugin status`. The plugin keeps its own checkpoint.
- R7.11. Live `event` messages keep flowing on their own connections during a backfill. A backfill batch can hold an older version of a memory than a live event did. The plugin must keep the newer version: the one with the greater `change_seq` **(OQ-26, #599)**.
- R7.12. Only Remember sends `backfill.pause` and `backfill.resume`. Fortkeeps decides when `[decision:plugins-rebuild-policy]`. The plugin has no pause request. The reply window gives backpressure **(OQ-27)**.
- R7.13. After `backfill.pause`, Remember sends no more batches and closes the connection. To resume, Remember opens a new connection, sends `backfill.resume` with `resume_after_seq`, and continues with batch `resume_after_seq + 1`. The plugin may skip memories that its checkpoint already covers.
- R7.14. Remember ends a stream with `backfill.done`. The plugin answers with `backfill.done.result`, whose `seq` is the `last_seq` **(PP-07)**.
- R7.15. Search never blocks or fails while an index builds. Index states are `ready`, `building`, `stale`, `missing`, `failed` and `disabled`.
- R7.16. A missing batch reply within the action timeout is a strike (section 9.2). Remember stops the stream. A later run resumes it from the last answered `seq` (R7.5b). `remember plugin reindex` exits 1 with `plugin_backfill_incomplete` `[decision:s-09]` **(NQ-2, NQ-3)**.

### 7.2 Schemas

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://itayavtalyon.github.io/remember/schema/plugin/v1/backfill.json",
  "title": "Backfill message data",
  "$defs": {
    "backfill": {"type": "object", "required": ["backfill_id", "seq", "total", "reason", "mode", "entries"],
      "properties": {
        "backfill_id": {"$ref": "common.json#/$defs/uuid"},
        "seq": {"type": "integer", "minimum": 1},
        "total": {"type": "integer", "minimum": 0, "description": "Number of memories this stream sends."},
        "reason": {"enum": ["enabled", "plugin_request", "reindex", "data_version", "index_version", "resume"], "description": "data_version: the manifest changed (R2.14). index_version: the plugin reported a different index_version (R6.13)."},
        "mode": {"enum": ["full", "catch_up"]},
        "since_seq": {"type": "integer", "minimum": 0, "description": "catch_up only: the plugin's applied_through_seq (R7.4)."},
        "since": false,
        "data_dir": {"type": "string"},
        "settings": {"$ref": "common.json#/$defs/settings"},
        "entries": {"type": "array", "maxItems": 100, "items": {"anyOf": [
          {"allOf": [{"$ref": "common.json#/$defs/memory"}], "required": ["id", "bin", "updated_at", "change_seq"]},
          {"type": "object", "required": ["id", "bin", "change_seq"], "properties": {"id": {"$ref": "common.json#/$defs/uuid"}, "bin": {"const": "purged"}, "change_seq": {"$ref": "common.json#/$defs/change_seq"}}, "description": "A purge tombstone in a catch_up (R7.4, G-09)."}]}}},
      "if": {"properties": {"mode": {"const": "catch_up"}}},
      "then": {"required": ["since_seq"]}},
    "backfill_result": {"type": "object", "required": ["backfill_id", "seq"],
      "properties": {
        "backfill_id": {"$ref": "common.json#/$defs/uuid"},
        "seq": {"type": "integer", "minimum": 0},
        "failed_memory_ids": {"type": "array", "items": {"$ref": "common.json#/$defs/uuid"}, "description": "Memories the plugin could not index."}}},
    "backfill_done": {"type": "object", "required": ["backfill_id", "last_seq", "sent"],
      "properties": {
        "backfill_id": {"$ref": "common.json#/$defs/uuid"},
        "last_seq": {"type": "integer", "minimum": 0},
        "sent": {"type": "integer", "minimum": 0}}},
    "backfill_pause": {"type": "object", "required": ["backfill_id"],
      "properties": {"backfill_id": {"$ref": "common.json#/$defs/uuid"}, "reason": {"type": "string", "maxLength": 200}}},
    "backfill_resume": {"type": "object", "required": ["backfill_id", "resume_after_seq"],
      "properties": {"backfill_id": {"$ref": "common.json#/$defs/uuid"}, "resume_after_seq": {"type": "integer", "minimum": 0}}}
  }
}
```

### 7.3 Examples

Backfill request inside `ping.result` (the reported `index_version` equals the stored one, so Remember starts a catch-up):

<!-- example: envelope.json -->
```json
{"format":"remember-message","version":1,"type":"ping.result","request_id":"01a14525-0000-7000-8000-000000000400","data":{"pid":48213,"index_version":{"data_version":3,"model":"small"},"active_requests":0,"backfill_needed":{"applied_through_seq":96}}}
```

First connection (batch, reply, pause):

<!-- example-lines: envelope.json -->
```json
{"format":"remember-message","version":1,"type":"backfill","request_id":"01a14525-0000-7000-8000-000000000401","data":{"backfill_id":"01a14525-0000-7000-8000-0000000b0001","seq":1,"total":12,"reason":"plugin_request","mode":"catch_up","since_seq":96,"entries":[{"id":"01a13a8c-55f0-7c2e-9b1d-4e0c7f3a2b11","key":null,"body":"Please review PR #88 (export path escaping).","tags":["to:reviewer"],"bin":"live","updated_at":"2026-10-14T12:30:00.000Z","change_seq":97},{"id":"01a13c55-3b21-7a0e-9f11-aa00bb11cc22","bin":"purged","change_seq":98}]}}
{"format":"remember-message","version":1,"type":"backfill.result","request_id":"01a14525-0000-7000-8000-000000000401","data":{"backfill_id":"01a14525-0000-7000-8000-0000000b0001","seq":1,"failed_memory_ids":[]}}
{"format":"remember-message","version":1,"type":"backfill.pause","data":{"backfill_id":"01a14525-0000-7000-8000-0000000b0001","reason":"fortkeeps: on battery"}}
```

Second connection (resume, later batches, done):

<!-- example-lines: envelope.json -->
```json
{"format":"remember-message","version":1,"type":"backfill.resume","data":{"backfill_id":"01a14525-0000-7000-8000-0000000b0001","resume_after_seq":1}}
{"format":"remember-message","version":1,"type":"backfill.done","request_id":"01a14525-0000-7000-8000-000000000405","data":{"backfill_id":"01a14525-0000-7000-8000-0000000b0001","last_seq":2,"sent":12}}
{"format":"remember-message","version":1,"type":"backfill.done.result","request_id":"01a14525-0000-7000-8000-000000000405","data":{"backfill_id":"01a14525-0000-7000-8000-0000000b0001","seq":2}}
```

---
## 8. Search response, provenance and exit codes

### 8.1 Rules

- R8.1. A search response is a full envelope (E1) with `format: "remember-message"`, `version: 1` `[decision:s-07]`. On the CLI it has no `request_id`, because CLI output is not a reply (JS §1.7a) **(NQ-1)**. On HTTP and MCP it has the `request_id` of the request. The type is `memory.search.result` **(E4)**.
- R8.2. A success carries `data`. A failure carries `error`, keeps `type: "memory.search.result"` (JS §1.7a), and has no `data` **(J1)**.
- R8.3. `data` holds `count`, `total`, `entries`, `query`, `ranked_by`, `filters`, and the 006 `keys` and `tags` facets. `total` is the chain length before the limit. Search has no paging: a search response has no `next_cursor` and no `has_more` `[decision:search-no-paging]`, `[decision:s-08]` (log 008 JE-2.11).
- R8.4. `warnings` appears only when it is not empty **(J2)**.
- R8.5. `entries` items are the normal memory objects, each with its own `id` (JS §2) **(E5)**. On the CLI they carry `local_id`. On HTTP and MCP they never carry `local_id` **(J6)**. (Single-memory commands such as `get`, `add` and `update` return `data.memory`, not `entries`.)
- R8.6. `data.query` holds `original` (the user's query) and `rewritten` (the query after the `search.query` chain). When no filter changed the query, the two are equal **(OQ-16)**.
- R8.7. `data.filters` lists the filters of the search in chain order, `search.query` entries first `[decision:plugins-result-provenance]`.
- R8.8. Each `filters` item has `name` (`<plugin>/<filter>`), `plugin_version`, `point`, `status`, `duration_ms` and `cold`. `status` is `applied`, `skipped`, `failed` or `timeout`. Each item that is not `applied` has a `reason`. An item may have one `note` of at most 120 Unicode characters. `<plugin>/<entry>` names the same entry as the settings prefix `plugin.<plugin>.<entry>` `[decision:plugin-protocol-small-fixes]` **(PP-16)**.
- R8.9. A filter that never ran appears with status `skipped`, `duration_ms: 0`, and a reason: `chain_capped` when the chain cap stopped it, `disabled` when a crash loop disabled the plugin. A filter that the user turned off does not appear **(OQ-29, NQ-9)**. A filter that passed its input through on purpose has status `skipped` and reason `pass_through` **(OQ-20)**.
- R8.10. A `search.results` item with status `applied` has `scores`: one `{memory_id, score}` item for each returned memory that the filter scored. The scores are on the plugin's own scale **(OQ-18)**.
- R8.11. `data.ranked_by` is the name of the last `search.results` filter with status `applied`, or `"bm25"` `[decision:plugins-backfill]`.
- R8.12. With `--json`, warnings go only into `warnings` on stdout. Without `--json`, the CLI prints one `remember: warning: …` line on stderr per warning (`[decision:plan16-warning-prefix-and-failure]` #409). Human search prints no extra line when every filter applied. `remember search --explain` prints the filter table (name, status, ms, note) and `ranked by <plugin>/<entry>`. A cold start prints one line: `remember: warning: hybrid/rerank is still starting, so results are in keyword order.` MCP and HTTP return the structured fields. The MCP search tool description explains `ranked_by`, with `bm25` meaning keyword order `[decision:plugin-provenance-human]` **(J7, PP-15)**.
- R8.13. A plugin or hook problem during a search or a write is a warning, with exit code 0. With `--strict` or `REMEMBER_STRICT=1`, any warning gives exit code 4 `[decision:g2-03]`. Exit code 4 has no other meaning `[decision:s-09]`. stdout holds the normal result **(OQ-1, PP-01)**.
- R8.14. With `--strict`, Remember returns exit code 4 when `warnings` holds at least one item, whatever its code `[decision:g2-03]`.
- R8.15. Each of these is a warning (section 8.2): a filter fails, times out, or sends an oversize message; the chain reaches its cap; a cold filter does not answer within its grace or the chain cap, so the search falls back to the BM25 results (R9.2, SET-2); a crash loop disables a plugin; a plugin fails its pre-flight check (file exists, file is executable); a plugin's code hash has no trust record (`plugin_untrusted`, revision 7); a process-less build cannot dispatch (`plugin_unsupported`, revision 7); or the dispatcher cannot start. A `plugin reindex` or `plugin enable` backfill that does not finish exits 1 with `plugin_backfill_incomplete` `[decision:s-09]`.
- R8.16. A hook or action failure after the handler starts never changes the exit code of a write, also with `--strict`. The write records it in the plugin log and in plugin status `[principle:engine-os-agnostic]`, `[decision:hooks-never-wait]` **(NQ-3)**.

### 8.2 Warning codes

The code list is open for readers. A new code is additive and does not bump the version **(OQ-28)**.

Under `--strict` or `REMEMBER_STRICT=1`, every code below gives exit code 4 `[decision:g2-03]`. Without it, each gives exit code 0.

| Code | Source |
|---|---|
| `plugin_filter_failed` | OQ-28. Failed reply, malformed output, crash, or oversize message. |
| `plugin_filter_timeout` | OQ-28. Warm budget passed. |
| `plugin_filter_cold_start` | OQ-28, SET-2. A cold filter did not answer within its grace or the chain cap. The search fell back to the BM25 results, and the filter keeps loading. |
| `plugin_chain_capped` | OQ-28. The chain cap stopped the chain. Shown together with the `chain_capped` status of each filter that did not run (R8.9, revision 7, D-02). |
| `plugin_disabled_crash_loop` | OQ-28. 3 crashes in 10 minutes. |
| `plugin_preflight_failed` | OQ-28 (was `hook_preflight_failed`, renamed in revision 6, S-01). File missing or not executable. A hash mismatch is `plugin_untrusted` (revision 7, C-20). |
| `plugin_dispatch_failed` | OQ-28 (was `hook_dispatch_failed`, renamed in revision 6, S-01). The dispatcher could not start. |
| `plugin_loop_halted` | Log 010 H-5.6 (M-19 N6: renamed from `plugin_chain_halted`). A cause chain reached 100, so an entry did not run. Sticky until `remember plugin rerun` or `remember plugin status --ack NAME` (M-19 N7). |
| `plugin_untrusted` | Log 010 H-9.5. The plugin's current code hash has no trust record, so it did not run. Includes a hash mismatch found by the pre-flight check (log 010 H-6.2, revision 7). |
| `plugin_unsupported` | Log 010 H-0.7 (revision 7, C-23). A build without process support saved a write that matches a folder-plugin action, and nothing was dispatched. |
| `plugin_action_timeout` | NQ-2. A persistent action did not reply in time, only in a command that runs the action synchronously, for example `plugin run` (NQ-3, revision 7). A write never prints it (R8.16). |
| `plugin_action_failed` | NQ-2. A persistent action sent a failed reply, only in a command that runs the action synchronously, for example `plugin run` (NQ-3, revision 7). A write never prints it (R8.16). |
| `plugin_additions_capped` | NQ-4. OQ-19. |
| `plugin_note_truncated` | NQ-4. OQ-21. |
| `plugin_request_trimmed` | NQ-12, CF-9. Remember trimmed the candidate pool to fit 16 MiB. |
| `unknown_field` | JS §1.13. From a plugin: plugin log and `remember plugin status` only, never command output (NQ-10). |
| `plugin_dev_mode` | R2.19. A dev plugin shaped this search. |
| `plugin_protocol_unsupported` | R6.2a (`plugin status` only). |

`time_budget_capped` is not a code: a budget above the time left in the chain cap is clipped silently (R2.13; 2026-10-22, M-22 A). The CLI warning `flag_meaning_changed` (`--trash`, log 010 H-16.9a; 2026-10-22, M-23 A) is not a plugin code; the CLI code catalog (API surface log, `[decision:api-surface-log]` #583) owns it, and log 010 H-16.9a defines it. Like every warning, it exits 4 under `--strict`.

No `hook_` code exists. A backfill in `plugin reindex` or `plugin enable` that does not finish is the error `plugin_backfill_incomplete` (exit 1), not a warning.

### 8.3 Schema

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://itayavtalyon.github.io/remember/schema/plugin/v1/search-response.json",
  "title": "memory.search.result envelope (CLI --json without request_id; HTTP and MCP with it)",
  "type": "object",
  "required": ["format", "version", "type"],
  "properties": {
    "format": {"const": "remember-message"},
    "version": {"const": 1},
    "type": {"const": "memory.search.result"},
    "request_id": {"$ref": "common.json#/$defs/uuid"},
    "data": {"$ref": "#/$defs/data"},
    "error": {"$ref": "common.json#/$defs/error"},
    "next_cursor": false,
    "has_more": false,
    "warnings": {"$ref": "common.json#/$defs/warnings"}
  },
  "oneOf": [
    {"required": ["data"], "not": {"required": ["error"]}},
    {"required": ["error"], "not": {"required": ["data"]}}
  ],
  "allOf": [
    {"if": {"not": {"required": ["request_id"]}},
     "then": {"properties": {"data": {"properties": {"entries": {"items": {"$ref": "#/$defs/cli_memory"}}}}}}},
    {"if": {"required": ["request_id"]},
     "then": {"properties": {"data": {"properties": {"entries": {"items": {"properties": {"local_id": false}}}}}}}}
  ],
  "$defs": {
    "response_memory": {
      "allOf": [{"$ref": "common.json#/$defs/memory_fields"}],
      "required": ["id", "key", "body", "tags", "created_at", "updated_at", "expires_at", "deleted_at", "bin", "version_vector", "links"]
    },
    "cli_memory": {"properties": {"local_id": {"type": "integer", "minimum": 1, "description": "CLI only (J6)."}}},
    "data": {
      "type": "object",
      "required": ["count", "total", "entries", "query", "ranked_by", "filters", "keys", "tags"],
      "properties": {
        "count": {"type": "integer", "minimum": 0},
        "total": {"type": "integer", "minimum": 0, "description": "The chain length before the limit (S-08)."},
        "keys": {"type": "array", "description": "The 006 key facet (log 008 J-Q5)."},
        "tags": {"type": "array", "description": "The 006 tag facet (log 008 J-Q5)."},
        "entries": {"type": "array", "items": {"$ref": "#/$defs/response_memory"}},
        "query": {"type": "object", "required": ["original", "rewritten"],
          "properties": {"original": {"$ref": "common.json#/$defs/query"}, "rewritten": {"$ref": "common.json#/$defs/query"}}},
        "ranked_by": {"type": "string"},
        "filters": {"type": "array", "items": {"$ref": "#/$defs/filter_entry"}}
      }
    },
    "filter_entry": {
      "type": "object",
      "required": ["name", "plugin_version", "point", "status", "duration_ms", "cold"],
      "properties": {
        "name": {"type": "string", "pattern": "^[a-z][a-z0-9_]*/[a-z][a-z0-9_]*$"},
        "plugin_version": {"type": "string"},
        "point": {"enum": ["search.query", "search.results"]},
        "status": {"enum": ["applied", "skipped", "failed", "timeout"]},
        "reason": {"type": "string", "description": "Open list: chain_capped, disabled, pass_through, budget, cold_grace, crash, malformed, error_reply, oversize, spawn_failed."},
        "duration_ms": {"type": "integer", "minimum": 0},
        "cold": {"type": "boolean"},
        "note": {"type": "string", "maxLength": 120},
        "scores": {"type": "array", "items": {"type": "object", "required": ["memory_id", "score"],
          "properties": {"memory_id": {"$ref": "common.json#/$defs/uuid"}, "score": {"type": "number"}}}}
      },
      "if": {"properties": {"status": {"not": {"const": "applied"}}}},
      "then": {"required": ["reason"]}
    }
  }
}
```

### 8.4 Example (CLI `--json`, exit code 0; 4 with `--strict`)

One filter timed out. A later filter ranked the results. The chain cap stopped the last filter.

<!-- example: search-response.json -->
```json
{"format":"remember-message","version":1,"type":"memory.search.result",
 "data":{"count":2,"total":2,"keys":[],"tags":[],
  "entries":[
   {"id":"01a13a8c-55f0-7c2e-9b1d-4e0c7f3a2b11","local_id":41,"key":null,"body":"Please review PR #88 (export path escaping).","tags":["to:reviewer"],"created_at":"2026-10-14T12:00:00.000Z","updated_at":"2026-10-14T12:30:00.000Z","expires_at":null,"deleted_at":null,"bin":"live","version_vector":{"01a13651-e244-717d-bd40-7850d5e2f27d":2},"links":[]},
   {"id":"01a13b02-1a10-7e3d-8d2a-1b2c3d4e5f60","local_id":44,"key":"howto:export-escaping","body":"Export paths are escaped with sqlite3_mprintf %q.","tags":["howto"],"created_at":"2026-10-13T09:10:00.000Z","updated_at":"2026-10-13T09:10:00.000Z","expires_at":null,"deleted_at":null,"bin":"live","version_vector":{"01a13651-e244-717d-bd40-7850d5e2f27d":1},"links":[]}],
  "query":{
   "original":{"text":"how do we escape export paths","tags":[],"bins":["live"],"time_range":{"field":"updated_at","after":null,"before":null}},
   "rewritten":{"text":"how do we escape (export OR exporter) paths","tags":[],"bins":["live"],"time_range":{"field":"updated_at","after":null,"before":null}}},
  "ranked_by":"recency/boost",
  "filters":[
   {"name":"synonyms/expand","plugin_version":"0.2.0","point":"search.query","status":"applied","duration_ms":21,"cold":false,"note":"expanded export"},
   {"name":"hybrid/rerank","plugin_version":"1.0.0","point":"search.results","status":"timeout","reason":"budget","duration_ms":800,"cold":false},
   {"name":"recency/boost","plugin_version":"0.1.0","point":"search.results","status":"applied","duration_ms":679,"cold":false,
    "scores":[{"memory_id":"01a13a8c-55f0-7c2e-9b1d-4e0c7f3a2b11","score":12.0},{"memory_id":"01a13b02-1a10-7e3d-8d2a-1b2c3d4e5f60","score":9.5}]},
   {"name":"dedupe/collapse","plugin_version":"0.3.1","point":"search.results","status":"skipped","reason":"chain_capped","duration_ms":0,"cold":false}]},
 "warnings":[
  {"code":"plugin_filter_timeout","message":"Filter hybrid/rerank took longer than 800ms, so it did not rank these results. If this keeps happening, run 'remember plugin status hybrid'.","detail":{"plugin":"hybrid","filter":"rerank","reason":"budget","strikes":1}},
  {"code":"plugin_chain_capped","message":"The filter chain reached its 1.5s cap, so filter dedupe/collapse did not run.","detail":{"plugin":"dedupe","filter":"collapse","reason":"chain_capped"}}]}
```

The same search over HTTP (shortened to one entry, no warnings):

<!-- example: search-response.json -->
```json
{"format":"remember-message","version":1,"type":"memory.search.result","request_id":"01a14530-0000-7000-8000-0000000000aa",
 "data":{"count":1,"total":1,"keys":[],"tags":[],
  "entries":[{"id":"01a13a8c-55f0-7c2e-9b1d-4e0c7f3a2b11","key":null,"body":"Please review PR #88 (export path escaping).","tags":["to:reviewer"],"created_at":"2026-10-14T12:00:00.000Z","updated_at":"2026-10-14T12:30:00.000Z","expires_at":null,"deleted_at":null,"bin":"live","version_vector":{"01a13651-e244-717d-bd40-7850d5e2f27d":2},"links":[]}],
  "query":{
   "original":{"text":"export paths","tags":[],"bins":["live"],"time_range":{"field":"updated_at","after":null,"before":null}},
   "rewritten":{"text":"export paths","tags":[],"bins":["live"],"time_range":{"field":"updated_at","after":null,"before":null}}},
  "ranked_by":"bm25","filters":[]}}
```

A cold filter on its first search (CLI `--json`, exit code 0; 4 with `--strict`). The hybrid process is still loading its model, so the chain cap ran out and the search fell back to the BM25 results **(SET-2)**:

<!-- example: search-response.json -->
```json
{"format":"remember-message","version":1,"type":"memory.search.result",
 "data":{"count":1,"total":1,"keys":[],"tags":[],
  "entries":[{"id":"01a13a8c-55f0-7c2e-9b1d-4e0c7f3a2b11","local_id":41,"key":null,"body":"Please review PR #88 (export path escaping).","tags":["to:reviewer"],"created_at":"2026-10-14T12:00:00.000Z","updated_at":"2026-10-14T12:30:00.000Z","expires_at":null,"deleted_at":null,"bin":"live","version_vector":{"01a13651-e244-717d-bd40-7850d5e2f27d":2},"links":[]}],
  "query":{
   "original":{"text":"export paths","tags":[],"bins":["live"],"time_range":{"field":"updated_at","after":null,"before":null}},
   "rewritten":{"text":"export paths","tags":[],"bins":["live"],"time_range":{"field":"updated_at","after":null,"before":null}}},
  "ranked_by":"bm25",
  "filters":[
   {"name":"hybrid/rerank","plugin_version":"1.0.0","point":"search.results","status":"timeout","reason":"cold_grace","duration_ms":1500,"cold":true}]},
 "warnings":[
  {"code":"plugin_filter_cold_start","message":"Filter hybrid/rerank is still starting, so these results are in keyword (BM25) order. It keeps loading, and a later search will use it.","detail":{"plugin":"hybrid","filter":"rerank","reason":"cold_grace","waited_ms":1500}}]}
```

Without `--json`, the human CLI prints on stderr: `remember: warning: hybrid/rerank timed out (800ms); dedupe/collapse did not run (chain cap 1.5s).` For the cold start: `remember: warning: hybrid/rerank is still starting, so results are in keyword order.` (R8.12).

---
## 9. Errors and failure semantics

### 9.1 Timeouts and budgets

- R9.1. Remember gives each filter entry its own budget, the manifest `budget` of the entry; no user setting overrides it `[decision:s-06]`. The default warm budget is 800 ms for both points `[decision:plugins-budget-defaults]` **(OQ-17, NQ-14)**.
- R9.2. A filter is cold on its first call after its process starts. Remember then gives the cold-start grace (the manifest `cold_start_grace` of the entry, default 10 s, `[decision:s-06]`) instead of the warm budget. The chain cap covers the grace: Remember waits for a cold filter at most the grace or the time left in the chain cap, whichever is shorter, and sends that time as `budget_ms` **(SET-2)**. When the cold filter has not answered by then (normally on its first search), Remember falls back: it passes the filter's input on unchanged, so the search returns the BM25 results (or the order of an earlier `search.results` filter that applied). The filter's status is `timeout` with reason `cold_grace` and `cold: true`. The warning is `plugin_filter_cold_start`. The exit code is 0 (4 with `--strict`) `[decision:plugins-degraded-exit-code]`. The filter keeps loading (R9.6). This is not a strike (R9.9). Under the default chain cap (1.5 s), a cold filter gets at most the cap, so most of the 10 s grace is used only when the user raises `remember.plugins.chain_budget` `[decision:plugin-cold-start-warmup]` **(PP-03)**.
- R9.3. No budget or grace can exceed 30 s. Remember rejects a manifest value above 30 s with `plugin_manifest_invalid`, which names the cap `[decision:s-06]`. A `cold_start_grace` below 1 ms is rejected the same way (2026-10-22, M-24 A). `remember plugin status` shows each filter's `cold_start_grace` next to its `budget` (2026-10-22, M-24 A).
- R9.4. The whole chain has a cap. The default is 1.5 s (`remember.plugins.chain_budget`) `[decision:plugins-chain-cap]`. The `search.query` and `search.results` filters share it **(OQ-17)**. The time of every filter entry counts toward the cap, warm or cold, including a cold filter's start-up grace **(NQ-14, SET-2)**. When the chain reaches the cap, Remember does not run the remaining filters. Their status is `skipped` with reason `chain_capped` **(OQ-29)**.
- R9.5. When a filter does not answer within its budget, Remember stops waiting. Remember passes the input on unchanged. The status is `timeout`.
- R9.6. On a cold-start fallback, Remember does not stop the process. The process keeps loading while the search returns. Later calls reach the warm process `[decision:plugins-budget-defaults]` **(SET-2)**.
- R9.6a. Remember warms persistent filter processes ahead of time. The daemon keeps them warm while it runs. Without the daemon, Remember starts them in the background at `plugin enable`, after a backfill, at Fortkeeps app launch, and on the first write or search of a CLI session when no live process exists. A search never waits for this start. `remember plugin status` shows `cold_start_ms` (the last measured load) and warns when it is longer than the chain cap `[decision:plugin-cold-start-warmup]`, `[decision:remember-daemon-scope]` **(PP-03)**.
- R9.7. A once-process action that runs longer than its `run.timeout` gets SIGTERM, then SIGKILL after 5 s, through the platform layer (HK §3.2). The result is `timeout`.

### 9.2 The strike rule

- R9.8. Remember counts strikes per process (per plugin per brain), over all its entries **(OQ-23)**.
- R9.9. These are strikes: a warm filter timeout (a cold-start fallback is not one), a missing persistent action reply (R3.19), a missing backfill reply (R7.16), a crash `[decision:plugins-strikes]`, malformed output, and an oversize message **(OQ-12, NQ-3, NQ-11)**. A missing action reply stays a strike even when no command waits for it.
- R9.10. These are not strikes: a well-formed failed reply **(OQ-22, NQ-11)**, a failed `event.result` (with `error`, PP-08), and an intentional pass-through **(OQ-20)**. They still give the status `failed` (or `skipped` for a pass-through) and the matching warning.
- R9.11. A good answer resets the consecutive count to 0.
- R9.12. After 3 consecutive strikes, Remember sends `shutdown` with reason `strikes` and stops the process. The next call starts it cold.
- R9.13. After 3 crashes within 10 minutes, Remember disables the plugin in that brain: it sets `disabled_reason` `crash_loop` in that brain's `index.json` and writes no setting `[decision:s-02]`. The warning `plugin_disabled_crash_loop` tells the user to run `remember plugin enable <name>`, which clears it.

### 9.3 Malformed and oversize output

- R9.14. Remember treats each of these as malformed output: invalid JSON; invalid UTF-8; a missing or wrong `format`, `version` or `type`; a reply type that does not match the request; a missing `request_id`, or one that does not echo the request; both `data` and `error`, or neither; `data` that fails its schema; an empty query text or empty `bins` from `search.query`; a `ready` or `ping.result` without `index_version`, or with an `index_version.data_version` that differs from the manifest `data_version` (R6.13).
- R9.15. Malformed output is a failure and a strike. Remember passes the input of that filter on unchanged. The status is `failed`, and the reason is `malformed` **(NQ-11)**.
- R9.16. A message over its size limit (R1.13) is a failure and a strike, with reason `oversize`. Remember passes the input through and warns (exit code 0; 4 with `--strict`, #516) **(OQ-6, NQ-11)**.
- R9.17. A note longer than 120 Unicode characters is not malformed. Remember keeps the first 119 characters, adds "…", and warns with `plugin_note_truncated` **(OQ-21)**.
- R9.18. In `search.results` output, none of these make the output malformed: a result id that is not a candidate and not an addition (Remember ignores it); a duplicate id after its first position (Remember ignores it); an addition that does not exist (Remember drops it); additions over `max_additions` (Remember drops the extras and warns with `plugin_additions_capped`) **(OQ-19)**.

### 9.4 Unknown fields and forward compatibility

- R9.19. Remember rejects an unknown field only in the `run`, `permissions` and `filter` sections of `plugin.json` (`plugin_manifest_invalid`). No hooks config exists. The error names the field **(J3)**.
- R9.20. Everywhere else, a receiver ignores an unknown field and warns with `unknown_field` (`detail`: `field`, `where`). This covers the rest of `plugin.json`, every protocol message, and every event **(J3)**. An old field such as an envelope `id` or `body` is an unknown field. A message with `body` and no `data` is also malformed (R9.14).
- R9.21. Remember warns once per process start for each message type and field. Remember writes a plugin's `unknown_field` warning only to the plugin log (the shared hooks log, R9.28) and to `remember plugin status`, never into command output **(NQ-10)**. A plugin that gets an unknown field from Remember ignores it and may log it. Remember writes the `warnings` of a plugin reply only to the plugin log and to `remember plugin status`. It never copies them into the `warnings` of a CLI or API response, so they never change the exit code (R8.14) **(NQ-17)**.
- R9.22. A plugin must answer a request of an unknown type with a failed reply of type `<request type>.result`, code `unknown_message_type` **(Q-A)**. It must not crash.
- R9.23. A new optional field, a new message type, a new enum value and a new warning code do not change the protocol version. A renamed field, a removed field, or a change in meaning makes version 2 **(OQ-5, OQ-28)**.
- R9.24. Remember must not send version 2 messages to a plugin whose manifest declares `protocol: 1`.
- R9.25. Before 1.0, a breaking change is a clean break with a changelog note. Remember accepts no old names or shapes (for example `sync_id`, `body`, `pong`, `type: "error"`, a bare `remember-event`, or colon-style setting keys) **(J9)**.

### 9.5 Isolation

- R9.26. No plugin or hook can fail, block or change a create, update or delete `[principle:plugins-never-change-writes]`.
- R9.27. Filters apply only to reads. On failure, Remember returns the unfiltered result.
- R9.28. Remember writes hook and plugin results to one shared run log, `<brain>.d/hooks.log` `[decision:brain-sidecar-dir]`. `plugin log --follow --json` emits one `remember-message` per line: `type` `plugin.run` for a run record, `plugin.stderr` for a stderr line. Hooks and plugins share this one file, because they share one mechanism. Authors read it with `remember plugin log <name>` and need not know the file name `[decision:plugin-protocol-small-fixes]` **(PP-18)**. The log rotates at 5 MB, keeps 3 rotated files, and drops entries older than 30 days `[decision:hooks-log-rotation]`. A log line refers to its memory as `memory_id` (HK §6.3) **(J5)**.
- R9.29. On mobile, only plugins compiled into the app run. A built-in plugin is a C-ABI library with one entry, `plugin_handle(msg_json) -> reply_json`, that takes the same protocol messages. It is linked statically, and the engine calls it on a worker thread, so budgets still apply `[decision:builtin-plugins-phone]`. One conformance test suite verifies the desktop and in-app forms `[decision:plugins-mobile-builtin-only]`, `[decision:plugins-open-core-adapters]`.

---

## 10. Plugin CLI

`[decision:plugin-cli-table]` **(PP-05)**, with the log 010 §16 commands `[decision:g-05]`, `[decision:g-35]`. `enable` and `reindex` exit 1 with `plugin_backfill_incomplete` when their own backfill does not finish `[decision:s-09]`. Every other plugin problem is a warning (exit code 4 with `--strict`, section 8.2).

| Command | Does | Exit |
|---|---|---|
| `remember plugin add <dir> [--dev]` | Validate (every error at once, R2.5a), show needs, confirm trust, install. `plugin add` of an installed name is the update path and asks again (R2.3). Writes `enabled = true` on the user layer (R2.15). `--dev`: R2.19. | 0; 1 invalid or usage |
| `remember plugin list` | One line per plugin: name, version, enabled, `dev`, index state. | 0 |
| `remember plugin show <name>` | The manifest, trust state and index state. | 0 |
| `remember plugin trust <name>` | Confirm the current code hash. Clears `disabled_reason` `untrusted` (R2.15). | 0; 1 denied |
| `remember plugin enable <name> [--scope brain]` | A shortcut for `settings set plugin.<name>.enabled true`; asks nothing (R2.3a). Starts the warm-up (R9.6a), runs a pending backfill, clears `crash_loop`. | 0; 1 `plugin_backfill_incomplete` |
| `remember plugin disable <name> [--scope brain]` | A shortcut for `settings set plugin.<name>.enabled false`. `shutdown` reason `disabled`. | 0 |
| `remember plugin remove <name> [--delete-data]` | `shutdown` reason `removed`, delete the install. Data is kept by default and the command says so; `--delete-data` deletes it. | 0 |
| `remember plugin reindex <name> [--full]` | Backfill with progress on stderr. Ctrl-C resumes later (R7.5b). The daemon runs it when it runs (R7.5a). | 0; 1 `plugin_backfill_incomplete` |
| `remember plugin rerun <name> <event-id>` | Rerun a `failed` or `halted` run once, with the chain reset (R3.14). Clears `plugin_loop_halted`. | 0; 1 `plugin_not_rerunnable` |
| `remember plugin status [<name>] [--ack <name>]` | Report only. Never waits for an ack. A missed ack goes in `last_problems`. `--ack` clears the sticky `plugin_loop_halted` warning (M-19 N7). | 0 whenever it can report |
| `remember plugin log <name> [--follow] [--since <duration>]` | The plugin's lines from the hooks log plus its stderr log (R6.15, R9.28). | 0 |
| `remember plugin test <name>|<dir> [--entry <entry>] [--query <text>]` | `<name>`: run an installed entry now. `<dir>`: run the conformance suite against the folder in a scratch brain. No install, no trust prompt. | 0 pass; 1 fail |

`remember plugin status --json` is a `remember-message` v1 with type `plugin.status.result`. Its `data.plugins[]` items have this shape:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://itayavtalyon.github.io/remember/schema/plugin/v1/plugin-status.json",
  "title": "One data.plugins[] item of remember plugin status --json",
  "type": "object",
  "required": ["name", "plugin_version", "protocol", "enabled", "disabled_reason", "dev", "process", "index", "entries", "last_problems"],
  "properties": {
    "name": {"$ref": "common.json#/$defs/plugin_name"},
    "plugin_version": {"type": "string"},
    "protocol": {"type": "integer", "minimum": 1},
    "enabled": {"type": "boolean"},
    "disabled_reason": {"enum": [null, "user", "untrusted", "crash_loop", "protocol_mismatch"]},
    "dev": {"type": "boolean"},
    "process": {"type": "object", "required": ["state", "pid", "started_at", "cold_start_ms", "strikes", "crashes_10m"],
      "properties": {
        "state": {"enum": ["running", "stopped", "starting"]},
        "pid": {"type": ["integer", "null"]},
        "started_at": {"$ref": "common.json#/$defs/nullable_timestamp"},
        "cold_start_ms": {"type": ["integer", "null"], "minimum": 0},
        "strikes": {"type": "integer", "minimum": 0},
        "crashes_10m": {"type": "integer", "minimum": 0}}},
    "index": {"type": "object", "required": ["state", "coverage", "index_version", "backfill"],
      "properties": {
        "state": {"enum": ["ready", "building", "stale", "missing", "failed", "disabled"]},
        "coverage": {"type": ["number", "null"], "minimum": 0, "maximum": 1},
        "index_version": {"oneOf": [{"type": "null"}, {"$ref": "control.json#/$defs/index_version"}]},
        "backfill": {"type": "object", "required": ["pending", "mode", "seq", "total"],
          "properties": {
            "pending": {"type": "boolean"},
            "mode": {"enum": [null, "full", "catch_up"]},
            "seq": {"type": ["integer", "null"], "minimum": 0},
            "total": {"type": ["integer", "null"], "minimum": 0}}}}},
    "entries": {"type": "array", "items": {"type": "object", "required": ["name", "point", "budget", "last_status", "last_duration_ms"],
      "properties": {
        "name": {"$ref": "common.json#/$defs/entry_name"},
        "point": {"enum": ["search.query", "search.results", "action"]},
        "budget": {"oneOf": [{"$ref": "common.json#/$defs/duration"}, {"type": "null"}]},
        "cold_start_grace": {"$ref": "common.json#/$defs/duration", "description": "Optional; filters only. The manifest grace (R9.2; 2026-10-22, M-24 A)."},
        "last_status": {"enum": [null, "applied", "skipped", "failed", "timeout", "ok"]},
        "last_duration_ms": {"type": ["integer", "null"], "minimum": 0}}}},
    "last_problems": {"type": "array", "items": {"type": "object", "required": ["at", "code", "message"],
      "properties": {
        "at": {"$ref": "common.json#/$defs/timestamp"},
        "code": {"type": "string", "pattern": "^[a-z][a-z0-9_]*$"},
        "message": {"type": "string", "minLength": 1}}}}
  }
}
```

<!-- example: plugin-status.json -->
```json
{"name":"hybrid","plugin_version":"1.0.0","protocol":1,"enabled":true,"disabled_reason":null,"dev":false,
 "process":{"state":"running","pid":48213,"started_at":"2026-10-19T08:00:00.000Z","cold_start_ms":2300,"strikes":0,"crashes_10m":0},
 "index":{"state":"ready","coverage":0.92,"index_version":{"data_version":3,"model":"small"},"backfill":{"pending":false,"mode":null,"seq":null,"total":null}},
 "entries":[{"name":"rerank","point":"search.results","budget":"800ms","cold_start_grace":"10s","last_status":"applied","last_duration_ms":212},{"name":"index","point":"action","budget":null,"last_status":"ok","last_duration_ms":40}],
 "last_problems":[{"at":"2026-10-19T08:00:02.300Z","code":"plugin_filter_cold_start","message":"Filter hybrid/rerank took 2300ms to start, longer than the 1.5s chain cap."}]}
```

The `point` value `action` and the `last_status` value `ok` for an action entry are this spec's reading of the Interface review shape; the review did not list them.
