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
- **Normalize + hash (pure):** `normalize.c` / `normalize.h` — no I/O, no store/cli. Body trim + UTF-8 + size; shared `normalize_token` for tag and key; `body_hash_hex` (lowercase SHA-256). SHA-256 amalgamation in `third_party/sha256/` (Brad Conte public domain); only `normalize.c` includes `sha256.h`.
- **Commands layer:** `commands.c` orchestrates normalize → `store_*` → `output_*`. No SQL. Norm errors map via full phrases (`empty body after trim`, …), not bare `norm_status_string` labels.
- **Output:** `output.c` owns JSON escaping (RFC 8259) and envelopes; human `add` prints id only; human `get` uses `output_body_human` (terminal C0/DEL → `?`, keep `\n`/`\t`); human list previews also neutralize controls. Never raw-`fputs` a stored body to a TTY.
- **Path resolve:** `util_resolve_db_path` — `--db` > `REMEMBER_DB` > `~/.remember/remember.db`. Rejects `:memory:` and `file:` URIs (silent data-loss footguns through `sqlite3_open_v2`).
- **`store_add`:** keyless body-hash merge (union tags, keep source/created_at/body) or keyed upsert (replace body, union tags, keep source/created_at/key). FTS resync in the same transaction. Thin `store_get` / `store_get_by_key` / `store_list` for verification (full filters in step 05).
- **Stable exit codes:** `0` ok, `1` usage/error, `2` not found. Scaffold NYI remains private for still-unimplemented commands (`search`/`update`/`delete`).

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
- **Shifting a `char`/`unsigned char`:** it promotes to signed `int`, so `byte << 24` is signed overflow (UB) for bytes ≥ 0x80. Cast to an unsigned type first: `(uint32_t)byte << 24`. (Bit us in vendored `sha256.c`.)
- **Loop counters over a `size_t` length must be `size_t`**, not a fixed-width type — a 32-bit counter truncates lengths ≥ 4 GiB. Fix for correctness on every arch even when today's inputs are bounded.
- **Error codes must be accurate:** a caller-contract violation (missing/too-small output buffer) is not the same as bad user input. Give it its own status (`NORM_ERR_INTERNAL`), never overload `TOO_LONG`/`OOM`.
- Format + lint clean on every step (`scripts/lint-all.sh`).
- Full quality matrix (sanitizers, LSan, scan-build, IWYU, CI): **`docs/QUALITY.md`**.

### Vendoring third-party C (checklist for every new drop)

CMake alone is not enough. A clean link on the developer machine still fails **Linux CI lint** if tools that do not fully honor `compile_commands.json` cannot see the vendor headers. Lesson from step 03: `normalize.c` built fine via `target_link_libraries(... sha256)` (PUBLIC `-I third_party/sha256`), but IWYU’s **direct** path only had `-I src -I third_party/sqlite` → `fatal error: 'sha256.h' file not found`.

When adding (or changing) anything under `third_party/<name>/`:

| Step | What to do |
|------|------------|
| 1. Tree + pin | `third_party/<name>/` with `README.md`: version, upstream URL, license, **local modifications** (if any). Prefer amalgamation / few files. |
| 2. CMake target | `add_library(<name> STATIC …)` with `target_compile_options(... PRIVATE -w)`. `target_include_directories(... PUBLIC …/third_party/<name>)` so consumers get the include path by linking. |
| 3. Single consumer | Only **one** project file includes the vendor header (e.g. only `store_sqlite.c` → `sqlite3.h`; only `normalize.c` → `sha256.h`). Everyone else uses a project port (`store.h`, `body_hash_hex`, …). |
| 4. Lint boundary | Extend `scripts/lint-all.sh` grep so the vendor API cannot leak into other `src/` files (pattern already used for sqlite / sha256). |
| 5. Lint include paths | Add `-I third_party/<name>` (or `$ROOT/third_party/<name>`) to **every** tool that parses `src/*.c` without relying solely on CMake: **IWYU** (`IWYU_VENDOR_INCLUDES` for both `iwyu_tool` *and* the per-file fallback), **cppcheck**. Keep that list in sync with CMake PUBLIC includes. |
| 6. Sanitizer gate | See below — auditable TUs must run under UBSan with real tests; huge amalgamations stay `-w` lib only. |
| 7. Local fixes | Prefer not forking style; if you must fix UB/portability, mark `Local fix:` in the vendor `.c` and document in its README. Do not reformat the whole amalgamation. |

**Do not** “fix” missing includes by copying headers into `src/` or using deep relative paths like `#include "../third_party/…"`. The include path is a **tooling contract**, not just a compiler flag from one target.

**Smoke after adding a vendor:** on a machine with IWYU (or in CI), `REMEMBER_IWYU=1 CI=true ./scripts/lint-all.sh` must pass — not only `cmake --build`.

### Vendored code gets a sanitizer gate, not blind trust

`-w` on a third-party TU silences *warnings* but a TU built only as a `-w` static lib is also **never instrumented** — UBSan/ASan can't see its bugs (this is how the `sha256.c` signed-shift UB hid). Rule:

- For any vendored TU **we call and can audit** (e.g. `sha256.c`), compile it **into** the sanitized unit binary (`remember_store_tests`) as a direct source with a per-source `-w`, and cover it with tests (NIST vectors) so UBSan runs over it. Do **not** also link its `-w` static lib into that target (double definition).
- `-fno-sanitize-recover=all` on instrumented targets so UB **aborts** on any run, not only under ctest's `halt_on_error` env.
- Large, upstream-trusted TUs with intentional/suppressed UB (`sqlite3.c`) stay the plain `-w` lib — blanket UBSan there is noise, not signal.
- **Digest test vectors must hit every padding/block path** under that instrumented TU — not only empty/`"abc"`. At minimum: a message with `len >= 56` (forces the long final-pad branch) and one with `len >= 64` (forces multi-block `sha256_update`). Covered by `body_hash_long_pad_and_multiblock` in `tests/test_normalize.c`. Short vectors alone leave the UBSan gate half-blind.
- **CLI links the `-w` `sha256` static lib; the unit binary compiles the digest under sanitizers.** That split is intentional: detection lives in `remember_store_tests` + NIST/long vectors, not in rebuilding the digest into every ASan CLI. Do not "fix" this by linking the static lib into the unit target (duplicate symbols) or by dropping the instrumented compile.

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

## Data model invariants

- **NUL is end-of-string.** Bodies and tokens are C strings: `body_trim_copy` stops at the first NUL within its `src_len`, so `*out_len == strlen(*out)` always. This keeps stored text, its byte length, and its hash in agreement — a body bound to SQLite with an explicit length would otherwise disagree with a hash taken over the full buffer, silently breaking dedup. Never reintroduce a path that carries bytes past a NUL.
- **Body vs token content differ by design.** Body: trim ASCII ws, valid UTF-8, ≤64 KiB — control characters are **allowed**. Token (tag/key): additionally rejects ASCII ws/control and casefolds ASCII. Do not "tidy" bodies by stripping controls at normalize time; that belongs at output.
- **Output owns escaping.** Because bodies keep control chars, every surface that emits a body must escape it: JSON per RFC 8259 (`"`, `\`, U+0000–U+001F as `\uXXXX`/short forms); human/TTY output must not pass raw escape sequences through. One shared escaping helper across `add` JSON envelope, `get`, and `list`. See step 04 plan.

## Testing

- Black-box CLI tests via harness (`--db` injected first).
- Parser edges: missing `--db` value, unknown option, globals after command, subcommand help, topic help.
- Collect-all asserts: guard against NULL deref after failed asserts (early return if dependent data missing).
- Inspect-only cases may use `sqlite3` CLI.
- **`--only` group matching is exact comma-tokens**, not substring (`key` must not select `key_add`). See `group_selected` in `tests/test_main.c`.
- **Step gates should include every plan-required green test** for that step. Leaving green cases only in mixed red suites (`verification_edges`, full `key`) means ctest can pass while plan criteria silently regress.
- **Path policy tests belong next to the policy.** Rejecting `:memory:` / `file:` in `util_resolve_db_path` needs a black-box case, not only manual smoke.
- **stdin vs argv body limits must share one enforcer.** Raw stdin caps are memory guards only (`REMEMBER_STDIN_MAX`); `body_trim_copy` owns the post-trim 64 KiB rule for both paths.

## Process

- After **each implementation plan step**: run a strict review (see skill `remember-step-review` / project checklist).
- Append step-specific learnings here and to the step plan’s “Review notes” section.
