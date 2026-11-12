# 23 — Import JSON, Markdown, CSV, and `--from-db`

## Goal

`remember import` reads a plan 22 export, or another brain file, into the open brain. Bad records are listed. The good records commit. Settings rows inside an export follow log 007, not add-wins.

```
remember import --from-json PATH | --from-md PATH | --from-csv PATH | --from-db PATH [--origin LABEL]
```

## Status

Copy approved 2026-10-25. DESIGN-PENDING D-04 is answered (2026-10-25, 007 ST-8.3a).

The memory merge, the JSON reader, and `--from-db` for v4, v5, and v6 are specified. Start those stages.

Markdown and CSV moved to [plan 23a](23a-import-md-csv.md) on 2026-11-12 (`decision:plan23-review-g1-md-csv-to-23a`). This plan ships `--from-json` and `--from-db`. `--from-md` and `--from-csv` stay a `usage` error until 23a. The Markdown and CSV rules below stay here as 23a's source of truth.

`unknown_field` is C-35 in [`COPY-PENDING.md`](COPY-PENDING.md). Print it. Do not silently ignore an unknown member in an export file.

## Source of truth

- 006 §6, amended by this plan. The old plan 21 decisions D3, D20, D21, D23, D25, D31, D32, D35, D37, P7, P8, P9, and P10 still apply and are restated below.
- [`design-logs/008-json-envelope.md`](../design-logs/008-json-envelope.md) JE-4.1, JE-4.4, JE-5.1, JE-8.1, JE-8.2.
- [`design-logs/007-settings.md`](../design-logs/007-settings.md) ST-8.3, ST-8.3a, ST-8.4 to ST-8.7, ST-10.4.
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

Settings inside a JSON, Markdown, or CSV export (007 ST-8.3a): read the JSON `settings` member, the Markdown folder's `settings.json`, or `<CSV basename>.settings.json` beside the CSV file (`brain.csv` → `brain.settings.json`; read no other name, and read a stale matching file as the user's file). Each is a `remember-settings-export` version 1 document. Apply ST-8.4 to ST-8.7, the same as `settings import --scope brain`. An absent member or file writes no settings row. 006 E7.11 is dead. Do not apply E12.6. User rows never appear. A brain tombstone imports as a tombstone.

Whole-file failures roll back. Exit 1 `import_parse`. The brain is unchanged.

- JSON root `format` is not `remember-export`.
- JSON root `version` is not 1.
- JSON root `entries` or `links` is present, not null, and not an array, or `brain_id` is present, not null, and not a canonical UUID (`decision:plan23-review-g4-shape-is-import-parse`).
- Any JSON string or member name holds `\u0000` (`decision:plan23-review-g3-nul-is-import-parse`).
- CSV header is not the JE-8.3 header.
- The JSON `settings` member, Markdown `settings.json`, or the matching `<CSV basename>.settings.json` is present and is not a settings document (007 ST-8.4). A missing `settings.json` is not an error.

A parsed file with bad records commits the good records and exits 0. `errors` lists each bad record.

The import result has no `prefixes_merged` (006 E7.6 is superseded). It has `settings` `{written, unchanged, skipped}` (007 ST-8.3a). A `kept_local` row counts as `unchanged`. An import with no settings writes all three as 0. Settings rows are not counted in `inserted`, `updated`, `unchanged`, `skipped` or `errors`. A skipped settings row gives its warning in `warnings`.

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

A record is bad when: the body is missing, empty after trim, or longer than 65536 bytes; `id` is present and is not a canonical UUID (`8-4-4-4-12` hex, either case); a present timestamp is not `YYYY-MM-DDTHH:MM:SS.mmmZ`; a present actor is not `{kind, id}` with a legal kind; a present key or tag fails the log 001 checks (a key or tag that passes is stored normalized, as `add` stores it: `decision:plan23-review-g6-normalize-keys-tags`); `version_vector` is present and is not a JSON object of canonical-UUID device id to a JSON integer greater than 0; a link object lacks `to` or `kind`; `kind` is not `related`, `cites`, or `supersedes`; an `id` repeats in the file; a CSV row's column count differs from the header; a JSON entry is not an object.

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

Plans 19, 21, and 22 are merged. Settings import from an export also needs plan 20's settings write path.

## Scope

In: `jsonread`, the three file readers, `--from-db` v4/v5/v6, shared merge, link rules, origin, the envelope fields `skipped`, `errors` and `settings`.

Out: zip, registering devices, creating plugins from imported data (H-0.3), outbox inserts (plan 25), E12.6.

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
| settings | The plan 22 one-tombstone export imports into an empty brain as a tombstone in all three formats. A JSON export without `settings` imports with no settings row. A Markdown folder without `settings.json`, or a CSV without its matching `<basename>.settings.json`, imports. A `settings.json` beside `brain.csv` is not read. A bad value is skipped with `setting_invalid` and the other rows are written. A `settings` member that is not a settings document fails with `import_parse` and the brain is unchanged. |

008 verification 12's settings half is the `settings` test above.

## Stages

1. `jsonread` unit tests, including nesting 64 and trailing garbage.
2. Validate memory records and import them through the shared merge. No settings.
3. Links, including the cycle check and the JE-5.1 bumps.
4. `--from-db` for v4, v5, and v6. Ignore `outbox`. Map E12 to one prefix row.
5. Origin and the `errors` envelope.
6. Import export settings per 007 ST-8.3a.
7. Emit `unknown_field` with the C-35 line, once per file.

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
- [ ] Settings import matches 007 ST-8.3a and ST-8.4 to ST-8.7.
- [ ] No E12.6 code.
- [ ] No reader maps `sync_id` inside a JSON, Markdown, or CSV export.
- [ ] Gate green. Second review clear.
- [ ] INDEX row 23 is ticked only when the review is closed. D-04 and C-35 are closed.

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

### Stage 1 — jsonread

- `src/jsonread.c` / `src/jsonread.h`: pure RFC 8259 reader. `JSONREAD_NESTING_MAX` is 64 (P11). No document size cap. Numbers stay raw text. Duplicate object members are kept (P8 is a later bad-record rule).
- Unit tests live in `remember_store_tests` (`tests/test_jsonread.c`), wired in CMake with `-Wcast-qual` and `REMEMBER_LIB_SOURCES`.
- Mutants recorded: nesting `>`→`>=` → `jsonread_accepts_nesting_at_the_named_limit` red (status 3); trailing-garbage skip would hit `jsonread_rejects_trailing_garbage`; lone `\uD800` would hit `jsonread_rejects_invalid_utf8_and_lone_surrogates`.

### Stage 2 — validate + shared merge + `--from-json`

- `src/import_read.c` / `import_validate.c`: JSON export → `ImportDoc`; validate marks bad memories; tags sorted for E7.2 set equality.
- `fileio_read_all`: whole-file read, no size cap.
- `store_import_doc`: one write txn; empty brain takes `brain_id`; `updated_by` is `{import,null}`; keyless id-less unions tags (amended D22); keyed id-less inserts (does not union); key clash does not upsert.
- CLI: exactly one of `--from-json|--from-md|--from-csv|--from-db`; stage 2 implements JSON (+ existing `--from-db`); md/csv accepted for the exactly-one check then refuse with usage until later stages.
- Version > 1 → `import_parse`, exit 1, nothing written (`decision:plan23-export-version-above-1`, parallel to settings C-43).
- Suite `import_formats`: 10 tests. Prove insert / brain_id / key_clash / keyed-without-id / keyless union / unchanged / bad body / unsorted tags / version>1 / sidecar.
- Mutants: `ntags > 1` → `> 99` → `import_json_same_id_with_unsorted_tags_is_unchanged` red (concurrent_vv); `ver > 1` → `>= 1` → insert test red (`import_parse` on version 1).

## Review Notes

### Stage 1 deep review (2026-11-10, in session)

Gate for this stage: `remember_store_tests` 521/0 including the 11 `jsonread_*` tests; `remember` links clean with sanitizers.

Lenses: C safety (alloc checks, UTF-8/surrogate rejects, no ignored parse status), architecture (pure TU, no I/O, SQLite untouched), simplify (kept separate array/object growers; one recursive `parse_value`).

Findings fixed before commit: `-Wpadded` on `Parser` (explicit `pad_[4]`); test bodies return after a failed parse so UBSan does not chase a NULL root.

No product or architecture grill. C-35 is unused until stage 7; printed at session start. No old-name alias. No add-wins.

Ready for stage 2.

### Stage 2 deep review (2026-11-11, in session)

Gate for this stage: `import_formats` 10/0; `sync_import` 39/0; `envelope` 40/0; `remember_store_tests` 521/0; `remember` links with sanitizers.

Lenses: C safety (pad_ on ImportMemory/ImportDoc, no cast-qual aliases without `import_alias`, OOM on read), architecture (readers pure of SQL; merge in store_sqlite; CLI stays one command), E7.2 tag sets, simplify (dedicated version/not-export message helpers).

Bugbot (branch uncommitted): keyed-without-id wrongly took the keyless union path — fixed + test; `content_identical` ignored `expires_at` on equal VV — file merge now uses `e72_identical`. Low: non-array `entries` ignored (deferred with envelope work); `--from-json --from-md` path confusion matches existing `take_value` pattern.

No product grill. C-35 still unused. No old-name alias. No add-wins. `--from-md`/`--from-csv` bodies deferred to later stages by design.

Ready for stage 3.

### Stage 3 — links + JE-5.1

- `ImportLink` in the reader; top-level JSON `links` parsed and validated.
- `store_import_doc` writes links after memories: related → (min,max); existing live triple ignored; new row keeps file stamps; ends get `{import,null}` + `change_seq` without memory VV bump; supersedes cycle skipped.
- Tests: cites write + re-import, cycle skip, related canonicalize. Mutant: disable cycle guard → two live supersedes (build-checked via `if (0 && …)` unused-var path; functional red on skip-count query when guard removed cleanly).

### Stage 3 deep review (2026-11-11, in session)

Gate: `import_formats` 13/0. Lenses: JE-5.1 ends vs link VV, cycle skip not hard-fail, missing endpoints → skipped. No grill. C-35 unused.

### Stage 4 — `--from-db` v4/v5/v6

- `IMPORT_MAX_USER_VERSION` is 6. v6 rows read `id` / `local_id` / JSON actors; tags still join on `local_id`.
- Link remap: v6 UUID lookup; tombstoned links skipped; `related` canonicalized; supersedes cycles skipped.
- E12 effective list → one `remember.buckets.prefixes` with `{human,null}` when the dest has no row (no add-wins). Invalid shapes stay out.
- v6 prefixes row → ST-8.6 (VV, then `updated_at`, then `device_id`; tie keeps local) with history (ST-8.7).
- `outbox` is never opened. Conflicts.origin still not copied (stage 5).
- Tests: refuse user_version 7; v5 prefixes land as `["work:"]`; v6 copies id + prefixes and leaves outbox alone (does not call `run_import`, which downgrades to v5).
- Mutants: `IMPORT_MAX` 6→5 → v6 import red; E12 `existing > 0` skip removed → would overwrite dest prefixes; deleted_at link continue removed → would revive tombstones.

### Stage 4 deep review (2026-11-11, in session)

Gate: `sync_import` 40/0; `import_formats` 13/0. Lenses: read-only source, no sidecar touch, no E12.6/add-wins, outbox ignored, human vs import actor for E12 vs memory.

Auto-fix: v6 test must not use `run_import` (harness_make_v5_source); prefixes need trailing `:`. No product grill. C-35 unused. Ready for stage 5.

### Stage 5 — origin + errors envelope

- `--origin LABEL` (max 64 bytes) or basename of the `--from-*` path; longer → usage.
- `conflicts.origin` written on insert/refresh; `conflicts --json` prints `origin` after `created_at` (null when unset).
- Import envelope gains `skipped` and `errors[{file,line,message}]` with `skipped == len(errors)`.
- Bad memories/links push an error instead of a bare skipped bump.
- Mutants: origin length `>`→`>=` → 64-byte label refused; skipped≠nerrors guard in `output_import_data`.

### Stage 5 deep review (2026-11-11, in session)

Gate: `import_formats` 14/0; `sync_import` 40/0; store tests 521/0. No grill. Settings counts still stage 6. C-35 unused. Ready for stage 6.

### Stage 6 — settings import ST-8.3a

- Nested `settings` object extracted only when the root member is a JSON object; bytes fed to `store_setting_import_apply` inside the memory/link write txn (`own_txn=false`, no nested BEGIN/COMMIT/ROLLBACK).
- CLI dry-runs `store_setting_import` before any write: `SETTING_ERR_DOCUMENT` → `import_parse` with `"settings" isn't a settings document.`; skip warnings via `settings_import_warn_skips(rep, false)` so D-15 never claims the brain was unchanged when memories write.
- Import envelope `settings.{written,unchanged,skipped}` (zeros when absent). File `updated_by` kept (ST-8.7). Actor for memories remains `{import,null}`.
- Tests: tombstone lands; absent settings → zeros/no rows; `{}` → import_parse/unchanged brain; bad value skipped + good row + memory written + `setting_invalid`.
- Mutants: `settings_len > 0` → absurd threshold → tombstone test red; extract gated on root object (Bugbot).

### Stage 6 deep review (2026-11-11, in session)

Gate: `import_formats` 19/0; `sync_import` 40/0; `settings` 151/0; store 521/0.

Lenses: txn nesting (apply must not `import_finish` the outer txn), dry-run before writes, ST-8.4/8.5 warnings shared with `settings import`, envelope counts, simplify (one `setting_import_run`).

Auto-fix: extract only when root `settings` is an object; OOM mapped via `serr` text (no fake `SETTING_ERR_OOM`); Bugbot findings on missing apply path addressed in this stage.

No product grill. `--from-md`/`--from-csv` settings still deferred with those readers. Ready for stage 7.

### Stage 7 — unknown_field C-35

- Scan root, then entries, then links; first unknown member name wins (`sync_id` / `source` are unknown).
- `diag_unknown_field` once per path with the C-35 file line. Import continues.
- Test: root `sync_id` + entry `source` → one warning naming `sync_id`; memory still inserts.
- Mutant: clear `unknown_field` after scan → unknown-field test red.

### Stage 7 deep review (2026-11-11, in session)

Gate: same as stage 6 plus the C-35 test green. Printed C-35 at session start; no invented sentence. Mutant clearing `unknown_field` after the root scan turns `import_json_unknown_field_warns_once_and_still_imports` red. No grill. Ready for full gate and whole-plan review.

### Whole-plan deep review (2026-11-11, in session — ready for second opinion)

Scope: LEGACY PLAN 23 tip `d05a8f0` on `plan/23-import` (base `plan/22-export`).

Gate: `import_formats` 19/0; `sync_import` 40/0; `settings` 151/0; store ASan 521/0; black-box gate suites including `import_formats` green except three envelope PTY tests (`openpty` → EPERM in this agent host; unrelated to plan 23). `tests/gate-suites` now lists `import_formats`.

Simplicity (S): `setting_import_run` with `own_txn` is the right split; extract gated on root object. Byte `extract_top_object` remains a pragmatic choice for nested settings bytes; md/csv readers still deferred.

Safety (A): nested apply never COMMIT/ROLLBACK the outer brain txn; dry-run validates settings before memory writes; bad settings document → `import_parse`, brain unchanged; unknown fields warn once and do not fail the import; no old-name/`sync_id` mapping in JSON; settings keep file actors (ST-8.7); memories still stamp `{import,null}`.

Not done here (by plan): INDEX 23 / D-04 / C-35 stay open until review closes; `--from-md`/`--from-csv` bodies; ask before push/PR (base `plan/22-export`).

`.claude/WHOLE_PLAN_REVIEW` written for the whole-plan second opinion.

### Whole-plan second deep review (2026-11-12, separate session)

Scope: plan 23 commits `6363119..f72a8ce` on `plan/23-import` and the production code they touch. Fixes are on `claude/magical-curie-4aa42e`:

- `f74286e`: eleven exploits, each with its test.
- `eabbd9c`: a test for every bad-record message, and the sorted duplicate-id check.
- `a03b65a`: simplifications, and the usage lines pinned.
- `52fe0f9`: round-trip and no-old-names tests, and the spec.
- `b9c7a43`: grill answers and mutant killers.
- `e4330d2`: plan 23a.
- `36388c4`: the last mutant killers.
- `f7b8c53`: a test for an unreadable import source. The coverage gate found that `import_report_io` never ran.

Production code: +517 / −776 lines. `import_formats` went from 19 to 72 tests.

#### Spec against code

- **Critical.** `--from-md` and `--from-csv` were not built. Both flags still print `not implemented yet`, and no stage had them scheduled. Itay moved them to [plan 23a](23a-import-md-csv.md) (G1).
- `--from-db` memories kept the source `updated_by`, which breaks G-33 and JE-4.4. A hostile v6 `created_by` became `human` with no error. Fixed (G2).
- `--from-db` links had no kind or timestamp check, no `errors` entry when skipped, and no JE-5.1 end bumps. They now go through the file link path (`import_remap_links`), and `errors[].line` is the source rowid.
- P8 `duplicate field` was not built. JSON entries and links now report it.
- Missing must-pass tests: the round trip (export, then import into an empty brain, with links and all bins) and no old names. Both added. The `sidecar` and `settings` rows for Markdown and CSV go with 23a.
- Spec drift in `specs/JSON-ENVELOPE.md`. The import row now lists `skipped`, `errors` and `settings`, and the plain line ends with `skipped N`. A `conflicts` row now carries `origin`. The `rejected` field order now includes `created_by`.
- The help text had a non-ASCII en dash. It now reads "v4 to v6" (`style:user-strings-ascii`).
- Nothing tested a `--from-json` source that cannot be read. Added.

#### Security audit (A): hostile JSON files and hostile `--from-db` v4, v5 and v6 brains

Each hole got a failing test before its fix. Every test below is red on `f72a8ce` and green after its fix.

| # | Attack | Effect before | Fix | Test |
|---|---|---|---|---|
| E1 | `\u0000` inside a body, id, `format` or `brain_id` | The string was cut short at the NUL, with no error | jsonread refuses the file: `import_parse` (G3) | `import_json_nul_in_a_body_is_import_parse`, `…_format_…`, `jsonread_rejects_a_nul_escape` |
| E2 | The same member twice in an entry or link | The first value won, with no error | `duplicate field` (P8) | `import_json_bad_record_duplicate_member`, `import_json_bad_link_duplicate_member` |
| E3 | A `settings` member inside an entry, or a root `"settings"` | Settings were read from the wrong object; an escaped root name gave a false `import_parse` | The root member's own bytes (`JsonValue.src_at`) | `import_json_reads_settings_from_the_root_member_only`, `…_escaped_settings_member_name` |
| E4 | `entries` as an object, `links` as a string, `brain_id` set to `"nope"` | Ignored: exit 0 with `inserted 0` | `import_parse` (G4) | `import_json_entries_object_is_import_parse`, `…links_string…`, `…bad_brain_id…` |
| E5 | A version counter of `0` | Accepted | `invalid version_vector` | `import_json_vector_counter_zero_is_invalid` |
| E6 | A keyless id'd body that another row already holds | `SQLITE_CONSTRAINT` aborted the whole import | `hash_clash` conflict | `import_json_keyless_body_held_by_another_id_is_hash_clash`, `…two_ids_with_one_keyless_body…` |
| E9 | A tombstone link in the file against a live local link | The live link was deleted. Import must never unlink (SY-M3). | Add wins: tombstones are inserted only when absent | `import_json_tombstone_link_does_not_unlink_a_live_link` |
| E10 | A v6 `remember.buckets.prefixes` row with a garbage value or actor | Stored raw, then `settings get` failed with an internal error | Goes through settings import (ST-8.4, ST-8.6) | `cli_import_v6_source_invalid_prefixes_value_is_skipped`, `…bad_actor_is_skipped` |
| E11 | 40,000 entries | O(n²) duplicate-id check: 19 s | Sorted check: 2.5 s | `import_json_bad_record_duplicate_id` (behavior) |
| E12 | A version vector with 30,000 devices | An internal error killed the import; the check was O(n²) (22 s) | A vector wider than the merge buffer is a bad record (settings A4 rule) | `import_json_vector_wider_than_the_merge_buffer_is_a_bad_record` |
| E13 | Key `Project:Alpha`, tag `"  pad  "` | Stored raw, so one brain could hold both `Project:Alpha` and `project:alpha` | Normalized the way `add` stores them (G6) | `import_json_normalizes_key_and_tags_like_add` |
| E14 | A v6 source with a supersedes cycle, or a link `kind` the schema forbids | Skipped with no error; a forbidden kind aborted the import | `invalid link`, with line = source rowid | `cli_import_v6_source_cycle_lists_an_invalid_link_by_rowid`, `…unknown_link_kind…` |
| E15 | A v6 `created_by` of `garbage` | Imported as `human` | `rejected` with field `created_by` (G2) | `cli_import_v6_source_bad_created_by_is_rejected` |

Checked and found safe, each with a probe run on 2026-11-12:

- Nesting past 64 is `import_parse`, and a depth of 100,000 does not overflow the stack.
- A key or actor id with ESC is refused: `invalid key or tag`, `invalid actor`.
- E7: an unknown member name holding ESC and a newline prints as `"x?[2J?line"` in plain output, and as escaped JSON under `--json`.
- `--origin` is capped at 64 bytes.
- The source brain is opened read-only, and the destination sidecar is untouched.

Left open:

- E8: `utc_stamp_is_canonical` accepts `2026-02-30`. It is a shared helper from before plan 23, so the fix is its own change.
- A duplicate member at the JSON root (for example two `settings`) still takes the first value. P8 names entries and links only.

#### Simplification (S)

| # | Change | Lines saved |
|---|---|---|
| S0 | Settings read from `JsonValue` source spans, replacing `extract_top_object` | ~75 |
| S0b | v6 prefixes built as one settings document in SQL `json_object` and sent through settings import, replacing the hand-written merge | ~95 |
| S0c | One `import_doc_view` for the three copy-pasted conflict view builders | ~40 |
| S0d | `--from-db` links go through `import_doc_one_link`, removing `insert_imported_link` and the remap copy | ~60 |
| S1 | Removed the dead `Store.import_origin` field and its 16 writes | 18 |
| S2 | Inlined `write_imported_prefix_row` (one caller) | 22 |
| S3 | Four timestamp blocks became one guard | 12 |
| S4 | `uuid_lowercase_in_place` replaces two malloc copies (out of memory was reported as a bad id) | 14 |
| S6 | `import_reply`: one shared reply tail for JSON and db imports | 15 |
| S7 | An `ImportSource` table and `import_take_source` replace four copied flag blocks | 30 |

All are applied. Net production change for the review: −259 lines, with the fixes included.

#### Mutation pass

I mutated the code plan 23 added and this review changed: one mutant per guard or boundary. Each mutant was rebuilt and run against `import_formats` and `sync_import`. A mutant that did not build was rewritten with a runtime-false guard and run again; none was counted as a survivor.

The first round (22 mutants, before the new tests) left 8 survivors: M4, M10, M14, M16 to M20 and M22. Each got a test. The final round (28 mutants) leaves 27 killed and 1 equivalent.

| Mutant | Change | Killed by |
|---|---|---|
| M1 | A counter of 0 accepted | `import_json_vector_counter_zero_is_invalid` |
| M2 | Vector width cap loosened | `…vector_wider_than_the_merge_buffer…` |
| M3 | Duplicate id not marked | `…bad_record_duplicate_id` |
| M4 | The later copy kept | `…duplicate_id_keeps_the_first_copy` (new) |
| M5 | id not validated | `…bad_record_id_not_a_uuid` |
| M6 | A long body accepted | `…bad_record_body_too_long` |
| M7 | `validate_one` actor check off | Equivalent: `actor_from_value` rejects the actor first. The check stays, because 23a's readers fill the same struct |
| M8 | `expires_at` not validated | `…bad_record_invalid_timestamp` (now covers all four fields) |
| M9 | Key and tags stored raw | `…normalizes_key_and_tags_like_add` |
| M9b | Repeated tags kept | the same test's re-import, which must be unchanged |
| M10 | A file tombstone revives a dead link | `…tombstone_link_over_a_tombstone_stays_dead` (new) |
| M11 | Link to itself | `…bad_link_to_itself` |
| M12 | `related` not canonical | `…canonicalizes_related_to_min_max` |
| M13 | No cycle check | `…skips_a_supersedes_cycle`, `cli_import_v6_source_cycle…` |
| M14 | No end bump on a new link | `…new_link_bumps_existing_ends` (new) |
| M15 | No end bump on a revived link | `…reviving_a_link_bumps_its_ends` (new) |
| M16 | The update path skips the hash clash | `…update_onto_a_held_body_is_hash_clash` (new) |
| M17 | An update keeps the file's `updated_by` | `…update_stamps_the_import_actor` (new) |
| M18, M19 | Concurrent treated as unchanged; conflict view actor | `…concurrent_change_is_a_conflict_with_the_import_actor` (new) |
| M20 | Origin limit off by one | `import_origin_of_64_bytes_passes_and_65_is_too_long` (new) |
| M21 | A repeated `--origin` allowed | `import_rejects_a_repeated_origin` (new) |
| M23 | NUL escape accepted | `…nul_in_a_body…`, `…nul_in_the_format…` |
| M24 | Non-array `links` accepted | `…links_string_is_import_parse` |
| M26 | Duplicate member missed | `…duplicate_member` (entry and link) |
| M27, M29 | `--from-db` insert or update keeps the source `updated_by` | `cli_import_v6_source_memories_are_updated_by_import`, `cli_import_incoming_dominates_updates` |
| M28 | A bad v6 actor accepted | `cli_import_v6_source_bad_created_by_is_rejected` |

M22 (`brain_id` not validated) is now covered by `import_json_bad_brain_id_is_import_parse`.

#### Grill (Itay, 2026-11-12)

| Q | Answer | Decision |
|---|---|---|
| G1 Markdown and CSV readers | B: split to plan 23a | `decision:plan23-review-g1-md-csv-to-23a` |
| G2 `--from-db` actors | A: fix both now, unify in 23a | `decision:plan23-review-g2-from-db-actors` |
| G3 NUL in strings | A: whole file `import_parse` | `decision:plan23-review-g3-nul-is-import-parse` |
| G4 Wrong root shapes | A: `import_parse` | `decision:plan23-review-g4-shape-is-import-parse` |
| G6 Key and tag case | A: normalize like `add` | `decision:plan23-review-g6-normalize-keys-tags` |

`decision:plan23-import-csv-formula-unescape` is amended: it is built in 23a. Three bullets of `standard:coding` are sharpened (PROOF test setup, TRUST hostile parsed members, DONE in-scope features left unbuilt). The old body is in `history:standard:coding:2026-11-12`.

#### Gate

Pending. Phase 2 runs the full local CI on the rebased tree. The Phase 1 gate was dropped after two machine crashes caused by load.

The first Phase 1 coverage run, at `36388c4`, failed one function: `cmd_import.c:import_report_io` (99.92%). `f7b8c53` adds its test. Coverage has not been rerun since.

#### Grades

**Coder: 5/10.**

What was good:

- jsonread is small, pure and strict, with the nesting cap.
- The merge is split along the plan's rules.
- Each stage had a gate and one mutant.
- The `errors[]` envelope and the C-35 line match the copy.

What lost points:

- A Critical scope miss. Markdown and CSV were deferred with no stage scheduled for them, and nothing said so in Status.
- Eleven exploitable holes, two of them data-destroying: E6 aborted the whole import, and E9 deleted a live link.
- `--from-db` broke G-33, the plan's own memory rule, and its links bypassed JE-5.1 and validation.
- P8 was not built.
- The spec envelope was not updated.
- An untested read-error path left function coverage below 100%.
- The tests were too weak to catch any of this. 8 of 22 first-round mutants survived, including the JE-5.1 end bump.

Pointers to paste:

- "Before you call a rule done, test it on every write path that can break it: here, file import and `--from-db`."
- "Never defer a scoped feature in a code comment. Put it in Status and grill it."
- "For every member a reader takes from hostile input, test the wrong type, null, a duplicate, and NUL."

**Stage reviews: 3/10.**

- All seven stage reviews and the whole-plan review ran in the coder's own session, and none got a second opinion.
- The whole-plan review said "memories still stamp `{import,null}`" while `--from-db` did not.
- No review ran an exploit pass on hostile files or brains.
- Each review ran only one mutant per stage, and each passed a link test that cannot see a missing end bump.
- No review flagged that Markdown and CSV had no stage.

Pointer to paste:

- "A review claim needs a test or a probe behind it. 'Safe' without a hostile input tried is not a finding."

#### Not done here

- Plan 23a: the Markdown and CSV readers, and `--from-db` memories through the file merge.
- E8 (Feb 30): a separate fix to the shared helper.
- Decisions `g2-16` and `r3-00b` stay `state:accepted` until 23a. The grill decisions G2, G3, G4 and G6 move to `state:implemented` when this plan merges.
