# Brain list

Status: how `remember` records every brain it opens read-write, tells a copy from a move, and lists or prunes that record. The file is `<data dir>/brains.json`. `remember brain list` and `remember brain prune` are its commands.

Decisions are `decision:…` entries tagged `subject:library`, `subject:data`, `subject:cli`, `subject:planning` or `subject:plugins-and-hooks`. Sentences are quoted from the approved copy: `decision:2026-11-12-copy-batch-e3b-warning-splits` entries 4 and 27 (E3B-1, approved 2026-11-12).

Items marked **OPEN** have no decision, no approved copy and no plan text. The code does what this spec says for them, and a later answer replaces it. They are listed in [Open](#open). The brains.json error, warning, usage and help lines are approved copy (`decision:open-2026-11-15-brains-json-unreadable-copy`, approved 2026-11-22); this spec quotes them.

## The file

`decision:g-01`, `decision:2026-10-22-m-04`, `decision:2026-11-11-brains-json-data-dir`, `decision:2026-11-11-brains-json-format`.

- The path is `<data dir>/brains.json`. The data dir is `$XDG_DATA_HOME/remember` when `XDG_DATA_HOME` is set and not empty, and `$HOME/.remember` when it is not. `REMEMBER_USER_DB` moves `user.db` only; it does not move `brains.json`. `default-db` and `remember.db` stay in `~/.remember` ([DEFAULT-DB.md](DEFAULT-DB.md)).
- The mode is `0600`. The folder is made with mode `0700` when it is missing.
- The document is one JSON object:

```json
{"format":"remember-brains","version":1,"brains":[{"path":"/absolute/path","brain_id":"01234567-89ab-7def-8123-456789abcdef","added_at":"2026-11-11T12:00:00.000Z"}]}
```

- `path` is the `realpath` of the brain file. `brain_id` is `brain_meta.brain_id` (`decision:brain-id`). `added_at` is `YYYY-MM-DDTHH:MM:SS.mmmZ`, the time of the first open.
- Remember is the only writer. The document ends with one newline.

## A read-write open

`decision:g2-08`, `decision:g2-05`, `decision:2026-11-11-brains-json-format`.

Every open that can write the brain takes one of these paths. A read-only file, and a brain from a newer remember ([OLDER-CLI.md](OLDER-CLI.md)), are not recorded, and their id does not change.

| The list holds | Remember does | Warning |
|---|---|---|
| no row with this `brain_id` and no row with this path | adds a row | none |
| a row with this path and this `brain_id` | nothing, and does not write the file | none |
| a row with this path and another `brain_id` (after `remember brain new-id`) | stores the new `brain_id` in that row | none |
| the same `brain_id` at another path that still has this brain | a **copy**: mints a new `brain_id`, stores it in the brain and in the row of this path (adds the row when there is none) | `brain_id_cloned` |
| the same `brain_id` at another path that has no such brain | a **move**: changes that row's path to this path, keeps the `brain_id` and `added_at`, and drops any other row of this path | none |

"Still has this brain" has three answers (`decision:2026-11-16-plan26-review-grill` G2):

- **yes**: a regular file whose `brain_meta.brain_id` is that id.
- **no**: proven not. `stat` proves nothing is there (`ENOENT`, `ENOTDIR`, `ELOOP`, `ENAMETOOLONG`), or the path is not a regular file (a FIFO, a device or a folder is never opened), or the file is not a database, has no `brain_meta`, or holds another id.
- **cannot tell**: any other `stat` failure (`EACCES`, `EIO`), an open that fails, or a read that is still busy after the open's busy timeout (`--busy-timeout`, default 5000 ms). The probe waits that long, as the open itself would. Then the command fails visibly with exit 1 and changes nothing: it writes nothing to `brains.json`, mints nothing and does not change the brain's id. The next open checks again. The error code is the existing code of the cause, classed as for any SQLite failure: `busy` (still locked), `cannot_open` (`EACCES`, or any other `stat` or open failure), `io_error` (`EIO`), `corrupt` or `not_a_database` (the id cannot be read). The message is `can't tell whether <path> is a copy of this brain or the same brain moved.` and the fix is `Run the command again once <path> can be read.`, where `<path>` is the other listed path.

The new id of a copy is minted only when the open is a copy. An open that is not a copy mints nothing.

If storing a copy's new id in the brain fails (busy, disk full, an I/O error), the open fails with that store error and exit 1, also for a command that only reads (`decision:2026-11-16-plan26-review-grill` G4 A). Going on would leave two brains with one id. `brains.json` already holds the new id for this path, so the next open finds the brain's old id there and takes it for a copy again.

The write goes to `brains.json.tmp` in the same folder, then `rename` replaces `brains.json`, while the process holds a POSIX record lock on `brains.json.lock`. The wait for the lock is bounded by the busy timeout (`--busy-timeout`, default 5000 ms): it tries every 10 ms. Past it, the open (or `brain prune`) fails with `busy` and exit 1, and reads and writes nothing (`decision:2026-11-16-plan26-review-grill` G5, `preference:no-unbounded-waits-fail-visibly`). The message is `brains.json is busy: another remember command is using it.` and the fix is `Try again in a moment.` `brain list` takes no lock. The lock file is opened without following a symlink and must be a regular file. A stale `brains.json.tmp` is unlinked first (the lock is held, so no other writer uses it), and the temp file is created with `O_EXCL`. A path that is not valid UTF-8 is not recorded, so one such brain cannot make the file unreadable for every brain.

A lock that cannot be made (not the timeout), or a failure to write, sync or rename, leaves the previous `brains.json` in place and the temp file removed. The open's exit code does not change, and nothing new is printed (no sentence is approved for it).

### The copy warning

`brain_id_cloned` (C-38, E3B-1 entry 27). `message`:

`brain "<path>" looks like a copy of another brain, so it got its own new id, <id>. The original keeps the old id. Both now sync and run hooks as separate brains.`

`fix`: `Run 'remember brain list' to see both.` `<path>` is the real path of the copy and `<id>` is its new id. The plain line is `remember: warning: <message> <fix>`. `detail` is not set. The warning is printed once, on the open that changed the id.

## `remember brain list`

`decision:2026-11-11-brain-command-names`, `decision:2026-10-22-m-19`, `decision:g2-08`.

- It reads `brains.json` and does not open a brain. It does not create a brain, does not record one, and is not refused on a newer brain. A missing file is an empty list.
- Plain output is one line per row, in file order: `path`, `brain_id` and `added_at`, tab-separated, the layout `remember tags` uses (`decision:2026-11-15-plan26-coder-grill` 3). Each field comes from the file, so in each one C0 controls, TAB included, DEL and C1 controls are shown as `?`, and a row always has three columns (`decision:2026-11-16-plan26-review-grill` G1 A). `--json` keeps the exact values.
- `--json` type is `brain.list.result`. `data` is `{count, brains[{path, brain_id, added_at}]}` (`decision:2026-11-15-plan26-coder-grill` 4, `specs/JSON-STYLE.md` §1.12).
- A row whose path has no brain stays in the file and gets the `brain_missing` warning (C-01, E3B-1 entry 4). `message`: `brain "<path>" is in the list but isn't on disk.` `fix`: `If you moved it, open it once from its new place. Otherwise run 'remember brain prune' to remove it from the list.` The plain line is `remember: warning: <message> <fix>`. Exit code, and `--strict`, follow the other warnings.
- "Has no brain" is what the approved C-01 line says, "isn't on disk": `stat` proves nothing is there (`ENOENT`, `ENOTDIR`, `ELOOP`, `ENAMETOOLONG`), or the path is not a regular file. Any other `stat` failure (`EACCES`, `EIO`) says nothing about the brain: the row stays, with no warning, and prune keeps it. The path is never opened.

## `remember brain prune`

`decision:g2-08`, `decision:2026-11-11-brain-command-names`.

- It removes every row whose path has no brain (as above), under the lock, with the same temp-and-rename write. It does not open a brain. It writes nothing when no row is missing, and it does not create `brains.json`.
- Plain output is empty: no extra sentence.
- `--json` type is `brain.prune.result`. `data` is `{action:"pruned", count, brains[]}`, the rows it dropped (`decision:2026-11-15-plan26-coder-grill` 4).

`brain list` and `brain prune` take no argument. `remember brain new-id` stores the new id as before, then records it in this path's row with the same lock and busy timeout as an open. A lock held past that timeout exits 1 (`busy`) with the new id already stored, and the next open writes it to the row. A file that cannot be read or written is left as it is, as on an open, and the command exits 0.

`remember import --from-json` into an empty brain takes the file's `brain_id` (plan 23), unless this list has a brain with that id that still has it. Then the brain keeps its own id, because the file came from that brain and taking its id would make this one a copy. If that brain cannot be checked (busy, EACCES, EIO), the import exits 1 with the same codes as an open and writes nothing. The message is `can't check whether <path> still holds this file's brain.` and the fix is `Try again once <path> is free and can be read.`, where `<path>` is the listed path (`decision:open-2026-11-17-import-source-brain-unchecked-copy`). A list that cannot be read lists nothing. An import that took the file's id records it in this path's row the way `brain new-id` does, with the same busy rule.

## brains.json is another program's input

`standard:c`, `standard:coding` §4. The reader treats the file as hostile.

- It opens the file with `O_NONBLOCK` and takes only a regular file (a symlink to one is followed). A FIFO, a device or a folder is unreadable and cannot hang the open.
- It reads at most `BRAIN_LIST_FILE_MAX`, 1 MiB (1048576 bytes; `decision:2026-11-15-plan26-coder-grill` 1). A longer file is unreadable. There is no cap on the number of rows.
- A NUL byte in the file, or `\u0000` in a string, makes the file unreadable: C would see only the prefix of the path.
- The document must have `format` `remember-brains`, `version` `1`, and a `brains` array of objects whose `path`, `brain_id` and `added_at` are strings. Anything else is unreadable.
- Each of those six members appears once. A member given twice makes the file unreadable: a first-wins reader and a last-wins reader would see two different files (the plan 25 rule for `plugin.json`). A member the reader does not take is warned with `unknown_field` and the C-35 file line, `remember: warning: "<path of brains.json>" has a field remember doesn't know, "<name>". The rest of the file was read. Remove that field, or update remember if the file came from a newer version.`, with the fix in `fix` and `detail {field, where}`. The warning names the first such member (root first, then the rows in file order), once per command, on every read: `brain list`, `brain prune` and each read-write open (`decision:2026-11-16-plan26-review-grill` G6 B, `specs/JSON-STYLE.md` §1.13). A rewrite (an add, a move, a copy or a prune) drops such a member (G7 C); the warning still fires on each read.
- A file that is unreadable is **never written over**: record and prune return at once and leave it for the user. A missing file is not unreadable.
- Every field it prints goes through the control-byte mask (plain) or JSON escaping.

An unreadable `brains.json` stops recording and copy detection until the user fixes it (`decision:2026-11-12-brains-json-unreadable-warns`). The decision says each read-write open prints one new warning with a `message` and a `fix`, keeps its exit code and leaves the file alone. The warning is `brains_json_unreadable` (`decision:open-2026-11-15-brains-json-unreadable-copy`). `message`: `can't read brains.json (<path>), so remember isn't recording this brain or checking it for copies.` `fix`: `Fix the file or delete it; remember starts a new one on the next command.` `<path>` is the path of `brains.json`. A command that opens its brain twice (`brain new-id`, an import that takes the file's id) prints it once, at the open. A deleted `brains.json` is a missing file: the next read-write open makes a new one.

`brain list`, `brain prune` and `brain sweep --all` on an unreadable file fail with `cannot_open`, exit 1. `message`: `can't read brains.json (<path>).` `fix`: `Check that the file can be read, then run the command again.` `brain prune` that cannot write fails with `io_error`: `can't update brains.json (<path>).` and `Check that its folder can be written to, then run the command again.` A data folder that will not resolve fails with `cannot_open`: `can't find the data folder: <reason>.` (`<reason>` is the resolver's own, such as `HOME is not set`) and `Fix that, then run the command again.` A bare `remember brain` is a usage error: `'remember brain' needs a command: list, prune, sweep, or new-id.` and `Run 'remember brain --help' to see what each does.` The help lines are `List the brains remember knows about` (list) and `Forget brains whose file is gone` (prune); prune drops only rows whose file is not on disk.

## Open

These need an answer. None is settled text.

1. The one-line summary of `brain` in the command list is the coder's wording (`decision:open-2026-11-20-help-text-new-verbs`, not approved). The other brains.json lines are approved (`decision:open-2026-11-15-brains-json-unreadable-copy`).
