# 019 — v1 roadmap round 3 decisions

**Status:** Decisions recorded (2026-10-29). Closes open questions OQ-2 to OQ-8 and OQ-10 of log 018 (PR #51). Log 018 is not edited.
**Author:** Itay (answers) + Chief Architect (recording)
**Created:** 2026-10-29

Cross-refs: [008](008-json-envelope.md), [010](010-plugins-and-hooks.md), 018 (`design/018-v1-roadmap-decisions-v2`, PR #51).
Numbering: R 011-R 017 stay reserved for the feature logs; 018 is the roadmap log, so this log is 019.

## Background

Log 018 recorded Itay's answers to the v1 roadmap sheet and left 10 open questions. OQ-1 and OQ-9 closed in Round 2. This log records Round 3, which closes the rest except OQ-8's research, which becomes a follow-up task.

## Problem

The feature logs (R 011 archive, R 012 API, R 013 daemon, R 014 sync, R 015 hybrid, R 016 URL expansion, R 017 packaging) cannot be written while these questions are open.

## Decisions

| ID | 018 OQ | Decision |
|---|---|---|
| V1-14 | OQ-4 | The daemon and Cloud serve both REST routes and `/mcp`. Both are generated from the one engine table, so the two surfaces cannot drift. |
| V1-04 | OQ-5 | Cloud and sync ship before the hybrid plugin. Cloud v1 launches with keyword search only and says so (docs and the search response state that search is keyword-only). |
| V1-06 | OQ-2 | Interface confirmed top-level `remember archive <key>` and `remember unarchive <key>`, with the same verbs over HTTP and MCP. Running either on an item already in that state exits 0 with a warning, and the JSON has `"changed": false`. |
| V1-07 | OQ-3 | A memory exposes `archived_at` read-only in the message format (log 008). Clients change it only through `archive` / `unarchive`. |
| V1-33 | OQ-6 | `url_expand` for Cloud API/MCP writes runs in a server background queue. The write returns at once; the expansion lands as a later edit. |
| V1-29 | OQ-7 | Homebrew users get plugins via a Homebrew formula; the apps bundle them. |
| V1-24 | OQ-8 | Keep A. Research per-platform embedding models and Apple built-in capabilities as a follow-up task; it does not block v1 logs. |
| V1-48 | OQ-10 | Mirror the schemas on fortkeeps.com. The GitHub Pages `$id` stays canonical. |

## Design notes for the feature logs

- R 011: archive idempotence (exit 0, warning, `"changed": false`) on CLI, HTTP and MCP alike; `archived_at` read-only.
- R 012 / R 013: one engine table generates REST routes and MCP tools for daemon and Cloud.
- R 014 / R 015: Cloud keyword-only at launch; hybrid is a later plugin.
- R 016: server background queue for Cloud writes.
- R 017: Homebrew plugin formula; apps bundle plugins; schema mirror on fortkeeps.com.

## Known Limitations

- Follow-up task: per-platform embedding model and Apple built-ins research (V1-24).
- Queue retry and failure reporting for `url_expand` are left to R 016.

## Implementation plan

[`implementation-plans/27-v1-roadmap-round3.md`](../implementation-plans/27-v1-roadmap-round3.md) (stub).
