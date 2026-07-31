# 11 - Core façade library (`remember_run`) + `tags`

Design: [`design-logs/002-core-facade.md`](../design-logs/002-core-facade.md).
Goal: let the native GUI (separate `remember-mac` repo) reuse the CLI brain
in-process with **byte-identical** `--json` output, and add a `tags` command.

## Scope

1. Injectable `out`/`err` streams (process-global pair, ponytail marker).
2. Extract `remember_run(argc, argv, out, err)` into `remember_app.{c,h}`;
   `main.c` becomes a thin caller.
3. New `tags`: `store_tags` + `output_tags_envelope` + `cmd_tags` + CLI wiring.
4. `libremember` static lib target (core without `main`).

Do it in that order; keep each sub-step green before the next.

## Must-pass tests

**Behavior unchanged (guardrail):** the entire existing suite stays green — it is
the proof the stream refactor and `run()` extraction did not change CLI behavior.
Run the full gate (`tests/gate-suites`) + `store_asan_gate` under ASan/UBSan.

**New `test_facade.c` (added to the black-box target):**
- For each command (`add`, `get`, `list`, `search`, `update`, `delete`, `tags`),
  a case that calls `remember_run` with `open_memstream` buffers and asserts the
  captured stdout **byte-equals** the CLI's `--json` stdout for the same argv
  (run the real `remember` binary via the harness, diff the bytes).
- Edge cases mirrored from existing suites: keyless body-hash **merge**, keyed
  **upsert**, `get`/`delete`/`update` **not-found → exit 2**, invalid FTS →
  exit 1 + "invalid search query", empty page still reports unpaged `total`,
  `update` keyless body-hash **conflict**.
- Exit-code parity: `remember_run` return value == CLI process exit for the same
  argv (0/1/2).

**New `tags` tests (`test_tags.c` or folded into `test_get_list_delete`-style):**
- Empty DB → `{"version":1,"count":0,"tags":[]}`.
- Distinct names with counts, sorted; tags shared across entries counted once
  per entry; deleting an entry drops/decrements as expected (orphan-tag GC
  already exists — assert `tags` reflects it).
- Human output shape (name + count per line) neutralizes controls like other
  human output (tags are `normalize_token` output → no controls; assert format).

**Store unit (`test_store.c`, under ASan/UBSan):**
- `store_tags` over a fixture: counts correct, ordering stable, OOM path via
  `REMEMBER_TEST_HOOKS` (prepare/step/alloc fault) returns the right status and
  leaks nothing.

## Quality gate (every sub-step — from `pref:code-review-workflow`)

- `-Werror -Wconversion` clean; new TUs added to the `-Wcast-qual` list.
- ASan/UBSan green on `remember_store_tests`; full black-box gate green.
- `cmake --build build --target lint` clean (IWYU/cppcheck/clang-tidy);
  `misc-include-cleaner` → every new `.c` directly `#include`s what it uses.
- `scripts/ci-linux.sh` passes (Linux parity).
- 100% function + effective-line coverage (`scripts/check-coverage.sh`).
- Verify by **running** edge cases, not reading (`lesson:review-run-edge-cases`);
  e.g. same-second add/add/update ordering; empty-DB `tags`.
- Strict multi-lens review (comprehensive + ponytail + DRY) before Done.

## Done checklist

- [x] Full existing suite green (behavior unchanged) + new façade/tags/store tests green. (314 black-box + 16 new; ASan store gate 55)
- [x] `remember_run` output byte-equals CLI `--json` for all commands (asserted in `test_facade.c`).
- [x] `remember tags` works on the CLI (human + `--json`); skill/help updated.
- [x] `libremember` (`remember_core`) links without `main`; only `store_sqlite.c` includes sqlite.
- [x] Lint + format clean; coverage 100% funcs + 100% effective lines; `ci-linux.sh` green.
- [x] Engineering notes updated (façade contract, stream-injection rule, tags op).
- [x] Design log 002 Implementation Notes + Review Notes filled in.
- [x] Reusable learnings persisted to Remember.

## Implementation Notes

- Order followed: streams (`appio`) → `remember_run` extraction (`remember_app`,
  thin `main.c`) → `tags` (`store_tags` + `output_tags_*` + `cmd_tags` + CLI wiring)
  → `remember_core` static lib. Each kept green before the next.
- `remember_main` added so `main.c` carries no `<stdio.h>` (portable across the two
  clang-tidy versions). See design log 002 Implementation Notes for the details and
  resolved open questions.
- Files: `src/appio.{c,h}`, `src/remember_app.{c,h}`, `src/cmd_tags.c` (new);
  `store.{h,c}` (+`store_tags`/`TagCount`), `output.{h,c}` (+tags), `cli.{h,c}`,
  `commands.h`, `commands_common.c`, `cmd_{add,locator,query}.c` (stream swap),
  `main.c` (thin), CMake (lib sources + cast-qual + `remember_core` + test TUs),
  `tests/test_facade.c`, `tests/test_tags.c`, `tests/test_store.c` (store_tags),
  `tests/gate-suites`.

## Review Notes

See design log 002 Review Notes. All blocking/important findings from the
self-review (include-cleaner, analyzer null-deref, two coverage gaps) fixed and
re-verified green on macOS + Linux.
