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
- **Commands layer:** public `commands.h` only. Implementation is split by **concern axis**, not one file per subcommand:
  - `commands_common.c` / `commands_common.h` — internal shared helpers (err messages, tag normalize, argv helpers, `load_body`). **Not** a public API; only `cmd_*.c` include it.
  - `cmd_add.c` — write path (key/tags, `store_add`; body via shared `load_body`).
  - `cmd_locator.c` — get + delete + update (shared id/`--key` locator; update adds `--text`/`--tag`/`--clear-tags`).
  - `cmd_query.c` — list + search (shared filter/paging parse; different store call).
  - Orchestration is still normalize → `store_*` → `output_*`. No SQL. Norm errors map via full phrases (`empty body after trim`, …), not bare `norm_status_string` labels.
- **Output:** `output.c` owns JSON escaping (RFC 8259) and envelopes; human `add` prints id only; human `get` uses `output_body_human` (terminal C0/DEL → `?`, keep `\n`/`\t`); human list previews also neutralize controls. Never raw-`fputs` a stored body to a TTY.
- **Path resolve:** `util_resolve_db_path` — `--db` > `REMEMBER_DB` > `~/.remember/remember.db`. Rejects `:memory:` and `file:` URIs (silent data-loss footguns through `sqlite3_open_v2`).
- **`store_add`:** keyless body-hash merge (union tags, keep source/created_at/body) or keyed upsert (replace body, union tags, keep source/created_at/key). FTS resync in the same transaction.
- **`store_list`:** takes `ListQuery` (tags AND via EXISTS, optional `source`/`key`, `limit`/`offset`). Sort `updated_at DESC, id DESC`. CLI owns default limit 20 / max 1000 / offset ≥ 0 and a **tag-filter cap** (`LIST_TAG_FILTER_MAX` 50) under the store bind budget (`LIST_BIND_CAP`); store uses values as given. COUNT + paged SELECT run in **one read transaction** so empty pages still report unpaged `total`.
- **`store_delete_by_id` / `store_delete_by_key`:** one write txn: load snapshot → FTS delete → `DELETE entries` (CASCADE `entry_tags`) → orphan-tag GC → COMMIT. Snapshot returned for JSON `action:deleted` matches the locked row.
- **Locator (get/delete/update):** shared validate/resolve in `cmd_locator.c` — exactly one of positional id / `--key`; reject `--source`. Update adds its own flag parse (`--text`/`--tag`/`--clear-tags`) on top of the same locator contract.
- **Stable exit codes:** `0` ok, `1` usage/error, `2` not found. All planned subcommands are implemented (no public NYI exit).
- **`store_search`:** `SearchQuery` = FTS MATCH string + embedded `ListQuery` filters (tag AND / source / key / limit / offset). Shared SQL filter builder with list (`list_append_filters`). Rank `bm25(entries_fts)` then `updated_at DESC, id DESC`. **FTS5 requires the real table name** in `MATCH`/`bm25()` — aliases fail (`no such column`). Invalid FTS syntax → `STORE_ERR_QUERY` ("invalid search query"). COUNT + page in one read txn (same empty-page total contract as list).
- **CLI search:** same filter/paging parse path as list (`cmd_query.c`) + one required QUERY positional. Outer ASCII whitespace is trimmed (same set as normalize: space/tab/LF/CR/VT/FF); empty-after-trim → usage error `"empty search query"` (not FTS). Trimmed string is the FTS MATCH input (not tag/key-normalized). Human preview via `output_entry_human_line` (≤80 codepoints); JSON full bodies via `output_list_envelope`.
- **`store_update`:** locate by id or key; independent `set_body` / `set_tags` flags; always bump `updated_at` on success; FTS resync in the same write txn. Keyless body-hash collision → `STORE_ERR_CONFLICT` + conflicting id (CLI prints it on stderr). Tag replace deletes `entry_tags` then re-links + orphan GC. Never changes `source` or `key`.
- **CLI update:** lives in `cmd_locator.c` (same id/`--key` contract as get/delete) + opt-in `--text` / `--tag` / `--clear-tags`. Body load via shared `load_body` in `commands_common` (add + update). No positional body; `--source` rejected.
- **Gate suites (step 07):** `update`, `key_gld` (keyed update cases), `verification_gld` (update edges), `verification_search` includes `fts_search_reflects_body_update`.
- **Timestamps (public contract, step 07 / design Round 9):** `utc_now` writes ISO-8601 UTC with a **fixed 3-digit millisecond** fraction: `YYYY-MM-DDTHH:MM:SS.mmmZ` (via C11 `timespec_get` + `gmtime`). Always pad `.mmm` so string sort equals chronological order for `updated_at DESC, id DESC`. Prefer this over `sleep(1)` when tests assert bumps or list order. Do **not** parse as a fixed 20-char second stamp — use quote-delimited field extract or a real ISO parser. Documented in `design-logs/001-foundations.md` Round 9. C11 `timespec_get(TIME_UTC)` already bounds `tv_nsec` to `[0, 999999999]` — do not add clamp branches that coverage cannot hit.
- **Test POSIX includes on Darwin:** `mkdtemp` is in `unistd.h` on macOS; on Linux it is in `stdlib.h` when `_POSIX_C_SOURCE` is set. Prefer `#ifdef __APPLE__` + `unistd.h` for TUs that only need `mkdtemp` (see `test_schema_config.c` / `test_verification_edges.c`) — unconditional `unistd.h` fails Linux clang-tidy `misc-include-cleaner`.

### Lessons: when/how to split a growing commands module

Learned while growing past ~1000 lines at step 06 (search):

1. **Split on shared knowledge, not on every subcommand name.** One file per CLI verb looks tidy but duplicates parse helpers. Axes that actually shared code:
   - **common** — messages, tag normalize, argv list growth, store→exit mapping
   - **locator** — get/delete (same “exactly one of id|key” contract; update will join here)
   - **query** — list/search (same filters + paging; different store call + optional FTS query)
   - **add** — unique body/stdin pipeline (and later `update` body edits may share pieces via common, not via query)
2. **Keep one public header.** `commands.h` stays the only face for `main`. Internal `commands_common.h` is for command TUs only — do not export it from the public surface or pull it into `main`/tests.
3. **Wire every new TU in CMake twice:** `REMEMBER_LIB_SOURCES` *and* the `-Wcast-qual` source list (easy to forget; a missed TU silently drops a warning gate).
4. **Defer the split until a second consumer of a parse path exists.** list alone did not justify a module; list+search did. Similarly, wait to extract body-edit helpers until `update` lands if they would otherwise be single-use.
5. **Line-count is a signal, not a rule.** Prefer navigating by concern (filter vs locator vs write) over forcing ~N lines per file.
6. **`misc-include-cleaner` requires *direct* includes in each `.c`.** Pulling `store.h` / `normalize.h` only via `commands.h` or `commands_common.h` is enough for the compiler but **fails clang-tidy**. Every command TU that uses those symbols must `#include "store.h"` / `"normalize.h"` itself (same rule for any new `cmd_*.c`).
7. **Fault-injection sweeps grow complexity fast.** When adding a store op to `store_fault_injection_sweep`, extract per-op helpers (`sweep_*_faults`) so cognitive-complexity stays under the tidy threshold — do not paste another free-page loop into the main `for`.

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
- **Prefer ISO C11 over POSIX when equivalent** for code IWYU/scan parses without relying solely on feature macros: e.g. `malloc`+`memcpy` instead of `strdup`, bounded scan instead of `strnlen`, `gmtime`+copy instead of `gmtime_r` (CLI is single-threaded). Keep CMake `_POSIX_C_SOURCE` for APIs that must be POSIX (`mkdir`, …) and pass the same defines into the **IWYU** clang command line on Linux (`scripts/lint-all.sh`).

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
- **`--only` group matching is exact comma-tokens**, not substring (`key` must not select `key_gld`). See `group_selected` in `tests/test_main.c`.
- **Step gates should include every plan-required green test** for that step. Leaving green cases only in mixed red suites (`verification_edges`, full `key`) means ctest can pass while plan criteria silently regress.
- **Gate suite names:** green get/list/delete slices are `key_gld` and `verification_gld` (not `*_add`). When step 06/07 lands, extend or rename deliberately — do not drop criteria by editing an outdated name.
- **Single green-suite list:** `tests/gate-suites` is the only place to list `--only` suites for ctest `step_gate` and `scripts/ci-linux.sh` / GHA. Update that file when a step lands greens so CI grows with the code (do not hard-code stale filters in `ci.yml`).
- **Linux CI one-liner:** `./scripts/ci-linux.sh` (Docker ubuntu:24.04). Agents/humans should run it before claiming Linux-ready.
- **pre-push hook:** `./scripts/install-hooks.sh` sets `core.hooksPath=.githooks` so `git push` runs `ci-linux.sh`. Skip with `SKIP_LINUX_CI=1` or `--no-verify`.
- **Line coverage gate:** `scripts/check-coverage.sh` — clang coverage on `src/*.c`; require **100% functions** + **100% effective lines** (defensive pure-error exits excluded; see script). Complements `tests/gate-suites` (untested *logic* vs dropped *suites*). `REMEMBER_TEST_HOOKS` enables store malloc/prepare/step/exec fault injection for OOM/SQLite error paths.
- **Path policy tests belong next to the policy.** Rejecting `:memory:` / `file:` in `util_resolve_db_path` needs a black-box case, not only manual smoke.
- **stdin vs argv body limits must share one enforcer.** Raw stdin caps are memory guards only (`REMEMBER_STDIN_MAX`); `body_trim_copy` owns the post-trim 64 KiB rule for both paths.

## Process

- After **each implementation plan step**: run a strict review (see skill `remember-step-review` / project checklist).
- Append step-specific learnings here and to the step plan’s “Review notes” section.
