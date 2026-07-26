# 05 — `get` / `list` / `delete`

## Goal

Read paths + hard delete; id **or** `--key` locator; list filters + paging.

## Behavior

- `get`: exactly one of id / `--key`; exit 2 missing; JSON envelope `entries` (count 1); keyless shows `"key":null`.
- `list`: `--tag` AND, `--source`, `--key`, `--limit` (default 20, max 1000), `--offset` (≥0); sort `updated_at DESC`; JSON includes `offset`,`limit`,`count`,`total`.
- `delete`: load then delete; JSON `action:deleted` echoes pre-delete entry; orphan tags GC; FTS delete.

## Store API

```c
StoreStatus store_get(Store *, long long id, Entry *out);
StoreStatus store_get_by_key(Store *, const char *key, Entry *out);
StoreStatus store_list(Store *, const ListQuery *q, Entry **out, size_t *count, size_t *total);
StoreStatus store_delete_by_id(Store *, long long id, Entry *out_deleted);
StoreStatus store_delete_by_key(Store *, const char *key, Entry *out_deleted);
```

## Tests that must pass

### get/list/delete

- `get_existing_json_envelope`
- `get_missing_exits_two`
- `get_human_shows_body`
- `list_empty_exits_zero`
- `list_default_order_updated_at_desc`
- `list_filter_tag_and`
- `list_filter_source`
- `list_limit`
- `list_limit_default_is_twenty`
- `list_limit_zero_rejected`
- `list_limit_over_hard_cap_rejected`
- `list_json_paging_fields`
- `list_offset_pages`
- `list_offset_negative_rejected`
- `delete_existing`
- `delete_missing_exits_two`
- `delete_json_shape`

### key locators / filters

- `get_by_key_missing_exits_two`
- `get_both_id_and_key_rejected`
- `get_neither_id_nor_key_rejected`
- `delete_by_key`
- `list_filter_by_key`
- `json_entry_includes_key_field_null_when_keyless`

### config

- `db_flag_isolates_stores`
- `remember_db_env_used_when_no_flag`
- `db_flag_wins_over_env`
- `json_entry_has_required_fields`
- `json_entry_includes_key_field_null_when_keyless`

### GC / source / JSON errors (V22, V24, V28, V34)

- `get_rejects_source_flag`, `delete_rejects_source_flag`
- `orphan_tag_removed_when_last_use_deleted` (needs `sqlite3` CLI)
- `shared_tag_survives_when_other_entry_uses_it`
- `list_no_matches_json_total_zero`
- `json_get_missing_empty_stdout`
- `delete_by_key_missing_exits_two` (already in suite)
- `human_list_preview_first_line_only`, `human_list_preview_truncates_long_line` (V32 — list human path)

## More tests?

| Gap | Status |
|-----|--------|
| list `--key` + `--tag` AND | Optional |
| Human key column blank | Soft |

## Done checklist

- [x] All must-pass tests green
- [x] Locator validation only in CLI/commands, not duplicated SQL inconsistently

## Carried from step 04 review

- **List filters** (`--tag` AND, `--source`, `--key`, `--limit`/`--offset`) — thin list in step 04 rejects unknown options; implement full `ListQuery` here.
- **Full get/list/delete** suite in `get_list_delete` + remaining `key` cases (`delete_by_key`, `list_filter_by_key`, …).
- **Output escaping on every body surface** — keep using `output.c` for JSON + human (never raw `fputs` of a stored body). Keys/tags stay control-free via normalize; if that invariant ever changes, escape them too.
- **Fold remaining green verification edges into the step gate** as they land (do not leave them only in mixed-red `verification_edges`).

## Review notes (2026-07-26) — implementation

- **Store:** `ListQuery` + filtered `store_list`; `store_delete_by_id` / `store_delete_by_key` (load → FTS delete → CASCADE entry delete → orphan tag GC, one txn).
- **Commands:** shared locator parse for get/delete; full `cmd_list` filters; human delete silent; JSON delete via `output_action_envelope(..., "deleted", ...)`.
- **Limits:** CLI default 20 / hard cap 1000 / offset ≥ 0; store does not invent defaults. CLI also caps `--tag` count (`LIST_TAG_FILTER_MAX` 50) under store `LIST_BIND_CAP`.
- **List paging:** COUNT + paged SELECT in one **read** transaction so empty pages still report unpaged `total` (see `list_offset_past_end_keeps_total`).
- **Tests:** `step05_gate` = `cli_global,store,normalize,add,get_list_delete,json_db_config,key_gld,verification_gld`. Green key delete/list + V22/V24/V28 edges folded into gated suites. Unit store tests for list filters + delete GC under ASan. Extra: parameterized tag with `'`, over-cap tags, offset past end.
- **Still red (later steps):** update, search, sync-path warning, FTS search verification.

## Review notes (2026-07-26) — formal step review (PR readiness)

**Verdict: Approve with nits** — ready for next step / PR after commit; no blocking defects.

### Blocking
(none)

### Important — fixed (same day follow-up)
1. **Delete load under write lock** — `delete_entry_tx`: BEGIN IMMEDIATE → load → FTS delete → DELETE → orphan GC → COMMIT.
2. **Gate suite rename** — `key_gld` / `verification_gld` (was `key_add` / `verification_add`); CMake `step05_gate` updated.

### Nits — fixed (same day follow-up)
- Shared `push_cstr_ptr` for add/list tag argv lists.
- `main`: `run_with_store` helper for open/run/close.
- Human get/list check `output_*` write failures.
- Plan API sketch uses `store_get` (not `store_get_by_id`).
- `commands.c` split deferred to step 06 plan (search/update).

### Confirmed OK
- Port boundary: only `store_sqlite.c` includes SQLite; lint gate clean.
- Locator policy only in commands (exactly one of id/`--key`); filters/normalize at CLI; store takes `ListQuery`.
- Tag/source/key filters parameterized (quote regression test); CLI tag-cap → user error not `STORE_ERR_INTERNAL`.
- Output: JSON envelopes + human body/preview neutralization; never raw body `fputs`.
- Orphan-tag GC + FTS delete in the delete transaction; shared tags retained.
- Gates: build `-Werror`, `step05_gate`, `store_asan_gate`, `scripts/lint-all.sh` LINT OK.
