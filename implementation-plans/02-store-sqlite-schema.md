# 02 — Store port + SQLite schema (sole SQLite unit)

## Goal

Introduce the **persistence port** and the **only** SQLite adapter. Create schema, pragmas, `user_version`, path mkdir, sync-volume warning hook.

## Deliverables

| File | Role |
|------|------|
| `src/store.h` | **Port:** opaque `typedef struct Store Store;`, domain types (`Entry`, `TagList`, `StoreError`), API below |
| `src/store_sqlite.c` | **Only file** that includes `sqlite3.h` / amalgamation |
| `third_party/sqlite/` | Amalgamation drop-in (pin version in README) |
| `src/store_types.h` | Optional: entry structs with no SQLite types |

### Port API (minimal; grow in later steps)

```c
Store *store_open(const char *path, char *err, size_t errlen); /* creates parent dir */
void   store_close(Store *s);

/* schema user_version handled inside open */
/* later steps add: store_add, store_get, store_list, store_search, store_update, store_delete */
```

Start with `store_open` / `store_close` only if commands aren't ready; prefer adding write/read as soon as step 04 needs them — **still only in this .c file**.

### Schema (inside adapter)

- Tables: `entries` (with nullable `key`), `tags`, `entry_tags`, `entries_fts` (standalone FTS5).
- Partial unique indexes: key WHERE NOT NULL; body_hash WHERE key IS NULL.
- `PRAGMA foreign_keys=ON`, `busy_timeout=5000`, default journal (no WAL).
- `user_version`: 0→create→1; 1→ok; >1→error string for CLI exit 1.
- FTS: `rowid = entries.id` set explicitly; **no triggers** (sync functions live here, called by store mutators).

### Path policy

- Resolve default elsewhere (`util` / `cli`); adapter receives final path.
- Sync warning: implement in `util` or `cli` (string markers) so adapter stays pure open — **or** in open with a callback. Prefer **util** `path_looks_synced()` called from commands/main so store stays “open this path”.

## Pragmatic notes

- **Replaceable:** commands never see `sqlite3 *`.
- **DRY:** one `fts_resync(store, entry_id)` used by add/update/delete.
- **Orthogonal:** SQL schema changes only touch `store_sqlite.c` (+ tests).

## Tests required this step

Open/create alone cannot green most CLI tests. Implement scaffolding; **must-pass moves to later steps** once commands exist:

| Test | Pass by step |
|------|----------------|
| `schema_user_version_too_new_refused` | 04+ (needs open path) / green with any command |
| `db_parent_dir_mode_0700` | 04 (add creates nested parent 0700) |
| `sync_path_warning_on_clouddocs_marker` | 04/09 |
| Orphan-tag GC (inspect via sqlite3 CLI) | 05 (delete) |

**Must pass by end of 02:** amalgamation links; `store_open`/`store_close` compile; optional smoke.

## Done checklist

- [ ] Amalgamation builds with relaxed warnings on that TU only
- [ ] `store_open` creates schema at user_version 1
- [ ] Grep: no `sqlite3` outside `store_sqlite.c` / third_party
- [ ] Format + compile clean
