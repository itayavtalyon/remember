# Test coverage matrix (design log #001, criteria 1–34)

Plans: [`../implementation-plans/INDEX.md`](../implementation-plans/INDEX.md).

## Suite status (red phase)

CLI black-box tests against skeleton (`exit 3`). **All tests must fail** until implemented.

Dev tools for inspect tests: `sqlite3` on PATH (`brew install sqlite` if needed).

## Verification criteria map

| V# | Topic | Primary tests |
|----|--------|----------------|
| 1 | Fresh add id 1 | `add_basic_prints_id_one` |
| 2 | Trim dedupe | `add_dedupe_trim_equivalent_bodies`, merge tags |
| 3–4 | Search/list filters | search_*, `list_filter_tag_and` |
| 5 | Missing → 2 | get/delete/update missing |
| 6 | Update hash conflict | `update_body_hash_collision_rejected` |
| 7 | Update tags / clear / bump | update_* suite |
| 8 | Empty body / size | add empty/whitespace/64kib |
| 9 | Paging JSON | list/search paging tests |
| 10 | `--db` isolation | `db_flag_isolates_stores` |
| 11 | Concurrent | `store_open_concurrent_create_all_succeed` (create race); write-path stress still manual |
| 12 | Skill | plan 10 |
| 13 | Lint | plan 08 |
| 14 | user_version>1 | `schema_user_version_too_new_refused` |
| 15 | JSON envelope | add/get/delete/update json shapes |
| 16 | Sync path warn | `sync_path_warning_on_clouddocs_marker` |
| 17–20 | `--key` | `test_key.c` |
| 21 | Tag/key normalize | add tag/key + `add_tag_casefold_merges_*`, control/utf8 |
| 22 | source rules | default unknown; reject on get/update/delete; keep on merge |
| 23 | Update invariants | `update_preserves_id_key_source_created_at` |
| 24 | Orphan tag GC | `orphan_tag_*`, `shared_tag_survives_*` |
| 25 | FTS hand-sync | `fts_search_reflects_*` |
| 26 | Compound tag / diacritics | `search_finds_compound_tag_*`, `search_diacritics_folded` |
| 27 | Unparseable FTS is a warning on exit 0 (006 E3.1) | `search_unbalanced_quote_warns`, `union_search_*`, `store_search_*` (plan 18 stage 3) |
| 28 | Empty = success | search/list empty total 0; an empty or blank search query is an empty result (`search_empty_query_is_an_empty_result`, `search_whitespace_only_query_is_an_empty_result`, plan 18) |
| 29 | stdin | add/update `-` |
| 30 | Invalid UTF-8 body | `add_invalid_utf8_body_*`, `update_invalid_utf8_*` |
| 31 | Filters/sort/limits | list/search suites; `list_dates` (`--sort`, `--since`, `--until`) |
| 32 | Preview edges | `human_list_preview_*` |
| 33 | Config + 0700 | db env tests + `db_parent_dir_mode_0700` |
| 34 | JSON errors → stderr | `json_*_empty_stdout` |

## Files

| File | Area |
|------|------|
| `test_cli_global.c` | Meta |
| `test_add.c` | add keyless |
| `test_get_list_delete.c` | get/list/delete/paging |
| `test_search.c` | FTS search |
| `test_store_search.c` | `store_search` union: P prefix part, E2.1a parse probe, `(P) OR (R)`, de-duplication, E2.4 ranking; keys and tags facets: scope, tiers and order, counts, label, `facet_limit` cap and totals, invalid stored UTF-8, allocation sweeps; failure paths (plan 18 stages 3-4; runs in the `store` suite and under ASan via `remember_store_tests`) |
| `test_union_search.c` | `search` CLI: warning on exit 0 (plain and JSON text), the E2 verification inputs, D27 invalid UTF-8, `--facet-limit` (default, bounds, usage), the facets in `--json` and the human lines, Examples 1, 2 and 4, `-x` without `--`, and `help search` (plan 18; gate suite `union_search`) |
| `test_update.c` | update, including `--if-version-vector` wiring (gate suite `update`) |
| `test_key.c` | `--key` (gate suites `key_gld` and `key`) |
| `test_json_db_config.c` | db path / JSON fields |
| `test_store.c` | store_open/close, schema, user_version gate, create race + rollback, parent 0700/0600, error edges (step 02 unit; runs black-box **and** under ASan via `remember_store_tests`) |
| `test_normalize.c` | body trim/size, UTF-8 edges, NUL-terminates, control-byte preserve, tag/key normalize, SHA-256 empty/abc + 56-byte NIST long-pad + 64-byte multi-block (step 03 pure unit; harness + ASan/UBSan unit binary, digest instrumented) |
| `test_schema_config.c` | sync path, key null |
| `test_schema_v5.c` | fresh v5 schema, key search, Hebrew FTS fold, v4→v5 schema identity, every v4-open exec fault keeps v4, sidecar-only device id, `store_device_new` replaces an invalid sidecar (plan 15; gate suite `schema_v5`; also under ASan via `remember_store_tests`) |
| `test_device.c` | `device` / `device new` human and JSON, version vector stays put until the next edit, `device new` replaces an invalid sidecar, help (plan 15; gate suite `device`) |
| `test_errors.c` | Plan 16 error catalog, JSON errors, the seven SQLite causes, `--busy-timeout`, `--no-create`, `repair search-index`, and help for those globals (gate suite `errors`) |
| `test_list_dates.c` | `parse_filter_ts`, `list`/`search` `--sort` `--since` `--until`, and their help (plan 17; gate suite `list_dates`) |
| `test_facade.c` | `remember_run` byte-matches the CLI, including `--json-errors` on a miss and a busy lock (gate suite `facade`) |
| `test_sync_import.c` | import merge and conflicts; a v4 or v5 source stays read-only and is not migrated (plan 15 stage 5; gate suite `sync_import`) |
| `test_verification_edges.c` | V21–34 edges (gate suites `verification_gld` and `verification_edges`) |
| `test_search_terms.c` | `search_terms.c` (pure): E2.2 tokens (NFC, splits, 16 × 64 caps, Hebrew fold), q, segment match and E2.6 tiers, key and D28 tag order (each comparator in both argument orders), D12/P2 labels with graphemes, allocation failures (plan 18 stage 2; ASan unit binary `remember_store_tests`) |
| `test_unicode.c` | `unicode.c` port over utf8proc: NFC, grapheme clusters, general categories, White_Space, the decoder, the fault hook (plan 18 stage 1; ASan unit binary `remember_store_tests`) |
| `failing_stream.c` | Test-only stream whose writes fail past a byte limit, for every-failed-write tests (plan 18b) |
| `test_output.c` | `output.c` argument guards, NULL fields, link filtering per subject, preview cuts (CR, DEL, the byte bound), comma separators (plan 18b); the search envelope with facets and the E2.7 human facet lines, escaping and masking, every failed write (plan 18 stage 5; ASan unit binary `remember_store_tests`) |

## Intentionally not automated

| Item | Why |
|------|-----|
| V11 concurrent insert stress | Flaky without dedicated harness |
| Perfect 80-codepoint UTF-8 boundary | Partial via long ASCII truncate |
| Default `~/.remember` path | Would touch real home; use `--db` |

## Run

```bash
cmake -S . -B build -DCMAKE_C_COMPILER=clang && cmake --build build
./build/remember_tests ./build/remember
export PATH="$(brew --prefix llvm)/bin:$PATH"
cmake --build build --target lint
```
