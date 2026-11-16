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
- [x] `remember brain list prints a missing path with the C-01 brain_missing line and keeps the row`: `brain_list_prints_path_id_and_added_at_per_row_and_a_path_with_no_brain_prints_the_c01_line`.
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
- X12 control bytes in `brain list`: `brain_list_masks_control_bytes_in_every_field_of_a_row` (mutant `list_not_masked`).
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

Second-opinion review, 2026-11-15/16 (Opus, worktree `eager-maxwell-42ff49`). Workflow: `workflow:deep-review` plus the PLAN scope of `workflow:whole-plan-deep-review`, against `standard:coding`, `standard:c`, `pref:review-lenses` and the decisions in the REVIEW_REQUEST, plus `decision:2026-11-15-plan26-coder-grill`. Coder commit 816c680 on main 42e7d0a; no rebase was needed. Grill answers: `decision:2026-11-16-plan26-review-grill` (notes: `plan-26-review-grills.md` in the session notes folder).

### Review points and Definition of Done

- Command tokens `brain list` and `brain prune` match `decision:2026-10-22-m-19` and `decision:2026-11-11-brain-command-names`. Its "dispatch under Maintenance" item is a help grouping no plan has built yet, so m-19 correctly stays `state:accepted`.
- Copy mints a new id and move keeps it: held, and hardened (X17).
- A failed write keeps the open's exit and prints nothing: held (`a_failed_replace_...`).
- `brain new-id` is unchanged: no `brain new-id` test was edited. The coder's edits to old tests are isolation only (a data dir per test, a twin dir in `facade_conflict_accept_matches_cli`, and an unwritable data dir for the settings fault sweeps).
- Every DoD box names a passing test. Two test names changed in this review; the boxes above name the new ones.
- Plan 25 base review X9-X12 are still plugged, each with its test: `brain_list_a_nul_inside_a_path_is_unreadable`, `brain_list_a_symlink_at_the_lock_is_not_followed`, `a_fifo_at_a_listed_brain_path_does_not_block_a_later_open`, `brain_list_masks_control_bytes_in_every_field_of_a_row`.

### Security audit (lens A): holes found and plugged

Each hole has a test that was red on the coder's tree and a mutant of the fix that turns it red again.

| # | Attack | Impact before | Fix | Test |
|---|---|---|---|---|
| X16 | Make a listed brain's folder unreadable (`chmod 000`), then run `brain prune` | `brain list` warned that the brain "isn't on disk", and prune **deleted the row of a brain that exists** | Only `ENOENT`, `ENOTDIR`, `ELOOP` and `ENAMETOOLONG` prove a path absent (`fileio_errno_means_absent`, one definition); any other `stat` failure keeps the row | `brain_list_and_prune_keep_a_brain_whose_folder_cannot_be_searched`, `..._take_each_kind_of_absent_path_...` |
| X17 | Copy `o.db` to `c.db`, hold `BEGIN EXCLUSIVE` on `o.db` (or make its folder unreadable), then open `c.db` | The probe could not read `o.db`, which counted as **a move**: both brains kept one `brain_id` and `o.db`'s row was gone (the sync hazard g2-05 guards) | The probe is three-way (yes, no, cannot tell) and waits the open's busy timeout. Cannot tell fails the command with the cause's own code and changes nothing (G2) | `a_copy_opened_while_the_original_is_locked_past_the_busy_timeout_fails_busy_and_changes_nothing`, `a_copy_whose_original_s_folder_cannot_be_searched_fails_cannot_open_and_changes_nothing`, `a_copy_that_could_not_be_told_is_a_copy_on_the_next_open`, `a_copy_whose_original_is_locked_for_less_than_the_busy_timeout_is_a_copy`, `store_path_has_brain_id_cannot_tell_*` |
| X18 | `{"path":"/nope/a","path":"/etc/hosts",...}`, or `"brains"` given twice | Parser differential: remember read the first member and Python the last | A member the reader takes, given twice, makes the file unreadable | `brain_list_a_row_field_given_twice_is_unreadable`, `brain_list_a_document_member_given_twice_is_unreadable` |
| X19 | A TAB in a path, or ESC in a `brain_id` | With the decided three-column row: a fake column, and raw ESC on the terminal from `brain_id` and `added_at` | Every field is masked, TAB included (G1 A) | `brain_list_masks_control_bytes_in_every_field_of_a_row`, `output_brain_row_human_masks_control_bytes_and_tabs_in_each_field` |
| X20 | An unknown member in brains.json | Ignored silently on read, and dropped on the next rewrite | `unknown_field` with the C-35 file line on every read (G6 B). A rewrite still drops it (G7 C) | `brain_list_warns_*_c35_*`, `brain_prune_warns_*`, `a_read_write_open_warns_*`, `brain_list_json_carries_unknown_field_*` |
| X21 | Hold `brains.json.lock` (any process of the user, or one stuck on a hung mount while it probes) | `F_SETLKW` had no bound: **every** `remember` open, read commands included, and `brain prune` hung until the holder let go | The wait tries `F_SETLK` every 10 ms up to the busy timeout, then fails `busy` with exit 1 and changes nothing (G5) | `an_open_that_waits_past_the_busy_timeout_for_the_brains_json_lock_fails_busy`, `brain_prune_that_waits_past_the_busy_timeout_for_the_lock_fails_busy_and_keeps_the_rows`, `an_open_whose_brains_json_lock_is_let_go_inside_the_busy_timeout_records_the_brain` |

Probed and not a hole: `brains.json` as a symlink (reads follow it, and a write replaces the link with a file, mode 0600); `version` `1.0` (unreadable); the 0700 data dir and the 0600 file and lock; a FIFO or folder at a listed path; a file of exactly the cap.

Residual: `stat` then `sqlite3_open_v2` on a listed path is check-then-use (as the base review recorded). The lock wait counts its 10 ms sleeps, so it can run a little past the timeout (marked `ponytail:` in `wait_for_lock`). A hostile brain at a listed path can make the read-only probe slow: that is the same trust as opening the brain itself.

### Simplicity (lens S)

| # | Candidate | Lines saved | Status |
|---|---|---|---|
| S1 | The row JSON writer existed twice, in `brain_list.c` `write_doc` and `output.c` `write_brain_rows` (two copies of one schema) | about 12 | Done: `output_brain_rows_json`, one writer |
| S2 | `brain_list_read` copied into a local and back; `load_file` already leaves `out` empty on every failure | 8 | Done |
| S3 | Three-way `if` for "brain X takes no arguments" | 6 | Done: `err_fmt` with the matched verb |
| S4 | `brain_path_is_missing` `if (S_ISREG) return false; return true;` | 3 | Done (`return !S_ISREG(...)`) |
| S5 | The absent-errno rule would have been written twice (prune and the probe) | about 4 | Done: one inline `fileio_errno_means_absent` |
| — | `still_has_brain` and `mint_id` wrappers | 0 | Kept: the callback signature needs them |

### Grill (answered 2026-11-16, `decision:2026-11-16-plan26-review-grill`)

- G1 A: mask TAB in the row.
- G2: busy wait, then cannot tell fails the command and changes nothing. Each cause takes its existing code: `busy`, `cannot_open` (`EACCES`, any other `stat` or open failure), `io_error` (`EIO`), `corrupt` or `not_a_database`. They are classed as for any SQLite failure (`classify_extended`); `stat` sets only `io_error` for `EIO`, else `cannot_open`.
- G3 A: the placeholder lines join Story Weaver's open item.
- G4 A: a copy whose new id cannot be stored fails the open.
- G5: the lock wait is bounded by the busy timeout (`--busy-timeout`, default 5000 ms). A timeout fails with `busy`.
- G6 B: an unknown field warns C-35.
- G7 C: a rewrite drops an unknown field.

The coder's open items 3, 5 and 7 are now settled (coder grill, G5, G4). The two G5 and G2 message lines are OPEN placeholders in `decision:open-2026-11-15-brains-json-unreadable-copy`.

### Mutants (this review's guards; each in its own process group, rebuilt with the source mtime set ahead)

Pass 1 (`mut.py`, 10 mutants, all killed): `dup_first_wins`; `missing_no_enoent`, `missing_no_enotdir`, `missing_no_eloop`, `missing_no_enametoolong`, `missing_any_errno`; `row_no_tab1`, `row_id_unmasked`, `row_at_unmasked`, `row_no_added_at` (also killed in `remember_store_tests` by the `output_brain_row_human_*` tests).

Pass 2 (`mut2.py`, 24 mutants): 17 killed, `tab_everywhere` survived (no test held that a body keeps its TAB: `output_text_human_keeps_a_tab` added), 6 did not build (an unused function, parameter or variable under `-Werror`; not survivors, rerun as compiling rewrites in passes 3 and 4).

Pass 3 (`mut3.py`, 22 mutants, the G5 and G2 guards plus the reruns): 15 killed. `open_busy_ok` and `undecided_ok` (print the error, return 0) survived: under `--json` the command's own envelope then fails to write, which exits 1 anyway. The G5 open and the G2 `cannot_open` tests now also run in plain mode, where only the open's failure can exit 1. 4 did not build and were rewritten.

Pass 4 (`mut4.py`, 6 mutants): all killed: `open_busy_ok`, `undecided_ok`, `unknown_root_skipped`, `lock_no_wait`, `lock_busy_maps_io`, `undecided_code_busy`.

Killed in passes 2 and 3, with their red test: `notadb_unknown`, `error_unknown`, `done_unknown`, `any_failure_is_no`, `no_busy_wait`, `stat_fail_is_no`, `stat_fail_is_unknown`, `open_fail_is_no`, `timeout_const`, `undecided_is_move`, `undecided_is_copy`, `undecided_writes`, `unknown_last_wins`, `unknown_rows_skipped`, `unknown_term1`, `unknown_term3`, `record_no_unknown`, `prune_no_unknown`, `open_no_warn`, `list_no_warn`, `prune_no_warn`, `row_keeps_tab`, `tab_everywhere`, `lock_busy_is_io`, `record_timeout_0`, `prune_timeout_long`, `prune_busy_unreported`, `prune_flag_ignored`, `open_lock_default`, `stat_class_dropped`, `open_snapshot_dropped`, `step_snapshot_dropped`.

Justified survivor: `stat_eio_dropped` (`stat` failing with `EIO` classed `cannot_open` instead of `io_error`). No test can make `stat` return `EIO`: the test VFS fails SQLite reads, not `stat`. The SQLite `EIO` path is classed by `classify_extended`, which `test_errors` covers.

### Gate fixes this review made

- clang-tidy (19 findings, all in this review's code): bool returns use `(...) != 0`; the busy-timeout default is one helper, `busy_timeout_ms_of`, for the open and `brain prune`; magic numbers are named; NOLINT with a reason for swappable parameters and for the test pipes.
- cppcheck `knownConditionTrueFalse` on `unknown != NULL` in `brain_note_open`: cppcheck does not see `brain_list_record` write through the compound-literal out struct (a named struct does not help either). It is suppressed inline with that reason.
- Coverage (7 must-cover lines):
  - `brain_list_read_of_a_file_with_an_unknown_member_survives_each_allocation_failure` covers the copy of the unknown member's name.
  - `brain_list_and_prune_that_cannot_write_the_unknown_field_warning_exit_1` covers a failed C-35 write.
  - `wait_for_lock` no longer has a separate `LOCK_IO` exit for an `F_SETLK` errno other than `EAGAIN`/`EACCES` (no test can produce one). Any failure is retried until the timeout and then fails `busy`, which is still a visible failure.
  - The `--json` data writes go to the envelope's buffer, so only out-of-memory fails them. They set `rc` in one statement and never overwrite an earlier failure (a mutant that drops that guard survives for the same reason).

- ci-linux: the lock tests called `usleep`, which glibc does not declare under `_POSIX_C_SOURCE=200809L` (POSIX 2008 dropped it). macOS declares it anyway. They use `nanosleep` now (`pause_ms`).
- CodeQL `cpp/overflowing-snprintf` in the coder's `brain_list_has_no_row_cap`: it summed `snprintf` returns as offsets without a check. Each row is now formatted into a fixed buffer with a checked return and copied in by `append_text`, which bounds every copy.
- The diff was read for implicit int-to-char narrowing (local ci-linux is aarch64 and GitHub is amd64, where `char` signedness differs). None: every added `char` is a pointer, an array or a literal.

### Gate (this tree, local)

- `ctest --test-dir build`: 6/6 passed.
- `scripts/check-coverage.sh`: 100% of functions, 100% of effective lines, and every branch side in the pure TUs.
- `CI=true scripts/lint-all.sh`: LINT OK (format, boundaries, cppcheck, gcc -fanalyzer, clang-tidy, scan-build).
- `scripts/ci-linux.sh` (ubuntu 24.04, aarch64): `ci-linux: OK`, including IWYU.
- `scripts/codeql-local.sh`: 0 alerts (after the `cpp/overflowing-snprintf` fix above).

### Grades

- Coder: 8/10. The plan was carried out with care: hostile-file reads, fault sweeps, an advisory lock and a concurrent-writer test. It missed the third outcome of the copy probe (X17) and of prune's `stat` (X16). Both cases deleted or merged data.
- In-session reviewer: 7/10. It caught the FIFO and NUL holes (X9-X12), but passed X16, X17 and the unbounded lock wait.

### Folds into standards

- `standard:coding` §4: "A check of another file (is it there, does it hold this id) has a third outcome, could not tell (EACCES, EIO, a busy lock), and that outcome never takes the branch that deletes a row, merges two records or gives two of them one identity."
- `standard:coding` §1: a failed command exits non-zero for more than one reason; under `--json` a second envelope fails its own write, so a failure path's exit code is also asserted in plain mode.
- `standard:c`: read every compiler warning in each target the gate builds, also one that is not `-Werror`; a new one is a finding.
- Dropped as already covered: "test a member given twice" (§4 hostile-file line) and "reuse the row serializer" (the one-definition rule).

Follow-up (not plan 26): `store_sqlite.c` `real_malloc`/`real_calloc`/`real_realloc` and `import_read.c` `msg_version_too_new` raise `-Wallocator-wrappers` on main. That warning is not `-Werror`; silence it with the pragma `test_alloc.c` uses.
