# 16 — Actionable errors, warnings, open flags, `repair search-index` (006 §3)

## Goal

Every failure names its cause and a fix. Users can fix what they can name;
apps and agents get a machine-readable code. Concretely:

- `--json-errors` writes one JSON object per error or warning on stderr.
- A closed catalog of error codes, with SQLite failures split into seven
  causes (`busy`, `read_only`, `disk_full`, `cannot_open`, `not_a_database`,
  `corrupt`, `io_error`) plus `database`.
- The plain line keeps the `database error` prefix and adds cause + fix.
- Warnings on exit 0 (`remember: note: …`), used first by search (plan 18).
- New global flags `--busy-timeout MS` and `--no-create`.
- New command `remember repair search-index`.
- A test-only failing VFS shim, so every SQLite cause is tested.

## Source of truth

[`design-logs/006-ui-overhaul-engine.md`](../design-logs/006-ui-overhaul-engine.md)
§0 (EN-01..EN-04), §3 (E4.1–E4.15, X-E02), §2 E3.2 (real errors are never
swallowed). Do not invent. If 006 does not decide a case, stop and ask Itay
(grill), then record the answer under Implementation Notes.

## Context (standalone summary)

Baseline: `main` at `a27efca` with plan 15 (schema v5) merged.

Today:

- Every SQLite failure becomes `STORE_ERR_SQLITE` →
  `remember: database error`. The SQLite code and message are dropped.
- `store_open(path, err, errlen)` returns `NULL` plus a string.
- Errors go to stderr as plain text through `err_msg()`
  (`src/commands_common.c`) and through `fprintf(app_err(), …)` in
  `src/remember_app.c` (parse errors, open errors, missing command).
- `apply_pragmas` sets `busy_timeout = 5000`; journal is the default rollback
  journal (not WAL; keep it).
- `ensure_parent_dirs` creates missing folders (`mkdir -p`, `0700`), and
  `sqlite3_open_v2(..., READWRITE | CREATE)` creates a new empty brain.
- Exit codes: 0 ok, 1 usage/error, 2 not found, 3 wrong bin.

Rules this plan implements (restated from 006):

| ID | Rule |
|----|------|
| E4.1 | Global `--json-errors`: an error writes exactly one stderr line `{"version":1,"error":{"code":"<code>","message":"<text>"}}` and no plain line. |
| E4.2 | Without the flag, stderr is as in 0.2.0, except real database errors (E4.8). |
| E4.3 | Codes: `usage`, `not_found`, `database`, `out_of_memory`, `internal`, `invalid_query`, `key_conflict`, `body_conflict`, `expired`, `not_expired`, `deleted`, `not_deleted`, `not_single_device`, `self_link`, `cycle`, `source_too_old`, `unique_taken`, `stale`, `newer_database`, `unsupported_database`, `import_parse`, `busy`, `read_only`, `disk_full`, `cannot_open`, `not_a_database`, `corrupt`, `io_error`. |
| E4.4 | `message` = the plain line without `remember: `. |
| E4.6 | Exit 0 may write one warning per notice. Plain `remember: note: <text>`; with `--json-errors` `{"version":1,"warning":{"code":"<code>","message":"<text>"}}`. Never with a non-zero exit. Only code in 0.3.0: `raw_query_ignored`. |
| E4.7 | Keep `sqlite3_extended_errcode()`, `sqlite3_errmsg()`, `sqlite3_system_errno()` at the point of failure; map for every command and for open; first matching row wins (table below). |
| E4.7 | `cannot_open` carries `detail.reason`: `permission` (EACCES), `missing` (ENOENT), `is_directory` (ISDIR), else `other`. A readable but unwritable file is `read_only` at the first write; no writability probe at open. |
| E4.8 | Plain line: `remember: database error: <cause> (<SQLITE_NAME>). <fix>` with the extended name (table below). |
| E4.9 | JSON `detail` (optional): `sqlite_code`, `sqlite_extended`, `sqlite_name` (always for SQLite errors); `sqlite_message`; `errno` (only non-zero); `path` (only for `read_only`, `disk_full`, `cannot_open`, `not_a_database`, `corrupt`, `io_error`; the absolute brain path); `reason` (`cannot_open` only); `fts_only` (`corrupt` only). |
| E4.10 | Never repair on its own. On `corrupt`: `PRAGMA quick_check` on `entries`, `entry_links`, `conflicts`, `settings`, `bucket_prefixes`. The failing statement touched `entries_fts` when its SQL contains `entries_fts`. Check passes and statement touched FTS, or code is CORRUPT_VTAB → `fts_only` true. |
| E4.11 | Global `--no-create`: a missing file fails with `cannot_open`, `reason` `missing`; creates nothing. |
| E4.12 | Exit codes unchanged. On an error, stdout is empty. |
| E4.13 | Global `--busy-timeout MS` (0–60000, default 5000) sets `PRAGMA busy_timeout`. Out of range → exit 1, `usage`, `invalid --busy-timeout (must be 0..60000)`. No own retry loop. |
| E4.14 | `repair search-index`: `quick_check` on base tables (refuse with `corrupt`, `fts_only` false, if they fail), then `INSERT INTO entries_fts(entries_fts) VALUES('rebuild')` in one transaction, then `VALUES('integrity-check')`. Output `{"version":1,"action":"rebuilt","entries":N}`, N = rows in `entries` (every bin). |
| E4.15 | SCHEMA (17): prepare the same SQL, bind the same values, run once more; a second failure is `database`. |
| E3.2 | A real FTS or SQLite error is never swallowed: exit 1, empty stdout, E4.7 code and E4.8 text. Usage errors stay exit 1 `usage`. |
| X-E02 | Test-only VFS shim wraps `unix`: `REMEMBER_TEST_FAIL_WRITE=N` (IOERR_WRITE after N writes), `REMEMBER_TEST_FAIL_READ=N` (IOERR_READ), `REMEMBER_TEST_ENOSPC=1` (FULL), `REMEMBER_TEST_NOMEM=1` (IOERR_NOMEM). Only test builds compile it; release binaries and apps never contain it. |

**E4.7 mapping (first match wins)**

| SQLite result | Code |
|---|---|
| BUSY 5, BUSY_RECOVERY 261, BUSY_SNAPSHOT 517, BUSY_TIMEOUT 773, LOCKED 6, LOCKED_SHAREDCACHE 262, LOCKED_VTAB 518, PROTOCOL 15 | `busy` |
| READONLY_DBMOVED 1032 | `cannot_open` |
| READONLY 8, READONLY_RECOVERY 264, READONLY_CANTLOCK 520, READONLY_ROLLBACK 776, READONLY_CANTINIT 1288, READONLY_DIRECTORY 1544 | `read_only` |
| FULL 13 | `disk_full` |
| CANTOPEN 14 + NOTEMPDIR 270, ISDIR 526, FULLPATH 782, CONVPATH 1038, DIRTYWAL 1294, SYMLINK 1550; PERM 3 | `cannot_open` |
| NOTADB 26 | `not_a_database` |
| CORRUPT 11, CORRUPT_VTAB 267, CORRUPT_SEQUENCE 523, CORRUPT_INDEX 779, IOERR_SHORT_READ 522 | `corrupt` |
| IOERR 10 and every other IOERR_* (READ 266, WRITE 778, FSYNC 1034, TRUNCATE 1546, LOCK 3850, CORRUPTFS 8458, …), NOLFS 22 | `io_error` |
| SCHEMA 17 | retry once (E4.15), then `database` |
| everything else (NOMEM, IOERR_NOMEM, MISUSE, RANGE, MISMATCH, FORMAT, INTERNAL, AUTH, INTERRUPT, TOOBIG, unmapped CONSTRAINT, ERROR) | `database` |

**E4.8 text**

| Code | `message` | Cause | Fix |
|---|---|---|---|
| `busy` | brain is busy | the brain is locked by another process | Wait a moment and try again. Another app or a terminal may be using this brain. |
| `read_only` | brain is read-only | the brain is read-only | Give yourself write permission on the brain file and its folder, or move the brain to a writable folder. |
| `disk_full` | disk is full | disk is full | Free some space and try again. |
| `cannot_open` | cannot open brain | cannot open the brain file | Check that the folder exists and that you can read and write it. |
| `not_a_database` | not a remember brain | the file is not a remember database | Point --db at a remember brain, or move this file away. |
| `corrupt` | brain is damaged | the brain file is damaged | Copy the file somewhere safe before anything else, then run `remember export`, or restore a backup. |
| `corrupt` + `fts_only` | search index is damaged | the search index is damaged | Run `remember repair search-index`. Your memories are not affected. |
| `io_error` | disk error | the disk reported a read or write error | Check the disk or drive, then try again. |
| `database` | database error | — | plain: `remember: database error (<SQLITE_NAME>). Try again. If it keeps happening, report the details from --json-errors.` |

## Prerequisites

- `main` clean and up to date.
- **Plan 15 merged** (the E4.10 check covers its `settings` and
  `bucket_prefixes` tables). If it is not, stop.

## Scope

**In:** error-code catalog (one table: code ↔ string); a single diagnostics
emitter for errors and warnings (plain and JSON); every existing stderr error
path routed through it with the right code; structured store errors (no
SQLite types in `store.h`); E4.7 mapping, E4.8 text, E4.9 detail; SCHEMA retry;
corrupt check + `fts_only`; `--json-errors`, `--busy-timeout`, `--no-create`
(parse, help, facade); `repair search-index` command; VFS shim (test builds
only); tests for each acceptance case; help/skill/What's New.

**Out:** the `raw_query_ignored` warning call site (plan 18 uses the emitter
built here); `stale` (plan 17), `import_parse` (plan 21) call sites; any
retry loop; WAL; changing exit codes.

## Locked decisions (grill 2026-10-12; index in [`006-locked-decisions.md`](006-locked-decisions.md))

| # | Decision |
|---|----------|
| D5 | Two more warning codes. `deprecated_flag` is the `--trash` / `purge-trash` deprecation line. `synced_folder` is the iCloud/Dropbox/Google Drive line. Plain text is unchanged. With `--json-errors` each becomes `{"version":1,"warning":{"code":"<code>","message":"<text>"}}`. These two and `raw_query_ignored` (plan 18) are the only warning codes. (Amends E4.6.) |
| D6 | SQLite NOMEM (7) and IOERR_NOMEM (3082) → `database`; this check runs **before** the IOERR row. Engine allocation failures (`STORE_ERR_OOM`, `NORM_ERR_OOM`, any malloc failure in cmd/output) → `out_of_memory`, plain `remember: out of memory` (unchanged text). |
| D7 | `detail.path` is absolute: join a relative path with the cwd and normalize `.` and `..` lexically, with no symlink resolution. The same rule applies whether the path came from `--db`, `REMEMBER_DB`, `default-db` or the built-in path. |
| D8 | `--no-create` checks the file before `ensure_parent_dirs`: no folder, no file, no sidecar. Human `repair search-index` prints `rebuilt N`. |
| D9 | Code table for 0.2.0 messages (below). A message it does not cover → stop and ask. |
| P3 | The shim's CMake option is `REMEMBER_TEST_VFS` (default OFF). |
| P6 | `repair` gets a registry row, a help topic and a facade byte-match test. |

**D9 code table**

| Code | Messages (0.2.0 text unchanged) |
|------|----------------------------------|
| `usage` | every argv/flag parse error (`unknown option`, `option requires a value`, `missing value for --key`, `unexpected argument`, `too many arguments`, `missing command (try --help)`, `unknown command`, `… takes no arguments`, `… requires …`, `usage: conflict accept …`); every `cannot combine …`; `invalid id`, `invalid sync-id`, `invalid conflict id`, `invalid --limit (must be 1..1000)`, `invalid --offset (must be >= 0)`, `invalid --ttl`, `invalid --expires`, `invalid source (…)`, `unknown kind (…)`, `--keep must be …`, `--source is only valid on add`, `too many --tag filters (max 50)`, `graph commands use --deleted …`, `provide …`, `missing …`, `each end needs …`, `duplicate --…`; body/token normalize errors (`missing body`, `empty body after trim`, `body exceeds 64 KiB`, `invalid UTF-8 in body`, `invalid UTF-8`, `invalid character`, `too long`, `empty`, `stdin input too large`); `missing search query`; `HOME is not set; pass --db PATH`; `database path is too long`; `empty database path`; `database path must be a regular file (not :memory: or file: URI)` |
| `not_found` | `not found` (StoreStatus NOT_FOUND) |
| `expired` / `not_expired` / `deleted` / `not_deleted` | the four bin StoreStatus values |
| `self_link` | `self-link` |
| `cycle` | `supersedes cycle` |
| `source_too_old` | `source database is older than this remember` |
| `unique_taken` | `cannot free unique key or body hash without inventing a key` |
| `body_conflict` | `body hash conflict` (keyless body-hash collision) |
| `key_conflict` | a keyed collision on update / rekey (the message that names the other id) |
| `newer_database` | `database is newer than this remember` |
| `unsupported_database` | `unsupported database version` |
| `cannot_open` (`reason` `other`) | `database path component is not a directory` |
| `corrupt` | `migration left a dangling reference` |
| `internal` | `internal error`, `internal error: path buffer missing`, `internal argument error`, `store error`, `unknown error`, `unknown normalize error`, `cannot read clock`, `cannot mint sync_id`, `cannot mint device_id`, `cannot write device_id sidecar`, `invalid device_id sidecar`, `failed to read body from stdin`, `failed to write output` |
| `out_of_memory` | `out of memory` (D6) |
| E4.7 table | `database error` and every `cannot open database …` (now mapped per SQLite cause) |

`empty search query` stays `usage` until plan 18 makes a blank search succeed.

**Observed facts (Linux, vendored SQLite 3.53.3, run as uid `nobody`; use
them as the test expectations, and record the macOS values in
Implementation Notes — a different macOS value is a grill, not a choice)**

| Case | Open | First statement | Expected code / `reason` |
|------|------|-----------------|--------------------------|
| file mode 444 | OK | READONLY (8) | `read_only` |
| folder mode 555 (file 666) | OK | READONLY_DIRECTORY (1544) | `read_only` |
| file mode 000 | CANTOPEN (14), errno 13 | — | `cannot_open`, `reason` `permission` |
| folder given as path | CANTOPEN (14) (not 526), errno 21 EISDIR | — | `cannot_open`, `reason` `is_directory` |
| text file as path | OK | NOTADB (26) | `not_a_database` |

So `reason` is derived from `sqlite3_system_errno()` (EACCES → `permission`,
ENOENT → `missing`, EISDIR → `is_directory`) and from the extended code
CANTOPEN_ISDIR (526) → `is_directory`; anything else → `other`. Capture the
errno right after the failing call.
Anything 006 and this section do not decide: stop and ask Itay. Do not choose.

## Files

New `src/diag.c/.h` (code enum + string table, `diag_error`,
`diag_warning`, plain/JSON writers, E4.8 cause/fix table; wire in
`REMEMBER_LIB_SOURCES` + `-Wcast-qual`), `src/store.h` (`StoreError` struct:
class enum, `int sqlite_code`, `int sqlite_extended`, `char sqlite_name[]`,
`char sqlite_message[]`, `int sys_errno`, `bool fts_only`, `reason` enum;
`store_last_error(const Store *)`; `store_open_opts(path, const
StoreOpenOpts *, StoreError *)` with `busy_timeout_ms`, `no_create`;
`store_repair_search_index`), `src/store_sqlite.c` (capture at the point of
failure, mapping, extended-name table, SCHEMA retry helper, quick_check,
repair), `src/commands_common.c` (`err_msg` → coded variants; StoreStatus →
code table next to `store_status_message`), `src/remember_app.c` (globals,
open errors, parse errors), `src/cli.c/.h` (new globals, `repair` command
with `search-index` sub-arg), `src/output.c` (JSON string escaping reused by
diag), new `src/test_vfs.c` compiled only when `REMEMBER_TEST_VFS=ON`,
`CMakeLists.txt` (option, test-only source, never in `remember_core`,
`remember_plain`, release), `scripts/install.sh` (refuse a binary that links
the shim, like the asan check), tests: new `tests/test_errors.c` (suite
`errors`), `tests/test_facade.c`, `tests/test_cli_global.c`,
`tests/gate-suites`, `tests/COVERAGE.md`; docs: help, `skills/remember/SKILL.md`,
`README.md` What's New, `docs/engineering-notes.md`, `docs/QUALITY.md` (shim).

## Must-pass tests

| Check | Proves |
|-------|--------|
| `--json-errors` + usage error → exactly one stderr line, valid JSON, `code` `usage`; stdout empty; exit 1 | E4.1, E4.12 |
| Without the flag every pre-0.3.0 error line is byte-identical (golden list of current messages) except database errors | E4.2 |
| Every code in E4.3 appears exactly once in the catalog; a test round-trips enum ↔ string | E4.3 single source |
| `message` equals the plain line minus `remember: ` for a sample of every code | E4.4 |
| chmod 444 file + write → `read_only`, `detail.path` absolute, plain line matches E4.8 | E4.7/8/9 |
| chmod 555 folder + write → `read_only` (READONLY_DIRECTORY or CANTOPEN per SQLite; record the observed name) | acceptance |
| chmod 000 file → `cannot_open`, `reason` `permission` | acceptance |
| Missing path + `--no-create` → `cannot_open`, `reason` `missing`; no file, no folder, no sidecar created | E4.11 |
| Folder given as path → `cannot_open`, `reason` `is_directory` | acceptance |
| Text file as path → `not_a_database` | acceptance |
| Truncated brain → `corrupt` | acceptance |
| Held write lock + `--busy-timeout 500` → `busy` within ~0.5 s | E4.13 |
| `--busy-timeout 60001` / `-1` / `abc` → `usage`, `invalid --busy-timeout (must be 0..60000)` | E4.13 |
| `REMEMBER_TEST_ENOSPC=1` → `disk_full`; `REMEMBER_TEST_FAIL_WRITE=N` / `_READ=N` → `io_error` with `sqlite_name`; `REMEMBER_TEST_NOMEM=1` → `database` (`SQLITE_IOERR_NOMEM`, D6) | X-E02 |
| `--json-errors` + `--trash` → one JSON warning `deprecated_flag`; `--db` under a `Dropbox` path → JSON warning `synced_folder`; plain text unchanged without the flag | D5 |
| Engine OOM (`REMEMBER_TEST_HOOKS` alloc fault) → `out_of_memory` | D6 |
| Relative `--db sub/x.db` → `detail.path` = cwd + `/sub/x.db`, normalized, symlinks kept | D7 |
| Damaged FTS shadow table (corrupt `entries_fts_data`) → exit 1, empty stdout, `corrupt`, `fts_only` true; then `repair search-index` → `{"version":1,"action":"rebuilt","entries":N}`; search works again | E4.10, E4.14 |
| Damaged base table → `repair search-index` refuses with `corrupt`, `fts_only` false | E4.14 |
| `detail` member order and presence rules (no `errno` when 0; `path` only for the six codes; `reason` only `cannot_open`; `fts_only` only `corrupt`) | E4.9 |
| SCHEMA retry: a schema change between prepare and step (second connection) is retried once and succeeds; a forced second failure → `database` | E4.15 |
| `get --key nope` → `not_found`, exit 2 (plain line unchanged) | acceptance |
| Warning emitter: plain `remember: note: x`; JSON warning line; never with non-zero exit (unit test with a fake notice) | E4.6 |
| Release build (`remember_plain`, `remember_core`, Release tree) contains no shim symbols (`nm` check in a ctest) | X-E02 |
| Facade: `remember_run` stderr bytes equal the CLI's for a usage error, a `not_found`, a `busy` case, and with `--json-errors` | EN-03 |

## Stages

TDD, one failing test at a time. After each stage: gate green, in-session deep
review, Implementation Notes, commit.

1. **Catalog + emitter.** `diag.c`: code enum, string table, plain + JSON
   writers (reuse `output.c` escaping), warnings. Unit tests. No call sites
   change yet.
2. **Route every error.** `--json-errors` global (parse anywhere, like
   `--json`). Replace `err_msg` and the `fprintf(app_err(), …)` error lines
   with coded calls (D9 table). Golden test proves plain bytes unchanged.
   Route the deprecation and synced-folder lines through `diag_warning`
   with `deprecated_flag` / `synced_folder` (D5).
3. **Structured store errors.** Capture code/extended/message/errno where
   SQLite fails (one helper; never after another SQLite call clobbers it).
   Mapping + extended-name table in the adapter only (D6 before the IOERR
   row). `store_open_opts`. D7 absolute path helper in `util.c`.
   E4.8 text, E4.9 detail, absolute path. SCHEMA retry helper.
4. **VFS shim + acceptance matrix.** `REMEMBER_TEST_VFS` CMake option,
   `test_vfs.c`, registration as default VFS in test builds only. Write the
   acceptance tests for every row (chmod cases skip when running as root;
   say so in the test output, and run them in CI as a non-root user).
5. **Open flags.** `--busy-timeout`, `--no-create` (before any mkdir).
6. **Corrupt check + repair.** E4.10 quick_check per table, `fts_only`;
   `repair search-index` command (registry, help, JSON/human, facade).
7. **Docs + full gate.** Help (globals, `repair`), SKILL.md (use
   `--json-errors`; read `code`; `busy` → retry later; `corrupt` +
   `fts_only` → `repair search-index`), What's New, engineering notes
   (diag emitter rule: never write an error line outside `diag`),
   QUALITY.md (shim), COVERAGE.md, gate-suites.

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

- SQLite only in `store_sqlite.c`. `store.h` exposes plain C (ints, names as
  strings, enums), never `sqlite3.h` types or macros.
- One source of truth per vocabulary: codes in `diag.c`, StoreStatus → code
  next to `store_status_message`, extended names + mapping in the adapter.
- Layers: `cli_parse` (pure argv) → `cmd_*` (no SQL) → `output.c` / `diag.c`.
- New TU: `REMEMBER_LIB_SOURCES` **and** `-Wcast-qual`; direct includes.
- `goto cleanup` single exit; capture the SQLite error before cleanup calls
  (finalize/rollback) can overwrite it.
- Write through `app_out()` / `app_err()` only.
- User-facing strings are ASCII (the E4.8 fix texts are ASCII; keep the
  backticks as in 006).
- Test-only code is behind a CMake option and can never reach a release.

**Quality gate** (before every commit that touches C; on macOS prefix
`PATH="$(brew --prefix llvm)/bin:$PATH"`)

```bash
cmake -S . -B build -DCMAKE_C_COMPILER=clang -DREMEMBER_ENABLE_SANITIZERS=ON -DREMEMBER_TEST_VFS=ON
cmake --build build
ctest --test-dir build --output-on-failure
./scripts/lint-all.sh
./scripts/check-coverage.sh                       # 100% functions + 100% effective lines
./scripts/ci-linux.sh                             # Linux parity; chmod cases run as non-root
cmake --build build --target leaks-macos          # macOS only
```

## Definition of Done

- [ ] Design log 006 unchanged except Implementation Results rows for
      E4.1–E4.15, E3.2, X-E02 (with the observed SQLite names for the chmod
      folder case).
- [ ] Every must-pass row green and in `tests/gate-suites`.
- [ ] No stderr error line is written outside the diag emitter (grep check
      in lint or a test).
- [ ] Plain stderr unchanged for non-database errors (golden test).
- [ ] Exit codes unchanged; stdout empty on every error.
- [ ] Shim absent from release, `remember_plain` and `remember_core`.
- [ ] Locked decisions D5–D9, P3, P6 implemented as written (D9 table is a test).
- [ ] Help, SKILL.md, README What's New (`--json-errors`, the seven causes,
      `--busy-timeout`, `--no-create`, `repair search-index`).
- [ ] Engineering notes + QUALITY.md updated.
- [ ] Gate: ctest, lint, coverage 100/100, `ci-linux.sh` all green.
- [ ] Second review done; no open **blocking** item.
- [ ] `implementation-plans/INDEX.md` row marked ✅.

## Second review handover

Hand this to an independent reviewer (a fresh session that did not write the
code). The reviewer runs `workflow:deep-review` if the brain is reachable,
else [`docs/STEP_REVIEW_CHECKLIST.md`](../docs/STEP_REVIEW_CHECKLIST.md),
and verifies by **running**, not reading.

**Package to hand over**

- Branch, PR link, commit range, this plan, 006 §3.
- Gate output (ctest, lint, coverage, `ci-linux.sh` as non-root).
- Locked decisions D5–D9, P3, P6, and any new grill answers.
- The observed `sqlite_name` for each acceptance case (some depend on the OS).

**Probe these (expected result in brackets)**

1. Run each acceptance case from the must-pass table by hand with and without
   `--json-errors` [code, detail, plain line as in the E4.8 table; stdout
   empty; exit 1].
2. `remember --json --json-errors get --key nope` [exit 2, `not_found`, one
   JSON line].
3. `remember --json-errors --busy-timeout 500 add x` while another process
   holds `BEGIN EXCLUSIVE` [`busy` after ~0.5 s; no retry loop].
4. `remember --no-create --db /tmp/none/x.db list` [`cannot_open`
   `missing`; `/tmp/none` not created; no sidecar].
5. Corrupt `entries_fts_data` with `sqlite3`, then `search x` [`corrupt`,
   `fts_only` true], `repair search-index` [rebuilt], `search x` [works].
6. Grep `src/` for `app_err()` writes outside `diag.c` [none for errors].
7. `nm build-release/remember | grep -i vfs` [no shim symbols].
8. Facade: run the facade suite and one manual `remember_run` with
   `--json-errors` [bytes equal].

**Hot spots**

- Error capture order: a `finalize`/`ROLLBACK` before reading
  `sqlite3_extended_errcode` loses the real code.
- `sqlite3_system_errno` is per connection and only meaningful right after
  the failing call.
- `--no-create` must run before `ensure_parent_dirs`.
- The SCHEMA retry must not double-apply a write.
- chmod tests pass vacuously as root.

**Do not re-open:** the code list, the mapping table, the plain-line format,
no own retry loop, no automatic repair, exit codes unchanged.

**Review output:** append `### <date> — second-opinion deep review (stage N |
FINAL)` under Review Notes. Tag each finding **blocking / important / nit /
praise**. Auto-fix small, local, gate-green issues and say so; send product
or architecture forks to Itay as a grill. Re-run the full gate after any fix.

## Implementation Notes

### 2026-10-15 — stage 1 (catalog + emitter)

No call sites changed. `err_msg` and the `fprintf(app_err(), …)` lines still write the 0.2.0 plain text. Stage 2 routes them.

- Error tokens are one array, E4.3 order, indexed by `DiagCode`. Warning tokens are a second array (`raw_query_ignored`, `deprecated_flag`, `synced_folder`). A warning token is not an error code. `diag_code_name` / `diag_warning_name` and the reverse lookups share one scanner.
- `diag_error` writes through `app_err()` only. Plain is `remember: <message>\n`. JSON is one line `{"version":1,"error":{"code":"<code>","message":"<message>"}}` and no plain line. `message` is escaped with `output_json_string`. Plain text is not escaped.
- `diag_warning` uses the same writer. Plain is `remember: note: <text>\n`. JSON is `{"version":1,"warning":{"code":"<code>","message":"<text>"}}`. `message` is `<text>`, not `note: <text>`. A non-zero exit writes nothing and returns 0. A code outside either catalog writes nothing and returns -1, including a warning whose exit is already non-zero.
- NULL message is an empty string, so a NULL cannot reach `printf("%s")`.
- E4.8 cause/fix text and E4.9 `detail` stay in stage 3, when the store error exists. This stage's writer takes the message from the caller.

## Review Notes

### 2026-10-15 — in-session deep review (stage 1)

**Verdict:** approve for the second-opinion pass. No open blocking item. No grill.

**Ran:** ctest 4/4 with ASan/UBSan, `lint-all.sh` LINT OK, coverage 100% functions and 100% effective lines, `leaks-macos` PASS, `ci-linux.sh` OK. A sanitizer probe checked percent signs, tab/CR/UTF-8, warning JSON quotes, a NULL warning message, exit `-1`, and a read-only error stream (plain, JSON, and warning all return -1). Bugbot on the uncommitted diff reported no bugs.

**Praise:** one string table per catalog and one writer. JSON goes through `output_json_string`. Warning tokens are not error codes. Unknown codes write nothing.

**Nit:** the memstream setup is repeated in each test. Left as is; it is the same shape as the other stream tests, and a helper would hide the arrange step.

**Ponytail:** nothing to cut. The padding byte and the `Notice` struct exist so `-Wpadded` and the swappable-parameter check stay quiet.

**Pragmatic write-gate:** DRY PASS, easy-to-replace PASS (`app_err` is the existing stream port), orthogonal PASS (no SQLite, no call-site edits). No deferred markers.

**Left for later:** stage 2 routes `err_msg`. Stage 3 adds E4.8 text and E4.9 `detail`.
