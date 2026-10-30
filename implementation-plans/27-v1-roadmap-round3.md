# 27 — v1 roadmap round 3 (stub)

**Design:** [`design-logs/019-v1-roadmap-round3-decisions.md`](../design-logs/019-v1-roadmap-round3-decisions.md)

## Status

STUB. Work is planned inside the feature logs R 011-R 017 once written.

## Items

- [ ] `archive` / `unarchive` top-level verbs; idempotent exit 0 + warning + `"changed": false` (CLI, HTTP, MCP).
- [ ] `archived_at` read-only in the message format.
- [ ] Generate REST routes and `/mcp` from the engine table (daemon and Cloud).
- [ ] Cloud v1 keyword-only search, stated in docs and responses.
- [ ] Server background queue for `url_expand` on Cloud API/MCP writes.
- [ ] Homebrew formula for plugins; app bundling.
- [ ] Mirror schemas on fortkeeps.com.
- [ ] Follow-up: embedding model / Apple built-ins research (V1-24).
