# Post-step review checklist (remember)

Run this after each `implementation-plans/NN-*.md` step is “done” (required tests green).

## 1. Scope

- [ ] Diff limited to the step (+ unavoidable fixes)
- [ ] Plan’s **must-pass tests** are green; no unexpected new greens without understanding why

## 2. C safety

- [ ] No ignored return values that matter; allocations checked
- [ ] No UB (signed overflow, bad pointers, non-terminated strings)
- [ ] `const` on inputs where possible
- [ ] Errors reported to the user; no assert-crash on bad CLI input

## 3. Architecture / pragmatic

- [ ] **SQLite** only in the store adapter
- [ ] No new dual sources of truth (one place for messages, one for command names, etc.)
- [ ] New commands/options extend tables/registry rather than copy-paste chains
- [ ] Cognitive load: no clever index mutation; clear ownership of strings/buffers

## 4. SOLID / clean code (pragmatic, not cargo-cult)

- [ ] SRP: modules have one job
- [ ] DRY: no parallel encodings of the same vocabulary
- [ ] YAGNI: no speculative abstractions without a second use
- [ ] Error model is uniform and documented

## 5. Tooling

- [ ] `cmake --build build` clean with `-Werror`
- [ ] `PATH="$(brew --prefix llvm)/bin:$PATH" ./scripts/lint-all.sh` → LINT OK
- [ ] Sanitizers clean on the CLI binary for the exercised paths

## 6. Capture

- [ ] Update `docs/engineering-notes.md` if a rule emerged
- [ ] Append “Review notes” under the step plan
- [ ] Open follow-ups listed explicitly (not silent)

## Severity labels (for written review)

| Tag | Meaning |
|-----|---------|
| **blocking** | Must fix before next step |
| **important** | Fix soon; OK to defer with a note |
| **nit** | Style / optional |
| **praise** | Keep this pattern |
