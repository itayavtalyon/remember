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
| clang-format / clang-tidy / cppcheck | `scripts/lint-all.sh` | required | required |
| misc-include-cleaner (IWYU-style) | via clang-tidy | via clang-tidy | via clang-tidy |
| include-what-you-use | if installed / `REMEMBER_IWYU=1` | if package present | optional |

**Vendor includes for IWYU/cppcheck:** tools that scan `src/*.c` must be told about every `third_party/<lib>` header directory that project code includes (not only via CMake `PUBLIC` link lines). When vendoring a new library, update `IWYU_VENDOR_INCLUDES` / cppcheck `-I` in `scripts/lint-all.sh` in the same change as the CMake target. Full checklist: `docs/engineering-notes.md` → *Vendoring third-party C*.
| scan-build | `REMEMBER_SCAN_BUILD=1` or `cmake --build --target analyze` | **required** | **required** |
| macOS `leaks(1)` on **`remember_plain`** | `cmake --build build --target leaks-macos` | n/a | **required** |
| **CodeQL** (Code Scanning) | n/a | `.github/workflows/codeql.yml` | n/a |
| Valgrind | **not used** | **not used** | **not used** |

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
| Raw line % | ~87% today — remaining zeros are pure defensive exits (`return -1`, `goto fail`, error `err_msg`, etc.) excluded from the *effective* denominator |
| What it protects | Untested production *logic* (every function entered; non-defensive lines hit) |
| What `gate-suites` protects | Dropping whole green *suites* from CI |

**Effective lines:** pure error-return / teardown / parser-kind exits need exponential fail-at-each-check injection to hit every sequential `if (write) return -1`. The gate still fails on any uncovered *logic* line. See the `DEFENSIVE` regex in `scripts/check-coverage.sh`.

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

## Design rules that prevent leaks

See `docs/engineering-notes.md`: **`goto cleanup`** single-exit teardown is mandatory for multi-resource functions.
