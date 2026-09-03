# 004 - Related memories (entry links)

**Status:** Design (finalized — immutable once implementation starts)
**Author:** Itay (+ agent facilitation)
**Created:** 2026-08-31
**Finalized:** 2026-08-31
**Amended:** 2026-08-31 (Round 1 — kinds, trash, output; Round 2 — `related`
is two directed rows `a→b` and `b→a`; **Round 3 — single canonical
`related` row (`min,max`); direction is from/to only, no read-dedup, no
self-heal; output `type` uses the 5-name vocabulary; idempotent `unlink`;
link/unlink bump both endpoints; add `rekey`**; **Round 4 — list/search
related column is IDs only; `related` human = get `Related:` block;
`rekey` promote/demote via `--to-key` / `--clear-key`**; pre-implementation)

Cross-refs: `001-foundations.md` (schema, JSON, locators, skill),
`003-ttl.md` (virtual trash, exit 3, `user_version` 2). Mac UI:
remember-mac `design-logs/004-related-shell.md`. Product: remember-mac
`docs/product-roadmap.md` P7. Locked slot: `decision:p7-related-memories`.

## Background

`entries` is a bag of rows plus tags. Tags are **set membership** (a facet
many items share: `#project:foo`, `#workflow`). There is no pairwise
relation. Agents fake graphs in prose (`See [[decision:ttl-trash]]`,
“this supersedes that”). FTS cannot reconstruct which note *updates*
which keyed slot.

`user_version` is **2** (TTL). Public exits stay **0 / 1 / 2 / 3**.

## Problem

Need an `entry_links` graph that:

1. Is useful to **both** a human and an agent (browse/jump, skill traverse).
2. Is **not** a 2-element tag — a link is an intentional pair.
3. Distinguishes see-also, supersession, and citation.
4. Survives virtual trash and dies on purge.
5. Surfaces neighbors on list/search/get without dumping full neighbor bodies.

Not embeddings, not auto-suggest, not a multi-hop graph view.

## Questions and Answers

### Round 1 — product (Itay, 2026-08-31)

- **Q: Tag vs link?**
  A: **Tag = facet/set** (project, workflow, personal). Members are not
  pairwise connected. **Link = intentional pair.** Link when a future
  agent must traverse A to understand B; tag when many entries share a
  facet. Do not link “same project” or “same session.”

- **Q: Direction and type in v1?**
  A: **Yes — a tiny closed enum**, not a free-string `kind`, not 12 verbs.
  Untyped undirected “related” is a 2-element tag and agents will over-link.

- **Q: v1 kinds?**
  A:
  - `related` — undirected see-also. Default `--kind` (human Mac sheet).
    Shows **one** neighbor, `type: related`. (Round 1/2 stored this as two
    rows; **Round 3 stores a single canonical `(min,max,related)` row** —
    the `from OR to` read already reaches it from either end.)
  - `supersedes` — directed **new → old**. This is current; old stays for
    history.
  - `cites` — directed. This depends on / quotes that; do not duplicate
    the target.
  Agents pass `--kind` when they know; prefer `supersedes`/`cites` over
  bare `related`. No new kinds until two real uses demand them.

- **Q: Cardinality, self-links, cycles?**
  A: Unbounded at personal scale. **No self-links** (usage). Cycles OK
  for `related` and `cites`. **Reject `supersedes` cycles** (direct or
  transitive) — A supersedes B supersedes A is nonsense.

- **Q: Delete / trash?**
  A: **Edges survive virtual trash; CASCADE on real delete/purge.**
  Restore must not have to rebuild the graph. Output **always marks** a
  neighbor that is in trash (human `[trash]`; JSON `"trash": true`).

- **Q: Locators?**
  A: Both ends, **exactly one of id | `--key` per end** (same contract as
  get/update). Link locators resolve in **either bin** (id/key are
  globally unique; TTL wrong-bin is for mutating/reading the *entry*).

- **Q: Output shape?**
  A: Neighbors are part of the public contract, not a hidden join.
  - Human **list/search**: extra column, compact —
    `Related: 34, foo:bar:key` (trash marked).
  - JSON **list/search/get** and `remember related`: a `links` array of
    **stubs** (id, key, type, trash, short preview) so an agent can
    decide whether to `get`. **Not** full neighbor bodies on list/search
    (or on get — get the id if the preview warrants it).

- **Q: Duplicate same pair + kind?**
  A: **Merge** (touch `updated_at`), like keyless add. Different kind on
  the same pair = two edges.

- **Q: CLI vs Mac order?**
  A: **CLI first** (schema + commands + skill + tests). Mac shell after
  the CLI pin is green: browse chip → jump; edit sheet add/remove.
  Add-panel “related to selection” later.

- **Q: Out of scope?**
  A: Embeddings, auto-suggest related, multi-hop graph UI, extra kinds.

### Round 2 — `related` storage (Itay, 2026-08-31)

**Superseded by Round 3** (kept as history). Round 2 chose two directed
rows; Round 3 reverts to a single canonical row once the mandatory
`from OR to` read made the two-row advantage moot.

- **Q: One canonical undirected row (`min,max`) vs two directed rows?**
  A: **Two rows.** `related` A–B is stored as `(A,B,related)` **and**
  `(B,A,related)` in one transaction. Same unique `(from_id, to_id, kind)`
  as directed kinds — no special-case CHECK. Query still **dedupes** so
  the public stub is one neighbor, `dir: "both"`. Unlink `related` deletes
  **both** rows. Merge touches `updated_at` on both.

### Round 3 — single-row `related`, output vocabulary, `rekey` (Itay, 2026-08-31)

- **Q: Direction — a `dir` column, or from/to?**
  A: **from/to only.** Reads already need `from_id = S OR to_id = S`
  (a directed *incoming* edge, e.g. “cited by”, appears only via
  `to_id`). Given that scan, direction is fully carried by which end the
  subject is; no `dir` column, no stored duplicate.

- **Q: Then does `related` still need two rows?**
  A: **No.** The two-row model existed so `from_id = S` alone would find
  the neighbor. The `from OR to` scan finds a single row from either end,
  so store `related` **canonically** as `(min(a,b), max(a,b), related)` —
  one row per unordered pair, the unique index enforces it. No mirror, no
  torn state, no self-heal, no read-dedup. Reverses Round 2.

- **Q: The 5 names (`cited_by`, `superseded_by`)?**
  A: **Output only.** Stored `kind` stays the 3 canonical values, and
  `link --kind` accepts only those 3. On read, the subject-relative
  `type` expands to 5: subject at the `from` end of a `cites` → `cites`,
  at the `to` end → `cited_by` (same for supersedes); `related` → itself.
  Legible without the reader doing direction math, and no
  same-fact-two-ways storage hazard.

- **Q: `unlink` of a missing edge?**
  A: **Idempotent** — exit 0, `action:"deleted"`, `count:0`, `links:[]`.

- **Q: Does linking touch the entries?**
  A: **Yes** — any real graph change bumps `updated_at` on **both**
  endpoints (a new association is a recency signal; the entry may
  resurface in `list`). The no-op `unlink` bumps nothing.

- **Q: Key rename without orphaning links?**
  A: New **`rekey`** command renames a key **in place** — id,
  `created_at`, and all incident edges preserved. Edges are id-based and
  the id never moves, so links cannot orphan. *(Round 3 said keyed→keyed
  only; **Round 4 pulls promote/demote into v1**.)* Replaces the
  foundations “delete + re-add to rename” guidance.

### Round 4 — human column, `related` human, `rekey` promote/demote (Itay, 2026-09-01)

Plan-extraction grill. Round 3 storage/kinds unchanged.

- **Q: `remember related` human output?**
  A: **Like get, because it is one subject.** Same `Related:` block as
  human `get` (`type id|key [trash] preview`). No entry body. Empty → no
  lines, exit 0. JSON is already the stub envelope.

- **Q: List/search related column — id or key?**
  A: **IDs only.** The sixth column is cramped; keys live on JSON stubs
  and on the single-entry `Related:` lines. `[trash]` glued to the id
  (`34[trash]`). Cap 5 then `, +N` remaining. Empty → blank cell (keep
  the pipes).

- **Q: Stub `preview` truncation?**
  A: Same as list preview at **40** codepoints (first line, controls →
  `?`, ellipsis if truncated, cut on a codepoint boundary).

- **Q: get `Related:` when there are no neighbors?**
  A: **Omit the block** (get stays body-only).

- **Q: `rekey` of a keyless row, or removing a key?**
  A: **Promote and demote are in v1.** `--to-key NEW` sets or renames
  the key (keyless→keyed or keyed→keyed). `--clear-key` removes it
  (keyed→keyless), matching `--clear-tags` / `--clear-expires`. Mutex;
  at least one required. Id, `created_at`, and edges preserved. Demote
  that collides with another keyless body-hash → exit 1 + conflicting
  id (same as `update`). NEWKEY taken → exit 1 + conflicting id.

- **Q: Empty `--to-key` — clear, or illegal?**
  A: **Illegal.** A key is a non-empty token (same normalize as `--key`
  on add: trim, then reject empty / invalid). `--to-key ""` or
  whitespace-only → usage `empty key`, exit 1; it does **not** demote
  and does not write `key=''`. Absence of a key is SQL NULL via
  `--clear-key` only. Bare `--to-key` with no value is missing-option
  usage, same as `--key` with no value.

- **Q: `rekey --to-key` equal to the current key, or `--clear-key` on
  an already-keyless row?**
  A: Success; bump `updated_at`. Conflict is **another** row’s id.

- **Q: Does `UNIQUE (from_id, to_id, kind)` forbid a reverse `related`?**
  A: **No — SQL as written.** The write path canonicalizes to
  `(min,max)`. Do not add a CHECK. No self-heal.

- **Q: `rekey` human / JSON envelope?**
  A: Match `update`: human id; JSON
  `{"version":1,"action":"updated","count":1,"entries":[…]}`. No
  `action:"rekeyed"`.

## Design

### Schema (`user_version` 2 → **3**)

New DBs create at 3. Existing v2 migrates in place.

```sql
CREATE TABLE entry_links (
  from_id    INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE,
  to_id      INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE,
  kind       TEXT NOT NULL CHECK (kind IN ('related', 'supersedes', 'cites')),
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  CHECK (from_id != to_id)
);
CREATE UNIQUE INDEX entry_links_edge
  ON entry_links(from_id, to_id, kind);
CREATE INDEX entry_links_to ON entry_links(to_id);
```

- Every kind is a **single** row `(from_id, to_id, kind)`. Direction is
  the `from_id → to_id` ordering; there is **no** `dir` column and stored
  `kind` is always one of the 3 canonical values.
- `supersedes` / `cites`: `from_id` is the subject, `to_id` the object
  (new supersedes old; this cites that).
- `related` is symmetric, stored **canonically** as
  `(min(a,b), max(a,b), related)` — exactly one row per unordered pair.
  The unique index forbids the same triple; the write path (not a CHECK)
  is what keeps the reverse out. No self-heal.
- Reads find a subject S's edges with `from_id = S OR to_id = S` (an
  incoming directed edge only appears via `to_id`). Each row → **one**
  stub; no read-dedup, because direction lives in from/to.
- The unique `(from_id, to_id, kind)` index serves `from_id = S`;
  `entry_links_to` serves `to_id = S`. Both are load-bearing for the scan.
- Timestamps: same canonical `.mmmZ` as entries.
- **No** `expires_at` on the edge — membership in trash is a property of
  the **entry** at read time.

### Commands

```text
remember link   (--from ID | --from-key KEY) (--to ID | --to-key KEY) [--kind KIND]
remember unlink (--from ID | --from-key KEY) (--to ID | --to-key KEY) [--kind KIND]
remember related <locator> [--kind KIND] [--outgoing | --incoming]
remember rekey  <locator> (--to-key NEWKEY | --clear-key)
```

Sugar: `remember link ID ID [--kind]` and `remember unlink ID ID [--kind]` =
`--from` / `--to` numeric ids. Same pair parser.

- `--kind` on `link`: one of `related` | `supersedes` | `cites` (default
  `related`). The reversed names (`cited_by`, `superseded_by`) are
  **output only**, never accepted as input. Unknown token → usage (exit 1).
- `unlink` is **idempotent**: removing an edge that isn't there is
  success (`count: 0`). Without `--kind` it removes **all** kinds between
  the pair regardless of `--from`/`--to` order. With `--kind related`, the
  single canonical `(min,max,related)` row. With a directed kind, the one
  `(from,to,kind)` row as given.
- `related` query: default **all** neighbors (`from_id = S OR to_id = S`),
  one stub per row. `--kind` filters. `--outgoing` = directed edges with
  `from_id = S`; `--incoming` = directed edges with `to_id = S`;
  **`related` edges are symmetric and appear under either flag** (no
  direction to exclude). Mutually exclusive outgoing/incoming. Locator is
  the usual exactly-one-of id | `--key` (either bin).
- `rekey` changes an entry’s key **in place**: id, `created_at`, and all
  incident edges preserved (edges are id-based and the id never moves, so
  links cannot orphan). Locator exactly-one-of id | `--key`, active by
  default (`--trash` reaches a trashed row, wrong bin → exit 3, same as
  `update`). Exactly one of `--to-key NEWKEY` (set/rename, including
  keyless→keyed) or `--clear-key` (keyed→keyless). Mutex. `NEWKEY` is
  a required non-empty key token (normalize then `empty key` / `invalid
  key` → exit 1); empty `--to-key` is never a demote and never stores
  `''`. `--clear-key` writes SQL NULL. `NEWKEY`
  already taken → exit 1 naming the conflicting id; locator missing →
  exit 2. Demote that collides with another keyless body-hash → exit 1 +
  conflicting id (same as `update`). Same-value (`--to-key` equal to
  current, or `--clear-key` on already keyless) succeeds and bumps
  `updated_at`. Bumps `updated_at` on the subject only (not a graph
  write). JSON matches `update` (`action:"updated"`, `entries:[one]`).
- Missing end → exit **2** (not found in either bin). Self-link, bad kind,
  supersedes cycle → exit **1**. No new process exit.

Human `link`/`unlink`: the subject id (and JSON envelope with `--json`).
JSON write: `{"version":1,"action":"created"|"merged"|"deleted","count":N,"links":[…]}`.
One row = one logical edge = one object, so **`count == len(links)`**
always (idempotent `unlink` no-op → `action:"deleted"`, `count:0`,
`links:[]`). `links` objects are subject-relative stubs (same shape as reads).

### Neighbor stub (JSON)

Appended on each **list/search/get** entry as `"links"` (empty array if
none). `remember related --json` is the same stubs under a small envelope
(`id`/`key` of the subject, `count`, `links`). Field order after
`expires_at`:

```json
"links": [
  {
    "id": 34,
    "key": "foo:bar:key",
    "type": "cites",
    "trash": false,
    "preview": "CLI TTL Round 4 locked exit 3…"
  }
]
```

| Field | Rule |
|-------|------|
| `id` | neighbor id |
| `key` | neighbor key or `null` |
| `type` | subject-relative relationship: `related`, `cites`, `cited_by`, `supersedes`, `superseded_by`. Derived from stored `kind` + which end the subject is: stored `cites` with subject at `from` → `cites`, at `to` → `cited_by` (same for supersedes); `related` → `related`. |
| `trash` | `true` iff neighbor is expired (`expires_at` not null and `<= now`) — the neighbor is in the trash bin; the edge is still shown |
| `preview` | neighbor body, **≤ 40 codepoints**, same rules as list preview (first line, controls → `?`, ellipsis if truncated), cut on a codepoint boundary (always valid UTF-8), indication only |

Stubs are ordered by edge `updated_at DESC`, then neighbor `id DESC`.

List/search human line gains a sixth column after `updated_at`:

```text
id | key | tags | preview | updated_at | related
```

`related` cell: comma-separated neighbor **ids only** (not keys), suffix
`[trash]` when the neighbor is expired. Cap **5** then `, +N` remaining.
Empty → blank cell (keep the pipes). Example: `34[trash], 64, 91`.

`get` human and `remember related` human: after the usual entry (get
only), a `Related:` block, one neighbor per line
(`type id|key [trash] preview`, using the 5-name vocabulary). Omit the
block when there are no neighbors (`related` then prints nothing, exit
0). Keys belong here and on JSON stubs, not in the list/search column.

### Graph rules

- Resolve both locators (either bin) → two ids. Equal → usage (self-link).
- `related`: canonicalize to `(min,max)` and upsert the single
  `(min,max,related)` row. Present → merge (`updated_at = now`, action
  `merged`). One row, so no torn state and no self-heal.
- Directed: upsert `(from, to, kind)`. Merge on the same triple.
- `supersedes`: before insert, if `to` can already reach `from` along
  `supersedes` edges, usage `supersedes cycle`. Personal-scale DFS.
- `cites` A→B and B→A: **allowed** (two rows; the subject sees one `cites`
  stub and one `cited_by` stub).
- Any successful mutating `link`/`unlink` (insert, merge, real delete)
  bumps `updated_at` on **both** endpoint entries (single per-command
  `now`); the idempotent no-op `unlink` bumps nothing.
- Purge / `delete` of an entry: FK CASCADE removes incident edges.
- Trash (expiry) does **not** remove edges.

### Skill (agent)

When to write a link: a future agent must traverse A to understand B.
When not: shared facet (that’s a tag); same session; “also in this
project.” Prefer `--kind supersedes` or `cites` when that is the truth.
Reading a stub `type`: `cited_by` / `superseded_by` mean the **neighbor**
points at *this* entry (this entry is the cited / superseded one) — decide
trust accordingly. `link`/`unlink`/`related` resolve a locator in **either
bin** (no exit 3), unlike `get`/`update`/`delete`; edges also **survive
trash**, so a stub can be `"trash": true`. Always `--json`. Honor `trash`
on stubs — `get` without `--trash` on a trashed neighbor is exit 3. Change
a key with `rekey` (`--to-key` / `--clear-key`; keeps links), never
delete + re-add.

## Data flow

```
link --from A --to B --kind K
  resolve A, B (either bin) → ids; A == B → usage
  if related: store (min,max, related)      # canonical, single row
  if directed: supersedes cycle check; store (A, B, K)
  upsert / merge; bump updated_at on both A and B

list / search / get / related
  load entries as today (active xor trash for the *subject*)
  SELECT edges WHERE from_id = subject OR to_id = subject
  per row → one stub:
    neighbor = the other end
    type = related | (cites/cited_by | supersedes/superseded_by,
           by which end the subject is)
    trash = neighbor expired at now
  order by edge updated_at DESC, neighbor id DESC
```

## Implementation Plan

1. Schema v3 + store ports: insert/merge/delete edge (canonical
   `related`), list neighbors, supersedes-cycle check, **`rekey`**
   (in-place key set/rename/clear, id preserved). Tests against a temp DB
   (active + trash neighbors, cascade on purge, merge, rekey keeps edges,
   promote/demote).
2. CLI `link` / `unlink` / `related` / `rekey` + argv locators + JSON
   write envelopes. Must-pass: merge, idempotent no-op unlink, cycle,
   self-link, missing end, either-bin, rekey conflict/rename,
   `updated_at` bump on both endpoints.
3. list/search/get emit `links` stubs + human related column / get block.
   Skill + help. `just` / ctest / coverage gate.
4. remember-mac pin + shell (log 004) after this CLI is on main.

## Examples

Human list (active), id 12 cites a trashed spike and is related to a keyed slot:

```text
12 |  | p7 | P7 kinds locked | 2026-08-31T13:51:22.000Z | 34[trash], 64
```

`remember --json related 12` (truncated):

```json
{
  "version": 1,
  "id": 12,
  "key": null,
  "count": 2,
  "links": [
    {"id": 34, "key": null, "type": "cites", "trash": true,
     "preview": "temp spike — drop after review"},
    {"id": 64, "key": "decision:p7-related-memories", "type": "related",
     "trash": false, "preview": "P7 related memories — locked"}
  ]
}
```

```text
remember link --from-key decision:p7-related-memories --to 34 --kind cites
remember link 12 64
remember unlink --from 12 --to 64 --kind related
remember rekey --key decision:p7-related-memories --to-key decision:p7-links
remember rekey 12 --to-key decision:p7-note
remember rekey --key decision:p7-note --clear-key
```

## Trade-offs

- **Chosen (Round 3): `related` as a single canonical `(min,max)` row.**
  Reverses Round 2. Once reads must scan `from_id = S OR to_id = S` anyway
  (directed incoming edges appear only via `to_id`), the two-row model's
  sole advantage — reachable by `from_id = S` alone — is gone, and the
  single row is strictly simpler: no torn-write invariant, no self-heal,
  no read-dedup, single-row unlink/merge.
- **Chosen: 5-name output `type`, 3 stored `kind`s.** `cited_by` /
  `superseded_by` are the same edge read from the other end — storing them
  would give one fact two spellings (and contradiction states). Direction
  from from/to keeps storage canonical; the reader still gets a
  self-describing label.
- **Chosen: closed kinds + directed supersedes/cites.** Untyped undirected
  only would collapse into tags. A large kind vocabulary would bit-rot in
  the skill. Closed enum is grep-able and JSON-stable.
- **Chosen: stubs on list/search, not full bodies.** User asked for enough
  to decide whether to `get`. Full bodies on a 20-row page is an N-body
  dump and breaks the list contract.
- **Chosen: edges survive trash.** Restore is not a scavenger hunt. The
  cost is stubs with `trash: true` — agents already know exit 3.
- **Chosen: `rekey` in place, not delete + re-add.** A stable id keeps the
  graph intact through a slot rename, promote, or demote; delete + re-add
  would CASCADE the edges away. `--clear-key` matches `--clear-tags` /
  `--clear-expires` (empty `--to-key` is an invalid key, not a demote).
- **Chosen (Round 4): list/search related column is ids only.** Keys on a
  20-row page fight the preview column; get/`related` human and JSON stubs
  still carry key + type + preview.
- **Rejected: `--trash` on each link locator.** Ugly with two ends; ids
  are unique across bins.
- **Rejected: embedding `links` only on `remember related`.** Agents would
  extra-round-trip every list hit. Stubs are cheap at personal scale
  (one `IN (ids)` query per page).

## Verification Criteria

- Schema v3; v2 DBs migrate; v>3 refuse.
- `related` writes **one** canonical `(min,max,related)` row; directed one
  row; unique `(from_id, to_id, kind)`; merge on duplicate.
- Idempotent `unlink` of a missing edge → exit 0, `count:0`, `links:[]`.
- Self-link and `supersedes` cycle → exit 1; missing end → exit 2.
- `link`/`unlink` bump `updated_at` on both endpoints (not on a no-op).
- `rekey` sets, renames, or clears a key in place, id + edges preserved;
  NEWKEY taken or demote body-hash collision → exit 1; missing → exit 2;
  wrong bin → exit 3.
- JSON stub `type` uses the 5-name vocabulary; stored `kind` is one of 3;
  `count == len(links)` on writes.
- Neighbor in trash: human `[trash]`, JSON `"trash": true`; edge still listed.
- Purge of either end drops the edge; restore of a trashed end keeps it.
- list/search human sixth column is **ids only** (cap 5 then `, +N`);
  JSON `links` on list/search/get/related with ≤40-codepoint preview
  (codepoint boundary, valid UTF-8), no full neighbor body; get/`related`
  human `Related:` block omitted when empty.
- Skill documents when to link vs tag, the three kinds, and the 5-name
  stub reading.
- `just check` / CLI coverage gate green.

## Known Limitations

- One hop only. `related` of a neighbor is another command.
- Preview is not a substitute for `get` (truncation, no tags on the stub).
- `--outgoing`/`--incoming` filter directed edges only; `related` is
  symmetric and always appears under either flag.
- `supersedes` is **descriptive only** — it does not hide or down-rank the
  superseded entry; both still surface in search at full weight. For a
  canonical keyed slot where the old value should disappear, use the
  `--key` upsert (replace in place); use `supersedes` for keyless entries
  where the history has value.
- Human related column caps at 5 then `, +N`; JSON lists all neighbors
  (personal scale). If a hub grows huge, add `--limit` on `related` later.
- Add-panel “related to current selection” is Mac-deferred (log 004).

## Future Work

- Extra kinds only after two real uses.
- Optional `--limit` on `remember related`.
- Mac add-panel link sugar.

## Implementation Results (2026-09-02)

Shipped as designed (Rounds 1–4). Notes:

- Schema, single-row canonical `related`, from/to direction, 5-name output
  `type`, idempotent `unlink`, both-endpoint `updated_at` bump, and either-bin
  graph locators (`store_get_any` / `store_get_any_by_key`) are all in as
  specified. Stored `kind` stays the 3 canonical values.
- `rekey` landed with **promote and demote** in v1 (Round 4): `--to-key`
  sets/renames/promotes, `--clear-key` demotes (honoring keyless body-hash
  uniqueness). Foundations' delete+re-add rename guidance is superseded.
- `supersedes` reachability uses `WITH RECURSIVE … UNION` (set semantics) so a
  dense supersedes DAG cannot blow up the cycle check.
- Plan + gates: `implementation-plans/13-related-memories.md`. CLI complete on
  `feat/p7-related-memories`; remember-mac pin + shell (Mac plan 13) follows.
