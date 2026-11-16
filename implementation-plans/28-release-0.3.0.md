# 28 — Release 0.3.0

**Area:** the version constant, `README.md`, `skills/remember/SKILL.md`, and `scripts/remember-install-skill`.
**Needs:** plans 22, 23, 25, 26, 27, 27a and 27b merged. No other plan. (The text "every lower-numbered plan … still open" is gone: Itay's rule of 2026-11-11 21:38.)
**Blocked on:** nothing.

## Goal

The tree says `0.3.0`, and the skill and What's New use the approved sentences. This plan does not tag and does not publish.

## Source

- `decision:main-version-label` (one version constant), implemented by this plan with no `-dev` step (`decision:2026-11-11-no-dev-label`, P9 A).
- `decision:2026-11-11-skill-settings-bullet` (P8 A).
- `decision:2026-11-11-plugin-host-0-4-0` (P1 A): 0.3.0 has no plugin host; What's New names none of it.
- `decision:release-0-3-0-scope`
- `decision:2026-11-07-bg-rem-06-releases` (0.3.0 stops at this release train; wiki-links stay out).
- `decision:2026-11-11-codex-skill-path`
- `decision:min-reader-version` and `decision:g-20` (`min_reader_version` is already in the brain).
- Sentences: [`COPY-PENDING.md`](COPY-PENDING.md) C-40 and C-41. Itay approved that file on 2026-10-25.
- [`docs/RELEASING.md`](../docs/RELEASING.md). Itay tags. This plan does not.
- Spec: `specs/RELEASE-0.3.0.md` (new; kept per P25 B, uppercase per P24 A). It lists what 0.3.0 contains, one line per plan 22, 23, 25, 26, 27, 27a and 27b, each with its spec file name.

## Out of scope

A git tag, a GitHub release, and the Homebrew formula. Wiki-links. Any feature from a plan that is not extracted.

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md).

The version text is `0.3.0` in both of these, and the two texts match:

- `project(remember VERSION ...)` in `CMakeLists.txt`
- the `REMEMBER_VERSION` fallback in `src/commands_common.h` (the plan said `src/remember_app.c`; that file only prints the value, found by `grep -rn REMEMBER_VERSION src`)

Do not add a second constant.

## What must be true

- `--version` prints `0.3.0`.
- The skill's bucket sentence is C-40 and no other bucket sentence:

> Tags that start with a bucket prefix group memories into buckets, for example `project:fortkeeps`. Bucket prefixes are the brain setting `remember.buckets.prefixes`, which is `project:` and `status:` by default; run `remember settings get remember.buckets.prefixes` to see the list in use.

- The skill does not contain `remember config get bucket-prefixes`, `--json-errors`, `--offset`, `--sync-id`, `--source`, or `purge-trash`.
- README What's New for 0.3.0 contains the C-41 bullets and no others from this plan:

- `--json` now puts success and failure in one `remember-message` version 1 envelope on stdout, and stderr stays empty. `--json-errors` is gone.
- Lists page with a cursor. Search doesn't page. `--offset` is gone.
- A memory's id in output is `id`, a lowercase hyphenated UUID. `--sync-id` is gone. The integer `local_id` appears only in CLI output.
- Actors replace `source`. `--source` is gone completely.
- `--trash` now means the Trash: memories you deleted. For expired memories, use `--expired`. For this release, `--trash` prints: `remember: warning: --trash now means the Trash (deleted memories). For expired memories, use --expired.`
- `purge-trash` is gone. To delete memories in the Trash for good, run `remember purge <locator>` for each one.

- The skill contains the C-41 skill bullets:

- Use `--json` to get one `remember-message` version 1 envelope on stdout, for success and failure alike. There is no `--json-errors`.
- To page a list, pass back its `next_cursor`. Search returns its top results and doesn't page. There is no `--offset`.
- Refer to a memory by `id`, a lowercase hyphenated UUID. The number `local_id` is for this device's CLI only. There is no `--sync-id`.
- Writes record actors, not `source`. There is no `--source`.
- `--trash` is the Trash (deleted memories). `--expired` is expired memories.
- To empty the Trash, run `remember purge <locator>` once for each memory in it. There is no `purge-trash`.
- For scripts and agents, use `--json`, because human dates are local.

- `scripts/remember-install-skill` adds `~/.codex/skills` when `~/.codex` is a directory. The installed file is `~/.codex/skills/remember/SKILL.md`.
- The script uses the symlink mode and the copy mode it already uses for `~/.grok`, `~/.claude`, and `~/.cursor`. A missing `~/.codex` adds no Codex path.
- When no product directory exists, the error is `error: no agent skill roots found (~/.grok, ~/.claude, ~/.cursor, ~/.codex);`
- The git history of this plan contains no tag.

- **Warnings carry a `fix` (E3 B, `decision:2026-11-12-e3-warnings-have-fix-field`).** Every warning this plan prints has a `message` and a non-empty `fix`: `flag_meaning_changed` (the README line of C-41 quotes the same plain line). The wording is copy item E3B-1, entry 1, in `decision:2026-11-12-copy-batch-e3b-warning-splits`: the approved sentence stays word for word and only its next step moves into `fix`; the plain line is `remember: warning: <message> <fix>`. `code`, `detail`, exit codes and `--strict` do not change. Itay approved E3B-1 as a whole on 2026-11-12, including every `fix` that is Story Weaver wording (`decision:2026-11-12-copy-batch-e3b-warning-splits`).
- **The `synced_folder` warning takes E3B-2 (approved by Itay 2026-11-12, option A, `decision:2026-11-12-copy-batch-e3b-warning-splits`).** The existing warning (plan 16 D5, printed from `src/remember_app.c` when the database path looks synced) has `message` `this brain looks like it is in a synced folder (iCloud Drive, Dropbox or Google Drive), and syncing can damage it.` and `fix` `Move the brain to a folder on a local disk.` The plain line is `remember: warning: <message> <fix>`. It replaces the old sentence `database path looks like a synced folder (iCloud/Dropbox/Google Drive); corruption risk - prefer a local disk`. `code`, `detail`, exit codes and `--strict` do not change.

## Review points

- The reviewer checks that `CMakeLists.txt` and `src/remember_app.c` contain the same version text.
- The reviewer checks that a home with only `~/.codex` receives `~/.codex/skills/remember/SKILL.md` and no other product path.
- The reviewer checks `git tag --points-at HEAD` and finds no release tag from this plan.
- The reviewer checks that wiki-links are not in the What's New list.

## Definition of Done

- [x] `--version` prints `0.3.0` from the CMake constant and from the fallback, in two builds. (`release_text`: `--version prints 0.3.0 from the CMake constant`, `--version text from the fallback build is 0.3.0`, and the two version texts match.)
- [x] `the skill bucket sentence is the C-40 paragraph` (`release_text`; `the skill has no other bucket sentence`).
- [x] `the skill and README contain the C-41 bullets and do not contain --json-errors, --offset, --sync-id, --source, or purge-trash` (`release_text`; read the clause as "outside the C-41 lines", see note 2).
- [x] `a fake home with only ~/.codex installs ~/.codex/skills/remember/SKILL.md` (`tests/test_install_skill.sh`, ctest `install_skill`).
- [x] `a fake home with no product directory prints the error line that names ~/.codex` (`install_skill`).
- [x] `git tag --points-at HEAD` shows no tag created by this plan (run in the notes, not a test: a later tag would turn a test red).
- [x] `the skill does not contain "see C-13"` (`release_text: the skill does not contain 'see C-13'`).
- [x] `synced_folder_warning_has_message_and_fix`: a database under a `Dropbox` path prints the E3B-2 plain line, the JSON warning carries `code`, `message` and `fix`, and the old sentence is gone.
- [x] `specs/RELEASE-0.3.0.md` is written (`release_text`: `RELEASE-0.3.0 has one line for plan N`, `plan N names specs/F, which exists`, `lists R27b-2 and R27b-3 with PR #75`), and the decisions this plan built are `state:implemented` (note 9).
- [x] The gate passes on the committed tree (note 13).

## Implementation Notes

Branch `claude/plan-28-release-0-3-0-0066c5`, from main `d2b4977` (plan 26 merged). No tag, no push, no PR.

1. **Plan text corrected.** The `REMEMBER_VERSION` fallback is in `src/commands_common.h`, not `src/remember_app.c` (`grep -rn "define REMEMBER_VERSION" src`). The plan and `docs/RELEASING.md` named the wrong file; both are fixed. `src/store_sqlite.c` and `docs/engineering-notes.md` said the release number would move in plan 26; both now say it is a separate constant from `k_reader_version`.
2. **One reading of "does not contain".** The plan says the skill and README must contain the C-41 bullets and must not contain `--json-errors`, `--offset`, `--sync-id`, `--source` or `purge-trash`. The C-41 bullets themselves name all five. The only reading that can hold: the names appear in the C-41 lines and nowhere else (README: in its v0.3.0 section; the 0.2.0 section is history and keeps them). `tests/test_release.sh` strips the C-41 lines and fails on any other line that names one. To make that true I removed four sentences from the skill that said the same thing (`--sync-id` in the locator list, `There is no --source`, `--offset is an unknown option`, `purge-trash does not exist`) and three phrases from the README's v0.3.0 section (`--json-errors is gone`, `--sync-id` in the id bullet, `--offset` in the digits bullet). Nothing else in those bullets changed.
3. **The README said `user_version` 5.** A fresh brain is 6 (`sqlite3 ... PRAGMA user_version` run: `6`; `enum { SCHEMA_VERSION = 6`). The sentence now says **6**, and `release_text` compares it to `SCHEMA_VERSION`.
4. **C-41 sentence 6 is false in this binary (grill, below).** `remember purge <locator>` is plan 32. Run: `remember --db X purge 1` prints `remember: purge takes no positional arguments`, exit 1. Both the README bullet and the skill bullet are the approved words, copied as written.
5. **Warnings.** `diag_warning_split_detail` (`src/diag.h`) is the one writer of a warning with a `fix`; `diag_warning_detail` calls it with no fix and the static `diag_warning_split` is gone (its two callers pass `NULL, 0U`). `flag_meaning_changed` and `synced_folder` now print E3B-1 entry 1 and E3B-2. The plain lines are byte for byte what they were for `--trash` and are the new sentence for `synced_folder`. `code`, `detail`, exit codes and `--strict` did not change. **Other warnings still print no `fix`** (found by grepping every caller of `diag_warning` and `diag_warning_detail`): `raw_query_ignored`, `unknown_field`, `setting_invalid`, `setting_import_skipped`. Run: `search 'hello AND' --json` gives `{"code":"raw_query_ignored","message":"fts5: syntax error near \"\""}` with no `fix`. This plan prints neither, so it does not touch them (grill, below).
6. **Installer.** `~/.codex` joins the list when it is a directory (`-d`: a file or a dangling link adds nothing; a link to a directory counts). The error names `~/.codex`; `scripts/install.sh --help`, README (3 places) and `docs/getting-started.md` say so. Deep review found two defects that applied to every product and fixed both with a failing test first: (a) `SKILL.md` that is a directory made `ln` and `install` nest the file inside it and print `skill:` with exit 0; (b) a `~/.codex/skills/remember` symlink to the repo's own `skills/remember` made `ln -sfn` turn the source `SKILL.md` into a link to itself (`cat` then fails with `Too many levels of symbolic links`; run before the fix). Both now warn and skip, and the exit is 1 when nothing else installed. Probed and fine: `~/.codex` a file, a dangling link, `~/.codex/skills` a file, `skills/remember` a file, a home with spaces, an old `SKILL.md` link to another file or to nothing (replaced, the other file untouched) in both modes. Not tested on this Mac: GNU `install` on a link that points at the source (Linux `ci-linux.sh` runs `install_skill`).
7. **Red first.** Before any production change: `release_text` 3 red (version) plus the copy and spec lines; `install_skill` 13 red; 10 C tests red (`trash_lists_deleted_and_expired_lists_expired`, `json_trash_warning_is_in_the_list_envelope`, `json_synced_folder_warning_is_in_the_list_envelope`, `synced_folder_warning_has_message_and_fix`, `envelope_failure_keeps_its_type_and_carries_the_warnings_after_the_error`, `envelope_warnings_keep_their_order_with_a_comma_between`, `envelope_color_and_quiet_are_accepted_as_globals`, `envelope_color_follows_the_tty_and_the_environment`, `envelope_progress_is_a_stderr_line_and_quiet_json_stop_it`, plus `envelope_fixed_ids_make_replies_predictable`, which was red only because my first configure had no `REMEMBER_TEST_VFS=ON`; green once configured as CI does). Later reds: the SKILL.md-directory and self-overwrite installer tests, the `user_version` README test, `split_warning_joins_message_and_fix_and_rejects_bad_input`.
8. **Mutants, 44, all killed** (harness in the scratchpad: edit one file, rebuild, run `errors,envelope,sync_output,get_list_delete` or the shell test, restore). Version: CMake `0.2.0`, fallback `0.3.1`. Warning writer: no fix, no detail, `detail` count 0, `use` swapped, message and fix swapped (both warnings), old `--trash` sentence kept in the message, empty fix, a typo in the fix (both warnings), the `name == NULL` guard, the `fields == NULL && nfields > 0` guard, `diag_warning_detail` passing a fix. Installer: no Codex branch, `skill` typo, error line without `~/.codex`, `-e` for `-d` (this one survived until I added the assert that a `~/.codex` file is "no root", not a failed mkdir), `SKILL.md`-directory guard removed, self-source guard removed, `cp` for `ln`. Copy: C-40 reworded, a second bucket sentence, `remember config get bucket-prefixes`, each of the five removed names and `see C-13` added elsewhere in the skill, a C-41 bullet reworded in each file, the last skill bullet changed, each of the three README overlaps put back, a wiki-links bullet. Spec: plan 26 line gone, plan 27a with no file, a missing file name, R27b line gone. One run was not a kill (`flag detail dropped`: exit 2, unused variable, did not build); I redid it as `detail` count 0.
9. **Decisions.** `state:implemented`: `decision:main-version-label`, `decision:2026-11-11-no-dev-label`, `decision:2026-11-11-skill-settings-bullet`, `decision:2026-11-11-plugin-host-0-4-0`, `decision:2026-11-11-codex-skill-path`, `decision:2026-11-11-keep-release-spec`. Left `state:accepted`: `decision:release-0-3-0-scope` and `decision:2026-11-07-bg-rem-06-releases` (the release scope holds until Itay tags), `decision:2026-11-12-e3-warnings-have-fix-field` and `decision:2026-11-12-copy-batch-e3b-warning-splits` (of the 29 entries only 1, 4 and 27 are built, and entry 2 to 3 and 11 to 25 are plugin warnings no code prints yet; the four codes in note 5 are 5 to 10, 26 and 29).
10. **Simplify pass (3 candidates).** Inlined the static `diag_warning_split` wrapper (two callers; 5 lines gone). Kept `json_synced_folder_warning_is_in_the_list_envelope` beside the named test: the first pins the whole envelope and the warning's place in it, the second pins the plain line and that the old sentence is gone, so neither proves the other. Kept the C-41 lines as literals in `tests/test_release.sh`: they are the oracle, and copying from the README would make the test pass on any text.
11. **Compiler warnings.** `-Wallocator-wrappers` on `store_sqlite.c:136,140,144` and `import_read.c:188` are in main already (not in a line this plan touches).
12. **Probes run.** `remember --version` is `remember 0.3.0`. `remember settings get remember.buckets.prefixes` prints `project: status:` (the C-40 command works). `git tag --points-at HEAD` prints nothing. Uncovered lines near the touched `src` lines: none (`build-cov/coverage.lcov`, lines 587 to 651 of `diag.c`, `commands_common.c` 590 to 600, `remember_app.c` 509 to 520).

13. **Gate, on commit 6fe688b** (the only later commit changes this plan file): `scripts/lint-all.sh` LINT OK (format, cppcheck, clang-tidy LLVM 22, scan-build; IWYU skipped on the Mac as the script says, run in `ci-linux.sh`); fresh `build` configured `-DCMAKE_BUILD_TYPE=Debug -DREMEMBER_ENABLE_SANITIZERS=ON -DREMEMBER_TEST_VFS=ON`, `ctest` 7 of 7 passed (`step_gate`, `step05_gate`, `store_asan_gate`, `install_skill`, `spec_drift`, `release_text`, `no_shim_symbols`); `scripts/check-coverage.sh` functions 100%, effective lines 100%, pure-TU branches 100%; `scripts/ci-linux.sh` OK (aarch64 here, amd64 on GitHub; I added no implicit int to char narrowing); `scripts/codeql-local.sh` OK (49 allowed entries, none added). `shellcheck -x` clean on `scripts/remember-install-skill`, `tests/test_release.sh`, `tests/test_install_skill.sh`. `git tag --points-at HEAD` prints nothing.

**Open for Itay (grills).**

- **G1. C-41 sentence 6 names a command 0.3.0 does not have.** README: `purge-trash` is gone. To delete memories in the Trash for good, run `remember purge <locator>` for each one. Skill: the same. Today `purge` takes `--expired` or `--deleted` only. Options: (A, as shipped) keep the approved words and accept that they are true only once plan 32 lands; (B) change both sentences to `remember purge --deleted`, which wipes the whole Trash and needs a copy approval; (C) hold the tag until plan 32. I recommend B or C: a person who copies the sentence gets exit 1.
- **G2. Four warning codes print no `fix`** (note 5). `decision:2026-11-12-e3-warnings-have-fix-field` says warnings carry one, and INDEX row 20 says plan 20 did it. Options: (A) a follow-up plan that splits E3B-1 entries 5 to 10, 26 and 29; (B) split them in this PR. I recommend A, as a task of its own before the tag.

## Review Notes

Second opinion (Opus, 2026-11-16), branch `claude/nervous-jang-1a25c9`, rebased onto main `37badf7` (Ubuntu 24.04 and 26.04 CI) with no conflict.

**Checked and holding.** The version text is `0.3.0` in `CMakeLists.txt` and in the `commands_common.h` fallback (the plan's `remember_app.c` was wrong; the coder's correction holds), and `--version` prints it. The C-40 paragraph and the C-41 bullets match `COPY-PENDING.md` word for word, with the `see C-13` clause dropped per `decision:2026-11-11-skill-settings-bullet`. The E3B-1 entry 1 and E3B-2 splits match the decision word for word in plain and JSON output. A home with only `~/.codex` gets only `~/.codex/skills/remember/SKILL.md`. `git tag --points-at HEAD` is empty. What's New names no wiki-links and no plugin host. The six decisions the plan built are `state:implemented`. Every plan line of `specs/RELEASE-0.3.0.md` names code that exists and a spec that describes it.

**Fixed here, each with a test that failed first (`tests/test_install_skill.sh` 1l to 1o).**
- (A) A `HOME` or a `REMEMBER_SKILL_DIRS` entry with a newline in it was split into two paths. The skill landed under the path's first line and in the current folder. The list is now built as an array: one loop over `.grok .claude .cursor .codex`, and `read -d ''` for the variable.
- (A) A relative `REMEMBER_SKILL_SRC` made a dangling link, and the script still exited 0. The source is now made absolute against `$PWD`, which keeps the Homebrew `share/` path unresolved.
- (A) `--copy` over a link that symlink mode left to the same source failed with `same file` (exit 64) and stopped before the next product. Copy mode now removes the old `SKILL.md` first. That removal sits after the source-directory guard, so it can never delete the source.
- Docs. `RELEASE-0.3.0.md` said only plans 19 to 21 and 24 shipped beyond its list, but `v0.2.0` was tagged before plan 15, so plans 15 to 18 (with 17a, 18b and 18c) ship too. The README said the device id sidecar is `<db-path>.device_id`; it is `<db-path>.d/device_id`.
- Mutants on the installer: 9, all killed. They were: `|| true` dropped, `rm` dropped, the absolute step dropped, a here-string read, `.codex` dropped, `-e` for `-d`, a `skills` typo, the source-directory guard removed, and the `SKILL.md`-directory guard removed.

**(S) Simplify.**
1. `default_skill_dirs` plus the line read became one 4-line loop, 12 lines saved. Done.
2. `diag_warning` now has one caller (`cmd_query.c`), and `diag_warning_detail` is a one-line wrapper. Calling the split writer directly would save about 6 lines, but that churns a header this plan does not own, so it is left alone.
3. `diag_brain_id_cloned` and `diag_brain_missing` repeat the measure, malloc and snprintf steps. A shared helper would save about 8 lines, but that is plan 26 code.
4. `test_release.sh` compiles a stub to read the fallback. That stub is the DoD's second build, so it stays.

**(A) Cleared.**
- `~/.codex` that is a file, a dangling link, or a link to a directory.
- `~/.codex/skills` that is a file.
- A `SKILL.md` directory, and a skill dir that is the source's own directory.
- Old links to another file, or to nothing, in both modes.
- A home with spaces, and with a newline.
- A dir that starts with `-` fails loudly (`cannot create`), with no write. A source that starts with `-` is now made absolute, so it installs.

Overwriting a hand-written `SKILL.md` follows `decision:2026-11-11-codex-skill-path`, which says to use the same modes as the other products, and those always replaced it.

**Gate** on `897715f` (the tree before these notes): `lint-all.sh` LINT OK, fresh sanitizer build `ctest` 7 of 7, `check-coverage.sh` 100% (functions, effective lines, pure-TU branches), `ci-linux.sh` OK on Ubuntu 24.04 and 26.04, `codeql-local.sh` OK. The diff adds no C (no int to char narrowing to check).

**Open for Itay.**
- G1: C-41 sentence 6 tells people to run `remember purge <locator>`, which is plan 32. 0.3.0 rejects it with exit 1, and the skill contradicts itself. The approved words are kept until Itay answers.
- G2: four warning codes print no `fix` (the coder's grill).

**Follow-ups.**
- A plan 26 bug: `brain new-id` does not update `brains.json`, so a brain copied after `new-id` makes the next open give the original a new id instead.
- What's New has no lines for plans 20, 23, 27 or 27b.
- What's New claims `--strict` is listed under Global options (it is not), and claims color, progress and prompts that no code produces.
- `import --help` lists `--from-md` and `--from-csv`, which plan 23a has not built.
- The full write-up is in the sessions' shared notes, `plan-28-review-grills.md`.

**Grades.**
- Coder: 8/10. Strengths: thorough mutants (44), red-first tests, two installer exploits caught, plan facts corrected (the fallback's file, `user_version` 6), and G1 and G2 raised rather than decided. Misses: newline paths, a relative source, and running one mode over the other mode's output. Also, the release spec's "Plans 19 to 21 and 24" was never checked against `git log v0.2.0..`.
- The coder's in-session review: 7/10. Its two locks (the self-link and the `SKILL.md` directory) held. It missed the three installer holes above and the false device-id sentence.

**Folded into `standard:coding`** (history copy `history:standard:coding:2026-11-16c`).
- PROBES rule 1 is sharpened: "a newline inside it" and "relative where the code keeps it for later", and a shell script keeps a list of paths in an array.
- PROBES sibling rule, one sentence added: run each mode of a command over what its other modes left.
