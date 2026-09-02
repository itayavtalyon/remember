---
name: remember
description: >
  Use the local `remember` CLI (SQLite + FTS5 personal second brain). Load when
  storing or recalling durable facts, preferences, decisions, corrections, or
  project memory; when the user says "remember that", "what did we decide",
  "search my memory", or when you need a stable key-value preference slot.
  Prefer this over chat-only notes for facts that must survive sessions.
  Use with /remember or when running remember add|search|list|get|update|delete|tags|purge-trash|link|unlink|related|rekey.
---

# remember — agent skill

Personal durable memory on the local machine. **Not** a chat log.

Binary: `remember` (install via `./scripts/install.sh`). Default DB: `~/.remember/remember.db`.

## When to write

- Durable facts, decisions, prefs, corrections useful in **future** sessions
- Project conventions the user wants recalled later
- Prefer a stable **`--key`** for anything that will change over time

## When not to write

- Secrets, tokens, passwords, API keys, private keys
- Transient todos or one-off session chatter
- Huge logs, full file dumps, or paste dumps better kept in the repo

## When to search / list

- Before re-asking a preference or past decision
- Session start when tags are known (`list --tag …` or `search …`)
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
| `0` | Success (including empty search/list) |
| `1` | Usage or error |
| `2` | Not found (`get` / `delete` / `update` / `rekey`) in either bin; `link`/`unlink`/`related` missing end |
| `3` | Wrong bin: `expired` (looked in active, it is trash) or `not_in_trash` (looked with `--trash`, it is active). Flip `--trash`. **Not** used by `link`/`unlink`/`related` (those resolve either bin). |

Never invent ids or keys. On exit `2`, the entry is missing — do not fabricate one. On exit `3`, the row exists in the other bin.

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

- Optional `--ttl 7d` (relative from **write** time; token `Nm`/`Nh`/`Nd`/`Nw`, no leading zeros) or `--expires YYYY-MM-DD` (local end-of-day → UTC) / `YYYY-MM-DDTHH:MM:SS[.frac]Z`. At most one. Past `--expires` is allowed (born in trash).
- Optional `--key KEY` → keyed **upsert** (same key replaces body; tags **union**; same id)
- Keyless → body-hash merge (duplicate body merges tags). A keyless/keyed add that hits an **expired** row **revives** it (same id; clears expiry unless this add sets a new one).
- Body token `-` alone means **stdin**; after `--`, `-` is a one-character body
- `--source`: `human` | `agent` | `tool` | `share` | `unknown` (default `unknown`). **Agents pass `--source agent` on add only.** `share` is used by the Mac app Share/Service/Intent surfaces — agents should not set it.
- Human stdout: id only. JSON: `{"version":1,"action":"created"|"merged"|"updated","count":1,"entries":[…]}`

### search

```bash
remember --json search "editor preference"
remember --json search --tag project:foo --limit 20 --offset 0 "FTS query"
```

- One required QUERY (FTS5 MATCH). Outer whitespace trimmed; empty after trim → exit 1.
- Filters: `--tag` (AND, repeatable), `--key`, `--source`, `--limit` (default 20, max 1000), `--offset` (≥ 0), `--trash` (trash only; default is **active only**)
- JSON envelope: `version`, `offset`, `limit`, `count`, `total`, `entries` (full bodies + `links` stubs after `expires_at`; empty array if none)
- If `total` > rows received, page with `--offset` or raise `--limit` before concluding nothing else exists

### list

```bash
remember --json list --tag pref --limit 20
remember --json list --key pref:editor
```

Same filters/paging as search; no FTS query. Sort: `updated_at DESC`, then `id DESC`. Default omits trash; `--trash` lists expired only. JSON `links` stubs after `expires_at`. Human sixth column is neighbor **ids** only.

### tags

```bash
remember --json tags
```

Every tag in use with its entry count, sorted by name. Optional `--trash` counts among trash only; default is active only.
Use to discover the tag vocabulary before filtering (`list --tag …`) or to offer
tag suggestions. Human: one `name<TAB>count` line per tag. JSON:
`{"version":1,"count":N,"tags":[{"name":"pref","count":3},…]}`. Empty DB →
`{"version":1,"count":0,"tags":[]}`.

### get / delete / update (locator)

Exactly **one** of positional `ID` or `--key KEY`. Never both. Do not invent either.

```bash
remember --json get 3
remember --json get --trash 3
remember --json get --key pref:editor
remember --json delete 3
remember --json delete --trash 3
remember --json delete --key pref:editor
remember --json update 3 --text "new body"
remember --json update --key pref:editor --text "nvim"
remember --json update 3 --tag a --tag b          # replace tags
remember --json update 3 --clear-tags             # clear tags
remember --json update 3 --text "x" --tag a       # body + tags
remember --json update --trash 3 --clear-expires  # restore from trash
remember --json update --trash 3 --ttl 7d         # restore with new TTL
remember --json purge-trash                       # permanently delete all trash
```

Default locators are **active only**. `--trash` is trash only. There is no include-both flag.

**Update rules:**

- At least one of `--text`, `--tag`, `--clear-tags`, `--ttl`, `--expires`, or `--clear-expires`
- `--tag` + `--clear-tags` together → usage error
- Omit tag flags → tags unchanged (never assume omit clears)
- Body only via `--text` (or `--text -` for stdin; use `--text=-` for a literal
  hyphen body); empty after trim rejected
- Success always bumps `updated_at`
- Never changes `source` or `key`
- Keyless body-hash collision with another keyless entry → exit 1 + conflicting id on stderr
- `--clear-expires` + `--ttl`/`--expires` → usage error. Restore is `update --trash <locator> --clear-expires`.
- `--ttl` on update is 7d from the **update**, not from creation.
- JSON: `"action":"updated"` or `"action":"deleted"` with full entry snapshot where applicable. **add / update / delete / purge-trash JSON have no `links` field** (kit may default missing `links` to `[]`).
- `purge-trash` JSON: `{"version":1,"action":"deleted","count":N,"entries":[…all snapshots…]}` (no cap). Empty trash: count 0, exit 0. Human stdout: the integer N.

### link / unlink / related / rekey

```bash
remember --json link --from 12 --to 64
remember --json link 12 64 --kind cites
remember --json unlink --from 12 --to 64
remember --json related 12
remember --json rekey --key decision:old --to-key decision:new
remember --json rekey 12 --clear-key
```

**Tag vs link:** tag = facet/set (`#project:foo`). Link = intentional pair. Link when a future agent must traverse A to understand B. Do not link “same project” or “same session.”

**Stored `--kind` (input):** `related` (default on link) | `supersedes` | `cites`. Unknown tokens including `cited_by` / `superseded_by` → exit 1. Prefer `supersedes`/`cites` when that is the truth.

**Stub `type` (output, 5 names):** `related`, `cites`, `cited_by`, `supersedes`, `superseded_by`. `cited_by` / `superseded_by` mean the **neighbor** points at *this* entry (this entry is the cited / superseded one).

**Locators:** `link`/`unlink`/`related` resolve in **either bin** (no exit 3). `get`/`update`/`delete`/`rekey` stay active-xor-trash (wrong bin → exit 3). Edges **survive trash**; a stub can be `"trash": true`. Honor that: `get` without `--trash` on a trashed neighbor is exit 3.

**rekey:** change a key in place (`--to-key NEW` set/rename, including keyless→keyed; `--clear-key` demote). Mutex; at least one required. Empty `--to-key` is illegal (`empty key`), never a demote, never `key=''`. **Never delete + re-add to rename** — that CASCADE-drops edges. JSON matches `update` (`action:"updated"`).

## Entry JSON shape (fields)

```json
{
  "id": 1,
  "key": "pref:editor",
  "body": "helix",
  "tags": ["tools"],
  "source": "agent",
  "created_at": "2026-07-26T12:00:00.000Z",
  "updated_at": "2026-07-26T12:00:00.000Z",
  "expires_at": null,
  "links": []
}
```

`key` is JSON `null` when keyless. `expires_at` is always present after `updated_at` (`null` or canonical UTC `.mmmZ`). On **list / search / get** only, `"links"` follows `"expires_at"` (empty array if none). Stubs are `{id,key,type,trash,preview}` — preview ≤ 40 codepoints, not a full neighbor body. `get` the neighbor if the preview warrants it. Timestamps are UTC with fixed 3-digit milliseconds. Human `list`/`search`: `id | key | tags | preview | updated_at | related` (related = neighbor ids, `[trash]` glued, cap 5 then `, +N`; empty cell keeps the pipes). Human `get`: body, then a `Related:` block (`type id|key [trash] preview`) omitted when none. Read expiry and links from `--json`.

## Evolving facts — use `--key`

For anything that changes over time:

1. `add --key pref:editor "value"` (first write or overwrite)
2. Read/correct: `get --key` / `update --key --text …`
3. Rename/promote/demote the key: `rekey --to-key` / `--clear-key` (keeps id + edges)
4. Remove: `delete --key`

Keyed slots make duplicate rivals impossible. Keyless facts still resolve by recency (`updated_at`).

## Tag / key normalization

- ASCII casefold; trim edges; internal spaces OK; no tabs/newlines/controls inside tokens
- Use stable names: `pref:editor`, `decision:db-engine`, `project:remember`

## Human output (when not using --json)

- `add` / `update` / `delete` / `rekey`: id (or JSON envelope with `--json`)
- `list` / `search`: `id | key | tags | preview | updated_at | related` (preview ≤ 80 codepoints; related ids cap 5)
- `get` / `related`: `Related:` block after the body (`related` has no body)

Agents should always use `--json` when parsing.

## FTS notes

- Query language is SQLite FTS5 MATCH (document in help / SQLite docs)
- Invalid MATCH syntax → exit 1 (`invalid search query`)
- Empty result set → exit 0 with `count:0` (not an error)

## Safety checklist for agents

1. No secrets in bodies or tags
2. Prefer `--key` for prefs/decisions that will change
3. Search before add when the subject may already exist
4. Honor paging (`total` vs `count`)
5. Cite real ids/keys from tool output only
6. Keep the DB on local disk
7. Link only for intentional pairs; tags for shared facets
8. Read stub `type` as the 5-name vocabulary; honor `"trash": true` (exit 3 on `get` without `--trash`)
9. Rename keys with `rekey`, never delete + re-add
