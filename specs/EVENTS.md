# Memory events and the outbox insert

Status: what a write that changes a memory does for plugins: the events it makes, the filter that picks the plugin actions that get each one, and the outbox row it writes in the same transaction. The code is `src/events.c` (rules, filter, planning, payload, `hooks.log`), `src/events.h`, the temp triggers and the outbox insert in `src/store_sqlite.c`, and the wiring in `src/remember_app.c`. Nothing here starts a process. Delivery is [OUTBOX-DELIVERY.md](OUTBOX-DELIVERY.md) (the state machine) and plan 35 (the dispatcher).

Decisions are `decision:…` entries tagged `subject:plugins-and-hooks`. Log 010 H-1, H-3, H-4 and H-7 and [PLUGIN-PROTOCOL.md](PLUGIN-PROTOCOL.md) R3 are the sources; where this spec and they differ, this spec says what the engine does.

## When an event is made

`decision:2026-11-08-sync-record-order-hooks-delivery`, `decision:2026-11-17-plan31-grill-purged-processes-cause`.

A brain has events only while at least one enabled plugin has an action. A plugin is enabled when `plugin.<name>.enabled` is true for the brain (SETTINGS.md "Plugin keys"). While any plugin is installed, a write reads that key for each one; when it cannot be read (an unreadable or corrupt `user.db`), the write is refused with the store's reason, for example `sqlite: file is not a database`, code `database`, exit 1, and nothing is saved. Saving the write with no events would drop them silently (`decision:2026-11-18-plan31-review-grill` G3). A search filter (`filters` in `plugin.json`) never gets an event. A command that changes no memory (`list`, `get`, `search`, `tags`, `settings`, `device`, `brain`, `repair`, `export`) sets up nothing.

The store does not look at the command. It finds what a write changed. Before the first change of a memory (its row, its tags or one of its links), a temp trigger keeps the memory as it was. Just before `COMMIT`, each memory the write touched is compared with how it is now, and the comparison gives its events:

| The memory | Event |
|---|---|
| is a new row | `memory.created` (also a row that is new and already in the Trash) |
| is a row that is gone | `memory.purged`, with the memory as it last was |
| had no `deleted_at` and has one | `memory.deleted` |
| had a `deleted_at` and has none | `memory.restored` |
| has a changed `body`, `tags`, `key` or `expires_at` | one `memory.updated` for those fields |
| has a live link that came or went | one `memory.updated` for each such link, with `changes.link` |

`memory.deleted` and `memory.restored` carry no `memory.updated` for the other fields of the same write. A new row gets no `memory.updated` for its own links. `updated_at`, `updated_by`, `version_vector` and `change_seq` are not fields: a write that changes only them (a `link` that touches the ends) makes no event for the memory itself. An `add` that merges nothing new changes nothing at all, also while events are on: the triggers' own rows never count as a change of the memory. A link makes one `memory.updated` per end (H-1.2), and no `memory.linked`. Purging a memory that has links also makes a `memory.updated` for each neighbour, because its link went (the cascade is a change like any other). `memory.expired` comes from the sweep, below.

Every write path gets this, including ones plan 31 did not name: `add`, `update`, `delete`, `rekey`, `link`, `unlink`, `import` (JSON and `--from-db`), `conflict accept` and `purge LOCATOR`. The event types follow what a conflict resolution changed (`decision:2026-11-18-plan31-grill-chain-env-conflict-accept`). `purge LOCATOR` writes one `memory.purged`; the whole-bin forms are gone (`decision:2026-11-18-plan32-grill-purge-delete-forms`).

## The outbox row

The v6 table of log 010 §17, `outbox(event_id, type, payload, recipients, created_at)`. Plan 31 adds no column.

One row for each event that at least one enabled action matches (the filter below), inserted in the write transaction, so the memory and its rows commit or roll back together. `event_id` is a new UUID v7. `type` is the event type. `created_at` is the write time. `payload` is the frozen `data` of the event message. `recipients` is `[{"entry":"plugin/<name>/<entry>","state":"pending"}, ...]`, one item for each action that matched, in plugin folder order and manifest order. A plugin added later never sees an event already written.

An event no action matches writes nothing. A miss that is not the origins test adds 1 to that action's `skipped_filter`, an in-process counter; it writes no log line (H-4.1).

## The filter

`decision:2026-11-18-plan31-grill-filter-semantics`, `decision:2026-11-18-plan31-review-grill`, `decision:2026-11-13-plugin-origins-empty`, `decision:g-11`, `decision:g2-10`.

An action matches an event when every test passes. A test with an empty or omitted list passes.

- `events`: the event type is in the list, or the list is `memory.*`.
- `tags.any`: at least one tag of the memory matches one pattern. `tags.all`: every pattern matches some tag. A pattern is a tag, or ends in `*` and matches every tag that starts with the text before the `*`. `plugin.json` is read with each pattern folded the way `add --tag` folds a tag (trimmed, ASCII lowercase), the text before a `*` too, so `Work` matches the stored `work`. Spaces just before the `*` stay, since a tag may hold a space (`proj *` does not widen to `proj*`). A pattern that no tag could fold to (empty, or holding a tab or another control) makes the manifest invalid. The tags are those after the write (the last ones, for a purge). With `tags.match_before` true, the tags before the write may pass the test instead. A new memory has no tags before.
- `created_by`, `updated_by`: the actor kind of the memory's creator, and of its last writer, after the write.
- `bins`: the bin the memory had before the write (`live`, `expired`, `deleted`). A new memory is tested by its own bin. `memory.purged` carries the last real bin, so `purged` is not a bin here and makes the manifest invalid; `events: ["memory.purged"]` says "only purges".
- `not_caused_by`: the event is skipped when `cause.hook` or any `cause.chain[].hook` is a listed `plugin/<name>/<entry>`, or belongs to a listed bare plugin name. A value of any other shape (`Wake`, `plugin/wake`) makes the manifest invalid. A direct write passes.
- `origins`: the event's `cause.origin` is listed. An omitted list is `["local"]`. A list that is empty matches nothing.

`--hooks` skips the `origins` test and no other (`decision:s-14`). An action of a plugin that is not enabled is not in the rules at all, so `--hooks` does not reach it. `--no-hooks` makes no row and counts nothing.

## Origins and `skipped_source`

`decision:hook-origins-filter`, log 010 H-7.1 to H-7.3.

`cause.origin` is `import` for `remember import` and `local` for every other command. The origin `sync` is the value plan 45 will pass. An action whose only miss is `origins` is skipped for that event and adds 1 to `skipped_source` for its event type. After a command with origin `sync` or `import` has run, one line is appended to `<brain>.d/hooks.log` for each action and event type with a count, then the counters go to 0:

```json
{"version":1,"at":"2026-10-25T18:00:00.000Z","event":"skipped_source","event_type":"memory.created","command":"import","origin":"import","plugin":"wake_coder","action":"run","count":12}
```

A local command writes no such line. A log that cannot be written (no folder, or `hooks.log` is a symlink or not a regular file: the file is opened `O_NOFOLLOW | O_NONBLOCK`, mode 0600, append, so a FIFO there never hangs the command) is not an error for the command, and the counts stay. `hooks.log` is created only when there is a line to write.

## Flags

`decision:s-14`, `decision:2026-11-18-plan31-grill-hooks-flags-together`.

Every command that changes a memory takes `--hooks` and `--no-hooks`. Both together is a usage error, exit 1, `--hooks and --no-hooks can't be used together`, before the command writes. The sentence is a draft (`decision:open-2026-11-18-plan31-hooks-flags-together-copy`).

## The event message

H-3, [PLUGIN-PROTOCOL.md](PLUGIN-PROTOCOL.md) R3.2 to R3.11.

`payload` is the `data` object with `event_id`, `event_type`, `occurred_at`, `detected_at`, `batch_id`, `cause`, `brain`, `memory`, `changes`, `truncated`, `truncated_fields` and `limits`. It has no `plugin`, `entry`, `data_dir` or `settings`: plan 35 adds those for each recipient.

- `occurred_at` and `detected_at` are the write time.
- `batch_id` is one UUID for a command, made with its first row, also for a command that changes one memory (`decision:2026-11-18-plan31-grill-link-direction-batch-id`). The schema's null is never written.
- `cause` is `{command, origin, hook, run_id, depth, chain}`. `command` is `add`, `update`, `delete`, `purge`, `rekey`, `link`, `unlink`, `import` or `sweep`; `conflict accept` writes `update` (`decision:open-2026-11-18-plan31-conflict-accept-command`). The rest comes from the environment of a run (below).
- `brain` is `{path, device_id}`: the absolute path of the brain, with the home folder written `~`.
- `memory` is what `remember --json get` prints without `local_id` and `version_vector`, plus `change_seq`, then `links`. Each link stub is `get`'s stub without `local_id`. For a purge it is the last state of the row, with no links.
- `changes` is `null` except for `memory.updated`. For fields: `{"fields":[...],"before":{...},"tags_added":[...],"tags_removed":[...]}`; `fields` lists `key`, `body`, `tags`, `expires_at` in that order, only those that changed, and `before` has the old value of each. For a link: `{"link":{"kind","direction","other":{"id","key"}}}`. `kind` is `related`, `supersedes` or `cites`; `direction` is relative to the event's memory: `outgoing` when it is the `from` end, `incoming` when it is the `to` end, `both` for `related`. `changes.link` does not say whether the link came or went; a reader compares `memory.links`.
- `truncated` and `truncated_fields`: `data` is at most 262144 bytes. Over that, the engine drops `changes.before.body`, then `memory.links` (the list is `[]`), then cuts `memory.body` to 16384 bytes on a UTF-8 boundary, in that order and each only when it removes something and the text is still over. `truncated_fields` names the cuts made: `changes.before.body`, `memory.links`, `memory.body`.

Known limit: the three cuts are all H-3.4 names. A memory with enough tags, or a long key, can leave `data` over 262144 bytes after them, and the row is still inserted. (`decision:open-2026-11-18-plan31-payload-over-limit`.)

## The cause of a write in a run

`decision:2026-11-18-plan31-grill-chain-env-conflict-accept`, H-5.2, H-5.3.

A write is in a run when `REMEMBER_HOOK_DEPTH` is a whole number from 1 to 1000000. Then `cause.depth` is that number, `cause.hook` is `REMEMBER_HOOK_NAME` and `cause.run_id` is `REMEMBER_HOOK_RUN_ID` (each is null when it is not valid: a hook is `plugin/<name>` or `plugin/<name>/<entry>`, a run id a lowercase UUID). `cause.chain` is `REMEMBER_HOOK_CHAIN`, a JSON array of `{hook, event_id, memory_id, depth}` of at most 99 items, plus this run's hop `{hook: REMEMBER_HOOK_NAME, event_id: REMEMBER_CAUSE_EVENT_ID, memory_id: REMEMBER_ID, depth}`. A chain or a hop that is missing or not valid gives `[]`: the write counts as depth 1. A write outside a run is `{hook: null, run_id: null, depth: 0, chain: []}`. Plans 35 and 36 set the variables this way.

## A build that cannot start programs

`REMEMBER_PROCESSES=0`, log 010 H-0.7, copy C-23 (E3B-1 entry 15, `decision:2026-11-12-copy-batch-e3b-warning-splits`).

The CMake option `REMEMBER_PROCESSES` (default ON) picks the default; `events_set_processes(false)` turns it off in a process (plan 29 feeds it from `REMEMBER_CAP_PROCESSES`). With it off, a write inserts no outbox row. An event that an enabled action matches is counted, and the command ends with the warning `plugin_unsupported`, exit 0 (4 with `--strict`), the write saved:

- message: `the write was saved, but this build of remember can't start programs, so its hooks and plugins didn't run.`
- fix: `Use a build of remember that can start programs.`
- plain line: `remember: warning: <message> <fix>`

An event no action lists, a disabled plugin and `--no-hooks` raise no warning.

## Expiry

Log 010 H-1.4 to H-1.7, H-16.7, H-16.8; `decision:expiry-local-marker`, `decision:purge-command`, `decision:2026-11-18-plan32-grill-purge-delete-forms`, `decision:2026-11-18-plan32-grill-sweep-triggers`, `decision:2026-11-18-plan32-grill-sweep-warning-and-wrong-bin`.

A memory is expired when it has no `deleted_at` and `expires_at` is at or before now. That is read from the clock at every command. `entries.expired_at` is a different thing: the time this device's sweep noticed it. It is NULL until then. It is device-local: setting it changes no `updated_at`, no version vector and no `change_seq`, and it does not sync.

**The sweep.** In one write transaction it sets `expired_at = now` on each row with `deleted_at IS NULL AND expires_at <= now AND expired_at IS NULL`, and writes one `memory.expired` outbox row per changed row (the rules of the outbox above: only when an enabled action matches). `occurred_at` is the row's `expires_at`, `detected_at` the sweep time, `cause.command` is `sweep`, `cause.origin` `local`, and `changes` is null. The rows of one sweep share one `batch_id`, apart from the command's own. `--hooks` and `--no-hooks` do not reach a sweep. With no enabled plugin action nothing is written but `expired_at`. With nothing due the sweep reads and takes no write lock.

**When it runs.**

- After every command that opened the brain read-write, reads included, once that command's own work is done, whatever the command's exit (`decision:2026-11-18-plan32-grill-sweep-triggers`). Not after a command that opens no brain (`--version`, `help`, `brain list`, `brain prune`, `sweep --all`) or after `sweep` itself.
- Not on a brain from a newer remember. The command prints its own warning or error; the sweep adds nothing and the exit code does not change.
- A sweep that fails warns `sweep_failed` and changes no exit code (4 under `--strict`, like every warning). When the command itself failed, the failure of its sweep is silent: one error is enough. The wording is a draft (`decision:open-2026-11-18-plan32-sweep-failed-copy`).
- `remember sweep` sweeps this brain and prints the count of rows it marked (JSON `sweep.result`, `data{expired}`). `remember sweep --all` reads only `<data dir>/brains.json` and sweeps each listed brain, in file order, with the plugins of this machine and each brain's own settings. A path with no brain gets the `brain_missing` warning (C-01) and its entry stays until `brain prune`. A newer brain is skipped. `sweep --all` opens no brain for itself and records nothing in `brains.json`. The count line is a draft (`decision:open-2026-11-18-plan32-sweep-count-copy`).

**When `expired_at` is cleared.** One temp trigger of the connection clears it in the same write as any change that starts the row's expiry over: `expires_at` set to a later time, to NULL, or from NULL to a time, and a restore from Deleted. A change to an earlier time keeps it (the row was already due). A restore of a row that is already due writes `memory.restored` in the command's own transaction; the sweep after the command then writes `memory.expired` (H-Q6: two events, two transactions).

**Purge and `delete --expired`.**

- `remember purge LOCATOR` removes one row that is already expired or deleted, writes one `tombstones` row and one `memory.purged` (log 008 JE-5.2). It takes an id, a UUID or `--key`, and `--hooks`/`--no-hooks`; no bin flag, no `--as`, no `--actor`. A live row exits 3 with the C-01 `wrong_bin` line (code `wrong_bin`). An expired row the sweep has not marked yet is purged with only `memory.purged`.
- `remember delete --expired` moves every row that is not deleted and is due to Deleted, in one transaction, one `memory.deleted` each and one `batch_id`, no cap, no `memory.expired`. It takes no locator. `delete --deleted`, `purge --expired`, `purge --deleted` and `purge --trash` are unknown options.

## Not here

Dispatch after a sweep (plan 35) and the daemon's sweep (plan 39). Dispatch, the pre-flight checks and runs (plan 35). Trust (plan 33). Sync apply (plan 45) passes the origin `sync`.
