# 24 — Plugin protocol in the repo

## Goal

Keep protocol 1, revision 6, as the spec in this repo. CI checks the spec. Schema files match the spec. Fixtures match the spec examples.

## Status

READY.

The spec, the changelog, `docs/JSON-STYLE.md`, and `scripts/validate_plugin_spec.py` are already on this branch. Do not edit the spec to add a behavior. A wording fix after revision 6 is a changelog revision and does not bump `protocol`. A breaking wire change is a new design log, not this plan.

## Source of truth

- [`design-logs/009-plugin-protocol.md`](../design-logs/009-plugin-protocol.md) Design, including PR-1 through PR-6 and the revision 6 amendment table.
- [`docs/PLUGIN-PROTOCOL.md`](../docs/PLUGIN-PROTOCOL.md) revision 6. The spec is the only normative wire text. Cite rule ids. Do not copy rules into comments.
- [`docs/PLUGIN-PROTOCOL-CHANGES.md`](../docs/PLUGIN-PROTOCOL-CHANGES.md).

Log 010 is the engine. It is plan 25. This plan does not implement the engine.

## Context

Revision 6 is already applied. The validator result to keep is 13 schemas, 29 examples, 81 negative cases, 0 code-check negatives, 14 positive cases, 0 failures.

`$id` values are `https://itayavtalyon.github.io/remember/schema/plugin/v1/<file>.json`. Itay turns on GitHub Pages. This plan does not change GitHub settings.

The spec has no conformance section. Do not add one.

## Locked decisions

| ID | Rule |
|---|---|
| PR-1 | The spec file is the only normative wire text. |
| PR-2 | A spec change adds a changelog revision, newest first. |
| PR-3 | The validator passes with 0 failures before a spec change merges. |
| PR-5 | `protocol` stays 1. |
| G-06 | The `$id` host is `itayavtalyon.github.io`. |
| S-07 | Message `format` is `remember-message` and `version` is 1. |

## Prerequisites

This branch already contains the spec and the validator. Branch from clean `main` after that spec has merged, or continue from this plans branch if the spec commits are in the same history. One branch. One pull request. Do not mix engine code from plan 25 into this pull request.

## Scope

In:

- A docs CI job that runs `scripts/validate_plugin_spec.py` and fails when the exit code is not 0.
- Extract each schema block into `schema/plugin/v1/*.json`. Fail CI when the extracted file differs from the spec block.
- Copy the spec examples, including the §2.4 hello plugin and the log-ids plugin, to `tests/fixtures/plugin/`. A test fails when a fixture differs from the spec example.
- A fetch of each `$id` URL, added only after Itay writes in Implementation Notes that GitHub Pages is on. Compare the response body to the file. Do not add a flag, a repository variable, or a skip switch. The stage does not exist in the workflow until that note is written.

Out:

- Engine dispatch, manifests at runtime, trust, search (plan 25).
- Turning on GitHub Pages.
- A new protocol number.
- Editing PLUGIN-PROTOCOL.md except a changelog line that records the extraction. Do not change a rule.

## Files

- CI config that already runs the C gate. Add a docs job. Do not create a second CI system.
- `schema/plugin/v1/`. One JSON file per spec schema block. The file name is the `$id` file name.
- `tests/fixtures/plugin/`.
- One small test or script that diffs fixtures against the spec. Prefer extending `scripts/validate_plugin_spec.py` if it already has an extract check. Do not add a second validator.

## Must-pass tests

| Test | Proves |
|---|---|
| 009-1 | The validator exits 0. |
| 009-2 | Extracted schema files equal the spec blocks. |
| 009-3 fixtures | Every spec example has a fixture with the same bytes. The desktop and in-app adapters are plan 25 and remember-mac. This plan does not run those adapters. |
| 009-4 | This plan adds no code comments that cite a missing rule id. Plan 25 satisfies the engine half of this check. |

## Stages

1. Add the docs CI job. Run the validator locally and in CI. Record the counts in Implementation Notes.
2. Extract schemas. Add the diff check. Fail the job on a difference.
3. Export fixtures. Add the byte compare.
4. Stop. Do not fetch `$id` URLs. When Itay writes that Pages is on, add the fetch compare as a later commit on this same plan, then run the gate again.

## How to work

Same loop as plan 19. Load `standard:coding` and the review docs.

Do not invent a protocol field, a skip flag, or a Pages workflow.

Quality gate: plan 19's commands, plus the validator. Coverage stays 100/100 if C code changes. A docs-only change still runs the validator and the fixture diff.

## Definition of done

- [ ] Validator exits 0 in CI.
- [ ] Schema files match the spec.
- [ ] Fixtures match the spec examples.
- [ ] `protocol` is still 1.
- [ ] GitHub settings are unchanged.
- [ ] The `$id` fetch is either not started, or Itay's note is in Implementation Notes and the fetch matches.
- [ ] Gate green. Second review clear. INDEX row 24 ticked after that review.

## Second review handover

Probes:

1. Run `scripts/validate_plugin_spec.py`. [exit 0, 81 negative cases]
2. Change one extracted schema byte. [CI or the local diff check fails]
3. Restore it. [check passes]
4. Confirm no GitHub Pages setting changed in the diff.
5. Confirm `protocol` in the spec is 1.

Do not re-open: revision 6 amendments, no conformance section, `$id` host, protocol frozen at 1.

## Implementation Notes

_(empty)_

## Review Notes

_(empty)_
