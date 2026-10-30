# 23 — Import JSON, Markdown, CSV, and `--from-db`

## Goal

`remember import` reads a plan 22 export, or another brain file, into the open brain. Bad records are listed. The good records commit. Settings rows inside an export follow log 007, not add-wins.

```
remember import --from-json PATH | --from-md PATH | --from-csv PATH | --from-db PATH [--origin LABEL]
```

## Status

Copy approved 2026-10-25. PENDING DESIGN for the settings bytes (D-04).

The memory merge, the JSON reader, and `--from-db` for v4, v5, and v6 are specified. Start those stages.

Do not import settings bytes until DESIGN-PENDING D-04 names them. `unknown_field` is C-35 in [`COPY-PENDING.md`](COPY-PENDING.md). Print it. Do not silently ignore an unknown member in an export file.

## Source of truth

- 006 §6, amended by this plan. The old plan 21 decisions D3, D20, D21, D23, D25, D31, D32, D35, D37, P7, P8, P9, and P10 still apply and are restated below.
- [`design-logs/008-json-envelope.md`](../design-logs/008-json-envelope.md) JE-4.1, JE-4.4, JE-5.1, JE-8.1, JE-8.2.
- [`design-logs/007-settings.md`](../design-logs/007-settings.md) ST-8.6, ST-8.7, ST-10.4.
- [`design-logs/010-plugins-and-hooks.md`](../design-logs/010-plugins-and-hooks.md) H-17.1.

D22, D33, and D34 are amended below. Do not implement the old text. E12.6 is deleted. Do not merge prefixes with add-wins.

## Context

Exactly one of `--from-json`, `--from-md`, `--from-csv`, `--from-db` (P9). Zero or several: `usage`, `import requires exactly one of --from-json, --from-md, --from-csv, --from-db`. A repeated `--from-*` or `--origin`: `usage`, `duplicate <flag>` (P7).

JSON reader: `src/jsonread.c`. Pure. No I/O. Own RFC 8259 reader. No vendored JSON library. Do not use SQLite JSON1 for this. Return a tree with the 1-based line of every value start. Nesting limit 64, one named constant, tested. No document size cap. Read the file whole into memory. Reject trailing garbage, NaN, Infinity, invalid UTF-8, and lone surrogates (D3).

No old-name reader. A file that uses `sync_id` or `source` is not mapped to `id` or actors. Those members are unknown fields (JE-8.2, G2-16). No `remember-export` shipped before 0.3.0.

Unknown field rule (JE-4.1 wins over old D33): an unknown member is a warning `unknown_field`, once per file, not a silent ignore. `bin`, `exported_at`, `engine`, and `schema` are known and ignored for merge, because export writes them for readers. `sync_id` and `source` are not known. The warning lines are C-35. Print them. Do not drop the member.

`--from-db` is the exception that still reads old columns. It reads a v4 or v5 SQLite file that has `sync_id`, and a v6 file that has `id` (JE-8.1, ST-10.4). It reads those files read-only and does not migrate them. It ignores `outbox` (H-17.1). It reads bucket prefixes from the E12 tables, using the same E12.7 effective list as plan 19, or from the v6 `remember.buckets.prefixes` row. It does not apply E12.6. A v6 prefix row imports as a brain settings row under ST-8.6. An E12 effective list imports as one `remember.buckets.prefixes` value, the same shape plan 19 writes, with `updated_by` `{kind: import, id: null}` only when the memory rule applies. A settings row keeps the source actor when the source is v6 (ST-8.7). An E12 table has no actor. Store `{kind: human, id: null}` for that one migrated list, the same actor plan 19 uses. This is the plan 19 migration actor, not a new choice.

Import does not read or write a sidecar and does not change the destination device id.

Memory merge:

- Compare identity by `id` when the record has one. Lowercase a valid UUID before merge and before duplicate detection (D35, applied to `id`).
- A record without `id`: when the body matches an existing keyless memory, union tags into that row. Keep that row's `id`, `created_by`, and `created_at`. Count `updated` when tags change, otherwise `unchanged` (amended D22).
- A keyed record whose key exists is `key_clash`. Do not upsert.
- Otherwise insert a new UUID v7 and count `inserted`.
- On every memory row this import writes, keep the file's `created_by`. Set `updated_by` to `{"kind":"import","id":null}` (JE-4.4). This is not the settings rule.
- E7.2 compares `key`, `body`, tags as a set, `expires_at`, and `deleted_at`. It ignores timestamps and actors for the equality check (amended D23).

Links (amended D34, JE-5.1 replaces the old "no endpoint bump"):

- Canonicalize `related` to `(min, max)`.
- `INSERT OR IGNORE` for an existing triple that is unchanged.
- A new link or a changed link updates `updated_at`, `updated_by`, and `change_seq` on both ends. It does not change either memory version vector. The link row gets its own `id` (UUID v7), its own vector bump, and its own `change_seq`.
- Keep the record's link `created_at` and `updated_at`. Missing timestamps become `now`.
- A `supersedes` link that would cycle is skipped. Error `invalid link`. The same cycle check runs for `--from-db`.
- Unlink is not an import operation. A link row with `deleted_at` set stays a tombstone link. Do not delete the row.

Events and `cause.origin` `import` are plan 25. This plan sets the fields in the write table. It does not insert outbox rows until plan 25's insert exists. Call the same write path plan 21 uses so the field changes stay one implementation.

Settings inside a JSON, Markdown, or CSV export: do not read them until D-04. When D-04 is approved, apply ST-8.6 and ST-8.7. Do not apply E12.6. User rows never appear. A brain tombstone imports as a tombstone.

Whole-file failures roll back. Exit 1 `import_parse`. The brain is unchanged.

- JSON root `format` is not `remember-export`.
- JSON root `version` is not 1.
- CSV header is not the JE-8.3 header.
- Markdown `settings.json` is present and unparseable. Until D-04, a missing `settings.json` is not an error, because plan 22 does not write it yet.

A parsed file with bad records commits the good records and exits 0. `errors` lists each bad record.

`errors[]` file and line (D20, D37):

- JSON: file is the basename. Line is the line of the entry's or the link's `{`.
- CSV: basename, and the physical line where the record starts. The header is line 1.
- Markdown: path relative to the import folder, such as `memories/x--abc.md`. Line is the offending front-matter line, or 1 for a whole-file problem.
- A missing link target uses the link's line.
- `--from-db`: file is the source basename. Line is the source `entry_links` rowid of the skipped link.

`skipped` equals the number of `errors` entries (D21). Every skipped item has one error.

Bad-record messages, with `id` in place of `sync_id` and actors in place of `source`:

- `body is missing`
- `body is empty`
- `body is too long`
- `id is not a UUID`
- `invalid timestamp`
- `invalid actor`
- `invalid key or tag`
- `invalid version_vector`
- `invalid link`
- `duplicate id`
- `column count does not match the header`
- `entry is not an object`
- `invalid front matter`
- `duplicate field`

A record is bad when: the body is missing, empty after trim, or longer than 65536 bytes; `id` is present and is not a canonical UUID (`8-4-4-4-12` hex, either case); a present timestamp is not `YYYY-MM-DDTHH:MM:SS.mmmZ`; a present actor is not `{kind, id}` with a legal kind; a present key or tag fails the log 001 checks; `version_vector` is present and is not a JSON object of canonical-UUID device id to a JSON integer greater than 0; a link object lacks `to` or `kind`; `kind` is not `related`, `cites`, or `supersedes`; an `id` repeats in the file; a CSV row's column count differs from the header; a JSON entry is not an object.

Null or an empty CSV cell means absent for `key`, `expires_at`, `deleted_at`, `version_vector`, `created_at`, `updated_at`, actors, `tags`, `links`, and `id` (D31). A null body is `body is missing`.

Front matter exists only when line 1 is exactly `---` and a later line is exactly `---` (D32). Otherwise the whole trimmed file is the body.

A repeated known front-matter field, a front-matter line that is not `<field>: <JSON>`, or a value that is not JSON: `invalid front matter`. A duplicate JSON member name: `duplicate field` (P8).

Markdown link objects get `from` equal to the file's `id`. A file with links and no `id` cannot be a link source. Each such link is `invalid link`. The JSON top-level `links` array uses `{from, to, kind, created_at, updated_at}`. A link without `from` is `invalid link`.

CSV accepts a file with or without the BOM, and with CRLF or LF. Export always writes BOM plus CRLF (P10).

`conflicts.origin` is the basename of the `--from-*` path. For `--from-md` it is the folder name. `--origin LABEL` overrides it. Maximum 64 bytes. Longer is exit 1 `usage` and `origin is too long (max 64 bytes)`. `conflicts --json` includes `origin` after `created_at`.

`--from-md`: when `memories/`, `expired/`, or `deleted/` exists, import only `*.md` directly inside those three directories. Otherwise import only `*.md` directly in the directory. No recursion. Ignore `README.txt` and every other file.

Import of a `remember-export` into an empty brain writes the file's `brain_id`, unless `brains.json` lists an existing brain with that id. Plan 25 owns `brains.json`. Until plan 25, there is no list, so an empty-brain import writes the file's `brain_id`. Import into a brain that already has rows keeps the target `brain_id` (JE-3.11).

A version above 1 fails with the JSON-STYLE §1.7 message. Quote that section. Do not paraphrase it.

## Locked decisions

| ID | Rule |
|---|---|
| G2-16 | No old-name reader for export files. `--from-db` still reads v4 and v5 `sync_id`. |
| G-33 | Memory import keeps `created_by` and sets `updated_by` to `{import, null}`. |
| ST-8.7 | Settings import keeps the file actor. Do not use the memory actor rule on a settings row. |
| JE-5.1 | Link import bumps `updated_at`, `updated_by`, and `change_seq` on both ends. It does not bump memory vectors. |
| H-17.1 | `--from-db` ignores `outbox`. |
| D3 | The pure JSON reader limits above. |

## Prerequisites

Plans 19, 21, and 22 are merged. Settings import from an export also needs D-04 and plan 20's settings write path.

## Scope

In: `jsonread`, the three file readers, `--from-db` v4/v5/v6, shared merge, link rules, origin, the envelope fields `skipped` and `errors`.

Out: zip, registering devices, creating plugins from imported data (H-0.3), outbox inserts (plan 25), settings bytes until D-04, E12.6.

## Files

- `src/jsonread.c` and `src/jsonread.h`. Pure.
- `src/import_read.c`. Readers produce one in-memory record set. File reads go through `fileio.c` from plan 22.
- `src/import_validate.c`. Pure checks.
- All three in `REMEMBER_LIB_SOURCES` and `-Wcast-qual`.
- SQL stays in `src/store_sqlite.c`. One merge function for `--from-db` and for the file formats.
- CLI stays in the existing import command file. Do not add a second import command.

Tests: `tests/test_import_formats.c`, suite `import_formats`. Unit tests for the pure files live in the store test binary. Reuse `tests/fixture_roundtrip.c` from plan 22.

## Must-pass tests

| Test | Proves |
|---|---|
| round trip memories | Export then import into an empty brain matches memories and links. `updated_by` is `{import, null}`. `created_by` is the file's actor. |
| no old names | A file that contains `sync_id` does not import that member as `id`. |
| key clash | A keyed record whose key exists is `key_clash` and does not upsert. |
| keyless union | A record without `id` and with the same body as a keyless row unions tags and keeps the old `id`. |
| bad record | One bad body commits the other rows. `skipped` equals `len(errors)`. The brain stays unchanged on `import_parse`. |
| links | A new link bumps both ends' `change_seq` and does not bump their vectors. A cycle is `invalid link`. |
| from-db v5 | A v5 file's `sync_id` values become `id`. Prefixes become one `remember.buckets.prefixes` row. `outbox` is ignored. The source file is unchanged. |
| from-db v6 | `id` is copied. The v6 prefix row follows ST-8.6. |
| sidecar | The destination device id is unchanged after each format. |
| brain id | Empty target takes the file `brain_id`. A non-empty target keeps its own. |
| settings wait | While D-04 is open, a JSON export without `settings` imports. A test does not require `settings.json`. |

008 verification 12's settings half waits on D-04.

## Stages

1. `jsonread` unit tests, including nesting 64 and trailing garbage.
2. Validate memory records and import them through the shared merge. No settings.
3. Links, including the cycle check and the JE-5.1 bumps.
4. `--from-db` for v4, v5, and v6. Ignore `outbox`. Map E12 to one prefix row.
5. Origin and the `errors` envelope.
6. Emit `unknown_field` with the C-35 line, once per file. Stop the settings-bytes import until D-04 is approved. Then import those bytes.

## Cloud contract

- The same merge rules apply to a cloud import of `remember-export` 1.
- Identical content under two ids merges, and the earlier id wins. Different content is a conflict (Cloud 001, SY-M4). Identical means the E7.2 fields.
- A received change keeps the author's `updated_by` on sync. Import is the `{import, null}` stamp, not the sync stamp.
- Unknown fields on a push are the cloud rule (dropped and reported). That is not this CLI reader. `remember sync --strict` exits 4 when the server reports that warning.
- Do not create a plugin from imported JSON (H-0.3).

## How to work

Same loop as plan 19. Load `standard:coding` and the review docs.

Branch from clean `main` after plan 22. One branch. One pull request.

Do not invent an old-name alias, an add-wins merge, or an `unknown_field` sentence.

Quality gate: plan 19's commands. Coverage 100/100.

## Definition of done

- [ ] Memory import and `--from-db` tests are green.
- [ ] Settings import is absent, or it matches approved D-04 and ST-8.6.
- [ ] No E12.6 code.
- [ ] No reader maps `sync_id` inside a JSON, Markdown, or CSV export.
- [ ] Gate green. Second review clear.
- [ ] INDEX row 23 is ticked only when D-04 is closed and the review is closed. C-35 is already approved.

## Second review handover

Probes:

1. Round-trip three memories and two links into an empty brain. [same ids, `updated_by.kind` is `import`]
2. Feed a v5 database to `--from-db`. [rows arrive, source `user_version` still 5, destination device id unchanged]
3. Feed a CSV with `sync_id` in the header. [`import_parse`, destination unchanged]
4. Import a second copy of the same keyed memory. [`key_clash`]
5. Add a `supersedes` cycle. [link skipped, both memories still present]
6. Confirm no `bucket_prefixes` table is recreated.

Do not re-open: no old-name reader, memory actor stamp, settings actor stamp, link vector rule, ignore outbox.

## Implementation Notes

_(empty)_

## Review Notes

_(empty)_
