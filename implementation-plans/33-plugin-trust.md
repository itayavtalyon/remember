# 33 — Plugin trust file and the confirm callback

**Area:** `src/plugin_trust.c` (new), `src/plugin_trust.h` (new), and the CLI TTY confirm function `cli_confirm_typed_name` in `src/cli.c`.
**Needs:** plan 25. No other plan. (The confirm callback type is already in `src/remember_env.h` on main.)
**Blocked on:** nothing.

## Goal

Trust belongs to the code hash. Remember records trust only after the host confirm callback (or the CLI typed-name prompt) says yes, and refuses to run code whose hash changed.

## Source

- `decision:2026-11-12-trust-file` (X2 A): the file is G4's location and format (`<data dir>/trust.json`, format `remember-trust`) plus H-9.2's hashing of the arguments and the interpreter, not only the executable. This supersedes the location and format in log 010 H-9.1, H-9.2 and `decision:g-29` (`<data dir>/plugins/trust.json`, format `remember-plugin-trust`) and amends the single-executable hash in `decision:2026-11-11-trust-file` (G4 A).
- Log 010 H-9.1 to H-9.7, H-Q19 (hashing, confirm and mismatch rules; the file path and format field follow X2 A, not H-9.1); `decision:s-05`, `decision:g-29` (hashing only), `decision:g2-09` (no HMAC), `decision:g2-11`, `decision:plugin-permission-prompt`, `decision:g2-13` (NULL confirm is never yes).
- Old plan 25 "Trust and install", paragraphs 1 to 5.
- 007 test 23: untrusted `settings set plugin.<name>.enabled true` fails with `plugin_untrusted` and prints C-17.
- Copy: C-15, C-16, C-17 and the C-01 trust prompt in `COPY-PENDING.md`.
- Spec: `specs/PLUGIN-TRUST.md` (new).

## Out of scope

The commands that call trust (`plugin add`, `hook add`, `plugin trust`: plan 34). Runs (plan 35).

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md). Tests in `tests/test_plugin_trust.c` (new, suite `plugins`). SHA-256 uses the existing hash code in the repo; no new library.

## What must be true

File. `<data dir>/trust.json`, where data dir is `RememberEnv.data_dir` (the same directory as `brains.json`, `decision:g-01` / G2 A). Mode 0600. The JSON object is `{"format":"remember-trust","version":1,"entries":[{kind, name, path, folder_sha256, manifest_sha256, files:[{path, sha256, size, mtime}], interpreter:{path, sha256}|null, needs, trusted_at, dev}]}`. `kind` is `plugin` or `hook`. `path` is the plugin folder or the hook command path. There is no `<data dir>/plugins/trust.json` and no `<plugins_dir>/trust.json`. Written by temp file and `rename`. No HMAC. No Keychain, libsecret or polkit.

Hash. Do not hash only the executable. Hash the plugin folder and the manifest. Also hash `command[0]` and every later argv item that names an existing regular file, at most 8 files, in argv order. When `command[0]` is `sh`, `bash`, `zsh`, `python`, `python3`, `node`, `ruby`, `perl`, `deno`, `bun`, or `/usr/bin/env` followed by one of those, also store the interpreter realpath and SHA-256. For `sh -c "<inline>"`, the inline text is part of the manifest hash. A built-in has no record and is trusted. A mismatch of any stored hash means untrusted: the run is skipped with `plugin_untrusted` and nothing is deleted.

Confirm. `plugin_trust_confirm(const RememberEnv *env, const char *name, const char *body)` calls `env->confirm(ctx, "plugin", name, body)`. NULL confirm: exit 1 `plugin_approval_required`, record nothing, print C-15. False: exit 1 `plugin_approval_denied`, record nothing, print C-16. The CLI's confirm (on a TTY) prints the body, then `Type "<name>" to allow it, or press Enter to cancel:` and returns true only when the typed text equals the name. No TTY means the CLI passes no callback. No `--confirm` flag. Folder body: `Allow "<name>" to read your memories? It can send them off this Mac.` followed by the needs, permissions and settings, one per line. Hook body: `Allow hook "<name>" to run <path>?` An update shows only the changed needs.

Check. `plugin_trust_check(name)` compares the record to the files. A mismatch returns `untrusted`; the caller (plan 35, 36) does not run, sets `disabled_reason` `untrusted` in that brain's `index.json`, and warns `plugin_untrusted` with C-17. `plugin trust` re-hashes, shows the changed needs, and asks again. True stores the new hash and sets `disabled_reason` to null. `enabled` stays.

Settings. `settings set plugin.<name>.enabled true` for an installed plugin with no matching record fails with `plugin_untrusted` and prints C-17 (007 test 23).

## Review points

- No code path records trust without a true confirm return.
- `trust.json` is rewritten only through `rename`.

## Definition of Done

- [ ] `record_is_data_dir_trust_json` (path `<data dir>/trust.json`, format `remember-trust`, mode 0600, `entries` with `kind` `plugin` or `hook`; no file under `plugins/trust.json`).
- [ ] `record_fields_h_9_2` (folder, manifest, files and interpreter hash fields).
- [ ] `hash_argv_files_max_8` and `interpreter_hashed` and `sh_c_inline_in_manifest_hash`.
- [ ] `null_confirm_c15_records_nothing` and `false_confirm_c16_records_nothing` (010 test 13, first half).
- [ ] `typed_name_must_match` (old probe 1).
- [ ] `mismatch_sets_untrusted_keeps_enabled` and `trust_clears_reason` (010 test 13).
- [ ] `untrusted_enable_setting_fails_c17` (007 test 23).
- [ ] `builtin_trusted_no_record`.
- [ ] `specs/PLUGIN-TRUST.md` written; the decisions above are `state:implemented`.
- [ ] The gate passes on the committed tree.

## Implementation Notes

## Review Notes
