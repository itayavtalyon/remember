# 13 — Related memories (entry links, P7)

## Goal

Pairwise `entry_links` graph: `related` / `supersedes` / `cites`. Neighbors
surface as stubs on list/search/get; agents link/unlink/traverse; keyed
rename (`rekey`) keeps id + edges. **Design (source of truth, including
Round 4):**
[`design-logs/004-related-memories.md`](../design-logs/004-related-memories.md).

Do not invent. If a case is missing from 004, stop and ask. Round 4
supersedes the plan-extraction grill Recs; Round 3 supersedes Round 2
(and any “two-row related” chat).

Mac UI is **not** this plan — remember-mac
`implementation-plans/13-related-shell.md` after this CLI is on the pin.

## Scope

**In:** schema `user_version` 2 → **3** (`entry_links` as in 004 SQL);
store ports (insert/merge/delete edge with canonical `related`, list
neighbors, supersedes-cycle check, either-bin load, **`rekey`**
set/rename/clear); CLI `link` / `unlink` / `related` / `rekey`
(`--to-key` | `--clear-key`); JSON `links[]` stubs on list/search/get +
`related` envelope; human list/search sixth column (**ids only**) + get
/ `related` `Related:` block; skill/help; tests + coverage.

**Out:** two-row `related`; `dir` column; stored `cited_by` /
`superseded_by`; read-dedup; self-heal of mirrors; extra CHECK on
`related` canonicalization; embeddings; auto-suggest; multi-hop; extra
kinds; `--limit` on `related`; Mac UI; `--trash` on each link locator
(either-bin); `links` on add/update/delete/purge envelopes (004 names
list/search/get only); new process exits (stay 0/1/2/3); storing
`key=''`; treating empty `--to-key` as `--clear-key`.

## Round 3 invariants (must not regress to Round 2)

- `related` is **one** canonical `(min(from,to), max(from,to), related)`
  row. `link A B` and `link B A --kind related` are the same edge (merge).
- No second row, no read-dedup, no self-heal. Reads are
  `from_id = S OR to_id = S`; each row → one stub.
- Stored `kind` is only `related` | `supersedes` | `cites`.
  `link --kind` accepts only those 3. Output `type` is the 5-name
  vocabulary (`cited_by` / `superseded_by` when the subject is the `to`
  end of a directed edge; `related` → itself).
- `UNIQUE (from_id, to_id, kind)` as written in 004. That index forbids
  the same triple, **not** a reverse related pair — the write path
  canonicalizes. Do **not** add a CHECK that is not in the SQL block.
- `unlink` of a missing edge is success (`count: 0`, `links: []`).
- Real graph change (insert, merge, real delete) bumps `updated_at` on
  **both** endpoint entries (one command `now`). No-op unlink bumps
  nothing.
- Edges survive expiry; `ON DELETE CASCADE` on purge/delete. Neighbor
  trash is a read-time property of the **entry**.

## Round 4 locks (grill, 2026-09-01)

| Topic | Lock |
|-------|------|
| `remember related` human | Same `Related:` block as get (`type id\|key [trash] preview`). No body. Empty → no lines, exit 0. |
| list/search related column | Neighbor **ids only** (not keys). `[trash]` glued (`34[trash]`). Cap 5 then `, +N`. Empty cell blank (keep the pipes). |
| Stub `preview` | List-preview rules at 40 codepoints (first line, controls → `?`, ellipsis). |
| Human list `+N` | Five id cells, then `, +N` remaining. |
| get `Related:` when none | Omit the block. |
| `rekey` | `--to-key NEW` sets/renames (incl. keyless→keyed); `--clear-key` removes the key. Mutex; at least one required. Id + edges preserved. A key is a **non-empty** token; empty/`''` is never legal and never means clear. |
| `rekey` same-value | `--to-key` equal to current, or `--clear-key` on already keyless: success, bump `updated_at`. |
| `rekey` JSON/human | Match `update`: human id; `action:"updated"` + `entries:[one]`. No `action:"rekeyed"`. |
| UNIQUE vs reverse `related` | SQL as written; write-path `min/max`; no extra CHECK; no self-heal. |
| add/update/delete/purge JSON | Unchanged (no `links`). Kit defaults missing `links` to `[]`. |
| Positional sugar | `remember link ID ID` and `unlink ID ID` = `--from` / `--to`. |
| `unlink` without `--kind` | All kinds between the unordered pair. Stubs subject-relative to `--from`. |
| `--kind` on `related` query | Stored 3-kind filter. `related` edges appear under either direction flag. |

## Must-pass tests

| Check | Proves |
|-------|--------|
| Fresh DB `user_version=3`; `entry_links` + `entry_links_edge` + `entry_links_to`; no `dir` column | bootstrap |
| Copy of a v2 file migrates in place; old rows unchanged; empty `entry_links` | migrate |
| v>3 refuse (`database is newer than this remember`) | refuse |
| `link A B --kind related` → **one** row, `from_id=min`, `to_id=max`, `kind=related` | canonical related |
| `link B A --kind related` on the same pair → `action:merged`, still **one** row | merge, not two rows |
| Directed `cites`/`supersedes` → one `(from,to,kind)` row (not min/max) | directed |
| Same pair + different kind → two rows | kinds independent |
| SQL `SELECT COUNT(*)` after related write is 1; reverse triple is **not** inserted | no mirror |
| Self-link (same id via id or key) → exit **1** | usage |
| Unknown `--kind` (incl. `cited_by`, `superseded_by`) → exit **1** | input vocabulary |
| Missing end (neither bin) → exit **2** | not found |
| `supersedes` cycle (direct + transitive) → exit **1**; message includes `supersedes cycle` | cycle |
| `cites` A→B and B→A both succeed; subject A sees `cites` + `cited_by` | cites both ways |
| `unlink` missing edge → exit **0**, JSON `action:deleted`, `count:0`, `links:[]` | idempotent |
| `unlink` without `--kind` removes every kind between the pair either order | unordered pair |
| `unlink --kind related` either order deletes the one canonical row | related unlink |
| `unlink --kind cites` deletes only the given `(from,to,cites)` | directed unlink |
| Successful link/merge/real unlink bumps **both** endpoints’ `updated_at`; no-op unlink does not | recency |
| Write JSON `count == len(links)` always; stub `type` is 5-name, never stored-only `kind` | envelope |
| `related S` (either bin, no `--trash`) lists all neighbors, one stub per row | query |
| `--outgoing` / `--incoming` mutex; `related` edges appear under either | direction flags |
| JSON list/search/get: `"links"` after `"expires_at"` (empty array if none); no full neighbor `body` | stubs |
| Stub `preview` ≤ 40 codepoints, valid UTF-8, codepoint boundary | preview |
| Neighbor expired: JSON `"trash": true`; human `[trash]`; edge still listed | trash mark |
| `expires_at == now` neighbor is trash (store fixture `now`) | boundary |
| Purge/delete of either end drops incident edges (FK CASCADE) | cascade |
| Restore (`update --trash --clear-expires`) keeps edges | survive trash |
| Human list/search sixth column is **ids only** (`34[trash], 64`); keys do not appear; TTL `human_list_still_five_columns` **replaced** | human list |
| Human get and `related`: `Related:` block (`type id\|key [trash] preview`); omitted when none | human get/related |
| `rekey --key OLD --to-key NEW`: same id, `created_at` unchanged, edges intact, key=NEW, `updated_at` bumped | rename |
| `rekey ID --to-key NEW` on keyless: same id, now keyed, edges intact | promote |
| `rekey --key OLD --clear-key`: same id, `key` null, edges intact; body-hash collision with another keyless → exit **1** + conflicting id | demote |
| `--to-key` + `--clear-key` → usage **1**; neither → usage **1** | flags |
| `--to-key ""` / whitespace-only → exit **1**, `empty key`; row unchanged (not demoted, not `key=''`) | empty key illegal |
| `--to-key` with no value → usage (missing option value), same as `--key` | flag arity |
| `rekey` NEWKEY taken → exit **1** + conflicting id; missing locator → **2**; active-vs-trash wrong bin → **3** | rekey errors |
| `rekey --to-key` equal to current (or `--clear-key` on keyless): exit 0, bump `updated_at` | same-value touch |
| Facade `remember_run` byte-match includes `link` / `unlink` / `related` / `rekey` | in-process |

## Stages

TDD. One failing test at a time. After each stage: tests green, then
strict review (code not only diff); persist Implementation/Review Notes.

Work **in this repo** (`external/remember` pin / the remember C tree that
already has TTL). Do not implement in a sibling checkout that lacks log
003.

1. Schema v3 + store ports. Fresh create-at-3; v2→v3 migrate (CREATE
   `entry_links` + indexes, bump `user_version`); v>3 refuse. Ports:
   either-bin load (graph locators — **not** a trash flag on `store_get`);
   upsert/merge/delete edge (canonical `related`); list neighbors for a
   subject (and a page of ids — one `IN` query); supersedes-cycle DFS;
   `store_rekey` (set/rename/clear key, id preserved; demote honors
   keyless body-hash uniqueness). Store unit tests on a temp DB (active +
   trash neighbors, cascade, merge, one-row related, rekey keeps edges,
   promote/demote). **No sqlite outside `store_sqlite.c`.** Output mapping
   (`cited_by` / preview / JSON) is **not** the store.
2. CLI `link` / `unlink` / `related` / `rekey` + argv locators + JSON
   write envelopes + help topics. Sugar: `link`/`unlink ID ID`. Graph commands
   resolve either bin (no exit 3). `rekey` uses update’s bin contract +
   `--to-key` / `--clear-key`. Must-pass: merge, idempotent unlink, cycle,
   self-link, missing end, either-bin, rekey rename/promote/demote/
   conflict, both-endpoint `updated_at` bump.
   New TUs: `REMEMBER_LIB_SOURCES` **and** `-Wcast-qual` (TTL forgot
   `cmd_purge.c` once). Split on concern (graph vs locator), not one file
   per verb if they share parse.
3. list/search/get emit `links` stubs + human sixth column (**ids
   only**) / get + `related` `Related:` block. Skill
   (`skills/remember/SKILL.md`): when to link vs tag, 3 stored kinds,
   5-name stub reading, either-bin vs exit 3, edges survive trash,
   `rekey --to-key`/`--clear-key` not delete+re-add, always `--json`.
   `tests/gate-suites` gains this suite. Replace
   `human_list_still_five_columns`.
4. Full suite + lint + coverage (store/commands) + facade byte-match.

## Definition of Done

- [x] Design log 004 (through Round 4) unchanged except a later Implementation Results
- [x] Must-pass table green; `user_version` 3; one-row `related`; no `dir`
- [x] Public exits still 0/1/2/3; no new process exit
- [x] Skill/help match commands, kinds, 5-name `type`, `rekey --to-key`/`--clear-key`
- [x] `just check` / CLI coverage gate green
- [ ] remember-mac pin + shell is a **follow-up** (Mac plan 13)

## Implementation Notes

### Stage 1 (schema v3 + store ports)

- Fresh CREATE runs `k_schema_sql` then `k_links_sql`, `user_version = 3`. v1 adds
  `expires_at` + links; v2 adds links only. Same `BEGIN IMMEDIATE` race as TTL.
- `UNIQUE (from_id, to_id, kind)` as written; related canonicalized on write.
  No extra CHECK. No `dir` column.
- Ports on `store.h`: `store_get_any` / `store_get_any_by_key` (no bin);
  `store_link` (canonical related, cycle on supersedes, SELF_LINK);
  `store_unlink` (NULL kind = unordered pair, missing → count 0);
  `store_list_neighbors` + `store_list_neighbors_for` (related visible
  outgoing and incoming); `store_rekey` (NULL new key = clear).
- Tests in `tests/test_store_links.c` (85 store-unit tests green, including
  one-row related, cycle, cascade, promote/demote).

### Stage 2 (CLI link / unlink / related / rekey)

- `cmd_graph.c`: pair parse (`--from`/`--from-key`/`--to`/`--to-key`/`--kind`)
  plus `link`/`unlink ID ID` sugar; locators via `store_get_any` (no exit 3). Default
  `--kind related` on link; unlink without `--kind` deletes the unordered pair.
- `cmd_rekey` in `cmd_locator.c`: `--to-key` / `--clear-key` mutex; empty
  `--to-key` is `empty key`; wrong bin exit 3; key/body-hash conflict names the
  other id.
- `output.c`: write envelope `{action,count,links}`; related envelope
  `{id,key,count,links}`; stub `type` is the 5-name vocabulary; preview 40 cp
  with list-preview rules. Human related is a `Related:` block (omitted if none).
- CLI tests: `tests/test_link.c` (suite `link` in `gate-suites`). Gate 330/330.

### Stage 3 (list/search/get stubs + skill)

- `output_entry_json` (add/update/delete/purge) still has no `links`. list/search/get use `write_entry_with_links` so `"links"` sits after `"expires_at"` (empty array if none). Stubs reuse the existing 5-name `write_stub_json` (preview 40 cp).
- One `store_list_neighbors_for` per list/search page; `store_list_neighbors` on get. Human list/search sixth column is neighbor ids (`[trash]` glued, cap 5 then `, +N`); empty cell keeps the pipes. Human get reuses `output_related_human` after the body (omit when none).
- TTL `human_list_still_five_columns` replaced by `human_list_has_related_column` (5 pipes). Skill documents tag vs link, 3 stored kinds, 5-name stub reading, either-bin vs exit 3, edges survive trash, `rekey --to-key`/`--clear-key`. `tests/gate-suites` already includes `link`.

## Review Notes

### Stages 1–2 (2026-09-01)

**Verdict:** Approve after auto-fix (blocking CLI exits + lint). Ready for stage 3.

**Blocking (fixed in this pass):**
- `resolve_end` returned `-1` → process exit **255** on invalid id / empty `--from-key`. Now `REMEMBER_ERR` (1). Locked in `link_invalid_id_is_exit_1`.
- `unlink 1 2` is the same pair sugar as `link` (004 amended). Not a special case.
- Lint FAIL (cognitive complexity / unused bind / suspicious fill_stub args). Extracted parsers and SQL builders; `scripts/lint-all.sh` LINT OK.

**Important (not blocking stage 3):**
- `supersedes_reaches` uses recursive `UNION ALL` (SQLite default depth 1000). Fine at personal scale if we never insert cycles; corrupt SQL could theoretically spin.
- Facade byte-match for new commands is stage 4.
- list/search/get `links[]` still absent (stage 3).

**Nits:** `link 1` (one positional) message is “missing --from/--to”.

**Praise:** 5-name `type` lives only in `output.c`; store keeps 3 kinds. Canonical `(min,max,related)` and either-bin trash subject verified by running the CLI.

**Gates:** link 8/8; store unit 85/85; lint OK; full gate **331/331**.

### Stage 4 (facade byte-match + coverage) (2026-09-02)

- Facade parity for the new commands: `test_facade.c` gains `related` and
  link-bearing `get` read parity, plus `link` / `unlink` / `rekey` mutation
  parity (two-DB, timestamp-masked). A `SeedFn` helper removes the copy-paste.
- Coverage close-out to 100% funcs + effective lines on `src/`:
  - `store_get_any_by_key` was never exercised — no test linked/`related`d
    **by key**. Added `link_and_related_by_key_either_bin` (real path,
    incl. a trashed neighbor resolved either-bin).
  - Extended the existing `rekey` CLI test: `--trash` bin, demote body-hash
    conflict message, `--clear-key` on an already-keyless entry.
  - `related --kind` filter + human `Related:` block (keyed neighbor) +
    `--kind` missing value; preview control-byte → `?` and multibyte
    first-line overflow (via 80-cp human list preview).
  - Per-topic `help link|unlink|related|rekey`.
  - Store: `store_list_neighbors_for` empty page; a fault-injection sweep
    (`#ifdef REMEMBER_TEST_HOOKS`) over link/unlink/neighbors/rekey for the
    OOM / prepare / step cleanup paths.
- Two dead guards removed (`parse_kind_token` NULL branch;
  `bump_endpoints` `a == b`); `cmd_related` write-fail uses the idiomatic
  `return REMEMBER_ERR`.
- `scripts/check-coverage.sh`: added `store_neighbor(s)_free(` and
  `*out_stubs =` to the defensive-cleanup exclusion list (same class as the
  pre-existing `store_entry_free(` / `*out_count =` entries; the P7 free
  helpers simply predated it).

**Gates (Stage 4):** full ctest 3/3 under ASan/UBSan; coverage 100% funcs +
100% effective lines; lint OK. Facade suite 18/18; link 16/16; store unit
87/87.
