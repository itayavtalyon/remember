# Older CLI, newer brain

Status: what this binary does when it opens a brain made by a newer remember.

Decisions are `decision:…` entries tagged `subject:daemon`. The frozen log 007 ST-10.2 (`schema_too_new`, `min_reader_version`) is history: this spec replaces its text. JSON value rules are [JSON-STYLE.md](JSON-STYLE.md). The envelope is [JSON-ENVELOPE.md](JSON-ENVELOPE.md). The printed sentences are copy item R27b-1 of `decision:2026-11-12-copy-batch-final`.

## 8. Older CLI, newer brain

`decision:2026-10-31-old-cli-read-only`, `decision:2026-10-31-old-cli-reads-warn-writes-fail`, `decision:2026-11-01-newer-brain-error-name`, `decision:2026-11-01-cli-outdated-merge`. 007 ST-10.2 is history.

A brain is newer than this binary when its `schema_version` is higher than the highest schema this binary knows, or when its `brain_meta.min_reader_version` is newer than the version this binary reads (or is not `MAJOR.MINOR.PATCH`). This binary then opens the brain read-only. Every read command works. Every write command fails.

### 8.1 Detection

`decision:2026-10-31-cli-outdated-detect`, `decision:2026-11-03-schema-read-only`.

The brain's `schema_version` is its `PRAGMA user_version`. The highest schema this binary knows is 6. An equal `schema_version` is not newer, unless `min_reader_version` is. A `schema_version` below 0 is `unsupported_database`, as before. The check runs on every open. This binary never migrates a newer brain and never writes to it.

### 8.2 Read or write

Every command has one class in one table (`app_command_class`): read, write, or no store.

| Class | Commands |
|---|---|
| Read | `get`, `list`, `search`, `tags`, `related`, `conflicts`, `device` (also `device new`), `export`, and `settings` with `get`, `list`, `history`, `explain`, `export` |
| Write | `add`, `update`, `delete`, `link`, `unlink`, `rekey`, `purge`, `import`, `conflict`, `repair`, `brain`, and `settings` with `set`, `reset`, `import` |
| No store | `help`, `version` |

`device new` is a read: it writes the device id file beside the brain and writes nothing to the brain (007 ST-10.2). A command missing from the table fails a test.

A write is refused before the command runs, so a refused write leaves the brain file byte-identical.

### 8.3 Read warning

`decision:2026-10-31-cli-outdated-warning-frequency`, `decision:2026-11-01-cli-outdated-version-shown`, `decision:2026-11-12-e3-warnings-have-fix-field`, `decision:2026-11-12-sw1-upgrade-line-no-command`.

Every read command on a newer brain prints the warning, every time. The code is `schema_too_new_read_only`. The exit code follows the command: 0, or 4 with `--strict`.

- `message`: `this brain was made by a newer version of remember, so it is read-only here. You can still read and export it. This brain uses schema <brain>; this remember reads up to schema <cli>.`
- `fix`: `Upgrade remember to make changes.`
- `detail`: `{"brain_schema_version":"<brain>","cli_schema_version":"<cli>"}`
- Plain line on stderr: `remember: warning: ` plus the message, a space, and the fix.

With `--json` the warning is an item of `warnings` with `code`, `message`, `fix`, `detail`, in that order, and stderr is empty.

### 8.4 Write failure

`decision:2026-10-31-cli-outdated-exit-code`, `decision:2026-11-01-newer-brain-error-name`.

Every write command on a newer brain fails with `schema_too_new`, exit 75 (`EX_TEMPFAIL`), `retryable` false. No warning is printed with it, so `--strict` leaves the exit at 75.

- `message` and `fix`: the same two sentences as § 8.3.
- `detail`: the same two members as § 8.3.
- Plain line: `remember: ` plus the message, a space, and the fix. Example: `remember: this brain was made by a newer version of remember, so it is read-only here. You can still read and export it. This brain uses schema 8; this remember reads up to schema 7. Upgrade remember to make changes.`

### 8.5 What the messages show

`decision:2026-11-01-cli-outdated-version-shown`, `decision:2026-10-31-coexistence-copy`, `decision:2026-10-31-coexistence-copy-2`.

The numbers are schema numbers, never app versions. Both numbers are integers this binary reads or knows, and no text from the brain is echoed. The words `cli_outdated`, `brew upgrade remember` and an app version never appear in these lines. A brain whose `min_reader_version` is newer and whose `schema_version` is equal shows the same number twice.

### 8.6 Sweep

`decision:2026-10-31-no-sweep-agent`.

A command on a newer brain does not write to it, and no later step of the command may. The post-command sweep must skip a newer brain and must not change the exit code. The engine has no sweep yet (plan 32); its first sweep must call `store_newer_brain` and skip.
