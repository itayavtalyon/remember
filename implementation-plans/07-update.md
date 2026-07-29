# 07 — `update`

## Goal

Opt-in `--text` / `--tag` / `--clear-tags`; locate by id or `--key`; keyless body-hash conflict only.

## Behavior

- At least one change flag required.
- `--tag` + `--clear-tags` → usage error.
- Successful update **always** bumps `updated_at` (even if body equal).
- Never change `source` or `key`.
- Keyed: no body-hash conflict check.
- Keyless: conflict with other keyless hash → exit 1, conflicting id on stderr.
- JSON: `action:updated`, full entry in `entries`.

## Store API

```c
int store_update(Store *,
                 /* locator */ int64_t id, /* or 0 */ const char *key_or_null,
                 bool set_body, const char *body, const char *hash,
                 bool set_tags, const char *const *tags, size_t ntags, /* set_tags clear => ntags 0 */
                 Entry *out);
```

Prefer explicit flags over magic.

## Tests that must pass

- `update_text_only_keeps_tags`
- `update_tags_only_keeps_body`
- `update_clear_tags_flag`
- `update_tag_and_clear_tags_rejected`
- `update_same_text_still_succeeds_and_returns_entry`
- `update_text_and_tags_together`
- `update_no_change_rejected`
- `update_missing_id_exits_two`
- `update_body_hash_collision_rejected`
- `update_empty_text_rejected`
- `update_source_immutable`
- `update_json_shape`
- `update_text_stdin_dash`
- `update_positional_body_not_accepted`
- `update_replace_tags_multiple`
- `update_by_key_text`
- `update_keyed_no_body_hash_conflict`
- `update_by_key_missing_exits_two`
- `update_both_id_and_key_rejected`
- `update_rejects_source_flag` (V22)
- `update_preserves_id_key_source_created_at` (V23)
- `update_invalid_utf8_text_rejected` (V30)
- `fts_search_reflects_body_update` (V25, with search)

## More tests?

| Gap | Status |
|-----|--------|
| missing key / both locators | **Added** |
| FTS after update | **Added** |

## Done checklist

- [x] All must-pass update tests green
- [x] FTS resync on body/tag change

## Review notes (2026-07-26)

### Implementation

- **`store_update`:** id or key locator; independent `set_body` / `set_tags`; always bumps `updated_at`; FTS resync in the same write txn. Keyless body-hash collision → `STORE_ERR_CONFLICT` + `out_conflict_id`. Tag replace = delete links → re-link → orphan GC. Never touches `source`/`key`.
- **CLI:** `cmd_update` in `cmd_locator.c` (same id/`--key` contract as get/delete). Shared `load_body` in `commands_common` (add + update). Help for `update` in `main`.
- **Gates:** suite `update` + keyed cases in `key_gld` + update edges in `verification_gld` + `fts_search_reflects_body_update` in `verification_search`.
- Removed public NYI exit for update (stable exits remain 0/1/2 only).

### Step review

- Verdict: **Approve** (gates: build `-Werror`, full step gate 251 tests, store unit, `lint-all.sh`, coverage 100% functions + effective lines).
- No open **blocking** items.
- Nits/follow-ups: optional split of `cmd_locator.c` if update+get+delete grow further; conflict message wording is CLI-owned (store returns id + `STORE_ERR_CONFLICT`).

### Pragmatic write-gate

- **DRY:** reused locator helpers, `load_body`, `fts_resync`, `replace_body`/`touch_updated_at`/`union_tags`/`gc_orphan_tags` — no parallel SQL/parsers. Cleared.
- **Orthogonal:** CLI parse ≠ store update policy ≠ FTS. `STORE_ERR_CONFLICT` is store-domain; stderr conflict text is CLI. Cleared.
- **Easy-to-replace:** only `store_sqlite.c` includes SQLite; public surface is `store_update` on the port. Cleared.
- **Hard-gate:** no open Criticals; Improvements fixed or N/A.

### PR-readiness review (2026-07-26)

- Re-ran gates on full working tree: build `-Werror`, step gate **256** tests, store unit **51**, `lint-all.sh` **LINT OK**, smoke add→update→get→search.
- Verdict: **Approve with nits** (ready for PR after commit).
- **Timestamps:** Documented design Round 9 + engineering notes — `utc_now` emits `YYYY-MM-DDTHH:MM:SS.mmmZ` (fixed 3-digit ms). Call out in PR body.
- **Nits:** (1) `parse_locator_args` vs `parse_update_args` still share contract knowledge only via validate/resolve — full flag-parser merge deferred. (2) `cmd_locator.c` ~440 lines — split only if it grows again. (3) Plan sketch lacks `out_conflict_id` / `StoreStatus`; implementation is the source of truth.
- Extra green tests beyond plan must-pass (shared-tag GC on replace, key clear-tags, FTS tag resync, list order after update, explicit `updated_at` bump) strengthen the suite; keep them.
- No open **blocking** items.
