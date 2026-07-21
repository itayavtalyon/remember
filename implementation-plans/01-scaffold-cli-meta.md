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

- [ ] Meta tests green
- [ ] Formatted + compiles with `-Werror`
- [ ] No sqlite includes
