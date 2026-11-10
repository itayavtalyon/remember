---
name: remember
description: >
  Use the local `remember` CLI (SQLite + FTS5 personal second brain). Load when
  storing or recalling durable facts, preferences, decisions, corrections,
  project memory, or project status/handoff; when the user says "remember that",
  "what did we decide", "search my memory", "where did we leave off",
  "project status", "deep review", "strict review", or "critical review",
  or when you need a stable key-value preference slot. Prefer this over
  chat-only notes for facts that must survive sessions. On deep review, get
  --key workflow:deep-review and follow it. On project pickup or pause, follow
  workflow:project-status (status:<slug> cards + links). Use with /remember or
  remember add|search|list|get|update|delete|tags|purge|import|conflicts|device|brain|link|unlink|related|rekey.
---

# remember — agent skill

Personal durable memory on the local machine. **Not** a chat log.

Binary: `remember` (install via `./scripts/install.sh`). Default DB: `~/.remember/remember.db`.
The device id is the sidecar `<db-path>.device_id` (never copy on import). Run `device new` after a restore or a copied brain. `device new` also repairs an invalid sidecar.

## When to write

- Durable facts, decisions, prefs, corrections useful in **future** sessions
- Project conventions the user wants recalled later
- **Project status / handoff** (`status:<slug>`) when starting, pausing, or blocking on the user — so a new chat can resume
- Prefer a stable **`--key`** for anything that will change over time

## When not to write

- Secrets, tokens, passwords, API keys, private keys
- Transient todos or one-off session chatter (unless it becomes the durable handoff in `status:<slug>`)
- Huge logs, full file dumps, or paste dumps better kept in the repo
- Architecture essays inside `status:<slug>` — those belong in `project:<slug>`

## When to search / list

- Before re-asking a preference or past decision
- Session start when tags are known (`list --tag …` or `search …`)
- User says **deep review** / strict review / critical review: `get --key workflow:deep-review` and the keys it lists, then execute that workflow
- Project pickup / "where did we leave off" / new conversation on a known repo: follow **Project status (handoff)** below
- Before `add` on a subject: search first; if an entry covers it, **`update`** that id (or re-`add --key`) instead of creating a rival
- Before `add --key`: read the search's `keys` facet. It lists the existing keys that match the query, so you reuse a slot instead of inventing a rival key

## Global flags

```text
remember [--db PATH] [--json] [--strict] [--color=auto|always|never] [--quiet] [--busy-timeout MS] [--no-create] <command> [options] [args]
```

| Flag | Meaning |
|------|---------|
| `--db PATH` | Overrides `REMEMBER_DB` and default `~/.remember/remember.db` |
| `--json` | One JSON envelope on stdout, also for errors and warnings (use this from agents) |
| `--color auto\|always\|never` | Use color: auto, always, or never. auto uses color only on a terminal, when NO_COLOR is unset and TERM isn't dumb. |
| `--quiet` | Turn off progress lines on stderr. |
| `--busy-timeout MS` | Milliseconds to wait for a lock (0..60000, default 5000) |
| `--no-create` | A missing brain file fails. Creates nothing |

**DB location:** must be a **local disk**. Paths under iCloud (`com~apple~CloudDocs`), Dropbox, or Google Drive get a stderr warning and risk corruption. Prefer default `~/.remember`.

## Exit codes

| Code | Meaning |
|------|---------|
| `0` | Success (including empty search/list; import with conflicts still exits 0) |
| `1` | Usage or error |
| `2` | Not found (`get` / `delete` / `update` / `rekey`) in either bin; `link`/`unlink`/`related` missing end |
| `3` | Wrong bin. `not_expired` / `not_deleted` stay those tokens. Trash: `remember: "<key>" is in the Trash. Add --trash to use it.` Expired: `remember: "<key>" has expired. Add --expired to use it.` `<key>` is the key, or the id when the row has no key. **Not** used by `link`/`unlink`/`related` (those resolve live+expired by default). |
| `4` | A warning under `--strict` or `REMEMBER_STRICT=1`. Exit 4 has no other meaning. |
| `5` | `confirmation_required`: run the command again with `--yes` (`settings reset --all` / `--namespace`). |

Never invent ids, keys, or `local_id`s. On exit `2`, the entry is missing — do not fabricate one. On exit `3`, the row exists in another bin.

## Errors

With `--json`, a failure is the same one envelope on stdout: the command's `type`, then `error{code, message, fix, retryable, detail?}` instead of `data`. stderr stays empty. Warnings are `warnings[]` (`{code, message, detail?}`) at the end of the envelope, on success or failure. Read `error.code`; `error.fix` is the next step; `retryable` true (`busy`, `io_error`, `out_of_memory`) means the same command may work later.

`busy` means another process holds the brain. Retry later. The tool does not retry.

`corrupt` with `detail.fts_only` true means the search index is damaged. Run `remember repair search-index`. Memories are not affected.

## Locators and bins

Exactly **one** of: a positional UUID or digits (`local_id`) | `--key KEY`.

JSON `id` is the memory's lowercase hyphenated UUID. Pass that UUID, a number from `remember list`, or `--key` to address a memory. `--id`, `--local-id`, and `--sync-id` are unknown options. There is no alias.

**Bins** (orthogonal flags; mutex):

| Flag | Meaning |
|------|---------|
| (default) | **live** only |
| `--expired` | expired memories: ones past their expiry time. They are not in the Trash. |
| `--deleted` | soft-deleted bin only (the same bin as `--trash`) |
| `--trash` | the Trash: memories you deleted. Not an alias of `--expired`. One warning this release (below). |

There is no include-all-bins flag. `--trash` and `--expired` cannot combine (`cannot combine --expired and --trash`). `--expired` and `--deleted` cannot combine. `--trash` and `--deleted` are the same bin.

For one release, a command that uses `--trash` prints one warning. Code `flag_meaning_changed`. Detail `{"flag":"--trash","use":"--expired"}`. Plain: `remember: warning: --trash now means the Trash (deleted memories). For expired memories, use --expired.` With `--json`, that item is in `warnings` and stderr is empty. Under `--strict` or `REMEMBER_STRICT=1`, any warning exits 4.

Help, on get, update, list, search, tags, and rekey:

```text
--trash     Use the Trash: memories you deleted. Restore them, or purge them for good.
--expired   Use expired memories: ones past their expiry time. They are not in the Trash.
```

JSON always has `"bin":"live"|"expired"|"deleted"` (never null, never omit). No `"trash"` field — stubs use `"bin"`.

## Commands (agent style: always `--json` when parsing)

### add

```bash
remember --json add --as agent --tag TOPIC "body text"
remember --json add --as agent --key pref:editor "helix"
remember --json add --as agent --key decision:db --tag project:foo "use SQLite FTS5"
# stdin body:
printf '%s' "long body" | remember --json add --as agent -
# literal body that is exactly a hyphen (not stdin):
remember --json add --as agent -- -
```

- Optional `--ttl 7d` (relative from **write** time; token `Nm`/`Nh`/`Nd`/`Nw`, no leading zeros) or `--expires YYYY-MM-DD` (local end-of-day → UTC) / `YYYY-MM-DDTHH:MM:SS[.frac]Z`. At most one. Past `--expires` is allowed (born expired).
- Optional `--key KEY` → keyed **upsert** (same key replaces body; tags **union**; same `id`)
- Keyless → body-hash merge (duplicate body merges tags). Hitting an **expired or soft-deleted** same key/hash **revives** it (same `id`).
- Body token `-` alone means **stdin**; after `--`, `-` is a one-character body
- `--as KIND`: `human` | `agent` | `tool` | `share` (default `human`); optional `--actor ID` (1 to 64 of `A-Za-z0-9_./:-`). **Agents pass `--as agent` on add.** `share` is used by the Mac app Share/Service/Intent surfaces — agents should not set it. There is no `--source`. Inside a hook (`REMEMBER_HOOK_DEPTH` ≥ 1) `--as` and `--actor` are refused.
- Every write also takes `--hooks` / `--no-hooks`; until hooks ship they change nothing.
- Human stdout: id only. JSON: type `memory.add.result`, `data` is `{"action":"created"|"merged"|"updated","memory":{…}}`; the memory includes `links`, like `get`.

### search / list / tags

```bash
remember --json search "editor preference"
remember --json search boo                      # word prefixes: finds "Book"
remember --json search 'book OR zed'            # FTS5 syntax still works
remember --json search --tag project:foo --limit 20 --facet-limit 50 "query"
remember --json list --tag pref --limit 20
remember --json list --sort created --since 2026-10-19
remember --json list --expired
remember --json tags
```

- search: one required QUERY. Word prefixes match in keys, bodies and tags (`boo` finds "Book", `edit` finds `pref:editor`), and the QUERY also runs as FTS5 syntax. A QUERY FTS5 cannot parse still exits 0 with the prefix matches and a warning (FTS notes below). An empty or blank QUERY exits 0 with an empty result. Put `--` before a QUERY that starts with `-` (`search -- -x`); without it, `-x` is an unknown option.
- Order: exact key, then key prefix, then relevance (bm25), then `updated_at`.
- Filters: `--tag` (AND, repeatable), `--key`, `--created-by KIND`, `--updated-by KIND`, `--limit` (default 20, max 1000), `--expired` / `--deleted` (default **live only**). Number options take ASCII digits only (`+5` or ` 5` is a usage error). `--offset` is an unknown option.
- JSON (list): type `memory.list.result`; `data` has `count`, `total`, `entries` (full bodies + `links` stubs; empty array if none). The envelope adds `next_cursor` and `has_more` after `data`. The last page has `"next_cursor":null` and `"has_more":false`. Pass `next_cursor` back as `--cursor` **alone** (only `--limit` may change): the cursor carries the whole query. A filter, sort, or bin flag next to `--cursor`, or a bad cursor, is `invalid_argument` `invalid --cursor`.
- JSON (search): type `memory.search.result`; `data` has `count`, `total`, `keys`, `tags`, `entries`. `keys` is `{"total":N,"items":[{"key","id","label","updated_at"}]}`, `tags` is `{"total":N,"items":[{"tag","count"}]}`: the keys and tags that match the QUERY among the memories in the active bin and filters (not only the matched rows). `id` is that memory's UUID. A key item has no `local_id`. `--facet-limit N` (default 50, 1..200) caps each `items`; `total` counts them all; `--limit` pages `entries` only. Search has no cursor. `label` is the memory's `# ` title, else the key. After `entries`: `query` (`{"original":{"text","tags","bins","time_range"}}`, what was asked), `"ranked_by":"bm25"`, `"filters":[]`.
- If `total` > rows received, pass list's `next_cursor` as `--cursor`, or raise `--limit`, before concluding nothing else exists
- list: same filters/paging; no FTS. `--sort updated` (default) orders by `updated_at DESC, id DESC`. `--sort created` orders by `created_at DESC, id DESC`.
- `--since TS` is inclusive and `--until TS` is exclusive. On `list` they filter the sort field. On `search` they filter `updated_at`. `search --sort` is validated, then ignored.
- TS is `YYYY-MM-DD` (local start of that day; a year before 1900 is `invalid timestamp`), `YYYY-MM-DDTHH:MM:SSZ`, or `YYYY-MM-DDTHH:MM:SS.mmmZ`. `since` at or after `until` is exit 0 with `count` 0 and `total` 0.
- tags: every in-use tag with count; optional `--expired`/`--deleted`

### get / delete / update / rekey (locator)

```bash
remember --json get 3
remember --json get --key pref:editor
remember --json get --deleted 3
remember --json delete 3                          # soft-delete (live or expired)
remember --json delete --expired 3                # hard wipe one expired row
remember --json update 3 --text "new body"
remember --json update --key pref:editor --if-version-vector '{"<device>":1}' --text "nvim"
remember --json update --trash 3 --undelete       # restore from the Trash
remember --json update --deleted 3 --undelete     # same bin as --trash
remember --json update --expired 3 --clear-expires
remember --json rekey --key decision:old --to-key decision:new
remember --json rekey 12 --clear-key
remember --json purge --expired                   # wipe whole expired bin
remember --json purge --deleted                   # wipe the Trash (the Deleted bin)
```

**Soft vs hard delete:**

- Unflagged `delete` = **soft** (sets `deleted_at`, clears `expires_at`, keeps body/tags/key/id/edges/FTS)
- `delete --expired` hard-wipes one row. `delete --deleted` does not exist. `delete --trash` does not wipe.
- `purge --expired` / `purge --deleted` = whole-bin hard wipe. `purge --trash` selects the Deleted bin and warns. `purge-trash` does not exist.

**Update rules:**

- At least one of `--text`, `--tag`, `--clear-tags`, `--ttl`, `--expires`, `--clear-expires`, or `--undelete`
- `--tag` + `--clear-tags` together → usage error
- Omit tag flags → tags unchanged
- Body only via `--text` (or `--text -` for stdin; `--text=-` for literal hyphen)
- `--undelete` restores a deleted row. Pass `--trash` or `--deleted`. Mutex with ttl/expires flags
- Never changes `created_by` or `key` (use `rekey` for keys)
- Read `version_vector` from the entry. Pass that object back with `--if-version-vector` (spacing does not matter). On code `stale` (`memory changed since it was read`), re-read and retry. A malformed vector is `usage` `invalid version vector: <vv>`. A wrong bin is still exit 3; the vector is checked only after the row is in the requested bin.
- Keyless body-hash collision → exit 1 + conflicting id on stderr
- JSON: `"action":"updated"` or `"action":"deleted"` with full entry snapshot. **update / delete / purge JSON have no `links` field.** add includes `links`, like get.

### import / conflicts / conflict accept

```bash
remember --json import --from-db /path/to/other.db
remember --json conflicts
remember --json conflict accept --id 1 --keep local|incoming|both
```

- Merges by **`id`** (the memory UUID). Source opened read-only; must be schema v4 or v5 (older or newer → exit 1).
- Does **not** copy the foreign `.device_id` sidecar.
- Exit **0** even when conflicts were recorded. The JSON reply lists them; plain output only counts them, so run `conflicts` after a plain import.
- Reasons: `concurrent_vv` | `key_clash` | `hash_clash`
- `--keep both` remints the incoming memory `id` on `concurrent_vv`; on clash keeps the incoming `id` (keyless if the slot is taken)
- JSON import: `action:imported`, `inserted`/`updated`/`unchanged` counts, then `conflicts` `{count, items}` (the conflict rows this import opened or refreshed, same objects as `conflicts`) and `rejected` `{count, items}` (each `{source_local_id, field}`, the first bad field)
- `rejected`: rows skipped because a field is not what a local write would store: a key or tag (a control byte, uppercase, too long), an `id` that is not a UUID v7, a timestamp not shaped `YYYY-MM-DDTHH:MM:SS.mmmZ`, a version vector that is not `{"<uuid v7>":<n>,...}`, a source outside the five tokens, or a body_hash that does not match the body. Nothing of that row is written.

### device

```bash
remember device
remember --json device new
```

- The device id is the sidecar `<db-path>.device_id` (one lowercase UUID, mode 0600). The database holds no second copy.
- `device` prints that id. `--json` is type `device.get.result` with `data` `{"device_id":"…"}`.
- `device new` mints a new id after a restore or a copied brain. It writes the sidecar and does not change version vectors. Plain output is the new id. `--json` adds `"previous"`.
- `device new` also repairs an invalid sidecar. No other command does.
- A missing sidecar is minted on open, so `"previous"` is that id. JSON `null` is only the type.

### brain

```bash
remember brain new-id
remember --json brain new-id
```

- `remember brain new-id` replaces `brain_meta.brain_id` with a new lowercase UUID v7 and prints that id. It does not change `change_seq`, a version vector, or a memory.
- `--json` type is `brain.new_id.result`. `data` is `{brain_id, previous_brain_id}`.
- The command name is `remember brain new-id`. There is no `remember brains`.
- Opening a copied brain file does not change `brain_id`.

### link / unlink / related

```bash
remember --json link --from 12 --to 64
remember --json link 12 64 --kind cites
remember --json unlink --from 12 --to 64
remember --json related 12
remember --json related --deleted --key gonek
```

**Tag vs link:** tag = facet/set (`#project:foo`). Link = intentional pair. Link when a future agent must traverse A to understand B. Do not link “same project” or “same session.”

**Stored `--kind` (input):** `related` (default on link) | `supersedes` | `cites`. Unknown tokens including `cited_by` / `superseded_by` → exit 1.

**Stub `type` (output, 5 names):** `related`, `cites`, `cited_by`, `supersedes`, `superseded_by`.

**Locators:** each end is exactly one of a UUID, digits (`local_id`), or `--*-key`. `--from-sync-id` and `--to-sync-id` are unknown options. Default resolves **live+expired**; `--deleted` includes deleted ends. No exit 3 on graph commands. Default related/stubs **omit deleted** neighbors; expired neighbors stay visible with `bin=expired`.

**rekey:** change a key in place (`--to-key` / `--clear-key`). **Never delete + re-add to rename** — that CASCADE-drops edges.

## Entry JSON shape (fields)

```json
{
  "id": "01234567-89ab-7cde-8f01-23456789abcd",
  "local_id": 1,
  "key": "pref:editor",
  "body": "helix",
  "tags": ["tools"],
  "created_by": {"kind": "agent", "id": null},
  "updated_by": {"kind": "agent", "id": null},
  "created_at": "2026-07-26T12:00:00.000Z",
  "updated_at": "2026-07-26T12:00:00.000Z",
  "expires_at": null,
  "deleted_at": null,
  "bin": "live",
  "version_vector": {"…device…": 1},
  "links": []
}
```

With `--json` the entry is `data.memory` (get, add, update, rekey) or one item of `data.entries` (list, search, delete, purge). Field order is fixed. `key` is JSON `null` when keyless. On **list / search / get / add** only, `"links"` follows `"version_vector"`. Stubs are `{id,local_id,key,type,bin,preview}` — `id` is the UUID, `local_id` the integer, preview ≤ 40 codepoints. Human list/search sixth column is neighbor **integers** only (`[expired]`/`[deleted]` suffix; never `[trash]`).

## Evolving facts — use `--key`

For anything that changes over time:

1. `add --key pref:editor "value"` (first write or overwrite)
2. Read/correct: `get --key` / `update --key --text …`
3. Rename/promote/demote the key: `rekey --to-key` / `--clear-key` (keeps `id`, `local_id`, and edges)
4. Soft-remove: `delete --key` (recover with `update --trash … --undelete`)

Keyed slots make duplicate rivals impossible. Keyless facts still resolve by recency (`updated_at`).

## Tag / key normalization

- ASCII casefold; trim edges; internal spaces OK; no tabs/newlines/controls inside tokens
- Use stable names: `pref:editor`, `decision:db-engine`, `project:remember`

## Human output (when not using `--json`)

- `add` / `update` / `rekey`: id (or JSON envelope with `--json`); human `delete` is silent
- `list` / `search`: `id | key | tags | preview | updated_at | related` (preview ≤ 80 codepoints; related ids cap 5)
- `search` first prints `keys: k1, k2` and `tags: t1 (n), t2 (m)` lines, each only when not empty, at most 10 items, then `, … and N more` (N = total − shown)
- `get` / `related`: `Related:` block after the body (`related` has no body)

Agents should always use `--json` when parsing.

## FTS notes

- Word prefixes match first; the QUERY also runs as SQLite FTS5 MATCH syntax (`remember help search`)
- The key is indexed: `search editor` finds `pref:editor`.
- Hebrew marks are folded out of the index. The stored body keeps them.
- A query FTS5 cannot parse is not an error: exit 0, prefix matches, and one stderr line `remember: warning: raw query ignored: <message>` (with `--json`: the item `{"code":"raw_query_ignored","message":"<message>"}` in `warnings`). A warning on stderr is not an error: trust the exit code
- Empty result set → exit 0 with `count:0` (not an error)

## Project status (handoff)

Lean **status cards** so you and a future agent know what is in flight, where work stopped, and what (if anything) is waiting on the human. Source of truth for the ritual: `workflow:project-status`.

### Keys and tags

| Slot | Purpose |
|------|---------|
| `status:<slug>` | Live handoff only (short, rewritten often) |
| `project:<slug>` | Durable project context (architecture, paths, conventions) |

- Tags on status: always `status` + `project:<slug>` (slug matches the key suffix)
- `list --tag status` → every status card; sort is `updated_at DESC`
- Exact read: `get --key status:<slug>`

### Body template (keep short)

```text
STATE: active | paused | blocked | done
FOCUS: <one line — what we are doing>
STOPPED: <concrete checkpoint — file/step/PR/branch tip>
NEXT_AGENT: <first actions on pickup>
WAITING_ON_YOU: <human action, grill answers needed, or none>
PATH: <absolute workspace path if known>
BRANCH: <git branch if relevant>
UPDATED: YYYY-MM-DD
```

`WAITING_ON_YOU` is for the human dashboard. `NEXT_AGENT` is for the next chat. Do not paste architecture or long history here — put that in `project:<slug>` and **link**.

### Links

- `remember --json link --from-key status:<slug> --to-key project:<slug>` (`related`) so pickup can traverse to context
- `cites` from the status card to the active `decision:*` / plan keys the agent must load
- Prefer `supersedes` only when a new status card replaces an old keyed note (rare; usually upsert the same `status:<slug>`)

### When agents must touch status

1. **Pickup** (new conversation / "continue X" / known repo): `get --key status:<slug>` (or `list --tag status` if slug unknown). Follow `links` stubs (`related` / `cites`) before asking the user where you left off.
2. **Pause / session end / meaningful progress**: rewrite `status:<slug>` via `add --key` (upsert) or `update --key --text`. Keep fields honest.
3. **Blocked on the human**: set `STATE: blocked` and fill `WAITING_ON_YOU` before stopping. Clear it when unblocked.
4. **Done / shelved**: `STATE: done` or `paused`; do not delete unless the user asks — history of the card's `updated_at` is useful.

Do **not** maintain a rival STATUS essay inside `project:<slug>` once a `status:<slug>` card exists; one-line pointer is fine.

### Examples

```bash
# Dashboard: what is in flight?
remember --json list --tag status --limit 50

# Pickup treasurehunter
remember --json get --key status:treasurehunter
remember --json related --key status:treasurehunter

# Upsert handoff
remember --json add --as agent --key status:treasurehunter \
  --tag status --tag project:treasurehunter "$(cat <<'EOF'
STATE: blocked
FOCUS: G5 uniques / post-lint play-test
STOPPED: lint-hardening committed on dl-010-product-loop (not pushed)
NEXT_AGENT: wait for owner play-test answers; then G5 site gate
WAITING_ON_YOU: play-test (just game) + earlier grill answers
PATH: <path>
BRANCH: dl-010-product-loop
UPDATED: 2026-09-13
EOF
)"
remember --json link --from-key status:treasurehunter --to-key project:treasurehunter
```

## Safety checklist for agents

1. No secrets in bodies or tags
2. Prefer `--key` for prefs/decisions that will change. Pass `local_id` when the memory has no key
3. Search before add when the subject may already exist
4. Honor paging (`total` vs `count`)
5. Cite real `id`s, `local_id`s, and keys from tool output only
6. Keep the DB on local disk. The device id is the sidecar beside it. Run `device new` after a restore or a copied brain
7. Link only for intentional pairs; tags for shared facets
8. Read stub `bin` (`live`/`expired`/`deleted`); wrong-bin get → exit 3
9. Rename keys with `rekey`, never delete + re-add
10. After `import --from-db`, always run `conflicts`
11. Keep `status:<slug>` lean; update it on pause/block/pickup; traverse links to `project:<slug>`
