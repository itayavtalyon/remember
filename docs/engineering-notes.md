# Engineering notes (remember)

Living notes distilled from design, reviews, and implementation decisions.
Update after each step review.

## Architecture

- **Store port:** only `store_sqlite.c` may include SQLite. Everyone else uses `store.h`.
- **CLI layers:** `cli_parse` (pure argv → `CliArgs`) / `main` (I/O + exit codes) / later `commands_*` / `output` as needed.
- **Stable exit codes:** `0` ok, `1` usage/error, `2` not found. Scaffold NYI is private (`REMEMBER_NYI` in `main.c` only), not public ABI.

## CLI parse contract

- `cli_parse` is **`void`**. Source of truth for failures: `out->error` (`CliError`) + optional `error_arg` / `error_option`.
- User-facing base strings: **`cli_error_message()`** only. `main` formats with arg pointers.
- **ArgCursor** walks argv; do not mutate the for-loop index inside helpers.
- **Globals** (`--db`, `--json`) allowed before or after the subcommand.
- **Before** subcommand, unknown `-flag` → usage error.
- **After** subcommand, unknown-looking flags stay in `rest_*` for command parsers.
- **Command registry:** single table `k_commands[]` + open-addressing hash map for name lookup. Add a command once in that table (+ dispatch in `main` until function pointers land).
- **Help:** general (`--help` / `help`), topic (`help add`, `add --help`).

### Subcommand identity: NONE vs UNKNOWN

| State | Meaning |
|-------|---------|
| `CLI_CMD_NONE` | User provided **no** subcommand token (only globals / empty). |
| `CLI_CMD_UNKNOWN` | User provided a token that is **not** a registered command. |
| `CLI_ERR_MISSING_COMMAND` | Error for NONE (after meta flags handled). |
| `CLI_ERR_UNKNOWN_COMMAND` | Error for unknown name; `error_arg` points at the token. |

Internal `name == NULL` in lookup is not a user path: treat as lookup miss → UNKNOWN if it ever surfaces; do not `abort()`. Prefer reporting via `CLI_ERR_*`, never assert-crash for CLI input.

## Style / C rules

- Prefer **`const`** on argv and string views.
- **`bool`** for booleans; **`int`** for counts/indices.
- Help and user strings: **ASCII** only.
- `(void)fprintf` / `(void)fputs` OK for stdout/stderr.
- Exhaustive `switch` on enums with **`default:`** defensive path (no silent fallthrough).
- Format + lint clean on every step (`scripts/lint-all.sh`).

## Testing

- Black-box CLI tests via harness (`--db` injected first).
- Parser edges: missing `--db` value, unknown option, globals after command, subcommand help, topic help.
- Collect-all asserts: guard against NULL deref after failed asserts (early return if dependent data missing).
- Inspect-only cases may use `sqlite3` CLI.

## Process

- After **each implementation plan step**: run a strict review (see skill `remember-step-review` / project checklist).
- Append step-specific learnings here and to the step plan’s “Review notes” section.
