# Brain list

Status: how `remember` records every brain it opens read-write, tells a copy from a move, and lists or prunes that record. The file is `<data dir>/brains.json`. `remember brain list` and `remember brain prune` are its commands.

Decisions are `decision:…` entries tagged `subject:library`, `subject:data`, `subject:cli`, `subject:planning` or `subject:plugins-and-hooks`. Sentences are quoted from the approved copy: `decision:2026-11-12-copy-batch-e3b-warning-splits` entries 4 and 27 (E3B-1, approved 2026-11-12).

Items marked **OPEN** have no decision, no approved copy and no plan text. The code does what this spec says for them, and a later answer replaces it. They are listed in [Open](#open).

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

"Still has this brain" is true only for a regular file whose `brain_meta.brain_id` is that id. A FIFO, a device, a folder, a file that is not a brain, and a missing file are all "no such brain", and the open never opens a path that is not a regular file. The new id of a copy is minted only when the open is a copy. An open that is not a copy mints nothing.

The write goes to `brains.json.tmp` in the same folder, then `rename` replaces `brains.json`, while the process holds a POSIX record lock on `brains.json.lock`. The lock file is opened without following a symlink and must be a regular file. A stale `brains.json.tmp` is unlinked first (the lock is held, so no other writer uses it), and the temp file is created with `O_EXCL`. A path that is not valid UTF-8 is not recorded, so one such brain cannot make the file unreadable for every brain.

A failure to lock, write, sync or rename leaves the previous `brains.json` in place and the temp file removed. The open's exit code does not change, and nothing new is printed (no sentence is approved for it).

### The copy warning

`brain_id_cloned` (C-38, E3B-1 entry 27). `message`:

`brain "<path>" looks like a copy of another brain, so it got its own new id, <id>. The original keeps the old id. Both now sync and run hooks as separate brains.`

`fix`: `Run 'remember brain list' to see both.` `<path>` is the real path of the copy and `<id>` is its new id. The plain line is `remember: warning: <message> <fix>`. `detail` is not set. The warning is printed once, on the open that changed the id.

## `remember brain list`

`decision:2026-11-11-brain-command-names`, `decision:2026-10-22-m-19`, `decision:g2-08`.

- It reads `brains.json` and does not open a brain. It does not create a brain, does not record one, and is not refused on a newer brain. A missing file is an empty list.
- Plain output is one line per row, in file order: the `path`, with C0 controls, DEL and C1 controls shown as `?` (**OPEN**: the columns, see [Open](#open)).
- `--json` type is `brain.list.result`. `data` is `{count, brains[{path, brain_id, added_at}]}` (**OPEN**: the shape; the list field is a plural noun and `data.count` is present, as `specs/JSON-STYLE.md` §1.12 requires).
- A row whose path has no brain stays in the file and gets the `brain_missing` warning (C-01, E3B-1 entry 4). `message`: `brain "<path>" is in the list but isn't on disk.` `fix`: `If you moved it, open it once from its new place. Otherwise run 'remember brain prune' to remove it from the list.` The plain line is `remember: warning: <message> <fix>`. Exit code, and `--strict`, follow the other warnings.
- "Has no brain" is: `stat` fails, or the path is not a regular file (**OPEN**: the reading of the decision's "path has no brain", see [Open](#open)). The path is never opened.

## `remember brain prune`

`decision:g2-08`, `decision:2026-11-11-brain-command-names`.

- It removes every row whose path has no brain (as above), under the lock, with the same temp-and-rename write. It does not open a brain. It writes nothing when no row is missing, and it does not create `brains.json`.
- Plain output is empty: no extra sentence.
- `--json` type is `brain.prune.result`. `data` is `{action:"pruned", count, brains[]}`, the rows it dropped (**OPEN**: the shape, see [Open](#open)).

`brain list` and `brain prune` take no argument. `remember brain new-id` is unchanged.

## brains.json is another program's input

`standard:c`, `standard:coding` §4. The reader treats the file as hostile.

- It opens the file with `O_NONBLOCK` and takes only a regular file (a symlink to one is followed). A FIFO, a device or a folder is unreadable and cannot hang the open.
- It reads at most `BRAIN_LIST_FILE_MAX` bytes (**OPEN**: the value, see [Open](#open)). A longer file is unreadable. There is no cap on the number of rows.
- A NUL byte in the file, or `\u0000` in a string, makes the file unreadable: C would see only the prefix of the path.
- The document must have `format` `remember-brains`, `version` `1`, and a `brains` array of objects whose `path`, `brain_id` and `added_at` are strings. Anything else is unreadable.
- A file that is unreadable is **never written over**: record and prune return at once and leave it for the user. A missing file is not unreadable.
- Every path it prints goes through the control-byte mask (plain) or JSON escaping.

An unreadable `brains.json` stops recording and copy detection until the user fixes it (`decision:2026-11-12-brains-json-unreadable-warns`). The decision says each read-write open prints one new warning with a `message` and a `fix`, keeps its exit code and leaves the file alone. **OPEN**: that warning has no approved copy, so this build does not print it, and `brain list` and `brain prune` on an unreadable file fail with `cannot_open` and the placeholder message `cannot read brains.json`.

## Open

These need an answer. None is settled text.

1. The warning for an unreadable `brains.json` (`decision:2026-11-12-brains-json-unreadable-warns`): its code, `message` and `fix`. Not printed yet.
2. `BRAIN_LIST_FILE_MAX`, the size cap of the read. The code uses 1 MiB (1048576 bytes) until the copy item gives a value.
3. What `brain list` and `brain prune` print and exit with when `brains.json` is unreadable. The code uses `cannot_open` with `cannot read brains.json`, which is not approved copy.
4. The columns of a plain `brain list` row. The code prints the path.
5. The `--json` data of `brain list` and `brain prune` (the shapes above follow `specs/JSON-STYLE.md` §1.12 and the other write commands).
6. The meaning of "has no brain" for `brain list` and `brain prune`: the code means "not a regular file".
7. The usage line for a bare `remember brain` still says `brain takes 'new-id'`, which no longer lists `list` and `prune`. It is unchanged so that the `brain new-id` tests pass unchanged (plan 26 review point).
8. The help lines for `brain list` and `brain prune`, and the one-line summary in the command list, are the coder's wording.
