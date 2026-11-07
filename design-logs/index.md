# Design Log Index

> **Frozen 2026-10-31.** No new design logs are written. What remember does today is in [`specs/`](../specs/README.md). Decisions are Remember `decision:*` entries tagged `subject:<name>`; the roadmap is `roadmap:remember`. These logs are history: where a spec or a later decision disagrees, it wins.

## Core
| # | Title | Description | Status |
|---|-------|-------------|--------|
| 001 | [foundations](001-foundations.md) | CLI surface, SQLite/FTS schema, agent contract, skill | Design (finalized; Round 8 amend) |
| 002 | [core-facade](002-core-facade.md) | In-process `remember_run` entrypoint + `tags` for the native macOS GUI (separate repo) | Design (proposed — in review) |
| 003 | [ttl](003-ttl.md) | Optional `expires_at`; expired = trash; exit 3 wrong-bin; restore / purge | Design (finalized, Round 4) |
| 004 | [related-memories](004-related-memories.md) | `entry_links` v3: one-row `related`; 3 stored kinds / 5-name output `type`; `link`/`unlink`/`related`/`rekey` (`--to-key`/`--clear-key`); list related column = ids | Design (finalized, Round 4) |
| 005 | [sync-foundations](005-sync-foundations.md) | `sync_id` (UUID v7), soft-delete + bins, device sidecar + VV, import merge + conflicts, `--sync-id` locators | Design (finalized, 2026-09-20) |
| 006 | [ui-overhaul-engine](006-ui-overhaul-engine.md) | 0.3.0 for the Fortkeeps overhaul: schema v5 (key in FTS, bucket prefixes, conflict origin, sidecar-only device id), union search with keys/tags, actionable errors, export/import JSON·MD·CSV, `--if-updated-at`, `device new`, Android presets | Design (closed 2026-10-11, amended 2026-10-12) |
| 007 | [settings](007-settings.md) | One generic `settings` table per DB; full dotted keys; brain rows travel, user rows stay; user.db in the data dir (`$XDG_DATA_HOME/remember/` else `~/.remember/`); `plugin.<name>.enabled` the only generated key; bucket prefixes a plain list; credential helper via `remember-credential-<id>`; actors on rows; schema v6 | Design (grill closed 2026-10-21) |
| 008 | [json-envelope](008-json-envelope.md) | One format `remember-message` 1 on every surface; `sync_id` → `id`; actors replace `source`; the write table (what each write changes); cursor paging for lists, no paging for search (S-08); links and purges as records; brain copy detection; `remember-export` 1 | Design (grill closed 2026-10-21) |
| 009 | [plugin-protocol](009-plugin-protocol.md) | Protocol 1 frozen after revision 6 (24 amendment rows: one format, hooks as plugins, manifest-only budgets, every warning strict, daemon kept; applied to the spec 2026-10-22, M-06); one name rule for plugins and entries (M-17); spec in `docs/PLUGIN-PROTOCOL.md`; schema `$id` on GitHub Pages | Design (grill closed 2026-10-21) |
| 010 | [plugins-and-hooks](010-plugins-and-hooks.md) | Hooks are plugins (`hook add` writes one); trust per code hash; write-time filters with `origins`; outbox with rerun; host callbacks (confirm, dispatch, token); `brains.json` of known brains; the daemon stays in v1 for local HTTP MCP (2026-10-22, M-08), hourly `remember sweep --all` | Design (grill closed 2026-10-21) |

## Roadmap
| # | Title | Description | Status |
|---|-------|-------------|--------|
| 018 | [v1-roadmap-decisions](018-v1-roadmap-decisions.md) | Itay's answers to the v1 roadmap sheet (V1-01 to V1-54): stack and repos (V1-42 A), versions, schema v6, archive, API, sync, Cloud, daemon, plugins, hybrid, packaging, onboarding; 8 open questions. R 011-R 017 stay reserved for the feature logs | Decisions recorded (2026-10-29) |
| 019 | [v1-roadmap-round3-decisions](019-v1-roadmap-round3-decisions.md) | Round 3: closes 018 OQ-2..8, OQ-10: REST + `/mcp` from the engine table; Cloud/sync before hybrid, keyword-only Cloud v1; idempotent `archive`/`unarchive`; read-only `archived_at`; `url_expand` server queue; Homebrew plugin formula; embedding research follow-up; schema mirror | Decisions recorded (2026-10-29) |
| 020 | [v1-roadmap-corrections](020-v1-roadmap-corrections.md) | Corrects 018 and 010 H-18.1: appcast on fortkeeps.com; domain split (fortkeeps.com site/downloads/docs/appcast/schema mirror, fortkeeps.app web app, api.fortkeeps.app sync+MCP); Sparkle decided, Fortkeeps Mac app only, Scroll App Store only; no sweep LaunchAgent (V1-17); two daemon labels plus a single-daemon lock (mechanism deferred to R 013) | Decisions recorded (2026-10-31) |
