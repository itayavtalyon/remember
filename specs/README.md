# Specs

A spec describes what `remember` does today. It is normative.

- The plan that ships a behavior edits its spec in the same PR. A spec never describes planned behavior. CI fails a PR that changes `src/` and not `specs/`, unless a line of the PR body starts with `No spec change:` and gives the reason ([`spec-drift.yml`](../.github/workflows/spec-drift.yml)).
- Each section names the decision keys behind it (`decision:…`). The rationale and its later changes live as linked entries in the maintainer's Remember brain, tagged `subject:<name>`.
- When a spec and a design log disagree, the spec wins. Design logs in [`design-logs/`](../design-logs/index.md) are frozen history (2026-10-31).
- The roadmap is the Remember entry `roadmap:remember`.

## Specs

| Spec | Subject | Covers |
|---|---|---|
| [PLUGIN-PROTOCOL.md](PLUGIN-PROTOCOL.md) | `subject:plugin-protocol` | Plugin protocol 1 (revision 6): manifest, messages, schemas |
| [PLUGIN-PROTOCOL-CHANGES.md](PLUGIN-PROTOCOL-CHANGES.md) | `subject:plugin-protocol` | Changelog of the plugin protocol |
| [JSON-STYLE.md](JSON-STYLE.md) | `subject:json-envelope` | JSON value rules for every surface |

## Not written yet

These subjects have no spec yet. Until the plan that next changes a subject writes its spec, the legacy log is the reference, read with every later log and decision that amends it.

| Subject | Legacy source |
|---|---|
| CLI surface, SQLite and FTS schema, agent contract | 001, amended by 003 to 008 |
| In-process core façade (`remember_run`) | 002 |
| Expiry and bins (`subject:archive` changes them) | 003, 005 |
| Entry links (`subject:wiki-links` extends them) | 004 |
| Sync, import merge, conflicts (`subject:sync`) | 005 |
| Union search, errors, export and import (`subject:search`, `subject:export`) | 006 |
| Settings (`subject:settings`) | 007 |
| Message envelope (`subject:json-envelope`) | 008, with [JSON-STYLE.md](JSON-STYLE.md) |
| Plugins and hooks in the engine (`subject:plugins-and-hooks`) | 010, with [PLUGIN-PROTOCOL.md](PLUGIN-PROTOCOL.md) |
