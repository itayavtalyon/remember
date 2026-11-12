# 31 — Memory events and the outbox insert

**Area:** event creation and the outbox table insert inside the write transaction (`src/events.c` new, `src/events.h` new, `src/store_sqlite.c`).
**Needs:** plans 25, 27a and 30. No other plan.
**Blocked on:** nothing.

## Goal

Every write that changes a memory creates its events in the same transaction, and inserts one outbox row per event when at least one enabled entry's full filter matches. No process starts in this plan.

## Source

- Log 010 H-3 (event message), H-4.1 to H-4.3 (insert), H-7 (origins, `skipped_source`), the event list.
- Old plan 25 sections "Events" (first two paragraphs), "Delivery" (first two paragraphs), "Event message".
- `decision:2026-11-08-sync-record-order-hooks-delivery` (ordered, durable, at least once).
- `decision:s-14`: `--hooks` and `--no-hooks` are global write flags (log 010 H-Q16 says `--hooks` does not exist; that line is stale against `decision:s-14` and H-4.3, and the spec drops it). Help lines H-16.11.
- Spec: `specs/EVENTS.md` (new).

## Out of scope

The expiry sweep and `memory.expired` (plan 32). Dispatch, pre-flight, runs (plan 35). Trust (plan 33).

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md). Tests in `tests/test_events.c` (new, suite `plugins`). SQL stays in `src/store_sqlite.c`.

## What must be true

Events: `memory.created` (a new memory row), `memory.updated` (body, tags, key, `expires_at`, or a link changes), `memory.deleted` (moves to Deleted, including `delete --expired`), `memory.restored` (leaves Deleted), `memory.purged` (`remember purge <locator>`). There is no `memory.linked` and no `memory.unlinked`. `link` and `unlink` emit one `memory.updated` per end, with `changes.link` `{kind, direction, other}`, and one shared `batch_id`. `memory.updated` fires only when a field or a link changed. N memories in one command share one `batch_id`.

Outbox table: the v6 table already on main (`k_v6_outbox_sql` in `src/store_sqlite.c`, log 010 §17): `outbox(event_id TEXT PRIMARY KEY, type TEXT NOT NULL, payload TEXT NOT NULL, recipients TEXT NOT NULL, created_at TEXT NOT NULL)`. This plan adds no column and no migration. `type` is the event type, `payload` the frozen event JSON. The comment in `src/commands_common.h` line 112 ("until plan 25 adds the outbox writer") is replaced by a pointer to `src/events.h`.

Insert: in the write transaction, insert one outbox row per event when an enabled entry's full filter matches. The filter is `events`, `origins`, `tags.any`, `tags.all`, `match_before`, `created_by`, `updated_by`, `bins`, and `not_caused_by`. A miss increments the in-memory counter `skipped_filter` and writes no log line. The row stores the frozen event JSON in `payload` and `recipients`: a JSON array of `{"entry":"plugin/<name>/<entry>","state":"pending"}`. A plugin added later does not see the event.

`--no-hooks` inserts nothing. `--hooks` matches every action for that write and ignores `origins`. It does not ignore `enabled` false.

Origins: `cause.origin` is `local`, `sync`, or `import`. Default action origins are `["local"]`. An action skipped only because of `origins` writes one `skipped_source` line per skipped action and event type to `<brain>.d/hooks.log` with the H-7.2 fields `{"version":1,"at","event":"skipped_source","event_type","command","origin","plugin","action","count"}`.

Event message (H-3): `type` `event`. `data` always has `event_id`, `event_type`, `occurred_at`, `detected_at`, `batch_id`, `cause` (with `origin`), `brain`, `memory`, `changes`, `truncated`, `truncated_fields`, `limits`, `plugin`, `entry`, `data_dir`, `settings`. `data.memory` is `remember --json get` without `local_id` and without `version_vector`, plus `change_seq`. `data` is at most 262144 bytes. Over the limit, drop `changes.before.body`, then `links`, then cut `memory.body` to 16384 bytes on a UTF-8 boundary, and set `truncated` and `truncated_fields`. `cause.hook` is `plugin/<name>/<entry>`. There is no `cause.plugin`. The stored `payload` omits `plugin`, `entry`, `data_dir` and `settings`; plan 35 fills them per recipient.

`REMEMBER_PROCESSES=0` (capability `processes` off): writes insert no outbox row. A write that matches an enabled folder-plugin action is saved and warns `plugin_unsupported`, exit 0 (4 with `--strict`). Print C-23.

No synced, imported, or exported data creates a plugin.

- **Warnings carry a `fix` (E3 B, `decision:2026-11-12-e3-warnings-have-fix-field`).** Every warning this plan prints has a `message` and a non-empty `fix`: `plugin_unsupported` (dispatch in a build that cannot start programs). The wording is copy item E3B-1, entry 15, in `decision:2026-11-12-copy-batch-e3b-warning-splits`: the approved sentence stays word for word and only its next step moves into `fix`; the plain line is `remember: warning: <message> <fix>`. `code`, `detail`, exit codes and `--strict` do not change. Itay approved E3B-1 as a whole on 2026-11-12, including every `fix` that is Story Weaver wording (`decision:2026-11-12-copy-batch-e3b-warning-splits`).

## Review points

- Outbox and memory commit or roll back together.
- No body or tag text goes into argv or env (nothing is spawned here; the test checks the stored row only).

## Definition of Done

- [ ] `six_event_types_created` (010 test 1, without `memory.expired`; the link half: two `memory.updated`, one `batch_id`, no version-vector change on the ends).
- [ ] `no_enabled_action_no_row` (010 test 2).
- [ ] `outbox_commits_with_memory` (010 test 3).
- [ ] `filter_miss_inserts_nothing_counts_skipped` (010 test 6).
- [ ] `truncation_order_256k` (010 test 10).
- [ ] `default_origins_skip_sync_and_import_and_log_skipped_source` and `hooks_flag_ignores_origins` (010 test 15).
- [ ] `no_hooks_inserts_nothing`.
- [ ] `processes_off_warns_plugin_unsupported_c23` (010 test 17, write half).
- [ ] `specs/EVENTS.md` written; the decisions above are `state:implemented`.
- [ ] The gate passes on the committed tree.

## Implementation Notes

## Review Notes
