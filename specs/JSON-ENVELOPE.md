# Message envelope

Status: the CLI `--json` object this binary writes.

Value rules (key casing, timestamps, null, enums, limits) stay in [JSON-STYLE.md](JSON-STYLE.md). This file is the envelope, the reply types, and the payloads. Where a row there describes this CLI, it matches this file.

Decisions are `decision:…` entries tagged `subject:json-envelope`. A section with no such key names the 008 JE section, or the later log section when 008 has none.

## Envelope

`decision:json-envelope-no-id`, `decision:json-data-wrapper`, `decision:s-07`, `decision:plan21-envelope-closed-by-app`. 008 JE-2.

One message format: `remember-message`, version 1. The CLI writes one JSON object and a newline on stdout.

Key order:

`format`, `version`, `type`, `data` or `error`, `next_cursor` and `has_more` when a list page is set, `warnings` when any warning was recorded.

| Field | Rule |
|---|---|
| `format` | `"remember-message"` |
| `version` | `1` |
| `type` | `<noun>.<verb>.result` (§ Reply types) |
| `data` | The payload on success. A JSON object. |
| `error` | The failure object (§ Errors and warnings). |
| `next_cursor` | Opaque string, or JSON `null` on the last page. List only (§ Cursor paging). |
| `has_more` | Boolean. Present exactly when `next_cursor` is present. |
| `warnings` | Array of warning objects. Omitted when empty. |

Absent optional fields are omitted. `next_cursor` on the last page is `null`, and `has_more` is `false`.

A success object has `data`. A failure object has `error`. The two do not appear together. `warnings` may appear on either object. The envelope has no `id` and no `request_id`. Domain ids live in `data`.

`help` and `version` print text. `--json` does not wrap them.

The command writes the payload. `remember_run` closes the envelope. With `--json`, stdout is that object and stderr is empty. A failure drops `next_cursor` and `has_more`.

```json
{"format":"remember-message","version":1,"type":"memory.get.result","data":{"memory":{}}}
```

```json
{"format":"remember-message","version":1,"type":"memory.get.result","error":{"code":"not_found","message":"No memory has the key \"nope\".","fix":"Run 'remember list' to see keys.","retryable":false}}
```

## Reply types

`decision:plan21-derived-reply-types`, `decision:json-failed-reply-type`. 008 JE-2.3–2.4.

A reply type is the request type plus `.result`. A failed reply keeps that type and fills `error`. When the CLI cannot tell the command, the type is `unknown.result`.

| Command | `type` |
|---|---|
| `add` | `memory.add.result` |
| `get` | `memory.get.result` |
| `list` | `memory.list.result` |
| `search` | `memory.search.result` |
| `update` | `memory.update.result` |
| `delete` | `memory.delete.result` |
| `purge` | `memory.purge.result` |
| `rekey` | `memory.rekey.result` |
| `link` | `memory.link.result` |
| `unlink` | `memory.unlink.result` |
| `related` | `memory.related.result` |
| `import` | `memory.import.result` |
| `tags` | `tag.list.result` |
| `conflicts` | `conflict.list.result` |
| `conflict accept` | `conflict.accept.result` |
| `device` | `device.get.result` |
| `device new` | `device.new.result` |
| `repair search-index` | `repair.search_index.result` |
| `brain new-id` | `brain.new_id.result` |
| unknown command | `unknown.result` |

### Payloads

A memory object, in order: `id`, `local_id`, `key`, `body`, `tags`, `created_by`, `updated_by`, `created_at`, `updated_at`, `expires_at`, `deleted_at`, `bin`, `version_vector`. `key`, `expires_at`, and `deleted_at` are JSON `null` when unset. `id` is the UUID stored as `sync_id` in C. The JSON field is `id`. There is no `sync_id` field and no `source` field.

`links`, when present, follows `version_vector`. Each stub, in order: `id`, `local_id`, `key`, `type`, `bin`, `preview`. `type` is `related`, `cites`, `cited_by`, `supersedes`, or `superseded_by`. `preview` is the body trimmed to 40 code points. `links` is `[]` when the memory has none in the reply.

`get`, `add`, `list`, and `search` include `links`. Deleted neighbors are left out unless the selected bin is Deleted. `update` and `rekey` return `data.memory` with no `links` member. `delete`, `purge`, and `conflict accept` return `entries[]` of memory objects with no `links` member.

| Command | `data` |
|---|---|
| `add` | `{action, memory}`. `action` is `created`, `merged`, or `updated` (`decision:plan21-add-action-names-kept`). `memory` includes `links`. The memory is saved before those links are read. A failure on that read exits 1 and the envelope is an error with no `data` (`decision:plan21-add-link-read-fails-loud`). |
| `get` | `{memory}` with `links`. |
| `update`, `rekey` | `{action, memory}`. `action` is `updated`. No `links`. |
| `delete`, `purge` | `{action, count, entries}`. `action` is `deleted`. |
| `link` | `{action, count, links}`. `action` is `created` or `merged`. `links` holds stubs. |
| `unlink` | `{action, count, links}`. `action` is `deleted`. |
| `related` | `{id, local_id, key, count, links}`. |
| `list` | `{count, total, entries}`. `count` is the number of entries in this reply. `total` is the unpaged count. Paging fields sit on the envelope (§ Cursor paging). |
| `search` | `{count, total, keys, tags, entries}`. `count` is the number of entries in this reply. `total` is the unpaged match count. `keys` is `{total, items}` and each item is `{key, id, label, updated_at}`. `tags` is `{total, items}` and each item is `{tag, count}`. `keys.total` and `tags.total` count every facet match. `items` holds at most the facet limit (default 50, maximum 200). Entries include `links`. The object has no `query`, `ranked_by`, or `filters`. The envelope has no paging fields. |
| `tags` | `{count, tags}`. Each tag is `{name, count}`. No paging fields. |
| `conflicts` | `{count, conflicts}`. Each row is `{id, memory_id, reason, local, incoming, created_at}`. `id` is the integer conflict-row id. `memory_id` is the memory UUID. `local` and `incoming` are JSON objects. No paging fields. |
| `conflict accept` | `{action, count, entries}`. `action` is `accepted`. The command takes `--id` (that conflict-row id) and `--keep` `local`, `incoming`, or `both`. |
| `import` | `{action, inserted, updated, unchanged, conflicts, rejected}`. `action` is `imported`. The four counts after `action`, and `conflicts` and `rejected`, are numbers. |
| `device` | `{device_id}`. |
| `device new` | `{device_id, previous}`. `previous` is the replaced id, or JSON `null`. |
| `repair search-index` | `{action, entries}`. `action` is `rebuilt`. `entries` is the number of entries rebuilt. |
| `brain new-id` | `{brain_id, previous_brain_id}`. § Brain id. |

`list` is the only command that sets envelope paging fields.

## Errors and warnings

`decision:json-errors-in-envelope`, `decision:json-warnings-stdout`, `decision:json-optional-warnings`, `decision:plan21-retryable-codes`, `decision:plan21-error-fix-drafts-c42`. 008 JE-2.6–2.8.

An error object, in order: `code`, `message`, `fix`, `retryable`, then `detail` when the error has detail fields. `fix` is a string. `retryable` is `true` for `busy`, `io_error`, and `out_of_memory`, and `false` for every other code. The CLI does not write `docs`.

The first error is the one in the envelope. A later error in the same run is dropped.

A warning object, in order: `code`, `message`, then `detail` when present. Warnings have no `fix`. `warnings` is omitted when empty.

With `--json`, the envelope on stdout holds the error and the warnings, and stderr is empty. The CLI has no `--json-errors` flag.

Exit codes:

| Exit | Meaning |
|---|---|
| 0 | Success, including an empty list or search. |
| 1 | Usage or error. |
| 2 | Not found. |
| 3 | Wrong bin (`expired`, `not_expired`, `deleted`, `not_deleted`). |
| 4 | A warning under `--strict`, or under `REMEMBER_STRICT=1` exactly. Any other value of that variable leaves strict off. |

Exit 4 applies to every warning code, including when the command also failed. The JSON object is the same. Only the exit changes.

An envelope that contains `error` exits non-zero.

Locked lines this binary prints (C-01, and the Trash warning):

| Case | `message` | `fix` or warning |
|---|---|---|
| positional locator (`invalid_argument`) | `"<value>" isn't a memory id.` | `Use a number from 'remember list', a UUID, or --key KEY.` |
| Trash, wrong list (`deleted`) | `"<key>" is in the Trash.` | `Add --trash to use it.` |
| expired, wrong list (`expired`) | `"<key>" has expired.` | `Add --expired to use it.` |
| `--trash` (`flag_meaning_changed`) | `--trash now means the Trash (deleted memories). For expired memories, use --expired.` | warning, no `fix`. `detail` is `{"flag":"--trash","use":"--expired"}`. |

`<key>` is the memory key, or the UUID when the row has no key. The plain line is `remember: ` plus the message, a space, and the fix. A warning plain line is `remember: warning: ` plus the message.

`not_found` fix is `Run 'remember list' to see keys.` `schema_too_new` fix is `Upgrade remember to make changes.` `not_expired` fix is `Run the command without --expired.` `not_deleted` fix is `Run 'remember delete' on it first.`

A usage error's fix is `Run 'remember <command> --help' to see how to use it.` when a command is known, and `Run 'remember --help' to see the commands.` when it is not.

Every other `error.fix` string is a C-42 draft (`decision:plan21-error-fix-drafts-c42`). Those drafts are what the binary prints. They are not approved copy. The shared `invalid_argument` draft is `Run 'remember list' again without --cursor.` The locator row above uses its own fix, not that draft.

## Locators and id

`decision:json-id-format`, `decision:json-id-rename`, `decision:json-local-id`, `decision:s-13`, `decision:positional-locator`. 008 JE-3.

A public id is a canonical UUID v7: 36 characters, lowercase hex, hyphens at indexes 8, 13, 18, and 23, version nibble `7`, variant nibble `8`, `9`, `a`, or `b`.

A memory is found by the positional argument or by `--key KEY`.

- A canonical UUID matches `id`.
- A string of digits matches `local_id`. The value is the integer, so leading zeros are the number. A value below 1 does not match.
- Any other positional is `invalid_argument` with the C-01 message and fix in the table above.

There is no memory-locator `--id`, `--local-id`, or `--sync-id`. `conflict accept --id` is the conflict-row id in § Payloads. This binary does not read `REMEMBER_SYNC_ID` or `REMEMBER_ID`, and it does not write a `sync_id` field.

`local_id` is the integer row id. It appears in CLI memory objects and link stubs.

## Actors

008 JE-4.4 and JE-4.5. No `decision:json-*` key covers this CLI's actor object. `decision:provenance-actors` still names a `sync` kind this binary does not store.

An actor is `{"kind":"<kind>","id":null}` or `{"kind":"<kind>","id":"<id>"}`, `kind` then `id`.

Stored kinds: `human`, `agent`, `tool`, `share`, `import`.

`--as` accepts `human`, `agent`, `tool`, or `share`. The default kind is `human`. `--as` does not accept `import`. `--actor` is optional. An omitted id is JSON `null`. A present id is 1 to 64 characters from `A-Za-z0-9_./:-`.

A bad `--as` is usage: `invalid --as (use human, agent, tool, or share)`. A bad `--actor` is usage: `invalid --actor (1 to 64 of A-Za-z0-9_./:-)`.

`--created-by` and `--updated-by` filter on the stored kind, including `import`.

When `REMEMBER_HOOK_DEPTH` is a whole integer of 1 or more, passing `--as` or `--actor` fails with `invalid_argument`. The approved sentence for that case is unwritten, so this spec does not quote one.

Memory JSON has `created_by` and `updated_by`. It has no `source` field.

## Bins and `--trash`

No `decision:json-*` key. 008 has no JE section for this flag. The rule is 010 H-16.9a.

`bin` is `live`, `expired`, or `deleted`.

`--trash` selects the Deleted bin and emits `flag_meaning_changed` once, with the warning in the table above. `--expired` selects the Expired bin and does not emit that warning. `--deleted` selects the Deleted bin and does not emit that warning.

`--expired` together with `--trash` is usage `cannot combine --expired and --trash`. `--expired` together with `--deleted` is usage `cannot combine --expired and --deleted`.

`delete` rejects `--trash` and `--deleted` after bin resolution. `delete --trash` therefore emits the warning and then usage `unknown option '--trash'`. `delete --deleted` is usage `unknown option '--deleted'` and does not warn.

An unflagged `delete` soft-deletes a live row. `delete --expired` hard-deletes that one expired row. `purge` requires `--expired` or `--deleted`. `purge --trash` selects Deleted, warns, and purges that bin. There is no `purge-trash` command.

A live command aimed at an expired row exits 3 with the expired wrong-list line. A live command aimed at a deleted row exits 3 with the Trash wrong-list line.

## Cursor paging

`decision:json-paging-envelope`, `decision:s-08`. 008 JE-2.10–2.12.

`list` is the only paging command. The default `--limit` is 20. The maximum is 1000. `--limit` must be 1..1000.

`next_cursor` and `has_more` sit on the envelope, together. `count`, `total`, and `entries` stay in `data`. Search, tags, conflicts, and every write omit both paging fields. The CLI has no `--offset`. Search rejects `--cursor` as an unknown option.

The cursor is standard base64, with `+`, `/`, and `=` padding, of one JSON object. Field order:

`v`, `sort`, `bin`, then any of `tags`, `created_by`, `updated_by`, `key`, `since`, `until` that are set, then `at`, `local_id`.

| Field | Rule |
|---|---|
| `v` | `1` |
| `sort` | `updated` or `created` |
| `bin` | `live`, `expired`, or `deleted` |
| `tags` | Omitted when the list has no tag filter. Otherwise a non-empty array of strings in `strcmp` order. An empty array is invalid. |
| `created_by`, `updated_by`, `key`, `since`, `until` | Omitted when that filter was not set. |
| `at` | The sort timestamp of the last row on the page. |
| `local_id` | The integer id of that row. No leading zero on a multi-digit number. |

`--limit` is not in the cursor and may differ on the next call. The cursor matches the flags of the call that presents it: the same sort, bin, tags, and the same optional filters. A bad cursor, an unknown member, or a mismatch is `invalid_argument`, message `invalid --cursor`. That path is not `unknown_field`. The `fix` on that error is the shared `invalid_argument` draft in § Errors and warnings.

`decision:plan21-cursor-binds-whole-query` is the subject key for this cursor. Its stored text says base64url and that `--cursor` alone continues the query. This binary writes standard base64, and the flags travel with the cursor.

## Brain id

008 JE-3.9. No `decision:json-*` key. `brain new-id` replaces `brain_meta.brain_id` with a new lowercase UUID v7. The reply is `{brain_id, previous_brain_id}`. The command does not change `change_seq`, a version vector, or a memory.

Opening a database leaves `brain_id` unchanged. 008 JE-3.10 (copied-brain detection) is not in this binary.

## `--color` and `--quiet`

No `decision:json-*` key. 008 has no JE section for these flags. The rule is 010 H-16.10. The help lines are C-02:

- `--color     Use color: auto, always, or never. auto uses color only on a terminal, when NO_COLOR is unset and TERM isn't dumb.`
- `--quiet     Turn off progress lines on stderr.`

Both are global options, before or after the command. `--color` takes `auto`, `always`, or `never`.

`never` is off. `always` is on. `auto` is on only when the stream is a terminal, `NO_COLOR` is unset (an empty value counts as set), and `TERM` is not exactly `dumb`. An unset `TERM`, an empty `TERM`, and `Dumb` are not `dumb`.

`--quiet` and `--json` suppress progress lines. A progress line goes to stderr, only when stderr is a terminal, at most once per 1000 ms, with no spinner. A failed write does not fail the command and does not move that clock. No command in this binary writes a progress line.

## `unknown_field`

`decision:json-unknown-fields`. 008 JE-4.1.

The warning code is `unknown_field`. The writer records it once per file path, or once for stdin. JSON `detail` is `field`, then `where`. `where` is the path bytes, or `stdin`.

C-35, without the `remember: warning: ` prefix:

- File: `"<path>" has a field remember doesn't know, "<name>". The rest of the file was read.`
- Stdin: `stdin has a field remember doesn't know, "<name>". The rest of the input was read.`

No command in this binary reads a JSON file or JSON stdin, so no command calls this writer. A list-cursor member this binary does not know stays `invalid --cursor` (§ Cursor paging).
