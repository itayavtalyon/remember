# 23a — Import Markdown and CSV, and one `--from-db` merge

## Goal

Build the two file readers plan 23 left out, and route `--from-db` memories through the file merge.

```
remember import --from-md PATH | --from-csv PATH [--origin LABEL]
```

## Status

Split from plan 23 by Itay on 2026-11-12 (`decision:plan23-review-g1-md-csv-to-23a`). Until this plan lands, `--from-md` and `--from-csv` exit 1 `usage` with `--from-md is not implemented yet` (or `--from-csv is not implemented yet`), the same as at plan 23's merge.

When this plan's review passes, set `decision:g2-16`, `decision:r3-00b`, `decision:plan23-import-csv-formula-unescape` and `decision:plan23-review-g1-md-csv-to-23a` to `state:implemented`.

## Source of truth

[Plan 23](23-import-formats.md) holds the rules. This plan only builds them. Read these parts of plan 23 first:

- Context: settings beside each format (`settings.json` in the Markdown folder, `<CSV basename>.settings.json` beside the CSV), the `errors[]` file and line rules for CSV and Markdown, the bad-record messages `column count does not match the header`, `invalid front matter`, `duplicate field`, D31 (an empty cell means absent), D32 (front matter), P8, P10 (BOM and CRLF).
- Whole-file failures: a CSV header that is not the JE-8.3 header is `import_parse`.
- Must-pass tests: `sidecar` and `settings` for Markdown and CSV, and plan 23 verification 3 (a CSV with `sync_id` in the header is `import_parse`).

Also in force:

- `decision:plan23-import-csv-formula-unescape`: strip exactly one leading `'` from a CSV cell before any other parsing of it.
- `decision:plan23-review-g3-nul-is-import-parse`, `decision:plan23-review-g4-shape-is-import-parse` and `decision:plan23-review-g6-normalize-keys-tags` apply to every format: a NUL in any JSON value is `import_parse`, and keys and tags are stored normalized.
- `decision:plan23-review-g2-from-db-actors`: move `--from-db` memories onto the `ImportMemory` merge (`import_doc_*` in `src/store_sqlite.c`). Then one merge function serves every format. Keep the `rejected` list and its field order for `--from-db`.

## Stages

1. CSV reader into `ImportDoc`: header check, BOM, CRLF and LF, quoted cells, the formula unescape, the column count message, and the settings file beside the CSV.
2. Markdown folder reader into `ImportDoc`: front matter (D32), `invalid front matter`, `duplicate field`, links with `from` equal to the file's id, `settings.json`.
3. `--from-db` memories through the file merge. Delete `import_one_entry` and its helpers once nothing calls them.

Each stage ends with an exploit pass (hostile files) and a mutation pass, the same as plan 23's whole-plan review.
