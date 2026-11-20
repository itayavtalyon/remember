# API surface: the engine verb table

This file is the one engine verb table (`decision:2026-11-07-bg-x-11-api`, R 012). One row per verb: each CLI command and each subcommand, with its arguments, rights, REST route, MCP tool, the plan that adds it, and why it is not a flag or a subcommand of another verb. The daemon and Cloud serve REST and `/mcp` from this table, so the surfaces cannot drift (log 019 V1-14). The file name is the one `decision:2026-11-12-x1-remember-call-requests` (X1-2 A) fixed (`decision:2026-11-18-verb-table-is-api-surface`). Plan 29 adds the request-type and member columns and then generates this file from `src/api_table.c`.

## The rule (`decision:2026-11-18-verb-ladder`)

- A new top-level verb or a new subcommand enters this table in the same PR that adds it.
- A new top-level verb needs Itay's approval in a decision entry. Its row names that decision.
- Prefer, in this order: a flag on an existing verb; a subcommand under an existing noun; only then a new top-level verb. The last column says why a row could not take a higher rung.

The ctest `cli_verbs_match_api_surface` (suite `cli`, `tests/test_cli.c`) keeps the table true. It fails when:

- a top-level verb in the CLI dispatch table (`k_commands` in `src/cli.c`) has no row with plan `main`;
- a row with plan `main` names a top-level verb that the dispatch table does not have;
- the Rights of a row with plan `main` differs from what the engine does: `app_command_class` (plan 27b) for a verb that opens the brain, and `none` for a verb that opens no brain (`help`, `version`, `brain list`, `brain prune`).

When a plan ships a planned row, it changes that row's plan to `main` in the same PR.

## Columns

- **CLI command**: the words after `remember`.
- **Arguments**: the command's own arguments. Global options (`--db`, `--json`, `--strict`, `--color`, `--quiet`, `--busy-timeout`, `--no-create`) are on every command and are not listed. `LOC` is a locator: `ID` or `--key KEY`. `BIN` is `--trash`, `--expired` or `--deleted`. `AUTHOR` is `--as KIND` and `--actor ID`. `HOOKS` is `--hooks` or `--no-hooks`.
- **Rights**: what the verb does to the brain. `read-write`: a brain from a newer remember refuses it (plan 27b). `read`: it runs on such a brain and warns. `none`: it opens no brain.
- **REST route** and **MCP tool**: `-` when the verb is on the CLI and remember_call only. Only the rows in `decision:2026-11-18-surfaces-catalog-plus-rekey` have them, and each route has one tool or tool action (REST mirrors MCP). Routes: `decision:2026-11-18-rest-routes` and JSON-STYLE 1.7a. Tools: log 018 V1-13 (`decision:mcp-tool-catalog`), V1-07, `decision:2026-11-18-mcp-conflicts-accept`.
- **Plan**: `main` when the verb is on main, else the plan that adds it, or `-` when no plan builds it yet.

## Verbs

| CLI command | Arguments | Rights | REST route | MCP tool | Plan | Why not a flag or subcommand |
|---|---|---|---|---|---|---|
| `remember help` | `[COMMAND]` | none | - | - | main | On main before the ladder (`decision:2026-11-18-top-level-approvals`). |
| `remember version` | none | none | - | - | main | On main before the ladder. |
| `remember add` | `BODY\|-`, `--key`, `--tag`..., `--ttl` or `--expires`, AUTHOR, HOOKS | read-write | `POST /v1/memories` | `remember_add` | main | On main before the ladder. |
| `remember get` | LOC, BIN | read | `GET /v1/memories/{id}` | `remember_get` | main | On main before the ladder. |
| `remember list` | `--tag`..., `--created-by`, `--updated-by`, `--key`, `--sort`, `--since`, `--until`, `--limit`, `--cursor`, BIN | read | `GET /v1/memories` | `remember_list` | main | On main before the ladder. |
| `remember search` | `QUERY`, `--tag`..., `--created-by`, `--updated-by`, `--key`, `--sort`, `--since`, `--until`, `--limit`, `--facet-limit`, BIN | read | `GET /v1/memories/search` | `remember_search` | main | On main before the ladder. |
| `remember update` | LOC, BIN, `--text`, `--tag`... or `--clear-tags`, `--ttl`, `--expires` or `--clear-expires`, `--undelete`, `--if-version-vector`, AUTHOR, HOOKS | read-write | `PATCH /v1/memories/{id}` | `remember_update` | main | On main before the ladder. |
| `remember delete` | LOC, `--expired`, AUTHOR, HOOKS | read-write | `DELETE /v1/memories/{id}` | `remember_delete` | main | On main before the ladder. |
| `remember purge` | `--expired` or `--deleted`, HOOKS | read-write | - | - | main | On main before the ladder. Plan 32 adds a `LOCATOR` argument, not a verb. |
| `remember rekey` | LOC, BIN, `--to-key` or `--clear-key`, AUTHOR, HOOKS | read-write | `POST /v1/memories/{id}/rekey` | `remember_rekey` | main | On main before the ladder. |
| `remember link` | `--from` or `--from-key`, `--to` or `--to-key`, `--kind`, `--deleted`, AUTHOR, HOOKS | read-write | `POST /v1/memories/{id}/links` | `remember_link` | main | On main before the ladder. |
| `remember unlink` | `--from` or `--from-key`, `--to` or `--to-key`, `--kind`, `--deleted`, AUTHOR, HOOKS | read-write | `DELETE /v1/memories/{id}/links/{to}` (`?kind=`) | `remember_unlink` | main | On main before the ladder. |
| `remember related` | LOC, `--kind`, `--outgoing`, `--incoming`, `--deleted` | read | `GET /v1/memories/{id}/related` | `remember_related` | main | On main before the ladder. |
| `remember tags` | BIN | read | `GET /v1/tags` | `remember_tags` | main | On main before the ladder. |
| `remember import` | `--from-db`, `--from-json`, `--from-md` or `--from-csv` `PATH`, HOOKS | read-write | - | - | main | On main before the ladder. |
| `remember export` | `--format json\|md\|csv`, `--out PATH`, `--include-expired`, `--include-trash`, `--exclude-archived`, `--force` | read | - | - | main | On main before the ladder. |
| `remember conflicts` | none | read | `GET /v1/conflicts` | `remember_conflicts` (`action` `list`) | main | On main before the ladder. |
| `remember conflict accept` | `--id N`, `--keep local\|incoming\|both`, HOOKS | read-write | `POST /v1/conflicts/{id}/accept` | `remember_conflicts` (`action` `accept`) | main | On main before the ladder. |
| `remember device` | none | read | - | - | main | On main before the ladder. |
| `remember device new` | none | read | - | - | main | Subcommand of `device`. It mints the sidecar file and writes nothing to the brain (007 ST-10.2). |
| `remember repair search-index` | none | read-write | - | - | main | Subcommand of `repair`. |
| `remember brain new-id` | none | read-write | - | - | main | Subcommand of `brain`. |
| `remember brain list` | none | none | - | - | main | Subcommand of `brain`. It reads `brains.json` only. |
| `remember brain prune` | none | none | - | - | main | Subcommand of `brain`. It writes `brains.json` only. |
| `remember settings get` | `KEY`, `--scope`, `--value` | read | `GET /v1/settings/{key}` | `remember_settings` (`action` `get`) | main | Subcommand of `settings`. |
| `remember settings list` | `[PREFIX]`, `--scope`, `--namespace`, `--changed`, `--limit`, `--cursor` | read | `GET /v1/settings` | `remember_settings` (`action` `list`) | main | Subcommand of `settings`. |
| `remember settings explain` | `KEY` or `--all` | read | `GET /v1/settings/{key}/explain` | `remember_settings` (`action` `explain`) | main | Subcommand of `settings` (route: `decision:2026-11-18-rest-settings-explain`). |
| `remember settings history` | `[KEY]`, `--scope`, `--limit`, `--cursor` | read | - | - | main | Subcommand of `settings`. |
| `remember settings export` | `--scope brain\|user\|all` | read | - | - | main | Subcommand of `settings`. |
| `remember settings set` | `KEY VALUE...` or `KEY --clear`, `--scope`, AUTHOR | read-write | - | - | main | Subcommand of `settings`. No settings write on REST or MCP (`decision:mcp-tool-catalog`). |
| `remember settings reset` | `KEY`, or `--namespace NS --yes`, or `--all --scope S --yes`; AUTHOR | read-write | - | - | main | Subcommand of `settings`. |
| `remember settings import` | `FILE`, `--dry-run`, `--scope` | read-write | - | - | main | Subcommand of `settings`. |
| `remember brain sweep` | `--all` | read-write | - | - | 32 | Subcommand of `brain`, which owns `brains.json` (`decision:2026-11-18-sweep-under-brain`). Not a flag: no existing verb sweeps on request. |
| `remember dispatch` | none | read-write | - | - | 35 | New top-level verb, approved in `decision:2026-11-18-dispatch-user-facing`. It drains the outbox of one brain, which no noun owns. |
| `remember plugin add` | `DIR`, `--dev` | read-write | - | - | 34 | New noun `plugin`, approved in `decision:2026-11-18-top-level-approvals`. |
| `remember plugin hook` | `NAME --command PATH [ARG...] --event EVENT`..., the filter options, `--timeout`, `--cwd`, `--disabled` | read-write | - | - | 34 | Subcommand of `plugin`: a hook is a one-action plugin (`decision:2026-11-18-hook-under-plugin`). |
| `remember plugin list` | none | read | - | - | 34 | Subcommand of `plugin` (`decision:2026-11-18-plugin-reads-stay`). |
| `remember plugin show` | `NAME` | read | - | - | 34 | Subcommand of `plugin`. Manifest and settings, not runtime state. |
| `remember plugin status` | `[NAME]`, `--ack NAME` | read | - | - | 34 | Subcommand of `plugin`. Runtime state. |
| `remember plugin enable` | `NAME`, `--scope brain` | read-write | - | - | 34 | Subcommand of `plugin`. Not `settings set`: it also clears a crash loop, starts a backfill and needs the confirm callback (`decision:2026-11-18-plugin-enable-stays`). |
| `remember plugin disable` | `NAME`, `--scope brain` | read-write | - | - | 34 | Subcommand of `plugin`, the pair of `enable`. |
| `remember plugin trust` | `NAME` | read-write | - | - | 34 | Subcommand of `plugin`. |
| `remember plugin remove` | `NAME`, `--delete-data` | read-write | - | - | 34 | Subcommand of `plugin`. |
| `remember plugin log` | `NAME`, `--follow`, `--since DURATION` | read | - | - | 34 | Subcommand of `plugin`. |
| `remember plugin rerun` | `NAME EVENT_ID` | read-write | - | - | 35 | Subcommand of `plugin`. Not a flag of `status` (a read) or `test` (runs outside the outbox) (`decision:2026-11-18-plugin-rerun-stays`). |
| `remember plugin reindex` | `NAME`, `--full` | read-write | - | - | 36 | Subcommand of `plugin`. |
| `remember plugin test` | `NAME\|DIR`, `--entry`, `--event`, `--query`, `--dry-run` | read-write | - | - | 38 | Subcommand of `plugin`. |
| `remember daemon` | `--plugin-dir PATH` | read-write | - | - | 39 | New noun `daemon`, approved in `decision:2026-11-18-top-level-approvals`. It runs the daemon in the foreground. |
| `remember daemon install` | `--yes` | none | - | - | 40, 41 | Subcommand of `daemon`. It also does the takeover from Fortkeeps (`decision:2026-11-18-daemon-install-absorbs-takeover`). It replies `daemon.install.result` `{changed, path, previous_owner}` (`decision:open-2026-11-20-daemon-install-reply`). |
| `remember sync peers` | none | read | - | - | 44 | New noun `sync`, approved in `decision:2026-11-18-top-level-approvals`. |
| `remember sync pull` | not grilled | read-write | - | - | - | Subcommand of `sync`. Needs a transport; no plan builds it yet (plan 44, Out of scope). |
| `remember archive` | LOC | read-write | `POST /v1/memories/{id}/archive` | `remember_archive` | - | Top-level verb approved by Itay and confirmed by Interface (log 019 V1-06). Reserved row (X1-11 A); no plan builds it yet. |
| `remember unarchive` | LOC | read-write | `POST /v1/memories/{id}/unarchive` | `remember_unarchive` | - | The pair of `archive`. |

## Not in this table

- `remember daemon takeover` and `remember hook add`: replaced (`decision:2026-11-18-daemon-install-absorbs-takeover`, `decision:2026-11-18-hook-under-plugin`).
- `remember sweep`: it is `remember brain sweep` (`decision:2026-11-18-sweep-under-brain`).
- `remember bot connect` (`decision:mcp-bot-connect`): no plan names it yet. It enters this table, with an approval decision, in the plan that builds it.
- Flags that plans add to existing verbs are not rows: `delete --expired` and `purge LOCATOR` (plan 32).
