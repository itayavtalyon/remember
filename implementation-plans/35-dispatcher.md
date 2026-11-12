# 35 — Dispatcher, pre-flight, kill switch and rerun

**Area:** `remember dispatch`, the post-commit pre-flight and detached start, once-runs, the chain allow list, `plugin rerun` (`src/dispatch.c` new, `src/dispatch.h` new, `src/preflight.c` new, `src/run_env.c` new).
**Needs:** plans 27a, 29, 31, 32 and 33. No other plan.
**Blocked on:** nothing.

## Goal

After a write commits, Remember checks each matching once-action and starts `remember dispatch` detached (CLI) or calls the host dispatch callback (`remember_call`). The dispatcher drains the outbox with plan 27a's `outbox_step`, runs once-actions with the run env, enforces the allow list and the 100-hop kill switch, and `plugin rerun` runs one failed or halted recipient in the foreground.

## Source

- Log 010 H-4.4 to H-4.9, H-5 (chain), H-6 (pre-flight), H-8 (once-run env); `decision:g-22`, `decision:g-27`, `decision:g2-06`, `decision:2026-11-12-hook-delivery-retry` (X3 A), `decision:s-15` (superseded; the 7-day `plugin rerun` window is restated in `decision:2026-11-12-hook-delivery-retry`), `decision:hooks-never-wait`, `decision:2026-11-11-d17-sync-commands` (G6 A).
- Old plan 25 "Delivery" paragraphs 3 to 6, "Chain and allow list", "Pre-flight".
- Copy: the locked `plugin_loop_halted` line, the C-01 `plugin_run_forbidden` row, C-20, C-21, C-22, C-30, C-31 in `COPY-PENDING.md`.
- Spec: `specs/DISPATCH.md` (new).

## Out of scope

Persistent processes and sockets (plan 36). Search filters (plan 37). `plugin test` (plan 38). The daemon (plans 39 to 42).

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md). Tests in `tests/test_dispatch.c` (new, suite `plugins`). Spawn goes through one injectable function `dispatch_spawn` so tests use a fake; the real fork/exec runs only in the end-to-end tests that use `tests/fixtures/plugins/` scripts.

## What must be true

Post-commit. After the user command commits: for each matching once-action, pre-flight checks that each trusted file exists and matches its hash and that `run.command[0]` is executable. A missing file or a non-executable `command[0]` sets that recipient `failed` at once, with no retry and no backoff step (E1 A, `decision:2026-11-12-e1-preflight-hook-failures-fail-at-once`; the pre-flight flag of the plan 27a `outbox_step`), logs it, and warns `plugin_preflight_failed` (C-20). A hash mismatch is also a pre-flight failure: it sets `failed` at once with no retry, warns `plugin_untrusted` (C-17) and follows plan 33. A run that started and failed is retried with backoff (X3 A). The `message` and `fix` of these warnings are copy item E3B-1, entries 11 and 13, in `decision:2026-11-12-copy-batch-e3b-warning-splits` (E3 B, `decision:2026-11-12-e3-warnings-have-fix-field`). The write exits 0, or 4 with `--strict`. Then the CLI starts `remember dispatch` detached: double fork, `setsid`, stdio to `/dev/null`, return. `remember_call` runs the same checks and then calls `env->dispatch(ctx, brain_path)` once; the engine never forks inside a host. NULL dispatch leaves the rows for the next CLI command or `remember sweep`. When the detached start fails, warn `plugin_dispatch_failed` (C-21), exit 0 or 4 with `--strict`. After `sweep` (plan 32) the same dispatch start runs.

Dispatcher. `remember dispatch` takes the advisory lock `<brain>.d/outbox.lock` and drains in `rowid` order. One entry runs its events strictly in order, one at a time. Different entries run in parallel, at most 4 at once per brain (`outbox_lane_allows`). Local events drain before sync and import events. Each result goes through `outbox_step` (plan 27a). A `pending` recipient with `next_retry_at` in the future is not started until that instant. After the runnable recipients are drained, if any recipient is still `pending` with a future `next_retry_at`, the detached dispatcher process waits until the earliest `next_retry_at` (or is woken by a new write) and continues; it exits only when no recipient remains that is waiting to retry. A row is deleted when every recipient is `done`; a `failed` or `halted` recipient stays until the row is 7 days old, then the row is deleted with log result `dropped_stale`. Automatic retry follows the plan 27a backoff (1 second, doubling, cap 1 hour, then `failed`). `plugin rerun` remains the foreground retry for a `failed` or `halted` recipient younger than 7 days.

Once-run. The event goes on stdin (the plan 31 payload plus `plugin`, `entry`, `data_dir`, `settings` for that recipient). stdout is discarded. stderr goes to `<brain>.d/out/<run_id>.log`, truncated at 1 MiB, the run continues. At dispatcher start, `out/*.log` files older than 7 days are deleted. `run.timeout` starts when the process starts; at the timeout SIGTERM goes to the process group, then SIGKILL 5 seconds later. Env: `REMEMBER_HOOK_NAME` (`plugin/<name>/<entry>`), `REMEMBER_HOOK_RUN_ID`, `REMEMBER_HOOK_DEPTH`, `REMEMBER_HOOK_CHAIN`, `REMEMBER_CAUSE_HOOK`, `REMEMBER_CAUSE_RUN_ID`, `REMEMBER_CAUSE_EVENT_ID`, `REMEMBER_EVENT`, `REMEMBER_EVENT_ID`, `REMEMBER_ID`, `REMEMBER_KEY`, `REMEMBER_DB`. argv and env carry ids, names and numbers only, never body or tag text.

Chain. A write inside a run is a normal write and emits events to every match, including the writer. Every plugin write stores `{kind: tool, id: plugin/<name>/<entry>}`; a hook id is `plugin/<name>/run`. `--as` and `--actor` are rejected when depth is 1 or more. Allow list when depth ≥ 1: `add`, `update`, `delete` of one locator, `link`, `unlink`, `rekey`, restore, and reads; one table with an `allowed_in_run` flag per command; a new command defaults to not allowed. Settings writes fail with `setting_from_hook`. Every other banned command (including `plugin` subcommands, `hook add`, `delete --expired`, `purge`, `import`, `sweep`) fails with exit 1 `plugin_run_forbidden` and the C-01 row. When `cause.chain` reaches 100 the recipient is `halted`, the plugin stays enabled, and later commands in that brain print the locked line until a rerun, `plugin status --ack NAME`, or the row is dropped: `remember: warning: "<name>" was stopped after 100 hook runs in a row set each other off. The write was saved. To run it once more, run 'remember plugin rerun <name> <event-id>' within 7 days. To clear this warning without rerunning, run 'remember plugin status --ack <name>'.`

Rerun. `remember plugin rerun NAME EVENT_ID` runs one `failed` or `halted` recipient once, with the chain reset, in the foreground, and waits. A timeout prints C-30 `plugin_action_timeout`, a failure C-31 `plugin_action_failed`. Nothing to rerun: exit 1 `plugin_not_rerunnable`, C-22. Per G6 A, no other command prints C-30 or C-31 (this plan's background runs log only).

## Review points

- No write's exit code changes because of a run result.
- `remember_call` never forks (a test counts `fork` through the injectable spawn).
- The allow list is one table.

## Definition of Done

- [ ] `kill_dispatcher_redelivers_same_event_id` (010 test 4).
- [ ] `two_dispatchers_one_lock` (010 test 5).
- [ ] `hook_sees_cause_hook` (010 test 7).
- [ ] `chain_100_halts_locked_line_rerun_resets_c22` (010 test 8).
- [ ] `preflight_missing_c20_mismatch_c17_strict_4` (010 test 9).
- [ ] `preflight_failure_not_retried`: a missing file, a non-executable `command[0]` and a hash mismatch each leave the recipient `failed` at once. The dispatcher starts no second attempt and sets no `next_retry_at` (E1 A).
- [ ] `warnings_carry_message_and_fix`: `plugin_preflight_failed`, `plugin_untrusted`, `plugin_dispatch_failed`, `plugin_loop_halted`, `plugin_action_timeout` and `plugin_action_failed` have a non-empty `fix`, and the plain line equals `remember: warning: <message> <fix>` per E3B-1.
- [ ] `argv_env_no_body_no_tags` (010 test 11).
- [ ] `retry_backoff_1s_doubles_to_1h_then_failed` (replaces the no-retry half of 010 test 12): a failing once-process is run again after 1 second, delays double up to 3600 seconds, and a failure after the 3600 second delay leaves the recipient `failed` with no further automatic run.
- [ ] `dispatcher_waits_for_next_retry_at`: after a failed attempt the detached dispatcher does not exit; it starts the next attempt only once `next_retry_at` has been reached.
- [ ] `failed_row_dropped_stale_at_7_days` (the drop half of 010 test 12): a `failed` or `halted` recipient stays until the row is 7 days old, then the row is deleted with log result `dropped_stale`.
- [ ] `depth_blocks_delete_expired_purge_import_sweep` (010 test 19).
- [ ] `one_entry_in_order` (010 test 29).
- [ ] `remember_call_dispatch_once_no_fork` (010 test 31).
- [ ] `dispatch_start_failure_c21`.
- [ ] `stderr_log_capped_1mib_old_logs_deleted`.
- [ ] `timeout_sigterm_then_sigkill_5s`.
- [ ] `rerun_timeout_c30_failure_c31`.
- [ ] `specs/DISPATCH.md` written; the decisions above are `state:implemented`.
- [ ] The gate passes on the committed tree.

## Implementation Notes

## Review Notes
