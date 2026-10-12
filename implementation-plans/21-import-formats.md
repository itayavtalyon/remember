# 21 — Import JSON, Markdown, CSV + origin + prefix merge (006 §6, §11 E12.6)

## Goal

`remember import` reads the open formats that `export` writes (plan 20),
plus hand-written files, and merges them into the brain with the same rules as
`import --from-db`. Bad records do not stop the import. Every conflict
records where it came from (`origin`). Bucket prefixes merge as an add-wins
set.

```
remember import --from-json FILE | --from-md DIR | --from-csv FILE | --from-db PATH [--origin LABEL]
```

## Source of truth

[`design-logs/006-ui-overhaul-engine.md`](../design-logs/006-ui-overhaul-engine.md)
§0 (EN-01..EN-04), §1 E1.4, §6 (E7–E7.16, X-E03, E13.2, E13.3), §11 E12.6,
§5 (the formats being read: E6.a, E6.b, E6.c, E12.9), Verification E6/E7,
X-E03, E12, E13, plus the locked decisions below. Do not invent. If 006 and
the locked decisions do not decide a case, stop and ask Itay (grill), then
record the answer under Implementation Notes.

## Context (standalone summary)

Baseline: `main` at `a27efca` with plans 15, 16, 19 and 20 merged.

- `store_import(dst, src_path, now, …)` (`src/store_sqlite.c`) implements
  `--from-db`: opens the source read-only (never `store_open`), merges by
  `sync_id` with version vectors (incoming dominates → apply; local dominates
  or equal → keep; concurrent and different → `concurrent_vv` conflict),
  turns unique key/body-hash clashes into `key_clash` / `hash_clash` rows,
  upserts open conflicts by `(sync_id, reason)` (re-import is idempotent),
  then remaps links by `sync_id` with `INSERT OR IGNORE`. It never reads or
  writes a sidecar and never registers foreign devices. It runs in one
  `BEGIN IMMEDIATE`.
- Envelope today: `{"version":1,"action":"imported","inserted","updated","unchanged","conflicts"}`.
  `conflicts --json` rows: `id, sync_id, reason, local, incoming, created_at`.
- Plan 15 added `conflicts.origin` (unused) and v5 sources; plan 16 codes and
  diag; plan 19 `bucket_prefixes` + `settings` + pure `bucket_prefixes.c`;
  plan 20 the export formats and the shared round-trip fixture
  (`tests/fixture_roundtrip.c`).
- Keys/tags are validated by `normalize_token` (log 001 rules); bodies by
  `body_trim_copy` (trim, valid UTF-8, ≤ 64 KiB).

Rules this plan implements (restated from 006):

| ID | Rule |
|----|------|
| E7 | `import --from-json FILE | --from-md DIR | --from-csv FILE | --from-db PATH [--origin LABEL]`. |
| E7.1 | Merge by `sync_id` + VV compare: incoming dominates → take it; local dominates → keep; equal → unchanged; concurrent → `concurrent_vv` conflict. Key / body-hash clashes → `key_clash` / `hash_clash`. Exit 0 with conflicts. |
| E7.2 | Record with `sync_id` and no `version_vector`: unchanged when every field equals the local row (**D23: content fields only**), else `concurrent_vv`. Never overwrites. |
| E7.3 | Record without `sync_id`: added with a new `sync_id` under the normal add rules, except a key clash records `key_clash` (no keyed upsert). Missing timestamps = now. Missing source = `unknown`. |
| E7.4 | A `.md` file without front matter imports as one memory: the whole file, trimmed, is the body. |
| E7.5 | Links apply after all entries. A link to a missing `sync_id` increments `skipped`; `errors` gains `missing link target: <sync_id>`. |
| E7.6 | Bad records do not stop the import. Output `{"version":1,"action":"imported","inserted","updated","unchanged","conflicts","skipped","errors":[{"file","line","message"}],"prefixes_merged":bool}`. |
| E7.7 | No zip. |
| E7.8 | Every import source merges bucket prefixes by E12.6. |
| E7.9 | One import = one transaction. JSON that does not parse, CSV that does not parse, a `--from-md` path that is not a directory → exit 1 `import_parse`, brain unchanged. `--from-db` keeps the E4 database codes. |
| E7.10 | A parsed file with bad records commits the good records and exits 0; `errors` lists each bad record. |
| E7.11 | Markdown `settings.json` and the CSV `#settings` body are the E12.9 object. Import reads `bucket_prefixes`, ignores other keys. `#settings` is not a memory. |
| E7.12 | Missing `settings.json` = empty object. Present but unparseable → exit 1 `import_parse`, brain unchanged. |
| E7.13 | Also exit 1 `import_parse`, brain unchanged: JSON root `format` ≠ `remember-export`; JSON root `version` ≠ `1`; CSV header ≠ the E6.c header. |
| E7.14 | `--from-md`: when `memories/`, `expired/` or `deleted/` exists, import only `*.md` directly inside those three; otherwise only `*.md` directly in the directory. No recursion. Ignore `README.txt` and every other file. |
| E7.15 | Bad record when: body missing; body empty after trim; body > 65536 bytes; `sync_id` present and not canonical UUID (`8-4-4-4-12` hex, either case); a present timestamp not `YYYY-MM-DDTHH:MM:SS.mmmZ`; `source` present and not `human|agent|tool|share|unknown`; a present key or tag fails the log-001 key/tag checks; `version_vector` present and not a JSON object of canonical-UUID device id → JSON integer > 0; a link object lacks `to` or `kind`; link `kind` not `related|cites|supersedes`; a `sync_id` repeats in the file (the second copy is bad); a CSV row's column count differs from the header; a JSON entry is not an object. |
| E7.16 | Messages: `body is missing`, `body is empty`, `body is too long`, `sync_id is not a UUID`, `invalid timestamp`, `invalid source`, `invalid key or tag`, `invalid version_vector`, `invalid link`, `duplicate sync_id`, `column count does not match the header`, `entry is not an object` (**plus D33 `invalid front matter`, P8 `duplicate field`**). |
| X-E03 | No import reads or writes a sidecar or changes the destination device id. A test covers each format. |
| E13.2 | `conflicts.origin` = basename of the `--from-*` path (`--from-md`: the folder name). `--origin LABEL` (max 64 bytes) overrides. Longer → exit 1 `usage` `origin is too long (max 64 bytes)`. |
| E13.3 | `conflicts --json` adds `"origin"` (string or null) after `"created_at"`. |
| E1.4 | `--from-db` accepts v4 and v5 sources, read-only, never migrated; a missing `origin` column / `bucket_prefixes` / `settings` means absent. |
| E12.6 | Merge: for each prefix in the union, `added_at = max`, `removed_at = max` (NULL lowest), `device_id` from the side of the later timestamp (ties: the larger device id). The order with the later `updated_at` wins (ties: local). No conflict rows. `prefixes_merged` = true when any row or the order changed. |

## Locked decisions (grill 2026-10-12; index in [`006-locked-decisions.md`](006-locked-decisions.md))

| # | Decision |
|---|----------|
| D3 | Own strict RFC 8259 JSON reader `src/jsonread.c/.h`: pure, no I/O, returns a tree (or a pull API) with the 1-based line of every value start; limits: nesting depth 64 (named constant, tested); no document size cap — the file is read whole into memory (P11); rejects trailing garbage, NaN/Infinity, invalid UTF-8, lone surrogates. Unit-tested under ASan/UBSan in `remember_store_tests`. No vendored JSON library; never SQLite JSON1 for this. |
| D20 | `errors[]` `file`/`line`: JSON → file = basename, line = line of the entry's (or link's) `{`. CSV → basename, physical line where the record starts (header = line 1). Markdown → path relative to the import folder (`memories/x--abc.md`), line = the offending front-matter line, or 1 for whole-file problems. `missing link target` uses the link's line. |
| D21 | `skipped` = bad records + skipped links. Every skipped item has exactly one `errors[]` entry: `skipped == len(errors)`. |
| D22 | Record without `sync_id`: same body as an existing keyless memory → union tags into it (keep its `sync_id`, `source`, `created_at`); counted `updated` if tags changed, else `unchanged`. A keyed record whose key exists → `key_clash` (no upsert). Otherwise insert with a new UUIDv7 → `inserted`. |
| D23 | E7.2 compares key, body, tags (as a set), `expires_at`, `deleted_at`. Timestamps and source are ignored. |
| D25 | CSV `links` cell = `[{to, kind, created_at, updated_at}]` for edges whose stored `from` is that row (Markdown shape). |
| D31 | JSON `null` or an empty CSV cell = absent for key, expires_at, deleted_at, version_vector, created_at, updated_at, source, tags, links, sync_id. `sync_id: null` → E7.3; `version_vector: null` → E7.2. Null/empty body → `body is missing`. |
| D32 | Front matter exists only when line 1 is exactly `---` and a later line is exactly `---`. Otherwise E7.4 (whole file trimmed = body). |
| D33 | Unknown JSON members and unknown front-matter fields are ignored (`bin` included). A repeated known front-matter field, a front-matter line not shaped `<field>: <JSON>`, or a value that is not valid JSON → bad record `invalid front matter`. |
| D34 | Links from every source: canonicalize `related` to (min, max); `INSERT OR IGNORE`; keep the record's `created_at`/`updated_at` (missing → now); no endpoint `updated_at`/VV bump; existing triple unchanged. A `supersedes` link that would create a cycle → skipped, error `invalid link`. Add the same cycle check to `--from-db`. |
| D35 | A valid `sync_id` in either case is lowercased on read, before merge and before E7.15 duplicate detection. Link `from`/`to` and `version_vector` device ids are lowercased the same way. Stored and matched lowercase. |
| P10 | The CSV reader accepts the file with or without the BOM and with CRLF or LF record ends (E7.13 checks only the header text). Exports always write BOM + CRLF (E6.c). |
| P7 | A repeated `--from-*` or `--origin` → `usage` `duplicate <flag>`. |
| P8 | A JSON entry object with a duplicate member name → bad record `duplicate field`. |
| P9 | Zero or several `--from-*` → `usage` `import requires exactly one of --from-json, --from-md, --from-csv, --from-db`. |

Derived rules (from the above; no new choice):
- Markdown link objects get `from` = the file's own `sync_id`; a file with
  links but no `sync_id` cannot be a link source → each such link is skipped
  as `invalid link`.
- The JSON top-level `links` array uses `{from, to, kind, created_at,
  updated_at}`; a link lacking `from` is `invalid link`.
- `bin` and `exported_at`, `engine`, `schema` are read for nothing (E6.2,
  D33).

Anything 006 and this table do not decide: stop and ask Itay. Do not choose.

## Prerequisites

- **Plans 15, 16, 19, 20 merged.** If any is missing, stop.

## Scope

**In:** `jsonread.c`; JSON/MD/CSV readers producing one in-memory record
set (entries, links, prefix settings, per-record file/line); validation
(E7.15/E7.16, D31–D33, P8); a store port that applies a record set in one
transaction through the **same** merge code as `--from-db` (refactor
`store_import` so both paths share it); D22 add path; D34 links + cycle check
(also for `--from-db`); E12.6 prefix merge (all sources, incl. `--from-db`
v5 sources; v4 sources have none); `origin` (E13.2/E13.3); envelope with
`skipped`, `errors`, `prefixes_merged` for **every** source; X-E03 tests;
round-trip tests; help, skill, What's New.

**Out:** zip (E7.7); registering devices (Q12, X-E03); export changes.

## Files

New `src/jsonread.c/.h`, new `src/import_read.c/.h` (format readers →
`ImportSet`; pure parsing; file reads only through a small `fileio.c`
helper from plan 20), new `src/import_validate.c/.h` (E7.15/E7.16; pure),
all in `REMEMBER_LIB_SOURCES` + `-Wcast-qual`; `src/cmd_import.c` (flags,
`--origin`, P7/P9, envelope), `src/store.h` (`ImportSet`, `ImportRecord`,
`ImportLink`, `ImportPrefixes`; `store_import_set(Store *, const ImportSet *,
const char *origin, const char *now, StoreImportResult *)`; `store_import`
keeps its signature + `origin`), `src/store_sqlite.c` (shared merge, D22,
D34, E12.6, origin column write, v4/v5 source probing), `src/output.c/.h`
(envelope; `origin` in conflicts), `src/bucket_prefixes.c` (pure E12.6
merge function next to the plan-19 effective list), tests: new
`tests/test_import_formats.c` (suite `import_formats`), unit tests for
`jsonread`, `import_read`, `import_validate`, `bucket_prefixes` merge in
`remember_store_tests`, `tests/test_sync_import.c` (envelope additions;
`origin`; cycle check), `tests/test_facade.c`, `tests/gate-suites`,
`tests/COVERAGE.md`; docs: help, `skills/remember/SKILL.md`, `README.md`,
`docs/engineering-notes.md` (import bullet).

## Must-pass tests

| Check | Proves |
|-------|--------|
| Round trip: fixture → export JSON/MD/CSV → import each into an empty brain → export again → equal to the first export except `exported_at` | Verification E6/E7 |
| Second import of the same file → only `unchanged`; `prefixes_merged` false; no new conflicts | Verification E6/E7, idempotence |
| JSON that does not parse; root `format` wrong; root `version` 2; CSV header wrong; CSV unparseable (unterminated quote); `--from-md` on a file; `settings.json` unparseable → each exit 1 `import_parse`, brain byte-identical (compare `sqlite3 .dump`) | E7.9, E7.12, E7.13 |
| Each E7.15 case once → one `errors[]` entry with the E7.16 message, D20 file/line, good records committed, exit 0 | E7.10, E7.15, E7.16, D20 |
| `skipped == len(errors)` in every test | D21 |
| Missing link target → `missing link target: <sync_id>` | E7.5 |
| Uppercase `sync_id` matches the local lowercase row and is stored lowercase; uppercase + lowercase copies of one id in a file → second is `duplicate sync_id`; uppercase VV device ids and link ends lowercased | E7.15, D35 |
| No `sync_id`: same body as a keyless memory → tags unioned, `updated`; keyed clash → `key_clash`; new → `inserted` with a fresh v7 | D22, E7.3 |
| `sync_id` + no VV: content equal → unchanged; differs → `concurrent_vv`; never overwrites | E7.2, D23 |
| `sync_id` + VV: dominate / dominated / equal / concurrent per E7.1 | E7.1 |
| Missing timestamps → now; missing source → `unknown` | E7.3 |
| `.md` without front matter, and with only an opening `---` → whole file trimmed is the body | E7.4, D32 |
| Unknown members/fields ignored; repeated front-matter field / malformed line → `invalid front matter`; duplicate JSON member → `duplicate field` | D33, P8 |
| `null` fields and empty CSV cells behave as absent | D31 |
| `--from-md` file selection: with `memories/` present only the three folders; without, only top-level `*.md`; no recursion; `README.txt` ignored | E7.14 |
| `#settings` CSV row and `settings.json` merge prefixes; never a memory | E7.11 |
| Prefix merge scenarios: add wins, re-add after remove, removal after add, order with later `updated_at` wins (ties local), device id tie-break, `prefixes_merged` true only on change; `--from-db` v5 source merges, v4 source has none | E12.6, E7.8, Verification E12 |
| Links: related canonicalized; existing triple unchanged; no endpoint bump; supersedes cycle skipped with `invalid link` (JSON, MD, CSV **and** `--from-db`) | D34 |
| `conflicts.origin` = basename (file) / folder name (md) / db basename; `--origin x` overrides; 65-byte label → `usage` `origin is too long (max 64 bytes)` | E13.2 |
| `conflicts --json` has `origin` after `created_at` (null for pre-0.3.0 rows) | E13.3 |
| Sidecar bytes and mtime unchanged after each of the four sources; `device` unchanged | X-E03 |
| One transaction: a fault injected after half the records leaves the brain unchanged | E7.9 |
| P7/P9 usage messages | P7, P9 |
| Facade byte-match for each source (mask timestamps, sync_ids, VVs) | EN-03 |

## Stages

TDD, one failing test at a time. After each stage: gate green, in-session deep
review, Implementation Notes, commit.

1. **`jsonread.c`** (D3) with line tracking, limits, strictness. Unit tests
   incl. adversarial inputs (deep nesting, huge numbers, invalid UTF-8,
   surrogates, duplicate members reported to the caller).
2. **Readers + validation.** JSON, CSV (RFC 4180, BOM, CRLF/LF, quoted
   newlines), Markdown (D32, D33, E7.14) → `ImportSet` with file/line;
   E7.15/E7.16 + P8 + D31. Whole-file failures → `import_parse` before any
   write. Pure unit tests over fixture files.
3. **Store apply.** Refactor `store_import` to share one merge path with
   `store_import_set`; D22; D23; origin; D34 links (+ cycle check for
   `--from-db`); one `BEGIN IMMEDIATE`. D35 lowercasing happens in the
   reader (stage 2).
4. **Prefix merge.** Pure E12.6 merge in `bucket_prefixes.c`; applied inside
   the same transaction for every source; `prefixes_merged`.
5. **CLI + output.** Flags, P7/P9, `--origin`, envelope for all sources,
   `conflicts` `origin`, help, facade.
6. **Round trip + docs + full gate.** Round-trip tests with the plan-20
   fixture for all three formats; SKILL.md (import formats; always run
   `conflicts` afterwards; `errors[]` meaning); What's New; engineering
   notes; COVERAGE.md; gate-suites.

## How to work (workflow + coding standards)

**Workflow**

1. Branch from an up-to-date, clean `main`. One branch and one PR for this plan.
2. If the brain is reachable, load `workflow:deep-review`,
   `pref:code-review-workflow`, `lesson:review-run-edge-cases` and
   `status:remember` (`workflow:project-status`), plus the plan-14 decisions
   `decision:sync-import-reimport-idempotent` and
   `decision:sync-import-hash-demote`. If it is not, the in-repo docs below
   are the reference.
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

- SQLite only in `store_sqlite.c`. Parsing never touches the store; the store
  never parses files.
- One merge path for all four sources (no copy of the VV rules).
- Untrusted input: every length and depth bounded; no recursion without a
  depth cap; no fixed buffers for bodies (64 KiB + JSON escapes).
- Reuse `normalize_token` and `body_trim_copy` for E7.15 key/tag/body checks;
  reuse the plan-17 canonical timestamp check.
- `goto cleanup` single exit; NULL-safe frees for every record set.
- Write through `app_out()` / `app_err()` and the diag emitter only.
- User-facing strings ASCII; `errors[].message` texts exactly as E7.16.
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

- [ ] Design log 006 unchanged except Implementation Results rows for E1.4,
      E7.*, X-E03, E12.6, E13.2, E13.3.
- [ ] Every must-pass row green and in `tests/gate-suites`.
- [ ] D3, D20–D23, D25, D31–D35, P7–P10 implemented exactly.
- [ ] `--from-db` and the file formats share one merge path; `--from-db`
      envelope gains `skipped`, `errors`, `prefixes_merged`.
- [ ] Round trip equal for JSON, Markdown, CSV.
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

- Branch, PR link, commit range, this plan, 006 §6 + E12.6, locked decisions.
- Gate output (ctest, lint, coverage, `ci-linux.sh`).
- The refactor map of `store_import` (old → shared merge path).

**Probe these (expected result in brackets)**

1. Round trip each format by hand and `diff` the two exports [only
   `exported_at` differs].
2. Hand-edit a Markdown export: change a body and drop `version_vector`
   [`concurrent_vv` conflict, origin = folder name]; leave it unchanged
   [unchanged].
3. Feed fuzzed JSON/CSV (truncate at every byte offset of a valid export)
   [each either `import_parse` with the brain unchanged, or exit 0 with bad
   records listed; never a crash; ASan clean].
4. 200-level nested JSON array [`import_parse`, no stack overflow].
5. `import --from-csv` of an export re-saved with LF record ends and no BOM
   [rows import (P10)].
6. Re-import twice [second run all `unchanged`, no duplicate conflicts].
7. `stat` the sidecar before/after each source [unchanged].
8. A supersedes cycle across two files [second import skips with
   `invalid link`].

**Hot spots**

- Whole-file failure must be detected before the transaction writes anything.
- Line numbers for CSV records that contain quoted newlines.
- Shared merge path: plan-14 idempotence and hash-demote decisions must still
  hold for `--from-db`.
- E12.6 tie-breaks (NULL lowest; larger device id; order ties → local).
- `prefixes_merged` false on an identical re-import.

**Do not re-open:** one transaction; bad records do not stop the import;
`import_parse` cases; no foreign device registration; origin rules; E12.6;
D20–D23, D31–D35.

**Review output:** append `### <date> — second-opinion deep review (stage N |
FINAL)` under Review Notes. Tag each finding **blocking / important / nit /
praise**. Auto-fix small, local, gate-green issues and say so; send product
or architecture forks to Itay as a grill. Re-run the full gate after any fix.

## Implementation Notes

_(empty; fill per stage)_

## Review Notes

_(empty; second review appends here)_
