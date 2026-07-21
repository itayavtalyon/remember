# 04 — `add` (keyless dedupe + keyed upsert)

## Goal

Implement `remember add` end-to-end via `store_add(...)`.

## Behavior summary

- Keyless: insert or body_hash merge (union tags, bump `updated_at`, keep source/created_at/body).
- Keyed: upsert by key (replace body, union tags, keep id/source/created_at/key, bump `updated_at`); **no** body_hash uniqueness.
- Actions JSON: `created` | `merged` | `updated` (keyed upsert).
- Human: print id only; JSON: uniform envelope with `action`, `count`, `entries:[full]`.
- stdin body `-`; `--source`; `--tag` repeatable; `--key` optional.

## Store API additions

```c
typedef enum { STORE_ADD_CREATED, STORE_ADD_MERGED, STORE_ADD_UPDATED } StoreAddAction;

int store_add(Store *s,
              const char *body,          /* already trimmed */
              const char *body_hash,
              const char *key_or_null,   /* normalized or NULL */
              const char *const *tags, size_t ntags,
              const char *source,
              StoreAddAction *out_action,
              Entry *out_entry);         /* heap or caller buffer — pick one style */
```

FTS insert/resync inside the same transaction.

## Pragmatic notes

- Commands orchestrate normalize → store_add → output.
- **No SQL in `commands.c`.**

## Tests that must pass

### Keyless add (`test_add.c`)

- `add_basic_prints_id_one`
- `add_with_tags_and_source_human`
- `add_default_source_is_unknown`
- `add_source_agent_tool_accepted`
- `add_invalid_source_rejected`
- `add_empty_body_rejected`
- `add_whitespace_only_body_rejected`
- `add_body_trimmed_before_store`
- `add_dedupe_same_body_merges_tags`
- `add_dedupe_trim_equivalent_bodies`
- `add_dedupe_keeps_original_source`
- `add_json_created_shape`
- `add_json_merged_shape`
- `add_stdin_body_dash`
- `add_body_over_64kib_rejected`
- `add_tag_ascii_casefold`
- `add_empty_tag_rejected`
- `add_tag_with_space_rejected`
- `add_tag_too_long_rejected`
- `add_tag_project_colon_style_allowed`
- `add_missing_body_rejected`
- `second_add_gets_id_two`

### Keyed add (`test_key.c`)

- `add_key_creates_slot`
- `add_key_upsert_same_id_replaces_body`
- `add_key_upsert_unions_tags`
- `add_key_keeps_original_source`
- `keyed_entries_may_share_body_text`
- `keyless_still_dedupes_among_keyless_only`
- `add_key_ascii_casefold`
- `add_empty_key_rejected`

### Normalization / UTF-8 / source / created_at (V21–22)

- `add_tag_casefold_merges_to_one_tag_name`
- `add_tag_control_char_rejected`
- `add_tag_invalid_utf8_rejected`
- `add_key_control_char_rejected`
- `add_invalid_utf8_body_rejected`
- `keyless_merge_keeps_created_at`

### Config / dir (partial)

- `db_parent_dir_mode_0700`
- `json_add_error_empty_stdout` (V34)
- Isolation tests: prefer thin `get` early or defer to 05

## More tests?

| Gap | Status |
|-----|--------|
| Invalid UTF-8 body/tag via stdin/argv | **Added** |
| Key with spaces | Covered by tag space + key normalize same rules — optional duplicate |

## Done checklist

- [ ] All listed add + key-add tests green
- [ ] FTS row written on add (search may still fail until 06)
- [ ] Format
