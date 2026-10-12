# Locked decisions for design log 006 (plan grill, 2026-10-12)

Itay answered these while plans 15–22 were written. They are law for the
implementer, like 006 itself. Each plan restates the rows it needs, so a
plan can be read alone. This file is the index.

| # | Topic | Decision | Plans |
|---|-------|----------|-------|
| D1 | FTS "does not parse" (E2.1a) | Add the message prefix `unterminated string` to `fts5:`, `no such column`, `unknown special query`. Observed on SQLite 3.53.3: `"` → `unterminated string`; `C++` → `fts5: syntax error near "+"`; `recs:books` → `no such column: recs`; `*` → `unknown special query: `; `((((…` → `fts5: parser stack overflow`. | 18 |
| D2 | Unicode data | Vendor **utf8proc** (MIT) through the vendoring checklist. One consumer TU `src/unicode.c` with a project port `unicode.h` (NFC, grapheme breaks, general category, whitespace). | 18, 19, 20 |
| D3 | JSON reader | Own strict RFC 8259 parser `src/jsonread.c/.h`: pure, no I/O, tracks line numbers, depth and size limits, unit-tested under ASan/UBSan in `remember_store_tests`. | 21 |
| D4 | `device new` `previous` | Open mints a missing sidecar as usual. `device new` reports that open-minted id as `previous`. `null` stays in the type only. | 15 |
| D5 | Old stderr notices | New warning codes `deprecated_flag` and `synced_folder`. With `--json-errors` they are JSON warning lines. Plain text is unchanged. | 16 |
| D6 | Memory errors | SQLite NOMEM and IOERR_NOMEM → `database`. Engine allocation failures (`STORE_ERR_OOM`, `NORM_ERR_OOM`, malloc in cmd/output) → `out_of_memory`. | 16 |
| D7 | `detail.path` | Absolute: join a relative path with the cwd, normalize `.`/`..` lexically, no symlink resolution. The same rule applies to every path source. | 16, 20 |
| D8 | `--no-create`; repair human | `--no-create` checks before any mkdir: no folder, no file, no sidecar. Human `repair search-index` prints `rebuilt N`. | 16 |
| D9 | Codes for 0.2.0 messages | Table in plan 16 § Locked decisions (approved as proposed). A message the table does not cover → stop and ask. | 16 |
| D10 | `--if-updated-at` | Canonical `YYYY-MM-DDTHH:MM:SS.mmmZ` only. Any other form → exit 1 `usage` `invalid timestamp: <ts>`. Byte compare. | 17 |
| D11 | since/until; search `--sort` | `since >= until` → empty result, exit 0. `search --sort` is validated (`invalid --sort (use updated or created)`) and then ignored. | 17 |
| D12 | Title / plain text / words | Title = the body's first line when it starts with `# `: the text after it, ASCII-trimmed; empty → no title. Plain text = the body with every run of whitespace or control characters collapsed to one space. Words = plain text split on spaces. No markdown stripping. | 18, 20 |
| D13 | Search code layout | Pure `src/search_terms.c/.h` (tokens, segment match, tiers, label). The adapter owns FTS5 quoting of P, the parse probe, the union query, ranking SQL and candidate key/tag fetch. | 18 |
| D14 | `config set` | No prefixes and no `--clear` → `usage` `config set bucket-prefixes requires at least one prefix (use --clear to remove all)`. Success prints exactly what `config get bucket-prefixes` prints after the write (human and JSON). | 19 |
| D15 | `config` errors | Unknown name → `usage` `unknown setting: <name>`. `config get` without a name → `usage` `config get requires a setting name`. `config` alone → `usage` `config requires get or set`. | 19 |
| D16 | Prefix input | NFC (D2), then ASCII lowercase, then validate and store. E12.3 default rows get the local sidecar device id. | 19 |
| D17 | Markdown `--force` | Swap with two renames: write `<PATH>.tmp-<rand>/`, rename old PATH to `<PATH>.old-<rand>/`, rename tmp onto PATH, delete the old folder recursively. | 20 |
| D18 | Deterministic export | JSON file = one compact line + final newline (same writer as `--json`). Tags sorted by code point; `version_vector` keys sorted; links sorted by (from sync_id, to sync_id, kind); prefix `items` sorted by prefix. The same orders apply in Markdown and CSV. | 20 |
| D19 | Export misc | Envelope `path` absolute (D7). Human: `exported N memories, M links to <path>`. `--bins`: comma list of `live,expired,deleted`, no duplicates, non-empty; else `usage` `invalid --bins (use live, expired, deleted)`. File write errors: ENOSPC → `disk_full`; EACCES/EROFS → `read_only`; ENOENT parent → `cannot_open` `reason` `missing`; else `io_error`. `detail.path` = the out path; `detail.errno` set. | 20 |
| D20 | Import `errors[]` file/line | JSON: file = basename, line = 1-based line of the entry's (or link's) `{`. CSV: basename, physical line where the record starts (header = 1). Markdown: file = path relative to the import folder, line = the offending front-matter line, or 1 for whole-file problems. `missing link target` uses the link's line. | 21 |
| D21 | `skipped` | `skipped` = bad records (E7.15) + skipped links. Every skipped item has one `errors[]` entry: `skipped == len(errors)`. | 21 |
| D22 | Record without `sync_id` | Like add: same body as an existing keyless memory → union tags into it (`updated` if the tags changed, else `unchanged`). Keyed record whose key exists → `key_clash` (no upsert). Otherwise insert with a new sync_id (`inserted`). | 21 |
| D23 | E7.2 compare | Content only: key, body, tags (set), `expires_at`, `deleted_at`. | 21 |
| D24 | Empty `REMEMBER_DB` | Counts as unset (0.2.0 behavior) → read `default-db`, then the built-in path. | 22 |
| D25 | CSV `links` cell | Same shape as Markdown: `[{to, kind, created_at, updated_at}]` for edges whose stored `from` is this row, sorted by (to sync_id, kind). | 20, 21 |
| D27 | Invalid UTF-8 in a search QUERY | Exit 1 `usage` `invalid UTF-8 in search query`. (Amends "never fails on its input" for non-text bytes.) | 18 |
| D28 | Tag facet order | Exact tag, then tier 1/2/3 (the E2.6 key tier rules applied to the tag), then `count DESC`, then tag. (Amends E2.6 to match Verification E2.) | 18 |
| D29 | Markdown folders | The export folder always has `README.txt`, `settings.json`, `memories/`, `expired/`, `deleted/`, with empty folders for excluded or empty bins. | 20 |
| D30 | `README.txt` | The sentence followed by one `\n`. | 20 |
| D31 | null in import records | JSON `null` or an empty CSV cell = absent, for key, expires_at, deleted_at, version_vector, created_at, updated_at, source, tags, links and sync_id. `sync_id: null` → E7.3; `version_vector: null` → E7.2. A null or empty body → `body is missing`. | 21 |
| D32 | Markdown front matter detection | Front matter exists only when line 1 is exactly `---` and a later line is exactly `---`. Otherwise E7.4: the whole file, trimmed, is the body. | 21 |
| D33 | Unknown / repeated fields | Unknown JSON members and unknown front-matter fields are ignored. A repeated known front-matter field, a front-matter line not shaped `<field>: <JSON>`, or a value that is not valid JSON → bad record, message `invalid front matter` (new E7.16 line). | 21 |
| D34 | Imported links (every source) | Canonicalize `related` to (min, max); `INSERT OR IGNORE`; keep the record's timestamps; no endpoint `updated_at`/VV bump; an existing triple is unchanged. A `supersedes` link that would create a cycle is skipped with error `invalid link`; this cycle check is added to `--from-db` too. | 21 |
| D26 | Scope | E11 (Android) is not planned here. E9 and E12.10 are in plan 22. | INDEX |

## Decisions the planner locked without a grill (low risk; Itay may veto)

| # | Decision | Plan |
|---|----------|------|
| P1 | `config set bucket-prefixes --clear P1` → `usage` `cannot combine --clear with prefixes`. | 19 |
| P2 | Label (E2.6): the `…` suffix is written only when the 60-grapheme cut actually truncates. | 18 |
| P3 | Test-only VFS shim CMake option is `REMEMBER_TEST_VFS` (default OFF). The Release tree, `remember_plain` and `remember_core` never set it. | 16 |
| P4 | The E8 stale check and the update run in the same write transaction. | 17 |
| P5 | Temp names beside PATH: `<PATH>.tmp-<16 hex random>`. They are removed on every failure path. | 20 |
| P7 | A repeated flag on a new or changed command (`--sort`, `--since`, `--until`, `--if-updated-at`, `--format`, `--out`, `--bins`, `--origin`, `--from-*`) → `usage` `duplicate <flag>`, as `duplicate --id` is today. | 17, 20, 21 |
| P8 | A JSON entry object with a duplicate member name is a bad record with message `duplicate field` (new E7.16 line). | 21 |
| P9 | Export flag messages: missing `--format` → `export requires --format (json, md, or csv)`; bad value → `invalid --format (use json, md, or csv)`; missing `--out` → `export requires --out PATH`. Import: zero or several `--from-*` → `import requires exactly one of --from-json, --from-md, --from-csv, --from-db`. | 20, 21 |
| P6 | Every new command (`device`, `repair`, `config`, `export`) gets a registry row, a help topic and a facade byte-match test. | all |
