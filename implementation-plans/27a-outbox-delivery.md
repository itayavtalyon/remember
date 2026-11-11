# 27a — Outbox delivery

**Module:** `src/outbox.c` and `src/outbox.h`.
**Requires:** nothing above plan 24. Plans 25, 26, and 27 do not require this plan. Plan 28 requires this plan.

## Goal

One function decides the next recipient state. It does not spawn a process and it does not write SQL.

## Source

- `decision:2026-11-11-hook-delivery`
- `decision:2026-11-08-sync-record-order-hooks-delivery` (ordered, durable, at least once)
- `decision:s-15` (no exit-75 retry; `plugin rerun` while the row is younger than 7 days)
- `decision:g-27` (one entry runs its events in order, one at a time; at most 4 entries at once)
- `decision:g2-06` (the 100-hop kill switch)
- `decision:hooks-never-wait` (the user command has already committed)
- `decision:protocol-oq-12-15` (a persistent action acks with `event.result`; a once-process counts its exit code)
- Spec: `specs/outbox-delivery.md` (new).

## Out of scope

Process spawn, the trust file, search filters, and inserting the outbox row. Those callers come later. This plan does not change a user command's exit code.

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md).

Tests live in `tests/test_outbox.c`, suite `outbox`, and that suite is added to `tests/gate-suites`. The module links no SQLite.

## What must be true

`outbox_step` takes the current state, the chain length, the run result, the once-process exit code, whether this `event_id` is already `done`, and the row age in whole days. It returns the next state, whether the next dispatch runs it, and whether the row is deleted.

States are `pending`, `done`, `failed`, and `halted`.

- An `event_id` that is already `done` is not run. The state stays `done`. The row is not deleted for that reason.
- A crash before a result is recorded leaves the state `pending`. The next dispatch runs that same `event_id`. The crashed process is not started again.
- A once-process exit of 0 sets `done`. Any other exit sets `failed`.
- A persistent `event.result` whose `event_id` matches sets `done`. A persistent run that ends with no `event.result` sets `failed`.
- A chain length of 100 or more, when the run has not started, sets `halted`. It is not run.
- A `failed` or `halted` row whose age is 7 days or more is deleted. A younger row stays.
- There is no automatic retry loop. `plugin rerun` is a later plan.
- One entry's events stay in order, one run at a time. At most 4 entries run at once. This function does not start the runs. `outbox_lane_count` returns 4. A caller that passes a 5th lane gets false from `outbox_lane_allows`.

The user command's exit does not change because of a result this function returns. This module has no exit-code output.

## Review points

- The reviewer checks that a crash stays `pending` and an `event_id` already `done` is not run.
- The reviewer checks that exit 0 and a matching `event.result` are the only results that set `done`.
- The reviewer checks that age 6 keeps a `failed` row and age 7 deletes it.
- The reviewer checks that the module does not fork, exec, or include `sqlite3.h`.

## Definition of Done

- [ ] `a crash before a result stays pending and asks for the same event_id again` (`tests/test_outbox.c`).
- [ ] `an event_id already done is not run`.
- [ ] `once-process exit 0 sets done and any other exit sets failed`.
- [ ] `a matching event.result sets done and a missing event.result sets failed`.
- [ ] `chain length 100 sets halted and does not run`.
- [ ] `a failed row at 6 days stays and at 7 days is deleted`.
- [ ] `outbox_lane_count is 4 and a 5th lane is refused`.
- [ ] `specs/outbox-delivery.md` is written, and `decision:2026-11-11-hook-delivery` is `state:implemented`.
- [ ] The gate passes on the committed tree, and the notes record the mutants and their red tests.

## Implementation Notes

## Review Notes
