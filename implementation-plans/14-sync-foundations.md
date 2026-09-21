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
