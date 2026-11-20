# Plugin manifest

Status: what `plugin_manifest_parse` (`src/plugin_manifest.c`) accepts and refuses. It reads the bytes of one `plugin.json` and returns every error and every warning. It reads no file and starts no process. Install, trust, settings rows, hooks and processes are later plans.

Decisions are `decision:…` entries tagged `subject:plugins`. The shape of the document is [PLUGIN-PROTOCOL.md](PLUGIN-PROTOCOL.md) R2 and `schema/plugin/v1/manifest.json`. Where a decision and the schema disagree, the decision wins.

Every sentence the parser prints is approved copy: R25-1 (`decision:2026-11-12-copy-batch-final`), C-35 and C-36 (`implementation-plans/COPY-PENDING.md`), and R25-2 (`decision:2026-11-14-copy-r25-2-manifest-approved`, text in `implementation-plans/COPY-APPROVED-2026-11-14.md`). The `fix` of the C-35 warning is not printed here: `diag_unknown_field` has no `fix` yet.

## Result

`plugin_manifest_parse(text, len, &manifest)` returns one of:

| Status | Meaning |
|---|---|
| `PLUGIN_PARSE_OK` | No error. Warnings may exist. |
| `PLUGIN_PARSE_INVALID` | At least one `plugin_manifest_invalid` error. |
| `PLUGIN_PARSE_UNSUPPORTED` | At least one `plugin_protocol_unsupported` error. |
| `PLUGIN_PARSE_SYNTAX` | The text is not JSON, its top level is not an object, or it nests deeper than 64 levels. One error, each with its own sentence. |
| `PLUGIN_PARSE_OOM` | An allocation failed. `*out` is NULL. |

In every other case `*out` is a heap manifest. It holds every error (`errors`) and every warning (`warnings`), with no limit and no truncation. A sentence that echoes a name from the file holds the whole name. Plugin-controlled text in a sentence (a field name, a name, a repeated item, a pointer) is printed with JSON string escaping, so the plain line and the JSON `message` are the same, one line, and no escape sequence reaches a terminal (`decision:2026-11-14-copy-r25-2-manifest-approved`, Q2). The bytes that are escaped are `"`, `\`, the C0 controls, DEL and the C1 controls (U+0080 to U+009F). `path` keeps the RFC 6901 form and its raw bytes, and the JSON writer escapes it. The caller prints `remember: <message> <fix>`.

Errors come in document order: each sits at the place in the file of what it names, and a repeated member sits at its second appearance. `at` is that byte offset. Errors at one place keep the order they were found in.

An error has `code`, `rule`, `path`, `message`, `fix` and `at`. `path` is a JSON Pointer (RFC 6901, `~` as `~0`, `/` as `~1`) into `plugin.json`, and it names a member that exists in the file, or the object that lacks a member. `rule` is the R-number of PLUGIN-PROTOCOL R2 for that field, or `budget_range`. `plugin_protocol_unsupported` also carries `supported`, the numbers this remember reads (`[1]`).

`decision:plugin-manifest-errors`: one parse returns every error, not only the first.

## Document

`decision:plugins-manifest`, `decision:2026-10-22-m-17`, `decision:g-11`, `decision:plugins-process-kinds`, `decision:s-06`.

- The top level is one JSON object. `jsonread` refuses nesting past 64, invalid UTF-8, a lone surrogate and a `\u0000` escape in a string or a key, so no name is cut short and no key aliases another.
- `format` is `"remember-plugin"`. `version` and `protocol` are the number `1`. `name` and `plugin_version` are required. At least one of `actions` and `filters` is required.
- A name (plugin, entry or setting key) is `^[a-z][a-z0-9_]{0,39}$`.
- A name with a hyphen is refused, never rewritten. When the name with each `-` replaced by `_` is valid, the error is the C-36 hyphen error and its fix names that value. When it is still invalid, the error is sentence 6, so no fix suggests a name that is refused (`decision:2026-11-15-plan25-review-grill`, G5 A).
- A setting key `enabled` is refused with the C-36 reserved-key error, at `/settings/<i>/key`. It names the plugin when `name` is any string, valid or not, and says "this plugin" otherwise (sentence 16).
- `plugin_version` is `^\d+\.\d+\.\d+([-+][0-9A-Za-z.-]+)?$`. `description` is at most 200 characters (UTF-8 characters, not bytes). `data_version` is a whole number of 1 or more (default 1), up to 9223372036854775807. `idle_timeout` is a duration (R2.20).
- A duration is digits and one of `ms`, `s`, `m`, `h`. A size is digits and one of `B`, `KiB`, `MiB`. The schema does not allow a fraction, so the parser does not either. A string over 23 bytes is not a duration or a size, and it gets the ordinary phrase of its field (Q3, L13).
- A whole number is read by value (Q1): `1.0`, `1e0`, `10e-1` and `-0` are whole numbers, and `1.5` is not. `priority`, `data_version`, `max_batch_entries`, `max_items`, `version`, `protocol` and the integer settings all use this. A value that needs more than 64 digits is not a whole number; zeros after the point are not digits. Up to 9223372036854775807 a value is read; a larger one prints its digits in `<n>` and is out of range anywhere else.
- `backfill` is an object with a required boolean `enabled`, `max_batch_entries` (1 to 100, default 100) and `max_batch_size` (at most `4MiB`, default `4MiB`). `backfill.enabled: true` needs a persistent entry (R2.17).
- An action has `name`, `events` (1 to 8 event types; `memory.*` only alone, R2.21), `run`, and optional `filter` and `permissions`. `filter` is closed: `tags` (`any`, `all`, `match_before`), `created_by`, `updated_by`, `bins`, `not_caused_by`, `origins`. An omitted `origins` is `["local"]`. An explicit empty `origins` stays empty and the action never fires (`decision:2026-11-13-plugin-origins-empty`).
- A filter has `name`, `point` (`search.query` or `search.results`), `run`, and optional `priority` (0 to 1000, default 500), `budget` and `cold_start_grace` (each 1ms to 30s inclusive, defaults `800ms` and `10s`), and `permissions`.
- `run` is closed. An action's `run` has `command`, and optional `process` (`once` by default, or `persistent`) and `timeout` (default `60s`). A filter's `run` has `process: "persistent"` and `command`, and no `timeout`.
- `command` is 1 to 8 non-empty strings of at most 255 bytes. Every bad item gets its own error. It is an argv, run without a shell (R2.8). A relative `command[0]` with a `..` segment is refused (`decision:2026-11-12-plugin-command-no-dotdot`). An absolute path and a relative path with no `..` segment stay allowed.
- Every persistent entry has the same `command` (R2.10). Each persistent entry whose command differs from the first one (in document order, actions before filters) gets its own error. An entry whose `command` is itself an error is not compared. The error is at `<entry>/run/command`, and the message names the first entry's pointer.
- `backfill.enabled: true` with no filter and no persistent action is an error at `/backfill/enabled` (R2.17). Every filter counts, even one whose `process` is itself an error.
- `permissions` is closed: `needs` lists memory fields (`key`, `body`, `tags`, `bin`, `created_at`, `updated_at`, `expires_at`, `deleted_at`, `change_seq`, `created_by`, `updated_by`, `links`) and `remember.*` setting keys (2 or 3 name segments after `remember.`, at most 96 characters). An item listed twice is refused.
- `priority`, `budget` and `cold_start_grace` come from the manifest only. No setting overrides them (`decision:s-06`).

## Budget and grace

`decision:2026-11-11-manifest-budget-range`, `decision:2026-10-22-m-24`, `decision:plugins-budget-defaults`.

`budget` and `cold_start_grace` run from `1ms` to `30s` inclusive. `0ms`, anything above `30s`, and a value that is not a duration are `plugin_manifest_invalid` with `path` `/filters/<i>/budget` or `/filters/<i>/cold_start_grace`. The rule of a budget error is `budget_range`. The rule of a grace error is `R2.13`. A manifest has no `entries` member, so the pointer names the filter in `filters`.

## Unknown fields

`decision:plugin-manifest-errors`, `decision:2026-11-12-e3-warnings-have-fix-field`.

- On a closed object (`run`, `permissions`, an action's `filter`) an unknown field is `plugin_manifest_invalid`. `path` is the pointer of the field, and the message names the object. The fix is `Did you mean "<known>"?` when exactly one known field of that object is one edit away, and `Remove "<field>".` otherwise.
- At the top level, `run`, `permissions` and `filter` are errors too, with their own sentence: `"<field>" can't be at the top level.` The fix is `Move "<field>" into an action or a filter.`, and `Move "filter" into an action.` for `filter`. The rule of an unknown field in an action's `filter` block, and of a top-level `filter`, is `R2.21`. The rule for `run` and `permissions` is `R2.5`.
- On every other object (the top level, an action, a filter entry, `tags`, `backfill`, a setting, a setting's `range`) an unknown field is a warning. The parser records the pointer of the object and the field name. The caller reports each one through `diag_unknown_field`, which prints the C-35 file line once per file, with the field JSON-escaped like every sentence here (G2 A). The rest of the document still parses.
- A repeated key in any object is an error (`R2.2`, fix `Keep one "<key>".`), inside an unknown member too. A name used twice in one list is an error too: two actions, two filters, or two settings with one name (`R2.22`, at the second use, fix `Rename one of them.`). An action and a filter may share a name. A parser that kept the first value would read another manifest than one that kept the last.

## Version and protocol

`decision:2026-11-12-copy-batch-final` (R25-1), `decision:2026-11-12-manifest-version-unsupported`.

A `protocol` number other than `1` is `plugin_protocol_unsupported` (exit 1, `retryable` false). The message is `plugin.json /protocol: "<name>" was written for plugin protocol <n>, and this remember reads protocol <range>.` The fix is `Update remember, or install an older version of "<name>".` `<n>` is the value. `<range>` is `1`, or `1 to 2` once a second protocol exists. `supported` is `[1]`. A `version` other than `1` is the same code with the same shape, and its message says `plugin.json format` in place of `plugin protocol`. `<n>` is the value as plain digits, never the source text: `2.0` prints `2`, and `-0` prints `0`. A `protocol` or `version` written as `1.0` or `1e0` is `1` and parses. A `protocol` or `version` that is not a whole number (a string, or `1.5`) is `plugin_manifest_invalid` with `must be a whole number`. When the file has no usable `name`, the sentence says "this plugin" in place of the name.

## Settings declarations

`decision:settings-scheme`, `decision:2026-10-22-m-17`. Plan 30 turns these into registry rows (SETTINGS.md § Plugin keys). A manifest whose default, or an enum value, is not text the registry accepts (it holds a control byte or DEL) registers none of its keys.

Each item of `settings` has `key`, `description` (at most 200 characters), `type`, `default`, and optional `range` and `scope`. At most 16 items.

| `type` | `default` | `range` |
|---|---|---|
| `boolean` | `true` or `false` | ignored |
| `integer` | a whole number of 0 or more | optional `min`, `max` (whole numbers of 0 or more) |
| `duration` | a duration | optional `min`, `max` (durations) |
| `size` | a size | optional `min`, `max` (sizes) |
| `enum` | one of `range.values` | required, with `values`: at most 8 strings of at most 63 bytes |
| `string` | a string of at most 511 bytes | ignored |
| `string_list` | a list of strings, at most 511 bytes of source JSON | optional `max_items` (1 or more) |

A numeric default outside `min` and `max` is refused. `min` above `max` is refused. `scope` is `user` (default), `brain` or `both`. A string list default is kept as the source JSON text of the array.

## Limits

`decision:2026-11-13-plugin-manifest-limits`. The schema has no size limit. The parser holds a manifest in fixed arrays, and a document past a limit is `plugin_manifest_invalid` with the limit in the sentence (R25-2). L1: 16 actions and 16 filters. L2: 8 events per action. L3: 8 command arguments. L4: 255 bytes per command argument. L5: 16 needs per entry. L6: 8 tags in each of `any` and `all`, 63 bytes each. L7: 5 actor kinds, 4 bins, 3 origins (the sizes of their enums). L8: 8 `not_caused_by` items of 79 bytes. L9: 16 settings. L10: 8 enum values of 63 bytes. L11: 511 bytes for a `string` default, and 511 bytes of source JSON for a `string_list` default. L12: 63 bytes for `plugin_version`. L13: 23 bytes for a duration or size string. L14: no cap on file size here, and nesting is capped at 64 by `jsonread`. The reader that opens `plugin.json` caps the file size (plan 33). The cap must allow for the result: a hostile file of many tiny unknown or repeated keys yields one error per key, about 16 to 20 heap bytes per byte of input (measured 2026-11-15).
