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

- [ ] `format` runs
- [ ] `lint` clean on `src/`
- [ ] Document in README
