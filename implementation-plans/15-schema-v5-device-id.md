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

## Open questions (ask before the stage that needs them)

1. **E14.6 `previous: null`.** Open mints a sidecar when the file is missing
   (E14.3), so by the time `device new` runs the file exists. When can
   `previous` be `null`? Options: (a) `device new` skips the open-time mint
   and reports `null` when the file was missing at open; (b) the id minted by
   open counts as `previous`. Recommendation: (a), because it matches the
   wording "previous is JSON null when the file was missing". Stage 4 needs
   the answer.
2. **E14.4 code and exit.** The message is fixed. The E4.3 code arrives with
   plan 16. Until then: exit 1, plain `remember: invalid device_id sidecar`.
   Confirm that plan 16 maps it to `internal` (recommended) or to a new code.

## Files

`src/store_sqlite.c` (DDL, migrate, `fts_resync`, fold helper, sidecar,
device ports, hatch removal, import source probing), `src/store.h`
(`store_device_id`, `store_device_new`), `src/normalize.c/.h` (pure
`fold_hebrew_marks` if it is reused by plan 18; otherwise keep it static in
the adapter; decide per YAGNI), `src/cli.c/.h` (`device` in `k_commands[]`),
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
   test. Needs open question 1.
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

- [ ] Design log 006 unchanged except new rows in its Implementation Results
      for E1.2–E1.5, E10.1 (rows), E14.
- [ ] Every must-pass row green and included in `tests/gate-suites`.
- [ ] Fresh and migrated schemas are identical; `user_version` 5.
- [ ] No code path reads or writes a `devices` table; `require_single_device`
      is gone; `not_single_device` is never returned.
- [ ] Every key-changing path resyncs the FTS key column (test per path).
- [ ] `device` / `device new` work in human and `--json`; facade byte-match.
- [ ] Open questions 1–2 answered and recorded.
- [ ] Help, SKILL.md, README What's New (v0.3.0 section, one line per user-
      visible change: key is searchable, Hebrew marks folded, `device`,
      sidecar-only identity, hard wipe without the device hatch).
- [ ] `docs/engineering-notes.md` `store_open` bullet updated.
- [ ] Gate: ctest, lint, coverage 100/100, `ci-linux.sh` all green.
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
- Decisions taken (open questions 1–2 and any grill) with their answers.
- The list of replaced 0.2.0 tests and why each was replaced.

**Probe these (expected result in brackets)**

1. Copy a real v4 brain; open it with the new build; `sqlite3 x.db .schema`
   [no `devices`; FTS `key, body, tags`; `user_version` 5]; diff `.schema`
   with a fresh brain [identical].
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
- `previous` semantics (open question 1).

**Do not re-open:** sidecar is the only device id; `devices` is dropped and
not copied; FTS column order `key, body, tags`; tokenizer unchanged;
`not_single_device` stays in the catalog.

**Review output:** append `### <date> — second-opinion deep review (stage N |
FINAL)` under Review Notes. Tag each finding **blocking / important / nit /
praise**. Auto-fix small, local, gate-green issues and say so; send product
or architecture forks to Itay as a grill. Re-run the full gate after any fix.

## Implementation Notes

_(empty; fill per stage)_

## Review Notes

_(empty; second review appends here)_
