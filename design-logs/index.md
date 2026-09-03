# Design Log Index

## Core
| # | Title | Description | Status |
|---|-------|-------------|--------|
| 001 | [foundations](001-foundations.md) | CLI surface, SQLite/FTS schema, agent contract, skill | Design (finalized; Round 8 amend) |
| 002 | [core-facade](002-core-facade.md) | In-process `remember_run` entrypoint + `tags` for the native macOS GUI (separate repo) | Design (proposed — in review) |
| 003 | [ttl](003-ttl.md) | Optional `expires_at`; expired = trash; exit 3 wrong-bin; restore / purge | Design (finalized, Round 4) |
| 004 | [related-memories](004-related-memories.md) | `entry_links` v3: one-row `related`; 3 stored kinds / 5-name output `type`; `link`/`unlink`/`related`/`rekey` (`--to-key`/`--clear-key`); list related column = ids | Design (finalized, Round 4) |
