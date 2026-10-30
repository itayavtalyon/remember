# 21 — JSON envelope, locators, actors

## Goal

Make every CLI `--json` response one `remember-message` version 1. Rename memory identity from `sync_id` to `id`. Store actors on new memory writes. Change `--trash` to mean the Trash.

## Status

Copy approved 2026-10-25. Print the C-01 locator line, the Trash and expired wrong-list lines, and C-35 from [`COPY-PENDING.md`](COPY-PENDING.md). Help for `--color` and `--quiet` is C-02. Do not invent a sentence past that file.

The field list, the write table, and the flag removals are specified. This plan starts after plan 19.

## Source of truth

- [`design-logs/008-json-envelope.md`](../design-logs/008-json-envelope.md) Design JE-1 through JE-7. §8 is plan 19.
- [`design-logs/010-plugins-and-hooks.md`](../design-logs/010-plugins-and-hooks.md) H-16.7 for the purge-live sentence, H-16.9a for `--trash` and `--expired`, H-16.10 for color and quiet, H-16.11 for the hook help lines.
- [`docs/JSON-STYLE.md`](../docs/JSON-STYLE.md) for the value rules JE-4 names. Where JSON-STYLE and log 008 disagree, log 008 wins.

Do not follow the draft step that says to re-extract plans 16, 17, 18, and 22. Update tests that still require `--json-errors`, `--offset`, `--sync-id`, or `source`. Do not edit those plan files.

## Context

One message format exists: `format` `remember-message`, `version` 1. Every CLI `--json` success and failure uses it. `remember_run` and `remember_call` use it. HTTP, MCP, plugin sockets, hook stdin, the `ready` line, and Cloud webhooks use it. This plan implements the CLI, `remember_run`, and `remember_call`. It does not implement HTTP or MCP servers (D-10).

Envelope field order: `format`, `version`, `type`, `request_id`, then `data` or `error`, then `next_cursor`, `has_more`, `warnings`. Omit an absent optional field. Do not write JSON null for an omitted field. `next_cursor` is null on the last page. That is the one exception.

No envelope `id`. No `type` `error`. A failed response keeps `<noun>.<verb>.result` and has `error` instead of `data`. Never both. Never null. When the CLI cannot tell the command, `type` is `unknown.result`.

`error` has `code`, `message`, `fix`, `retryable`, and optional `detail`. `fix` is a non-empty string. For a code whose message and fix are already split in a merged plan (plan 16 catalog), in ST-3.6, or in COPY-PENDING C-01, keep that split.

`warnings` appears only when it has one or more items. Each item has `code`, `message`, and optional `detail`. With `--json`, warnings and failures go only to the stdout envelope. stderr is empty. Plain mode keeps `remember: …` on stderr. Delete `--json-errors`. There is no replacement flag.

`request_id` is a UUID v7 on a reply. CLI output is not a reply. Omit `request_id` on CLI output.

`next_cursor` and `has_more` appear only on list responses, always together. A bad cursor is `invalid_argument`. The cursor is opaque. Search does not page. Search has no `next_cursor`, no `has_more`, and no `offset`. Delete `--offset`.

Search `data` has `count`, `entries`, `total`, `query`, `ranked_by`, `filters`, and the 006 `keys` and `tags` facets. `total` is the number of ids after the filter chain and before the limit. `count` is the number returned. Until plan 25, the chain is empty, so `total` is the BM25 count before `--limit` or `remember.search.max_results`. `filters` is an empty array until plan 25. Do not invent a ranked_by plugin name. With no filter, `ranked_by` is the BM25 label plan 18 already emits. Keep that label.

A memory id is `id`: lowercase hyphenated UUID v7, no prefix. A prefixed form such as `mem_…` or a brace form fails with `invalid_argument`. The integer is `local_id`. Only CLI output includes `local_id`. HTTP, MCP, plugin messages, events, and export files do not. Plan 22 omits it.

A CLI locator is the positional argument or `--key KEY`. The positional accepts a UUID, matched as `id`, or digits, matched as `local_id`. Anything else fails with `invalid_argument`. The plain line, message, and fix are the C-01 positional-locator row. `<value>` is the token the user typed. Delete `--id`, `--local-id`, and `--sync-id`. Delete the field `sync_id`. Delete the env name `REMEMBER_SYNC_ID`. No alias.

`get`, `add`, `update`, and `rekey` return `data.memory`. A list returns `data.entries` and `data.count`. `add` output includes `links`, the same fields as `get`.

Actors. A memory has `created_by` and `updated_by`. Each is `{kind, id}`. `kind` is `human`, `agent`, `tool`, `share`, or `import`. `id` is a string or null. There is no `source` field. There is no actor kind `sync`.

- A person at the CLI gets `human` and id null.
- `--as KIND` replaces `kind`. Allowed input: `human`, `agent`, `tool`, `share`. Not `sync`. Not `import`.
- `--actor ID` replaces only `id`. `ID` is 1 to 64 characters of `[A-Za-z0-9_./:-]`. `--as` does not clear `--actor`.
- `--as` replaces the old `--source` flag. Delete `--source`.
- Every plugin write, including a hook, is `{kind: tool, id: plugin/<name>/<entry>}`. A hook id is `plugin/<name>/run`. Plan 25 stamps those writes. This plan rejects `--as` and `--actor` when `REMEMBER_HOOK_DEPTH` is 1 or more, with `invalid_argument`. That sentence is not written. Do not invent it. The copy editor writes it. It is the same hole plan 20 names. The code stays `invalid_argument`.
- Import keeps the file `created_by` and sets `updated_by` to `{kind: import, id: null}` on each memory row it writes. Plan 23 does that write. This plan stores the columns and the rule. Settings import is different and is plan 20.
- Sync keeps the author's `created_by` and `updated_by`.

Search filters are `--created-by KIND` and `--updated-by KIND`. Delete `--source` as a filter.

Write table. Implement this on every memory write path. A check means the write sets that field. A dash means it does not.

| Write | `updated_at` | `updated_by` | memory vector | `change_seq` | Event |
|---|---|---|---|---|---|
| body, tags, key, or `expires_at` changes | set | set | +1 | set | `memory.updated` |
| `link` or `unlink`, on each end | set | set | do not change the memory vector. The link row's vector +1 | set on the end and on the link row | `memory.updated` with `changes.link` |
| `delete` to Deleted | set | set | +1 | set | `memory.deleted` |
| restore | set | set | +1 | set | `memory.restored` |
| `purge` | row removed | dash | dash | one `tombstones` row | `memory.purged` |
| expiry marking (`expired_at`) | dash | dash | dash | dash | `memory.expired` |
| a write that changes nothing | dash | dash | dash | dash | none |
| sync apply | the author's | the author's | merged | set | that change's event, `cause.origin` `sync` |
| import | the file's | `{import, null}` | the file's, merged | set | that change's event, `cause.origin` `import` |

`brain_meta.change_seq` is the counter. Each changed entry, link, brain settings row, or tombstone gets the next value, in write order, in the same transaction. Purge deletes the entry and inserts `tombstones(id, change_seq, purged_at)`. Unlink sets `entry_links.deleted_at` and keeps the row. A user-layer settings write does not change the counter. Purge never lowers the counter. Plan 20 owns the settings bump. This plan owns entry, link, and tombstone bumps. Plan 25 inserts the outbox row for the event. This plan performs the field changes. It does not build the outbox payload.

`remember brain new-id` replaces `brain_meta.brain_id` with a new lowercase UUID v7 and prints that id. It does not change `change_seq`, a version vector, or a memory. `--json` type is `brain.new_id.result`. `data` is `{brain_id, previous_brain_id}`. The command name is `remember brain new-id`. Do not add `remember brains`.

Do not implement JE-3.10. Until plan 25, open does not read `brains.json`, does not mint a new id for a copy, and does not rewrite a path for a move.

`--trash` selects the Deleted bin. `--expired` selects expired memories. `--trash` is not an alias of `--expired`. This replaces the 0.2 behavior from plan 12. For one release, a command that uses `--trash` prints one warning. Code `flag_meaning_changed`. Detail `{"flag":"--trash","use":"--expired"}`. Plain: `remember: warning: --trash now means the Trash (deleted memories). For expired memories, use --expired.` With `--json`, that item is in `warnings` and stderr is empty. Under `--strict` or `REMEMBER_STRICT=1`, any warning exits 4. Exit 4 has no other meaning. Remove the warning in the release after 0.3.0. Do not remove it in this plan.

Help, exact:

```
--trash     Use the Trash: memories you deleted. Restore them, or purge them for good.
--expired   Use expired memories: ones past their expiry time. They are not in the Trash.
```

Wrong list, exit 3. Use the key when the row has one, otherwise the id:

- `remember: "<key>" is in the Trash. Add --trash to use it.`
- `remember: "<key>" has expired. Add --expired to use it.`

`purge-trash` does not exist. Do not add `empty --trash`. Purge of one live row is plan 25. It prints the C-01 `wrong_bin` row.

`delete --deleted` does not exist. Restore from Trash is `update --trash --undelete`.

Color and quiet (H-16.10). Apply them in this plan because they wrap every command:

- Color only on a TTY, when `NO_COLOR` is unset and `TERM` is not `dumb`. `--color=auto|always|never` overrides.
- Status is a word. Do not print checkmarks. Do not encode meaning by color alone.
- Progress goes to stderr only, when stderr is a TTY, as whole lines, at most once a second, with no spinner. `--quiet` turns it off. Progress is never in stdout and never in `--json`.
- Prompts echo typed input.
- `--color` and `--quiet` are global, listed with `--db`, `--json`, `--strict`, `--busy-timeout`, and `--no-create`. Help sentences for `--color` and `--quiet` are C-02. Print them.

Hook help lines, exact, even though the flags act in plan 25:

```
--hooks     Run matching hooks for this write, also ones you turned off for local writes.
--no-hooks  Run no hooks for this write.
```

Accept the flags in the parser now. Plan 25 implements their delivery effect. Until plan 25, `--hooks` and `--no-hooks` are accepted and do not change delivery, because there is no outbox writer yet. Document that in the stage notes. Do not make them errors.

`REMEMBER_TEST_FIXED_IDS=1` makes ids and timestamps predictable in a test build. A release build ignores the variable.

An unknown field is an error in `plugin.json` `run`, `permissions`, and `filter` (plan 25). Everywhere else it is a warning `unknown_field`, once per file or stream. The plain lines are C-35. Print them. Do not silently ignore the field. Export and import files are plans 22 and 23.

## Locked decisions

| ID | Rule |
|---|---|
| S-07 | One format, `remember-message` 1, on every surface. |
| S-08 | No `--offset`. Search does not page. Lists use a cursor. |
| S-13 | Locator is positional or `--key`. |
| JE-4.4 | Actors. No `source`. No kind `sync`. |
| JE-5.1 | The write table above. |
| G2-03 / S-09 | Any warning under `--strict` exits 4. Exit 4 means only that. |
| M-23 / H-16.9a | `--trash` is the Trash, with the locked warning. |
| #572 | No `--json-errors`. |

## Prerequisites

Plan 19 is merged, so `id`, `local_id`, `brain_meta`, and `tombstones` exist.

Plan 20 stages 1–4 may land before or after this plan. Settings command JSON uses this plan's writer. See plan 20 for that order.

## Scope

In: `output.c` envelope, removal of the old flags, locator change, actor columns on new memory writes, the write table, `remember brain new-id`, `--trash` meaning, color and quiet globals, test updates for plans 16 and 18 that cite removed flags.

Out: `brains.json` and copy detection (plan 25). Settings storage (plan 20). Export files (plan 22). Import readers (plan 23). Outbox payloads (plan 25). HTTP and MCP servers (D-10). Daemon.

## Files

Keep the writer in `src/output.c`. Do not add a second JSON library. Locators stay in the existing locator code. Flag table stays in `src/cli.c`. Actor stamping for memory writes stays in the command that already writes the memory, calling one helper. Put that helper next to the existing actor or source code. Do not add a new module for the envelope.

Tests: update `tests/test_errors.c` and the search and store tests that still expect `sync_id`, `source`, `offset`, or `--json-errors`. Add `tests/test_envelope.c` for the field order. Suite name `envelope`.

## Must-pass tests

| Test | Proves |
|---|---|
| 008-1 | Every CLI `--json` golden output validates as `remember-message` 1. Schema files for the message live in `schema/v1/` when plan 24's extraction pattern exists. Until those files exist, validate the field list in this plan with a fixture checker in the test. Do not invent extra fields. |
| 008-2 | No output has envelope `id`, `sync_id`, `source`, `offset`, or `"type":"error"`. |
| 008-3 | A failed known command keeps `<noun>.<verb>.result`, has `error`, and has no `data`. |
| 008-4 | A warning is only in `warnings`. stderr is empty with `--json`, including on failure. |
| 008-5 | `warnings` is absent when there is no warning. |
| 008-6 | A list has `next_cursor` and `has_more`. A single-item response and a search response have neither. |
| 008-7 | Search has the 006 facets. `data.total` equals the count before the limit. |
| 008-8 | `mem_…` fails with `invalid_argument`. `--id`, `--local-id`, `--offset`, and `--json-errors` are unknown options. |
| 008-10 | A release build ignores `REMEMBER_TEST_FIXED_IDS`. |
| 008-13 | `get 42` and `get <uuid>` find the same memory. |
| 008-14 write half | After migration, the next memory write stores `change_seq` 1, or 2 when plan 19 set the counter to 1. `brain new-id` replaces `brain_id` only. |
| 008-16 | Purge of a deleted row removes it and writes one tombstone. The purge command's live-row error is plan 25. This test calls the store purge on a deleted row if the CLI purge command is not registered yet. |
| 008-17 | `link` changes `updated_at`, `updated_by`, and `change_seq` on both ends and does not change either memory vector. Unlink keeps the row with `deleted_at` set. |
| 008-18 | A synced change keeps the author's `updated_by`. |
| 008-20 | `search --created-by agent` returns only memories whose `created_by.kind` is `agent`. |
| trash | `--trash` lists Deleted rows and prints the locked warning once. `--expired` does not list those rows. Exit 3 uses the locked wrong-list lines. |
| no copy detect | Opening a copied brain file does not change `brain_id`. |

008-9 is HTTP and MCP. Do not fake a server. 008-12 is plan 22 plus plan 23. 008-15 is plan 25. 008-19 is plan 25.

## Stages

1. Write a red test that a `get --json` object has `format`, `version`, and `type` in that order and has `data.memory`. Then change `output.c`. Keep one writer.
2. Remove `--json-errors`, `--offset`, `--sync-id`, `--id`, and `--local-id`. Update the plan 16 and plan 18 tests that cited them. Point those tests at the stdout envelope.
3. Change the positional locator. Assert the C-01 plain line, message, and fix.
4. Delete `source` from memory JSON. Add `created_by` and `updated_by` on new writes. Replace `--source` with `--as` and add `--actor`. Apply the write table to add, update, delete, restore, link, unlink, rekey, and purge.
5. Change `--trash` and add the locked warning and the locked wrong-list lines. Delete `purge-trash`.
6. Add `remember brain new-id` with the locked JSON object.
7. Add `--color` and `--quiet` as globals. Use the C-02 help lines.
8. Emit `unknown_field` with the C-35 lines. Do not drop unknown fields silently in new parsers.

Each stage: one red test, then the change, then the gate, then review.

## Cloud contract

- Every route returns `remember-message` version 1. `id` replaces `sync_id`.
- `GET /v1/memories/{id}` returns `data.memory`. No `local_id`.
- `/v1/sync/pull` uses `id`.
- Search does not page. `total` is the chain length before the limit.
- List routes use cursor paging. No `offset`.
- A link is its own sync record (`entry_links.id`, `version_vector`, `deleted_at`). Concurrent add and unlink: later `updated_at`, then larger device id. A link change does not change either memory vector.
- A received change keeps the author's `updated_by`.
- `remember sync --strict` exits 4 when the server drops unknown fields.
- Cloud v1 does not offer end-to-end encryption. No opt-in line.
- Webhooks send the same `event` message plan 25 builds. This plan does not build that message.
- HTTP status per code is D-10. Do not assign one.

## How to work

Same loop as plan 19. Load `standard:coding` and the review keys. Docs: [`docs/STEP_REVIEW_CHECKLIST.md`](../docs/STEP_REVIEW_CHECKLIST.md), [`docs/engineering-notes.md`](../docs/engineering-notes.md), [`docs/QUALITY.md`](../docs/QUALITY.md).

Branch from clean `main` after plan 19. One branch. One pull request.

Do not invent `error.fix`, an `unknown_field` sentence, a page size, or a copy-detection rule. The approved lines are C-01, C-02, and C-35.

Quality gate: the plan 19 commands. Coverage 100/100.

## Definition of done

- [ ] Must-pass rows for this plan are green.
- [ ] `--json-errors`, `--offset`, `--sync-id`, `--source`, and `purge-trash` are gone from the CLI and from tests.
- [ ] Plan files 16, 17, 17a, 18, 18b, and 18c are unchanged.
- [ ] `--json` `fix` values are previously locked splits or the approved C-01 text.
- [ ] Open still does not implement JE-3.10.
- [ ] Gate green. Second review clear. INDEX ticked only then.

## Second review handover

Probes:

1. `remember --json get 1` on a migrated brain. [envelope order, `data.memory`, `local_id` present, no `sync_id`, no `source`]
2. `remember --json get nope` as a positional. [type `memory.get.result`, `error`, no `data`, stderr empty]
3. `remember --json-errors get 1`. [unknown option]
4. `remember get --sync-id <uuid>`. [unknown option]
5. `remember --trash list` on a brain with a deleted row and an expired row. [deleted row only, one warning, the locked sentence]
6. `remember --expired list`. [expired row only, no Trash warning]
7. `remember brain new-id` twice. [second `previous_brain_id` equals the first `brain_id`, memory vectors unchanged]
8. Link two memories and read both version vectors. [unchanged vectors, both `change_seq` increased]
9. Copy the brain file to a new path and open it. [`brain_id` unchanged]

Do not re-open: one format, no envelope id, no offset, actors replace source, `--trash` is Deleted.

## Implementation Notes

_(empty)_

## Review Notes

_(empty)_
