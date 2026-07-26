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

- [x] All must-pass search tests green (except `fts_search_reflects_body_update` → step 07)
- [ ] FTS rowid == entry id verified (by search finding after update in 07)

## Carried from step 05 review

1. **Reuse list filter + paging patterns** — ✅ `list_append_filters` + `ListParse` / `list_prepare_query` shared; `SearchQuery.filters` is a `ListQuery`.
2. **FTS ownership stays in the store adapter** — ✅ only `store_sqlite.c` touches FTS.
3. **Gate green FTS edges as they land** — ✅ suite `verification_search` + `search` + `key_gld` (`search_filter_by_key`) in `tests/gate-suites`.
4. **Suite names** — ✅ `verification_search` added deliberately (not overloading `verification_gld`).
5. **`commands.c` size** — ✅ split by concern: `commands_common` + `cmd_add` + `cmd_locator` + `cmd_query` (see engineering notes).
6. **Output** — ✅ `emit_entry_page` → `output_list_envelope` / `output_entry_human_line`.

## Review notes (2026-07-26)

- FTS5 `MATCH`/`bm25()` need the **table name** `entries_fts`, not a JOIN alias.
- Invalid MATCH → `STORE_ERR_QUERY` via errmsg heuristics (`fts5` / `syntax error` / …).
- Left red for step 07: `fts_search_reflects_body_update`.
- Commands split: public `commands.h` unchanged; internal `commands_common.h` for shared helpers only.
- **Post-split lint:** `misc-include-cleaner` requires direct `#include "store.h"` / `"normalize.h"` in each `cmd_*.c` and `commands_common.c` (transitive via public headers is not enough). Fault-injection sweep complexity fixed by extracting `sweep_list_faults` / `sweep_search_faults`.
- Step review verdict: **Approve** after lint fixes (gates: build/tests/lint/coverage green). Open nits only (errmsg heuristic fragility; optional txn-wrapper DRY).
