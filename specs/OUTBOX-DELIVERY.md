# Outbox delivery

Status: the rule that decides the next state of one outbox recipient. The code is `src/outbox.c`, with the header `src/outbox.h`.

Decisions are `decision:…` entries tagged `subject:plugins-and-hooks` or `subject:sync`. This module is a pure function. It has no SQL, no clock, no file access and no process spawn. The caller reads the outbox row, starts the run and writes the row. The caller passes in everything the function needs and persists what it returns.

## Recipient states

`decision:2026-11-12-hook-delivery-retry`, `decision:2026-11-11-hooks-at-least-once-contract`, `decision:2026-11-08-sync-record-order-hooks-delivery`.

A recipient is in one of four states: `pending`, `done`, `failed`, `halted`.

- `done` is final. A `done` recipient stays `done` whatever else the call reports, and it is never run again.
- `failed` and `halted` get no automatic run. Only `plugin rerun` may run them, while the row is younger than 7 days.
- `pending` is waiting for a run. A `pending` recipient may be waiting for its retry time.

Delivery is at least once. A recipient is acknowledged only after the handler succeeds. Handlers must be idempotent.

## Input

`outbox_step` takes one `OutboxInput` and fills one `OutboxStep`.

| Field | Meaning |
|---|---|
| `state` | The state stored on the recipient. |
| `chain_len` | The `cause.chain` length of the event. At least 0. |
| `result` | What the caller reports for this call (below). |
| `exit_code` | The once-process exit code. Read only for `OUTBOX_RESULT_ONCE_EXIT`. A process that a signal ended passes a code other than 0. `WEXITSTATUS` of a signaled status reads 0, so the caller checks `WIFEXITED` first. |
| `event_done` | This `event_id` is already `done`. |
| `age_days` | Whole days since the row was created. At least 0. |
| `waited_s` | The delay in seconds that was waited before the attempt reported now. 0 on a first attempt. At least 0. For a call that reports nothing, the `backoff_s` stored on the row. |

`result` is one of:

| Value | Meaning |
|---|---|
| `OUTBOX_RESULT_NONE` | Nothing is reported. The recipient has not run yet, or the run crashed before a result was recorded. |
| `OUTBOX_RESULT_ONCE_EXIT` | A once-process ended. Only `exit_code` counts. |
| `OUTBOX_RESULT_PERSISTENT_ACK` | A persistent run sent a success `event.result`: its `data.event_id` is this event. |
| `OUTBOX_RESULT_PERSISTENT_FAILED` | A persistent run sent no success `event.result`: no reply before the timeout, or a failed reply (`error`, PLUGIN-PROTOCOL R3.18, PP-08), whatever its `retryable` says. |
| `OUTBOX_RESULT_PREFLIGHT` | The pre-flight check failed: the command is missing, not executable, or untrusted. The run did not start. |

`decision:protocol-oq-12-15`: a persistent action acknowledges with `event.result`, and a once-process counts its exit code only.

## Output

| Field | Meaning |
|---|---|
| `state` | The next state. |
| `run_now` | The next dispatch may run this recipient now. |
| `backoff_s` | The delay to store on the row. 0 on a fresh `pending` recipient and in every final state. |
| `retry_at` | What `next_retry_at` becomes: `KEEP` (leave it), `CLEAR` (unset it), or `AFTER_BACKOFF` (now plus `backoff_s`). |
| `delete_row` | The row is deleted. |

The caller computes `now + backoff_s` when `retry_at` is `AFTER_BACKOFF`. Then `backoff_s` is at most 3600, so the sum cannot overflow for any real clock.

The function returns false and leaves the output untouched when the pointer is NULL, `state` or `result` is not a defined value, or `chain_len`, `age_days` or `waited_s` is negative. A stored row that fails this check is a corrupt row. The caller reports it. It does not run the recipient.

## Results

`decision:2026-11-12-hook-delivery-retry`, `decision:2026-11-12-e1-preflight-hook-failures-fail-at-once`, `decision:2026-11-11-hooks-at-least-once-contract`.

Rules apply in this order.

1. A recipient that is `done`, or whose `event_id` is already `done`, gives `done`. It is not run and its row is not deleted.
2. A call that reports nothing and a `pending` recipient:
   - At `chain_len` 100 or more, the answer is `halted` and `run_now` is false (`decision:g2-06`, the 100-hop kill switch).
   - Otherwise the answer is `pending` with `run_now` true, `backoff_s` equal to `waited_s`, and `retry_at` `KEEP`. This is the crash case. A crash consumes no backoff step. The next dispatch runs the same `event_id` as soon as it can. The caller holds a `pending` recipient until its stored `next_retry_at` has passed before it makes this call.
3. A call that reports nothing and a `failed` or `halted` recipient gives the same state, with `run_now` false.
4. Exit 0 of a once-process, and a matching `event.result`, give `done`.
5. A pre-flight failure gives `failed`. It is not a backoff step. `backoff_s` is 0 and there is no automatic run. It is the same on a first attempt and after a backoff step.
6. A once-process exit other than 0, and a persistent run with no success `event.result`, are a failed attempt. Exit 75 is a failed attempt like any other non-zero exit.
   - When `waited_s` is below 3600, the answer is `pending` with `run_now` false, `retry_at` `AFTER_BACKOFF`, and `backoff_s` of 1 when `waited_s` is 0, else twice `waited_s`, at most 3600.
   - When `waited_s` is 3600 or more, the answer is `failed`.

The delays after successive failed attempts are 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, then 3600 seconds. The next failed attempt, whose waited delay was 3600, gives `failed`.

The chain check of rule 2 applies only to a call that reports nothing, because only then the run has not started. A call that reports a result is judged by the result, whatever `chain_len` says. This includes a pre-flight failure.

## Drop after 7 days

`decision:2026-11-12-hook-delivery-retry`.

When the next state is `failed` or `halted` and `age_days` is 7 or more, `delete_row` is true. A row of age 6 stays. A `pending` or `done` row is never deleted by age.

## Lanes

`decision:g-27`.

One entry's events run in order, one run at a time. At most 4 entries run at once. This module does not start runs. `outbox_lane_count()` returns 4. `outbox_lane_allows(lane)` takes a 0-based lane index and is true for 0 to 3 and false for 4 and above.

## Not in this module

The user command's exit code does not change because of a result this function returns (`decision:hooks-never-wait`). This module has no exit-code output. It does not spawn a process, read the trust file, evaluate a search filter, or write SQL. Those belong to the dispatcher, not to this module.
