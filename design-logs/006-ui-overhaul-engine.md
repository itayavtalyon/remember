# 006 - Engine changes for the Fortkeeps UI overhaul (0.3.0)

**Status:** Design (closed 2026-10-11, amended 2026-10-12)
**Author:** Itay (+ agent facilitation)
**Created:** 2026-10-11

Cross-refs: [001](001-foundations.md), [002](002-core-facade.md), [003](003-ttl.md),
[004](004-related-memories.md), [005](005-sync-foundations.md). App logs in `remember-mac`:
010 (shared rules, full grill transcript), 011 (macOS), 012 (iOS/iPadOS), 013 (Android).
Source: the designer handoff `02-engine-cli.md` of 2026-10-11e and its follow-up §H–§J.

## Background

Baseline: commit `512d3ed`, version 0.2.0, schema `user_version` 4. Verified facts:

- The key is not in `entries_fts`. `search editor` does not find `pref:editor`.
- `search` runs the user text as raw FTS5. `search boo` does not find "Book". `search 'recs:books'` fails with `database error`. `search 'C++'` fails with `invalid search query`.
- `list` orders by `updated_at DESC, id DESC`. There is no created-at sort and no date filter.
- Every SQLite failure becomes `remember: database error`. The SQLite code and message are dropped.
- Errors go to stderr as plain text, also with `--json`.
- The engine sets `PRAGMA busy_timeout = 5000` and uses the default rollback journal, not WAL.
- A missing folder or file is created (mkdir -p, a new empty brain).
- There is no place for brain settings.
- `conflicts` stores no origin. `conflict accept --keep both` fails on `hash_clash`.
- With the sidecar missing and a `devices` row present, the engine rewrites the sidecar from the row (005 "Lost sidecar").
- `import --from-db` does not copy the foreign sidecar and does not register devices.
- Log 005 lists "JSON export" as a non-goal.

## Problem

The apps need: search as you type over keys, bodies and tags; errors a user can fix; a
stale-write guard; export and import in open formats; brain-level bucket prefixes; conflict
origins; a way to mint a new device id after a restore; Android build presets. Every change
must keep the stable contracts and be useful to CLI users and agents too.

## Questions and Answers

The full transcript is in `remember-mac` log 010 § Questions and Answers. Decisions that
shape this log:

- Export and import are required (supersedes 005's non-goal; PRINCIPLES value 1).
- Q4 → one `search`, no `--prefix` flag; the union of prefix words and raw FTS; never
  fails on its input; keys and tags returned; scoped facets (Rounds 4–5).
- Errors: a raw part that does not parse is a warning on exit 0; a real error exits 1 with
  nothing on stdout; seven new codes split by SQLite cause; plain text keeps the
  `database error` prefix and adds cause and fix; JSON adds `detail` (Rounds 5–6).
- Engine additions accepted: `--no-create`, `--busy-timeout`, `repair search-index`,
  `detail.reason`, `detail.fts_only`, a test-only failing VFS shim (Round 7).
- Bucket prefixes live in the brain as an add-wins set with tombstones, cap 8 (Q7, Q20).
- E5, E9 and E10 ship in 0.3.0. E11 keeps only the Android presets (the apps compile the C
  sources for iOS through SwiftPM).
- E2.6 JSON order: `keys` and `tags` go before `entries` (Itay, Round 7), an exception to
  EN-01/EN-02 named in What's New.
- Device id: E14 `device` / `device new`; the 005 recovery rule stays (Round 5, replaces
  the Round 4 rule change).
- Q6: a hand-edited import record with a `sync_id` and no `version_vector`, whose fields differ, records a `concurrent_vv` conflict (E7.2). An equal version vector stays unchanged (E7.1).
- Q12: import never registers foreign devices; this log makes it a tested rule.
- v5 migrates silently on open; Homebrew 0.3.0 ships before any app that links v5.
- Deep review 2026-10-11 (Itay): a blank search writes no warning (E2.1); one import is one transaction (E7.9, E7.10); Markdown and CSV settings are the E12.9 object and import reads bucket prefixes only (E7.11); an existing export path is `usage` (E6.6); `--keep incoming` and `--keep both` on `hash_clash` stay failures.
- Plan grill 2026-10-12 (Itay, while writing implementation plans 15–22). These amend the Design below; each plan restates them as Locked decisions:
  - E2.1a: an `SQLITE_ERROR` whose message starts `unterminated string` also means "does not parse". SQLite 3.53.3 reports `unterminated string` for `"` and `"abc`.
  - E4.6: two more warning codes, `deprecated_flag` (the `--trash` / `purge-trash` line) and `synced_folder` (the cloud-folder line). With `--json-errors` they are JSON warning lines. Plain text is unchanged.
  - E4.7: SQLite NOMEM and IOERR_NOMEM map to `database` (the explicit row wins over the IOERR row). Engine allocation failures map to `out_of_memory`.
  - E7.2: "every field equals" compares content only: key, body, tags as a set, `expires_at`, `deleted_at`.
  - E14.6: open mints a missing sidecar as usual (E14.3), so `device new` reports the id that open minted as `previous`. `null` stays in the type only.
  - E15.1: an empty `REMEMBER_DB` counts as unset, as in 0.2.0. The engine then reads `default-db`.
  - E6.6 (Markdown `--force`): write `<PATH>.tmp-<rand>/`, rename the old PATH to `<PATH>.old-<rand>/`, rename tmp onto PATH, then delete the old folder.
  - E2.1/E2.2: a QUERY that is not valid UTF-8 exits 1 `usage` `invalid UTF-8 in search query`.
  - E2.6: tag items order is exact tag, then the key tiers 1/2/3 applied to the tag, then `count DESC`, then tag.
  - E6.b: the Markdown folder always has all three bin folders. `README.txt` ends with one newline.
  - E7: JSON `null` or an empty CSV cell means absent. Front matter needs an opening and a closing `---` line. Unknown fields are ignored. A repeated field or a malformed front-matter line is a bad record `invalid front matter`. Imported links use `INSERT OR IGNORE` with no endpoint bump, and a `supersedes` cycle is skipped as `invalid link` (also for `--from-db`).
  - E7.6 for `--from-db`: `errors[]` `file` is the source basename and `line` is the source `entry_links` rowid. A `sync_id` read in either case is lowercased. Invalid UTF-8 in `default-db` is ignored like a missing line.
  - E11 (Android presets) is not part of the CLI plans.
- Grill 2026-10-12 (Itay): key and tag suggestions use every word against a part, and the same part may serve two words. Whole-start stays. Key rank is whole-start, then in-order parts, then the rest, by code point (E2.5, E2.6). This round supersedes the Round 5 sentence that keeps the 005 sidecar recovery. The sidecar file is the only device id. Schema v5 drops `devices` (E1.3, E14). `--since` and `--until` accept seconds-only `Z` (E5.2). Markdown files and `README.txt` have a fixed shape (E6.b). Import classifies bad records and rolls the named whole-file failures back (E7.12, E7.13). The brain path reads `~/.remember/default-db` after `REMEMBER_DB` (E15).

## Design

### 0. Rules

- EN-01. The stable contracts do not change: the command name `remember`, `--key`, `--tag`, `sync_id`, `quota_exceeded`, the JSON field order, the exit codes 0/1/2/3, and the Cloud tool names `remember_add`, `remember_search`, `remember_list`, `remember_get`, `remember_update`, `remember_delete`.
- EN-02. New behavior is additive: new flags, new commands, new optional JSON fields at the end of objects. E4.2 and E2.6 are the stated exceptions.
- EN-03. Apps use only `remember_run(argc, argv, out, err)` with `--db` and `--json`.
- EN-04. Every change ships with unit tests in `tests/` and a line in `README.md › What's New`.
- X-E01. The release is 0.3.0. Homebrew publishes it before any app release that links schema v5.

### 1. Schema v5 (one migration)

- E1.3. The engine migrates v4 → v5 on open, in one `BEGIN IMMEDIATE` transaction:
  1. Drop `entries_fts` and create `entries_fts(key, body, tags, tokenize='unicode61 remove_diacritics 2')`. The column order is key, body, tags.
  2. Repopulate `entries_fts` from every row of every bin (E1.2, E10.1).
  3. Create `bucket_prefixes(prefix TEXT PRIMARY KEY, added_at TEXT NOT NULL, removed_at TEXT, device_id TEXT NOT NULL)`.
  4. Create `settings(name TEXT PRIMARY KEY, value TEXT NOT NULL, updated_at TEXT NOT NULL)`.
  5. `ALTER TABLE conflicts ADD COLUMN origin TEXT`.
  6. `DROP TABLE devices`. The engine does not copy any row into the sidecar.
  7. Set `user_version = 5`.
- E1.2. `fts_resync` writes the key (empty string when NULL), the body, and the tag names joined with one space, as in 0.2.0.
- E1.4. `import --from-db` accepts sources at v4 and v5. The engine opens the source read-only and does not migrate it. A missing `origin` column, a missing `bucket_prefixes` table, or a missing `settings` table means those values are absent.
- E1.5. remember 0.2.x refuses a v5 brain with `database is newer than this remember`.

### 2. Search (E2, E3)

- E2.1. `remember search QUERY` is the only search. The filters `--tag`, `--source`, `--key`, `--limit`, `--offset`, `--expired`, `--deleted`, `--since`, `--until` apply. `--facet-limit N` (default 50, max 200) caps the `keys` and `tags` items. A value outside 1–200 exits 1 with code `usage` and message `invalid --facet-limit (must be 1..200)`.
- E2.1. The memory query is a union:
  1. P = the prefix part built by E2.2.
  2. R = QUERY trimmed of ASCII whitespace.
  3. When R is not empty and parses alone (E2.1a), the engine runs one FTS query `(<P>) OR (<R>)`. When P is empty, it runs R alone.
  4. When R is empty, the engine runs P alone and writes no warning. When P is also empty, the result is empty: exit 0, `count` 0, `total` 0, empty `keys` and `tags`.
  5. When R is not empty and does not parse, the engine runs P alone and writes the `raw_query_ignored` warning (E2.3). When P is empty, the result is empty and the warning is still written.
- E2.1a. The "parses alone" test prepares `SELECT 1 FROM entries_fts WHERE entries_fts MATCH ?1 LIMIT 1`, binds R and steps once on the same connection and transaction. `SQLITE_ROW` or `SQLITE_DONE` means it parses. `SQLITE_ERROR` with a message starting `fts5:`, `no such column` or `unknown special query` means it does not parse; the engine keeps the message. Any other result is a real error (E3.2).
- E2.1b. Each row appears once. `count`, `total`, `--offset` and `--limit` apply to the de-duplicated set.
- E2.2. The engine builds P:
  1. Normalize QUERY to Unicode NFC. Split it into tokens on Unicode whitespace and on every ASCII character outside `[A-Za-z0-9]`. Non-ASCII characters stay inside tokens.
  2. Drop empty tokens. Keep the first 16 tokens and the first 64 code points of each.
  3. Remove the Hebrew marks of E10.1 from each token.
  4. For each token: double every `"`, wrap it in `"…"`, append `*`.
  5. Join the tokens with one space (FTS5 implicit AND).
  6. When no token remains, P is empty.
- E2.3. A raw part that does not parse is not an error. The engine exits 0, prints the prefix results and writes one warning: plain `remember: note: raw query ignored: <fts message>`; with `--json-errors` `{"version":1,"warning":{"code":"raw_query_ignored","message":"<fts message>"}}`.
- E2.4. Memories are ordered by, with q = QUERY trimmed and ASCII-lowercased:
  1. `e.key = q` first.
  2. Then `substr(e.key, 1, length(q)) = q`.
  3. Then `bm25(entries_fts, 10.0, 1.0, 4.0) ASC` (key, body, tags). A smaller score comes first, as in 0.2.0.
  4. Then `e.updated_at DESC, e.id DESC`.
- E2.5. The engine ASCII-lowercases the P tokens. Call those tokens t1…tn, in that order. A key or a tag splits into segments on `:`, `-`, `_`, `.`, `/`, and whitespace. The engine keeps the segment order.
- E2.5a. A token matches a segment when the segment starts with the token. The same segment may match more than one token.
- E2.5b. A key or a tag text-matches when every token matches one of its segments. A key or a tag also text-matches when q is a prefix of the whole key or tag. q is QUERY trimmed and ASCII-lowercased, as in E2.4.
- E2.5c. Word order does not decide whether a key or a tag matches. Word order decides the key rank in E2.6.
- E2.5d. The engine returns a key or a tag only when it text-matches and belongs to at least one memory inside the active bin and filters. A key belongs to its memory. A tag belongs to every memory that carries it.
- E2.6. The `--json` output adds `keys` and `tags` between `total` and `entries`. This is a stated exception to EN-01 and EN-02 (Itay, Round 7); What's New names it:
  `{"version":1,"offset":0,"limit":20,"count":2,"total":2,"keys":{"total":1,"items":[{"key":"recs:books","sync_id":"…","label":"Book recommendations","source":"agent","updated_at":"…Z"}]},"tags":{"total":1,"items":[{"tag":"type:books","count":1}]},"entries":[…]}`.
  - `keys.items` rank uses three tiers. Tier 1 holds a key when q is a prefix of the whole key. Tier 2 holds a key when the tokens match segments in order and the key is not in tier 1. Token t1 matches a segment at index i1. Each later token matches a segment at an index greater than or equal to the index used by the previous token. Tier 3 holds every other text-match.
  - Inside one tier, the engine orders keys by the stored key. The order is ascending Unicode code point order.
  - `tags.items` order: exact tag, then `count DESC`, then tag.
  - A tag's `count` is the number of memories in the active bin and filters that carry it.
  - `total` is the number of scoped matches. `items` holds at most `--facet-limit`.
  - `label` is the one-line label: the `# ` title, else the key, else the first 60 graphemes of the plain text + "…".
  - `--offset` and `--limit` page `entries` only.
- E2.7. Human output prints `keys: k1, k2, …` and `tags: t1 (n), …` lines before the entry lines, only when not empty, at most 10 each, then `… and N more`. The key line uses the E2.6 key order. The tag line uses the E2.6 tag order.
- E2.8. Release note: "`remember search` now also matches word prefixes and returns matching keys and tags. FTS5 queries work as before; a query that FTS5 cannot parse no longer fails: it returns prefix matches and prints a note on stderr."
- E5.3. `search` ignores `--sort`. `--since` and `--until` filter on `updated_at`.
- E3.1. Input that FTS5 cannot parse is never an error (E2.3).
- E3.2. A real FTS or SQLite error is never swallowed: exit 1, nothing on stdout, the E4.7 code and E4.8 text on stderr. Usage errors stay exit 1 with code `usage`.
- E3.3. `search` no longer produces `invalid search query`. The code `invalid_query` stays reserved.

### 3. Errors (E4)

- E4.1. The global flag `--json-errors` makes an error write exactly one stderr line `{"version":1,"error":{"code":"<code>","message":"<text>"}}` and no plain line.
- E4.2. Without the flag, stderr stays as in 0.2.0, except real database errors (E4.8).
- E4.3. The codes are: `usage`, `not_found`, `database`, `out_of_memory`, `internal`, `invalid_query`, `key_conflict`, `body_conflict`, `expired`, `not_expired`, `deleted`, `not_deleted`, `not_single_device`, `self_link`, `cycle`, `source_too_old`, `unique_taken`, `stale`, `newer_database`, `unsupported_database`, `import_parse`, `busy`, `read_only`, `disk_full`, `cannot_open`, `not_a_database`, `corrupt`, `io_error`.
- E4.4. `message` is the plain line without `remember: `.
- E4.6. A command that exits 0 may write one warning line per notice. Plain: `remember: note: <text>`. With `--json-errors`: `{"version":1,"warning":{"code":"<code>","message":"<text>"}}`. Warnings never appear with a non-zero exit. The only code in 0.3.0 is `raw_query_ignored`.
- E4.7. The engine keeps `sqlite3_extended_errcode()`, `sqlite3_errmsg()` and `sqlite3_system_errno()` at the point of failure. It maps them to a code for every command and for opening the brain; the first matching row wins:

| SQLite result | Code |
|---|---|
| BUSY 5, BUSY_RECOVERY 261, BUSY_SNAPSHOT 517, BUSY_TIMEOUT 773, LOCKED 6, LOCKED_SHAREDCACHE 262, LOCKED_VTAB 518, PROTOCOL 15 | `busy` |
| READONLY_DBMOVED 1032 | `cannot_open` |
| READONLY 8, READONLY_RECOVERY 264, READONLY_CANTLOCK 520, READONLY_ROLLBACK 776, READONLY_CANTINIT 1288, READONLY_DIRECTORY 1544 | `read_only` |
| FULL 13 | `disk_full` |
| CANTOPEN 14 and its extended codes (NOTEMPDIR 270, ISDIR 526, FULLPATH 782, CONVPATH 1038, DIRTYWAL 1294, SYMLINK 1550), PERM 3 | `cannot_open` |
| NOTADB 26 | `not_a_database` |
| CORRUPT 11, CORRUPT_VTAB 267, CORRUPT_SEQUENCE 523, CORRUPT_INDEX 779, IOERR_SHORT_READ 522 | `corrupt` |
| IOERR 10 and every other IOERR_* (READ 266, WRITE 778, FSYNC 1034, TRUNCATE 1546, LOCK 3850, CORRUPTFS 8458, …), NOLFS 22 | `io_error` |
| SCHEMA 17 | the engine retries the statement once, then `database` |
| everything else (NOMEM, IOERR_NOMEM, MISUSE, RANGE, MISMATCH, FORMAT, INTERNAL, AUTH, INTERRUPT, TOOBIG, unmapped CONSTRAINT, ERROR) | `database` |

- E4.7. `cannot_open` carries `detail.reason`: `permission` for EACCES, `missing` for ENOENT, `is_directory` for ISDIR, else `other`. A readable but unwritable file becomes `read_only` at the first write. The engine does not probe writability at open.
- E4.15. A SCHEMA retry prepares the same SQL, binds the same values, and runs that statement once more. A second failure returns `database`.
- E4.8. The plain line is `remember: database error: <cause> (<SQLITE_NAME>). <fix>` with the extended name:

| Code | `message` | Cause | Fix |
|---|---|---|---|
| `busy` | brain is busy | the brain is locked by another process | Wait a moment and try again. Another app or a terminal may be using this brain. |
| `read_only` | brain is read-only | the brain is read-only | Give yourself write permission on the brain file and its folder, or move the brain to a writable folder. |
| `disk_full` | disk is full | disk is full | Free some space and try again. |
| `cannot_open` | cannot open brain | cannot open the brain file | Check that the folder exists and that you can read and write it. |
| `not_a_database` | not a remember brain | the file is not a remember database | Point --db at a remember brain, or move this file away. |
| `corrupt` | brain is damaged | the brain file is damaged | Copy the file somewhere safe before anything else, then run `remember export`, or restore a backup. |
| `corrupt` with `fts_only` | search index is damaged | the search index is damaged | Run `remember repair search-index`. Your memories are not affected. |
| `io_error` | disk error | the disk reported a read or write error | Check the disk or drive, then try again. |
| `database` | database error | — | plain: `remember: database error (<SQLITE_NAME>). Try again. If it keeps happening, report the details from --json-errors.` |

- E4.9. The JSON error has an optional `detail` object: `sqlite_code`, `sqlite_extended`, `sqlite_name` (always for SQLite errors); `sqlite_message`; `errno` (only when non-zero); `path` (only for `read_only`, `disk_full`, `cannot_open`, `not_a_database`, `corrupt`, `io_error`; always the absolute brain path given to `--db`); `reason` (`cannot_open` only); `fts_only` (`corrupt` only).
- E4.13. The global flag `--busy-timeout MS` (0–60000, default 5000) sets `PRAGMA busy_timeout`. A value outside that range exits 1 with code `usage` and message `invalid --busy-timeout (must be 0..60000)`. The engine has no own retry loop.
- E4.10. The engine never repairs on its own. On `corrupt`, it runs `PRAGMA quick_check` on `entries`, `entry_links`, `conflicts`, `settings` and `bucket_prefixes`. The failing statement touched `entries_fts` when its SQL text contains `entries_fts`. When the check passes and the statement touched `entries_fts`, or the code is CORRUPT_VTAB, `fts_only` is true.
- E4.14. `remember repair search-index` runs `quick_check` on the base tables (it refuses with `corrupt`, `fts_only` false, when they fail), then `INSERT INTO entries_fts(entries_fts) VALUES('rebuild')` in one transaction, then `VALUES('integrity-check')`. Output: `{"version":1,"action":"rebuilt","entries":N}`. N is the number of rows in `entries`, in every bin.
- E4.11. The global flag `--no-create` makes a missing file fail with `cannot_open`, `reason` `missing`, and creates nothing.
- E4.12. Exit codes do not change. On an error, stdout is empty.
- X-E02. A test-only VFS shim wraps `unix` and fails on demand: `REMEMBER_TEST_FAIL_WRITE=N` (IOERR_WRITE after N writes), `REMEMBER_TEST_FAIL_READ=N` (IOERR_READ), `REMEMBER_TEST_ENOSPC=1` (FULL), `REMEMBER_TEST_NOMEM=1` (IOERR_NOMEM). Only test builds compile it. Release binaries and the apps never contain it.

### 4. List (E5)

- E5.1. `list --sort updated|created` (default `updated`) orders by `<field> DESC, id DESC`.
- E5.2. `list --since TS --until TS` filter on the sort field. `since` is inclusive. `until` is exclusive. Search `--since` and `--until` use the same TS rule and filter on `updated_at` (E5.3).
- E5.2a. The canonical timestamp is `YYYY-MM-DDTHH:MM:SS.mmmZ`. The engine accepts that form.
- E5.2b. The engine accepts `YYYY-MM-DD`. The engine reads it as the local start of that day. The engine converts it to the canonical form.
- E5.2c. The engine accepts `YYYY-MM-DDTHH:MM:SSZ`. The engine reads it as the same instant with `.000`.
- E5.2d. Any other TS exits 1 with code `usage` and message `invalid timestamp: <ts>`.

### 5. Export (E6)

- E6. `remember export --format json|md|csv --out PATH [--bins live,expired,deleted]`. `--bins` defaults to all three.
- E6.1. Export never needs an account, a network or a quota check.
- E6.2. Each memory exports `sync_id, key, body, tags, source, created_at, updated_at, expires_at, deleted_at, version_vector` and its links. The local `id` is not exported. `bin` is written for readers and ignored on import.
- E6.3. Links export as `{from, to, kind, created_at, updated_at}` with `sync_id` ends, for every `entry_links` row whose two ends are exported.
- E6.4. The sidecar is not exported. The `conflicts` table is not exported. Brain settings are exported.
- E6.5. Output: `{"version":1,"action":"exported","format":"<f>","path":"…","count":N,"links":M}`.
- E6.6. The engine writes to a temporary file beside PATH and renames it onto PATH. When PATH exists and `--force` is absent, the engine writes nothing, exits 1 with code `usage`, and sets the message to `output already exists: <path>`. `--force` replaces PATH by that rename.
- E6.a. JSON (UTF-8, no BOM): `{"format":"remember-export","version":1,"exported_at":"…Z","engine":"0.3.0","schema":5,"entries":[…],"links":[…],"settings":{…}}`. Entry members are in the E6.2 order with `bin` before `version_vector`. Entries sort by `created_at`, then `sync_id`. `settings` is last; an untouched brain writes `{}`.
- E6.b. Markdown is a folder: `README.txt`, `settings.json`, `memories/`, `expired/`, `deleted/`. `settings.json` is the E12.9 settings object.
- E6.b1. `README.txt` is exactly this sentence: `This folder is a Fortkeeps export. Import it with: remember import --from-md <folder>`.
- E6.b2. A file name is `<slug>--<last 12 hex of sync_id>.md`. The slug is the key, else the `# ` heading text, else the first 6 words. The engine lowercases ASCII in the slug. The engine replaces every run outside `[a-z0-9]` with `-`. The engine trims `-`. The slug max is 48 characters. An empty slug becomes `memory`.
- E6.b3. A memory file starts with a line `---`. The next lines are one field per line, in this order: `remember_export`, `sync_id`, `key`, `tags`, `source`, `created_at`, `updated_at`, `expires_at`, `deleted_at`, `version_vector`, `links`. Each line is `<field>: <JSON>`. A line `---` closes the front matter. The body bytes follow. The file ends with one newline.
- E6.b4. `remember_export` is the JSON number `1`. A null field is JSON `null`. `links` is a JSON array of `{to, kind, created_at, updated_at}`. A link object has no `from`. `links` lists the edges whose `from` is this memory.
- E6.c. CSV: RFC 4180, UTF-8 with BOM, CRLF, every field quoted, `"` doubled. Header: `sync_id,key,body,tags,source,created_at,updated_at,expires_at,deleted_at,bin,version_vector,links`. `tags`, `version_vector` and `links` cells hold JSON. NULL is an empty cell. Settings are one extra last row with `sync_id` `#settings` and the E12.9 settings object in `body`. That row is not a memory.

### 6. Import (E7)

- E7. `remember import --from-json FILE | --from-md DIR | --from-csv FILE | --from-db PATH [--origin LABEL]`.
- E7.1. Merge matches by `sync_id` and compares version vectors: incoming dominates → take it; local dominates → keep; equal → unchanged; concurrent → `concurrent_vv` conflict. Key and body-hash clashes record `key_clash` and `hash_clash`. Exit 0 with conflicts.
- E7.2. A record with `sync_id` and no `version_vector` is unchanged when every field equals the local row; otherwise it records `concurrent_vv`. It never overwrites.
- E7.3. A record without `sync_id` is added with a new `sync_id` under the normal add rules, except that a key clash records `key_clash` (no keyed upsert). Missing timestamps are now. A missing source is `unknown`.
- E7.4. A `.md` file without front matter imports as one memory with the whole file, trimmed, as its body.
- E7.5. Links apply after all entries. A link to a missing `sync_id` increments `skipped`. The `errors` array gains one object whose message is `missing link target: <sync_id>`.
- E7.6. Bad records do not stop the import. Output: `{"version":1,"action":"imported","inserted","updated","unchanged","conflicts","skipped","errors":[{"file","line","message"}],"prefixes_merged":bool}`.
- E7.7. The engine reads no zip.
- E7.8. Every import source merges bucket prefixes by E12.6.
- E7.9. The engine applies one import in one transaction. A JSON value that does not parse exits 1 with code `import_parse`. A CSV file that does not parse exits 1 with code `import_parse`. A `--from-md` path that is not a directory exits 1 with code `import_parse`. The brain does not change. `--from-db` keeps the database codes in E4.
- E7.10. A parsed file with bad records commits the good records and exits 0. The `errors` array lists each bad record.
- E7.11. Markdown `settings.json` is the E12.9 settings object. The CSV `#settings` body is that same object. Import reads `bucket_prefixes` and ignores other keys. The `#settings` row is not a memory.
- E7.12. A missing `settings.json` is an empty settings object. A present `settings.json` that does not parse exits 1 with code `import_parse`. The brain does not change.
- E7.13. These whole-file failures also exit 1 with code `import_parse` and leave the brain unchanged. The JSON root `format` is not `remember-export`. The JSON root `version` is not `1`. The CSV header is not the E6.c header.
- E7.14. `--from-md` selects files as follows. When `memories/`, `expired/`, or `deleted/` exists, the engine imports only `*.md` files directly inside those three folders. Otherwise the engine imports only `*.md` files directly in the directory. The engine does not recurse. The engine ignores `README.txt` and every other file.
- E7.15. A record is bad in these cases. The body is missing. The body is empty after trim. The body is longer than 65536 bytes. `sync_id` is present and is not a canonical UUID (`8-4-4-4-12` hex, either case). A present timestamp is not `YYYY-MM-DDTHH:MM:SS.mmmZ`. `source` is present and is not `human`, `agent`, `tool`, `share`, or `unknown`. A present key or tag fails the key and tag checks of log 001. `version_vector` is present and is not a JSON object of device id to a JSON integer greater than 0. A device id in that object is not a canonical UUID. A link object lacks `to` or `kind`. A link `kind` is not `related`, `cites`, or `supersedes`. A `sync_id` repeats in the file. The second copy is the bad record. A CSV row's column count differs from the header. A JSON entry is not an object.
- E7.16. The `errors` message for a bad record is the matching line: `body is missing`, `body is empty`, `body is too long`, `sync_id is not a UUID`, `invalid timestamp`, `invalid source`, `invalid key or tag`, `invalid version_vector`, `invalid link`, `duplicate sync_id`, `column count does not match the header`, `entry is not an object`.
- X-E03 (Q12). No import reads or writes a sidecar. No import changes the destination device id. A test covers each import format.
- E13.2. `import` writes `conflicts.origin` = the basename of the `--from-*` path (for `--from-md`, the folder name). `--origin LABEL` (max 64 bytes) overrides it. A longer label exits 1 with code `usage` and message `origin is too long (max 64 bytes)`.
- E13.3. `conflicts --json` adds `"origin"` (string or null) after `"created_at"`.

### 7. Stale-write guard (E8)

- E8.1. `update … --if-updated-at TS`: when the row's `updated_at` is not exactly TS, the engine writes nothing and exits 1 with code `stale` and message `memory changed since it was read`.

### 8. Skills (E9)

- E9.1. `remember-install-skill` also installs `~/.codex/skills/remember/SKILL.md` when `~/.codex` exists.
- X-E04. Spike E9-S: the first E9 step checks the Codex documentation for the user skill folder. When it is not `~/.codex/skills/`, the step writes the real path into this log's Implementation Results and uses it.
- E12.10. `skills/remember/SKILL.md` gains the line "Tags that start with a bucket prefix (`remember config get bucket-prefixes`) group memories into buckets, for example `project:fortkeeps`."

### 9. Hebrew folding (E10)

- E10.1. FTS rows and prefix tokens drop U+0591–U+05BD, U+05BF, U+05C1–U+05C2, U+05C4–U+05C5 and U+05C7. `entries` keeps the original body. The raw part R is not folded.

### 10. Android build (E11)

- E11.1. CMake presets build a shared library (`.so`) with the NDK for arm64-v8a, armeabi-v7a and x86_64.
- E11.3. Mobile ships only `remember_run`, no CLI binary.
- X-E05. CI builds the presets and runs a smoke test that calls `remember_run version` on an emulator. A POSIX gap found there is fixed in this step.
- X-E06. There is no iOS preset. The apps compile the C sources for iOS through SwiftPM.

### 11. Bucket prefixes (E12)

- E12.2. A prefix is live when `removed_at IS NULL OR added_at >= removed_at`. A row that is not live is a tombstone. Tombstones are kept.
- E12.3. With an empty table, `config get` reports `project:`, `status:` with `"default":true` and writes nothing. The first write stores those defaults with `added_at = "1970-01-01T00:00:00.000Z"`, then applies E12.5, in that same transaction.
- E12.4. A prefix is 1–32 code points of `\p{L}`, `\p{Nd}`, `-`, `_`, `.`, then one `:`. ASCII folds to lowercase. Errors exit 1 with code `usage`: `invalid bucket prefix: <p>`, `duplicate bucket prefix: <p>`, `too many bucket prefixes (max 8)`.
- E12.5. `config set bucket-prefixes P1 … Pn` (n ≤ 8; `--clear` = an empty list), in one transaction with now = the current time:
  1. Each Pi that is not live gets `added_at = now` and `device_id = local`.
  2. Each visible live prefix not in the list gets `removed_at = now` and `device_id = local`. Overflow prefixes are not touched.
  3. `settings.bucket_prefix_order` becomes `[P1 … Pn]` with `updated_at = now`.
- E12.6. Merge (import, later sync): for each prefix in the union, `added_at = max`, `removed_at = max` (NULL is lowest), `device_id` from the side of the later timestamp (ties: the larger device id). The order with the later `updated_at` wins (ties: local). No conflict rows. `prefixes_merged` is true when any row or the order changed.
- E12.7. The effective list is computed on every read: start from `bucket_prefix_order` without non-live entries; append live prefixes missing from it by `added_at`, then prefix; when more than 8 are live, keep the 8 earliest-added in that order and report the rest as `overflow`. Removing a visible prefix lets the earliest-added overflow prefix fill the slot at the end.
- E12.8. `config get bucket-prefixes --json`: `{"version":1,"name":"bucket_prefixes","value":[…],"overflow":[…],"updated_at":"…Z"|null,"default":bool}`. Human output: one prefix per line, then `(not shown: x:, y:)` when overflow is not empty.
- E12.9. Export writes `settings.bucket_prefixes = {"order":{"value":[…],"updated_at":"…Z"},"items":[{"prefix","added_at","removed_at","device_id"}]}` with tombstones.

### 12. Device id (E14)

- E14.1. The writer id is the one lowercase UUID line in `<db-path>.device_id`. The file mode is `0600`. The database holds no second copy.
- E14.2. Open uses a valid sidecar as the writer id. Open does not read a device id from the database.
- E14.3. A missing sidecar mints a new UUIDv7. Open writes that id to the sidecar. This includes a v4 brain whose sidecar file is gone after the v5 migration.
- E14.4. An invalid sidecar file is an error. The message is `invalid device_id sidecar`. Open does not mint over an invalid file.
- E14.5. `remember device` prints the current id. `--json` returns `{"version":1,"device_id":"…"}`.
- E14.6. `remember device new` mints a new UUIDv7 and writes the sidecar. Version vectors do not change. The plain output is the new id. `--json` returns `{"version":1,"device_id":"…","previous":"…"}`. `previous` is the old sidecar id. `previous` is JSON `null` when the file was missing.
- E14.7. Hard delete and purge run after open has a valid sidecar. `not_single_device` stays in the E4.3 catalog. No v5 command returns it.
- E14.8. `bucket_prefixes.device_id` and `version_vector` still store device-id strings. They do not reference a `devices` table.

### 13. Brain path (E15)

- E15.1. The engine chooses the brain path in this order. It uses the first item that applies.
  1. `--db PATH`.
  2. The environment variable `REMEMBER_DB`, when the variable is present. An empty value still counts as present.
  3. The absolute path in `~/.remember/default-db`.
  4. `~/.remember/remember.db`.
- E15.2. `~` is the home directory the engine already uses for the built-in brain path.
- E15.3. `default-db` is a UTF-8 file with one line. The app writes a trailing newline. The engine trims ASCII whitespace. The engine accepts a missing trailing newline.
- E15.4. The engine ignores `default-db` when the file is missing, the line is empty, or the path is not absolute. The engine then uses the built-in path. The engine does not create `default-db`.

### 14. Not changing

- The deprecated aliases `--trash` and `purge-trash` stay.
- No `status` field or command.
- `tags --json` stays as is.
- `--source` stays single-valued.
- Conflict verbs stay `local|incoming|both`. `--keep incoming` and `--keep both` on `hash_clash` stay failures with code `unique_taken`. Log 010 does not offer Use incoming for that kind.
- Soft `delete` keeps clearing `expires_at`.

## Examples

```
remember add --key recs:books --tag recs --tag type:books "# Book recommendations"
remember add --tag family "Book the dentist"
remember add --key pref:editor "Use Zed"
remember --json search boo            # entries 2, first key recs:books; keys [recs:books]; tags [type:books (1)]
remember --json search "recs:bo"      # entries 1; keys [recs:books]; tags []; warning (no such column: recs)
remember --json search 'book OR zed'  # raw parses; entries 3; no warning
remember --json search boo --tag family   # entries ["Book the dentist"]; keys []; tags []
remember --json --json-errors search 'foo AND'   # exit 0; stderr warning raw_query_ignored
remember --db r.db add b              # (r.db chmod 444) exit 1: remember: database error: the brain is read-only (SQLITE_READONLY). Give yourself write permission on the brain file and its folder, or move the brain to a writable folder.
```

## Trade-offs

| Choice | Alternative | Why |
|---|---|---|
| Union search, no flag | `--prefix` flag; prefix only | One command for humans and agents; nothing breaks (Rounds 4–5). |
| Warning on exit 0 | Silent | Do not swallow (Round 5). |
| Seven error codes | One `database` code | Users can fix what they can name (Round 6). |
| Add-wins prefix set | Last write wins | No device loses a prefix silently (Q20). |
| Sidecar file only | A second copy in `devices` | A backup must not carry the writer id (Itay, 2026-10-12). |

## Verification Criteria

- E1: `search editor` finds `pref:editor`; F5 (v4) migrates keeping every entry, tag, link and `sync_id`; body and tag search results are equal before and after.
- E2: every acceptance line of the Examples passes; the inputs `C++`, `"`, `recs:books`, `key:recs`, `foo AND`, `a AND`, `NEAR(`, `*`, `-x`, `(`, `ספר`, emoji and 10,000 characters exit 0; a 60-tag match gives `tags.total` 60 and 50 items (200 with `--facet-limit 200`). Against key `recs:books`, `book` matches, `boo book` matches, `recs games` does not match, and `recs:bo` is tier 1. Against tag `project:fortkeeps-cloud`, `fort cloud` is tier 2 and `cloud fortkeeps` is tier 3. Tier 1 sorts before tier 2. Inside a tier, keys sort by code point.
- E3/E4: a damaged index exits 1 with empty stdout, code `corrupt`, `fts_only` true; `repair search-index` then succeeds; each E4.7 acceptance case (chmod 444 file, chmod 555 folder, chmod 000 file, missing path with `--no-create`, folder as path, text file, truncated file, held lock with `--busy-timeout 500`, tmpfs or ENOSPC shim, IOERR shims, NOMEM shim) gives its code, `detail` and plain line; `get --key nope` gives `not_found` exit 2.
- E5: `list --sort created` orders by `created_at` after the oldest is updated; `list --since <today>` returns today's only.
- E6/E7: the round trip of JSON, Markdown and CSV through an empty brain gives equal exports (except `exported_at`) for the fixture with keyed and keyless memories, tags with spaces and `:`, all 5 sources, expired and deleted rows, all 3 link kinds, CRLF, tabs, emoji, Hebrew, a `---` line, a body starting with `---`, quotes, commas, a 64 KiB body, null and non-null `expires_at`, and changed bucket prefixes with tombstones; a second import reports only `unchanged` and `prefixes_merged` false. An existing export path without `--force` exits 1 with `usage` and writes nothing. A JSON file that does not parse exits 1 with `import_parse` and leaves the brain unchanged. A CSV `#settings` row does not become a memory. A whitespace-only search exits 0 with an empty result and no warning.
- X-E03: after each import format, the destination sidecar is unchanged.
- E8: an old `--if-updated-at` exits 1 with `stale`.
- E10: `ספר` prefix-matches a body with niqqud.
- E12: every E12 acceptance scenario (defaults, set, validation, round trip, add wins, re-add, removal after add, order, cap and overflow, idempotent import) passes.
- E13: `conflicts --json` shows `origin`.
- E14: `device` → A; `device new` → B with `previous` A; a new edit counts under B. A brain with no sidecar opens as a new id. A v4 `devices` row is not copied. `not_single_device` is never returned.
- E15: a set `REMEMBER_DB` hides `default-db`. An absolute `default-db` line is used. A missing or relative line uses `~/.remember/remember.db`.
- E11: the Android `.so` builds for three ABIs and the emulator smoke test passes.

## Known Limitations

- Two copies that both keep the sidecar file share a device id. `device new` gives the copy its own id. Mac backup exclusion is remember-mac log 011.
- Undo in the apps cannot restore `updated_at`.

## Future Work

- Remove `--trash` and `purge-trash` in a major version.
- Sync registers multiple devices (Cloud log).

## Implementation Results

Shipped in plan 15 (branch `plan15-stage1-schema-v5`, 2026-10-13–14). The Design section above is unchanged.

- E1.2. `fts_resync` writes the key (empty string when NULL), the body, and the tag names joined with one space. `store_rekey` resyncs that key before commit.
- E1.3. v4 migrates to v5 on open in one `BEGIN IMMEDIATE`. **Amendment (Itay 2026-10-13, `decision:plan15-stage1-review-grill`):** step 5 rebuilds `conflicts` (rename the old table away, `CREATE` the canonical DDL, copy the rows, drop the old table) instead of `ALTER TABLE ... ADD COLUMN`, so `sqlite_master` text matches a fresh brain. Fresh and migrated schemas are the same set of statements. Order may differ.
- E1.4. `import --from-db` accepts a v4 or v5 source, opened read-only and not migrated. A missing `origin` column, or a missing `bucket_prefixes` or `settings` table, means those values are absent.
- E1.5. This build refuses `user_version` above 5 with `database is newer than this remember`. remember 0.2.x already refuses a v5 brain with that text.
- E10.1 (rows). FTS rows drop U+0591–U+05BD, U+05BF, U+05C1–U+05C2, U+05C4–U+05C5 and U+05C7. `entries` keeps the original body. Query-side folding is plan 18.
- E14.1–E14.3, E14.5, E14.7, E14.8. The sidecar `<db-path>.device_id` (mode 0600, one lowercase UUID) is the only device id. Open uses a valid sidecar and does not read a device id from the database. A missing sidecar is a new UUIDv7, including a v4 brain whose sidecar is gone; the `devices` row is not copied. `remember device` prints the id. Hard delete and purge run once open has a valid sidecar. `not_single_device` stays in the catalog and no v5 command returns it. `bucket_prefixes.device_id` and `version_vector` store device-id strings and do not reference a `devices` table.
- E14.4 / E14.6. **Amendment (Itay 2026-10-13, `decision:plan15-device-new-replaces-invalid`):** `device new` always mints and replaces an invalid sidecar. Every other open, including `device` and `device new extra`, still exits 1 with `invalid device_id sidecar` and leaves the file unchanged. Version vectors do not change. Plain output is the new id. `--json` is `{"version":1,"device_id":"…","previous":"…"}`. `previous` is JSON `null` only in the type: open mints a missing or invalid file first, so the command reports that id.
