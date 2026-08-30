# 12 — TTL, expiry, and trash (P6)

## Goal

Optional `expires_at` on entries: durable by default; expired rows are a
virtual trash bin (hidden from default list/search/get); restore or
permanent delete. **Design (source of truth, including Round 4):**
[`design-logs/003-ttl.md`](../design-logs/003-ttl.md).

Do not invent. If a case is missing from 003, stop and ask.

## Scope

**In:** schema `user_version` 2 (**no** `expires_at` index); store bin
filters + `const char *now`; `--ttl` / `--expires` / `--clear-expires` /
`--trash`; `purge-trash`; revive-on-add; exit **3** (`expired` /
`not_in_trash`); JSON `expires_at`; skill/help; tests + coverage.

**Out:** auto-GC timer; mixed include-expired; persisting non-UTC
timestamp strings (date-only `--expires` is **local EOD → UTC Z on
save**, which **is** in); Mac UI (remember-mac plan 11 after pin bump);
Share UI expiry; human-list column for `expires_at`; production clock
port; snapshot cap on `purge-trash` JSON.

## Must-pass tests

| Check | Proves |
|-------|--------|
| Fresh DB `user_version=2`, column present; **no** `expires_at` index | bootstrap |
| Copy of a v1 file migrates; old rows `expires_at` NULL | migrate |
| Add without flags → JSON `expires_at: null`; in default list | default durable |
| `--ttl 1h` stored as canonical `.mmmZ` `> now`; in default list | relative |
| `--expires` past (full `…Z`) → only `--trash` list | absolute + trash |
| `--expires …:59Z` stored as `…:59.000Z` | normalize |
| Default list/search/tags omit expired; `--trash` sees them | hide |
| `expires_at == now` is trash (store fixture `now`) | boundary |
| `get` expired → **exit 3**, stderr exactly `remember: expired` | wrong bin |
| `get --trash` expired → 0 + row | trash read |
| `get`/`update`/`delete --trash` of **active** → exit 3, `not_in_trash` | other direction |
| `get`/`update`/`delete` expired **without** `--trash` → exit 3, `expired` | locators active-only |
| True-missing id → exit **2** (not 3) | not-found stays 2 |
| `update --trash --clear-expires` restores | restore |
| `update --trash --ttl 7d` leaves trash when new expiry is future | restore-with-TTL |
| Keyless add of expired body → same id, `expires_at` null | revive |
| `purge-trash` deletes only expired; FTS/tag GC; empty → count 0 | empty trash |
| `purge-trash --json` returns **all** snapshots (no cap) | envelope |
| `--ttl` + `--expires` → usage exit 1 | flag mutex |
| Invalid `--ttl` (`0`, `7`, `07d`, `7x`, `1.5d`, `1M`) → usage | grammar |
| Human `list` line still 5 columns (no `expires_at`) | human contract |
| Facade `remember_run` byte-match includes new flags / `purge-trash` | in-process |

## Stages

TDD. One failing test at a time. After each stage: tests green, then
strict review (code not only diff); persist Implementation/Review Notes.

1. Schema + `Entry.expires_at` + v0-create-at-2 + v1→v2 migrate. **No index.**
2. Store: bin filters + `now`; get/update/delete wrong-bin; add revive;
   `set_expires`; `purge_trash`. Store unit tests with fixture `now`.
3. CLI parse (`--ttl` / `--expires` / `--clear-expires` / `--trash`) +
   `cmd_purge.c` + JSON `expires_at` after `updated_at` + help +
   `REMEMBER_WRONG_BIN` + `store_status_to_exit`.
4. Skill + `tests/gate-suites` + this plan Implementation Notes.
5. Full suite + lint + coverage (store/commands) + facade.

## Definition of Done

- [x] Design log 003 unchanged except Implementation Results
- [x] Must-pass table green; `user_version` 2; no `expires_at` index
- [x] Public exits 0/1/2/**3**; tokens `expired` / `not_in_trash`
- [x] Skill/help match flags and exit 3
- [ ] remember-mac pin bump is a **follow-up** (plan 11)

## Implementation Notes

### Stage 1 (schema + Entry.expires_at)

- Fresh CREATE includes `expires_at TEXT` and sets `user_version = 2`. No
  `expires_at` index.
- `ensure_schema`: `2` ok; `0` create-at-2; `1` `ALTER TABLE ... ADD COLUMN
  expires_at TEXT` then bump to 2; `>2` refuse newer; other (incl. negative)
  unsupported. Create and migrate share one `BEGIN IMMEDIATE` so a concurrent
  winner is visible on re-read (same race as v1 create).
- `Entry.expires_at` is heap-owned (NULL = durable). All entry SELECTs include
  the column; `store_entry_free` releases it. Writes still omit it (NULL
  default) until stage 2 `store_add` / `set_expires`.
- Tests: `store_open_creates_user_version_2` (column + no index),
  `store_open_migrates_v1_to_v2` (compact v1 file via sqlite3 CLI),
  `store_get_loads_expires_at` (NULL on add; SQL UPDATE then load).

### Stage 2 (store bins / revive / purge)

- New statuses: `STORE_ERR_EXPIRED` / `STORE_ERR_NOT_IN_TRASH` (`expired` /
  `not_in_trash`). `utc_now` is public (CLI calls it once per command).
- Locator ops take `trash` + `now`. Load the row, then bin-check: missing →
  `NOT_FOUND`; wrong bin → expired / not_in_trash. `expires_at == now` is trash
  (`strcmp` on canonical `.mmmZ`).
- `ListQuery.trash`; list/search/tags bind `now` and filter
  `expires_at IS NULL OR > now` vs `IS NOT NULL AND <= now`. No include-both.
- `store_add(..., expires_at, now)`: insert writes expiry; expired keyless/keyed
  hit revives (keep id, union tags, incoming expiry or NULL, keyed replaces
  body, bump `updated_at`). Active merge/upsert does **not** change expiry
  (not specified in 003 — use `update` to set/clear on an active row).
- `store_update` gains `set_expires` + `expires_at` + locator `trash`/`now`.
  At least one of body/tags/expires. `set_expires` + NULL clears.
- `store_purge_trash(now)`: one write txn, all expired snapshots (no cap), FTS
  delete + tag GC. Empty → count 0 / NULL array.
- Callers (cmd_*, existing tests) pass `trash=false` and a `now` snapshot until
  stage 3 parses `--trash`.

### Stage 3 (CLI parse + purge + JSON + help)

- `REMEMBER_WRONG_BIN = 3`; `store_status_to_exit` maps expired / not_in_trash.
- `--ttl` / `--expires` parsed in `commands_common` (`resolve_expiry_flags`).
  `--ttl` is `^[1-9][0-9]*[mhdw]$`; add seconds to command `now`; overflow or
  UTC year outside 0001–9999 → usage 1. `--expires` date-only = local EOD
  23:59:59.999 → UTC; Z form padded/truncated to 3 ms digits.
- `--trash` on get/delete/update/list/search/tags. `--clear-expires` on update.
  Mutex `--ttl`+`--expires` and `--clear-expires`+`--ttl`/`--expires`.
- `CLI_CMD_PURGE_TRASH` + `cmd_purge.c` (lib + `-Wcast-qual`). JSON
  `{version,action:deleted,count,entries:[all]}`. Human: integer N.
- JSON `"expires_at"` after `"updated_at"` (`null` or ISO). Human list still
  5 columns.

### Stage 4 (skill + gate-suites)

- `skills/remember/SKILL.md`: flags, exit 3, revive, purge-trash, JSON field.
- `tests/gate-suites` includes `ttl`. Facade parity includes `purge-trash` and
  `list --trash`.

### Stage 5 (gate)

- `remember_tests` gate suites green (304 tests). `remember_store_tests` 73/73.
- `scripts/lint-all.sh` LINT OK after extracting `resolve_expiry_flags` (cognitive
  complexity) and dropping sscanf for digit parse.

## Review Notes

### Stages 3–5 (2026-08-24)

**Verdict:** Approve with nits after lint fixes (cognitive complexity on
`cmd_add`/`cmd_update` extracted to `resolve_expiry_flags`; no sscanf on
ISO tokens; tags SQL no longer uses a bool ternary).

**Strengths:** One expiry parser for add+update; purge is its own TU wired
twice; facade byte-match includes new command/flags; skill matches the
locked flags and exit 3.

**Nits:** `--ttl` overflow/year-range is exercised via format failure more
than a dedicated huge-token CLI case (`+7d` is covered). Date-only
`--expires` depends on `TZ` (tests that need determinism use `…Z`).

### Stage 1 (2026-08-24)

**Verdict:** Approve with nits.

**Strengths:** v0 create-at-2 and v1 migrate share one write lock; `expires_at`
load/free is wired through every entry SELECT; no index.

**Nits:** `ensure_schema` else-after-re-read is a defensive TOCTOU path (same
class as the old concurrent-create fallback). Coverage later.

### Stage 2 (2026-08-24)

**Verdict:** Approve with nits. Existing CLI suites still green (commands pass
`trash=false` + one `utc_now` until stage 3).

**Strengths:** Bin check is one helper (`load_then_check_bin`); list/search
share `list_append_bin`; purge is one write txn with FTS + tag GC.

**Important:** Active keyless merge / keyed upsert does not apply incoming
`expires_at` (only insert + revive do). 003 specifies revive for expired hits
and insert for new rows; changing TTL on an existing *active* row is `update`.
If `add --ttl` of a duplicate active body should also set expiry, say so.

**Nits:** `store_tags` uses two SQL strings (active vs trash) rather than a
shared binder; fine at this size. `utc_now` is declared on `store.h` so CLI
can call it without a clock port.
