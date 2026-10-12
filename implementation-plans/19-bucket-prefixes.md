# 19 — Brain settings: bucket prefixes (`config`) (006 §11)

## Goal

Store the list of **bucket prefixes** (for example `project:`, `status:`) in
the brain itself, so every app and agent shows the same buckets. The list is
an add-wins set with tombstones, capped at 8 visible prefixes. The new
command is:

```
remember config get bucket-prefixes
remember config set bucket-prefixes P1 … Pn
remember config set bucket-prefixes --clear
```

Merging prefixes from another brain (E12.6) belongs to the import plan (21),
and writing them into exports (E12.9) belongs to the export plan (20).

## Source of truth

[`design-logs/006-ui-overhaul-engine.md`](../design-logs/006-ui-overhaul-engine.md)
§0 (EN-01..EN-04), §11 (E12.2–E12.5, E12.7, E12.8), §1 E1.3 (tables), plus
the locked decisions below. Do not invent. If 006 and the locked decisions do
not decide a case, stop and ask Itay (grill), then record the answer under
Implementation Notes.

## Context (standalone summary)

Baseline: `main` at `a27efca` with plans 15, 16 and 18 merged.

- Plan 15 created, empty and unused:
  `bucket_prefixes(prefix TEXT PRIMARY KEY, added_at TEXT NOT NULL, removed_at TEXT, device_id TEXT NOT NULL)`
  and `settings(name TEXT PRIMARY KEY, value TEXT NOT NULL, updated_at TEXT NOT NULL)`.
- The local device id is the sidecar `<db-path>.device_id` (plan 15). The
  store holds it in `Store.device_id`.
- Plan 18 vendored utf8proc behind `src/unicode.h` (NFC, general category).
- Plan 16 gives codes (`usage`, …) and the diag emitter.
- Commands follow `cli_parse` → `cmd_*` → `store_*` → `output_*`.

Rules this plan implements (restated from 006):

| ID | Rule |
|----|------|
| E12.2 | A prefix is **live** when `removed_at IS NULL OR added_at >= removed_at`. A row that is not live is a tombstone. Tombstones are kept. |
| E12.3 | With an empty table, `config get` reports `project:`, `status:` with `"default":true` and writes nothing. The first write stores those defaults with `added_at = "1970-01-01T00:00:00.000Z"`, then applies E12.5, in the same transaction. |
| E12.4 | A prefix is 1–32 code points of `\p{L}`, `\p{Nd}`, `-`, `_`, `.`, then one `:`. ASCII folds to lowercase. Errors exit 1, code `usage`: `invalid bucket prefix: <p>`, `duplicate bucket prefix: <p>`, `too many bucket prefixes (max 8)`. |
| E12.5 | `config set bucket-prefixes P1 … Pn` (n ≤ 8; `--clear` = an empty list), one transaction, now = current time: (1) each Pi that is not live gets `added_at = now`, `device_id = local`; (2) each **visible** live prefix not in the list gets `removed_at = now`, `device_id = local` — overflow prefixes are not touched; (3) `settings.bucket_prefix_order` = `[P1 … Pn]` with `updated_at = now`. |
| E12.7 | The effective list is computed on every read: start from `bucket_prefix_order` minus non-live entries; append live prefixes missing from it by `added_at`, then prefix; when more than 8 are live, keep the 8 earliest-added in that order and report the rest as `overflow`. Removing a visible prefix lets the earliest-added overflow prefix fill the slot at the end. |
| E12.8 | `config get bucket-prefixes --json` → `{"version":1,"name":"bucket_prefixes","value":[…],"overflow":[…],"updated_at":"…Z"|null,"default":bool}`. Human: one prefix per line, then `(not shown: x:, y:)` when overflow is not empty. |

## Locked decisions (grill 2026-10-12; index in [`006-locked-decisions.md`](006-locked-decisions.md))

| # | Decision |
|---|----------|
| D2 | Unicode categories (`\p{L}`, `\p{Nd}`) come from utf8proc through `src/unicode.h` (vendored in plan 18). No other Unicode source. |
| D14 | `config set bucket-prefixes` with no prefixes and no `--clear` → exit 1 `usage` `config set bucket-prefixes requires at least one prefix (use --clear to remove all)`. A successful `set` prints exactly what `config get bucket-prefixes` prints after the write (human and JSON). |
| D15 | Unknown setting name → `usage` `unknown setting: <name>` (only `bucket-prefixes` exists). `config get` without a name → `usage` `config get requires a setting name`. `config` alone, or an unknown verb → `usage` `config requires get or set`. |
| D16 | Each input prefix is NFC-normalized, then ASCII-lowercased, then validated and stored. Duplicate detection runs after that normalization. E12.3 default rows get the **local** sidecar device id. |
| P1 | `--clear` together with prefixes → `usage` `cannot combine --clear with prefixes`. |
| P6 | `config` gets a registry row, a help topic and a facade byte-match test. |

The CLI name is `bucket-prefixes`. The JSON `name` and the settings row use
`bucket_prefixes` / `bucket_prefix_order` as 006 writes them.

Anything 006 and this table do not decide: stop and ask Itay. Do not choose.

## Prerequisites

- **Plan 15 merged** (tables, sidecar device id).
- **Plan 16 merged** (codes, diag emitter).
- **Plan 18 merged** (utf8proc + `unicode.h`). If not, stop.

## Scope

**In:** `config get|set bucket-prefixes` (CLI, store, output, help, facade);
validation (E12.4 + D16); defaults (E12.3); set algorithm (E12.5); effective
list + overflow (E12.7); JSON/human output (E12.8); a store read port that
returns the full row set + order (used later by export and import);
SKILL.md line E12.10 is **plan 22**, not here; tests; What's New.

**Out:** merge (E12.6, plan 21); export object (E12.9, plan 20); any other
setting; using prefixes in search or list.

## Files

New `src/cmd_config.c` (in `REMEMBER_LIB_SOURCES` + `-Wcast-qual`),
`src/cli.c/.h` (`config` in `k_commands[]`), `src/remember_app.c` (dispatch +
help), `src/normalize.c/.h` (pure `bucket_prefix_normalize` using
`unicode.h`: NFC, ASCII lowercase, E12.4 shape; returns a status, no I/O),
`src/store.h` (`BucketPrefixRow`, `BucketPrefixState` {rows, order,
order_updated_at, is_default}; `store_bucket_prefixes_get`,
`store_bucket_prefixes_set`), `src/store_sqlite.c` (SQL + one transaction),
new pure `src/bucket_prefixes.c/.h` (E12.2 live test + E12.7 effective list
over in-memory rows + order; plans 20 and 21 reuse it; in
`REMEMBER_LIB_SOURCES` + `-Wcast-qual`), `src/output.c/.h` (E12.8
envelopes), tests: new
`tests/test_config.c` (suite `config`), unit tests for the pure functions in
`remember_store_tests`, `tests/test_facade.c`, `tests/gate-suites`,
`tests/COVERAGE.md`; docs: help, `README.md` What's New,
`docs/engineering-notes.md`.

## Must-pass tests

| Check | Proves |
|-------|--------|
| Fresh brain: `config get bucket-prefixes --json` → value `["project:","status:"]`, overflow `[]`, `updated_at` null, `default` true; tables still empty afterwards | E12.3 defaults, no write |
| Human get on fresh brain → `project:` / `status:` lines | E12.8 |
| First `set a: b:` stores `project:`/`status:` with `added_at` 1970 + local device, then tombstones them (`removed_at = now`), adds `a:`/`b:`; order `[a:, b:]`; output equals a following `get` | E12.3 + E12.5 + D14 |
| `set` validation: `Project` (no colon), `a:b:` (two colons), 33-code-point name, `:`, `a b:`, `a/b:` → `invalid bucket prefix: <p>`; `A:` and `a:` together → `duplicate bucket prefix: a:`; 9 prefixes → `too many bucket prefixes (max 8)`; brain unchanged after each | E12.4 |
| Hebrew `פרויקט:`, digits `٣:` (Arabic-Indic digit), `é:` vs `e`+U+0301`:` (NFC → duplicate) accepted / detected | E12.4 + D16 |
| ASCII fold: `PROJECT:` stored as `project:` | E12.4 |
| `--clear` → empty value, all visible prefixes tombstoned, order `[]` | E12.5 |
| `--clear` + prefixes → `cannot combine --clear with prefixes`; no args → D14 message | P1, D14 |
| Re-add a tombstoned prefix → `added_at = now` > `removed_at` → live again | E12.2, "re-add" |
| Set the same list twice → second set changes only `bucket_prefix_order.updated_at` | E12.5 idempotence |
| Order: `set b: a:` → value `[b:, a:]` | E12.7 order |
| Cap and overflow: fixture with 10 live rows (inserted by SQL with distinct `added_at`) → value = 8 earliest-added (order rule), overflow = the other 2; human prints `(not shown: x:, y:)` | E12.7 |
| `set` that drops one visible prefix leaves overflow rows untouched and the earliest-added overflow prefix appears at the end of `value` | E12.5(2), E12.7 |
| Live prefix missing from `bucket_prefix_order` (inserted by SQL) is appended by `added_at`, then prefix | E12.7 |
| `config` errors per D15 | D15 |
| `--json-errors` usage error is one JSON line | plan 16 |
| Facade byte-match for get/set/clear | P6 |

## Stages

TDD, one failing test at a time. After each stage: gate green, in-session deep
review, Implementation Notes, commit.

1. **Pure functions.** `bucket_prefix_normalize` (D16, E12.4) and the
   effective-list computation (E12.2, E12.7) over in-memory rows. Unit tests.
2. **Store ports.** Read all rows + order + `updated_at`; `set` in one
   `BEGIN IMMEDIATE` (E12.3 default seeding, E12.5 steps).
3. **CLI + output.** `config get|set`, D14/D15/P1 messages, JSON/human
   output (E12.8), help, facade.
4. **Docs + full gate.** What's New (bucket prefixes live in the brain;
   `config` command), engineering notes (settings tables; effective-list is
   computed on read), COVERAGE.md, gate-suites.

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

- SQLite only in `store_sqlite.c`; utf8proc only in `unicode.c`.
- Pure logic (normalize, effective list) has no I/O and is unit-tested under
  ASan/UBSan.
- One source of truth for the defaults (`project:`, `status:`) and the cap (8).
- Timestamps `YYYY-MM-DDTHH:MM:SS.mmmZ`; one `now` per command.
- `goto cleanup` single exit; NULL-safe frees.
- Write through `app_out()` / `app_err()` and the diag emitter only.
- User-facing strings are ASCII (prefix values echoed back are data).
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

- [ ] Design log 006 unchanged except Implementation Results rows for E12.2–
      E12.5, E12.7, E12.8.
- [ ] Every must-pass row green and in `tests/gate-suites`.
- [ ] D2, D14, D15, D16, P1, P6 implemented exactly.
- [ ] Effective-list computation is a pure function reusable by plans 20/21.
- [ ] Help, README What's New, engineering notes updated.
- [ ] Gate: ctest, lint, coverage 100/100, `ci-linux.sh` all green.
- [ ] Second review done; no open **blocking** item.
- [ ] `implementation-plans/INDEX.md` row marked ✅.

## Second review handover

Hand this to an independent reviewer (a fresh session that did not write the
code). The reviewer runs `workflow:deep-review` if the brain is reachable,
else [`docs/STEP_REVIEW_CHECKLIST.md`](../docs/STEP_REVIEW_CHECKLIST.md),
and verifies by **running**, not reading.

**Package to hand over**

- Branch, PR link, commit range, this plan, 006 §11, locked decisions.
- Gate output (ctest, lint, coverage, `ci-linux.sh`).
- Any new grill answers.

**Probe these (expected result in brackets)**

1. Fresh brain: `config get bucket-prefixes`, then `sqlite3 x.db "select
   count(*) from bucket_prefixes"` [defaults shown; 0 rows].
2. `config set bucket-prefixes a: b:`; dump `bucket_prefixes` [defaults
   tombstoned with 1970 `added_at`, local device; `a:`/`b:` live].
3. Same-millisecond edge: a prefix with `added_at == removed_at` [live,
   per `>=`].
4. Insert 10 live rows by SQL, `config get` [8 shown, `(not shown: …)`];
   `set` the 8 minus one [earliest overflow fills the end; overflow rows'
   timestamps unchanged].
5. NFC duplicate: `config set bucket-prefixes $'é:' é:` [duplicate].
6. `config set bucket-prefixes` [D14 message]; `config get foo` [D15].
7. Facade parity for get/set.

**Hot spots**

- "Visible" vs "live": E12.5 step 2 only tombstones visible prefixes.
- Default seeding must happen in the same transaction as the first write.
- Code-point counting (32) after NFC, not bytes.
- Ties in E12.7 ordering (`added_at`, then prefix).

**Do not re-open:** add-wins set with tombstones; cap 8; defaults `project:`
and `status:`; computed-on-read effective list; CLI name `bucket-prefixes`.

**Review output:** append `### <date> — second-opinion deep review (stage N |
FINAL)` under Review Notes. Tag each finding **blocking / important / nit /
praise**. Auto-fix small, local, gate-green issues and say so; send product
or architecture forks to Itay as a grill. Re-run the full gate after any fix.

## Implementation Notes

_(empty; fill per stage)_

## Review Notes

_(empty; second review appends here)_
