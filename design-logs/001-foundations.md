# 001 - Foundations: `remember` CLI (personal second brain)

**Status:** Design (immutable — deviations go in Implementation Results)
**Author:** Itay (solo)
**Created:** 2026-07-20
**Finalized:** 2026-07-21
**Amended:** 2026-07-21 (Round 6 — update footgun + strict C tooling; pre-implementation)
**Amended:** 2026-07-21 (Round 7 — clear-tags, paging, JSON uniformity, FTS/rowid, schema version, iCloud; pre-implementation)
**Amended:** 2026-07-21 (Round 8 — `--key` key-value slots; pre-implementation)
**Amended:** 2026-07-27 (Round 9 — timestamps: millisecond precision; step 07 implementation)

## Background

No code exists yet. `remember` is a new C project under `Code/remember/`. The only artifacts so far are this design-logs tree.

The product intent: a small local CLI for **humans and coding agents** to write durable facts into persistent memory and retrieve them later. Storage is **SQLite** with **FTS5** for ranked full-text search and relational tags for structured filters.

This is also an intentional **C project**: in-tree SQLite amalgamation, careful CLI/schema design, agent-friendly contracts. C is a deliberate choice (control, craft, no runtime), not a fallback.

## Problem

Without a design log, implementation will drift on the hard parts:

1. **CLI grammar and lifecycle** — store/search alone is not enough for agents that must cite, correct, and delete memories.
2. **Search semantics** — tags vs full-text must not be an unexplained union with random ranking.
3. **Agent contract** — stable IDs, JSON schema, exit codes, and a skill that teaches *when* to write vs search.
4. **Dedupe vs identity** — exact-body dedupe must not destroy IDs agents already cited.
5. **Scope discipline** — C + amalgamation is enough work; every extra feature costs.

## Questions and Answers

### Round 1 — Product north star

- **Q: Primary job of `remember` in v1?**  
  A: Personal second brain — humans + agents store durable facts, decisions, prefs; search later. Not a chat log, not multi-user team KB.

- **Q: How many memory stores by default?**  
  A: One global DB per user (`~/.remember/remember.db`).

- **Q: What is an entry, minimally?**  
  A: Text body + tags + auto metadata (id, timestamps, optional source). No required title.

- **Q: Tag model?**  
  A: Repeated flags only: `--tag a --tag b`. No multi-word tags in a single flag value.

- **Q: Search behavior for `--search "foo bar"`?**  
  A: Single ranked FTS over body + tags (tags indexed into FTS). Document FTS5 query syntax. Keyword-only; no embeddings in v1.

- **Q: Minimum CLI surface?**  
  A: Initially add/search/list/get/delete; **update pulled into v1** in a later round (see Round 4–5).

- **Q: Agent output?**  
  A: Human text by default; `--json` for agents (stable schema).

- **Q: Why C?**  
  A: Deliberate preference — C is the right tool for a small systems CLI with amalgamated SQLite. Learning/practice is welcome; language is not provisional.

- **Q: Duplicate policy?**  
  A: Dedupe on body hash (after trim — see Round 5).

- **Q: Provenance?**  
  A: Optional `--source` (refined to enum in Round 3). Metadata only; first writer wins.

### Round 2 — CLI, dedupe, IDs, agents

- **Q: CLI grammar?**  
  A: Subcommands only: `add`, `search`, `list`, `get`, `delete`, `update`. No bare-string store.

- **Q: On exact-body dedupe, tags/metadata?**  
  A: Merge tags; keep original id; touch `updated_at`. Return existing id + indicate merge.

- **Q: ID format?**  
  A: Monotonic integer (SQLite integer primary key).

- **Q: Long bodies?**  
  A: Argv string and/or stdin if body is `-`.

- **Q: Result bounds?**  
  A: Default `--limit 20` on search/list; overridable.

- **Q: Delete semantics?**  
  A: Hard delete by id only.

- **Q: Secret scanning?**  
  A: None in tool v1. Skill documents “never store secrets/PII.”

- **Q: Default DB path?**  
  A: `~/.remember/remember.db`.

- **Q: When should an agent WRITE?**  
  A: Only durable facts the user would want next session — not chat fluff or transient plans.

- **Q: Platform / build?**  
  A: Amalgamated SQLite in-tree; CMake; target macOS + Linux via portable C11.

### Round 3 — Schema contracts and polish

- **Q: Tag normalization?**  
  A: Lowercase + allow Unicode letters — with pragmatic C constraint: UTF-8 stored; only ASCII A–Z casefolded.

- **Q: `list` filters?**  
  A: Repeatable `--tag` (AND), `--source`, `--limit`.

- **Q: Timestamps?**  
  A: ISO-8601 UTC TEXT with **fixed 3-digit millisecond** fraction (`2026-07-20T15:04:05.123Z`). Written by `utc_now` via C11 `timespec_get`. Fixed-width `.mmm` keeps lexicographic order equal to chronological order for `updated_at DESC` / `id DESC` sorts. (Round 9 revises the second-only example from earlier rounds.)

- **Q: Dedupe merge vs `source`?**  
  A: Keep original source; only merge tags + `updated_at`. `source` is first-writer metadata with no other behavior.

- **Q: Body size limit?**  
  A: Reject bodies above 64 KiB (**after trim**).

- **Q: Human list/search columns?**  
  A: `id | tags | short body preview | updated_at`.

- **Q: JSON shape?**  
  A: Versioned envelope `{"version":1,"entries":[...]}` (add/merge may use a smaller object; see Design).

- **Q: Exit codes?**  
  A: `0` success (including empty search/list); `1` usage/error; `2` not found (`get`/`delete`/`update` missing id).

- **Q: Agent skill location?**  
  A: In-repo `skills/remember/SKILL.md` + install notes in README.

- **Q: DB path override?**  
  A: `--db PATH` wins over `REMEMBER_DB` env wins over default.

### Round 4 — Implementation edges + update

- **Q: Unicode casefold in pure C?**  
  A: UTF-8 store as-is; casefold only ASCII A–Z → a–z. Non-ASCII case variants may not merge. Document limitation.

- **Q: `source` values?**  
  A: Enum: `human` | `agent` | `tool` | `unknown`. Default `unknown` if omitted.

- **Q: Successful add/merge stdout?**  
  A: Human: print id only. JSON: object with `id` + `created` | `merged`.

- **Q: Tag storage schema?**  
  A: `entries` + `tags` + `entry_tags` M2M + FTS5 virtual table.

- **Q: Default sort?**  
  A: `list`: `updated_at DESC`. `search`: FTS rank, then `updated_at DESC`.

- **Q: Empty body / empty search?**  
  A: Reject empty body (refined in Round 5). Empty search → usage error (use `list` to browse).

- **Q: Preview length?**  
  A: First line, max 80 columns, ellipsis.

- **Q: Regret shipping without…?**  
  A: Must have **`update` / retag without rewrite** in v1.

### Round 5 — Smell fixes and open-item freeze

- **Q: Near-duplicate bodies differing only by surrounding whitespace?**  
  A: **Trim** leading/trailing ASCII whitespace before store and before hash. Hash the trimmed body as stored.

- **Q: Can `update` set tags?**  
  A: **Yes.** Earlier draft always allowed full tag-set replace; the only gap was “clear all tags.” Final rule: **`update` always sets body and always sets the tag list** from the `--tag` flags present on that invocation (zero `--tag` flags ⇒ empty tag list). No separate `--clear-tags`.

- **Q: Full bodies in JSON search/list?**  
  A: Yes — agents should not need N extra `get`s.

- **Q: Is `source` useful?**  
  A: Keep as first-writer metadata only. No merge/update behavior beyond that. Fine for v1.

- **Q: Language alternative to C?**  
  A: Not desired. C is the chosen implementation language.

- **Q: Final `update` semantics?**  
  A: Sets body and tag list. If the new trimmed body hashes to a **different** existing entry → fail with error (name conflicting id on stderr). `source` unchanged. Missing id → exit 2.

- **Q: Empty / whitespace-only body?**  
  A: Trim; if empty → reject with error (expecting body). Applies to `add` and `update`.

- **Q: Tag charset?**  
  A: UTF-8; ASCII casefold A–Z; allow letters (Unicode bytes as-is), digits, and `:_./-`. Reject ASCII whitespace and control chars inside a tag. Max 64 bytes per tag.

- **Q: SQLite amalgamation version?**  
  A: Pin current stable at implementation time; record version in README / third_party note.

### Round 6 — Update footgun + strict C (pre-implementation amendment)

- **Q: Accidental tag wipe on text-only update?**  
  A: Real footgun. **Do not** treat omitted `--tag` as “clear tags.” Body and tags are **independently opt-in** on `update`.

- **Q: How do you change the body on update?**  
  A: Explicit **`--text BODY|-`** (alias name: text, not positional). No bare positional body on `update`.

- **Q: How do you change tags on update?**  
  A:
  - Omit all `--tag` flags → **tags unchanged**
  - One or more `--tag NAME` → **replace** tag set with exactly those names
  - Clear all tags → `--tag` with **nothing** (empty value): prefer `--tag=` ; also accept a lone `--tag` when it does not consume a following value (see parsing note). Mixing non-empty `--tag NAME` with clear form in one command → usage error.

- **Q: Minimum `update` invocation?**  
  A: Must request at least one change: `--text` and/or a tag operation (non-empty `--tag`s and/or clear form). Otherwise usage error.

- **Q: How strict should the C toolchain be?**  
  A: As strict as practical — warnings-as-errors, sanitizers in Debug, and a lint suite: `clang-tidy`, `cppcheck`, `scan-build` (and `splint` if available). CMake `lint` target (pattern from MazeMaker). Format with `clang-format`.

### Round 7 — Amendment: clear-tags, paging, JSON uniformity, FTS/rowid, schema version, iCloud (pre-implementation)

- **Q: Clear tags on `update` — bare `--tag` or a flag?**  
  A: **`--clear-tags` flag.** All bare/empty `--tag` forms are removed. `--tag` **always** consumes exactly one following tag token, on every subcommand (`--tag one --tag two`). This kills the parse ambiguity where `remember update --tag foo 5` could read as either "set {foo} on id 5" or "clear tags, positionals foo/5" — it now unambiguously means the former.

- **Q: Final `update` tag semantics?**  
  A: No `--tag` and no `--clear-tags` → tags unchanged. One or more `--tag NAME` → replace set. `--clear-tags` → empty set. `--tag NAME` with `--clear-tags` → usage error. At least one change (`--text`, `--tag`, or `--clear-tags`) still required.

- **Q: Truncation / paging?**  
  A: Add `--offset M` (default 0) to `search`/`list`. JSON envelope carries `offset`, `limit`, `count` (rows returned) and `total` (rows matching, ignoring limit/offset) so a caller can detect truncation. No more silent truncation.

- **Q: JSON shape uniformity?**  
  A: Every command returns `{"version":1, ..., "entries":[...]}`. `add`/`update`/`get`/`delete` return the single affected entry (count 1); `delete` fetches the row before deleting so the response echoes what was removed. `add`/`update`/`delete` also carry `action`.

- **Q: FTS storage — alternative to duplicating the body?**  
  A: Standalone FTS is correct. The only way to avoid the copy is an external-content table, but `tags` isn't a column of `entries`, so external-content would force a synced `tags_text` column plus the stricter external-content trigger protocol — more complexity to save a few MB. Keep standalone; duplication accepted.

- **Q: FTS rowid?**  
  A: `entries_fts.rowid = entries.id`, set explicitly on insert. Non-negotiable so filters/joins line up.

- **Q: FTS sync — triggers?**  
  A: **No triggers; sync explicitly in C.** All writes flow through one writer (`commands.c`); after mutating `entries`/`entry_tags` for an entry, rewrite that entry's FTS row (body + space-joined tags) in the same transaction. Triggers earn their keep with many writers; here they'd just duplicate code we already control, harder to debug.

- **Q: Tag special chars vs FTS tokenizer?**  
  A: **Use the default `tokenize = 'unicode61 remove_diacritics 2'`; do NOT set `tokenchars`.** (Revises an earlier Round 7 draft that proposed `tokenchars '-_:./'`.) `tokenchars` is tokenizer-global — one tokenizer serves all FTS columns — so making `-_:./` token characters would also change *body* tokenization (`client-side`, `read/write`, `v1.2.3`, `foo_bar` stop splitting) and hurt prose recall on the primary content. Letting the default split compound tags into components (`pref:editor` → `pref`, `editor`) actually *helps* free-text recall — searching `editor` surfaces the tagged entry — while exact compound matching stays on `--tag pref:editor`. Rejected: rewriting chars to `_` (underscore also splits under unicode61, and it desyncs stored vs indexed forms).

- **Q: Schema versioning?**  
  A: `PRAGMA user_version = 1` at create. On open: `0` → fresh, create schema, set 1; `1` → proceed; `> 1` → refuse ("database is newer than this remember", exit 1). No migrations planned; the hook exists.

- **Q: WAL vs iCloud?**  
  A: Keep the default rollback journal + `busy_timeout=5000` (no WAL): fewer sidecar files, and it avoids WAL's shared-memory requirement, which breaks on network/synced volumes. The DB must live on a local disk — any file-sync layer (iCloud, Dropbox) can copy the db + journal out from under an open connection and corrupt it, in any journal mode. Default `~/.remember` is local. If the resolved path looks synced (`com~apple~CloudDocs`, `Dropbox`, `Google Drive`), print a one-line stderr warning and proceed. Heuristic, not a real filesystem-type probe.

- **Q: (6) Evolving / contradictory facts?**  
  A: No schema-level supersession in v1 (still a non-goal). The mechanism is a **skill protocol + tag convention**: before `add`, `search` the subject; if an entry already covers it, `update` that id instead of adding a rival. Encourage a stable subject-key tag (`pref:editor`, `decision:db-engine`) so the canonical entry is findable via `list --tag pref:editor`. Contradictions can still coexist; newest `updated_at` wins when a reader must choose (already the tie-break). Documented as a known limitation.

- **Q: Preview width unit?**  
  A: **Codepoints**, not "columns". Max 80 codepoints, truncate on a codepoint boundary (never split a multibyte sequence), ellipsis if cut.

- **Q: Tag UTF-8 validation?**  
  A: Reject invalid UTF-8 in tags (parity with body).

- **Q: Re-add bumping `updated_at`?**  
  A: Intended. Re-storing an identical body is a relevance signal; it refreshes recency.

- **Q: `update` that requests a change but changes nothing (e.g., `--text` equal to the current body)?**  
  A: **No-op success** — exit 0, `updated_at` not bumped, current entry returned. Deliberately asymmetric with `add` (which *does* bump on an identical body): `add` re-asserts relevance, `update` is a diff — an empty diff is nothing to record.

### Round 8 — Amendment: `--key` key-value slots (pre-implementation)

Pulls "upsert by subject key" from the v2 wishlist into v1. A `--key` names a durable **slot**; the body is its **value**; writing an existing key replaces the value in place. This makes the fact-lifecycle protocol mechanical instead of disciplinary.

- **Q: What is a key's "value"?**  
  A: The **body is the value**. `add --key editor "helix"` sets slot `editor` to "helix"; re-writing the key replaces the body. No separate `--value` field.

- **Q: How does `--key` interact with body-hash dedupe?**  
  A: **Key is the identity when present.** Two independent identity axes via partial unique indexes: keyless entries dedupe by `body_hash` (as before); keyed entries are unique by `key` and do **not** participate in body-hash dedupe. A keyed entry's body need not be globally unique.

- **Q: Tags on a key upsert (existing key, new body)?**  
  A: **Merge (union)**, exactly like a keyless duplicate add. `--tag` adds, never removes; change or clear tags via `update`. Avoids the tag-wipe footgun.

- **Q: Which commands take `--key`?**  
  A: **Full surface.** `add --key` upserts; `get`/`delete`/`update` accept `--key` as an alternative locator to the positional id; `list`/`search` accept `--key` as an exact filter. Exactly one of {positional id, `--key`} on get/delete/update — both or neither → usage error.

- **Q: Can a key be changed or cleared after creation?**  
  A: **No in v1.** Like the numeric id, a key is a stable identity: set once at `add`, used thereafter to address the entry. Renaming a slot or promoting a keyless entry to keyed → delete + re-add, or Future Work.

- **Q: Key normalization?**  
  A: Same algorithm as tags (trim, valid UTF-8, no whitespace/control, ASCII casefold, ≤ 64 bytes, `:_./-` allowed). One key per entry.

- **Q: `source`/`created_at` on upsert?**  
  A: First-writer wins, like merge — keep the original `source` and `created_at`; replace body, union tags, bump `updated_at`.

- **Q: `update` whose new value equals the current value — bump `updated_at`?**  
  A: **Yes** (revises Round 7's no-op-success answer). Writing is a touch: any successful `update` sets `updated_at = now`, symmetric with re-`add`. `update` with *no* change flag at all is still a usage error. Net rule: **every successful write — `add`, keyed upsert, `update` — refreshes `updated_at`.**

- **Q: May distinct keys hold the same value?**  
  A: **Yes, required.** Keys are unique; values are not. Different slots must be able to hold the same body — `true`, `0`, a shared string. The partial-unique-on-`body_hash`-where-`key IS NULL` index already allows this (keyed rows skip body-hash dedupe).

### Round 9 — Amendment: millisecond timestamps (step 07 implementation)

Recorded while landing `update`. Second-only stamps forced tests to `sleep(1)` to observe `updated_at` bumps and list order; fractional seconds remove that footgun without changing sort contracts.

- **Q: Timestamp precision?**  
  A: **Millisecond**, fixed-width fraction. Format: `YYYY-MM-DDTHH:MM:SS.mmmZ` (24 characters + NUL). Source: C11 `timespec_get(..., TIME_UTC)`. Applies to every write that sets `created_at` / `updated_at` (`add`, keyed upsert, merge, `update`).

- **Q: Why fixed three digits, not variable fraction?**  
  A: Lexicographic `ORDER BY updated_at DESC` must match chronological order. Variable-length fractions (or omitting `.000`) would sort incorrectly. Always pad to `.mmm`.

- **Q: Is this a schema migration?**  
  A: **No.** Columns remain `TEXT`. Existing second-only rows (if any) still sort correctly against ms rows of the same second only if they lack a fraction — green-field v1 DBs always write ms. No `user_version` bump.

- **Q: Parsing for agents / tests?**  
  A: Treat as opaque ISO-8601 ending in `Z`. Do **not** assume a 20-character second stamp; extract fields by JSON quotes / a real parser.

## Design

### Goals (v1)

| Goal | Detail |
|------|--------|
| Local personal memory | Single user DB, no network |
| Human + agent UX | Readable default; `--json` + skill for agents |
| Retrieve well | FTS5 ranked search over body+tags; tag AND filters |
| Correctable | get / update / delete by stable integer id or `--key` |
| Durable slots | Optional `--key` names a key-value slot; re-writing replaces the value in place |
| Solid C systems tool | Amalgamation, clear modules, tests, sanitizers, static analysis |

### Non-goals (v1)

- Multi-user sync, auth, cloud
- Embeddings / semantic search
- Full Unicode casefolding (ICU/utf8proc)
- Secret scanning / encryption at rest
- TUI, fzf integration, GUI
- Project-scoped DBs (may come later via `--db` discipline)
- Soft delete / history / revisions
- Multi-word tags
- Changing `source` after first write
- Changing or clearing an entry's `--key`, or promoting a keyless entry to keyed (delete + re-add)

### CLI surface

```
remember add    [--key KEY] [--tag TAG ...] [--source SRC] [--db PATH] [--json] BODY|-
remember search [--limit N] [--offset M] [--tag TAG ...] [--key KEY] [--source SRC] [--db PATH] [--json] QUERY
remember list   [--limit N] [--offset M] [--tag TAG ...] [--key KEY] [--source SRC] [--db PATH] [--json]
remember get    [--db PATH] [--json] ID|--key KEY
remember update [--text BODY|-] [--tag TAG]... [--clear-tags] [--db PATH] [--json] ID|--key KEY
remember delete [--db PATH] [--json] ID|--key KEY
remember --help
remember --version
```

**Parsing rules:**

- Subcommand required.
- **`add` body:** single argv string, or `-` for stdin.
- **`update` body:** only via **`--text BODY|-`**. Never positional — avoids “I only changed the text” wiping tags.
- Body pipeline (when a body is supplied): read → **trim leading/trailing ASCII whitespace** → reject if empty → reject if `len > 64 KiB` → store and hash trimmed bytes. Reject invalid UTF-8.
- `--tag NAME` repeatable; **always consumes exactly one following tag token** (every subcommand). No bare or empty `--tag` form.
  - On **`add`**: tags attached to the new/merged entry (union on dedupe merge).
  - On **`update`**: omit all `--tag`/`--clear-tags` = leave unchanged; one or more `--tag NAME` = replace set; `--clear-tags` = empty set. `--tag NAME` together with `--clear-tags` = usage error.
- `--key KEY` optional stable slot name (one per entry; normalized like a tag). On **`add`** it upserts by key. On **`get`/`delete`/`update`** it locates the entry instead of a positional id — supply **exactly one** of {`ID`, `--key`}; both or neither → usage error. On **`list`/`search`** it is an exact-match filter.
- `--source` one of: `human`, `agent`, `tool`, `unknown` (**add only**).
- `--db` overrides `REMEMBER_DB` overrides `~/.remember/remember.db`.
- `--limit` default 20; must be ≥ 1; hard cap 1000. `--offset` default 0; must be ≥ 0 (`search`/`list` only).
- `--json` switches stdout to JSON; diagnostics on stderr.

**Preferred form:** `remember <subcmd> [opts] [args]`.

**`update` tag operations:**

| Form | Meaning |
|------|---------|
| *(no `--tag`, no `--clear-tags`)* | Do not touch tags |
| `--tag a --tag b` | Set tags to `{a, b}` |
| `--clear-tags` | Clear all tags (empty set) |
| `--tag NAME` **and** `--clear-tags` together | Usage error (exit 1) |

`--tag` always consumes exactly one following token; there is no bare or empty `--tag` form. This removes the old parse ambiguity where `remember update --tag foo 5` could read as either "set {foo} on id 5" or "clear tags, positionals foo/5" — it now unambiguously means the former.

### Data model

```text
entries
  id            INTEGER PRIMARY KEY
  key           TEXT                   -- optional stable slot name (normalized); NULL = keyless
  body          TEXT NOT NULL          -- trimmed body as stored (the value)
  body_hash     TEXT NOT NULL          -- hex sha256 of trimmed body bytes
  source        TEXT NOT NULL          -- human|agent|tool|unknown (first writer)
  created_at    TEXT NOT NULL          -- ISO-8601 UTC ms: YYYY-MM-DDTHH:MM:SS.mmmZ
  updated_at    TEXT NOT NULL          -- same format; refreshed on every successful write

-- identity axes (partial unique indexes):
--   keyed entries unique by key;  keyless entries unique by body_hash
CREATE UNIQUE INDEX ux_entries_key      ON entries(key)       WHERE key IS NOT NULL;
CREATE UNIQUE INDEX ux_entries_bodyhash ON entries(body_hash) WHERE key IS NULL;

tags
  id            INTEGER PRIMARY KEY
  name          TEXT NOT NULL UNIQUE   -- normalized

entry_tags
  entry_id      INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE
  tag_id        INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE
  PRIMARY KEY (entry_id, tag_id)

entries_fts                                     -- FTS5, standalone (not external-content)
  rowid         -- MUST equal entries.id (set explicitly on insert)
  body          -- entry body (duplicated; accepted for search)
  tags          -- space-separated tag names (denormalized for FTS)
  -- kept in sync explicitly in C on every write (single writer), no triggers
```

**Tag normalization algorithm:**

1. Trim leading/trailing ASCII whitespace; reject if empty after trim.
2. Reject invalid UTF-8 (parity with body).
3. Reject if tag contains ASCII whitespace.
4. Reject if longer than 64 bytes (UTF-8).
5. Reject ASCII control characters.
6. ASCII A–Z → a–z; other bytes unchanged.
7. Allowed content: UTF-8 text including letters/digits and `:_./-` (and other non-control non-space bytes). Skill should prefer short tokens and optional `project:name` style.

**Key normalization:** identical to the tag algorithm above. One key per entry (nullable).

**Body rules:**

1. Read from argv or stdin (`-`).
2. Trim leading/trailing ASCII whitespace (`space`, `tab`, `LF`, `CR`, `VT`, `FF`).
3. If empty after trim → error: expecting body (exit 1).
4. If length > 65536 bytes → error (exit 1).
5. Reject invalid UTF-8 (exit 1).
6. `body_hash` = lowercase hex SHA-256 of the **trimmed** body bytes as stored.
7. SHA-256: tiny public-domain implementation in-tree (`third_party/sha256/`).

**FTS5:**

- `tokenize = 'unicode61 remove_diacritics 2'` — default tokenizer, no `tokenchars` (see Round 7). `tokenchars` is tokenizer-global and would degrade body search; letting compound tags split into components aids free-text recall, with exact compound matching via `--tag`.
- `entries_fts.rowid = entries.id`, set explicitly. Sync is done in C inside the same transaction as the entry/tag mutation — no triggers.
- Search query → FTS5 MATCH; invalid syntax → exit 1 with clear error.
- Rank: `bm25(entries_fts)`; tie-break `updated_at DESC`.
- `--tag` filters are SQL AND on M2M, combined with FTS when both present (narrowing, not OR).

**Search clarification (supersedes original sketch):**

- Original: “tagged with query OR containing it” as one flag.
- **v1:** `search QUERY` is FTS only (body + tags text). Structured tag filter is `--tag`, AND-combined with FTS.

### Command semantics

| Command | Behavior |
|---------|----------|
| `add` | Trim body; hash. **Keyless:** on `body_hash` conflict → merge tags (union), `updated_at=now`, keep id/source/created_at/body (`action:"merged"`); else insert (`action:"created"`). **Keyed (`--key`):** upsert by key — if key exists → replace body+hash, union tags, keep id/source/created_at/key, bump `updated_at` (`action:"updated"`); else insert with key (`action:"created"`). Keyed writes ignore body-hash dedupe (so distinct keys may share a value like `0` or `true`). Re-sync FTS. Human stdout: `id`. JSON: `{"version":1,"action":"created"\|"merged"\|"updated","count":1,"entries":[<full entry>]}`. |
| `search` | FTS MATCH + optional tag AND + optional `--key`/`--source` filters; `limit`/`offset` page; human previews; JSON paged envelope (`offset`,`limit`,`count`,`total`) with **full** body + tags + metadata per hit. `total` = matches ignoring limit/offset. |
| `list` | No FTS; `--tag`/`--key`/`--source` filters only; `limit`/`offset` page; same paged output shapes as search; sort `updated_at DESC`. |
| `get` | Full entry by `ID` or `--key`; exit 2 if missing. |
| `update` | Locate by `ID` or `--key`. Require at least one change (`--text`, `--tag`, or `--clear-tags`). Load entry (exit 2 if missing). **If `--text`:** trim body; reject empty; set body+hash. **Body-hash conflict applies only to keyless entries:** if the entry has no key and the new hash equals **another keyless** entry → exit 1 + conflicting id; keyed entries have no body-hash uniqueness, so no conflict check. **Tags:** one or more `--tag NAME` → replace set; `--clear-tags` → empty set; neither → leave unchanged (`--tag`+`--clear-tags` together → exit 1). Re-sync FTS. A successful update always sets `updated_at = now` (writing is a touch, symmetric with re-`add`), even when the new value equals the old. **Never change `source` or `key`**. Human: print id. JSON: `{"version":1,"action":"updated","count":1,"entries":[<full entry>]}`. |
| `delete` | Locate by `ID` or `--key`; load entry (exit 2 if missing); hard delete (CASCADE entry_tags); remove tags with refcount 0; re-sync FTS. JSON echoes the removed row: `{"version":1,"action":"deleted","count":1,"entries":[<entry as it was>]}`. |

**Note on update vs add dedupe (keyless entries):** Updating a keyless entry A to body text that already exists on another keyless entry B fails (ids stay stable). Re-`add`ing the same body merges into the existing row. Agents that need “change text to something already stored” must delete, choose different wording, or use a `--key` (keyed entries are not body-deduped).

### Fact lifecycle & evolution (v1 protocol)

A memory's job is to hold facts that change. In v1 the mechanism is **`--key` key-value slots** — the schema enforces the canonical entry, so this is no longer a matter of discipline. Canonical source for README + `SKILL.md`:

1. **Give an evolving fact a stable key** — `add --key pref:editor "helix"`. The key names the *slot*, not the current value.
2. **To change it, write the same key again** — `add --key pref:editor "zed"` replaces the value in place: same id, tags unioned, `updated_at` bumped. No search-first, no rival entry.
3. **Read / correct / remove by key** — `get --key pref:editor`, `update --key pref:editor …`, `delete --key pref:editor`. No need to know the numeric id.
4. **Keyless facts** (one-off observations without a natural slot) still work and still dedupe by body; when two keyless entries conflict, newest `updated_at` wins (the search/list tie-break).

Keyed slots make contradictory duplicates structurally impossible for anything you key. The residual limit: keyless facts can still accumulate rivals, and a key can't be renamed in v1 (delete + re-add). Deeper history/supersession is Future Work.

**JSON envelope (uniform across commands):**

Every command prints `{"version":1, ..., "entries":[...]}`. `search`/`list` add paging fields; `add`/`update`/`delete` add `action`.

```json
{
  "version": 1,
  "offset": 0,          // search/list only
  "limit": 20,          // search/list only
  "count": 1,           // entries returned in this response
  "total": 47,          // search/list only: matches ignoring limit/offset
  "action": "created",  // add/update/delete only
  "entries": [
    {
      "id": 42,
      "key": "pref:editor",
      "body": "full text…",
      "tags": ["project", "decision"],
      "source": "human",
      "created_at": "2026-07-20T12:00:00.000Z",
      "updated_at": "2026-07-20T15:00:00.456Z"
    }
  ]
}
```

- `add`/`update`/`get`/`delete`: `count` = 1, single entry, no paging fields; `delete` echoes the row as it was before deletion.
- `search`/`list`: `count` = rows on this page, `total` = full match count; `offset + count < total` means more pages exist.
- `key` is the entry's slot name, or `null` for keyless entries.
- Missing id/key on `get`/`update`/`delete`: exit 2, message on stderr, no/empty stdout (do not print an empty envelope).

**Preview (human mode):** first line, max **80 codepoints**, truncated on a codepoint boundary (never split a multibyte sequence), ellipsis if cut.

**Human columns:** `id | key | tags | preview | updated_at` (key column blank for keyless entries).

### Configuration resolution

```
db_path = --db if set
       else getenv("REMEMBER_DB") if set
       else ~/.remember/remember.db
```

On first use: `mkdir -p ~/.remember` (mode 0700), create DB, apply schema, `PRAGMA foreign_keys=ON`, `PRAGMA busy_timeout=5000`, `PRAGMA user_version=1`. Default rollback journal (no WAL): fewer sidecar files, and it avoids WAL's shared-memory requirement, which breaks on network/synced volumes.

**Schema version:** on open, read `PRAGMA user_version`: `0` → fresh DB, create schema + set to `1`; `1` → proceed; `> 1` → refuse with "database is newer than this remember" (exit 1). No migrations planned; the hook exists for the future.

**Storage location:** the DB must live on a local disk. Any file-sync layer (iCloud, Dropbox, Google Drive) can copy the db and its journal out from under an open connection and corrupt it, in any journal mode. Default `~/.remember` is local. If the resolved `--db`/`REMEMBER_DB` path contains a known sync marker (`com~apple~CloudDocs`, `Dropbox`, `Google Drive`), print a one-line stderr warning and proceed — heuristic, not a real filesystem-type probe.

### Module layout (C)

```text
remember/
  CMakeLists.txt
  third_party/sqlite/   # amalgamation sqlite3.c/h (version pinned)
  third_party/sha256/   # tiny sha256
  src/
    main.c
    cli.c / cli.h
    db.c / db.h
    commands.c / commands.h
    normalize.c / normalize.h
    output.c / output.h
    util.c / util.h
  tests/
  skills/remember/SKILL.md
  design-logs/
  README.md
```

### Agent skill (v1 content requirements)

`skills/remember/SKILL.md` must teach:

1. **What it is** — personal durable memory, not chat log.
2. **When to write** — durable facts/decisions/prefs/corrections for future sessions.
3. **When not to write** — secrets, tokens, passwords, transient todos, huge logs, full file dumps.
4. **When to search** — before re-asking prefs/decisions; session start with relevant tags.
5. **Commands** — exact CLI with `--json` examples.
6. **IDs and keys** — cite ids; address by id or `--key`; never invent either. Supply exactly one of {id, `--key`} to get/update/delete.
7. **Tags on update** — omit `--tag`/`--clear-tags` to leave tags alone; `--tag a --tag b` to replace; `--clear-tags` to clear. Never assume omit-clears.
8. **Bodies on update** — only via `--text`; trimmed; empty rejected.
9. **Source** — agents pass `--source agent` on add only.
10. **Exit codes** — 0 / 1 / 2.
11. **DB** — default path; tests use `--db`. DB must live on a local disk — steer users off iCloud/Dropbox/network paths (corruption risk).
12. **Evolving a fact — use `--key`** — for anything that changes over time, `add --key pref:editor "value"`; re-writing the same key replaces the value in place (same id, tags unioned). Read/correct/remove by key: `get --key`, `update --key`, `delete --key`. Keyed slots make duplicate rivals impossible; keyless facts still resolve by recency.
13. **Paging** — `search`/`list` return `count`/`total`; if `total` exceeds what you received, page with `--offset` or raise `--limit` before concluding "nothing else exists".

### Data flow

```text
  argv/stdin
      │
      ▼
   cli_parse → Command
      │
      ▼
   db_open (path resolve, migrate)
      │
      ├─ add    → trim → hash → keyed upsert OR keyless insert/merge → print id
      ├─ search → FTS + filters (--tag/--key/--source) → output
      ├─ list   → filters (--tag/--key/--source) → output
      ├─ get    → by id or --key → output
      ├─ update → locate id/--key → optional --text / optional tag op → print id
      └─ delete → by id or --key → ack
```

### Build & quality bar (strict C)

**Compiler (all builds):**

```text
-std=c11 -Wall -Wextra -Werror -pedantic
```

Additional strictness where supported (Clang/GCC):  
`-Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wwrite-strings`  
(Enable as many as we can without false-positive noise on amalgamation — amalgamation compiled as a separate TU with relaxed flags if needed.)

**Debug:** `-g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined`  
(macOS: no LeakSanitizer; Linux may add leak.)

**Release:** `-O2 -DNDEBUG` (no sanitizers).

**Static analysis / lint suite** (CMake target `lint`, script patterned on MazeMaker `lint-all.sh`):

| Tool | Role |
|------|------|
| `clang-tidy` | Primary linter / bugprone / cert / readability checks on `src/**` |
| `cppcheck` | Additional defect detection (`--enable=all`, suppress third_party) |
| `scan-build` | Clang static analyzer full build |
| `splint` | Optional if installed; weak/posix profile |
| `clang-format` | Format check / `format` target |

**Rules:**

- Project `src/` must be clean under the lint suite (zero errors).
- `third_party/` is not linted to project standards (amalgamation + sha256).
- CI-equivalent local gate: `cmake --build build --target lint` and `ctest` / test binary.
- Prefer `goto cleanup` resource patterns; no ignored return values; bounds-checked strings (`snprintf`, no `strcpy`/`sprintf`/`gets`).

## Implementation Plan

1. **Scaffold** — CMake, amalgamation, `remember --version`, README.
2. **Schema + db layer** — open/create, `user_version` gate, pragmas (`foreign_keys`, `busy_timeout`), config resolution (`--db`/`REMEMBER_DB`/default), `0700` dir, iCloud-path warning, prepared statements.
3. **Normalize + hash** — trim body, **shared tag+key normalization** (casefold, UTF-8/whitespace/control/length rules), SHA-256.
4. **`add` + dedupe merge + tags** — argv/stdin; keyless body-hash path + keyed `--key` upsert (partial unique indexes); first-writer `source`/`created_at`, `updated_at` bump.
5. **`get` / `list` / `delete`** — human + JSON output; id-or-`--key` locators; `--key`/`--source` filters; `updated_at DESC` sort; orphan-tag GC on delete.
6. **FTS5 + `search`** — explicit C sync (also wired into `add`/`update`/`delete`, all in **one transaction**); rank + `--tag`/`--key`/`--source` filters; invalid MATCH → exit 1.
7. **`update`** — id-or-`--key` locate; `--text` / opt-in tags / `--clear-tags`; keyless-only hash conflict reject; always bump `updated_at`; never touch `source`/`key`.
8. **Lint toolchain** — `lint-all.sh`, clang-tidy config, CMake `lint`/`format` targets.
9. **Polish** — exit codes, limit/offset bounds, help text, empty-result success, `--json` error-to-stderr contract.
10. **Tests** — one per Verification Criterion: keyless dedupe/merge, keyed upsert, id-vs-key locators, tag/key normalization + casefold, `source` default/first-writer, orphan-tag GC, **FTS sync after update/delete**, compound-tag recall, invalid-MATCH/empty-result exit codes, stdin + invalid-UTF-8 bodies, preview edges, paging/filters/sort, config precedence, `user_version`, concurrency.
11. **Skill + dogfood** — `SKILL.md`; real use.

## Examples

```bash
# Human store
remember add --tag pref --tag editor --source human \
  "Preferred editor is helix; avoid forcing VS Code settings"

# Agent store (JSON)
remember add --json --source agent --tag decision --tag remember-cli \
  "v1 uses FTS5 only; no embeddings"

# Search
remember search "preferred editor"
remember search --json --tag pref "editor"
remember list --tag decision --limit 5

# Update text only — tags unchanged
remember update 3 --text "Preferred editor is helix (Kakoune second)"

# Update tags only — body unchanged
remember update 3 --tag pref --tag editor --tag helix

# Update both
remember update 3 --text "Preferred editor is helix" --tag pref --tag helix

# Clear all tags only
remember update 3 --clear-tags

# Page through results
remember search --limit 20 --offset 20 "editor"

# Key-value slot: write, then replace the value in place
remember add --key pref:editor "Preferred editor is helix"
remember add --key pref:editor "Preferred editor is zed"    # replaces value, same id
remember get --key pref:editor
remember update --key pref:editor --tag pref --tag editor   # retag the slot by name
remember delete --key pref:editor

# Trim: these dedupe to the same entry
remember add "  hello  "
remember add "hello"          # merged

remember delete 3
```

## Trade-offs

- **Chosen: C + SQLite amalgamation**  
  - Pros: control, portable build, no runtime, craft.  
  - Cons: more code than a scripting-language CLI.  
  - Rationale: language preference and fit for a systems-local tool.

- **Chosen: Subcommands over bare-string add**  
  - Pros: unambiguous, extensible.  
  - Cons: slightly more typing than original sketch.  
  - Rationale: agents and help text stay consistent.

- **Chosen: FTS + separate `--tag` filters (not OR-union)**  
  - Pros: explainable, composable.  
  - Cons: differs from first intuition “tagged OR contains”.  
  - Rationale: original OR-union is hard to rank and teach.

- **Chosen: Standalone FTS over external-content (Round 7)**  
  - Pros: FTS is self-contained; hand-sync in C is simple (`INSERT`/`UPDATE`/`DELETE` by rowid), no old-value bookkeeping.  
  - Cons: body stored twice (entries + FTS) — a few MB at realistic scale.  
  - Rationale: external-content would need a `tags_text` column on `entries` plus FTS's `'delete'`-with-old-values contract on every mutation — fragile to hand-sync, corrupts the index if the old values are wrong. Not worth the storage saving for a personal DB.

- **Chosen: Trim-then-hash dedupe with tag merge on add**  
  - Pros: idempotent retries; ignores accidental padding whitespace.  
  - Cons: cannot store two bodies that differ only by surrounding whitespace (acceptable).  
  - Rationale: fixes near-dupe smell without fuzzy matching.

- **Chosen: `update` with explicit `--text` + opt-in tags + `--clear-tags` (Round 6–7)**  
  - Pros: text-only update cannot wipe tags; tag-only update needs no body; clear is an explicit flag with no parsing ambiguity.  
  - Cons: slightly more verbose than positional body; one extra flag.  
  - Rationale: accidental tag wipe is worse than extra flags.

- **Chosen: `--key` key-value slots with partial-unique identity (Round 8)**  
  - Pros: evolving a fact is `add --key K` again — schema-enforced single slot, no search-first discipline, no rival duplicates; addressable by name.  
  - Cons: a second identity axis; keyed entries skip body-dedupe (two keyed slots may hold identical text); key can't be renamed in v1.  
  - Rationale: contradictory duplicates were the core second-brain risk; a KV slot removes it mechanically for anything keyed, at the cost of one nullable column + two partial indexes.

- **Chosen: Integer IDs; first-writer `source`**  
  - Pros: short CLI; simple metadata.  
  - Cons: not multi-device merge-friendly; weak audit.  
  - Rationale: single-user v1.

- **Chosen: ASCII-only casefold for tags**  
  - Pros: no ICU.  
  - Cons: `É` vs `é` not merged.  
  - Rationale: honest pure-C limitation.

## Verification Criteria

1. Fresh install: `add` creates `~/.remember/remember.db` and returns id `1`.
2. Trim dedupe: `"  x  "` and `"x"` merge to one id; tags union on second add.
3. `search` finds by body token and by tag token via FTS; `--tag` narrows with AND.
4. `list --tag a --tag b` returns only entries having both tags.
5. `get` / `delete` / `update` missing id → exit 2; bad usage → exit 1.
6. `update --text` to another entry’s trimmed text → exit 1; conflicting id on stderr; no data loss.
7. `update 1 --text "body"` leaves tags unchanged; `update 1 --tag a --tag b` sets tags `{a,b}` only; `update 1 --clear-tags` clears tags; `update 1 --tag a --clear-tags` → usage error; `update 1` with no change flag → usage error; `update 1 --text "<current body>"` → exit 0 and `updated_at` **bumped** (writing is a touch).
8. Body empty after trim rejected; search with empty query rejected; body > 64 KiB rejected.
9. `--json` search/list include full bodies in versioned envelope with `count`/`total`; `total` reflects matches beyond `--limit`; `--offset` pages; human mode previews ≤80 codepoints.
10. `--db` isolation works for tests.
11. Concurrent inserts: no corruption (busy timeout).
12. Skill is sufficient for an agent to add/search/update without reading C sources.
13. `cmake --build build --target lint` clean on `src/`; Debug ASan/UBSan clean on tests.
14. Fresh DB gets `user_version=1`; opening a DB with `user_version>1` is refused (exit 1).
15. All `--json` outputs share the `{"version":1,...,"entries":[...]}` envelope; `add`/`update`/`delete` return the affected entry; `delete` echoes the pre-delete row.
16. `--db` pointing at a path containing `com~apple~CloudDocs` prints a stderr warning and still works.
17. `add --key k "a"` creates; `add --key k "b"` replaces the body on the same id and unions tags (`action:"updated"`); `get --key k` returns it.
18. Keyed writes skip body-hash dedupe: `add --key k1 "x"` and `add --key k2 "x"` yield two entries; a keyless `add "x"` still dedupes among keyless entries only.
19. `get`/`update`/`delete` with `--key` on a missing key → exit 2; supplying both `ID` and `--key`, or neither → usage error.
20. `update` on a keyed entry to a body already stored elsewhere does **not** conflict; on a keyless entry it still conflicts (exit 1).
21. **Tag/key normalization** — `--tag Foo` and `--tag foo` map to one tag (ASCII casefold merge); a tag/key containing ASCII whitespace, a control char, > 64 bytes, or invalid UTF-8 → exit 1; `pref:editor` is stored verbatim.
22. **`source`** — default `unknown` when `--source` omitted; `--source` on any command other than `add` → usage error; keyless merge and keyed upsert **keep the original `source` and `created_at`**, changing only `updated_at`.
23. **`update` invariants** — a successful `update` never alters `id`, `key`, `source`, or `created_at`, and always refreshes `updated_at`.
24. **Orphan-tag GC** — deleting (or retagging away) the last entry using a tag removes that tag row; a tag still used by another entry survives.
25. **FTS sync correctness** — after `update`ing a body, `search` finds the new text and **no longer** finds the old; after `delete`, `search` no longer returns the entry; after a keyed upsert, search reflects the new value only. (Guards the hand-rolled C sync.)
26. **Compound-tag recall** — an entry tagged `pref:editor` is returned by free-text `search "editor"` (validates the default tokenizer, Round 7/8); `remove_diacritics` folds `café` ↔ `cafe`.
27. **Invalid FTS query** — malformed MATCH (e.g. unbalanced quote) → exit 1 with a clear error, not a crash.
28. **Empty results are success** — `search`/`list` with no matches → exit 0 and `{"version":1,…,"entries":[],"total":0}` (never exit 2).
29. **stdin bodies** — `add -` and `update <loc> --text -` read the body from stdin; trim/empty/UTF-8/size rules apply identically.
30. **Invalid UTF-8 body** → exit 1.
31. **Filters & sort** — `list`/`search` honor `--key` (exact) and `--source` filters; `list` returns `updated_at DESC`; `--limit < 1` → usage error, `--limit` capped at 1000, `--offset < 0` → usage error.
32. **Preview edges** — a multi-line body previews only the first line; an 80-codepoint cut lands on a codepoint boundary (never splits a multibyte sequence); `key` renders `null`/blank for keyless entries.
33. **Config precedence** — `--db` > `REMEMBER_DB` > default; fresh DB dir created mode `0700`.
34. **Errors under `--json`** — diagnostics always go to stderr as plain text with the exit code; stdout stays empty (no JSON error envelope). Agents branch on exit code, then read stderr.

## Known Limitations

- No semantic/embedding search.
- No multi-word tags.
- Incomplete Unicode casefolding.
- No encryption; DB is plaintext under `~/.remember` (dir mode 0700).
- No project-auto DB discovery.
- FTS5 query syntax must be documented in the skill.
- Keyed slots prevent duplicates for keyed facts; **keyless** facts can still coexist/contradict — resolution is by recency. No supersession/history.
- A `--key` cannot be renamed or cleared, and a keyless entry cannot be promoted to keyed, in v1 (delete + re-add).
- Distinct keyed slots may share identical body text — **by design**, so keys can hold common values like `0`, `true`, or a repeated string (keyed writes skip body-hash dedupe).
- DB must be on a local disk; sync/network volumes (iCloud, Dropbox) risk corruption. Warned heuristically, not enforced.
- `source` is informational only.
- Amalgamation not under project lint rules.

## Future Work

**Fact evolution (`--key` shipped in v1; deeper mechanisms later):**

- **Rekey / promote** — rename a slot's `--key`, or attach a `--key` to an existing keyless entry, without delete + re-add.
- **Explicit supersession** — `add --supersedes ID` (or `--replaces ID`): create the new entry, mark the old one superseded (a `superseded_by` column / soft tombstone), carry tags forward. `search`/`list` hide superseded rows by default; `--include-superseded` to audit. History without losing recall.
- **Staleness / validity** — optional `valid_until` or a `stale` flag so time-bound facts self-demote.

**Other:**

- Project-scoped stores (`--project` / git-root discovery)
- `export` / `import`
- Optional incremental tag flags if tag-only edits become painful
- Full Unicode casefold via optional lib
- Shell completions
- Optional secret heuristics
- `remember gc` / vacuum
