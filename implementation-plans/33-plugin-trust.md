# 33 — Plugin trust file and the confirm callback

**Area:** `src/plugin_trust.c` (new), `src/plugin_trust.h` (new), and the CLI TTY confirm function `cli_confirm_typed_name` in `src/cli.c`.
**Needs:** plan 25. No other plan. (The confirm callback type is already in `src/remember_env.h` on main.)
**Blocked on:** nothing.

## Goal

Trust belongs to the code hash. Remember records trust only after the host confirm callback (or the CLI typed-name prompt) says yes, and refuses to run code whose hash changed.

## Source

- `decision:2026-11-12-trust-file` (X2 A): the file is G4's location and format (`<data dir>/trust.json`, format `remember-trust`) plus H-9.2's hashing of the arguments and the interpreter, not only the executable. This supersedes the location and format in log 010 H-9.1, H-9.2 and `decision:g-29` (`<data dir>/plugins/trust.json`, format `remember-plugin-trust`) and amends the single-executable hash in `decision:2026-11-11-trust-file` (G4 A).
- `decision:2026-11-12-plugin-approval-required-exit-5`: no TTY and no callback exits 5, like `confirmation_required` (`decision:2026-11-12-e2-confirmation-required-exits-5-everywhere`); `plugin_approval_denied` stays exit 1. `decision:2026-11-12-hook-approval-required-exit-5` (H-Q19 A, Itay 2026-11-12) extends that to `hook_approval_required`: a hook confirm with no TTY and no callback also exits 5.
- Log 010 H-9.1 to H-9.7, H-Q19 (hashing, confirm and mismatch rules; the file path and format field follow X2 A, not H-9.1); `decision:s-05`, `decision:g-29` (hashing only), `decision:g2-09` (no HMAC), `decision:g2-11`, `decision:plugin-permission-prompt`, `decision:g2-13` (NULL confirm is never yes).
- Old plan 25 "Trust and install", paragraphs 1 to 5.
- 007 test 23: untrusted `settings set plugin.<name>.enabled true` fails with `plugin_untrusted` and prints C-17.
- Copy: C-15, C-16, C-17 and the C-01 trust prompt in `COPY-PENDING.md`.
- Spec: `specs/PLUGIN-TRUST.md` (new).

## Out of scope

The commands that call trust (`plugin add`, `plugin hook`, `plugin trust`: plan 34). Runs (plan 35).

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md). Tests in `tests/test_plugin_trust.c` (new, suite `plugins`). SHA-256 uses the existing hash code in the repo; no new library.

## What must be true

File. `<data dir>/trust.json`, where data dir is `RememberEnv.data_dir` (the same directory as `brains.json`, `decision:g-01` / G2 A). Mode 0600. The JSON object is `{"format":"remember-trust","version":1,"entries":[{kind, name, path, folder_sha256, manifest_sha256, files:[{path, sha256, size, mtime}], interpreter:{path, sha256}|null, needs, trusted_at, dev}]}`. `kind` is `plugin` or `hook`. `path` is the plugin folder or the hook command path. There is no `<data dir>/plugins/trust.json` and no `<plugins_dir>/trust.json`. Written by temp file and `rename`. No HMAC. No Keychain, libsecret or polkit.

Hash. Do not hash only the executable. Hash the plugin folder and the manifest. Also hash `command[0]` and every later argv item that names an existing regular file, at most 8 files, in argv order. When `command[0]` is `sh`, `bash`, `zsh`, `python`, `python3`, `node`, `ruby`, `perl`, `deno`, `bun`, or `/usr/bin/env` followed by one of those, also store the interpreter realpath and SHA-256. For `sh -c "<inline>"`, the inline text is part of the manifest hash. A built-in has no record and is trusted. A mismatch of any stored hash means untrusted: the run is skipped with `plugin_untrusted` and nothing is deleted.

Confirm. `plugin_trust_confirm(const RememberEnv *env, const char *name, const char *body)` calls `env->confirm(ctx, "plugin", name, body)`. NULL confirm: exit 5, record nothing. The code is `plugin_approval_required` for kind `plugin` (print C-15) and `hook_approval_required` for kind `hook` (the E2 rule, `decision:2026-11-12-plugin-approval-required-exit-5` and `decision:2026-11-12-hook-approval-required-exit-5`; log 010 H-Q19 said exit 1 for both). The wording of `hook_approval_required` is not approved yet; it is a pending copy item for Story Weaver, and the plugin code keeps C-15. False: exit 1 `plugin_approval_denied`, record nothing, print C-16. The CLI's confirm (on a TTY) prints the body, then `Type "<name>" to allow it, or press Enter to cancel:` and returns true only when the typed text equals the name. No TTY means the CLI passes no callback. No `--confirm` flag. Folder body: `Allow "<name>" to read your memories? It can send them off this Mac.` followed by the needs, permissions and settings, one per line. Hook body: `Allow hook "<name>" to run <path>?` An update shows only the changed needs.

Check. `plugin_trust_check(name)` compares the record to the files. A mismatch returns `untrusted`; the caller (plan 35, 36) does not run, sets `disabled_reason` `untrusted` in that brain's `index.json`, and warns `plugin_untrusted` with C-17. `plugin trust` re-hashes, shows the changed needs, and asks again. True stores the new hash and sets `disabled_reason` to null. `enabled` stays.

Settings. `settings set plugin.<name>.enabled true` for an installed plugin with no matching record fails with `plugin_untrusted` and prints C-17 (007 test 23).

- **Warnings carry a `fix` (E3 B, `decision:2026-11-12-e3-warnings-have-fix-field`).** Every warning this plan prints has a `message` and a non-empty `fix`: `plugin_untrusted`. The wording is copy item E3B-1, entry 11, in `decision:2026-11-12-copy-batch-e3b-warning-splits`: the approved sentence stays word for word and only its next step moves into `fix`; the plain line is `remember: warning: <message> <fix>`. `code`, `detail`, exit codes and `--strict` do not change. Itay approved E3B-1 as a whole on 2026-11-12, including every `fix` that is Story Weaver wording (`decision:2026-11-12-copy-batch-e3b-warning-splits`).

## Review points

- No code path records trust without a true confirm return.
- `trust.json` is rewritten only through `rename`.

## Definition of Done

- [x] `record_is_data_dir_trust_json` (path `<data dir>/trust.json`, format `remember-trust`, mode 0600, `entries` with `kind` `plugin` or `hook`; no file under `plugins/trust.json`).
- [x] `record_fields_h_9_2` (folder, manifest, files and interpreter hash fields).
- [x] `hash_argv_files_max_8` and `interpreter_hashed` and `sh_c_inline_in_manifest_hash`.
- [x] `null_confirm_exits_5_c15_records_nothing` (exit 5, code `plugin_approval_required`), `null_confirm_hook_exits_5_records_nothing` (exit 5, code `hook_approval_required`) and `false_confirm_exits_1_c16_records_nothing` (exit 1, code `plugin_approval_denied`) (010 test 13, first half). The codes and exits are proven by `c15_plugin_approval_required_prints_message_and_fix_and_exits_5`, `a_hook_with_no_callback_exits_5_with_the_hook_code_and_the_c15_words` and `c16_plugin_approval_denied_prints_message_and_fix_and_exits_1`.
- [x] `typed_name_must_match` (old probe 1; also `the_cli_confirm_records_trust_only_for_the_typed_name`).
- [x] `mismatch_sets_untrusted_keeps_enabled` and `trust_clears_reason` (010 test 13; `an_edit_after_trust_keeps_enabled_and_refuses_the_next_set_true` drives the CLI).
- [x] `untrusted_enable_setting_fails_c17` (007 test 23).
- [x] `builtin_trusted_no_record`.
- [x] `specs/PLUGIN-TRUST.md` written; the decisions above are `state:implemented`.
- [ ] The gate passes on the committed tree. (See Implementation Notes for what ran.)

## Implementation Notes

Built by the plan 33 coder session (2026-11-22). Decisions answered by Itay in that session: `decision:2026-11-22-plan33-folder-hash`, `-hash-caps`, `-many-entries`, `-argv-paths`, `-enable-guard-error`, `-import-not-guarded`, `-trust-json-unreadable`, `-trust-lock`. Open copy items filed for Story Weaver: `decision:open-2026-11-22-plan33-never-allowed-copy`, `-trust-json-unreadable-copy`, `-hook-approval-copy`, `-trust-body-lines`.

Plan corrections (the plan text above stands as written):

- `RememberEnv.data_dir` does not exist on main (plan 29 adds it). Every function takes `data_dir` as an argument; plan 29's callers pass `env->data_dir`. `src/remember_env.h` is not touched.
- `plugin_trust_confirm` and `plugin_trust_allow` take the kind (`PLUGIN_TRUST_KIND_PLUGIN` or `_HOOK`), because the plan makes the NULL-callback code depend on it.
- `plugin_trust_check(name)` takes the code to hash (`PluginTrustSubject`): a check that read the paths out of `trust.json` would let a hand-edited file point it at a harmless folder. `plugin_trust_check_installed(data_dir, name, search_path)` is the by-name form.
- `plugin_trust_allow` is the one writer: hash, ask, then write under the lock. No code path records trust without a true confirm return.
- `disabled_reason` in `index.json` is plan 36's. `mismatch_sets_untrusted_keeps_enabled` and `trust_clears_reason` prove this module's part: a mismatch changes and deletes nothing and leaves `enabled` alone, and an allow after the edit makes the next check pass.
- The hook's `hook_approval_required` has no approved words; it prints C-15's. C-15's and C-16's `<command>` is a parameter of `diag_plugin_approval`.
- The catalog fix of the four new error codes is a draft that no command prints.
- The plan says folder files are `files`: H-9.1 says files outside the folder, and `decision:2026-11-22-plan33-argv-paths` follows H-9.1 (a folder file is covered by `folder_sha256`).

Also changed: SHA-256 stays in `normalize.c` (`hash_stream_*`, `body_hash_hex` is built on it); the lock moved from `brain_list.c` to `fileio_lock_open`; `app_err_block` in `appio.c` writes the multi-line body; `harness_install_hooker` records trust (and `harness_trust_hooker` for a test that writes the manifest itself), because `settings set plugin.hooker.enabled true` is guarded now.

Mutation check (49 one-clause mutants of `src/plugin_trust.c`, each built with the store-test flags and run against the 88 trust tests under ASan/UBSan; the runner is `register_plugin_trust_tests` alone, so a mutant that survives is one the trust tests cannot see). 45 were killed (assert or ASan abort). Four survive, each with its reason:

- `no_sort` (drop the `qsort` of the folder list): the order of `readdir` on APFS is already the byte order of the names, so `the_folder_hash_does_not_depend_on_the_order_the_files_were_made` cannot turn red here. The Linux run (ext4 and overlayfs return another order) is what proves it.
- `eof_always` (the digest is taken even when the read loop broke on an error or the cap): the fstat cap stops a file over 512 MiB first, and a failed `read` needs a fault. No test drives it.
- `errno_check_off` (a `readdir` that stopped on an error is not a failure): no portable fault.
- `needs_update_all` (the length pass counts every need, old or new): it only over-allocates the body buffer. Equivalent.

The first run of the mutants left 17 survivors. Each got a test: `commands_max` (a subject of 33 valid commands), `dirs_ge` (4096 folders), `entries_cap_commit` and the read cap (1024 and 1025 entries), `env_argc` (a bare `/usr/bin/env`), `files_cap_gt8` and `no_dedupe` (eight files over two commands, a file named twice), `hex_len` (65 digits), `inline_any_shell` (`python3 -c`), `same_file_path`, `same_interp_path`, `same_interp_sha` (the same bytes under another path, an env interpreter that changes, a `PATH` that finds another copy), `same_kind`. `is_one_segment` was deleted: a name that is not one segment cannot equal the name in a parsed manifest, so the guard was dead code.

Commands that prove the claims (all from the worktree):

- Build and the whole gate list: `cmake -S . -B build -DREMEMBER_TEST_VFS=ON && cmake --build build && (cd build && ctest --output-on-failure)` (step_gate and store_asan_gate pass; `plugins` is in `tests/gate-suites`).
- Format: `clang-format --dry-run --Werror` (llvm@22) on every changed C file.
- Lint: `PATH=/opt/homebrew/opt/llvm@22/bin:$PATH ./scripts/lint-all.sh` (format, boundaries, cppcheck, gcc -fanalyzer, clang-tidy 22, scan-build).
- Coverage: `PATH=/opt/homebrew/opt/llvm@22/bin:$PATH ./scripts/check-coverage.sh` (functions 100%, effective lines 100%, pure-TU branches 100%).
- Linux: `./scripts/ci-linux.sh --ubuntu=24.04` (lint with clang-tidy 18 and IWYU found two includes and the `getenv` NOLINT line; fixed).
- The trust tests alone under ASan/UBSan: link the objects of `remember_store_tests` with a main that calls `register_plugin_trust_tests` (88 tests, 0 failures).

Simplify pass, three cuts made: `is_one_segment` deleted (12 lines: a name that is not one segment cannot equal the name in a parsed manifest); `diag.c`'s own printf helper `sentence()` deleted for the existing `fmt_alloc` (22 lines); the lock code moved out of `brain_list.c` into `fileio_lock_open`, so two files share one copy (35 lines fewer in `brain_list.c`). SHA-256 reuses `normalize.c` (`body_hash_hex` is now built on `hash_stream_*`). Regions that cannot shrink: the folder walk (one recursion, depth-capped), the record reader (one `has_exactly` per object) and the check (one `same_code`).

Ceilings, named: no cap on the total bytes hashed in one check (4096 files of 512 MiB are each allowed); a check always re-hashes (a size and mtime fast path is plan 35's); a write waits at most 2 s for the lock; the readdir of the folder fails the hash on an error but is not driven by a test (no portable fault).

## Review Notes
