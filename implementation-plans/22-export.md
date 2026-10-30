# 22 — Export JSON, Markdown, and CSV

## Goal

`remember export` writes the brain to JSON, Markdown, or CSV. A person can read the files. Plan 23 can read them back. The command needs no account, no network, and no quota.

```
remember export --format json|md|csv --out PATH [--bins live,expired,deleted] [--force]
```

## Status

PENDING DESIGN (D-04). No copy row. Copy does not block this plan.

Memory rows, links, atomic write, slugs, and the CSV memory header are specified. Start those stages.

Do not write the `settings` member, Markdown `settings.json`, or a CSV `#settings` row until DESIGN-PENDING D-04 is answered. Do not invent a settings shape. Do not write the E12.9 object.

## Source of truth

- [`design-logs/006-ui-overhaul-engine.md`](../design-logs/006-ui-overhaul-engine.md) §5, except where this plan names an amendment.
- [`design-logs/008-json-envelope.md`](../design-logs/008-json-envelope.md) JE-1.5, JE-3.5, JE-8.2, JE-8.3.
- [`design-logs/007-settings.md`](../design-logs/007-settings.md) ST-8.3. The settings bytes wait on D-04.
- Locked decisions D7, D12, D17, D18, D19, D25, D29, D30, P5, and P6 from the old export plan, restated below. They still apply.

006 E12.9 is dead. 006 E6.2's `sync_id` and `source` are replaced by JE-8.3.

## Context

`--bins` defaults to all three of `live`, `expired`, and `deleted`. A comma list, no duplicates, non-empty. Otherwise `usage` and `invalid --bins (use live, expired, deleted)`.

No account check. No network check. No quota check.

Each memory exports `id`, `key`, `body`, `tags`, `created_by`, `updated_by`, `created_at`, `updated_at`, `expires_at`, `deleted_at`, `bin`, `version_vector`, and its links. No `local_id`. No `sync_id`. No `source`. `bin` is written for readers. Import ignores it (plan 23).

Links in the JSON top-level array are `{from, to, kind, created_at, updated_at}` with `id` ends (the UUID). Export every `entry_links` row whose two ends are in the export. A `related` edge uses the stored ends.

The device-id sidecar and `conflicts` are not exported.

JSON success envelope on stdout, after the file is written, is `remember-message` 1 from plan 21. `data` keeps the old action fields with these names: `action` `exported`, `format`, `path`, `count`, `links`. `path` is absolute: join with the cwd, then lexical `.` and `..` normalization, no symlink resolution (D7). Human line: `exported N memories, M links to <path>` (D19).

File errors: ENOSPC is `disk_full`. EACCES or EROFS is `read_only`. ENOENT on the parent is `cannot_open` with `reason` `missing`. Any other errno is `io_error`. `detail.path` is the out path. `detail.errno` is set. These codes already have plan 16 sentences. Keep those sentences.

Missing `--format`: `export requires --format (json, md, or csv)`. Bad value: `invalid --format (use json, md, or csv)`. Missing `--out`: `export requires --out PATH`. A repeated flag: `duplicate <flag>`.

Atomic write. Temp name is `<PATH>.tmp-<16 lowercase hex>`. Write the temp file, then `rename` it onto PATH. PATH exists and there is no `--force`: write nothing, exit 1, `usage`, `output already exists: <path>`. `--force` replaces a file by that rename. Remove the temp name on every failure path (P5).

Markdown `--force` on an existing folder (D17): write `<PATH>.tmp-<rand>/`, rename the existing PATH to `<PATH>.old-<rand>/`, rename the temp directory onto PATH, then delete the old folder recursively. A crash leaves the old or the new folder plus a stray sibling. It never leaves a half-written PATH.

Determinism (D18), with the new field names: one compact JSON line plus a final newline. Tags sorted by code point. `version_vector` keys sorted. Top-level `links` sorted by (from id, to id, kind). The same orders inside Markdown and CSV cells. `created_by` and `updated_by` use the actor field order `kind`, then `id`.

JSON document (amends E6.a). UTF-8, no BOM. Members in this order:

1. `format` is `remember-export`.
2. `version` is 1.
3. `brain_id` is `brain_meta.brain_id`.
4. `exported_at`.
5. `engine` is `REMEMBER_VERSION`.
6. `schema` is the open brain's `user_version` (6 after plan 19).
7. `entries`.
8. `links`.
9. `settings`. Do not write this member until D-04. Reserve the position. A build that runs before D-04 omits `settings` rather than writing `{}` or the E12.9 object. Say so in the test name. When D-04 answers, write exactly that answer and change the test.

Entry member order: `id`, `key`, `body`, `tags`, `created_by`, `updated_by`, `created_at`, `updated_at`, `expires_at`, `deleted_at`, `bin`, `version_vector`. Entries sort by `created_at`, then `id`.

Markdown is a folder: `README.txt`, `memories/`, `expired/`, `deleted/`. `settings.json` waits on D-04. Do not write `settings.json` until then. The three bin folders always exist, including empty folders for a bin `--bins` excluded (D29).

`README.txt` is exactly this sentence plus one newline (D30):

```
This folder is a Fortkeeps export. Import it with: remember import --from-md <folder>
```

File name: `<slug>--<last 12 hex of id>.md` (E6.b2, D12). Slug is the key, else the body's first line when it starts with `# ` (text after that prefix, ASCII-trimmed, empty means skip), else the first 6 words. First 6 words: collapse whitespace and control runs to one space, split on spaces, take the first 6, join with one space. Then ASCII lowercase, every run outside `[a-z0-9]` becomes `-`, trim `-`, max 48 characters, trim `-` again after the cut, empty becomes `memory`. Reuse the plan 18 helpers in `search_terms.c`.

Markdown file: a line `---`, then one field per line in the order `remember_export`, `id`, `key`, `tags`, `created_by`, `updated_by`, `created_at`, `updated_at`, `expires_at`, `deleted_at`, `version_vector`, `links`, each `<field>: <JSON>`, then a line `---`, then the body bytes, then one newline. `remember_export` is the JSON number `1`. Null is JSON `null`. `links` in the file is a JSON array of `{to, kind, created_at, updated_at}` with no `from`, for edges whose stored `from` is this memory (E6.b4, D25).

CSV: RFC 4180, UTF-8 with BOM, CRLF, every field quoted, `"` doubled. Header, exact:

```
id,key,body,tags,created_by,updated_by,created_at,updated_at,expires_at,deleted_at,bin,version_vector,links
```

Actor, tag, vector, and link cells hold JSON. NULL is an empty cell. The `links` cell uses the Markdown shape (D25). A `related` edge appears on its stored `from` end only. Do not add a `#settings` row until D-04.

Bin folders: `live` → `memories/`, `expired` → `expired/`, `deleted` → `deleted/`.

No zip. The engine writes no zip and reads no zip.

## Locked decisions

| ID | Rule |
|---|---|
| R3-00b / G2-16 | `remember-export` version 1. No old-name reader. No `sync_id`. No `source`. |
| JE-8.3 | The CSV header above. Markdown front matter uses the same names. |
| ST-8.3 | Brain export carries brain settings rows only, including tombstones, never user rows. The container is D-04. |
| D17 | Folder swap for Markdown `--force`. |
| D19 | Human line and the file-error codes. |
| E6.1 | No account, network, or quota. |

## Prerequisites

Plans 19 and 21 are merged. `id`, actors, and the envelope writer exist. Plan 20's brain settings rows exist if the D-04 stage runs. Memory export does not need plan 20.

## Scope

In: the export command, one read transaction, JSON and Markdown and CSV memory writers, atomic write, `--force`, D19 errors, help, a facade byte-match test that masks `exported_at`.

Out: import (plan 23), zip, sidecar, conflicts, user rows, the settings bytes until D-04, the daemon.

## Files

- `src/cmd_export.c` in `REMEMBER_LIB_SOURCES` and `-Wcast-qual`.
- `src/export_format.c` and `src/export_format.h`. Pure. Slug, front matter, CSV quoting, document assembly on top of `output.c` escaping. No file I/O.
- `src/fileio.c` and `src/fileio.h`. Temp file, rename, D17 folder swap, recursive delete, errno to code. The only export file I/O.
- `store_export_snapshot` declared in `src/store.h` and implemented in `src/store_sqlite.c`.
- Tests: `tests/test_export.c`, suite `export`. A fixture builder `tests/fixture_roundtrip.c` that plan 23 reuses. Do not put settings bytes in that fixture until D-04.

Do not add a settings-specific exporter module. When D-04 answers, format the approved bytes inside `export_format.c`.

## Must-pass tests

| Test | Proves |
|---|---|
| fields | A memory round-trips its JE-8.3 fields except `settings`. No `local_id`, no `sync_id`, no `source`. |
| bins | `--bins live` writes only live memories. Markdown still has the three folders. |
| force | Existing PATH without `--force` writes nothing. `--force` replaces a file. Markdown `--force` leaves no half-written directory after a simulated crash between the two renames. |
| determinism | Two exports of the same brain match byte for byte except `exported_at`. |
| csv | BOM, CRLF, every field quoted, header exact, NULL is empty. No `#settings` row while D-04 is open. |
| readme | `README.txt` is the locked sentence plus one newline. |
| errors | Missing flags use the locked usage sentences. A full disk maps to `disk_full`. |
| human | `exported N memories, M links to <path>`. |
| settings omitted | While D-04 is open, the JSON document has no `settings` member and the folder has no `settings.json`. |

008 verification 12 (export then import) is plan 23, after D-04.

## Stages

1. Snapshot reader and JSON memory document without `settings`. Red test first.
2. Atomic write, `--force`, and D19 errors.
3. Markdown folder, slug, front matter, README. No `settings.json`.
4. CSV memory rows. No `#settings` row.
5. Facade test and help. Stop. Stage 6 starts only after D-04 is pasted into this plan: write the approved settings bytes and add the tests D-04 requires.

## Cloud contract

- `GET /v1/export` returns JSON, Markdown, or CSV in every account state, including lapsed. Quota errors never block export (Cloud 001, M-12, M-14).
- The body uses `remember-export` version 1 and the CSV header in this plan.
- No `local_id`. `id` is the UUID. Actors are `created_by` and `updated_by`.
- User settings rows are not in the export. Brain settings rows are, once D-04 names the bytes.
- Cloud v1 does not offer end-to-end encryption.

## How to work

Same loop as plan 19. Load `standard:coding` and the review docs named there.

Branch from clean `main` after plans 19 and 21. One branch. One pull request.

Do not invent the settings container. A D-04 guess is a defect.

Quality gate: plan 19's commands. Coverage 100/100.

## Definition of done

- [ ] Memory export tests are green.
- [ ] Settings bytes are absent, or they match an approved D-04 answer quoted in this plan.
- [ ] No E12.9 object anywhere in the output.
- [ ] Gate green. Second review clear.
- [ ] INDEX row 22 is ticked only when D-04 is closed and the review is closed.

## Second review handover

Probes:

1. Export JSON and look at the first keys. [`format`, `version`, `brain_id`, then `exported_at`]
2. Confirm no `sync_id`, `source`, or `local_id`.
3. Export Markdown and confirm `README.txt` bytes, including the final newline.
4. Export CSV and confirm the header line and the BOM.
5. While D-04 is open, confirm `settings` is absent.
6. `--force` on a Markdown folder, kill between the two renames if the test shim allows it. [old or new folder intact]

Do not re-open: no zip, atomic rename, RFC 4180, slug rules, no user rows.

## Implementation Notes

_(empty)_

## Review Notes

_(empty)_
