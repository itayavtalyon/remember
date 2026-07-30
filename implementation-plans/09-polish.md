# 09 — Polish

## Goal

Production-feeling edges: default DB dir mode 0700, help completeness, sync warning wired, schema version refusal, human preview codepoints, any remaining config tests.

## Work items

1. Default `~/.remember` mode `0700` on create.
2. Sync path warning on resolved path (markers from design).
3. `user_version > 1` refuse with clear stderr.
4. Human list/search columns: `id | key | tags | preview | updated_at`.
5. Preview: 80 codepoints, no mid-UTF-8 split.
6. Help text documents `--key`, `--clear-tags`, `--offset`, exit codes.
7. `--version` string stable.

## Tests that must pass

- `sync_path_warning_on_clouddocs_marker` (V16)
- `schema_user_version_too_new_refused` (V14)
- `db_parent_dir_mode_0700` (V33)
- Full suite green (criteria 1–34)

## More tests?

| Gap | Status |
|-----|--------|
| Mid-UTF-8 preview split | Optional soft |
| Concurrent busy_timeout (V11) | Optional stress |

## Done checklist

- [x] Full suite green (all tests in `tests/`)
- [x] Lint clean

## Review notes (2026-07-29)

### Implementation

- Most polish already landed with earlier command steps (0700 parent, `user_version` refuse, human columns, 80-cp preview, help flags).
- **Wired sync-path warning:** `util_path_looks_synced()` (markers: `com~apple~CloudDocs`, `Dropbox`, `Google Drive`) called from `main` after path resolve, before `store_open`. One-line stderr; proceed. Store stays path-pure.
- General help documents exit codes 0/1/2.
- Gate: `schema_config` added to `tests/gate-suites` (includes `sync_path_warning_on_clouddocs_marker`).

### Pragmatic write-gate

- **DRY:** single marker list in util; warning text only in main. Cleared.
- **Orthogonal:** sync heuristic in util; I/O warning in main; store open unchanged. Cleared.
- **Easy-to-replace:** drop warning by removing one call in `open_store`. Cleared.

## Review notes (2026-07-29) — final v1 review (steps 08–10)

### Verdict for step 09

**Request changes** (one blocking string issue). Architecture of the sync warning is correct; gate + full suite green.

### Findings (addressed 2026-07-30)

- **blocking (fixed):** Sync-path warning is ASCII (`-` not em dash).
- **important (fixed):** Unit tests for all three markers + black-box CLI tests for CloudDocs, Dropbox, and Google Drive (`schema_config`).

## Carried from earlier reviews

- **Sync-path warning** (`path_looks_synced()` in util, called from main after path resolve) — deferred since step 02/04; markers: `com~apple~CloudDocs`, `Dropbox`, `Google Drive`. Test: `sync_path_warning_on_clouddocs_marker`.
- **Help completeness** — document `--key`, `--clear-tags`, `--offset`, exit codes 0/1/2 once those commands exist.
- **Human preview** — 80 codepoints, no mid-UTF-8 split (partially exercised by `human_list_preview_*` once list is complete).
