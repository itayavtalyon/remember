# Default brain path

Status: how the `remember` binary picks the brain file. The file `~/.remember/default-db` is read; the engine never writes it.

Decisions are `decision:…` entries tagged `subject:data` or `subject:ui-overhaul`. No sentence in this spec is printed: a resolver failure uses the sentences that `--db` already uses.

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

## Where `~` is

`decision:2026-11-11-default-db-home`.

`~` is the `HOME` environment variable, the same home the built-in path uses. `XDG_DATA_HOME` does not move `default-db` or `remember.db`: both stay in `~/.remember` also when `XDG_DATA_HOME` is set. An empty or unset `HOME` fails with `HOME is not set; pass --db PATH`, unless `--db` or `REMEMBER_DB` chose the path.

## Not ruled

The decision does not rule on a second line or on invalid UTF-8. The binary adds no rule for either: it reads the file as one string, so a second line stays in the string and is part of the path.
