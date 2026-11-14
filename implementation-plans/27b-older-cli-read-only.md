# 27b — Older CLI, newer brain: read-only with a warning (`schema_too_new`)

**Module:** remember CLI (C): the store open path and command dispatch (`src/store_sqlite.c`, `src/remember_app.c`, `src/output.c`, `src/diag.c`).
**Needs:** plans 19 and 21 (both merged). No other plan.
**Blocked on:** nothing. The copy is item R27b-1 (called R29-4 in the request) in `decision:2026-11-12-copy-batch-final`, and items R27b-2 and R27b-3 in `decision:2026-11-14-copy-r27b-2-r27b-3-approved` (approved 2026-11-14). The R27b-2 and R27b-3 sentences are in [`COPY-APPROVED-2026-11-14.md`](COPY-APPROVED-2026-11-14.md).

## Goal

When a CLI meets a brain whose `schema_version` or `min_reader_version` is newer than it knows, the CLI opens the brain read-only. Every read works and warns on every command. Every write fails with `schema_too_new` and exit 75. Every message shows schema versions, not engine versions, except when only `min_reader_version` is newer: then the line names the two app versions (R27b-2).

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
- decision:2026-10-31-coexistence-copy (read-only warning; its `v<brain>`/`v<cli>`/`brew upgrade remember` line is replaced by R27b-1, amended 2026-11-12)
- decision:2026-10-31-coexistence-copy-2 (write failure; replaced by R27b-1 the same way)
- decision:2026-11-12-sw1-upgrade-line-no-command (SW1 A: the upgrade line contains no command)
- decision:2026-11-12-e3-warnings-have-fix-field (E3 B: a warning has a `message` and a `fix`)
- decision:2026-11-12-copy-batch-final (copy item R27b-1)
- decision:2026-11-14-copy-r27b-2-r27b-3-approved (R27b-2: Variant 1 and Variant 2 for a brain newer only by `min_reader_version`, with `min_reader_version` and `cli_version` in `detail`; R27b-3: the `--help` exit codes 5 and 75; Q1 A, Q2 A)
- decision:2026-11-13-plan27b-review-grill (G2 A, G4)

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
- **Copy.** Both human lines, the JSON `message` and the JSON `fix` equal copy item R27b-1 in `decision:2026-11-12-copy-batch-final`, or R27b-2 in [`COPY-APPROVED-2026-11-14.md`](COPY-APPROVED-2026-11-14.md) when only `min_reader_version` is newer. R27b-1 carries schema numbers, never app versions. R27b-2 Variant 1 names `min_reader_version` and this remember's version, as app versions. No line has a command. The `--help` lines are R27b-3.
- **Prerequisites.** Work starts only when each item below holds. When one does not hold, the work stops and Itay is told:
  - Copy item R27b-1 is approved (SW1 A, 2026-11-12). The Oct 31 lines (`v<brain>`, `v<cli>`, `brew upgrade remember`) retire.
  - Copy items R27b-2 and R27b-3 are approved (Itay, 2026-11-14, Q1 A and Q2 A).

## What must be true

The rules below hold. Each quote is decision text.

- Merge: "cli_outdated and the existing schema_too_new merge into one error (name to settle in plan 29)." ("plan 29" in that decision means this plan, 27b.)
- Name: "The merged newer-brain error is named schema_too_new (cli_outdated is dropped); it carries the read-only warning rules and exit 75."
- Detection: "An old CLI detects a newer brain by comparing the brain's schema_version with the highest schema it knows."
- Reads: "reads work with a stderr warning (JSON: warnings[] code cli_outdated_read_only with cli_version, daemon_version, fix: brew upgrade remember)". Per decision:2026-11-01-cli-outdated-version-shown and the 2026-11-03 answer, the warning code is `schema_too_new_read_only` and the fields are `brain_schema_version` and `cli_schema_version`.
- Frequency: "The cli_outdated_read_only warning is shown on every command that reads a newer brain (stderr, and in warnings with --json)."
- Exit code: "The cli_outdated write failure exits with code 75 (EX_TEMPFAIL)." After the merge, this is the exit code of `schema_too_new`.
- Versions: "The old-CLI messages show schema versions (brain schema vs CLI schema), not engine versions."
- Amended 2026-11-12 (SW1 A): the `fix` of the read warning is `Upgrade remember to make changes.`, with no command, and it is a separate field from `message` (E3 B). The "fix: brew upgrade remember" in the quote above no longer holds.
- Read warning, `schema_too_new_read_only` (copy item R27b-1; exit follows the command, 0 or 4 with `--strict`). `message`: `this brain was made by a newer version of remember, so it is read-only here. You can still read and export it. This brain uses schema <brain>; this remember reads up to schema <cli>.` `fix`: `Upgrade remember to make changes.` `detail`: `{"brain_schema_version": "<brain>", "cli_schema_version": "<cli>"}`. Plain line on stderr, on every read command: `remember: warning: this brain was made by a newer version of remember, so it is read-only here. You can still read and export it. This brain uses schema <brain>; this remember reads up to schema <cli>. Upgrade remember to make changes.`
- Write failure, `schema_too_new` (copy item R27b-1; exit 75, `retryable` false). `message`: `this brain was made by a newer version of remember, so it is read-only here. You can still read and export it. This brain uses schema <brain>; this remember reads up to schema <cli>.` `fix`: `Upgrade remember to make changes.` Plain line: `remember: this brain was made by a newer version of remember, so it is read-only here. You can still read and export it. This brain uses schema <brain>; this remember reads up to schema <cli>. Upgrade remember to make changes.` Example: `remember: this brain was made by a newer version of remember, so it is read-only here. You can still read and export it. This brain uses schema 8; this remember reads up to schema 7. Upgrade remember to make changes.`
- The sentences are the approved C-01 `schema_too_new` sentences with one added sentence for the schema numbers. The word `schema` is the only new term.
- Newer only by `min_reader_version` (R27b-2, Q1 A). This applies when this remember opens the brain read-only because of `min_reader_version` and the brain's `schema_version` is not greater than the CLI's. When the `schema_version` is greater, R27b-1 prints, whatever `min_reader_version` says. The code, exit, `retryable` and `fix` do not change: `schema_too_new_read_only` (exit follows the command), `schema_too_new` (exit 75, `retryable` false), `fix` `Upgrade remember to make changes.` `<min>` is the stored `min_reader_version`; `<version>` is this remember's version.
  - Variant 1, when the store's version parser reads `min_reader_version`. `message` (both codes): `this brain was made by a newer version of remember, so it is read-only here. You can still read and export it. This brain needs remember <min> or later; this is remember <version>.` The plain lines are the R27b-1 plain lines with that message: `remember: warning: <message> <fix>` for the read warning and `remember: <message> <fix>` for the write failure. Example: `remember: this brain was made by a newer version of remember, so it is read-only here. You can still read and export it. This brain needs remember 0.4.0 or later; this is remember 0.3.2. Upgrade remember to make changes.`
  - Variant 2, when the store's version parser cannot read `min_reader_version` (the store already counts it as newer). The line prints no number. `message`: `this brain was made by a newer version of remember, so it is read-only here. You can still read and export it.` This is the approved C-01 `schema_too_new` text word for word.
  - `detail` in both variants and both codes: `{"brain_schema_version": "<brain>", "cli_schema_version": "<cli>", "min_reader_version": "<min>", "cli_version": "<version>"}`. The two schema members stay (`decision:2026-11-03-schema-read-only`) and are equal in this case. `min_reader_version` is the stored value with JSON string escaping, in Variant 2 too. `cli_version` is this remember's version. The two new member names are an [ASSUMPTION] of the approved draft.
  - `src/diag.h`: the comment that says the version fields are "never an app version" changes. It says that `brain_schema_version` and `cli_schema_version` are schema numbers, and that `min_reader_version` and `cli_version` are app versions.
  - user.db: nothing in this plan prints a newer `user_meta.min_reader_version`. That needs its own copy item when a plan prints it (Q2 A).
- `--help` exit codes (R27b-3). After the `4` line, the "Exit codes" block of `remember --help` gains two lines, exactly: `  5  confirmation required (run again with --yes)` and `  75 brain is read-only (made by a newer version of remember)`. The `75` line has one space after the number. The five old lines do not change. The text is in `src/remember_app.c`; any test that locks the general help text changes with it.
- Current code: `check_reader_version` in `src/store_sqlite.c` sets `schema_too_new` when `brain_meta.min_reader_version` is newer than the engine. `begin_write` then returns `STORE_ERR_SCHEMA_TOO_NEW` (007 ST-10.2, plan 19). This plan changes the trigger to `schema_version` and adds the warning and exit 75.
- Note: brew remember 0.2.0 already refuses a newer brain with `database is newer`. This plan cannot change 0.2.0.

## Stop and ask

The coder stops and asks Itay, through the architect, at each of these. The plan does not choose.

1. What `<version>` prints for a build that is not a plain release (a dev build or a suffix), and what `--version` prints for it.
2. Whether a hostile `min_reader_version` is escaped (the approved draft) or masked (the plan's hostile-value review point and `standard:coding` §3) in `detail`.
3. Whether R27b-2 amends `decision:2026-11-01-cli-outdated-version-shown` ("show schema versions, not engine versions") for the `min_reader_version`-only case. The record says only that the "never an app version" comment changes.
4. Whether the `5` help line fits every exit 5. `plugin_approval_required` and `hook_approval_required` also exit 5, and those requests never take `--yes` (X1-8), so "run again with --yes" is wrong for them.

## Review points

- The read-or-write class of every command is one table, and a new command that is missing from it fails a test.
- A refused write leaves the brain file byte-identical, the post-command sweep included.
- No output path emits `cli_outdated`.
- A newer `min_reader_version` triggers read-only as a newer `schema_version` does (decision:2026-11-03-schema-read-only).
- A hostile `brain_meta` value is masked in human and JSON output. For `min_reader_version` the approved draft says JSON string escaping in `detail` and no printing in the human line (Variant 2); see Stop and ask 2.
- The diff stays inside these files. A change outside them is a finding. Each change in them follows What must be true:
  - `src/store_sqlite.c`, `src/store.h`.
  - `src/remember_app.c` (the version branch of the two lines and the `--help` exit-code block).
  - `src/output.c`.
  - `src/diag.c`, `src/diag.h`.
  - `src/exit_codes.h`.
  - `tests/test_old_cli.c` (new), `tests/fixtures/` (a brain stamped with a higher `schema_version`). Update the plan 19 `schema_too_new` tests to exit 75.
  - `specs/OLDER-CLI.md` §8.

## Definition of Done

- [ ] `detect_by_schema_version`: a brain with `schema_version` one higher than the highest known schema is a newer brain. An equal `schema_version` is not.
- [ ] `newer_brain_read_human`: `remember get` on a newer brain prints the entry on stdout and the read-only line on stderr with the brain schema and the CLI schema. Exit 0.
- [ ] `newer_brain_read_json`: with `--json`, stdout has `warnings[]` with code `schema_too_new_read_only`, `brain_schema_version`, `cli_schema_version`, the R27b-1 `message`, and `fix` `Upgrade remember to make changes.` Stderr is empty (JE-2.8).
- [ ] `newer_brain_write_human`: `remember add` exits 75 and prints the R27b-1 write-failure plain line with the two schema versions. The brain file is byte-identical after the command.
- [ ] `newer_brain_write_json`: `error.code` is `schema_too_new` with the same fields. Exit 75.
- [ ] `no_brew_command_in_output`: no human line and no JSON `message` or `fix` of this plan contains `brew upgrade remember`, and the read warning and the write failure print the R27b-1 text exactly (SW1 A).
- [ ] `no_cli_outdated_code`: no output path emits the code `cli_outdated`.
- [ ] `every_write_command_refused`: run every write command (add, update, delete, link, unlink, rekey, purge, purge-trash, import, conflict accept, repair, settings set). Each one exits 75 with `schema_too_new`.
- [ ] `every_read_command_warns_every_time`: run every read command twice in a row. Each run prints the warning once. No run is silent.
- [ ] `post_command_sweep_skipped`: the post-command sweep (decision:2026-10-31-no-sweep-agent) does not write to a newer brain and does not change the exit code.
- [ ] `version_echo_hostile`: a hostile schema value in `brain_meta` (non-integer, 300 bytes, control bytes) is masked in human and JSON output (standard:coding §3).
- [ ] `same_version_no_warning`: an equal schema prints no warning.
- [ ] `min_reader_version_path`: a brain with an equal `schema_version` and a newer `min_reader_version` is a newer brain: reads warn with `schema_too_new_read_only`, and writes exit 75 with `schema_too_new`.
- [ ] `min_reader_variant_1_read_and_write_lines`: with `min_reader_version` `0.4.0` and this remember `0.3.2`, the read warning and the write failure print the R27b-2 Variant 1 plain lines, and the JSON `message` and `fix` match.
- [ ] `min_reader_variant_2_for_unparseable_value`: a `min_reader_version` the store cannot parse prints the C-01 `schema_too_new` text with no number in the human line and in `message`.
- [ ] `min_reader_detail_has_four_members_both_variants_both_codes`: `detail` holds `brain_schema_version`, `cli_schema_version`, `min_reader_version` and `cli_version`, and a hostile `min_reader_version` is JSON-escaped.
- [ ] `greater_schema_prints_r27b_1_whatever_min_reader_says`: a brain with a greater `schema_version` and a newer `min_reader_version` prints the R27b-1 line with the two schema numbers.
- [ ] `help_exit_codes_list_5_and_75`: `remember --help` prints the two new lines exactly, after the `4` line, and the five old lines are unchanged.

- [ ] `specs/OLDER-CLI.md` §8 and the ST-10.2 spec text name the decision keys, including `decision:2026-11-14-copy-r27b-2-r27b-3-approved`.
- [ ] The decisions in Source have `state:implemented`.
- [ ] The full gate passes on the committed tree.
- [ ] The Implementation Notes record the mutants and their red tests, the probes past this plan, and every deviation.
- [ ] `.claude/REVIEW_REQUEST` is written per `workflow:stage-review-handoff`.

## Implementation Notes

(Empty.)

## Review Notes

- decision:2026-10-31-cli-outdated-exit-code changes the "exit 1" of decision:2026-10-31-old-cli-reads-warn-writes-fail, and decision:2026-11-01-newer-brain-error-name drops its `cli_outdated` code. Neither older entry carries an "[Amended by …]" line. Add the lines in Remember before work starts (this pass did not edit Remember).
- decision:2026-10-31-coexistence-copy and -copy-2 carry an "[Amended 2026-11-12 …]" line in Remember (read-only and write-failure lines replaced by R27b-1). The two older entries named above still lack theirs.
- Reshaped 2026-11-03 to the no-steps plan shape (decision:2026-11-03-plan-no-steps). The Files list is a review point, the Context rules are What must be true, the must-pass tests are the Definition of Done, and the open items are Prerequisites.
