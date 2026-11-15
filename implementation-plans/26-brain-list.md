# 26 — Brain list

**Module:** `src/brain_list.c` and `src/brain_list.h`. The commands call this module. Command parse stays in the existing CLI parser.
**Needs:** plan 19 (merged), plan 25 (this plan's code is cut from the `plan/25-plugins` branch after plan 25 merges, G1 A).
**Blocked on:** nothing.

## Goal

Remember records every brain it opens read-write, tells a copy from a move, and lists or prunes that record.

## Source

- `decision:g-01` and `decision:2026-10-22-m-04` (the data directory).
- `decision:g2-08` (what the file lists, when a row is added, temp file plus rename, advisory lock, prune).
- `decision:g2-05` (copy mints a new `brain_id` and warns `brain_id_cloned`; otherwise the path is updated).
- `decision:brain-id` (`brain_id` is a UUID v7 minted at create).
- `decision:2026-10-22-m-19` (the command tokens `remember brain list` and `remember brain prune`).
- `decision:2026-11-11-brains-json-format` (P7 A): the JSON document below.
- `decision:2026-11-11-brains-json-data-dir` (G2 A): `brains.json` follows the data dir; `default-db` stays in `~/.remember`.
- Sentences: [`COPY-PENDING.md`](COPY-PENDING.md) C-38 and the C-01 `brain_missing` line. Itay approved that file on 2026-10-25.
- Spec: `specs/BRAIN-LIST.md` (new).

`decision:2026-11-11-brain-command-names` supersedes the token names in `decision:g2-08`. The commands are `remember brain list` and `remember brain prune`. The coder does not implement `remember brains`.

Starting code: the `plan/25-plugins` branch holds `src/brains.c`, `src/brains.h`, `src/brain_note.c`, `src/brain_note.h` and `src/cmd_brain.c` changes. Rename `src/brains.c` to `src/brain_list.c` and `src/brains.h` to `src/brain_list.h` with `git mv`, rename every `brains_` function prefix to `brain_list_`, and keep `brain_note.c` as is. Then make every box below green.

## Out of scope

Changes to `remember brain new-id`. That command already exists on main (`src/cmd_brain.c`, "Replace the brain id"). This plan does not change it. The `brain` subcommand parser gains `list` and `prune` next to it. The daemon, sweep, and plugins are other plans.

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md).

Tests live in `tests/test_brain_list.c`, suite `brain_list`, and that suite is added to `tests/gate-suites`. SQL stays in `src/store_sqlite.c`. This module reads and writes the JSON file. It does not open SQLite.

## What must be true

- The file is `<data dir>/brains.json`. The data dir is `$XDG_DATA_HOME/remember` when `XDG_DATA_HOME` is set, and `~/.remember` when it is not.
- The file mode is `0600`, the same mode as `user.db` in that directory.
- The document is one JSON object:

```json
{"format":"remember-brains","version":1,"brains":[{"path":"/absolute/path","brain_id":"01234567-89ab-7def-8123-456789abcdef","added_at":"2026-11-11T12:00:00.000Z"}]}
```

- `path` is the canonical absolute path (`realpath`). `brain_id` is the brain's id. `added_at` is `YYYY-MM-DDTHH:MM:SS.mmmZ`.
- A read-write open adds a row on the first open of that `brain_id`. Remember writes `<data dir>/brains.json.tmp`, then renames it over `brains.json`, while it holds an advisory lock on `<data dir>/brains.json.lock`.
- A failure to write the file leaves the previous file in place. The open's exit code does not change. This plan prints no new sentence for that failure.
- The same `brain_id` at another path, when that path still has that brain, is a copy. Remember mints a new `brain_id`, adds this path, and warns `brain_id_cloned` with the C-38 line: `remember: warning: brain "<path>" looks like a copy of another brain, so it got its own new id, <id>. The original keeps the old id. Both now sync and run hooks as separate brains.`
- The same `brain_id` at another path, when that path has no such brain, is a move. Remember changes the stored path and keeps the id. It prints no warning.
- No row has this `brain_id`: Remember adds a row.
- `remember brain list` prints each row. A row whose path has no brain stays in the file and prints the C-01 line: `remember: warning: brain "<path>" is in the list but isn't on disk. If you moved it, open it once from its new place. Otherwise run 'remember brain prune' to remove it from the list.`
- `remember brain prune` removes each row whose path has no brain. It prints no extra sentence.

- **Warnings carry a `fix` (E3 B, `decision:2026-11-12-e3-warnings-have-fix-field`).** Every warning this plan prints has a `message` and a non-empty `fix`: `brain_missing` and `brain_id_cloned`. The wording is copy item E3B-1, entries 4 and 27, in `decision:2026-11-12-copy-batch-e3b-warning-splits`: the approved sentence stays word for word and only its next step moves into `fix`; the plain line is `remember: warning: <message> <fix>`. `code`, `detail`, exit codes and `--strict` do not change. Itay approved E3B-1 as a whole on 2026-11-12, including every `fix` that is Story Weaver wording (`decision:2026-11-12-copy-batch-e3b-warning-splits`).

## Review points

- The reviewer checks the command tokens against `decision:2026-10-22-m-19`.
- The reviewer checks that a copy mints a new id and a move keeps the id.
- The reviewer checks that a failed write does not change the open's exit and does not print a new sentence.
- The reviewer checks that `remember brain new-id` behaves exactly as on main (its existing tests still pass unchanged).

## Definition of Done

- [x] `the first read-write open adds one row and a second open of the same brain does not add another` (`tests/test_brain_list.c`): `opening_a_brain_read_write_adds_one_row_and_a_second_open_adds_none`, `brain_list_first_record_adds_one_row_and_a_second_adds_none`.
- [x] `the write is a temp file renamed over brains.json under the lock`: `brain_list_write_is_a_temp_file_renamed_over_brains_json`, `brain_list_record_holds_the_advisory_lock_while_it_decides`, `brain_list_stale_temp_symlink_is_replaced_not_followed`.
- [x] `a copy of a brain file mints a new brain_id and prints the C-38 line`: `a_copied_brain_gets_a_new_id_and_prints_the_c38_line_with_its_fix`.
- [x] `a moved brain file keeps the id and updates the path, with no warning`: `a_moved_brain_keeps_its_id_updates_the_path_and_warns_nothing`.
- [x] `remember brain list prints a missing path with the C-01 brain_missing line and keeps the row`: `brain_list_prints_each_listed_path_and_a_path_with_no_brain_prints_the_c01_line`.
- [x] `remember brain prune drops a missing path and leaves a path that still has the brain`: `brain_prune_drops_a_path_with_no_brain_and_keeps_one_that_has_it`.
- [x] `a failed replace leaves the previous file and does not change the open's exit`: `a_failed_replace_leaves_the_previous_file_and_the_open_exits_as_before`, `brain_list_a_folder_that_cannot_take_the_temp_file_returns_io_and_keeps_the_file`.
- [x] `specs/BRAIN-LIST.md` is written, and the decisions this plan built are `state:implemented` (list in the notes).
- [x] The gate passes on the committed tree, and the notes record the mutants and their red tests.

## Implementation Notes

Built 2026-11-15 on `main` 42e7d0a. Code taken from the plan 25 base review commit `0aa22f3` (brains.c, brain_note.c, the brain parts of cmd_brain.c, store_sqlite.c and fileio.h) and reworked. The base review's red tests are green and every plugged hole keeps a test.

### What is built

- `src/brain_list.c`/`.h`: reads and writes `<data dir>/brains.json` (no SQLite). `brain_list_read`, `brain_list_record` (add, update, copy, move, with a lock, a temp file named `brains.json.tmp` and a rename), `brain_list_prune`. The id of a copy is minted through a callback, only when the open is a copy.
- `src/brain_note.c`/`.h`: the open-time bridge (`brain_note_open`, called by `open_store`) and `brain_json_path`.
- `src/cmd_brain.c`: `brain list` and `brain prune` run without opening a brain (`cmd_brain_file`). `brain new-id` is unchanged and its tests are unchanged.
- `src/util.c`: `util_resolve_data_dir` is the one data dir rule; `user.db` follows it too.
- `src/store_sqlite.c`: `store_brain_id`, `store_brain_set_id` (the same write as `new-id`, `replace_brain_id`), `store_mint_uuid`, `store_path_has_brain_id` (regular files only), `store_is_writable` (false for a read-only file and for a newer brain).
- `src/diag.c`: `diag_brain_id_cloned` (C-38, entry 27) and `diag_brain_missing` (C-01, entry 4), each with `message` and `fix` (E3 B).
- `src/output.c`: `output_brain_list_data`, `output_brain_prune_data`. `src/fileio.c`: `fileio_read_regular` (non-blocking, regular file, capped) and `fileio_mkdir_parents`. `src/fileio.h` holds the one copy of the `O_NOFOLLOW` shim.
- `tests/test_brain_list.c` (suite `brain_list`, 85 tests) is in `tests/gate-suites`. `tests/test_main.c` gives every test its own `XDG_DATA_HOME` (`tst_set_setup`).
- `specs/BRAIN-LIST.md` is the spec. README, SKILL.md, `docs/engineering-notes.md` and `specs/README.md` are updated.

### Plan rows from the base review

- Ten red gate tests: `open_store` wrote `brains.json` beside the test's `user.db` and minted an id on every open. Now the file follows the data dir (`decision:g-01`, `decision:2026-11-11-brains-json-data-dir`), and the id is minted only for a copy (`brain_list_a_plain_open_mints_nothing`, mutant `mint_on_every_open` turns `envelope_fixed_ids_make_replies_predictable` red). Full gate: 1531 tests run, 0 failed (command below).
- X9 NUL in a path: `brain_list_a_nul_inside_a_path_is_unreadable`, `..._brain_id_...`, `..._a_raw_nul_byte_in_the_file_...`. `jsonread` refuses NUL and `\u0000`, so the old extra `memchr` was dead code and is gone; the three tests prove the contract.
- X10 symlinked lock: `brain_list_a_symlink_at_the_lock_is_not_followed` (mutant `lock_follows_symlink`).
- X11 FIFO at a listed path: `a_fifo_at_a_listed_brain_path_does_not_block_a_later_open` (mutant `has_id_follows_fifo`, killed by timeout), and `brain_list_and_prune_take_a_listed_fifo_or_folder_for_a_path_with_no_brain` (mutant `path_missing_not_regular`).
- X12 control bytes in `brain list`: `brain_list_masks_control_bytes_in_a_listed_path` (mutant `list_not_masked`).
- S8: `brain_missing_message` is gone; `diag_brain_missing` is wired into `brain list`.
- E3B `fix` fields: the two warnings carry `message` and `fix` in plain and JSON (`a_copied_brain_gets_a_new_id_and_prints_the_c38_line_with_its_fix`, `brain_list_json_gives_the_rows_and_a_brain_missing_warning_with_its_fix`; mutants `clone_fix_text`, `missing_fix_text`).
- G5 A, done: the 128-row cap is dropped (`brain_list_has_no_row_cap`), the read has a size cap (`BRAIN_LIST_FILE_MAX`; `..._exactly_the_cap_is_read`, `..._one_byte_over_the_cap_is_unreadable_and_left_alone`), a file that cannot be used is never written over (`brain_list_an_unreadable_file_is_never_written_over`, `an_unreadable_brains_json_does_not_change_the_open_and_is_left_alone`). **Not done:** the warning itself. See Open items.

### New holes found and plugged here (each test failed first or has a killing mutant)

- A non-UTF-8 path made every later read of the file fail: `brain_list_a_path_that_is_not_utf8_is_not_recorded_and_the_file_stays_readable` (mutant `non_utf8_recorded`).
- A FIFO at `brains.json` that holds a whole document and no writer read as a valid file: `brain_list_a_fifo_at_brains_json_is_not_read_even_when_it_holds_a_whole_document` (mutant `read_follows_fifo`). `O_NONBLOCK` is proved by a hang (`read_blocks_on_fifo`, killed by timeout).
- A stale `brains.json.tmp` symlink: unlinked under the lock, not followed (`brain_list_stale_temp_symlink_is_replaced_not_followed`, `stale_tmp_not_unlinked`).
- A newer brain was recorded and its id replaced (it would exit 75 mid-open): `a_newer_brain_is_not_recorded_keeps_its_id_and_is_not_taken_for_a_copy` (mutant `writable_ignores_newer`).
- An unreadable list file turned a fault into a bogus "unreadable": allocation failures return `BRAIN_LIST_IO`, not `UNREADABLE` (`brain_list_*_survives_each_allocation_failure`, 7 tests, two counters).
- A missing data folder made recording silent forever: the folder is made, mode 0700 (`the_data_dir_is_made_with_mode_0700_when_it_is_missing`).
- A failure to store the new id of a copy fails the open and leaves the old id or the new one, never a third, and the warning is printed only for an id that is in the brain (`a_copy_whose_new_id_cannot_be_stored_fails_the_open_and_leaves_brain_and_list_agreeing`, mutants `set_id_failure_ignored`, `open_note_error_ignored`).
- Eight concurrent writers keep all their rows (`brain_list_eight_concurrent_records_all_keep_their_row`, mutant `lock_not_taken`).

### Mutants (one clause each, rebuilt each time, each run in its own process group with a 420 s limit)

Command: `python3 mutate.py` (scratchpad harness). 57 mutants, 56 killed, 1 survived. Red test names are in the plan session log; the main ones:

| Mutant | Red test |
|---|---|
| lock_not_taken | `brain_list_eight_concurrent_records_all_keep_their_row`, `brain_list_record_holds_the_advisory_lock_while_it_decides` |
| lock_follows_symlink | `brain_list_a_symlink_at_the_lock_is_not_followed` |
| lock_mode_open | `brain_list_file_mode_is_0600` |
| stale_tmp_not_unlinked, tmp_name | `brain_list_stale_temp_symlink_is_replaced_not_followed` |
| no_rename | `brain_list_first_record_adds_one_row_and_a_second_adds_none` and the copy tests |
| cap_off_by_one / cap_ignored | `..._exactly_the_cap_is_read` / `..._one_byte_over_the_cap_...` |
| read_follows_fifo / read_blocks_on_fifo | the FIFO test / timeout |
| doc_format_unchecked, doc_version_unchecked, doc_brains_not_array, doc_root_not_object | `brain_list_a_document_of_another_shape_is_unreadable` |
| row_not_object, row_string_type_unchecked | `brain_list_a_row_with_a_wrong_type_or_a_missing_field_is_unreadable` |
| unreadable_is_empty | `..._an_unreadable_file_is_never_written_over` and the cap/FIFO tests |
| enoent_is_unreadable | `brain_list_first_record_adds_one_row_and_a_second_adds_none` |
| never_a_copy, always_a_copy, move_keeps_stale_row, update_not_stored, same_id_rewritten, clone_always_pushes, clone_mint_skipped, move_updates_wrong_row | the copy, move and update tests of the same names |
| unchanged_is_written | `brain_list_a_record_that_changes_nothing_leaves_the_file_as_it_is` |
| write_failure_reported_ok | `brain_list_a_folder_that_cannot_take_the_temp_file_returns_io_and_keeps_the_file` |
| prune_inverted, prune_always_writes, prune_rows_not_returned | the prune tests |
| non_utf8_recorded, data_dir_not_made, mkdir_parents_eexist_fails | the UTF-8 test and the data dir tests |
| path_missing_not_regular, list_never_warns_missing, list_not_masked, list_opens_a_brain | the `brain list` tests |
| realpath_skipped, readonly_recorded, set_id_skipped, clone_warning_skipped, set_id_failure_ignored, open_not_noted, open_note_error_ignored | the CLI copy, move and newer-brain tests |
| mint_on_every_open | `envelope_fixed_ids_make_replies_predictable` |
| has_id_follows_fifo, has_id_any_id, writable_ignores_newer, set_id_not_canonical_checked | the store tests (FIFO by timeout) |
| data_dir_suffix_swapped, data_dir_xdg_ignored | the data dir tests |
| clone_fix_text, missing_fix_text, json_count_wrong, prune_action_wrong | the warning and JSON tests |

Survivor: `lock_not_regular_checked` (drop `S_ISREG` on the lock fd). On macOS `fcntl` refuses a lock on a FIFO, so the open fails either way. On Linux a lock on a FIFO succeeds, so there the check decides, and `brain_list_a_fifo_at_the_lock_is_not_a_lock_and_does_not_block` asserts `BRAIN_LIST_IO`. `scripts/ci-linux.sh` runs that test on Linux (aarch64 here).

A first mutation run reused stale objects (make compares mtimes at one second); the harness now sets each written file's mtime ahead. Runs before that fix were discarded and redone.

### Claims run (standard:coding rule 2)

- Gate: `ctest --test-dir build` 6/6 (step_gate, store_asan_gate, spec_drift, no_shim_symbols ...); `scripts/lint-all.sh` with `CI=true`: format, boundaries, cppcheck src and tests, gcc -fanalyzer, clang-tidy (LLVM 22), scan-build PASS (IWYU not installed here; Linux CI runs it); `scripts/check-coverage.sh`: functions 100%, effective lines 100%, pure-TU branches 100%; `leaks-macos` PASS.
- Probed with a fake `HOME`: a full gate run under `HOME=<temp>` leaves no `.remember` folder (no test reaches the real data dir).
- jsonread refuses NUL: `brain_list_a_nul_inside_a_path_is_unreadable` passes with the extra check removed.
- Fault sweeps: `settings` sweeps count allocations, prepares and steps of an open. The reads the open note makes use `note_prepare`/`note_step` (the real calls) and the settings fixture sets `XDG_DATA_HOME=/dev/null/remember`, so those sweeps count what they counted before plan 26. Without this, the first run of a sweep absorbed the fault and changed the state (`cmd_settings.c` lines 2580 and 2656 lost coverage).

### Simplify pass

1. `util.c`: `util_resolve_user_db_path` and the new data dir rule share `resolve_data_path` (one rule, about 12 lines saved).
2. `store_sqlite.c`: `store_brain_new_id` and `store_brain_set_id` share `replace_brain_id`.
3. `brain_list.c`: the `BrainDoc` type (a copy of `BrainList` plus a capacity) and `grow` are gone; every list has one free slot (about 25 lines saved). Prune is one pass (it was a memmove per row).
4. `fileio_mkdir_parents` dropped its `stat`: a component that is not a folder fails at the next step or at the open (mutant `mkdir_parents_file_ok` was equivalent).
5. The base review's own NUL checks (`take_string`, `parse_doc`) were dead behind `jsonread`.
6. Tests: `given_scene`, `given_dir`, `given_path` and `REQUIRE_COUNT` (now `row_at`) keep each test to its story; the OOM tests share `sweep_allocations`.

### Decisions

`state:implemented` set on: `decision:g2-08`, `decision:g2-05`, `decision:2026-11-11-brains-json-format`, `decision:2026-11-11-brains-json-data-dir`, `decision:2026-11-11-brain-command-names`, `decision:2026-10-22-m-04`. Left `state:accepted`: `decision:g-01` (its data dir also holds `plugins/`, built later), `decision:2026-10-22-m-19` (the other items of N2 to N9), `decision:2026-11-12-brains-json-unreadable-warns` (its warning is not built), `decision:2026-11-12-e3-warnings-have-fix-field` (`setting_import_skipped` still prints no `fix`; not this plan).

### Deep review in session (workflow:deep-review)

- Simplicity (S): see the simplify pass above. Dead checks removed, one type and one function fewer, prune in one pass.
- Security audit (A): `brains.json` is another program's input. Each crossing field is handled: `path` (masked in plain output, JSON-escaped, never opened except by `stat` and, for a regular file, a read-only SQLite open), `brain_id` and `added_at` (never printed in plain output, compared by `strcmp` only). Probed by running `remember brain list` on a row whose path holds ESC, a newline, 300 bytes, DEL and U+0085: the plain line shows `?` for each, JSON shows `\u001b` and `\n`, and the `brain_missing` warning line is one line. A hostile row cannot make the file unreadable for others (UTF-8 refusal on write), cannot hang an open (FIFO and folder paths are "no brain" by `stat`), and cannot change an id (the copy test needs a regular file with that id).
- Exploit hunt: TOCTOU between `stat` and the read-only SQLite open of a listed path (an attacker who can swap that file can already replace the brain); a symlink at the lock, at the temp name, at `brains.json` (lock and temp covered; `brains.json` is read through, replaced by rename); a FIFO at all three paths; a file over the cap; 300 rows; eight concurrent writers; a crash between the temp write and the rename (the old file stays, the next writer unlinks the stale temp under the lock).
- Not fixed, needs Itay (open items 5 to 7 above): the lock wait has no bound.

### Open items (no decision, no approved copy, no plan text; the code does what `specs/BRAIN-LIST.md` says and marks each)

1. The warning for an unreadable `brains.json` (code, `message`, `fix`) and the value of `BRAIN_LIST_FILE_MAX` (the code uses 1 MiB). `decision:2026-11-12-brains-json-unreadable-warns` says both come with a plan 26 copy item that does not exist.
2. What `brain list` and `brain prune` print on an unreadable file: placeholder `cannot_open` with `cannot read brains.json`.
3. The plain `brain list` row (path only), the `--json` data of `brain list` and `brain prune`, and the meaning of "has no brain" (not a regular file).
4. The usage line of a bare `brain` (`brain takes 'new-id'`) is untouched, so `brain new-id` tests pass unchanged, and it is now incomplete. The help lines and the one-line summary for `brain` are the coder's wording.
5. The lock wait: `F_SETLKW` blocks until the holder lets go, and the holder runs `stat` and a read-only SQLite open on every listed path while it holds the lock (the decision names an advisory lock, not a bound). A listed path on a hung mount would stall every `remember` open. A bound needs a number.
6. `brains.json` as a symlink: a write replaces the link with a regular file (rename). Reads follow the link.
7. A copy is recorded before its new id is stored in the brain. If that store fails, the open fails and the list names an id the brain lacks; the next open sees the same file again as a copy and mints again (self-healing).

## Review Notes
