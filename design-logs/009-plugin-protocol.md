# 009 - Plugin protocol, version 1

**Status:** Design (grill closed 2026-10-21). Amended 2026-10-22: the daemon stays in v1 (S-11 reversed) and plugins read settings read-only (rev 6 table). 2026-10-25: spec revision 7, a document revision of protocol 1 (warning-code wording only, no wire change, P-Q4): a pre-flight hash mismatch is `plugin_untrusted` (C-20), `plugin_chain_capped` is shown with the `chain_capped` status (D-02), `plugin_filter_failed` is confirmed (D-08), and dispatch under `REMEMBER_PROCESSES=0` warns `plugin_unsupported` (C-23). 2026-10-22: spec revision 6 applied in this PR (M-06), names and codes amended (M-17, M-19, Interface R2), the Cloud index cache line deleted (M-13). Revision 5 is protocol 1. P-Q1 to P-Q5 closed 2026-10-20. The deep review of 2026-10-21 reopened the log. Rounds 2 to 4 closed the same day. Revision 6 below carries every amendment. The spec file is revised to revision 6 for the amendments in Design before implementation starts.
**Author:** Itay (+ agent facilitation)
**Created:** 2026-10-18

Cross-refs: [007](007-settings.md) (budgets, plugin settings), [008](008-json-envelope.md) (envelope, ids),
[010](010-plugins-and-hooks.md) (plugin and hook runtime, commands, trust).
**Spec:** [`docs/PLUGIN-PROTOCOL.md`](../docs/PLUGIN-PROTOCOL.md) (proposed path; today the box draft
`fk-review/drafts/remember/PLUGIN-PROTOCOL.md`; revision 6 since 2026-10-22, M-06) with its changelog `docs/PLUGIN-PROTOCOL-CHANGES.md`
and its validator.

> Drafting note: `[decision:plugins-filter-needs]` (#463) says the data protocol "must be standardized as one spec with
> a JSON schema and examples, reviewed by Interface and approved by Itay". This log does not repeat the spec. It
> records why the spec exists, where it lives, what it decides, and how the repo keeps it true.
> The grill closed on 2026-10-20. P-Q1 to P-Q5 are resolved below. Design states the winners.

## Background

- Remember gets plugins (log 010): search filters, actions on events, and backfill for derived indexes (hybrid search, URL expansion).
- Hooks and plugins share one runtime and one message format `[decision:hooks-plugins-one-mechanism]` (#452), `principle:protocol-one-shape` (#465).
- Third parties write plugins. They need a fixed wire contract, schemas and examples, not C headers.
- On phones, built-in plugins run in process through an adapter with the same JSON messages `[decision:plugins-open-core-adapters]` (#459). One conformance suite tests both forms.
- The spec went through four revisions on 2026-10-16 to 2026-10-18. Each revision closed open items with Itay. Interface reviewed revision 4 (`fk-review/PLUGIN-PROTOCOL-REVIEW.md`, PP-01 to PP-18). Revision 5 (2026-10-19) applies Itay's decisions on every PP item and has no open items (spec §0.5, §0.6).
- On phones, a built-in plugin is a C-ABI library with one entry `plugin_handle(msg_json) -> reply_json`, called on a worker thread `[decision:builtin-plugins-phone]` (#567). The Fortkeeps app schedules long jobs in the OS background slots `[decision:builtin-plugins-phone-host-bridge]` (#568).
- The plugin protocol and every built-in plugin core live in public remember `[decision:repo-privacy]` (#542).

## Problem

Without one written contract:

- Remember, the in-app adapters and third-party plugins drift.
- A plugin cannot know what it may change (PRINCIPLES 3: plugins never change writes, `principle:plugins-never-change-writes` #425).
- Budgets, strikes and the exit code rules stay in chat logs.

The spec must be in the repo, versioned, machine-checked and cited by code and tests.

## Questions and Answers

The spec §0 lists every locked decision with its tag. The decision memories in Itay's Remember DB:

| Group | Keys (ids) |
|---|---|
| Plugin runtime (log 010 owns the behaviour) | `plugins-process-model` (#427), `plugins-filter-lifecycle` (#428), `plugins-manifest` (#429), `plugins-filter-budget` (#430), `plugins-runtime-dir` (#431, partly superseded by #438), `plugins-budget-defaults` (#432), `plugins-strikes` (#433), `plugins-liveness` (#434), `plugins-filter-chain` (#435), `plugins-chain-cap` (#436), `plugins-process-kinds` (#438), `plugins-filter-points` (#439), `plugins-candidate-pool` (#440), `search-no-paging` (#441), `plugins-budget-clip` (#442), `plugins-backfill` (#457), `plugins-rebuild-policy` (#460), `plugins-partial-index-owned-by-plugin` (#461), `plugins-result-provenance` (#462), `plugins-filter-needs` (#463) |
| Protocol open questions OQ-1 to OQ-30 | `protocol-oq-1-2` (#464), `protocol-oq-5-7` (#466), `protocol-oq-8-11` (#467), `protocol-oq-12-15` (#468), `protocol-oq-16-18` (#470), `protocol-oq-19-20` (#471), `protocol-oq-21-22` (#472), `protocol-oq-23-24` (#473), `protocol-oq-25-26` (#474), `protocol-oq-27-28` (#475), `protocol-oq-29-30` (#476); OQ-3 and OQ-4 in `principle:protocol-one-shape` (#465) |
| Revision 1 questions | `protocol-nq-2` (#488), `protocol-nq-3` (#489), `protocol-nq-4` (#491), `protocol-nq-6` to `nq-9` (#493–#496); NQ-1 is `json-cli-version-2` (#487); NQ-5 is in #492 |
| Revision 2 and 3 questions | `protocol-nq-10` to `nq-14` (#502–#506), `protocol-nq-16` (#507), `protocol-nq-17` (#510), `settings-chain-cap-covers-grace` (#509, SET-2 and CF-8), `settings-entry-budget-key` (#511, CF-7), `protocol-cf-9` (#512), `protocol-cf-10` (#513), `protocol-cf-6` (#514) |
| Revision 4 | `protocol-index-version-report` (#515) |
| Revision 5 (Interface review PP-01 to PP-18, FX answers) | `plugins-degraded-exit-code` (#516, PP-01), `strict-warnings` (#574, PP-02/FX-30), `plugin-cold-start-warmup` (#600, PP-03), `plugin-minimal-example` (#601, PP-04), `plugin-cli-table` (#602, PP-05), `plugin-dev-mode` (#603, PP-06), `plugin-protocol-small-fixes` (#604, PP-07/08/12/13/14/16/18), `plugin-version-glossary` (#605, PP-09), `plugin-manifest-errors` (#606, PP-10), `plugin-permission-prompt` (#607, PP-11), `plugin-provenance-human` (#608, PP-15), `search-paging-fields` (#575, PP-17), `protocol-backfill-seq` (#599), `change-seq` (#543), `warning-time-budget-capped` (#597), `plugin-enabled-default` (#596), `brain-sidecar-dir` (#566), `hook-origins-filter` (#577), `provenance-actors` (#563), `no-fortkeeps-settings-namespace` (#571), `builtin-plugins-phone` (#567), `remember-daemon` (#517), `remember-daemon-scope` (#518) |
| Envelope (log 008 owns it) | `json-*` (#477–#483, #486, #487, #492, #497–#501) |

Superseded texts (the spec follows the newer decision):

- #465 names an envelope with `id` and `body`. #492 and #497 replace it.
- #476 (OQ-30) and #494 (NQ-7) give `backfill_needed` a reason and an `index_version`. #515 moves `index_version` to the top level of `ready` and `ping.result` data. Revision 4 kept `backfill_needed` as `{applied_through}` only. `[decision:protocol-backfill-seq]` (#599) replaces it with `{applied_through_seq}` (spec R6.14).
- #431 names `.pid` and `.sock` files. #438 replaces them with a state file per process.
- #442 names the warning `setting_overridden`. Revision 4 used `setting_capped`. `[decision:warning-time-budget-capped]` (#597) names it `time_budget_capped` (log 007 S-Q10). Removed 2026-10-22 (M-22 A, #763).
- #507 (NQ-16) says "There is no daemon in v1". `[decision:remember-daemon]` (#517) and `[decision:remember-daemon-scope]` (#518) supersede that part: the optional daemon runs backfills, warm filters and timed sync when it runs. S-11 (2026-10-21) removed the daemon; [decision:2026-10-22-daemon-v1] (#738) reverses S-11, so #517 and #518 stand and the daemon also serves local HTTP MCP.
- #509 (SET-2) and #426 give exit code 4 for a degraded search or write. #516 and #574 supersede that part: exit code 0 with a warning, 4 with `--strict`.
- #476, #494 and #515 name `applied_through` (a version vector). #599 replaces it with `applied_through_seq`.

### Resolved questions (grill, 2026-10-20)

- **P-Q1.** The spec is `remember/docs/PLUGIN-PROTOCOL.md`. The changelog is `remember/docs/PLUGIN-PROTOCOL-CHANGES.md`. Both are in the public remember repo.
- **P-Q2.** The validator extracts schemas to `remember/schema/plugin/v1/*.json`. CI fails when a file differs from its spec block. `$id` is `https://github.com/itayavtalyon/remember/schema/plugin/v1/<file>.json`. That string is an identifier. It is not a server Remember fetches. (Superseded by G-06.)
- **P-Q3.** `remember/scripts/validate_plugin_spec.py` runs as a docs CI job. It is Python. The C conformance suite tests the engine.
- **P-Q4.** Revision 5 is protocol 1. A breaking wire change bumps the protocol number. A wording fix is a changelog revision and does not bump it. The amendments below are revision 6 of the document and stay protocol 1, because they close holes in revision 5 before anyone ships a plugin.
- **P-Q5.** A plugin that cannot parse a request closes the connection and sends no reply. Remember counts that as a strike, the same as malformed output. `unknown.result` is only for the CLI (log 008 J-Q10).

### Resolved questions (deep review, 2026-10-21)

From `fk-review/QUESTIONS-2026-10-21.md`. Stored as `decision:g-NN`.

- **G-05.** B: `plugin show` prints the R2-03 facts. `plugin remove` keeps data, and `--delete-data` deletes it.
- **G-06.** A: `$id` uses `https://itayavtalyon.github.io/remember/schema/plugin/v1/` (R2-09).
- **G-09.** A: a catch-up sends purge tombstones.
- **G-11.** A: plugin actions default to `origins: ["local"]`.
- **G-13.** "A, but including what you said as not strict." Round 2 (G2-03) confirms the reading.
- **G-18.** "Pass the variables, but persistent plugin can write." Round 2 (G2-06) covers the kill switch.
- **G-28.** A: `data_dir` is `<brain>.d/plugins/<name>/data/`.
- **G-33.** (1) A: `source` is not a memory field.
- **G-35.** "all rec, but 10 - adding an existing one updates it."

### Resolved questions (deep review rounds 2 and 3, 2026-10-21)

Stored as `decision:g2-NN` and `decision:s-NN`. They change the spec through the revision 6 table below.

- **G2-03.** A: every warning is strict. **G2-06.** B: a persistent plugin passes the cause on each write. **G2-10.** C: `created_by` and `updated_by` in place of `source`. **G2-11.** A, A.
- **S-01.** Hooks are plugins. **S-02.** The crash-loop disable is runtime state. **S-05.** Trust belongs to the code hash. **S-06.** Budgets, grace and priority come from the manifest only. **S-07.** One format, `remember-message` 1. **S-08.** Search does not page. **S-09.** Exit 4 means only a strict warning. **S-10.** No link events. **S-11.** No daemon in v1 (reversed 2026-10-22 [decision:2026-10-22-daemon-v1] (#738): the daemon stays to serve local HTTP MCP). **S-15.** No exit-75 retry. **S-16.** Search loads the pool once.

### Resolved questions (deep review round 5, 2026-10-21)

- `plugin_name` uses the settings segment grammar. `entry_name` keeps hyphens. (Amended 2026-10-22 by M-17: `entry_name` uses the same grammar, no hyphens.)
- `hook add` writes `enabled` true on the user layer.
- A successful `plugin trust` clears `disabled_reason` `untrusted`.

## Design

Normative: the spec. Summary by spec section of revision 5, so a reader knows where each rule is. The revision 6 table below changes several of these rows:

| Spec § | Contents | Key rules |
|---|---|---|
| 0 | Decisions | All locked items with tags (OQ-n, NQ-n, Q-A to Q-D, CF-n, SET-2, #515). §0.6: the revision 5 decisions (PP-01 to PP-18 and the FX answers). |
| 1 | Envelope and framing | JSON Lines on a Unix socket and on stdin; dotted type names (`backfill.done`, PP-07); 16 MiB per message; one request per connection (OQ-8); `request_id` matching; reply type `<request type>.result`; failed reply keeps `.result` with `error` (R1.11). |
| 2 | Manifest `plugin.json` | `remember-plugin` v1 document; `actions`, `filters`, `settings`, `backfill`, `data_version` (default 1, R2.14); `needs` (no `version_vector`, no `fortkeeps.*`); per-entry `budget`; the filter-name collision rule (R2.11, #511); manifest error detail (R2.5a); `--dev` (R2.19); `idle_timeout` (R2.20); the version glossary (§2.1a); the minimal plugin (§2.4). |
| 3 | Action and hook event input | `type: "event"`, `data{event_id, event_type, …, memory, entry}`; `cause.origin` (#577); default data without `needs` (OQ-11); once-process output ignored (OQ-13); persistent actions ack with `event.result` `{event_id}` or `error` (OQ-12, PP-08). |
| 4 | Filter point `search.query` | Rewrites the query text and filters (OQ-16); the same budgets (OQ-17). |
| 5 | Filter point `search.results` | Candidate ids with scores; reorder, drop, add by `id`; Remember loads the memories; additions cap (OQ-19); skip with a note (OQ-20, OQ-21); request trim over 16 MiB with exit code 0 (NQ-12, CF-9). |
| 6 | Control messages | `ready` (with `protocol`, `pid`, `index_version`, optional `backfill_needed`), `ping` and `ping.result` (with `index_version`), `shutdown` (Remember sends `idle`, PP-13); protocol mismatch (R6.2a); routing by `data.entry` (R6.4); the index state rule R6.13 (#514, #515); `backfill_needed = {applied_through_seq}` (R6.14, #599); persistent stderr log (R6.15). |
| 7 | Backfill | `full` and `catch_up` (`since_seq`, #599); batches of at most 100 memories or 4 MiB (OQ-25); live events during a backfill (OQ-26); the daemon or the foreground driver with a lock and resume (NQ-16, #518); pending backfills (CF-10, R7.3); the stored `index_version` after `backfill.done.result` (R7.5). |
| 8 | Search response | `memory.search.result` with `data.filters[]` provenance (#462); exit code 0 with warnings, 4 with `--strict` (#516, #574); the warning code table with a Strict column; human output and `--explain` (#608); the cold-start BM25 fallback (SET-2). |
| 9 | Failure semantics | Budgets and the chain cap (R9.1–R9.9), warm-up (R9.6a, #600); the strike rule (#433); malformed and oversize output is a strike (NQ-11); unknown fields (J3, NQ-10); plugin warnings go only to the plugin log and status (NQ-17); one shared hooks log (PP-18); isolation; built-ins as C-ABI libraries (#567). |
| 10 | Plugin CLI | The command table and the `plugin status --json` shape (#602). |

The spec has no conformance section. Spec §10 names `remember plugin test` as the conformance runner (#602). The validator in PR-3 and the fixtures in PR-6 are the conformance. Do not add a spec section for them.

Revision 6 amends the spec as follows. The spec editor applies these rows. Implementers do not choose among them.

| Rule | Amendment |
|---|---|
| §1, R1.3, every schema | `format` is `remember-message` and `version` is 1 on every message (log 008 JE-1.2, `[decision:s-07]`). `remember-plugin-message` no longer exists. This is not a protocol bump, because no plugin shipped (P-Q4). |
| R1.11 | An unreadable request closes the connection with no reply. Remember counts a strike. |
| R1.14, R8.13–R8.16, §8.2 | Under `--strict` or `REMEMBER_STRICT=1`, any warning gives exit code 4 `[decision:g2-03]`. Delete the §8.2 Strict column. Exit code 4 has no other meaning `[decision:s-09]`. A `plugin reindex` or `plugin enable` backfill that does not finish exits 1 with `plugin_backfill_incomplete`. R2.19 reads "exit code 0, 4 with `--strict`". Delete the "see the draft-update report" notes. In §8.2, rename `hook_preflight_failed` → `plugin_preflight_failed` and `hook_dispatch_failed` → `plugin_dispatch_failed`, and add `plugin_loop_halted` (010 H-5.6; M-19 N6 renamed it from `plugin_chain_halted`), `plugin_untrusted` (H-9.5) and `plugin_dev_mode` (R2.19). No `hook_` code remains (S-01; Interface R2, 2026-10-22). |
| R2.3, R2.3a, R2.15 | Trust belongs to the code hash `[decision:s-05]`. Remember asks for confirmation at `plugin add`, at an update (`plugin add` of an installed name), at `hook add` and at `plugin trust`, through the host confirm callback (log 010 H-9.4). Built-ins are trusted. `plugin enable` asks nothing. `plugin.<name>.enabled` is an ordinary setting with scopes `user` and `brain`. `plugin add` and `hook add` write `true` on the user layer. A successful `plugin trust` clears `disabled_reason` `untrusted`. |
| R2.11, R2.13, R9.1–R9.3 | Remember generates one key per plugin: `plugin.<name>.enabled`. A manifest setting key `enabled` makes the manifest invalid. `budget` is no longer reserved, and the entry-name collision rule is deleted. Each entry's budget, cold-start grace and priority come from the manifest only. No user setting overrides them. `time_budget_capped` is removed (R2.13, §8.2): a manifest budget larger than the time left in the chain cap is clipped silently, and the clipped value is the `budget_ms` Remember sends (2026-10-22, M-22 A, `decision:2026-10-22-M-22` #763, as S-06 says). `cold_start_grace` is 1 ms to 30 s, default 10 s, and `plugin status` shows it (2026-10-22, M-24 A, `decision:2026-10-22-M-24` #765). A manifest budget above 30 s stays invalid `[decision:s-06]`. |
| §2.2, R3.5a | A manifest action has a `filter` block: `tags.any`, `tags.all`, `match_before`, `created_by`, `updated_by`, `bins`, `not_caused_by`, and `origins` (`local`, `sync`, `import`; default `["local"]`). Remember evaluates it at write time (log 010 H-4.1) `[decision:g-11]`, `[decision:g2-10]`. `cause.hook` is `plugin/<name>/<entry>`. No `cause.plugin` field exists. |
| R2.16, R3.7 | Remove `source` from the memory fields. A plugin asks for `created_by` or `updated_by` `[decision:g-33]`. |
| R3.3, R3.16 | Six event types: `memory.created`, `memory.updated`, `memory.deleted`, `memory.restored`, `memory.purged`, `memory.expired`. A link change is a `memory.updated` on each end, with `changes.link` `[decision:s-10]`. |
| R3.6 | Every event has real `plugin`, `entry`, `data_dir` and `settings` values, because every hook is a plugin (log 010 H-10.3, `[decision:s-01]`). Delete the null values for hooks. |
| R3.9, R6.15 | `data_dir` is `<brain>.d/plugins/<name>/data/`. Remember keeps `index.json` and `stderr.log` in `<brain>.d/plugins/<name>/`, outside `data_dir` `[decision:g-28]`. |
| R3.14 | Delete the exit-75 retry. A non-zero exit is `failed`. `remember plugin rerun` reruns it `[decision:s-15]`. |
| R6.1, new R3.x | A persistent process also gets `REMEMBER_HOOK_DEPTH=1` and `REMEMBER_HOOK_NAME=plugin/<name>`. When it writes through the CLI for an event, it sets `REMEMBER_CAUSE_EVENT_ID` and `REMEMBER_HOOK_CHAIN` from that event's `data.cause` `[decision:g2-06]`. |
| R6.5 | The socket is `<runtime>/s/<16 hex>.sock`. The state file is the log 010 H-11.4 document. |
| R6.9, R7.3, R7.5a, R9.6a | Keep the daemon branches (#517, #518) `[decision:2026-10-22-daemon-v1]`, which reverses `[decision:s-11]`. When the daemon does not run, the CLI and the host run each job (log 010 H-11.6, H-11.7, H-13.4). |
| R2.16, R3.10 | Settings access is read-only. A plugin reads its own `plugin.<name>.*` values and the `remember.*` keys it lists in `permissions.needs` (for example `remember.search.max_results`) from the `settings` list of each request. No message writes a setting, and a run that calls `settings set`, `reset` or `import` fails with `setting_from_hook` (log 007 ST-7.10) `[decision:2026-10-22-plugin-settings-read]` (2026-10-22). |
| R7.4, R7.2 | A `catch_up` also sends each purge tombstone with a `change_seq` above `since_seq`, as `{id, bin: "purged", change_seq}`. The plugin deletes that id. A link change reaches a catch-up through the new `change_seq` of both ends `[decision:g-09]`. |
| R7.16 | A missing batch reply stops the stream. `plugin reindex` exits 1 with `plugin_backfill_incomplete` (S-09). |
| R8.3, R8 search data | Search has no `next_cursor` and no `has_more` `[decision:s-08]`. Add the 006 `keys` and `tags` facets. `total` is the chain length before the limit. |
| R9.13 | A crash loop sets `disabled_reason` `crash_loop` in that brain's `index.json`. It writes no setting `[decision:s-02]`. `plugin enable` clears it. |
| R9.19 | An unknown field is an error only in `run`, `permissions` and `filter` of `plugin.json`. The hooks config no longer exists. |
| R9.28 | The shared run log is `<brain>.d/hooks.log`. `plugin log --follow --json` emits one `remember-message` per line: `type` `plugin.run` for a run record, `plugin.stderr` for a stderr line. |
| §10 | The command table is log 010 §16: add `plugin show`, `plugin trust`, `plugin rerun`, `plugin remove [--delete-data]` (data kept by default), `plugin enable|disable [--scope brain]` (shortcuts for `settings set plugin.<name>.enabled`, M-19 N2), `plugin status --ack NAME` (clears the sticky `plugin_loop_halted`, M-19 N7), `plugin test NAME|DIR`, and `plugin add` as the update path. `disabled_reason` is `null`, `user`, `untrusted`, `crash_loop` or `protocol_mismatch` `[decision:g-05]`, `[decision:g-35]`. |
| `$id` | Replace `https://remember.fortkeeps.app/schema/plugin/v1/` with `https://itayavtalyon.github.io/remember/schema/plugin/v1/<file>.json` `[decision:g-06]`, R2-09. GitHub Pages serves the extracted schema files. |
| `plugin_name`, `entry_name` | One rule for both: `^[a-z][a-z0-9_]{0,39}$`, the same as a settings segment (log 007 ST-1.1). Remember normalizes a hyphen the user types to `_`. A hyphen in a manifest is `plugin_manifest_invalid`, with a fix that suggests the `_` form (2026-10-22, M-17 A). A manifest setting key uses the same pattern. Examples `url-expand` and `log-ids` become `url_expand` and `log_ids`. |

Rules that this log adds for the repo:

- PR-1. The spec file is the only normative text for the wire format. Code comments and other logs cite spec rule ids (`R6.13`), not copies.
- PR-2. Every change to the spec adds a revision section at the top of the changelog, newest first, with breaking changes, rule changes and the validator result.
- PR-3. The validator must pass with 0 failures before a spec change merges. It checks every schema against JSON Schema 2020-12, every example against its schema, every negative case, and the code checks of the manifest.
- PR-4. Revision 6 result (2026-10-22): 13 schemas, 29 examples, 81 negative cases, 0 code-check negatives (the CF-7 check is gone with S-06), 14 positive cases, 0 failures (81 after the 2026-10-22 Interface final-review fixes B2, B3). (Revision 5: 13, 29, 75, 1, 12, 0. Revision 4: 12, 25, 57, 1, 10, 0.)
- PR-5. Protocol 1 is frozen after revision 6. A breaking wire change after that bumps `protocol`. Revision 6 is a document revision of protocol 1, because no plugin shipped before it. The amendment table above is that revision. A wording fix after revision 6 is a changelog revision and does not bump `protocol`.
- PR-6. The in-app adapters (#459) and the desktop executables pass the same conformance suite. The suite reads the spec examples as fixtures.

## Cross-repo dependencies

| Repo | Change |
|---|---|
| remember | Adds `docs/PLUGIN-PROTOCOL.md`, the changelog, `schema/plugin/v1/`, the validator and the conformance fixtures. The engine (log 010) implements the Remember side. |
| remember-mac | The built-in plugins link as C-ABI libraries with the same messages (#458, #459, #567). The Plugins pane shows `remember plugin status`, including pending backfills and stale indexes. |
| fortkeeps-cloud | Webhooks use `remember-message` 1 `event` messages (NQ-13, #505). Cloud-run plugins use the same protocol. The Cloud-built index stays on the server as part of Cloud v1 enhanced search; nothing Cloud-built is downloaded into `plugins/<name>/data/` (2026-10-22, M-13; amends #456's cache sentence). |

## Examples

From the spec §6.3:

```json
{"format":"remember-message","version":1,"type":"ready","data":{"protocol":1,"pid":4242,"index_version":{"data_version":3,"model":"small"}}}
```

## Trade-offs

- A spec document over a C header: plugin authors in any language can follow it. The cost is a validator to keep the examples true.
- One request per connection (OQ-8) over multiplexing: simpler plugins and no head-of-line rules. The cost is a connect per request.
- Embedded schemas over separate files: one review surface. The cost is an extraction step.

## Verification Criteria

1. The validator passes with 0 failures on the repo copy.
2. The extracted schema files equal the spec blocks.
3. Every spec example is a conformance fixture, and the desktop and in-app adapters pass the suite.
4. Every rule id that the code cites exists in the spec.

## Known Limitations

- The schemas cannot express every rule (for example, the `data_version` match of R6.13). The validator has code checks for some of them; the conformance suite covers the rest.
- The spec is English only.

## Future Work

- **Web (remote HTTP) plugins** (`[decision:plugins-mobile-builtin-only]` #458): the same messages over HTTPS, with auth and a new trust model.
- **Multiplexing**: several requests on one connection, matched by `request_id`. This needs a protocol version bump.
- **A search result cache**: cache the filter chain output per query and index version, so a repeated search skips the plugins. It needs a cache key rule in the spec.
- A list reply with paging, if a plugin ever returns long lists.

## Implementation plan (draft, moves to `implementation-plans/` after the grill)

1. **Move the spec into the repo.** Copy revision 5 to `remember/docs/PLUGIN-PROTOCOL.md` and the changelog to `remember/docs/PLUGIN-PROTOCOL-CHANGES.md`. Apply the revision 6 amendment table in Design. Do not add a conformance section. Done: the spec cites logs 007–010 and the validator passes on revision 6. (2026-10-22: done on the box for this PR, M-06; the validator was updated for revision 6.)
2. **Validator in CI.** Add `scripts/validate_plugin_spec.py` and a docs-only CI job. Done: Verification 1.
3. **Schema extraction.** Write `schema/plugin/v1/*.json` from the spec and fail CI on a difference. Turn on GitHub Pages for the remember repo so the `$id` URLs resolve (R2-09, #659). Done: Verification 2.
4. **Conformance fixtures.** Export the spec examples, including the §2.4 hello and log-ids plugins (#601), to `tests/fixtures/plugin/`. Log 010 and `remember plugin test` use them. Done: Verification 3.
5. **Freeze protocol 1.** Leave `protocol` at 1. Record revision 6 in the changelog. The Interface review is done (PP-01 to PP-18). A later breaking wire change bumps `protocol` in a new log.
