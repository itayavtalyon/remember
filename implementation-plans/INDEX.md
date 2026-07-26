# Implementation plans — `remember`

Source of truth for product behavior: [`design-logs/001-foundations.md`](../design-logs/001-foundations.md).  
Test matrix: [`tests/COVERAGE.md`](../tests/COVERAGE.md).

## Pragmatic architecture (all steps)

| Principle | Rule for this project |
|-----------|------------------------|
| **Easy-to-replace** | **Only `store_sqlite.c` may `#include "sqlite3.h"`** (or amalgamation header). Everything else talks to **`store.h`** (opaque `Store *`, domain structs, error codes). Swap storage later without touching CLI/commands. |
| **Orthogonal** | CLI parse ≠ command policy ≠ store persistence ≠ output format. Change FTS without rewriting argv parser; change JSON without rewriting SQL. |
| **DRY** | Tag/key normalization once (`normalize.c`). Entry JSON/human rendering once (`output.c`). Body pipeline once. FTS resync one function in the store adapter. |

```text
main → cli_parse → commands_* → store_* → [store_sqlite only: SQLite]
                      ↘ output_* (stdout)
```

## Steps (do in order)

| # | Plan | Done when |
|---|------|-----------|
| 01 | [Scaffold + meta CLI](01-scaffold-cli-meta.md) | Listed global tests green; format/lint clean on touched `src/` ✅ |
| 02 | [Store port + SQLite schema](02-store-sqlite-schema.md) | Open/create/version/pragmas work; unit smoke; **no sqlite outside adapter** ✅ |
| 03 | [Normalize + hash](03-normalize-hash.md) | Unit tests for trim/tags/keys/hash; used by add ✅ |
| 04 | [add](04-add.md) | All `add_*` + key add/upsert tests green ✅ |
| 05 | [get / list / delete](05-get-list-delete.md) | get/list/delete + paging + key locator tests green ✅ |
| 06 | [search FTS](06-search-fts.md) | All search_* + search key filter green |
| 07 | [update](07-update.md) | All update_* + keyed update green |
| 08 | [Lint + format toolchain](08-lint-format.md) | `lint` + `format` targets work; src clean |
| 09 | [Polish](09-polish.md) | Help text, defaults dir 0700, warnings, remaining config tests |
| 10 | [Skill + dogfood](10-skill.md) | `skills/remember/SKILL.md` matches CLI |

**Definition of done for the whole tool:** full suite green (all verification criteria 1–34 automated where practical), `cmake --build build --target lint` clean on `src/`, no `#include` of sqlite outside the store adapter.

## Design append note (criteria 21–34)

Design log verification was expanded. New/edge cases live mainly in `tests/test_verification_edges.c` (plus earlier key/update/search files). Each step plan lists which of those must pass.

**Dev dependency:** some GC/schema inspect tests call the **`sqlite3` CLI** on PATH. Install: `brew install sqlite` if missing.

## Cross-cutting quality gate (every step)

1. `cmake --build build && ./build/remember_tests ./build/remember` — required tests for that step pass; none regressed.
2. Format: `cmake --build build --target format` (or clang-format on touched files).
3. Lint: after step 08, full lint; before that, at least compile with `-Werror`.
4. No new SQLite knowledge outside `store_sqlite.c`.
