# 32 — Expiry sweep, purge and delete --expired

**Area:** the post-command sweep, `remember brain sweep [--all]`, `remember purge LOCATOR`, `remember delete --expired` (`src/sweep.c` new, `src/cmd_purge.c`, `src/store_sqlite.c`).
**Needs:** plans 26 (`brains.json` for `--all`) and 31 (events). No other plan.
**Blocked on:** nothing.

## Goal

Due memories get `expired_at` and one `memory.expired` event, after every read-write command and from `remember brain sweep`. Purge and `delete --expired` follow the old plan 25 rules.

## Source

- Log 010 H-1 (sweep), H-2.1, the event list; log 018 V1-17 and log 020 D-3 (no sweep LaunchAgent).
- Old plan 25 "Events" third paragraph and "Logs and CLI" (`sweep`, `purge`, `delete --expired`).
- Spec: `specs/EVENTS.md` gains "Expiry". `specs/API-SURFACE.md`: the `brain sweep` row moves from planned to shipped, and the `delete` and `purge` rows' Arguments name the forms this plan changes. `brain sweep` enters `k_subcommands` in `src/cli.c` (`decision:2026-11-20-subcommand-table`), and `app_command_class` (`src/remember_app.c`) classes it read-write: today every `brain` subcommand except `new-id` is no-store, so the row's rights and the cli suite fail until the class changes.
- `decision:2026-11-18-sweep-under-brain`: the command is `remember brain sweep [--all]`, not a top-level `sweep`. Request `brain.sweep`, reply `brain.sweep.result`. The event `cause.command` stays `sweep`.

## Out of scope

Dispatch after the sweep (plan 35 adds the call). The daemon's sweep (plan 39).

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md). Tests in `tests/test_sweep.c` (new, suite `plugins`).

## What must be true

- `expired_at` is device-local. Setting it does not bump `updated_at`, the version vector, or `change_seq`, and it does not sync.
- The sweep runs after every command that opened the brain read-write, including reads, after that command commits. A read-only open skips it. One transaction. It sets `expired_at = now` on each row with `deleted_at IS NULL AND expires_at <= now AND expired_at IS NULL`, and inserts one `memory.expired` outbox row per changed row (plan 31 rules). `occurred_at` is `expires_at`. `detected_at` is the sweep time. `cause.command` is `sweep`.
- A later or null `expires_at`, or a restore, sets `expired_at` to null in that same write. Restore of a row that is already due emits `memory.restored`, then the sweep emits `memory.expired`.
- `remember brain sweep` sweeps one brain. `remember brain sweep --all` reads only `brains.json` and sweeps each listed brain; a missing path warns `brain_missing` with the C-01 line and the entry stays.
- `remember purge LOCATOR` permanently deletes one row that is already expired or deleted, writes one tombstone, and emits `memory.purged`. A live row exits 3 `wrong_bin` with the C-01 `wrong_bin` row. Purge of an unmarked expired row emits only `memory.purged`.
- `delete --expired` moves every due, not-yet-deleted row to Deleted in one transaction, emits `memory.deleted` only, and has no cap. `delete --deleted` is an unknown option.
- When no plugin is installed the sweep inserts no outbox row.

## Review points

- The sweep never touches `change_seq` or the version vector (asserted by reading both before and after).

## Definition of Done

- [x] `sweep_emits_once_and_revival_clears` (010 test 14).
- [x] `sweep_no_plugins_no_row` (010 test 16).
- [x] `read_only_open_skips_sweep`.
- [x] `sweep_all_reads_brains_json_and_warns_missing`.
- [x] `purge_live_exits_3_wrong_bin`, `purge_deleted_writes_one_tombstone`, `delete_deleted_unknown` (010 test 28).
- [x] `delete_expired_moves_all_due_emits_deleted_only`.
- [x] `specs/EVENTS.md` "Expiry" written.
- [ ] The gate passes on the committed tree.

## Implementation Notes

Coder: Sonnet 5.5 session, 2026-11-18 to 2026-11-20. Stacked on plan 31's tip 8aee4b1 (STACK_BASE in `.claude/REVIEW_REQUEST`); plan 31 is not merged. The spec is `specs/EVENTS.md` "Expiry". Tests are in `tests/test_sweep.c` (CLI, suite `plugins`) and `tests/test_events.c` (store level, also in the ASan `remember_store_tests`).

**Built.**
- `src/store_sqlite.c`: `store_sweep` (reads first, takes no write lock when nothing is due; one transaction; `expired_at = now`; one `memory.expired` event per row through the plan 31 `ev_emit`), `store_soft_delete_expired`, the temp trigger `ex_clear`, `select_local_ids` (one id reader, now also behind `ev_touched_ids`). `src/events.c`: `occurred_at` of `memory.expired` is the row's `expires_at`.
- `src/sweep.c`, `src/sweep.h`: `sweep [--all]` (parse, count and `sweep.result`, the `--all` loop over `brains.json`).
- `src/remember_app.c`: `sweep_after` after every command that opened the brain (not after `sweep`), `run_sweep_all`, `arm_events` takes the command name so the sweep arms again as `"sweep"`.
- `src/cmd_locator.c`: `cmd_purge` (moved here from `src/cmd_purge.c`, which is deleted: it shares the locator parser), `cmd_delete` with `--expired` bulk. `src/commands_common.c`: `exit_wrong_list` also prints the C-01 purge line; `wrong_bin_fix`. `src/diag.c`: warning `sweep_failed`, error code `wrong_bin`, `diag_error_json_fix`.
- Docs: `specs/EVENTS.md` (Expiry), `specs/JSON-ENVELOPE.md`, `specs/OLDER-CLI.md` 8.6, `docs/engineering-notes.md`, README and skill lines for purge, delete and the error code.

**Grills answered by Itay** (all filed): `decision:2026-11-18-plan32-grill-purge-delete-forms` (the whole-bin `purge --expired|--deleted|--trash` are unknown options, `purge` takes no bin flag, `delete LOCATOR --expired` is a usage error), `decision:2026-11-18-plan32-grill-sweep-triggers` (the sweep runs after every command whatever its exit; a failed sweep warns), `decision:2026-11-18-plan32-grill-sweep-warning-and-wrong-bin` (the warning only when the command itself succeeded; the four wrong-bin codes become the one code `wrong_bin`, everywhere). Open copy for Story Weaver: `decision:open-2026-11-18-plan32-sweep-failed-copy`, `decision:open-2026-11-18-plan32-sweep-count-copy`, `decision:open-2026-11-18-plan32-wrong-bin-fix-copy`.

**Plan corrections and design calls (for the reviewer).**
- "exits 3 `wrong_bin`": there was no such code; Itay's answer made it one, replacing `expired`, `not_expired`, `deleted`, `not_deleted` (a breaking change of the JSON error contract; messages, fixes and plain lines are unchanged). The plain line of the store statuses stays the legacy token (`remember: not_deleted`), with the per-case fix only in JSON (`diag_error_json_fix`).
- `sweep` JSON data and `purge`/`delete --expired` plain output are my reading of Itay's "plain prints a count": `sweep.result` data `{expired}` or `{expired, brains[]}`; `delete --expired` plain prints the count; `purge LOCATOR` prints nothing (like delete). Not asked beyond that; flagged for review.
- `ex_clear` is a TEMP trigger, not a SQL line at each writer: six sites write `expires_at` or `deleted_at` (update, revive, soft delete, undelete, import merge, a later sync apply) and the trigger is one definition. `PRAGMA temp_store = MEMORY` had to move into the same open-time statement: SQLite deletes every temp object when it changes, and plan 31's arm list did it, which dropped the trigger in the first version (found by `sweep_emits_once_and_revival_clears`).
- Found and fixed in plan 31's code: `ev_flush_one` read the "had an image before" probe without checking the step, so a failed step made a delete look like a create (`delete_expired_moves_all_its_rows_with_their_events_or_none` saw 2 deleted rows and 1 `memory.deleted`).
- Existing tests changed to the new contract: whole-bin purge and hard-wipe `delete --expired` tests (`test_sync_delete.c`, `test_ttl.c`, `test_envelope.c`, `test_verification_edges.c`, `test_link.c`, `test_events.c`), the code names in `test_errors.c`, and `export_out_is_never_the_brain` (the fixture holds an expired row, which the sweep after any command now marks; the test sweeps first).
- `sweep --all` skips a newer brain with no warning (the post-command sweep is also silent there). Not in the plan; follows `specs/OLDER-CLI.md` 8.6.
- `store_purge` and `store_purge_trash` have no caller outside tests now. Left in place, listed as follow-up.
- Dispatch after the sweep (`remember sweep` then dispatch, log 010 H-16.4) and `plugin_run_forbidden` for `sweep`, `purge` and `delete --expired` in a run are plan 35's.

**Evidence.**
- DoD tests by name are in `tests/test_sweep.c`: `sweep_emits_once_and_revival_clears`, `sweep_no_plugins_no_row`, `read_only_open_skips_sweep`, `sweep_all_reads_brains_json_and_warns_missing`, `purge_live_exits_3_wrong_bin`, `purge_deleted_writes_one_tombstone`, `delete_deleted_unknown`, `delete_expired_moves_all_due_emits_deleted_only` (and `delete_expired_has_no_cap`, 1200 rows). Review point (sweep never touches `change_seq` or the vector): `the_sweep_changes_neither_updated_at_nor_the_vector_nor_either_change_seq`. Atomicity: `sweep_commits_its_marks_with_its_events`, `delete_expired_moves_all_its_rows_with_their_events_or_none`.
- C-41 sentence 6 against this build: `purge_deleted_writes_one_tombstone` runs `remember purge 1` on a Trash row and exits 0; `tests/test_release.sh build/remember` passes.
- Gate suites (`scripts/read-gate-suites.sh`): 1842 tests, 0 failed; `remember_store_tests` (ASan): 0 failed; plugins suite 421, 0 failed.

**Mutants** (`.claude/gates/p32/mutants.py`, results `mut-results.txt`; one clause each, built before it ran): the sweep's due test without `expired_at IS NULL`, and without `deleted_at IS NULL`; `occurred_at` as the sweep time; each of the three clauses of the `ex_clear` trigger (a row with no expiry that gets one, the restore, and `>` turned to `<`); a sweep that always takes the write lock; the failed-command warning always on; no sweep after a failed command; purge of a live row allowed; `delete --expired` that ignores an id locator, and one that ignores `--key`; `sweep --all` with no missing-path check; `delete --expired` that also takes Deleted rows (a first run let this one survive: `delete_expired_moves_all_due_emits_deleted_only` now puts a due time on a Trash row); a sweep that does not check `store_is_writable`. All 14 killed by a named test. Two clauses are equivalent and have no mutant: the `k_ev_reset_sql` after the marking (only skips work: a diff of an unchanged row plans no event) and a sweep after `sweep` itself (finds nothing).

**Not verified here.** The macOS lint-all, coverage, leaks and CodeQL steps on this tree (the stacked-plan shortcut: the reviewer's gate after the rebase onto main covers them). clang-tidy on the touched files is clean on macOS llvm@22. A real dispatcher reading `memory.expired` rows (plan 35).

## Review Notes

**2026-11-20, second opinion (Claude Opus 5.5).** On `c9ead33`, then the coder's lint tip `4384b49` (stacked on plan 31's `8aee4b1`), fixed on `claude/fervent-mirzakhani-6bb83f`. Loaded `workflow:deep-review`, the PLAN scope of `workflow:whole-plan-deep-review`, `standard:coding`, `standard:c`, every decision in the request and `decision:2026-11-18-sweep-under-brain`. Local Bugbot is not installed.

Findings, fixed here:
- **Critical (A), purge wipes another memory.** `cmd_purge` read the row, then hard-deleted it by local id in a second transaction. `entries.local_id` is `INTEGER PRIMARY KEY` without AUTOINCREMENT, so SQLite hands the highest id out again once it is free: another process purges the row, a new Trash row takes its id, and `purge --key k:old` exited 0 after wiping `k:victim` for good. Exploit first: `purge_never_wipes_another_memory_that_took_the_local_id_of_the_one_it_read` (red on `c9ead33`: exit 0, victim gone), through the test CLI's `REMEMBER_TEST_PURGE_RACE=<sql>`, which runs SQL on a second connection between the read and the write. Fix: `store_purge_by_sync_id` reads the row by its sync id inside the write transaction and judges its bin there (`hard_delete_loaded` is the shared tail). Two more tests went red first: a row restored since the read printed the store token `remember: not_deleted`, not the C-01 purge line; a row moved from the Trash to Expired was refused with `Add --expired to use it.`, a fix `purge` itself refuses.
- **Critical (decision), top-level `sweep`.** `decision:2026-11-18-sweep-under-brain` (filed while the plan was coded) makes it `remember brain sweep [--all]`, reply `brain.sweep.result`, `cause.command` still `sweep`. `cli_parse` maps `brain sweep` to `CLI_CMD_SWEEP`; `cli_command_name` names it `brain` (the usage fix said `remember --help`, red first in `the_sweep_command_takes_no_argument_and_no_flag_but_all`); `event_command_name` keeps `sweep` (`the_sweep_command_marks_and_prints_how_many_and_a_second_run_prints_zero` asserts it). New: `there_is_no_top_level_sweep_command_only_brain_sweep`. The help lines moved under `help brain`, marked draft (`decision:open-2026-11-20-help-text-new-verbs`, amended).
- **Improvement (claims), stale docs.** `docs/engineering-notes.md` (delete --expired "one-row hard", "`not_expired` and `not_deleted` stay those tokens", purge via `store_hard_delete_by_id`), `specs/JSON-ENVELOPE.md` and `specs/JSON-STYLE.md` (purge `action` is `purged`; the old bin-flag purge paragraph), `specs/RELEASE-0.3.0.md` (plan 32 listed as not in 0.3.0; now a "Plan 32" line, and `tests/test_release.sh` checks it).
- **Improvement (copy).** The README and skill lines plan 32 rewrote are filed for Story Weaver: `decision:open-2026-11-20-plan32-readme-skill-lines`. `decision:open-2026-11-18-plan32-sweep-failed-copy` is amended: the build prints the path, and the draft fix is false under `brain sweep --all`.

Simplify: (1) purge by UUID read the row twice (`locator_bind_local_id`, then by local id); now one read by sync id. (2) The hard-delete tail is one function for both purge paths. (3) Follow-up, not here: `store_hard_delete_by_id`/`_by_key`, `store_purge` and `store_purge_trash` have no production caller now (about 150 lines plus their store tests). (4) `store_sweep`'s existence read allocates an id list for `LIMIT 1`; two lines with a scalar helper, left.

Probes that held: purge locators `0`, `-1`, `1.0`, ` 1`, 20 digits, `--key=k:one` (all `invalid_argument`), a key in another case, empty and 300-byte keys; `brain sweep --all` over a `brains.json` with a newline+ESC path, a FIFO, a 300-byte path (escaped in both formats, no hang) and with no `brains.json`; a chmod 444 brain (read-only open, no sweep, no warning); `delete --expired` with a locator, `--key`, or nothing due.

Mutants on the review code (all killed): the store's live check compared with Expired (5 purge tests), purge reading by local id (6), `cli_command_name` without the sweep mapping (1), `brain sweep` cause from `cli_command_name` (1).

Grades. Coder 7/10: real atomicity tests, one trigger for six writers, 14 killed mutants, a plan 31 bug found and fixed, good grills. Missed the purge race on the one permanent wipe, a decision filed mid-plan, and four stale docs. In-session review 6/10: its mutants held, but it accepted a read-then-write on a destructive path and did not re-read the decisions before the marker. Folded into Remember: `standard:coding` 7 (re-run the decision READ queries before the marker) and 2 (docs and the release spec in the grep; a rewritten user doc line is copy, filed as an open item), `standard:c` (a destructive write decides its row by a never-reused id inside its own transaction). Stale docs: covered by rule 2, sharpened.

Restack onto main after plan 31 (df83d00): main's verb table (#85) added `k_subcommands` and the `cli` suite, which reported "dispatch verb has no plan main row: brain" for `CLI_CMD_SWEEP`. `brain sweep` is now `CLI_SUB_BRAIN_SWEEP` and `CLI_CMD_SWEEP` is gone, with the `cli_parse` mapping and the `cli_command_name` special case above; specs/API-SURFACE.md has `brain sweep` on main and `purge` as `LOC, HOOKS`. Red first: the row on main gave "plan main row names a subcommand the CLI lacks". `cli_verbs_planned_subcommand_does_not_cover_its_noun` used `brain sweep` as its planned row; it now plans that row itself.
The first gate after the restack failed the Linux lint's sqlite boundary: the review's `REMEMBER_TEST_PURGE_RACE` hook opened SQLite from `cmd_locator.c`. It now runs at the top of `store_purge_by_sync_id` in `store_sqlite.c`, the one file that may. Running `scripts/lint-all.sh` before the first gate would have caught it in seconds.
The second gate failed mac coverage: twelve plan 32 lines no test ran (the coder's gate never reached that step). Tests now drive them: failing stdout for `delete --expired` and `brain sweep --all`, failing stderr for the purge `wrong_bin` line and the missing-brain warning, `REMEMBER_TEST_FAIL_WRITE` for `delete --expired`, and a listed brain that is not a database for `sweep_failed`. Three tails changed shape, not behaviour: the out-of-memory paths in `exit_wrong_list` and purge's JSON write return early, and `arm_events` reports only when asked without an `if` around its return. `check-coverage.sh` lists `brain_list_free` with the other error-tail cleanups (`brain sweep --all`'s unreadable list and calloc failure).
