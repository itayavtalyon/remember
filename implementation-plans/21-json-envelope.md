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
9. Spec (`decision:2026-11-01-legacy-plans-spec-step`). Do this stage last, before `.claude/WHOLE_PLAN_REVIEW`. Merge `origin/main` first, so `specs/` exists. Write `specs/JSON-ENVELOPE.md`. Describe only the behavior this branch ships: the `remember-message` version 1 fields and their order, the reply types, errors and warnings in the envelope, locators and `id`, actors, the bins and `--trash`, cursor paging, `brain new-id`, `--color` and `--quiet`, and `unknown_field`. Each section names the `decision:json-*` keys it describes (`remember --json list --tag subject:json-envelope`). When no key exists, name the 008 JE section. `specs/JSON-STYLE.md` stays the value rules. Link the two specs to each other. When `JSON-STYLE.md` disagrees with what ships, change `JSON-STYLE.md`. Move the Message envelope row in `specs/README.md` from "Not written yet" to "Specs". Set `state:implemented` on each `subject:json-envelope` decision this plan built, per `workflow:decision-tags`. This stage has no red test. The whole-plan review checks the spec against the code.

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
- [ ] `specs/JSON-ENVELOPE.md` describes what this branch ships, `specs/JSON-STYLE.md` agrees with it, and `specs/README.md` lists it (stage 9).
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

### Stage 1 — the envelope writer (2026-10-30)

Branch `plan/21-json-envelope`, from `origin/main` 132f89c. The approved copy is PR #54 (docs only). Merge `main` into this branch after #54 merges (`decision:plan21-docs-pr-first`).

What changed:

- `src/output.c` has one head, `write_envelope_head(out, type)`, which writes `{"format":"remember-message","version":1,"type":<type>,"data":{`, and one tail, `ENVELOPE_END` (`}}\n`). Every `--json` success writer in `output.c` uses that pair. There is no second writer.
- `get` writes `data.memory` (the old `count` and `entries` are gone). `add` writes `data{action, memory{... links}}` and loads its links like `get` does: deleted neighbors are left out. `update` and `rekey` write `data{action, memory}` without links. `delete`, `purge`, and `conflict accept` share `output_entries_envelope`: `data{action, count, entries[]}`. `output_deleted_list`, `output_get_envelope`, and `output_accepted_envelope` are deleted.
- Reply types: JSON-STYLE §1.7a for the commands it names. For the five it does not name, `decision:plan21-derived-reply-types`: `device.get.result`, `device.new.result`, `memory.related.result`, `memory.import.result`, `repair.search_index.result`.
- `repair search-index --help` names the new JSON type.
- `tests/test_envelope.c` (suite `envelope`, in `tests/gate-suites`). `tests/test.c` gets `ASSERT_STR_PREFIX`.

Not in stage 1, on purpose:

- Errors and warnings still use the old `diag.c` lines. Stage 2 moves them into the stdout envelope with the `--json-errors` removal.
- The memory object still has `"id":<int>,"sync_id":…` and `source`. Stage 2 renames identity (`id`, `local_id`). Stage 4 replaces `source` with actors.
- List and search `data` still carry `offset` and `limit`. Stage 2 deletes `--offset` and adds the cursor.
- `import` still prints counts only. `decision:plan21-import-problem-rows` (conflicts and rejected groups) needs store work and is open.
- Search `query` (`decision:plan21-search-query-original-only`) and `filters` are not written yet.

Grill locks this stage: `decision:plan21-derived-reply-types`, `decision:plan21-import-problem-rows`, `decision:plan21-search-query-original-only`, `decision:plan21-add-action-names-kept`, `decision:plan21-docs-pr-first`.

Mutants (one clause each). The runner restores a file with `mv`, which keeps the old mtime, so the first run reused mutated objects. It now touches the file after the restore. Rerun results:

| Mutant | Red tests |
|---|---|
| M1 head writes `type` before `version` | `envelope_get_head_is_format_version_type_then_data_memory`, every `output_*_envelope` literal in `test_output.c` |
| M2 `get` passes an action | `envelope_get_head_…`, `get_existing_json_envelope` |
| M3 `output_memory_envelope` drops links | `output_memory_envelope_with_action_…`, `…_without_action_…`; by hand also `list_search_get_json_links_empty_after_expires`, `get_list_json_stubs_five_name_preview` (the runner's CLI capture missed those, so it was run by hand) |
| M4 `ENVELOPE_END` loses a brace | `list_equal_bounds_are_empty`, `search_crossed_bounds_are_empty`, `tags_empty_db_json`, the `test_output.c` literals |
| M5 `action` written only when NULL | `add_json_created_shape`, `add_json_merged_shape`, `update_json_shape`, `output_action_envelope_writes_the_memory_without_links` |
| M6 `add` keeps deleted neighbors | `envelope_add_leaves_out_a_neighbor_in_the_trash_like_get` (added after M6 survived) |

Found while updating tests: `tests/test_device.c` built its expected line in a 160-byte buffer, and a longer line was cut silently. The buffer is 256 bytes now, and the test asserts that `snprintf` fit.

Probe, the add-with-a-deleted-neighbor case, by hand:

```
remember --db D add --key hub hub; remember --db D add other; remember --db D link 1 2
remember --db D delete 2; remember --db D --json add --key hub hub2   # "links":[]
```

Gate on the committed tree (after the two clang-tidy fixes: a `NOLINTNEXTLINE(bugprone-easily-swappable-parameters)` on `write_memory_envelope`, whose `type, action` order matches every public writer, and an `if`/`else` in place of a ternary that LLVM 22 read as bool to int): `ctest` 5/5 passed. `check-coverage.sh`: functions 100%, effective lines 100%, `output.c` 708 branch sides with 0 missed. `lint-all.sh`: LINT OK. `leaks-macos`: PASS. `ci-linux.sh`: OK (IWYU PASS, scan-build PASS).

Simplify pass:

1. `output_deleted_list` and `output_accepted_envelope` were the same writer with a different action word. Both became `output_entries_envelope` (about 35 lines saved, and `delete` moves to it too).
2. `output_get_envelope` and the old one-memory envelope became `write_memory_envelope` behind two public names (about 20 lines saved).
3. Each writer's open and close is one head and one `ENVELOPE_END`, not a `{"version":1,` and `]}\n` pair per function.
4. Kept: `output_action_envelope` and `output_memory_envelope` stay two public functions. Merging them needs a `with_links` flag at every call site, and the flag is the difference the plan states (only `add` and `get` carry links).

### Stage 2a: errors and warnings in the envelope, `--json-errors` gone (2026-10-31)

Stage 2 is split into three parts, each with its own deep review: 2a errors and warnings, 2b `--offset` to `--cursor`, and 2c identity flags with the `id`/`local_id` rename.

What changed:

- The `output_*_envelope` writers are renamed `output_*_data`. They write only the data object, with no type and no newline. `remember_run` (`run_json`) writes the envelope (`decision:plan21-envelope-closed-by-app`). With `--json` it points `app_out()` at a memstream and runs the command. It then writes `{"format","version","type"`, then `data` or the run's first error, then `warnings` and `}\n`. A failure drops any data. The reply type has one table, `reply_noun_verb`, keyed by the command; `device new` is `device.new`. A parse error before the command is known gives `unknown.result`. `--help` and `--version` stay text.
- With `--json`, `diag.c` writes nothing. `diag_json_begin` opens two memstreams, and errors and warnings are buffered there. Only the first error is kept. An error is `{code, message, fix, retryable, detail?}`. A SQLite error's E4.8 fix moves from `detail.fix` to `error.fix`. stderr stays empty, except when stdout itself cannot be written: then the plain `failed to write output` line goes to stderr, the one place left.
- `retryable` is true for `busy`, `io_error`, and `out_of_memory` (`decision:plan21-retryable-codes`).
- `--json-errors` is removed (it is now an unknown option), with its help lines. The `database` fix says `--json` instead of `--json-errors`, in the plain line too.
- If `--json` cannot get memory for its buffers, a fixed out-of-memory envelope is written directly.

C-42, drafts for copy (`decision:plan21-error-fix-drafts-c42`). Copy approves or rewrites these before INDEX is ticked. Rows marked locked are not drafts.

| Code | `fix` |
|---|---|
| `usage` | `Run 'remember <command> --help' to see how to use it.` With no command: `Run 'remember --help' to see the commands.` Copy may ask for one fix per usage message. |
| `not_found` | `Run 'remember list' to see keys.` (JSON-STYLE example, locked) |
| `database` | `Try again. If it keeps happening, report the details from --json.` (E4.8 sentence; only `--json-errors` changed) |
| `out_of_memory` | `Close other apps to free memory, then try again.` |
| `internal` | `This is a bug in remember. Report it with the command you ran.` |
| `invalid_query` | `Check the search words, or put the query in double quotes.` (no producer today) |
| `key_conflict` | `Pick another key, or rekey the other memory first.` |
| `body_conflict` | `Change the text, or update the memory that already has it.` |
| `expired` | `Add --expired to use it.` (C-01 expired row, locked) |
| `not_expired` | `Run the command without --expired.` |
| `deleted` | `Add --deleted to use it.` Stage 5 changes it to the C-01 `Add --trash to use it.` |
| `not_deleted` | `Run 'remember delete' on it first.` (C-01 `wrong_bin` shape, without `<key>`) |
| `not_single_device` | `Delete it instead; it stays in the Trash and syncs as deleted.` |
| `self_link` | `Link two different memories.` |
| `cycle` | `Remove the link that closes the loop, then try again.` |
| `source_too_old` | `Open the source brain once with this remember, then import it.` |
| `unique_taken` | `Rekey or delete the memory that holds the key or text, then try again.` |
| `stale` | `Read the memory again, then retry the change.` |
| `newer_database` | `Upgrade remember, then try again.` |
| `unsupported_database` | `Open the brain with the remember that made it, and export it.` |
| `import_parse` | `Check the import source, then try again.` |
| `schema_too_new` | `Upgrade remember to make changes.` (C-01, locked) |
| SQLite codes | The E4.8 fix (locked). The `k_fix` rows for these codes cover a failure without a SQLite cause. |

Tests:

- The `--json-errors` tests now assert the stdout envelope with an empty stderr. `k_error_fixes` in `test_errors.c` is the literal table above.
- New: `json_keeps_only_the_first_error`, `diag_code_fix_is_the_codes_sentence`, `run_reports_a_failed_json_flush_on_stderr`, `json_help_and_version_print_text`, `envelope_every_command_names_its_reply_type`, `envelope_failure_keeps_its_type_and_carries_the_warnings_after_the_error`, `envelope_warnings_keep_their_order_with_a_comma_between`.

Tests also lock `remember --json help not-a-command` as one `unknown.result` envelope (`json_help_with_an_unknown_topic_is_an_envelope`). Help's command stays `help` when the topic is unknown, and the error is still an envelope.

Mutation (the 16 mutants in the other agent's `mutants2a.py`, run 2026-10-31). Three needles no longer matched the tree (count 0). The same clause was applied to the current line and run again. That retarget is named below. A mutant that does not build is not a survivor. A segfault that drops piped FAIL lines is not a survivor. One expected survivor: M5.

| Mutant | Result | Red tests |
|---|---|---|
| M1 retryable drops `out_of_memory` | Agent needle not applied. Retarget drops ` \|\| code == DIAG_OUT_OF_MEMORY`. Build ok. Killed. | `every_error_code_json_message_matches_the_plain_line` (errors, 1 of 109) |
| M2 a later error replaces the first | `if (g_json.has_error)` becomes `if (false)`. Build ok. Killed. | `json_keeps_only_the_first_error` |
| M3 comma before the first warning | `> 0U` becomes `>= 0U`. Build failed (`-Wtautological-unsigned-zero-compare`). Not a survivor. | none (did not build) |
| M4 usage fix ignores the command | `g_json.command == NULL` becomes `!= NULL`. Build ok. Killed. | `cli_busy_timeout_out_of_range_is_usage`, `every_error_code_json_message_matches_the_plain_line`, `golden_error_lines_and_d9_codes`, `json_error_escapes_message_bytes`, `json_keeps_only_the_first_error`, `json_usage_error_is_one_stdout_envelope` |
| M5 exit code stays 0 when an error was buffered | Deletes `rc = (rc == REMEMBER_OK && diag_json_has_error()) ? REMEMBER_ERR : rc`. Build ok. Store 0. CLI 343 tests, 0 failed. Expected survivor. | none. The only path with `rc == REMEMBER_OK` and a later buffered error is the data memstream `fflush`/`ferror` treated as out of memory. Tests cannot force that. The line stays. |
| M6 help with a parse error skips the envelope | Agent needle not applied. Retarget drops `args->error == CLI_ERR_OK &&`, so help and version skip the envelope even on error. First run, before the help-topic test: 343 tests, 0 failed (untested clause). After `json_help_with_an_unknown_topic_is_an_envelope`: errors 110 run, 1 failed. Killed. | `json_help_with_an_unknown_topic_is_an_envelope` |
| M7 `device new` named `device.get` | Agent ternary needle not applied. Retarget returns `"device.get"` from the mint branch. Build ok. Killed. | `envelope_every_command_names_its_reply_type` (envelope, 1 of 6) |
| M8 `purge-trash` has no type | Deletes the `CLI_CMD_PURGE_TRASH` case. Build failed (`-Wswitch-enum`). Not a survivor. | none (did not build) |
| M9 a failure writes `data` | `if (failed)` becomes `if (!failed)`. Build ok. First pass reported no red tests because `remember_tests` segfaulted (exit 139) and the pipe dropped the FAIL lines. Follow-up to a file: 43 red tests, then the same segfault. Not a survivor. | `add_tag_casefold_merges_to_one_tag_name`, `facade_busy_json_matches_cli`, `facade_json_not_found_matches_cli`, `facade_update_stale_matches_cli`, `fts_reflects_tag_update`, `fts_search_empty_after_delete`, `fts_search_reflects_body_update`, `fts_search_reflects_keyed_upsert`, `json_add_error_is_an_error_envelope`, `json_get_missing_is_an_error_envelope`, `keyless_merge_keeps_created_at`, `list_no_matches_json_total_zero`, `search_diacritics_folded`, `search_finds_compound_tag_component`, `union_search_deep_nesting_is_a_warning`, `union_search_example_2_has_facets_and_the_warning`, `union_search_example_4_filter_scopes_the_facets`, `union_search_facet_limit_accepts_both_bounds`, `union_search_facet_limit_defaults_to_50`, `union_search_facet_limit_out_of_range_is_usage`, `union_search_invalid_utf8_is_usage`, `union_search_json_carries_the_facets_before_entries`, `union_search_parsed_raw_part_has_no_warning`, `union_search_unparseable_raw_part_is_a_json_warning`, `update_bumps_updated_at`, `update_by_key_clear_tags`, `update_clear_tags_flag`, `update_if_version_vector_match_by_key_writes`, `update_if_version_vector_match_writes`, `update_if_version_vector_mismatch_is_stale`, `update_json_shape`, `update_moves_entry_to_top_of_list`, `update_positional_body_not_accepted`, `update_preserves_id_key_source_created_at`, `update_replace_tags_keeps_shared_tag_on_other_entry`, `update_replace_tags_multiple`, `update_same_text_still_succeeds_and_returns_entry`, `update_source_immutable`, `update_tags_only_keeps_body`, `update_text_and_tags_together`, `update_text_literal_dash_equals_form`, `update_text_only_keeps_tags`, `update_text_stdin_dash` |
| M10 an empty warnings array is written | Deletes the `nwarnings == 0` return. Build ok. CLI 343 run, 51 failed. Killed. | `cli_busy_timeout_500_reports_busy_within_half_a_second`, `cli_busy_timeout_out_of_range_is_usage`, `cli_damaged_fts_search_reports_fts_only`, `cli_db_under_a_file_keeps_the_legacy_open_line`, `cli_enospc_shim_reports_disk_full`, `cli_fail_read_shim_reports_io_error`, `cli_fail_write_shim_reports_io_error`, `cli_import_text_source_names_the_source`, `cli_mode_000_reports_cannot_open_permission`, `cli_no_create_missing_path_creates_nothing`, `cli_nomem_shim_reports_database`, `cli_read_only_file_reports_read_only_on_write`, `cli_readonly_directory_reports_read_only`, `cli_relative_db_emits_absolute_detail_path`, `cli_repair_counts_deleted_entries`, `cli_repair_heals_page_damaged_fts_content`, `cli_repair_heals_page_damaged_fts_data`, `cli_repair_read_fault_during_check_is_corrupt`, `cli_repair_refuses_damaged_base_table`, `cli_repair_refuses_truncated_brain`, `cli_repair_search_index_rebuilds`, `cli_search_on_damaged_base_is_not_fts_only`, `cli_text_file_reports_not_a_database`, `cli_truncated_brain_reports_corrupt`, `device_new_keeps_the_old_vector_and_counts_the_next_edit_under_the_new_id`, `device_prints_the_sidecar_id`, `envelope_add_leaves_out_a_neighbor_in_the_trash_like_get`, `error_outside_the_catalog_writes_nothing`, `every_error_code_json_message_matches_the_plain_line`, `golden_error_lines_and_d9_codes`, `json_error_escapes_message_bytes`, `json_error_is_one_line_and_not_the_plain_line`, `json_keeps_only_the_first_error`, `json_not_found_is_one_stdout_envelope`, `json_usage_error_is_one_stdout_envelope`, `list_equal_bounds_are_empty`, `list_rejects_a_bad_timestamp`, `list_rejects_a_newline_timestamp_as_one_json_line`, `list_sort_rejects_a_bad_value`, `null_message_is_empty_text`, `search_crossed_bounds_are_empty`, `sqlite_busy_reports_cause_fix_and_detail`, `sqlite_cannot_open_carries_errno_reason_and_absolute_path`, `sqlite_corrupt_marks_fts_only_and_keeps_base_text`, `sqlite_database_keeps_its_own_text_without_a_path`, `sqlite_emitter_rejects_misuse_quietly`, `sqlite_out_of_range_reason_falls_back_to_other`, `union_search_facet_limit_out_of_range_is_usage`, `union_search_invalid_utf8_is_usage`, `update_if_version_vector_mismatch_is_stale`, `warning_outside_the_catalog_writes_nothing` |
| M11 a SQLite error replaces the first | Deletes the `has_error` return in `diag_error_sqlite`. Build ok. Killed. | `json_keeps_only_the_first_error` |
| M12 a SQLite error takes `fix` from `k_fix` | `write_fix_retryable(err, fix, code)` becomes `k_fix[code]`. Build ok. Errors 109 run, 14 failed. Killed. | `cli_busy_timeout_500_reports_busy_within_half_a_second`, `cli_damaged_fts_search_reports_fts_only`, `cli_import_text_source_names_the_source`, `cli_read_only_file_reports_read_only_on_write`, `cli_readonly_directory_reports_read_only`, `cli_repair_read_fault_during_check_is_corrupt`, `cli_repair_refuses_damaged_base_table`, `cli_repair_refuses_truncated_brain`, `cli_search_on_damaged_base_is_not_fts_only`, `cli_text_file_reports_not_a_database`, `cli_truncated_brain_reports_corrupt`, `golden_error_lines_and_d9_codes`, `sqlite_busy_reports_cause_fix_and_detail`, `sqlite_corrupt_marks_fts_only_and_keeps_base_text` |
| M13 stdout gone stays in JSON mode | Deletes `diag_json_end()` before the failed-to-write `err_msg`. Build ok. Killed. | `run_reports_a_failed_json_flush_on_stderr` |
| M14 no internal error when a command fails silently | The guard becomes `if (false)`. Build failed (`-Wunreachable-code`). Not a survivor. The compiler guards the clause. | none (did not build) |
| M15 help and version are wrapped | Drops `CLI_CMD_VERSION` from the text skip. Build ok. Killed. | `json_help_and_version_print_text` |
| M16 a JSON error drops `detail` | Deletes the `write_json_detail_fields` call. Build failed (`-Wunused-function`). Not a survivor. | none (did not build) |

Gate on this tree, after the help-topic test (Apple clang, Debug, sanitizers on, LSAN off, `REMEMBER_TEST_VFS=ON`, Unix Makefiles; coverage with Homebrew LLVM 22). Started 2026-10-31T21:05:09Z at HEAD `0acd7bd` with stage 2a uncommitted. `EXIT: ctest=0 coverage=0 lint=0` at 2026-11-01T00:33:35Z. ctest 5/5. Coverage: functions 100%, effective lines 100% (must-cover lines missed: 0), branch sides 0 missed on `output.c` (698), `search_terms.c` (176), `unicode.c` (80), `normalize.c` (384). Lint OK. `default.profraw` stays untracked.

### Stage 2b: `--offset` becomes an opaque `--cursor` (2026-11-01)

Itay (2026-11-01): the cursor is standard base64 (with padding) of one canonical JSON object. `--limit` stays a flag. Any other flag that changes the SELECT or the WHERE, including `--sort`, is a different query. The resume key is the last row's sort-column timestamp plus `local_id`. The UUID (`entries.id`) is not in the ORDER BY, so a UUID-only cursor cannot break a timestamp tie, and looking the row up fails if that row was deleted.

What changed:

- Only `remember list` pages. `next_cursor` and `has_more` sit on the envelope, after `data` or `error` and before `warnings`. The last page has `"next_cursor":null` and `"has_more":false`. A failure drops data and omits the paging fields. Search has neither field and no `--offset`. `--offset` on list is `unknown option '--offset'`.
- List data is `{count, entries, total}`. `total` is the unpaged filter count. The page is `LIMIT limit+1` with the extra row dropped, so `has_more` does not need a second count. The keyset is on the SELECT only: `(e.{updated_at|created_at} < ? OR (same column = ? AND e.local_id < ?))`.
- The cursor JSON, opaque to clients, is `{"v":1,"sort":"updated"|"created","bin":"live"|"expired"|"deleted", optional tags/source/key/since/until, "at","local_id"}`. Absent optionals are omitted. Tags are sorted with `strcmp`. A different `--limit` is the same query. Default sort and `--sort updated` match. `--trash` and `--expired` match until stage 5. `--source` compares the flag the user passed. The cursor field is `local_id` so stage 2c does not change the bytes. `Entry.id` is still the integer local id until 2c.
- A bad cursor, or a cursor reused with a different query, is `invalid_argument`. Missing or duplicate `--cursor` stays usage. Search `--cursor` is an unknown option. Plain list still pages and does not print the cursor.

Draft, not approved copy. Same status as C-42. Copy approves or rewrites this before INDEX is ticked.

| Code | message | fix |
|---|---|---|
| `invalid_argument` | `invalid --cursor` | `Run 'remember list' again without --cursor.` |

`retryable` stays false. `k_fix` says the sentence is a draft.

Tests: `list_json_cursor_pages`, `list_json_cursor_filters_and_rejects`, `list_cursor_past_end_keeps_total`, `list_offset_is_an_unknown_option`. The reject table covers non-canonical JSON, non-canonical base64 (including non-zero padding bits), tag count and tag text mismatches, and a 51-tag cursor.

Gate (Apple clang, Debug, sanitizers on, LSAN off, `REMEMBER_TEST_VFS=ON`, Unix Makefiles; coverage with Homebrew LLVM 22). Started 2026-11-01T17:03:21Z at HEAD `42f6793` with stage 2b uncommitted. ctest 5/5. Coverage: functions 100%, effective lines 100% (must-cover lines missed: 0), branch sides 0 missed on `output.c` (698), `search_terms.c` (176), `unicode.c` (80), `normalize.c` (384). The first lint failed cppcheck `variableScope` / `unreadVariable` on two locals in the 51-tag fixture. They moved inside the `if`. Lint re-run 2026-11-01T17:12:24Z: `LINT OK`, `EXIT:0`. `default.profraw` stays untracked.

## Review Notes

Stage flow (`decision:plan21-stage-flow`, Itay 2026-10-31): after each stage, run the in-session deep review, fix, and continue. There is no per-stage `REVIEW_REQUEST`. One whole-plan review runs at the end.

### Stage 1 deep review (2026-10-31, in session)

Probes run: every `--json` success command on one brain (add, get, list, search, update, link, related, unlink, tags, delete, purge, device, device new, conflicts, repair search-index) parses as JSON, starts `format`, `version`, `type`, has `data` and no `error`, exits 0, and leaves stderr empty. `import --from-db` takes v4 and v5 sources only, so its envelope is proved in `test_output.c`, `test_sync_import.c`, and `test_facade.c`, not in this probe.

Findings:

- **Important, fixed (grill):** `add --json` reads the row's links after the write commits. A `REMEMBER_TEST_FAIL_READ` sweep shows N=17 to 20 save the memory and exit 1 with `SQLITE_IOERR_READ`. Itay chose fail loud (`decision:plan21-add-link-read-fails-loud`). Test `envelope_add_link_read_error_after_the_write_exits_1_and_keeps_the_write`. Mutant: skip the neighbor-status check; that test goes red.
- **Important, fixed:** `docs/engineering-notes.md` (Output, Repair, `tags`) and `skills/remember/SKILL.md` (add, list, search, device, entry placement) still described `{"version":1,...}`. Both are corrected. The notes name the consumer impact: remember-mac and scripts must read `data`.
- **Nit, kept:** reply types go into the head through `%s` unescaped. Every caller passes a string literal. No stored or user value reaches it.

### Stage 2a deep review (2026-10-31, in session)

`decision:plan21-stage-flow`: in-session deep review, fix, continue. No `.claude/REVIEW_REQUEST`. Local Bugbot is not installed (`command -v bugbot` is empty, and there is no bugbot skill). `quality:linters-max` allows a local `NOLINT`. This repo's engineering notes, and the `AppStreams` comment, pack the two `FILE*` into a struct so the ends cannot be swapped. The struct is what shipped. `standard:coding` says merge `main` at the start of a stage. This session's prompt says merge `origin/main` after the 2a commit and not during the gate. The merge waits until after this commit.

No product or architecture question is open. The help-topic envelope was a specified contract with no test. The test is the fix.

Findings:

- **Important, fixed:** `writes_envelope` skipped the envelope for every help or version command only when `error == CLI_ERR_OK`. `remember --json help not-a-command` keeps command `help` and sets `CLI_ERR_UNKNOWN_COMMAND`, because `cli_parse` returns before `apply_help_request`. M6's first retarget survived 343 tests. `json_help_with_an_unknown_topic_is_an_envelope` locks the probed bytes: type `unknown.result`, code `usage`, message `unknown command 'not-a-command'`, fix `Run 'remember help --help' to see how to use it.`, `retryable` false, stderr empty, exit 1. The same retarget then fails that one test.
- **Nit, fixed:** `tests/COVERAGE.md` still named `--json-errors`. `docs/engineering-notes.md` still named `output_tags_envelope` and `output_search_envelope`. Those names are now `remember_run`, `output_tags_data`, and `output_search_data`.
- **Nit, kept:** the first-error gate is in `json_slot` and again in `diag_error_sqlite` (about 4 lines). A SQLite notice is not a `Notice`. Folding them adds a bool and a NULL check and saves nothing. `CliGlobals` keeps `pad_[1]` so `-Wpadded` stays clean. `usage_fix` uses a 96-byte stack buffer. The longest catalog command name (`purge-trash`) makes a sentence of about 56 bytes.
- **Kept, not a defect:** M5. Deleting the exit-code line would hide an out-of-memory failure of the data buffer. There is no fault hook, and this review does not add one.

DRY / easy to replace / orthogonal / simplify:

- One reply-type table (`reply_noun_verb`). One closer (`run_json`). `diag.c` buffers notices and does not write the envelope. Plan 20's `diag_error_fix` path (fix, no `retryable`) stays a different path. `k_fix` covers a failure with no SQLite cause. The E4.8 sentence stays on the SQLite path.
- C-42 rows in `k_fix` and `k_error_fixes` are the plan's drafts. The `deleted` fix is still `Add --deleted to use it.` Stage 5 changes it.

### Stage 2b deep review (2026-11-01, in session)

`decision:plan21-stage-flow`: in-session deep review, fix, continue. No `.claude/REVIEW_REQUEST`. Local Bugbot is not installed. No product question is open. The cursor bytes and the resume key are Itay's 2026-11-01 answer. The `invalid_argument` sentence stays a draft, recorded the way C-42 was recorded.

Findings:

- **Nit, fixed:** cppcheck wanted `used` and `wrote` inside the 51-tag `if`. The initializers were unused. They now live in that block. `get_list_delete` stayed 25 tests, 0 failed, 465 asserts. Lint was re-run and is `LINT OK`.
- **Nit, fixed:** the list SQL comment still said `LIMIT/OFFSET`. It now says keyset + LIMIT.
- **Nit, kept:** the test file has its own base64 encoder so the reject table can build cursors. The production codec stays `static` in `cmd_query.c`, so CMake does not gain a source. Exporting it would be a second public surface for an opaque token.
- **Nit, kept:** base64 shifts and alphabet offsets are silenced with `NOLINT`, the same way UUID v7 packing is. Naming every shift does not make the layout easier to read. `json_escape`, `append_decoded`, and `write_cursor_tags` are the splits that keep the three hot functions under the complexity limit of 25. OOM returns stay literal `return -1` so an untested malloc miss stays defensive.

DRY / easy to replace / orthogonal / simplify:

- One closer still writes the envelope. The list command only sets a page slot. A failure clears the slot, so paging fields never ride on an error.
- The keyset is one predicate on the SELECT. COUNT stays the full filter, so an empty page still reports `total`.
- `--limit` is not in the cursor. Sort, bin, tags, source, key, since, and until are. Clients do not parse the cursor.
