# 20 — Export: JSON, Markdown, CSV (006 §5)

## Goal

`remember export` writes the whole brain (or chosen bins) to an open format
that a person can read and that `import` (plan 21) can read back. It needs no
account, no network and no quota.

```
remember export --format json|md|csv --out PATH [--bins live,expired,deleted] [--force]
```

## Source of truth

[`design-logs/006-ui-overhaul-engine.md`](../design-logs/006-ui-overhaul-engine.md)
§0 (EN-01..EN-04), §5 (E6–E6.c), §11 E12.9, Verification E6/E7 (round-trip
fixture), plus the locked decisions below. Do not invent. If 006 and the
locked decisions do not decide a case, stop and ask Itay (grill), then record
the answer under Implementation Notes.

## Context (standalone summary)

Baseline: `main` at `a27efca` with plans 15, 16, 18 and 19 merged.

- Memories carry `id` (local only), `sync_id` (UUID v7), `key`, `body`, tags,
  `source` (`human|agent|tool|share|unknown`), `created_at`, `updated_at`,
  `expires_at`, `deleted_at`, `version_vector` (JSON object, device id →
  counter). `bin` is derived: `live`, `expired`, `deleted`.
- Links are `entry_links(from_id, to_id, kind)` with stored kinds `related`
  (canonical `min,max`), `cites`, `supersedes`, plus `created_at`/`updated_at`
  (`entry_links` has `created_at`/`updated_at NOT NULL`).
- `output.c` owns JSON escaping (RFC 8259) and is the only JSON writer.
- Plan 19 added `store_bucket_prefixes_get` (all rows incl. tombstones +
  `bucket_prefix_order` + `updated_at`) and the pure `bucket_prefixes.c`.
- Plan 16 added codes, `detail`, the absolute-path helper (D7), and the diag
  emitter. Plan 18 added `unicode.h` (not needed here) and D12 title/word
  helpers in `search_terms.c` (reused for the slug).
- `REMEMBER_VERSION` is the engine version string (0.2.0 until plan 22 bumps
  it to 0.3.0). Tests compare against the macro, not a literal.

Rules this plan implements (restated from 006):

| ID | Rule |
|----|------|
| E6 | `remember export --format json|md|csv --out PATH [--bins live,expired,deleted]`. `--bins` defaults to all three. |
| E6.1 | No account, network or quota check. |
| E6.2 | Each memory exports `sync_id, key, body, tags, source, created_at, updated_at, expires_at, deleted_at, version_vector` and its links. No local `id`. `bin` is written for readers and ignored on import. |
| E6.3 | Links export as `{from, to, kind, created_at, updated_at}` with `sync_id` ends, for every `entry_links` row whose two ends are exported. |
| E6.4 | The sidecar and `conflicts` are not exported. Brain settings are exported. |
| E6.5 | Output `{"version":1,"action":"exported","format":"<f>","path":"…","count":N,"links":M}`. |
| E6.6 | Write to a temp file beside PATH, then rename onto PATH. PATH exists and no `--force` → write nothing, exit 1 `usage` `output already exists: <path>`. `--force` replaces PATH by that rename. |
| E6.a | JSON (UTF-8, no BOM): `{"format":"remember-export","version":1,"exported_at":"…Z","engine":"<version>","schema":5,"entries":[…],"links":[…],"settings":{…}}`. Entry members in E6.2 order with `bin` before `version_vector`. Entries sorted by `created_at`, then `sync_id`. `settings` last; an untouched brain writes `{}`. |
| E6.b | Markdown is a folder: `README.txt`, `settings.json`, `memories/`, `expired/`, `deleted/`. `settings.json` is the E12.9 object. |
| E6.b1 | `README.txt` is exactly: `This folder is a Fortkeeps export. Import it with: remember import --from-md <folder>` |
| E6.b2 | File name `<slug>--<last 12 hex of sync_id>.md`. Slug = key, else the `# ` heading text, else the first 6 words; ASCII lowercased; every run outside `[a-z0-9]` → `-`; trim `-`; max 48 characters; empty → `memory`. |
| E6.b3 | File: line `---`; then one field per line in order `remember_export`, `sync_id`, `key`, `tags`, `source`, `created_at`, `updated_at`, `expires_at`, `deleted_at`, `version_vector`, `links`, each `<field>: <JSON>`; line `---`; the body bytes; the file ends with one newline. |
| E6.b4 | `remember_export` is the JSON number `1`. Null → JSON `null`. `links` = JSON array of `{to, kind, created_at, updated_at}` (no `from`) for edges whose `from` is this memory. |
| E6.c | CSV: RFC 4180, UTF-8 **with** BOM, CRLF, every field quoted, `"` doubled. Header `sync_id,key,body,tags,source,created_at,updated_at,expires_at,deleted_at,bin,version_vector,links`. `tags`, `version_vector`, `links` cells hold JSON. NULL = empty cell. One extra last row: `sync_id` `#settings`, the E12.9 object in `body`; that row is not a memory. |
| E12.9 | `settings.bucket_prefixes = {"order":{"value":[…],"updated_at":"…Z"},"items":[{"prefix","added_at","removed_at","device_id"}]}` with tombstones. |

## Locked decisions (grill 2026-10-12; index in [`006-locked-decisions.md`](006-locked-decisions.md))

| # | Decision |
|---|----------|
| D7 | Envelope `path` and `detail.path` are absolute: cwd join + lexical `.`/`..` normalization, no symlink resolution. |
| D12 | Slug "`# ` heading text" = the body's first line when it starts with `# ` (text after it, ASCII-trimmed; empty → none). "First 6 words" = the body with whitespace/control runs collapsed to one space, split on spaces, first 6 joined with one space, then the E6.b2 slug rules. Reuse the plan-18 helpers in `search_terms.c`. |
| D17 | Markdown `--force` on an existing folder: write `<PATH>.tmp-<rand>/`, rename the existing PATH to `<PATH>.old-<rand>/`, rename tmp onto PATH, then delete the old folder recursively. A crash leaves the old or the new folder plus a stray sibling, never a half-written PATH. For JSON/CSV files a single `rename()` replaces PATH. |
| D18 | Determinism: the JSON file is one compact line + a final `\n` (the same writer as `--json`). Tags sorted by code point. `version_vector` keys sorted. Top-level `links` sorted by (from sync_id, to sync_id, kind). Prefix `items` sorted by prefix. The same orders inside Markdown and CSV cells. `settings.json` is one compact line + `\n`. |
| D19 | Human output: `exported N memories, M links to <path>`. `--bins`: comma list of `live`, `expired`, `deleted`, no duplicates, non-empty; else `usage` `invalid --bins (use live, expired, deleted)`. File write failures: ENOSPC → `disk_full`; EACCES/EROFS → `read_only`; ENOENT on the parent → `cannot_open` `reason` `missing`; other errno → `io_error`; `detail.path` = the out path, `detail.errno` set. |
| D25 | CSV `links` cell = the Markdown shape `[{to, kind, created_at, updated_at}]` for edges whose stored `from` is this row, sorted by (to sync_id, kind). A `related` edge appears on its stored `from` end only. |
| P5 | Temp names: `<PATH>.tmp-<16 lowercase hex random>` (and `.old-<…>` for D17). They are removed on every failure path. |
| P6 | `export` gets a registry row, a help topic and a facade byte-match test (mask `exported_at`). |

Also locked for consistency with existing flag errors (`usage`): missing
`--format` → `export requires --format (json, md, or csv)`; bad value →
`invalid --format (use json, md, or csv)`; missing `--out` → `export
requires --out PATH`; a repeated flag → `duplicate <flag>`. Markdown folder
mapping: `live` → `memories/`, `expired` → `expired/`, `deleted` →
`deleted/`. CSV `#settings` row: every other cell is empty. A slug cut at 48
characters is trimmed of `-` again after the cut. The JSON `engine` member is
`REMEMBER_VERSION`; `schema` is the open brain's `user_version` (5).

**Answered in the final grill:**
- D29: the Markdown folder always has `README.txt`, `settings.json`,
  `memories/`, `expired/`, `deleted/`, with empty folders for excluded or
  empty bins.
- D30: `README.txt` is the E6.b1 sentence followed by one `\n`.

Anything else 006 and this table do not decide: stop and ask Itay. Do not
choose.

## Prerequisites

- **Plans 15, 16, 18, 19 merged.** If any is missing, stop.

## Scope

**In:** `export` command (parse, help, facade); one read transaction that
snapshots entries, tags, links and prefix settings for the chosen bins; JSON,
Markdown, CSV writers; atomic write + `--force`; D19 errors; tests incl. the
006 round-trip fixture (export side; the import half is plan 21); What's New.

**Out:** import (plan 21); zip (E7.7: the engine reads no zip and writes
none); exporting the sidecar or `conflicts` (E6.4).

## Files

New `src/cmd_export.c` (`REMEMBER_LIB_SOURCES` + `-Wcast-qual`), new pure
`src/export_format.c/.h` (slug, front matter lines, CSV quoting, JSON
document assembly on top of `output.c` escaping; no file I/O), new
`src/fileio.c/.h` (temp + rename, D17 folder swap, recursive delete of the
old folder, errno → code; the only TU doing export file I/O; POSIX APIs
allowed here), `src/store.h` (`store_export_snapshot(Store *, StoreBinSet,
ExportSnapshot *)` + free), `src/store_sqlite.c`, `src/output.c/.h` (export
envelope), `src/cli.c/.h`, `src/remember_app.c`, tests: new
`tests/test_export.c` (suite `export`), unit tests for `export_format.c` in
`remember_store_tests`, a shared fixture builder `tests/fixture_roundtrip.c/.h`
(006 Verification E6/E7 list; plan 21 reuses it), `tests/test_facade.c`,
`tests/gate-suites`, `tests/COVERAGE.md`; docs: help, SKILL.md (export for
backup), `README.md` What's New, `docs/engineering-notes.md`.

## Must-pass tests

The round-trip fixture (006 Verification E6/E7): keyed and keyless memories;
tags with spaces and `:` (stored forms); all 5 sources; expired and deleted
rows; all 3 link kinds; CRLF, tabs, emoji, Hebrew; a `---` line inside a
body; a body starting with `---`; quotes; commas; a 64 KiB body; null and
non-null `expires_at`; changed bucket prefixes with tombstones.

| Check | Proves |
|-------|--------|
| JSON: root members in E6.a order; `format`, `version` 1, `schema` 5, `engine` = `REMEMBER_VERSION`; entries sorted by `created_at`, `sync_id`; entry member order with `bin` before `version_vector`; no `id` | E6.a, E6.2 |
| Untouched brain → `"settings":{}`; after `config set` → `settings.bucket_prefixes` per E12.9 with tombstones | E6.a, E12.9 |
| Links only when both ends exported (`--bins live` drops edges to deleted rows) | E6.3 |
| No sidecar content and no `conflicts` rows anywhere in any format | E6.4 |
| Envelope E6.5; human line D19 | E6.5, D19 |
| Existing PATH without `--force` → exit 1 `usage` `output already exists: <path>`; PATH bytes unchanged; no temp left | E6.6 |
| `--force` replaces a file (JSON/CSV) and a non-empty folder (Markdown, D17); no `.tmp-`/`.old-` left on success | E6.6, D17 |
| Failure mid-write (ENOSPC via a full tmpfs or a fault hook in `fileio.c`) → `disk_full`; PATH untouched; temp removed | D19, P5 |
| Unwritable parent → `read_only`; missing parent → `cannot_open` `missing` | D19 |
| Markdown: `README.txt` = sentence + `\n` (D30); `settings.json` = E12.9 object; files in `memories/`, `expired/`, `deleted/` by bin; all three folders exist even with `--bins live` (D29) | E6.b, E6.b1, D29, D30 |
| Slug: key `recs:books` → `recs-books--<12hex>.md`; `# Hello, World!` → `hello-world--…`; keyless no heading → first 6 words; Hebrew-only body → `memory--…`; 60-char key → 48 then trimmed | E6.b2, D12 |
| Front matter: fields in order, `<field>: <JSON>`, nulls as `null`, `remember_export: 1`, `links` without `from`, sorted (D25/D18) | E6.b3, E6.b4 |
| A body containing a `---` line and a body starting with `---` are written verbatim after the closing `---`; file ends with exactly one `\n` | E6.b3 |
| CSV: BOM, CRLF line ends, every field quoted, `"` doubled, header exact, NULL empty, `#settings` last row with the E12.9 object in `body` | E6.c |
| CSV cells with CRLF/commas/quotes inside bodies round-trip through a strict RFC 4180 reader (Python `csv` in the test or the plan-21 reader once merged) | E6.c |
| Two exports of the same brain differ only in `exported_at` (byte compare after masking) | D18 |
| `--bins` validation per D19; `--bins deleted` exports only deleted rows | D19 |
| Facade byte-match (mask `exported_at`, `path`) | P6 |

## Stages

TDD, one failing test at a time. After each stage: gate green, in-session deep
review, Implementation Notes, commit.

1. **Snapshot port.** `store_export_snapshot` reads entries (chosen bins),
   tags, links with both ends inside the set, and prefix settings in one
   read transaction. Store unit tests.
2. **Pure formatting.** `export_format.c`: JSON document (D18 orders), slug
   (E6.b2, D12), front matter, CSV quoting, E12.9 object. Unit tests with the
   fixture.
3. **File I/O.** `fileio.c`: temp + rename, D17 swap, cleanup on failure,
   errno → code (D19). Folder layout per D29/D30.
4. **CLI.** Flags, messages, envelope, human line, help, facade.
5. **Docs + full gate.** What's New (export command; 005 non-goal superseded),
   SKILL.md, engineering notes (`fileio.c` is the only file-writing TU),
   COVERAGE.md, gate-suites.

## How to work (workflow + coding standards)

**Workflow**

1. Branch from an up-to-date, clean `main`. One branch and one PR for this plan.
2. If the brain is reachable, load `workflow:deep-review`,
   `pref:code-review-workflow`, `lesson:review-run-edge-cases` and
   `status:remember` (`workflow:project-status`). If it is not, the in-repo
   docs below are the reference.
3. TDD: a red test first, then the smallest change that turns it green, then
   refactor. One failing test at a time.
4. Do not invent. A case 006 and the locked decisions do not decide → stop,
   grill Itay, record the decision (plan Implementation Notes; brain
   `decision:<slug>` if reachable).
5. After each stage: full gate, in-session review against
   [`docs/STEP_REVIEW_CHECKLIST.md`](../docs/STEP_REVIEW_CHECKLIST.md),
   append Implementation Notes, commit.
6. On pause or block, update the `status:remember` card.

**Coding standards** ([`docs/engineering-notes.md`](../docs/engineering-notes.md),
[`docs/QUALITY.md`](../docs/QUALITY.md))

- SQLite only in `store_sqlite.c`. JSON escaping only in `output.c`.
- Pure formatting (`export_format.c`) has no I/O; file I/O lives in
  `fileio.c` only.
- Bodies keep control characters (data model invariant); export writes them
  verbatim (JSON-escaped in JSON; raw in Markdown and quoted CSV).
- Check every `fwrite`/`fclose`/`fsync`/`rename` return value; `fclose`
  failure is a write failure.
- `goto cleanup` single exit; remove temp files on every failure path.
- Write through `app_out()` / `app_err()` and the diag emitter only.
- Prefer ISO C where equal; POSIX (`mkdir`, `rename`, `unlink`, `rmdir`,
  `opendir`) is allowed in `fileio.c` with the project feature macros.
- New TU: `REMEMBER_LIB_SOURCES` **and** `-Wcast-qual`; direct includes.

**Quality gate** (before every commit that touches C; on macOS prefix
`PATH="$(brew --prefix llvm)/bin:$PATH"`)

```bash
cmake -S . -B build -DCMAKE_C_COMPILER=clang -DREMEMBER_ENABLE_SANITIZERS=ON -DREMEMBER_TEST_VFS=ON
cmake --build build
ctest --test-dir build --output-on-failure
./scripts/lint-all.sh
./scripts/check-coverage.sh                       # 100% functions + 100% effective lines
./scripts/ci-linux.sh
cmake --build build --target leaks-macos          # macOS only
```

## Definition of Done

- [ ] Design log 006 unchanged except Implementation Results rows for E6–
      E6.c and E12.9.
- [ ] Every must-pass row green and in `tests/gate-suites`.
- [ ] D7, D12, D17, D18, D19, D25, D29, D30, P5, P6, P7, P9 implemented
      exactly.
- [ ] Round-trip fixture builder is shared and ready for plan 21.
- [ ] Help, SKILL.md, README What's New, engineering notes updated.
- [ ] Gate: ctest, lint, coverage 100/100, `ci-linux.sh` all green.
- [ ] Second review done; no open **blocking** item.
- [ ] `implementation-plans/INDEX.md` row marked ✅.

## Second review handover

Hand this to an independent reviewer (a fresh session that did not write the
code). The reviewer runs `workflow:deep-review` if the brain is reachable,
else [`docs/STEP_REVIEW_CHECKLIST.md`](../docs/STEP_REVIEW_CHECKLIST.md),
and verifies by **running**, not reading.

**Package to hand over**

- Branch, PR link, commit range, this plan, 006 §5 + E12.9, locked decisions.
- Gate output (ctest, lint, coverage, `ci-linux.sh`).
- A sample export of the fixture in all three formats (attach to the PR).

**Probe these (expected result in brackets)**

1. Export the fixture three times [byte-identical except `exported_at`].
2. `python3 -c 'import json,sys; json.load(open(sys.argv[1]))' out.json`
   [valid]; `python3 -c 'import csv; list(csv.reader(open("out.csv", newline="", encoding="utf-8-sig")))'`
   [valid, 12 columns per row].
3. Open a Markdown file whose body contains `---` lines [front matter ends
   at the second line `---` only; body verbatim].
4. `export --format md --out existing_dir --force` while watching with
   `ls -la` [swap; no leftovers].
5. Fill a small tmpfs and export there [`disk_full`; destination unchanged;
   no temp].
6. `export --bins live` with a link from a live to a deleted memory [link
   absent].
7. `strings out.json | grep device_id` [only prefix `items` device ids and
   VV keys; never the sidecar file content as such].

**Hot spots**

- Determinism: every collection sorted per D18; no reliance on SQL row order.
- CSV quoting of bodies with CR, LF, `"` and the BOM only once at file start.
- D17 swap ordering and cleanup.
- `related` edges appear once (stored `from` end).
- 64 KiB body in CSV/JSON (no fixed-size buffers).

**Do not re-open:** formats and layouts in E6.a/E6.b/E6.c; settings shape
E12.9; temp + rename; D17 swap; D18 orders.

**Review output:** append `### <date> — second-opinion deep review (stage N |
FINAL)` under Review Notes. Tag each finding **blocking / important / nit /
praise**. Auto-fix small, local, gate-green issues and say so; send product
or architecture forks to Itay as a grill. Re-run the full gate after any fix.

## Implementation Notes

_(empty; fill per stage)_

## Review Notes

_(empty; second review appends here)_
