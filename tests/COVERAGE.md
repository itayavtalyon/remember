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
| 27 | Invalid FTS | `search_invalid_fts_*`, `search_unbalanced_quote_*` |
| 28 | Empty = success | search/list empty total 0 |
| 29 | stdin | add/update `-` |
| 30 | Invalid UTF-8 body | `add_invalid_utf8_body_*`, `update_invalid_utf8_*` |
| 31 | Filters/sort/limits | list/search suites |
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
| `test_update.c` | update |
| `test_key.c` | `--key` (green slice suite: `key_gld`; WIP: `key`) |
| `test_json_db_config.c` | db path / JSON fields |
| `test_store.c` | store_open/close, schema, user_version gate, create race + rollback, parent 0700/0600, error edges (step 02 unit; runs black-box **and** under ASan via `remember_store_tests`) |
| `test_normalize.c` | body trim/size, UTF-8 edges, NUL-terminates, control-byte preserve, tag/key normalize, SHA-256 empty/abc + 56-byte NIST long-pad + 64-byte multi-block (step 03 pure unit; harness + ASan/UBSan unit binary, digest instrumented) |
| `test_schema_config.c` | sync path, key null |
| `test_verification_edges.c` | V21–34 edges (green slice: `verification_gld`; WIP: `verification_edges`) |

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
