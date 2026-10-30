# 25 — Plugins and hooks (CLI and host)

## Goal

Run hooks and plugins from the CLI and from a host process. Trust the code hash. Deliver events from an outbox. Filter search in three stages. Do not build the daemon, the URL plugin, or the hybrid ranker.

## Status

PENDING COPY AND DESIGN.

Behavior in this plan is specified. A stage that prints a sentence in COPY-PENDING C-02 or C-15 through C-38 does not start until that row is approved. These stages also wait:

- Chain-cap warning: DESIGN-PENDING D-02 is answered (2026-10-25). Set status `skipped`, reason `chain_capped`, and warn `plugin_chain_capped`. The sentence waits on its COPY-PENDING row.
- Non-timeout filter failure code: D-08 is answered (2026-10-25). It is `plugin_filter_failed`.
- `skipped_source` log bytes: D-14 is answered (2026-10-25, 010 H-7.2). Write one line per skipped action and event type, with the H-7.2 fields (`at`, `event_type`, `count`). `remember plugin log <plugin>` prints `<at>  skipped "<action>" for <count> <event_type> events from 'remember <command>', because its origins don't include <origin>.`, or `<at>  skipped "<action>" for 1 <event_type> event from 'remember <command>', because its origins don't include <origin>.` when `count` is 1. Leave out the plugin name. Where the output mixes plugins, write `"<plugin>/<action>"`. Print `<event_type>` verbatim. Print `<at>` in the 007 ST-7.16 human date form. Keep `at` in RFC 3339 UTC in JSON.
- Human `add` line: D-05. Do not change it.
- Tool-undo name parse: D-13. Do not parse it in the engine.

This plan is not ready as a whole.

## Source of truth

- [`design-logs/010-plugins-and-hooks.md`](../design-logs/010-plugins-and-hooks.md) Design. The wire format is [`docs/PLUGIN-PROTOCOL.md`](../docs/PLUGIN-PROTOCOL.md) revision 6 (plan 24). Cite spec rule ids. Do not copy the spec into code comments.
- [`design-logs/007-settings.md`](../design-logs/007-settings.md) ST-5.7 through ST-5.10 and ST-9.6.
- [`design-logs/008-json-envelope.md`](../design-logs/008-json-envelope.md) JE-3.10 and JE-4.4.

The daemon decision (M-08 C) keeps a daemon in the product. This plan does not implement it. Itay chose CLI and host only for this extract. See DESIGN-PENDING D-06.

## Context

A plugin is an installed folder with `plugin.json`, or a built-in. An entry is one action or one filter. A hook is a plugin that `remember hook add` wrote. It has one once-action named `run`. The outbox holds events that wait for runs.

The host passes three callbacks in `RememberEnv`, with one `callback_ctx`. Each may be NULL. Call each on the thread that made the call.

```c
typedef bool (*remember_confirm_fn)(void *ctx, const char *kind /* "plugin" */,
                                    const char *name, const char *body);
typedef void (*remember_dispatch_fn)(void *ctx, const char *brain_path);
typedef int  (*remember_token_fn)(void *ctx, const char *helper_id, char *buf, size_t buflen);
/* token: 0 ok, -1 unknown id, -2 lookup failed; buf holds a NUL-terminated token */
```

The token callback is plan 20. This plan uses the same struct. NULL confirm is not a yes. NULL dispatch leaves outbox rows for the next CLI command or `remember sweep`.

A plugin never changes, blocks, or rolls back a write. Memory text reaches an action only on stdin or the socket. argv and env carry ids, names, and numbers only.

No synced, imported, or exported data creates a plugin.

`REMEMBER_PROCESSES=0`: `hook add` and folder `plugin add` return `plugin_unsupported`. Writes insert no outbox rows. A write that matches an enabled folder-plugin action is saved and warns `plugin_unsupported`, exit 0 (4 with `--strict`) (010 H-0.7, C-23). Built-in C-ABI filters still run. Phones never run folder plugins or hooks. The sentence is C-23.

A plugin problem during a search or a write is a warning and exit 0. `--strict` or `REMEMBER_STRICT=1` makes any warning exit 4. Exit 4 has no other meaning. A backfill that does not finish is exit 1 `plugin_backfill_incomplete`, not exit 4. The sentence is C-24.

### Events

| Event | When |
|---|---|
| `memory.created` | a new memory row |
| `memory.updated` | body, tags, key, `expires_at`, or a link changes |
| `memory.deleted` | the memory moves to Deleted, including `delete --expired` |
| `memory.restored` | the memory leaves Deleted |
| `memory.purged` | `remember purge <locator>` |
| `memory.expired` | the sweep marks `expired_at` |

There is no `memory.linked` and no `memory.unlinked`. `link` and `unlink` emit one `memory.updated` per end, with `changes.link` `{kind, direction, other}`, and one shared `batch_id`. `memory.updated` fires only when a field or a link changed.

`expired_at` is device-local. Setting it does not bump `updated_at`, the version vector, or `change_seq`, and it does not sync. The sweep runs after every command that opened the brain read-write, including reads, after that command commits. A read-only open skips it. One transaction. Set `expired_at = now` on each row with `deleted_at IS NULL AND expires_at <= now AND expired_at IS NULL`. One `memory.expired` outbox row per changed row. `occurred_at` is `expires_at`. `detected_at` is the sweep time. `cause.command` is `sweep`. A later or null `expires_at`, or a restore, sets `expired_at` to null in that same write. Restore of a row that is already due emits `memory.restored`, then the sweep emits `memory.expired`. N memories in one command share one `batch_id`.

### Brain list

`<data dir>/brains.json`, mode 0600:

```json
{"format":"remember-brains","version":1,"brains":[{"path":"/absolute/realpath","brain_id":"…","added_at":"…Z"}]}
```

Remember is the only writer. On each read-write open, read the file. Write it only when an entry is added or changed. Write a temp file and rename it, under an advisory lock on `brains.json.lock`.

JE-3.10:

- Same `brain_id` at another path, and that path still has that brain: this file is a copy. Mint a new `brain_id`, add this path, warn `brain_id_cloned`. The sentence is C-38. Do not warn until C-38 is approved. The mint itself is specified and may land. The warning print waits.
- Same `brain_id` at another path, and that path has no such brain: the file moved. Change the path. Keep the id.
- No entry has this `brain_id`: add an entry.

`remember brain list` lists entries. `remember brain prune` removes entries whose path has no brain. `remember sweep --all` reads only this file. A missing path warns `brain_missing` with the locked line `brain "<path>" is in the list and is not on disk. Run 'remember brain prune' to remove it.` The entry stays until prune.

### Delivery

In the write transaction, insert one outbox row per event when an enabled entry's full filter matches. The filter is `events`, `origins`, `tags.any`, `tags.all`, `match_before`, `created_by`, `updated_by`, `bins`, and `not_caused_by`. A miss increments `skipped_filter` and writes no log line. The row stores the frozen event JSON and `recipients`: a JSON array of `{"entry":"plugin/<name>/<entry>","state":"pending"}`. A plugin added later does not see the event.

`--no-hooks` inserts nothing. `--hooks` runs every matching action for that write and ignores `origins`. It does not ignore `enabled` false. Help lines are locked in plan 21.

After commit, run pre-flight. Then the CLI starts `remember dispatch` detached: double fork, `setsid`, stdio to `/dev/null`, and return. `remember_call` runs the same checks and then calls the dispatch callback with the brain path. The engine never forks inside a host.

`remember dispatch` takes the advisory outbox lock in `<brain>.d/` and drains in `rowid` order. One entry runs its events strictly in order, one run at a time. Different entries run in parallel, at most 4 at once per brain. Drain local events before sync and import events.

Recipient states: `done`, `failed`, `halted`. Delete the row only when every recipient is `done`. A `failed` or `halted` recipient stays until it is 7 days old, then delete it with log result `dropped_stale`. Delivery is at-least-once. A non-zero exit is `failed`. Do not retry. There is no exit-75 retry.

A once-process gets the event on stdin. Discard stdout. Write stderr to `<brain>.d/out/<run_id>.log`, truncate at 1 MiB, and continue the run. When the dispatcher starts, delete `out/*.log` files older than 7 days. `run.timeout` starts when the process starts. At the timeout, send SIGTERM to the process group, then SIGKILL 5 seconds later.

A persistent action gets the event on its socket and acks with `event.result`. A missing ack is a strike. A failed `event.result` is not a strike.

### Chain and allow list

A write inside a run is a normal write. It emits events to every match, including the writer.

Once-run env: `REMEMBER_HOOK_NAME` (`plugin/<name>/<entry>`), `REMEMBER_HOOK_RUN_ID`, `REMEMBER_HOOK_DEPTH`, `REMEMBER_HOOK_CHAIN`, `REMEMBER_CAUSE_HOOK`, `REMEMBER_CAUSE_RUN_ID`, `REMEMBER_CAUSE_EVENT_ID`, `REMEMBER_EVENT`, `REMEMBER_EVENT_ID`, `REMEMBER_ID`, `REMEMBER_KEY`, `REMEMBER_DB`. `plugin test` also sets `REMEMBER_HOOK_TEST`.

A persistent process gets the spec R6.1 variables plus `REMEMBER_HOOK_DEPTH=1` and `REMEMBER_HOOK_NAME=plugin/<name>`. For a write that an event causes, it sets `REMEMBER_CAUSE_EVENT_ID` and `REMEMBER_HOOK_CHAIN` from that event. Remember extends that chain. A write without them counts as depth 1. The kill switch then does not see that writer's loop. This is a known limitation. Do not add a second rule.

Every plugin write, including a hook, stores `{kind: tool, id: plugin/<name>/<entry>}`. A hook id is `plugin/<name>/run`. A child that inherits `REMEMBER_HOOK_DEPTH` gets the same stamp. Reject `--as` and `--actor` when depth is 1 or more.

Allow list when depth is 1 or more: `add`, `update`, `delete` of one locator, `link`, `unlink`, `rekey`, restore, and reads. The list is complete. One table. Each command has an `allowed_in_run` flag. A new command defaults to not allowed.

Settings writes fail with `setting_from_hook` (plan 20). Every other banned command fails with exit 1 `plugin_run_forbidden` and the locked line `a hook or plugin run can't run "<command>". Run it in your own terminal.` JSON `fix` waits on C-01. Banned commands include `plugin` subcommands, `hook add`, `delete --expired`, and `purge`.

When `cause.chain` reaches 100, do not run that entry for that event. Set the recipient to `halted`. Keep the plugin enabled. Later commands in that brain warn `plugin_loop_halted` until a rerun, `plugin status --ack NAME`, or until the row is dropped. The plain warning is locked:

`remember: warning: "<name>" was stopped after 100 hook runs in a row set each other off. The write was saved. To run it once more, run 'remember plugin rerun <name> <event-id>' within 7 days. To clear this warning without rerunning, run 'remember plugin status --ack <name>'.`

`remember plugin rerun NAME EVENT_ID` runs one `failed` or `halted` recipient once, with the chain reset. Run it in the foreground and wait for the run before exiting (010 H-5.7, D-17). For a persistent action, print `plugin_action_timeout` or `plugin_action_failed`. Otherwise exit 1 `plugin_not_rerunnable`. The sentence is C-22.

### Pre-flight

For each matching once-action, after commit: each trusted file exists and matches its hash, and `run.command[0]` is executable. A failed check sets that recipient to `failed`, logs it, and warns. A missing file or a `run.command[0]` that is not executable warns `plugin_preflight_failed`. A hash mismatch warns `plugin_untrusted` and follows 010 H-9.5 (010 H-6.2, C-20). Writes never wait for a handler. `plugin_action_timeout` and `plugin_action_failed` print only in `remember plugin test NAME`, in `remember plugin rerun NAME EVENT_ID`, and in the backfill of `plugin reindex` and `plugin enable`, which then exits 1 with `plugin_backfill_incomplete` (010 H-6.4, PP R3.21, R8.16, D-17). A write never prints them, and its exit code does not change. The sentence is C-20. Do not print it until C-20 is approved. The state change may land. Remember does not run that recipient again by itself. `plugin rerun` runs it. The write exits 0, or 4 with `--strict`. The next `remember dispatch` does not start that recipient.

When the dispatcher cannot start, warn `plugin_dispatch_failed` (C-21). Exit 0, or 4 with `--strict`. After a successful start, a failed handler does not change the user command's exit. The failure shows in `plugin log` and the app toast. The toast is remember-mac (C-39). Do not print it from the CLI.

### Trust and install

Trust belongs to the code hash. No HMAC. No Keychain for trust. File `<data dir>/plugins/trust.json`, mode 0600, format `remember-plugin-trust` version 1. The record fields are H-9.2: `name`, `folder_sha256`, `manifest_sha256`, `files` (`path`, `sha256`, `size`, `mtime`), `interpreter` (`path`, `sha256` or null), `needs`, `trusted_at`, `dev`.

Hash the plugin folder and the manifest. Also hash `command[0]` and every later argv item that names an existing regular file, at most 8 files, in argv order. When `command[0]` is `sh`, `bash`, `zsh`, `python`, `python3`, `node`, `ruby`, `perl`, `deno`, `bun`, or `/usr/bin/env` followed by one of those, also store the interpreter realpath and SHA-256. For `sh -c "<inline>"`, the inline text is part of the manifest hash.

A built-in has no trust record and is trusted.

Confirm at `plugin add`, `hook add`, `plugin trust`, and `plugin add` of an installed name. Do not confirm at `plugin enable`. The CLI on a TTY prints the body, then `Type "<name>" to approve:`, and returns true only when the typed text equals the name. No TTY means no callback. No callback: exit 1 `plugin_approval_required`, record nothing (C-15). False: exit 1 `plugin_approval_denied`, record nothing (C-16). No `--confirm` flag. The folder body is `Allow "<name>" to read your memories? It can send them off this Mac.` Then the needs, permissions, and settings. The hook body is `Allow hook "<name>" to run <path>?`

Before each start, compare the record to the files. A mismatch does not run the entry, sets `disabled_reason` `untrusted` in that brain's `index.json`, and warns `plugin_untrusted` (C-17). `plugin trust` re-hashes, shows the changed needs, and asks again. True stores the new hash and sets `disabled_reason` to null. `enabled` stays. The next use runs the plugin when `enabled` resolves true.

`plugin add --dev <dir>` uses the folder in place, re-hashes at each start, needs a terminal, and marks searches with `plugin_dev_mode` (C-25).

Install once per user into `<data dir>/plugins/<name>/`. `plugin add DIR` copies the folder. An installed name updates: confirm, copy, new hash, `shutdown` reason `upgrade`. `plugin add` of a built-in name fails with `plugin_exists` (C-18).

`hook add` is the only hook command. It writes `plugin.json` with one once-action named `run`. The name is the typed name after `-` becomes `_`, and it must match a settings segment. The action holds the command, the events, the filter, `timeout` default `60s` range 1s–1h, and `working_directory` default `~`. After a true confirm, write `plugin.<name>.enabled` true on the user layer. `hook add --disabled` writes no enabled row.

`plugin enable NAME [--scope brain]` and `plugin disable NAME [--scope brain]` are `settings set` of `plugin.<name>.enabled`. No prompt. `plugin enable` also clears `crash_loop` for this brain, starts warm-up, and runs a pending backfill.

A brain with enabled true for a plugin that is not installed lists `missing` and warns `plugin_missing` (C-19).

`plugin remove NAME` sends `shutdown` reason `removed`, deletes the install and the trust record, and keeps every `<brain>.d/plugins/<name>/`. It says so (C-37). `--delete-data` also deletes those folders for brains in `brains.json`. No prompt.

Reserve the built-in name `hybrid`. It is trusted. It has no filter entry. `plugin add hybrid` fails with `plugin_exists` once C-18 is approved. Until C-18, still reject the add and assert only the code. Do not rank. The ranker is D-09. Enabling `plugin.hybrid.enabled` changes the setting and does not change search order, because there is no filter entry.

### Processes

Runtime dir: `$XDG_RUNTIME_DIR/remember/` when that variable is set, else `/tmp/remember-<uid>/`. Create it at mode 0700. Refuse it when another user owns it. A host may pass `RememberEnv.runtime_dir`.

Actions default to `once`. Filters are `persistent`. Socket: `<runtime>/s/<16 hex>.sock`. The hex is the first 16 hex characters of the SHA-256 of `<brain_id>/<plugin name>`. State file: `<runtime>/plugins/<name>/<brain-id>.state`, format `remember-plugin-state` version 1, fields `pid`, `started_at`, `strikes`, `crashes`. `crashes` holds instants from the last 10 minutes.

A process is alive only when `ping` answers within 200 ms. Hold an OS advisory lock while checking and starting. Start persistent filters in the background at `plugin enable`, after a backfill, at app launch, and on a write or search when no live process exists. A search never waits for that start.

Every CLI run sends `shutdown` reason `idle` to every process past its idle timeout. The timeout is `remember.plugins.idle_timeout` (default 10 minutes) unless the manifest sets `idle_timeout`.

Strikes: timeout, crash, malformed output, or oversize output. A well-formed error reply is not a strike. 3 consecutive strikes stop the process. 3 crashes in 10 minutes set `disabled_reason` `crash_loop` in `index.json` and warn `plugin_disabled_crash_loop` (C-26). Do not change the `enabled` setting. `plugin enable` clears `crash_loop`. A `ready.protocol` that differs from the manifest is not a strike. Set `disabled_reason` `protocol_mismatch`.

A plugin runs only when `enabled` resolves true, the trust record matches or the plugin is built-in, and the stored `disabled_reason` is null. Status value `user` is computed when the setting resolves false and the stored reason is null. Do not write `user` into `index.json`. Stored values are null, `crash_loop`, `protocol_mismatch`, or `untrusted`.

### Search

Three stages:

1. `search.query` rewrites the query first. Then take up to `remember.search.candidate_pool` (default 200) candidate ids with BM25 scores, after bin, tag, actor, and key filters.
2. Load the full rows of that pool once. Send each filter only its `needs`. Run `search.results`.
3. Keep the chain order. Drop ids that are missing or Deleted. Apply `--limit` when given, else `remember.search.max_results`. `data.total` is the length before the limit.

No enabled filters, or `--no-filters`: the result is BM25 order.

Priority: lower number first, default 500. Tie: plugin name, then entry name, byte order. No user setting changes priority, budget, or cold-start grace.

`remember.plugins.chain_budget` caps the chain. When the cap is reached, do not run the remaining filters. Status `skipped`, reason `chain_capped`. Do not emit `time_budget_capped`. Clip a budget that is larger than the time left. The clipped value is the `budget_ms` that is sent. No warning for the clip. Warn `plugin_chain_capped` as well (D-02).

`cold_start_grace` comes from the manifest. Default 10s. Range 1 ms to 30 s. `0ms` makes the manifest invalid. `plugin status` shows it next to `budget`. A cold filter that does not answer: the whole result is BM25 order, and the code is `plugin_filter_cold_start`. The locked sentence names hybrid/rerank only. Any other filter's sentence is C-29. The filter keeps loading.

A filter that sleeps past its budget: status `timeout`, code `plugin_filter_timeout`, input passes on. The sentence is C-27. A failure that is not a timeout is `plugin_filter_failed` (D-08). Its sentence waits on C-28.

Every search response has `data.filters[]`: name, version, status (`applied`, `skipped`, `failed`, `timeout`), time, and an optional note. Warnings that a plugin sends in its reply go only to the plugin log and `plugin status`. They do not enter command output. Warnings Remember raises do enter command output.

Human search prints no extra line when every filter applied. `remember search --explain` prints the filter table and `ranked by <plugin>/<entry>`.

### Event message

`type` `event`. `data` always contains: `event_id`, `event_type`, `occurred_at`, `detected_at`, `batch_id`, `cause` (with `origin`), `brain`, `memory`, `changes`, `truncated`, `truncated_fields`, `limits`, `plugin`, `entry`, `data_dir`, `settings`.

`data.memory` is `remember --json get` without `local_id` and without `version_vector`, plus `change_seq`. A plugin receives `id`, `bin`, and its `needs`. A hook's `needs` is every memory field. `settings` on the request is the plugin's own `plugin.<name>.*` values and each `remember.*` key listed in `permissions.needs`. No message writes a setting.

`data` is at most 262144 bytes. Over the limit, drop `changes.before.body`, then `links`, then cut `memory.body` to 16384 bytes on a UTF-8 boundary. Set `truncated` and `truncated_fields`. Handlers dedupe on `event_id`. Cloud webhooks send this same message. This plan builds the message. It does not send webhooks.

`cause.origin` is `local`, `sync`, or `import`. Default action origins are `["local"]`. `cause.hook` is `plugin/<name>/<entry>`. There is no `cause.plugin` field.

### Backfill and index

`data_dir` is `<brain>.d/plugins/<name>/data/`. `index.json` and `stderr.log` sit beside `data/`, not inside it. `index.json` is format `remember-plugin-index` version 1: `index_version` or null, and `disabled_reason` null or `crash_loop` or `protocol_mismatch` or `untrusted`. The folder is never synced, exported, or backed up.

Index states the plugin reports: `ready`, `building`, `stale`, `missing`, `failed`, `disabled`. After a completed full backfill, store `index_version`. A different `index_version` later marks the index `stale`. A search or a write only marks a backfill pending. It does not start it. A backfill runs on `plugin enable`, on `plugin reindex <name>`, and when the host asks at launch. The runner holds a per-plugin, per-brain lock and resumes from the last answered batch. A catch-up sends changes and purge tombstones with `change_seq` above `applied_through_seq`. A missing batch reply stops the stream. Exit 1 `plugin_backfill_incomplete`. A second run resumes. Search does not block while an index builds.

### Logs and CLI

Run log: `<brain>.d/hooks.log`, JSON Lines, mode 0600. Rotate at 5 MB, keep 3 files, delete entries older than 30 days, whichever comes first. `plugin log --follow --json` emits one `remember-message` per line. Type `plugin.run` for a run record. Type `plugin.stderr` with `data` `{plugin, line, at}` for a stderr line. A `skipped_source` record streams the same way, in the same run-record shape: a `plugin.run` line whose `data` is `{"version":1,"at","event":"skipped_source","event_type","command","origin","plugin","action","count"}`. There is no separate message type for it (Itay 2026-10-26).

Commands:

```
remember hook add NAME --command PATH [ARG…] --event EVENT [--event …]
                 [--tag-any P]… [--tag-all P]… [--created-by KIND]… [--updated-by KIND]…
                 [--bin BIN]… [--origin ORIGIN]… [--not-caused-by NAME]…
                 [--timeout 60s] [--cwd PATH] [--disabled]
remember plugin add DIR [--dev]
remember plugin list | show NAME | status [NAME]
remember plugin enable NAME [--scope brain] | disable NAME [--scope brain]
remember plugin trust NAME
remember plugin reindex NAME [--full]
remember plugin rerun NAME EVENT_ID
remember plugin remove NAME [--delete-data]
remember plugin log NAME [--follow] [--since DURATION]
remember plugin test NAME|DIR [--entry ENTRY] [--event EVENT] [--query TEXT] [--dry-run]
remember dispatch
remember sweep [--all]
remember purge LOCATOR
```

`hook add` filter flags set the action filter. `--command` takes the rest of argv until the next `hook add` flag. `--` passes arguments that start with `-`. `--origin` default is `local`. `--not-caused-by` is a plugin name, which matches every entry of that plugin, or `plugin/<name>/<entry>`.

`plugin test NAME` runs an installed entry in the foreground. `--dry-run` prints the filter decision, argv, env, and payload, and runs nothing. `plugin test DIR` runs the plan 24 fixtures against a folder in a scratch brain, with no install and no prompt.

`remember sweep` sweeps one brain and then dispatches. `sweep --all` does that for each brain in `brains.json`.

`remember purge LOCATOR` permanently deletes one row that is already expired or deleted. A live row exits 3 `wrong_bin`. The plain line is `"<key>" is live. Delete it first, then purge it.` Use the id when there is no key. Purge writes one tombstone. `delete --expired` moves every due, not-yet-deleted row to Deleted in one transaction, emits `memory.deleted` only, and has no cap. Bots never get purge. A run cannot purge or `delete --expired`.

Exit codes: 0 ok, including a degraded command with a warning. 1 usage or error. 2 not found. 3 wrong bin. 4 a warning under strict. `plugin status` exits 0 whenever it can report.

JSON types follow `<noun>.<verb>.result`. A hook appears in `plugin.list.result` like any plugin, with `timeout` and `timeout_ms` on its action.

Manifest names match `^[a-z][a-z0-9_]{0,39}$`. A hyphen in a manifest is `plugin_manifest_invalid`. The fix suggests the `_` form (C-36). A manifest setting key `enabled` is `plugin_manifest_invalid`. Generate `plugin.<name>.enabled` for each installed plugin, scopes user and brain, default false. Generate other keys from the manifest `settings` list. Do this in this plan, not in plan 20.

### App contract, not the app

The engine does not install launchd or systemd. The Mac app installs LaunchAgent label `app.fortkeeps.remember-sweep`, command `remember sweep --all`, `StartInterval` 3600, only as the fallback when the daemon is not running. Linux docs name user units `io.github.itayavtalyon.remember.sweep.service` and `io.github.itayavtalyon.remember.sweep.timer` with `OnUnitActiveSec=1h`. Do not install the daemon unit. Do not use label `app.fortkeeps.daemon`.

Empty Trash in the app is one `purge` per Deleted memory. No Touch ID. The engine has no Empty Trash command.

The app supplies confirm, dispatch, and token callbacks. Confirm uses Touch ID or the device password and returns true only after Allow. That UI is remember-mac.

## Out of scope

- Daemon socket, handshake, crash rule, stale locks, HTTP MCP, per-bot tokens, activity log (D-06).
- URL expansion plugin (D-07). Do not move the remember-mac fetcher.
- Hybrid ranking (D-09).
- remember-mac panes, toasts, and a11y tests (D-12).
- Sync wire (D-11).

## Prerequisites

Plans 19, 20 (stages 1–4 at least), 21, and 24 are merged. Generated plugin keys need plan 20's registry functions.

## Files

Keep SQL in `src/store_sqlite.c`. Pure manifest and filter match in `src/plugin_manifest.c`. Process spawn, locks, and the dispatcher in `src/dispatch.c`. CLI in `src/cmd_plugin.c`. Register new translation units in `REMEMBER_LIB_SOURCES` and `-Wcast-qual`. Do not add a plugin framework, a hook registry separate from plugins, or `hooks.json`.

Tests: `tests/test_plugins.c`, suite `plugins`. Use the plan 24 fixtures.

## Must-pass tests

Use the 010 verification numbers. Skip any assertion whose sentence is still in COPY-PENDING. Assert the code and the stored state instead.

| Test | Proves |
|---|---|
| 1 | Six event types. Link emits two `memory.updated` with one `batch_id`. No version-vector change on the ends. |
| 2 | No enabled action means no outbox row. |
| 3 | Outbox and memory commit or roll back together. |
| 4 | Kill the dispatcher mid-run. The next write re-delivers the same `event_id`. |
| 5 | Two dispatchers. Only one holds the lock. |
| 6 | Each filter miss inserts nothing, logs nothing, and increments `skipped_filter`. |
| 7 | A hook that writes the memory it listens for sees `REMEMBER_CAUSE_HOOK` equal to `plugin/<name>/run`. |
| 8 | Chain 100 halts, plugin stays enabled, locked `plugin_loop_halted` line, rerun resets depth. `plugin_not_rerunnable` code when there is nothing to rerun. Sentence C-22 waits. |
| 9 | Pre-flight failure: recipient `failed`, no run, exit 0, exit 4 with `--strict`. A missing or non-executable file is `plugin_preflight_failed`, and a changed hash is `plugin_untrusted`. State only until C-20. |
| 10 | Truncation order at 256 KiB. |
| 11 | argv and env contain no body and no tag text. |
| 12 | No self-retry. Drop after 7 days with `dropped_stale`. |
| 13 | Hash mismatch sets `untrusted` and does not run. Trust clears the reason and keeps `enabled`. Codes for approval wait on C-15 and C-16 for their sentences. |
| 14 | Expiry sweep emits once. Revival clears `expired_at`. Purge of an unmarked expired row emits only `memory.purged`. |
| 15 | Default origins skip sync and import. `--hooks` runs them. One `skipped_source` line per skipped action and event type, with the H-7.2 fields, `event_type` and `count`. |
| 16 | Sweep with no plugins inserts no outbox row. |
| 17 | `REMEMBER_PROCESSES=0` returns `plugin_unsupported` and inserts nothing. A built-in filter path still runs. Hybrid has no filter, so this test uses a test double built-in registered only in the test binary. Do not ship that double. |
| 19 | Hook depth blocks `delete --expired`, `purge`, `import`, and `sweep` with the locked `plugin_run_forbidden` line. |
| 20 | No filter matches today's BM25 order. Stage 3 drops a Deleted id. |
| 21 | Timeout code `plugin_filter_timeout`, status `timeout`, exit 0. Sentence waits on C-27. |
| 22 | Cold filter: BM25, code `plugin_filter_cold_start`, process keeps loading. Sentence for a non-hybrid name waits on C-29. |
| 23 | Three crashes in brain A set `crash_loop` in A's `index.json` only. `enabled` is unchanged. |
| 24 | A new `index_version` marks `stale` and pending. Search does not start the backfill. |
| 26 | Plan 24 fixtures pass `plugin test DIR`. Hybrid is not in this suite. |
| 27 | `plugin status --json` exits 0. |
| 28 | `delete --deleted` is unknown. Purge of a live row exits 3 with the locked line. Purge of a deleted row writes one tombstone. |
| 29 | One plugin's second run starts after the first ends. |
| 31 | `remember_call` invokes the dispatch callback once and does not fork. |
| 32 | Update shows changed needs and shuts down with `upgrade`. `plugin add hybrid` is `plugin_exists`. |
| 33 | A persistent plugin that sets the cause env extends the chain. |
| 34 | A lost reindex exits 1 `plugin_backfill_incomplete` and resumes. Sentence waits on C-24. |
| 35 | `brain list` after a read-write open. `brain prune` drops a missing path. |
| 36 | `hook add wake --command /bin/echo --event memory.created` writes `plugin.wake.enabled` true on the user layer. `--disabled` writes no row. |
| copy | A copied brain file mints a new `brain_id`. The warning waits on C-38. A moved brain keeps the id and updates the path. |
| chain cap | Remaining filters are `skipped` / `chain_capped`. No `time_budget_capped`. The `plugin_chain_capped` warning as well (D-02). |
| hybrid | Search order with `plugin.hybrid.enabled` true equals BM25. |

Do not implement 010 test 25 (URL). Do not implement test 18 beyond "default origins insert no row", which test 15 already covers.

007 tests 8, 10, 23, and 30 move here: manifest key `enabled` is invalid, untrusted `settings set` of `plugin.<name>.enabled` fails with `plugin_untrusted` once C-17 exists, `plugin enable` and `plugin disable --scope brain`, and `hook add url-expand` creates `url_expand`.

## Stages

1. Manifest parser and name rule. Generated settings keys through plan 20's registry. No process yet.
2. `brains.json` and JE-3.10. Warning text waits on C-38.
3. Outbox insert, write-time filter, post-command sweep. No spawn yet.
4. Trust file and confirm callback. Sentences C-15, C-16, C-17, C-18, and C-37 wait. State changes do not.
5. `hook add` and `plugin add` / `enable` / `disable` / `remove` / `list` / `show`.
6. Dispatcher, pre-flight, kill switch, rerun, sweep. Locked sentences that are already approved (`plugin_loop_halted`, `plugin_run_forbidden`, purge-live) land here.
7. Search stages, manifest budgets, chain cap with `plugin_chain_capped` (D-02).
8. Persistent processes, acks, backfill, `index.json`.
9. `plugin test` against plan 24 fixtures.
10. Stop for every remaining COPY-PENDING and DESIGN-PENDING row this plan names. Apply the approved sentences only. Do not start URL, hybrid ranking, or the daemon.

Each stage: one red test, the gate, review, then the next stage.

## Cloud contract

- Webhooks send the H-3 event message. Delivery retries are not this plan.
- Cloud-run plugins use protocol 1. The Cloud-built hybrid index stays on the server. Nothing Cloud-built is downloaded into `plugins/<name>/data/`.
- `cause.origin` `sync` is how a synced write is marked. Default hooks do not run for it.
- Cloud v1 does not offer end-to-end encryption.

## How to work

Same loop as plan 19. Load `standard:coding` and the review docs.

Branch from clean `main` after plans 19, 20, 21, and 24. One branch. One pull request.

Do not invent IPC, a ranker, a parser for URL HTML, a warning code, or a sentence. Simplicity: one plugin model, one trust file, one outbox, no daemon process in this plan.

Quality gate: plan 19's commands. Coverage 100/100.

## Definition of done

- [ ] Must-pass rows that are not waiting on copy or design are green.
- [ ] No daemon, no URL plugin, no ranker, no `hooks.json`, no `time_budget_capped`.
- [ ] Waiting sentences are unprinted, or they match an approved COPY-PENDING row quoted here.
- [ ] Gate green. Second review clear.
- [ ] INDEX row 25 is ticked only when the waiting rows are closed and the review is closed.

## Second review handover

Probes:

1. `hook add` on a TTY with the wrong typed name. [nothing installed]
2. Add a memory with an enabled hook whose script is missing. [memory exists, recipient `failed`, exit 0]
3. Same add with `--strict`. [exit 4]
4. `--no-hooks`. [no outbox row]
5. Chain of 100. [halted, enabled still true, locked warning]
6. Copy a brain file beside the original and open the copy. [new `brain_id`]
7. Move a brain file and open it. [same `brain_id`, path updated]
8. Enable hybrid and search. [same order as BM25]
9. Confirm no daemon binary and no listen socket for HTTP.

Do not re-open: hooks are plugins, trust on the hash, CLI and host only, hourly sweep is the app's fallback, hybrid is disabled and does not rank.

## Implementation Notes

_(empty)_

## Review Notes

_(empty)_
