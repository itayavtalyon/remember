# 22 — Export JSON, Markdown, and CSV

## Goal

`remember export` writes the brain to JSON, Markdown, or CSV. A person can read the files. Plan 23 can read them back. The command needs no account, no network, and no quota.

```
remember export --format json|md|csv --out PATH [--bins live,expired,deleted] [--force]
```

## Status

READY. DESIGN-PENDING D-04 is answered (2026-10-25).

Memory rows, links, atomic write, slugs, and the CSV memory header are specified. Start those stages.

DESIGN-PENDING D-04 is answered (2026-10-25, 007 ST-8.3). The settings stage may start after plan 20's brain settings rows exist. Never write a CSV `#settings` row. Do not write the E12.9 object.

## Source of truth

- [`design-logs/006-ui-overhaul-engine.md`](../design-logs/006-ui-overhaul-engine.md) §5, except where this plan names an amendment.
- [`design-logs/008-json-envelope.md`](../design-logs/008-json-envelope.md) JE-1.5, JE-3.5, JE-8.2, JE-8.3.
- [`design-logs/007-settings.md`](../design-logs/007-settings.md) ST-8.3 (the brain settings document, D-04).
- Locked decisions D7, D12, D17, D18, D19, D25, D29, D30, P5, and P6 from the old export plan, restated below. They still apply.

006 E12.9 is dead. 006 E6.2's `sync_id` and `source` are replaced by JE-8.3.

## Context

`--bins` defaults to all three of `live`, `expired`, and `deleted`. A comma list, no duplicates, non-empty. Otherwise `usage` and `invalid --bins (use live, expired, deleted)`.

No account check. No network check. No quota check.

Each memory exports `id`, `key`, `body`, `tags`, `created_by`, `updated_by`, `created_at`, `updated_at`, `expires_at`, `deleted_at`, `bin`, `version_vector`, and its links. No `local_id`. No `sync_id`. No `source`. `bin` is written for readers. Import ignores it (plan 23).

Links in the JSON top-level array are `{from, to, kind, created_at, updated_at}` with `id` ends (the UUID). Export every `entry_links` row whose two ends are in the export. A `related` edge uses the stored ends.

The device-id sidecar and `conflicts` are not exported.

JSON success envelope on stdout, after the file is written, is `remember-message` 1 from plan 21. `data` keeps the old action fields with these names: `action` `exported`, `format`, `path`, `count`, `links`. `path` is absolute: join with the cwd, then lexical `.` and `..` normalization, no symlink resolution (D7). Human line: `exported N memories, M links to <path>` (D19).

File errors: ENOSPC is `disk_full`. EACCES or EROFS is `read_only`. ENOENT on the parent is `cannot_open` with `reason` `missing`. Any other errno is `io_error`. `detail.path` is the out path. `detail.errno` is set. These codes already have plan 16 sentences. Keep those sentences.

Missing `--format`: `export requires --format (json, md, or csv)`. Bad value: `invalid --format (use json, md, or csv)`. Missing `--out`: `export requires --out PATH`. A repeated flag: `duplicate <flag>`.

Atomic write. Temp name is `<PATH>.tmp-<16 lowercase hex>`. Write the temp file, then `rename` it onto PATH. PATH exists and there is no `--force`: write nothing, exit 1, `usage`, `output already exists: <path>`. `--force` replaces a file by that rename. Remove the temp name on every failure path (P5).

Markdown `--force` on an existing folder (D17): write `<PATH>.tmp-<rand>/`, rename the existing PATH to `<PATH>.old-<rand>/`, rename the temp directory onto PATH, then delete the old folder recursively. A crash leaves the old or the new folder plus a stray sibling. It never leaves a half-written PATH.

Determinism (D18), with the new field names: one compact JSON line plus a final newline. Tags sorted by code point. `version_vector` keys sorted. Top-level `links` sorted by (from id, to id, kind). The same orders inside Markdown and CSV cells. `created_by` and `updated_by` use the actor field order `kind`, then `id`.

JSON document (amends E6.a). UTF-8, no BOM. Members in this order:

1. `format` is `remember-export`.
2. `version` is 1.
3. `brain_id` is `brain_meta.brain_id`.
4. `exported_at`.
5. `engine` is `REMEMBER_VERSION`.
6. `schema` is the open brain's `user_version` (6 after plan 19).
7. `entries`.
8. `links`.
9. `settings`. The brain settings document of 007 ST-8.3, nested as a JSON object. A brain with no settings rows writes the document with `"settings":[]`. Never omit the member once the settings stage lands. Before that stage, omit the member and say so in the test name.

Entry member order: `id`, `key`, `body`, `tags`, `created_by`, `updated_by`, `created_at`, `updated_at`, `expires_at`, `deleted_at`, `bin`, `version_vector`. Entries sort by `created_at`, then `id`.

Markdown is a folder: `README.txt`, `memories/`, `expired/`, `deleted/`. Add `settings.json` at the folder root when the brain has settings rows: the brain settings document (007 ST-8.3), then one newline. When the brain has no settings rows, write no `settings.json`. The three bin folders always exist, including empty folders for a bin `--bins` excluded (D29).

`README.txt` is exactly this sentence plus one newline (D30):

```
This folder is a Fortkeeps export. Import it with: remember import --from-md <folder>
```

File name: `<slug>--<last 12 hex of id>.md` (E6.b2, D12). Slug is the key, else the body's first line when it starts with `# ` (text after that prefix, ASCII-trimmed, empty means skip), else the first 6 words. First 6 words: collapse whitespace and control runs to one space, split on spaces, take the first 6, join with one space. Then ASCII lowercase, every run outside `[a-z0-9]` becomes `-`, trim `-`, max 48 characters, trim `-` again after the cut, empty becomes `memory`. Reuse the plan 18 helpers in `search_terms.c`.

Markdown file: a line `---`, then one field per line in the order `remember_export`, `id`, `key`, `tags`, `created_by`, `updated_by`, `created_at`, `updated_at`, `expires_at`, `deleted_at`, `version_vector`, `links`, each `<field>: <JSON>`, then a line `---`, then the body bytes, then one newline. `remember_export` is the JSON number `1`. Null is JSON `null`. `links` in the file is a JSON array of `{to, kind, created_at, updated_at}` with no `from`, for edges whose stored `from` is this memory (E6.b4, D25).

CSV: RFC 4180, UTF-8 with BOM, CRLF, every field quoted, `"` doubled. Header, exact:

```
id,key,body,tags,created_by,updated_by,created_at,updated_at,expires_at,deleted_at,bin,version_vector,links
```

Actor, tag, vector, and link cells hold JSON. NULL is an empty cell. The `links` cell uses the Markdown shape (D25). A `related` edge appears on its stored `from` end only. Never add a `#settings` row (008 JE-8.3). Write the brain settings document, then one newline, to `<CSV basename>.settings.json` in the directory that holds the CSV file (`brain.csv` → `brain.settings.json`; the basename drops the last extension), with the same temp-name, rename and `--force` rules. When that file exists without `--force`, write neither file and fail with `usage`, `output already exists: <path of that file>`. When the brain has no settings rows, write no settings file and leave an existing one alone (007 ST-8.3).

Bin folders: `live` → `memories/`, `expired` → `expired/`, `deleted` → `deleted/`.

No zip. The engine writes no zip and reads no zip.

## Locked decisions

| ID | Rule |
|---|---|
| R3-00b / G2-16 | `remember-export` version 1. No old-name reader. No `sync_id`. No `source`. |
| JE-8.3 | The CSV header above. Markdown front matter uses the same names. |
| ST-8.3 | Brain export carries brain settings rows only, including tombstones, never user rows, in a nested `remember-settings-export` version 1 document. JSON always has the member. Markdown and CSV write `settings.json` only when rows exist. |
| D17 | Folder swap for Markdown `--force`. |
| D19 | Human line and the file-error codes. |
| E6.1 | No account, network, or quota. |

## Prerequisites

Plans 19 and 21 are merged. `id`, actors, and the envelope writer exist. Plan 20's brain settings rows exist before the settings stage runs. Memory export does not need plan 20.

## Scope

In: the export command, one read transaction, JSON and Markdown and CSV memory writers, atomic write, `--force`, D19 errors, help, a facade byte-match test that masks `exported_at`.

Out: import (plan 23), zip, sidecar, conflicts, user rows, the daemon.

## Files

- `src/cmd_export.c` in `REMEMBER_LIB_SOURCES` and `-Wcast-qual`.
- `src/export_format.c` and `src/export_format.h`. Pure. Slug, front matter, CSV quoting, document assembly on top of `output.c` escaping. No file I/O.
- `src/fileio.c` and `src/fileio.h`. Temp file, rename, D17 folder swap, recursive delete, errno to code. The only export file I/O.
- `store_export_snapshot` declared in `src/store.h` and implemented in `src/store_sqlite.c`.
- Tests: `tests/test_export.c`, suite `export`. A fixture builder `tests/fixture_roundtrip.c` that plan 23 reuses. Add settings rows to that fixture in the settings stage, including one tombstone.

Do not add a settings-specific exporter module. Format the brain settings document inside `export_format.c`.

## Must-pass tests

| Test | Proves |
|---|---|
| fields | A memory round-trips its JE-8.3 fields except `settings`. No `local_id`, no `sync_id`, no `source`. |
| bins | `--bins live` writes only live memories. Markdown still has the three folders. |
| force | Existing PATH without `--force` writes nothing. `--force` replaces a file. Markdown `--force` leaves no half-written directory after a simulated crash between the two renames. |
| determinism | Two exports of the same brain match byte for byte except `exported_at`. |
| csv | BOM, CRLF, every field quoted, header exact, NULL is empty. No `#settings` row. |
| readme | `README.txt` is the locked sentence plus one newline. |
| errors | Missing flags use the locked usage sentences. A full disk maps to `disk_full`. |
| human | `exported N memories, M links to <path>`. |
| settings | The one-tombstone brain writes the 007 ST-8.3 example bytes (with fixed `exported_at`) as the JSON `settings` member, as Markdown `settings.json`, and as `brain.settings.json` next to `brain.csv`. A brain with no settings rows writes `"settings":[]` in JSON, and no settings file for Markdown or CSV. No user row appears. |
| settings force | An existing `brain.settings.json` next to `brain.csv` without `--force` writes neither file and fails with `usage`. |

008 verification 12 (export then import) is plan 23.

## Stages

1. Snapshot reader and JSON memory document without `settings`. Red test first.
2. Atomic write, `--force`, and D19 errors.
3. Markdown folder, slug, front matter, README. No `settings.json`.
4. CSV memory rows. No `#settings` row.
5. Facade test and help.
6. Settings: the brain settings document in JSON, Markdown and CSV (007 ST-8.3). Needs plan 20's brain settings rows.

## Cloud contract

- `GET /v1/export` returns JSON, Markdown, or CSV in every account state, including lapsed. Quota errors never block export (Cloud 001, M-12, M-14).
- The body uses `remember-export` version 1 and the CSV header in this plan.
- No `local_id`. `id` is the UUID. Actors are `created_by` and `updated_by`.
- User settings rows are not in the export. Brain settings rows are, as the 007 ST-8.3 document.
- Cloud v1 does not offer end-to-end encryption.

## How to work

Same loop as plan 19. Load `standard:coding` and the review docs named there.

Branch from clean `main` after plans 19 and 21. One branch. One pull request.

Do not change the settings container. 007 ST-8.3 names the bytes.

Quality gate: plan 19's commands. Coverage 100/100.

## Definition of done

- [x] Memory export tests are green.
- [x] Settings bytes match 007 ST-8.3.
- [x] No E12.9 object anywhere in the output.
- [x] Gate green. Second review clear.
- [ ] INDEX row 22 stays unticked until C-44 is approved. Stage 6 and the review below are done.

## Second review handover

Probes:

1. Export JSON and look at the first keys. [`format`, `version`, `brain_id`, then `exported_at`]
2. Confirm no `sync_id`, `source`, or `local_id`.
3. Export Markdown and confirm `README.txt` bytes, including the final newline.
4. Export CSV and confirm the header line and the BOM.
5. Export a brain with no settings rows. [JSON has `"settings":[]`, no `settings.json` in the folder, no `<basename>.settings.json` beside the CSV]
6. `--force` on a Markdown folder, kill between the two renames if the test shim allows it. [old or new folder intact]

Do not re-open: no zip, atomic rename, RFC 4180, slug rules, no user rows.

## Implementation Notes

Stages 1 to 6 are built (2026-11-09 to 2026-11-10). Plan 20's brain settings rows are on main (PR #59, merged 2026-11-10 as 7244fa4, plus 30341ac). Stage 6 nests the brain settings document. The hand-over below is the order that built it.

Grill decisions (Itay, 2026-11-09; each filed in Remember):

- `decision:plan22-export-bin-flags`: the V1-09 flags replace `--bins`. Live by default, `--include-expired`, `--include-trash`, and `--exclude-archived` (parsed, duplicate-checked, no effect until the archive bin exists). This supersedes the `--bins` line in the usage block above.
- `decision:plan22-export-bins-header`: every format names its bins. JSON has `bins` after `schema`, a count per exported bin in live, expired, deleted order. `README.txt` has a second line `Bins: live N, expired N, deleted N.` with the exported bins only. CSV writes `<CSV basename>.manifest.json` (the header members `format` to `bins`, then a newline) with the same temp, rename and `--force` rules as the CSV file.
- `decision:plan22-export-file-error-copy`, `decision:plan22-export-out-is-folder`, `decision:plan22-export-reply-type` (`memory.export.result`, amends JSON-STYLE §1.7a), `decision:plan22-export-human-count-agrees`, `decision:plan22-export-file-mode` (0600 files, 0700 folders), `decision:plan22-export-no-create`.
- New sentences are C-44 drafts in `COPY-PENDING.md`, including the `help export` block.

Code:

- `store_export_snapshot` reads brain id, `user_version`, entries, live links, and brain settings rows in one transaction. The bin is `store_bin_of` at `exported_at`. A link is exported when both ends are. Settings rows, tombstones included, are freed with the snapshot.
- `export_format.c` is pure: the JSON document, Markdown name, file and README, CSV rows, the manifest. `output_export_entry_json` is `output_entry_json` without `local_id`. Version-vector keys are sorted at export. The Markdown name needs no allocation: `search_words_end` (search_terms.c) finds the end of the first 6 words with the D12 gap rule that plain text uses.
- `fileio.c` holds every file call: temp siblings `<path>.tmp-<16 hex>`, `O_EXCL` 0600 writes (`O_NOFOLLOW` was dropped: strict-POSIX gcc `-fanalyzer` does not declare it, and `O_EXCL` already refuses a symlink) with fsync, rename, the D17 folder swap with `<path>.old-<16 hex>`, and tree removal.
- Test CLI seams: `REMEMBER_TEST_EXPORT_FAIL=<site>[#N][:errno]` (sites random, alloc, join, fdopen, write, close, rename, swap; N defaults to 1) and `REMEMBER_TEST_EXPORT_CRASH=1` (exit between the D17 renames; `exit`, not `_exit`, so the coverage build keeps its profile).

Known limits (ponytail, recorded):

- Without `--force` the existence check runs before the write, so a file created at `--out` in between is replaced by the rename. Closing that needs `renamex_np`/`renameat2` with no-replace, which differ by platform.
- The parent folder is not fsynced after the rename.
- A CSV export renames the CSV, then the manifest, then `<basename>.settings.json` when the brain has rows. If a later rename fails, the earlier new file stands beside the old later file. Renames inside one folder do not fail in practice. With no rows the settings sibling is not opened and an existing one is left in place.
- Two memories with the same slug and the same last 12 hex of their ids would collide (EEXIST, `io_error`). The ids are UUID v7 with 48 random bits at the end.

Commands: `cmake --build build -j8`, `./build/remember_tests ./build/remember --only export`, `./build/remember_store_tests`, then the gate (`ctest --test-dir build`, `./scripts/check-coverage.sh`, `CI=true BUILD_DIR=build REMEMBER_SCAN_BUILD=1 REMEMBER_IWYU=0 ./scripts/lint-all.sh`, `cmake --build build --target leaks-macos`).

### Hand-over (2026-11-10)

State: branch `plan/22-export`, commit 6f0fadb (stages 1 to 5, one commit), base 9495f48. Not pushed, no PR. The macOS gate was green on 6f0fadb. The Linux pre-push run (Docker, about 20 minutes) has not happened. Never push, merge or open a PR without asking Itay.

Do these in order.

1. **Bring in main (7244fa4).** `git merge-tree --write-tree origin/main HEAD` shows 7 conflicts: `CMakeLists.txt`, `implementation-plans/COPY-PENDING.md`, `src/cli.c`, `src/cli.h`, `src/commands.h`, `src/commands_common.h`, `src/remember_app.c`. Each is two sides adding to the same list: the `export` and `settings` commands, their help blocks, test sources, and the C-44 drafts next to plan 20's drafts. Keep both sides. In `commands_common.h`, keep this branch's `REMEMBER_VERSION` fallback (gcc `-fanalyzer` needs it). `store.h`, `store_sqlite.c`, `diag.c/.h`, the specs and the engineering notes merge cleanly. Rerun the full gate before going further.

2. **Stage 6: settings (007 ST-8.3, the example bytes are at `design-logs/007-settings.md` line 367).**
   - Read: `store_setting_export(brain, NULL, SETTING_LAYER_BRAIN, &rows, &n, err, errlen)` (main, `store_sqlite.c`). Brain layer only, tombstones included, sorted by key, version-vector keys sorted in SQL, and it never opens `user.db`. It runs outside the `store_export_snapshot` transaction. Put the brain settings read inside that transaction: `export_layer` is static in the same file, so add `rows`/`nrows` to `StoreExportSnapshot` and free them in `store_export_snapshot_free`. If you choose not to, record the gap under known limits.
   - Format: the plan says no settings-specific module; the document goes in `export_format.c`. Move `export_ms`, `write_export_row` and `write_export` out of `src/cmd_settings.c` (all static there) into `export_format.c`. Make `cmd_settings_export` call the moved writer, so the ST-8.2 bytes have one owner. `write_export` ends in `"]}\n"`. Split it so the nested JSON member gets no newline, and the file writers (`settings export`, Markdown `settings.json`, CSV settings file) add one. `export_ms` needs `setting_by_key`, `quoted_plain` (static in cmd_settings.c) and `norm_duration_canonical`. Move `quoted_plain` with it or expose it; keep `export_format.c` free of I/O. `value_bytes` is not needed: main has no size setting type yet.
   - JSON: `"settings":<document>` always, last member after `links`. No rows writes `{"format":"remember-settings-export","version":1,"exported_at":"<export exported_at>","settings":[]}`. The inner `exported_at` is the export's `exported_at`.
   - Markdown: `settings.json` (document, then a newline) at the folder root only when rows exist. It is one more file inside the temp folder before the D17 swap, so no new rename rules are needed.
   - CSV: `<CSV basename>.settings.json` beside the CSV (`brain.csv` gives `brain.settings.json`; the same basename rule as `manifest_path`, so share the helper). Write it only when rows exist. Add it to the `OutFile` list so `refuse_existing` and `write_files` treat it like the manifest. When it exists without `--force`, write nothing and fail with `usage`, `output already exists: <path>` (the existing copy). With no rows, leave an existing settings file alone and do not delete it.
   - Never write a CSV `#settings` row or the 006 E12.9 object.
   - Tests (`tests/test_export.c`): rename `export_json_every_bin_without_settings_member` and any other `*without_settings*` name. Add the one-tombstone fixture to `tests/fixture_roundtrip.c` (plan 23 reuses it). Assert the ST-8.3 example bytes with a fixed `exported_at` in all three formats. Cover the empty case (`"settings":[]`, no Markdown or CSV settings file, an existing CSV settings file untouched), "settings force" (an existing `brain.settings.json` without `--force` writes neither file), and that no user row appears even with a `user.db` holding rows. Extend the `REMEMBER_TEST_EXPORT_FAIL` sweep so it reaches the new file, and run the mutation pass again on the new branches.
   - Specs: the JSON-STYLE brain export row and the JSON-ENVELOPE export payload gain `settings`; check `specs/SETTINGS.md` (new on main) still matches. Add a Stage 6 entry to the Review Notes.

3. **Copy round.** The C-44 drafts in `COPY-PENDING.md` (the file error lines and the `help export` block) are not approved. Any new sentence is a grill with Itay. Play `afplay /System/Library/Sounds/Funk.aiff` first, then record it in `COPY-PENDING.md`.

4. **Whole-plan deep review** (Remember `workflow:whole-plan-deep-review`). Record it in the Review Notes. Then move the `decision:plan22-export-*` cards from `state:accepted` to `state:implemented`.

5. **Close.** Tick the Definition of done boxes. Tick INDEX row 22 only after stage 6 and the review. Ask Itay before pushing (the hook runs the Linux CI) or opening the PR.

Notes for the next coder:

- Every commit ends with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and every stage has a dated review note.
- Set `export PATH="$(brew --prefix llvm)/bin:$PATH"` before the lint.
- Use `REMEMBER_TEST_FIXED_IDS=N` for frozen ids and the clock (2026-01-01T00:00:00.000Z) and `REMEMBER_TEST_FAIL_READ=N` for SQLite read faults. The ST-8.3 example has its own `exported_at` (2026-10-25T18:00:00.000Z), so the bytes test compares the settings document written by the pure formatter with an explicit `exported_at`, or swaps that timestamp before comparing.
- clang-format would not settle on long CSV literals. That is why the tests build CSV rows with macros.

## Review Notes

Stage flow: in-session deep review after each stage, as plan 21. Local Bugbot is not installed. Stages 1 to 5 went into one commit: the stages share `cmd_export.c`, `export_format.c` and `tests/test_export.c`, and splitting them afterwards would make commits that do not build alone.

### Stages 1 and 2 deep review (2026-11-09, in session)

- **Important, fixed (grill):** the plan's `--bins` was stale against V1-09. Itay chose the `--include-*` flags (`decision:plan22-export-bin-flags`).
- **Important, fixed (grill):** usage tests on a missing brain created it. Export now forces no-create, and `cmd_export_reject_args` runs before the open, as repair does.
- **Nit, fixed:** a 300-byte single path component hit `ENAMETOOLONG` (`io_error`) in the error test; it now uses two 150-byte folders.

### Stage 3 deep review (2026-11-10, in session)

- **Important, fixed:** the crash test's last `--force` ran after the path was gone, so it never swapped a real folder. Coverage showed `fileio_replace_dir` at 25%. The test now swaps once cleanly (no sibling left) before the crash.
- **Important, fixed:** `export_md_name` could fail on allocation and its callers set `ENOMEM` by hand. It now writes into a fixed buffer and cannot fail. Invalid UTF-8 (never stored) slugs the whole body, and the slug test locks that.
- **Nit, fixed:** the test leaked `harness_side_path` results (`write_side` frees them).

### Stage 4 deep review (2026-11-10, in session)

- **Important, fixed:** every file failure path had no test. `export_every_file_fault_keeps_the_old_output` runs each fault site for json, csv and md with `--force` over an earlier export: exit 1, the D19 code, the earlier file byte-identical, no sibling left. `export_store_failure_writes_nothing` sweeps `REMEMBER_TEST_FAIL_READ` until the export succeeds.
- **Nit, fixed:** `write_files` tracked the failing file with a comma increment; it is a plain index now.

### Stage 5 deep review (2026-11-10, in session)

- Facade parity (`facade_export_matches_cli`): CLI and `remember_run` write the same json, csv and manifest bytes with `exported_at` masked, and the same stdout. `help export` matches too.
- Specs: JSON-ENVELOPE gains the `export` reply row and payload; JSON-STYLE §1.7a/§1.7b and the brain export row carry the reply type, `bins`, and the manifest.
- **Important, fixed:** coverage found the Markdown `mkdir` failure untested. `export_md_missing_folder` locks `cannot_open`, reason `missing`, with no sibling left. The memstream close in the CSV JSON cell now shares one defensive tail.
- **Nit, fixed (lint):** gcc `-fanalyzer` builds with strict POSIX headers, where macOS hides `O_NOFOLLOW`. The flag is gone: `O_CREAT | O_EXCL` already refuses any existing path, a symlink included. The same pass could not see `REMEMBER_VERSION`; its fallback moved from `remember_app.c` to `commands_common.h`, so both users share it.
- **Nit, fixed (lint):** clang-tidy and cppcheck findings in the new code and tests (named buffer sizes, no one-letter names, no `bool` from `&&`, a possible NULL from `strdup`). clang-format did not settle on the long CSV literal, so the expected CSV is one macro per row.
- **Nit, fixed (lint):** cppcheck saw every fault site as a constant condition in the release configuration. A release build now gets `#define FAULT(site) false`, and only the test CLI compiles `fault()`, the way `REMEMBER_TEST_FIXED_IDS` is fenced. `export_vv_sorted` had no outside caller and is now static.

Mutation pass (2026-11-10, run on a copy of the tree, export suite; 16 mutants, all killed):

- `!a->force` inverted → `export_force_existing_path`. Folder check skipped → `export_out_is_a_folder`.
- Entry bin filter inverted, or the link filter `||` → `&&` → `export_json_default_is_live_only` (alpha links to an expired and a deleted memory).
- `SLUG_MAX` 47 and the trailing-dash trim removed → `export_md_slug_rules`. `SLUG_WORDS` 5, and `search_words_end` stopping one word late → `export_md_folder_every_bin`.
- `write_own_links` walk-back removed → `export_md_folder_every_bin`, `export_csv_every_bin` (bsearch lands on alpha's third link).
- Version-vector sort reversed → `export_json_every_bin_empty_settings`.
- CSV quote doubling removed, NULL written as `""` → `export_csv_every_bin`. Manifest `dot > base` → `>=` → `export_csv_manifest_names` (`.csv`).
- The D17 restore rename removed, the temp removal removed → `export_every_file_fault_keeps_the_old_output`.
- Not mutated, justified: dropping `O_EXCL` on the temp. Only a 64-bit random name collision reaches it.

### Stage 6 deep review (2026-11-10, in session)

The brain settings read runs inside the snapshot transaction (`export_layer` on the brain layer). `export_settings_write` is the one ST-8.2 writer: `settings export` and the two files pass a trailing newline, and the nested JSON member does not. Markdown writes `settings.json` in the temp tree only when rows exist. CSV adds `<basename>.settings.json` to the `OutFile` list after the snapshot, and only then, so a brain with no rows leaves an existing sibling alone.

Tests that lock it: `export_settings_tombstone_matches_example` (the 007 example bytes in all three formats, no `sync_id`, `source`, `local_id`, `merge`, or a CSV `#settings` row), `export_settings_empty_omits_the_files`, `export_settings_existing_file_without_force`, `export_settings_skips_user_rows`, `export_settings_file_fault_keeps_the_old_output`. The CSV fault asserts the error path is `brain.settings.json`, so a fault on an earlier file cannot pass as the settings fault.

- **Nit, fixed (lint):** the move of `quoted_plain` dropped `ASCII_UNIT` and `ASCII_DEL`. The trailer used a bool as a ternary condition. The settings error buffer was a bare `32`. Two tests named a path `md`. `getenv` and `setenv` in the user-row test had no `NOLINT`.
- No new user-facing sentence. C-44 stays unapproved, so INDEX row 22 stays unticked.

Simplify, considered and not taken. `settings_scope` repeats `scope_name` (about six lines); a shared public name would be a new API for one word. `export_brain_settings` is the error map into `STORE_ERR_OOM` or `STORE_ERR_SQLITE`, and inlining it lengthens `store_export_snapshot`. `settings_writer` exists because `FileWriteFn` has no clock. `trailing_newline` is the split the hand-over required.

Stage 6 mutation pass (2026-11-10, export suite unless a settings test is named; 12 mutants, all killed):

- CSV `nsettings > 0` inverted → `export_settings_tombstone_matches_example`. Markdown `nsettings > 0` inverted → `export_md_folder_every_bin` and the tombstone test.
- Scope forced to `"user"` → the tombstone bytes. File newline dropped → the same test. Nested newline forced → `export_json_every_bin_empty_settings`.
- The settings read removed → the unused static fails `-Werror`. The settings-file `refuse_existing` removed → `export_settings_existing_file_without_force`.
- `hit != want` inverted → `export_every_file_fault_keeps_the_old_output`. The `#N` parser ignored → `export_settings_file_fault_keeps_the_old_output`.
- The duration check inverted → `export_prints_the_settings_document` and `export_scope_selects_one_layer` (no `value_ms`). `deleted_at` branches swapped → the tombstone bytes (`null` instead of the stamp).
- The out-of-memory `strcmp` inverted → `export_store_failure_writes_nothing` (the line becomes `out of memory`, and the test wants `database error:`).

### Whole-plan deep review (2026-11-10, in session)

This is the hand-over's whole-plan pass. Local Bugbot is not installed. The passes were the review lenses, then simplicity, then an adversarial pass. SQLite stays in the adapter, file calls stay in `fileio.c`, and the settings document has one writer. The new rows are freed with the snapshot. `beside()` results are freed on every exit, including the manifest allocation that returns before the snapshot. JSON strings go through `output_json_string`. `value`, `updated_by`, and `version_vector` are embedded raw because `export_layer` already ran them through `json()`; a value that is not JSON fails that read, which `export_fails_on_a_hand_edited_value_that_is_not_json` already locks for `settings export`.

The second-review probes are the suite. Header order, the missing sync fields, `README.txt`, and the CSV header and BOM are the stage 1 to 5 tests. Probe 5 is `export_settings_empty_omits_the_files`. Probe 6 is `export_md_force_survives_a_crash`. User rows, tombstones, an existing settings file, and a fault on the new file are the stage 6 tests.

No grill. The known limits are unchanged in kind. The CSV rename window is now three files when the brain has rows.

Grades. The implementer, 8/10: the locked decisions and the example bytes match, and the stage 6 lint misses were the standing bar (named constants, no bool ternary, `NOLINT` on `getenv`). The previous in-session reviews of stages 1 to 5, 8/10: those locks held, and they did not see stage 6. This pass found no hole in the file, slug, or bin rules.

Pointer, for the bar: when a fault token grows `#N`, the test has to name the file that failed. An earlier fault leaves the same old bytes.

Linux CI (the pre-push hook) failed the first push: `list_tree` used `DT_REG`, which `_POSIX_C_SOURCE=200809L` does not declare. It now uses `stat` and `S_ISREG`. The Linux tidy and include checker also wanted a null check before `strstr`, a `NOLINT` on the in-place timestamp copy, `<stdbool.h>` in `export_format.h`, and no `<sys/types.h>` in `fileio.c`.
