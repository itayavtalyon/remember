# 27 — Default brain path

**Module:** `util_resolve_db_path` in `src/util.c`. The declaration stays in `src/util.h`.
**Requires:** nothing above plan 19. Plans 25 and 26 do not require this plan.

## Goal

The engine reads `~/.remember/default-db` when `--db` and `REMEMBER_DB` do not choose a path.

## Source

- `decision:ui-overhaul-grill-2026-10-12`, item 3 (the path order and the file rules).
- Spec: `specs/default-db.md` (new).

## Out of scope

The app that writes the file, the LaunchAgent, and the Codex skill. The engine does not create `default-db`. A file that is not one UTF-8 line ending in a newline is not given a new rule here: the coder does not add a case the decision does not name.

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md).

Tests are added to `tests/test_coverage_edges.c`. That suite is already in `tests/gate-suites`. Do not add a test binary.

A path taken from the file goes through the existing `copy_regular_db_path`. Do not write a second check for `:memory:` or a `file:` URI.

`REMEMBER_DB` is read with the existing `env_nonempty`. An empty value stays unset.

## What must be true

- The order is `--db`, then a set `REMEMBER_DB`, then the one absolute path in `~/.remember/default-db`, then `~/.remember/remember.db`.
- A set `REMEMBER_DB` hides the file.
- The file is one UTF-8 line, the absolute path, ending in a newline. The engine trims ASCII whitespace.
- A missing file, an empty line, or a path that is not absolute counts as no file. The engine then uses `~/.remember/remember.db`.
- The engine does not create the file.
- `~` is the home directory `util_resolve_db_path` already uses.

## Review points

- The reviewer checks that an empty `REMEMBER_DB` still goes through `env_nonempty` and does not hide the file.
- The reviewer checks that the engine does not create `default-db` on a missing file, an empty line, or a relative line.
- The reviewer checks that `:memory:` and a `file:` URI from the file fail with the same error `copy_regular_db_path` already returns for `--db`.
- The reviewer checks that the coder did not add a rule for a second line or for invalid UTF-8.

## Definition of Done

- [ ] `a set REMEMBER_DB hides default-db` (`tests/test_coverage_edges.c`).
- [ ] `an absolute line is the brain path`.
- [ ] `a missing file uses ~/.remember/remember.db and the engine does not create default-db`.
- [ ] `an empty line uses ~/.remember/remember.db and the engine does not create default-db`.
- [ ] `a relative line uses ~/.remember/remember.db and the engine does not create default-db`.
- [ ] `a default-db value of :memory: fails the way --db :memory: fails`.
- [ ] `specs/default-db.md` is written, and `decision:ui-overhaul-grill-2026-10-12` item 3 is cited from that spec. The decision stays `state:accepted` because this plan builds one item of it.
- [ ] The gate passes on the committed tree, and the notes record the mutants and their red tests.

## Implementation Notes

## Review Notes
