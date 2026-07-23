# 02 — Store port + SQLite schema (sole SQLite unit)

## Goal

Introduce the **persistence port** and the **only** SQLite adapter. Create schema, pragmas, `user_version`, path mkdir, sync-volume warning hook.

## Deliverables

| File | Role |
|------|------|
| `src/store.h` | **Port:** opaque `typedef struct Store Store;`, domain types (`Entry`, `TagList`, `StoreError`), API below |
| `src/store_sqlite.c` | **Only file** that includes `sqlite3.h` / amalgamation |
| `third_party/sqlite/` | Amalgamation drop-in (pin version in README) |
| `src/store_types.h` | Optional: entry structs with no SQLite types |

### Port API (minimal; grow in later steps)

```c
Store *store_open(const char *path, char *err, size_t errlen); /* creates parent dir */
void   store_close(Store *s);

/* schema user_version handled inside open */
/* later steps add: store_add, store_get, store_list, store_search, store_update, store_delete */
```

Start with `store_open` / `store_close` only if commands aren't ready; prefer adding write/read as soon as step 04 needs them — **still only in this .c file**.

### Schema (inside adapter)

- Tables: `entries` (with nullable `key`), `tags`, `entry_tags`, `entries_fts` (standalone FTS5).
- Partial unique indexes: key WHERE NOT NULL; body_hash WHERE key IS NULL.
- `PRAGMA foreign_keys=ON`, `busy_timeout=5000`, default journal (no WAL).
- `user_version`: 0→create→1; 1→ok; >1→error string for CLI exit 1.
- FTS: `rowid = entries.id` set explicitly; **no triggers** (sync functions live here, called by store mutators).

### Path policy

- Resolve default elsewhere (`util` / `cli`); adapter receives final path.
- Sync warning: implement in `util` or `cli` (string markers) so adapter stays pure open — **or** in open with a callback. Prefer **util** `path_looks_synced()` called from commands/main so store stays “open this path”.

## Pragmatic notes

- **Replaceable:** commands never see `sqlite3 *`.
- **DRY:** one `fts_resync(store, entry_id)` used by add/update/delete.
- **Orthogonal:** SQL schema changes only touch `store_sqlite.c` (+ tests).

## Tests required this step

Open/create alone cannot green most CLI tests. Implement scaffolding; **must-pass moves to later steps** once commands exist:

| Test | Pass by step |
|------|----------------|
| `schema_user_version_too_new_refused` | 04+ (needs open path) / green with any command |
| `db_parent_dir_mode_0700` | 04 (add creates nested parent 0700) |
| `sync_path_warning_on_clouddocs_marker` | 04/09 |
| Orphan-tag GC (inspect via sqlite3 CLI) | 05 (delete) |

**Must pass by end of 02:** amalgamation links; `store_open`/`store_close` compile; optional smoke.

## Done checklist

- [x] Amalgamation builds with relaxed warnings on that TU only
- [x] `store_open` creates schema at user_version 1
- [x] Grep: no `sqlite3` outside `store_sqlite.c` / third_party
- [x] Format + compile clean

## Review notes (2026-07-22)

- **Port:** `store.h` exposes only `store_open` / `store_close` (opaque `Store *`). Domain mutators land in later steps.
- **Amalgamation:** SQLite **3.53.3** in `third_party/sqlite/`; CMake target `sqlite3` with `SQLITE_ENABLE_FTS5`, compiled with `-w`.
- **Schema:** `entries` (nullable `key`), partial uniques `ux_entries_key` / `ux_entries_bodyhash`, `tags`, `entry_tags` (CASCADE FKs), standalone `entries_fts` (`unicode61 remove_diacritics 2`, no triggers).
- **Open path:** parent dirs `0700`, `foreign_keys=ON`, `busy_timeout=5000`, default journal (no WAL), `user_version` 0→create→1 / 1→ok / >1→`"database is newer than this remember"`.
- **Tests:** unit suite `store` (7 cases) in `tests/test_store.c`; ctest gate is now `--only cli_global,store`.
- **Harness:** `sqlite3_query_line` shell-quotes SQL with double quotes so SQL string literals work (was breaking inspect queries).
- **Lint boundary:** `scripts/lint-all.sh` allows SQLite only in `store_sqlite.c` (was `db.c`).
- **Deferred to later steps:** CLI wiring of `store_open`, sync-path warning, mutators, FTS resync helper.

## Review notes (2026-07-23) — formal step review

**Verdict: Approve with nits** (no blockers for starting step 03).

### Important (fix soon; OK before or early in a later store touch)

1. **Schema create is not transactional.** `k_schema_sql` runs as a multi-statement `sqlite3_exec` without `BEGIN`/`COMMIT`. If create fails after some `CREATE`s but before `PRAGMA user_version=1` (or process dies mid-create), the file is left at `user_version=0` with partial objects; the next `store_open` retries create and fails permanently (`table entries already exists`). Wrap DDL in a single transaction (or delete/recreate on failed create).
2. **`ensure_parent_dirs` treats any `EEXIST` as success.** If a path component exists as a **file**, `mkdir` returns `EEXIST` and we continue; later open fails with a less clear error. Prefer `stat` + require `S_ISDIR` on `EEXIST`.

### Nits

- `store_open` error paths repeat `sqlite3_close` + `free(s)` — `goto cleanup` would reduce drift when mutators add resources.
- Plan table mentioned `Entry` / `TagList` / `StoreError`; YAGNI open/close-only API is fine — grow types with mutators (step 04).
- Unit tests assert object *names*, not partial-index `WHERE` clauses or FTS tokenize string (could inspect `sqlite_master.sql`).
- No automated assert for DB file mode `0600` (compile flag verified manually).
- Empty-string path covered via same branch as NULL message; optional explicit test.

### Confirmed OK

- Port boundary: only `store_sqlite.c` includes SQLite; lint gate enforces it.
- Schema matches design (tables, partial uniques, FTS5 tokenizer, no triggers).
- Pragmas + version gate + parent `0700` + file `0600` (probe) + recommended compile flags.
- Gates: build, `cli_global,store` (23), lint all green.

## Review follow-ups applied (2026-07-23)

- Schema create is **transactional** (`BEGIN IMMEDIATE` → DDL → `user_version=1` → `COMMIT`; `ROLLBACK` on failure without clobbering err).
- Parent mkdir: `EEXIST` requires `S_ISDIR`.
- `store_open` uses **`goto cleanup_fail`**; pattern documented in `docs/engineering-notes.md` as project-wide rule.
- Tests: partial-index `WHERE` + FTS tokenize via `sqlite_master.sql` LIKE; DB file mode `0600`; empty path; parent component is a file.

## Second review round (2026-07-23) — applied

### Fixed

1. **Create race (TOCTOU).** `user_version` was read *before* `BEGIN IMMEDIATE`, so two `remember` processes on a fresh DB both saw 0; the loser of the write lock then ran `CREATE TABLE` into a populated database and failed to open. `ensure_schema` now re-reads the version **under the write lock** and falls through to the normal version gate when another process won. Regression test `store_open_concurrent_create_all_succeed` (4 forked racers): 3/4 fail without the fix, 4/4 pass with it.
2. **Transactional create had no test.** `store_open_rolls_back_partial_create` pre-creates `tags` at `user_version=0` so the DDL fails part-way, then asserts `entries` is absent and the version is still 0 — i.e. the file is still openable. Fails without the transaction.
3. **Temp-dir cleanup was one level deep.** `remove_temp_dir` `unlink`ed direct children only, so `<tmp>/nested/t.db` from the parent-dir test survived every run (17 dirs × 52 KB found in `/tmp`). Now recurses depth-first. A full suite run leaves 0 behind.
4. **`sqlite3_close` → `sqlite3_close_v2`** in `store_close` / `store_open`. Plain `close` returns `SQLITE_BUSY` and leaks the connection if a prepared statement is outstanding — which mutators (steps 04/05) will have.
5. **`ensure_parent_dirs` rewritten** on a `PATH_MAX` stack buffer: no `malloc`, no duplicated `free`/`return` teardown (it was violating the `goto cleanup` rule this step introduced), and a new "database path is too long" guard.
6. **Real fd leak in the harness.** `run_remember` returned early when the second `pipe()` failed, leaking the first pair. All failure paths now `goto cleanup`.
7. **`sqlite3_query_line` → `harness_sqlite_query_line`.** The old name squatted SQLite's reserved prefix in a binary that links the amalgamation. It also shell-**double**-quoted SQL, leaving `$`, backtick and `\` live; SQL is now single-quoted with `'\''` escaping.
8. **Sanitizer coverage.** `store_sqlite.c` was only exercised by the uninstrumented `remember_tests`. New `remember_store_tests` target runs the store units directly under ASan/UBSan (`store_asan_gate` in ctest).

### Store suite: 10 → 18 tests (88 asserts)

Added: negative `user_version`, non-database file, path too long, unwritable parent (non-`EEXIST` `mkdir` failure), NULL / zero-length `err` buffer, error-message truncation, `ON DELETE CASCADE` on `entry_tags`, plus the two regression tests above.

### Carried to step 04

- **`--db` path policy owns `:memory:` and `file:` strings.** `store_open` passes the path straight to `sqlite3_open_v2`, so `--db :memory:` currently yields a store that silently discards everything on close. Decide in the CLI layer (reject, or document).
- Sync-path warning (`path_looks_synced()` in `util`, called from `main`) is still deferred.
- Once the CLI calls `store_open`, the black-box suite covers the adapter under ASan too — `store_asan_gate` stays as the fast unit-level gate.
