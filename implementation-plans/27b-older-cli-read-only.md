# 27b — Older CLI, newer brain: read-only with a warning (`schema_too_new`)

**Module:** remember CLI (C): the store open path and command dispatch (`src/store_sqlite.c`, `src/remember_app.c`, `src/output.c`, `src/diag.c`).
**Needs:** plans 19 and 21 (both merged). No other plan.
**Blocked on:** copy item R29-4 (Story Weaver copy request, 2026-11-11, not yet approved).

## Goal

When a CLI meets a brain whose `schema_version` or `min_reader_version` is newer than it knows, the CLI opens the brain read-only. Every read works and warns on every command. Every write fails with `schema_too_new` and exit 75. Every message shows schema versions, not engine versions.

## Source

Decisions built:

- decision:2026-10-31-old-cli-read-only, amended by decision:2026-10-31-old-cli-reads-warn-writes-fail
- decision:2026-10-31-cli-outdated-detect
- decision:2026-10-31-cli-outdated-warning-frequency
- decision:2026-10-31-cli-outdated-exit-code
- decision:2026-11-01-cli-outdated-merge
- decision:2026-11-01-newer-brain-error-name
- decision:2026-11-01-cli-outdated-version-shown
- decision:2026-11-03-schema-read-only (warning code, fields, and the `min_reader_version` trigger)
- decision:2026-10-31-coexistence-copy (read-only warning copy; see R29-4)
- decision:2026-10-31-coexistence-copy-2 (write failure copy; see R29-4)

Spec sections this plan changes:

- `specs/OLDER-CLI.md` (new). This plan does not create `specs/DAEMON.md`; plan 39 does.
- The spec section that holds 007 ST-10.2 (`schema_too_new`, `min_reader_version`): change it to the merged rule in the same PR. When no spec holds it yet, write it in `specs/OLDER-CLI.md` and name the frozen log 007 ST-10.2 as history.

## Out of scope

- The daemon, `daemon.hello`, and takeover (plans 39 to 42).
- Any change to brew remember 0.2.0.

## Standards

- **Coding bar.** `standard:coding` and its matching narrow keys. The decisions in Source and the accepted decisions of their subject are loaded before work starts (`workflow:decision-tags` READ).
- **Decisions are law.** The plan builds only what an accepted decision or a frozen log states. A choice that no decision makes is a grill, recorded as a decision before it is built.
- **Spec.** The spec sections in Source ship in the same pull request, each naming its decision keys. Each decision built here is `state:implemented` in the same pull request.
- **Gate.** `just check` (or the repo gate) passes on the exact committed tree with no warnings. `scripts/check-coverage.sh` stays at 100% effective. Linux CI (`scripts/ci-linux.sh`) runs every test that is not macOS-only.
- **Proof.** TDD: the Definition of Done boxes are the must-pass tests, in TDD order. Each test is red before its production change. Every rule has a test that a one-clause mutant turns red. Expected values are literals or an independent computation.
- **OS integrations.** Share, Spotlight, login items, Shortcuts, and other OS integrations are Fortkeeps-only. They never go in the open-source engine or daemon (decision:2026-11-03-os-integrations-fortkeeps-only).
- **Copy.** Both human lines equal the R29-4 decision text, which re-words the locked lines for schema versions.
- **Prerequisites.** Work starts only when each item below holds. When one does not hold, the work stops and Itay is told:
  - The copy item **R29-4 (Story Weaver)** is a decision: The locked lines say "a newer Remember (v<brain>), and you have v<cli>". With schema numbers, `v7` reads as an app version. Re-word both lines (read warning and write failure) for schema versions.

## What must be true

The rules below hold. Each quote is decision text.

- Merge: "cli_outdated and the existing schema_too_new merge into one error (name to settle in plan 29)." ("plan 29" in that decision means this plan, 27b.)
- Name: "The merged newer-brain error is named schema_too_new (cli_outdated is dropped); it carries the read-only warning rules and exit 75."
- Detection: "An old CLI detects a newer brain by comparing the brain's schema_version with the highest schema it knows."
- Reads: "reads work with a stderr warning (JSON: warnings[] code cli_outdated_read_only with cli_version, daemon_version, fix: brew upgrade remember)". Per decision:2026-11-01-cli-outdated-version-shown and the 2026-11-03 answer, the warning code is `schema_too_new_read_only` and the fields are `brain_schema_version` and `cli_schema_version`.
- Frequency: "The cli_outdated_read_only warning is shown on every command that reads a newer brain (stderr, and in warnings with --json)."
- Exit code: "The cli_outdated write failure exits with code 75 (EX_TEMPFAIL)." After the merge, this is the exit code of `schema_too_new`.
- Versions: "The old-CLI messages show schema versions (brain schema vs CLI schema), not engine versions."
- Human read warning (stderr), locked in decision:2026-10-31-coexistence-copy: `This brain was saved by a newer Remember (v<brain>), and you have v<cli>. Opening it read-only. To make changes, upgrade: brew upgrade remember`. `<brain>` and `<cli>` are now schema versions (see R29-4).
- Human write failure, locked in decision:2026-10-31-coexistence-copy-2: `Can't save: this brain was saved by a newer Remember (v<brain>), and you have v<cli>. To make changes, upgrade: brew upgrade remember`.
- Current code: `check_reader_version` in `src/store_sqlite.c` sets `schema_too_new` when `brain_meta.min_reader_version` is newer than the engine. `begin_write` then returns `STORE_ERR_SCHEMA_TOO_NEW` (007 ST-10.2, plan 19). This plan changes the trigger to `schema_version` and adds the warning and exit 75.
- Note: brew remember 0.2.0 already refuses a newer brain with `database is newer`. This plan cannot change 0.2.0.

## Review points

- The read-or-write class of every command is one table, and a new command that is missing from it fails a test.
- A refused write leaves the brain file byte-identical, the post-command sweep included.
- No output path emits `cli_outdated`.
- A newer `min_reader_version` triggers read-only as a newer `schema_version` does (decision:2026-11-03-schema-read-only).
- A hostile `brain_meta` value is masked in human and JSON output.
- The diff stays inside these files. A change outside them is a finding. Each change in them follows What must be true:
  - `src/store_sqlite.c`, `src/store.h`.
  - `src/remember_app.c`.
  - `src/output.c`.
  - `src/diag.c`, `src/diag.h`.
  - `src/exit_codes.h`.
  - `tests/test_old_cli.c` (new), `tests/fixtures/` (a brain stamped with a higher `schema_version`). Update the plan 19 `schema_too_new` tests to exit 75.
  - `specs/OLDER-CLI.md` §8.

## Definition of Done

- [ ] `detect_by_schema_version`: a brain with `schema_version` one higher than the highest known schema is a newer brain. An equal `schema_version` is not.
- [ ] `newer_brain_read_human`: `remember get` on a newer brain prints the entry on stdout and the read-only line on stderr with the brain schema and the CLI schema. Exit 0.
- [ ] `newer_brain_read_json`: with `--json`, stdout has `warnings[]` with code `schema_too_new_read_only`, `brain_schema_version`, `cli_schema_version`, and fix `brew upgrade remember`. Stderr is empty (JE-2.8).
- [ ] `newer_brain_write_human`: `remember add` exits 75 and prints the `Can't save:` line with the two schema versions. The brain file is byte-identical after the command.
- [ ] `newer_brain_write_json`: `error.code` is `schema_too_new` with the same fields. Exit 75.
- [ ] `no_cli_outdated_code`: no output path emits the code `cli_outdated`.
- [ ] `every_write_command_refused`: run every write command (add, update, delete, link, unlink, rekey, purge, purge-trash, import, conflict accept, repair, settings set). Each one exits 75 with `schema_too_new`.
- [ ] `every_read_command_warns_every_time`: run every read command twice in a row. Each run prints the warning once. No run is silent.
- [ ] `post_command_sweep_skipped`: the post-command sweep (decision:2026-10-31-no-sweep-agent) does not write to a newer brain and does not change the exit code.
- [ ] `version_echo_hostile`: a hostile schema value in `brain_meta` (non-integer, 300 bytes, control bytes) is masked in human and JSON output (standard:coding §3).
- [ ] `same_version_no_warning`: an equal schema prints no warning.
- [ ] `min_reader_version_path`: a brain with an equal `schema_version` and a newer `min_reader_version` is a newer brain: reads warn with `schema_too_new_read_only`, and writes exit 75 with `schema_too_new`.

- [ ] `specs/OLDER-CLI.md` §8 and the ST-10.2 spec text name the decision keys.
- [ ] The decisions in Source have `state:implemented`.
- [ ] The full gate passes on the committed tree.
- [ ] The Implementation Notes record the mutants and their red tests, the probes past this plan, and every deviation.
- [ ] `.claude/REVIEW_REQUEST` is written per `workflow:stage-review-handoff`.

## Implementation Notes

(Empty.)

## Review Notes

- decision:2026-10-31-cli-outdated-exit-code changes the "exit 1" of decision:2026-10-31-old-cli-reads-warn-writes-fail, and decision:2026-11-01-newer-brain-error-name drops its `cli_outdated` code. Neither older entry carries an "[Amended by …]" line. Add the lines in Remember before work starts (this pass did not edit Remember).
- Reshaped 2026-11-03 to the no-steps plan shape (decision:2026-11-03-plan-no-steps). The Files list is a review point, the Context rules are What must be true, the must-pass tests are the Definition of Done, and the open items are Prerequisites.
