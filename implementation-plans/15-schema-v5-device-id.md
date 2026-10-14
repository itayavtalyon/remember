# 15 — Schema v5 + sidecar-only device id (006 §1, §9 FTS rows, §12)

## Goal

Move the brain from `user_version` 4 to **5** in one migration. After this
plan the key is searchable through FTS. Hebrew marks are folded out of the
FTS rows. The tables `bucket_prefixes` and `settings` and the column
`conflicts.origin` exist (empty and unused for now). The `devices` table is
gone, so the sidecar file is the only device id. Two commands are added:
`remember device` and `remember device new`.

This plan is the base for plans 16–22. It changes no search syntax, no error
format and no list behavior.

## Source of truth

[`design-logs/006-ui-overhaul-engine.md`](../design-logs/006-ui-overhaul-engine.md)
§0 (EN-01..EN-04), §1 (E1.2–E1.5), §9 (E10.1, FTS rows only), §12 (E14).
Do not invent. If 006 does not decide a case, stop and ask Itay (grill), then
record the answer under Implementation Notes.

## Context (standalone summary)

Baseline: `main` at `a27efca`, version 0.2.0, `user_version` 4.

- `entries_fts` today is `fts5(body, tags, tokenize='unicode61 remove_diacritics 2')`.
  The key is not indexed, so `search editor` does not find `pref:editor`.
- `fts_resync(db, entry_id)` in `src/store_sqlite.c` rewrites one FTS row.
  It inserts `(rowid, body, tags)`, with tags joined by one space.
- Device identity today has two copies: the sidecar `<db-path>.device_id`
  (mode 0600) and a one-row `devices` table. `ensure_device_identity` /
  `recover_device_identity` reconcile them; a lost sidecar is rewritten from
  the row (005 "Lost sidecar").
- Hard wipe (`delete --expired|--deleted`, `purge`) calls
  `require_single_device`. It fails with `STORE_ERR_NOT_SINGLE_DEVICE` when
  `devices` COUNT != 1.
- `store_import` (`import --from-db`) opens the source read-only, refuses
  `user_version` < 4, never reads or writes a sidecar and never inserts
  foreign devices.

Rules this plan implements (restated from 006):

| ID | Rule |
|----|------|
| E1.3 | v4 → v5 on open, one `BEGIN IMMEDIATE` transaction: (1) drop `entries_fts`, create `entries_fts(key, body, tags, tokenize='unicode61 remove_diacritics 2')` in that column order; (2) repopulate it from every row of every bin; (3) `CREATE TABLE bucket_prefixes(prefix TEXT PRIMARY KEY, added_at TEXT NOT NULL, removed_at TEXT, device_id TEXT NOT NULL)`; (4) `CREATE TABLE settings(name TEXT PRIMARY KEY, value TEXT NOT NULL, updated_at TEXT NOT NULL)`; (5) `ALTER TABLE conflicts ADD COLUMN origin TEXT`; (6) `DROP TABLE devices`, copying no row into the sidecar; (7) `user_version = 5`. |
| E1.2 | `fts_resync` writes the key (empty string when NULL), the body, and the tag names joined with one space. |
| E10.1 (rows) | FTS rows drop U+0591–U+05BD, U+05BF, U+05C1–U+05C2, U+05C4–U+05C5, U+05C7. `entries` keeps the original body. (Query-side folding is plan 18.) |
| E1.4 | `import --from-db` accepts v4 and v5 sources, read-only, never migrated. A missing `origin` column or a missing `bucket_prefixes` / `settings` table means "absent". |
| E1.5 | 0.2.x refuses v5 with `database is newer than this remember`. (Already true: 0.2.0 refuses `> 4`.) This build refuses `> 5` with the same text. |
| E14.1 | Writer id = the one lowercase UUID line in `<db-path>.device_id`, mode `0600`. No copy in the database. |
| E14.2 | Open uses a valid sidecar. Open never reads a device id from the database. |
| E14.3 | Missing sidecar → mint UUIDv7, write it. Includes a v4 brain whose sidecar is gone after migration. |
| E14.4 | Invalid sidecar → error `invalid device_id sidecar`. Never mint over an invalid file. |
| E14.5 | `remember device` prints the id; `--json` → `{"version":1,"device_id":"…"}`. |
| E14.6 | `remember device new` mints a UUIDv7 and writes the sidecar. Version vectors do not change. Plain output = new id. `--json` → `{"version":1,"device_id":"…","previous":"…"}`; `previous` is `null` when the file was missing. |
| E14.7 | Hard delete and purge run once open has a valid sidecar. `not_single_device` stays in the catalog; no v5 command returns it. |
| E14.8 | `bucket_prefixes.device_id` and `version_vector` store device-id strings; no FK to a `devices` table. |

## Prerequisites

- `main` clean and up to date; `user_version` 4 build green.
- No other 006 plan is required.

## Scope

**In:** v5 schema for fresh create and for migrate (v1/v2/v3 → 5 too, by
chaining the existing path then the v5 step, in one transaction); FTS key
column + Hebrew row folding; `bucket_prefixes`, `settings`, `conflicts.origin`
DDL only; drop `devices` and all its code; sidecar-only identity; atomic
sidecar write; `device` / `device new` commands (CLI + help + facade);
hard-wipe hatch removal; `import --from-db` reading v4 and v5 sources;
tests; What's New lines; skill/help updates for `device`.

**Out:** search syntax or ranking changes (plan 18); error codes and
`--json-errors` (plan 16); writing or reading `bucket_prefixes`/`settings`
(plan 19); writing `conflicts.origin` (plan 21); query-side Hebrew folding
(plan 18); version bump to 0.3.0 (plan 22).

## Locked decisions (grill 2026-10-12; index in [`006-locked-decisions.md`](006-locked-decisions.md))

| # | Decision |
|---|----------|
| D4 | Open mints a missing sidecar as usual (E14.3). `device new` mints again and reports the open-minted id as `previous`. `previous` is never `null` in practice; keep `null` in the JSON type only. |
| D9 | `invalid device_id sidecar`, `cannot write device_id sidecar` and `cannot mint device_id` map to code `internal` once plan 16 lands. Until then: exit 1, plain `remember: <message>`. |
| P6 | `device` gets a registry row, a help topic and a facade byte-match test. |

Anything 006 and this table do not decide: stop and ask Itay. Do not choose.

## Files

`src/store_sqlite.c` (DDL, migrate, `fts_resync`, fold helper, sidecar,
device ports, hatch removal, import source probing), `src/store.h`
(`store_device_id`, `store_device_new`), `src/normalize.c/.h` (pure
`fold_hebrew_marks`: plan 18 reuses it for query tokens, so it lives in
`normalize.c`, not in the adapter), `src/cli.c/.h` (`device` in `k_commands[]`),
`src/remember_app.c` (dispatch + help), new `src/cmd_device.c` (wire it in
`REMEMBER_LIB_SOURCES` **and** the `-Wcast-qual` list), `src/output.c/.h`
(device envelopes), `tests/test_store_sync.c`, `tests/test_sync_delete.c`,
`tests/test_sync_import.c`, a new `tests/test_schema_v5.c` (suite
`schema_v5`), `tests/test_facade.c`, `tests/gate-suites`, `tests/COVERAGE.md`,
`skills/remember/SKILL.md`, `README.md` (What's New), `docs/engineering-notes.md`.

## Must-pass tests

| Check | Proves |
|-------|--------|
| Fresh DB: `user_version=5`; `entries_fts` columns are `key, body, tags` in order; `bucket_prefixes`, `settings` exist; `conflicts.origin` exists; no `devices` table | bootstrap |
| Fresh DB and migrated DB have the same schema text (`sqlite_master` compare) | no drift |
| v4 fixture (keys, keyless, tags, links, all three bins, conflicts) migrates; every `id`, `sync_id`, body, tag, link, `version_vector`, conflict row kept | E1.3 migrate |
| v1, v2, v3 fixtures open straight to v5 | chained migrate |
| Migration is atomic: a fault injected after step 3 leaves `user_version` 4 and the old FTS (store unit, `REMEMBER_TEST_HOOKS`) | E1.3 one txn |
| `search editor` finds `pref:editor` after migrate and on a fresh brain | E1 verification |
| Body and tag search results equal before and after migration (same ids, same order) for a fixture query set | E1 verification |
| `rekey`, keyed `add` upsert, `import --from-db` insert, `conflict accept` (each that can change a key) resync the FTS key column | E1.2 every path |
| Body `סֵפֶר` (with niqqud) is found by `search ספר`; `get` still returns the niqqud body byte-identical | E10.1 rows |
| `user_version` 6 → exit 1 `database is newer than this remember` | E1.5 |
| `import --from-db` from a v4 source and from a v5 source both succeed; source file bytes unchanged (read-only, not migrated) | E1.4 |
| v4 brain + valid sidecar → same device id after migrate; `devices` row not consulted | E14.2 |
| v4 brain whose sidecar is missing → new UUIDv7, not the old `devices` row value | E14.3, "v4 row not copied" |
| Missing sidecar on a v5 brain → minted, mode `0600`, one lowercase line | E14.3 |
| Invalid sidecar (garbage, uppercase UUID, two lines, empty) → exit 1 `invalid device_id sidecar`; file unchanged | E14.4 |
| `device` → A; `device new` → B with `previous` A; next `update` bumps VV under B, A's counter kept | E14 verification |
| `device new` does not change any `version_vector` | E14.6 |
| `delete --expired` / `purge --deleted` succeed on a v5 brain (no hatch); no command returns `not_single_device` | E14.7 |
| Sidecar write is atomic (temp file + rename, mode 0600) | safety |
| Facade byte-match for `device`, `device new` (mask ids) and for search after migrate | EN-03 |

## Stages

TDD, one failing test at a time. After each stage: gate green, in-session deep
review, Implementation Notes, commit.

1. **Fresh v5 schema.** `SCHEMA_VERSION = 5`. New FTS DDL with key first.
   Single-source the DDL so fresh create and migrate cannot drift (plan 14
   lesson: `K_ENTRIES_COLUMNS` / `_INDEXES`). `fts_resync` writes key, body,
   tags. Fold Hebrew marks into the FTS key/body/tags text (a pure helper
   that removes the listed code points from valid UTF-8). Tests: bootstrap,
   key search, Hebrew row fold.
2. **Migrate v4 → v5** (and v1/v2/v3 through it) in one `BEGIN IMMEDIATE`.
   Repopulate FTS from all bins with the fold. Drop `devices` last before
   `user_version`. Fixture tests and the fault-injection atomicity test.
3. **Sidecar-only identity.** Delete `devices_*`, `recover_device_identity`,
   `ensure_device_identity` branches that read the table. New open flow:
   read sidecar → valid: use it; missing: mint + atomic write; invalid:
   fail. Remove `require_single_device` call sites; keep
   `STORE_ERR_NOT_SINGLE_DEVICE` and its message for the catalog (exclude it
   from coverage only if the coverage script's defensive regex allows it;
   otherwise ask). Rewrite the 0.2.0 tests that assert the COUNT hatch or
   "lost sidecar rewritten from row": replace them with the E14 tests and
   name each replaced test in Implementation Notes.
4. **`device` / `device new`.** Store ports `store_device_id(Store *)` and
   `store_device_new(Store *, char *prev_out, size_t, bool *had_prev)`.
   `cmd_device.c`, registry row, help topic, JSON + plain output, facade
   test. `previous` per D4.
5. **Import sources v4 + v5.** Probe the source schema read-only (missing
   `origin`, `bucket_prefixes`, `settings` are "absent"; nothing reads
   `devices`). Accept 4 and 5, refuse < 4 as today, refuse > 5 with
   `database is newer than this remember`.
6. **Docs + full gate.** Help (`device`), SKILL.md (device id is the sidecar;
   `device new` after a restore or a copied brain), What's New lines,
   engineering notes (`store_open` bullet: v5, sidecar-only, no hatch),
   `tests/COVERAGE.md` rows, `tests/gate-suites`.

## How to work (workflow + coding standards)

**Workflow**

1. Branch from an up-to-date, clean `main`. One branch and one PR for this plan.
2. If the brain is reachable, load `workflow:deep-review`,
   `pref:code-review-workflow`, `lesson:review-run-edge-cases` and
   `status:remember` (`workflow:project-status`). If it is not, the in-repo
   docs below are the reference.
3. TDD: a red test first, then the smallest change that turns it green, then
   refactor. One failing test at a time.
4. Do not invent. A case 006 does not decide → stop, grill Itay, record the
   decision (plan Implementation Notes; brain `decision:<slug>` if reachable).
5. After each stage: full gate, in-session review against
   [`docs/STEP_REVIEW_CHECKLIST.md`](../docs/STEP_REVIEW_CHECKLIST.md),
   append Implementation Notes, commit.
6. On pause or block, update the `status:remember` card.

**Coding standards** ([`docs/engineering-notes.md`](../docs/engineering-notes.md),
[`docs/QUALITY.md`](../docs/QUALITY.md))

- SQLite only in `store_sqlite.c`; everyone else uses `store.h`.
- Layers: `cli_parse` (pure argv) → `cmd_*` (normalize → `store_*` →
  `output_*`, no SQL) → `output.c` (escaping + envelopes).
- New TU: add to `REMEMBER_LIB_SOURCES` **and** the `-Wcast-qual` list;
  include directly what each `.c` uses (`misc-include-cleaner`).
- `goto cleanup` single exit; NULL-safe frees in reverse order; set the error
  before `goto`.
- Write through `app_out()` / `app_err()` only (facade parity).
- User-facing strings are ASCII. Timestamps are `YYYY-MM-DDTHH:MM:SS.mmmZ`.
- C11 with the project feature macros; prefer ISO C to POSIX where equal;
  `size_t` loop counters over lengths; exhaustive `switch` with `default:`.
- Fault-injection sweeps: one `sweep_*_faults` helper per new op.

**Quality gate** (before every commit that touches C; on macOS prefix
`PATH="$(brew --prefix llvm)/bin:$PATH"`)

```bash
cmake -S . -B build -DCMAKE_C_COMPILER=clang -DREMEMBER_ENABLE_SANITIZERS=ON
cmake --build build
ctest --test-dir build --output-on-failure        # step_gate + store_asan_gate + install_skill
./scripts/lint-all.sh                             # LINT OK
./scripts/check-coverage.sh                       # 100% functions + 100% effective lines
./scripts/ci-linux.sh                             # Linux toolchain parity (Docker)
cmake --build build --target leaks-macos          # macOS only
```

## Definition of Done

- [x] Design log 006 unchanged except new rows in its Implementation Results
      for E1.2–E1.5, E10.1 (rows), E14.
- [x] Every must-pass row green and included in `tests/gate-suites`.
- [x] Fresh and migrated schemas are identical; `user_version` 5.
- [x] No code path reads or writes a `devices` table; `require_single_device`
      is gone; `not_single_device` is never returned.
- [x] Every key-changing path resyncs the FTS key column (test per path).
- [x] `device` / `device new` work in human and `--json`; facade byte-match.
- [x] Locked decisions D4, D9, P6 implemented as written.
- [x] Help, SKILL.md, README What's New (v0.3.0 section, one line per user-
      visible change: key is searchable, Hebrew marks folded, `device`,
      sidecar-only identity, hard wipe without the device hatch).
- [x] `docs/engineering-notes.md` `store_open` bullet updated.
- [x] Gate: ctest, lint, coverage 100/100, `ci-linux.sh` all green.
- [ ] Second review done; no open **blocking** item.
- [ ] `implementation-plans/INDEX.md` row marked ✅.

## Second review handover

Hand this to an independent reviewer (a fresh session that did not write the
code). The reviewer runs `workflow:deep-review` if the brain is reachable,
else [`docs/STEP_REVIEW_CHECKLIST.md`](../docs/STEP_REVIEW_CHECKLIST.md),
and verifies by **running**, not reading.

**Package to hand over**

- Branch, PR link, commit range, this plan, 006 §1/§9/§12.
- Gate output pasted in the PR (ctest, lint, coverage summary, `ci-linux.sh`).
- Locked decisions D4, D9, P6 and any new grill answers.
- The list of replaced 0.2.0 tests and why each was replaced.

**Probe these (expected result in brackets)**

1. Copy a real v4 brain; open it with the new build; `sqlite3 x.db .schema`
   [no `devices`; FTS `key, body, tags`; `user_version` 5]; diff the sorted
   `.schema` with a fresh brain's [identical; statement order may differ,
   Itay 2026-10-13].
2. `remember --db x.db --json search editor` on a brain with `pref:editor`
   [found].
3. Kill-mid-migration: run the fault-injection test and inspect the file
   [still v4, old FTS intact, reopen migrates].
4. Every key-changing path: `rekey --to-key`, `rekey --clear-key`, keyed
   `add` upsert, `import --from-db`, `conflict accept --keep incoming|both`;
   after each, `search <new-key-word>` [found] and `search <old-key-word>`
   [not found].
5. Sidecar: delete it [new id minted, 0600]; write `GARBAGE` [exit 1
   `invalid device_id sidecar`, file untouched]; make it unwritable while
   missing [clean error, no partial file].
6. `device new` then `update` [VV gains B; A's counter unchanged].
7. `import --from-db` of a v5 source; `stat` the source before/after
   [unchanged mtime and bytes].
8. Hebrew: body with niqqud; `search ספר` [found]; `get` [original bytes].

**Hot spots**

- FTS key resync coverage of every write path (missed paths make stale keys).
- Migration atomicity with `foreign_keys` toggling (plan 14 rebuild lesson).
- Fold helper on invalid or truncated UTF-8 (must not read past the buffer).
- `previous` semantics (D4): the id open just minted, never `null` in practice.

**Do not re-open:** sidecar is the only device id; `devices` is dropped and
not copied; FTS column order `key, body, tags`; tokenizer unchanged;
`not_single_device` stays in the catalog.

**Review output:** append `### <date> — second-opinion deep review (stage N |
FINAL)` under Review Notes. Tag each finding **blocking / important / nit /
praise**. Auto-fix small, local, gate-green issues and say so; send product
or architecture forks to Itay as a grill. Re-run the full gate after any fix.

## Implementation Notes

### 2026-10-13 — stage 1 (fresh v5 + migrate, devices kept)

Itay chose migrate-keep-devices: fresh databases and v1–v4 all land on the same `user_version` 5 schema in this stage (FTS rebuild, `bucket_prefixes`, `settings`, `conflicts.origin`). The `devices` table stays until stage 3. Stage 2 is the leftover migrate edge (a scripted fail-after-step-3 atomicity test); a prepare failure during FTS repopulate is already covered and leaves the file at v4.

- DDL macros (`K_FTS_SQL`, tags, devices, conflicts, bucket_prefixes, settings) are shared by fresh create and migrate. Conflicts are rebuilt by renaming the old table away, then `CREATE` with the canonical DDL, because renaming the new table quotes its name in `sqlite_master`.
- `fold_hebrew_marks` lives in `normalize.c` for plan 18. Entries keep the original body. Invalid or truncated UTF-8 is copied one byte and never read past the buffer.
- Import still accepts user_version >= 4 (`IMPORT_MIN_USER_VERSION`). Refusing a source newer than 5 is stage 5.
- `store_rekey` now calls `fts_resync` before commit. Add, update, and import already did. Search follows the new key and drops the old one.
- What's New, SKILL.md, and the design-log Implementation Results stay for stage 6 / the whole-plan DoD. Engineering notes record the v5 open rules because stage 2 depends on them.

### 2026-10-13 — stage 2 (rollback after step 3; tags already exist)

Devices stay until stage 3 (`decision:plan15-stage1-keep-devices`). This stage does not drop them.

- `apply_v5_delta` no longer creates `tags` or `entry_tags`. Real v1–v4 brains already have them. The compact v1 and v2 fixtures now create those tables with the canonical DDL before `PRAGMA user_version`. Without that, open fails with `cannot rebuild search index` and the file stays at the old version.
- `schema_v5_fault_after_step3_keeps_v4` fails the 8th `sqlite3_exec` on a v4 open (`store_test_fail_exec_after(7)`). That statement is `CREATE TABLE settings`, the first exec after `bucket_prefixes`. The file stays `user_version` 4, FTS column 0 stays `body`, and `bucket_prefixes` is absent. Reopen then migrates. A probe that committed after `bucket_prefixes` and continued made this test fail (FTS column 0 became `key`, and `bucket_prefixes` was left behind). The probe was removed.
- The prepare-failure test from stage 1 still covers a fault during FTS repopulate.

### 2026-10-13 — stage 3 (sidecar-only identity)

The sidecar is the only device id. Fresh create and a v1–v4 migrate both finish with no `devices` table. `DROP TABLE IF EXISTS devices` is the last statement in `apply_v5_delta` and copies no row. v1–v3 no longer create the table, so the drop is `IF EXISTS`. `decision:plan15-stage1-keep-devices` ends here.

Open reads the sidecar and nothing in the database. A valid file is used. A missing file is a new UUIDv7, written as one lowercase line, mode 0600, through `<path>.device_id.tmp` then `rename`. An invalid file (garbage, uppercase UUID, a second line, empty) fails with `invalid device_id sidecar` and is not overwritten. `fgetc` after the first line must be EOF; a read error is not treated as the end of the file. Those messages stay plain `remember: …` exit 1 until plan 16 (D9).

Identity is bound before `backfill_sync_ids` on v0–v3, and after the v5 delta on v4. An already-v5 brain binds on open. `require_single_device` is gone. `STORE_ERR_NOT_SINGLE_DEVICE` and `hard delete requires exactly one registered device` stay in the catalog. No command returns that status. Purge help no longer claims the hatch. README and SKILL.md still do; stage 6 owns them.

`store_test_fail_sidecar_rename(n)` fails the next rename when `n > 0` and is compiled only into the test hooks. A failed rename unlinks the temp, so neither the sidecar nor the `.tmp` is left behind.

Replaced tests:

- `store_get_does_not_bump_last_seen` → `store_get_does_not_rewrite_sidecar`
- `store_lost_sidecar_rewritten_from_devices` → `store_missing_sidecar_mints_a_new_id` (a missing sidecar is minted, not restored)
- `store_sidecar_wins_over_devices_mismatch` → removed; the v4 case is `schema_v5_v4_sidecar_is_kept_and_devices_row_is_ignored`
- `store_empty_devices_inserted_from_sidecar` → `store_reopen_keeps_sidecar`
- `store_hard_delete_hatch_two_devices` → `store_hard_delete_needs_no_device_hatch` (catalog string still present; hard delete returns `STORE_OK`)
- `store_purge_deleted_and_hatch` → `store_purge_deleted_and_expired`
- `store_devices_replace_exec_fail_undoes_savepoint` → `store_sidecar_rename_failure_leaves_no_file`
- `cli_delete_deleted_hard_and_hatch` → `cli_delete_deleted_hard_wipes_the_row`
- `cli_purge_hatch_two_devices` → `cli_purge_expired_wipes_without_device_hatch`
- `store_import_no_sidecar_copy_devices_stay_one` kept its name. It now asserts there is no `devices` table and that the foreign sidecar is still absent.
- `facade_conflict_accept_matches_cli` copies the sidecar with the database. Open of a copied file no longer recovers the writer id from a `devices` row, so the two accepts would bump different ids.

Schema and open tests that asserted `COUNT(*) FROM devices = 1` after a successful open now assert the table is absent. The rolled-back fault file is still v4 and still has one devices row.

### 2026-10-13 — stage 4 (`device` / `device new`)

`remember device` prints the sidecar id. `remember device new` mints a UUIDv7, writes the sidecar, and does not change any version vector. Plain output is the new id. `--json` is `{"version":1,"device_id":"…","previous":"…"}`.

The plan's port omitted an error buffer. `store_device_new` takes the same `err` buffer as `store_open`, so `invalid device_id sidecar`, `cannot write device_id sidecar`, and `cannot mint device_id` stay plain `remember: …` exit 1 until plan 16 (D9). Statuses are `STORE_ERR_INTERNAL`, `STORE_ERR_OOM`, and `STORE_ERR_SQLITE`.

The new id is duplicated before the rename. The handle pointer is swapped only after the rename succeeds, so an allocation failure leaves the old file. A failed rename frees that duplicate. The mint holds `BEGIN IMMEDIATE` across the read and the rename, the same lock and the same `<path>.device_id.tmp` as a missing-sidecar open. The transaction writes nothing. If that empty `COMMIT` fails after the sidecar is published, the mint still stands: memory already matches the file.

Open mints a missing sidecar first (D4), so a CLI `device new` reports that id as `previous`. JSON `null` is only the type. A direct `output_device_new_envelope(..., NULL)` covers it. A store test unlinks the sidecar after open and checks `had_prev` is false. `UUID_STR_LEN` is `STORE_DEVICE_ID_LEN` (one length). Delete help no longer says hard wipe needs one device. Import help, README, and SKILL.md still mention devices or the hatch; stage 6 owns them.

### 2026-10-14 — stage 5 (import sources v4 and v5)

`store_import` accepts `user_version` 4 and 5. Below 4 is still `source database is older than this remember`. Above 5 is `STORE_ERR_SOURCE_TOO_NEW`, the same text as open (`database is newer than this remember`), from one string `k_newer_database`. The max is `SCHEMA_VERSION`.

The source stays `SQLITE_OPEN_READONLY` and is not migrated. `probe_import_source` counts `conflicts.origin`, `bucket_prefixes`, and `settings` before the merge. A count of 0 is absent. Those rows are not copied (plans 19 and 21). `devices` is not queried. A failed probe returns before `BEGIN IMMEDIATE`, so the destination is unchanged.

A v4 fixture with no origin column, no `bucket_prefixes`, no `settings`, and one `devices` row imports. The source bytes and `user_version` stay 4. The destination has the entry and no `devices` table. A v5 source that already has a settings row, a bucket prefix, and a conflict `origin` keeps those rows and its sidecar bytes. The destination does not gain them. `user_version` 6 is refused and the destination stays empty.

### 2026-10-14 — stage 6 (docs + full gate)

Help `device` already described `new`, `previous`, the version vector, and the invalid-sidecar repair. This stage adds when to run it: after a restore or a copied brain. Import help no longer says "register devices". The source line is "schema v4 or v5" (E1.4, E1.5). `cli_import_and_conflict_human_and_help` locks both sentences. `help_device_describes_new` locks the restore line and the repair line.

SKILL.md: the device id is the sidecar; `device new` after a restore or a copied brain; `device new` also repairs an invalid sidecar. Hard delete and purge no longer mention the device hatch. Import no longer says it registers foreign devices. Search notes say the key is indexed and Hebrew marks are folded out of the index.

README What's New has a v0.3.0 section with one line each for key search, Hebrew marks, `device`, sidecar-only identity, and hard wipe without the hatch. The v0.2.0 section stays as the history of that release. The version string stays 0.2.0 (plan 22).

`docs/engineering-notes.md` `store_open` bullet now says v5, sidecar-only, and no device-count hatch, and that only `device new` replaces an invalid sidecar. The import bullet no longer says the adapter inserts foreign `devices`. `store.h` already said "never reads devices" (stage 5); this stage left that comment.

`tests/gate-suites` already lists `schema_v5` and `device`. Nothing was added. `tests/COVERAGE.md` rows for `test_schema_v5.c`, `test_device.c`, and `test_sync_import.c` name those suites.

006 Design is unchanged. Implementation Results records E1.2–E1.5, E10.1 (rows), and E14, including the two amendments already locked: the conflicts rebuild (`decision:plan15-stage1-review-grill`) and `device new` replacing an invalid sidecar (`decision:plan15-device-new-replaces-invalid`).

## Review Notes

### 2026-10-13 — in-session deep review (stage 1)

**Verdict:** approve for the second-opinion pass. No open blocking item.

**Auto-fixed:** `store_rekey` wrote `entries.key` and left the FTS key stale (search still hit the old key and missed the new one). Locked by `schema_v5_rekey_search_follows_the_new_key`.

**Praise:** one DDL source; v4 fixture text is compared to a fresh database with `EXCEPT` both ways; Hebrew fold is proven by a search that failed before the helper existed.

**Nit:** a brand-new database creates the v5 FTS table and `apply_v5_delta` drops and recreates it with the same SQL. One path, no drift. Left as is.

**Left for later stages:** drop `devices` (stage 3); `device` / `device new` (stage 4); refuse import user_version > 5 (stage 5); query-side Hebrew fold (plan 18); What's New (stage 6).

### 2026-10-13 — second-opinion deep review (stage 1)

**Verdict:** stage 1 approved. No open blocking item.

**Run, not read.** Gate: ctest 4/4 (ASan/UBSan), LINT OK, coverage 100% functions and 100% effective lines, `leaks-macos`, `ci-linux.sh`. Probes used a real 0.2.0 binary built from `42df03e`:

- A real v4 brain (keys, keyless, tags, a `cites` link, live/expired/deleted, one `concurrent_vv` conflict) opens to `user_version` 5. Every entries/tags/entry_tags/links/conflicts column is byte-identical before and after. `conflicts.origin` is NULL. The sidecar is unchanged. 30 body/tag/bin search queries return the same totals under 0.2.0 before and under the new build after.
- The sorted `.schema` of a migrated brain equals a fresh brain's. FTS columns are `key, body, tags`.
- `search editor` finds `pref:editor` on a migrated brain. `search exp --expired` finds `t:exp`.
- Key paths, checking that search follows the new key and drops the old one: `rekey --to-key`, `rekey --clear-key`, keyed `add` upsert (old body word gone), `import --from-db` insert, `import` fast-forward of a rekeyed row, and `conflict accept --keep incoming` and `--keep both` all pass.
- Hebrew: `search ספר` finds `סֵפֶר טוֹב` and `get` returns the niqqud bytes. Niqqud in a key or tag is folded in the FTS row and kept in `entries`/`tags`. `list --tag` with niqqud still matches exactly.
- 0.2.0 refuses a v5 file and this build refuses `user_version` 6, both exit 1 with `database is newer than this remember`. `import --from-db` of a 0.2.0 v4 source and of a v5 source both succeed, and the source sha and mtime are unchanged. Purging deleted entries leaves no FTS rows behind.

**Auto-fixed (gate re-run green):**

- **important** — the v4 fixture had no conflict row and no tag, so no test proved `rebuild_conflicts_with_origin` copies rows or that migration carries tags into FTS. `given_v4_brain` now has a tag and a conflict row. `schema_v5_v4_migrates_to_the_fresh_schema` asserts the conflict row byte-for-byte with `origin` NULL, and that the tag is searchable.
- **nit** — `fold_hebrew_marks` range edges were not pinned, so moving `0x05BD` to `0x05BC` still passed. The fold test now covers every edge (U+0590/0591, 05BD/05BE/05BF/05C0, 05C1–05C3, 05C4–05C6, 05C7/05C8). Mutation-checked: that edit now fails the test.
- **nit** — `ensure_conflicts_origin` repeated the `sqlite_master` probe from `create_table_if_missing`. It now calls `create_table_if_missing` and checks only the `origin` column.

**Grill (Itay 2026-10-13):**

- E1.3 step 5: keep the conflicts rebuild (rename away → canonical `CREATE` → copy → drop) instead of `ALTER TABLE ... ADD COLUMN`, because ALTER leaves `sqlite_master` text that differs from a fresh brain. Record this as an E1.3 amendment in the 006 Implementation Results (stage 6).
- Fresh and migrated schemas must be the same set of statements. Order may differ. Probe 1 is amended.
- Stage 2: remove the `create_table_if_missing` guards for `tags` / `entry_tags` in `apply_v5_delta`. Real v1–v4 brains always had them. Give the compact v1/v2 test fixtures the tags tables instead.

**Important (forward, stage 3):** `read_sidecar` reads only the first line through `fgets`. A sidecar with the valid id plus a second line `junk` opens today (verified). The stage 3 must-pass row "two lines → `invalid device_id sidecar`" needs `read_sidecar` to reject trailing content.

**Nit (left):** a fresh create builds the v5 FTS table and `apply_v5_delta` drops and rebuilds it (already noted in-session). Repopulate prepares four statements per row; fine at personal scale.

**Not done:** there is no local Bugbot binary on this machine (`bugbot` / `cursor-agent` not found). Its pass was replaced by a manual DRY/orthogonality read of the diff.

**Praise:** one DDL source for fresh create and migrate; the `EXCEPT` both-ways schema compare; the fold helper never reads past `src_len` (`utf8_cont` bounds-checks before it touches continuation bytes); `fts_resync` now covers every key-changing path. Probe 4 found no stale key anywhere.

### 2026-10-13 — in-session deep review (stage 2)

**Verdict:** approve for the second-opinion pass. No open blocking item. No grill.

**Ran:** ctest 4/4 (ASan/UBSan), LINT OK, coverage 100% functions and 100% effective lines, `leaks-macos` PASS, `ci-linux.sh` OK. The fault test was mutation-checked: committing after `bucket_prefixes` leaves the new FTS and the new table, and the test fails.

**Nit (left):** `SCHEMA_V5_EXEC_AFTER_STEP3` is a count of `sqlite3_exec` calls. An extra exec before `CREATE TABLE settings` would still leave a v4 file, so the test could pass without failing the settings statement. The comment lists the seven successes. Same pattern as `store_devices_replace_exec_fail_undoes_savepoint`.

**Nit (left):** the canonical `tags` / `entry_tags` DDL is copied into the v1 and v2 fixtures. The sqlite3 CLI cannot see the `K_TAGS_SQL` macro.

**Not done:** no local Bugbot binary (`bugbot` and `cursor-agent` are not on PATH). That pass was a manual DRY and orthogonality read of the diff.

**Praise:** one transaction still owns the delta. Removing the tag-table guards made the existing v1 and v2 tests fail until the fixtures matched real brains.

### 2026-10-13 — second-opinion deep review (stage 2)

**Verdict:** stage 2 approved. No open blocking item. No grill: the tag-guard removal is Itay's stage 1 answer, applied as written.

**Run, not read.** Gate: ctest 4/4 (ASan/UBSan), LINT OK, coverage 100% functions and 100% effective lines, `leaks-macos` PASS, `ci-linux.sh` OK.

- Fault position: a temporary `fprintf` in the exec hook (since reverted) showed that `store_test_fail_exec_after(7)` faults `CREATE TABLE settings`, as the test comment says.
- Probe 3 with real crashes: a 200k-row v4 brain (migrating takes about 3 s on the ASan build) was killed with `kill -9` at 0.3 s, 0.8 s, 1.5 s and 2.3 s. Each time the hot journal rolled the file back to `user_version` 4 with FTS column 0 `body` and `integrity_check` ok. Reopen migrated to v5 and the key search found all 200k rows.

**Auto-fixed (gate re-run green):**

- **important** — `schema_v5_fault_after_step3_keeps_v4` depends on an exec count. Mutation: adding `COMMIT; BEGIN IMMEDIATE;` before `CREATE TABLE settings` (a real mid-migration commit) still passed that test, because the extra exec moved the fault onto the injected `COMMIT`. This is the in-session nit, now proven. New `schema_v5_v4_open_exec_fault_sweep` fails every exec position of a v4 open in turn. Each failed open must leave v4, FTS column 0 `body`, and the old rows searchable. The sweep must also pass step 3 (`n > SCHEMA_V5_EXEC_AFTER_STEP3`) before a clean open migrates. The same mutation fails the sweep (`ASSERT_STREQ("key", "body")`). The named test stays as the readable E1.3 case.
- **nit** — clang-format on the new test.

**Nit (left, pre-existing):** `ensure_schema` ignores the result of `PRAGMA foreign_keys = ON` after migrating. If that PRAGMA failed, the connection would run with foreign keys OFF. It cannot fail outside a transaction in practice; failing loudly would be the fail-fast version.

**Praise:** removing the guards was proven by the old v1/v2 tests failing first. The engineering notes state the new rule (no `tags`/`entry_tags` creation, rollback after `bucket_prefixes`).

### 2026-10-13 — in-session deep review (stage 3)

**Verdict:** approve for the second-opinion pass. No open blocking item. No grill.

**Ran:** ctest 4/4 (ASan/UBSan), LINT OK, coverage 100% functions and 100% effective lines, `leaks-macos` PASS, `ci-linux.sh` OK. Store unit 127/0. Removing the trailing-content check makes `schema_v5_invalid_sidecar_is_left_unchanged` fail (a two-line sidecar opens). The check was put back.

**Auto-fixed:**

- **important** — `write_sidecar` set `fd = -1` only on the close-failure path. Coverage rejected that line (not a defensive pattern). The assignment now always runs after `close`.
- **important** — `read_sidecar` treated a read error as end of file. `ferror` after `fgetc` is now invalid, same as a second line.
- **important** — `facade_conflict_accept_matches_cli` copied the database and not the sidecar. The copy minted a new id, so the two accepts no longer matched. The test copies the sidecar too.
- **nit** — purge help still said the command requires one registered device. That sentence is gone. README and SKILL.md still say it; stage 6.
- **nit** — clang-tidy: null `db` into `sidecar_path`, swappable `write_text` parameters, `ArrayBound` on `is_uuid_v7` after `strlen` (the analyzer drops the length of a malloc'd line).

**Nit (left):** an unwritable directory while the sidecar is missing is not a unit test. The rename hook covers a failed `rename` and the temp unlink. The second-opinion probe list still asks for the directory case.

**Bugbot:** the in-session Bugbot pass reported no findings. Sidecar read/write, migration order, and the remaining `devices` assertions (rollback only) match the stage.

**Praise:** invalid sidecars are compared as file bytes, so a "fix" that rewrites the file fails the test. The catalog string is still called, and hard delete returns `STORE_OK` in the same test.

### 2026-10-13 — second-opinion deep review (stage 3)

**Verdict:** stage 3 approved after one grill-decided fix. No open blocking item.

**Run, not read.** Gate: ctest 4/4 (ASan/UBSan), LINT OK, coverage 100% functions and 100% effective lines, `leaks-macos` PASS, `ci-linux.sh` OK. Probe 5 on the built binary:

- A fresh brain has a sidecar at mode 0600 with one 37-byte line, and there is no `devices` table.
- A deleted sidecar is re-minted at 0600, with a new id.
- A sidecar of garbage, an uppercase UUID, the id plus a second line, or empty → exit 1 `invalid device_id sidecar`, and the sha is unchanged. The id with one trailing `\n` or `\r\n` opens. The second-line case is the stage 1 forward item, now closed.
- With the sidecar missing in a `0555` folder → `cannot write device_id sidecar`, and no `.tmp` or partial file is left behind.
- A 0.2.0 v4 brain with its sidecar removed gets a new id, not the `devices` row value, and ends with no `devices` table.

**Blocking → fixed (Itay grill 2026-10-13: mint under the DB lock):** on an existing v5 brain whose sidecar was missing, open minted outside any lock. All processes used the same `<path>.device_id.tmp`, and `rename` overwrites. With 16 parallel CLI `add` runs per round, one round in five had one run fail with `cannot write device_id sidecar` (its temp file had already been renamed away). Another run wrote its row under an id the sidecar did not keep: a phantom writer in `version_vector`. Fresh create and v4 migrate were safe because they mint inside `BEGIN IMMEDIATE`.

- New `bind_current_identity`: a valid sidecar binds with no lock, so normal opens stay lock-free. Otherwise it takes `BEGIN IMMEDIATE` (the lock migrate takes), re-reads the sidecar through `ensure_device_identity`, mints only if the file is still missing, and commits.
- New `cli_parallel_opens_mint_one_sidecar_id` (gate suite `sync_delete`): 8 rounds of 16 real CLI processes. Each round must give 16 rows, none under an id other than the sidecar's, and no failed run. It went red in 2 of 3 runs before the fix (`15|0`, `15|1`, failures 2–3) and green in 5 of 5 after. A 10-round shell probe also had 0 bad rounds.
- The test spawns the CLI because forked children inherit SQLite's PRNG state: they mint the same UUID within the same millisecond, which hides the race. A 48×20 in-process fork version stayed green on the buggy code.

**Auto-fixed:** **important** — `write_sidecar` renamed without `fsync`. After a power loss the rename could land before the data, leaving an empty sidecar. E14.4 then refuses the brain and never re-mints it. It now calls `fsync(fd)` before close and rename.

**Nit (left for stage 6 help/docs):** `import` help still says "or register devices", and the `store_import` comment in `store.h` still says "never INSERTs foreign devices". Both are stale now that the table is gone.

**Nit (left):** `store_test_fail_sidecar_rename(int n)` uses only `n > 0`, so it is a flag.

**Praise:** about 180 lines of `devices` reconciliation are gone. `ensure_device_identity` now has three branches (valid, missing, invalid). `read_sidecar` checks the trailing `fgetc` against `EOF` and `ferror`. The replaced-tests list in Implementation Notes names every 0.2.0 test and gives the reason.

### 2026-10-13 — in-session deep review (stage 4)

**Verdict:** approve for the second-opinion pass. No open blocking item. No grill.

**Ran:** ctest 4/4 (ASan/UBSan), LINT OK, coverage 100% functions and 100% effective lines, `leaks-macos` PASS, `ci-linux.sh` OK. Device suite 6/6. Store unit 131/0. Facade byte-match for `device` and `device new` (ids masked).

**Auto-fixed:** **important** — `store_device_new` published the sidecar and then adopted the id. An allocation failure after the rename left the file on the new id and the handle on the old one, so a later `device` in that process would not print the sidecar. The duplicate now happens first; the pointer moves only after the rename. Bugbot reported that pre-fix order. The fix is in this commit.

**Nit (left):** the whole-plan must-pass row "facade byte-match for search after migrate" is not this stage's bullet. Store search-after-migrate and `facade_search` on a fresh v5 already exist.

**Nit (left):** parallel `device new` has no test of its own. It uses the stage 3 lock (`BEGIN IMMEDIATE`, `busy_timeout` 5000, same temp name). The stage 3 test is `cli_parallel_opens_mint_one_sidecar_id`.

**Nit (left for stage 6):** import help still says "register devices". README and SKILL.md still describe the device hatch.

**Praise:** the version-vector test mints once, then updates. The vector keeps `"A":1` and gains `"B":1`. Human `device new` is the new id and nothing else.

### 2026-10-13 — second-opinion deep review (stage 4)

**Verdict:** stage 4 approved after one grill-decided change. No open blocking item.

**Run, not read.** Gate: ctest 4/4 (ASan/UBSan), LINT OK, coverage 100% functions and 100% effective lines, `leaks-macos` PASS, `ci-linux.sh` OK. Probes on the built binary:

- `device` prints A. `--json` prints `{"version":1,"device_id":"A"}`.
- `--json device new` prints `{"version":1,"device_id":"B","previous":"A"}`. Every `version_vector` is byte-identical before and after.
- The next `update` makes the vector `{"A":1,"B":1}`: B is added and A's counter is kept (probe 6).
- Human `device new` prints only the new id. `help device` describes `new`, `previous`, and the version vector.

**Grill (Itay 2026-10-13): `device new` always mints and replaces an invalid sidecar.** Before, an invalid sidecar failed every command, `device new` included, because open fails first. The only recovery was deleting the file by hand, and nothing told the user. This amends E14.4 and grill item 12 for `device new` only. Every other open, including `device` and `device new extra`, still exits 1 with `invalid device_id sidecar` and leaves the file unchanged.

- `store_open_replacing_sidecar` sets `Store.replace_invalid_sidecar` (explicit tail pad, kept `-Wpadded`-clean). In that mode `ensure_device_identity` treats an invalid file like a missing one: it mints over it under the same lock as a missing-sidecar open. `store_device_new` no longer refuses an invalid file. It replaces it and reports no previous id.
- `open_store` in `remember_app.c` uses that opener only when `cmd_device_mints()` (exported from `cmd_device.c`, so the `new` rule lives in one place) sees exactly `device new`. A bad argument list never touches the file.
- `previous` follows D4: open mints over the bad file first, so the CLI reports that id. `null` stays type-only.
- `help device` gains "It also replaces an invalid sidecar (no other command does)."
- Tests: `store_device_new_invalid_sidecar_is_unchanged` was replaced by `store_device_new_replaces_an_invalid_sidecar`, which also checks that plain `store_open` still refuses and that the device-new opener mints over the bad file. New `device_new_replaces_an_invalid_sidecar` (CLI): `device` and `device new extra` exit 1 with the file still `GARBAGE`, `device new` exits 0 and the sidecar equals the reported id, and a later `device` prints it.
- For stage 6: record this amendment in the 006 Implementation Results (E14.4, E14.6). SKILL.md should say "`device new` also repairs an invalid sidecar".

**Praise:** `store_device_new` takes the stage 3 write lock, so it cannot race a missing-sidecar open on the shared `.tmp`. The new id is duplicated before the rename, so an allocation failure leaves the old file. The facade byte-match masks ids (P6). `STORE_DEVICE_ID_LEN` is the single source of the UUID length.

### 2026-10-14 — in-session deep review (stage 5)

**Verdict:** approve for the second-opinion pass. No open blocking item. No grill.

**Ran:** ctest 4/4 (ASan/UBSan; `install_skill` outside the sandbox), LINT OK, coverage 100% functions and 100% effective lines, `leaks-macos` PASS, `ci-linux.sh` OK. Import suite 32/0. Store unit 132/0. Bugbot reported no findings.

**Nit (left):** `probe_count` discards the count. Zero and one are both success, because this stage does not copy the rows. The v4 test (objects missing) and the v5 test (rows present, not copied) lock that.

**Praise:** one string for `database is newer than this remember`. The version gate returns before any write on the destination. The source check compares file bytes, not only `user_version`.

### 2026-10-14 — second-opinion deep review (stage 5)

**Verdict:** stage 5 approved. No open blocking item. No code change.

**Run, not read.** Gate: ctest 4/4 (ASan/UBSan), LINT OK, coverage 100% functions and 100% effective lines, `leaks-macos` PASS, `ci-linux.sh` OK. Probes on the built binary:

- `import --from-db` of a `user_version` 6 source → exit 1 `database is newer than this remember`, and the destination stays empty. A v3 source → `source database is older than this remember`.
- A real 0.2.0 v4 brain as the source → `inserted 4`. The source sha and mtime are unchanged, it stays at `user_version` 4, and no sidecar is created beside it.
- A v5 source with a `settings` row and a `bucket_prefixes` row → imported. The source and its sidecar are byte-unchanged, and the destination `settings` and `bucket_prefixes` stay empty.
- A non-database file or a missing path as the source → exit 1 `database error`, and no file is created. The vague text is plan 16's error work.

**Grill (Itay 2026-10-14):** `probe_import_source` runs three read-only schema counts and discards each answer (`(void)n`), so today it changes no outcome. Itay chose to **keep it as scaffolding**. Plans 19 and 21 replace `(void)n` with a real use when they copy `bucket_prefixes` / `settings` / `conflicts.origin` rows.

**Nit (left, plan 16):** the refusal of a v6 source says "database is newer than this remember" without naming the source, so a user may think their own brain is too new. The text is locked (E1.5 / stage 5 "same text"). Plan 16's `detail.path` can carry the source path.

**Praise:** one `k_newer_database` string backs both open and import, so the two messages cannot drift. The gate is ordered so the probe never runs on a version this build does not understand. A failed probe returns before `BEGIN IMMEDIATE`.

### 2026-10-14 — in-session deep review (stage 6)

**Verdict:** approve for the second-opinion pass. No open blocking item. No grill.

**Ran:** ctest 4/4 (ASan/UBSan), LINT OK, coverage 100% functions and 100% effective lines, `leaks-macos` PASS, `ci-linux.sh` OK. The import-help assertion failed first on `schema v4+` and `register devices`, then passed after the help change.

**Auto-fixed:** **nit** — the What's New Hebrew line said "folded out of search", which can be read as query-side folding. It now says "folded out of the search index". Query-side folding stays plan 18.

**Bugbot:** two runs died before any findings (connection interrupted). A manual DRY and orthogonality read of the diff replaced that pass. No new logic. The same sentences sit in help, SKILL.md, What's New, the `store_open` bullet, and the 006 Implementation Results because each of those readers was named for this stage.

**Left for the closer:** `implementation-plans/INDEX.md` stays unmarked until the second review closes the plan. The v0.2.0 What's New section still describes the single-device hatch as that release's behavior.

**Praise:** import help no longer mentions a device registry, and the test fails if that phrase returns. 006 Design is unchanged. The two amendments are copied from the locked decision keys.

## Pragmatic write-gate
- DRY: PASS
- Easy-to-replace: PASS
- Orthogonal: PASS
- Deferred markers added: none
