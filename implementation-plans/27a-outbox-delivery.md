# 27a — Outbox delivery

**Module:** `src/outbox.c` and `src/outbox.h`.
**Needs:** plan 24 (merged). No other plan.
**Blocked on:** nothing.

## Goal

One function decides the next recipient state. It does not spawn a process and it does not write SQL.

## Source

- `decision:2026-11-12-hook-delivery-retry` (X3 A): failed deliveries retry with backoff starting at 1 second, doubling, capped at 1 hour, then the recipient is `failed`. This supersedes the "no automatic retry loop" text of `decision:2026-11-11-hook-delivery` and `decision:s-15`.
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

`outbox_step` takes the current state, the chain length, the run result, the once-process exit code, whether this `event_id` is already `done`, the row age in whole days, the delay in seconds that was waited before the attempt just reported (0 if this is the first attempt), and whether a run result is being reported now. It returns the next state, whether the next dispatch may run this recipient now, the next backoff delay in seconds, and whether the row is deleted. The caller persists `backoff_s` (0 on a fresh `pending` recipient) and `next_retry_at` (unset on a fresh `pending` recipient) on the outbox row. This module still writes no SQL.

States are `pending`, `done`, `failed`, and `halted`.

- An `event_id` that is already `done` is not run. The state stays `done`. The row is not deleted for that reason.
- A crash before a result is recorded leaves the state `pending`. `backoff_s` and `next_retry_at` do not change. The next dispatch runs that same `event_id` as soon as it can. The crashed process is not started again.
- A once-process exit of 0 sets `done`. A persistent `event.result` whose `event_id` matches sets `done`. `next_retry_at` is cleared.
- A once-process exit other than 0, or a persistent run that ends with no `event.result`, is a failed attempt. It does not set `failed` on the first such attempt, and it does not set `failed` while the delay waited for this attempt was less than 3600 seconds. The recipient stays `pending`. The next delay is 1 second after the first failed attempt, then twice the delay just waited, capped at 3600 seconds. The exact delay sequence is 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, then 3600. `next_retry_at` is now plus that next delay. The next dispatch must not run this recipient until `next_retry_at`. After a failed attempt whose waited delay was already 3600 seconds, the recipient is set to `failed`, `next_retry_at` is cleared, and there is no further automatic run. There is no special path for exit 75; it is a failed attempt like any other non-zero exit.
- A chain length of 100 or more, when the run has not started, sets `halted`. It is not run and it is not retried automatically.
- A `failed` or `halted` row whose age is 7 days or more is deleted. A younger row stays. `plugin rerun` (plan 35) may run a `failed` or `halted` recipient while the row is younger than 7 days.
- One entry's events stay in order, one run at a time. At most 4 entries run at once. This function does not start the runs. `outbox_lane_count` returns 4. A caller that passes a 5th lane gets false from `outbox_lane_allows`.

The user command's exit does not change because of a result this function returns. This module has no exit-code output.

## Review points

- The reviewer checks that a crash stays `pending` and an `event_id` already `done` is not run.
- The reviewer checks that exit 0 and a matching `event.result` are the only results that set `done`.
- The reviewer checks that the first failed attempt stays `pending` with a 1 second delay, that delays double and cap at 3600 seconds, and that a failure after a 3600 second delay sets `failed`.
- The reviewer checks that age 6 keeps a `failed` row and age 7 deletes it.
- The reviewer checks that the module does not fork, exec, or include `sqlite3.h`.

## Definition of Done

- [ ] `a crash before a result stays pending and asks for the same event_id again` (`tests/test_outbox.c`).
- [ ] `an event_id already done is not run`.
- [ ] `once-process exit 0 sets done`.
- [ ] `a matching event.result sets done`.
- [ ] `first_failed_attempt_stays_pending_delay_1s`: a first non-zero once-process exit or a missing `event.result` leaves the recipient `pending` with `backoff_s` 1 and `next_retry_at` equal to now plus 1 second, and the next dispatch must not run it yet.
- [ ] `backoff_doubles_then_caps_at_3600s`: successive failed attempts produce delays 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, then 3600, and never more than 3600.
- [ ] `failure_after_3600s_delay_sets_failed`: a failed attempt whose waited delay was 3600 seconds sets `failed` and asks for no further automatic run.
- [ ] `crash_does_not_consume_backoff`: a crash before a result leaves `pending` with the same `backoff_s` and an immediate retry of the same `event_id`.
- [ ] `chain length 100 sets halted and does not run`.
- [ ] `a failed row at 6 days stays and at 7 days is deleted`.
- [ ] `outbox_lane_count is 4 and a 5th lane is refused`.
- [ ] `specs/OUTBOX-DELIVERY.md` is written, and `decision:2026-11-12-hook-delivery-retry` is `state:implemented`.
- [ ] The gate passes on the committed tree, and the notes record the mutants and their red tests.

## Implementation Notes

## Review Notes
