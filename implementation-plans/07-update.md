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

- [ ] All must-pass update tests green
- [ ] FTS resync on body/tag change
