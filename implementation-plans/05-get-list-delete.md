# 05 — `get` / `list` / `delete`

## Goal

Read paths + hard delete; id **or** `--key` locator; list filters + paging.

## Behavior

- `get`: exactly one of id / `--key`; exit 2 missing; JSON envelope `entries` (count 1); keyless shows `"key":null`.
- `list`: `--tag` AND, `--source`, `--key`, `--limit` (default 20, max 1000), `--offset` (≥0); sort `updated_at DESC`; JSON includes `offset`,`limit`,`count`,`total`.
- `delete`: load then delete; JSON `action:deleted` echoes pre-delete entry; orphan tags GC; FTS delete.

## Store API

```c
int store_get_by_id(Store *, int64_t id, Entry *out);
int store_get_by_key(Store *, const char *key, Entry *out);
int store_list(Store *, const ListQuery *q, Entry **out, size_t *count, size_t *total);
int store_delete_by_id(Store *, int64_t id, Entry *out_deleted);
int store_delete_by_key(Store *, const char *key, Entry *out_deleted);
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

- [ ] All must-pass tests green
- [ ] Locator validation only in CLI/commands, not duplicated SQL inconsistently
