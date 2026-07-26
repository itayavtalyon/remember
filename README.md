# remember

Local CLI personal second brain (SQLite + FTS5).  
Design: [`design-logs/001-foundations.md`](design-logs/001-foundations.md).  
Plans: [`implementation-plans/INDEX.md`](implementation-plans/INDEX.md).

## Status

Step **01** (meta CLI) and **02** (store port + SQLite schema) done. Remaining command steps still red under TDD.

Pinned SQLite amalgamation: **3.53.3** in `third_party/sqlite/` (see that README).

## Architecture (pragmatic)

Only **`store_sqlite.c`** may include SQLite. All other code uses **`store.h`** (port). CLI → commands → store → SQLite adapter.

Engineering rules and post-step review: [`docs/engineering-notes.md`](docs/engineering-notes.md), [`docs/STEP_REVIEW_CHECKLIST.md`](docs/STEP_REVIEW_CHECKLIST.md).

## Build & test

```bash
cmake -S . -B build -DCMAKE_C_COMPILER=clang \
  -DREMEMBER_ENABLE_SANITIZERS=ON
cmake --build build
ctest --test-dir build --output-on-failure   # step_gate + store_asan_gate

GATE=$(./scripts/read-gate-suites.sh)        # grows via tests/gate-suites
./build/remember_tests ./build/remember --only "$GATE"
./build/remember_store_tests   # store port under ASan

# Full Linux CI locally (Docker one-liner — same as GHA linux job)
./scripts/ci-linux.sh

# Auto-run that check on git push (once per clone)
./scripts/install-hooks.sh
# Skip: SKIP_LINUX_CI=1 git push  |  git push --no-verify

# Production line coverage (src/*.c; target 100%)
PATH="$(brew --prefix llvm)/bin:$PATH" ./scripts/check-coverage.sh
```

Quality matrix (sanitizers, LSan on Linux CI, scan-build, IWYU, macOS `leaks`, coverage):  
[`docs/QUALITY.md`](docs/QUALITY.md). CI: [`.github/workflows/ci.yml`](.github/workflows/ci.yml).

## Format & lint

```bash
export PATH="$(brew --prefix llvm)/bin:$PATH"   # clang-tidy, clang-format, scan-build
brew install cppcheck llvm                      # once
cmake --build build --target format
cmake --build build --target lint
# Optional heavy gates:
REMEMBER_SCAN_BUILD=1 ./scripts/lint-all.sh
cmake --build build --target leaks-macos        # macOS only; uses remember_plain
```

Coverage: [`tests/COVERAGE.md`](tests/COVERAGE.md).
