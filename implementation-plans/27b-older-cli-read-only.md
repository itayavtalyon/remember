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
- Newer only by `min_reader_version` (R27b-2, Q1 A). This applies when this remember opens the brain read-only because of `min_reader_version` and the brain's `schema_version` is not greater than the CLI's. When the `schema_version` is greater, R27b-1 prints, whatever `min_reader_version` says. The code, exit, `retryable` and `fix` do not change: `schema_too_new_read_only` (exit follows the command), `schema_too_new` (exit 75, `retryable` false), `fix` `Upgrade remember to make changes.` `<min>` is the stored `min_reader_version`; `<version>` is the string `remember --version` prints after `remember ` (answer 1; `--version` does not change).
  - Variant 1, when the store's version parser reads `min_reader_version` and the value is at most 32 bytes. `message` (both codes): `this brain was made by a newer version of remember, so it is read-only here. You can still read and export it. This brain needs remember <min> or later; this is remember <version>.` The plain lines are the R27b-1 plain lines with that message: `remember: warning: <message> <fix>` for the read warning and `remember: <message> <fix>` for the write failure. Example: `remember: this brain was made by a newer version of remember, so it is read-only here. You can still read and export it. This brain needs remember 0.4.0 or later; this is remember 0.3.2. Upgrade remember to make changes.`
  - Variant 2, when the store's version parser cannot read `min_reader_version` (the store already counts it as newer), or the value has more than 32 bytes (over-long is hostile, answer 2). The line prints no number. `message`: `this brain was made by a newer version of remember, so it is read-only here. You can still read and export it.` This is the approved C-01 `schema_too_new` text word for word.
  - `detail` in both variants and both codes: `{"brain_schema_version": "<brain>", "cli_schema_version": "<cli>", "min_reader_version": "<min>", "cli_version": "<version>"}`. The two schema members stay (`decision:2026-11-03-schema-read-only`) and are equal in this case. `min_reader_version` is the stored value in Variant 1. In Variant 2 it is `""`: a hostile value is masked, not echoed (Itay 2026-11-14, `decision:2026-11-14-r27b-2-3-stop-and-ask`, answer 2). `cli_version` is this remember's version. The two new member names are an [ASSUMPTION] of the approved draft.
  - `src/diag.h`: the comment that says the version fields are "never an app version" changes. It says that `brain_schema_version` and `cli_schema_version` are schema numbers, and that `min_reader_version` and `cli_version` are app versions.
  - user.db: nothing in this plan prints a newer `user_meta.min_reader_version`. That needs its own copy item when a plan prints it (Q2 A).
- `--help` exit codes (R27b-3). After the `4` line, the "Exit codes" block of `remember --help` gains two lines, exactly: `  5  confirmation or approval required` (answer 4: it is true for every exit 5, `plugin_approval_required` and `hook_approval_required` included) and `  75 brain is read-only (made by a newer version of remember)`. The `75` line has one space after the number. The five old lines do not change. The text is in `src/remember_app.c`; any test that locks the general help text changes with it.
- Current code: `check_reader_version` in `src/store_sqlite.c` sets `schema_too_new` when `brain_meta.min_reader_version` is newer than the engine. `begin_write` then returns `STORE_ERR_SCHEMA_TOO_NEW` (007 ST-10.2, plan 19). This plan changes the trigger to `schema_version` and adds the warning and exit 75.
- Note: brew remember 0.2.0 already refuses a newer brain with `database is newer`. This plan cannot change 0.2.0.

## Stop and ask

The coder stopped and asked Itay at each of these. Itay answered on 2026-11-14 (`decision:2026-11-14-r27b-2-3-stop-and-ask`, the recommended option each time):

1. `<version>` is the full string `remember --version` prints today, for a dev build or a suffix too. `--version` does not change.
2. A hostile `min_reader_version` is masked in `detail` (it is not echoed, not JSON-escaped), as plan 27b masks hostile schema values. The human line is Variant 2. Over-long (more than 32 bytes), not `MAJOR.MINOR.PATCH`, and control bytes are hostile.
3. R27b-2 amends `decision:2026-11-01-cli-outdated-version-shown` for the `min_reader_version`-only case.
4. The exit 5 help line is `  5  confirmation or approval required`. The 75 line is as approved.

## Review points

- The read-or-write class of every command is one table, and a new command that is missing from it fails a test.
- A refused write leaves the brain file byte-identical, the post-command sweep included.
- No output path emits `cli_outdated`.
- A newer `min_reader_version` triggers read-only as a newer `schema_version` does (decision:2026-11-03-schema-read-only).
- A hostile `brain_meta` value is masked in human and JSON output. For `min_reader_version` the `detail` member is `""` and the human line is Variant 2 (Stop and ask 2).
- The diff stays inside these files. A change outside them is a finding. Each change in them follows What must be true:
  - `src/store_sqlite.c`, `src/store.h`.
  - `src/remember_app.c` (the version branch of the two lines and the `--help` exit-code block).
  - `src/output.c`.
  - `src/diag.c`, `src/diag.h`.
  - `src/exit_codes.h`.
  - `tests/test_old_cli.c` (new), `tests/fixtures/` (a brain stamped with a higher `schema_version`). Update the plan 19 `schema_too_new` tests to exit 75.
  - `specs/OLDER-CLI.md` §8.

## Definition of Done

- [x] `detect_by_schema_version`: a brain with `schema_version` one higher than the highest known schema is a newer brain. An equal `schema_version` is not.
- [x] `newer_brain_read_human`: `remember get` on a newer brain prints the entry on stdout and the read-only line on stderr with the brain schema and the CLI schema. Exit 0.
- [x] `newer_brain_read_json`: with `--json`, stdout has `warnings[]` with code `schema_too_new_read_only`, `brain_schema_version`, `cli_schema_version`, the R27b-1 `message`, and `fix` `Upgrade remember to make changes.` Stderr is empty (JE-2.8).
- [x] `newer_brain_write_human`: `remember add` exits 75 and prints the R27b-1 write-failure plain line with the two schema versions. The brain file is byte-identical after the command.
- [x] `newer_brain_write_json`: `error.code` is `schema_too_new` with the same fields. Exit 75.
- [x] `no_brew_command_in_output`: no human line and no JSON `message` or `fix` of this plan contains `brew upgrade remember`, and the read warning and the write failure print the R27b-1 text exactly (SW1 A).
- [x] `no_cli_outdated_code`: no output path emits the code `cli_outdated`.
- [x] `every_write_command_refused`: run every write command (add, update, delete, link, unlink, rekey, purge, purge-trash, import, conflict accept, repair, settings set). Each one exits 75 with `schema_too_new`.
- [x] `every_read_command_warns_every_time`: run every read command twice in a row. Each run prints the warning once. No run is silent.
- [x] `post_command_sweep_skipped`: the post-command sweep (decision:2026-10-31-no-sweep-agent) does not write to a newer brain and does not change the exit code.
- [x] `version_echo_hostile`: a hostile schema value in `brain_meta` (non-integer, 300 bytes, control bytes) is masked in human and JSON output (standard:coding §3).
- [x] `same_version_no_warning`: an equal schema prints no warning.
- [x] `min_reader_version_path`: a brain with an equal `schema_version` and a newer `min_reader_version` is a newer brain: reads warn with `schema_too_new_read_only`, and writes exit 75 with `schema_too_new`.
- [x] `min_reader_variant_1_read_and_write_lines`: with `min_reader_version` `0.4.0` and this remember `0.3.2`, the read warning and the write failure print the R27b-2 Variant 1 plain lines, and the JSON `message` and `fix` match.
- [x] `min_reader_variant_2_for_unparseable_value`: a `min_reader_version` the store cannot parse, or one of more than 32 bytes, prints the C-01 `schema_too_new` text with no number in the human line and in `message`.
- [x] `min_reader_detail_has_four_members_both_variants_both_codes`: `detail` holds `brain_schema_version`, `cli_schema_version`, `min_reader_version` and `cli_version`, and a hostile `min_reader_version` is masked (`""`), not echoed (answer 2).
- [x] `greater_schema_prints_r27b_1_whatever_min_reader_says`: a brain with a greater `schema_version` and a newer `min_reader_version` prints the R27b-1 line with the two schema numbers.
- [x] `help_exit_codes_list_5_and_75`: `remember --help` prints the two new lines exactly (`  5  confirmation or approval required` and the `75` line), after the `4` line, and the five old lines are unchanged.

- [x] `specs/OLDER-CLI.md` §§ 8, 8.5, 8.7 and 8.8 name the decision keys, including `decision:2026-11-14-copy-r27b-2-r27b-3-approved` and `decision:2026-11-14-r27b-2-3-stop-and-ask`.
- [x] The decisions in Source have `state:implemented`. (The R27b-1 decisions and the review grill do; `decision:2026-11-14-copy-r27b-2-r27b-3-approved` and `decision:2026-11-14-r27b-2-3-stop-and-ask` are set in the R27b-2/3 follow-up.)
- [x] The full gate passes on the committed tree (R27b-1 scope, PR #72).
- [x] The Implementation Notes record the mutants and their red tests, the probes past this plan, and every deviation (R27b-1 scope).
- [x] `.claude/REVIEW_REQUEST` is written per `workflow:stage-review-handoff`.

Status (2026-11-14): PR #72 built R27b-1. The R27b-2 and R27b-3 boxes were built in the follow-up on branch `claude/r27b-2-3-9167ab` (see "R27b-2 and R27b-3 follow-up" in Implementation Notes), after Itay answered the four Stop and ask points.

## Implementation Notes

Tests are in `tests/test_old_cli.c` (suite `old_cli`, in `tests/gate-suites`). DoD name -> test:

| DoD box | Test |
|---|---|
| `detect_by_schema_version` | `old_cli_detect_by_schema_version` (also `store_open_reads_a_newer_user_version_read_only`) |
| `newer_brain_read_human` | `old_cli_newer_brain_read_human` |
| `newer_brain_read_json` | `old_cli_newer_brain_read_json` |
| `newer_brain_write_human` | `old_cli_newer_brain_write_human` (byte-identical file) |
| `newer_brain_write_json` | `old_cli_newer_brain_write_json` |
| `no_brew_command_in_output` | `old_cli_no_brew_command_in_output` |
| `no_cli_outdated_code` | `old_cli_no_cli_outdated_code` (outputs and both catalogs) |
| `every_write_command_refused` | `old_cli_every_write_command_is_refused` (15 invocations, human and `--json`, file byte-identical) |
| `every_read_command_warns_every_time` | `old_cli_every_read_command_warns_every_time`, `old_cli_export_warns_every_time` |
| `post_command_sweep_skipped` | `old_cli_a_command_leaves_no_journal_and_no_new_bytes` (see deviation 1) |
| `version_echo_hostile` | `old_cli_hostile_min_reader_version_is_never_echoed`, `old_cli_the_largest_schema_prints_as_digits` |
| `same_version_no_warning` | `old_cli_same_version_no_warning` |
| `min_reader_version_path` | `old_cli_min_reader_version_path` |

Other tests: `old_cli_every_command_has_a_class` and `old_cli_the_class_table_splits_reads_from_writes` (the class table; a command missing from it fails), `old_cli_the_store_refuses_a_write_on_a_newer_schema`, `old_cli_strict_turns_a_read_warning_into_exit_4_and_keeps_exit_75`, `old_cli_help_and_version_do_not_open_a_brain`.

**Mutants** (one clause each; build exit checked; the listed tests went red; all killed). Run order: `/private/tmp` scripts, ASan build with `-DREMEMBER_TEST_VFS=ON`.

- Detection: `version > SCHEMA_VERSION` to `+ 1` (open fails; detect, read, write tests). `schema_too_new = true` to `false` in the newer branch. `store_newer_brain` never newer (`old_cli_newer_brain_read_*`); always newer (`old_cli_same_version_no_warning`, `old_cli_detect_by_schema_version`). `brain_schema` not stored, or the two gap fields swapped or mislabeled (`old_cli_detect_by_schema_version`, `old_cli_newer_brain_read_json`, hostile test).
- min_reader_version: `>` to `<` in `reader_too_old` (`old_cli_min_reader_version_path`, plan 19 tests); trigger ignored (`old_cli_min_reader_version_path`, hostile test); trigger always on (`old_cli_same_version_no_warning`).
- Store refusal: `begin_write` check off (`old_cli_the_store_refuses_a_write_on_a_newer_schema`, `schema_v6_a_newer_min_reader_version_makes_the_brain_read_only`); the repair check off (`schema_v6_a_newer_min_reader_version_makes_the_brain_read_only`).
- Class table: ADD, BRAIN, REPAIR, IMPORT, CONFLICT to read (`old_cli_every_write_command_is_refused`); GET, DEVICE, EXPORT to write (`old_cli_every_read_command_warns_every_time`, `old_cli_export_warns_every_time`); settings `set`, `reset`, `import` each dropped, and `get` added (`old_cli_every_write_command_is_refused`, `old_cli_every_read_command_warns_every_time`, `old_cli_the_class_table_splits_reads_from_writes`).
- App: exit 75 to 1 (write tests) and the constant 75 to 74; read warning removed (read tests); write also warns (`old_cli_strict_turns_a_read_warning_into_exit_4_and_keeps_exit_75`, write tests); the command still runs after a refusal (write tests).
- Diag: JSON warning without `fix` (`old_cli_newer_brain_read_json`); message numbers swapped (all message tests); detail value wrong in the warning and in the error (`old_cli_newer_brain_read_json`, `old_cli_newer_brain_write_json`); plain `warning: ` prefix removed; code name `cli_outdated_read_only` (`old_cli_no_cli_outdated_code`, `warning_catalog_round_trips_every_code`); catalog `fix` with a brew command (`diag_code_fix_is_the_codes_sentence` and others).
- Four first-round mutants did not compile (`-Werror`); each was redone as a valid rewrite. Their first-round results were discarded.

**Deviations and gaps**

1. `post_command_sweep_skipped`: the post-command sweep (plan 32) does not exist in the engine. The test proves what holds today: a read and a refused write leave the brain bytes identical and leave no `-journal` or `-wal`. The first sweep must call `store_newer_brain` and skip; `specs/OLDER-CLI.md` § 8.6 and `docs/engineering-notes.md` say so.
2. TDD order: the write gate (`meet_newer_brain`) landed in the same change as the read warning, so the write tests were never red against production code that lacked the gate. The mutants above are their red proof.
3. A newer brain whose only trigger is `min_reader_version` has an equal `schema_version`, so the line reads `This brain uses schema 6; this remember reads up to schema 6.` That is the literal result of R27b-1 plus the plan. It reads oddly. Itay answered with R27b-2 (the follow-up below prints the two app versions for that case).
4. `device new` is a read: it writes only the device id file and never the brain (007 ST-10.2 note). It works on a newer brain and warns. No decision names it; this keeps the behavior it had for a newer `min_reader_version` since plan 19. Itay may want it refused.
5. Write refusal runs before the command, so a write command with bad arguments on a newer brain reports `schema_too_new`, not a usage error.
6. The store's own `store_status_message(STORE_ERR_SCHEMA_TOO_NEW)` is now the R27b-1 message without the schema sentence (the engine does not know the CLI numbers). The CLI never prints it: it prints the full line from `diag_schema_too_new_error`.
7. (R27b-1 scope, now done) `--help` "Exit codes" listed 0 to 4 only. Rows 5 and 75 are built in the R27b-3 follow-up below.
8. Files outside the plan's list that changed, all required: `CMakeLists.txt`, `tests/test_main.c`, `tests/register.h`, `tests/gate-suites` (new suite); `tests/test_store.c`, `tests/test_verification_edges.c` (a user_version above 6 no longer fails the open), `tests/test_schema_v6.c`, `tests/test_envelope.c`, `tests/test_settings.c`, `tests/test_errors.c` (exit 75, the new text, the new warning code); `tests/COVERAGE.md` (renamed row); `specs/JSON-ENVELOPE.md` (a warning may carry `fix`, exit 75), `specs/JSON-STYLE.md` (exit 75), `specs/README.md`; `docs/engineering-notes.md`.
9. `import --from-db` with a newer source still reports `newer_database`. That is the import-source path, outside this plan.
10. `state:implemented` is set on the ten decisions this plan builds whole: `old-cli-read-only`, `old-cli-reads-warn-writes-fail`, `cli-outdated-detect`, `cli-outdated-warning-frequency`, `cli-outdated-exit-code`, `cli-outdated-merge`, `newer-brain-error-name`, `cli-outdated-version-shown`, `schema-read-only`, `sw1-upgrade-line-no-command`. Four stay `state:accepted` because they hold work this plan does not build: `coexistence-copy` and `coexistence-copy-2` (the Mac takeover sheet and Settings lines), `copy-batch-final` (every other plan's copy items), `e3-warnings-have-fix-field` (the `fix` split of every other warning, plans 28 and 42). The reviewer decides whether any should flip.
11. Decisions amended in Remember before work: `decision:2026-10-31-old-cli-reads-warn-writes-fail` (exit code, error name, warning code and fields, fix text) and `decision:2026-11-01-newer-brain-error-name` (warning code, trigger, R27b-1 text), each with one dated "[Amended 2026-11-13 by ...]" line.

**Probes past the plan** (run on the built binary):

Brain at `user_version=7`, ASan binary, `HOME` in a temp dir:

- `export --format json --out <the brain> --force`: exit 1 `--out is the brain file`, brain sha256 unchanged (the export guard predates this plan and holds on a newer brain).
- `--quiet list`: the warning still prints (quiet turns off progress lines only).
- `entries` table dropped from the v7 brain: `list` prints the warning, then `database error (SQLITE_ERROR)`, exit 1. A newer layout fails loudly after the warning, never silently.
- `brain_meta` dropped from the v7 brain: `list` works and warns (a newer schema skips the `min_reader_version` read).
- `device new`: exit 0 with the warning, brain sha unchanged, new id in `<brain>.d/device_id` (deviation 4).
- `import --from-db <v7 brain>` into a v6 brain: `database is newer than this remember`, exit 1 (deviation 9).
- `user_version=-5`: `unsupported database version`, exit 1, unchanged.
- `settings set --scope user ...`: exit 75 `schema_too_new`; user.db untouched.
- `user_version=2147483647`: `old_cli_the_largest_schema_prints_as_digits`.

**Simplify pass.** (1) `diag_schema_too_new_warning` and `_error` built the same detail array twice; one `SchemaTooNewText` now holds the message, the numbers and the fields (-8 lines). (2) The newer-brain check lives in one function, `meet_newer_brain`, not in each command: no command file changed. (3) No new store query: the schema number is read once, in `ensure_schema`. (4) Declined: a write-class flag on each `cmd_*` function. The table is one place and a test walks it; a flag per command would be 20 sites.

**Security audit and exploit hunt.** The numbers in the line come from `PRAGMA user_version` (a 32-bit int, at most 11 digits) and from the build; no `brain_meta` text is ever printed, so a hostile `min_reader_version` (non-integer, 300 bytes, ESC and newline, overflowing parts) shows only the fixed sentence (`old_cli_hostile_min_reader_version_is_never_echoed`). The message buffer is 256 bytes for a 175-byte sentence plus two 11-digit numbers; `old_cli_the_largest_schema_prints_as_digits` runs the widest case. Writes are refused twice: by the class table before the command, and by `begin_write` and the repair check in the store (`old_cli_the_store_refuses_a_write_on_a_newer_schema`). A refused write never opens a transaction, so the brain file is byte-identical. The open itself still binds the device id (a sidecar file beside the brain, never the brain) as it did for `min_reader_version` since plan 19.

**Gate** (local, no GitHub CI; commit f4dcb39, code tree identical to this commit, which differs only in this plan file): ASan/UBSan build with `-DREMEMBER_TEST_VFS=ON`: 1205 black-box tests and 527 store unit tests pass; `scripts/lint-all.sh` LINT OK (clang-tidy 22, cppcheck, gcc -fanalyzer, scan-build); `scripts/check-coverage.sh` functions, effective lines and pure-TU branches 100%; `scripts/ci-linux.sh` OK (ctest step_gate, store_asan_gate, spec_drift, no_shim_symbols, clang-format 18, IWYU); `scripts/codeql-local.sh` OK. Two gate findings fixed on the way: `commands_common.c:311` was uncovered because the CLI no longer reaches it (`code_for_store_status` is now tested directly), and the settings reads needed `REMEMBER_USER_DB` set on Linux.

### R27b-2 and R27b-3 follow-up (2026-11-14)

Built on main at 55a64f8. Tests are in `tests/test_old_cli.c`. DoD name -> test:

| DoD box | Test |
|---|---|
| `min_reader_variant_1_read_and_write_lines` | `old_cli_min_reader_variant_1_read_and_write_lines` (the `<version>` is read from `remember version`, never a literal) |
| `min_reader_variant_2_for_unparseable_value` | `old_cli_min_reader_variant_2_for_unparseable_value`, `old_cli_hostile_min_reader_version_is_masked` (8 hostile values: text, 300 bytes, ESC and newline, overflow, 305 bytes that parse, trailing space, empty), `old_cli_min_reader_version_prints_up_to_32_bytes` (32 prints, 33 is masked) |
| `min_reader_detail_has_four_members_both_variants_both_codes` | `old_cli_min_reader_detail_has_four_members_both_variants_both_codes` |
| `greater_schema_prints_r27b_1_whatever_min_reader_says` | `old_cli_greater_schema_prints_r27b_1_whatever_min_reader_says` (two values, the two-member detail) |
| `help_exit_codes_list_5_and_75` | `old_cli_help_exit_codes_list_5_and_75` (`--help`, `-h`, `help`; the whole block, nothing after it) |

Changed older tests: `old_cli_hostile_min_reader_version_is_never_echoed` became `old_cli_hostile_min_reader_version_is_masked`; `old_cli_min_reader_version_path` now expects Variant 1; `schema_v6_cli_a_write_to_a_newer_brain_prints_schema_too_new` and `envelope_brain_new_id_replaces_only_the_brain_id` expect the Variant 1 prefix (their `min_reader_version` is `0.4.0`); `tests/test_store.c` initializes the larger `StoreSchemaGap`.

Red first: the new tests were run against the main build and failed on the old text ("This brain uses schema 6; ..."); the help test failed on the missing lines. Then the store, diag, app and help changes made them green.

How it is built. The store keeps `min_reader` (`Store.min_reader`) only when `min_reader_version` made the brain read-only and `version_is_printable` holds: `MAJOR.MINOR.PATCH` and at most `STORE_VERSION_TEXT_MAX` (32) bytes. `store_newer_brain` reports it with `by_min_reader` (`brain_schema <= SCHEMA_VERSION`). `diag_schema_too_new_warning` and `_error` take `cli_version` (`REMEMBER_VERSION`, set by `meet_newer_brain`) and pick one of three messages: schema sentence (R27b-1), Variant 1, Variant 2. `detail` has two members for R27b-1 and four for R27b-2. A `_Static_assert` keeps `REMEMBER_VERSION` at 64 bytes or less, which the 384-byte message buffer holds.

**Mutants** (one clause each; build exit checked; run `old_cli,schema_v6_cli,envelope,ttl`, each build and run in its own process group with a timeout; all 22 killed). Two first-round mutants did not compile (an unused function under `-Werror`, an unreachable branch) and were redone as valid rewrites.
- 32-byte cap `<=` to `<` (`old_cli_min_reader_version_prints_up_to_32_bytes`); cap removed (that test and `old_cli_hostile_min_reader_version_is_masked`); parse test inverted (six tests); printable check made always true (four tests).
- `by_min_reader` `<=` to `<` (six tests); always true (`old_cli_greater_schema_prints_r27b_1_whatever_min_reader_says` and the read/write command tests); the guard in diag dropped (three tests); `min_reader` never copied (six tests).
- diag: variant choice inverted (six tests); `nfields` always 4 or always 2 (`old_cli_greater_schema_prints_r27b_1_whatever_min_reader_says`, `old_cli_newer_brain_read_json`, `old_cli_min_reader_detail_has_four_members_both_variants_both_codes`); `<min>` and `<version>` swapped (four tests); the `cli_version` and `min_reader_version` member value or name wrong (two tests each); Variant 2 keeps the schema sentence (three tests).
- app: the warning and the error each pass a fixed version (three tests each).
- help: the `5` line dropped, the `5` line the old text, the `75` line with two spaces, the `75` line dropped (`old_cli_help_exit_codes_list_5_and_75`).

**Probes past the plan** (built binary, ASan, `HOME` in a temp dir): `remember --version` prints `remember 0.2.0`, unchanged. `min_reader_version` `0.4.0`: the read warning and the `--json add` error print Variant 1 with `"min_reader_version":"0.4.0","cli_version":"0.2.0"`, exit 75. ESC, newline and 300 bytes: Variant 2, `"min_reader_version":""`, no control byte on stderr. `--strict` read: exit 4. `--help` ends with the two new lines. `0.2.0` followed by a NUL byte and `x`: the write succeeds (exit 0). `select_text` cuts a value at the NUL (older than this plan), so the brain reads as `0.2.0`; see gap 3.

**Simplify pass (R27b-2/3).** (1) The `by_min_reader` flag could be derived in `diag.c` from `brain_schema <= cli_schema` (one field fewer). Declined: the store owns why a brain is read-only; a second rule in `diag.c` would be a second definition. (2) `SchemaTooNewText.min_reader` copies the value that `gap` already holds. Declined: `gap` is a by-value parameter and `fields` must outlive the call, so the copy is the lifetime fix. (3) The R27b-1 and R27b-2 messages could share one `snprintf` with `%s` for the ending. Declined: three formats keep each as a visible literal, so a test can compare it with the decision text. (4) Test side: `given_four_runs`, `assert_plain_lines` and `assert_json_notices` are shared by five tests. `old_cli_min_reader_version_path` overlaps `old_cli_min_reader_variant_1_read_and_write_lines` for the lines; it stays because it is the R27b-1 DoD box for the trigger (reads warn, writes exit 75).

**Security audit and exploit hunt (R27b-2/3).** The one new brain text that can reach a line is `min_reader_version`. It prints only after `version_is_printable`: at most 32 bytes, and `parse_version` accepts only digits and dots (each part starts with a digit and `strtol` must stop at a `.` or the end, so no sign, space, `0x`, newline or ESC). Anything else is `""` in `detail` and Variant 2 in the line (`old_cli_hostile_min_reader_version_is_masked`: text, 300 bytes, ESC and newline, overflow, 305 bytes that parse, trailing space, empty). `cli_version` is a build constant; the `_Static_assert` bounds it at 64 bytes. The widest message is 263 bytes of the 384-byte buffer (R27b-1's widest is 191), so `snprintf` never truncates; computed with a script from the literals. The trigger is unchanged (`reader_too_old`), so a hostile value still makes the brain read-only, never writable. One open edge: a NUL byte (gap 3). Writes are still refused twice (class table and `begin_write`); this plan touches neither, and `every_write_command_refused` passes.

**Deviations and gaps**
1. The masked `detail.min_reader_version` is `""`. Answer 2 says "masked" and the DoD requires four members, but no decision names the placeholder. `""` follows 27b's rule (the value is never echoed) and keeps the member; Itay may want another mask. One constant in `diag.c` and one test row.
2. The 32-byte limit on a printable `min_reader_version` is the coder's number. Answer 2 names "over-long" as hostile and gives no length. 32 is three 10-digit parts and two dots. Itay may want another limit; it is `STORE_VERSION_TEXT_MAX` and one test.
3. A `min_reader_version` with a NUL byte is read up to the NUL (`select_text`), so `0.2.0` + NUL + junk counts as a readable older version and the brain stays writable. This is older than this plan and not changed here (a fix needs the byte count in `select_text`, which many callers share). Itay may want a follow-up.
4. `<version>` is `REMEMBER_VERSION` (what `--version` prints), not `k_reader_version`. Until plan 26 moves the release to 0.3.0, a brain with `min_reader_version` `0.3.1` prints `this is remember 0.2.0`. Both facts are true; the line is not wrong, only odd.
5. Files outside plan 27b's list that changed, all required: `tests/test_store.c` (the struct), `tests/test_schema_v6.c` and `tests/test_envelope.c` (the copy), `docs/engineering-notes.md`, `specs/OLDER-CLI.md` (listed), `src/store.h` and `src/diag.h` (listed).
6. The `StoreSchemaGap` and `Store` structs have explicit `pad_` members, as the repo does for `-Wpadded`.

**Gate (R27b-2/3 follow-up).** Run on the committed code tree (the commit after this one differs only in this plan file; a machine reboot mid-run forced a restart of the whole sequence): `scripts/lint-all.sh` LINT OK (clang-tidy 22, cppcheck, gcc -fanalyzer, scan-build; the three `real_malloc/calloc/realloc` allocator-wrapper warnings are older and not in this diff); `scripts/check-coverage.sh` functions 100%, effective lines 100%, pure-TU branches 100%; `ctest` 6 of 6 (step_gate with the `old_cli` suite, store_asan_gate, spec_drift, no_shim_symbols); `scripts/ci-linux.sh` OK (clang-format 18, clang-tidy 18, IWYU); `scripts/codeql-local.sh` OK. Two gate findings fixed on the way: clang-tidy 22 (`readability-implicit-bool-conversion`, a magic `65U`, and an analyzer ArrayBound in the test helper `given_cli_version`) and clang-format 18 (formatted with the Ubuntu 24.04 clang-format in Docker).

## Review Notes

- decision:2026-10-31-cli-outdated-exit-code changes the "exit 1" of decision:2026-10-31-old-cli-reads-warn-writes-fail, and decision:2026-11-01-newer-brain-error-name drops its `cli_outdated` code. Neither older entry carries an "[Amended by …]" line. Add the lines in Remember before work starts (this pass did not edit Remember).
- decision:2026-10-31-coexistence-copy and -copy-2 carry an "[Amended 2026-11-12 …]" line in Remember (read-only and write-failure lines replaced by R27b-1). The two older entries named above still lack theirs.
- Reshaped 2026-11-03 to the no-steps plan shape (decision:2026-11-03-plan-no-steps). The Files list is a review point, the Context rules are What must be true, the must-pass tests are the Definition of Done, and the open items are Prerequisites.

### Second opinion (Opus, 2026-11-13)

Every Review point and Definition of Done box was checked against the code and the decisions in Source. Copy: the four R27b-1 lines match `decision:2026-11-12-copy-batch-final` byte for byte (the test literals were compared with the decision text). `cli_outdated` and `brew upgrade` appear nowhere in `src/`. The diff stays inside the listed files plus the required build and test files that deviation 8 lists. The class table covers every `CliCommand`. A forgotten entry is `APP_CMD_UNLISTED`, which `old_cli_every_command_has_a_class` refuses.

Fixed in review:

- **R1 (exploit, Improvement): a newer remember that upgrades the brain while this open waits for the write lock made the open fail.** `ensure_schema` read `user_version` 5, then `migrate_to_current` re-read 7 under `BEGIN IMMEDIATE` and returned "database is newer than this remember". Its comment still said "same refusal as the outer gate", but the outer gate no longer refuses a newer brain. Test `old_cli_a_brain_upgraded_during_the_open_opens_read_only`: a child opens the brain while the parent holds the lock and commits `user_version = 7`. It was red before the fix (child exit 1). Fix: `migrate_to_current` hands any version at or above 6 back to the caller (rc 1, `brain_schema` set). `ensure_schema` sends current and newer brains through `bind_identity`, which now sets `schema_too_new` for a newer schema. `check_version` is gone, because both of its callers now see only a negative version (-9 lines).
- **R2 (Nit): `docs/engineering-notes.md` `store_open` line still said `>6→error`.** It now points to Read-only brains.
- **R3 (Remember):** the coder's two "[Amended 2026-11-13 …]" lines are accurate. `cli-outdated-warning-frequency` and `cli-outdated-exit-code` still named `cli_outdated…` with no amend line; each now has one. The `--kind cites` links that `workflow:decision-tags` asks for were added from `schema-read-only` and `copy-batch-final` to the amended entries.

Probes that held (built binary, brain at `user_version` 7): a brain with no `<brain>.d/device_id` mints the sidecar under an empty write transaction and leaves the brain bytes identical (now `old_cli_a_read_without_a_sidecar_leaves_the_brain_unchanged`). `settings --json set` is a usage error, not a read (the verb must be `rest_argv[0]`). The fixed 256-byte message holds `INT_MAX`.

Review mutants (build exit checked, `old_cli` run; all killed): DEVICE to write (`old_cli_device_new_is_a_read_that_warns`, `old_cli_every_read_command_warns_every_time`); settings `import` dropped from the write verbs (`old_cli_every_write_command_is_refused`, the `--dry-run` row); `migrate_to_current` without `s->brain_schema = version`, and with `==` for `>=` (`old_cli_a_brain_upgraded_during_the_open_opens_read_only`); the newer check at `bind_identity` off (six `old_cli` tests).

Simplify pass (S):
- R1 removes `check_version` and the duplicated newer-brain branch (-9 lines).
- Declined: a shared `write_fix` for the error and warning `"fix"` members. It came to +3 lines.
- Declined: moving `settings_verb_writes` beside `settings_verb` in `cmd_settings.c`. It would be one less place naming the verbs, but no lines are saved and it is an API change outside the file list. Left as a Nit.
- Test overlap: `old_cli_no_brew_command_in_output` re-asserts the exact lines that the read and write tests already pin. It is kept as the named DoD test.

Security pass (A): the numbers come from `PRAGMA user_version` and the build, and no `brain_meta` text is echoed. Writes are refused twice, by the class table and by `begin_write` plus the repair check. A refused write never opens a transaction. Known limits, not opened here:
- An `APP_CMD_UNLISTED` command would run as a read. Only the test, not the runtime, stops that, and the store refusal still holds.
- A newer brain in WAL mode with frames left by a crashed newer writer is checkpointed by this CLI's last close. The logical content is unchanged, the bytes are not.

Grills answered by Itay (`decision:2026-11-13-plan27b-review-grill`, G1 A, G2 A, G3 A, G4 yes): `device new` stays a read (`old_cli_device_new_is_a_read_that_warns`); the same-schema line ships as is; `settings set|reset --scope user` and `settings import --dry-run` exit 75 (rows in `old_cli_every_write_command_is_refused`); the four accepted decisions stay accepted. The G2 variant and the `--help` exit-code rows (5, 75) are copy requests with Story Weaver, follow-ups outside 27b.
