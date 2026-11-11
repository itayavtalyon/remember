# 25 — Plugin manifest

**Module:** `src/plugin_manifest.c` and `src/plugin_manifest.h`.
**Requires:** plan 24 merged. Plans 26 and 27 do not require this plan.

## Goal

`plugin.json` parses to the protocol schema, and every refusal uses a locked code and a locked sentence.

## Source

- `decision:plugins-manifest`
- `decision:2026-10-22-m-17`
- `decision:plugin-manifest-errors`
- `decision:2026-10-22-m-24`
- `decision:s-06`
- `decision:plugins-budget-defaults`
- `decision:g-11`
- `decision:plugins-process-kinds`
- `schema/plugin/v1/manifest.json` and `schema/plugin/v1/common.json` (plan 24). Where a decision and the schema disagree, the decision wins.
- Sentences: [`COPY-PENDING.md`](COPY-PENDING.md) C-35 and C-36. Itay approved that file on 2026-10-25. The coder prints those sentences and no others.
- Spec: `specs/plugin-manifest.md` (new).

## Out of scope

Install, trust, hook add, settings rows, processes, search, and the outbox. A hyphen in a typed command normalizes in a later plan. This plan does not rewrite a hyphen inside `plugin.json`. The human `add` line stays as it is. The plain sentence for `plugin_protocol_unsupported` is not in a decision and is not in COPY-PENDING. This plan does not print one.

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md).

The parser is hand-written in this module. It does not add a library. It does not read the filesystem except the bytes the caller passes. Tests live in `tests/test_plugins.c`, suite `plugins`, and that suite is added to `tests/gate-suites`.

The uncommitted work on branch `plan/25-plugins` is this module. Continue that module. Do not implement the other stages of the deleted plan on that branch.

## What must be true

- A document that `schema/plugin/v1/manifest.json` accepts parses. `format` is `remember-plugin`. `version` is 1. `protocol` is 1.
- A plugin name and an entry name match `^[a-z][a-z0-9_]{0,39}$`. A hyphen in a manifest name is `plugin_manifest_invalid`. The message is `"<name>" in plugin.json can't contain "-".` The fix is `Use "<value>".` `<value>` is the name with each `-` replaced by `_`.
- A manifest setting whose key is `enabled` is `plugin_manifest_invalid`. The message is `"<name>" declares a setting named "enabled", and that name is reserved.` The fix is `Rename that setting in plugin.json.`
- An unknown field on an object the schema closes (`additionalProperties` false), including `run`, `permissions`, and `filter`, is `plugin_manifest_invalid`. `detail.path` is a JSON Pointer. `detail.rule` is the schema rule id. The message and the fix follow `decision:plugin-manifest-errors`: the message names the path and the field, and the fix names the field the schema allows when one name is within one edit.
- An unknown field on an object the schema leaves open warns with the C-35 file line: `remember: warning: "<path>" has a field remember doesn't know, "<name>". The rest of the file was read.` The rest of the document still parses.
- `plugin add` and `plugin test` report every manifest error in that file in one result. This plan's tests call the parser. They do not add the commands.
- A `protocol` other than 1 returns `plugin_protocol_unsupported`. The test asserts that code and no plain sentence.
- An omitted `budget` is `800ms`. An omitted `cold_start_grace` is `10s`. An omitted filter `priority` is 500. An omitted action `process` is `once`. An omitted `origins` is `["local"]`.
- `cold_start_grace` of `0ms` is `plugin_manifest_invalid`. `1ms` and `30s` parse. A grace above `30s` is `plugin_manifest_invalid`.
- `priority`, `budget`, and `cold_start_grace` are manifest fields. This plan does not read a setting for them.

## Review points

- The reviewer checks that a hyphen in `plugin.json` is rejected and is not rewritten.
- The reviewer checks that an unknown field on a closed object is an error and an unknown field on an open object is the C-35 warning.
- The reviewer checks that the parser prints no sentence that this plan does not quote.
- The reviewer checks that the branch does not add a process, a trust file, or an outbox write.

## Definition of Done

- [ ] `a schema-valid manifest parses` (`tests/test_plugins.c`, the plan 24 hello fixture).
- [ ] `a hyphen in a manifest name prints the C-36 hyphen lines`.
- [ ] `a setting key enabled prints the C-36 reserved-key lines`.
- [ ] `an unknown field on run is plugin_manifest_invalid with a JSON Pointer`.
- [ ] `an unknown root field prints the C-35 file line and the rest parses`.
- [ ] `two errors in one file both come back from one parse`.
- [ ] `protocol 2 returns plugin_protocol_unsupported and prints no sentence`.
- [ ] `omitted budget, grace, priority, process, and origins are the defaults above`.
- [ ] `0ms grace is invalid, 1ms and 30s parse, and 31s is invalid`.
- [ ] `specs/plugin-manifest.md` is written, and the decisions this plan built are `state:implemented`.
- [ ] The gate passes on the committed tree, and the notes record the mutants and their red tests.

## Implementation Notes

## Review Notes
