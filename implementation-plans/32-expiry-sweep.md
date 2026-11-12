# 32 — Expiry sweep, purge and delete --expired

**Area:** the post-command sweep, `remember sweep [--all]`, `remember purge LOCATOR`, `remember delete --expired` (`src/sweep.c` new, `src/cmd_purge.c`, `src/store_sqlite.c`).
**Needs:** plans 26 (`brains.json` for `--all`) and 31 (events). No other plan.
**Blocked on:** nothing.

## Goal

Due memories get `expired_at` and one `memory.expired` event, after every read-write command and from `remember sweep`. Purge and `delete --expired` follow the old plan 25 rules.

## Source

- Log 010 H-1 (sweep), H-2.1, the event list; log 018 V1-17 and log 020 D-3 (no sweep LaunchAgent).
- Old plan 25 "Events" third paragraph and "Logs and CLI" (`sweep`, `purge`, `delete --expired`).
- Spec: `specs/EVENTS.md` gains "Expiry".

## Out of scope

Dispatch after the sweep (plan 35 adds the call). The daemon's sweep (plan 39).

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md). Tests in `tests/test_sweep.c` (new, suite `plugins`).

## What must be true

- `expired_at` is device-local. Setting it does not bump `updated_at`, the version vector, or `change_seq`, and it does not sync.
- The sweep runs after every command that opened the brain read-write, including reads, after that command commits. A read-only open skips it. One transaction. It sets `expired_at = now` on each row with `deleted_at IS NULL AND expires_at <= now AND expired_at IS NULL`, and inserts one `memory.expired` outbox row per changed row (plan 31 rules). `occurred_at` is `expires_at`. `detected_at` is the sweep time. `cause.command` is `sweep`.
- A later or null `expires_at`, or a restore, sets `expired_at` to null in that same write. Restore of a row that is already due emits `memory.restored`, then the sweep emits `memory.expired`.
- `remember sweep` sweeps one brain. `remember sweep --all` reads only `brains.json` and sweeps each listed brain; a missing path warns `brain_missing` with the C-01 line and the entry stays.
- `remember purge LOCATOR` permanently deletes one row that is already expired or deleted, writes one tombstone, and emits `memory.purged`. A live row exits 3 `wrong_bin` with the C-01 `wrong_bin` row. Purge of an unmarked expired row emits only `memory.purged`.
- `delete --expired` moves every due, not-yet-deleted row to Deleted in one transaction, emits `memory.deleted` only, and has no cap. `delete --deleted` is an unknown option.
- When no plugin is installed the sweep inserts no outbox row.

## Review points

- The sweep never touches `change_seq` or the version vector (asserted by reading both before and after).

## Definition of Done

- [ ] `sweep_emits_once_and_revival_clears` (010 test 14).
- [ ] `sweep_no_plugins_no_row` (010 test 16).
- [ ] `read_only_open_skips_sweep`.
- [ ] `sweep_all_reads_brains_json_and_warns_missing`.
- [ ] `purge_live_exits_3_wrong_bin`, `purge_deleted_writes_one_tombstone`, `delete_deleted_unknown` (010 test 28).
- [ ] `delete_expired_moves_all_due_emits_deleted_only`.
- [ ] `specs/EVENTS.md` "Expiry" written.
- [ ] The gate passes on the committed tree.

## Implementation Notes

## Review Notes
