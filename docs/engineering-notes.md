# Engineering notes (remember)

Living notes distilled from design, reviews, and implementation decisions.
Update after each step review.

## Architecture

- **Store port:** only `store_sqlite.c` may include SQLite. Everyone else uses `store.h`.
- **Amalgamation:** `third_party/sqlite/` (pinned version in its README); CMake target `sqlite3` built with `-w` (no project `-Werror` on that TU).
- **SQLite compile flags** (single-threaded personal CLI):
  - Required: `SQLITE_ENABLE_FTS5`, `SQLITE_OMIT_LOAD_EXTENSION`, `SQLITE_DQS=0`
  - Recommended: `SQLITE_THREADSAFE=0`, `SQLITE_DEFAULT_MEMSTATUS=0`, `SQLITE_LIKE_DOESNT_MATCH_BLOBS`, `SQLITE_OMIT_DEPRECATED`, `SQLITE_OMIT_DECLTYPE`, `SQLITE_OMIT_PROGRESS_CALLBACK`, `SQLITE_OMIT_SHARED_CACHE`, `SQLITE_DEFAULT_FOREIGN_KEYS=1`
  - Polish: `SQLITE_DEFAULT_FILE_PERMISSIONS=0600` (private DB files), `SQLITE_SECURE_DELETE` (zero deleted content)
  - Debug only: `SQLITE_ENABLE_API_ARMOR` (misuse checks; not a security boundary)
  - Do **not** re-enable threads without flipping `SQLITE_THREADSAFE` back to `1` (or `2`).
- **`store_open`:** creates parent dirs `0700`, applies `foreign_keys=ON` + `busy_timeout=5000`, gates on `user_version` (0→create→1, 1→ok, >1→error `"database is newer than this remember"`). Default journal (no WAL).
- **Schema bootstrap must be atomic:** run initial DDL + `user_version=1` inside one transaction (or tear down on failure). A partial create at `user_version=0` bricks later opens (`table already exists`).
- **Parent mkdir:** on `EEXIST`, verify the path component is a directory (`S_ISDIR`), not a file.
- **CLI layers:** `cli_parse` (pure argv → `CliArgs`) / `main` (I/O + exit codes) / later `commands_*` / `output` as needed.
- **Stable exit codes:** `0` ok, `1` usage/error, `2` not found. Scaffold NYI is private (`REMEMBER_NYI` in `main.c` only), not public ABI.

## CLI parse contract

- `cli_parse` is **`void`**. Source of truth for failures: `out->error` (`CliError`) + optional `error_arg` / `error_option`.
- User-facing base strings: **`cli_error_message()`** only. `main` formats with arg pointers.
- **ArgCursor** walks argv; do not mutate the for-loop index inside helpers.
- **Globals** (`--db`, `--json`) allowed before or after the subcommand.
- **Before** subcommand, unknown `-flag` → usage error.
- **After** subcommand, unknown-looking flags stay in `rest_*` for command parsers.
- **Command registry:** single table `k_commands[]` with **linear** name lookup (n is tiny). Add a command once in that table (+ dispatch in `main` until function pointers land).
- **Help:** general (`--help` / `help`), topic (`help add`, `add --help`).

### Sanitizers / leaks (summary)

- Instrument **`remember`** and **`remember_store_tests`** with ASan+UBSan (Debug).
- Do **not** ASan **`remember_tests`** (fork parent + ASan child deadlocks).
- Linux CI: `REMEMBER_ENABLE_LSAN=ON` + `detect_leaks=1`.
- macOS leak smoke: **`remember_plain`** + `leaks(1)` (ASan heaps are invisible to `leaks`).
- Details: `docs/QUALITY.md`.

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
- Full quality matrix (sanitizers, LSan, scan-build, IWYU, CI): **`docs/QUALITY.md`**.

### Resource cleanup: `goto cleanup` (required pattern)

Multi-resource functions use a **single exit with `goto cleanup` / `goto cleanup_fail`**, not copy-pasted free/close on every branch.

```c
int do_thing(...)
{
    T *a = NULL;
    U *b = NULL;
    int rc = -1;

    a = acquire_a();
    if (a == NULL) {
        goto cleanup;
    }
    b = acquire_b();
    if (b == NULL) {
        goto cleanup;
    }
    if (work(a, b) != 0) {
        goto cleanup;
    }
    rc = 0;

cleanup:
    free_b(b);   /* NULL-safe */
    free_a(a);
    return rc;
}
```

Rules:
- One cleanup label (or success path + `cleanup_fail` when the success path retains ownership — see `store_open`).
- Free/close in reverse acquisition order; helpers must accept NULL.
- On failure, set the error **before** `goto`; cleanup must not overwrite a useful error (use quiet rollback helpers when needed).
- Prefer this over nested `if` pyramids and over duplicated teardown blocks.

## Testing

- Black-box CLI tests via harness (`--db` injected first).
- Parser edges: missing `--db` value, unknown option, globals after command, subcommand help, topic help.
- Collect-all asserts: guard against NULL deref after failed asserts (early return if dependent data missing).
- Inspect-only cases may use `sqlite3` CLI.

## Process

- After **each implementation plan step**: run a strict review (see skill `remember-step-review` / project checklist).
- Append step-specific learnings here and to the step plan’s “Review notes” section.
