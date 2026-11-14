# 25 — Plugin manifest

**Module:** `src/plugin_manifest.c` and `src/plugin_manifest.h`.
**Needs:** plan 24 (merged). No other plan.
**Blocked on:** nothing. The copy is item R25-1 in `decision:2026-11-12-copy-batch-final` and item R25-2 in `decision:2026-11-14-copy-r25-2-manifest-approved` (approved 2026-11-14). The R25-2 sentences are in [`COPY-APPROVED-2026-11-14.md`](COPY-APPROVED-2026-11-14.md).

## Goal

`plugin.json` parses to the protocol schema, and every refusal uses a locked code and a locked sentence.

## Source

- `decision:plugins-manifest`
- `decision:2026-10-22-m-17`
- `decision:plugin-manifest-errors`
- `decision:2026-10-22-m-24`
- `decision:s-06`
- `decision:plugins-budget-defaults`, with its exit-4 sentence replaced by `decision:2026-11-11-filter-degraded-exit` (this parser has no exit code)
- `decision:2026-11-11-manifest-budget-range` (P5 A)
- `decision:2026-11-11-plan25-branch` (P2 B, G1 A)
- `decision:g-11`
- `decision:plugins-process-kinds`
- `decision:2026-11-14-copy-r25-2-manifest-approved`: the R25-2 parser sentences (1 to 16 and the `<what>` phrases), with Q1 A (whole numbers by value), Q2 A (JSON string escaping of plugin-controlled text) and Q3 A (strings over 23 bytes).
- `decision:2026-11-12-plugin-manifest-copy-r25-2`, `decision:2026-11-12-manifest-version-unsupported`, `decision:2026-11-12-plugin-command-no-dotdot`, `decision:2026-11-13-plugin-manifest-limits` and `decision:2026-11-13-plugin-origins-empty`: the rules behind the R25-2 sentences.
- `schema/plugin/v1/manifest.json` and `schema/plugin/v1/common.json` (plan 24). Where a decision and the schema disagree, the decision wins.
- Sentences: [`COPY-PENDING.md`](COPY-PENDING.md) C-35 and C-36 (Itay approved that file on 2026-10-25), R25-1 in `decision:2026-11-12-copy-batch-final`, and R25-2 in [`COPY-APPROVED-2026-11-14.md`](COPY-APPROVED-2026-11-14.md). The coder prints those sentences and no others.
- Spec: `specs/PLUGIN-MANIFEST.md` (new; uppercase per P24 A).

## Out of scope

Install, trust, hook add, settings rows, processes, search, and the outbox. A hyphen in a typed command normalizes in a later plan. This plan does not rewrite a hyphen inside `plugin.json`. The human `add` line stays as it is. The plain sentence for `plugin_protocol_unsupported` is copy item R25-1 in `decision:2026-11-12-copy-batch-final`. This plan prints exactly that text: the plain line, the JSON `message`, and the JSON `fix`. Exit 1, `retryable` false. `<name>` is the plugin name, `<n>` the protocol number in the manifest, `<range>` what this remember reads (`1`, or `1 to 2` once a second protocol exists). `message`: `plugin.json /protocol: "<name>" was written for plugin protocol <n>, and this remember reads protocol <range>.` `fix`: `Update remember, or install an older version of "<name>".` Plain line: `remember: plugin.json /protocol: "<name>" was written for plugin protocol <n>, and this remember reads protocol <range>. Update remember, or install an older version of "<name>".` (The `plugin.json /protocol:` prefix follows PLUGIN-MANIFEST R2.4; R25-1's [ASSUMPTION] is whether the prefix also stays in `message` when `detail.path` carries it.)

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md).

The parser is hand-written in this module. It does not add a library. It does not read the filesystem except the bytes the caller passes. Tests live in `tests/test_plugins.c`, suite `plugins`, and that suite is added to `tests/gate-suites`.

Branch rule (P2 B, G1 A). The local branch `plan/25-plugins` stays one branch and lands three pull requests in order. This plan's pull request carries only `src/plugin_manifest.c`, `src/plugin_manifest.h`, `tests/test_plugins.c`, the `CMakeLists.txt` lines for them, and the `tests/gate-suites` line. The branch's `src/brains.c`, `src/brains.h`, `src/brain_note.c`, `src/brain_note.h` and the `src/cmd_brain.c` changes are plan 26's pull request, cut from the same branch after this one merges. The branch's changes to `src/settings.c`, `src/settings.h`, `src/cmd_settings.c` and `tests/test_settings.c` stay on the branch, uncommitted to any pull request, until plan 30. Before this plan's pull request opens, `git diff origin/main --stat` lists only the files named in this paragraph.

## What must be true

- A document that `schema/plugin/v1/manifest.json` accepts parses. `format` is `remember-plugin`. `version` is 1. `protocol` is 1.
- A plugin name and an entry name match `^[a-z][a-z0-9_]{0,39}$`. A hyphen in a manifest name is `plugin_manifest_invalid`. The message is `"<name>" in plugin.json can't contain "-".` The fix is `Use "<value>".` `<value>` is the name with each `-` replaced by `_`.
- A manifest setting whose key is `enabled` is `plugin_manifest_invalid`. The message is `"<name>" declares a setting named "enabled", and that name is reserved.` The fix is `Rename that setting in plugin.json.`
- An unknown field on an object the schema closes (`additionalProperties` false), including `run`, `permissions`, and `filter`, is `plugin_manifest_invalid` and prints R25-2 sentence 7. `run`, `permissions` or `filter` at the top level prints sentence 7b. `detail.path` is a JSON Pointer. `detail.rule` is the schema rule id. The message names the object and the field, and the fix names the known field when exactly one is one edit away (`decision:plugin-manifest-errors`).
- An unknown field on an object the schema leaves open warns with the C-35 file line: `remember: warning: "<path>" has a field remember doesn't know, "<name>". The rest of the file was read.` The rest of the document still parses.
- `plugin add` and `plugin test` report every manifest error in that file in one result. This plan's tests call the parser. They do not add the commands.
- A `protocol` other than 1 returns `plugin_protocol_unsupported`. With a usable `name` it prints the R25-1 `message` and `fix`. With no usable `name` (missing, or not a string) it prints R25-2 sentence 9. A `version` other than 1 returns `plugin_protocol_unsupported` and prints sentence 10, with or without a usable name. `detail.supported` is `[1]` in all three.
- Sentences (R25-2). Every other refusal prints the R25-2 sentence it maps to, in [`COPY-APPROVED-2026-11-14.md`](COPY-APPROVED-2026-11-14.md): sentences 1 to 8 and 11 to 16, with the `<what>` phrase list for sentence 5. Each has a `message`, a `fix`, a `detail` of `{"path", "rule"}`, and the plain line `remember: <message> <fix>`. The code is `plugin_manifest_invalid`. One parse returns every error in the file, in document order, and a list over its limit names the list and reads no item past the limit.
- Whole numbers by value (R25-2 Q1 A). The parser reads every whole-number field by its value: `version`, `protocol`, `priority`, `data_version`, `max_batch_entries` and an integer setting. `1.0` and `1e0` parse as 1. `-0` parses as 0. `1.5` prints sentence 5 with `a whole number`. `<n>` in sentences 9 and 10 prints the value, never the source text, so `2.0` prints `2`.
- Plugin-controlled text (R25-2 Q2 A). A field name, a plugin, entry or setting name, a list item and every pointer segment print inside the double quotes and inside a pointer with JSON string escaping: `\"`, `\\`, `\n`, and `\u001b` for a control character. The plain line and the JSON `message` hold the same text, so one line never holds a terminal escape or a line break. A pointer segment applies the `~0` and `~1` rule first and the JSON escaping after. This covers the R25-1, C-35 and C-36 lines too, which print a name or a path.
- Long duration and size strings (R25-2 Q3 A). A duration or size string over 23 bytes (L13) prints the same sentence 5 phrase as any bad duration or size for that field: `a duration like "10m"`, `a size like "4MiB"`, or the range phrase where the field has one. The line has no "23" and no byte wording. A string of 23 bytes or fewer is judged by its value.
- An omitted `budget` is `800ms`. An omitted `cold_start_grace` is `10s`. An omitted filter `priority` is 500. An omitted action `process` is `once`. An omitted `origins` is `["local"]`.
- `cold_start_grace` of `0ms` is `plugin_manifest_invalid`. `1ms` and `30s` parse. A grace above `30s` is `plugin_manifest_invalid`.
- `budget` follows the same range (P5 A): `1ms` to `30s` inclusive parse; `0ms` and anything above `30s` are `plugin_manifest_invalid` with `detail.path` `/entries/<i>/budget` and `detail.rule` `budget_range`.
- `priority`, `budget`, and `cold_start_grace` are manifest fields. This plan does not read a setting for them.

## Stop and ask

The coder stops and asks Itay, through the architect, at each of these. The plan does not choose.

1. Whether the Q2 escaping also covers U+007F and U+0080 to U+009F, which JSON string escaping leaves as they are and some terminals read as controls (U+009B starts a control sequence).
2. Whether Q2 reaches the already approved R25-1, C-35 and C-36 lines. The plan escapes them, because they print a name or a path; their text does not change for an ordinary name.
3. What `<n>` prints for a whole value that does not fit 64 bits (`1e30`), and what a value like `1e999` does.
4. The `<what>` phrase for a duration or size string over 23 bytes in a field that has no phrase of its own.

## Review points

- The reviewer checks that a hyphen in `plugin.json` is rejected and is not rewritten.
- The reviewer checks that an unknown field on a closed object is an error and an unknown field on an open object is the C-35 warning.
- The reviewer checks that the parser prints no sentence that this plan does not quote or point to.
- The reviewer checks that no name, field or pointer segment from the file reaches a line without JSON string escaping, and that `1.0`, `1e0` and `-0` are read by value in every whole-number field.
- The reviewer checks that the branch does not add a process, a trust file, or an outbox write.
- The reviewer checks `git diff origin/main --stat` against the branch rule above.

## Definition of Done

- [ ] `a schema-valid manifest parses` (`tests/test_plugins.c`, the plan 24 hello fixture).
- [ ] `a hyphen in a manifest name prints the C-36 hyphen lines`.
- [ ] `a setting key enabled prints the C-36 reserved-key lines`.
- [ ] `an unknown field on run is plugin_manifest_invalid with a JSON Pointer`.
- [ ] `an unknown root field prints the C-35 file line and the rest parses`.
- [ ] `two errors in one file both come back from one parse`.
- [ ] `protocol 2 returns plugin_protocol_unsupported with the R25-1 message and fix`.
- [ ] `each R25-2 sentence 1 to 8 and 11 to 16 prints its plain line, message, fix and detail` (one test per sentence, expected text as literals).
- [ ] `sentence 9 and 10 return plugin_protocol_unsupported with and without a usable name`.
- [ ] `whole numbers 1.0, 1e0 and -0 parse in version, protocol, priority, data_version, max_batch_entries and an integer setting; 1.5 prints sentence 5 with a whole number`.
- [ ] `protocol 2.0 prints 2 in the R25-1 line and in sentence 9, never 2.0`.
- [ ] `a field name, a plugin name and a pointer segment with a quote, a backslash, a newline and 0x1b print JSON-escaped, and the plain line and the JSON message hold the same text`.
- [ ] `the R25-1, C-35 and C-36 lines escape a hostile name the same way`.
- [ ] `a 24-byte duration and a 24-byte size print the normal sentence 5 phrase with no 23-byte wording; a 23-byte string is judged by its value`.
- [ ] `budget 0ms and 31s are invalid; 1ms and 30s parse`.
- [ ] `omitted budget, grace, priority, process, and origins are the defaults above`.
- [ ] `0ms grace is invalid, 1ms and 30s parse, and 31s is invalid`.
- [ ] `specs/PLUGIN-MANIFEST.md` is written, and the decisions this plan built are `state:implemented`.
- [ ] The gate passes on the committed tree, and the notes record the mutants and their red tests.

## Implementation Notes

## Review Notes
