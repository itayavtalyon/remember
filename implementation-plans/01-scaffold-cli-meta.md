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
- [x] Post-review refactor: void parse, CliError, ArgCursor, command hash map, subcommand help, ASCII help, printing in main, private NYI

## Review notes (2026-07-22)

Strict review of step-01 code drove a refactor (not just nits):

- **Parse errors:** `cli_parse` is `void`; `CliError` + `cli_error_message()` + `error_arg`/`error_option` are the source of truth.
- **ArgCursor** owns walking argv (no `int *ip` mutation).
- **Globals** work before/after the subcommand; command flags only after.
- **Command table + open-addressing hash map** for name lookup; summaries for help.
- **Subcommand help:** `add --help`, `help add`.
- **Printing** only in `main.c`; **REMEMBER_NYI** private to main.
- **bool** for booleans; **const** argv; ASCII strings; `default` in switches.
- **NONE vs UNKNOWN:** none = missing token; unknown = bad token + `error_arg`. No abort on bad input.
- Lasting rules: `docs/engineering-notes.md`, checklist `docs/STEP_REVIEW_CHECKLIST.md`, skill `remember-step-review`.

### Still note for later steps

- `rest_argv` may still contain global tokens if the user mixed them after the command; command parsers should ignore re-seen `--db`/`--json` or re-scan with shared helpers.
