# 005 - Local sync foundations (`sync_id`, soft-delete, merge, VV)

**Status:** Design (finalized — immutable once implementation starts)
**Author:** Itay (+ agent facilitation)
**Created:** 2026-09-15
**Finalized:** 2026-09-20 (grill Rounds 1–6 + API governance pass)
**Amended:** 2026-09-20 (Round 7 — pre-implementation review locks)
**Depends on:** [`001-foundations`](001-foundations.md), [`003-ttl`](003-ttl.md),
[`004-related-memories`](004-related-memories.md), [`002-core-facade`](002-core-facade.md)
**Product sources:** Fortkeeps handoff R1; MVP E1 / Technical Roadmap Phase 0;
remember-mac `007-browse-list-detail` (L3 / 15f blocked on CLI `sync_id` links)
**Locked slot:** `decision:sync-foundations`

Cross-refs: Mac pin after this CLI is green; Cloud network/auth (E2+) and Mac
15f UI are **out of this log**.

## Background

CLI schema is `user_version` **3**: entries with optional `expires_at` (virtual
TTL trash per 003), `entry_links` id FKs (004). Public locators are positional
`id` | `--key`. JSON stubs are `{id,key,type,trash,preview}`. User `delete` is
hard CASCADE. There is no durable public identity, no soft-delete, no merge,
no device/concurrency metadata.

Fortkeeps / Mac Browse L3 needs portable link identity (`sync_id`). E1 / Phase 0
needs local foundations for later multi-device sync without forcing anyone
online. Installed CLI users and RememberKit (soft `sync_id` decode already)
must keep working offline.

## Problem

Need one coherent CLI design that:

1. Gives every entry an immutable global `sync_id` (UUID v7).
2. Exposes `sync_id` on JSON entries and link stubs; addresses locators by
   id | key | `sync_id` so the CLI stands alone (Mac kit is not required).
3. Soft-deletes for syncable undo (separate from TTL expiry).
4. Merges another brain DB by `sync_id` with conflict records (no whole-import
   fail; no silent data drop).
5. Mints a per-brain-folder `device_id` (sidecar) and stores version vectors
   toward multi-device sync and single-device purge gating.
6. Updates skill/docs; keeps `remember_run` byte-identical to CLI `--json`.

Not Cloud, not Fortkeeps UI copy, not renaming link kinds, not `quota_exceeded`.

## Questions and Answers

### Round 1 — highest-risk forks (Itay, 2026-09-20)

- **Q: `sync_id` generation + import?**
  A: Mint on create; **never** change on update. Import/merge **preserves**
  foreign `sync_id`s (local `add` does not take a caller-chosen id).

- **Q: Expose `sync_id` on local JSON?**
  A: **Yes** (open platform). Humans keep short ids; machine contract always
  includes `sync_id` on entries and stubs. (Refined through Rounds 2–4.)

- **Q: Link addressing?**
  A: CLI must support `--sync-id` locators (Round 4 flipped from “kit-only
  bridge”). Storage may keep id FKs; sync/merge remap via `sync_id`.

- **Q: Soft-delete vs defer?**
  A: **Decide now** — do not defer. Expiry ≠ deletion. Syncable delete required.

- **Q: JSON export as backup?**
  A: **No** required dump-export. File-copy of the DB remains backup; product
  need is **`import --from-db` merge**.

### Round 2 — pushbacks (Itay, 2026-09-20)

- **Q: UUID clash vs snowflake?**
  A: Random UUID collision is not the threat. Real issues are duplicate
  creates (two `sync_id`s for “same” fact) and needing **device identity** for
  version vectors. Snowflake ≠ VV.

- **Q: Hide `sync_id` from day-to-day UX?**
  A: Human/agent **prefer** id/`--key`. JSON still exposes `sync_id`. After
  merge, local ids change; stubs without `sync_id` break portable citation.

- **Q: Store links by `sync_id`?**
  A: **Keep** `entry_links` id FKs (004). Merge/sync translate via `sync_id`.

- **Q: Soft-delete shape?**
  A: Deleted bin with body kept for undo; orthogonal to TTL. Hard CASCADE
  user-delete goes away.

- **Q: Merge conflicts?**
  A: Never auto-delete on conflict; compare content; user decides; do not fail
  the whole merge.

- **Q: Scope?**
  A: **CLI only** this log.

### Round 3 — sync end-goal (Itay, 2026-09-20)

- **Q: Identity stack?**
  A: **Device id now** + immutable `sync_id` + **version vector** per entry.
  Sync-by-generation / ack watermarks enable future purge-when-everywhere;
  this log gates hard purge on `devices` count == 1 until then.

- **Q: UUID v7 vs ULID?**
  A: Same job (time-sortable unique id). **UUID v7** chosen (Round 4) for
  Swift/UUID/Cloud interop. ULID rejected for this design.

- **Q: Bins?**
  A: Soft-delete sets `deleted_at`; recoverable; filtered from default
  list/search/links. Expired and Deleted are **separate**. Expire-then-delete
  moves to Deleted (clear `expires_at`).

- **Q: Conflict store?**
  A: Side table + accept command (drilled in Round 4).

- **Q: CLI `--sync-id` locator?**
  A: **Yes** — CLI works without Mac/Cloud services on top.

### Round 4 — bins, purge, conflicts, format (Itay, 2026-09-20)

- **Q: `sync_id` format?** A: **UUID v7**.
- **Q: Bin flags?** A: Orthogonal `--expired` and `--deleted`; default = live only.
- **Q: Expire then delete?** A: Set `deleted_at`, **clear** `expires_at`.
- **Q: Edges + deleted neighbors?** A: Edges **survive**. Default stubs **omit**
  deleted neighbors; `--deleted` includes them with `"bin":"deleted"`.
- **Q: Stub flag naming?** A: `"bin"` enum (governance later locked to
  `live|expired|deleted`, never null). Drop `"trash"`.
- **Q: Hard purge?** A: Only if exactly one registered device; else refuse.
- **Q: Conflicts?** A: Side table; `conflict accept --keep local|incoming|both`;
  identical content auto-merges (no human).
- **Q: Locators?** A: id | `--key` | `--sync-id` (and graph `--from-*` / `--to-*`).

### Round 5 — implementer forks (Itay, 2026-09-20)

- **Q: `device_id` location?** A: **Outside** SQLite (sidecar); import never
  copies device identity. (Path refined Round 6.)
- **Q: VV storage?** A: `TEXT` JSON column `version_vector` on `entries`.
- **Q: Soft-deleted + add same key/body?** A: **Revive** (same `sync_id`), like TTL.
- **Q: Purge surface?** A: One `purge` with `--expired` | `--deleted`.
- **Q: link/unlink/rekey bump VV?** A: **Yes** on affected entries.

### Round 6 — last forks (Itay, 2026-09-20)

- **Q: Sidecar path?** A: `dirname(db_path)/device_id` (beside the DB file).
- **Q: JSON always include?** A: `sync_id`, `deleted_at`, `bin`, `version_vector`
  on every entry object.
- **Q: Undelete / conflict verbs?** A: `update --undelete`; `conflicts`;
  `conflict accept --id N --keep local|incoming|both`.
- **Q: Device registry?** A: Table `devices(device_id, first_seen, last_seen)`;
  local registered on open; import registers foreign ids from VVs; purge iff
  `COUNT(*)==1`.

### API governance pass (Itay, 2026-09-20)

- **Q: `bin` null for live?** A: **Forbidden.** `bin` is always
  `"live" | "expired" | "deleted"`. Agents and RememberKit must not special-case
  null.
- **Q: `--trash`?** A: Alias of `--expired`; one stderr deprecation per
  invocation. Does **not** map to deleted.
- **Q: `purge-trash`?** A: Alias of `purge --expired`; same deprecation pattern.
- **Q: Exit 3 tokens?** A: Machine-stable one token:
  `expired` | `deleted` | `not_expired` | `not_deleted`.
  Meaning: wrong bin. Do not reuse bare `expired` for other soft failures.
  (Supersedes 003’s `not_in_trash` for the expired direction’s “looked in
  expired, row is live” case → `not_expired`. Soft-delete adds `deleted` /
  `not_deleted`.)
- **Q: JSON field order?** A: `id`, `sync_id`, `key`, `body`, `tags`, `source`,
  `created_at`, `updated_at`, `expires_at`, `deleted_at`, `bin`,
  `version_vector`, then `links` when present.
- **Q: Link kinds?** A: **Unchanged** — stored `related` | `cites` | `supersedes`;
  output 5-name vocabulary. Do not “clean” or rename.
- **Q: Human UI vocabulary?** A: Humans see Live / Expired / Deleted — not
  `bin`, `version_vector`, or UUID soup. VV is sync machinery.
- **Q: Fortkeeps Delete?** A: Reversible until purge. Purge is advanced
  single-device; Mac copy later, not this log.
- **Q: Out of scope names?** A: `quota_exceeded`, Cloud `remember_*` tools —
  leave alone.

### Round 7 — pre-implementation review (Itay, 2026-09-20)

Closes forks that would force the implementer to invent (plan 14 forbids
that). Rounds 1–6 still hold except where a bullet **supersedes**.

- **Q: Sidecar path?** *(supersedes Round 6 `dirname(db)/device_id`)*
  A: **Per-file** `<db-path>.device_id` (example: `~/.remember/remember.db`
  → `~/.remember/remember.db.device_id`). One line, UUID, mode `0600`.
  Two DBs in one folder do not share identity. Copying only the `.db`
  does not copy identity. Document: do not copy the sidecar with the DB
  if the copy is meant to be a different replica.

- **Q: Does import register foreign device ids?** *(supersedes Round 6
  “import registers foreign ids from VVs”)*
  A: **No.** `devices` is **local-only**, at most one row: this process’s
  sidecar id. Import merges entries and version vectors but does **not**
  INSERT foreign device ids. Purge hatch `COUNT(*)==1` stays true after
  a backup merge. Cloud/multi-device registration is a later log.

- **Q: Lost sidecar?**
  A: Recover, do not remint if `devices` already has the one local row
  — rewrite the sidecar file from that row. Sidecar present + empty
  `devices` → insert from sidecar. Both missing → mint sidecar and
  insert. Sidecar vs `devices` mismatch → **sidecar wins**, replace the
  single `devices` row (folder identity). Never leave COUNT > 1 from
  local recovery.

- **Q: `--keep both` vs same `sync_id`?**
  A: **Remint incoming.** `--keep both` always keeps the local row
  unchanged and materializes incoming as a **separate** row:
  - `concurrent_vv`: mint a **new** `sync_id` for the incoming copy
    (local keeps S). The new row is a new identity (`{local:1}` VV).
  - `key_clash` / `hash_clash`: keep incoming’s own `sync_id`; make it
    keyless if the key/hash unique slot is taken.
  `concurrent_vv` + `--keep both` is allowed (not usage 1).

- **Q: Unique key/body_hash vs deleted?**
  A: **Deleted occupies the slot.** Keep `ux_entries_key` /
  `ux_entries_bodyhash` as they are (not partial on `deleted_at`).
  `add` of the same key / keyless hash **revives** (same `sync_id`),
  like TTL. Import of a different `sync_id` that would violate unique
  → `conflicts` row (`key_clash` or `hash_clash`), never a raw SQLITE
  constraint error.

- **Q: One-row hard delete?** *(supersedes “delete is always soft” as
  the only forget path; 003 `delete --trash` preserved)*
  A:
  - Plain `delete` **soft-deletes**. Locator for that verb is
    **live or expired** (not deleted): expired → deleted (clear
    `expires_at`) so expire-then-delete still holds. Deleted without
    `--deleted` → exit 3 `deleted`.
  - `delete --expired` / `delete --deleted`: **hard CASCADE** that one
    row (003 analog). `--trash` on delete aliases `--expired` (one-row
    hard wipe of expired).
  - `get` / `update` / `rekey` / list / search / tags default locators
    stay **live only** (005 Round 4). Only `delete`’s unflagged locator
    includes expired.
  - Every **hard** wipe (one-row or `purge`) is allowed iff
    `devices COUNT==1`; else exit 1. Same hatch.

- **Q: Exit 3 three-bin matrix?**
  A: Token names the **row’s bin** when you looked in live (or, for
  unflagged `delete`, live+expired) and missed; token names the
  **mistaken flag** (`not_expired` / `not_deleted`) when you passed a
  bin flag and the row is live. When two bins are both “wrong”, prefer
  the row’s actual bin (`expired` / `deleted`).

  | Looked in | Row live | Row expired | Row deleted |
  |-----------|----------|-------------|-------------|
  | live (default)* | ok | `expired` | `deleted` |
  | `--expired` | `not_expired` | ok | `deleted` |
  | `--deleted` | `not_deleted` | `expired` | ok |

  \*Unflagged `delete` treats expired as ok (soft-delete). Other
  unflagged locators still miss expired → `expired`.

- **Q: VV compare / identical content?**
  A: Missing device key = 0. **Dominates** iff every counter ≥ and at
  least one >. Incoming dominates → apply incoming
  body/tags/key/`expires_at`/`deleted_at` onto the local row (same
  `sync_id`); `created_at` unchanged; `updated_at` = command `now`;
  VV = pairwise max then bump local. Local dominates → skip apply;
  still pairwise-max VV so a re-import does not loop. Concurrent +
  identical **body+tags+key+delete state** (005 Round 4; not
  `expires_at`/`source`/links) → auto-merge VV max, no conflict.
  Concurrent + different → `conflicts` reason `concurrent_vv`.
  Accept always unions incoming VV into the surviving row(s) so
  re-import is stable.

- **Q: Import source open / v3 / envelope?**
  A: Open the foreign path **read-only** inside the adapter. Never
  mint or write the source sidecar; never `store_open` the source as
  a normal brain. `user_version` < 4 → exit 1
  (`source database is older than this remember`). JSON (no entry
  dump): `action:"imported"`, `inserted`, `updated`, `unchanged`,
  `conflicts` (count). Exit 0 even when `conflicts` > 0. Agents run
  `conflicts` next.

- **Q: `store_get_any` vs deleted?**
  A: Graph default resolvers **exclude** deleted (live+expired only,
  004 either-bin for TTL). Deleted ends need `--deleted` (like
  update). Today’s no-bin `store_get_any` must start filtering.

- **Q: `devices.last_seen` on every open?**
  A: **No write on read-only commands.** Insert-if-absent (and
  sidecar recovery) may write. Do not `UPDATE last_seen` on `get` /
  `list` / `search`.

- **Q: UUID mint home / format?**
  A: Mint only in the store adapter (sqlite stays out of other TUs).
  Canonical string: lowercase 8-4-4-4-12. Time from `timespec_get`
  (same clock family as `utc_now`); entropy via `sqlite3_randomness`.
  Device sidecar may use the same generator.

- **Q: Conflict `reason`?**
  A: Closed enum: `concurrent_vv` | `key_clash` | `hash_clash`.
  Unknown on disk → store error (fail fast), not a free string.

- **Q: FTS on soft-delete?**
  A: Keep the FTS row. Bin filters live in SQL (same as expiry).
  Hard wipe removes FTS (today’s delete path).

- **Q: Mac pin vs import?**
  A: One plan 14. Mac/kit pin is a **follow-up** after this CLI is
  green; stages 1–4 (`sync_id` on stubs + locators + bins) are what
  15f needs. Stage 5 import does not block writing those stages.

## Design

### Schema (`user_version` 3 → **4**)

New DBs create at 4. Open: `0`→create@4; `1`/`2`/`3`→migrate to 4; `>4`→refuse
(`database is newer than this remember`).

```sql
-- entries: additive columns (migrate v3→v4 in one txn)
-- sync_id TEXT NOT NULL UNIQUE  -- UUID v7 canonical string
-- deleted_at TEXT              -- NULL = not soft-deleted; else ISO .mmmZ
-- version_vector TEXT NOT NULL -- JSON object {"<device_uuid>": counter, ...}

CREATE TABLE devices (
  device_id  TEXT PRIMARY KEY,
  first_seen TEXT NOT NULL,
  last_seen  TEXT NOT NULL
);

CREATE TABLE conflicts (
  id            INTEGER PRIMARY KEY,
  sync_id       TEXT NOT NULL,
  reason        TEXT NOT NULL,  -- e.g. concurrent_vv | key_clash
  local_json    TEXT NOT NULL,  -- snapshot
  incoming_json TEXT NOT NULL,
  created_at    TEXT NOT NULL
);
```

Migration v3→4: for each entry mint UUID v7 `sync_id`; set `deleted_at` NULL;
set `version_vector` to `{local_device: 1}` (valid JSON object, never `{}`
after migrate); create empty `devices` / `conflicts`; register local device
from the sidecar on first open after migrate (insert-if-absent).

**Sidecar (not in SQLite):** `<db-path>.device_id` — one line, lowercase
UUID, mode `0600`. Mint if missing **and** `devices` is empty; else recover
per Round 7. Import **never** reads/writes the foreign sidecar.

`entry_links` unchanged (id FKs, CASCADE on **hard** wipe only). Soft-delete
does **not** CASCADE.

### Bins

| `bin` value | Predicate |
|-------------|-----------|
| `live` | `deleted_at IS NULL` AND (expires null OR `expires_at > now`) |
| `expired` | `deleted_at IS NULL` AND expires set AND `expires_at <= now` |
| `deleted` | `deleted_at IS NOT NULL` |

Default list/search/get/tags/update/rekey locators: **live only**.
Unflagged `delete`: **live or expired** (soft-delete). `--expired` /
`--deleted`: that bin only. Mutex with each other. `--trash` →
`--expired` + one stderr deprecation line per invocation.

Wrong bin → exit **3**, stderr `remember: <token>\n` with token one of
`expired` | `deleted` | `not_expired` | `not_deleted` (Round 7 matrix).

Graph `link`/`unlink`/`related`: resolve locators across live+expired (004
either-bin for expiry). Default graph resolvers **exclude** deleted.
Deleted ends need `--deleted` (like update). Default `related` omits
deleted neighbors; expired neighbors still listed.

### Locators

Exactly one of: positional `ID` | `--key KEY` | `--sync-id UUID`.
Graph: exactly one form per end (`--from` / `--from-key` / `--from-sync-id`
and `--to` / `--to-key` / `--to-sync-id`). Sugar `link ID ID` unchanged
(numeric ids only).

### Commands

**Changed**

- `delete` (no bin flag) → **soft-delete** live or expired: set
  `deleted_at=now`, clear `expires_at`, keep body/tags/key/`sync_id`/edges,
  bump VV. JSON `action:"deleted"` with snapshot `"bin":"deleted"`.
- `delete --expired` / `delete --deleted` → **hard CASCADE** that one row
  (003 `delete --trash` analog). `--trash` on delete aliases `--expired`.
  Hard wipe iff `devices COUNT==1`.
- `update --undelete` — clear `deleted_at` (locator needs `--deleted`);
  mutex with `--ttl`/`--expires`/`--clear-expires`. Bump VV.
- `add` — revive soft-deleted same key / keyless body-hash (same `sync_id`).
- `list`/`search`/`get`/`tags`/`related` — bin filters; JSON fields below.
- `link`/`unlink`/`rekey` — `--*-sync-id`; bump VV on affected entries.
- `purge-trash` → alias of `purge --expired` + deprecation stderr.

**New**

```text
remember purge --expired|--deleted
remember import --from-db PATH
remember conflicts
remember conflict accept --id N --keep local|incoming|both
```

- `purge`: exactly one of `--expired`|`--deleted`. Hard-wipe that **whole
  bin**. Allowed iff `SELECT COUNT(*) FROM devices` == 1; else exit 1 with
  a clear ASCII message. No prompt.
- `import --from-db`: merge by `sync_id` (Round 7 VV rules). Unique
  clashes (`key` or keyless `body_hash`, any bins) → `conflicts` row,
  continue. Remap links via `sync_id` after entries (skip edges whose
  ends are not both present locally). **Do not** register foreign device
  ids. Never copy sidecar. Source opened read-only; v3 → exit 1.
- `conflicts` / `conflict accept`: `--keep local|incoming|both`.
  `both` keeps local and inserts incoming as a second row (Round 7:
  remint `sync_id` on `concurrent_vv`; preserve incoming `sync_id` on
  clash; keyless if unique taken). Accept unions incoming VV into
  survivors. `reason` is `concurrent_vv` | `key_clash` | `hash_clash`.

### JSON contract (`version` stays 1)

Entry field order (goldens):

```text
id, sync_id, key, body, tags, source,
created_at, updated_at, expires_at, deleted_at,
bin, version_vector [, links]
```

- `bin`: always `"live"|"expired"|"deleted"` (never null, never omit).
- `deleted_at`: always present (`null` or ISO `.mmmZ`).
- `version_vector`: always a JSON object.
- `links`: list/search/get only (and `related` envelope), after
  `version_vector`.

Stub shape:

```json
{
  "id": 64,
  "sync_id": "018f…",
  "key": null,
  "type": "related",
  "bin": "live",
  "preview": "…"
}
```

No `trash` field. Default omit neighbors with `bin=deleted`; with
`--deleted`, include them (`"bin":"deleted"`). Expired neighbors default
**shown** with `"bin":"expired"` (004 edges-survive-expiry; human
`[expired]` instead of `[trash]`).

### Human output

- Do not print `bin`, `version_vector`, or raw UUID soup in normal human
  lines. Related column: neighbor ids; suffix `[expired]` / `[deleted]` when
  shown. Labels conceptually Live / Expired / Deleted.
- `get`/`related` Related: block: same; drop `[trash]` wording.

### Link kinds (explicit confirm)

Stored `--kind` / DB: `related` | `cites` | `supersedes` only.
Output stub `type`: `related` | `cites` | `cited_by` | `supersedes` |
`superseded_by`. **Unchanged from 004.** Do not rename or expand in this work.

### Skill

- Prefer `--key` for portable named facts; `--sync-id` for keyless across
  machines; local `id` is one-device only.
- Soft-delete ≠ expiry; unflagged `delete` does not free space until a
  hard wipe (`delete --expired|--deleted` or `purge`).
- After `import`, run `conflicts` before assuming a clean brain.
- Honor stub `bin`; wrong-bin exit 3 tokens as above.
- Facade / agents: always `--json` when parsing.

### Compatibility

| Consumer | Change |
|----------|--------|
| Scripts using `--trash` / `purge-trash` | Aliases + deprecation stderr. `delete --trash` stays **one-row hard** wipe of expired (003). `purge-trash` stays **whole-bin** expired. |
| Scripts parsing `"trash"` | **Break** → `"bin"` (migration note) |
| RememberKit | Hard-require `sync_id`; decode `bin` / `deleted_at`; may ignore VV in UI |
| `remember_run` | Byte-identical to CLI `--json` |

### Non-goals / Future Work

- Cloud auth, network sync, oplog push/pull (E2+).
- Multi-device purge after ack watermarks (schema prepares via `devices` + VV).
- Mac 15f Add Link UI (separate repo after pin).
- JSON `export` dump command.
- Embedding device id inside `sync_id` (sidecar + VV suffice).
- Fortkeeps purge UX copy.

## Data flow

```
add / update / delete / link / unlink / rekey
  load-or-recover sidecar <db-path>.device_id
  ensure at most one local devices row (insert-if-absent; no last_seen on reads)
  mutate row(s); bump version_vector[local] += 1; bump updated_at

import --from-db OTHER
  open OTHER read-only (no sidecar I/O); refuse user_version < 4
  for each foreign entry by sync_id:
    missing → insert (preserve sync_id); remap later links
    present → Round 7 VV / unique → apply or conflicts row
  never insert foreign devices; never touch local sidecar

delete --expired|--deleted  /  purge --expired|--deleted
  if devices count != 1 → refuse
  hard DELETE those rows (CASCADE links)
```

## Implementation Plan

Derived plan: [`implementation-plans/14-sync-foundations.md`](../implementation-plans/14-sync-foundations.md).

1. Schema v4 + sidecar `<db-path>.device_id` + local-only `devices` + migrate
   mint `sync_id`/VV.
2. JSON/output: field order, `bin` enum, stubs drop `trash`; bin filters +
   exit 3 matrix; `--trash` alias.
3. Soft-delete (unflagged, live+expired) / one-row hard `delete --expired|
   --deleted` / `--undelete` / revive / `purge` + aliases.
4. Locators `--sync-id` (+ graph forms); VV bumps on link/unlink/rekey;
   graph excludes deleted unless `--deleted`.
5. `import --from-db` (no device register) + `conflicts` / `conflict accept`
   (both remints on `concurrent_vv`).
6. Skill/help/docs; facade byte-match; full suite + coverage.

## Examples

Live entry (abbrev):

```json
{
  "id": 12,
  "sync_id": "018f2a3b-…",
  "key": "pref:editor",
  "body": "helix",
  "tags": ["tools"],
  "source": "agent",
  "created_at": "2026-09-20T12:00:00.000Z",
  "updated_at": "2026-09-20T12:00:00.000Z",
  "expires_at": null,
  "deleted_at": null,
  "bin": "live",
  "version_vector": {"a1b2c3d4-…": 2},
  "links": [
    {
      "id": 64,
      "sync_id": "018f2a99-…",
      "key": null,
      "type": "cites",
      "bin": "live",
      "preview": "decision:db…"
    }
  ]
}
```

```bash
remember --json link --from-sync-id 018f… --to-sync-id 018f… --kind cites
remember --json delete --key pref:editor          # soft-delete (live or expired)
remember --json delete --deleted --key pref:editor  # one-row hard wipe
remember --json update --deleted --key pref:editor --undelete
remember --json import --from-db /path/other.db
remember --json conflicts
remember --json conflict accept --id 1 --keep both  # remint if concurrent_vv
remember --json purge --deleted                   # whole-bin; single-device only
```

## Trade-offs

| Choice | Alternative | Why |
|--------|-------------|-----|
| UUID v7 | ULID / v4 / snowflake | Time-sortable + UUID ecosystem; device lives in sidecar/VV |
| id FK links | sync_id FK | Avoid re-graph; merge remaps |
| Soft-delete keep body | Tombstone table + hard delete | Undo + one row for LWW; purge later |
| Merge + conflict table | Fail whole import / LWW silent | Personal brain must not lose data |
| `bin` always enum | null = live | No null special-case for agents/kit |
| Purge iff 1 device | Always purge + tombstone | Honest until ack-GC exists |
| Per-file sidecar | `dirname(db)/device_id` | Two DBs in one folder stay distinct (Round 7) |
| Local-only `devices` | Import registers VV ids | Import must not brick purge (Round 7) |
| `--keep both` remint on `concurrent_vv` | Reject `both` / drop `both` | User chose two facts over one identity |
| No JSON export | Phase 0 dump | Merge-from-db is the real need; file copy backups |

## Verification Criteria

1. Fresh DB `user_version=4`; all entries have unique UUID v7 `sync_id`.
2. v3 file migrates in place; ids/bodies/links preserved; every row gets
   `sync_id` + VV; sidecar created as `<db-path>.device_id`.
3. `sync_id` unchanged across update/rekey/soft-delete/undelete/revive.
4. JSON field order matches governance list; `bin` never null; no `trash`.
5. Locators: id | key | sync-id mutex; graph twins work; facade byte-match.
6. `--trash` / `purge-trash` alias + deprecation stderr; map only to expired.
7. Soft-delete keeps edges; default related omits deleted; `--deleted` shows
   `"bin":"deleted"`.
8. Unflagged `delete` of an expired row clears `expires_at`; `bin` becomes
   `deleted` (soft). `delete --expired` hard-wipes that one expired row.
9. Exit 3 tokens exactly: `expired`|`deleted`|`not_expired`|`not_deleted`
   (Round 7 matrix).
10. Hard wipe (`purge` or one-row `delete --expired|--deleted`) refuses when
    `devices` count ≠ 1; succeeds when == 1. Import does not change COUNT.
11. Import preserves foreign `sync_id`s; remaps links; does not copy sidecar;
    does **not** register foreign devices; v3 source refused; identical
    content auto-merges; concurrent/unique → conflict row; accept
    local|incoming|both (both remints on `concurrent_vv`).
12. link/unlink/rekey bump VV; add/update/delete/undelete bump VV.
13. Link kinds unchanged (3 stored / 5 output).
14. Skill documents key vs sync-id vs id; bins; conflicts; aliases.
15. Full suite + coverage gate green; lint clean.

## Implementation Results

Shipped in plan 14 (branch `plan14-stage1-sync-foundations`, 2026-09-20–22). Schema
`user_version` 4: `sync_id` / `deleted_at` / `version_vector`, local-only `devices`,
`conflicts`, sidecar `<db-path>.device_id`. JSON field order + always `bin` enum;
no `"trash"`. Unflagged `delete` is soft (live or expired); `--expired`/`--deleted`
one-row hard CASCADE iff `devices` COUNT==1. Locators id | `--key` | `--sync-id`.
`import --from-db` read-only, refuse v3, no foreign device register; clash and
`concurrent_vv` upsert by `(sync_id, reason)` so re-import is idempotent.
`conflict accept --keep both` remints on `concurrent_vv`. Skill/help/facade match.
remember-mac kit pin + 15f remain follow-up.

## Review Notes

### 2026-09-20 — strict review (pre-implementation)

Round 7 closed the blocking forks. Design section amended to match.

Grill locks: per-file sidecar; local-only `devices` (import does not
register); `--keep both` remints incoming on `concurrent_vv`; deleted
occupies unique key/hash; unflagged `delete` soft (live+expired);
`delete --expired|--deleted` one-row hard wipe (COUNT==1).

Also locked as implementer contract: exit-3 matrix; VV dominate/max;
identical content = body+tags+key+delete state; import read-only +
refuse v3; import JSON counts; `store_get_any` excludes deleted; no
`last_seen` on reads; UUID v7 mint in adapter; closed `reason` enum;
FTS kept on soft-delete.

Remaining for plan 14 (not product): thicken store-port signatures like
plan 13; kit `LinkStub.trash` break is a pin-bump follow-up.

