# 06 — FTS5 `search`

## Goal

Ranked full-text search with filters and paging. FTS maintained only via store mutators (already writing); search reads `entries_fts`.

## Behavior

- Query required non-empty; invalid FTS syntax → exit 1.
- MATCH on body+tags; optional `--tag` AND, `--key`, `--source`.
- Rank bm25, tie-break `updated_at DESC`.
- `--limit` / `--offset`; JSON paging fields; full bodies in JSON; human preview ≤80 **codepoints**.
- Empty hits → exit 0, `total:0`.

## Store API

```c
int store_search(Store *, const SearchQuery *q, Entry **out, size_t *count, size_t *total);
```

## Tests that must pass

- `search_finds_body_token`
- `search_finds_tag_token_via_fts`
- `search_tag_filter_ands_with_query`
- `search_source_filter`
- `search_empty_query_rejected`
- `search_missing_query_rejected`
- `search_no_matches_exits_zero`
- `search_json_paging_fields`
- `search_json_includes_full_body`
- `search_human_preview_not_only_id`
- `search_limit`
- `search_multi_tag_and_filter`
- `search_filter_by_key`
- `search_invalid_fts_syntax_rejected`
- `search_unbalanced_quote_rejected` (V27)
- `search_finds_compound_tag_component` (V26)
- `search_diacritics_folded` (V26)
- `fts_search_reflects_body_update` (V25 — needs update from 07; green after 07 or implement search+update order)
- `fts_search_empty_after_delete` (V25 — needs delete from 05)
- `fts_search_reflects_keyed_upsert` (V25)

## More tests?

| Gap | Status |
|-----|--------|
| Invalid FTS | **Added** (AND AND + unbalanced quote) |
| Diacritics / compound tags | **Added** |
| FTS after mutate | **Added** — pass with 05–07 |

## Done checklist

- [ ] All must-pass search tests green
- [ ] FTS rowid == entry id verified (by search finding after update in 07)
