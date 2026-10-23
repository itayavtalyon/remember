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
- **`store_open`:** creates parent dirs `0700` unless `--no-create` (then a missing file fails before any mkdir). Applies `foreign_keys=ON` + `busy_timeout` (default 5000; `--busy-timeout` 0..60000). Gates on `user_version` (0→create at **5**, 1/2/3 climb the old rebuild then the v5 delta, 4→v5 delta, 5→ok, >5→error `"database is newer than this remember"`). One `BEGIN IMMEDIATE` for create and migrate. FTS is `key, body, tags` (`unicode61 remove_diacritics 2`); rows store the key (empty string when NULL), body, and space-joined tags with Hebrew marks removed (`fold_hebrew_marks` in `normalize.c`). `store_rekey` resyncs that key before commit. `entries` keeps the original body. Fresh DDL also creates `bucket_prefixes`, `settings`, and `conflicts.origin` (unused until later plans). There is no `devices` table. Sidecar `<path>.device_id` (mode 0600, one lowercase UUID line, written via temp file + rename) is the only device id: a valid file is used, a missing file is minted, an invalid file is `invalid device_id sidecar` and is not overwritten. `device new` replaces an invalid sidecar; no other open does. Open does not read a device id from the database. Hard delete and purge do not check a device count (`not_single_device` stays in the catalog; no command returns it). `device` prints that id. `device new` mints another under the same write lock as a missing-sidecar open, leaves every version vector unchanged, and reports the previous sidecar id. Run it after a restore or a copied brain. Default journal (no WAL). Every connection sets `SQLITE_DBCONFIG_DEFENSIVE` (`decision:remember-sqlite-defensive-mode`); repair lifts it only around the schema unlink. **No `expires_at` index** (ponytail; personal scale).
- **Schema bootstrap must be atomic:** run initial DDL + `user_version=5` inside one transaction (or tear down on failure). A partial create at `user_version=0` bricks later opens (`table already exists`). v1 migrate is `ALTER TABLE entries ADD COLUMN expires_at TEXT` plus `entry_links` DDL plus v4 columns, then the v5 delta; v2 migrate is `entry_links` plus v4 columns, then the v5 delta; v3 migrate is v4 columns (`sync_id`/`deleted_at`/`version_vector` + `conflicts`), then the v5 delta. The v5 delta drops and recreates `entries_fts` from the shared `K_FTS_SQL`, repopulates every `entries` row (all bins), creates `bucket_prefixes` and `settings` when missing, rebuilds `conflicts` so `origin` exists with the same `sqlite_master` text as a fresh database (rename the old table away, then `CREATE` the canonical DDL — renaming the new table quotes its name), and `DROP TABLE IF EXISTS devices` last (the row is not copied into the sidecar). It does not create `tags` or `entry_tags`: real v1–v4 brains already have them (plan 15 stage 2). A fault after `bucket_prefixes` is created rolls the whole migration back to the old `user_version` and the old FTS. `import --from-db` accepts `user_version` 4 and 5 only (`IMPORT_MIN_USER_VERSION`, max `SCHEMA_VERSION`). Above that is `STORE_ERR_SOURCE_TOO_NEW` with the same text as open (`database is newer than this remember`). The source is probed read-only: a missing `conflicts.origin` column or a missing `bucket_prefixes` / `settings` table is absent, and those rows are not copied. `devices` is not read. Same write-lock transaction.
- **`entry_links` (P7, store stage):** one row per `(from_id, to_id, kind)` with stored kind `related|supersedes|cites`. `related` is canonical `(min,max)`. No `dir` column, no self-heal. Reads are `from_id = S OR to_id = S`. Ports: `store_link` / `store_unlink` / `store_list_neighbors` / `store_list_neighbors_for` / `store_get_any` / `store_rekey`. Output `cited_by` is CLI, not store. Real graph writes bump both endpoint `updated_at`. `store_rekey` set/rename/clear in place (id + edges kept); empty new key is INTERNAL (CLI rejects `empty key`). `supersedes_reaches` is recursive set **`UNION`** (not `UNION ALL`). Neighbor row copies fail closed on OOM (never emit a keyless/durable lie); unlink `fill_stub` failures keep their real status.
- **Parent mkdir:** on `EEXIST`, verify the path component is a directory (`S_ISDIR`), not a file.
- **CLI layers:** `cli_parse` (pure argv → `CliArgs`) / `main` (I/O + exit codes) / later `commands_*` / `output` as needed.
- **Normalize + hash (pure):** `normalize.c` / `normalize.h` — no I/O, no store/cli. Body trim + UTF-8 + size; shared `normalize_token` for tag and key; `body_hash_hex` (lowercase SHA-256). SHA-256 amalgamation in `third_party/sha256/` (Brad Conte public domain); only `normalize.c` includes `sha256.h`.
- **Commands layer:** public `commands.h` only. Implementation is split by **concern axis**, not one file per subcommand:
  - `commands_common.c` / `commands_common.h` — internal shared helpers (err messages, tag normalize, argv helpers, `load_body`, `--ttl`/`--expires` parse). **Not** a public API. Included by `cmd_*.c`, by `remember_app.c` (parse and open errors), and by the mapper tests. `main.c` does not include it.
  - `cmd_add.c` — write path (key/tags, `store_add`; body via shared `load_body`; optional expiry).
  - `cmd_locator.c` — get + delete + update + rekey (shared id/`--key`/`--sync-id` locator; update adds `--text`/`--tag`/`--clear-tags`/`--ttl`/`--expires`/`--clear-expires`/`--undelete`; `--expired`/`--deleted`/`--trash` via `CmdBinOpts`). Unflagged delete is soft (live or expired); `--expired`/`--deleted` on delete is one-row hard. `--sync-id` must be canonical lowercase UUID v7.
  - `cmd_query.c` — list + search (shared filter/paging parse; different store call; same bin flags).
  - `cmd_purge.c` — `purge --expired|--deleted` (exactly one flag; whole-bin hard wipe) and `purge-trash` (deprecated alias of `purge --expired`). Wire in `REMEMBER_LIB_SOURCES` **and** the `-Wcast-qual` list.
  - `cmd_graph.c` — `link` / `unlink` / `related` (pair locators id/`--*-key`/`--*-sync-id`; default either-bin live+expired via `store_get_any*`; `--deleted` expands to `store_get_row*` so deleted ends resolve; `--expired` rejected on graph). 5-name `type` is output, not store. `rekey` lives in `cmd_locator.c` (update’s bin contract). Graph resolvers must return `REMEMBER_ERR` (1), never `-1` (that becomes exit 255). Positional `ID ID` sugar is `--from`/`--to` on **link and unlink** (numeric ids only). link/unlink/rekey bump VV on affected ends; no-op unlink does not.
  - `cmd_import.c` — `import --from-db`, `conflicts`, `conflict accept --id --keep`. Store owns RO open + merge; commands never open a second SQLite handle.
  - `cmd_device.c` — `device` prints the sidecar id; `device new` mints a new one (`store_device_new`) and does not touch version vectors. It also replaces an invalid sidecar; no other command does. JSON `previous` is null only when the file was missing at the mint (open already minted it, D4).
  - Orchestration is still normalize → `store_*` → `output_*`. No SQL. Norm errors map via full phrases (`empty body after trim`, …), not bare `norm_status_string` labels.
- **Test VFS (plan 16 stage 4, X-E02):** `src/test_vfs.c` is the only TU besides `store_sqlite.c` that may include `sqlite3.h` (the lint grep allows both). CMake option `REMEMBER_TEST_VFS` defaults OFF and is forced OFF for `CMAKE_BUILD_TYPE=Release`. It is compiled into the `remember` executable only — never `remember_plain`, `remember_core`, or the core library list. `main` calls `remember_test_vfs_install` when the define is set; that wraps the `unix` VFS and registers it as the default. The unix file stays at the front of the buffer. A tail after it holds a copy of the unix method table with only `xRead` and `xWrite` replaced. With the env vars unset the wrapper is transparent. `REMEMBER_TEST_ENOSPC=1` fails writes with `SQLITE_FULL`. `REMEMBER_TEST_FAIL_WRITE=N` / `FAIL_READ=N` let N calls succeed, then stick on `SQLITE_IOERR_WRITE` / `SQLITE_IOERR_READ`. `REMEMBER_TEST_NOMEM=1` returns `SQLITE_IOERR_NOMEM` from a write and from a read of a non-empty file; a zero-length read (the new-brain probe) is passed through. SQLite rewrites that to `SQLITE_NOMEM` before the store sees it (`decision:plan16-nomem-sqlite-name`), and D6 still classifies it `database`. chmod acceptance tests print `SKIP <test> (running as root; file mode bits are ignored)` when `euid` is 0. Linux CI drops to the source-tree owner, or to `rememberci` when the mount is uid 0, before the suite so those cases run. `install.sh` refuses a binary that defines `remember_test_vfs_install`.
- **Diagnostics (plan 16):** `diag.c` is the only code catalog and the only stderr writer for errors and warnings. Error tokens are the E4.3 list, in that order, one string each. Warning tokens are `raw_query_ignored`, `deprecated_flag`, and `synced_folder`, a separate enum. Plain errors are `remember: <message>\n`. A plain notice is one line: `write_notice` writes the message through `output_text_human`, which maps every byte below `0x20` except tab, and DEL, to `?`; quotes and backslashes stay, and JSON is unchanged (`decision:plan17a-plain-notice-one-line`). `output_body_human` uses the same classifier and also keeps `\n`. JSON errors are one line `{"version":1,"error":{"code":"<code>","message":"<message>"}}`. Strings are escaped with `output_json_string`. Every warning's plain line is `remember: warning: <text>\n` (Itay 2026-10-15; amends E4.6 `note:`). JSON warnings use the same envelope with `"warning"`, and `message` is `<text>` without the prefix. `--json-errors` is a global, parsed anywhere like `--json`, and `remember_run` sets one process flag that `err_msg` reads. Deprecation and synced-folder warnings still print when the command then fails. `STORE_ERR_QUERY` is `invalid_query` with the sentence `invalid search query` (`decision:plan16-invalid-search-query`). Non-SQLite store-open text stays byte-identical, coded per `decision:plan16-open-message-codes`. `err_fmt` allocates the formatted line, so a long option stays that sentence. SQLite failures go through one `capture_sqlite_error` helper in the adapter, called before `finalize`/`ROLLBACK` (those clobber `sqlite3_extended_errcode`). Mapping and the extended-name table live only in `store_sqlite.c` (D6: `SQLITE_NOMEM` / `SQLITE_IOERR_NOMEM` before the IOERR row). `store.h` never includes sqlite. `store_open_opts` carries `StoreOpenOpts` and a `StoreError` out-param; NULL opts are the 5000 ms create defaults. `--busy-timeout MS` (0..60000) and `--no-create` are globals parsed with `--json` (a missing `--busy-timeout` value is `--db`'s "option requires a value"; `-1` is the invalid-range sentence, so a leading dash is still the value). `util_parse_nonneg` is the one decimal parser for that flag and for `--limit` / `--offset`. General help and the command footer name `--json-errors`, `--busy-timeout MS`, and `--no-create`. D7 `util_absolute_path` is lexical (cwd join, `.`/`..`, no symlink resolve); E4.9 `path` is that absolute `--db` path on the six codes that carry it. E4.8 is `database error: <cause> (<name>). <fix>` except `database`, which has no cause. JSON `detail` always includes `sqlite_code`, `sqlite_extended`, `sqlite_name`, `sqlite_message` (empty string when SQLite gave none); `errno` only when non-zero; `reason` only on `cannot_open`; `fts_only` only on `corrupt`. SCHEMA retry is `exec_sql` once more, then `database`. `capture_sqlite_error(...)` is a coverage-defensive line, same class as `set_err`. Never write an error or warning line outside `diag.c`. `app_err()` is the emitter's stream; command code calls `err_msg`, `diag_warning`, or `diag_error_sqlite`. `scripts/lint-all.sh` rejects any other `app_err(` call, a raw `stderr` argument, and `perror(`. `remember_main` is the one place that binds `stderr`.
- **Output:** `output.c` owns JSON escaping (RFC 8259) and envelopes; human `add` prints id only; human `get` uses `output_body_human` (terminal C0/DEL → `?`, keep `\n`/`\t`) then an optional `Related:` block; human list previews also neutralize controls. Never raw-`fputs` a stored body to a TTY. Entry JSON field order is `id,sync_id,key,body,tags,source,created_at,updated_at,expires_at,deleted_at,bin,version_vector[,links]`. `bin` is always `"live"|"expired"|"deleted"` (never null, never omit). No `"trash"` field. Stubs are `{id,sync_id,key,type,bin,preview}`. list/search/get JSON append `"links"` after `"version_vector"` (empty array if none). add/update/delete/purge JSON stay without `links`. Human list/search sixth column is neighbor **ids** only (cap 5 then `, +N`); suffix `[expired]` / `[deleted]` (never `[trash]`). Stub `type` is the 5-name vocabulary; stored kind stays 3. Default related/stubs omit deleted neighbors; expired neighbors stay visible. `--deleted` includes them.
- **Repair (plan 16 stage 6, E4.10 / E4.14):** `fts_only` is set in the adapter, after `capture_sqlite_error` has snapshotted the failing call. It is true for `SQLITE_CORRUPT_VTAB`, and otherwise only when `PRAGMA quick_check` passes on `entries`, `tags`, `entry_tags`, `entry_links`, `conflicts`, `settings`, and `bucket_prefixes` (`decision:plan16-repair-heals-index-damage`) and an active statement's SQL contains `entries_fts`. The check does not call `capture`. `remember repair search-index` runs that check first and refuses with `corrupt` / `fts_only` false when it fails; it never repairs on its own. A passing check tries `INSERT INTO entries_fts(entries_fts) VALUES('rebuild')` in one transaction, then `VALUES('integrity-check')`. If that fails, repair unlinks the index from `sqlite_schema` (defensive mode lifted only for that unlink), recreates it from `entries`, commits, and runs `VACUUM`. If both fail, `fts_only` is forced false so the message does not point back at repair. `COUNT(*)` from `entries` counts every bin. Repair checks its argv before the open and opens with `no_create` forced, so it never creates a brain (`decision:plan16-repair-no-create`). Human stdout is `rebuilt N`. JSON is `{"version":1,"action":"rebuilt","entries":N}`.
- **Path resolve:** `util_resolve_db_path` — `--db` > `REMEMBER_DB` > `~/.remember/remember.db`. Rejects `:memory:` and `file:` URIs (silent data-loss footguns through `sqlite3_open_v2`).
- **In-process façade (step 11):** the native GUI links the core instead of spawning the process (MAS sandbox can't spawn to reach `~/.remember`). One public entrypoint `remember_run(argc, argv, out, err)` in `remember_app.c` (`remember_app.h`) — the CLI pipeline minus `main`, so its `--json` bytes are identical to the CLI by construction (no parallel orchestration). The GUI builds argv incl. `--db <bookmarked path> --json`. `remember_main` wraps `remember_run` with stdout/stderr so `main.c` stays include-free (also dodges a Linux clang-tidy misc-include-cleaner quirk on `<stdio.h>`). Design log 002.
  - **Stream injection (`appio.c`):** command/help/error paths write through `app_out()`/`app_err()` (process-global pair, default stdout/stderr; `remember_run` swaps around one call). `ponytail:` global, not threaded — CLI is single-threaded and the GUI serializes via an actor. **Rule:** never `fprintf(stdout/stderr, …)` or `output_*(stdout, …)` directly in command code. Help and success output use `app_out()`. Error and warning lines go through `diag.c` only. `output.c` is unchanged (already `FILE*`-parameterized).
  - **Output write failures:** a command checks its own writes and reports `failed to write output` (`internal`). Short output stays in the stdio buffer, so its write fails only at the flush. `remember_run` therefore flushes `out` after the command and checks `ferror(out)`. A command that succeeded then returns 1 with `failed to write output`, plain or `--json-errors`. A failed command keeps its own single error line. Before this check, `remember list > /dev/full` exited 0 with nothing written (Linux). An in-process caller passes an `out` with a clear error flag, so an earlier failure on a reused stream is not reported again. A closed stdout that SQLite reuses for `/dev/null` (it never hands fds 0-2 to a database) is a successful write.
  - **`libremember` (`remember_core` target):** `REMEMBER_LIB_SOURCES` minus `main.c`; the only symbol a consumer needs is `remember_run`. Only `store_sqlite.c` includes sqlite still holds.
  - **`tags` command:** `store_tags` (distinct names + counts, INNER JOIN so only in-use tags appear, sorted `COLLATE BINARY`) → `output_tags_envelope`/`output_tags_human` → `cmd_tags`. Envelope `{"version":1,"count":N,"tags":[{"name","count"}]}`. Added for GUI tag chips + add-form autocomplete; on the CLI too so surfaces stay in sync.
- **Facade parity test (`test_facade.c`):** asserts `remember_run` stdout/stderr/exit byte-match the CLI subprocess for the same argv. Reads compare exact bytes; mutations mask `created_at`/`updated_at`/`deleted_at`/`sync_id`/`version_vector` (they legitimately differ between two writes). Guards the stream injection against future drift. Stage 6 covers `import` / `conflicts` / `conflict accept`. Stage 7 covers `--json-errors` on a miss and a `busy` lock.
- **Sync-path warning (step 09):** `util_path_looks_synced()` — substring markers `com~apple~CloudDocs`, `Dropbox`, `Google Drive`. Called from `main` after resolve, before `store_open`; one-line stderr; still opens. Store stays path-pure (no warning logic in adapter).
- **Format target (step 08):** CMake always defines `format`. On Apple, configure prepends `$(brew --prefix llvm)/bin` to `CMAKE_PROGRAM_PATH` so `clang-format` is found without a manual PATH. Reconfigure after installing llvm if the target previously reported missing.
- **Install (step 10):** `scripts/install.sh` builds/installs from a dedicated **Release** tree (`build-release/` by default, `REMEMBER_RELEASE_DIR`). It never reuses developer `build/` (often ASan) and refuses to install a binary that links asan/ubsan or whose `--version` differs from `CMakeLists.txt`. Every run does an incremental `cmake --build` (a no-op when current) so a stale `build-release/remember` is never installed; `--no-build` installs the existing binary. `--force-build` cleans only that Release tree. Skill install is `scripts/remember-install-skill` (symlink by default; `--copy` for clone installs). Destinations: existing `~/.grok` / `~/.claude` / `~/.cursor` skill trees, or `REMEMBER_SKILL_DIRS`. CMake installs the binary, the helper, and `share/remember/SKILL.md` — it does not write `$HOME` (Homebrew cannot).
- **User-facing strings are ASCII** (help, stderr, warnings). No Unicode dashes/arrows in CLI messages.
- **Sync markers tested:** unit coverage for `com~apple~CloudDocs`, `Dropbox`, `Google Drive` plus black-box stderr warnings for each (`tests/test_schema_config.c`, suite `schema_config`).
- **`store_add`:** keyless body-hash merge (union tags, keep source/created_at/body/`sync_id`) or keyed upsert (replace body, union tags, keep source/created_at/key/`sync_id`). Hitting an expired **or deleted** row revives it (clear `deleted_at`, apply incoming expiry or none). A write onto an existing row bumps `version_vector[local]` and `updated_at` together when the row changes, and moves neither when it does not (`decision:plan17-upsert-vv-rule`). A revive is one bump. FTS resync in the same transaction.
- **`store_list`:** takes `ListQuery` (tags AND via EXISTS, optional `source`/`key`, `limit`/`offset`, `sort`, `since`, `until`). Default order is `updated_at DESC, id DESC`; `STORE_SORT_CREATED` is `created_at DESC, id DESC`. The column comes only from `list_sort_column`, and each SELECT writes its own `ORDER BY` (`decision:plan17-inline-order-by`). `since` is `>=` and `until` is `<` on that column for list. Search asks for `updated_at` and ignores `ListQuery.sort`. The timestamp is bound. `since >= until` is an empty page. CLI owns default limit 20 / max 1000 / offset ≥ 0 and a **tag-filter cap** (`LIST_TAG_FILTER_MAX` 50) under the store bind budget (`LIST_BIND_CAP`); store uses values as given. COUNT + paged SELECT run in **one read transaction** so empty pages still report unpaged `total`. Date-only `YYYY-MM-DD` is the local start of that day; a year before 1900 is `invalid timestamp` (`decision:plan17-date-floor-1900`).
- **Soft vs hard delete (two functions; hard cannot default):** `store_soft_delete_by_id`/`_by_key` (live or expired) sets `deleted_at=now`, clears `expires_at`, keeps body/tags/key/`sync_id`/edges/FTS, bumps VV. `store_hard_delete_by_id`/`_by_key` requires `StoreBin` EXPIRED or DELETED (LIVE → INTERNAL). There is no device-count hatch. `STORE_ERR_NOT_SINGLE_DEVICE` stays in the catalog and no command returns it. FTS delete then `DELETE entries` (CASCADE links/tags) + orphan-tag GC. Unflagged CLI `delete` is soft; `delete --expired|--deleted` (and `--trash` ≡ `--expired`) is one-row hard. JSON `action:deleted` still names the snapshot.
- **Locator (get/delete/update/rekey):** shared validate/resolve in `cmd_locator.c` — exactly one of positional id / `--key` / `--sync-id` (canonical v7); reject `--source`. Unflagged delete locators are live **or** expired; other default locators stay live-only. Update adds its own flag parse (`--text`/`--tag`/`--clear-tags`/`--undelete`/`--if-version-vector`) on top of the same locator contract.
- **Stable exit codes:** `0` ok, `1` usage/error, `2` not found (neither bin), **`3` wrong bin**. Tokens (Round 7): `expired` | `deleted` | `not_expired` | `not_deleted`. `REMEMBER_WRONG_BIN = 3`. `store_status_to_exit` maps those four statuses to 3. Locators take `StoreBin` + `now`. `--expired`/`--deleted` are mutex; `--trash` aliases `--expired` with one stderr deprecation line per invocation.
- **`store_search`:** `SearchQuery` = FTS MATCH string + embedded `ListQuery` filters (tag AND / source / key / limit / offset / since / until on `updated_at`). `ListQuery.sort` is ignored. Shared SQL filter builder with list (`list_append_filters`). Rank `bm25(entries_fts)` then `updated_at DESC, id DESC`. **FTS5 requires the real table name** in `MATCH`/`bm25()` — aliases fail (`no such column`). Invalid FTS syntax → `STORE_ERR_QUERY` ("invalid search query"). COUNT + page in one read txn (same empty-page total contract as list).
- **CLI search:** same filter/paging parse path as list (`cmd_query.c`) + one required QUERY positional. `--since`/`--until` filter `updated_at`. `--sort` is validated, then ignored. Outer ASCII whitespace is trimmed (same set as normalize: space/tab/LF/CR/VT/FF); empty-after-trim → usage error `"empty search query"` (not FTS). Trimmed string is the FTS MATCH input (not tag/key-normalized). Human preview via `output_entry_human_line` (≤80 codepoints, plus related-id cell); JSON full bodies + link stubs via `output_list_envelope`. Page neighbors loaded with one `store_list_neighbors_for`.
- **`store_update`:** takes `StoreUpdate` (locate by id; independent `set_body` / `set_tags` / `set_expires` / `undelete` flags). Always bump `updated_at` and the local version vector on success. FTS resync in the same write txn. Optional `if_version_vector` is compared as parsed maps inside that transaction, after the bin check. A missing device counts as 0. Not equal is `STORE_ERR_STALE` and writes nothing. Malformed is `STORE_ERR_BAD_VV`. An integer that does not fit in int64 is malformed: SQLite would saturate the cast, so `2^63` would compare equal to `2^63-1`. A stored counter that does not fit compares unequal. A repeated device key is malformed, even when the copies agree: given is `STORE_ERR_BAD_VV`, stored is `STORE_ERR_STALE` (`decision:plan17a-duplicate-vv-key`). `vv_compare` is not the check (`decision:plan17-stale-guard-version-vector`). `undelete` clears `deleted_at` (CLI mutex with `--ttl`/`--expires`/`--clear-expires`; locator normally `--deleted`). Keyless body-hash collision → `STORE_ERR_CONFLICT` + conflicting id (CLI prints it on stderr). Tag replace deletes `entry_tags` then re-links + orphan GC. Never changes `source` or `key`.
- **CLI update:** lives in `cmd_locator.c` (same id/`--key` contract as get/delete) + opt-in `--text` / `--tag` / `--clear-tags` / `--undelete` / `--if-version-vector`. The flag's raw string reaches the store. Body load via shared `load_body` in `commands_common` (add + update). No positional body; `--source` rejected.
- **`store_purge`:** whole-bin hard wipe of EXPIRED or DELETED (LIVE → INTERNAL); no device-count hatch. `store_purge_trash` is the EXPIRED wrapper. CLI `purge` requires exactly one of `--expired`|`--deleted`; `purge-trash` ≡ `purge --expired` + deprecation stderr.
- **Import / conflicts (plan 14 stage 5, Round 7):** `store_import` opens the foreign path read-only inside the adapter (never `store_open`, never sidecar I/O, never reads `devices`). `user_version` < 4 → `STORE_ERR_SOURCE_TOO_OLD` (`source database is older than this remember`). `user_version` > 5 → `STORE_ERR_SOURCE_TOO_NEW` (`database is newer than this remember`). A missing `origin` column or missing `bucket_prefixes` / `settings` table is absent; those rows are not copied. Nothing reads `devices`. Merge by `sync_id`: missing → insert preserving sync_id/VV (unique key/hash clash → `conflicts` row); present → VV dominate / identical auto-merge / concurrent → `concurrent_vv` (pairwise-max local VV first; open conflict upserted by `(sync_id, reason)` — `decision:sync-import-reimport-idempotent`). **Clash re-import uses the same upsert** (`upsert_conflict`): a second import of the same `key_clash`/`hash_clash` refreshes snapshots and does not insert another row (envelope `conflicts` counts only new rows). Accept always unions incoming VV into survivors; `--keep both` remints sync_id on `concurrent_vv` (`{local:1}`), preserves incoming sync_id on clash (keyless if slot taken). Keyed clash: keep local key, demote imported until accept; never invent `key=sync_id` — contested keyless hash → `STORE_ERR_UNIQUE_TAKEN` (`decision:sync-import-hash-demote`). `--keep incoming` on `concurrent_vv` demotes any *other* unique occupant before apply (same hatch — never raw SQLITE unique). Reason tokens live in one place: `store_conflict_reason_str`. CLI: `import --from-db`, `conflicts`, `conflict accept --id N --keep local|incoming|both`. JSON import envelope: `action:imported` + inserted/updated/unchanged/conflicts counts (exit 0 even with conflicts). `VV_OUT_MAX` is 4096 so pairwise-max can absorb foreign device ids that live only in `version_vector`.
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
2. **Keep one public header.** `commands.h` stays the only face for `main`. Internal `commands_common.h` is not a public API — do not export it from the public surface or pull it into `main.c`. `remember_app.c` and the error-mapper tests include it so parse, open, and the code table share one router.
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
- Inspect-only cases may use `sqlite3` CLI. macOS CI puts Homebrew sqlite on `PATH` (`brew --prefix sqlite` is keg-only). `/usr/bin/sqlite3` cannot create or read `entries_fts` written by the vendored amalgamation, so a v4 fixture stays at `user_version` 0 and the next open dies with `table entries already exists`. `tests/test_store_sync.c` still checks FTS through `store_search`.
- **`--only` group matching is exact comma-tokens**, not substring (`key` must not select `key_gld`). See `group_selected` in `tests/test_main.c`.
- **Step gates should include every plan-required green test** for that step. Leaving green cases only in mixed red suites (`verification_edges`, full `key`) means ctest can pass while plan criteria silently regress.
- **Gate suite names:** green get/list/delete slices are `key_gld` and `verification_gld` (not `*_add`). When step 06/07 lands, extend or rename deliberately — do not drop criteria by editing an outdated name.
- **Single green-suite list:** `tests/gate-suites` is the only place to list `--only` suites for ctest `step_gate` and `scripts/ci-linux.sh` / GHA. Update that file when a step lands greens so CI grows with the code (do not hard-code stale filters in `ci.yml`).
- **Linux CI one-liner:** `./scripts/ci-linux.sh` (Docker ubuntu:24.04). Agents/humans should run it before claiming Linux-ready.
- **pre-push hook:** `./scripts/install-hooks.sh` sets `core.hooksPath=.githooks` so `git push` runs `ci-linux.sh`. Skip with `SKIP_LINUX_CI=1` or `--no-verify`.
- **Line coverage gate:** `scripts/check-coverage.sh` — clang coverage on `src/*.c`; require **100% functions** + **100% effective lines** (defensive pure-error exits excluded; see script). Complements `tests/gate-suites` (untested *logic* vs dropped *suites*). `REMEMBER_TEST_HOOKS` enables store malloc/prepare/step/exec fault injection for OOM/SQLite error paths.
- **Path policy tests belong next to the policy.** Rejecting `:memory:` / `file:` in `util_resolve_db_path` needs a black-box case, not only manual smoke.
- **stdin vs argv body limits must share one enforcer.** Raw stdin caps are memory guards only (`REMEMBER_STDIN_MAX`); `body_trim_copy` owns the post-trim 64 KiB rule for both paths.
- **Literal body `"-"` vs stdin:** `load_body(body, dash_is_stdin, …)` — when `dash_is_stdin` is true, `"-"` reads stdin (CLI convention); when false, `"-"` is a one-character body. Callers: `add` sets literal after `--` (and top-level `cli_parse` **forwards** `--` into rest_argv once a subcommand is chosen, so `add -- -` reaches the command); `update` uses `--text=-` for a literal hyphen and `--text -` for stdin.

## Process

- After **each implementation plan step**: run a strict review (see skill `remember-step-review` / project checklist).
- Append step-specific learnings here and to the step plan’s “Review notes” section.
