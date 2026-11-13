# 27a — Outbox delivery

**Module:** `src/outbox.c` and `src/outbox.h`.
**Needs:** plan 24 (merged). No other plan.
**Blocked on:** nothing.

## Goal

One function decides the next recipient state. It does not spawn a process and it does not write SQL.

## Source

- `decision:2026-11-12-hook-delivery-retry` (X3 A): failed deliveries retry with backoff starting at 1 second, doubling, capped at 1 hour, then the recipient is `failed`. This supersedes the "no automatic retry loop" text of `decision:2026-11-11-hook-delivery` and `decision:s-15`.
- `decision:2026-11-12-e1-preflight-hook-failures-fail-at-once` (E1 A): a pre-flight failure (the command is missing, not executable, or untrusted) sets `failed` at once. It is not retried and it takes no backoff step. The backoff of X3 A applies only to a run that started and failed. This amends the pre-flight text of plans 27a and 35 and design log 010 H-6.2.
- `decision:2026-11-11-hooks-at-least-once-contract` (Q7 A): acknowledge only after the handler succeeds; handlers must be idempotent.
- `decision:2026-11-08-sync-record-order-hooks-delivery` (ordered, durable, at least once)
- `decision:s-15` (superseded; the 7-day `plugin rerun` window and the rule that a hook result does not change the user command's exit are restated in `decision:2026-11-12-hook-delivery-retry`)
- `decision:g-27` (one entry runs its events in order, one at a time; at most 4 entries at once)
- `decision:g2-06` (the 100-hop kill switch)
- `decision:hooks-never-wait` (the user command has already committed)
- `decision:protocol-oq-12-15` (a persistent action acks with `event.result`; a once-process counts its exit code)
- Spec: `specs/OUTBOX-DELIVERY.md` (new).

## Out of scope

Process spawn, the trust file, search filters, and inserting the outbox row. Those callers come later. This plan does not change a user command's exit code.

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md).

Tests live in `tests/test_outbox.c`, suite `outbox`, and that suite is added to `tests/gate-suites`. The module links no SQLite.

## What must be true

`outbox_step` takes the current state, the chain length, the run result, the once-process exit code, whether this `event_id` is already `done`, the row age in whole days, the delay in seconds that was waited before the attempt just reported (0 if this is the first attempt), whether a run result is being reported now, and whether that result is a pre-flight failure (a missing, non-executable or untrusted command, E1 A). It returns the next state, whether the next dispatch may run this recipient now, the next backoff delay in seconds, and whether the row is deleted. The caller persists `backoff_s` (0 on a fresh `pending` recipient) and `next_retry_at` (unset on a fresh `pending` recipient) on the outbox row. This module still writes no SQL.

States are `pending`, `done`, `failed`, and `halted`.

- An `event_id` that is already `done` is not run. The state stays `done`. The row is not deleted for that reason.
- A crash before a result is recorded leaves the state `pending`. `backoff_s` and `next_retry_at` do not change. The next dispatch runs that same `event_id` as soon as it can. The crashed process is not started again.
- A once-process exit of 0 sets `done`. A persistent `event.result` whose `event_id` matches sets `done`. `next_retry_at` is cleared.
- A once-process exit other than 0, or a persistent run that ends with no `event.result`, is a failed attempt. It does not set `failed` on the first such attempt, and it does not set `failed` while the delay waited for this attempt was less than 3600 seconds. The recipient stays `pending`. The next delay is 1 second after the first failed attempt, then twice the delay just waited, capped at 3600 seconds. The exact delay sequence is 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, then 3600. `next_retry_at` is now plus that next delay. The next dispatch must not run this recipient until `next_retry_at`. After a failed attempt whose waited delay was already 3600 seconds, the recipient is set to `failed`, `next_retry_at` is cleared, and there is no further automatic run. There is no special path for exit 75; it is a failed attempt like any other non-zero exit.
- A pre-flight failure sets `failed` at once (E1 A). `backoff_s` is 0, `next_retry_at` is cleared, no backoff step is taken, and there is no automatic run after it. A pre-flight failure on a first attempt and on a later attempt are the same. `plugin rerun` (plan 35) may run it while the row is younger than 7 days, and the 7-day drop applies as for any `failed` row.
- A chain length of 100 or more, when the run has not started, sets `halted`. It is not run and it is not retried automatically.
- A `failed` or `halted` row whose age is 7 days or more is deleted. A younger row stays. `plugin rerun` (plan 35) may run a `failed` or `halted` recipient while the row is younger than 7 days.
- One entry's events stay in order, one run at a time. At most 4 entries run at once. This function does not start the runs. `outbox_lane_count` returns 4. A caller that passes a 5th lane gets false from `outbox_lane_allows`.

The user command's exit does not change because of a result this function returns. This module has no exit-code output.

## Review points

- The reviewer checks that a crash stays `pending` and an `event_id` already `done` is not run.
- The reviewer checks that exit 0 and a matching `event.result` are the only results that set `done`.
- The reviewer checks that the first failed attempt stays `pending` with a 1 second delay, that delays double and cap at 3600 seconds, and that a failure after a 3600 second delay sets `failed`.
- The reviewer checks that a pre-flight failure is `failed` with no delay and no `next_retry_at`, and that it is never confused with a failed attempt.
- The reviewer checks that age 6 keeps a `failed` row and age 7 deletes it.
- The reviewer checks that the module does not fork, exec, or include `sqlite3.h`.

## Definition of Done

- [x] `a crash before a result stays pending and asks for the same event_id again` (`tests/test_outbox.c`: `outbox_crash_before_a_result_stays_pending_and_runs_again`).
- [x] `an event_id already done is not run` (`outbox_an_event_id_already_done_is_not_run`, `outbox_a_done_recipient_stays_done_and_is_not_run`, `outbox_done_is_never_undone_by_a_late_failure_or_halt`).
- [x] `once-process exit 0 sets done` (`outbox_once_process_exit_0_sets_done`).
- [x] `a matching event.result sets done` (`outbox_a_matching_event_result_sets_done`).
- [x] `first_failed_attempt_stays_pending_delay_1s`: a first non-zero once-process exit or a missing `event.result` leaves the recipient `pending` with `backoff_s` 1 and `next_retry_at` equal to now plus 1 second, and the next dispatch must not run it yet (`outbox_first_failed_attempt_stays_pending_delay_1s`, `outbox_a_persistent_run_with_no_event_result_is_a_failed_attempt`).
- [x] `backoff_doubles_then_caps_at_3600s`: successive failed attempts produce delays 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, then 3600, and never more than 3600 (`outbox_backoff_doubles_then_caps_at_3600s`, `outbox_a_delay_between_2048_and_3600_is_capped_not_doubled`, `outbox_a_delay_off_the_sequence_still_doubles_and_caps`, `outbox_exit_75_is_a_failed_attempt_like_any_other_non_zero_exit`).
- [x] `failure_after_3600s_delay_sets_failed`: a failed attempt whose waited delay was 3600 seconds sets `failed` and asks for no further automatic run (`outbox_failure_after_a_3600s_delay_sets_failed`, `outbox_a_delay_past_the_cap_sets_failed_without_overflow`).
- [x] `preflight_failure_sets_failed_at_once`: a result flagged as a pre-flight failure sets `failed` with `backoff_s` 0 and `next_retry_at` cleared, asks for no further automatic run, and does so from `pending` on a first attempt and after a backoff step alike (`outbox_preflight_failure_sets_failed_at_once_on_a_first_attempt`, `outbox_preflight_failure_sets_failed_at_once_after_a_backoff_step`).
- [x] `preflight_failure_is_not_a_backoff_step`: a flagged result never produces a delay from the 1, 2, 4 ... 3600 sequence, and the same non-zero exit unflagged still does (`outbox_preflight_failure_is_not_a_backoff_step`).
- [x] `crash_does_not_consume_backoff`: a crash before a result leaves `pending` with the same `backoff_s` and an immediate retry of the same `event_id` (`outbox_crash_does_not_consume_backoff`).
- [x] `chain length 100 sets halted and does not run` (`outbox_chain_length_100_sets_halted_and_does_not_run`, with `outbox_chain_length_99_still_runs` and `outbox_a_huge_chain_length_sets_halted`).
- [x] `a failed row at 6 days stays and at 7 days is deleted` (`outbox_a_failed_row_at_6_days_stays`, `outbox_a_failed_row_at_7_days_is_deleted`, `outbox_a_halted_row_at_6_days_stays_and_at_7_days_is_deleted`).
- [x] `outbox_lane_count is 4 and a 5th lane is refused` (`outbox_lane_count_is_4_and_a_5th_lane_is_refused`).
- [x] `specs/OUTBOX-DELIVERY.md` is written, and `decision:2026-11-12-hook-delivery-retry` is `state:implemented`.
- [x] The gate passes on the committed tree, and the notes record the mutants and their red tests (Implementation Notes: Mutants, Gate).

## Implementation Notes

**Shape.** `outbox_step(const OutboxInput *, OutboxStep *)` returns false (and leaves the output alone) for NULL, an undefined `state` or `result`, or a negative `chain_len`, `age_days` or `waited_s`. The plan's "run result", "once-process exit code", "whether a result is being reported" and "whether it is a pre-flight failure" are one enum, `OutboxResult` (`NONE`, `ONCE_EXIT`, `PERSISTENT_ACK`, `PERSISTENT_NONE`, `PREFLIGHT`), plus `exit_code`. That removes the impossible pairs (a flagged pre-flight with an exit code, a non-reported result with a pre-flight flag). The module also returns `retry_at` (`KEEP`, `CLEAR`, `AFTER_BACKOFF`) so the caller does not re-derive when `next_retry_at` is unset or set. The module is linked into the CLI and the test binaries (`REMEMBER_LIB_SOURCES`, also the `-Wcast-qual` list) and into `remember_store_tests` (ASan/UBSan). It includes no `sqlite3.h` and calls no `fork`, `exec` or `system` (`grep -nE '(fork|exec[a-z]*|system|popen)\(|sqlite3|#include <(unistd|spawn)' src/outbox.c src/outbox.h` finds nothing).

**Choices the plan and the decisions leave open.** Each reads the plan's text one way and is not a product rule from Itay. Each needs Itay's confirmation or a change before plans 31 and 35 call this function.

1. A chain length of 100 or more halts only a call that reports nothing (`OUTBOX_RESULT_NONE`, the plan's "when the run has not started" with "whether a run result is being reported now"). A reported pre-flight failure at chain 100 or more is `failed`, not `halted`. Halt (H-5.6) and E1 A both describe a run that did not start.
2. A reported result is judged the same whatever the stored state is (`failed`, `halted`, `pending`), except `done`, which is final. So a `plugin rerun` that fails with `waited_s` 0 gives `pending` with a 1 second delay, and automatic retries start again. The plan says nothing about the result of a rerun.
3. `backoff_s` is 0 in `done`, `failed` and `halted`. The plan fixes it only for a pre-flight failure and a fresh `pending`.
4. A `waited_s` above 3600 counts as 3600 ("already 1 hour"). A hand-edited `backoff_s` of 10 gives 20 (twice the delay just waited).
5. Only `failed` and `halted` rows are dropped at 7 days. A `pending` or `done` row never is, as the plan says.
6. What counts as a "matching" `event.result` is the caller's. A `PERSISTENT_NONE` result means "no matching result".
7. The 7-day age is judged on the next state, so a row that fails for good at age 7 or more is deleted in the same call.
8. `decision:2026-11-12-e1-preflight-hook-failures-fail-at-once` stays `state:accepted`: this plan builds only the state rule; plan 35 builds the pre-flight check itself. The same holds for `decision:2026-11-11-hooks-at-least-once-contract` (the acknowledge step is in the dispatcher).
9. `outbox_lane_allows(lane)` reads `lane` as a 0-based index: 0 to 3 run and 4 does not ("a 5th lane"). A caller that numbers lanes from 1 must pass `number - 1`.

**Lint.** The test file carries one `NOLINTBEGIN/END(magic-numbers)` region (expected values are literals by rule), two `NOLINTNEXTLINE(bugprone-easily-swappable-parameters)` on `given_attempt` and `given_row`, and four `NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)` on the casts that feed the guard under test (the same form as `tests/test_output.c`). `src/outbox.c` has no suppression. Itay: these are the per-line disables to look at.

**TDD.** One red test at a time, with a throwaway ASan/UBSan loop (`clang -std=c11 -Wall -Wextra -Wconversion -Werror -fsanitize=address,undefined -Isrc -Itests main.c tests/test.c tests/test_outbox.c src/outbox.c`). Red, then green, in this order: crash stays pending; done is final; exit 0 and matching result; failed attempt and the delay sequence; pre-flight; chain 100; age; refused input; lanes. Two tests were green when first written (`outbox_a_delay_between_2048_and_3600_is_capped_not_doubled` and `outbox_crash_does_not_consume_backoff`); they pin boundaries and were checked by mutants (below). One test failed for a test bug (it forgot `exit_code = 1`), not for production code.

**Mutants** (one change each, by a throwaway script that rewrites `src/outbox.c`, builds the unit loop with `-Werror`, and runs it; a build error is not a survivor). 64 mutants, 62 killed, 2 equivalent. The red test is the first of those that failed.

| Mutant | Red test |
|---|---|
| `step_to` clears `next_retry_at` -> keeps it | `a_failed_row_stays_failed_and_is_not_run_when_nothing_is_reported` (+6) |
| first delay 1 -> 2, or `waited == 0` test moved or dropped | `a_persistent_run_with_no_event_result_is_a_failed_attempt` (+3) |
| doubling x2 -> x3, +1, or add 1 | `a_delay_off_the_sequence_still_doubles_and_caps` (+2) |
| cap clamp removed, reversed, or 3598 | `a_delay_between_2048_and_3600_is_capped_not_doubled` |
| cap clamp `> cap+2` | `a_delay_off_the_sequence_still_doubles_and_caps` |
| fail when `waited > cap` (not `>=`) | `failure_after_a_3600s_delay_sets_failed` |
| fail when `waited >= cap-1` | `a_delay_between_2048_and_3600_is_capped_not_doubled` |
| failed attempt never fails (and the overflow of `waited * 2` under UBSan) | `a_delay_past_the_cap_sets_failed_without_overflow` |
| failed attempt returns `failed`, or `retry_at` `KEEP` | `a_persistent_run_with_no_event_result_is_a_failed_attempt` (+7) |
| not-reported branch on `state == pending` inverted, or terminal state turned `failed` | `a_failed_row_at_6_days_stays` (+10), `a_halted_row_stays_halted_and_is_not_run_when_nothing_is_reported` |
| halt at `> 100`, at `>= 99`, or to `failed` | `chain_length_100_sets_halted_and_does_not_run`, `chain_length_99_still_runs`, `a_huge_chain_length_sets_halted` |
| crash `run_now` false, `backoff_s` 0 or +1 | `chain_length_99_still_runs`, `crash_does_not_consume_backoff`, `crash_before_a_result_stays_pending_and_runs_again` |
| matching result removed | `a_matching_event_result_sets_done` |
| exit 0 test inverted, `&&` -> `||`, exit check dropped | `a_delay_between_2048_and_3600_is_capped_not_doubled` (+8 to +13) |
| pre-flight branch removed, sent to the backoff path, or to `halted` | `preflight_failure_sets_failed_at_once_on_a_first_attempt`, `a_row_that_fails_for_good_at_7_days_is_deleted` |
| `state` or `result` upper bound `>=`, or the check removed | `a_halted_row_at_6_days_stays_and_at_7_days_is_deleted`, `step_refuses_input_it_cannot_judge_and_leaves_out_untouched` |
| `chain_len`, `age_days`, `waited_s` check removed, `<= 0`, or `< -1`; every refusal returning true | `step_refuses_input_it_cannot_judge_and_leaves_out_untouched`, `step_refuses_bad_input_even_when_the_event_is_already_done` |
| NULL `in` or `out` check removed | `the_exit_code_is_ignored_for_every_other_result` (UBSan null access) |
| `state == done` or `event_done` removed, `||` -> `&&` | `done_is_never_undone_by_a_late_failure_or_halt`, `an_event_id_already_done_is_not_run` |
| drop at `> 7`, at `>= 6`, `failed` only, `halted` only, any state, `&&` -> `||`, never, judged on the input state | `a_failed_row_at_7_days_is_deleted`, `a_failed_row_at_6_days_stays`, `a_halted_row_at_6_days_stays_and_at_7_days_is_deleted`, `a_pending_or_done_row_is_never_deleted_by_age`, `a_row_that_fails_for_good_at_7_days_is_deleted` |
| lane `<=`, `< 3`, count 5 | `lane_count_is_4_and_a_5th_lane_is_refused` |

Two mutants survive and are equivalent: the clamp `next >= cap` (it sets 3600 to 3600) and the clamp `next > cap + 1` (`next` is `waited * 2`, always even, or 1, so 3601 never occurs).

**Probes past the plan.** `waited_s` of `INT64_MAX`, `3601`, `3599`, `1799`, `1800`, `1801` and `5` (off the sequence); `chain_len` of `INT64_MAX`; `age_days` of `INT64_MAX`; `-1` and `INT64_MIN` for each numeric field; `state` 4 and -1 and `result` 5 and -1 (cast, so the check holds whether the compiler's enum is signed or not); exit codes -1, 0, 256 and 75; a late failure or a halt reported for a `done` recipient; a flagged pre-flight with a chain of 100; `event_done` with a bad number. `outbox_lane_allows(SIZE_MAX)` is false.

**Simplify pass.** (1) `next_backoff` folded into `step_failed_attempt`: 6 lines fewer, and the overflow guard sits next to the multiply it protects. (2) The plan's pre-flight flag and the result kind became one enum: one input field fewer, and the impossible input pairs gone. (3) `retry_at` stays: without it each caller re-derives the unset rule, and the plan's "cleared" clauses would have no test. (4) `step_reported` and `step_not_reported` stay separate: each has a single three-way decision, and one function would nest them. (5) The tests stay one behaviour each: `crash_*` and `preflight_*` pairs look alike but one pins the backoff left alone and the other pins the run, as the plan lists them. No contract is tested in two layers: `outbox_step` has no caller yet.

**Security and exploit pass.** The inputs come from an outbox row, so a hand-edited or synced row is hostile: negative or undefined values are refused; `waited_s` and `chain_len` up to `INT64_MAX` give `failed` and `halted` with no multiply or add that can overflow (the only multiply runs after the `>= 3600` check); `age_days` up to `INT64_MAX` compares, never adds. The module never reads a clock and never adds to a time. The caller adds `backoff_s` (at most 3600 when `retry_at` is `AFTER_BACKOFF`) to its own `now`. Residual risk, not in this module: a hand-edited `next_retry_at` far in the future holds a `pending` recipient for as long, because only `failed` and `halted` rows are dropped by age (the plan's rule); a `pending` row's `backoff_s` is passed back unchanged on a crash, so an edited huge value survives a crash but never reaches a time sum (`retry_at` is `KEEP`).

**Gate** (run on commit `28c1438`, a clean tree; the next commit changes only this plan file). Each command ran inside the shared heavy-job lock.

| Check | Command | Result |
|---|---|---|
| ASan/UBSan build and ctest | `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DREMEMBER_ENABLE_SANITIZERS=ON -DREMEMBER_ENABLE_LSAN=OFF -DREMEMBER_TEST_VFS=ON && cmake --build build && ctest --test-dir build --output-on-failure` | 6 of 6 passed, no build warning in the new files (the allocator-wrapper warnings in `store_sqlite.c` and `import_read.c` are older) |
| Lint | `scripts/lint-all.sh` | format, boundaries, cppcheck, gcc -fanalyzer, clang-tidy (LLVM 22), scan-build pass; IWYU skipped on macOS |
| Coverage | `scripts/check-coverage.sh` | functions 100%, effective lines 100%, pure-TU branches 100%. `outbox.c` alone: 8 of 8 functions, 66 of 66 lines, 46 of 46 branch sides (read from `build-cov/coverage-report.txt` and `coverage-branches.json`). `outbox.c` is not in the branch-gated list; I did not widen that list. |
| Linux CI | `scripts/ci-linux.sh` | OK (clang-tidy 18, IWYU, scan-build) |
| macOS leaks | `cmake --build build --target leaks-macos` | PASS |
| CodeQL | `scripts/codeql-local.sh` | OK, 43 allowed alerts, none new |

Two earlier lint runs failed and were fixed: the test file's magic numbers, easily-swapped parameters, int-to-bool and enum-cast findings, the missing `<stddef.h>`/`<stdint.h>` includes in `src/outbox.c`, and an unnamed `pad_[6]`. The first ctest run used a build without `-DREMEMBER_TEST_VFS=ON`; 20 older tests failed there for that reason and pass with the CI flags.

## Review Notes
