# 26 — Brain list

**Module:** `src/brain_list.c` and `src/brain_list.h`. The commands call this module. Command parse stays in the existing CLI parser.
**Requires:** plan 19 merged (`brain_id` in the brain). Plans 25 and 27 do not require this plan.

## Goal

Remember records every brain it opens read-write, tells a copy from a move, and lists or prunes that record.

## Source

- `decision:g-01` and `decision:2026-10-22-m-04` (the data directory).
- `decision:g2-08` (what the file lists, when a row is added, temp file plus rename, advisory lock, prune).
- `decision:g2-05` (copy mints a new `brain_id` and warns `brain_id_cloned`; otherwise the path is updated).
- `decision:brain-id` (`brain_id` is a UUID v7 minted at create).
- `decision:2026-10-22-m-19` (the command tokens `remember brain list` and `remember brain prune`).
- The JSON document below is not named by a decision. This plan locks it so the coder does not invent a document.
- Sentences: [`COPY-PENDING.md`](COPY-PENDING.md) C-38 and the C-01 `brain_missing` line. Itay approved that file on 2026-10-25.
- Spec: `specs/brain-list.md` (new).

`decision:g2-08` names the jobs as `remember brains` and `remember brains prune`. This plan uses the tokens in `decision:2026-10-22-m-19`. The coder does not implement the older tokens.

## Out of scope

`remember brain new-id`. `decision:brain-id` says the command forks a copy into a new brain. It does not name the output path, which file keeps which id, or the output line. The coder does not invent that command. The daemon, sweep, and plugins are other plans.

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md).

Tests live in `tests/test_brain_list.c`, suite `brain_list`, and that suite is added to `tests/gate-suites`. SQL stays in `src/store_sqlite.c`. This module reads and writes the JSON file. It does not open SQLite.

## What must be true

- The file is `<data dir>/brains.json`. The data dir is `$XDG_DATA_HOME/remember` when `XDG_DATA_HOME` is set, and `~/.remember` when it is not.
- The file mode is `0600`, the same mode as `user.db` in that directory.
- The document is one JSON object:

```json
{"format":"remember-brains","version":1,"brains":[{"path":"/absolute/path","brain_id":"01234567-89ab-7def-8123-456789abcdef","added_at":"2026-11-11T12:00:00.000Z"}]}
```

- `path` is the canonical absolute path (`realpath`). `brain_id` is the brain's id. `added_at` is `YYYY-MM-DDTHH:MM:SS.mmmZ`.
- A read-write open adds a row on the first open of that `brain_id`. Remember writes `<data dir>/brains.json.tmp`, then renames it over `brains.json`, while it holds an advisory lock on `<data dir>/brains.json.lock`.
- A failure to write the file leaves the previous file in place. The open's exit code does not change. This plan prints no new sentence for that failure.
- The same `brain_id` at another path, when that path still has that brain, is a copy. Remember mints a new `brain_id`, adds this path, and warns `brain_id_cloned` with the C-38 line: `remember: warning: brain "<path>" looks like a copy of another brain, so it got its own new id, <id>. The original keeps the old id. Both now sync and run hooks as separate brains.`
- The same `brain_id` at another path, when that path has no such brain, is a move. Remember changes the stored path and keeps the id. It prints no warning.
- No row has this `brain_id`: Remember adds a row.
- `remember brain list` prints each row. A row whose path has no brain stays in the file and prints the C-01 line: `remember: warning: brain "<path>" is in the list but isn't on disk. If you moved it, open it once from its new place. Otherwise run 'remember brain prune' to remove it from the list.`
- `remember brain prune` removes each row whose path has no brain. It prints no extra sentence.

## Review points

- The reviewer checks the command tokens against `decision:2026-10-22-m-19`.
- The reviewer checks that a copy mints a new id and a move keeps the id.
- The reviewer checks that a failed write does not change the open's exit and does not print a new sentence.
- The reviewer checks that `remember brain new-id` is not implemented.

## Definition of Done

- [ ] `the first read-write open adds one row and a second open of the same brain does not add another` (`tests/test_brain_list.c`).
- [ ] `the write is a temp file renamed over brains.json under the lock`.
- [ ] `a copy of a brain file mints a new brain_id and prints the C-38 line`.
- [ ] `a moved brain file keeps the id and updates the path, with no warning`.
- [ ] `remember brain list prints a missing path with the C-01 brain_missing line and keeps the row`.
- [ ] `remember brain prune drops a missing path and leaves a path that still has the brain`.
- [ ] `a failed replace leaves the previous file and does not change the open's exit`.
- [ ] `specs/brain-list.md` is written, and the decisions this plan built are `state:implemented`.
- [ ] The gate passes on the committed tree, and the notes record the mutants and their red tests.

## Implementation Notes

## Review Notes
