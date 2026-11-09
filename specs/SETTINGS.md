# Settings

Status: the settings this binary reads and writes, and the `remember settings` commands.

Decisions are `decision:…` entries tagged `subject:settings`. A section with no such key names the 007 ST section. Printed sentences are the COPY-PENDING rows named here. A row still open there (C-42, C-43) is printed as drafted and is not part of this spec. JSON value rules are [JSON-STYLE.md](JSON-STYLE.md). The envelope, error and warning objects, and cursor paging are [JSON-ENVELOPE.md](JSON-ENVELOPE.md).

## Registry

`decision:settings-scheme`, `decision:s-02`, `decision:s-03`, `decision:g-25`, `decision:settings-scope`. 007 ST-1.

The database is the only source of settings. There is no settings file and no environment override, except `REMEMBER_SYNC_TOKEN` for the `env` credential helper (§ Sync token). The registry is compiled in. It has six keys:

| Key | Type | Default | Range | Layers |
|---|---|---|---|---|
| `remember.buckets.prefixes` | `string_list` | `["project:","status:"]` | bucket prefixes (ST-5.4) | brain |
| `remember.sync.credential_helper` | `string` | none (`null`) | `[a-z][a-z0-9_-]{0,39}` | user |
| `remember.search.candidate_pool` | `integer` | `200` | 20–2000 | user, brain |
| `remember.search.max_results` | `integer` | `20` | 1–1000 | user, brain |
| `remember.plugins.chain_budget` | `duration` | `1.5s` | 100ms–30s | user |
| `remember.plugins.idle_timeout` | `duration` | `10m` | 1m–24h | user |

Each key also has a one-line description and its related keys, shown by `settings explain`. `candidate_pool` and `max_results` are related to each other. A setting has no sync attribute: every brain row syncs and exports, and no user row syncs.

The prefix default is not stored. Nothing is written until a `set`.

## Keys and values

`decision:s-12`, `decision:q4-02`. 007 ST-1.4, ST-1.5, ST-4, ST-5.

A key is 2 to 4 segments joined by `.`, at most 96 bytes. A segment is `[a-z][a-z0-9_]{0,39}`. Input folds `-` to `_` before the check. Commands take full keys only. An unknown key is `setting_unknown`, exit 2, with C-03. When exactly one registry key is within the ST-1.5 distance, the line adds that key as a did-you-mean. A colon is not rewritten.

Values are stored as JSON text:

- `integer`: a JSON number. Input matches `^(0|[1-9][0-9]*)$`. No sign, no leading zero, no exponent.
- `duration`: a JSON string of the canonical form. Units `ms`, `s`, `m`, `h`. The canonical unit is the largest one that gives at least 1 and at most 3 fraction digits. The value must be a whole number of milliseconds. JSON adds a `_ms` twin with the integer milliseconds.
- `string`: a JSON string.
- `string_list`: a compact JSON array in input order, after the ST-5.4 prefix fold. Each item ends in `:`. A duplicate after the fold is refused.

A value outside its range is `setting_out_of_range` (C-05). A value of the wrong form is `setting_invalid_value` (C-05). A stored value is at most 16384 bytes of JSON.

## Layers and resolution

`decision:settings-scope`, `decision:settings-scope-flag`, `decision:settings-set-default-layer`, `decision:g-34`. 007 ST-2, ST-7.2.

Three layers: the compiled default, the user layer (user.db), and the brain layer (the brain's `settings` table). A key allows the layers in the registry table.

A read takes the first of: the brain row when it is live, the user row, the default. A brain tombstone is no value. A layer the key does not allow is never read, also when a row was written there by hand.

`--scope brain|user` chooses one layer. It is the only layer flag. `set` and `reset` without `--scope` write the user layer, unless the key allows only the brain layer. A scope the key does not allow is `setting_scope_mismatch` (`decision:plan20-user-only-scope-draft`).

## Storage

`decision:g-14`, `decision:q4-04`, `decision:s-04`, `decision:g2-12`. 007 ST-2, ST-10.

The brain table `settings` has `key`, `value`, `updated_by`, `updated_at`, `device_id`, `version_vector`, `deleted_at` and `change_seq`. A brain write increments this device in the version vector and bumps the brain's `change_seq`. `device_id` is the last writer's device. It breaks merge ties and is never a filter.

user.db has its own `settings` table without `device_id`, `version_vector`, `deleted_at` and `change_seq`. Its path is the first of: a non-empty `REMEMBER_USER_DB`, `$XDG_DATA_HOME/remember/user.db`, `$HOME/.remember/user.db`. An empty variable counts as unset. A missing `HOME` fails the command with `HOME is not set`. `:memory:` and `file:` paths are refused. The file is created by the first user-layer write, not by a read and not by a refused value. It is opened without following a symlink and kept at mode 0600. Its `user_version` is 1.

`updated_by` is the actor object `{kind, id}` (JSON-ENVELOPE § Actors). An omitted actor is `{"kind":"human","id":null}`. `updated_at` is the write time in the JSON-STYLE stamp form.

A set of the value the live layer already holds writes nothing: no history and no `change_seq` bump. Each database holds at most 256 `settings` rows. A tombstone counts. An update of an existing row is allowed at the cap. A new key past the cap is `quota_exceeded` (C-09).

## History and tombstones

`decision:q4-01`. 007 ST-2.5, ST-7.6.

Every write adds a `settings_history` row in the same database: `key`, `old_value`, `new_value`, `updated_by`, `at`. Each database keeps the newest 50 rows per key by insertion order. There is no age limit. History rows do not count toward the quota.

A brain reset sets `deleted_at`, keeps `value`, bumps the vector and `change_seq`, and writes history with a null `new_value`. The tombstone syncs and exports. A user reset deletes the row and writes history with a null `new_value`. A set over a tombstone writes history with a null `old_value`.

## Sync token

`decision:g-02`, `decision:r3-00a`, `decision:g2-13`. 007 ST-3, H-0.6.

The engine reads `remember.sync.credential_helper` when sync needs a token.

- No row, or the id `env`: the token is `REMEMBER_SYNC_TOKEN` with ASCII whitespace trimmed. Unset, empty, or blank is no token, and the lookup does not fail. A control byte or DEL inside, or a value that does not fit, fails the lookup and prints nothing (C-42 is open).
- Any other id: the host's `token` callback in `RememberEnv`. A NULL host or callback serves only `env`. An id outside the grammar is not passed to the host.

A helper id that gives no token is `sync_credential_unavailable` with its fix. The token is never printed, logged, or put in JSON. The caller's buffer is wiped on failure, and a token that does not fit is wiped, not cut.

The CLI host runs `remember-credential-<id> get` from `PATH`, with no shell and empty stdin. The token is the first stdout line, trimmed. The process group is killed after 30 seconds. A program not on `PATH`, a non-zero exit, a timeout, an empty or oversized first line, or another exec failure gives no token. `set` does not check that the helper exists.

## Commands

`decision:settings-scheme`, `decision:settings-scope-flag`. 007 ST-7.

Every verb takes `--json`, which writes one envelope with type `setting.<verb>.result` (JSON-ENVELOPE § Reply types). `--` ends options. `settings` with no known verb is a usage error.

### The setting object

007 ST-7.11a. `{key, type, value, from, changed, layers}`. `value` is the shown value or `null`, with `value_ms` for a duration. `from` is `default`, `user` or `brain`. `changed` is true when a user or brain row gives the value, even one equal to the default. `layers` has `default`, then each allowed layer, each `null` or `{value, value_ms?, updated_by, updated_at}`. With `--scope`, `from` is that scope and `layers` holds that layer only. A stamp that is not a real calendar day is left out, and `updated_by` stays.

### get

`remember settings get KEY [--scope brain|user] [--value]`. `decision:plan20-empty-list-related`. 007 ST-7.3, ST-7.4.

Plain output is `KEY = VALUE`, then `from LAYER[, set by ACTOR on DATE]`, then the other layers. With `--scope`, an empty layer prints `(unset)` and exits 0. `--value` prints the value only. An empty list prints `(empty)`. The date is the local ST-7.16 form. JSON `data` is `{setting}`.

### set

`remember settings set KEY VALUE... [--scope brain|user] [--as KIND] [--actor ID] [--hooks|--no-hooks]`, and `set KEY --clear` for a list. `decision:plan20-scalar-shape-draft`. 007 ST-7.7, ST-7.11b.

A list takes one item per argument. A key that is not a list takes one value. Plain output is the C-13 set line for the layer written, or the C-13 no-op line. JSON `data` is `{setting, scope, old_value, changed}`.

### reset

`remember settings reset KEY [--scope]`, `reset --namespace NS [--scope] --yes`, `reset --all --scope brain|user --yes`. `decision:plan20-reset-count-zero-one`. 007 ST-7.8, ST-7.11b.

A missing row changes nothing. A bulk reset without `--yes` is `confirmation_required` (C-11) and changes nothing. A bulk reset also resets stored keys the registry does not know. The two databases are not one transaction. Plain output is C-10 for each key. JSON `data` is `{setting, scope, old_value, changed, action}`, with `action` `tombstone`, `delete` or `none`. A bulk reset is `{count, settings[]}`.

### list

`remember settings list [PREFIX] [--namespace NS] [--scope brain|user] [--changed] [--limit N] [--cursor C]`. `decision:plan20-list-limit-draft`. 007 ST-7.5, ST-7.14.

Registry keys in byte order. `PREFIX` keeps keys that start with it. `--namespace` keeps keys under it. `--scope` keeps keys that allow that layer and shows that layer only. `--changed` keeps keys whose value comes from a user or brain row. Plain output is one line per key: `KEY = VALUE   (LAYER)`. JSON `data` is `{count, settings[]}` of setting objects. `--limit` is 1 to 1000, default 100.

### history

`remember settings history [KEY] [--scope brain|user] [--limit N] [--cursor C]`. `decision:plan20-history-plain-draft`. 007 ST-7.6, 007-32.

Rows from both databases, newest `at` first. On one `at`, brain before user, then key in byte order, then the newer row. `KEY` and `--scope` filter. JSON `data` is `{count, changes[]}`. A change is `{key, scope, old_value, new_value, updated_by, changed_at, action}`, with `_ms` twins for a duration. `action` is `set` when `new_value` is set, else `tombstone` in the brain and `delete` in user.db. A key the registry does not know shows its stored text as a JSON string. `--limit` is 1 to 1000, default 50.

### explain

`remember settings explain KEY` or `explain --all`. `decision:plan20-explain-human-form`, `decision:plan20-explain-json-shape`, `decision:plan20-explain-description-drafts`. 007 ST-7.9.

Plain output is one block per key: the header with type, range and default, the description, the layers the key allows, the current value and where it comes from, and the related keys. `--all` shows every key in byte order, one blank line apart. JSON `data` is `{count, entries[]}`: each entry is the setting object plus `description`, `scopes`, `default`, `min` and `max` (with `_ms` twins for a duration, absent when the key has no range), and `related`.

### Paging

`decision:plan20-cursor-carries-filters`, `decision:plan20-cursor-fix-draft`. 007 ST-7.14. `list` and `history` page with `--limit` and `--cursor`. With `--json`, the cursor is the envelope's `next_cursor`. In plain output, a more-pages line on stderr gives the command to run. The cursor holds the last row and every filter. A filter left out with `--cursor` comes from the cursor. A filter that differs from the cursor's, a cursor from the other verb, or a cursor that is not one is `invalid_argument`.

### export

`remember settings export [--scope brain|user|all]`. `decision:settings-export-scope`, `decision:plan20-export-json-error`. 007 ST-8.1.

Prints the document `{format: "remember-settings-export", version: 1, exported_at, settings[]}` on stdout. Rows sort by key, brain before user on one key. A brain row carries `device_id`, the sorted `version_vector` and `deleted_at`, so a tombstone travels. A user row carries `device_id` `""` and `version_vector` `{}`. With `--json`, a success is the same document, not an envelope. A failure is the envelope. `--scope brain` works without `HOME`.

### import

`remember settings import FILE [--dry-run] [--scope brain|user] [--hooks|--no-hooks]`. `decision:g-16`, `decision:plan20-import-brain-merge`, `decision:plan20-import-history-at`, `decision:plan20-import-quota`, `decision:plan20-import-bad-field`, `decision:plan20-import-unknown-key-draft`, `decision:plan20-import-parse-draft`, `decision:plan20-import-version-draft`, `decision:plan20-import-dry-run-draft`. 007 ST-8.2 to ST-8.7.

A file that is not a settings document, or has a later version, is `import_parse` and writes nothing. Each row is checked in ST-8.2 member order. A bad row is skipped with a warning, and the other rows are written (ST-8.4): `setting_import_skipped` for an unknown key, another scope, or a layer the key does not allow; `setting_invalid` for a bad value, shape or field.

A row merges like sync: the version vector first (brain only), then the later `updated_at`, then the larger `device_id` (brain only). A tie keeps the local row. A row equal to the local one writes nothing. A brain write stores the file's fields with the pointwise-max vector and bumps `change_seq`. Each write adds a history row with the file's actor and `updated_at`. A new key over the quota fails the whole import.

Each database runs one transaction, and the brain commits first. `--dry-run` runs the merge, rolls both back, and does not create user.db. JSON `data` is `{count, written, unchanged, skipped, rows[]}`. A row is `{key, scope, result, action, reason?}`.

## Hooks

007 ST-7.10. `set`, `reset` and `import` fail with `setting_from_hook` when `REMEMBER_HOOK_DEPTH` is 1 or more. `--hooks` and `--no-hooks` are accepted and change nothing yet.

## Errors

Codes: `setting_unknown` (exit 2), `setting_invalid_value`, `setting_out_of_range`, `setting_scope_mismatch`, `setting_from_hook`, `quota_exceeded`, `confirmation_required`, `invalid_argument`, `import_parse`, `sync_credential_unavailable`. Warnings: `setting_import_skipped`, `setting_invalid`. Each `error` object carries the message, the fix and, where the code has one, `detail` with string members (`decision:plan20-error-detail-strings`).
