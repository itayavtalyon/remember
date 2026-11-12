# 43 — Schema v7: link origin, device, peers, pending links, conflicts

**Area:** the brain schema and its migration (`src/store_sqlite.c`, `src/store.h`), the device defaults in RememberEnv (`include/remember.h`), and the `device.set` request (`src/api_table.c`).
**Needs:** plans 19, 27b and 29. No other plan.
**Blocked on:** nothing for the migration. Each item under "Stop and ask" blocks only the clause it names.

## Goal

One migration takes a v6 brain to v7. v7 adds the link `origin`, the device table and the dormant sync tables, all created empty. Nothing reads or writes `peers` or `pending_links` until plans 44 and 45. An engine older than v7 opens a v7 brain read-only (plan 27b).

## Source

- `decision:2026-11-12-q10-v7-dormant-peer-device-schema` (Q10-1 to Q10-12, all A): the tables, columns and rules below.
- `decision:2026-11-07-bg-x-10-schema` and `decision:2026-11-08-f-06-schema`: `entry_links.origin`, the device name and the peers tables ship in one v7 migration, dormant, with wiki-links; migration and compatibility tests gate the release.
- `decision:2026-11-11-wiki-links-7` (Q6.7 A: two rows, one per origin), `decision:2026-11-11-wiki-links-8` (Q6.8 A: confirm the v7 grouping), `decision:2026-11-11-wiki-links-3` (Q6.3 A: the pending table is promoted when the key is created), `decision:2026-11-11-wiki-links-1` (Q6.1 B: text links are parsed on local writes only).
- `decision:2026-11-11-sync-q11b` (a link whose target has not arrived waits in `pending_links`), `decision:2026-11-11-sync-applied-retention` and `decision:2026-11-12-q10-v7-dormant-peer-device-schema` Q10-5 (no applied-changes table), `decision:2026-11-11-sync-conflict-identity` (Q18), `decision:2026-11-11-log005-merge-rules` (Q9, amended 2026-11-12 by X1-12 C), `decision:2026-11-11-sync-pull-model`, `decision:2026-11-11-sync-sq1`, `decision:2026-11-11-sync-lagging-peer-retention` (Q12), `decision:2026-11-09-sync-capture-change-seq-per-peer-ack`, `decision:sync-peer-model`.
- `decision:2026-11-03-schema-read-only` (an older engine opens a newer brain read-only with `schema_too_new_read_only`).
- `decision:2026-11-12-x1-remember-call-requests` (Q10-2 A adds `device.set` to the request table).
- Plan 19 (the v6 migration order and the device id file) and design log 005 Round 7 (conflict reasons).
- Spec: `specs/SCHEMA.md` (new), each section naming its decision keys, and a row in `specs/README.md`.

## Out of scope

- Parsing `[[key]]` and `[[uuid]]` in bodies. The wiki-link plan does not exist yet (Stop and ask).
- Reading or writing `peers`, `pending_links` and the new `conflicts` rows for sync (plans 44 and 45).
- Any endpoint, credential or auth column (Q10-11 A). Sync auth waits on Q48, Q50 and Q51.
- The archive schema (`decision:2026-10-22-archive-state`): v8 or later.

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md). Tests live in `tests/test_schema_v7.c` (new, suite `schema_v7`, added to `tests/gate-suites`). SQLite stays inside `src/store_sqlite.c`.

## What must be true

Migration. A v6 brain migrates to v7 in one `BEGIN IMMEDIATE` transaction. Every step below runs inside it, then `PRAGMA user_version = 7` runs. No earlier step sets `user_version`. A failure rolls the brain back to v6 untouched. `min_reader_version` is raised so an older engine opens the brain read-only (value: Stop and ask 1).

Link origin. `entry_links` gains `origin TEXT NOT NULL` with the values `manual` and `text`. Every existing row becomes `manual`. The unique index `entry_links_edge` becomes `(from_id, to_id, kind, origin)`, so a manual link and a text link between the same two memories are two rows (Q6.7 A).

Device. A `device` table is keyed by device id and holds the name and the kind. A device id that has no row starts unnamed. A restored or copied brain mints a new device id (`decision:device-clone-detect`) and so starts unnamed. The host passes a default name and kind in RememberEnv (two new string fields, `device_name` and `device_kind`, either may be NULL); the engine uses them only when it creates the device row. A `device.set` request renames the device (Q10-2 A). `device.get` and `device.new` keep their meaning.

Peers. `peers` has one row per peer, with these columns: `peer_id` (canonical lowercase UUID, primary key), `name`, `kind`, `acked_seq`, `pulled_seq`, `first_seen_at`, `last_ack_at`, `last_pull_at`, `last_attempt_at` and `last_error` (JSON or null). It has no endpoint, no credential, and no stored `state` or `history_expires_at`: both are worked out when read (Q10-4 A). `kind` has no CHECK constraint; the engine validates it (Q10-10 A). `acked_seq` is the last `change_seq` the peer acknowledged; `pulled_seq` is the highest `change_seq` this device applied from the peer (Q10-3 A). There is no applied-changes table (Q10-5 A).

Pending links. One `pending_links` table holds a waiting link and what it waits for. It has a `target_kind` column with the value `id` or `key` (Q10-6 A).

Tombstones and indexes. `tombstones` gains `version_vector TEXT NOT NULL`; every existing row gets `{}` (Q10-9 A). `change_seq` indexes exist on `entries`, `entry_links` and `tombstones`, named `ix_entries_change_seq`, `ix_entry_links_change_seq` and `ix_tombstones_change_seq` (Q10-8 A). They stay unused until plan 44.

Conflicts. `conflicts` is rebuilt (Q10-12 A) with these columns: a conflict id, the record id, a `versions` array of the competing versions without local or incoming labels and sorted, `reason` (`concurrent_vv`, `key_clash` or `hash_clash`), `created_at` and `origin`. Each v6 row becomes one row: its `sync_id` is the record id, and its `local_json` and `incoming_json` become the two entries of `versions`. The conflict identity is the record id plus the sorted version vectors; one conflict exists per identity (Q18).

Compatibility. The CLI commands `conflicts` and `conflict accept` keep working on the rebuilt table. `conflict accept --keep local|incoming|both` keeps its words (X1-12 C); with no labels stored, `local` is the version this device authored. A v6 engine opens a v7 brain read-only and warns (plan 27b).

## Stop and ask

The coder stops and asks Itay, through the architect, at each of these. The plan does not choose.

1. The `min_reader_version` value that v7 writes, and the release that ships v7. v6 wrote `0.3.0`.
2. The types, nullability and defaults of the `device`, `peers` and `pending_links` columns that the Q10 record does not state.
3. Whether `entry_links.origin` has a CHECK constraint and a default. `kind` has a CHECK; Q10-10 chose none for a peer `kind`.
4. The columns of `pending_links` beyond `target_kind`, and how a waiting synced link (Q11b) differs from a waiting body reference (Q6.3).
5. Which store is the source of truth for the device id: the file `<brain>.d/device_id` or the `device` table. Also whether a copied brain keeps its old `device` row.
6. The kind value the CLI passes when it has no host, the members of `device.set`, and the CLI spelling that sets the name.
7. The format of the conflict id and the hash behind it (Interface says `hash(record id + sorted vvs)`; `conflict accept --id` and the request `conflict.accept` take an `id` today), and whether two migrated v6 rows with the same identity merge into one.
8. The canonical text form and the sort order of a version vector in the sorted `versions` array.
9. When an existing brain gets incremental auto-vacuum (`decision:2026-11-11-sync-space-reclaim`). `VACUUM` cannot run inside the migration transaction. A new brain gets it when created.
10. Whether `import --from-db` accepts a v7 source. Plan 23 lists v4 to v6.
11. The wiki-link plan does not exist, and F-06 ships v7 together with wiki-links. Itay says which plan parses `[[key]]` and `[[uuid]]` and whether v7 releases without it.

## Review points

- No step sets `user_version` before the last one.
- Every v6 row survives: `entries`, `entry_links` (all `manual`), `tombstones` (all `{}`) and `conflicts`.
- `peers`, `pending_links` and `device` hold no row after a migration, except the one device row.
- An older engine opens the migrated brain read-only and no write succeeds.
- `PRAGMA integrity_check` and `PRAGMA foreign_key_check` are clean after the migration.

## Definition of Done

- [ ] `v6_fixture_migrates_to_user_version_7_in_one_transaction`.
- [ ] `failed_step_rolls_back_to_v6`.
- [ ] `entry_links_origin_all_manual_and_edge_index_has_origin`.
- [ ] `manual_and_text_link_between_same_pair_are_two_rows`.
- [ ] `device_row_starts_unnamed_and_env_defaults_name_it_once`.
- [ ] `device_set_renames_and_device_get_returns_name`.
- [ ] `copied_brain_mints_new_device_id`.
- [ ] `peers_has_the_listed_columns_and_no_endpoint_no_state`.
- [ ] `peers_kind_has_no_check_constraint`.
- [ ] `pending_links_target_kind_is_id_or_key`.
- [ ] `tombstones_version_vector_defaults_to_empty_object_for_old_rows`.
- [ ] `change_seq_indexes_exist_on_three_tables`.
- [ ] `conflicts_rebuilt_unlabeled_sorted_one_per_identity`.
- [ ] `conflict_accept_keeps_local_incoming_both_after_rebuild`.
- [ ] `v6_engine_opens_v7_brain_read_only` (a compatibility test against the plan 27b path).
- [ ] `integrity_and_foreign_key_check_clean_after_migration`.
- [ ] `specs/SCHEMA.md` is written and the decisions above are `state:implemented`.
- [ ] The gate passes on the committed tree.

## Implementation Notes

## Review Notes
