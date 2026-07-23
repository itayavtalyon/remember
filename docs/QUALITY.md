# Quality gates (`remember`)

How we keep C code safe: compile hard, analyze statically, run sanitizers, detect leaks.

## What runs where

| Gate | Local (macOS) | GitHub Actions Linux | GitHub Actions macOS |
|------|---------------|----------------------|----------------------|
| Layer A warnings (`-Werror`, …) | always | always | always |
| ASan + UBSan on `remember` | Debug | yes | yes |
| ASan + UBSan on `remember_store_tests` | Debug | yes | yes |
| **LSan** (`detect_leaks=1`) | off (unreliable) | **on** | off |
| Black-box `remember_tests` (no ASan parent) | yes | yes | yes |
| clang-format / clang-tidy / cppcheck | `scripts/lint-all.sh` | required | required |
| misc-include-cleaner (IWYU-style) | via clang-tidy | via clang-tidy | via clang-tidy |
| include-what-you-use | if installed / `REMEMBER_IWYU=1` | if package present | optional |
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

## Targets

```bash
cmake -S . -B build -DCMAKE_C_COMPILER=clang \
  -DREMEMBER_ENABLE_SANITIZERS=ON \
  -DREMEMBER_ENABLE_LSAN=OFF          # ON on Linux CI only

cmake --build build
ctest --test-dir build --output-on-failure

./build/remember_store_tests          # store port under ASan
./build/remember_tests ./build/remember --only cli_global,store

PATH="$(brew --prefix llvm)/bin:$PATH" ./scripts/lint-all.sh
REMEMBER_SCAN_BUILD=1 ./scripts/lint-all.sh   # full analyzer (slow)
cmake --build build --target analyze          # same idea via CMake
cmake --build build --target leaks-macos      # macOS only
```

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
