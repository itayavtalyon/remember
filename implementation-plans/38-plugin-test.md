# 38 — `remember plugin test`

**Area:** `remember plugin test NAME|DIR [--entry ENTRY] [--event EVENT] [--query TEXT] [--dry-run]` (`src/cmd_plugin_test.c` new).
**Needs:** plans 35 and 36. No other plan.
**Blocked on:** nothing.

## Goal

A plugin author runs one entry in the foreground against an installed plugin, or runs the plan 24 conformance fixtures against a folder, with no install and no prompt.

## Source

- Log 010 H-16 (`plugin test`), log 009 conformance; `decision:2026-11-11-d17-sync-commands` (G6 A).
- Old plan 25 "Logs and CLI" (`plugin test`).
- Copy: C-30, C-31.
- Spec: `specs/PLUGIN-CLI.md` gains "plugin test".

## Out of scope

Hybrid in the suite.

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md). Tests in `tests/test_plugin_test.c` (new, suite `plugins`).

## What must be true

- `plugin test NAME` runs an installed entry in the foreground with `REMEMBER_HOOK_TEST` set. A timeout prints C-30, a failure C-31.
- `--dry-run` prints the filter decision, argv, env and payload, and runs nothing.
- `plugin test DIR` runs the plan 24 fixtures against a folder in a scratch brain under a temp data dir, with no install and no confirm.
- `--entry` picks one entry; default all entries. `--event` picks the event type for an action; default `memory.created`. `--query` is the query for a filter; default the fixture query.

## Review points

- `plugin test DIR` writes nothing outside its temp dir.

## Definition of Done

- [ ] `fixtures_pass_plugin_test_dir` (010 test 26).
- [ ] `dry_run_runs_nothing`.
- [ ] `test_sets_remember_hook_test`.
- [ ] `test_timeout_c30_failure_c31`.
- [ ] `plugin_test_dir_no_writes_outside_temp`.
- [ ] The gate passes on the committed tree.

## Implementation Notes

## Review Notes
