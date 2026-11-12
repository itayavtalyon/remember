# 36 — Persistent plugin processes, backfill and index.json

**Area:** the runtime dir, sockets, state files, ping, idle shutdown, strikes, crash loop, `index.json`, the backfill and `plugin reindex` (`src/plugin_proc.c` new, `src/plugin_proc.h` new, `src/plugin_index.c` new, `src/backfill.c` new).
**Needs:** plan 35. No other plan.
**Blocked on:** the host warm-up request type `plugin.warm` joins the open question `decision:open-2026-11-12-remember-call-requests` (X1) (request types of `remember_call`).

## Goal

Persistent actions and filters run as long-lived processes on a per-brain socket, start in the background, stop when idle or after strikes, and catch up through a resumable backfill.

## Source

- Log 010 H-12.1 to H-12.3 (processes), H-13 (backfill and index); PLUGIN-PROTOCOL R6.1 (env), R3.21, R8.16; `decision:protocol-oq-12-15`.
- Old plan 25 "Processes", "Backfill and index", "Delivery" last paragraph, "Chain and allow list" persistent paragraph.
- `decision:2026-11-11-d17-sync-commands` (G6 A): the backfill does not print C-30/C-31; it exits 1 `plugin_backfill_incomplete` (C-24) when it does not finish.
- Copy: C-24, C-26 in `COPY-PENDING.md`.
- Spec: `specs/PLUGIN-PROCESSES.md` (new).

## Out of scope

The search chain (plan 37). The daemon (plans 39 to 42).

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md). Tests in `tests/test_plugin_proc.c` (new, suite `plugins`), using the plan 24 fixture plugins.

## What must be true

Runtime dir: `RememberEnv.runtime_dir` when set, else `$XDG_RUNTIME_DIR/remember/` when that variable is set (read by `src/cli_env.c`), else `/tmp/remember-<uid>/`. Created at mode 0700. Refused (error `runtime_dir_unsafe`, copy item R36-1 in `decision:2026-11-12-copy-batch-final`) when another user owns it. Exit 1, `retryable` false. `message`: `the plugin runtime folder "<path>" belongs to another user, so remember won't use it.` `fix`: `Remove "<path>", or set XDG_RUNTIME_DIR to a folder you own.` Plain line: `remember: the plugin runtime folder "<path>" belongs to another user, so remember won't use it. Remove "<path>", or set XDG_RUNTIME_DIR to a folder you own.`

Actions default to `once`. Filters are `persistent`. Socket `<runtime>/s/<16 hex>.sock`, where the hex is the first 16 hex characters of SHA-256 of `<brain_id>/<plugin name>`. State file `<runtime>/plugins/<name>/<brain-id>.state`, format `remember-plugin-state` version 1, fields `pid`, `started_at`, `strikes`, `crashes` (instants from the last 10 minutes).

Alive only when `ping` answers within 200 ms. An OS advisory lock is held while checking and starting. Persistent filters start in the background at `plugin enable`, after a backfill, at host launch (`remember_call` type `plugin.warm`, part of X1's list), and on a write or search when no live process exists. A search never waits for that start.

Every CLI run sends `shutdown` reason `idle` to every process past its idle timeout: `remember.plugins.idle_timeout` (default 10 minutes) unless the manifest sets `idle_timeout`. `plugin add` of an installed name sends `shutdown` reason `upgrade`; `plugin remove` sends `removed`.

A persistent process gets the R6.1 variables plus `REMEMBER_HOOK_DEPTH=1` and `REMEMBER_HOOK_NAME=plugin/<name>`. For a write an event causes it sets `REMEMBER_CAUSE_EVENT_ID` and `REMEMBER_HOOK_CHAIN` from that event; Remember extends that chain. A write without them counts as depth 1 (known limitation; no second rule). A persistent action gets the event on its socket and acks with `event.result`. A missing ack is a strike. A failed `event.result` is not a strike.

Strikes: timeout, crash, malformed output, oversize output. A well-formed error reply is not a strike. 3 consecutive strikes stop the process. 3 crashes in 10 minutes set `disabled_reason` `crash_loop` in that brain's `index.json` and warn `plugin_disabled_crash_loop` (C-26); `enabled` is unchanged; `plugin enable` clears it. A `ready.protocol` that differs from the manifest is not a strike and sets `disabled_reason` `protocol_mismatch`.

Runs only when `enabled` resolves true, the trust record matches or it is built-in, and `disabled_reason` is null. Status `user` is computed (setting false, reason null) and never written.

`index.json`: `<brain>.d/plugins/<name>/index.json` beside `data/` and `stderr.log`, format `remember-plugin-index` version 1, fields `index_version` (string or null) and `disabled_reason` (null, `crash_loop`, `protocol_mismatch`, `untrusted`). The folder is never synced, exported, or backed up.

Backfill: index states `ready`, `building`, `stale`, `missing`, `failed`, `disabled`. After a completed full backfill, store `index_version`. A different `index_version` later marks `stale`. A search or a write only marks a backfill pending. It runs on `plugin enable`, on `remember plugin reindex NAME [--full]`, and when the host asks at launch. The runner holds a per-plugin, per-brain lock and resumes from the last answered batch. A catch-up sends changes and purge tombstones with `change_seq` above `applied_through_seq`. A missing batch reply stops the stream: exit 1 `plugin_backfill_incomplete` (C-24). A second run resumes. Search does not block while an index builds.

- **Warnings carry a `fix` (E3 B, `decision:2026-11-12-e3-warnings-have-fix-field`).** Every warning this plan prints has a `message` and a non-empty `fix`: `plugin_disabled_crash_loop`. The wording is copy item E3B-1, entry 17, in `decision:2026-11-12-copy-batch-e3b-warning-splits`: the approved sentence stays word for word and only its next step moves into `fix`; the plain line is `remember: warning: <message> <fix>`. `code`, `detail`, exit codes and `--strict` do not change. Itay approved E3B-1 as a whole on 2026-11-12, including every `fix` that is Story Weaver wording (`decision:2026-11-12-copy-batch-e3b-warning-splits`).

## Review points

- No search path waits for a process start.
- `user` is never written to `index.json`.

## Definition of Done

- [ ] `three_crashes_crash_loop_only_brain_a` (010 test 23).
- [ ] `new_index_version_marks_stale_search_does_not_start` (010 test 24).
- [ ] `persistent_cause_env_extends_chain` (010 test 33).
- [ ] `lost_reindex_exits_1_c24_and_resumes` (010 test 34).
- [ ] `update_sends_shutdown_upgrade` (010 test 32, shutdown half).
- [ ] `ping_200ms_alive_rule`, `idle_shutdown_default_10_min`, `three_strikes_stop`, `error_reply_not_strike`, `protocol_mismatch_reason`.
- [ ] `runtime_dir_other_owner_refused`.
- [ ] `specs/PLUGIN-PROCESSES.md` written.
- [ ] The gate passes on the committed tree.

## Implementation Notes

## Review Notes
