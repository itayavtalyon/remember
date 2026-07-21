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

- [ ] Full suite green (all tests in `tests/`)
- [ ] Lint clean
