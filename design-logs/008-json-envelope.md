# 008 - JSON style and the message envelope

**Status:** Design (grill closed 2026-10-21). J-Q1 to J-Q10 closed 2026-10-20. The deep review of 2026-10-21 (G-01 to G-36) reopened the log. Rounds 2 and 3 (simplification) closed the same day. Round 4 closed the same day. The grill is closed. Design states each winner. Amended 2026-10-22: §6 says the log wins where a JSON-STYLE row differs (M-05); `--as` replaces `--source` on writes (M-19 N8).
**Author:** Itay (+ agent facilitation)
**Created:** 2026-10-18

Cross-refs: [002](002-core-facade.md) (`remember_run` is byte-identical to CLI `--json`), [005](005-sync-foundations.md)
(`sync_id`), [006](006-ui-overhaul-engine.md) (E6 export, E12.8 output), [007](007-settings.md) (settings shapes, schema v6),
[009](009-plugin-protocol.md) (plugin messages), [010](010-plugins-and-hooks.md) (event messages).
fortkeeps-cloud [001](../../fortkeeps-cloud/design-logs/001-cloud-brain-core.md) (public API).
Sources: `fk-review/JSON-STYLE.md` (cited **JS**, revision of 2026-10-18 14:57, the normative style guide),
`drafts/remember/PLUGIN-PROTOCOL.md` revision 5 (**PP**), `fk-review/INTERFACE-REVIEW.md` (**IR**), and the
locked decisions named as `[key]` (#id).

> Drafting note: the same as log 007. This log records the decisions and the plan. JS stays the full style guide.
> The guide file is `remember/docs/JSON-STYLE.md` in the public remember repo (J-Q1).

## Background

Verified on the box copy of `remember` (`src/output.c`, `src/store_sqlite.c`) and in JS §3:

- CLI `--json` output today is flat: `{"version":1,"action":…,"entries":[…]}` (JS D1).
- Each memory shows the integer `id` and the UUID `sync_id` (D2). Locators use `--sync-id` (D3).
- The schema has `entries.id INTEGER PRIMARY KEY` and `entries.sync_id TEXT` (D4).
- Warnings go to stderr as text. The JSON has no warnings (D5).
- A failure in JSON mode has no JSON form on 0.2.0. Plan 16 (unreleased; Homebrew is 0.2.0) added `--json-errors`; #572 removes it before 0.3.0 (D8).
- `remember_run` (log 002) returns the same bytes as CLI `--json`, so the Mac app parses this output.
- Cloud log 001 exposes `sync_id` on the public API `[decision:cloud-e2-public-identity]` (#394).
- The plugin protocol (PP) and hooks (HK) send JSON messages on stdin and on a socket.

## Problem

Five surfaces (CLI, HTTP API, MCP, plugins and hooks, export files) grew five JSON shapes. A client that reads two surfaces needs two parsers. A field name (`id`) means a different thing on each surface. The integer id leaks a value that is local to one file. Remember must fix the shapes before 0.3.0, because the apps and Cloud start to depend on them.

## Questions and Answers

Decisions of 2026-10-18 (Itay), in their own words:

- `[decision:json-one-envelope]` (#492), changed by `[decision:json-envelope-no-id]` (#497): "The envelope has no id field: {format, version, type, request_id?, data?, error?, warnings?, next_cursor?, has_more?}. Domain ids live in data under explicit names ... Requests and replies match only by request_id."
- `[decision:json-format-names]` (#499): "three format names, each with its own version: remember-cli-message (v2), remember-api-message (v1, for HTTP and MCP) and remember-plugin-message (v1, for plugins, hooks and webhooks)."
- `[decision:json-data-wrapper]` (#477): a success puts its payload in `data`. A failure has `error`.
- `[decision:json-optional-warnings]` (#480): `warnings` appears only when it is not empty. "The schema marks warnings as optional, with minItems 1."
- `[decision:json-warnings-stdout]` (#483): "With --json, warnings go only into the warnings array on stdout."
- `[decision:json-paging-envelope]` (#486): "next_cursor and has_more live in the envelope ... only list responses include them. count and entries stay in data."
- `[decision:search-no-paging]` (#441): search has no paging in v1.
- `[decision:json-id-format]` (#479): hyphenated UUID, no type prefix; input accepts only that form.
- `[decision:json-local-id]` (#481): the integer row id is `local_id`, in CLI output only.
- `[decision:json-id-rename]` (#482): "The memory UUID previously called sync_id is called id on every JSON surface and in the SQLite schema. ... This needs a schema migration, planned next to the dotted-key rename before plan 19."
- `[decision:json-failed-reply-type]` (#498): "A failed reply keeps its <noun>.<verb>.result type and fills in error. There is no generic type error."
- `[decision:json-export-plain]` (#500): export files are plain documents with their own `format` and `version`.
- `[decision:json-cli-version-2]` (#487): CLI JSON goes to version 2 as a clean break with a changelog note.
- `[decision:json-unknown-fields]` (#478): an unknown field is an error in `hooks.json` and in `run` and `permissions` of `plugin.json`, and a warning elsewhere.
- `[decision:json-test-fixed-ids]` (#501): `REMEMBER_TEST_FIXED_IDS=1` works only in test builds.
- `principle:protocol-one-shape` (#465): a message has the same shape on every transport. (Its envelope text, with `id` and `body`, is superseded by #492 and #497.)
- `[decision:design-log-split]` (#485): pre-1.0 breaking changes are a clean break, no aliases.

Decisions of 2026-10-18 and 2026-10-19 (Itay) that change this log:

- `[decision:json-errors-in-envelope]` (#572): "With --json, failures go in the one envelope on stdout (same type, error filled, warnings in warnings[]), stderr empty; matches #498 and remember_call. --json-errors (plan 16, unreleased; Homebrew is 0.2.0) is removed before 0.3.0 ships. Plain mode keeps readable 'remember: ...' stderr lines."
- `[decision:search-paging-fields]` (#575): search is a list response and always carries `next_cursor` and `has_more` (`null`/`false` when complete). Amends #486.
- `[decision:positional-locator]` (#576): a positional locator accepts a UUID (matched as `id`) or digits (matched as the device-local `local_id`).
- `[decision:id-rename-cut]` (#529): the `sync_id` → `id` and `id` → `local_id` rename lands as one cut: engine v6, CLI JSON v2, the remember-mac decoder and a Cloud log 001 amendment, before any app or Cloud code links 0.3.0. Decision 394 is superseded by 482.
- `[decision:reopen-cloud-001]` (#550): Cloud log 001 is relabelled "Design (in review, reopened 2026-10-18)" and amended in place.
- `[decision:schema-v6-scope]` (#524), `[decision:release-0-3-0-scope]` (#527): one v6 step, in 0.3.0, with CLI JSON v2.
- `[decision:provenance-actors]` (#563): every entry gets `created_by` and `updated_by` actor objects `{kind, id}` in the v6 step. `kind` is `human`, `agent`, `tool`, `share`, `sync` or `import`.
- `[decision:quota-exceeded-envelope]` (#549): one error code `quota_exceeded`, HTTP 429, `detail {quota, limit, usage, resets_at?}`. A body size error is `invalid_argument` with `detail.reason` `body_too_large`.
- `[decision:version-handshake]` (#585): one handshake `{engine_version, schema, formats, min_client}` for `GET /v1/brain`, the daemon and MCP initialize. Below `min_client`: `upgrade_required` (HTTP 426).
- `[decision:library-handle-api]` (#530): `remember_open`, `remember_call(handle, request_json)` with `remember-api-message`, `remember_close`. `remember_run` stays for CLI byte-parity tests.
- `[decision:api-surface-log]` (#583): the HTTP routes, MCP tool schemas and the error-code catalog live in the API surface v1 log. This log links to it.
- `[decision:main-version-label]` (#586): `main` reports `0.3.0-dev` from one version constant.

### Resolved questions (2026-10-19)

- **J-Q2. Failures in `--json` mode and stderr.** Resolved: option (b), the failure envelope on stdout with `--json`, stderr empty. `--json-errors` is removed before 0.3.0 `[decision:json-errors-in-envelope]` (#572, FX-28).
- **J-Q3. Cloud public identity.** Resolved: option (a), #482 wins and Cloud 001 gets an amendment `[decision:id-rename-cut]` (#529, FX-2), `[decision:reopen-cloud-001]` (#550).
- **J-Q4. Positional locator.** Resolved: option (a), a UUID or digits (`local_id`) `[decision:positional-locator]` (#576, FX-32).
- **J-Q8. Migration order.** Resolved: option (a), one v6 step `[decision:schema-v6-scope]` (#524, FX-6).

### Resolved questions (grill, 2026-10-20)

- **J-Q1. Where JS lives.** `remember/docs/JSON-STYLE.md` in the public remember repo. This log keeps the contract. The guide keeps the rules and the examples. A wording fix edits the guide. A wire change that a reader cannot ignore bumps the format version in this log.
- **J-Q5. Search facets.** Search keeps the 006 key and tag facets. `data` also has `entries`, `count`, `total`, and `filters[]`. `total` is the number of ids after the filter chain and before offset and limit. `count` is the number returned. `next_cursor` is null and `has_more` is false. The plugin spec's search result list gains the facet fields.
- **J-Q6. `hook log --follow --json`.** Each line of `hook log --follow --json` and `plugin log --follow --json` is a `remember-cli-message` version 2 envelope with `type` `hook.run`. `data` is the log record. A bare record is not used.
- **J-Q7. Version of the brain export.** `remember-export` is version 2. Import reads version 1 (`sync_id`, 006 settings shape) and version 2 (`id`, the log 007 settings row). `remember-settings-export` stays version 1.
- **J-Q9. Actor objects.** A human CLI write is `kind` `human` and `id` null. A script hook forces `kind` `agent` and `id` the hook name. A plugin child forces `kind` `tool` and `id` `plugin/<name>/<action>`. Share is `kind` `share` and `id` null. Sync is `kind` `sync` and `id` the other device id. Import is `kind` `import` and `id` null. `--source` is rejected when `REMEMBER_HOOK_DEPTH` is 1 or more. `--actor ID` replaces only `id`. `ID` is 1 to 64 characters of `[A-Za-z0-9_./:-]`. v6 copies `kind` from the old `source`. `source` `unknown` becomes `kind` `human` and `id` null.
- **J-Q10. Envelope details.** When a command accepted `--offset`, the envelope field `offset` sits after `has_more` and before `warnings`. `error.fix` is a required non-empty string. A list cursor is opaque. A bad cursor is `invalid_argument`. A plugin that cannot parse a request closes the connection and sends no reply (log 009). `unknown.result` is only for the CLI, when the CLI cannot name the command.

### Resolved questions (deep review, 2026-10-21)

From `fk-review/QUESTIONS-2026-10-21.md`. Stored as `decision:g-NN`. Where a G answer differs from J-Q1 to J-Q10, the G answer wins.

- **G-08.** A (both ends of a link get new `updated_at`, `updated_by` and `change_seq`), "and if we do not have a VV for links, we should increment the one for the memory. We should discuss the VV and standarize what is considered an update." G-10 gives links their own vector, so a link change does not bump the memory vector (R2-07). The update table is round 2 (G2-02).
- **G-09.** A: a `tombstones` table in v6.
- **G-10.** A: links get `id`, `version_vector` and `deleted_at` in v6.
- **G-17.** B (detect a copy, re-mint `brain_id`, warn `brain_id_cloned`). The detection rule is round 2 (G2-05).
- **G-33.** (1) A, "one source of data": the memory `source` field is dropped in v6. (2) A: import keeps the file's `created_by` and stores `updated_by` as `{import, null}`.
- **G-36.** "all rec": item 6 (`brain new-id` envelope) and item 7 (`offset` only when passed).

### Resolved questions (deep review rounds 2 and 3, 2026-10-21)

From `fk-review/QUESTIONS-2026-10-21-round2.md` and `QUESTIONS-2026-10-21-round3-simplify.md`. Stored as `decision:g2-NN`, `decision:r3-00b` and `decision:s-NN`. Where these differ from J-Q1 to J-Q10 or G-NN, they win.

- **G2-02.** (1) A: the update table (JE-5). (2) A: a sync apply keeps the author's `updated_by`.
- **G2-05.** "compare to the known brain list and update that on move."
- **G2-10.** C: explicit actor filters, `--created-by` and `--updated-by`.
- **G2-16.** A, "but import does not read the old column, only these. Unless I am mistaken and export was released already." (It was not.)
- **R3-00b.** Yes: `remember-export` ships as version 1.
- **S-07.** Yes: one format, `remember-message` version 1.
- **S-08.** Yes: no `--offset`. Search does not page.
- **S-10.** Yes: no `memory.linked` or `memory.unlinked`.
- **S-13.** Yes: the positional and `--key` are the only locators.

### Resolved questions (round 4, 2026-10-21)

From `fk-review/QUESTIONS-2026-10-21-round4.md`. Stored as `decision:q4-NN`. Q4-01 A (history: the newest 50 rows per key, no age limit). Q4-02 A (integer grammar). Q4-03 A (every plugin write, hooks included, is `tool`). Q4-04 A (`device_id` stays for the tie-break only). Q4-05 "all ok".

## Design

Normative. JS gives the full rules and examples. This section states the contract that tests check. Where JS differs from this section, this section wins.

### 1. Format and versions

- JE-1.1. A message is one JSON object with `format`, `version` and `type`.
- JE-1.2. One message format exists: `remember-message`, version 1 `[decision:s-07]`. Every surface uses it: CLI `--json` stdout (success and failure), `remember_run`, `remember_call`, the HTTP API, MCP `structuredContent`, the plugin socket and stdin, hook stdin, the `ready` line, and Cloud webhooks. It replaces #499's three formats.
- JE-1.3. A change that a reader cannot ignore increments `version`. A new optional field or a new enum value does not. A plugin wire change increments the plugin `protocol` number (log 009), not `version`.
- JE-1.4. A reader that gets a higher `version` than it knows stops and names both versions (JS §1.7).
- JE-1.5. A stored file is a plain document: it starts with `format` and `version` and has no `type` `[decision:json-export-plain]`. Each document below is version 1:

| `format` | File |
|---|---|
| `remember-export` | `remember export` output (log 007 ST-8.3) |
| `remember-settings-export` | `remember settings export` output (log 007 ST-8.1) |
| `remember-plugin` | `plugin.json` (log 009) |
| `remember-plugin-trust` | `<data dir>/plugins/trust.json` (log 010 H-9.2) |
| `remember-plugin-state` | `<runtime>/plugins/<name>/<brain-id>.state` (log 010 H-11.4) |
| `remember-plugin-index` | `<brain>.d/plugins/<name>/index.json` (log 010 H-13.2) |
| `remember-brains` | `<data dir>/brains.json` (log 010 H-2.1) |

### 2. The envelope

- JE-2.1. Fields and order: `format`, `version`, `type`, `request_id`, `data` or `error`, `next_cursor`, `has_more`, `warnings` (JS §1.8, #492, #497). Absent optional fields are omitted. They are not null, except `next_cursor`, which is null on the last page.
- JE-2.2. The envelope has no `id` field (#497).
- JE-2.3. A response to a request has `type` `<noun>.<verb>.result` (`memory.add.result`). A failed response keeps that `type` and has `error` instead of `data` `[decision:json-failed-reply-type]`. No `type` `error` exists.
- JE-2.4. When the CLI cannot tell the command, the failure `type` is `unknown.result`.
- JE-2.5. A response has `data` or `error`, never both, never `null` `[decision:json-data-wrapper]`.
- JE-2.6. `error` has `code`, `message`, `fix`, and `retryable`, and an optional `detail`. `fix` is a non-empty string (J-Q10). HTTP status follows `error.code` (JS §1.8). The code catalog lives in the API surface v1 log.
- JE-2.7. `warnings` appears only when it holds one or more items `[decision:json-optional-warnings]`. Each item has `code` and `message`, and an optional `detail`.
- JE-2.8. With `--json`, Remember writes warnings only to `warnings` on stdout `[decision:json-warnings-stdout]`. A failure goes in the same envelope on stdout (same `type`, `error` filled, warnings in `warnings`), and stderr stays empty `[decision:json-errors-in-envelope]` (#572). No `--json-errors` flag exists. Plain mode keeps readable `remember: …` stderr lines.
- JE-2.9. `request_id` is a UUID v7. The sender of a request sets it. The reply copies it. HTTP sets it on every response. CLI output has no `request_id`, because it is not a reply.
- JE-2.10. `next_cursor` (string or `null`) and `has_more` (boolean) appear only on responses that page, always together `[decision:json-paging-envelope]`. A list command pages by cursor. A cursor is opaque, and a bad cursor is `invalid_argument` (J-Q10). `count`, the list and an optional `total` stay in `data`.
- JE-2.11. Search does not page `[decision:search-no-paging]`, `[decision:s-08]`. A search response has no `next_cursor` and no `has_more`. Search returns the top `--limit` results, or `remember.search.max_results` (log 007 ST-5.6). `data` has `count`, `entries`, `total`, `query`, `ranked_by`, `filters`, and the 006 `keys` and `tags` facets (J-Q5). `total` is the number of ids after the filter chain, before the limit.
- JE-2.12. No command has `--offset`, and no envelope has `offset` (S-08). This breaks the 0.2.0 `--offset` flag.

### 3. Identity

- JE-3.1. A memory's id is `id`: a lowercase hyphenated UUID v7 with no prefix `[decision:json-id-format]`.
- JE-3.2. Input accepts only that form. A prefixed or brace form fails with `invalid_argument`.
- JE-3.3. The integer row id is `local_id`. Only CLI output shows it `[decision:json-local-id]`.
- JE-3.4. A CLI locator is the positional or `--key KEY` `[decision:s-13]`. The positional accepts a UUID (matched as `id`) or digits (matched as `local_id`) `[decision:positional-locator]` (#576). Any other positional fails with `invalid_argument`. The plain message is `"abc" isn't a memory id. Use a UUID, a number from 'remember list', or --key.` No `--id`, `--local-id` or `--sync-id` flag exists.
- JE-3.5. HTTP, MCP, plugin messages, events and export files never contain `local_id`.
- JE-3.6. A reference to another object is `<noun>_id` (`memory_id`, `event_id`, `backfill_id`).
- JE-3.7. The SQLite schema uses the same names: `entries.id TEXT` (the UUID) and `entries.local_id INTEGER PRIMARY KEY` `[decision:json-id-rename]`.
- JE-3.8. The env name `REMEMBER_SYNC_ID` and the field `sync_id` are removed. No alias exists (#485).
- JE-3.9. `remember brain new-id` replaces `brain_meta.brain_id` with a new lowercase UUID v7 and prints that id `[decision:brain-id]` (#523). The command does not change `change_seq`, a version vector, or a memory. `--json` returns `type` `brain.new_id.result` with `data {brain_id, previous_brain_id}` (G-36.6).
- JE-3.10. On open, Remember checks the brain against `brains.json` (log 010 H-1.6) `[decision:g2-05]`:
  - When an entry has this `brain_id` at another path, and a brain with this `brain_id` exists at that path, this brain is a copy. Remember mints a new `brain_id` for this brain, adds this path as a new entry, and warns `brain_id_cloned`.
  - When an entry has this `brain_id` at another path, and that path has no such brain, the brain moved. Remember changes that entry's path.
  - When no entry has this `brain_id`, Remember adds an entry.
- JE-3.11. Import of a `remember-export` into an empty brain writes the file's `brain_id`, unless `brains.json` lists an existing brain with that id. Then import mints a new `brain_id`. Import into a brain that has rows keeps the target `brain_id`.

### 4. Fields and values

The JS §1.1–§1.18 rules apply without change: snake_case names, no data in member names, `_at` instants with milliseconds and `Z`, `_on` dates, duration and size strings with `_ms` and `_bytes` twins, plain boolean names, `[]` for an empty list, `null` for no value, open enums, integers only (scores excepted), UTF-8 without BOM, the documented key order, JSON Lines for logs and streams, and no comments.

- JE-4.1. An unknown field is an error in `run`, `permissions` and `filter` of `plugin.json` (`plugin_manifest_invalid`) (log 009 R9.19). Everywhere else it is a warning `unknown_field`, once per file or stream `[decision:json-unknown-fields]`.
- JE-4.2. `get`, `add`, `update` and `rekey` return `data.memory`. A list returns `data.entries` and `data.count` (#497).
- JE-4.3. A memory in `add` output has the same fields as in `get`, including `links` (JS D6).
- JE-4.4. Memories and settings rows (log 007 ST-7.9) carry actors. An actor is `{kind, id}` `[decision:provenance-actors]` (#563), `[decision:s-04]`. `kind` is `human`, `agent`, `tool`, `share`, or `import`. `id` is a string or null. A memory has `created_by` and `updated_by`. A settings row has `updated_by`.
  - A person at the CLI gets kind `human` and id null.
  - Every plugin write, hooks included, gets kind `tool` and id `plugin/<name>/<entry>`. A hook's id is `plugin/<name>/run` `[decision:q4-03]`. This amends J-Q9. An agent that a hook starts inherits the run environment, so its writes get the same stamp.
  - A write with `--as share` gets kind `share`. The id is null, or the `--actor` value.
  - Import keeps the file's `created_by` and stores `updated_by` `{kind: import, id: null}` on each row that it writes `[decision:g-33]`.
  - Sync keeps the author's `created_by` and `updated_by` (G2-02). No actor has kind `sync`.
  - `--actor ID` replaces only `id`. `ID` is 1 to 64 characters of `[A-Za-z0-9_./:-]`.
  - A person at the CLI may pass `--as KIND` (`human`, `agent`, `tool`, or `share`), which replaces `--source KIND` (2026-10-22, M-19 N8). That flag replaces `kind` only. It does not clear an `--actor` id.
  - When `REMEMBER_HOOK_DEPTH` is 1 or more, Remember rejects `--as` and `--actor`. The error is `invalid_argument`.
- JE-4.5. A memory has no `source` field `[decision:g-33]`. The actor filters name the actor `[decision:g2-10]`: search `--created-by KIND` and `--updated-by KIND`, hook filters `created_by` and `updated_by`, and plugin `needs` items `created_by` and `updated_by`.
- JE-4.6. The error-code catalog lives in the API surface v1 log (#583). `quota_exceeded` has `detail {quota, limit, usage, resets_at?}` (#549).

### 5. What a write changes

- JE-5.1. Every write follows this table `[decision:g2-02]`. ✓ means the write sets that field. "—" means it does not.

| Write | `updated_at` | `updated_by` | memory vector | `change_seq` | Event |
|---|---|---|---|---|---|
| body, tags, key or `expires_at` changes (`update`, a merging `add`, `rekey`) | ✓ | ✓ | +1 | ✓ | `memory.updated` |
| `link` or `unlink`, on each end | ✓ | ✓ | — (the link row's vector +1) | ✓ (and the link row) | `memory.updated` with `changes.link` |
| `delete` (to Deleted) | ✓ | ✓ | +1 | ✓ | `memory.deleted` |
| restore | ✓ | ✓ | +1 | ✓ | `memory.restored` |
| `purge` | row removed | — | — | a `tombstones` row | `memory.purged` |
| expiry marking (`expired_at`, #570) | — | — | — | — | `memory.expired` |
| a write that changes nothing | — | — | — | — | none |
| sync apply | the author's | the author's | merged | ✓ | the event of that change, `cause.origin` `sync` |
| import | the file's | `{import, null}` | the file's, merged | ✓ | the event of that change, `cause.origin` `import` |

- JE-5.2. `brain_meta.change_seq` is the brain counter. Each row that a write changes (an entry, a link, a brain settings row, a tombstone) gets the next counter value, in write order, in the same transaction. Purge deletes the entry row and inserts a `tombstones` row with the next value `[decision:g-09]`. Unlink sets `entry_links.deleted_at` and does not delete the row `[decision:g-10]`. A user-layer settings write does not change the counter. Purge never lowers the counter. Device confirmation of tombstones (R2-02) belongs to the sync log.

### 6. Surfaces

The top-level shape per surface is JS §2, read with JE-1.2, JE-2.10–2.12 and JE-1.5. Where a JSON-STYLE row differs, this log wins (2026-10-22, M-05 C: Interface revised `JSON-STYLE.md`; this rule is permanent).

### 7. Tests

- JE-7.1. With `REMEMBER_TEST_FIXED_IDS=1`, a test build makes ids and timestamps predictable. A release build ignores the variable `[decision:json-test-fixed-ids]`.
- JE-7.2. JSON Schema 2020-12 files for the format and each document live in `remember/schema/v1/`, and a test validates every golden output against them (log 009 for the plugin schemas).

### 8. Migration (part of schema v6, log 007 §10)

In the v5 → v6 transaction, before the log 007 settings steps:

1. Rebuild `entries` with `local_id INTEGER PRIMARY KEY` (the old `id`) and `id TEXT NOT NULL UNIQUE` (the old `sync_id`). Add `change_seq INTEGER NOT NULL`, `created_by TEXT NOT NULL`, and `updated_by TEXT NOT NULL`. Fill both actors from the old `source`: `kind` copies `source`, `source` `unknown` becomes `human`, and `id` is null. Do not copy the `source` column (G-33). Keep every other value.
2. Rebuild `entry_links` with `id TEXT NOT NULL UNIQUE` (a new UUID v7 per link), `version_vector TEXT NOT NULL` (`{}`), `deleted_at TEXT` (null), and `change_seq INTEGER NOT NULL` `[decision:g-10]`. Keep the integer foreign keys (`entry_links`, `entry_tags`, `conflicts`, `entries_fts.rowid`) on `local_id`.
3. Create `brain_meta` with exactly one row: `brain_id TEXT NOT NULL` (a new lowercase UUID v7), `change_seq INTEGER NOT NULL` (0), `min_reader_version TEXT NOT NULL` (`0.3.0`) (#523, #543, `[decision:g-20]`). Create `tombstones(id TEXT PRIMARY KEY, change_seq INTEGER NOT NULL, purged_at TEXT NOT NULL)`, empty `[decision:g-09]`. Set `change_seq` to 0 on every entry and link.
4. Rebuild the indexes and the FTS triggers under the new names. Run `PRAGMA foreign_key_check` and `PRAGMA integrity_check` before `COMMIT`.

- JE-8.1. `import --from-db` reads a v4 or v5 source with `sync_id` and a v6 source with `id`. These are SQLite files that 0.2.0 wrote, so this reader stays.
- JE-8.2. `import` of a JSON, CSV or Markdown export reads only the v6 names (`id`, `created_by`, `updated_by`). No export shipped before 0.3.0, so no old-name reader exists `[decision:g2-16]`, `[decision:r3-00b]`.
- JE-8.3. The CSV header is `id,key,body,tags,created_by,updated_by,created_at,updated_at,expires_at,deleted_at,bin,version_vector,links`. The actor, tag, vector and link cells hold JSON. Markdown front matter uses the same names (amends 006 E6.b, E6.c).

## Cross-repo dependencies

| Repo | Change |
|---|---|
| remember | `src/output.c` (every writer), `src/cli.c` (remove `--json-errors` (#572), `--offset`, `--sync-id`), `src/cmd_locator.c`, `src/cmd_graph.c`, `src/store_sqlite.c` (schema v6), skills, README, handoff docs (JS D1–D11). |
| remember-mac | RememberKit decodes `remember-message` 1: `data` wrapper, `data.memory`, `id` (UUID) and `local_id`, actors, `warnings` on stdout, failures in the stdout envelope (#572). Spotlight uses the memory `id` (the value of the old `sync_id`, #389). The app must ship after the engine (#396). |
| fortkeeps-cloud | `remember-message` on every route; `id` replaces `sync_id` (#529); error shape per JS D12; `GET /v1/memories/{id}` returns `data.memory` (D29); `/v1/sync/pull` uses `id` (D13). Cloud 001 is reopened and amended (#550). |

## Examples

```json
{"format":"remember-message","version":1,"type":"memory.get.result",
 "data":{"memory":{"id":"01929c4e-8b2a-7cc0-9f4e-3b1d2a5c6e7f","local_id":42,"key":"project:fortkeeps","body":"…","tags":["project:fortkeeps"],"created_by":{"kind":"human","id":null},"updated_by":{"kind":"human","id":null},"created_at":"2026-10-18T09:00:00.000Z","updated_at":"2026-10-18T09:00:00.000Z","expires_at":null,"deleted_at":null,"bin":"live","version_vector":{},"links":[]}}}
```

```json
{"format":"remember-message","version":1,"type":"memory.get.result",
 "error":{"code":"not_found","message":"No memory has the key \"nope\".","fix":"Run 'remember list' to see keys.","retryable":false}}
```

```json
{"format":"remember-message","version":1,"type":"memory.list.result",
 "data":{"count":20,"entries":[…]},"next_cursor":"eyJ1Ijoi…","has_more":true}
```

## Trade-offs

- One format on every surface over three: one parser and one schema set. The cost is that a CLI-only change and a plugin-only change share one `version` number. Plugin semantics have their own `protocol` number.
- `type` on every message over HTTP routes only: logs and streams are self-describing. The cost is a few bytes.
- No envelope `id`: no field means two things. The cost is that a reader looks in `data` for the object id.
- A clean break over a fallback: no second writer to test. The cost is that every script that reads `--json` or uses `--offset` must change once.
- Cursor paging only: one paging model. The cost is that a client cannot jump to page 5.

## Verification Criteria

1. Every CLI `--json` golden output validates against the `remember-message` 1 schema.
2. No output has an envelope `id`, `sync_id`, `source`, `offset` or `"type":"error"`.
3. A failed command keeps `<noun>.<verb>.result` and has `error` and no `data`.
4. A warning appears only in `warnings`, and stderr is empty with `--json`, also for a failure (#572).
5. `warnings` is absent when there is no warning.
6. A list response has `next_cursor` and `has_more` at the top level. A single-item response and a search response have neither.
7. A search response has the 006 facets and `data.total` equal to the chain length before the limit.
8. A prefixed id (`mem_…`) fails with `invalid_argument`. `--id`, `--local-id`, `--offset` and `--json-errors` are unknown options.
9. HTTP and MCP outputs have no `local_id`.
10. Release builds ignore `REMEMBER_TEST_FIXED_IDS`.
11. M1: v5 → v6 keeps every `sync_id` value as `id` and every integer as `local_id`. M2: links, tags, conflicts and FTS still resolve. M3: a failure in the middle leaves the brain at v5 (failing-VFS test).
12. `remember export`, then `import` into an empty brain, gives the same memories, links and brain settings. `updated_by` becomes `{import, null}`.
13. `remember get 42` and `remember get <uuid>` find the same memory (#576).
14. v6 creates one `brain_meta` row with a UUID v7 `brain_id` and `change_seq` 0, or 1 when log 007 §10 migrated a bucket-prefix row. The next memory write stores the next value. `remember brain new-id` replaces `brain_id` and does not change a version vector.
15. A plugin write stores `updated_by` `{tool, "plugin/<name>/<entry>"}`. A hook write stores `{tool, "plugin/<name>/run"}`. `--as` and `--actor` on either fail with `invalid_argument`.
16. Purge of a deleted row removes it and writes one `tombstones` row with a new `change_seq`.
17. `link` changes `updated_at`, `updated_by` and `change_seq` on both ends and does not change either memory vector. Unlink keeps the `entry_links` row with `deleted_at` set.
18. A synced change keeps the author's `updated_by`.
19. A copied brain file gets a new `brain_id` and warns `brain_id_cloned` on its first open. A moved brain keeps its `brain_id`, and its `brains.json` entry gets the new path.
20. `remember search --created-by agent` returns only memories whose `created_by.kind` is `agent`.

## Known Limitations

- Old scripts that read `.entries`, `sync_id`, `source` or the old `version` break once (#487).
- The `local_id` of a memory differs on each device. A script that stores it breaks after an import into another brain.
- Search has no cursor. A client cannot fetch result 1001.
- A brain copied while its original path is gone looks like a move, so both copies keep one `brain_id` until one of them opens next to the other.

## Future Work

- Sync side of tombstones (the sync log): a tombstone is deleted once every known device confirmed it, and a device silent for 90 days stops counting (R2-02, #652). A device that returns after that cutoff does a full re-sync, and its rows the server never saw become conflicts, not new rows (R3-02, #662).
- Search paging or a search result cache (a cursor on a stored result set), if users need more than `max_results`.
- Multiplexing several requests on one socket connection (PP).
- Web (remote HTTP) plugins.

## Implementation plan (draft, moves to `implementation-plans/` after the grill)

1. **Schema v6 identity rename.** Write the §8 steps in the one v5 → v6 migration (log 007 §10). This migration lands before the rewritten plan 19. Done: tests 11, 14 (M1–M3).
2. **Store and locators.** Rename `sync_id` to `id` in `Entry`, `store_*` and the locator code. Remove `--sync-id` and `--offset`. Make the positional take a UUID or digits (#576). Add `remember brain new-id` and the JE-3.10 open check. Done: tests 8, 13, 14, 19.
3. **Actors and the write table.** Store actors on memories and settings rows. Apply JE-5.1 to every write path. Done: tests 15–18, 20.
4. **Output writer.** Rewrite every writer in `src/output.c` to the JE-2 envelope with `format` `remember-message`. Add `data.memory` for single-memory commands, `links` in write output, `warnings` on stdout, and cursor fields on lists. Done: tests 1–7.
5. **Failures in the envelope.** Put the failure envelope on stdout with `--json`, with `unknown.result` when the command is unknown. Remove plan 16's `--json-errors` before 0.3.0 ships, and re-extract plans 16–18 and 22 tests and SKILL.md that cite it (#572). Done: tests 3, 4, 8.
6. **Schemas and goldens.** Add `remember/schema/v1/*.json` and a test that validates every golden file. Add `REMEMBER_TEST_FIXED_IDS` to test builds. Done: tests 1, 10.
7. **Export formats.** Write the JSON, CSV and Markdown exports with the v6 names (JE-8.2, JE-8.3). Done: test 12.
8. **Docs.** Change the skills, README, handoff docs and the CHANGELOG ("JSON: one format, `remember-message` 1, clean break") per JS D9–D11.
9. **remember-mac.** Change RememberKit decoding and Spotlight ids. Done: remember-mac tests that decode the new goldens.
10. **fortkeeps-cloud.** Amend Cloud 001 (#529, #550) and use `remember-message` on every route (D12, D13, D29).
