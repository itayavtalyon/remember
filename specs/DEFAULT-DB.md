# Default brain path

Status: how the `remember` binary picks the brain file. The file `~/.remember/default-db` is read; the engine never writes it.

Decisions are `decision:…` entries tagged `subject:data` or `subject:ui-overhaul`. A resolver failure uses the sentences that `--db` already uses, and one more sentence, `cannot read default-db`, quoted below.

## Order

`decision:ui-overhaul-grill-2026-10-12` item 3. The binary uses the first of these that applies:

1. `--db PATH`. An empty `--db` value counts as not given.
2. `REMEMBER_DB`. An empty value counts as unset, so it does not hide the file.
3. The one absolute path in `~/.remember/default-db`.
4. `~/.remember/remember.db`.

A set `REMEMBER_DB` hides the file. `--db` hides it too.

## The file

`decision:ui-overhaul-grill-2026-10-12` item 3.

The file is one UTF-8 line: the absolute path, ending in a newline. The binary trims ASCII whitespace from both ends of what it reads. A missing final newline is accepted.

The binary uses the built-in path `~/.remember/remember.db` in these cases:

- the file is missing;
- the trimmed line is empty;
- the trimmed line does not start with `/`.

A line that is `:memory:` or starts with `file:` is a failure, not a missing file. It fails with the sentence `--db :memory:` gives: `database path must be a regular file (not :memory: or file: URI)`. A line too long for the path buffer fails with `database path is too long`, as `--db` does. A path read from the file goes through the same check as the path of `--db`; there is no second check.

The binary does not create the file, not on a missing file, an empty line, or a relative line. The binary does not change an existing file.

`decision:2026-11-14-plan27-review-grill` G3: a program that writes the file replaces it atomically. It writes a temp file in `~/.remember`, then renames it over `default-db`, so a reader never sees a torn write (a cut path would name the wrong brain). The Mac app builds this writer (plan M53).

## An unreadable file

`decision:2026-11-13-default-db-unreadable`, linked to `decision:ui-overhaul-grill-2026-10-12`, and `decision:2026-11-14-plan27-review-grill` G1 and G2.

When `~/.remember/default-db` exists but cannot be read (permission denied, a folder, an I/O error, a symlink loop), is not a regular file (a FIFO, a socket, a device, or a symlink to one), or holds a NUL byte, the command fails at once. The binary opens the file without blocking, so a FIFO fails instead of hanging. There is no fallback to `~/.remember/remember.db`. The error code is `cannot_open`, the message is `cannot read default-db`, and the fix text and the JSON and plain-line forms are those of the existing `cannot_open` row for a path that is not a folder (`Check that the folder exists and that you can read and write it.`).

A file that does not exist is not unreadable: a missing file, a symlink that points nowhere, and a `~/.remember` that is not a folder all count as no file.

## Where `~` is

`decision:2026-11-11-default-db-home`.

`~` is the `HOME` environment variable, the same home the built-in path uses. `XDG_DATA_HOME` does not move `default-db` or `remember.db`: both stay in `~/.remember` also when `XDG_DATA_HOME` is set. An empty or unset `HOME` fails with `HOME is not set; pass --db PATH`, unless `--db` or `REMEMBER_DB` chose the path.

## Not ruled

The decisions do not rule on a second line or on invalid UTF-8. The binary adds no rule for either: it reads the file as one string, so a second line stays in the string and is part of the path.
