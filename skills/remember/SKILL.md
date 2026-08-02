---
name: remember
description: >
  Use the local `remember` CLI (SQLite + FTS5 personal second brain). Load when
  storing or recalling durable facts, preferences, decisions, corrections, or
  project memory; when the user says "remember that", "what did we decide",
  "search my memory", or when you need a stable key-value preference slot.
  Prefer this over chat-only notes for facts that must survive sessions.
  Use with /remember or when running remember add|search|list|get|update|delete|tags.
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
| `2` | Not found (`get` / `delete` / `update`) |

Never invent ids or keys. On exit `2`, the entry is missing — do not fabricate one.

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

- Optional `--key KEY` → keyed **upsert** (same key replaces body; tags **union**; same id)
- Keyless → body-hash merge (duplicate body merges tags)
- Body token `-` alone means **stdin**; after `--`, `-` is a one-character body
- `--source`: `human` | `agent` | `tool` | `unknown` (default `unknown`). **Agents pass `--source agent` on add only.**
- Human stdout: id only. JSON: `{"version":1,"action":"created"|"merged"|"updated","count":1,"entries":[…]}`

### search

```bash
remember --json search "editor preference"
remember --json search --tag project:foo --limit 20 --offset 0 "FTS query"
```

- One required QUERY (FTS5 MATCH). Outer whitespace trimmed; empty after trim → exit 1.
- Filters: `--tag` (AND, repeatable), `--key`, `--source`, `--limit` (default 20, max 1000), `--offset` (≥ 0)
- JSON envelope: `version`, `offset`, `limit`, `count`, `total`, `entries` (full bodies)
- If `total` > rows received, page with `--offset` or raise `--limit` before concluding nothing else exists

### list

```bash
remember --json list --tag pref --limit 20
remember --json list --key pref:editor
```

Same filters/paging as search; no FTS query. Sort: `updated_at DESC`, then `id DESC`.

### tags

```bash
remember --json tags
```

Every tag in use with its entry count, sorted by name. Takes no options/args.
Use to discover the tag vocabulary before filtering (`list --tag …`) or to offer
tag suggestions. Human: one `name<TAB>count` line per tag. JSON:
`{"version":1,"count":N,"tags":[{"name":"pref","count":3},…]}`. Empty DB →
`{"version":1,"count":0,"tags":[]}`.

### get / delete / update (locator)

Exactly **one** of positional `ID` or `--key KEY`. Never both. Do not invent either.

```bash
remember --json get 3
remember --json get --key pref:editor
remember --json delete 3
remember --json delete --key pref:editor
remember --json update 3 --text "new body"
remember --json update --key pref:editor --text "nvim"
remember --json update 3 --tag a --tag b          # replace tags
remember --json update 3 --clear-tags             # clear tags
remember --json update 3 --text "x" --tag a       # body + tags
```

**Update rules:**

- At least one of `--text`, `--tag`, or `--clear-tags`
- `--tag` + `--clear-tags` together → usage error
- Omit tag flags → tags unchanged (never assume omit clears)
- Body only via `--text` (or `--text -` for stdin; use `--text=-` for a literal
  hyphen body); empty after trim rejected
- Success always bumps `updated_at`
- Never changes `source` or `key`
- Keyless body-hash collision with another keyless entry → exit 1 + conflicting id on stderr
- JSON: `"action":"updated"` or `"action":"deleted"` with full entry snapshot where applicable

## Entry JSON shape (fields)

```json
{
  "id": 1,
  "key": "pref:editor",
  "body": "helix",
  "tags": ["tools"],
  "source": "agent",
  "created_at": "2026-07-26T12:00:00.000Z",
  "updated_at": "2026-07-26T12:00:00.000Z"
}
```

`key` is JSON `null` when keyless. Timestamps are UTC with fixed 3-digit milliseconds.

## Evolving facts — use `--key`

For anything that changes over time:

1. `add --key pref:editor "value"` (first write or overwrite)
2. Read/correct: `get --key` / `update --key --text …`
3. Remove: `delete --key`

Keyed slots make duplicate rivals impossible. Keyless facts still resolve by recency (`updated_at`).

## Tag / key normalization

- ASCII casefold; trim; no whitespace/controls inside tokens
- Use stable names: `pref:editor`, `decision:db-engine`, `project:remember`

## Human output (when not using --json)

- `add` / `update` / `delete`: id (or JSON envelope with `--json`)
- `list` / `search`: `id | key | tags | preview | updated_at` (preview ≤ 80 codepoints)

Agents should prefer `--json`.

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
