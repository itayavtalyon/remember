# 45 — Sync apply: receive a page, merge, conflicts, pending links, delivery state

**Area:** the receiving side of pull sync (`src/sync_apply.c` new, `src/sync_apply.h` new, `src/store_sqlite.c` for the apply transaction and the merge) and the client request row that applies a page (`src/api_table.c`).
**Needs:** plans 29, 31, 43 and 44. No other plan.
**Blocked on:** the proposed record `decision:2026-11-12-sync-api-interface-message` (Itay approves it before work starts) and Stop and ask 2, the request name, which blocks only the request row. Every other item under "Stop and ask" blocks only the clause it names.

## Goal

A device applies a page it received from a peer in one transaction, merges each record by the rules of design log 005 as Q9 amended them, records conflicts once per identity, holds links whose target has not arrived, and keeps the delivery state of each peer. The host owns the HTTP transport and the timers.

## Source

- `decision:2026-11-11-sync-q11a`: each page applies in one transaction; the ack is sent only after the commit; pulling a page again is harmless. `decision:2026-11-11-sync-applied-retention` (Q14) and Q10-5 A: de-duplicate by sending peer and `change_seq`; the `pulled_seq` cursor, moved with each page, is the record.
- `decision:2026-11-11-log005-merge-rules` (Q9, amended 2026-11-12): identical concurrent versions are joined only when read and never written during apply; `updated_at` and the local counter change only when this device authors a change; a conflict stores its versions as an unordered set without local or incoming labels.
- Design log 005 Round 7 (frozen): the version-vector comparison, the conflict reasons, a deleted memory occupies its key and hash slot, and a clash becomes a conflict row and never a raw SQLite error. Q9 replaces its "bump local" and "updated_at = now" lines.
- `decision:2026-11-11-sync-conflict-identity` (Q18): one conflict per record, identified by the record id plus the sorted competing version vectors; the event and any hook fire once per identity. `decision:2026-11-11-sync-duplicate-level` (Q19 C): duplicates are not reported. `decision:2026-11-11-sync-key-hash-clash-tests` (Q21): the key-clash and hash-clash tests are written before the merge code.
- `decision:2026-11-11-sync-q11b`: a record whose link target has not arrived is applied now, and the link waits in `pending_links`. `decision:2026-11-11-wiki-links-3` (Q6.3): `pending_links` is promoted when the key is created. `decision:2026-11-11-wiki-links-1` (Q6.1): sync creates no text links.
- `decision:2026-11-11-sync-q11c`: a returning expired peer gets a snapshot that removes the local records missing from it. `decision:2026-11-11-sync-wipe-resync` (Q13).
- `decision:2026-11-11-sync-reject-origin` (Q17 B): an error for an unreadable change names `change_id` when it can be read. `decision:2026-11-11-sync-sq2`: the sender sees unreadable records through `apply_errors` on the next pull.
- `decision:2026-11-11-sync-give-up-error` (Q20): retry with backoff while the peer is known; after 24 hours without delivery raise a user-visible error and keep retrying. `decision:2026-11-11-sync-q11d`: on reconnect pull at once from the stored cursor and reset the retry backoff.
- `decision:2026-11-08-sync-record-order-hooks-delivery`: replicated records may arrive in any order; hooks keep their ordered, durable, at-least-once delivery.
- `decision:2026-11-12-q10-v7-dormant-peer-device-schema` (Q10-3 to Q10-12): `peers`, `pending_links`, tombstone vectors and the rebuilt `conflicts`. `decision:2026-11-12-x1-gaps-bin-keep-archived-hidden` (G2 A): `keep: local` fails with `invalid_argument` unless exactly one version came from this device.
- `decision:2026-11-12-sync-api-interface-message` (proposed): the receiver error and warning codes quoted below. Plan 31: events carry `cause.origin` `sync`.
- Spec: `specs/SYNC-APPLY.md` (new), each section naming its decision keys, and a row in `specs/README.md`.

## Out of scope

- Serving pages and the sender's cursors (plan 44). The HTTP transport, timers and triggers (`decision:sync-engine-host-http`, `decision:sync-triggers`).
- Authentication (Q48, Q50 and Q51) and the Cloud server.
- Parsing `[[key]]` in synced bodies: sync creates no text links (Q6.1).
- Settings screens and the wording of every message (Story Weaver).

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md). Tests live in `tests/test_sync_apply.c` (new, suite `sync_apply`, added to `tests/gate-suites`). The key-clash and hash-clash tests are the first tests written (Q21).

## What must be true

Page apply. A page applies in one transaction, and the peer's `pulled_seq` moves in that same transaction. The device sends the ack only after the commit. A record at or below `pulled_seq` is dropped. Applying a page twice leaves the brain unchanged. Records may arrive in any order and converge.

Merge. A missing device key counts as 0. A version vector dominates when every counter is at least as large and one is larger. When the incoming record dominates, the device applies it onto the local row with the same `id`. When the local record dominates, the device skips it. Identical concurrent versions (same body, tags, key and delete state) are joined only when read and never written during apply. Concurrent versions that differ record a conflict with reason `concurrent_vv`. Apply never bumps this device's counter and never sets `updated_at` to the current time (Q9).

Clashes. An incoming record with a different `id` whose key, or whose body hash when it has no key, is already taken records a conflict with reason `key_clash` or `hash_clash`. A deleted memory occupies its slot. No clash raises a SQLite constraint error. `--keep both` on a `key_clash` or `hash_clash` keeps the incoming `id` and makes the incoming memory keyless when its slot is taken; on a `concurrent_vv` it mints a new `id` for the incoming copy (log 005 Round 7).

Conflicts. One conflict exists per identity, which is the record id plus the sorted version vectors. Applying the same page again writes no second conflict. The event and any hook fire once per identity. The device warns `conflict_recorded` with the `conflict_id`. A duplicate incoming change is not reported (Q19).

Pending links. A record whose link target has not arrived is applied, and the link waits in `pending_links` with a `target_kind` of `id`. A waiting body reference with a `target_kind` of `key` is promoted when the key is created. Sync creates no text link (Q6.1).

Tombstones and snapshots. A purge tombstone carries a version vector, and the receiver compares vectors before it removes anything. A snapshot from a peer that expired removes the local records that the snapshot lacks (Q11c). A snapshot from a wiped peer replaces the peer's records with the current ones and then the peer continues incrementally (Q13).

Errors. An unreadable record is reported with `record_undecodable`, with its `change_id` when it can be read and its `where` otherwise (Q17 B). A record that decodes but breaks a rule is reported with `record_invalid`. The device sends the list in `apply_errors` on the next pull (SQ2).

Delivery. The device records each attempt in the peer row: `last_attempt_at` and `last_error`. After a failed attempt it warns `delivery_retrying` with the next retry time. After 24 hours without delivery it warns `delivery_stalled` and keeps retrying; the warning clears on success (Q20). On reconnect the device pulls at once from the stored cursor and resets the backoff (Q11d).

Events. Every write that apply causes carries `cause.origin` `sync` (plan 31). The default actions skip it and write one `skipped_source` line (plan 31).

## Stop and ask

The coder stops and asks Itay, through the architect, at each of these. The plan does not choose.

1. Approval of `decision:2026-11-12-sync-api-interface-message`.
2. The name, the members and the CLI spelling of the request that applies a received page, and of the one that records a delivery attempt. X1 reserves only `sync.pull` and `sync.peers`; `decision:sync-engine-host-http` names `sync.apply` and `sync.status`.
3. Whether one unreadable record fails the whole page transaction or is skipped and reported while the rest apply. Q11a says one transaction; SQ2 says the sender sees unreadable records.
4. The `updated_at` and `change_seq` that an applied row stores (a hub must forward applied rows, so a new `change_seq` is needed), and the version vector that a read-time join shows (Q9).
5. What happens when a tombstone meets a local edit that is concurrent with it.
6. Whether a snapshot may remove local records that were authored here and never reached the peer (Q11c says it removes the local records missing from the snapshot).
7. The event type and payload of a recorded conflict. Plan 31 lists no event type for a conflict.
8. The retry backoff schedule for sync delivery. The 1 second to 1 hour schedule of X3 is for hooks.
9. Itay confirms the key-clash and hash-clash test list. Q21 names no cases; the Definition of Done lists the ones derived from log 005 Round 7.
10. Whether link rows with origin `text` arrive through sync (Q6.1 says sync creates no text links until a local edit), and how links appear in a page (plan 44, Stop and ask 5).

## Review points

- The cursor and the applied rows commit together or not at all.
- No apply path writes `updated_at` from the clock.
- A repeat of a page produces no second row, conflict, event or warning.
- No clash path reaches a SQLite constraint error.
- No apply path creates a text link.

## Definition of Done

- [ ] `key_clash_different_id_same_key_records_conflict_key_clash_no_sqlite_error`.
- [ ] `key_clash_when_the_slot_is_held_by_a_deleted_memory`.
- [ ] `hash_clash_keyless_different_id_same_body_hash_records_conflict_hash_clash`.
- [ ] `keep_both_on_key_clash_keeps_incoming_id_and_makes_it_keyless`.
- [ ] `keep_both_on_concurrent_vv_mints_a_new_id`.
- [ ] `clash_conflict_is_not_written_twice_when_the_page_is_applied_again`.
- [ ] `page_applies_in_one_transaction_and_cursor_moves_with_it`.
- [ ] `page_failing_midway_rolls_back_rows_and_cursor`.
- [ ] `page_applied_twice_changes_nothing`.
- [ ] `records_at_or_below_pulled_seq_are_dropped`.
- [ ] `incoming_dominates_applies_and_local_dominates_skips`.
- [ ] `identical_concurrent_versions_are_not_written_and_record_no_conflict`.
- [ ] `concurrent_different_versions_record_concurrent_vv_once_per_identity`.
- [ ] `apply_does_not_bump_the_local_counter_or_set_updated_at_to_now`.
- [ ] `conflict_event_and_hook_fire_once_per_identity`.
- [ ] `duplicate_incoming_change_is_not_reported`.
- [ ] `link_with_missing_target_waits_and_is_promoted_when_the_key_is_created`.
- [ ] `sync_creates_no_text_link`.
- [ ] `tombstone_vector_compared_before_removal`.
- [ ] `expired_snapshot_removes_local_records_missing_from_it`.
- [ ] `unreadable_record_reports_change_id_when_readable_else_where`.
- [ ] `apply_errors_are_sent_on_the_next_pull`.
- [ ] `delivery_retrying_then_stalled_after_24_hours_and_clears_on_success`.
- [ ] `reconnect_pulls_at_once_and_resets_backoff`.
- [ ] `sync_writes_carry_origin_sync_and_default_actions_skip_them`.
- [ ] `keep_local_with_none_or_two_local_versions_fails_invalid_argument_and_lists_versions`.
- [ ] `specs/SYNC-APPLY.md` is written and the decisions above are `state:implemented`.
- [ ] The gate passes on the committed tree.

## Implementation Notes

## Review Notes
