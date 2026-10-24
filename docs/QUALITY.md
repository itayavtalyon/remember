# Quality gates (`remember`)

How we keep C code safe: compile hard, analyze statically, run sanitizers, detect leaks.

## What runs where

| Gate | Local (macOS) | GitHub Actions Linux | GitHub Actions macOS |
|------|---------------|----------------------|----------------------|
| Layer A warnings (`-Werror`, …) | always | always | always |
| ASan + UBSan on `remember` | Debug | yes | yes |
| ASan + UBSan on `remember_store_tests` | Debug | yes | yes |
| **LSan** (`detect_leaks=1`) | off (unreliable) | **on** | off |
| Black-box `step_gate` (from `tests/gate-suites`) | yes | yes | yes |
| Line coverage `src/*.c` (functions 100% + effective lines 100%) | `check-coverage.sh` | GHA job | optional |
| Branch coverage of the pure TUs (every side, no exclusion) | `check-coverage.sh` | GHA job | optional |
| clang-format / clang-tidy / cppcheck | `scripts/lint-all.sh` | required | required |
| misc-include-cleaner (IWYU-style) | via clang-tidy | via clang-tidy | via clang-tidy |
| include-what-you-use | if installed / `REMEMBER_IWYU=1` | if package present | optional |
| scan-build | `REMEMBER_SCAN_BUILD=1` or `cmake --build --target analyze` | **required** | **required** |
| macOS `leaks(1)` on **`remember_plain`** | `cmake --build build --target leaks-macos` | n/a | **required** |
| **CodeQL** (Code Scanning) | n/a | `.github/workflows/codeql.yml` | n/a |
| Valgrind | **not used** | **not used** | **not used** |

**Vendor includes for IWYU/cppcheck:** tools that scan `src/*.c` must be told about every `third_party/<lib>` header directory that project code includes (not only via CMake `PUBLIC` link lines). When vendoring a new library, update `IWYU_VENDOR_INCLUDES` / cppcheck `-I` in `scripts/lint-all.sh` in the same change as the CMake target. Full checklist: `docs/engineering-notes.md` → *Vendoring third-party C*.

### Vendored C (`third_party/`)

| Library | Build | Consumer | Sanitizer gate |
|---------|-------|----------|----------------|
| SQLite amalgamation | static lib `sqlite3`, `-w` | `src/store_sqlite.c` (and the test-only VFS shim `src/test_vfs.c`) | none: trusted, upstream has intentional suppressed UB |
| sha256 | static lib `sha256`, `-w` | `src/normalize.c` only | also compiled into `remember_store_tests` under ASan/UBSan; NIST vectors in `test_normalize.c` |
| utf8proc 2.12.0 (Unicode 18) | static lib `utf8proc`, `-w` | `src/unicode.c` only (plan 18, D2) | also compiled into `remember_store_tests` under ASan/UBSan; NFC, grapheme and category vectors in `test_unicode.c` |

`scripts/lint-all.sh` fails when any other file under `src/` names a vendored header or symbol (the sqlite, sha256 and utf8proc boundaries; `check_boundary`).

### Code Scanning vs our `ci` workflow

| | **ci.yml** | **codeql.yml** |
|--|------------|----------------|
| Purpose | Build, tests, sanitizers, lint, scan-build | GitHub **Code Scanning** alerts |
| Ruleset “status checks” | Job names like `linux (…)` | Separate |
| Ruleset “Wait for Code Scanning results” | No | **Yes** — needs this workflow (or turn that rule off) |

If merge is blocked with *“Waiting for Code Scanning results”*:

1. Prefer: keep `codeql.yml` (this repo) and wait for the **codeql** workflow to finish once on the PR/default branch, **or**
2. **Settings → Rules →** your `main` ruleset → disable **Require code scanning results** (fine if you rely on `ci` + scan-build only).

Also ensure **Settings → Code security → Code scanning** is not disabled for the repo.

## Green test gate (grows with the code)

| File / target | Role |
|---------------|------|
| **`tests/gate-suites`** | Single source of truth: `--only` suites for the implemented green surface |
| **`ctest` `step_gate`** | Runs that list (alias `step05_gate` kept for docs) |
| **`scripts/ci-linux.sh`** | Linux CI one-liner (Docker) — same install/build/ctest/lint as GHA |
| **`.github/workflows/ci.yml`** | Calls `ci-linux.sh --inner --install` (no stale hard-coded suite list) |

When a step lands more green suites, **edit `tests/gate-suites` only** — CMake and CI pick it up.

```bash
# Linux CI locally (Docker method A — one-liner; start Docker Desktop first)
./scripts/ci-linux.sh

# Run the same check automatically before every git push (once per clone):
./scripts/install-hooks.sh
# Skip one push: SKIP_LINUX_CI=1 git push   or   git push --no-verify
```

> **Toolchain skew — a green local `lint-all.sh` is not proof CI is green.**
> CI runs *older* tools than a typical macOS dev box (Ubuntu 24.04: clang /
> clang-tidy / clang-format 18, cppcheck 2.13; macOS Homebrew: clang 22,
> cppcheck 2.21). The newer tools are more lenient and silently tolerate things
> the older ones reject — a `-Wno-<name>` for a warning that only exists in a
> newer clang, a magic number clang-tidy 18 flags but 22 does not, a cppcheck
> check that exists in only one version. So **before pushing any change to the C
> sources or the lint config, run `./scripts/ci-linux.sh`** (or install the
> pre-push hook above); it reproduces CI's exact toolchain in Docker and is the
> real gate. Match CI's flags in hand-rolled gates too (e.g. pass the project's
> POSIX feature macros to any standalone compiler invocation).

## Targets

```bash
cmake -S . -B build -DCMAKE_C_COMPILER=clang \
  -DREMEMBER_ENABLE_SANITIZERS=ON \
  -DREMEMBER_ENABLE_LSAN=OFF          # ON on Linux CI only

cmake --build build
ctest --test-dir build --output-on-failure   # step_gate + store_asan_gate

GATE=$(./scripts/read-gate-suites.sh)
./build/remember_tests ./build/remember --only "$GATE"
./build/remember_store_tests          # store port under ASan

PATH="$(brew --prefix llvm)/bin:$PATH" ./scripts/lint-all.sh
REMEMBER_SCAN_BUILD=1 ./scripts/lint-all.sh   # full analyzer (slow)
cmake --build build --target analyze          # same idea via CMake
cmake --build build --target leaks-macos      # macOS only
```

## Line coverage (src/*.c)

```bash
PATH="$(brew --prefix llvm)/bin:$PATH" ./scripts/check-coverage.sh
```

| Fact | Detail |
|------|--------|
| What is measured | `src/*.c` only (not `third_party/`, not `tests/`) |
| What runs | Green gate + `remember_store_tests` (+ fault-injection hooks under `REMEMBER_TEST_HOOKS`) |
| Pass bar | **100% function coverage** and **100% effective line coverage** |
| Raw line % | 85.88% after plan 18 (2026-10-24, macOS) — remaining zeros are pure defensive exits (`return -1`, `goto fail`, error `err_msg`, etc.) excluded from the *effective* denominator |
| What it protects | Untested production *logic* (every function entered; non-defensive lines hit) |
| What `gate-suites` protects | Dropping whole green *suites* from CI |

**Effective lines:** pure error-return / teardown / parser-kind exits need exponential fail-at-each-check injection to hit every sequential `if (write) return -1`. The gate still fails on any uncovered *logic* line. See the `DEFENSIVE` regex in `scripts/check-coverage.sh`.

### Branch coverage of the pure TUs (plan 18b)

`check-coverage.sh` also fails when any branch side in `src/search_terms.c`, `src/unicode.c`, `src/normalize.c`, or `src/output.c` is not taken. It reads `llvm-cov export -format=text` (`build-cov/coverage-branches.json`) and prints each missed side as `file:line:col true|false`; a branch inside a macro is counted and tagged `(macro)` at its `#define` (`decision:plan18b-macro-branches-counted`).

| Rule | Detail |
|------|--------|
| No excuses | No inline marker, no skip-list, no regex (`decision:plan18b-no-branch-excuses`). The line regex above does not apply to these four files' branches. |
| A side that cannot run | Remove or restructure it, and argue the removal in the plan notes. |
| A side reached only by failure | Fault injection under `REMEMBER_TEST_HOOKS`: `test_alloc_fail_after(n)` (`src/test_alloc.c`, the allocation hook the pure TUs share) and `unicode_test_fail_after(n)` (the Nth decode / grapheme call fails). Release builds compile neither. |
| A stream write | `tests/failing_stream.c` (`funopen` / `fopencookie`): writes fail past a byte limit. Drive a writer at every limit; it must return -1 and make no later write. |
| Comparators and callbacks | Test the function directly in each argument order; the caller (`qsort`) need not reach every branch. |
| No `assert` | An `assert` is a branch whose failing side never runs, so these four files have none; a removed guard is not replaced by one (`decision:plan18b-no-assert-pure-tus`). |
| List equals report | Per file, the listed sides must equal `llvm-cov report`'s count, or the script fails: a branch in a macro `#define`d in a header counts in the report but sits in the header's record (`decision:plan18b-branch-list-equals-report`). |

Why: in plan 18 stage 2, `search_tag_order`'s exact-first `return -1` never ran, and the line regex (which excludes `return -1;`) still read 100%.

## Portability (Linux glibc + strict C11)

We build with `-std=c11` and `CMAKE_C_EXTENSIONS=OFF` (no `gnu11`). On glibc that
**hides** POSIX prototypes (`strdup`, `mkdtemp`, `popen`, …) and often `PATH_MAX`
unless feature-test macros are set **before** system headers.

CMake therefore defines for all project TUs:

- `_POSIX_C_SOURCE=200809L`
- `_XOPEN_SOURCE=700`

Plus a `#ifndef PATH_MAX` → `4096` fallback in `store_sqlite.c` / harness.

macOS is more permissive; these defines are still safe there. CI on Ubuntu is what
catches “works on my Mac” gaps.

## Layer A (compiler)

Project TUs (not amalgamation) use warnings-as-errors plus:

`-Wall -Wextra -Wshadow -Wconversion -Wsign-conversion -Wformat=2 -Wformat-security`
`-Wmissing-prototypes -Wstrict-prototypes -Wmissing-declarations`
`-Wnull-dereference -Wdouble-promotion -Wwrite-strings -Wpointer-arith -Wundef`
`-Wempty-body -Wredundant-decls -Wstrict-overflow=2 -pedantic`
`-Wcast-qual` on production `src/*.c` only (tests use `execv` const→char\* argv tables)

## Sanitizers & leaks

- **`remember`** and **`remember_store_tests`**: ASan + UBSan in Debug.
- **`remember_tests`**: not instrumented (fork/exec parent + ASan child deadlocks).
- **Linux CI**: `REMEMBER_ENABLE_LSAN=ON` → `ASAN_OPTIONS=detect_leaks=1` on store unit + env for the job.
- **macOS**: use `leaks --atExit` smoke on meta CLI paths; LSan off.

## Test-only VFS shim

`REMEMBER_TEST_VFS` (default OFF) compiles `src/test_vfs.c` into the `remember` CLI only. A Release configure forces the option OFF. `remember_plain`, `remember_core`, and an installed binary never define `remember_test_vfs_install`. `scripts/install.sh` refuses a binary that defines it, and ctest `no_shim_symbols` checks the same split. The shim wraps the `unix` VFS and fails `xRead` / `xWrite` when `REMEMBER_TEST_ENOSPC`, `REMEMBER_TEST_FAIL_WRITE`, `REMEMBER_TEST_FAIL_READ`, or `REMEMBER_TEST_NOMEM` is set. It exists so the SQLite cause tests can run. It is not part of a release.

## Design rules that prevent leaks

See `docs/engineering-notes.md`: **`goto cleanup`** single-exit teardown is mandatory for multi-resource functions.
