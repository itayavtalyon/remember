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
| `update --if-updated-at <current updated_at> --text new` succeeds | E8.1 |
| `update --if-updated-at <old value>` → exit 1, `stale`, `memory changed since it was read`, row unchanged (body, tags, `updated_at`, VV) | E8 verification |
| `--if-updated-at 2026-10-12` → `usage` `invalid timestamp: 2026-10-12` | D10 |
| `--if-updated-at` works with id, `--key`, `--sync-id`, and with `--deleted --undelete` | E8.1 locators |
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

- [ ] Design log 006 unchanged except Implementation Results rows for E5.1–
      E5.3 and E8.1.
- [ ] Every must-pass row green and in `tests/gate-suites`.
- [ ] D10, D11, P4 and the locked usage messages implemented exactly.
- [ ] One TS parser serves list, search and update.
- [ ] Help (`list`, `search`, `update`), SKILL.md, README What's New lines
      (`--sort`, `--since`/`--until`, `--if-updated-at`).
- [ ] Engineering notes `store_list` bullet updated (sort + time filters).
- [ ] Gate: ctest, lint, coverage 100/100, `ci-linux.sh` all green.
- [ ] Second review done; no open **blocking** item.
- [ ] `implementation-plans/INDEX.md` row marked ✅.

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
