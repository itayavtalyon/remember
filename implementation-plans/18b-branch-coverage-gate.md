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
   Count every record in the TU's `branches` array, including a branch inside
   a macro, tagged `(macro)` (amended 2026-10-20 by
   `decision:plan18b-macro-branches-counted`; see the stage 1 notes). The script still exits on the line gate alone. Record the
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

### 2026-10-20 — stage 2 (remove what cannot run)

Itay, after stage 1: "fix also the ones Linux finds. Move on with the plan". So the stages run back to back, and the gate must end green on Linux too: the 11 Linux-only stream sides (stage 1 notes) are stage 4's job, driven by the deterministic failing stream on both platforms.

No public contract changed: every removed side sits in a static helper or behind a static caller. The public NULL and argument guards (`output_*(NULL, …)`, `count > 0 && entries == NULL`, `normalize_token(…, 0)`, `body_hash_hex(NULL, …)`) stay; a unit test can reach each one, so stage 3 tests them instead.

Removed or restructured, each with the reason it could not run (side as `line:col` on `59dc617`):

| Side | Code | Why it could not run | Change |
|---|---|---|---|
| `search_terms.c` 30:5T, 32:5T | `from_unicode` cases `UNICODE_DONE`, `default` | No caller passes `UNICODE_DONE`: `plain_label` returns on it before mapping, and `unicode_nfc`, `unicode_decode`, `unicode_graphemes_open` never return it. `default` needs a value outside the enum. | `switch` → three `if`s; anything not OK / INVALID / OOM is INTERNAL. The INTERNAL fallthrough is taken (`search_tokens_nfc_internal_is_internal`). |
| `search_terms.c` 44:5T … 49:5T | `from_norm` cases EMPTY, TOO_LONG, INVALID_UTF8, INVALID_CHAR, INTERNAL, `default` | `from_norm` had one caller, `fold_hebrew_marks(nfc, nfc_len, &folded)`. That function returns INTERNAL only for a NULL `src` or `out`; `nfc` is non-NULL after `unicode_nfc` returns OK (it is utf8proc's buffer) and `&folded` is never NULL. It returns no other status. | `from_norm` deleted. One line: `st = (fold == NORM_OK) ? SEARCH_OK : SEARCH_ERR_OOM;`. Its OOM side (`308:10F` now) and the old `40:5F` / `42:5T` are the same failure, reached by stage 5's allocation hook. |
| `search_terms.c` 228:12F | `while (i <= len)` exits by its condition | At `i == len` the body always returned or `break`ed, so the condition never read false. | `while (i < len)`, then the last segment checked once after the loop. The shared test is `segment_starts_with` (new static helper). |
| `output.c` 107:5T, 147:9T, 602:9T | `bin_token` `default: return NULL`, and both callers' `bin == NULL` | `bin_token` only ever received `store_bin_of(…)`, which returns one of the three `StoreBin` values (`store_sqlite.c:2125`). | `BIN_TOKENS[]`, a designated-initializer table indexed by the bin; no branch. |
| `output.c` 515:9T | `link_type_token` `n == NULL` | Both callers pass `&links[i]` (`write_stub_json` after its own check, `output_related_human`). | Guard deleted. |
| `output.c` 524:9F | `kind == STORE_EDGE_SUPERSEDES` false, then `return "related"` | `StoreEdgeKind` has three values (`store.h:373`); RELATED and CITES return earlier. | The SUPERSEDES ternary is the last return. |
| `output.c` 598:9T | `write_stub_json` `n == NULL` | Its three callers pass `&links[i]`. | `n == NULL` dropped; `n->neighbor_sync_id == NULL` stays (data, reachable). |
| `output.c` 553:9T, 553:24T | `fill_preview` `dst == NULL`, `dst_cap == 0` | All three callers pass a `char[PREVIEW_BUF]` stack buffer. | Guard deleted, and the `dst_cap` parameter with it: `fill_preview(char dst[PREVIEW_BUF], …)`. |
| `output.c` 575:37F | `body[i + k] != '\0'` in the copy loop | `utf8_clen_bounded` already returns 1 when a NUL falls inside the sequence. | Clause deleted; a comment names the reason. |
| `output.c` 582:47F | the post-loop `if (body[i] != '\0' && … != '\r') truncated = 1;` | The loop ends naturally only on `\0`, `\n`, or `\r`, so that `if` is false; after a `break`, `truncated` is already 1. The whole block never changed `truncated`. | Block deleted (3 lines). |
| `output.c` 585:22F | `truncated && o + 3U < dst_cap` false | `o` starts at 0 and each copy passed `o + clen + 4 < PREVIEW_BUF`, so `o + 3 < PREVIEW_BUF` always holds. | `if (truncated)`. |
| `output.c` 914:9T | `output_one_conflict_json` `row == NULL` | Its one caller passes `&rows[i]`. | Guard deleted. |
| `output.c` 87:36F | `field.value != NULL ? field.value : empty_str()` | Not unreachable, but duplicated: `output_json_string` already renders NULL as `""` (`if (s == NULL) s = empty_str();`). | Ternary deleted; the value goes straight to `output_json_string`. Stage 3 tests a NULL field. |

Counts on this tree (macOS): `search_terms.c` 176 sides, 16 missed (was 198 / 26); `output.c` 548 / 181 (was 584 / 194); `normalize.c` 172 / 6; `unicode.c` 74 / 0.

What the remaining sides need (the plan for stages 3–5):

- Rule sides (stage 3): `normalize.c` 147:43F (3-byte above the surrogates), 153:13F (4-byte overlong); `search_terms.c` 361:16T (busier tag on the left), 453:22F (`# ` title with no newline), 263:13F (in-order already lost, then a later token); public argument guards in `normalize.c` (361:24T `out_cap == 0`, 424:9F `data == NULL`) and in `output.c`; the `output.c` data sides (NULL `version_vector`, `source`, a tag, `updated_at`, `neighbor_sync_id`, a conflict field, an unknown conflict reason, `\r` and DEL in a preview, links for another subject).
- Stream writes (stage 4): every `fputs` / `fprintf` / `fputc` result in `output.c`, and the callers that propagate them. This includes the 11 sides on which macOS and Linux disagree.
- Allocation (stage 5): `normalize.c` 261:9T, 338:9T; `search_terms.c` 83:9T (`dup_bytes`), 411:9T (`plain_label`), 308:10F / 309:9T (the fold).
- Callee status (stage 5): `search_terms.c` 106:13T, 154:13T, 222:13T (`unicode_decode` on NFC text), 163:13T, 256:13T, 265:17T, 313:9F (propagation of those), 419:9T, 429:13T (grapheme iterator on validated text).

Mutations (one clause each; runner: copy the file, apply one replacement, `cmake --build build-mut` (ASan/UBSan, `REMEMBER_TEST_VFS=ON`), run `remember_store_tests` and/or `remember_tests --only <gate suites>`, restore):

- `from_unicode`: OK → DONE, INVALID → INTERNAL, OOM → INTERNAL: RED (`search_tokens_split_on_ascii_punctuation_and_space`, `search_tokens_invalid_utf8_is_rejected`, `search_tokens_nfc_oom_is_oom`, and more).
- `segment_starts_with` `>=` → `>`: RED (`search_tier_in_order_tokens_are_tier_2`, …). `memcmp == 0` → `!= 0`: RED (`search_tier_a_token_without_a_segment_does_not_match`, …). Both rerun after the clang-tidy fix below: RED.
- Last-segment check dropped, and its start `seg` → `from`: RED (`search_tier_word_prefix_of_a_key_segment_matches`, …).
- Fold ternary `== NORM_OK` → `!=`: RED (`search_tier_word_prefix_of_a_key_segment_matches`, …).
- `BIN_TOKENS` each value swapped or misspelled: RED (`json_expired_and_deleted_bins`, `human_expired_and_deleted_neighbor_marks`, `json_entry_field_order_bin_always_enum`).
- SUPERSEDES → `"related"`: RED (`get_list_json_stubs_five_name_preview`). `if (truncated)` → `if (!truncated)`: RED (`get_list_json_stubs_five_name_preview`, `list_sort_*`). Copy loop `k < clen` → `k < 1`: RED (`preview_control_and_multibyte_truncation`).
- **Survived, killed in stage 3** (the code existed before; no test pins it):
  - `find_segment` inner length `i - seg` → `len - seg` (ST8). Equivalent for tokens from `search_tokens_build`: a token never holds a segment separator (all five ASCII separators and White_Space split tokens), and both are valid UTF-8, so a token cannot match across a separator byte for byte. A hand-built `SearchTokens` with `a:b` reaches it; E2.5a says a token matches *a* segment.
  - SUPERSEDES direction `==` → `!=` (O4): no test checks `supersedes` vs `superseded_by`.
  - The preview buffer bound `PREVIEW_BUF` → `PREVIEW_BUF - 100` (O7): no test checks a preview cut by bytes rather than by code points.
  - A NULL field rendered as `"x"` instead of `""` (O9): no test passes a NULL `body` / `created_at` / `updated_at`.

Lint fix: `return a && b;` in `segment_starts_with` tripped `readability-implicit-bool-conversion`; it now uses the file's `if (…) { return true; } return false;` form.

Simplify pass (round 1.5):

- Applied: `from_norm` (16 lines) → one ternary line; `bin_token` (12 lines) + two NULL checks (6 lines) → a 5-line table; the `fill_preview` dead block and guards (−8 lines) and its `dst_cap` parameter; the duplicate NULL ternary in `write_json_field_str`.
- Applied: `find_segment` lost the `at_end` flag and its two `if (at_end)` tests; the match test moved into `segment_starts_with`, used twice.
- Declined: drop `segment_starts_with`'s length argument and always compare against `len - seg` (ST8 shows it is equivalent for built tokens). It would make E2.5a depend on a tokenizer invariant that `search_facet_classify` cannot see; the length argument keeps the rule local.
- Declined: share `BIN_TOKENS` with `store_sqlite.c`'s `bin_token_store` (`store_sqlite.c:5999`). Out of this plan's scope (other `src/` files); noted for a later DRY pass.

### 2026-10-20 — stage 3 (rule and logic sides)

No grill. All new tests run in `remember_store_tests` (ASan/UBSan). `src/output.c` and the new `tests/test_output.c` (38 tests) are now in that target, so `output.c` has an in-process unit suite for the first time; before this it ran only under the black-box CLI suites. `tests/register.h`, `tests/test_store_main.c`, and `tests/COVERAGE.md` name the new suite.

Tests by side (`line:col` on the stage 2 tree):

| Side | Test |
|---|---|
| `search_terms.c` 361:16T (count rule, busier on the left) | `search_tag_order_puts_the_busier_tag_first_from_either_side` |
| comparators in each argument order (round 9.4) | `search_tag_order_puts_the_lower_tier_first_from_either_side`, `…_breaks_a_count_tie_by_code_point_from_either_side`, `search_key_order_puts_the_lower_tier_first_from_either_side`, `…_breaks_a_tier_tie_by_code_point_from_either_side`; the existing exact test now uses the shared `assert_first_from_either_side` |
| `search_terms.c` 453:22F (title with no newline) | `search_label_title_may_be_the_whole_body` |
| `search_terms.c` 263:13F (order already lost) | `search_tier_order_lost_at_the_second_token_stays_tier_3` |
| stage 2 survivor ST8 (inner segment length) | `search_tier_a_token_does_not_match_across_a_separator` (a hand-built `SearchTokens` with `a:b`) |
| `normalize.c` 147:43F | `body_trim_accepts_the_first_scalar_after_the_surrogates` (U+E000) |
| `normalize.c` 153:13F | `body_trim_rejects_a_four_byte_overlong` (F0 8F BF BF); boundary `body_trim_accepts_the_first_four_byte_scalar` (U+10000) |
| `normalize.c` 361:24T | `normalize_token_zero_capacity_is_internal` (and the buffer stays unwritten) |
| `normalize.c` 424:9F | `body_hash_of_null_data_is_the_empty_digest` (NULL with length 3) |
| `output.c` public guards (40 clauses) | one `output_*_rejects_missing_arguments` test per writer, plus `output_human_writers_reject_a_missing_stream`, `output_device_human_rejects_a_missing_id` |
| `output.c` data sides | `output_entry_json_rejects_an_incomplete_entry`, `…_renders_a_null_source_as_unknown`, `…_renders_null_text_fields_as_empty_strings`, `output_links_write_envelope_rejects_a_link_without_a_sync_id`, `output_conflicts_envelope_rejects_an_incomplete_conflict`, `output_tags_human_renders_a_null_name_as_empty`, `output_entry_human_line_skips_a_null_tag`, `…_leaves_a_null_updated_at_empty`, `…_leaves_a_null_body_preview_empty` |
| `output.c` rule sides | `output_get_envelope_lists_only_the_subjects_links`, `output_entry_human_line_counts_only_the_subjects_links`, `output_links_write_envelope_names_both_supersedes_directions`, `output_preview_stops_at_a_carriage_return`, `output_preview_masks_del`, `output_preview_cuts_by_bytes_before_the_scalar_cap`, `output_tags_human_writes_nothing_for_no_tags`, `output_conflicts_envelope_lists_no_conflicts`, `output_accepted_envelope_lists_no_entries`, `output_deleted_list_separates_entries_with_a_comma`, `output_conflicts_envelope_separates_conflicts_with_a_comma`, `output_related_human_omits_an_empty_preview` |

Every expected string in `test_output.c` was written by hand from the format strings in `output.c` before that test's first run, and each passed on its first run, so none was pasted from output. The byte-bound case was worked out on paper: iteration k checks `3k + 3 + 4 >= 192`, first true at k = 62, so 62 euro signs and `...`.

Mutations (one clause each, same runner as stage 2; `remember_store_tests` unless noted). All RED with the named test unless listed under equivalent:

- Comparators: count direction and its right-hand side (`? -1 : -1`), tier right-hand side: the order tests above. Title end without a newline → `0`: `search_label_title_may_be_the_whole_body`. ST8: `search_tier_a_token_does_not_match_across_a_separator`.
- `normalize.c`: surrogate max widened by 0x1000, 4-byte min lowered by 0x1000, `>=` → `>` on U+10000, `out_cap == 0` dropped (first survived: the call still returned INTERNAL, because `n + 1 > 0` fails too; the test now also asserts the buffer is unwritten, and the mutant writes `out[0] = '\0'`), `data != NULL` dropped (UBSan null load in `sha256.c:108`).
- `output.c`: SUPERSEDES direction, the byte bound (−100), NULL field → `"x"`, `version_vector[0] != '{'` → `'['`, `"unknown"` → `"unk"`, NULL tag name → `"?"`, the related-ids total counting every link (`3, 3, 3, 3, 3, +1`), the cell and the JSON `links` not skipping the other subject (rewritten as `== -1` / `== -subject_id` because `if (false)` is `-Wunreachable-code` and an unused `subject_id` is `-Wunused-parameter`), `\r` dropped from the preview stop, DEL → DEL+1, a NULL body previewed as `?`, a NULL tag printed, a NULL `updated_at` printed, `count > 0U &&` dropped from three envelopes, `neighbor_sync_id` check weakened, each of the five conflict-field clauses dropped, the two comma guards `i > 0U` → `i > 1U`, and the empty-preview check dropped.
- Every clause of every public `output.c` argument guard, generated from the source (each `X == NULL` term dropped in turn, each `count > 0U && X == NULL` → `count > 1U`): 40 of 40 RED (crash in, or FAIL of, the matching `*_rejects_missing_arguments` test). The related-links test first used two links and equal neighbor ids; it now uses five subject links plus one other subject's link with neighbor id 9, so a mutant that miscounts shows `, +1` and a mutant that skips the filter shows `9`.
- **Equivalent, kept**: `segment_tier`'s `if (in_order)` → `if (true)` (R5). Once order is lost, `cursor == found == len`, and `find_segment` from `len` returns `len` again, so `in_order` stays false either way. The guard only skips a search that cannot succeed; the side itself is taken by `search_tier_order_lost_at_the_second_token_stays_tier_3`.

Lint: `clang-analyzer-optin.core.EnumCastOutOfRange` flagged the deliberate out-of-range `StoreConflictReason` in the incomplete-conflict test; a per-line `NOLINTNEXTLINE` names it as the guard under test. clang-tidy also asked for `fseek` over `rewind`, named constants (`EURO_KEPT`, `CONFLICT_FIELDS`), and parentheses in `(EURO_COUNT * 3) + 1`. The conflict test was unrolled (no loop in a test body), and the euro string moved into `given_euros`.

Counts after stage 3 (macOS): `search_terms.c` 13 missed, `normalize.c` 2, `output.c` 107, `unicode.c` 0. A classifier over the remaining `output.c` sides (the condition at the side starts with `fputs`/`fprintf`/`fputc` or a writer call) leaves none outside the stream category. The `search_terms.c` and `normalize.c` remainders are exactly the stage 5 list in the stage 2 notes.

Process note: a stage 2 `ci-linux.sh` run failed clang-tidy on `search_terms.c:327` (`? -1 : -1`) and a missing final newline. Those were mutants: the mutation runner rewrote `src/` while the Docker run linted the same mounted tree. `git diff --quiet src/` was clean afterwards. Linux CI now runs only when no mutation is running.

Process note: `build/CMakeCache.txt` had `REMEMBER_TEST_VFS=OFF` after a reconfigure (ctest listed 4 tests, not 5). I did not find what wrote it; no script configures `build` itself. Reconfigured with `-DREMEMBER_TEST_VFS=ON`; ctest is 5/5 again.

Pre-existing, not this plan: compiling `store_sqlite.c` for the two hook targets (`remember_tests`, `remember_store_tests`) prints three `-Wallocator-wrappers` warnings (`real_malloc`, `real_calloc`, `real_realloc`, the store's allocation hook). Both compiles carry `-Werror` (checked in `build/compile_commands.json`) and still succeed, so clang 22 does not promote this warning. Relevant to stage 5: a new allocation hook written the same way gets the same warning.

Gate on stage 3: ctest 5/5 (VFS on), `lint-all.sh` LINT OK on macOS, `leaks-macos` PASS, `check-coverage.sh` exit 0. `ci-linux.sh` failed clang-tidy on `strcat`/`strcpy` in `test_output.c` (`clang-analyzer-security.insecureAPI.strcpy`, Linux only); fixed with `memcpy` in `given_euros` and one `snprintf`. `remember_store_tests` is green after the fix. Rerun on `c0f62fd` (2026-10-21, the uncommitted stage 4 shim moved out of the tree first): `lint-all.sh` LINT OK; `ci-linux.sh` exit 0 (5/5 suites, iwyu PASS, scan-build PASS).

### 2026-10-21 — stage 4 (failing stream)

No grill. Itay asked that the 11 Linux-only stream sides from stage 1 also be fixed; this stage covers them on both platforms with one deterministic stream.

Probe (plan step: `fmemopen` + `setvbuf(_IONBF)` first). Scratch `probe.c`: for capacity 1..7, `fputs("ab")`, `fputc('c')`, `fprintf("%d", 42)`, then `fputs("z")`:

- macOS: capacity N accepts exactly N bytes; partial writes land; after the first failure every later write fails (`cap 4: … after-error fputs=-1 buf=abc4`).
- glibc 2.39 (ubuntu 24.04): one byte goes to the NUL (`cap 6: buf=abc42`), and the error is not sticky: `cap 1: fputs=-1 fputc=99`, `cap 4: … after-error fputs=1`.

The platforms disagree, so per the plan the helper is `tests/failing_stream.c/.h`: `funopen` on macOS, `fopencookie` on glibc (`_GNU_SOURCE` with a reasoned `NOLINT`), `#error` elsewhere. Unbuffered. A write succeeds while the total stays within `limit`; the write that would pass it fails, and every later one fails too (sticky on both platforms by construction). `late_writes` counts write calls after the first failure. Probe of the helper itself (`shimprobe.c`, limits 0..6 over the same calls, and `late.c` for one failed call of each kind): the macOS and Linux outputs are byte-identical (`diff` empty), and neither libc retries inside a failed call (`late=0` for `fputs`, `fputc`, `fprintf`, and a mid-call failure).

One finding from the first test run: macOS stdio keeps the byte of a failed `fputc` and writes it again at `fclose` (`late_writes` 1 when read after `fclose`, 0 before). The driver reads `late_writes` before `fclose`; the contract is about the writer's own call.

Driver (`assert_reports_every_failed_write` in `tests/test_output.c`): run the writer unlimited (it must return 0 and write `len > 0` bytes), then once per limit `0 … len-1`: it must return -1 and make no write after the failed one. The second assertion matters: a sticky stream alone cannot tell "stopped at the failure" from "ignored it and failed on the next write".

**Bug fixed (fail fast).** `output_entry_human_line` ignored 10 write results (`(void)fputs`/`fputc` for the key, separators, tags, preview, `updated_at`, and the final newline), and `write_preview` returned `void`. A failed write there returned 0 to `cmd_query.c`, which already turns -1 into an error. TDD: `output_entry_human_line_reports_every_failed_write` went red first (`at byte limit 4 of 54`: the key write), and it was red again with `git stash -- src/output.c`. Now every write is checked; `write_text_or_nothing` (fputs that writes nothing for NULL) replaces the four `if (x != NULL) (void)fputs(x)` blocks, and `write_preview` returns its write result.

Tests: 25 `output_*_reports_every_failed_write`, one per public writer, plus a second variant where a writer has a NULL / non-NULL branch around a write (`related_envelope` with and without a key, `device_new_envelope` with and without `previous`, `rebuilt` JSON and human). Fixtures give every write at least one byte: two items wherever a comma is written, a keyed deleted link and a keyless live link (`given_two_links`), six links on the human line (the `, +1` overflow and a `[deleted]` mark), every JSON escape plus a C0 byte in `output_json_string`.

Removed, superseded: `output_write_fail_returns_error` and `open_write_fail` in `tests/test_coverage_edges.c`. It wrote 10,000 strings into `/dev/full` (Linux) or a closed pipe (macOS) until one failed, then called seven writers with `(void)` and asserted nothing about them. It was the source of the 11 platform-dependent sides. `<signal.h>` and `<unistd.h>` went with it (no other use in the file).

Mutations: every write check in `output.c`, generated from the source, one at a time: `) < 0` → `) < -1`, `) == EOF` → `) == EOF - 1`, `) != 0` → `) < -1`, `n < 0` → `n < -1`. Each makes that error ignored. 127 of 127 RED, each in the matching `*_reports_every_failed_write` test.

Counts after stage 4: macOS `output.c` 566 sides, 0 missed (566 = 548 + the new checks in `output_entry_human_line`); `search_terms.c` 13, `normalize.c` 2 (stage 5), `unicode.c` 0.

Lint fixes: `#if defined(__APPLE__)` → `#ifdef`, the ternary returns in the cookie writers → `if`, direct `<stdio.h>`/`<stdbool.h>`/`<stddef.h>` includes, `FAILING_STREAM_PAD` for the tail padding.
Linux clang-tidy (only Linux compiles the glibc branch) asked for a longer name than `io`: now `funcs`.

Gate on this tree (2026-10-21): macOS ctest 5/5 (VFS on), `leaks-macos` PASS, `lint-all.sh` LINT OK, `check-coverage.sh` exit 0. `ci-linux.sh` exit 0 (5/5, iwyu PASS, scan-build PASS) after the `funcs` rename. Linux coverage (`BUILD_DIR=build-cov-linux ./scripts/check-coverage.sh` in ubuntu 24.04, LLVM 18): `output.c` 566 sides, 0 missed; the other three TUs match macOS (13 / 2 / 0). The stage 1 platform difference is gone.


### 2026-10-21 — stage 5 (fault injection)

One grill before the first hook, answered with the recommended option: `decision:plan18b-shared-alloc-hook` (one allocation hook shared by the pure TUs, in a hooks-only TU).

**Allocation hook.** `src/test_alloc.c/.h`: `test_alloc_fail_after(n)` and `test_alloc_malloc(n)` over one process-global counter. `normalize.c` and `search_terms.c` include `test_alloc.h` last under `#ifdef REMEMBER_TEST_HOOKS`; the header defines `malloc(n)` as `test_alloc_malloc(n)` after it has included `<stdlib.h>`, so a later `<stdlib.h>` cannot expand the macro in its own declaration. `test_alloc.c` `#undef`s `malloc` because it is the hook. CMake adds `src/test_alloc.c` to `remember_tests` and `remember_store_tests` (both always define `REMEMBER_TEST_HOOKS`), and through `remember_apply_test_hooks` to `remember` / `remember_plain` when the option is on (coverage builds). `remember_core` and Release builds never compile it.

**Callee-status hook.** `unicode_test_fail_after(n)` in `unicode.c`: one counter across `unicode_decode`, `unicode_graphemes_open`, and `unicode_graphemes_next`. The (n+1)-th call returns `UNICODE_ERR_INTERNAL`. Each check sits in an `#ifdef REMEMBER_TEST_HOOKS` block after the function's own argument checks. The one-shot countdown is shared: `test_countdown_fires(int *counter)` in `test_alloc.c` serves both hooks (one copy of the logic; the store keeps its own, per the decision). `unicode_test_inject_nfc` was reused unchanged: it already reaches the NFC OOM and INTERNAL sides.

Tests (TDD: the three `normalize.c` tests went red, `actual=0, expected=5` (OK, not OOM), before the `malloc` redirect existed; `unicode_test_fail_after_fails_only_the_nth_call` failed to link before the hook existed):

| Side | Test |
|---|---|
| the countdown (fires on the Nth, one-shot, off after) | `test_alloc_fails_only_the_nth_allocation`, `unicode_test_fail_after_fails_only_the_nth_call` |
| `normalize.c` 261:9T, 338:9T | `fold_hebrew_marks_reports_oom`, `body_trim_copy_reports_oom` |
| `search_terms.c` 83:9T (`dup_bytes`) | `search_label_reports_oom_when_the_title_copy_fails` |
| `search_terms.c` 411:9T (`plain_label`) | `search_label_reports_oom_when_the_plain_text_buffer_fails` |
| `search_terms.c` 308:10F, 309:9T (the fold) | `search_tier_reports_oom_when_the_fold_fails` |
| `search_terms.c` 154:13T, 106:13T, 163:13T | `search_tokens_report_a_failed_decode_at_a_token_start`, `…_inside_a_token` |
| `search_terms.c` 222:13T, 256:13T, 265:17T, 313:9F | `search_tier_reports_a_failed_decode_in_the_segment_search`, `…_in_the_in_order_search` |
| `search_terms.c` 419:9T, 429:13T | `search_label_reports_a_failed_grapheme_open`, `…_grapheme_step` |

Each OOM test checks the out pointer stays NULL and the facet is left as it was. Leaks: `ctest` runs `store_asan_gate`; `ci-linux.sh` runs it with `detect_leaks=1`.

Counts after stage 5, macOS: all four TUs 0 missed (`search_terms.c` 176, `unicode.c` 80, `normalize.c` 172, `output.c` 566). `unicode.c` grew from 74 to 80 sides: the three hook checks, both sides taken.

Mutations (one clause each, RED with the named tests):

- Countdown: fire at 1 instead of 0, count down only above 1, no reset after firing (also breaks unrelated store tests, because a stuck counter fails every later allocation), the alloc hook ignored: `test_alloc_fails_only_the_nth_allocation`, `unicode_test_fail_after_fails_only_the_nth_call`, and the OOM tests.
- Each of the three `unicode.c` injection checks weakened (`&& len > 1000000U`): the grapheme open / step tests, the decode tests.
- Each OOM status (`dup_bytes`, `plain_label`, the fold ternary, the two `normalize.c` mallocs) returned as INTERNAL: the matching OOM test.
- Every `if (st != SEARCH_OK)` in `search_terms.c` (13 sites), first mutated to swallow INTERNAL: 7 RED. The other 6 never see INTERNAL in a test (they see OOM or invalid UTF-8), so they were mutated to swallow everything except INTERNAL: 5 RED (`search_tokens_invalid_utf8_is_rejected`, `search_tier_invalid_utf8_text_is_rejected`, `search_tier_reports_oom_when_the_fold_fails`, `search_label_invalid_utf8_plain_text_is_rejected` ×2).
- **Equivalent, kept**: the `unicode_graphemes_open` status check in `plain_label` (L425) survives both mutants. If it is skipped, `unicode_graphemes_next` gets an iterator whose `bytes` is NULL and returns INTERNAL itself, which is the same status. The check stays at the call that failed (fail fast); `search_label_reports_a_failed_grapheme_open` takes its side.

Release (round 9.2, handover "nm on a Release remember"): `cmake --build build-release --target remember` (Release, hooks off), `nm build-release/remember | grep -ciE 'test_alloc|test_countdown|unicode_test|failing_stream'` = 0. Positive control: `nm build-mut/remember_store_tests` lists `_test_alloc_fail_after`, `_test_countdown_fires`, `_unicode_test_fail_after`.

New dependency (round 9.2): `src/test_alloc.c` is a source every hooks target must compile. Consumers: CMake (above); `scripts/lint-all.sh` lints it with the other `src/*.c` (no change needed, it globs); `test_no_shim.sh` checks only the VFS shim, so the Release `nm` check above is the proof for the new symbols. No consumer outside the build.

Lint: clang-tidy rejected `(malloc)(n)` as redundant parentheses (now `#undef malloc`); cppcheck called a non-hook `injected_failure()` stub "always false" (the stub is gone; the call sites are `#ifdef`'d). Apple clang 21 warns `-Wallocator-wrappers` on `test_alloc_malloc`, which is not promoted under `-Werror`. `__attribute__((malloc))`, `alloc_size(1)`, and `ownership_returns(malloc)` each still warn (scratch copy of the file). A `#pragma clang diagnostic ignored` under `#ifdef __clang__` with the reason silences it; the extra `-Wunknown-warning-option` ignore is for clang 18 on Linux, which does not know the warning. The store's three identical warnings (`real_malloc`, …) predate this plan and are untouched.
Linux IWYU asked to drop `<stddef.h>` from `test_alloc.h` and `test_alloc.c` (`size_t` comes from the kept `<stdlib.h>`); done.

Gate on this tree (2026-10-21): macOS ctest 5/5 (VFS on), `leaks-macos` PASS, `lint-all.sh` LINT OK, `check-coverage.sh` exit 0 with all four TUs at 0 missed. `ci-linux.sh` exit 0 (5/5, iwyu PASS, scan-build PASS). Linux coverage (ubuntu 24.04, LLVM 18): all four TUs 0 missed, and the build printed no warnings.


## Review Notes
