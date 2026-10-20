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
| B5 | A branch inside a macro is counted, and the listed sides of each TU must equal its `llvm-cov report` count, so a branch in a header macro cannot drop out of the list (`decision:plan18b-macro-branches-counted`, `decision:plan18b-branch-list-equals-report`, stage 1). |
| B6 | No `assert()` in the four TUs: its failing side can never run, so the branch gate wins over `style:fail-fast` there. A removed guard is not replaced by an assert (`decision:plan18b-no-assert-pure-tus`, stage 1 second review). |

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
   Count every record in the TU's `branches` array, including a branch inside
   a macro, tagged `(macro)` (amended 2026-10-20 by
   `decision:plan18b-macro-branches-counted`; see the stage 1 notes). Exit 1
   when a TU's listed sides differ from its `llvm-cov report` count
   (`decision:plan18b-branch-list-equals-report`, second review). Otherwise the
   script still exits on the line gate alone. Record the
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

### 2026-10-20 — stage 1 (branch report, report mode)

One grill, answered with the recommended option: `decision:plan18b-macro-branches-counted`. Definition of Done boxes stay unticked; this stage does not finish the plan.

What changed: `scripts/check-coverage.sh` only. After the line gate, it writes `build-cov/coverage-branches.json` (`llvm-cov export -format=text -skip-functions`) and prints, per gated TU, `<file>: <n> branch sides, <m> missed`, then each missed side as `file:line:col true|false`, sorted by position. The exit code still comes from the function and line gates alone. The three `llvm-cov` calls now share one `COV_ARGS` array (binaries, profile, ignore regex) instead of three copies.

**Plan correction (grilled).** The stage text said to count only records whose file is the TU itself, not a macro expansion. A probe disagrees with the assumption behind it: llvm-cov puts a macro-expanded branch in the same `files[].branches` list (with `file_id` != 0 and the coordinates of the `#define`), not only in `expansions`, and `llvm-cov report` counts it in the TU's total. Probe (scratch `p.c`: `#define POS(x) ((x) > 0 ? 1 : 0)` used in one function, Homebrew LLVM 22.1.8): `files[0].branches` = `[[1,17,1,24,1,0,1,0,4], [3,51,3,59,0,1,0,0,4]]`; `llvm-cov report` TOTAL branches = 4. Itay chose to count them, so the gate equals Definition of Done box 2. A macro side prints with a ` (macro)` tag. On this tree the four TUs have 0 records with `file_id` != 0 (the only function-like macro, `PIN_CAT` in `unicode.c`, is a `_Static_assert`), so the counts do not change.

Report on this tree, macOS (Homebrew LLVM 22.1.8), matching the baseline exactly:

```
src/search_terms.c: 198 branch sides, 26 missed
src/unicode.c: 74 branch sides, 0 missed
src/normalize.c: 172 branch sides, 6 missed
src/output.c: 584 branch sides, 194 missed
```

The missed sides (`line:col` + `T`rue / `F`alse):

- `src/search_terms.c` (26): 30:5T, 32:5T, 40:5F, 42:5T, 44:5T, 45:5T, 46:5T, 47:5T, 48:5T, 49:5T, 101:9T, 124:13T, 172:13T, 181:13T, 228:12F, 234:17T, 269:13T, 276:13F, 278:17T, 321:9T, 325:9F, 373:16T, 423:9T, 431:9T, 441:13T, 465:22F
- `src/normalize.c` (6): 147:43F, 153:13F, 261:9T, 338:9T, 361:24T, 424:9F
- `src/output.c` (194): 62:9T, 70:9T, 84:9T, 87:36F, 93:16T, 107:5T, 116:9T, 120:23T, 123:13T, 127:12T, 132:9T, 135:12T, 142:9T, 142:46T, 143:9T, 147:9T, 151:9T, 154:9T, 157:9T, 160:9T, 163:9T, 166:9T, 168:39F, 171:9T, 174:9T, 177:9T, 180:9T, 183:9T, 193:9T, 199:9T, 210:9T, 210:24T, 216:9T, 219:9T, 222:9T, 225:9T, 235:9T, 238:23T, 241:9T, 246:13T, 246:23F, 246:23T, 249:13T, 253:9T, 262:9T, 262:37T, 265:24T, 271:9T, 274:9T, 286:9T, 286:24T, 292:24T, 302:23T, 305:13T, 309:9T, 317:9T, 356:9T, 362:9T, 418:9T, 421:23T, 424:9T, 428:23T, 431:13T, 434:13T, 437:13T, 441:9T, 451:9T, 454:9F, 454:23T, 459:13T, 459:40F, 477:9T, 480:24T, 495:13F, 502:9F, 506:9T, 515:9T, 524:9F, 553:9T, 553:24T, 557:9T, 560:50F, 571:42T, 575:37F, 582:47F, 585:22F, 598:9T, 598:22T, 602:9T, 606:9T, 609:9T, 612:9T, 615:9T, 618:9T, 621:9T, 624:9T, 627:9T, 639:9T, 643:13T, 646:23T, 650:13T, 654:9T, 663:9T, 666:9T, 669:9T, 686:13F, 691:13T, 697:27T, 700:13T, 705:33T, 712:13T, 724:9T, 724:24T, 724:42T, 727:23T, 730:9T, 733:9T, 736:9T, 740:13T, 740:23F, 740:23T, 743:13T, 747:9T, 758:9T, 758:24T, 761:23T, 764:9T, 768:13T, 771:16T, 774:9T, 778:23T, 781:13T, 785:9T, 796:9T, 796:24T, 802:9T, 805:9T, 811:13T, 815:17T, 818:20T, 823:33T, 827:13F, 827:35T, 830:13T, 839:9T, 847:9T, 847:24T, 850:9T, 858:9T, 858:24T, 861:9T, 864:9T, 874:9T, 874:24T, 877:9T, 880:9T, 884:13T, 887:16T, 890:9T, 898:9T, 898:24T, 901:9T, 914:9T, 918:9T, 918:27T, 918:51T, 919:9T, 919:39T, 922:9T, 925:9T, 928:9T, 931:9T, 934:9T, 938:9T, 941:12T, 948:9T, 951:9F, 951:23T, 954:9T, 958:13T, 958:23F, 958:23T, 961:13T, 965:9T, 975:9T, 975:24T, 978:9F, 978:23T, 981:9T, 986:23T, 989:13T, 993:9T, 1003:9T, 1011:9T

Independent check (round 1.4): the same list, built from `llvm-cov show -show-branches=count src/<f>.c` (`Branch (L:C): [True: n, False: m]` lines, one zero → one missed side, `sort -u`), is identical: 226 lines, `diff` empty.

Mutations of the report (round 1.1, 2.1), each one clause on an extracted copy of the Python block, run on the real `coverage-branches.json` and diffed against the `llvm-cov show` list:

- Side labels swapped (`("true", b[5]), ("false", b[4])`): RED (diff).
- `count == 0` → `count <= 1`: RED.
- Line taken from `end_line` (`b[2]`): RED. Column taken from `end_col` (`b[3]`): RED.
- File lookup without the `root` prefix: exit 1, `error: src/search_terms.c has no coverage record`.
- `(macro)` tag condition → `False`: the fake export below prints `src/search_terms.c:1:17 false` with no tag (RED against the expected tagged line).
- Unmutated: GREEN (diff empty).

Macro probe through the real report code: the scratch export above, with its file renamed to `/fake/src/search_terms.c` and empty records for the other three TUs, prints `src/search_terms.c: 4 branch sides, 2 missed`, `src/search_terms.c:1:17 false (macro)`, `src/search_terms.c:3:51 true`. 4 sides equals the probe's `llvm-cov report` TOTAL.

Probes beyond the plan (round 1.2):

- A missing export file: Python raises `FileNotFoundError` and exits 1; under `set -e` the script stops.
- A gated TU absent from the export (wrong root): exit 1 with `error: <file> has no coverage record` (mutation above).
- `lcov` export as the source instead of a second export: rejected. Its `BRDA:line,block,branch,count` records have no column (`BRDA:46,0,0,98` in `build-cov/coverage.lcov`), so two conditions on one line (`a && b`) could not be told apart in the plan's `file:line:col` format.

Linux LLVM (ubuntu 24.04, the image `ci-linux.sh` uses; `clang` and `llvm` 18.1.3): `BUILD_DIR=build-cov-linux ./scripts/check-coverage.sh` in Docker, exit 0. The branch record positions (`[line, col, end_line, end_col]` per TU) are identical to macOS: a Python compare of both `coverage-branches.json` files printed `True True` (same files, same records). The missed counts differ in `output.c` only: 197 on Linux, 194 on macOS. Linux misses `output.c` 213:9, 268:9, 295:9, 320:9, 347:13, 359:9, 483:9 (true) that macOS takes; macOS misses 222:9, 271:9, 305:13, 663:9 (true) that Linux takes. All 11 are stream-write-failure sides. The cause is the existing failing stream in `tests/test_coverage_edges.c:162`: `/dev/full` under `#ifdef __linux__`, a pipe with its read end closed elsewhere, so each platform's first failing write lands in a different writer. I did not trace the exact flush points further; stage 4 replaces that helper with a deterministic failing stream on both platforms, and stage 6 must be green on both. So the must-pass row "the branch report lists exactly the baseline's missed sides" holds on macOS; on Linux it holds for `search_terms.c`, `normalize.c`, and `unicode.c`.

GitHub Actions `linux-coverage` (`ubuntu-latest`) was not run: that needs a push, and stage 1 does not push. Its record format is the same `llvm-cov export` JSON; the job's proof is Definition of Done box 7 (stage 6).

New dependency (round 9.2): none. The report uses `llvm-cov export -format=text` (already required for the lcov export) and the `python3` the line gate already uses. Consumers: `check-coverage.sh` callers (local, `linux-coverage` job) get one more artifact, `build-cov/coverage-branches.json`; nothing reads it yet.

Simplify pass (round 1.5):

- Applied: `COV_ARGS` holds the binaries, profile, and ignore regex once. The three `llvm-cov` calls (report, lcov export, JSON export) shrank from 15 lines to 3 plus one array line; `IGNORE_REGEX` is gone.
- Declined: fold the branch report into the line gate's Python heredoc (saves 4 lines of heredoc framing). The line gate `sys.exit(1)`s on its first failure, so a merged block would hide the branch list exactly when both are red; two blocks keep the gates independent.
- Declined: parse `llvm-cov show -show-branches=count` with `grep`/`awk` instead of Python (about 8 lines shorter). It prints counts abbreviated (`1.52k`) and is meant for humans; the cross-check above uses it once, the gate should not.
- Declined: build the report from the lcov export (no second export, 1 line). No column; see the probe above.
- Declined: inline `BINS` into `COV_ARGS` (1 line). `BINS` names the three binaries and keeps the array line readable.

Gate on this tree (2026-10-20). The C sources are unchanged from `123ac31`; only the script changed after the C gate runs:

- `ctest --test-dir build` (ASan/UBSan, `REMEMBER_TEST_VFS=ON`): 5/5.
- `./scripts/lint-all.sh`: LINT OK. `shellcheck scripts/check-coverage.sh`: clean.
- `./scripts/ci-linux.sh` (ubuntu 24.04): exit 0, iwyu PASS, scan-build PASS, LINT OK.
- `cmake --build build --target leaks-macos`: PASS.
- `./scripts/check-coverage.sh` (final script): exit 0; functions 100%, effective lines 100%; branch report as above.

## Review Notes

### 2026-10-20 — second-opinion deep review (stage 1)

Ref `743874e`. Baseline gate on a clean worktree of the ref: see the gate line at the end. Fixes in a clean worktree, full gate there before commit.

**Verdict: stage 1 is done. No open blocking item.** Two grills, both answered with the recommended option.

Findings:

- **important (fixed): a branch in a header macro dropped out of the list.** `decision:plan18b-macro-branches-counted` rested on "the gate equals `llvm-cov report`, so no branch can hide in a macro". That is true for a macro `#define`d in the TU, not for one in a header. Probe (Homebrew LLVM 22.1.8, scratch `p.c` that includes `h.h` with `#define HPOS(x) ((x) > 0 ? 1 : 0)`, plus a local `POS` and an `assert`): `llvm-cov report` counts 6 branch sides for `p.c`; `p.c`'s `files[].branches` holds 2 records (4 sides); the `HPOS` record sits in `h.h`'s entry (`[1,18,1,25,1,0,2,0,4]`). Grilled; Itay chose `decision:plan18b-branch-list-equals-report`: per TU, the script exits 1 when `2 * len(branches)` differs from `summary.branches.count`. Fix in `check-coverage.sh` (4 lines). Real tree: all four TUs match (198/74/172/584 both ways), so the report output is unchanged. A `static inline` function in a header is attributed to the header in both the report and the export (probe `h2.h`/`q.c`: `q.c` 0 sides, `h2.h` 2), so it is out of B1's scope, not hidden. No `src/*.h` has a function-like macro or a `static inline` today.
- **important (grilled, no code change): `assert` conflicts with B2.** An `assert` in a gated TU becomes a branch at its use site with `file_id` 0 (probe: `assert(x != 99)` → `[4,16,4,16,0,1,0,0,4]`, true side 0). Its failing side can never run, so under no-excuses it fails the gate, while `style:fail-fast` asks for asserts on programmer invariants. Stage 2 removes NULL guards, which is exactly where an assert would be the reflex. Itay chose `decision:plan18b-no-assert-pure-tus`: no `assert` in the four TUs, and a removed guard is not replaced by one. Recorded as B6. No `assert(` in `src/` today except `src/test_vfs.c`.
- **nit (fixed):** the amended stage 1 text had one 130-column line; rewrapped while adding the B5 sentence.
- **praise:** probing the plan's macro assumption before coding and grilling the correction; the independent `llvm-cov show` cross-check; rejecting the lcov source with a concrete reason (no column in `BRDA`); `COV_ARGS` (15 lines → 3); recording honestly that the must-pass row holds only on macOS.

Verification by running:

- Independent cross-check re-run: the report's 226 missed sides equal a `llvm-cov show -show-branches=count` parse of the three binaries (`diff` empty).
- Per TU, the report's sides and missed count equal `summary.branches` (`count` / `notcovered`): 198/26, 74/0, 172/6, 584/194. 0 macro records, 0 duplicate positions.
- Linux claim re-run (ubuntu 24.04, clang/llvm 18, `BUILD_DIR=build-cov-linux ./scripts/check-coverage.sh` in a fresh worktree of the ref): exit 0, 26 / 0 / 6 / 197. The `output.c` diff against macOS is exactly the coder's 11 lines (Linux only: 213, 268, 295, 320, 347, 359, 483; macOS only: 222, 271, 305, 663).
- Binary probes (C tree equals `59dc617`): empty and spaced query → `empty search query` exit 1; newline, 300-byte, 100 KB, invalid UTF-8 → exit 0; ESC → `invalid search query`; `--db` with no value → exit 1; unwritable `--db` path → exit 1 with the mkdir reason; `get`/`list` under `TZ=Pacific/Kiritimati` and `America/Adak` print UTC stamps.
- Zero-hit lines: the stage changed no C file, so no new uncovered line.

Mutation table (one clause each, extracted report block run on the gate's real `coverage-branches.json`, compared with the independent list; the mismatch check also on a fake export whose `search_terms.c` record is the real `p.c` probe export):

| Mutant | Result |
|--------|--------|
| Side labels swapped (`b[5]`/`b[4]`) | killed |
| Line from `end_line` (`b[2]`) | killed |
| Column from `end_col` (`b[3]`) | killed |
| `count == 0` → `count <= 1` | killed |
| Skip `file_id != 0` records (the plan's old text) | survived on the real tree (0 macro records, expected); the coder's fake export kills it |
| New check: `if sides != want` → `if False` | survived on the real export, killed by the header-macro export (exit 0 instead of 1) |
| New check: `want` computed from the list instead of the summary | killed by the header-macro export |
| Unmutated on the header-macro export | exit 1, `error: src/search_terms.c: 4 listed branch sides, llvm-cov report counts 6 (a branch in a header macro?)` |

Simplify pass:

- Applied by the coder: `COV_ARGS`. Agree.
- `json.loads(Path(...).read_text())` → `json.load(open(...))` drops the `pathlib` import (1 line). Declined: the line gate's heredoc uses `Path(...).read_text()`, so it stays consistent.
- `root = sys.argv[1].rstrip("/") + "/"` → `sys.argv[1] + "/"` (0 lines; `pwd` never ends in `/` here). Not worth a change.
- Merge the mismatch check into the `print` line: 1 line shorter but hides the error in an f-string conditional. Declined, for clarity.
- The coder's four declines (merge heredocs, awk over `llvm-cov show`, lcov source, inline `BINS`) re-checked: agree with each reason.

Coder rating: **8.5 / 10.** Strengths: probed the plan's macro assumption, found it wrong, and grilled instead of deciding; independent cross-check; one-clause mutants of every report clause; reproduced and explained the Linux difference, then deferred it to the stage that owns it; DRY'd the three `llvm-cov` calls. Misses: the macro probe tried one form (a macro in the TU) and then generalized to "no branch can hide in a macro" (round 5.4: try one more form, here a header); did not check the new strict gate against standing rules, so the `assert` vs no-excuses conflict surfaced only in review (round 8.3: grill when two decisions conflict); one overlong line in the amended plan text.

Instructions for the coder:

1. When a probe proves a tool's behavior for one form, repeat it across a file boundary (a macro or inline function in a header, an included file) before writing "no X can hide".
2. When a gate's list should equal a tool's own total, check that equality in the script on every run, not once in the notes.
3. Before a new strict gate lands, check each standing style rule (assertions, guards, defensive returns) against it and grill any conflict in the same stage.

Gate (2026-10-20, clang + `REMEMBER_ENABLE_SANITIZERS=ON` + `REMEMBER_TEST_VFS=ON`, configured twice, cache flags checked): build, ctest, `lint-all.sh`, `check-coverage.sh`, `leaks-macos`, `ci-linux.sh` → `GATE-ALL-GREEN` on the ref `743874e`, and again on the fixed tree (the script fix plus these notes; this gate line added after it is Markdown only). `shellcheck scripts/check-coverage.sh`: clean. Standard filed: `standard:coding` ROUND 10.
