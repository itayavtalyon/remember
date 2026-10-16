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

No call sites changed. `err_msg` and the `fprintf(app_err(), …)` lines still write the 0.2.0 plain text. Stage 2 routes them. The warning prefix below is the pre-grill API; the stage 1 second-opinion review replaced `note:` with `warning: ` and dropped the exit gate.

- Error tokens are one array, E4.3 order, indexed by `DiagCode`. Warning tokens are a second array (`raw_query_ignored`, `deprecated_flag`, `synced_folder`). A warning token is not an error code. `diag_code_name` / `diag_warning_name` and the reverse lookups share one scanner.
- `diag_error` writes through `app_err()` only. Plain is `remember: <message>\n`. JSON is one line `{"version":1,"error":{"code":"<code>","message":"<message>"}}` and no plain line. `message` is escaped with `output_json_string`. Plain text is not escaped.
- `diag_warning` uses the same writer. Plain is `remember: note: <text>\n`. JSON is `{"version":1,"warning":{"code":"<code>","message":"<text>"}}`. `message` is `<text>`, not `note: <text>`. A non-zero exit writes nothing and returns 0. A code outside either catalog writes nothing and returns -1, including a warning whose exit is already non-zero.
- NULL message is an empty string, so a NULL cannot reach `printf("%s")`.
- E4.8 cause/fix text and E4.9 `detail` stay in stage 3, when the store error exists. This stage's writer takes the message from the caller.

### 2026-10-15 — stage 2 (route every error)

`--json-errors` is a global, stripped wherever `--json` is. `remember_run` sets `diag_set_json_errors` for that one call. `err_msg(code, text)` is the coded write. `err_store` maps `StoreStatus` beside the print. Plain bytes stay the 0.2.0 lines, except the two deprecation lines, which gained `warning: ` (stage 1 grill).

Itay 2026-10-15:

- `decision:plan16-open-message-codes` — `database` for `sqlite: …`, `cannot read user_version: …`, `cannot rebuild search index`, and `cannot open database` (bare or with a detail). `cannot_open` for `cannot create database directory: …`. `usage` for the empty-buffer fallback `invalid database path`.
- `decision:plan16-invalid-search-query` — `STORE_ERR_QUERY` is `invalid_query`. The sentence stays `invalid search query`.

### 2026-10-15 — stage 3 (structured store errors)

One `capture_sqlite_error` helper snapshots extended code, primary, name, message, and errno immediately after the failing SQLite call. `finalize` / `ROLLBACK` run after that. Mapping and the extended-name table live only in `store_sqlite.c`. D6 puts `SQLITE_NOMEM` and `SQLITE_IOERR_NOMEM` before the IOERR row; unnamed `IOERR_*` still fall through to `io_error` via the primary-code switch. `store.h` stays sqlite-free: `StoreError` / `StoreOpenOpts` / `store_open_opts` / `store_last_error` / `store_path`.

`exec_sql` retries `SQLITE_SCHEMA` once, then classifies `database`. `prepare_v2` already re-prepares; a second helper was not added. `--busy-timeout` and `--no-create` stay stage 5; NULL opts are 5000 ms and create. D8 `no_create` is already on `StoreOpenOpts` and skips mkdir.

D7 `util_absolute_path` joins cwd, then lexical `.` / `..`. It does not walk symlinks. `getcwd` uses a `PATH_MAX` stack buffer (GNU `getcwd(NULL,0)` is not C11).

E4.8 cause/fix is `diag_error_sqlite`. `database` has its own sentence (no cause). E4.9 always writes `sqlite_code`, `sqlite_extended`, `sqlite_name`, `sqlite_message` (empty string when SQLite gave none — 006 has no omit rule for the message). `errno` only when non-zero; `path` only on the six codes, always the absolute `--db` path; `reason` only `cannot_open`; `fts_only` only `corrupt`.

`capture_sqlite_error(...)` is a coverage-defensive line, same class as `set_err`. `remember_tests` always defines `REMEMBER_TEST_HOOKS` so the mapping/SCHEMA probes compile in the sanitizer binary.

No new grill. `sqlite_message` always-emit follows 006's omit list (errno/path/reason/fts_only only).

### 2026-10-15 — stage 4 (VFS shim + acceptance matrix)

`REMEMBER_TEST_VFS` defaults OFF. A Release configure forces it OFF even if the cache asks. The shim compiles into the `remember` CLI only. `remember_plain` and `remember_core` do not link it. `main` calls `remember_test_vfs_install` under the define; that wraps `unix` and registers it as the default VFS. Unix methods other than `xOpen` stay the parent's (they ignore the VFS pointer). `xOpen` leaves the `unixFile` at the front of the buffer — those methods expect that layout — and stores a method-table copy after it. Only `xRead` and `xWrite` are replaced. Truncate, sector size, and the lock methods stay the unix functions, which is what SQLite calls.

Env, read once at install:

- `REMEMBER_TEST_ENOSPC=1` — every `xWrite` returns `SQLITE_FULL`.
- `REMEMBER_TEST_FAIL_WRITE=N` — N writes succeed, then every later write returns `SQLITE_IOERR_WRITE`.
- `REMEMBER_TEST_FAIL_READ=N` — same for reads, `SQLITE_IOERR_READ`.
- `REMEMBER_TEST_NOMEM=1` — a write returns `SQLITE_IOERR_NOMEM`, and so does a read of a file that already has bytes. A zero-length read is the probe SQLite does before it writes a new brain's header; that probe is passed through so the write-side fault can run.

Unset or non-numeric counts are off. `=1` is the only value that turns the two flags on. A fault stays on once the count is exhausted, so SQLite cannot retry past it. NOMEM is checked before ENOSPC, which is checked before the write count.

Itay 2026-10-15, `decision:plan16-nomem-sqlite-name`: the shim returns `SQLITE_IOERR_NOMEM`, and SQLite's `apiHandleError` rewrites that to `SQLITE_NOMEM` before `sqlite3_exec` / `sqlite3_step` return. The CLI locks `sqlite_name` `SQLITE_NOMEM`, message `out of memory`, code `database`. Do not bypass the rewrite.

The failing `xRead` / `xWrite` returns before that system call, so `detail.errno` is whatever the unix VFS stored last. NOMEM checks size with `xFileSize` first; a successful `fstat` does not set errno. Observed on macOS: `FAIL_WRITE=0` on a new path reports `errno` `ENOENT` (2); `FAIL_READ=0` on an existing brain reports `errno` `EEXIST` (17). ENOSPC and NOMEM reported no errno.

chmod acceptance (macOS, uid not root), same names as the Linux table in this plan:

| Case | `sqlite_name` |
|------|----------------|
| file 444 + `add` | `SQLITE_READONLY` |
| folder 555, file 666 + `add` | `SQLITE_READONLY_DIRECTORY` |
| file 000 | `SQLITE_CANTOPEN`, `reason` `permission`, errno `EACCES` |
| truncated to 512 bytes | `SQLITE_CORRUPT`, `fts_only` false |

Each chmod test prints `SKIP <name> (running as root; file mode bits are ignored)` when `geteuid` is 0, and returns before the mode bits are checked. `scripts/ci-linux.sh` runs the suite as the source-tree owner when the container is root. Docker Desktop mounts the tree as uid 0, so that path creates `rememberci` and runs as that user instead. The build dir is made world-writable rather than chowned, so the host owner of the build tree stays put. GitHub Actions is already non-root.

`tests/test_no_shim.sh` (ctest `no_shim_symbols`) requires the symbol on the test CLI and rejects it on `remember_plain`, `remember_core`, and a Release tree configured with `-DREMEMBER_TEST_VFS=ON`. `scripts/install.sh` refuses a binary that defines the symbol. `test_vfs.c` is the one TU besides `store_sqlite.c` allowed to include sqlite.

`--busy-timeout`, `--no-create`, and `repair search-index` stay stages 5 and 6. QUALITY.md's shim paragraph stays stage 7.

## Review Notes

### 2026-10-15 — in-session deep review (stage 3)

**Verdict:** approve for the second-opinion pass. No open blocking item. No new grill.

**Ran:** ctest 4/4 with ASan/UBSan, `lint-all.sh` LINT OK, coverage 100% functions and 100% effective lines, `leaks-macos` PASS, `ci-linux.sh` OK (iwyu and scan-build included). Probes: text file → `not_a_database` with absolute `path` (plain and JSON, empty stdout, exit 1); chmod 444 write → `read_only` with path; `get --key nope --json-errors` → `not_found` exit 2, no detail; truncated zeros → `not_a_database`. Bugbot on uncommitted changes: one validator-dropped finding; follow-up on `store_sqlite.c` found capture-after-finalize in `foreign_keys_ok` (and the same step-loop shape in three list helpers). Auto-fixed: capture, then `errmsg`, then `finalize`.

**Praise:** one capture helper, mapping only in the adapter, `store.h` still sqlite-free, D7 lexical path in `util.c`.

**Auto-fixed:** capture-before-finalize on `foreign_keys_ok`, VV parse, conflict list, and snapshot tags. Linux tidy magic `0xA5` → `POISON_BYTE`. D7 root `"/"` success plus a one-byte buffer miss. Coverage treats `capture_sqlite_error` as defensive.

**Nit:** `k_sqlite_codes` / `k_sqlite_code_names` stay parallel arrays with `_Static_assert`. A struct row would merge them; the class table cannot merge because it names a different set. Left as is.

**Ponytail:** SCHEMA retry is `exec_sql` only. `prepare_v2` already re-prepares. No second helper.

**Pragmatic write-gate:** DRY PASS (one capture, one class table, one advice table). Easy-to-replace PASS (`store.h` is the port; sqlite stays in the adapter). Orthogonal PASS (codes do not change exit codes; E4.8 does not rewrite non-SQLite open text). Simplify PASS after the capture-order fix. No deferred markers.

**Left for later:** stage 4 VFS shim + acceptance matrix. Stage 5 `--busy-timeout` / `--no-create`. Stage 6 `fts_only` from `quick_check`. Stage 7 help / SKILL / QUALITY shim note.

### 2026-10-15 — in-session deep review (stage 2)

**Verdict:** approve for the second-opinion pass. No open blocking item. No new grill. The two code splits stay `decision:plan16-open-message-codes` and `decision:plan16-invalid-search-query`.

**Ran:** ctest 4/4 with ASan/UBSan, `lint-all.sh` LINT OK, coverage 100% functions and 100% effective lines, `leaks-macos` PASS, `ci-linux.sh` OK (iwyu and scan-build included). Probes: `--json-errors` before and after the subcommand, both flags together on a miss and on an empty list, a plain and JSON unknown option, `--trash` then a miss (plain and JSON), help text unchanged, a missing `--db` value, `--json-errors=1` left as an unknown option, an empty search, a missing body after the synced-folder warning, and `purge-trash`. Bugbot on the uncommitted diff reported no bugs.

**Praise:** one process flag, one writer, and the 0.2.0 sentences stay put except the two deprecation lines, which already gained `warning: ` in the stage 1 grill.

**Auto-fixed:** command files include `diag.h` directly. `err_fmt` allocates the formatted line, so a long option stays that usage sentence. A wrapped `err_msg` tail counts as the same defensive line as a one-line call.

**Nit:** `remember_app.c` and the mapper tests include the private `commands_common.h`. The router lives there; `main.c` still does not. Recorded in the engineering notes.

**Ponytail:** the flag is process-global for the same reason as the stream pair. No second writer.

**Pragmatic write-gate:** DRY PASS (one status map, one text map). Easy-to-replace PASS (`app_err` stays the stream port). Orthogonal PASS (codes do not change exit codes). Simplify PASS (the extra open-failure block is gone). No deferred markers.

**Left for later:** stage 3 adds E4.8 text and E4.9 `detail`. Stage 7 adds `--json-errors` to help. `raw_query_ignored` still has no call site.

### 2026-10-15 — in-session deep review (stage 1)

**Verdict:** approve for the second-opinion pass. No open blocking item. No grill.

**Ran:** ctest 4/4 with ASan/UBSan, `lint-all.sh` LINT OK, coverage 100% functions and 100% effective lines, `leaks-macos` PASS, `ci-linux.sh` OK. A sanitizer probe checked percent signs, tab/CR/UTF-8, warning JSON quotes, a NULL warning message, exit `-1`, and a read-only error stream (plain, JSON, and warning all return -1). Bugbot on the uncommitted diff reported no bugs.

**Praise:** one string table per catalog and one writer. JSON goes through `output_json_string`. Warning tokens are not error codes. Unknown codes write nothing.

**Nit:** the memstream setup is repeated in each test. Left as is; it is the same shape as the other stream tests, and a helper would hide the arrange step.

**Ponytail:** nothing to cut. The padding byte and the `Notice` struct exist so `-Wpadded` and the swappable-parameter check stay quiet.

**Pragmatic write-gate:** DRY PASS, easy-to-replace PASS (`app_err` is the existing stream port), orthogonal PASS (no SQLite, no call-site edits). No deferred markers.

**Left for later:** stage 2 routes `err_msg`. Stage 3 adds E4.8 text and E4.9 `detail`.

### 2026-10-15 — second-opinion deep review (stage 1)

**Verdict:** stage 1 approved after a grill-driven change to the warning emitter. No open blocking item.

**Run, not read.** Gate: ctest 4/4 (ASan/UBSan), LINT OK, coverage 100% functions and 100% effective lines, `leaks-macos` PASS, `ci-linux.sh` OK. The catalog matches E4.3 exactly: 28 codes in order, each once, with enum ↔ string round-trips. Warning tokens are a separate catalog. A message containing `%`, a quote, CR or tab is safe on both plain and JSON output.

**Blocking → fixed (Itay grill 2026-10-15).** The stage 1 warning API could not carry out stage 2's D5 routing as locked:

- `diag_warning(..., exit_code)` wrote nothing on a non-zero exit (E4.6). But the `--trash` / `purge-trash` deprecation line and the synced-folder line are written before the command runs, and 0.2.0 prints them on failing commands too. Verified: `get --trash 999` printed the deprecation line, then `not found`, then exit 2.
- The plain format `remember: note: <text>` could not reproduce `remember: --trash is deprecated; use --expired` or `remember: warning: database path looks like ...`, so D5 "plain text unchanged" was impossible.

Itay's answers:

- **deprecated_flag and synced_folder keep printing when the command then fails.**
- **Every warning's plain prefix is `warning:`**: `remember: warning: <text>`. This amends E4.6 (`note:` becomes `warning:`). It also amends D5: the deprecation lines gain `warning: ` (`remember: warning: --trash is deprecated; use --expired`), and the synced-folder line is unchanged. JSON `message` is `<text>` without the prefix.

Change: `diag_warning(code, message, json_errors)` drops `exit_code`; plain prefix is `warning: `. The test `warning_with_nonzero_exit_writes_nothing` is replaced by `every_warning_uses_the_warning_prefix`. E4.6's "never with a non-zero exit" now holds by contract at the plan 18 call site, which writes `raw_query_ignored` only when the search succeeds (written in `diag.h`).

**For stage 2:** the E4.2 golden list must include the two new deprecation lines with `warning: `. For stage 7: record both amendments in the 006 Implementation Results (E4.6, D5).

**Praise:** one string table per catalog. JSON reuses `output_json_string`. An unknown code writes nothing and returns -1. A NULL message cannot reach `printf("%s")`.

### 2026-10-15 — second-opinion deep review (stage 2)

**Verdict:** stage 2 approved after one grill-decided simplification and one test auto-fix. No open blocking item.

**Run, not read.** Gate (after the fixes): ctest 4/4 (ASan/UBSan), LINT OK, coverage 100% functions and 100% effective lines, `leaks-macos` PASS, `ci-linux.sh` OK.

- **Plain golden by experiment.** `build-release/remember` was built from stage 1, which changed no call sites, so it is an exact 0.2.0-text baseline. I diffed stderr and exit codes against stage 2 on 49 error cases: argv and flag errors, ids, limits, bins, add/update/rekey/link/search/import/conflict/device/tags/purge, `--db` path errors, `--trash` and `purge-trash`. Only the two deprecation lines differ, and only by the `warning: ` prefix Itay chose.
- **JSON by experiment.** With `--json-errors`, the same 49 cases give exactly one valid JSON error line, with a warning line before it where one applies. stdout is empty on every error, `message` equals the plain line minus `remember: `, and every code matches D9: `usage`, `not_found`, `deleted`, `not_deleted`, `invalid_query`, `self_link`, `key_conflict`, `body_conflict`, `database`, `cannot_open`.

**Auto-fixed:**

- **important** — two stage 2 checks had no test. The plan's "golden test proves plain bytes unchanged" was missing, and so was the DoD row "D9 table is a test". The repo had only three plain samples, and `code_for_cmd_text` (text → code) had no test. New `golden_error_lines_and_d9_codes` is a table of 16 rows over every D9 family. Each row is run plain (exact `remember: <msg>\n`, exit, empty stdout) and with `--json-errors` (exact JSON line, same exit, empty stdout). Mutation-checked: mapping `STORE_ERR_SELF_LINK` to `cycle` fails it.

**Simplify lens → grill (Itay 2026-10-15: drop it):** after stage 2 added the process-wide flag, every `src/` caller passed `diag_json_errors()` back into `diag_error` / `diag_warning`. Both functions now take `(code, message)` and read the flag themselves. The getter `diag_json_errors()` then had no caller, which the function-coverage gate caught (99.78%), so it is deleted too. Unit tests set the mode through two small helpers, `diag_error_as` and `diag_warning_as`.

**Nit (left; stage 3 and later):** `code_for_store_text` maps store error *text* to codes. That is the locked bridge (`decision:plan16-open-message-codes`) until stage 3 adds structured store errors. `code_for_cmd_text` does the same for command helpers, which return only a string. The golden table now pins every family it routes, so a reworded sentence fails a test. Plan 16 or a later plan could let those helpers return their code, which would delete the classifier.

**Praise:** `err_store` puts the StoreStatus → code switch next to `store_status_message`, as the plan asks. `err_fmt` keeps long unknown-option text without truncating it (it has its own test). Parse errors map through one `code_for_cli_error` switch.

### 2026-10-15 — second-opinion deep review (stage 3)

**Verdict:** stage 3 approved after three grill-decided changes and one auto-fixed simplification. No open blocking item.

**Run, not read.** Gate (after the changes): ctest 4/4 (ASan/UBSan), LINT OK, coverage 100% functions and 100% effective lines, `leaks-macos` PASS, `ci-linux.sh` OK. Acceptance on macOS (uid 501, real binary):

| Case | Code | `sqlite_name` (macOS) | Detail |
|------|------|------------------------|--------|
| file 444 + `add` | `read_only` | `SQLITE_READONLY` (8) | absolute `path` |
| folder 555 + `add` | `read_only` | `SQLITE_READONLY_DIRECTORY` (1544), same as Linux | absolute `path` |
| file 000 | `cannot_open` | `SQLITE_CANTOPEN` (14), errno 13 | `reason` `permission` |
| folder as path | `cannot_open` | `SQLITE_CANTOPEN` (14, not 526), errno 21 | `reason` `is_directory` |
| text file | `not_a_database` | `SQLITE_NOTADB` (26) | absolute `path` |
| truncated brain | `corrupt` | `SQLITE_CORRUPT` (11) | `fts_only` false |
| `--db ./sub/../sub/r.db` (444) | `read_only` | `SQLITE_READONLY` | `path` = cwd + `/sub/r.db` (D7) |

Capture runs right after the failing call, before finalize or ROLLBACK (the hot spot). `exec_sql` retries SCHEMA once.

**Grill 1 (Itay 2026-10-15): JSON shape for SQLite errors (amends E4.4 for the eight SQLite codes).** 006 contradicted itself: E4.4 says `message` = plain line minus the prefix, but the E4.8 table has a short `message` column. Itay's answer: `message` = `database error: <cause>` (`database error` for code `database`). `detail` gains `fix` (the E4.8 fix sentence) right after `sqlite_message`. `code` stays the catalog token, and the SQLite name stays in `detail.sqlite_name`. The plain stderr line is unchanged. `diag_error_sqlite` now writes one bounded stack line in place of the measure → malloc → format → free sequence.

**Grill 2 (Itay 2026-10-15): name the import source.** `import --from-db /missing.db` used to say "cannot open the brain file", with `detail.path` = the user's healthy brain. `StoreError.import_source` is set by `capture_source_error` (every source-handle capture) and by the source-only version read and gate. `cmd_import` reports with the `--from-db` path, so `detail.path` is the source. The cause names "the import source" for `busy`, `cannot_open`, `not_a_database` and `corrupt`, and the fix is `Check the --from-db path.` Other causes keep the base row. The golden import row is updated.

**Grill 3 (Itay 2026-10-15, simplicity lens): one open with options.** `store_open_replacing_sidecar` is gone. `StoreOpenOpts.replace_invalid_sidecar` replaces it, `store_open_opts` is the single implementation, and `store_open` is `store_open_opts(path, NULL, …)`. `STORE_DEFAULT_BUSY_TIMEOUT_MS` moves to `store.h`, so the CLI's `opts` starts from the real default (a zeroed struct would mean a busy timeout of 0). Stage 5 sets `--busy-timeout` / `--no-create` on the same struct.

**Auto-fixed (simplicity lens, behaviour-neutral):**

- The E4.7 class table had 59 rows. 55 of them only restated what `classify_extended`'s primary-code switch already returns; I checked this mechanically against the pre-change file. 4 rows remain, each a real exception or a D6 marker: `NOMEM`, `IOERR_NOMEM`, `READONLY_DBMOVED`, `IOERR_SHORT_READ`.
- The two parallel 74-entry arrays (codes and names, plus a `_Static_assert` tying their lengths) are now one `{name, code}` table, so a name cannot drift from its code.
- `store_sqlite.c` is a net 123 lines shorter, including the import-source addition. The mapping tests (`store_test_classify_extended` table, names, reasons) pass unchanged.

**Gate follow-ups (all fixed before commit):** the coverage script now treats `capture_source_error(` as a defensive snapshot line, the same class as `capture_sqlite_error(`. The source version-read branch is covered by the new `cli_import_text_source_names_the_source` (a text file as `--from-db` gives `not_a_database`, source wording, and `detail.path` = the source). After the stack-buffer change, Linux IWYU removed `<stdlib.h>` from `diag.c`. Two clang-tidy fixes: an order comment on `write_sqlite_detail`, and an `if` instead of an int→bool `&&` assignment in `open_store`.

**Nit (left):** `diag.h` includes `store.h` so `diag_error_sqlite` can take a `StoreError`. The emitter now knows a store type. A diag-local detail struct would decouple them, at the cost of a copy at the one call site. Left as is.

**Praise:** `store.h` stays SQLite-free. One capture helper feeds every failure site. The cannot_open reason table matches the plan's observed facts on macOS too.

### 2026-10-15 — in-session deep review (stage 4)

**Verdict:** approve for the second-opinion pass. No open blocking item. No new grill. `decision:plan16-nomem-sqlite-name` stays the only stage 4 decision.

**Ran:** ctest 5/5 with ASan/UBSan (including `no_shim_symbols`), `lint-all.sh` LINT OK, coverage 100% functions and 100% effective lines, `leaks-macos` PASS, `ci-linux.sh` OK (iwyu and scan-build included). The container is root and the mount is uid 0; the suite ran as `rememberci`. No chmod test printed `SKIP`.

**Praise:** the unix file stays at the front of the buffer, and only `xRead` / `xWrite` are replaced. Truncate and sector size stay the unix functions, which is what SQLite calls, so the wrapper does not grow methods the suite never enters. Release, `remember_plain`, and `remember_core` stay free of the symbol.

**Auto-fixed before commit:** clang-tidy and cppcheck on the shim (env helpers, NOLINT on the process counters, `const` on the file header). Coverage was 99.60% while every io method was a wrapper SQLite does not call (`xTruncate`, `xSectorSize` on a powersafe-overwrite filesystem). Those wrappers are gone. NOMEM on a brand-new brain used to die on the empty-file probe, so the write-side return never ran; that probe is passed through and the header write takes the fault. The user-visible line is still `SQLITE_NOMEM` / `database` / no errno.

**Follow-up, not this stage:** Bugbot on the branch (it did not finish `store_sqlite.c`, `tests/test_errors.c`, or `tests/test_no_shim.sh`) reported one medium finding in `cmd_import.c`: `load_tags` on the import source captures a SQLite error without `import_source`, so a tag-read failure is blamed on the brain. That is the stage 3 source-naming contract, not the VFS. Left for the whole-plan review.

**Nit (left):** the tail cast needs a clang `-Wcast-align` suppression. `unixFile`'s size is pointer-aligned, and an assert checks that before the cast.

### 2026-10-15 — second-opinion deep review (stage 4)

**Verdict:** stage 4 approved. No open blocking item. No grill.

**Run, not read.** Gate (`-DREMEMBER_TEST_VFS=ON`): ctest (ASan/UBSan, incl. `no_shim_symbols`), LINT OK, coverage 100% functions and 100% effective lines, `leaks-macos` PASS, `ci-linux.sh` OK. X-E02 by hand on the shim CLI:

| Env | Code | `sqlite_name` | Notes |
|-----|------|---------------|-------|
| `REMEMBER_TEST_ENOSPC=1` + `add` | `disk_full` | `SQLITE_FULL` | exit 1, stdout empty |
| `REMEMBER_TEST_FAIL_WRITE=0` | `io_error` | `SQLITE_IOERR_WRITE` | errno 2 (stale unix errno; documented) |
| `REMEMBER_TEST_FAIL_READ=0` | `io_error` | `SQLITE_IOERR_READ` | |
| `REMEMBER_TEST_NOMEM=1` | `database` | `SQLITE_NOMEM` | `decision:plan16-nomem-sqlite-name` |

`nm`: `remember_test_vfs_install` is in the shim CLI only. `remember_plain`, the Release `remember`, a shim-off build, and `remember_core` all show 0. Release safety has four layers: CMake forces the option OFF in Release, `main` is `#ifdef`-guarded, `install.sh` refuses the symbol, and a ctest checks it.

**Auto-fixed:**

- **important** — a default build failed `ctest`. P3 keeps `REMEMBER_TEST_VFS` default OFF, but the seven shim tests ran unconditionally, so a plain `cmake -S . -B build && ctest` was red: the CLI had no shim, and every shim assertion failed. Found by experiment (my reconfigure lost the flag). CI and every gate script pass the flag, so they stayed green. Now CMake defines `REMEMBER_TEST_VFS` on `remember_tests` when the option is ON. The shim tests and their registrations are `#ifdef REMEMBER_TEST_VFS`, and otherwise the run prints `SKIP 7 VFS shim tests (remember built without REMEMBER_TEST_VFS)`, the same pattern as the root-chmod skip. Verified both ways: OFF gives 58 tests and 0 failures with the SKIP line; ON gives 65 tests and 0 failures.
- **simplify (lens 9)** — `tail_of` in `test_vfs.c` silenced `-Wcast-align` with 12 lines of clang/GCC `#pragma` push/pop. It now casts through `void *`, with one `NOLINTNEXTLINE(bugprone-casting-through-void)` (C has no `reinterpret_cast`), and the alignment `assert` already above it states the guarantee.

**Praise:** the shim opens through the real unix VFS, keeps the unix file at the front of the buffer, and replaces only `xRead` / `xWrite`. That is the least it can touch. The faults are sticky, so SQLite cannot retry past them. Each chmod skip prints its name, so a root run cannot pass silently.

### 2026-10-16 — stage 5 (open flags)

No new grill. No new `decision:*`. Help text for the flags stays stage 7.

- `--busy-timeout MS` and `--no-create` are globals, stripped wherever `--json` is. There is no `--busy-timeout=MS` form (`--db=` stays the only equals form).
- Range is 0..60000 inclusive. `60001`, `-1`, and `abc` are exit 1, code `usage`, sentence `invalid --busy-timeout (must be 0..60000)`. Stdout is empty and the brain is not created. With `--json-errors` that sentence is the one JSON line.
- A missing token is `remember: --busy-timeout: option requires a value` (the `--db` shape). `-1` is a specified bad value, so a leading dash is still the value. Any present token outside the range uses the invalid sentence, including a following command name.
- Omitted flag keeps 5000 ms. `0` is a real timeout (`busy_timeout_set`), not "use the default". A held `BEGIN EXCLUSIVE` plus `--busy-timeout 500` is `busy`, `SQLITE_BUSY` (5), `sqlite_message` `database is locked`, with no `path` and no `errno`. The wait is about half a second on macOS and in Linux CI, not the 5 s default. `--busy-timeout 0` returns the same `busy` line without that wait. `60000` is accepted and does not wait.
- `--no-create` is applied before `store_open_opts`, which already skips `ensure_parent_dirs` and opens without `SQLITE_OPEN_CREATE`. A missing parent and a missing file in an existing directory both fail `cannot_open`, `reason` `missing`, `SQLITE_CANTOPEN` (14), errno `ENOENT`. No folder, no file, no `<path>.device_id`. An existing brain still opens.
- Decimal rules live in `util_parse_nonneg`, shared with `--limit` and `--offset` (empty, leading `-`, trailing junk, `ERANGE`). Leading `+` and whitespace stay what `strtoull` already accepted for `--limit`.

### 2026-10-16 — in-session deep review (stage 5)

**Verdict:** approve for the second-opinion pass. No open blocking item. No grill.

**Ran:** ctest step_gate, store_asan_gate, and no_shim_symbols (ASan/UBSan). `install_skill` failed inside the sandbox (`mkdir` on `.cursor` not permitted) and passed outside it. `lint-all.sh` LINT OK. Coverage 100% functions and 100% effective lines. `leaks-macos` PASS. `ci-linux.sh` OK, including non-root tests, IWYU, and scan-build. Bugbot on the uncommitted stage found no bugs.

**Pragmatic write-gate:** DRY PASS (`util_parse_nonneg` is the one decimal parser). Easy-to-replace PASS (the parser is a plain function; SQLite stays in the adapter). Orthogonal PASS (cli parses, `open_store` applies, the store already implemented D8). No deferred markers.

**Simplify:** `busy_timeout_set` is what makes `0` different from "flag omitted". A zeroed `StoreOpenOpts` would have meant a timeout of 0, which stage 3 already forbade.

**Left for later:** stage 6 is the corrupt check and `repair search-index`. Stage 7 is help for these flags, plus the facade byte-match for a `busy` case (the CLI and `remember_run` already share one pipeline).

**Praise:** `-1` is not swallowed as a missing option. The missing-file test, not only the missing-parent test, is what proves `SQLITE_OPEN_CREATE` is off.

### 2026-10-16 — second-opinion deep review (stage 5)

**Verdict:** stage 5 approved after one auto-fix. No open blocking item. No grill.

**Run, not read.** Gate (`-DREMEMBER_TEST_VFS=ON`): ctest (ASan/UBSan, incl. `no_shim_symbols`), LINT OK, coverage 100% functions and 100% effective lines, `leaks-macos` PASS, `ci-linux.sh` OK. Probes on the built CLI:

- A held `BEGIN EXCLUSIVE` from a second process plus `--json-errors --busy-timeout 500 add` → `busy` (`SQLITE_BUSY`, no `path`, no `errno`) after about 0.65 s, not the 5 s default (probe 3).
- `--busy-timeout 60001`, `abc`, or `" -1"` → `invalid --busy-timeout (must be 0..60000)`. A missing value → `--busy-timeout: option requires a value`. `+500` is accepted, as `--limit` accepts it in 0.2.0.
- `--no-create --db <dir>/none/x.db list` → `cannot_open`, `reason` `missing`, errno 2, and no folder (probe 4). A missing file in an existing folder → the same, with no file and no sidecar. An existing brain with `--no-create` opens.

**Auto-fixed:**

- **important** — `--offset " -1"` was accepted as offset 18446744073709551615. It exited 0 with `"offset":18446744073709551615` in the JSON, and rows came back as if there were no offset. `util_parse_nonneg` checked for a minus only at `raw[0]`, and `strtoull` skips leading whitespace and wraps `-1` to `ULLONG_MAX`. The bug predates this stage: 0.2.0's `parse_size_token` had the same check. Stage 5 made it the single parser for `--limit`, `--offset` and `--busy-timeout`, so the fix lands there once: any `-` anywhere is rejected, because a non-negative decimal never contains one. `+5` and ` 7` stay accepted, for 0.2.0 compatibility. New `util_parse_nonneg_rejects_every_sign` (unit) and `cli_offset_spaced_negative_is_usage` (CLI) were red before the fix.

**Simplify (lens 9, left):** `CliGlobals` carries `busy_timeout_ms` plus `busy_timeout_set`. A `-1` "omitted" sentinel would drop the bool, but `cli.c` would then need the store default or the sentinel would leak into `open_store`. The in-session review chose the explicit bool for that reason, and it reads clearly. Left as is.

**Praise:** one decimal parser for three flags. `-1` is reported as a bad value, not as a missing option. The missing-file test (not only the missing-parent test) proves `SQLITE_OPEN_CREATE` is off.

### 2026-10-16 — stage 6 (corrupt check + repair)

No new grill. No new `decision:*`. Help for `--json-errors`, `--busy-timeout`, and `--no-create` stays stage 7, and so do SKILL.md, What's New, the QUALITY shim paragraph, COVERAGE, gate-suites, and 006 Implementation Results.

- `fts_only` is set in the adapter, after the snapshot and before any later SQLite call can clobber it. `quick_check` does not call `capture`. It is true for `SQLITE_CORRUPT_VTAB`. Otherwise it is true only when the five base checks pass and an active statement's SQL contains `entries_fts`. A search that names the index still stays `fts_only` false when a base table fails.
- Base tables, in order: `entries`, `entry_links`, `conflicts`, `settings`, `bucket_prefixes`. Each is `PRAGMA quick_check(<table>)` on the same connection. One `ok` row passes. A problem row is `corrupt`, `SQLITE_CORRUPT`, `fts_only` false, and `sqlite_message` is that row. An engine error on the statement keeps the engine code, name, message, and errno, forces the class to `corrupt`, and sets `fts_only` false.
- Observed on macOS, vendored SQLite, and confirmed by Linux CI: zeroing the entries root page (page 2 of a fresh brain) makes `quick_check` return the text row `*** in database main ***` / `Tree 2 page 2: btreeInitPage() returns error code 11`, not an engine code. A committed `UPDATE entries_fts_data SET block = zeroblob(32) WHERE id = (SELECT MIN(id) FROM entries_fts_data)` makes search return `SQLITE_CORRUPT_VTAB` (267), message `database disk image is malformed`, `fts_only` true. A truncated brain (512 bytes) makes repair refuse with the base `corrupt` line and `fts_only` false; it does not rebuild.
- `REMEMBER_TEST_FAIL_READ=2` on a seeded brain fails a read inside the check (open uses the first two). Repair reports `corrupt` with `sqlite_name` `SQLITE_IOERR_READ`, `sqlite_message` `disk I/O error`, errno `ENOENT` (the shim returns before the syscall, same stale-errno rule as stage 4). Linux CI passed this test on the same count.
- `repair search-index` checks first, then `BEGIN`, `INSERT INTO entries_fts(entries_fts) VALUES('rebuild')`, `COMMIT`, then `VALUES('integrity-check')`, then `COUNT(*)` from `entries` in every bin. Human stdout is `rebuilt N`. JSON is `{"version":1,"action":"rebuilt","entries":N}`. A failed rebuild rolls back. The integrity check is outside the transaction, as E4.14 says.
- Every misuse (`repair`, `repair foo`, an extra token, `repair --nope`) is `usage: repair search-index`. Open runs before that reject, so the brain file is created, same as the other commands. Registry summary is `Rebuild the search index`. Command help states the check, the human line, the JSON object, and that N counts every bin. Facade byte-matches human repair, `--json` repair, and `help repair`.

### 2026-10-16 — in-session deep review (stage 6)

**Verdict:** approve for the second-opinion pass. No open blocking item. No grill. No new `decision:*`.

**Ran:** ctest 5/5 with ASan/UBSan and `REMEMBER_TEST_VFS=ON` (100% passed). `lint-all.sh` LINT OK. Coverage 100% functions and 100% effective lines. `leaks-macos` PASS (the first run died in the sandbox with "Couldn't get task port"; the same target passed outside it). `ci-linux.sh` OK, including the non-root suite, IWYU, and scan-build. Bugbot on the uncommitted stage found no bugs.

**Pragmatic write-gate:** DRY PASS (the five base tables are the E4.10 list, not a second copy of the schema). Easy-to-replace PASS (`cmd_repair` talks to `Store` and `output`; SQLite stays in the adapter). Orthogonal PASS (the check is part of corrupt reporting, which is what E4.10 says). No deferred markers.

**Ponytail:** nothing to cut. The `touched = false` assignment is the form of the E4.10 rule that stays one covered return.

**Auto-fixed:** coverage had left the engine-failure body of the base-table check unhit, so repair-on-truncated and `REMEMBER_TEST_FAIL_READ=2` now lock it. clang-tidy rejected `bool && bool` as an int conversion; the return assigns through a bool instead. cppcheck wanted `counts` const and `text` in the row block. IWYU wanted `<stdbool.h>` in `output.h` because `output_rebuilt` takes `bool`. The unreachable format-overflow branch is gone; the SQL is a fixed list.

**Left for later:** stage 7 is help for `--json-errors`, `--busy-timeout`, and `--no-create`, plus SKILL.md, What's New, the QUALITY shim paragraph, COVERAGE, gate-suites, and 006 Implementation Results. The repair command topic is already in this stage.

**Praise:** a damaged index says `fts_only` true and names `repair search-index`. A damaged base table stays the old corrupt line, and repair refuses it.
