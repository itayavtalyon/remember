# 29 — Handle API: remember_open, remember_call, remember_close

**Area:** the public C library API (`include/remember.h`, `src/remember_api.c`, `src/api_table.c` new) and the CLI's RememberEnv (`src/main.c`, `src/util.c`).
**Needs:** plans 20 and 21 (both merged). No other plan (G3 A).
**Blocked on:** the proposed record `decision:2026-11-11-handle-api-shape`, which Itay approves before work starts. It holds the shape choices this plan adds: the `struct_size` field, the `busy_timeout_ms` and `flags` fields, the `remember_open` out-parameter form, `remember_free`, the codes `unknown_request_type`, `handle_busy`, `cancelled`, and one thread per handle. Itay asked to review the whole shape first (G4 B). X1 and its gaps are answered (`decision:2026-11-12-x1-gaps-bin-keep-archived-hidden`); no request detail is open.

## Goal

A host links the Remember library and calls it through a handle, not through argv. `remember_open(env, path)` returns a handle, `remember_call(handle, request_json)` returns a `remember-message` reply, and `remember_close` frees it. The engine stops reading `HOME` and `XDG_*`; the CLI fills a RememberEnv in `main` and passes it down.

## Source

- `decision:library-handle-api`: "remember_open(env, path) returns a handle, remember_call(handle, request_json) returns response_json, remember_close closes it, with an optional progress/cancel callback. … remember_run stays for CLI byte-parity tests."
- `decision:remember-env`: "data_dir, user_db_path, runtime_dir, plugins_dir, and capability flags (processes, sockets, hooks, daemon). The CLI fills it from HOME/XDG; phones fill it from the app container. No HOME lookup inside the engine."
- `decision:g2-13`: three nullable callbacks, one `callback_ctx`, called on the caller's thread; "a NULL confirm callback never counts as a confirmation."
- `decision:2026-11-12-x1-remember-call-requests` (X1-1 to X1-14, Itay 2026-11-12): the request list, the member rule, the surfaces, `bin` as an array, authors, unknown members, `yes`, export, `plugin.warm`, the reserved archive rows, conflict `keep`, the no-create flag and the busy timeout. `decision:2026-11-12-x1-gaps-bin-keep-archived-hidden` (G1 A to G3 A, Itay 2026-11-12) settles the arity of `bin`, `keep: local` and the `archived_hidden` member of the search reply. X1 amends Q9 (`decision:2026-11-11-log005-merge-rules`) for the request surface only. `decision:2026-11-07-bg-x-11-api`: one engine verb table.
- `decision:g-21`, ST-9.3: `remember_call` accepts `setting.set`, `setting.reset` and `setting.import`.
- `decision:plan20-remember-call-deferred`: those setting actions are built here.
- `decision:2026-11-07-bg-cl-03-engine-binding`: production callers use `remember_open`/`remember_call`; `remember_run` is for internal tests.
- Log 018 (stable ABI answer): "`remember_run` is internal and not in the public header."
- Log 008 JE-1.2, JE-2.1 to JE-2.9 (envelope, `request_id` copied into the reply).
- `decision:2026-11-11-handle-api-scope` (G3 A): the dispatch callback field exists and is never called here; plan 35 calls it.
- Spec: `specs/LIBRARY-API.md` (new) and `specs/API-SURFACE.md` (new, the living request list, X1-2 A).

## Out of scope

- Calling the dispatch callback and the hook pre-flight (plan 35).
- The confirm callback's first caller (plan 33). This plan stores it.
- HTTP and MCP (no plan). The table records which surface each request is open on, but this plan builds only remember_call and the CLI.
- The engine verbs `archive` and `unarchive`. Only their request rows are reserved (X1-11 A); no plan builds them yet.
- The sync requests `sync.pull` and `sync.peers` (reserved rows only; plan 44 builds them) and `device.set` (plan 43).
- Swift wrappers (Mac plan M26).

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md). Tests live in `tests/test_library_api.c`, suite `library_api`, added to `tests/gate-suites`. The public header compiles as C11 and C++17 (a test translation unit `tests/test_header_cxx.cpp` includes it).

## What must be true

Header. `include/remember.h` (new, installed by `install(FILES include/remember.h DESTINATION include)`) declares exactly:

- `typedef struct RememberHandle RememberHandle;` (opaque).
- The three callback typedefs, moved unchanged from `src/remember_env.h`: `remember_confirm_fn`, `remember_dispatch_fn`, `remember_token_fn`.
- `typedef int (*remember_progress_fn)(void *ctx, const char *phase, uint64_t done, uint64_t total);` A non-zero return cancels the call.
- `typedef struct RememberEnv { uint32_t struct_size; const char *data_dir; const char *user_db_path; const char *runtime_dir; const char *plugins_dir; uint32_t capabilities; uint32_t flags; uint32_t busy_timeout_ms; remember_confirm_fn confirm; remember_dispatch_fn dispatch; remember_token_fn token; remember_progress_fn progress; void *callback_ctx; } RememberEnv;`
- Flag bits for `flags`: `REMEMBER_FLAG_NO_CREATE 0x1u`.
- Capability bits: `REMEMBER_CAP_PROCESSES 0x1u`, `REMEMBER_CAP_SOCKETS 0x2u`, `REMEMBER_CAP_HOOKS 0x4u`, `REMEMBER_CAP_DAEMON 0x8u`.
- `int remember_open(const RememberEnv *env, const char *brain_path, RememberHandle **out, char **error_json);`
- `char *remember_call(RememberHandle *h, const char *request_json);`
- `void remember_close(RememberHandle *h);`
- `void remember_free(char *s);`

`src/remember_env.h` becomes `#include "remember.h"` plus the engine-private helpers. `remember_run` and `remember_main` stay in `src/remember_app.h` and are not in `include/remember.h`.

Open. `remember_open` returns 0 and sets `*out` on success. On failure it returns 1, sets `*out` to NULL, and sets `*error_json` to a `remember-message` failure with `type` `brain.open.result` (free with `remember_free`). `env` NULL, `struct_size` smaller than `offsetof(RememberEnv, callback_ctx) + sizeof(void *)`, `data_dir` NULL or relative, or `brain_path` NULL or relative fail with `invalid_argument`. `user_db_path` NULL means `<data_dir>/user.db`. `runtime_dir` NULL means the plan 36 default. `plugins_dir` NULL means `<data_dir>/plugins`. The handle copies every string; the caller may free its env after the call. `flags` with `REMEMBER_FLAG_NO_CREATE` makes a missing brain file fail with the error the CLI's `--no-create` gives; with the bit clear, a first write creates the brain, like the CLI (X1-13 A). `busy_timeout_ms` is the SQLite busy timeout; 0 means 2000 (X1-14 A). A call has no strict mode: the reply's `warnings` carry what `--strict` would turn into exit 4, and the host reads them.

Call. `request_json` is one `remember-message` version 1 object with `type` (a request type, `<noun>.<verb>`), optional `request_id`, and `data`. The reply is the same envelope the CLI prints with `--json` for the same command, with `type` `<request type>.result`, and `request_id` copied when the request had one (JE-2.9). The supported requests and their `data` members are the X1 list below, built from one engine table in `src/api_table.c` (new), which also feeds the CLI verb table. Every verb that has a reply type in JSON-STYLE 1.7a is in it (X1-3 A), plus `plugin.warm`.

Members (X1-1 A). A request's `data` members are the CLI's long option names with `-` turned into `_`; positionals are named (`body`, `query`); a repeatable option is an array (`tags`); a switch is a boolean. `bin` is an array of `live`, `expired` or `deleted`, with default `["live"]` (X1-5 C). `memory.list` and `memory.search` take one or more values. `memory.get`, `memory.update`, `memory.delete`, `memory.rekey` and `memory.purge` act on exactly one memory or one bin, so they take exactly one value; an empty array or two or more values fail with `invalid_argument` (G1 A). Writes take optional `as` and `actor`; the default is human with no id (X1-6 A). `yes` exists only where the CLI has `--yes` (bulk `setting.reset` and daemon takeover); `plugin.add`, `plugin.trust`, `plugin.enable` and `hook.add` never take it and need the confirm callback (X1-8 A). A member the engine does not know is ignored and adds a warning with code `unknown_field`; nothing is rejected for it (X1-7 B).

The list (`?` means optional):
- `memory.add`: `body`, `key?`, `tags[]?`, `ttl?`, `expires?`, `as?`, `actor?`, `hooks?`. Reply `{action, memory}`.
- `memory.get`: `id` or `key`, `bin?`. Reply `{memory}` with link stubs.
- `memory.list`: `tags[]?`, `created_by?`, `updated_by?`, `key?`, `sort?`, `since?`, `until?`, `limit?`, `cursor?`, `bin?`. Reply `{count, total, entries[]}`, with `next_cursor` and `has_more` in the envelope.
- `memory.search`: `query`, `tags[]?`, `created_by?`, `updated_by?`, `key?`, `since?`, `until?`, `limit?`, `facet_limit?`, `bin?`, `no_filters?`, `explain?`. Reply `{count, total, archived_hidden, keys, tags, entries, query, ranked_by, filters}`; search never pages. `archived_hidden` is a number and is always present. It is 0 when `bin` includes `archived`, and 0 while no archive exists (G3 A). `specs/JSON-ENVELOPE.md` gets the member in its search row.
- `memory.update`: `id` or `key`, `bin?`, `text?`, `tags[]?`, `clear_tags?`, `ttl?`, `expires?`, `clear_expires?`, `undelete?`, `if_version_vector?`, `as?`, `actor?`, `hooks?`. Reply `{memory}`. There is no restore request: restoring is this request with `undelete` or `clear_expires`.
- `memory.delete`: `id` or `key`, `bin?`, `as?`, `actor?`, `hooks?`. Reply `{action}`.
- `memory.purge`: `bin`, `hooks?`. Reply `{count}`.
- `memory.rekey`: `id` or `key`, `bin?`, `to_key` or `clear_key`, `as?`, `actor?`, `hooks?`. Reply `{memory}`.
- `memory.link`, `memory.unlink`: `from` or `from_key`, `to` or `to_key`, `kind?`, `deleted?`, `as?`, `actor?`, `hooks?`. Reply `{link}` or `{removed}`.
- `memory.related`: `id` or `key`, `kind?`, `outgoing?`, `incoming?`, `deleted?`. Reply `{memory, links}`.
- `memory.import`: `from_db` (and `from_json`, `from_md`, `from_csv` when the CLI has them), `origin?`, `hooks?`. Reply the import counts.
- `memory.export`: `format`, `out`, `force?`. It writes the file at `out`, never returns the document, and replies `{format, path, count}` (X1-9 A).
- `tag.list`: `bin?`. Reply `{tags[{tag, count}]}`.
- `conflict.list`, `conflict.accept`: `id`, `keep` (`local`, `incoming` or `both`; local is the version this device authored, X1-12 C). `keep: local` fails with `invalid_argument` when no version, or more than one version, came from this device. The failure lists the versions in its detail, so the caller can retry with `incoming` or `both` or pick one (G2 A). Reply the conflict.
- `setting.list`, `setting.get`, `setting.set`, `setting.reset`, `setting.explain`, `setting.history`, `setting.import`: the long option names of `remember settings`.
- `plugin.list`, `plugin.show`, `plugin.add`, `plugin.trust`, `plugin.enable`, `plugin.disable`, `plugin.remove`, `plugin.reindex`, `plugin.rerun`, `plugin.status`, `plugin.log`, `plugin.test`, `hook.add`: the options of plan 34. `plugin.add`, `plugin.trust`, `plugin.enable` and `hook.add` fail with exit 5 `plugin_approval_required` or `hook_approval_required` when the confirm callback is NULL (plan 33).
- `plugin.warm`: `plugin?`, `wait_ms?`. Reply `{started[], running[], skipped[]}`. With `wait_ms` the call returns when the plugins are ready or the time ends. Plan 36 builds the behavior; this plan builds the row.
- `device.get`, `device.new`, `brain.new_id`, `repair.search_index`, `sweep`: the CLI options only. `dispatch` stays a host callback, not a request.
- Reserved: `memory.archive`, `memory.unarchive`, `sync.pull`, `sync.peers`. They answer `unknown_request_type` until an engine plan builds them (X1-11 A). `device.set` joins the table with the v7 schema plan, plan 43 (Q10-2 A). Plan 44 builds `sync.pull` and `sync.peers`.

Surfaces (X1-4 A). The table has one column per surface. Reads and memory writes are on remember_call, the CLI, REST and MCP. `memory.purge`, every `setting.*` write and every plugin write are on remember_call and the CLI only (`decision:mcp-tool-catalog`). `specs/API-SURFACE.md` is generated from the table and lists, for each request, its members, reply, surfaces and CLI command. A test fails when a CLI verb or a table row has no counterpart in the other.
- Unknown `type` → `error.code` `unknown_request_type`, `type` `unknown.result`. Malformed JSON → `invalid_argument`, `type` `unknown.result`.
- `updated_by` for a write through `remember_call` is `{kind: human, id: null}` (ST-11.2) unless the request carries `as` and `actor`.
- A reply to a write that has `as` or `actor` records that author on the memory, the link and the event.
- A `progress` callback that returns non-zero makes the call return `error.code` `cancelled`, `retryable` true, and roll back the write.
- Each `remember_call` opens and ends its own read or write transaction. No handle keeps a read snapshot between calls.
- One handle is used by one thread at a time. A second thread calling the same handle while a call runs gets `error.code` `handle_busy`. Two handles on one brain from two threads work (SQLite WAL, busy timeout as the CLI).

HOME. The engine reads no `HOME`, `XDG_DATA_HOME`, `XDG_RUNTIME_DIR` or `XDG_CONFIG_HOME`. `src/main.c` builds a RememberEnv through `cli_env_from_process(RememberEnv *out, char *err, size_t errlen)` in `src/cli_env.c` (new): `data_dir` is `$XDG_DATA_HOME/remember` when set and non-empty, else `$HOME/.remember` (`decision:g-01`). `default-db` and `remember.db` are read from `$HOME/.remember` (`decision:2026-11-11-default-db-home`), passed as `RememberEnv`-private CLI fields in `CliContext`. `util_resolve_db_path` and `util_resolve_user_db_path` take the env instead of calling `env_nonempty("HOME")`. `REMEMBER_DB`, `REMEMBER_STRICT`, `NO_COLOR` and `REMEMBER_HOOK_DEPTH` stay CLI reads.

The error codes `unknown_request_type`, `handle_busy` and `cancelled` are new. Their `message`, `fix` and `retryable` are copy item R29-1 in `decision:2026-11-12-copy-batch-final` (approved 2026-11-12), all exit 1. `unknown_request_type`, `retryable` false: `remember doesn't know the request type "<type>".` / `Check the request type, or update remember if the type is newer than this build.` `handle_busy`, `retryable` true: `this handle is already running another call.` / `Wait for that call to finish, or open a second handle.` `cancelled`, `retryable` false: `the call was cancelled before it finished.` / `Call again if you still want it to run.` A handle call returns the envelope, so the plain line is what a host prints. R29-1's [ASSUMPTION] is that a cancelled write commits whole or not at all.

## Stop and ask

The coder stops and asks Itay, through the architect, at each of these. The plan does not choose.

1. The `tag.list` `bin` arity. G1 A names search and list as multi-bin and the five single-record requests as single-bin; `tag.list` is neither.
2. The member name of the version list in the `invalid_argument` detail for `keep: local` (G2 A says the reply lists the versions and names no member).
3. Whether `archived` is an accepted `bin` value before the archive plan exists, and what a request with `archived` returns until then.
4. Whether human `remember search` prints a line for a non-zero `archived_hidden`. The member exists for hosts (M79); the CLI text needs approved copy.

## Review points

- A request member that is not on the list does not fail the call; it adds `unknown_field` and the call proceeds.
- `memory.get`, `memory.update`, `memory.delete`, `memory.rekey` and `memory.purge` fail with `invalid_argument` for a `bin` of zero or several values (G1 A).
- `keep: local` never picks a version by guessing; it fails unless exactly one version is local (G2 A).
- `archived_hidden` is in every search reply, as a number (G3 A).
- `git grep -n 'getenv("HOME")\|XDG_' src/` lists only `src/cli_env.c`.
- `remember_run` is absent from `include/remember.h`.
- The reply of `remember_call` for `setting.get` is byte-identical to `remember --json settings get` for the same key (a parity test).
- No fork, no thread creation in `src/remember_api.c`.

## Definition of Done

- [ ] `open_null_env_fails_invalid_argument`.
- [ ] `open_relative_data_dir_fails`.
- [ ] `open_copies_env_strings` (the test frees its env before the first call).
- [ ] `open_defaults_user_db_and_plugins_dir`.
- [ ] `call_setting_set_writes_human_updated_by`.
- [ ] `call_setting_get_matches_cli_bytes`.
- [ ] `call_copies_request_id`.
- [ ] `call_unknown_type_is_unknown_request_type`.
- [ ] `call_unknown_member_warns_unknown_field_and_proceeds`.
- [ ] `members_are_cli_long_names_with_underscores` (one request per verb, the same result as the CLI with `--json`).
- [ ] `bin_array_spans_live_and_expired_in_one_search`.
- [ ] `list_takes_several_bins_and_search_takes_several_bins`.
- [ ] `get_update_delete_rekey_purge_with_zero_or_two_bins_fail_invalid_argument`.
- [ ] `search_reply_has_archived_hidden_zero_when_there_is_no_archive`.
- [ ] `write_with_as_and_actor_records_author` and `write_without_them_is_human_no_id`.
- [ ] `yes_member_only_on_requests_whose_cli_has_yes`.
- [ ] `plugin_add_trust_enable_hook_add_null_confirm_exit_5`.
- [ ] `memory_export_writes_file_and_replies_format_path_count`.
- [ ] `plugin_warm_takes_plugin_and_wait_ms_replies_three_lists`.
- [ ] `archive_unarchive_sync_rows_answer_unknown_request_type`.
- [ ] `conflict_accept_keep_local_incoming_both`.
- [ ] `keep_local_with_no_local_version_fails_invalid_argument_and_lists_versions`.
- [ ] `keep_local_with_two_local_versions_fails_invalid_argument_and_lists_versions`.
- [ ] `open_no_create_flag_missing_brain_fails_like_cli_and_default_creates`.
- [ ] `busy_timeout_ms_zero_means_2000`.
- [ ] `call_has_no_strict_mode_warnings_stay_in_reply`.
- [ ] `api_table_matches_cli_verbs_and_specs_api_surface` (the completeness test of X1-2 A).
- [ ] Every purge, setting-write and plugin-write row has no REST or MCP surface in the table.
- [ ] `call_malformed_json_is_invalid_argument`.
- [ ] `call_progress_cancel_rolls_back`.
- [ ] `call_same_handle_two_threads_handle_busy`.
- [ ] `two_handles_two_threads_both_succeed`.
- [ ] `null_confirm_is_never_yes` (a stored NULL confirm reads as false through the engine helper `env_confirm`).
- [ ] `dispatch_never_called` (a counting dispatch callback stays at 0 after a write).
- [ ] `engine_reads_no_home` (unset `HOME`, open with an absolute `data_dir`, write and read succeed).
- [ ] `cli_env_xdg_data_home` and `cli_env_home_default`.
- [ ] `header_compiles_cxx`.
- [ ] `specs/LIBRARY-API.md` and `specs/API-SURFACE.md` are written; `decision:library-handle-api`, `decision:remember-env`, `decision:g2-13` (storage half), `decision:g-21`, `decision:plan20-remember-call-deferred` are `state:implemented`.
- [ ] The gate passes on the committed tree.

## Implementation Notes

## Review Notes
