# 14 — Local sync foundations (E1 / Phase 0 / R1)

## Goal

Ship CLI schema + API for durable `sync_id`, soft-delete bins, device
sidecar + version vectors, merge-import + conflicts, and `--sync-id`
locators so Fortkeeps Mac L3 (15f) and later Cloud sync have a stable
local foundation.

**Design (source of truth, including Round 7):**
[`design-logs/005-sync-foundations.md`](../design-logs/005-sync-foundations.md).

Do not invent. If a case is missing from 005, stop and ask. Round 7
supersedes Round 6 on sidecar path, import device registration, `--keep
both`, unique occupancy, and one-row hard delete.

Mac UI is **not** this plan — remember-mac 15f after this CLI is on the
pin. Stages 1–4 are what 15f needs (`sync_id` on stubs + locators +
bins); stage 5 is E1 import.

## Scope

**In:** `user_version` 3 → **4**; `sync_id` / `deleted_at` / `version_vector`;
`devices` + `conflicts` tables; sidecar `<db-path>.device_id`; JSON field
order + `bin` enum (`live|expired|deleted`); drop stub `trash`; bin flags
`--expired`/`--deleted` + `--trash`/`purge-trash` aliases; unflagged
soft-delete (live+expired); one-row hard `delete --expired|--deleted`;
`--undelete` / revive; `purge --expired|--deleted` (single-device hatch
on every hard wipe); locators `--sync-id` (+ graph forms); VV bumps on
mutating cmds including link/unlink/rekey; `import --from-db` (no foreign
device register); `conflicts` / `conflict accept`; skill/help; tests +
coverage; facade byte-match.

**Out:** Cloud/network/auth; JSON export dump; Mac UI; changing link kinds;
multi-device ack-GC; embedding device in `sync_id`; `quota_exceeded`;
`SQLITE` outside `store_sqlite.c`.

## Locked invariants (must not regress)

- `sync_id` = UUID v7, unique, mint on create, never change **except**
  `--keep both` on `concurrent_vv` mints a **new** id for the incoming
  copy (local keeps S). Import of an existing row preserves its `sync_id`.
- Local `id` is not durable across merge.
- Sidecar is `<db-path>.device_id` (not `dirname/device_id`). Local-only
  `devices` (at most one row). Import does not INSERT foreign device ids.
- `bin` always `"live"|"expired"|"deleted"` — never null, never omit.
- No `"trash"` field on stubs/entries.
- Unique indexes unchanged: deleted occupies key/hash; `add` revives.
- `entry_links` remain id FKs; soft-delete does not CASCADE; hard wipe does.
- Link kinds: stored 3 / output 5 — unchanged from 004.
- Exit 3 tokens: `expired` | `deleted` | `not_expired` | `not_deleted`
  (Round 7 matrix).
- Unflagged `delete` locators: live **or** expired (soft). Other default
  locators: live only. Graph default: live+expired, **exclude** deleted.
- Hard wipe (one-row or `purge`) only when `devices` COUNT == 1.
- `remember_run` byte-identical to CLI `--json`.
- Device sidecar never imported; source DB opened read-only.

## Must-pass tests

| Check | Proves |
|-------|--------|
| Fresh DB `user_version=4`; columns + `devices` + `conflicts`; sidecar `<db>.device_id` | bootstrap |
| Two DBs in one dir get **different** sidecars | per-file identity |
| v3→v4 migrate: rows keep id/body/links; each unique v7 `sync_id`; VV object `{local:1}` | migrate |
| v>4 refuse | refuse |
| `sync_id` stable across update, rekey, soft-delete, undelete, revive | immutability |
| JSON entry field order + always `bin` enum; stubs have `sync_id`+`bin`, no `trash` | contract |
| Locator mutex id \| `--key` \| `--sync-id`; graph `--from-sync-id`/`--to-sync-id` | locators |
| `--trash` ≡ `--expired` + deprecation stderr; does not select deleted | alias |
| `purge-trash` ≡ `purge --expired` + deprecation | alias |
| Unflagged `delete` of live: `deleted_at` set, `expires_at` null, body/edges kept, `bin=deleted` | soft-delete |
| Unflagged `delete` of expired: `bin=deleted`, `expires_at` null (E2) | expire-then-soft |
| `delete --expired` / `delete --deleted` hard-wipes one row (CASCADE); COUNT==1 | one-row hard |
| `delete --trash` ≡ `delete --expired` (003 analog, deprecation) | alias hard |
| Hard wipe with COUNT≠1 → exit 1 | hatch |
| `update --deleted --undelete` restores; wrong bin → exit 3 `not_deleted`/`deleted` | undelete |
| Default list/search/get omit expired+deleted; flags select one bin | filters |
| Default `related`/stubs omit deleted neighbors; `--deleted` shows `bin=deleted` | stubs |
| Expired neighbor still listed by default with `bin=expired`; human `[expired]` | 004 preserve |
| Graph link to deleted id without `--deleted` → not found or requires flag (no silent link) | get_any |
| Exit 3 matrix (Round 7 table) | tokens |
| `add` revive soft-deleted same key / keyless body-hash; same `sync_id` | revive |
| link/unlink/rekey bump VV on affected ends; no-op unlink does not | VV |
| `purge --deleted` with 1 device hard-wipes bin; with 2 devices exit 1 | hatch bin |
| `purge` requires exactly one of `--expired`\|`--deleted` | flags |
| `get`/`list` do not rewrite `devices.last_seen` (mtime/sidecar stable) | no dirty read |
| Lost sidecar + one `devices` row → sidecar rewritten from that row, COUNT stays 1 | recover |
| Import preserves foreign `sync_id`; remaps links; no sidecar copy; COUNT still 1 | import |
| Import of v3 source → exit 1 `source database is older than this remember` | refuse v3 |
| Identical content (body+tags+key+delete state) auto-merge; concurrent VV → conflict | conflicts |
| Import unique key/hash clash (incl. vs deleted) → `key_clash`/`hash_clash`, exit 0 | unique |
| Import JSON: `action:imported`, `inserted`/`updated`/`unchanged`/`conflicts` counts | envelope |
| `conflict accept --keep local\|incoming\|both`; `both`+`concurrent_vv` remints incoming | accept |
| `both`+`key_clash` keeps incoming `sync_id`, keyless if needed | both clash |
| Facade byte-match for new/changed commands | facade |
| Link kind vocabulary unchanged (unknown `cited_by` still usage 1) | 004 |

## Stages

TDD. One failing test at a time. After each stage: tests green, then
deep-review when moving on. Persist Implementation/Review Notes.

Work **in this repo**. No sqlite outside `store_sqlite.c`. UUID v7 mint
lives in the adapter (`timespec_get` + `sqlite3_randomness`); canonical
lowercase 8-4-4-4-12.

1. **Schema v4 + sidecar + migrate.** Fresh create-at-4; v1/v2/v3 → 4
   (mint `sync_id` + `{local:1}` VV per row, `deleted_at` NULL, `devices`
   + `conflicts` empty then local insert-if-absent). Sidecar
   `<db-path>.device_id`. Recovery rules Round 7. Store unit tests
   (two DBs one dir; lost sidecar rewrite; `get` does not bump
   `last_seen`). Replace `ListQuery.bool trash` with a 3-way bin in
   `store.h` (`live` / `expired` / `deleted`); map
   `STORE_ERR_NOT_IN_TRASH` → `STORE_ERR_NOT_EXPIRED` (CLI token
   `not_expired`). `Entry` grows `sync_id`, `deleted_at`, `version_vector`.
   `store_get_any` excludes deleted.
2. **Output + bins + exit tokens + aliases.** Field order; `bin` always
   enum; drop `trash`; `--expired`/`--deleted` mutex; `--trash`
   deprecation; Round 7 exit-3 matrix; human `[expired]`/`[deleted]`.
   Neighbor stubs carry `sync_id`+`bin`.
3. **Soft-delete / one-row hard delete / undelete / revive / purge.**
   Unflagged `delete` = soft (live+expired). `delete --expired|--deleted`
   = hard one-row iff COUNT==1. `purge` whole-bin, same hatch.
   `purge-trash` alias. FTS kept on soft-delete; removed on hard wipe.
   New TU if needed: wire `REMEMBER_LIB_SOURCES` **and** `-Wcast-qual`.
4. **`--sync-id` locators + VV bumps on graph/rekey.** Graph `--deleted`
   for deleted ends; default excludes deleted. No-op unlink still no VV
   bump (004).
5. **`import --from-db` + conflicts + accept.** Read-only foreign open
   (not `store_open`). Refuse v3. VV rules Round 7. Unique clash →
   conflict, not SQLITE error. Do **not** register foreign devices.
   `conflict accept --keep both` remints on `concurrent_vv`. Closed
   `reason` enum. JSON counts envelope; `conflicts` list command.
6. **Skill/help + facade parity + full suite + coverage + lint.**
   Skill: key vs sync-id vs id; bins; aliases; hard vs soft delete;
   run `conflicts` after import; exit 3 matrix; sidecar next to DB file.

## Store port sketch (stage 1, refine names to match `store.h` style)

Replace `bool trash` with a bin enum on get/list/search/tags/update/rekey.
Add `store_get_by_sync_id`. Soft vs hard delete are **two** functions (or
a flag that cannot default to hard). `store_import(Store *dst, const char
*src_path, …)` + `store_conflicts_*` stay in the adapter. Commands never
open a second SQLite handle.

## Definition of Done

- [ ] Design log 005 unchanged except later Implementation Results
- [ ] Must-pass table green; `user_version` 4
- [ ] No `"trash"` in JSON; `bin` always enum
- [ ] Skill/help match locators, bins, aliases, conflicts, sync_id guidance
- [ ] `just check` / coverage gate green
- [ ] remember-mac pin + 15f is a **follow-up** (kit must decode `bin`,
      drop `trash`, hard-require `sync_id`)

## Implementation Notes

### Stage 1 (2026-09-20)

Schema `user_version` 4: `sync_id` / `deleted_at` / `version_vector` on
`entries`; `devices` + `conflicts` tables; sidecar `<db-path>.device_id`
(0600). UUID v7 mint in the adapter (`timespec_get` + `sqlite3_randomness`).
Recovery: lost sidecar rewrite; sidecar vs `devices` mismatch → sidecar
wins (atomic SAVEPOINT replace); empty `devices` insert from sidecar.
`StoreBin` replaces `bool trash`. `store_get_any` excludes deleted.
`STORE_ERR_NOT_EXPIRED` / `DELETED` / `NOT_DELETED`. CLI `--trash` still
maps to expired pending stage 2 aliases. VV bumps on update deferred to
stage 4.

### Stage 2 (2026-09-20)

JSON field order + always `bin` enum `live|expired|deleted`; no `"trash"`
on entries or stubs. Stubs `{id,sync_id,key,type,bin,preview}`. Shared
`CmdBinOpts` (`cmd_bin_take_flag` / `cmd_bin_resolve`): `--expired` /
`--deleted` mutex (`cannot combine --expired and --deleted`); `--trash`
≡ `--expired` + one stderr line
`remember: --trash is deprecated; use --expired`. Round 7 exit-3 matrix
on get (tokens `expired|deleted|not_expired|not_deleted`). Human neighbor
marks `[expired]` / `[deleted]`. Default list/search/get/related omit
deleted neighbors; expired neighbors remain; `--deleted` includes them.
`store_bin_of` is the single bin predicate (adapter). Facade mutation
parity masks `sync_id` + `version_vector` as well as timestamps. Help /
skill / `--sync-id` stay later stages.

### Stage 3 (2026-09-20)

Unflagged `delete` is soft (live or expired): `deleted_at=now`,
`expires_at` cleared, body/tags/key/`sync_id`/edges/FTS kept, VV bumped,
`bin=deleted`. `delete --expired|--deleted` is one-row hard CASCADE
(FTS removed) iff `devices` COUNT==1; `--trash` ≡ `--expired` +
deprecation. Soft vs hard are two store functions (`LIVE` hard →
INTERNAL). `update --deleted --undelete` restores (mutex with
`--ttl`/`--expires`/`--clear-expires`). `add` revives the same key /
keyless body-hash (same `sync_id`). `purge --expired|--deleted` wipes
that whole bin (exactly one flag, same hatch); `purge-trash` ≡
`purge --expired` + deprecation. Hatch ASCII:
`hard delete requires exactly one registered device`. Graph/rekey VV
and `--sync-id` locators stay stage 4. Help/skill stay stage 6.

Gates (this stage): ctest 4/4 ASan/UBSan, store 113, coverage 100%
functions + effective lines, `lint-all` LINT OK. In-session deep-review:
no new grill locks. Auto-fixed lint/coverage (`clear_deleted_at` instead
of a dead bind-text branch; `handle_update_ttl_flag` split; VV miss-key
and hard-delete-by-key tests). Note for the second pass: CLI hatch after
reopen only stays COUNT>1 when the extra `device_id` sorts after the
local v7 (recovery `SELECT device_id LIMIT 1` follows TEXT PK order).

### Stage 4 (2026-09-21)

CLI `--sync-id` (canonical lowercase UUID v7) as the third locator form
alongside id / `--key` (mutex; same for graph `--from-sync-id` /
`--to-sync-id` / related `--sync-id`). Sugar `link ID ID` stays numeric
ids only. Graph default resolvers stay live+expired via `store_get_any*`;
`--deleted` expands to `store_get_row*` so deleted ends resolve (no silent
link without the flag); `--expired` rejected on graph. Default related /
stubs still omit deleted neighbors. `store_link` / `store_unlink` /
`store_rekey` bump VV on affected ends (via `bump_endpoints` +
`apply_vv_bump`); no-op unlink still skips the bump (004). `sync_id`
immutable across rekey. Suite `sync_locators` in `tests/gate-suites`.
Help/skill stay stage 6.

Gates (this stage): step_gate + store_asan green; coverage functions
100% + effective lines 100%; `lint-all` LINT OK. In-session deep-review:
no new grill locks. Auto-fixed related subject double-load.

## Review Notes

### 2026-09-20 — strict review (pre-implementation)

Round 7 landed in 005. This plan updated to match. Stage 1 TDD is
unblocked **if** the implementer treats 005 Round 7 as law.

Do not re-open: UUID v7, `bin` always enum, `--trash`→`--expired`,
CLI `--sync-id`, id-FK links, no JSON export, link kinds 3/5, field
order, facade byte-match.

### 2026-09-20 — Claude second-opinion deep review (stage 1)

Independent second pass (workflow:deep-review). Verified against 005 Round 7
by **running** the gate, not reading: ctest 4/4 under ASan/UBSan, coverage
functions 100% + effective lines 100%, `-Weverything -Werror` build clean,
clang-tidy clean. Confirmed correct: exit-3 matrix (`bin_status`) cell-for-cell;
all five sidecar-recovery branches; `store_get_any` excludes deleted; no
`last_seen` write on reads; UUID v7 mint in-adapter; faithful `StoreBin`
CLI migration with `--trash`→expired preserved.

**Fix applied (grill → "full table rebuild"):** fresh-create and v3-migrate
produced divergent schemas — fresh `sync_id TEXT NOT NULL UNIQUE` created a
redundant second index (inline autoindex **and** `ux_entries_sync_id`), while
ALTER-migrated columns were nullable. Now: dropped the inline `UNIQUE` (one
named index via `K_ENTRIES_INDEXES`), and migrate rebuilds `entries` via
`rebuild_entries_notnull` so migrated rows get `NOT NULL` sync_id/version_vector
— byte-identical to a fresh create. Rebuild runs with `foreign_keys=OFF`
(toggled around the txn in `ensure_schema`) so `DROP TABLE entries` does not
cascade `entry_links`/`entry_tags`; `foreign_key_check` verifies afterward.
Column list + indexes are single-sourced (`K_ENTRIES_COLUMNS`/`_INDEXES`) so
fresh and rebuild cannot drift. Tests assert NOT-NULL columns, no autoindex,
and that links **and** tags survive the FK-off drop.

**Grill decision:** `have_devices` param kept as scaffolding (Itay's call).

**Auto-fixed:** stale test name `store_open_migrates_v1_to_v2` → `_v1_to_v4`.

**Notes (out of stage-1 scope, left for Itay):** `skills/remember/SKILL.md`
project-status churn is unrelated to plan 14 (skill sync-docs are stage 6) —
commit separately. `-Wallocator-wrappers` fires only on Apple clang 21 under
`REMEMBER_TEST_HOOKS` (pre-existing `real_*` wrappers); not `-Werror`-promoted,
gate stays green; add `-Wno-allocator-wrappers` if silence is wanted.

### 2026-09-20 — Claude second-opinion deep review (stage 2)

Independent pass on output/bins/exit-tokens/aliases. Verified by running:
ctest 4/4 (ASan/UBSan), coverage functions 100% + effective lines 100%,
`-Weverything -Werror` clean, lint OK (format/cppcheck src+tests/gcc
-fanalyzer/clang-tidy). Confirmed against 005 Round 7: JSON entry field order
(id,sync_id,key,body,tags,source,created_at,updated_at,expires_at,deleted_at,
bin,version_vector[,links]) with `version_vector` a raw object; `bin` always
enum; stub `{id,sync_id,key,type,bin,preview}`, no `trash`; `--expired`/
`--deleted` mutex + `--trash` one-line deprecation (`cmd_bin_resolve`);
human `[expired]`/`[deleted]`; neighbor stubs carry sync_id+bin and default
paths drop only deleted neighbors (expired stay) via `cmd_neighbors_drop_deleted`,
`--deleted` includes them. `store_bin_of` is the single shared predicate
(entry_bin + output + drop-deleted all delegate). Neighbor loader refactored to
`neighbor_row_from_stmt`/`dup_col_req|opt`/`neighbor_append` (DRY), fail-closed
on OOM; both neighbor SELECTs add `sync_id,deleted_at`. `delete` correctly
stays hard (soft-delete is stage 3); `--deleted`/`--expired` bins simply select
empty bins until then. Golden test `test_sync_output.c` (8 tests) asserts field
order, bin values, deprecation/mutex, human marks. No product/arch forks — no
grill.

**Auto-fixed:** coverage script had *replaced* the `add_parse_free` defensive
exemption with `update_parse_free`, dropping a valid one (add_parse_free still
exists in cmd_add.c); restored both (harmless — script ignores unused
exemptions).

**Notes (not blocking):** general-help exit-3 line still reads
`expired / not_expired` though `--deleted` now makes `deleted`/`not_deleted`
reachable — reasonable to fold into stage 6 (help/skill). `SKILL.md`
project-status churn still uncommitted (carry-over from stage 1).

### 2026-09-20 — Claude second-opinion deep review (stage 3)

Independent pass on soft-delete / one-row hard delete / undelete / revive /
purge. Verified by running: ctest 4/4 (ASan/UBSan), coverage functions 100% +
effective lines 100%, `-Weverything -Werror` clean, lint OK. Confirmed against
005 Round 7: soft vs hard are two store functions (hard `LIVE` → INTERNAL);
COUNT==1 hatch (`NOT_SINGLE_DEVICE`) checked before any destructive op → exit 1
with clear ASCII; unflagged `delete` soft-deletes live-or-expired (clears
`expires_at`, keeps body/tags/key/sync_id/edges/FTS); `--expired`/`--deleted`
one-row hard CASCADE; `--trash` ≡ `--expired`; `update --undelete` (mutex with
ttl/expires, needs `--deleted` via bin locator); `add` revives deleted+expired
same key/hash preserving sync_id; `purge --expired|--deleted` bin-specific with
the same hatch, `purge-trash` ≡ `purge --expired`. `vv_increment` hand-rolled
JSON bump is sound (fixed-length UUID needle, overflow-guarded, snprintf
truncation fails safe) with edge tests; `del` finalize path has no
double-finalize.

**Grill → fixed now (Itay chose "fix in stage 3"):** plain `update`
(set_body/set_tags/set_expires) and keyed-upsert did NOT bump the version
vector — only soft-delete/undelete did — which would leave a body edit
invisible to stage-5 import dominance (criteria #12). Fix: single
`apply_vv_bump` at the end of `update_apply_changes` covering every update kind
(guarded against the undelete double-bump). Verified: add→VV1, `update --text`
→VV2, `update --tag`→VV3. Test extends `store_sync_id_stable_across_update` to
assert the bump. Supersedes stage-1's "update VV deferred to stage 4" note;
stage 4 keeps only graph (link/unlink/rekey) VV.

**Forward note (not this plan):** `devices_get_one` uses `SELECT … LIMIT 1`
with no ORDER BY and "sidecar wins" recovery replaces the single row. Correct
now (Round 7: `devices` is local-only, one row; COUNT>1 is a defensive/future
path only reachable by the test's manual insert). When multi-device
registration lands in a later log, revisit so a reopen cannot collapse a
legitimate multi-device registry. `VV_OUT_MAX=256` is ample for single-device;
re-check when stage-5 import can grow a VV with foreign device keys (bump fails
safe with `STORE_ERR_INTERNAL` if exceeded). `SKILL.md` churn still uncommitted.

### 2026-09-21 — Claude second-opinion deep review (stage 4)

Independent pass on `--sync-id` locators + graph `--deleted` + VV bumps on
link/unlink/rekey. Verified by running: ctest 4/4 (ASan/UBSan), coverage
functions 100% + effective lines 100%, `-Weverything -Werror` clean, lint OK.
Confirmed against 005 Round 7: locator mutex (exactly one of id | `--key` |
`--sync-id`, canonical-UUID validated → "invalid sync-id"); graph twins
`--from-sync-id`/`--to-sync-id` with per-end mutex; graph default resolvers
live+expired and **exclude deleted** (`store_get_any_by_*`), `--deleted`
includes deleted ends (`store_get_row*`), `--expired` on graph rejected;
`bump_endpoints` bumps updated_at + VV on **both** ends of a real link/unlink,
no-op unlink (`out_count==0`) does not bump; `store_rekey` bumps VV. Store
`store_get_any_by_sync_id` correctly returns NOT_FOUND for a deleted row.

**Auto-fixed (coverage gap):** `related --deleted --key` (subject-by-key in the
deleted bin, `cmd_graph.c` `related_load_subject`) was untested — the coder's
"100%" was a pre-final run. Added a `related --deleted --key gonek` assertion to
`cli_graph_deleted_via_key_and_sync_id`; coverage back to 100% effective.

No product/arch forks — no grill. `SKILL.md` churn still uncommitted.

### Stage 5 (2026-09-21)

`import --from-db` + `conflicts` + `conflict accept` (005 Round 7). New TU
`cmd_import.c` (wired in `REMEMBER_LIB_SOURCES` + `-Wcast-qual`). Store port:
`store_import` / `store_conflicts_list` / `store_conflict_accept` in the
adapter only — foreign DB opened `SQLITE_OPEN_READONLY` (never `store_open`);
refuse `user_version` < 4; never touch foreign sidecar; never INSERT foreign
`devices` (COUNT stays 1). Merge by `sync_id` with VV compare (missing key = 0):
incoming dominates → apply + pairwise-max then bump local; local dominates /
identical content (body+tags+key+delete state) → pairwise-max only;
concurrent different → `conflicts` reason `concurrent_vv`. Unique key/hash
clash (any bin) → `key_clash`/`hash_clash` row, never SQLITE constraint error.
Link remap after entries via sync_id (`INSERT OR IGNORE`; skip missing ends).
`VV_OUT_MAX` raised 256 → 4096 for foreign VV keys in pairwise-max.

Accept: `--keep local|incoming|both`; `both`+`concurrent_vv` remints incoming
sync_id with `{local:1}` (keyless when local still holds the key);
`both`+clash keeps incoming sync_id keyless if needed. Closed reason enum.
JSON: `action:imported` counts envelope; conflicts list; `action:accepted`
entries. Suite `sync_import` (28 tests: re-import upsert + hash demote
refuse). Gate: step_gate green, lint OK, coverage 100% fn + effective
lines. Skill/help still stage 6; `SKILL.md` left alone this stage.

**In-session deep-review auto-fix:** `conflict accept --keep incoming` on
`concurrent_vv` demotes another unique occupant before apply (was raw SQLITE
`database error`). Reason tokens unified via `store_conflict_reason_str`.

**Grill locked (2026-09-22):**
1. `decision:sync-import-reimport-idempotent` — concurrent insert pairwise-maxes
   local VV; open conflict by `(sync_id, reason)` is upserted (refresh
   snapshots), never duplicated on re-import.
2. `decision:sync-import-hash-demote` — on keyed clash keep local / demote
   imported (incoming keyless until accept). Never mint `key=sync_id`. Both
   keyless / hash slot contested → `STORE_ERR_UNIQUE_TAKEN` clear ASCII
   (`cannot free unique key or body hash without inventing a key`).

### 2026-09-22 — Claude second-opinion deep review (stage 5)

Independent pass on `import --from-db` + `conflicts` + `conflict accept`.
Loaded both new decision keys before reviewing. Verified by running: ctest 4/4
(ASan/UBSan), coverage functions 100% + effective lines 100%, `-Weverything
-Werror` clean, lint OK. **Clean pass — no auto-fix, no grill** (both grill
locks were pre-applied and implemented exactly).

Confirmed against 005 Round 7 + the two new decisions:
- `store_import` opens the source `SQLITE_OPEN_READONLY` (never `store_open`),
  refuses `user_version < 4` (`SOURCE_TOO_OLD` → "source database is older than
  this remember", exit 1), never touches the foreign sidecar, never INSERTs
  foreign `devices` (COUNT stays 1), rolls back + closes src on any failure.
- `vv_compare` is a correct VV comparison (missing key = 0; dominates iff all ≥
  and one >). Incoming-dominates → unique-check-before-apply (clash → conflict
  row, not raw SQLITE), apply fields, `vv_union_bump`. Local-dominates or
  concurrent+identical (body+tags+key+delete-state, excl. expires_at/source) →
  pairwise-max VV only, unchanged.
- **Reimport idempotent:** `record_concurrent_vv` pairwise-maxes local VV (so a
  re-import makes local dominate) AND upserts the open `(sync_id, reason)`
  conflict (`find_open_conflict` → update vs insert). Double-guarded. Test
  `cli_import_concurrent_reimport_idempotent`.
- **Hash-demote:** `insert_snapshot_row` demotes the incoming copy on a keyed
  clash (keyless), and on a keyless hash clash with no free slot returns
  `STORE_ERR_UNIQUE_TAKEN` — never `key=sync_id`. Tests
  `..._both_refuse_fake_key`, `..._demote_hash_escape_refuses`.
- Accept local/incoming/both: unions incoming VV into survivors; `both`+
  `concurrent_vv` remints incoming `{local:1}`, `both`+clash keeps incoming
  sync_id keyless if needed. Closed `reason` enum (bad reason on disk → error).
- Link remap after entries via sync_id, `INSERT OR IGNORE`, skips edges whose
  ends aren't both present locally. `VV_OUT_MAX` raised 256 → 4096 (resolves the
  stage-4 forward note about foreign-key VV growth).
- JSON envelopes (`imported` counts / `conflicts` list / `accepted`), exit 0
  even with conflicts. `sync_import` suite (28 tests) covers every rule + both
  decisions + fail-fast (bad VV, bad reason).

**Note (stage 6):** `import`/`conflicts`/`conflict` are not yet in the facade
byte-match test (`test_facade.c`) — they use `app_out()`/`app_err()` so they
are facade-safe by construction, but the must-pass "facade byte-match for
new/changed commands" needs their cases added in stage 6 (facade parity).
`SKILL.md` project-status churn still uncommitted.
