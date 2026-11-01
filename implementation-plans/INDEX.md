# Implementation plans — `remember`

Source of truth for product behavior: [`specs/`](../specs/README.md). It lists the specs and, for a subject with no spec yet, the frozen design log that still describes it.  
Test matrix: [`tests/COVERAGE.md`](../tests/COVERAGE.md).

## Pragmatic architecture (all steps)

| Principle | Rule for this project |
|-----------|------------------------|
| **Easy-to-replace** | **Only `store_sqlite.c` may `#include "sqlite3.h"`** (or amalgamation header). Everything else talks to **`store.h`** (opaque `Store *`, domain structs, error codes). Swap storage later without touching CLI/commands. |
| **Orthogonal** | CLI parse ≠ command policy ≠ store persistence ≠ output format. Change FTS without rewriting argv parser; change JSON without rewriting SQL. |
| **DRY** | Tag/key normalization once (`normalize.c`). Entry JSON/human rendering once (`output.c`). Body pipeline once. FTS resync one function in the store adapter. |

```text
main → cli_parse → commands.h (cmd_add | cmd_locator | cmd_query) → store_* → [store_sqlite only]
                      ↘ output_* (stdout)
         commands_common (internal helpers only; not public)
```

## Steps (do in order)

| # | Plan | Done when |
|---|------|-----------|
| 01 | [Scaffold + meta CLI](01-scaffold-cli-meta.md) | Listed global tests green; format/lint clean on touched `src/` ✅ |
| 02 | [Store port + SQLite schema](02-store-sqlite-schema.md) | Open/create/version/pragmas work; unit smoke; **no sqlite outside adapter** ✅ |
| 03 | [Normalize + hash](03-normalize-hash.md) | Unit tests for trim/tags/keys/hash; used by add ✅ |
| 04 | [add](04-add.md) | All `add_*` + key add/upsert tests green ✅ |
| 05 | [get / list / delete](05-get-list-delete.md) | get/list/delete + paging + key locator tests green ✅ |
| 06 | [search FTS](06-search-fts.md) | All search_* + search key filter green ✅ |
| 07 | [update](07-update.md) | All update_* + keyed update green ✅ |
| 08 | [Lint + format toolchain](08-lint-format.md) | `lint` + `format` targets work; src clean ✅ |
| 09 | [Polish](09-polish.md) | Help text, defaults dir 0700, warnings, remaining config tests ✅ |
| 10 | [Skill + install + dogfood](10-skill.md) | `SKILL.md` + `scripts/install.sh` (binary + agent skills) ✅ |
| 11 | [Core façade lib + `tags`](11-core-facade-lib.md) | `remember_run` byte-equals CLI `--json`; `tags` cmd; `libremember` links w/o `main` ✅ |
| 12 | [TTL / trash](12-ttl.md) | `expires_at`; `--ttl`/`--expires`/`--trash`; `purge-trash`; exit 3; v1→v2 migrate |
| 13 | [Related memories](13-related-memories.md) | `entry_links` v3; `link`/`unlink`/`related`/`rekey`; one-row `related`; stubs; skill |
| 14 | [Sync foundations](14-sync-foundations.md) | `sync_id` v7; soft-delete bins; per-file sidecar + local-only devices + VV; import merge + conflicts; `--sync-id` locators (design 005 Round 7) |
| 15 | [Schema v5 + device id](15-schema-v5-device-id.md) | `user_version` 5; FTS `key, body, tags` + Hebrew fold; `bucket_prefixes`/`settings`/`conflicts.origin`; `devices` dropped; sidecar-only id; `device` / `device new` (006 §1, §12) ✅ |
| 16 | [Actionable errors](16-actionable-errors.md) | `--json-errors`; code catalog; SQLite cause split + fix text + `detail`; warnings; `--busy-timeout`; `--no-create`; `repair search-index`; test VFS shim (006 §3) ✅ |
| 17 | [List dates + stale guard](17-list-dates-stale-guard.md) | `list --sort/--since/--until`; search date filters; `update --if-version-vector` → `stale` (006 §4, §7) ✅ |
| 17a | [Plain notices + duplicate VV keys](17a-plain-line-and-duplicate-vv.md) | One plain notice stays one line; a repeated device key is a malformed version vector. After 17, before 18 ✅ |
| 18 | [Union search](18-union-search.md) | Prefix ∪ raw FTS; never fails on input; `keys`/`tags` facets with tiers; utf8proc vendored (006 §2, §9). Itay finishes this plan and merges it. Do not tick it here. |
| 18b | [Branch-coverage gate](18b-branch-coverage-gate.md) | `check-coverage.sh` fails on any untaken branch side in `search_terms.c`, `unicode.c`, `normalize.c`, `output.c`; no excuses. Runs on `plan18-union-search` between plan 18 stages 2 and 3 ✅ |
| 18c | [Import control bytes](18c-import-control-bytes.md) | `import --from-db` skips and counts (`rejected`) rows whose key or tag is not canonical; human output masks C0/DEL in every stored string it prints. On `fix/import-control-bytes` (off plan 18b) ✅ |
| 19 | [Schema v6](19-schema-v6.md) | One v5→v6 migration for 007 §10, 008 §8, and 010 §17. Copy approved 2026-10-25 (C-01 `schema_too_new`). ✅ |
| 20 | [Settings](20-settings.md) | Registry, user.db, resolution, commands, credential helper, settings export/import. Copy approved 2026-10-25. After plan 19. Command JSON after plan 21. The `unknown_key` reason words are still unwritten. |
| 21 | [JSON envelope](21-json-envelope.md) | `remember-message` 1, locators, actors, `--trash` means Trash. Copy approved 2026-10-25. After plan 19. Does not implement `brains.json`. The hook-depth `--as`/`--actor` sentence is still unwritten. |
| 22 | [Export](22-export.md) | JSON, Markdown, CSV memory export. READY. D-04 is answered (2026-10-25). |
| 23 | [Import formats](23-import-formats.md) | JSON, Markdown, CSV, and `--from-db` v4–v6. Copy approved 2026-10-25. D-04 is answered (2026-10-25). No old-name reader. No add-wins. |
| 24 | [Plugin protocol](24-plugin-protocol.md) | Docs CI, schema extraction, fixtures. READY. Spec revision 6 is already in the repo. ✅ |
| 25 | [Plugins and hooks](25-plugins-and-hooks.md) | CLI and host only. Copy approved 2026-10-25. D-14 is answered (2026-10-25). Do not change the human `add` line (D-05). No daemon, no URL plugin, no hybrid ranker. |
| 26 | [Brain path + skills + 0.3.0](26-release-0.3.0.md) | `default-db`, Codex skill, version 0.3.0 last. Copy approved 2026-10-25 (C-40, C-41). Stage 4 after plans 19–25 merge. Do not tag. Do not bump the version in stages 1–2. |

**Design 006 plans 15–18:** do them in order. Plan 18 is not done. Plans 18b
and 18c land with the plan 18 merge. Do not tick plan 18.

**Design 007–010 plans 19–26:** do them in the order in the table, with two
exceptions that the plans state. Plan 20 stages 1–4 run after plan 19 and
before plan 21. Plan 20's JSON command stages run after plan 21. Plan 26
stage 4 runs after plans 19–25. A row marked PENDING is not ready. The
implementer does the stages that the plan marks as specified and stops at a
DESIGN gate. Approved sentences are in
[`COPY-PENDING.md`](COPY-PENDING.md). Nothing in that file is pending. A
sentence it marks as unwritten stays unprinted. Missing decisions are in
[`DESIGN-PENDING.md`](DESIGN-PENDING.md). Do not invent either.

Locked decisions from the 2026-10-12 plan grill remain law for behavior that
007–010 did not replace:
[`006-locked-decisions.md`](006-locked-decisions.md). Add-wins prefix merge
(E12.6) and the E12.9 settings object are replaced. E11 (Android presets) is
not planned here. Each plan carries its own workflow, coding standards,
quality gate, Definition of Done, and second-review handover.

The old plan files `19-bucket-prefixes.md`, `20-export.md`,
`21-import-formats.md`, and `22-brain-path-skills-release.md` are deleted.
Do not implement them. Log 008's draft line that says to re-extract plans
16–18 and 22 is not a plan. Do not re-extract those plans.

**Definition of done for the whole tool:** full suite green (all verification criteria 1–34 automated where practical), `cmake --build build --target lint` clean on `src/`, no `#include` of sqlite outside the store adapter. Plan 14 adds criteria from design 005.

## Design append note (criteria 21–34)

Design log verification was expanded. New/edge cases live mainly in `tests/test_verification_edges.c` (plus earlier key/update/search files). Each step plan lists which of those must pass.

**Dev dependency:** some GC/schema inspect tests call the **`sqlite3` CLI** on PATH. Install: `brew install sqlite` if missing.

## Cross-cutting quality gate (every step)

1. `cmake --build build && ./build/remember_tests ./build/remember` — required tests for that step pass; none regressed.
2. Format: `cmake --build build --target format` (or clang-format on touched files).
3. Lint: after step 08, full lint; before that, at least compile with `-Werror`.
4. No new SQLite knowledge outside `store_sqlite.c`.
