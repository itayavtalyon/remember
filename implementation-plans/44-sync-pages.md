# 44 — Sync pages: serve a page, cursors, snapshots, peers

**Area:** the sender side of pull sync (`src/sync_pull.c` new, `src/sync_pull.h` new), the `sync.pull` and `sync.peers` request rows (`src/api_table.c`) and the CLI `remember sync peers` (`src/cmd_sync.c` new).
**Needs:** plans 29 and 43. No other plan.
**Blocked on:** the proposed record `decision:2026-11-12-sync-api-interface-message` (Interface's message; Itay approved only SQ1 to SQ3 of it), which Itay approves before work starts. Each item under "Stop and ask" blocks only the clause it names.

## Goal

A device that holds a brain answers a peer's pull with a gzipped JSON page of whole current records, moves that peer's stored cursor only as the rules below say, and lists its peers with their state. The host owns the HTTP transport, the timers and the credentials.

## Source

- `decision:2026-11-11-sync-pull-model`: per known peer the sender stores the last acked `change_seq` and the last successful sync time; a pull returns records above the stored cursor; the ack and the request for the next batch are one call; a batch is a paged list of whole current records.
- `decision:2026-11-09-sync-capture-change-seq-per-peer-ack`: sync reuses `change_seq`, with a per-peer acknowledged cursor; no separate change log.
- `decision:2026-11-09-sync-encoding-json-full-record-gzip` and `decision:2026-11-11-sync-batch-frame` (Q16): JSON, the full record on every change, gzip level 6 over the whole batch, a next-page token and a `change_seq`.
- `decision:2026-11-11-sync-real-body-measurement` (Q22 B): JSON is final.
- `decision:2026-11-11-sync-sq3`: page size 200 by default, 1000 at most, 8 MB before gzip, at least one record per page. `decision:2026-11-11-sync-sq1`: Settings shows the older of the two directions' last-sync times. `decision:2026-11-11-sync-sq2`: the sender also sees unreadable records, through an optional `apply_errors` list on the next pull.
- `decision:2026-11-11-sync-lagging-peer-retention` (Q12): changes are kept until every known peer acknowledges; a peer silent for 30 days stops holding history and does a full resync. `decision:2026-11-11-sync-wipe-resync` (Q13): a wiped or reset peer gets a snapshot, then continues incrementally. `decision:2026-11-11-sync-q11c`: tombstones are kept until every active peer acks them.
- `decision:2026-11-11-sync-space-reclaim` (Q15): incremental auto-vacuum, and an incremental vacuum step after a drain.
- `decision:2026-11-12-q10-v7-dormant-peer-device-schema` (Q10-3, Q10-4, Q10-7, Q10-9): the `peers` columns, state and expiry worked out on read, `change_id` derived as `{brain_id}:{change_seq}`, tombstones that carry a version vector.
- `decision:sync-peer-model` (hub and spoke), `decision:sync-engine-host-http` (the engine owns the sync calls; a host adapter does HTTP), `decision:sync-triggers`.
- `decision:2026-11-12-x1-remember-call-requests`: `sync.pull` and `sync.peers` are reserved rows; this plan builds them.
- `decision:2026-11-12-sync-api-interface-message` (proposed): the request, the response, the errors and the cases quoted below.
- Spec: `specs/SYNC-PULL.md` (new), each section naming its decision keys, and a row in `specs/README.md`; `specs/API-SURFACE.md` rows for `sync.pull` and `sync.peers`.

## Out of scope

- The HTTP route, the status codes on the wire and the transport (`decision:sync-engine-host-http`: a host adapter). This plan returns the error codes and leaves the status mapping to the adapter.
- Authentication and authorization: `unauthorized` and `forbidden` are raised by the host. Sync auth waits on Q48, Q50 and Q51.
- The receiving side, merge and conflicts (plan 45).
- The CLI `remember sync pull` (it needs a transport) and the MCP `sync_peers` tool (no MCP plan).
- Triggers and timers (`decision:sync-triggers`: daemon work), Settings screens and wording (Story Weaver owns every message), and the Cloud server.

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md). Tests live in `tests/test_sync_pull.c` (new, suite `sync_pull`, added to `tests/gate-suites`). The page body is built and compressed in memory in the engine; the engine opens no socket.

## What must be true

Request. `sync.pull` takes `protocol` (1), `peer_id`, `ack_seq` (a number or null), `page_token` (optional), `page_size` (default 200, 1 to 1000; a larger value is clamped and warns `page_size_clamped`; zero or less fails `page_size_invalid`) and `reset` (default false). A page holds at most 8 MB before gzip and at least one record. The sender uses its own stored cursor and never the caller's `ack_seq` as the cursor.

Response. The reply data has `protocol`, `mode` (`incremental` or `snapshot`), `snapshot_reason` (`new_peer`, `expired`, `wiped` or null), `cursor`, `high_seq`, `records`, `page_seq`, `next_page_token` (or null), `has_more`, `warnings` and `errors`. Each record is `{id, change_id, change_seq, version_vector, deleted, updated_at, record}`, where `record` is the full record or null. Records are in ascending `change_seq` and bounded by `high_seq`. Over HTTP the adapter gzips the body at level 6.

Cases. A known peer's first call is incremental. An unknown peer gets a snapshot with reason `new_peer`. A peer silent for more than 30 days gets a snapshot with reason `expired`. A request with `reset` true gets a snapshot with reason `wiped`. A snapshot is every current record plus the kept tombstones, up to `high_seq`, and the reply carries the info `snapshot_started`.

Token. `page_token` is opaque base64url, bound to the `peer_id`, the mode and `high_seq`, and valid for 1 hour. A record edited while paging appears again on a later page; the receiver's de-duplication makes that harmless.

Closing. After the page with `has_more` false, the peer sends one more pull with `ack_seq` equal to the last `page_seq` and no token. That pull moves the stored cursor, resets the peer's 30-day clock and allows pruning. A device trusts only the cursor it stored itself, and that cursor never moves backward; an ack at or below the stored cursor warns `ack_stale` and changes nothing.

Errors. A call fails with one of these codes: `protocol_unsupported` (the detail names the supported versions), `peer_id_invalid`, `page_token_invalid`, `page_token_expired`, `page_size_invalid`, `ack_out_of_range`. The adapter maps them to 426, 400, 400, 410, 400 and 409, and maps a busy engine to 503 `unavailable` with Retry-After. `unauthorized` (401) and `forbidden` (403) belong to the host.

Retention. The sender keeps a change until every known peer has acknowledged it. A peer silent for 30 days stops holding history. A tombstone stays until every active peer has acknowledged it. After a drain, the sender runs an incremental vacuum step (Q15).

Peers. `sync.peers` replies `{self{peer_id, name, kind}, peers[{peer_id, name, kind, acked_seq, last_sync_at, last_attempt_at, state, history_expires_at, error}]}`. `kind` is `mac`, `iphone`, `ipad`, `android` or `cloud`. `state` is `ok`, `retrying`, `stalled`, `expired` or `never`, worked out when read. `history_expires_at` is `last_sync_at` plus 30 days. Times are RFC 3339 UTC. `last_sync_at` is the older of the peer's last acknowledgement and this device's last pull (SQ1). `remember sync peers` prints the same data, with times in local time in human output.

## Stop and ask

The coder stops and asks Itay, through the architect, at each of these. The plan does not choose.

1. Approval of `decision:2026-11-12-sync-api-interface-message`, the source of every shape above except SQ1 to SQ3.
2. Whether an `ack_seq` sent with a mid-session page request moves the stored cursor. The pull model says the ack and the next-batch request are one call; Interface's closing pull is what moves the cursor.
3. The meaning of `cursor` and `page_seq` in the response, and the exact condition of `ack_out_of_range`.
4. Where a new peer's `name` and `kind` come from. The request carries only `peer_id`.
5. How links and brain settings rows appear in a page, since a page record shows only a memory `record`. Also whether `deleted` is true for a soft-deleted memory or only for a purged one (a tombstone).
6. The exact rule that maps the stored times and `last_error` to each state. Interface names five states; Q12 gives 30 days and Q20 gives 24 hours.
7. What `last_sync_at` is when only one direction has happened, and what `self` shows for an unnamed device.
8. The shape of `apply_errors` inside a pull request, and where the sender shows it (SQ2 names the list and nothing else).
9. Whether tombstones are kept for ever on a brain that has no peer row.
10. Whether two pulls from one peer at the same time are serialized or refused.
11. How the 30-day, 1-hour and 24-hour rules behave when the clock moves backward.
12. What a drain is for the incremental vacuum step, and how many pages the step frees.

## Review points

- The sender never moves the cursor from a caller's value alone and never moves it backward.
- A page is built from one read transaction; a write during paging cannot split a record.
- A snapshot includes the kept tombstones and no purged record.
- `sync.pull` writes only `peers` rows; it changes no memory, link or tombstone.
- No `unauthorized` or `forbidden` code comes from the engine.

## Definition of Done

- [ ] `known_peer_first_call_is_incremental_above_stored_cursor`.
- [ ] `unknown_peer_gets_snapshot_new_peer_and_a_peer_row`.
- [ ] `peer_silent_over_30_days_gets_snapshot_expired`.
- [ ] `reset_true_gets_snapshot_wiped`.
- [ ] `snapshot_holds_current_records_and_kept_tombstones_up_to_high_seq`.
- [ ] `records_ascend_by_change_seq_and_stop_at_high_seq`.
- [ ] `page_size_default_200_clamp_1000_warns_and_zero_fails`.
- [ ] `page_cap_8_mb_before_gzip_and_at_least_one_record`.
- [ ] `page_token_binds_peer_mode_high_seq_and_expires_after_1_hour`.
- [ ] `closing_pull_moves_cursor_resets_clock_and_never_moves_back`.
- [ ] `stale_ack_warns_ack_stale_and_changes_nothing`.
- [ ] `ack_out_of_range_fails`.
- [ ] `edited_while_paging_record_appears_again`.
- [ ] `tombstone_kept_until_every_active_peer_acks`.
- [ ] `incremental_vacuum_step_runs_after_a_drain`.
- [ ] `peers_reply_state_history_expiry_and_older_of_two_times`.
- [ ] `sync_peers_cli_matches_request_reply`.
- [ ] `engine_raises_no_unauthorized_or_forbidden`.
- [ ] `specs/SYNC-PULL.md` is written and the decisions above are `state:implemented`.
- [ ] The gate passes on the committed tree.

## Implementation Notes

## Review Notes
