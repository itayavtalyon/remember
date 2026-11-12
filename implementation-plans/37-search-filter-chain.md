# 37 — Three-stage search with the filter chain

**Area:** `remember search` stages 1 to 3, the chain budget, cold start, `data.filters[]`, `--no-filters` and `--explain` (`src/search_chain.c` new, `src/search_chain.h` new, `src/cmd_query.c`).
**Needs:** plan 36. No other plan.
**Blocked on:** open question `decision:open-2026-11-12-ranked-by-filter-chain` (X5): which entry `ranked_by` names when filters applied. Log 010 and old plan 25 give only the `--explain` line `ranked by <plugin>/<entry>`; no decision says which entry that is when several `search.results` filters applied.

## Goal

Search runs BM25, then each enabled filter in priority order inside a budget, then drops missing or Deleted ids. With no filters the order equals today's BM25 order.

## Source

- Log 010 H-12.4 to H-12.9; `decision:plugins-budget-defaults`; `decision:2026-11-11-filter-degraded-exit` (degraded filter: exit 0, or 4 with `--strict`); `decision:2026-11-11-filter-tie-break` (G5 A: same priority runs in plugin name byte order, then entry name).
- Old plan 25 "Search".
- Copy: C-27, C-28, C-29, C-32, C-33, C-34, the C-01 cold-start line, the `plugin_chain_capped` line in `COPY-PENDING.md`.
- Spec: `specs/SEARCH-CHAIN.md` (new); `specs/JSON-ENVELOPE.md` search row: `filters` and `ranked_by` filled.

## Out of scope

The hybrid ranker (no plan). URL expansion (no plan).

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md). Tests in `tests/test_search_chain.c` (new, suite `plugins`).

## What must be true

1. `search.query` filters rewrite the query first. Then up to `remember.search.candidate_pool` (default 200) candidate ids with BM25 scores, after bin, tag, actor and key filters.
2. Load the full rows of that pool once. Send each filter only its `needs`. Run `search.results` filters.
3. Keep the chain order. Drop ids that are missing or Deleted. Apply `--limit` when given, else `remember.search.max_results`. `data.total` is the length before the limit.

No enabled filters, or `--no-filters`: BM25 order. Priority: lower first, default 500. Tie: plugin name, then entry name, byte order. No user setting changes priority, budget or cold-start grace.

`remember.plugins.chain_budget` caps the chain. At the cap the remaining filters do not run: status `skipped`, reason `chain_capped`, and warn `plugin_chain_capped` with its line. No `time_budget_capped`. A budget larger than the time left is clipped; the clipped value is the `budget_ms` sent; no warning for the clip.

Cold start: `cold_start_grace` from the manifest (plan 25). A cold filter that does not answer: the whole result is BM25 order, code `plugin_filter_cold_start`; `hybrid`/rerank prints the C-01 cold-start line, any other filter C-29. The filter keeps loading.

A filter past its budget: status `timeout`, code `plugin_filter_timeout`, input passes on (C-27). Another failure is `plugin_filter_failed` (C-28). Every degraded filter exits 0, or 4 with `--strict`.

`data.filters[]`: name, version, status (`applied`, `skipped`, `failed`, `timeout`), time, optional note. Warnings a plugin sends go only to the plugin log and `plugin status`. Warnings Remember raises enter output: `plugin_additions_capped` (C-32), `plugin_note_truncated` (C-33), `plugin_request_trimmed` (C-34). `ranked_by` is `bm25` when no filter applied. When a filter applied, it is `<plugin>/<entry>` of the entry X5 names. Human search prints no extra line when every filter applied. `--explain` prints the filter table and `ranked by <plugin>/<entry>`. `REMEMBER_PROCESSES=0`: built-in C-ABI filters still run.

`plugin.hybrid.enabled` true does not change order (no filter entry).

- **Warnings carry a `fix` (E3 B, `decision:2026-11-12-e3-warnings-have-fix-field`).** Every warning this plan prints has a `message` and a non-empty `fix`: `plugin_filter_cold_start`, `plugin_filter_timeout`, `plugin_filter_failed`, `plugin_additions_capped`, `plugin_note_truncated`, `plugin_request_trimmed` and `plugin_chain_capped`. The wording is copy item E3B-1, entries 3, 18 to 20, 23 to 25 and 28, in `decision:2026-11-12-copy-batch-e3b-warning-splits`: the approved sentence stays word for word and only its next step moves into `fix`; the plain line is `remember: warning: <message> <fix>`. `code`, `detail`, exit codes and `--strict` do not change. Itay approved E3B-1 as a whole on 2026-11-12, including every `fix` that is Story Weaver wording (`decision:2026-11-12-copy-batch-e3b-warning-splits`).

## Review points

- With no enabled filter the code path does not call the chain (identical BM25 bytes).

## Definition of Done

- [ ] `no_filter_matches_bm25_stage3_drops_deleted` (010 test 20).
- [ ] `timeout_c27_exit_0_strict_4` (010 test 21).
- [ ] `cold_start_c29_and_c01` (010 test 22).
- [ ] `chain_capped_skipped_no_time_budget_capped`.
- [ ] `same_priority_plugin_then_entry_byte_order`.
- [ ] `hybrid_enabled_same_order`.
- [ ] `processes_off_builtin_filter_runs` (010 test 17, filter half, with a test-only built-in double not shipped).
- [ ] `c32_c33_c34_printed`.
- [ ] `specs/SEARCH-CHAIN.md` written; `decision:2026-11-11-filter-degraded-exit` and `decision:2026-11-11-filter-tie-break` are `state:implemented`.
- [ ] The gate passes on the committed tree.

## Implementation Notes

## Review Notes
