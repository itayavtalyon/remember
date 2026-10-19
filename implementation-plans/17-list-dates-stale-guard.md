# 17 — List sort + date filters, stale-write guard (006 §4, §7)

## Goal

- `list --sort updated|created` orders memories by update or creation time.
- `list --since TS --until TS` (and `search --since/--until`) filter by time.
- `update … --if-updated-at TS` refuses to write when the memory changed
  after the caller read it (code `stale`), so an app cannot overwrite a newer
  edit by mistake.

## Source of truth

[`design-logs/006-ui-overhaul-engine.md`](../design-logs/006-ui-overhaul-engine.md)
§0 (EN-01..EN-04), §4 (E5.1–E5.2d), §2 E5.3, §7 (E8.1), plus the locked
decisions below. Do not invent. If 006 and the locked decisions do not decide
a case, stop and ask Itay (grill), then record the answer under Implementation
Notes.

## Context (standalone summary)

Baseline: `main` at `a27efca`.

- `list` and `search` share filter and paging parsing in `src/cmd_query.c`.
  The store takes a `ListQuery` (tags AND, `source`, `key`, `limit`,
  `offset`); `SearchQuery` embeds a `ListQuery`. The shared SQL filter builder
  is `list_append_filters` in `src/store_sqlite.c`.
- `list` orders by `updated_at DESC, id DESC`. There is no created-at sort and
  no date filter.
- Timestamps are stored as `YYYY-MM-DDTHH:MM:SS.mmmZ` (fixed 3-digit ms), so
  string order equals time order.
- `src/commands_common.c` already parses `--expires`: `YYYY-MM-DD` is the
  **local end of day** (via `mktime`), and `…Z` accepts **any** number of
  fraction digits. Neither rule is the E5.2 rule, so do not reuse
  `parse_expires_to_iso` as is. Reuse its small helpers (`match_mask`,
  `parse_n_digits`, `format_iso_mmmz`, `parse_iso_mmmz`).
- `update` lives in `src/cmd_locator.c` → `store_update` (one write
  transaction, bumps `updated_at` and the version vector).

Rules this plan implements (restated from 006):

| ID | Rule |
|----|------|
| E5.1 | `list --sort updated|created` (default `updated`) orders by `<field> DESC, id DESC`. |
| E5.2 | `list --since TS --until TS` filter on the sort field. `since` is inclusive; `until` is exclusive. |
| E5.2a | Canonical TS `YYYY-MM-DDTHH:MM:SS.mmmZ` is accepted. |
| E5.2b | `YYYY-MM-DD` is accepted: the **local start** of that day, converted to the canonical UTC form. |
| E5.2c | `YYYY-MM-DDTHH:MM:SSZ` is accepted: the same instant with `.000`. |
| E5.2d | Any other TS → exit 1, code `usage`, message `invalid timestamp: <ts>`. |
| E5.3 | `search` ignores `--sort`. Search `--since` / `--until` use the same TS rule and filter on `updated_at`. |
| E8.1 | `update … --if-updated-at TS`: when the row's `updated_at` is not exactly TS, write nothing, exit 1, code `stale`, message `memory changed since it was read`. |

## Locked decisions (grill 2026-10-12; index in [`006-locked-decisions.md`](006-locked-decisions.md))

| # | Decision |
|---|----------|
| D10 | `--if-updated-at` accepts only the canonical form `YYYY-MM-DDTHH:MM:SS.mmmZ`. Any other form → exit 1 `usage` `invalid timestamp: <ts>`. The check is a byte compare with the stored `updated_at`. |
| D11 | `since >= until` is not an error: the result is empty (exit 0, `count` 0, `total` 0). `search --sort` is validated like `list --sort` (`invalid --sort (use updated or created)`, code `usage`) and then ignored. |
| P4 | The stale check reads `updated_at` inside the same write transaction as the update. No check-then-write race. |

Also locked here, for consistency with the existing flags: a bad `--sort`
value on `list` → exit 1 `usage` `invalid --sort (use updated or created)`.
A repeated flag (`--sort`, `--since`, `--until`, `--if-updated-at`) is a usage
error `duplicate <flag>`, as `duplicate --id` is today. Anything else: stop
and ask.

## Prerequisites

- `main` clean and up to date.
- **Plan 16 merged** (for the `usage` / `stale` codes and `--json-errors`).
  If it is not merged, stop: `stale` needs the diag emitter.

## Scope

**In:** `--sort`, `--since`, `--until` on `list`; `--since`, `--until`, and a
validated-then-ignored `--sort` on `search`; one shared TS parser; store
`ListQuery` gains `sort_field`, `since`, `until`; `--if-updated-at` on
`update`; `STORE_ERR_STALE` → code `stale`; help, skill, What's New; tests.

**Out:** search union/ranking (plan 18; this plan keeps search ranking as is);
`--if-updated-at` on any other command (006 names only `update`); date
filters on `tags`, `related`, `conflicts`.

## Files

`src/commands_common.c/.h` (new `parse_filter_ts(const char *in, char
out[ISO_BUFSIZE])`, shared by list/search/update), `src/cmd_query.c` (flags),
`src/cmd_locator.c` (`--if-updated-at`), `src/store.h` (`ListQuery`:
`StoreSort sort`, `const char *since`, `const char *until`;
`store_update` gains `const char *if_updated_at` or an options struct;
`STORE_ERR_STALE`), `src/store_sqlite.c` (`list_append_filters` binds
`since`/`until` on the right column; ORDER BY per sort; stale check),
`src/commands_common.c` (`store_status_message` + D9 code row for `stale`),
`src/remember_app.c` (help), tests: new `tests/test_list_dates.c` (suite
`list_dates`), `tests/test_update.c`, `tests/test_facade.c`,
`tests/gate-suites`, `tests/COVERAGE.md`; docs: `skills/remember/SKILL.md`,
`README.md` What's New, `docs/engineering-notes.md` (store_list bullet).

## Must-pass tests

| Check | Proves |
|-------|--------|
| Three memories; update the oldest; `list --sort created` keeps creation order (newest first); default and `--sort updated` put the updated one first | E5.1, E5 verification |
| Same `created_at` → `id DESC` tie-break | E5.1 |
| `list --since <today YYYY-MM-DD>` returns only today's memories (fixture rows with yesterday's timestamps written by SQL) | E5 verification |
| `--since` is inclusive and `--until` exclusive at an exact canonical boundary | E5.2 |
| `--sort created --since X` filters on `created_at`; `--sort updated --since X` on `updated_at` | E5.2 |
| `YYYY-MM-DD` = local 00:00:00.000 converted to UTC (run with `TZ=Asia/Jerusalem` and `TZ=America/Los_Angeles`; expected value computed in the test) | E5.2b |
| `…T10:00:00Z` equals `…T10:00:00.000Z` | E5.2c |
| Rejected: `2026-1-01`, `2026-10-12T10:00Z`, `…T10:00:00.1Z`, `…T10:00:00.0000Z`, `…+02:00`, `2026-13-01`, `2026-02-30`, empty, `now` → `usage` `invalid timestamp: <ts>` | E5.2d |
| `since >= until` → exit 0, empty, `total` 0 | D11 |
| `search x --since A --until B` filters on `updated_at`; `search x --sort created` is accepted and ignored (same order as without it); `search x --sort bogus` → usage | E5.3, D11 |
| `--since`/`--until` combine with `--tag`, `--source`, `--key`, bins, `--limit`/`--offset`; `total` counts the filtered set | filters |
| `update --if-version-vector <current version_vector> --text new` succeeds | E8.1 |
| `update --if-version-vector <other vector>` → exit 1, `stale`, `memory changed since it was read`, row unchanged (body, tags, `updated_at`, VV) | E8 verification |
| `--if-version-vector not-a-vector` → `usage` `invalid version vector: not-a-vector` | D10 |
| `--if-version-vector` works with id, `--key`, `--sync-id`, and with `--deleted --undelete` | E8.1 locators |
| `--json-errors` stale → one JSON line, stdout empty | E4.1 + E8 |
| Facade byte-match for `list --sort created --since …` and a stale update | EN-03 |

## Stages

TDD, one failing test at a time. After each stage: gate green, in-session deep
review, Implementation Notes, commit.

1. **TS parser.** `parse_filter_ts` with the three accepted forms, exactly.
   Date-only uses local midnight (`tm_isdst = -1`, `mktime`) then UTC.
   Calendar-invalid dates are rejected (round-trip the fields after `mktime`
   or validate days per month; do not accept `02-30`). Pure unit tests.
2. **Store.** `ListQuery` sort + since/until; one place builds the ORDER BY
   and the time predicates for list (sort field) and search (`updated_at`).
   COUNT and page stay in one read transaction.
3. **CLI list/search.** Flags, D11 validation, help text.
4. **Stale guard.** `--if-updated-at` parse (D10), store compare inside the
   update transaction (P4), `STORE_ERR_STALE` → `stale` (exit 1).
5. **Docs + full gate.** Help, SKILL.md (agents: read `updated_at`, pass it
   back with `--if-updated-at`; on `stale`, re-read and retry), What's New,
   engineering notes, COVERAGE.md, gate-suites.

**Amendment (Itay 2026-10-18, second review of stage 4): stage 4 is reworked as stage 4b before stage 5.** The `updated_at` guard has millisecond resolution. An in-process probe through `remember_run` saw 95 of 500 guarded updates overwrite a write made in the same millisecond. The guard moves to the version vector. These decisions are law for 4b, like D10–D11. Load them first: `decision:plan17-stale-guard-version-vector`, `decision:plan17-upsert-vv-rule`, `decision:plan17-store-update-options-struct`, `decision:plan17-stale-after-bin`, `decision:plan17-cli-tests-prove-wiring`.

4b. **Stale guard on the version vector.** Three commits, each with a full gate:
   1. **`StoreUpdate` options struct, no behavior change.** `store_update(Store *s, long long id, const StoreUpdate *u, const char *now, Entry *out_entry, long long *out_conflict_id)`. Convert all 24 call sites. The NOLINT for swappable parameters goes. Every existing test stays green unchanged in behavior.
   2. **Add-path VV rule.** Both paths onto an existing row: keyed upsert (`STORE_ADD_UPDATED`) and keyless re-add (`STORE_ADD_MERGED`). A write that changes the row bumps `version_vector[local]` and `updated_at` together. A write that changes nothing moves neither, and `add` still prints the existing id. Store tests first, red: a changed keyed upsert bumps the VV; an identical keyed upsert moves neither; a keyless re-add with a new tag bumps both; an identical keyless re-add moves neither; a re-add that revives an expired row bumps both. Check what `maybe_revive` already bumps, so a revive does not bump twice. Then one CLI wiring test.
   3. **Guard.** Replace `--if-updated-at` with `--if-version-vector VV`. The CLI passes the raw string. The adapter parses it with `vv_parse_pairs` and compares it with the row's `version_vector` as maps, a missing device counting as 0, after the bin check and inside the write transaction. `vv_compare` returns 0 for both equal and concurrent, so equality needs its own check. Malformed is the new `STORE_ERR_BAD_VV` → `usage` `invalid version vector: <vv>`. Not equal is `STORE_ERR_STALE` → `stale`. Duplicate and missing value use the house sentences. Store tests carry the semantics: equal, missing-device-as-0, key order and whitespace free, a counter changed, a device added, malformed (not an object, a negative or non-integer counter, an empty string), and a stale row unchanged. Add the in-process same-millisecond probe from this review as a store or facade test: W1, read the VV, W2, then a guarded update must be `stale` every time. CLI tests prove wiring only: the flag reaches the store, the duplicate and missing-value cases, one `--json-errors` stale line, and the facade byte-match.

   Must-pass rows that name `--if-updated-at` now read `--if-version-vector`. The D10 row (`--if-updated-at 2026-10-12` → `invalid timestamp`) becomes a malformed VV → `invalid version vector: <vv>`. Stage 5 writes the 006 Implementation Results rows for E8.1 (amended), D10 (superseded), and the add-path VV rule. SKILL.md tells agents to read `version_vector`, pass it back with `--if-version-vector`, and re-read and retry on `stale`.

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

- SQLite only in `store_sqlite.c`; the store takes parsed canonical strings,
  never raw user input.
- Bind every value (`?N`); never format a timestamp into SQL text.
- Layers: `cli_parse` → `cmd_*` (no SQL) → `output.c` / diag emitter.
- `goto cleanup` single exit; `const` inputs; exhaustive `switch` on
  `StoreSort` with `default:`.
- Write through `app_out()` / `app_err()` and the plan-16 diag emitter only.
- Tests that depend on the clock write fixture rows with fixed timestamps
  (engineering notes: prefer fixed `.mmm` values over `sleep`).
- User-facing strings are ASCII.

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

- [x] Design log 006 unchanged except Implementation Results rows for E5.1–
      E5.3, E8.1 (amended), D10 (superseded), and the add-path version-vector
      rule. Proved by the diff: the Design section is untouched.
- [x] Every must-pass row green and in `tests/gate-suites` (`list_dates`,
      `update`, `store`, `facade`). Proved by `step_gate` and `ci-linux.sh`.
- [x] D10 (as amended: malformed vector → `invalid version vector`), D11, P4,
      and the locked usage messages. Proved by
      `update_if_version_vector_rejects_malformed`,
      `store_list_crossed_bounds_are_empty`, and the store guard tests.
- [x] One TS parser serves list and search (`parse_filter_ts` in
      `test_list_dates.c`). Update does not use it. The guard is the version
      vector (`decision:plan17-stale-guard-version-vector`).
- [x] Help (`list`, `search`, `update`), SKILL.md, README What's New lines
      (`--sort`, `--since`/`--until`, `--if-version-vector`). Help proved by
      `list_and_search_help_name_sort_and_bounds` and
      `update_help_names_the_version_vector_guard`.
- [x] Engineering notes `store_list` bullet updated (sort + time filters),
      plus the version-vector guard and the add-path rule.
- [x] Gate: ctest, lint, coverage 100/100, `ci-linux.sh` all green. See
      Implementation Notes.
- [ ] Second review done; no open **blocking** item.
- [ ] `implementation-plans/INDEX.md` row marked ✅. The flag name in that row
      is already `--if-version-vector`. The mark waits for the second review.

## Second review handover

Hand this to an independent reviewer (a fresh session that did not write the
code). The reviewer runs `workflow:deep-review` if the brain is reachable,
else [`docs/STEP_REVIEW_CHECKLIST.md`](../docs/STEP_REVIEW_CHECKLIST.md),
and verifies by **running**, not reading.

**Package to hand over**

- Branch, PR link, commit range, this plan, 006 §4/§7, locked decisions.
- Gate output (ctest, lint, coverage, `ci-linux.sh`).
- Any new grill answers.

**Probe these (expected result in brackets)**

1. `TZ=Pacific/Kiritimati remember list --since 2026-10-12` and the same with
   `TZ=Pacific/Pago_Pago` [different UTC boundaries; each equals local
   midnight].
2. DST day: `TZ=Asia/Jerusalem remember list --since 2026-10-25`
   (Israel DST ends that night) [local midnight, correct UTC offset].
3. `remember list --since 2026-02-29` and `--since 2026-02-30` [usage
   error for both, since 2026 is not a leap year].
4. `remember list --since 2026-10-12T00:00:00.000Z --until
   2026-10-12T00:00:00.000Z` [exit 0, empty].
5. Two shells: read `updated_at`, update in shell B, then update in shell A
   with `--if-updated-at` [shell A gets `stale`; row has B's edit].
6. `remember --json-errors update 1 --if-updated-at x --text y` [usage
   `invalid timestamp: x`].
7. `remember search foo --sort created` vs `remember search foo` [identical
   output].

**Hot spots**

- Reusing the `--expires` parser by accident (EOD and loose fractions are
  wrong here).
- `mktime` normalizes invalid dates (`02-30` → `03-02`) unless the fields are
  checked after the call.
- The stale check must run inside the update transaction.
- `total` must use the same predicates as the page.

**Do not re-open:** inclusive/exclusive bounds; local start of day; search
filters on `updated_at`; canonical-only `--if-updated-at`; empty result for
`since >= until`.

**Review output:** append `### <date> — second-opinion deep review (stage N |
FINAL)` under Review Notes. Tag each finding **blocking / important / nit /
praise**. Auto-fix small, local, gate-green issues and say so; send product
or architecture forks to Itay as a grill. Re-run the full gate after any fix.

## Implementation Notes

### 2026-10-18 — stage 1 (TS parser)

No new grill. `parse_filter_ts` does not reuse `parse_expires_to_iso`: that helper is local end of day and accepts any fraction length. The filter parser takes the three forms exactly.

- Canonical `YYYY-MM-DDTHH:MM:SS.mmmZ` is copied. `YYYY-MM-DDTHH:MM:SSZ` becomes the same instant with `.000`. `YYYY-MM-DD` is local midnight (`tm_isdst = -1`, `mktime`, then UTC).
- A date that is not a real civil day is rejected before `mktime`. The red test for `2026-02-30` came back `2026-03-01T22:00:00.000Z` until that check landed. After `mktime`, the broken-down local fields must still be that midnight, so a libc normalization is refused. `(time_t)-1` is an error only when `errno` is set, because that value is also a real instant.
- Leap years follow the Gregorian rule (400 / 100 / 4). Seconds stop at 59. The function does not print. On rejection it leaves `out` unchanged.
- `list_dates` is on `tests/gate-suites` now, because the coverage gate only runs suites on that line and the new functions live in `src/`. `tests/COVERAGE.md` stays stage 5.

### 2026-10-18 — stage 2 (store list sort and since/until)

No new grill. No new `decision:*`. The store binds the caller's strings. It does not parse them. Definition of Done boxes stay unticked; this stage does not finish the plan.

`ListQuery` gained `StoreSort sort` (zero is updated), `since`, and `until`. `list_sort_column` is the only sort-to-column map. `list_append_time` appends `since` as `>=` and `until` as `<`, and the timestamp is a bind. `write_time_order` writes the ORDER BY for list and for search. List passes the sort column into both the WHERE and the ORDER BY. Search calls `list_sort_column(STORE_SORT_UPDATED)` and ignores `filters.sort`; rank stays `bm25(entries_fts), updated_at DESC, id DESC`. `COUNT` and the page stay inside the existing `BEGIN`/`COMMIT` in `store_list` and `store_search`. An unknown sort returns `STORE_ERR_INTERNAL` before any SQL.

Tests live in `tests/test_store.c` (the ASan store binary). `./build/remember_store_tests` on this tree: 142 tests, 0 failed, 1399 asserts.

Mutations, each restored from a copy of `src/store_sqlite.c` (not `git checkout`), proved by `./build/remember_store_tests`:

- `STORE_SORT_UPDATED` returning `"created_at"` fails `store_list_updated_sort_puts_the_edit_first` (`newest`, `middle`, `oldest` instead of the edited row first) and the updated half of `store_list_since_follows_sort_column` (count 0, expected 1).
- Dropping `, e.id DESC` from `write_time_order` fails `store_list_created_tie_breaks_by_id` (`low-id` then `high-id`).
- Changing the until predicate from `<` to `<=` fails `store_list_since_inclusive_until_exclusive` (count 2, first body `at_until`), `store_list_crossed_bounds_are_empty` (count 1 on equal bounds), and `store_search_since_filters_updated_at` (count 1, expected 0, on equal search bounds).
- Search using `q->filters.sort` instead of `STORE_SORT_UPDATED` fails `store_search_since_filters_updated_at` (`backdated alpha` instead of `edited alpha`).
- `default: return "updated_at"` fails `store_list_unknown_sort_is_internal` (status 0, expected 4).
- `list_append_time` binding NULL for both sides fails `store_list_since_inclusive_until_exclusive` (count 3, first body `at_until`), the created half of `store_list_since_follows_sort_column` (count 1, expected 0), `store_list_crossed_bounds_are_empty`, `store_search_since_filters_updated_at` (count 2, expected 1), and `store_list_bounds_compose_with_filters` (total 3, expected 2).

The first red runs, before this code existed: `store_list_sort_created_keeps_creation_order` came back `oldest` first (updated-at order), and `store_list_since_inclusive_until_exclusive` came back count 3 with first body `at_until`.

Probe, thrown away before this commit, one row at `2026-06-15T12:00:00.000Z`, ASan `./build/remember_store_tests`. The store does not reject non-canonical strings. Lexicographic compare, status 0 unless noted:

- `since` `""`, `" "`, or a leading space matches the row (count 1). The same three as `until` match nothing (count 0).
- A trailing space or `not-a-timestamp` as `since` matches nothing. As `until`, each matches the row.
- A 4095-nines `since` matches nothing. The same string as `until` matches the row.
- `(StoreSort)-1` is status 4, count 0, no rows.
- Search `until` equal to that row's `updated_at`, with `filters.sort` created, is count 0. One millisecond later it returns `alpha`.

No crash. Empty, spaced, and non-canonical bounds are a caller bug. Stage 3's parser rejects them before the store. This stage adds no path, so a missing file is not a new case. The year and DST scan stays on `parse_filter_ts` (stage 1); these strings are already canonical or they are not dates.

Simplify, not applied:

- Inline `list_append_time` into `list_append_filters` (about 8 lines). The pair is the since/until contract.
- Delete `write_time_order` and paste the ORDER BY twice (about 12 lines). The plan wants one place. The tie-break mutation is one string.
- Drop the search `column == NULL` check (about 2 lines). A broken switch should return `STORE_ERR_INTERNAL`, as list does.

### 2026-10-18 — stage 3 (CLI list/search flags)

No new grill. No new `decision:*`. The CLI parses with the existing `parse_filter_ts` and passes canonical strings. It does not compare `since` with `until`. Definition of Done boxes stay unticked; this stage does not finish the plan.

`list` takes `--sort`, `--since`, and `--until`. `search` takes the same three. A bad `--sort` is exit 1, code `usage`, `invalid --sort (use updated or created)`, on both commands. Search then ignores a valid `--sort` (the store still filters and ranks on `updated_at`). A repeated flag is `duplicate --sort`, `duplicate --since`, or `duplicate --until`. A missing value is `missing value for --sort`, `--since`, or `--until`. Anything `parse_filter_ts` rejects is `invalid timestamp: <raw token>`. `list_bind_bound` prints that and returns -1; `cmd_list` and `cmd_search` do not print it again. `--sort=created` stays `unknown option '--sort=created'`, because `take_value` reads the next argv. The brain opens in `run_with_store` before those flags are parsed, so a missing file is `cannot_open` even when the timestamp is also invalid.

Help for `list` and `search` names the flags. Search help repeats the three TS forms. SKILL.md, What's New, engineering notes, and `tests/COVERAGE.md` stay stage 5. `--if-updated-at` stays stage 4.

`./build/remember_tests ./build/remember --only list_dates` on this tree: 40 tests, 0 failed, 413 asserts. The first red runs were `unknown option '--sort'` and `unknown option '--since'`. The search-help assertion was red until the two TS lines were added; `search_crossed_bounds_are_empty` was already green.

Mutations, each one clause, restored from the original text (not `git checkout`), proved by `./build/remember_tests ./build/remember --only list_dates` before the two review fixes:

- Comparing `"created"` as `"CREATED"` fails `list_sort_created_keeps_creation_order`, `list_created_tie_breaks_by_id`, and `list_since_follows_sort_column`.
- `*bound = raw` instead of the canonical buffer fails both date-since tests and `list_seconds_form_matches_canonical`.
- Binding NULL for `since` fails the inclusive/exclusive test, the sort-column test, both date-since tests, the seconds form, the bad-timestamp test, and `search_since_filters_updated_at`.
- Binding NULL for `until` fails the inclusive/exclusive test, `list_equal_bounds_are_empty`, the bad-timestamp test, `list_bounds_total_counts_the_filtered_set`, and `search_equal_bounds_are_empty`.
- Accepting every `--sort` except `not-a-sort` fails `list_sort_rejects_a_bad_value` and `search_sort_bogus_is_usage`.
- Treating an empty slot as a duplicate fails `list_bound_flags_reject_duplicates_and_missing_values`.
- Rejecting `since >= until` as usage fails `list_equal_bounds_are_empty`, `list_crossed_bounds_are_empty`, and `search_equal_bounds_are_empty`.

Probes, thrown away, `./build/remember`:

- `--no-create` on a missing brain with `--since 2026-01-01`: exit 1, stdout empty, `cannot open the brain file (SQLITE_CANTOPEN)`.
- `--no-create --db /no/such/remember-plan17.db list --until 2026-02-30`: the same `cannot_open`. The bad day is not reported.
- `list --sort=created`: `unknown option '--sort=created'`. `list --sincee 2026-01-01`: `unknown option '--sincee'`.
- `list --since --until`: `invalid timestamp: --until`. `list --since -1`: `invalid timestamp: -1`.
- `TZ=Asia/Jerusalem list --since 2026-10-25` includes the row at `2026-10-24T21:00:00.000Z` and excludes `2026-10-24T20:59:59.999Z`.

Simplify:

- Applied: `list_take_sort` calls `list_take_raw` (about 12 lines).
- Not applied: inline `list_bind_bound` (about 11 lines, twice). The helper is the parse-or-null contract.
- Not applied: drop `q->sort = STORE_SORT_UPDATED` (1 line). `list_prepare_query` owns the field.
- Not applied: one shared help block. The list and search sentences differ. The TS legend is copied so `help search` stands alone.

Gate on this tree, after the search-help lines and `search_crossed_bounds_are_empty`: ctest 5/5 (ASan/UBSan, `REMEMBER_TEST_VFS=ON`), LINT OK, coverage 100% functions and 100% effective lines, `leaks-macos` PASS, `ci-linux.sh` OK (ctest 5/5, LINT OK). These notes were appended after that gate. No C file changed after it.

### 2026-10-18 — stage 4 (stale guard)

One grill. **Itay 2026-10-18 (`decision:plan17-stale-after-bin`):** when the row is in the wrong bin and `--if-updated-at` does not match, the bin error wins (exit 3). The stamp is compared only after `load_then_check_bin` accepts the row. A matching stamp on the wrong bin still reports expired, deleted, not_expired, or not_deleted. Definition of Done boxes stay unticked; this stage does not finish the plan.

`--if-updated-at` accepts only `YYYY-MM-DDTHH:MM:SS.mmmZ`. `update_take_if_updated` calls `parse_filter_ts` and then `strcmp`s the raw token with the canonical buffer. A date or `…T10:00:00Z` rewrites, so it fails that byte match: exit 1, code `usage`, `invalid timestamp: <raw token>`. There is no second calendar parser. `store_update` takes `const char *if_updated_at` immediately before `Entry *out_entry`. NULL skips the guard. Any other string is `strcmp`ed with the row's `updated_at` inside the `BEGIN IMMEDIATE` that also applies the write. A mismatch frees the loaded row, rolls back, and returns `STORE_ERR_STALE`. The message is `memory changed since it was read`. `code_for_store_status` maps that to `DIAG_STALE` (`stale`). `store_status_to_exit` already returns exit 1 for it. The flag alone is not a mutation. `--if-updated-at=value` stays `unknown option`, same as `--since=`.

Help, SKILL.md, What's New, engineering notes, and `tests/COVERAGE.md` stay stage 5. The plain diag writer still echoes a raw newline or ESC. `--json-errors` escapes both. That writer is older than plan 17 and is not part of this stage.

`store_update`'s `now` and `if_updated_at` are adjacent `const char *`. The definition is wrapped in `NOLINT(bugprone-easily-swappable-parameters)`, the same suppression the file already uses for that check. The match store test passes a local named `stamp` (a copy of `k_now`) so clang-tidy does not read the call as swapped. Linux cppcheck wanted that mismatch test's `stamp` in the block that uses it. `conflict` stays at function scope because the retry update uses it too.

Store tests own the write and the refusal. CLI tests own the flag, the usage errors, and that the stamp reaches the store (`decision:plan17-cli-tests-prove-wiring`). The id, key, and sync-id success tests assert exit 0 only. Dropping the stamp leaves them green. `update_if_updated_at_mismatch_is_stale` is the one that goes exit 0 if the CLI passes NULL. The facade test byte-matches the CLI on one database and does not repeat the JSON sentence.

First red runs, before the feature existed: the store mismatch returned status 0, body `new`, version vector 1 → 2. The CLI date form returned `unknown option '--if-updated-at'`.

Mutations, each restored from a copy (not `git checkout`):

| Mutant | Caught by |
|---|---|
| Compare `!=` flipped to `==` | both store tests |
| Compare deleted | mismatch store test |
| `if (if_updated_at != NULL)` with no `strcmp` | match store test |
| `strncmp(..., 22)` | mismatch store test (1 ms) |
| Rollback removed | mismatch store test's retry (status 2, `STORE_ERR_SQLITE`) |
| Message changed to `stale` | mismatch store message assert |
| CLI passes NULL | `update_if_updated_at_mismatch_is_stale` (exit 0). Key, sync-id, and undelete success tests stay green |
| `strcmp` against the canonical buffer removed | date-only and seconds form become the stale sentence. `now` still fails parse |
| Duplicate check removed | duplicate test gets the stale sentence |
| `DIAG_STALE` changed to `DIAG_INTERNAL` | JSON stale test, and `store_text_codes_follow_the_locked_split` (actual 4, expected 17) |
| Bin failure with a stamp returns `STORE_ERR_STALE` | `store_update_wrong_bin_wins_over_stale` (actual 17, expected 7) |

Probes, thrown away, `./build/remember`, one added row:

- `""`, a leading space, `-1`, and `2026-02-30T00:00:00.000Z`: exit 1, `invalid timestamp: <token>`, empty stdout.
- 300 nines: one plain line, 330 bytes on stderr. `--json-errors`: one line, 371 bytes.
- A newline in the token prints two plain `remember:` lines. An ESC is unchanged. `--json-errors` escapes `\n` and `\u001b`.
- `--no-create` on a missing brain: `cannot open the brain file (SQLITE_CANTOPEN)`. The brain opens before the flag is parsed.
- `--if-updated-at=2020-01-01T00:00:00.000Z` and `list --if-updated-at`: `unknown option`.

Simplify, not applied:

- Do not drop the key, sync-id, or undelete tests. The must-pass row names those locators. They catch the flag being parsed as the locator. They do not catch a dropped stamp.
- Do not inline `update_take_if_updated`. It is the D10 check.
- Do not add a second timestamp parser.

Gate on this tree: ctest 5/5 (ASan/UBSan, `REMEMBER_TEST_VFS=ON`), LINT OK, coverage 100% functions and 100% effective lines, `ci-linux.sh` OK (ctest 5/5, LINT OK, including cppcheck), `leaks-macos` PASS, then `GATE-ALL-GREEN`. These notes were appended after that gate. No C file changed after it.

## Review Notes

### 2026-10-18 — in-session deep review (stage 1)

**Verdict:** approve for the second-opinion pass. No open blocking item. No grill. No new `decision:*` beyond `decision:plan17-whole-plan-flag`, which is the finish flag for the whole plan, not a parser rule.

**Ran:** ctest 5/5 with ASan/UBSan and `REMEMBER_TEST_VFS=ON`. `lint-all.sh` LINT OK. Coverage 100% functions and 100% effective lines. `leaks-macos` PASS. `ci-linux.sh` OK, including IWYU and scan-build. [Bugbot](22ac6b1a-dcd7-4f01-a87d-97f96ca874c5) on the uncommitted stage found no blocking bug.

**Pragmatic write-gate:** DRY PASS (one civil check for all three forms; the expires parser stays on its own rules). Easy-to-replace PASS (a pure function, no SQLite). Orthogonal PASS (the parser does not print and does not see SQL). No deferred markers.

**Ponytail:** Lean already. The month-length table is the calendar.

**Auto-fixed:** `mktime` returning `(time_t)-1` is a failure only when `errno` is set. The temporary `days` local is gone. `list_dates` was re-run under ASan, and clang-tidy on `commands_common.c` was clean after that fix. The full gate above is the tree before that two-line change; the success path of every date test still runs the new check.

**Left for later:** stage 2 is `ListQuery` sort plus `since`/`until`. Help, SKILL, What's New, and `COVERAGE.md` stay stage 5.

**Praise:** the February 30 test failed first by showing the normalized March instant, which is the bug the plan warned about.

### 2026-10-18 — second-opinion deep review (stage 1)

**Verdict:** stage 1 approved after two fixes. No open blocking item. One grill, answered and implemented.

**Run, not read.** Baseline `d26613d`: the local gate is green (ctest, LINT OK, coverage 100/100, `leaks-macos`), and `ci-linux.sh` is OK. Fixed tree: ctest 5/5 (ASan/UBSan, `REMEMBER_TEST_VFS=ON`), LINT OK, coverage 100% functions and 100% effective lines, `leaks-macos` PASS, `ci-linux.sh` OK including IWYU and scan-build, then `GATE-ALL-GREEN`. Stage 1 has no CLI call site, so I probed `parse_filter_ts`'s date-only branch two ways: with a standalone copy of its `mktime` logic on macOS and on Linux glibc, scanning every day of 2026 in ten zones, and with a throwaway ASan test that called the real function for the year extremes.

**Findings:**

1. **blocking → fixed. A real day whose midnight falls in a DST gap was `invalid timestamp`.** On both libcs, `mktime` moves a missing 00:00 forward to 01:00 on the same day. The stage's post-check also required `tm_hour == 0`, so it refused that. Rejected days in 2026: Havana 03-08, Santiago 09-06, Beirut 03-29, Cairo 04-24. E5.2b says the local start of the day, and E5.2d rejects only other forms. The post-check now compares only year, month, and day. A second probe showed that the instant `mktime` picks is then the day's first instant on every day in seven zones on both libcs. The date check is still needed: Samoa skipped 2011-12-30, and `mktime` lands it on 12-29. New tests `filter_ts_skipped_midnight_is_the_first_local_instant` (literal `2026-03-08T05:00:00.000Z`) and `filter_ts_rejects_a_day_the_zone_skipped`. Mutation-checked: putting the hour check back fails the first test, and dropping the date check fails the second.
2. **important → fixed (grill). On macOS, a date-only year 0001–1899 returned success with `1969-12-31T23:59:59.000Z`.** macOS `mktime` returns `-1` for every year before 1900 and sets no `errno`, so the stage's "errno distinguishes failure" check passed it. glibc converts all those years. **Itay 2026-10-18 (`decision:plan17-date-floor-1900`, amends E5.2b):** a date-only year before 1900 is `invalid timestamp` on every platform. The canonical and whole-seconds forms keep 0001–9999. Failure detection is now the `tm_wday = -1` sentinel, which a successful `mktime` always overwrites, instead of `errno`. Tests `filter_ts_rejects_a_date_before_1900` (red first, showing the 1969 value) and `filter_ts_date_1900_01_01_is_accepted`. On macOS the floor and the sentinel each cover the other, so neither mutant alone fails there. Dropping the floor fails on Linux, where glibc would convert 1899. The sentinel stays as a defensive line. Stage 5 adds the 006 Implementation Results row.
3. **nit → fixed (simplify, about 11 lines in `src/`).** The whole-seconds branch repeated the six-field parse and the civil check of the canonical branch. It now rewrites `…:SSZ` to `…:SS.000Z` and falls into the canonical branch. The three redundant zero assignments after `memset` are gone. Mutation-checked: a wrong `.001` rewrite fails `filter_ts_seconds_become_millis`.
4. **nit → fixed.** `filter_ts_date_is_jerusalem_local_midnight` repeated `use_tz`/`restore_tz` inline. The helpers moved above it, and it now reads like the Los Angeles test.

**Open nits (not fixed):**

- A date-only `9999-12-31` is accepted in Tokyo, Honolulu, and UTC. Probed under ASan: no crash, and each output is the right instant (Tokyo `9999-12-30T15:00:00.000Z`). Nothing to fix; it is recorded so stage 2 does not re-probe it.

**Environment:** the local `ubuntu:24.04` image was x86_64 under emulation, and the baseline `ci-linux.sh` took 87 minutes. With Itay's OK I pulled the native `linux/arm64` image. The gate script now configures twice, because a compiler-path change wipes the CMake cache and drops `-DREMEMBER_TEST_VFS=ON`. It also checks the cache for both options before it builds.

**Praise:** the parser does not print and leaves `out` unchanged on rejection, so stage 3 can phrase the usage error in one place. The February 30 test is the hot spot the plan warned about.

**Addendum (Itay 2026-10-18, `decision:plan17-literal-tz-expectations`):** the three time-zone tests now assert literal UTC instants instead of calling the `expected_local_midnight` oracle. That oracle ran the same `mktime` call as `parse_filter_ts`, so the two could share a wrong assumption. The literals are Jerusalem `2026-06-15` → `2026-06-14T21:00:00.000Z`, Los Angeles `2026-06-15` → `2026-06-15T07:00:00.000Z`, and Jerusalem `2026-10-25` → `2026-10-24T21:00:00.000Z`. This deviates from the must-pass row "expected value computed in the test". The cost: a literal fails if a zone's 2026 rules change in tzdata. `take_digits`, the oracle, its constants, and two includes are gone (77 lines). Mutation-checked: `tm_isdst = 0` instead of `-1` fails all three tests. Gate: ctest 5/5 (ASan/UBSan, `REMEMBER_TEST_VFS=ON`), LINT OK, coverage 100/100, `leaks-macos` PASS, `ci-linux.sh` OK, then `GATE-ALL-GREEN`, run in a clean worktree of `02b6850` because the coder had stage 2 work uncommitted in the main checkout.

### 2026-10-18 — in-session deep review (stage 2)

**Verdict:** approve for the second-opinion pass. No open blocking item. No grill. No new `decision:*`.

**Ran:** ctest 5/5 with ASan/UBSan and `REMEMBER_TEST_VFS=ON`. `lint-all.sh` LINT OK. Coverage 100% functions and 100% effective lines (`must-cover: 0`). `leaks-macos` PASS. `ci-linux.sh` OK, including LINT OK. The base of that run is `b9eef14` plus this stage. These Review Notes were appended after that gate. No C file changed after it. [Bugbot](e606274b-5b80-42e7-9f21-9f6ede28c74d) reported a 32-byte `memcpy` in `parse_filter_ts`. The tree copies `ISO_BUFSIZE` (25), which is the size of the stack `canon` buffer (`src/commands_common.c`). That finding does not match the code. The store check matched E5.1–E5.3 and D11.

**Pragmatic write-gate:** DRY PASS (one column map, one bound append, one ORDER BY). Easy-to-replace PASS (SQL stays in `store_sqlite.c`). Orthogonal PASS (list uses the sort column for order and bounds; search ignores sort and filters `updated_at`). No deferred markers.

**Simplify:** three candidates, none applied. Inlining `list_append_time` saves about 8 lines and hides the inclusive/exclusive pair. Deleting `write_time_order` saves about 12 lines and duplicates the tie-break. Dropping the search NULL check saves about 2 lines and would format a null column.

**Auto-fixed:** `store_search_since_filters_updated_at` now also asserts that equal search bounds are empty and that one millisecond later includes the edited row. Changing `<` to `<=` fails that assertion (count 1, expected 0). The gate above includes that assertion.

**Left for later:** stage 3 is the CLI list/search flags, D11 validation, and help text. Help, SKILL, What's New, engineering notes, and `COVERAGE.md` stay stage 5.

**Praise:** the creation-order test failed first by returning updated-at order.

### 2026-10-18 — second-opinion deep review (stage 2)

**Verdict:** stage 2 approved. No open blocking item. One grill, answered. Two tests added, and the code and tests are shorter.

**Run, not read.** Baseline `0e8c345` (gate root clean): ctest 5/5, LINT OK, coverage 100/100, `leaks-macos` PASS, `ci-linux.sh` OK, `GATE-ALL-GREEN`. Fixed tree: ctest 5/5 (ASan/UBSan, `REMEMBER_TEST_VFS=ON`), LINT OK, coverage 100/100, `leaks-macos` PASS, `ci-linux.sh` OK, `GATE-ALL-GREEN`, run in a clean worktree of `0e8c345` plus these changes. The CLI caps `--tag` at 50, so the worst list is 50 + source + key + `now` + since + until = 55 binds, inside `LIST_BIND_CAP` 64. `COUNT` and the page share the existing `BEGIN`/`COMMIT`.

**Mutations on the coder's tests** (`remember_store_tests`, each from a copy):

| Mutant | Result |
|--------|--------|
| since `>=` → `>` | caught: `store_list_since_inclusive_until_exclusive`, by a UBSan abort (exit 134, see nit 5) |
| until `<` → `<=` | caught by three tests |
| list tie-break `e.id DESC` → `ASC` | caught: `store_list_created_tie_breaks_by_id` |
| list ignores `q->sort` | caught by four tests |
| COUNT without the time bounds | caught |
| **search ORDER BY uses `q->filters.sort`, filter unchanged** | **survived** → finding 1 |
| **search tie-break `e.id DESC` → `ASC`** | **survived** (older than this stage) → finding 2 |

**Findings:**

1. **important → fixed. No test proved that search keeps its order when the query asks for `created` (E5.3).** The coder's mutation swapped the column for both the filter and the order, so the filter test caught it. An order-only bug passed. New `store_search_ignores_sort_for_order`: two identical bodies whose created and updated orders disagree, `filters.sort = CREATED`, ids 1 then 2. It fails the order-only mutant.
2. **important → fixed. No test pinned search's `id` tie-break.** New `store_search_ties_break_by_id`: two identical bodies with the same timestamps come back as ids 2 then 1. It fails the `ASC` mutant. With the ORDER BY inlined (grill 3), each tie-break now has its own test.
3. **Grill → fixed (Itay 2026-10-18, `decision:plan17-inline-order-by`, deviates from stage 2's "one place builds the ORDER BY").** `write_time_order`, `TIME_ORDER_CAP`, and two 64-byte `order` buffers are gone. List writes `ORDER BY e.%s DESC, e.id DESC` and search writes `ORDER BY bm25(entries_fts), e.%s DESC, e.id DESC` straight into their existing SELECT formats. The column still comes only from `list_sort_column`.
4. **nit → fixed (simplify).** The one-caller wrapper `list_append_time` is inlined, so `list_append_filters` calls `list_append_cmp` twice, once with `true, q->since` and once with `false, q->until`. I first passed the operator string instead of `bool inclusive`. clang-tidy rejected that as three adjacent swappable `const char *` parameters, and the ternary form as an implicit bool conversion, so the coder's two-branch `list_append_cmp` stays as written. With finding 3, `src/store_sqlite.c` is net −27. The coder's argument for keeping search's `column == NULL` check holds (a broken switch would format NULL), so it stays.
5. **nit → fixed (simplify, DRY).** 21 eleven-argument `store_add` calls, each with `Entry` / `StoreAddAction` / `memset` / `store_entry_free` around it, are now `given_entry(s, (GivenEntry){.body, .hash, .created, …})` (a `given*` helper, per `quality:test-the-contract`). Two tests inlined the loop that `free_page` already does, and one inlined the `UPDATE` that `set_updated_at` does. Net −71 test lines before the two new tests.
6. **nit, open (harness).** After a failed `count` assert, a test still reads `page.entries[0]`. On an empty page that is a NULL read, so UBSan aborts the whole binary and hides every later result. The gate still goes red. The pattern is house-wide (12 sites in `test_store.c`). Follow-up: a `REQUIRE`-style assert in `tests/test.h` that returns from the test.

**Praise:** `list_sort_column` is the one map, and zero-init keeps today's order. The unknown-sort test returns `STORE_ERR_INTERNAL` before any SQL. The coder probed non-canonical bounds and recorded that the store binds them as given.

**Coder rating (stage 2): 8/10, up from 7.** It applied last round's instructions:

- Seven mutations with the observed failures.
- A thrown-away probe of non-canonical bounds.
- Three simplify candidates, each with a line count.
- The gate on the committed tree.
- A pull of the review commit before committing.

**Misses:**

- Its mutations changed whole behaviors (search used the sort for both filter and order), so two narrower bugs survived. Mutate one clause at a time.
- It turned down a simplification by citing plan wording instead of asking. The plan said "one place", and Itay chose the shorter code. A simplification blocked only by plan wording is a grill, not a no. Its other reasons held: search's NULL check stays, and lint forces the `bool` in `list_append_cmp`.
- It wrote 21 copies of an 11-argument call instead of a `given*` helper.

**Paste-ready for the coder:**

```text
1. Mutate one clause at a time (the ORDER BY alone, the WHERE alone, one operator, one tie-break). A mutant that changes two things is caught by the easier one.
2. When a simplification is blocked only by plan wording, ask Itay; do not decline it yourself.
3. When a test needs the same setup call more than twice, write a given* helper first.
4. After a failed count assert, do not index the page.
```

### 2026-10-18 — in-session deep review (stage 3)

**Verdict:** approve for the second-opinion pass. No open blocking item. No grill. No new `decision:*`.

**Ran:** the gate in the stage 3 Implementation Notes. [Bugbot](60839d72-93d6-4223-8528-587613f99ad1) found no blocking bugs. Two nits, both auto-fixed before that gate: `help search` now states the three TS forms (the assertion was red first), and `search_crossed_bounds_are_empty` locks D11 for search. Missing `--since` now also asserts empty stdout, as missing `--until` already did.

**Pragmatic write-gate:** DRY PASS. One parser, one flag reader, one bind. The TS legend is on both help pages on purpose, and the help test locks both copies. Easy-to-replace PASS. SQL stays in `store_sqlite.c`; the CLI hands it canonical strings. Orthogonal PASS. List filters the sort field. Search validates `--sort`, then ignores it and filters `updated_at`. No deferred markers.

**Simplify:** one applied shrink (`list_take_sort` through `list_take_raw`, about 12 lines). Three candidates declined in the Implementation Notes. Ponytail: nothing further to cut.

**Lenses:** bad `--sort` and a bad timestamp return before the store. `since >= until` stays an empty page. The bound buffers are caller stack storage and live across `store_list` / `store_search`. `sort_raw` points at argv and is not freed. Human lines, the empty JSON page, and the time-zone instants are literals. Open still happens before flag parse.

**Left for later:** stage 4 is the stale guard (`--if-updated-at`, D10, `STORE_ERR_STALE` → `stale`). Help, SKILL, What's New, engineering notes, and `COVERAGE.md` stay stage 5.

**Praise:** the first red runs were `unknown option '--sort'` and `unknown option '--since'`.

### 2026-10-18 — second-opinion deep review (stage 3)

**Verdict:** stage 3 approved. No open blocking item. One grill, answered. No production code change.

**Run, not read.** Baseline `343d04d` (gate root clean): ctest 5/5, LINT OK, coverage 100/100, `leaks-macos` PASS, `ci-linux.sh` OK, `GATE-ALL-GREEN`. Fixed tree: ctest 5/5 (ASan/UBSan, `REMEMBER_TEST_VFS=ON`), LINT OK, coverage 100/100, `leaks-macos` PASS, `ci-linux.sh` OK, `GATE-ALL-GREEN`, in a clean worktree of `343d04d` plus these changes.

**Handover probes on the built binary:** three rows at `10-11T10:30Z`, `10-12T05:00Z`, and `10-12T11:30Z`, then `list --since 2026-10-12`. Kiritimati (+14) gives all three. Pago Pago (−11) gives only the 11:30 row. Jerusalem and UTC give the two 10-12 rows. So each zone's local midnight is a different UTC boundary, as the plan expects.

- Jerusalem `2026-10-25`: correct.
- `2026-02-29` and `2026-02-30`: both `invalid timestamp`.
- Equal canonical bounds: exit 0, `"count":0,"total":0`.
- `search foo --sort created` and `search foo`: byte-identical.
- `--json-errors` on a newline in the timestamp: one escaped JSON line.

**Mutations on `src/cmd_query.c`**, one clause each:

| Mutant | Caught by |
|--------|-----------|
| W1: `--sort created` ignored | `list_sort_created_keeps_creation_order` |
| W2: since and until swapped | 7 tests |
| W3: search drops both bounds | 2 tests |
| W4: bad `--sort` accepted | 2 tests |
| W5: a duplicate is last-wins | 2 tests |
| W6: raw string passed to the store | 3 tests |

**Findings:**

1. **Grill → fixed (Itay 2026-10-18, `decision:plan17-cli-tests-prove-wiring`).** Six CLI tests re-proved store semantics that stage 2's store tests already pin. They are removed, 195 lines:
   - `list_created_tie_breaks_by_id`
   - `list_since_follows_sort_column`
   - `list_crossed_bounds_are_empty` (`list_equal_bounds_are_empty` stays for D11)
   - `list_bounds_total_counts_the_filtered_set`
   - `list_bounds_compose_with_bins`
   - `search_equal_bounds_are_empty` (`search_crossed_bounds_are_empty` stays)

   W1–W6 were re-run on the pruned suite, and each is still caught. The CLI suite now proves wiring, parse errors, D11 output, the time-zone conversion, one composition, help, and facade. The store suite proves order, tie-breaks, bound column, `total`, and bins.
2. **nit, considered and not applied (simplify).** I tried mapping `--sort` to `StoreSort` once, when the flag is read, so `"created"` is compared in one place instead of two. A `StoreSort` field in `ListParse` needs an explicit pad under `-Wpadded`, and the change was +3 lines. The coder's second `strcmp` stays.
3. **important, open, cross-cutting, older than plan 17. Plain error lines echo user input raw.** `list --since $'2026\nremember: fake line'` prints two `remember:` lines. An ESC sequence reaches the terminal unchanged. The same happens for `unknown option '…'` and `unknown command '…'`. `--json-errors` escapes correctly. When an agent passes text from untrusted content, the plain line can forge output. Suggested fix: the diag emitter's plain writer neutralizes C0/DEL as `?`, as `output_body_human` already does, in one place. That changes plain bytes only for inputs with control characters (E4.2). It needs a plan or a grill of its own.
4. **nit, open, older than plan 17.** `take_value` takes a following option as the value. `list --since --limit 5` reads `--limit` as the timestamp and reports `unexpected argument` for `5`. `--key` and `--source` do the same. The coder probed `--since --until` (`invalid timestamp: --until`), which is the better case.

**Praise:** each flag fails before the store with its own sentence, and the bound buffers are caller stack storage that lives across the store call. The coder recorded that the brain opens before flag parsing, so `cannot_open` wins over a bad timestamp.

**Coder rating (stage 3): 8.5/10, up from 8.** It applied every instruction from the stage 2 list:

- One-clause mutants (seven, each with the failing tests named).
- A `--no-create` probe and a DST-day probe.
- Literal instants.
- Guards after count asserts.
- Four simplify candidates, each with a reason.
- The gate on the committed tree.

**Misses:**

- Its simplify pass looked only at production code, and it wrote "nothing further to cut" over 195 lines of tests that repeated another layer.
- Its hostile-input probes stopped at dashes and empty strings. A newline or an ESC in an echoed value would have shown finding 3.

**Paste-ready for the coder:**

```text
1. Before writing a CLI test, check whether a store test already proves that behavior. If it does, the CLI test proves only that the flag reaches the store.
2. The simplify pass covers tests too. Count test lines per behavior and look for the same contract proven in two layers.
3. Probe every value that an error message echoes with a newline, an ESC, and a 300-byte string, in plain and in --json-errors.
```

### 2026-10-18 — in-session deep review (stage 4)

**Verdict:** approve for the second-opinion pass. No open blocking item. One grill, answered. `decision:plan17-stale-after-bin`: the bin error wins over a mismatched stamp.

**Ran:** the gate in the stage 4 Implementation Notes. [Bugbot](123f4ecb-720d-4b43-8a7a-bed25105ceb0) on the uncommitted stage found no bugs. These Review Notes were appended after that gate. No C file changed after it.

**Pragmatic write-gate:** DRY PASS. One parser, then a byte compare. Easy-to-replace PASS. SQL stays in `store_sqlite.c`. Orthogonal PASS. The CLI rejects a non-canonical token. The store compares. The diag code is a separate map. No deferred markers.

**Ponytail:** Lean already. The adjacent `const char *` parameters keep the signature the plan named, with the file's existing NOLINT. An options struct would be a new shape for one argument.

**Auto-fixed:** `store_update_wrong_bin_wins_over_stale` locks the grill. Returning `STORE_ERR_STALE` on a bin failure fails it (actual 17, expected 7). clang-tidy wanted a NOLINT on `store_update` and a local named `stamp` on the match call. Linux cppcheck wanted that mismatch `stamp` inside the block that uses it.

**Simplify:** three candidates, none applied. Reasons are in the Implementation Notes.

**Lenses:** the compare runs after the bin check, inside the write transaction. A failed load frees the row inside `load_then_check_bin`, so the compare only sees a row that passed. `updated_at` is non-NULL on that path. The flag alone is not a change. Open still happens before flag parse. The plain writer still echoes a newline and an ESC. That stays the older cross-cutting issue.

**Left for later:** stage 5 is docs and the full gate: help, SKILL.md, What's New, engineering notes, `tests/COVERAGE.md`. Stage 5 is the last stage. After its second review is closed, write `.claude/WHOLE_PLAN_REVIEW` and stop.

**Praise:** the first red store run wrote the body and bumped the version vector. The first red CLI run did not know the flag.

### 2026-10-18 — second-opinion deep review (stage 4)

**Verdict:** stage 4 is **not approved as is. It is reworked as stage 4b** (see the amendment under Stages). The code does what E8.1 and D10 say, but the token they chose cannot carry the guarantee. Six grill answers set the rework. No production code changed in this review.

**Run, not read.** Baseline `06cc93b` (gate root clean): ctest 5/5, LINT OK, coverage 100/100, `leaks-macos` PASS, `ci-linux.sh` OK, `GATE-ALL-GREEN`. Handover probes on the binary all held:

- **Two writers:** A reads the stamp, B edits, A's guarded update is `stale`, and B's edit survives.
- **`--json-errors`:** one `stale` line.
- **D10:** the date-only and whole-seconds forms are `invalid timestamp`.
- **Flags:** a duplicate is caught, and the stamp alone is not a change.

The coder's eleven mutants were not re-run, because 4b replaces the code they cover.

**Findings:**

1. **blocking → rework (grills 1–3, `decision:plan17-stale-guard-version-vector`).** A write stores `updated_at = now` at millisecond resolution, so two writes in the same millisecond carry the same stamp. My in-process probe through `remember_run` (the Mac app's path) ran W1, read its stamp, ran W2, then a guarded update. It accepted **95 of 500** stale updates and overwrote W2. A clock stepped backwards can repeat a stamp too.

   Itay asked whether a revision counter would be better. The version vector already is one: per device, never repeating, already in entry JSON, and the token sync uses. My first answer said link writes do not bump it. That was wrong, and I said so and re-asked: run, link and unlink do bump it.

   The answers:
   - The flag is `--if-version-vector VV`, and `--if-updated-at` is removed.
   - VV is compared as parsed maps, a missing device counting as 0. Malformed is `usage` `invalid version vector: <vv>`.
   - The store parses it with the existing `vv_parse_pairs` (a recorded exception to "the store takes canonical strings").
   - D10 is superseded in `006-locked-decisions.md`.
2. **blocking → rework (grills 4–5, `decision:plan17-upsert-vv-rule`), older than plan 17.** `add --key K BODY` on an existing key replaced the body and left `version_vector` at 1. A keyless re-add of an existing body touched `updated_at` and never the VV either. Design log 005 says every write bumps the VV. The rule now: a write onto an existing row that changes it bumps the VV and `updated_at` together, and one that changes nothing moves neither.
3. **important → rework (grill 6, `decision:plan17-store-update-options-struct`).** `store_update` takes 17 positional arguments behind a NOLINT for swappable parameters, at 24 call sites. It becomes `store_update(s, id, &(StoreUpdate){…}, now, &entry, &conflict)`, as the first 4b commit, with no behavior change.
4. **nit, environment.** `build/` holds iCloud conflict copies (`libremember_core 2.a` … `21.a`, `libsqlite3 2.a` …). They are harmless to the build but waste disk. The repo is inside iCloud Drive, which is also why the CLI warns about synced folders.

**Praise:**

- The bin-first ordering (`decision:plan17-stale-after-bin`) was the coder's own grill.
- The compare already sits inside the write transaction, after the bin check, with a rollback. 4b keeps that shape and changes only the token.
- The Implementation Notes say which success tests a dropped stamp leaves green.

**Coder rating (stage 4): 8/10.** Execution was the best so far:

- Eleven one-clause mutants, in a table.
- Hostile-input probes with a newline, an ESC, and 300 bytes (last round's advice), in plain and JSON.
- One well-framed grill.
- Honest notes.

**Miss:** it never asked whether `updated_at` actually changes on every write. The brain's `lesson:review-run-edge-cases` is exactly that bug, with second-resolution timestamps. The plan locked the token, so part of this is the plan's fault. A guard is only as good as its token's uniqueness, and that is the probe to run first. The coder also declined the options struct as "a new shape for one argument" without asking. Itay chose it.

**Paste-ready for the coder:**

```text
1. When a check relies on a value changing (a stamp, a counter, a hash), first prove it changes on every write: two writes in the same millisecond, in-process through remember_run, and every write path (add upsert, keyless re-add, update, link, rekey, delete, undelete).
2. Before relying on a field, run the design log's rule for it against the code (for example, 005 says every write bumps the version vector). Report the gaps you find, even if they predate the plan.
3. A declined simplification that changes an API is a grill, not a no.
```

## Implementation Notes — stage 4b

Three commits. Each one had a full gate before it was committed. Definition of Done boxes stay unticked. This stage does not finish the plan.

1. `664d643` — `store_update` takes a `StoreUpdate`. No behavior change.
2. `fafed75` — an add onto an existing row bumps `version_vector[local]` and `updated_at` together when the row changes, and moves neither when it does not.
3. This commit — `--if-updated-at` is gone. `--if-version-vector` passes the raw string. The adapter checks the shape, parses with `vv_parse_pairs`, and compares maps inside the write transaction, after the bin check. A missing device counts as 0. `vv_compare` is not the check: it returns 0 for equal and for concurrent. Malformed is `STORE_ERR_BAD_VV` → `usage` `invalid version vector: <vv>`. Not equal is `STORE_ERR_STALE`.

**Gate on this commit's tree, before these notes.** ctest 5/5 (ASan/UBSan, `REMEMBER_TEST_VFS=ON`). `lint-all.sh` LINT OK. Coverage 100% functions and 100% effective lines (`must-cover: 0`). `leaks-macos` PASS. `ci-linux.sh` OK, including LINT OK, IWYU, and scan-build. No C file changed after that gate. [Bugbot](ac3884da-72ed-49e7-bacc-fd7e88082ba1) on the uncommitted guard found no logic bug.

**Mutants, one clause each.** Run after the gate and restored. The C tree matches the gate.

| Clause | Test | Result |
|---|---|---|
| Reject the token before the bin check | `store_update_wrong_bin_wins_over_stale` | actual 18 (`BAD_VV`), expected 7 (`EXPIRED`) |
| An explicit 0 counter is a difference | `store_update_version_vector_missing_device_is_zero` | actual 17, expected 0. No other store test failed |
| The CLI passes NULL | `update_if_version_vector_mismatch_is_stale` and `update_if_version_vector_rejects_malformed` | exit 0. The match tests stayed green |

TDD reds, before the clause existed: an equal vector compared with `updated_at` was status 17; whitespace against a byte compare of the vector was status 17; an empty string before the shape check was status 4 (`INTERNAL`).

**Probes, run.** `./build/remember --db /tmp/remember-vv-probe.db`.

- Newline in the token. Plain stderr is two lines (`remember: invalid version vector: not` / `a-vector`). `--json-errors` is one line and the message contains `\n`.
- ESC (`\x1b`). Plain is one line. JSON is one line and the message contains `\u001b`.
- 300 `x`. Plain is 335 bytes, JSON is 376 bytes, each one line, exit 1, code `usage`.
- In-process: add, read the vector (`:1`), update, read it again (`:2`), then `update --if-version-vector` with the first vector. Exit 1, code `stale`, body stayed `second`. `store_update_same_timestamp_is_still_stale` is the same pair of writes with one forced timestamp.

**Simplify.** Three candidates.

1. Fold the two clock loops into one helper. About 6 lines. Not applied. The two sides are the equality rule, and the free on a difference is what a reader checks.
2. A `given*` helper for the store guard tests. About 80 lines. Not applied. Each test's setup is the contract, and the rest of `tests/test_store.c` is written the same way.
3. One SQL statement for `json_valid`, `json_type`, and the member check. Cannot. `SELECT json_valid(''), json_type('')` errors with `malformed JSON` before the 0 from `json_valid` comes back. Probed with `sqlite3`. The three statements are the check.

### 2026-10-19 — second-opinion deep review (stage 4b)

**Verdict:** stage 4b approved. No open blocking item. No grill: every decision in the amendment is implemented as written. Two auto-fixed test gaps and two simplifications.

**Run, not read.** Baseline `b9eac7d` (gate root clean): ctest 5/5, LINT OK, coverage 100/100, `leaks-macos` PASS, `ci-linux.sh` OK, `GATE-ALL-GREEN`. Fixed tree: ctest 5/5 (ASan/UBSan, `REMEMBER_TEST_VFS=ON`), LINT OK, coverage 100/100, `leaks-macos` PASS, `ci-linux.sh` OK, `GATE-ALL-GREEN`, in a clean worktree of `b9eac7d` plus these changes. The first run failed clang-tidy `readability-identifier-length` on my `vv` locals, which are now `vector`.

- **The hole is closed.** The stage 4 in-process probe (W1, read the VV, W2, then a guarded update through `remember_run`) now uses `--if-version-vector`. It wrongly accepted **0 of 500**, against 95 of 500 with `updated_at`.
- **Add-path rule on the binary:**
  - A repeated keyed or keyless add with the same body and tags moves neither stamp.
  - A changed body or a new tag bumps `updated_at` and the VV by exactly 1.
  - A keyless re-add of an expired row revives it with one bump (2 → 3), not two.
- **Shape check in the `sqlite3` CLI:** 19 hostile inputs (`''`, `x`, `null`, `5`, `[]`, `{"a":-1}`, `{"":1}`, `{"a":1.5}`, `{"a":"1"}`, `{"a":true}`, nested, …) all return BAD, and valid objects with any spacing return OK.

**Mutations on the guard**, one clause each:

| Mutant | Before this review | After |
|--------|--------------------|-------|
| shape check skipped | caught (2 tests) | caught |
| negative counter allowed | caught | caught |
| non-integer allowed | caught | caught |
| empty device id allowed | **survived** | caught: `{"":1}` added to the malformed list |
| given-side compare loop never runs | **survived** | caught: new `store_update_extra_device_is_stale` |
| row-side compare loop never runs | **survived** | caught: new `store_update_empty_object_is_stale` |

**Findings:**

1. **important → fixed. Two test gaps let one side of the equality check go.** The rule is symmetric: every row counter must match the given one, and every given counter must match the row's. Every stale test differed in a way both loops catch, so either loop could be deleted with the suite green. The amendment listed "a device added" as a required store test, and it was missing. The two new tests each fail one mutant. The malformed list now includes `{"":1}`.
2. **nit → fixed (simplify, `src/store_sqlite.c` net −43).** `vv_reject_malformed` used three prepare/step/finalize blocks, because `SELECT json_valid(''), json_type('')` errors before `json_valid`'s 0 comes back. The coder probed that form and wrote "Cannot". A `CASE` evaluates only the branch it needs, so `CASE WHEN json_valid(?1) = 0 THEN 1 WHEN json_type(?1) != 'object' THEN 1 ELSE EXISTS (…json_each…) END` is one statement. The malformed tests and all three shape mutants pass through it.
3. **important → fixed (simplify, DRY).** Seven of the eight guard tests repeated about 25 lines of setup: open a store, check the device id, add "old" with an eleven-argument `store_add`, and guard early returns. The coder declined a helper because "the rest of `tests/test_store.c` is written the same way". It is not: `given_entry` (stage 2 review) and the coder's own `given_live_add` both exist. The tests now use `given_live_add`, plus two small helpers, `when_guarded_body_update` and `then_body_is`. `store_update_if_version_vector_mismatch_writes_nothing` keeps its full row-unchanged and rollback checks. `store_update_same_timestamp_is_still_stale` keeps its point: the second write's `updated_at` equals the first's, and its VV does not. One assertion was dropped: `equal_version_vector_writes` no longer checks that `updated_at` becomes `now` (update tests elsewhere prove that). `tests/test_store.c` is net −215 lines, including the two new tests.
4. **nit, open.** The shape check accepts a counter above int64 (`{"a":99999999999999999999}`) and a repeated device (`{"a":1,"a":2}`). Neither can equal a real row, so the outcome is `stale`, never a lost write. A future sync plan could make both malformed.

**Praise:**

- `vv_compare`'s equal-or-concurrent trap was caught in the coder's own notes.
- The compare stays after the bin check and inside the write transaction.
- The options-struct commit really is behavior-neutral, so the VV commit reads on its own.

**Coder rating (stage 4b): 7.5/10.** It implemented six locked decisions exactly, across three gated commits. It closed the hole, wrote a same-timestamp test, and probed newline and ESC input.

**Misses:**

- It ran three mutants this time against eleven last stage, and the two it skipped were the symmetric halves of the new compare.
- A plan-listed test ("a device added") never got written.
- It declined a helper with a claim about the file that a grep disproves.
- It stopped at "Cannot" after one SQL form.

**Paste-ready for the coder:**

```text
1. Mutate every clause of new logic: each branch, each side of a symmetric check, each WHERE term. Not a sample.
2. Before REVIEW_REQUEST, tick every test the plan amendment names against the actual test names.
3. Before declining a helper, grep the file for given* helpers that already exist, and reuse them.
4. When a probe says "cannot", try one more form (in SQLite, CASE short-circuits) before writing it down.
```

## Implementation Notes — stage 5

Last stage. Docs and the full gate. No new grill. Definition of Done stays open on the second review and on the INDEX check mark.

Help for `list` and `search` was already in stage 3 (`list_and_search_help_name_sort_and_bounds`). This stage adds `update` help for `--if-version-vector`. The first red run of `update_help_names_the_version_vector_guard` failed `ASSERT_STR_CONTAINS` because `help update` had no such lines. The other update tests stayed green.

**Mutants, one clause each.** Restored after. The C tree matches the gate.

| Clause | Test | Result |
|---|---|---|
| Description says `matches VV` | `update_help_names_the_version_vector_guard` | the equals line failed. The stale sentence stayed |
| Stale sentence says `writes anyway` | same test | the stale sentence failed. One assert |

**Probes, run** with `./build/remember` before the claims were written.

- Missing `--db` path `/tmp/remember-does-not-exist-stage5.db help update`: exit 0, help text, file not created. `update --help` prints the same topic.
- Keyed add of the same body and tags: `updated_at` and `version_vector` stay put. A changed body bumps the vector 1 → 2. Keyless re-add of the same body stays put. A new tag bumps 1 → 2.
- Re-add of an expired row (expiry set with `sqlite3`, then `add` of the same body): bin `expired` vector 1 becomes `live` vector 2, `expires_at` null. One bump.
- `list --sort created` vs `--sort updated` on two rows: created order is the newer id first; updated order is the edited row first.
- `--since <updated_at>` includes that row. `--until` the same stamp excludes it. Equal bounds: `count` 0, `total` 0.
- A `version_vector` object with a space after `:` is accepted by `--if-version-vector`. The old object after a later write is `stale`. Re-reading and passing the new object writes.
- `list --since 1899-12-31`: exit 1, `invalid timestamp: 1899-12-31`. `list --since ''`: `invalid timestamp: ` (empty). `search probe --sort created` matches `search probe`. `search probe --sort bogus`: `invalid --sort (use updated or created)`.
- `not-a-vector`: `usage` `invalid version vector: not-a-vector`.
- Expired row, live locator, mismatched vector: exit 3, code `expired`. Same row with `--expired` and a mismatched vector: exit 1, code `stale`.

**Gate on this commit's C tree.** ctest `step_gate`, `step05_gate`, `store_asan_gate`, and `no_shim_symbols` passed (ASan/UBSan, `REMEMBER_TEST_VFS=ON`). `install_skill` failed in the sandbox (`mkdir` Operation not permitted) and passed when re-run outside it. `lint-all.sh` LINT OK. Coverage 100% functions and 100% effective lines. `leaks-macos` failed in the sandbox (`Couldn't get task port`) and passed outside it. `ci-linux.sh` OK, including LINT OK, IWYU, and scan-build. No C file changed after that gate. [Bugbot](674275d6-8edb-448c-994e-834202ef9007) on the uncommitted stage found no blocking bug.

**Simplify.** Three candidates.

1. Two help sentences (`code stale` and `Nothing is written`) folded into one. Applied. One fprintf line.
2. Fold the new test into `help_update_topic`. About 20 lines. Not applied. That test only checks that update help prints. This one checks the guard text, and it lives in the `update` suite with the flag.
3. A `given*` helper for "open a db and run help". The new test is the only caller in this file. `help_update_topic` is in another file. A shared helper is more code than the two call sites. Not applied.

The TS parser does not serve `update`. The amendment moved the guard to the version vector. The Definition of Done box now says that.

`implementation-plans/006-locked-decisions.md` P7 still named `--if-updated-at`. It now names `--if-version-vector`, with the amendment on the same row. D10 in that file was already superseded.

## Review Notes

### 2026-10-19 — in-session deep review (stage 5)

**Verdict:** Approve with nits. No open blocking item. No grill.

**Lenses.** DRY: the stale sentence is quoted in help, the test, and SKILL.md. That is the locked message, not a second implementation. Easy-to-replace: no new module or dependency. Orthogonal: list/search help, the skill, What's New, engineering notes, COVERAGE.md, and 006 Implementation Results moved with the flag. Ponytail: the help test is the check; nothing else to cut. Comprehensive: the help test failed before the lines existed, and each clause mutant failed on its own assert.

**Bugbot** (uncommitted stage): no blocking bug. It noted that help says "equals" and does not spell out spacing, malformed, or the bin check. Spacing and malformed are in SKILL.md, and both were run. The bin check was missing from SKILL.md; one sentence was added after the probe above (exit 3 before `stale`). P7 was the stale flag name; fixed. Help stays the short form. "equals" is the word in `decision:plan17-stale-guard-version-vector`.

**Left open:** the stage 4b nit on a counter above int64 and a repeated device. Neither can equal a stored vector, so the result is `stale`. A future sync plan can make them malformed. The INDEX check mark and this stage's second review are not done.

**Praise:** list and search help were not rewritten. The new test is one behavior in the suite that already owns the flag.
