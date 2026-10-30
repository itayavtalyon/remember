# 26 — Brain path, Codex skill, 0.3.0 release prep

## Goal

Teach the engine to read `~/.remember/default-db`. Install the skill for Codex. Prepare version 0.3.0. Do not tag and do not publish.

This plan runs last. It does not ship 0.3.0 before plans 19 through 25.

## Status

Copy approved 2026-10-25. C-40 and C-41 are in [`COPY-PENDING.md`](COPY-PENDING.md). Paste them in stage 4. Do not invent another sentence.

Stages 1 and 2 are specified and may start from a clean `main`. Do not bump the version in those stages. Stage 4 waits until plans 19 through 25 have merged. Do not tag.

## Source of truth

- [`design-logs/006-ui-overhaul-engine.md`](../design-logs/006-ui-overhaul-engine.md) §8 (E9.1, X-E04) and §13 (E15.1–E15.4), plus D24 and D36.
- [`design-logs/007-settings.md`](../design-logs/007-settings.md) ST-10.2. Schema v6 and `min_reader_version` `0.3.0` replace the 006 sentence that says Homebrew publishes before any app that links schema v5. The release is still 0.3.0. The schema it publishes is v6.
- [`docs/RELEASING.md`](../docs/RELEASING.md). Itay tags. This plan does not.

E12.10 is wrong after plan 20. There is no `remember config get bucket-prefixes`. Do not copy that sentence into the skill.

## Context

Brain path, first match wins:

1. `--db PATH`.
2. `REMEMBER_DB` when it is set. An empty value counts as unset (D24).
3. The absolute path in `~/.remember/default-db`.
4. `~/.remember/remember.db`.

`~` is the home directory the engine already uses. `default-db` is a UTF-8 file with one line. The app writes a trailing newline. The engine trims ASCII whitespace and accepts a missing trailing newline. Ignore the file when it is missing, the line is empty, the path is not absolute, or the bytes are not valid UTF-8 (D36). Read only the first line. The engine never creates `default-db`.

A path from `default-db` still goes through the existing `:memory:` and `file:` refusal and the existing length check.

`remember-install-skill` installs `~/.codex/skills/remember/SKILL.md` when `~/.codex` exists. The same symlink and `--copy` modes as the other products. Install only when the product folder exists. The same rule for `REMEMBER_SKILL_DIRS`.

X-E04: before writing the Codex path into the installer, check the Codex documentation for the user skill folder. When the documented folder is not `~/.codex/skills/`, write the documented path into 006 Implementation Results and use that path. Do not guess a third path. When the documentation is unavailable, stop and ask Itay. Do not pick `~/.codex/skills/` as a fallback in that case. The locked default applies only when the documentation confirms it, or when Itay confirms the spike cannot be run.

Version: set `0.3.0` in `CMakeLists.txt` and in the `REMEMBER_VERSION` fallback in `src/remember_app.c`. They must match. Do this in stage 4 only, after plans 19 through 25 have merged.

What's New: each feature plan adds its own line under the 0.3.0 heading (006 EN-04). This plan consolidates those lines. It does not rewrite a feature. The skill page must drop `--json-errors`, `--offset`, `--sync-id`, `--source`, `purge-trash`, and the claim that `--trash` aliases `--expired`. Paste C-41. The bucket sentence is C-40. Do not write the E12.10 sentence.

Upgrade notes that are already decided and may be included with C-41, using these facts and no others:

- Opening a v5 brain migrates it to v6.
- An engine older than the brain's `min_reader_version` can read and cannot write. The write error is `schema_too_new`.
- `min_reader_version` is `0.3.0`.
- `--trash` selects Deleted memories. `--expired` selects expired memories.

Do not write the E12.10 bucket sentence. Paste C-40.

Tagging, the GitHub release, and the Homebrew formula are Itay's steps. This plan prepares the tree and stops.

## Locked decisions

| ID | Rule |
|---|---|
| E15 | The path order above. The engine does not create `default-db`. |
| D24 | Empty `REMEMBER_DB` is unset. |
| D36 | Invalid UTF-8 is ignored. Only the first line is read. |
| E9.1 | Codex skill directory when `~/.codex` exists, subject to the X-E04 spike. |
| X-E01 amended | The release number is 0.3.0. The schema is v6, not v5. |
| D26 | Android presets are not part of this plan. |

## Prerequisites

Plans 19 through 25 are merged before stage 4. Stages 1 and 2 may start earlier, from a clean `main`, and must not bump the version.

## Scope

In: path resolution, the installer change, the version bump, the skill and What's New sweep after copy approval, a release checklist for Itay.

Out: tagging, Homebrew tap edits, the Mac app, Android, the daemon, any feature from plans 19–25.

## Files

- `src/util.c` for the path order.
- `CMakeLists.txt` and `src/remember_app.c` for the version, in stage 4 only.
- `scripts/remember-install-skill`, `scripts/install.sh`, `tests/test_install_skill.sh`.
- `skills/remember/SKILL.md` and `README.md` in stage 4 only.
- `docs/engineering-notes.md` for the path order.

Tests for the path live in the existing schema or config test file. Suite name stays the one that file already uses. Do not add a new test binary.

## Must-pass tests

| Test | Proves |
|---|---|
| E15 set | A set `REMEMBER_DB` hides `default-db`. |
| E15 empty | An empty `REMEMBER_DB` does not hide `default-db`. |
| E15 absolute | An absolute line is the brain path. |
| E15 ignore | A missing file, a relative line, an empty line, and invalid UTF-8 use `~/.remember/remember.db`. The engine does not create `default-db`. |
| E15 flags | A `default-db` value of `:memory:` or a `file:` URI fails the way `--db` fails today. |
| E9.1 | When a fake home has `~/.codex` and not the other product folders, the installer writes only the Codex skill. `--copy` and symlink match the other products. |

## Stages

1. Implement E15 and D24 and D36. Do not bump the version.
2. Run the X-E04 check. Then implement the Codex install path the check confirmed. Do not bump the version.
3. Stop until plans 19 through 25 are merged.
4. Paste C-40 and C-41. Set the version to 0.3.0 in both places. Run the full gate. Write a release checklist in Implementation Notes that tells Itay the tag name, the schema version, and that this plan did not tag.

## How to work

Same loop as plan 19. Load `standard:coding` and the review docs. Also read [`docs/RELEASING.md`](../docs/RELEASING.md) before stage 4.

Branch from clean `main`. One branch. One pull request.

Do not tag. Do not push a release. Do not invent a sentence past C-40 and C-41.

Quality gate: plan 19's commands. Coverage 100/100.

## Definition of done

- [ ] Path tests and installer tests are green.
- [ ] Version is still the pre-0.3.0 value, or stage 4 has run and the version is `0.3.0` in both files, with C-40 and C-41 pasted.
- [ ] No `remember config get bucket-prefixes` remains in the skill.
- [ ] The git history of this plan contains no tag.
- [ ] Gate green. Second review clear.
- [ ] INDEX row 26 is ticked only after stage 4 and the review. Stages 1 and 2 alone do not tick it.

## Second review handover

Probes:

1. `REMEMBER_DB` set to a temp brain, `default-db` pointing elsewhere. [the env path wins]
2. `REMEMBER_DB=` empty, `default-db` absolute. [the file path wins]
3. Relative `default-db`. [built-in path, file still not created by the engine]
4. Installer with only `~/.codex` present. [one skill path]
5. `git tag --points-at HEAD`. [no release tag from this plan]
6. Search the skill for `bucket-prefixes` and `--json-errors`. [absent after stage 4, or stage 4 has not started]

Do not re-open: empty env is unset, engine does not create `default-db`, Itay tags, schema is v6.

## Implementation Notes

_(empty)_

## Review Notes

_(empty)_
