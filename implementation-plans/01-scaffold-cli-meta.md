# 01 — Scaffold + meta CLI

## Goal

Ship a real argv front-door: version, help, unknown/missing subcommand, global flags shell. Still no persistence beyond optional path resolution stubs.

## Deliverables

| File | Role |
|------|------|
| `src/main.c` | Wire: parse → dispatch (stubs may return not-implemented for real cmds) |
| `src/cli.c` / `cli.h` | Parse argv into `CliArgs` / `Command` enum — **no I/O, no SQL** |
| `src/util.c` / `util.h` | `xstrdup`, path join, exit helpers if needed |
| `src/exit_codes.h` | `REMEMBER_OK=0`, `REMEMBER_ERR=1`, `REMEMBER_NOT_FOUND=2` |
| CMake | Link only what exists; keep tests harness |

## Behavior

- `remember --version` → exit 0, non-empty version on stdout (project version).
- `remember --help` / `remember help` → exit 0, mentions `add`, `search`, `update`, `get`, `list`, `delete`.
- No subcommand / unknown subcommand → exit 1, message on stderr.
- Global: `--db PATH`, `--json` recognized when parsing (may not fully apply until later steps).
- Subcommands may still fail until later steps **except** the meta cases above.

## Pragmatic notes

- **Orthogonal:** `cli.c` only parses; does not open DBs.
- **DRY:** single option table / getopt_long loop shared by all subcommands.
- **Replaceable:** parsing returns a pure struct; commands consume it.

## Tests required this step

| Test | Must pass |
|------|-----------|
| `help_exits_zero_and_prints_usage` | ✓ |
| `version_exits_zero` | ✓ |
| `no_subcommand_exits_usage` | ✓ |
| `unknown_subcommand_exits_usage` | ✓ |

**More tests?** Optional: `remember help` alias. Not required.

## Done checklist

- [x] Meta tests green (expanded parser suite)
- [x] Formatted + compiles with `-Werror`
- [x] No sqlite includes
- [x] Post-review refactor: void parse, CliError, ArgCursor, command table (linear lookup), subcommand help, ASCII help, printing in main, private NYI

## Review notes (2026-07-22)

Strict review of step-01 code drove a refactor (not just nits):

- **Parse errors:** `cli_parse` is `void`; `CliError` + `cli_error_message()` + `error_arg`/`error_option` are the source of truth.
- **ArgCursor** owns walking argv (no `int *ip` mutation).
- **Globals** (`--db`/`--json`) are recognized anywhere and stripped out; `cli_parse` fully owns them, so subcommand parsers never see a global in `rest_argv`.
- **`rest_argv` is a clean, heap-owned list** of the subcommand's own tokens (release via `cli_args_free`). Elements alias argv.
- **End-of-options `--`:** everything after `--` is literal (flows into `rest_argv` verbatim, even flags/globals) — standard POSIX behavior, needed for bodies beginning with `-`.
- **Command table** with a linear lookup over eight entries; the earlier open-addressing hash map was removed as over-engineering (more code + mutable globals, slower at n=8).
- **Subcommand help:** `add --help`, `help add`.
- **Printing** only in `main.c`; **REMEMBER_NYI** private to main.
- **bool** for booleans; **const** argv; ASCII strings; `default` in switches.
- **NONE vs UNKNOWN:** none = missing token; unknown = bad token + `error_arg`. No abort on bad input.
- Lasting rules: `docs/engineering-notes.md`, checklist `docs/STEP_REVIEW_CHECKLIST.md`, skill `remember-step-review`.

### Second review pass (2026-07-22) — resolved

- **`rest_argv` is clean:** globals stripped wherever they appear; array heap-owned (`cli_args_free`). Subcommand parsers in later steps consume only their own tokens — no global re-scanning needed.
- **Hash map removed** (linear scan); no file-scope mutable lookup state.
- **`--` end-of-options** handled in the single scan pass.
- **Harness:** `drain_two` (poll) replaces sequential pipe reads → no large-stderr deadlock; temp DBs removed at exit via an atexit registry.
- **ctest gate:** `scaffold_gate` runs only implemented suites (`remember_tests <bin> --only cli_global`) and must stay green; **extend the `--only` list as each step lands** so regressions in finished work aren't masked by WIP failures.
- **Lint:** `clang-format` covers `src` + `tests`; the sqlite-boundary guard targets `db.c` (matches the design's module layout); `cert-err33-c` re-enabled (we `(void)`-cast ignored returns).

### Test code held to the same clang-tidy bar (2026-07-22)

`lint-all.sh` now runs clang-tidy over `tests/*.c` too, and `HeaderFilterRegex` includes `tests/`. Getting there:

- **Framework refactored** so asserts/`RUN_TEST` are thin macros over functions in `tests/test.c` (not `if`/`printf` macros). This removed ~538 `cert-err33-c` and ~74 `cognitive-complexity` findings *structurally* (test bodies are now branch-free), rather than by suppression.
- **`run_remember` decomposed** into `harness_error`/`build_child_argv`/`child_run`/`write_all`/`wait_status` (was one 49-complexity function).
- **Real bugs the analyzer caught in test code, now fixed:** null-deref paths after non-aborting `ASSERT_TRUE(ptr != NULL)` (guarded), a CERT ENV31-C `getenv`-then-`setenv` invalidation (copy first), `mkdtemp(NULL)`/`strlen(NULL)` paths, and a raw `system()` replaced by the existing query helper.
- **Two checks disabled by context** (documented in `.clang-tidy`), not to lower the bar: `misc-include-cleaner` (macOS/POSIX false positives — flags `strdup`/`open`/`popen` that *are* included) and `concurrency-mt-unsafe` (project is single-threaded). One justified `NOLINT` remains: `popen` in the test-only DB inspection helper.
- Result: `clang-tidy` reports **0 findings across `src` + `tests`**; `make lint` is green.
