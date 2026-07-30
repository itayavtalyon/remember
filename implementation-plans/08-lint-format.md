# 08 — Lint + format toolchain

## Goal

Make strict C quality mechanical: format target, lint script, CI-local gate.

## Deliverables

| Artifact | Role |
|----------|------|
| `.clang-format` | Project style |
| `.clang-tidy` | Checks for `src/**` |
| `scripts/lint-all.sh` | cppcheck, clang-tidy, scan-build (skip third_party) |
| CMake `format` / `lint` targets | `cmake --build build --target lint` |

## Tooling install (macOS)

```bash
brew install cppcheck llvm  # clang-tidy, scan-build via llvm
# optional: brew install splint
# ensure $(brew --prefix llvm)/bin on PATH for clang-tidy
```

Amalgamation: compile with relaxed warnings; **exclude from tidy**.

## Tests

No suite tests. **Gate:** lint exit 0 on `src/`.

## More tests?

None. Optionally a `scripts/check-no-sqlite-leak.sh` grepping `#include.*sqlite` outside adapter — **recommended**.

## Done checklist

- [x] `format` runs
- [x] `lint` clean on `src/`
- [x] Document in README

## Review notes (2026-07-29)

### Implementation

- Tooling already existed from earlier steps (`.clang-format`, `.clang-tidy`, `scripts/lint-all.sh`, CMake `lint`, CI).
- **Fix:** CMake always defines `format` (clear error if tool missing). On Apple, prepends `$(brew --prefix llvm)/bin` to `CMAKE_PROGRAM_PATH` so `clang-format` is found without manual PATH at configure time.
- SQLite/sha256 boundary greps stay **inside** `lint-all.sh` (no separate `check-no-sqlite-leak.sh` — YAGNI; same exit path).
- README documents format/lint and brew llvm PATH.

### Pragmatic write-gate

- **DRY:** one lint entry (`lint-all.sh`); CMake `lint` delegates. Cleared.
- **Orthogonal:** format tool discovery ≠ CI lint env (script still prepends brew PATH at runtime). Cleared.
- **Easy-to-replace:** swap clang-format binary via PATH/`find_program`; no hard-coded brew path in the format command itself. Cleared.

## Review notes (2026-07-29) — final v1 review (steps 08–10)

### Verdict for step 08 alone

**Approve** (tooling). Format always defined; brew LLVM discovery; lint gate green (`format` dry-run, cppcheck, clang-tidy, sqlite/sha256 boundaries).

### Step-08-specific findings

- None blocking. CMake `format` missing-tool path fails clearly instead of omitting the target — good.
- **Nit:** `find_program(SCAN_BUILD)` benefits from the same `CMAKE_PROGRAM_PATH` prepend (order is correct today); keep brew discovery in one place if more tools are added.
