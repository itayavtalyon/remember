# 29 — Handle API: remember_open, remember_call, remember_close

**Area:** the public C library API (`include/remember.h`, `src/remember_api.c`) and the CLI's RememberEnv (`src/main.c`, `src/util.c`).
**Needs:** plans 20 and 21 (both merged). No other plan (G3 A).
**Blocked on:** open question `decision:open-2026-11-12-remember-call-requests` (X1): the request `data` schema of each request type. `decision:library-handle-api` says requests use "the same types as HTTP and MCP", and `decision:mcp-tool-catalog` puts that catalog in "the API-surface log", which does not exist on origin/main. The shape choices this plan adds (the `struct_size` field, the `remember_open` out-parameter form, `remember_free`, the codes `unknown_request_type`, `handle_busy`, `cancelled`, one thread per handle) are proposed record `decision:2026-11-11-handle-api-shape`, which Itay approves before work starts.

## Goal

A host links the Remember library and calls it through a handle, not through argv. `remember_open(env, path)` returns a handle, `remember_call(handle, request_json)` returns a `remember-message` reply, and `remember_close` frees it. The engine stops reading `HOME` and `XDG_*`; the CLI fills a RememberEnv in `main` and passes it down.

## Source

- `decision:library-handle-api`: "remember_open(env, path) returns a handle, remember_call(handle, request_json) returns response_json, remember_close closes it, with an optional progress/cancel callback. … remember_run stays for CLI byte-parity tests."
- `decision:remember-env`: "data_dir, user_db_path, runtime_dir, plugins_dir, and capability flags (processes, sockets, hooks, daemon). The CLI fills it from HOME/XDG; phones fill it from the app container. No HOME lookup inside the engine."
- `decision:g2-13`: three nullable callbacks, one `callback_ctx`, called on the caller's thread; "a NULL confirm callback never counts as a confirmation."
- `decision:g-21`, ST-9.3: `remember_call` accepts `setting.set`, `setting.reset` and `setting.import`.
- `decision:plan20-remember-call-deferred`: those setting actions are built here.
- `decision:2026-11-07-bg-cl-03-engine-binding`: production callers use `remember_open`/`remember_call`; `remember_run` is for internal tests.
- Log 018 (stable ABI answer): "`remember_run` is internal and not in the public header."
- Log 008 JE-1.2, JE-2.1 to JE-2.9 (envelope, `request_id` copied into the reply).
- `decision:2026-11-11-handle-api-scope` (G3 A): the dispatch callback field exists and is never called here; plan 35 calls it.
- Spec: `specs/LIBRARY-API.md` (new).

## Out of scope

- Calling the dispatch callback and the hook pre-flight (plan 35).
- The confirm callback's first caller (plan 33). This plan stores it.
- HTTP and MCP (no plan).
- Swift wrappers (Mac plan M26).

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md). Tests live in `tests/test_library_api.c`, suite `library_api`, added to `tests/gate-suites`. The public header compiles as C11 and C++17 (a test translation unit `tests/test_header_cxx.cpp` includes it).

## What must be true

Header. `include/remember.h` (new, installed by `install(FILES include/remember.h DESTINATION include)`) declares exactly:

- `typedef struct RememberHandle RememberHandle;` (opaque).
- The three callback typedefs, moved unchanged from `src/remember_env.h`: `remember_confirm_fn`, `remember_dispatch_fn`, `remember_token_fn`.
- `typedef int (*remember_progress_fn)(void *ctx, const char *phase, uint64_t done, uint64_t total);` A non-zero return cancels the call.
- `typedef struct RememberEnv { uint32_t struct_size; const char *data_dir; const char *user_db_path; const char *runtime_dir; const char *plugins_dir; uint32_t capabilities; remember_confirm_fn confirm; remember_dispatch_fn dispatch; remember_token_fn token; remember_progress_fn progress; void *callback_ctx; } RememberEnv;`
- Capability bits: `REMEMBER_CAP_PROCESSES 0x1u`, `REMEMBER_CAP_SOCKETS 0x2u`, `REMEMBER_CAP_HOOKS 0x4u`, `REMEMBER_CAP_DAEMON 0x8u`.
- `int remember_open(const RememberEnv *env, const char *brain_path, RememberHandle **out, char **error_json);`
- `char *remember_call(RememberHandle *h, const char *request_json);`
- `void remember_close(RememberHandle *h);`
- `void remember_free(char *s);`

`src/remember_env.h` becomes `#include "remember.h"` plus the engine-private helpers. `remember_run` and `remember_main` stay in `src/remember_app.h` and are not in `include/remember.h`.

Open. `remember_open` returns 0 and sets `*out` on success. On failure it returns 1, sets `*out` to NULL, and sets `*error_json` to a `remember-message` failure with `type` `brain.open.result` (free with `remember_free`). `env` NULL, `struct_size` smaller than `offsetof(RememberEnv, callback_ctx) + sizeof(void *)`, `data_dir` NULL or relative, or `brain_path` NULL or relative fail with `invalid_argument`. `user_db_path` NULL means `<data_dir>/user.db`. `runtime_dir` NULL means the plan 36 default. `plugins_dir` NULL means `<data_dir>/plugins`. The handle copies every string; the caller may free its env after the call.

Call. `request_json` is one `remember-message` version 1 object with `type` (a request type, `<noun>.<verb>`), optional `request_id`, and `data`. The reply is the same envelope the CLI prints with `--json` for the same command, with `type` `<request type>.result`, and `request_id` copied when the request had one (JE-2.9). The supported request types and each one's `data` members are X1. Until X1 is answered, this plan supports exactly: `setting.set`, `setting.reset`, `setting.import`, `setting.get`, `setting.list`, whose `data` members are the long option names of the matching `remember settings` subcommand with `-` replaced by `_` (that is the X1 question; the coder does not extend it).
- Unknown `type` → `error.code` `unknown_request_type`, `type` `unknown.result`. Malformed JSON → `invalid_argument`, `type` `unknown.result`.
- `updated_by` for a write through `remember_call` is `{kind: human, id: null}` (ST-11.2).
- A `progress` callback that returns non-zero makes the call return `error.code` `cancelled`, `retryable` true, and roll back the write.
- Each `remember_call` opens and ends its own read or write transaction. No handle keeps a read snapshot between calls.
- One handle is used by one thread at a time. A second thread calling the same handle while a call runs gets `error.code` `handle_busy`. Two handles on one brain from two threads work (SQLite WAL, busy timeout as the CLI).

HOME. The engine reads no `HOME`, `XDG_DATA_HOME`, `XDG_RUNTIME_DIR` or `XDG_CONFIG_HOME`. `src/main.c` builds a RememberEnv through `cli_env_from_process(RememberEnv *out, char *err, size_t errlen)` in `src/cli_env.c` (new): `data_dir` is `$XDG_DATA_HOME/remember` when set and non-empty, else `$HOME/.remember` (`decision:g-01`). `default-db` and `remember.db` are read from `$HOME/.remember` (`decision:2026-11-11-default-db-home`), passed as `RememberEnv`-private CLI fields in `CliContext`. `util_resolve_db_path` and `util_resolve_user_db_path` take the env instead of calling `env_nonempty("HOME")`. `REMEMBER_DB`, `REMEMBER_STRICT`, `NO_COLOR` and `REMEMBER_HOOK_DEPTH` stay CLI reads.

The error codes `unknown_request_type`, `handle_busy` and `cancelled` are new. Their `message`, `fix` and `retryable` are copy item R29-1 in `decision:2026-11-12-copy-batch-final` (approved 2026-11-12), all exit 1. `unknown_request_type`, `retryable` false: `remember doesn't know the request type "<type>".` / `Check the request type, or update remember if the type is newer than this build.` `handle_busy`, `retryable` true: `this handle is already running another call.` / `Wait for that call to finish, or open a second handle.` `cancelled`, `retryable` false: `the call was cancelled before it finished.` / `Call again if you still want it to run.` A handle call returns the envelope, so the plain line is what a host prints. R29-1's [ASSUMPTION] is that a cancelled write commits whole or not at all.

## Review points

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
- [ ] `call_malformed_json_is_invalid_argument`.
- [ ] `call_progress_cancel_rolls_back`.
- [ ] `call_same_handle_two_threads_handle_busy`.
- [ ] `two_handles_two_threads_both_succeed`.
- [ ] `null_confirm_is_never_yes` (a stored NULL confirm reads as false through the engine helper `env_confirm`).
- [ ] `dispatch_never_called` (a counting dispatch callback stays at 0 after a write).
- [ ] `engine_reads_no_home` (unset `HOME`, open with an absolute `data_dir`, write and read succeed).
- [ ] `cli_env_xdg_data_home` and `cli_env_home_default`.
- [ ] `header_compiles_cxx`.
- [ ] `specs/LIBRARY-API.md` is written; `decision:library-handle-api`, `decision:remember-env`, `decision:g2-13` (storage half), `decision:g-21`, `decision:plan20-remember-call-deferred` are `state:implemented`.
- [ ] The gate passes on the committed tree.

## Implementation Notes

## Review Notes
