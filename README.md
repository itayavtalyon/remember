# remember

Local CLI personal second brain (SQLite + FTS5).  
Design: [`design-logs/001-foundations.md`](design-logs/001-foundations.md).  
Plans: [`implementation-plans/INDEX.md`](implementation-plans/INDEX.md).

## Status

Skeleton + **failing** suite (TDD red): **96 tests, 0 pass**. Implementation follows the step plans.

## Architecture (pragmatic)

Only **`store_sqlite.c`** may include SQLite. All other code uses **`store.h`** (port). CLI → commands → store → SQLite adapter.

## Build & test

```bash
cmake -S . -B build -DCMAKE_C_COMPILER=clang
cmake --build build
./build/remember_tests ./build/remember
```

## Format & lint

```bash
export PATH="$(brew --prefix llvm)/bin:$PATH"   # clang-tidy, clang-format, scan-build
brew install cppcheck llvm                      # once
cmake --build build --target format
cmake --build build --target lint
```

Coverage: [`tests/COVERAGE.md`](tests/COVERAGE.md).
