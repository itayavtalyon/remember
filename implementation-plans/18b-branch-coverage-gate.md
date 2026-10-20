# 18b — Branch-coverage gate for the pure TUs

Insert into [plan 18](18-union-search.md). It runs on `plan18-union-search`
after plan 18 stage 2 and before stage 3. Plan 18 stages 3–6 then meet this
gate as they are written. It does not change search behavior.

## Goal

- `scripts/check-coverage.sh` fails when any branch side in `src/search_terms.c`,
  `src/unicode.c`, `src/normalize.c`, or `src/output.c` is not taken by a test.
- No branch side is excused. Every side is reached by a test, or the code that
  holds it is removed or restructured.
- The line gate stays as it is.

## Why

In plan 18 stage 2, `search_tag_order`'s `return -1` (the exact tag sorts
first) never ran. `qsort` only passed the exact tag as the right-hand
argument. `check-coverage.sh` excludes every zero-hit `return -1;` line as
defensive, so line coverage still read 100% effective. Line coverage cannot
tell a defensive return from a rule branch. Branch coverage can.

## Source of truth

- `decision:coverage-branch-gate-followup` (scope, and placement: before plan 18 stage 3)
- `decision:plan18b-no-branch-excuses` (no marker, no skip-list; fault injection; failing stream)
- `quality:coverage-100`, `quality:test-the-contract`, `standard:coding` (round 9 is in force)

Do not invent. A case these decisions do not cover → stop and ask Itay.

## Baseline (measured 2026-10-20 at `59dc617`)

`llvm-cov report` on `build-cov` after `./scripts/check-coverage.sh`, macOS,
Homebrew LLVM. A branch counts as two sides (true and false).

| File | Branch sides | Missed | Cover |
|------|-------------:|-------:|------:|
| `src/unicode.c` | 74 | 0 | 100.00% |
| `src/normalize.c` | 172 | 6 | 96.51% |
| `src/search_terms.c` | 198 | 26 | 86.87% |
| `src/output.c` | 584 | 194 | 66.78% |
| **Total** | 1,028 | 226 | |

The missed sides, sorted by what reaches them (reviewer's classification from
`llvm-cov show -show-branches=count`; stage 1 replaces it with the gate's own list):

| Kind | `normalize.c` | `search_terms.c` | `output.c` |
|------|--------------:|-----------------:|-----------:|
| A stream write fails (`fputs` / `fprintf` / `fputc` result) | — | — | ~106 |
| A NULL argument or an allocation failure | 3 | 2 | ~65 |
| A callee returns a status other than OK | — | 9 | — |
| A switch case for a status the callee never returns | — | 10 | — |
| A rule or logic side | 3 | 5 | ~20 |

Examples of rule sides that no test takes today:

- `search_terms.c:373`: `search_tag_order` with the busier tag on the left (`return -1` of the count rule). This is the same pattern as the stage 2 bug.
- `search_terms.c:465`: a `# ` title with no newline after it.
- `search_terms.c:276`: the in-order search already failed for an earlier token.
- `normalize.c:147`, `:153`: the surrogate and 4-byte range checks, false side.

## Locked decisions (Itay 2026-10-20)

| # | Decision |
|---|----------|
| B1 | Scope: the four pure TUs above. Every branch side must be taken. Other files keep the line gate only. |
| B2 | No excuses. No inline marker and no skip-list. A side that cannot be reached is removed or restructured, and the notes argue each removal. |
| B3 | A side reached only by failure gets fault injection under `REMEMBER_TEST_HOOKS`, in the `store_test_fail_alloc_after(n)` style. |
| B4 | Stream write failures are covered with a failing-stream test helper. They are not removed and not marked. |

## Scope

**In:** a branch report and gate in `scripts/check-coverage.sh`; tests that
take every side in the four TUs; removal or restructuring of sides that cannot
run; fault-injection hooks for allocations and callee statuses; engineering
notes and `docs/QUALITY.md`.

**Out:** search behavior; other `src/` files; the line-coverage regex; plan 18
stage 3 code.

## Files

`scripts/check-coverage.sh`, `src/search_terms.c`, `src/normalize.c`,
`src/output.c` (and their headers only if a hook is added),
`tests/test_search_terms.c`, `tests/test_normalize.c`, `tests/test_unicode.c`,
a test file for `output.c` (existing or new, linked into
`remember_store_tests`), `tests/COVERAGE.md`, `docs/engineering-notes.md`,
`docs/QUALITY.md`, `.github/workflows/ci.yml` (only if the branch gate needs a
newer LLVM on Linux).

## Must-pass tests

| Check | Proves |
|-------|--------|
| On the stage 1 tree, the branch report lists exactly the baseline's missed sides per file (6 / 26 / 194 / 0), each as `file:line:col true|false` | The report reads the real data |
| Delete `search_tag_order_puts_exact_first_from_either_side`: the enforced gate exits non-zero and names `src/search_terms.c` at the `return -1` line | B1, the gate catches the stage 2 bug |
| Remove one `output.c` failing-stream test: the gate exits non-zero and names that writer's branch | B4 is enforced |
| Each `output.c` writer, driven by the failing stream to fail at every write in turn, returns its error value, and the call that succeeds returns 0 | B4 |
| `search_tag_order` with the busier tag on the left returns -1, and on the right returns 1 | The count rule from both sides |
| Each fault hook makes its allocation or callee fail on the Nth call, and the function returns its OOM or error status, with nothing leaked under ASan | B3 |
| Each removed side: the notes name it and the reason it could not run, and the existing tests stay green | B2 |
| GitHub Actions `linux-coverage` passes with the branch gate on | The gate holds on Linux LLVM too |

Mutate one clause at a time, as `standard:coding` says. For the gate itself,
the mutations are the two deleted-test rows above, plus a test that takes only
one side of a new branch.

## Stages

TDD. One red test, then the smallest change. Gate green after each stage.

1. **Branch report (report mode).** In `check-coverage.sh`, read the branch
   records for the four files from `llvm-cov export -format=text` (the
   `branches` arrays) and print each missed side as `file:line:col true|false`.
   Count only records whose file is the TU itself, not an expansion from a
   header or macro. The script still exits on the line gate alone. Record the
   list in the notes, and check that it matches the baseline counts. Probe that
   the Linux LLVM in `ci-linux.sh` and in GitHub Actions emits the same records.
2. **Remove what cannot run.** Go through each missed side. A NULL guard in a
   static helper whose callers already check, or a switch case for a status the
   callee never returns, is removed or restructured. Write each removal and its
   reason in the notes. If a removal changes a public function's contract (a
   NULL check on an exported function), stop and ask Itay. All existing tests
   stay green.
3. **Rule and logic sides.** One named test per rule side in `normalize.c`,
   `search_terms.c`, and `output.c`. Comparators get a test in each argument
   order (`standard:coding` round 9).
4. **Failing stream.** A test helper that hands out a `FILE *` whose writes fail
   at a chosen byte. First probe `fmemopen` with `setvbuf(_IONBF)` on macOS and
   on Linux. If the two platforms disagree, use `funopen` (BSD) or
   `fopencookie` (glibc) behind a test-only shim. Drive each `output.c` writer
   once per write: fail at the 1st write, then the 2nd, and so on, until the
   call succeeds.
5. **Fault injection.** Allocation hooks for `normalize.c` and
   `search_terms.c`, and callee-status hooks for the `search_terms.c` sides
   that need a failing callee. Reuse `unicode_test_inject_nfc` where it already
   reaches the side. Before adding the first hook, ask Itay whether the pure
   TUs share one allocation hook or get one per module, as the store does.
6. **Enforce and document.** The script exits non-zero on any missed side in
   the four TUs. Run the two deleted-test mutations above and record the red
   output. Write the rule in `docs/engineering-notes.md` and `docs/QUALITY.md`,
   and add the branch gate to plan 18's Definition of Done. Full gate. Stop.
   Plan 18 resumes at stage 3.

## How to work

Work on `plan18-union-search`, after plan 18 stage 2's second review. Load
`standard:coding`, `workflow:deep-review`, and `status:remember`. After each
stage: full gate (sanitizers, `REMEMBER_TEST_VFS=ON`, ctest, `lint-all.sh`,
`check-coverage.sh`, `ci-linux.sh`, `leaks-macos`), in-session review,
Implementation Notes, commit, then `.claude/REVIEW_REQUEST` for the second
opinion. On pause, update `status:remember`. Do not open a pull request.

## Definition of Done

- [ ] `check-coverage.sh` fails on any missed branch side in the four TUs, proved by the two deleted-test mutations.
- [ ] All four TUs show 100% branch sides in `llvm-cov report`.
- [ ] No marker, no skip-list, and no new line-regex exclusion was added.
- [ ] Every removed side is named in the notes with the reason it could not run.
- [ ] Every fault hook is test-only (`REMEMBER_TEST_HOOKS`) and absent from a Release build.
- [ ] Engineering notes, `QUALITY.md`, and plan 18's Definition of Done mention the branch gate.
- [ ] Gate green, including GitHub Actions `linux-coverage`.
- [ ] Second review done, no open blocking item.
- [ ] INDEX row 18b marked ✅.

## Second review handover

Run the gate. Then re-run the two deleted-test mutations and check that the
gate names the right line. Read every hook for a Release leak (`nm` on a
Release `remember`). Read every removed side against its reason. Read the
failing-stream helper on both platforms.

## Implementation Notes

## Review Notes
