# Fortkeeps JSON style guide

## Changelog

- **2026-10-25 (rev 2.2, architect; Itay approved D-01, D-03 and the date answer as written).**
  - The settings `data` objects follow log 007 ST-7.11a–7.11b: one setting object, `setting.set|reset.result` `{setting, scope, old_value, changed}` (plus `action` on reset), `settings[]`, `changes[]`, and import `rows[]`. The §2 table shows them.
  - The settings history JSON field is `changed_at`, not `at` (§1.3).
  - The new enum `action` is `set|tombstone|delete|none` (§1.10).
  - Settings `list` pages 100 items by default, `history` 50, and both allow at most 1000 (§1.11).
  - Human dates are a CLI rule, not JSON: JSON keeps RFC 3339 UTC (§1.3, log 007 ST-7.16).
  - A bad value in `settings import` is the warning `setting_invalid`, and the row is skipped with reason `invalid_value`. An unknown key is `setting_import_skipped` with reason `unknown_key`. `settings reset --all` or `--namespace` without `--yes` fails with `confirmation_required`.
  - PLUGIN-PROTOCOL is revision 7.
- **2026-10-22 (rev 2.1, Interface).** Applies Itay's M-22 A, M-23 A and M-24 A.
  - `time_budget_capped` is gone. A budget larger than the time left in the chain cap is clipped silently, and the clipped value is the `budget_ms` that Remember sends.
  - The CLI warning `flag_meaning_changed` is added (§1.8a, §1.10).
  - `cold_start_grace` is 1 ms to 30 s, default 10 s, and `plugin status` `entries[]` shows it.
  - The architect must recopy this file to `fk-merged/remember/docs/JSON-STYLE.md`.
- **2026-10-22 (rev 2, Interface, M-05 C).** This pass aligns the guide with log 008, PLUGIN-PROTOCOL rev 6, and Itay's S-, G2- and M- answers. Log 008 §6 makes §2 of this guide normative, read with JE-1.2, JE-1.5 and JE-2.10–2.12. Where a row here differs from log 008, the log wins.
  - **Formats (S-07):** one message format, `remember-message` version 1. The three formats `remember-cli-message`, `remember-api-message` and `remember-plugin-message` are gone.
  - **Paging (S-08):** `--offset` and the envelope `offset` field are gone. Search has no `next_cursor` and no `has_more`. Paging fields stay in the envelope for list responses only.
  - **Plugin settings (S-06):** budget, grace and priority come from the manifest only. `plugin.<name>.enabled` is the only generated key. The CF-7 collision text is gone. `cold_start_grace` is listed as a manifest field.
  - **Hooks (S-01):** hooks are plugins. `<brain>.hooks.json`, `remember-hooks`, `hooks_config_invalid` and the `hook list|show|…|resume` command family are gone. `hook add` and `plugin rerun` remain.
  - **Warning codes:** the code catalog is PLUGIN-PROTOCOL §8.2 (`plugin_*`, including `plugin_loop_halted`, M-19 N6). No `hook_` code exists.
  - **Strict (G2-03, S-09):** under `--strict`, every warning exits 4. Exit 4 has no other meaning.
  - **Names (M-17):** plugin names, entry names and setting segments all use `^[a-z][a-z0-9_]{0,39}$`. A typed hyphen is normalized to `_`. A hyphen in a manifest is `plugin_manifest_invalid`, with a fix that names the `_` form.
  - **Actors (G-33, G2-10, M-19 N8):** `source` is replaced by the actors `created_by` and `updated_by`. The CLI flag is `--as KIND`.
  - **Locators (S-13):** `--id` and `--local-id` are gone. The positional takes a UUID or digits, or you use `--key`.
  - **Files (JE-1.5):** the document list is updated. The examples are updated.
  - Section 3 (the old deviation list D1–D31) is closed. Its open items now live in logs 007–010 and the spec.
- **2026-10-18 (rev 1).** Itay's rulings: one envelope, the `data` wrapper, no envelope `id`, `.result` reply types, `id` and `local_id`, warnings on stdout with `--json`, and the unknown-field rule.
- **2026-10-16.** Proposal.

Owner: Interface. Status: normative through log 008 §6.
Covers every JSON surface:
- CLI `--json`, `remember_run` and `remember_call`;
- the HTTP API, and MCP `structuredContent`;
- the plugin socket and stdin, hook stdin, and the `ready` line;
- Cloud webhooks;
- stored documents (exports, `plugin.json`, plugin state and index files, `brains.json`);
- the run log.

The plugin wire details are in `remember/docs/PLUGIN-PROTOCOL.md` (PP). The design decisions are in logs 007 (settings), 008 (envelope), 009 (protocol) and 010 (plugins and hooks).

**Compatibility policy (Itay, 2026-10-18):** before 1.0, a breaking change ships as a clean break with a CHANGELOG note. It has no aliases and no compatibility shims (PP R9.25, J9). The typed-input `-`→`_` normalization (M-17, 007 ST-1.3) is an input convenience, not an alias. The one-release `--trash` meaning warning (M-21 B, 010 H-16.9a) is a warning, not an alias.

---

## 1. Rules

Each rule states the rule, says why, and gives a correct example (✓) and a wrong one (✗).

### 1.1 Key casing: snake_case

- Rule: every JSON object key is lowercase `snake_case`, in ASCII `[a-z][a-z0-9_]*`. This holds on every surface.
- Data never becomes a JSON key. Setting keys, tag names, plugin names and entry names are **values** (007 ST-1.6). The one exception is `version_vector`, a map from `device_id` to a counter, which is opaque to clients.
- Why: one casing means no mapping layer between surfaces. A schema can validate values, but it cannot validate data used as keys.
- ✓ `{"settings":[{"key":"remember.search.max_results","scope":"brain","value":20}]}`
- ✗ `{"syncId":"…"}` · `{"settings":{"remember.search.max_results":20}}`

### 1.1a Names (M-17, 2026-10-22)

- Rule: a plugin name, an entry name, and each settings segment match `^[a-z][a-z0-9_]{0,39}$`. A setting key is `namespace "." segment {"." segment}`. It has at most 3 segments after the namespace and at most 96 characters. The namespaces are `remember.` and `plugin.<name>.` (007 ST-1.1–1.2, PP R2.22).
- Typed input (CLI arguments, `settings set` keys) is normalized: `-` becomes `_`. Output always shows the normalized name.
- A manifest is not typed input. A hyphen there is `plugin_manifest_invalid`, and its fix names the `_` form.
- An entry id on the wire is `plugin/<name>/<entry>`. It appears in `cause.hook`, in the actor id of a plugin write, in `filters[].name` (as `<name>/<entry>`) and in `REMEMBER_HOOK_NAME`. A hook's entry is `run`.
- ✓ `"name":"url_expand"` · `"hook":"plugin/url_expand/fetch_title"`
- ✗ `"name":"url-expand"` in `plugin.json` · `"plugin.url-expand.enabled"` on the wire

### 1.2 IDs

- Rule: a public id is a UUID string in canonical form: lowercase, hyphenated, 36 characters. On input, upper or lower case is accepted, hyphenated only. Any other form is refused with `invalid_argument` (008 JE-3.2).
- No type prefixes (`req_`, `evt_`, `mem_`). The field name says what the id is.
- Every id Remember mints is UUID v7: memory `id`, `brain_id`, `device_id`, `event_id`, `batch_id`, `run_id`, `backfill_id` and `request_id`.
- An object's own id is `id`. A reference is `<noun>_id`: `memory_id`, `event_id`, `backfill_id` (JE-3.6).
- **The integer row id is `local_id`.** It appears only in CLI output. It never appears in HTTP, MCP, plugin messages, events or export files, and it is never identity (JE-3.3, JE-3.5).
- `key` is a lookup alias, not an id.
- ✓ `"id":"01a14513-6501-7928-b9c4-2cc649cb59a9","local_id":3` (CLI only) · `"memory_id":"01a14513-…"`
- ✗ `"sync_id":"…"` · `"id":"01A145136501…"` · `"local_id":3` in an HTTP body · `"request_id":"req_01a1…"`

### 1.2a CLI locators (S-13, #576)

- A memory is found by the positional or by `--key KEY`. The positional accepts a UUID, matched as `id`, or digits, matched as `local_id`. Any other positional fails with `invalid_argument`: `"abc" isn't a memory id. Use a UUID, a number from 'remember list', or --key.` (JE-3.4).
- No `--id`, `--local-id` or `--sync-id` flag exists. `REMEMBER_SYNC_ID` and `sync_id` are gone, with no alias (JE-3.8). The hook env is `REMEMBER_ID`, which is `data.memory.id`.
- HTTP and MCP use `id` (`GET /v1/memories/{id}`, `remember_get {"id":"…"}`).

### 1.3 Timestamps and dates

- Rule: an instant is an RFC 3339 UTC string with `Z` and exactly 3 fraction digits, in a field ending in `_at`. A calendar date is `YYYY-MM-DD`, in a field ending in `_on`.
- Input accepts any RFC 3339 offset and normalizes it. Output is always canonical. JSON has no epoch numbers and no local time.
- ✓ `"occurred_at":"2026-10-14T12:06:31.512Z"` · ✗ `"created":"1697461061"` · `"exported_at":"2026-10-16T08:00:00Z"` · `"at":…` (settings history uses `changed_at`)
- Human output is not JSON. It shows the short local form `21 Oct at 12:04`, with the year when it is not the current year (log 007 ST-7.16). `--json` never does.

### 1.4 Durations

- **Configured** durations are settings, a hook `timeout`, a manifest `budget`, `cold_start_grace` or `idle_timeout`. Each is a string with one unit (`ms`, `s`, `m`, `h`). Output adds an integer `_ms` twin. Input accepts only the unit string.
- **Measured** durations are integer `_ms` fields only (`duration_ms`, `cold_start_ms`). Values that Remember computes and sends to a plugin, such as `budget_ms`, are also `_ms` integers.
- ✓ `"timeout":"60s","timeout_ms":60000` · `"duration_ms":900` · ✗ `"timeout_seconds":60` · `"budget_ms":"800ms"`

### 1.5 Sizes

- The rule is the same as for durations. A configured size is a string with a binary unit (`B`, `KiB`, `MiB`), and output adds a `_bytes` twin. A measured size or a constant is an integer `_bytes` field.
- ✓ `"max_batch_size":"4MiB"` · `"limits":{"max_bytes":262144}` · ✗ `"size":"256 KB"`

### 1.6 Booleans

- Use plain names: `enabled`, `truncated`, `changed`, `dev`. `has_` is used only in `has_more`. Never `is_`. A boolean is never tri-state: when "unknown" is a real state, use an enum.
- ✓ `"enabled":true` · ✗ `"is_enabled":true`, `"enabled":null`

### 1.7 Versioning

- Every message starts with `format` and `version` (§1.8), and every stored document does too (§1.7b).
- **One message format (S-07, 008 JE-1.2): `remember-message`, version 1, on every surface.** Each document format has its own `version`, starting at 1 (JE-1.5).
- Bump rules: an optional field, a new enum value, a new message `type` or a new warning code doesn't bump `version`. A rename, a removal, a type change or a change in meaning does. A reader refuses a `version` higher than it knows and names both versions (JE-1.4).
- A plugin wire change bumps the plugin `protocol` number, not `version` (JE-1.3, PP R9.23). Protocol 1 is frozen at PP revision 6.
- ✓ `{"format":"remember-message","version":1,"type":"memory.list.result","data":{…},"next_cursor":null,"has_more":false}`
- ✗ `{"format":"remember-cli-message","version":2,…}` · `{"schema":"v1",…}` · a CLI response without `format`

### 1.7a `type` and `request_id`

- A request type is `<noun>.<verb>` (`memory.add`), or a plugin type from the PP §1.2 table (`event`, `ping`, `filter.search.query`, `backfill`, …).
- **A reply's type is the request type plus `.result`.** There are no other reply names: no `pong` and no `event_ack`.
- **A failed reply keeps its `.result` type and fills `error` instead of `data`.** No `type: "error"` exists. When the CLI cannot tell which command was meant, the type is `unknown.result` (JE-2.3–2.4).

| CLI command / API route / MCP tool | reply `type` |
|---|---|
| `add` / `POST /v1/memories` / `remember_add` | `memory.add.result` |
| `get` / `GET /v1/memories/{id}` / `remember_get` | `memory.get.result` |
| `list` / `GET /v1/memories` / `remember_list` | `memory.list.result` |
| `search` / `GET /v1/memories/search` / `remember_search` | `memory.search.result` |
| `update`, `delete`, `purge`, `rekey`, `link`, `unlink` | `memory.<verb>.result` |
| `tags`, `conflicts`, `conflict accept` | `tag.list.result`, `conflict.list.result`, `conflict.accept.result` |
| `settings list\|get\|set\|reset\|explain\|history\|import` | `setting.<verb>.result` |
| `plugin add\|list\|show\|trust\|enable\|disable\|remove\|reindex\|rerun\|status\|log\|test` | `plugin.<verb>.result` |
| `hook add` (the only `hook` command, S-01) | `hook.add.result` |
| `brain list\|prune\|new-id` (M-19 N5) | `brain.list.result`, `brain.prune.result`, `brain.new_id.result` |
| `dispatch`, `sweep [--all]` | `dispatch.result`, `sweep.result` |

- A multi-word verb uses `_` in the type (`brain.new_id.result`), following §1.1a.
- `export` and `settings export` print a document, not a message (§1.7b), so they have no reply type.

`request_id`:
- The envelope has no `id` (JE-2.2). Domain ids live in `data` (`data.memory.id`, `data.entries[].id`, `data.event_id`, `data.backfill_id`).
- A request that expects a reply carries a `request_id` (UUID v7) minted by its sender. The reply echoes it, and it is the only matching key (PP R1.8).
- An `event` also carries one on stdin, where it gets no reply, so the run log can name the delivery. Deduplicate on `data.event_id`, never on `request_id`.
- Messages that get no reply carry no `request_id`: `ready`, `shutdown`, `backfill.pause` and `backfill.resume`.
- HTTP sets `request_id` on every response: from the `Request-Id` header when the client sends one, otherwise minted by the server. On MCP, it is the server's id for the tool call, separate from the JSON-RPC `id`.
- CLI output has no `request_id`, because it is not a reply (JE-2.9).
- In test builds only, `REMEMBER_TEST_FIXED_IDS=1` fixes the id sequence (JE-7.1).

### 1.7b Stored documents (JE-1.5)

A stored file is a plain document `{format, version, …content}`. It has no `type` and is not an envelope.

| `format` | File |
|---|---|
| `remember-export` | `remember export` output (007 ST-8.3) |
| `remember-settings-export` | `remember settings export` output (007 ST-8.1) |
| `remember-plugin` | `plugin.json` (PP §2) |
| `remember-plugin-trust` | `<data dir>/plugins/trust.json` (010 H-9.2) |
| `remember-plugin-state` | `<runtime>/plugins/<name>/<brain-id>.state` (010 H-11.4) |
| `remember-plugin-index` | `<brain>.d/plugins/<name>/index.json` (010 H-13.2) |
| `remember-brains` | `<data dir>/brains.json` (010 H-2.1) |

`remember-hooks` (`hooks.json`) and the standalone `remember-event` are gone. An event is the `data` of an `event` message (S-01).

### 1.8 Envelope

```
{format, version, type, request_id?, data?, error?, warnings?, next_cursor?, has_more?}
```

| Field | Required | Rule |
|---|---|---|
| `format` | yes | `"remember-message"` (S-07) |
| `version` | yes | `1` |
| `type` | yes | §1.7a |
| `request_id` | optional | §1.7a. Never on CLI output. |
| `data` | optional | The payload. Present on success and on every request and notification (`{}` when empty). |
| `error` | optional | On failure only, never together with `data`. Shape `{code, message, fix, retryable, detail?, docs?}`. `fix` is a non-empty string (JE-2.6). |
| `warnings` | optional | Only when non-empty. Items are `{code, message, detail?}` (JE-2.7). |
| `next_cursor`, `has_more` | optional | Only on list responses that page, always together (§1.11). Never on search (S-08), single-item or write responses, or plugin messages. |

- The key order is `format, version, type, request_id, data | error, next_cursor, has_more, warnings` (JE-2.1). Absent optional fields are omitted, not null. The one exception is `next_cursor`, which is `null` on the last page.
- Where an envelope goes:
  - **CLI:** with `--json`, success and failure both go in one envelope on stdout, and stderr stays empty (#572, JE-2.8). No `--json-errors` flag exists. Without `--json`, a failure or a warning is a plain `remember: …` or `remember: warning: …` line on stderr.
  - **HTTP:** the body is the envelope. The status follows `error.code` (the API surface v1 log holds the catalog, #583). Success is 200, or 201 for a create.
  - **MCP:** `structuredContent` is the envelope. The text content is the same JSON. `isError: true` on failure.
  - **Plugins and hooks:** every socket line, every stdin message and the `ready` line is an envelope (PP §1). Warnings in a plugin reply go only to the plugin log and `plugin status`. They never go into a CLI or API response, so they never change an exit code (PP R9.21).

Success (CLI):
```json
{"format":"remember-message","version":1,"type":"memory.add.result",
 "data":{"action":"created","memory":{"id":"01a14513-6501-7928-b9c4-2cc649cb59a9","local_id":1,…}}}
```
Search with a warning (HTTP; no paging fields, S-08):
```json
{"format":"remember-message","version":1,"type":"memory.search.result","request_id":"01a14530-0000-7000-8000-0000000000aa",
 "data":{"count":3,"total":3,"entries":[…],"query":{"original":"tax","rewritten":"tax"},"ranked_by":"bm25","filters":[…],"keys":{…},"tags":{…}},
 "warnings":[{"code":"plugin_filter_cold_start","message":"hybrid/rerank is still starting, so results are in keyword order."}]}
```
Failure (CLI `--json`, stdout):
```json
{"format":"remember-message","version":1,"type":"memory.get.result",
 "error":{"code":"not_found","message":"No memory has the key \"nope\".","fix":"Run 'remember list' to see keys.","retryable":false}}
```
Event (hook stdin, plugin socket, webhook):
```json
{"format":"remember-message","version":1,"type":"event","request_id":"01a14526-0000-7000-8000-000000000501",
 "data":{"event_id":"01a13a8c-0001-7…","event_type":"memory.created","cause":{"command":"add","origin":"local","hook":null,…},"memory":{"id":"01a13a8c-55f0-…","created_by":{"kind":"human","id":null},…},"changes":null,"plugin":{…},"entry":"run",…}}
```

- ✗ `{"version":1,"data":{…}}` (no `format` or `type`) · an envelope `"id"` · `"data":{"entries":[…]}` on `get` (use `data.memory`) · `"error":null` · `"warnings":[]` · `"body":{…}` on a plugin message · `"next_cursor":null` on a search

### 1.8a Exit codes and `--strict` (G2-03, S-09; 010 H-16.5)

- 0: ok, also when a plugin or hook degraded the command and a warning was given.
- 1: usage or error, including `plugin_backfill_incomplete` from `plugin reindex` and the `plugin enable` backfill.
- 2: not found.
- 3: wrong bin.
- 4: a warning under `--strict` or `REMEMBER_STRICT=1`, **whatever its code**. Exit 4 has no other meaning. No warning is exempt (G2-03). That includes the CLI codes `deprecated_flag`, `unknown_field` and `flag_meaning_changed`.
- `flag_meaning_changed` (M-23 A, 010 H-16.9a) is one release only, and is removed after 0.3.0. Its `detail` is `{"flag":"--trash","use":"--expired"}`. With `--json` it is an item in `warnings[]` and stderr stays empty. Without `--json` it is the line `remember: warning: --trash now means the Trash (deleted memories). For expired memories, use --expired.`
- The JSON output is the same under `--strict`. Only the exit code changes.
- Hook and action failures after the handler starts never change a write's exit code (PP R8.16). Warnings from plugin replies never reach command `warnings` (§1.8).

### 1.9 null vs absent

- **Output:** every documented field is always present, and `null` means "no value". The envelope's optional fields are the exception (§1.8).
- **Patch input** (HTTP PATCH, MCP `remember_update`): an absent field means "don't change", and `null` means "clear".
- **Arrays are never null.** An empty list is `[]`. The exception is the envelope's `warnings`, which is absent when empty. An object that doesn't apply is `null` (`"changes":null` on a non-update event).
- ✓ `"key":null,"tags":[],"links":[]` · ✗ `"tags":null` · PATCH `{"key":""}` to clear

### 1.10 Enums

- An enum value is a lowercase snake_case string (dotted for event types: `memory.created`). The list is closed for writers and **open for readers**: a reader must handle an unknown value. A new value doesn't bump `version`.
- Shipped lists:
  - actor `kind` = `human|agent|tool|share|import` (JE-4.4);
  - `bin` = `live|expired|deleted`, plus `purged` in events only. `deleted` is the bin that users see as the **Trash** (CLI `--trash`, 010 H-16.9a);
  - `cause.origin` = `local|sync|import`;
  - filter `status` = `applied|skipped|failed|timeout`;
  - `disabled_reason` = `user|untrusted|crash_loop|protocol_mismatch`;
  - settings `action` = `set|tombstone|delete|none` (reset uses `tombstone|delete|none`, history uses `set|tombstone|delete`, import rows use `set|tombstone|none`; log 007 ST-7.11b);
  - settings import `result` = `written|unchanged|skipped|kept_local`, and `reason` = `plugin_not_installed|other_scope|layer_not_allowed|unknown_key|invalid_value`;
  - `setting_invalid` `reason` includes `bad_prefix_shape|duplicate_prefix` (log 007 ST-7.15).
- Warning and error codes are open too: a new code is additive. The plugin warning catalog is PP §8.2. Every plugin code starts with `plugin_` (for example `plugin_filter_timeout` and `plugin_loop_halted`), and no `hook_` code exists. CLI codes such as `flag_meaning_changed` and `deprecated_flag` are in the CLI code catalog (the API surface v1 log, #583). The error catalog is the API surface v1 log too. No `time_budget_capped` code exists (M-22 A).
- ✓ `"bin":"deleted"` · ✗ `"bin":"trash"`, `"bin":2`, `"source":"agent"` (no `source` field, G-33)

### 1.11 Pagination (S-08, JE-2.10–2.12)

- A list command pages **by cursor only**. Request: `limit` and `cursor`. Response: `next_cursor` (an opaque string, `null` on the last page) and `has_more` (boolean), **at the top level of the envelope**, always together. `count`, the list and an optional `total` stay in `data`.
- **Search does not page.** It returns the top `--limit` results, or `remember.search.max_results`. A search response has no `next_cursor` and no `has_more` (JE-2.11, PP R8.3).
- **No command has `--offset`, and no envelope has `offset`** (JE-2.12). This breaks 0.2.0.
- Default page sizes: settings `list` 100, settings `history` 50, and both at most 1000 (log 007 ST-7.14). A bad cursor is `invalid_argument`. The names are the same everywhere: CLI `--limit` and `--cursor`, HTTP `?limit=&cursor=`, MCP `limit` and `cursor`.
- Why cursors: offsets skip or repeat items while sync and agents write.
- ✓ `{"format":"remember-message","version":1,"type":"memory.list.result","data":{"count":20,"entries":[…]},"next_cursor":"eyJ1Ijoi…","has_more":true}`
- ✗ paging inside `data` · `"next_cursor":null,"has_more":false` on a search or a `get` · `"offset":40` · `{"page":2}`

### 1.12 Lists

- A list field is a plural noun: `entries`, `tags`, `plugins`, `settings`, `links`, `conflicts`, `filters`, `warnings`. A list of memories is always `entries`. A response with a list has `data.count`.
- An item has the same shape wherever it appears. A memory in `add` output has the same fields as in `get`, including `links` (JE-4.3). `get`, `add`, `update` and `rekey` return `data.memory` (JE-4.2).
- ✗ `"results":[…]`, `"memories":[…]`

### 1.13 Unknown fields (J3; PP R2.5, R9.19–R9.21; JE-4.1)

- **Error** in the `run`, `permissions` and action `filter` sections of `plugin.json`: `plugin_manifest_invalid`, which names the field (`detail {path, rule}`). No hooks config file exists (S-01).
- **Warning everywhere else**: CLI, HTTP and MCP input, settings import, exports, events, plugin messages and the rest of `plugin.json`. The reader ignores the field and warns `unknown_field` (`detail {field, where}`), once per file or stream. A plugin's `unknown_field` goes only to the plugin log and `plugin status`.
- Why: in the sections that decide what runs, "ignore" would mean "run something else".
- ✓ `plugin.json /filters/0/run: unknown field "proces".` fix `Did you mean "process"?`

### 1.14 Numbers

- Use integers for counts, sizes and durations, within ±(2⁵³−1). No floats for time, money or quotas.
- Scores are the exception (PP OQ-18). Each filter has its own scale, passed through as a JSON number in `filters[].scores[] {memory_id, score}`. A score is never compared across filters.

### 1.15 Strings and encoding

- UTF-8, with no BOM and no NUL. Memory bodies are opaque.
- Limits:
  - a memory body is at most 65,536 bytes;
  - a setting value is at most 16 KiB;
  - the `data` of an `event` is at most 256 KiB;
  - a socket message is at most 16 MiB;
  - nesting is at most 16 levels.
- Going over a limit is an error that names the limit (`detail.limit`, `detail.size`). An oversize plugin message is a plugin failure (PP R1.13–R1.14).

### 1.16 Ordering

- Key order has no meaning for readers. Writers still emit the documented order:
  - the envelope order of §1.8;
  - in a memory: `id, local_id (CLI only), key, body, tags, created_by, updated_by, created_at, updated_at, expires_at, deleted_at, bin, version_vector, links` (008 Examples);
  - in `data`: `count`, the list, `total`.

### 1.17 JSON Lines for logs and streams

- Logs (`<brain>.d/hooks.log`) and streams (the plugin socket, `plugin log --follow --json`) use JSON Lines.
- A stream line is a full envelope. `plugin log --follow --json` emits `type` `plugin.run` for a run record and `plugin.stderr` (`data {plugin, line, at}`) for a stderr line (010 H-15.3, PP R9.28).
- A record in the log file is a file line, not a message, and carries `version` only.
- `--json --pretty` is for humans only.

### 1.18 No comments

- Plain RFC 8259 JSON, in every file: no JSONC, JSON5, trailing commas or comments.

---

## 2. Top-level shapes per surface

`E(t)` is the envelope `{format:"remember-message", version:1, type:t, …}`, with no envelope `id`. `?` marks an optional field. This table is normative through 008 §6. Where it differs from log 008, the log wins.

| Surface | Document or message | Top-level shape |
|---|---|---|
| CLI `--json` write, one memory | `add`, `update`, `rekey` | `E(memory.<verb>.result)` + `data{action, memory{}}`, `warnings?` |
| CLI `--json` write, many | `delete`, `purge`, `link`, `unlink`, `conflict accept`, `import` | `E(memory.<verb>.result \| conflict.accept.result \| …)` + `data{action, count, entries[]}`, `warnings?` |
| CLI `--json` read, one | `get` | `E(memory.get.result)` + `data{memory{}}`, `warnings?` |
| CLI `--json` list | `list` | `E(memory.list.result)` + `data{count, entries[], total?}`, `next_cursor`, `has_more`, `warnings?` |
| CLI `--json` search | `search` | `E(memory.search.result)` + `data{count, total, entries[], query{original, rewritten}, ranked_by, filters[], keys{}, tags{}}`, `warnings?`. **No `next_cursor` and no `has_more`** (S-08, PP §8). |
| CLI `--json` settings | `settings get\|set\|reset\|list\|history\|import` | `E(setting.<verb>.result)` + `data`: get `{setting}`; set `{setting, scope, old_value, changed}`; reset the same plus `action`; list `{count, settings[]}` and history `{count, changes[]}`, each with `next_cursor`, `has_more`; import `{count, written, unchanged, skipped, rows[]}`. `setting` = `{key, type, value, value_ms?\|value_bytes?, from, changed, layers{default, user?, brain?}}`; a history item = `{key, scope, action, old_value, new_value, updated_by, changed_at}`; an import row = `{key, scope, result, action, reason?}` (log 007 ST-7.11a–7.11b). `warnings?` |
| CLI `--json` other lists | `tags`, `conflicts`, `plugin list`, `brain list` | `E(<noun>.list.result)` + `data{count, <plural>[]}`, `next_cursor` and `has_more` when the command pages, `warnings?` |
| CLI `--json` single | `settings explain`, `plugin show\|add\|rerun`, `hook add`, `brain new-id`, `sweep`, `dispatch` | `E(<noun>.<verb>.result)` + `data{…}`, `warnings?` (for example `brain.new_id.result` `data{brain_id, previous_brain_id}`) |
| CLI plugin status | `plugin status` | `E(plugin.status.result)` + `data{plugins[]}`; each item follows PP §10 `plugin-status.json`. Each filter item in `entries[]` has `budget` and an optional `cold_start_grace` (M-24 A) |
| CLI failure | any command with `--json` | `E(<request type>.result)` + `error{code, message, fix, retryable, detail?}`, `warnings?`; `unknown.result` when no command is known; stdout only; stderr empty |
| CLI memory object | `data.memory`, `data.entries[]` | `{id, local_id, key, body, tags[], created_by{kind,id}, updated_by{kind,id}, created_at, updated_at, expires_at, deleted_at, bin, version_vector{}, links[]}` (no `source`) |
| HTTP | every route | `E(<request type>.result)` + `request_id`, `data` \| `error`, `next_cursor?`/`has_more?` (paging list routes only; never search), `warnings?`; memory objects without `local_id`; status from `error.code` |
| MCP | `remember_add\|search\|list\|get\|update\|delete`, `remember_settings` | input: snake_case tool arguments (`id`, `key`, `tags`, `bin`, `limit`, `cursor`; search takes no `cursor`); output: the HTTP envelope in `structuredContent` |
| Plugin message | socket, plugin and hook stdin, the `ready` line, Cloud webhooks | `E(<PP §1.2 type>)` + `request_id?` (§1.7a), `data` \| `error`, `warnings?`; never `next_cursor` or `has_more` (PP R1.12). The type list is PP §1.2. |
| Event message | `type: "event"` (hooks, plugin actions, webhooks) | `E(event)` + `data{event_id, event_type, occurred_at, detected_at, batch_id, cause{command, origin, hook, run_id, depth, chain[]}, brain{}, memory{}, changes, truncated, truncated_fields[], limits{}, plugin{}, entry, data_dir, settings[]}` (PP §3). The actor is `memory.updated_by` (R3.5a). Six event types; a link change is `memory.updated` with `changes.link` (S-10). `data` is at most 256 KiB. |
| Settings export | `remember settings export` (document) | `{format:"remember-settings-export", version, exported_at, settings:[{key, scope, value, value_ms?\|value_bytes?, updated_by, updated_at, device_id, version_vector, deleted_at}]}` (007 ST-8.1–8.2) |
| Brain export | `remember export` (document) | `{format:"remember-export", version, …}`: entries (no `local_id`), brain settings rows in the ST-8.2 shape, and tombstones (007 ST-8.3) |
| Plugin manifest | `plugin.json` (document) | `{format:"remember-plugin", version, name, plugin_version, protocol, description?, data_version?, idle_timeout?, backfill?{enabled, max_batch_entries?, max_batch_size?}, actions[]{name, events[], filter?{tags, created_by, updated_by, bins, not_caused_by, origins}, run, permissions}, filters[]{name, point, priority?, budget?, cold_start_grace?, run, permissions}, settings[]{key, description, type, default, range?, scope}}`. Names follow §1.1a. `budget` (default `800ms`), `cold_start_grace` (default `10s`) and `priority` (default 500) come **from the manifest only**: no user setting overrides them, and a budget or grace above 30 s is invalid. `cold_start_grace` must be at least 1 ms, so `0ms` is invalid (M-24 A). A budget larger than the time left in the chain cap is clipped silently, with no warning (M-22 A) (S-06, PP R2.11, R2.13, R9.1–R9.3). The only generated setting is `plugin.<name>.enabled`, and a declared setting key `enabled` is invalid. An unknown field in `run`, `permissions` or `filter` is an error (§1.13). |
| Brain list | `brains.json` (document) | `remember-brains` (010 H-2.1) |
| Run log | `<brain>.d/hooks.log` line (file record) | `{version, run_id, hook, event_id, request_id, event_type, memory_id, started_at, ended_at, duration_ms, result, exit_code, signal, depth, test, truncated_payload, output, message}` (010 H-15.3); the `--follow --json` stream wraps each record as `E(plugin.run)` |

---

## 3. Deviations (closed)

Rev 1 listed D1–D31 against the code, the handoff docs and the drafts of 2026-10-16. Every row is now either done in logs 007–010 and PP rev 6, or carried by their implementation plans. The backup `JSON-STYLE.bak-2026-10-22.md` keeps the old list. The migration notes for the 0.3.0 CHANGELOG are:
- `.entries` becomes `.data.entries`, and `get`/`add`/`update`/`rekey` return `.data.memory`;
- `sync_id` becomes `id`, and the integer id becomes `local_id`;
- one format, `remember-message` 1;
- no `--offset`, and search has no paging fields;
- no `--id`/`--local-id`;
- `source` becomes `created_by`/`updated_by`, and `--source` becomes `--as`;
- `hook_*` codes become `plugin_*`;
- `--trash` now means the Trash, and `--expired` is its own flag (010 H-16.9a, with a one-release warning, M-21).

---

## 4. Checklist for new JSON

1. Keys are snake_case. No data is used as keys. Names follow `^[a-z][a-z0-9_]{0,39}$`.
2. A message is `{format:"remember-message", version:1, type, …}`. It has no envelope `id`. A reply type is `<request type>.result`, also on failure. A stored document starts with `format` and `version` and has no `type`.
3. Ids live in `data`. `request_id` is only for matching. A response has `data` on success and `error` on failure, never both, never `null`.
4. `warnings` appears only when non-empty. With `--json`, warnings go only to stdout `warnings`. Under `--strict`, any warning exits 4.
5. `next_cursor` and `has_more` sit at the top level, only on paging list responses. Never on search. No `offset`.
6. Ids are lowercase hyphenated UUID v7 with no prefix. `local_id` appears only in CLI output.
7. Instants are `…T….mmmZ` in `_at` fields. Dates are `YYYY-MM-DD` in `_on` fields.
8. Configured durations and sizes are unit strings with `_ms`/`_bytes` twins. Measured ones are integers.
9. Every error has `code`, `message`, `fix` and `retryable`.
10. Documented fields are always present, with `null` for no value and `[]` for an empty list (the envelope's `warnings` excepted).
11. Enums and codes are lowercase and open for readers. Plugin codes are `plugin_*`.
12. Unknown fields are an error in plugin `run`, `permissions` and `filter`, and a warning `unknown_field` everywhere else.
13. Actors are `{kind, id}` in `created_by` and `updated_by`. There is no `source`.
14. UTF-8, no BOM, no comments, stated limits, the documented key order, and JSON Lines for logs and streams.

---

## 5. Decisions this guide follows

- Itay, 2026-10-18: one envelope, the `data` wrapper, no envelope `id`, `.result` reply types with `error` on failure, paging in the envelope, unknown fields as an error in security sections and a warning elsewhere, hyphenated UUID v7 with no prefix, `id`/`local_id`, warnings on stdout with `--json`, and export files as plain documents.
- Itay, 2026-10-21 (round 3 sheet): S-01 (hooks are plugins), S-06 (budget, grace and priority from the manifest only), S-07 (one format), S-08 (no offset, search never pages), S-09 and G2-03 (exit 4 = strict saw a warning), S-13 (locators), G-33 and G2-10 (actors replace `source`).
- Itay, 2026-10-22 (merge sheet): M-05 C (Interface revises this guide), M-17 A (one name rule), M-19 A (N6 `plugin_loop_halted`, N8 `--as`), M-21 B.
- Itay, 2026-10-22 (rev 6 sheet `QUESTIONS-2026-10-22-rev6.md`):
  - M-22 A: `time_budget_capped` is dropped, and budgets are clipped silently.
  - M-23 A: the code is `flag_meaning_changed`, it goes in `warnings[]` under `--json`, it exits 4 under `--strict`, and it is removed after 0.3.0.
  - M-24 A: `cold_start_grace` defaults to 10 s, ranges from 1 ms to 30 s, and is shown in status.
- PP §8.2 is the plugin warning catalog.
