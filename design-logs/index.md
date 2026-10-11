# Design Log Index

## Core
| # | Title | Description | Status |
|---|-------|-------------|--------|
| 001 | [foundations](001-foundations.md) | CLI surface, SQLite/FTS schema, agent contract, skill | Design (finalized; Round 8 amend) |
| 002 | [core-facade](002-core-facade.md) | In-process `remember_run` entrypoint + `tags` for the native macOS GUI (separate repo) | Design (proposed — in review) |
| 003 | [ttl](003-ttl.md) | Optional `expires_at`; expired = trash; exit 3 wrong-bin; restore / purge | Design (finalized, Round 4) |
| 004 | [related-memories](004-related-memories.md) | `entry_links` v3: one-row `related`; 3 stored kinds / 5-name output `type`; `link`/`unlink`/`related`/`rekey` (`--to-key`/`--clear-key`); list related column = ids | Design (finalized, Round 4) |
| 005 | [sync-foundations](005-sync-foundations.md) | `sync_id` (UUID v7), soft-delete + bins, device sidecar + VV, import merge + conflicts, `--sync-id` locators | Design (finalized, 2026-09-20) |
| 006 | [ui-overhaul-engine](006-ui-overhaul-engine.md) | 0.3.0 for the Fortkeeps overhaul: schema v5 (key in FTS, bucket prefixes, conflict origin), union search with keys/tags, actionable errors, export/import JSON·MD·CSV, `--if-updated-at`, `device new`, Android presets | Design (closed 2026-10-11) |
