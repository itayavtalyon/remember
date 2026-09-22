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
  remember add|search|list|get|update|delete|tags|purge|import|conflicts|link|unlink|related|rekey.
---

# remember — agent skill

Personal durable memory on the local machine. **Not** a chat log.

Binary: `remember` (install via `./scripts/install.sh`). Default DB: `~/.remember/remember.db`.
Device identity lives next to the DB as `<db-path>.device_id` (never copy on import).

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

## Global flags

```text
remember [--db PATH] [--json] <command> …
```

| Flag | Meaning |
|------|---------|
| `--db PATH` | Overrides `REMEMBER_DB` and default `~/.remember/remember.db` |
| `--json` | Machine-readable JSON on stdout (use this from agents) |

**DB location:** must be a **local disk**. Paths under iCloud (`com~apple~CloudDocs`), Dropbox, or Google Drive get a stderr warning and risk corruption. Prefer default `~/.remember`.

## Exit codes

| Code | Meaning |
|------|---------|
| `0` | Success (including empty search/list; import with conflicts still exits 0) |
| `1` | Usage or error |
| `2` | Not found (`get` / `delete` / `update` / `rekey`) in either bin; `link`/`unlink`/`related` missing end |
| `3` | Wrong bin. Tokens: `expired` / `not_expired` / `deleted` / `not_deleted`. Flip `--expired` or `--deleted`. **Not** used by `link`/`unlink`/`related` (those resolve live+expired by default). |

Never invent ids, keys, or sync_ids. On exit `2`, the entry is missing — do not fabricate one. On exit `3`, the row exists in another bin.

## Locators and bins

Exactly **one** of: positional `ID` | `--key KEY` | `--sync-id UUID` (canonical lowercase UUID v7).

Prefer **id** or **`--key`** day-to-day. Use **`--sync-id`** after merge / when citing across brains (local numeric ids can change).

**Bins** (orthogonal flags; mutex):

| Flag | Meaning |
|------|---------|
| (default) | **live** only |
| `--expired` | expired bin only |
| `--deleted` | soft-deleted bin only |
| `--trash` | **deprecated** alias of `--expired` (one stderr deprecation line) |

There is no include-all-bins flag. JSON always has `"bin":"live"|"expired"|"deleted"` (never null, never omit). No `"trash"` field — stubs use `"bin"`.

## Commands (agent style: always `--json` when parsing)

### add

```bash
remember --json add --source agent --tag TOPIC "body text"
remember --json add --source agent --key pref:editor "helix"
remember --json add --source agent --key decision:db --tag project:foo "use SQLite FTS5"
# stdin body:
printf '%s' "long body" | remember --json add --source agent -
# literal body that is exactly a hyphen (not stdin):
remember --json add --source agent -- -
```

- Optional `--ttl 7d` (relative from **write** time; token `Nm`/`Nh`/`Nd`/`Nw`, no leading zeros) or `--expires YYYY-MM-DD` (local end-of-day → UTC) / `YYYY-MM-DDTHH:MM:SS[.frac]Z`. At most one. Past `--expires` is allowed (born expired).
- Optional `--key KEY` → keyed **upsert** (same key replaces body; tags **union**; same id / sync_id)
- Keyless → body-hash merge (duplicate body merges tags). Hitting an **expired or soft-deleted** same key/hash **revives** it (same sync_id).
- Body token `-` alone means **stdin**; after `--`, `-` is a one-character body
- `--source`: `human` | `agent` | `tool` | `share` | `unknown` (default `unknown`). **Agents pass `--source agent` on add only.** `share` is used by the Mac app Share/Service/Intent surfaces — agents should not set it.
- Human stdout: id only. JSON: `{"version":1,"action":"created"|"merged"|"updated","count":1,"entries":[…]}`

### search / list / tags

```bash
remember --json search "editor preference"
remember --json search --tag project:foo --limit 20 --offset 0 "FTS query"
remember --json list --tag pref --limit 20
remember --json list --expired
remember --json tags
```

- search: one required QUERY (FTS5 MATCH). Outer whitespace trimmed; empty after trim → exit 1.
- Filters: `--tag` (AND, repeatable), `--key`, `--source`, `--limit` (default 20, max 1000), `--offset` (≥ 0), `--expired` / `--deleted` (default **live only**)
- JSON envelope: `version`, `offset`, `limit`, `count`, `total`, `entries` (full bodies + `links` stubs; empty array if none)
- If `total` > rows received, page with `--offset` or raise `--limit` before concluding nothing else exists
- list: same filters/paging; no FTS. Sort: `updated_at DESC`, then `id DESC`
- tags: every in-use tag with count; optional `--expired`/`--deleted`

### get / delete / update / rekey (locator)

```bash
remember --json get 3
remember --json get --key pref:editor
remember --json get --sync-id 01234567-89ab-7cde-8f01-23456789abcd
remember --json get --deleted 3
remember --json delete 3                          # soft-delete (live or expired)
remember --json delete --expired 3                # hard wipe one expired row
remember --json delete --deleted 3                # hard wipe one deleted row
remember --json update 3 --text "new body"
remember --json update --key pref:editor --text "nvim"
remember --json update --deleted 3 --undelete     # restore from deleted
remember --json update --expired 3 --clear-expires
remember --json rekey --key decision:old --to-key decision:new
remember --json rekey 12 --clear-key
remember --json purge --expired                   # wipe whole expired bin
remember --json purge --deleted
remember --json purge-trash                       # deprecated ≡ purge --expired
```

**Soft vs hard delete:**

- Unflagged `delete` = **soft** (sets `deleted_at`, clears `expires_at`, keeps body/tags/key/sync_id/edges/FTS)
- `delete --expired` / `delete --deleted` = **one-row hard** CASCADE (needs exactly one registered device)
- `purge --expired|--deleted` = whole-bin hard wipe (same single-device hatch)
- `--trash` ≡ `--expired` + deprecation stderr

**Update rules:**

- At least one of `--text`, `--tag`, `--clear-tags`, `--ttl`, `--expires`, `--clear-expires`, or `--undelete`
- `--tag` + `--clear-tags` together → usage error
- Omit tag flags → tags unchanged
- Body only via `--text` (or `--text -` for stdin; `--text=-` for literal hyphen)
- `--undelete` needs `--deleted`; mutex with ttl/expires flags
- Never changes `source` or `key` (use `rekey` for keys)
- Keyless body-hash collision → exit 1 + conflicting id on stderr
- JSON: `"action":"updated"` or `"action":"deleted"` with full entry snapshot. **add / update / delete / purge JSON have no `links` field**

### import / conflicts / conflict accept

```bash
remember --json import --from-db /path/to/other.db
remember --json conflicts
remember --json conflict accept --id 1 --keep local|incoming|both
```

- Merges by **`sync_id`**. Source opened read-only; must be schema v4+ (older → exit 1).
- Does **not** copy the foreign `.device_id` sidecar or register foreign devices.
- Exit **0** even when conflicts were recorded. **Always run `conflicts` after import.**
- Reasons: `concurrent_vv` | `key_clash` | `hash_clash`
- `--keep both` remints incoming `sync_id` on `concurrent_vv`; on clash keeps incoming sync_id (keyless if the slot is taken)
- JSON import: `action:imported` + `inserted`/`updated`/`unchanged`/`conflicts` counts

### link / unlink / related

```bash
remember --json link --from 12 --to 64
remember --json link 12 64 --kind cites
remember --json link --from-sync-id … --to-sync-id …
remember --json unlink --from 12 --to 64
remember --json related 12
remember --json related --deleted --key gonek
```

**Tag vs link:** tag = facet/set (`#project:foo`). Link = intentional pair. Link when a future agent must traverse A to understand B. Do not link “same project” or “same session.”

**Stored `--kind` (input):** `related` (default on link) | `supersedes` | `cites`. Unknown tokens including `cited_by` / `superseded_by` → exit 1.

**Stub `type` (output, 5 names):** `related`, `cites`, `cited_by`, `supersedes`, `superseded_by`.

**Locators:** each end is exactly one of id / `--*-key` / `--*-sync-id`. Default resolves **live+expired**; `--deleted` includes deleted ends. No exit 3 on graph commands. Default related/stubs **omit deleted** neighbors; expired neighbors stay visible with `bin=expired`.

**rekey:** change a key in place (`--to-key` / `--clear-key`). **Never delete + re-add to rename** — that CASCADE-drops edges.

## Entry JSON shape (fields)

```json
{
  "id": 1,
  "sync_id": "01234567-89ab-7cde-8f01-23456789abcd",
  "key": "pref:editor",
  "body": "helix",
  "tags": ["tools"],
  "source": "agent",
  "created_at": "2026-07-26T12:00:00.000Z",
  "updated_at": "2026-07-26T12:00:00.000Z",
  "expires_at": null,
  "deleted_at": null,
  "bin": "live",
  "version_vector": {"…device…": 1},
  "links": []
}
```

Field order is fixed. `key` is JSON `null` when keyless. On **list / search / get** only, `"links"` follows `"version_vector"`. Stubs are `{id,sync_id,key,type,bin,preview}` — preview ≤ 40 codepoints. Human list/search sixth column is neighbor **ids** only (`[expired]`/`[deleted]` suffix; never `[trash]`).

## Evolving facts — use `--key`

For anything that changes over time:

1. `add --key pref:editor "value"` (first write or overwrite)
2. Read/correct: `get --key` / `update --key --text …`
3. Rename/promote/demote the key: `rekey --to-key` / `--clear-key` (keeps id + edges + sync_id)
4. Soft-remove: `delete --key` (recover with `update --deleted … --undelete`)

Keyed slots make duplicate rivals impossible. Keyless facts still resolve by recency (`updated_at`).

## Tag / key normalization

- ASCII casefold; trim edges; internal spaces OK; no tabs/newlines/controls inside tokens
- Use stable names: `pref:editor`, `decision:db-engine`, `project:remember`

## Human output (when not using `--json`)

- `add` / `update` / `delete` / `rekey`: id (or JSON envelope with `--json`)
- `list` / `search`: `id | key | tags | preview | updated_at | related` (preview ≤ 80 codepoints; related ids cap 5)
- `get` / `related`: `Related:` block after the body (`related` has no body)

Agents should always use `--json` when parsing.

## FTS notes

- Query language is SQLite FTS5 MATCH (document in help / SQLite docs)
- Invalid MATCH syntax → exit 1 (`invalid search query`)
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
remember --json add --source agent --key status:treasurehunter \
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
2. Prefer `--key` for prefs/decisions that will change; `--sync-id` only when citing across merges
3. Search before add when the subject may already exist
4. Honor paging (`total` vs `count`)
5. Cite real ids/keys/sync_ids from tool output only
6. Keep the DB on local disk; leave `<db>.device_id` beside it
7. Link only for intentional pairs; tags for shared facets
8. Read stub `bin` (`live`/`expired`/`deleted`); wrong-bin get → exit 3
9. Rename keys with `rekey`, never delete + re-add
10. After `import --from-db`, always run `conflicts`
11. Keep `status:<slug>` lean; update it on pause/block/pickup; traverse links to `project:<slug>`
