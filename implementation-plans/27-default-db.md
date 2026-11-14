# 27 — Default brain path

**Module:** `util_resolve_db_path` in `src/util.c`. The declaration stays in `src/util.h`.
**Needs:** plan 19 (merged). No other plan.
**Blocked on:** nothing.

## Goal

The engine reads `~/.remember/default-db` when `--db` and `REMEMBER_DB` do not choose a path.

## Source

- `decision:ui-overhaul-grill-2026-10-12`, item 3 (the path order and the file rules). Its app half is amended by `decision:2026-11-11-mac-brain-path-no-agent` (no grant, no bookmark, no LaunchAgent); the engine half below is unchanged.
- `decision:2026-11-11-default-db-home` (P4 A, G2 A): `default-db` and `remember.db` stay in `~/.remember` also when `XDG_DATA_HOME` is set. This plan reads `HOME` through the existing helper; plan 29 moves that read into the CLI's RememberEnv.
- Spec: `specs/DEFAULT-DB.md` (new).

## Out of scope

The app that writes the file (Mac plan M53) and the Codex skill (plan 28). There is no LaunchAgent. The engine does not create `default-db`. A file that is not one UTF-8 line ending in a newline is not given a new rule here: the coder does not add a case the decision does not name.

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md).

Tests are added to `tests/test_coverage_edges.c`. That suite is already in `tests/gate-suites`. Do not add a test binary.

A path taken from the file goes through the existing `copy_regular_db_path`. Do not write a second check for `:memory:` or a `file:` URI.

`REMEMBER_DB` is read with the existing `env_nonempty`. An empty value stays unset.

## What must be true

- The order is `--db`, then a set `REMEMBER_DB`, then the one absolute path in `~/.remember/default-db`, then `~/.remember/remember.db`.
- A set `REMEMBER_DB` hides the file.
- The file is one UTF-8 line, the absolute path, ending in a newline. The engine trims ASCII whitespace.
- A missing file, an empty line, or a path that is not absolute counts as no file. The engine then uses `~/.remember/remember.db`.
- The engine does not create the file.
- `~` is the home directory `util_resolve_db_path` already uses.

## Review points

- The reviewer checks that an empty `REMEMBER_DB` still goes through `env_nonempty` and does not hide the file.
- The reviewer checks that the engine does not create `default-db` on a missing file, an empty line, or a relative line.
- The reviewer checks that `:memory:` and a `file:` URI from the file fail with the same error `copy_regular_db_path` already returns for `--db`.
- The reviewer checks that the coder did not add a rule for a second line or for invalid UTF-8.

## Definition of Done

- [x] `a set REMEMBER_DB hides default-db` (`tests/test_coverage_edges.c`).
- [x] `an absolute line is the brain path` (`util_resolve_default_db_absolute_line_is_the_brain`).
- [x] `a missing file uses ~/.remember/remember.db and the engine does not create default-db` (`util_resolve_default_db_missing_file_uses_builtin_and_creates_nothing`).
- [x] `an empty line uses ~/.remember/remember.db and the engine does not create default-db` (`util_resolve_default_db_empty_line_uses_builtin_and_creates_nothing`).
- [x] `a relative line uses ~/.remember/remember.db and the engine does not create default-db` (`util_resolve_default_db_relative_line_uses_builtin_and_creates_nothing`).
- [x] `a default-db value of :memory: fails the way --db :memory: fails` (`util_resolve_default_db_ephemeral_fails_like_cli_db`).
- [x] `a set XDG_DATA_HOME does not move default-db: the engine still reads ~/.remember/default-db` (`util_resolve_default_db_xdg_data_home_does_not_move_it`).
- [x] `specs/DEFAULT-DB.md` is written, and `decision:ui-overhaul-grill-2026-10-12` item 3 is cited from that spec. The decision stays `state:accepted` because this plan builds one item of it.
- [x] `a default-db that exists but cannot be read fails with cannot read default-db and no fallback` (`util_resolve_default_db_directory_fails_with_no_fallback`, `..._symlink_loop_fails_with_no_fallback`, `..._without_permission_fails_with_no_fallback`; `decision:2026-11-13-default-db-unreadable`).
- [x] The gate passes on the committed tree, and the notes record the mutants and their red tests.
- [x] Review grill (`decision:2026-11-14-plan27-review-grill`): a default-db that is not a regular file fails without hanging (`util_resolve_default_db_not_a_regular_file_fails`); a NUL byte fails (`util_resolve_default_db_nul_byte_fails`); the spec states the atomic writer (G3); the CodeQL per-source rule is documented (G4).

## Implementation Notes

Code: `read_default_db`, `use_default_db` and `trim_ascii_space` in `src/util.c`. `util_resolve_db_path` calls `use_default_db` after `--db` and `REMEMBER_DB`. Tests: `tests/test_coverage_edges.c` (suite `coverage_edges`, already in `tests/gate-suites`). Spec: `specs/DEFAULT-DB.md`.

Two points the plan implies:
- A line goes through `copy_regular_db_path` before the absolute check. `:memory:` is not absolute, so an absolute check first would make it "no file" and break the Definition of Done item that says it fails like `--db`. A relative line that is also longer than the caller's buffer fails with `database path is too long`, not "no file".
- The code has no rule for a second line or for invalid UTF-8. The file is read (at most `PATH_MAX + 1` bytes) as one string. A second line stays in the string. A NUL byte ends the C string, so `/tmp/a<NUL>/b` selects `/tmp/a` (run: it created a brain at `/tmp/a`). A file of `PATH_MAX + 1` bytes or more fails with `database path is too long`; a symlink to `/dev/zero` therefore fails that way. A FIFO at `default-db` blocks `fopen`, as a FIFO given to `--db` does.

Test order and red runs (`remember_tests --only coverage_edges`, before the production change): `an absolute line is the brain path`, the trim test, `util_resolve_empty_remember_db_does_not_hide_default_db`, the `:memory:`/`file:` test, the XDG test and the too-long test were red. The tests for `a set REMEMBER_DB hides default-db`, `--db` hiding the file, a missing file, an empty line, a blank line and a relative line passed before the change, because the old code already did that. They guard the mutants below.

Mutants (each built, then the suite run; all red unless noted):

| Mutant | Red tests |
|---|---|
| relative line accepted (`buf[0] != '\0'`) | `..._relative_line_uses_builtin_and_creates_nothing` |
| absolute check before `copy_regular_db_path` | `..._ephemeral_fails_like_cli_db` |
| no leading trim | `..._trims_ascii_whitespace_and_needs_no_newline` |
| no trailing trim (`strchr("", c)`) | absolute-line, trim, empty-`REMEMBER_DB`, ephemeral, XDG tests |
| too-long guard `==` to `>` | `..._line_too_long_fails` |
| error from `use_default_db` swallowed | ephemeral and too-long tests |
| file name `default-dbx` | absolute-line, trim, empty-`REMEMBER_DB`, ephemeral, XDG, too-long tests |
| `fopen "a+"` (creates the file) | `..._missing_file_uses_builtin_and_creates_nothing` and the tests that read the file |
| file read before `--db`/`REMEMBER_DB` | `util_resolve_env_hides_default_db`, `util_resolve_cli_db_hides_default_db` |
| empty `REMEMBER_DB` hides the file | `util_resolve_empty_remember_db_does_not_hide_default_db`, `util_resolve_empty_env_falls_through_to_home` |
| `XDG_DATA_HOME` moves the file | `..._xdg_data_home_does_not_move_it` |
| `--db` loses to the file | `util_resolve_cli_db_hides_default_db` |
| `REMEMBER_DB` read before `--db` | survived `coverage_edges`; killed by `db_flag_wins_over_env` (`json_db_config`, black-box; the second opinion ran it) |

Equivalent mutant removed instead of tested: the check `line[0] == '\0'` before `copy_regular_db_path`. An empty line copies as an empty string and then fails the absolute check, so the result is the same. The line was deleted (simplify pass).

Simplify candidates: the empty-line check (deleted, 3 lines); the blank-line test (merged into the empty-line test, about 10 lines); the two `fopen` sites in the tests (one `write_text_file` helper, 8 lines, and cppcheck's leak finding); `given_home` and `given_builtin_path` are used by every new test and by the changed empty-env test (kept).

Hostile-line probes run on the binary (HOME set to a temp dir): ESC and newline in the path, a 300-byte component, `:memory:`, `file:`, a relative line, NUL. No error sentence echoes the path from the file; the open errors print the system message only.

Unreadable file (`decision:2026-11-13-default-db-unreadable`, Itay chose A): a `default-db` that exists but cannot be opened or read fails with `cannot read default-db`, code `cannot_open` (mapped in `code_for_store_text`), and no fallback. ENOENT and ENOTDIR on the open mean the file does not exist and count as no file. The fix text and the JSON and plain-line forms are the existing `cannot_open` ones; no new copy but the message. Tests, red first: `util_resolve_default_db_directory_fails_with_no_fallback` (read error), `util_resolve_default_db_without_permission_fails_with_no_fallback` (mode 000, skipped as root), `util_resolve_default_db_symlink_loop_fails_with_no_fallback` (open error, also as root), and the `cannot read default-db` line in `store_text_codes_follow_the_locked_split`. `util_resolve_default_db_that_does_not_exist_uses_builtin` (dangling link, `~/.remember` not a folder) passed before the change and guards the exemption.

Mutants for the unreadable branch (each built, then the suite run):

| Mutant | Red tests |
|---|---|
| exemption replaced by `errno == EEXIST` | `..._missing_file_uses_builtin_and_creates_nothing`, `..._xdg_data_home_does_not_move_it`, `..._that_does_not_exist_uses_builtin`, `util_resolve_empty_env_falls_through_to_home`, `util_resolve_default_path_buffer_too_small` |
| `ENOTDIR` not exempt | `..._that_does_not_exist_uses_builtin` |
| `ENOENT` not exempt | `..._missing_file_uses_builtin_and_creates_nothing`, `..._xdg_data_home_does_not_move_it`, `..._that_does_not_exist_uses_builtin`, `util_resolve_empty_env_falls_through_to_home`, `util_resolve_default_path_buffer_too_small` |
| open failure falls back (returns 0) | `..._symlink_loop_fails_with_no_fallback`, `..._without_permission_fails_with_no_fallback` |
| read error ignored (`failed = 0`) | `..._directory_fails_with_no_fallback` |
| read error falls back | `..._directory_fails_with_no_fallback` |
| message `invalid database path` | `..._directory_fails_with_no_fallback` |
| mapping removed from `code_for_store_text` | `store_text_codes_follow_the_locked_split` |

(`if (0)` for the exemption did not build under `-Werror`; redone as `EEXIST`.)

Deviation (outside the plan's file list, approved by Itay 2026-11-13): commit `8ee5408` changes `scripts/codeql-local.sh` and `scripts/codeql-allow.tsv`. The content of `default-db` is a second CodeQL taint source ("string read by fread") next to the environment variable. It raised 8 `cpp/path-injection` alerts in `src/store_sqlite.c` (lines 1438, 1545, 1570, 1626, 2445, 2448, 2466, 2527) where the brain path is opened. The old rule needed one allow row to match every source of an alert, so no row could allow an alert with two sources. Now an alert passes when each source contains the text of some row for its rule and path, so every taint source needs its own reviewed row. The one new row covers `fread` only, only in `src/store_sqlite.c`. The Opus reviewer checks this change.

## In-session deep review (coder)

Gate on `a59abac`: sanitizer build and ctest 6/6, `lint-all.sh` OK (IWYU skipped locally; `ci-linux.sh` ran it, OK), `check-coverage.sh` 100% functions and effective lines, `ci-linux.sh` OK. `codeql-local.sh` failed with the 8 alerts above until `8ee5408`; its run on `8ee5408` (same `src/` as `a59abac`) printed `codeql-local: OK (45 allowed by scripts/codeql-allow.tsv)` with nothing blocked, run by the parent session. The count is 45, not 43 + 8, because CodeQL merged some existing env-only alerts in `store_sqlite.c` into the new two-source ones. `git diff --name-only 6c35803 | scripts/spec-drift.sh` exits 0 on the final tree, and `tests/test_spec_drift.sh` passes.

(S) Simplicity. `read_default_db`, `use_default_db` and `trim_ascii_space` are 3 small statics with one caller each; no flag, no option, no second path check. The two `set_err(..., "cannot read default-db")` lines repeat one literal; a helper would add more than it removes, so they stay. The empty-line check was deleted earlier (equivalent). `util_resolve_user_db_path` is untouched.

(A) Adversarial, a path read from a file is a trust boundary:
- Whoever can write `~/.remember/default-db` already owns the account. The path opens a SQLite brain with the user's rights, as `--db` does, after `copy_regular_db_path` (no `:memory:`, no `file:` URI). No error sentence echoes the path.
- Size: at most `PATH_MAX + 1` bytes are read; a longer file is `database path is too long`. A symlink to `/dev/zero` therefore ends in that error, not a hang.
- A NUL byte in the file ends the C string, so `/tmp/a<NUL>/b` selects `/tmp/a` (run, the brain was created there). Open item: no rule for it; the decision says nothing.
- A FIFO at `default-db` blocks `fopen` for every command (the same happens for `--db`). Open item: a non-regular-file rule is not in any decision; not built.
- A second line stays in the string and is part of the path; invalid UTF-8 is not checked. The plan says to add no rule for either.
- No TOCTOU worth a rule: the file is opened once and read once; the open error is read from `errno` right after `fopen`.
- Decision states: `decision:2026-11-13-default-db-unreadable` and `decision:2026-11-11-default-db-home` are `state:implemented`; `decision:ui-overhaul-grill-2026-10-12` stays accepted (one item built).

No finding needed a fix. Mutants for the whole resolver are in the tables above; the survivor (`REMEMBER_DB` read before `--db`) is old behavior. (Second opinion: not a survivor; the black-box suite kills it.)

## Review Notes

Second opinion (Opus, 2026-11-14), on main dcdcb51 (rebase: one `specs/README.md` table conflict, both rows kept).

Grill answered by Itay: `decision:2026-11-14-plan27-review-grill` (G1-G4 option A). Built here:
- G1: `read_default_db` opens with `open(O_RDONLY | O_NONBLOCK | O_CLOEXEC)`, checks `fstat` for a regular file, then `fdopen`. A FIFO, a socket, a device, or a symlink to one is `cannot read default-db`. Red first: the `/dev/null` case resolved as no file, the FIFO case hung until the timeout. A folder now fails at `fstat` instead of at the read.
- G2: a NUL byte anywhere in the bytes read is `cannot read default-db`. Red first: `/tmp/a<NUL>/b` resolved to `/tmp/a`.
- G3: `specs/DEFAULT-DB.md` says a writer replaces the file atomically (temp file in `~/.remember`, then rename); M53 builds it.
- G4: the per-source CodeQL rule is written in `scripts/codeql-local.sh` and `docs/QUALITY.md`.

Auto-fixes:
- CodeQL allowlist hole (security tooling). With 8ee5408, the row `string read by fread` matched the source's text only, so an `fread` in any file (or a source with no location) reaching a `src/store_sqlite.c` sink passed. Main had the same hole for any `read()` reaching `src/cmd_export.c`. Each source now matches as `<source file>: <text>` (from the SARIF `relatedLocations` id), and the two rows name their file: `src/util.c: user input (string read by fread)`, `src/fileio.c: user input (buffer read by read)`. The filter moved to `scripts/codeql-allow.jq`. `codeql-local.sh` runs it on `tests/codeql-allow/alerts.sarif` (13 alerts) against the real allowlist before it builds, and fails unless it gives `tests/codeql-allow/expected.tsv`. The 8ee5408 filter fails that fixture on alerts 2, 3, 4 and 8. Filter mutants, all killed by the fixture: rule ignored, path ignored, `all` to `any`, no source file, relatedLocations id ignored, zero-source branch to true, zero-source branch to false, links-only scan. Real SARIF of this tree: 45 allowed, 0 blocked.
- "No weaker than main" for alerts that do not involve the default-db read: true for single-source and zero-source alerts (same rule). A multi-source alert whose sources each have their own row now passes where main blocked it (environment + `random_hex` in `src/cmd_export.c`; command-line argument + `read by` in `tests/harness.c`). Itay accepted that as the rule (G4).
- Read-limit boundary: the mutant `DEFAULT_DB_READ_MAX = PATH_MAX` survived the whole gate. `util_resolve_default_db_read_limit_is_path_max_bytes` kills it (a `PATH_MAX`-byte file is read whole; one byte more is too long).
- `src/util.h` named the old order; it names `default-db` now.

Mutants of the reader after G1/G2 (each built, then `coverage_edges,errors` run with a 60 s cap): no `S_ISREG` (killed: not_a_regular_file), no `O_NONBLOCK` (killed: the FIFO case hangs, capped), no NUL check (killed: nul_byte), `failed` starts 0 (killed: 4 tests), `close(fd)` dropped on the non-regular path (killed: the lowest-free-descriptor probe in not_a_regular_file), `ENOENT`/`ENOTDIR` not exempt (killed), read max `PATH_MAX` (killed). Survivor: `ferror` dropped. A read error after a good open and `fstat` of a regular file needs I/O fault injection, and the folder case that hit it before now stops at `fstat`. The clause stays for EIO (decision: an I/O error fails).

(S) Simplicity:
1. Done: `GivenHome` keeps the default-db path. Seven `snprintf` copies and five `char file[96]` locals went away (about 12 lines).
2. Done: `read_default_db` has one `cannot read default-db` site for open, type, read and NUL failures (was two), with a `failed` flag instead of comparing a pointer after `fclose`.
3. Declined: folding the built-in fallback into `use_default_db` so the resolver returns its result directly saves about 4 lines but makes the default-db helper own the remember.db path.
4. Declined: a table for the three precedence tests saves about 12 lines but loses the names the Definition of Done cites.

(A) Adversarial (probes on the binary with a temp `HOME`): FIFO (fixed, G1); a symlink to `/dev/stdin` read the path from stdin (fixed, G1: a device); NUL (fixed, G2); ESC, newline and a 300-byte component: no error sentence echoes the path; `/` and a folder as the path: `SQLITE_CANTOPEN`, no crash; a second line is part of the path, and the brain's parent folders get created with that name (the plan rules it out of scope); huge file or `/dev/zero`: capped at `PATH_MAX + 1` bytes; torn write by the app (G3, spec); TOCTOU: the file is opened once, and type and content come from that descriptor. `--db` still gets around a broken `default-db`.

Grades:
- Coder (Sonnet): 7/10. Strengths: small statics with one caller each; red-first tests; full mutant tables; the unreadable-file grill was raised and built cleanly; the FIFO and NUL holes were found and written down. Misses: FIFO and NUL went into the notes as open items rather than to the grill (standard:coding 8); the allowlist row was never attacked with an alert from a different source file; the read-limit boundary was not tested; one "survivor" was claimed without running the black-box suite; the util.h comment was stale.
- Previous reviewer (coder's in-session deep review): 6/10. The (A) pass listed the right threats but closed none, and it called "no TOCTOU worth a rule" without asking who writes the file and how. It did not test the CodeQL change as a gate (no injected alert). The (S) pass was real but short of three candidates.
- Paste-ready pointers:
  1. When a change edits a gate or an allowlist, feed it known alerts through the real filter, including one from a source the entry is not meant to cover (another file, no location), and keep that fixture in the gate.
  2. For a file the engine reads and another program writes, state in the spec how the writer replaces it (atomic rename), and probe what a reader does with a cut-off copy.
  3. An error path that opens a descriptor gets a lowest-free-descriptor probe, so a leak turns a test red.

