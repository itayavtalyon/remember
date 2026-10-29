# 20 — Settings: registry, user.db, commands, credential helper

## Goal

Store settings in the brain and in user.db. Resolve them by layer. Expose `remember settings`. Resolve the sync token through a helper id.

## Status

PENDING COPY AND DESIGN.

Storage, resolution, the credential helper, and `settings export` are specified. Do those stages.

Do not start a stage that prints a sentence listed in [`COPY-PENDING.md`](COPY-PENDING.md) C-01 or C-03 through C-14. [`DESIGN-PENDING.md`](DESIGN-PENDING.md) D-01 and D-03 are answered (2026-10-25, 007 ST-5.4 and ST-7.11a–7.16). The prefix errors, the success JSON, the page sizes, the cursor and the human date form are specified below. Do not generate `plugin.<name>.*` keys in this plan. Plan 25 generates them after the manifest parser exists.

This plan is not ready as a whole.

## Source of truth

- [`design-logs/007-settings.md`](../design-logs/007-settings.md) Design ST-1 through ST-9, and ST-10.3 for user.db. §10's brain migration is plan 19.
- [`design-logs/008-json-envelope.md`](../design-logs/008-json-envelope.md) JE-2 and JE-4.4 for the envelope and actors. Plan 21 owns the envelope writer for every other command. This plan uses that writer for settings commands. When plan 21 has not merged, stop before any settings command that prints JSON.
- SETTINGS-SCHEME §5.1 for the cells 007 did not delete. Deleted codes: `setting_ambiguous`, `setting_read_only`, `time_budget_capped`, `setting_overridden`. Do not emit them.
- 006 E12.4 is the prefix grammar only. The error sentences are 007 ST-5.4 (D-01, answered 2026-10-25). Do not apply E12.6.

## Context

Two layers store a value. `user` is the `settings` table in user.db. `brain` is the `settings` table in the brain. Plan 19 creates the brain tables. This plan creates user.db.

Resolution order, last layer with a value wins: `default`, then `user`, then `brain`, then `flag`. A tombstone is no row (ST-4.6).

Scopes are `user`, `brain`, or both. `set` and `reset` without `--scope` write the user layer, unless the key allows only the brain layer.

user.db path: `$XDG_DATA_HOME/remember/user.db` when `XDG_DATA_HOME` is set, else `~/.remember/user.db`. `REMEMBER_USER_DB` overrides that path only. Mode 0600. Create the file on the first write to the user layer, not during the brain migration. The same directory holds `brains.json` and `plugins/` later. This plan creates only user.db. There is no HOME lookup beyond the `~/.remember/` path the engine already uses for the default brain. The Mac app passes the same data directory in `RememberEnv`. Phones pass `RememberEnv.user_db_path`.

user.db schema:

```sql
PRAGMA user_version = 1;
CREATE TABLE user_meta (min_reader_version TEXT NOT NULL);
-- one row: 0.3.0
CREATE TABLE settings (
  key TEXT PRIMARY KEY, value TEXT NOT NULL, updated_by TEXT NOT NULL, updated_at TEXT NOT NULL
);
CREATE TABLE settings_history (
  key TEXT NOT NULL, old_value TEXT, new_value TEXT, updated_by TEXT NOT NULL, at TEXT NOT NULL
);
```

user.db has no `device_id`, no `version_vector`, no `deleted_at`, and no `change_seq`.

Brain settings columns are plan 19. A brain write adds 1 to the local device counter in `version_vector`, bumps `brain_meta.change_seq`, and stores that integer in `settings.change_seq`. A user write does not bump `change_seq`.

History: each database keeps its own rows. Keep the newest 50 rows for each key in that database. Delete older ones. No age limit. A history row does not count toward the 256 quota. A tombstone does.

Quota: 256 `settings` rows per brain and 256 per user.db. A value is at most 16 KiB of UTF-8. Over the cap, fail with `quota_exceeded` and `detail.quota` `settings_limit`. The brain sentence is locked: `this brain has 256 settings, the most it can hold. Reset settings you don't need with 'remember settings reset'.` The user.db sentence and the 16 KiB sentence are C-09. Do not print them until C-09 is approved. The code and the detail are locked now.

No-op: `set` of the value that the layer already stores writes no history row, does not bump `change_seq`, and exits 0. `reset` on a layer with no row exits 0, prints `(unset)`, and writes no history row.

Brain `reset` sets `deleted_at`, keeps `value`, and bumps the vector and `change_seq`. User `reset` deletes the row. A later `set` on a brain tombstone clears `deleted_at` on that same row.

Undo is not a command. The newest history row for that key in that layer is the undo input. `old_value` null means `reset` on that layer. Any other `old_value` means `set` of that value on that layer. The app calls this. The CLI does not grow an undo command.

Actor on a settings row is `updated_by`. Without `--as` and without `--actor`, store `{"kind":"human","id":null}`. `--as KIND` replaces `kind` and allows only `human`, `agent`, `tool`, or `share`. `--actor ID` replaces only `id`. `ID` is 1 to 64 characters of `[A-Za-z0-9_./:-]`. `--as` does not clear an `--actor` id. When `REMEMBER_HOOK_DEPTH` is 1 or more, `settings set`, `settings reset`, and `settings import` fail with `setting_from_hook` before key lookup. The plain line is `a hook or plugin run can't change settings. Change them in your own terminal.` JSON `fix` waits on C-01. Also reject `--as` and `--actor` when depth is 1 or more, with `invalid_argument`, before the settings ban when both apply. The `invalid_argument` sentence for that rejection is not written. Treat it as part of C-01. Do not invent it.

Sync merge of a brain row: version vector, then later `updated_at`, then larger `device_id`. The loser stays in history. A synced apply keeps the author's `updated_by`. An invalid synced value is skipped with the warning `setting_invalid` and is not written. `detail` has `key`, `value` and `reason` (ST-7.15). A command that applies such a batch exits 0, or 4 with `--strict`. When the value is the subject of one command (`settings set`), it is the error `setting_invalid_value` instead. The plain line is `remember: warning: skipped "<key>": "<value>" isn't a valid value.` (ST-7.15, D-15). Add ` This brain wasn't changed.` only when the batch has one row and the command writes no other row, memory or link. Do not invent a sync wire (D-11).

Registry, compiled in. Do not add a key that is not in this table.

| Key | Type | Default | Range | Scopes |
|---|---|---|---|---|
| `remember.buckets.prefixes` | string_list | `["project:","status:"]` | any number of items, each one bucket prefix, no duplicates | brain |
| `remember.sync.credential_helper` | string | unset | helper id `[a-z][a-z0-9_-]{0,39}` | user |
| `remember.search.candidate_pool` | integer | `200` | 20–2000 | user, brain |
| `remember.search.max_results` | integer | `20` | 1–1000 | user, brain |
| `remember.plugins.chain_budget` | duration | `1.5s` | 100ms–30s | user |
| `remember.plugins.idle_timeout` | duration | `10m` | 1m–24h | user |

`plugin.<name>.enabled` and `plugin.<name>.<setting>` are plan 25. A settings command that sees one of those keys before plan 25 returns `setting_unknown`. That is correct. There is no installed plugin yet.

There is no `remember.search.hybrid` key. There is no `fortkeeps.` namespace. There is no `plugin.<name>.budget`, `plugin.<name>.priority`, or `plugin.<name>.cold_start_grace`. `settings set` of a budget or grace key is `setting_unknown` exit 2. The fix is `Budgets and cold-start grace are set by the plugin's manifest. Raise remember.plugins.chain_budget instead.` The message is the `setting_unknown` line. That line's no-candidate form is C-03. Use the did-you-mean line when ST-1.5 finds a candidate. The candidate line is locked: `unknown setting "<input>". Did you mean "<full key>"? Run 'remember settings list' to see all settings.`

Keys: `namespace "." segment { "." segment }`. A segment matches `[a-z][a-z0-9_]{0,39}`. At most 96 characters. At most 3 segments after the namespace. Typed input rewrites `-` to `_` before lookup. A full key only. A short name, a colon key, or an alias is exit 2 `setting_unknown`. Did-you-mean: when exactly one key has that final segment, offer that key. Otherwise offer at most 3 keys at edit distance 2 or less. With no candidate, the message has no did-you-mean (C-03).

A bucket prefix is 1 to 32 code points of letters, numbers, `-`, `_`, or `.`, then one `:`. ASCII letters fold to lowercase. The list has no item limit. The 16 KiB value limit still applies. Two items that are equal after folding are a duplicate. A bad prefix and a duplicate are each `setting_invalid_value`, with `detail` `key`, `value`, `type`, `allowed` and `item`, plus `other` for a folded duplicate (ST-5.4):

- Bad shape. Message: `"<item>" isn't a bucket prefix.` Fix: `Use 1 to 32 letters, digits, "-", "_" or ".", then one ":", like "project:".`
- Duplicate. Message: `"<item>" is in the list twice.`, or `"<item>" and "<other>" are the same prefix.` when folding made them match. Fix: `List each prefix once.`
- `<item>` and `<other>` are the items as typed, before folding.
- `settings set` fails with exit 1 and writes nothing. `settings import` and a sync apply skip the row with the warning `setting_invalid`, reason `bad_prefix_shape` or `duplicate_prefix`, and go on. There is no ninth-prefix error.

Store a valid list. Default when no row exists: `project:` and `status:`, with no row written until `set`.

Values:

- Duration matches `^[0-9]+(\.[0-9]+)?(ms|s|m|h)$`. Size matches `^[0-9]+(\.[0-9]+)?(B|KiB|MiB)$`. Boolean is `true` or `false`. Integer matches `^(0|[1-9][0-9]*)$`.
- A duration is a whole number of milliseconds. A size is a whole number of bytes. `1.5s` is valid. `0.5ms` is `setting_invalid_value`.
- Store the canonical form. The canonical unit is the largest unit in which the number is at least 1 and has at most 3 fraction digits. No trailing zeros. No trailing `.`. When no unit qualifies, use `ms` or `B`. `1500ms` stores `1.5s`. `90s` stores `1.5m`. `61s` stays `61s`. `100ms` stays `100ms`.
- Output adds `value_ms` or `value_bytes` next to `value`.
- `string_list` is one CLI argument per item. `settings set KEY --clear` stores an empty list.

Flags that are not stored: `--no-filters` skips search filters for one search (plan 25). `--candidate-pool N` overrides `remember.search.candidate_pool`. `--limit N` overrides `remember.search.max_results`. A flag outside the key's range is `setting_out_of_range`. The message names the flag and the key. `detail.flag` is the flag and `detail.key` is the setting key (ST-2.5). The sentence is C-06.

`remember.search.max_results` limits the final search output. Plan 21 and plan 25 apply it. This plan only stores the value.

Credential helper:

- The key holds an id, never a command and never a secret. `set` does not check that a helper exists.
- The engine knows `env`. `env` reads `REMEMBER_SYNC_TOKEN`. Every other id goes to the host token callback. The engine runs no helper process.
- NULL token callback: only `env` works.
- CLI callback: program `remember-credential-<id>` on `PATH`, argv `remember-credential-<id>` and `get`, no shell, empty stdin, stderr to the user stderr, token is the first stdout line with whitespace trimmed, SIGKILL the process group at 30 seconds. Return -1 when the program is not on `PATH`. Return -2 when the program exits non-zero, times out, or prints an empty first line.
- No row: use `REMEMBER_SYNC_TOKEN` when it is non-empty. When it is missing or empty, return no token and do not fail.
- Key set and no token: exit 1 `sync_credential_unavailable`. Message: `the sync credential helper "<id>" did not return a token.` Fix: `Check the helper, or set remember.sync.credential_helper to env and export REMEMBER_SYNC_TOKEN.`
- The token never appears in stdout, stderr, logs, or JSON. Wipe the buffer after the request.
- The Fortkeeps app answers id `keychain` from its Keychain and sets the helper to `keychain`. This plan does not link Keychain.

Commands:

```
remember settings list [--scope brain|user] [--namespace NS] [--changed] [--limit N] [--cursor C] [PREFIX]
remember settings get KEY [--scope brain|user] [--value]
remember settings set KEY VALUE… [--scope brain|user] [--as KIND] [--actor ID]
remember settings set KEY --clear [--scope brain|user] [--as KIND] [--actor ID]
remember settings reset KEY [--scope brain|user]
remember settings reset --namespace NS [--scope brain|user] --yes
remember settings reset --all --scope brain|user --yes
remember settings explain KEY | --all
remember settings history [KEY] [--scope brain|user] [--limit N] [--cursor C]
remember settings export [--scope brain|user|all]
remember settings import FILE [--dry-run] [--scope brain|user]
```

`--scope` is the only layer flag. There is no `config` command.

`get` without `--scope` shows the effective value, the winning layer (`from`), every layer (`layers`), and `updated_by` with `updated_at`. Human layout for that command is the 007 example:

```
remember.search.candidate_pool = 400
  from brain, set by human on 21 Oct at 12:04
  user: (unset) · default: 200
```

Use that layout for `get`. Other human layouts are C-13. Do not invent them.

Human dates (ST-7.16), in `get`, `list`, `history` and `explain`: the short local form `21 Oct at 12:04`, with the local time zone, a 24-hour clock and English month names. When the year is not the current year, show it: `21 Oct 2025 at 12:04`. When the local zone can't be read, use UTC and add ` UTC`. JSON always carries the RFC 3339 UTC `_at` fields.

`get --scope` and `list --scope` show only that layer. No row: print `(unset)`, exit 0. JSON: `value` null, `from` the requested scope, `changed` false. `get --value` on an empty layer prints `(unset)`.

`list --changed` shows only keys that have a row in some layer.

`history` without `--scope` reads both databases. Each item includes `scope` `user` or `brain`. Newest `changed_at` first (the JSON field; the column stays `at`). A tie breaks by scope `brain` then `user`, then by `key` in byte order.

`reset --namespace` and `reset --all` require `--yes`. Without it, nothing changes, and the command fails with `confirmation_required`. The refusal names the number of settings keys that would change (keys, not rows) and the command to confirm. The refusal sentence is C-11.

JSON (ST-7.11a–7.11b). Every settings command uses one setting object: `{key, type, value, value_ms?|value_bytes?, from, changed, layers{default, user?, brain?}}`.

- `layers.default` is `{value}`. A stored layer is `{value, value_ms?|value_bytes?, updated_by, updated_at}`. A layer with no row, or a tombstone, is null. A layer that the key does not allow is absent.
- With `--scope X`, `from` is X, `value` is that layer's value or null, and `layers` has only X.

The wrappers:

| Type | `data` |
|---|---|
| `setting.get.result` | `{setting}` |
| `setting.set.result` | `{setting, scope, old_value, changed}` |
| `setting.reset.result` | `{setting, scope, old_value, changed, action}` |
| `setting.list.result` | `{count, settings[]}`, plus `next_cursor` and `has_more` |
| `setting.history.result` | `{count, changes[]}`, plus `next_cursor` and `has_more` |
| `setting.import.result` | `{count, written, unchanged, skipped, rows[]}` |

- In `set` and `reset`, `setting` is the object after the write. `scope` is the layer that was written. `old_value` is that layer's value before, or null. `changed` is false for a no-op.
- `reset` `action` is `tombstone` for a brain row, `delete` for a user row, and `none` for a layer with no row.
- A history item is `{key, scope, action, old_value, new_value, updated_by, changed_at}`. `action` is `set`, `tombstone` or `delete`. Derive it from the row: a null `new_value` is `tombstone` in a brain and `delete` in user.db.
- An import row is `{key, scope, result, action, reason?}`. `result` is `written`, `unchanged`, `skipped` or `kept_local`, and a `kept_local` row counts as `unchanged`. `action` is `set` or `tombstone` for a written row, and `none` otherwise. `reason` is on `skipped` only. `--dry-run` returns the same shape.

Paging (ST-7.14). `list` returns 100 items by default and `history` returns 50. `--limit` is 1 to 1000. Outside that range, the command fails with `setting_out_of_range`, naming the flag. `next_cursor` is opaque base64url JSON, null on the last page:

- `list`: `{"v":1,"k":"<last key>","s":"<scope or all>","c":<--changed>}`, sorted by key in byte order.
- `history`: `{"v":1,"t":"<changed_at>","s":"<scope>","k":"<key>","r":<row id>}`, in the history order.
- A bad or old cursor, or one reused with a different `--scope` or `--changed`, is `invalid_argument`.

Export prints one plain document, not a `remember-message` envelope:

```json
{"format":"remember-settings-export","version":1,"exported_at":"…Z",
 "settings":[…]}
```

Each element has `key`, `scope`, `value`, `updated_by`, `updated_at`, `device_id`, `version_vector`, and `deleted_at`. A duration or size also has `value_ms` or `value_bytes`. A user row exports `device_id` `""`, `version_vector` `{}`, and `deleted_at` null. Sort by `key`, then `scope`. `--scope` defaults to `all`. Tombstones are included. `remember.sync.credential_helper` exports as its id. User rows never appear in a brain export. Brain export of settings is plan 22. It nests this same document (ST-8.3, D-04 answered 2026-10-25).

Import validates the whole file first, and no row fails the import (ST-8.4, overriding S-Q21). An unknown key skips the row with the warning `setting_import_skipped`, reason `unknown_key`. A bad value skips the row with the warning `setting_invalid`, reason `invalid_value`, and the ST-7.15 plain line. The import writes the other rows. A `version` above 1 fails with the JSON-STYLE §1.7 message. Do not implement `setting_import_invalid`. It is deleted (ST-8.4, D-16). A file that is not a settings document fails with `import_parse`, exit 1, and the brain does not change. The cases are: the JSON does not parse, the root is not an object, the root `format` is not `remember-settings-export`, or `settings` is absent or not an array (ST-8.4). An element that is not an object, or has no `key` string, is skipped with `setting_invalid`, reason `bad_shape`, and the import continues. Its import row has `key` and `scope` null when absent, `result` `skipped`, `action` `none`. Print `remember: warning: skipped row <row> in the settings file because it isn't a setting (each row needs a "key").`, where `<row>` is the 1-based position in `settings`. With no row number, print `remember: warning: skipped a row in the settings file because it isn't a setting (each row needs a "key").` (007 ST-7.15, C-14a). Quote that message from `docs/JSON-STYLE.md` §1.7. Do not paraphrase it.

Import also skips three kinds of row, names each skipped row, and writes the rest. Each is the warning `setting_import_skipped`, with `detail` `key`, `scope` and `reason`, and an import row with `result` `skipped`. The name sentence is C-12. The three kinds are: a plugin that is not installed (`plugin_not_installed`), the other scope when `--scope` is set (`other_scope`), and a layer the target does not allow (`layer_not_allowed`). This plan has no plugin keys, so the first kind does not fire until plan 25. Implement the skip so plan 25 does not add a second merge.

When the target has no row, write the file row and keep the file's `updated_by`. Do not stamp `{kind: import, id: null}` on a settings row. When the target has a row, compare version vector and `updated_at` only (ST-2.7). A user row has no vector, so the later `updated_at` wins. When `updated_at` ties and `device_id` does not pick a winner, the local row stays. The losing value stays in history.

HTTP and MCP are read-only: `GET /v1/settings` and `GET /v1/settings/{key}`, and MCP tool `remember_settings` with `action` `list`, `get`, or `explain`. No HTTP or MCP call writes a setting. `remember_call` accepts `setting.set`, `setting.reset`, and `setting.import`. Route schemas and HTTP status numbers are DESIGN-PENDING D-10. This plan does not add an HTTP server. It accepts those `remember_call` actions in the library.

A hook or plugin run cannot write settings. Plugins read settings. Each request carries the plugin's `plugin.<name>.*` values and each `remember.*` key listed in `permissions.needs`. Plan 25 attaches that list. This plan provides the read function the request calls.

`time_budget_capped` is not a code. A budget larger than the time left in the chain cap is clipped silently. Plan 25 clips it. This plan does not emit a warning for it.

## Locked decisions

| ID | Rule |
|---|---|
| S-02 | Every brain-layer row syncs and exports. No user-layer row travels. No per-key sync flag. |
| S-03 | Prefixes are a plain string list. No add-wins. |
| S-12 | Full keys only. |
| ST-7.2 | Default write layer is user, except a brain-only key. |
| ST-7.10 | Hook runs cannot set, reset, or import settings. |
| ST-8.7 | Settings import keeps the file actor. Memory import does not. That difference is plan 23. |
| G-02 | Engine runs no helper process. |
| M-22 | No `time_budget_capped`. |
| M-24 | Budget and grace are not settings. The fix sentence is locked. |

## Prerequisites

Plan 19 is merged. The brain has the v6 settings tables.

JSON command stages also require plan 21's envelope writer. Storage stages do not.

Do not implement plan 20's command JSON before plan 21. INDEX order is: plan 19, then this plan's stages 1–4, then plan 21, then this plan's stages 5–8.

## Scope

In: user.db, compiled-in registry, pure resolution, brain and user writes, history cap, credential callback and the CLI runner, `settings export`, import merge at the store, `remember_call` setting actions.

Out: plugin-generated keys (plan 25), brain file export (plan 22), HTTP server and MCP schemas (D-10), daemon, Keychain, the settings UI (remember-mac).

## Files

- `src/settings.c` and `src/settings.h`. Pure. No file I/O. Registry, parse, canonical form, resolution, import comparison.
- Prefix checks and duration and size parse live in `src/normalize.c`. Do not add `bucket_prefixes.c`.
- SQL for both databases stays in `src/store_sqlite.c`.
- CLI in `src/cmd_settings.c`. Register it in `REMEMBER_LIB_SOURCES` and compile it with `-Wcast-qual`.
- Token callback type is the 010 H-0.6 `remember_token_fn`. Put the typedef where `RememberEnv` already lives. Do not add a second env struct.
- CLI helper runner is the only process spawn in this plan. Keep it in `src/cmd_settings.c` or in the existing process helper if one already exists. Do not add a daemon.

Tests: `tests/test_settings.c`. Gate suite `settings`.

## Must-pass tests

Assert stored rows, codes, exits, locked sentences, and the ST-7.11b success JSON. Skip plain text that COPY-PENDING still owns.

| Test | Proves |
|---|---|
| 007-1 | Set on user and on brain. `get` reports `from` and `layers` in ST-2.2 order. |
| 007-2 | `candidate_pool` is exit 2 `setting_unknown` and the locked did-you-mean line. `remember:search/candidate_pool` is `setting_unknown`. |
| 007-3 | Typed `-` is stored as `_`. |
| 007-4 | `--scope user` on `remember.buckets.prefixes` is `setting_scope_mismatch`. Do not assert the sentence until C-07. |
| 007-6 | A bare number for a duration is `setting_invalid_value`. |
| 007-7 | `remember.plugins.chain_budget` `45s` is `setting_out_of_range`. Sentence waits on C-06. |
| 007-9 | `REMEMBER_HOOK_DEPTH=1` on `settings set` of a known key is `setting_from_hook` and the locked plain line. |
| 007-11 | `settings export` then `settings import` into an empty user.db matches, except `exported_at`. `updated_by` is the file actor, not `{import, null}`. |
| 007-12 | One bad value: every other row imports, the warning is `setting_invalid`, and that row is `skipped` with reason `invalid_value`. One unknown key: every other row imports, with the warning `setting_import_skipped`, reason `unknown_key`. |
| 007-16 | 51 writes of one key leave the newest 50 history rows. A 2-year-old row inside those 50 stays. |
| 007-17 | `set remember.buckets.prefixes project: area:` without `--scope` writes the brain layer. `--scope user` is `setting_scope_mismatch`. A new brain with no row resolves `project:` and `status:`. |
| 007-18 | Brain `reset` writes a tombstone. `get` then shows the user or default value. |
| 007-19 | `fortkeeps.library.link_enrichment` is `setting_unknown`. |
| 007-20 | A brain-layer write bumps `change_seq`. A user-layer write does not. |
| 007-21 | `get --scope user` when only a brain row exists prints `(unset)` and exits 0. JSON `value` is null. |
| 007-22 | Helper id `~/bin/helper` is rejected. `env` and `pass` are accepted. With `remember-credential-pass` on `PATH`, sync reads the first stdout line. With no program, `sync_credential_unavailable` and the locked message and fix. The token is absent from the output. |
| 007-24 | A user.db row written while brain A is open resolves while brain B is open. |
| 007-25 | `set` of `1500ms` then `get --value` prints `1.5s`. `0.5ms` is `setting_invalid_value`. A second `set` of the same value writes no history row. |
| 007-26 | An older brain row does not replace a newer local row. A skipped-row sentence is not asserted until C-12. |
| 007-29 | `--as agent --actor bot1` stores `{"kind":"agent","id":"bot1"}` on the row and in history. |
| 007-31 | A newer file row keeps that row's `updated_by`. An older file row leaves the local actor. Two user rows with the same `updated_at` keep the local row. |
| 007-32 | `history` items include `scope`. Undo of a brain item with null `old_value` resets the brain layer. |
| 007-34 | 20 valid prefixes set fine. `my project:` is `setting_invalid_value` with the ST-5.4 bad-prefix message. `status: Status:` gives the folded-duplicate message. A synced row with a bad prefix is skipped with `setting_invalid`, and the brain row is unchanged. |
| 007-35 | `set`, `reset`, `get` and `list` with `--json` carry the same setting object. `reset` gives `action` `tombstone` for a brain row, `delete` for a user row, and `none` for a missing row. History items and import rows carry `action`. |
| 007-36 | `list` returns 100 items and a `next_cursor` when there are more. `history` returns 50. `--limit 0` and `--limit 1001` are `setting_out_of_range`, naming the flag. A list cursor reused with another `--scope` is `invalid_argument`. |
| 007-37 | Human `history` prints `21 Oct at 12:04` for a row from this year and `21 Oct 2025 at 12:04` for an older one. `--json` prints `changed_at` in RFC 3339 UTC. |
| 007-38 | Importing a file with a row of an uninstalled plugin warns `setting_import_skipped`, reason `plugin_not_installed`. It fires once plan 25 exists. Until then, test it with an unknown key. |
| 007-39 | `reset --all --scope user` without `--yes` fails with `confirmation_required`, changes nothing, and names the number of keys. |
| no hybrid key | `remember.search.hybrid` is `setting_unknown`. |
| M-24 | `settings set plugin.hybrid.budget 1s` is exit 2. The fix is the locked M-24 sentence. |
| quota | The 257th brain row is `quota_exceeded` with `detail.quota` `settings_limit`. Assert the locked brain sentence only. |

007 tests 5, 8, 10, 13, 15, 23, 28, 30, and 33 are not this plan. They need sync delivery, the manifest parser, search limits, or HTTP. Plan 21, plan 23, or plan 25 names the ones it owns. Do not stub them green.

## Stages

1. Add the pure registry and the parsers in `settings.c` and `normalize.c`. Test canonical duration, integer grammar, and key grammar. Do not print errors.
2. Create user.db on the first user-layer write. Test ST-10.3 and the path rules, including `REMEMBER_USER_DB` and `XDG_DATA_HOME`.
3. Implement resolution and brain and user writes, including tombstone reset, history cap, no-op set, and `change_seq`. Test through the store, not through unapproved CLI sentences.
4. Implement the token callback and the CLI runner. Test 007-22 with the locked sentences.
5. Stop until plan 21's envelope writer is merged. Then add `get` with the locked human layout and the locked JSON fields from ST-7.3 and ST-7.4.
6. Add `set` and `reset` storage flags (`--scope`, `--as`, `--actor`, `--clear`, `--yes`). Assert codes, stored rows and the ST-7.11b success `data`, including `action` and `confirmation_required`. Do not assert C-03, C-06, C-07, C-09, C-10, or C-11.
7. Add `settings export` as the ST-8.1 document. Test 007-11 at the file level.
8. Add import merge per ST-8.4 through ST-8.7. Assert that a bad value or an unknown key skips only that row with its warning code, and that the other rows are written. Do not assert C-12 sentences.
9. After the copy rows are approved (D-01 and D-03 are answered), add the remaining sentences and the success payloads exactly as those answers state. Then tick the matching tests.

Each stage's definition of done: one red test first, the gate green, review closed, then the next stage.

## Cloud contract

- Brain settings rows travel with the brain. User rows stay on the device. There is no secret class and no device filter (ST-2.6).
- Merge is version vector, then later `updated_at`, then larger `device_id`. The apply keeps the author's `updated_by` (ST-2.7).
- Invalid values are skipped with `setting_invalid` and are not written.
- HTTP has GET only. The route schemas are not in this plan (D-10).
- The credential helper id may be `keychain` on a Fortkeeps device. The token is not in the brain and not in user.db.
- Cloud v1 does not offer end-to-end encryption.

## How to work

Same loop as plan 19. Load `standard:coding`, `workflow:deep-review`, `pref:code-review-workflow`, and `lesson:review-run-edge-cases`. Use [`docs/STEP_REVIEW_CHECKLIST.md`](../docs/STEP_REVIEW_CHECKLIST.md), [`docs/engineering-notes.md`](../docs/engineering-notes.md), and [`docs/QUALITY.md`](../docs/QUALITY.md).

Branch from an up-to-date clean `main` after plan 19 has merged. One branch. One pull request.

Do not invent a sentence, a JSON field, a page size, or a prefix error. A hole is a stop. Record it under Implementation Notes and wait for the COPY-PENDING or DESIGN-PENDING row.

Quality gate: the same commands as plan 19. Coverage stays 100% functions and 100% effective lines.

SQLite only in `store_sqlite.c`. Pure logic in `settings.c` has no I/O. Write CLI text through `app_out` and `app_err` and the diag emitter only.

## Definition of done

- [ ] Stages 1–4 and 7 are green without unapproved sentences.
- [ ] Stages 5, 6, and 8 assert codes and stored rows only.
- [ ] Stage 9 has not started, or every blocking COPY-PENDING and DESIGN-PENDING row it needs is approved and quoted in this plan.
- [ ] No `config` command, no add-wins, no `remember.search.hybrid`, no plugin budget keys.
- [ ] Gate green on the committed tree.
- [ ] Second review has no open blocking item.
- [ ] INDEX row 20 is ticked only when stage 9 is done and the review is closed. Until then the row stays pending.

## Second review handover

Probes:

1. Fresh brain, `settings get remember.buckets.prefixes`. [project: and status:, zero rows in `settings`]
2. `settings set remember.buckets.prefixes a: b:` with no `--scope`. [one brain row, user.db absent until a user write]
3. Set the same value again. [no new history row, `change_seq` unchanged]
4. Brain reset, then get. [tombstone present, effective value is the default]
5. 51 sets of one user key. [50 history rows]
6. `settings set candidate_pool 400`. [exit 2 and the locked did-you-mean line]
7. `REMEMBER_HOOK_DEPTH=1 remember settings set remember.search.max_results 5`. [locked `setting_from_hook` line, no row change]
8. Export, import into an empty user.db, export again. [equal except `exported_at`]
9. Helper `pass` with a fake program that prints `secret\n`. [sync sees `secret`, and `secret` is not in the command output]
10. `settings reset remember.search.max_results --json` on a user row. [`action` `delete`, `setting.layers.user` null]
11. `settings import` of a file with one unknown key and one bad value. [two warnings, two `skipped` rows, the other rows written]

Do not re-open: two layers, plain prefix list, full keys, user rows do not travel, hook ban, file actor on settings import.

## Implementation Notes

### Stage 1 — registry and parsers (2026-10-27)

Pure code only. No CLI, no store, no printed error. `src/settings.c` holds the six-key registry, the key fold, and the integer grammar. `src/normalize.c` holds duration and size (canonical form and whole milliseconds or bytes). Integer grammar stayed in `settings.c` so `normalize.c` did not gain another branch-gated parser for a rule that is not a measure.

Key fold rewrites `-` to `_`, then checks segments. A segment is `[a-z][a-z0-9_]{0,39}`. The whole key is 2 to 4 segments and at most 96 characters. Duration and size keep at most 15 fraction digits so the scale stays in `long long`. The canonical unit is the largest one that is at least 1 and has at most 3 fraction digits. `0` stays `0ms` or `0B`. A fraction that is not a whole millisecond or a whole byte is rejected and does not write the whole-value out-parameter.

The six registry rows are the plan table: prefixes (string list, no default row, brain only), credential helper (string, unset, user only), candidate pool (200, 20–2000, both), max results (20, 1–1000, both), chain budget (`1.5s`, 100–30000 ms, user only), idle timeout (`10m`, 60000–86400000 ms, user only).

Tests: `tests/test_settings.c`, suite `settings`, in `tests/gate-suites`. Sanitizer build, `./build/remember_tests ./build/remember --only settings`: 7 tests, 0 failed, 246 asserts. `check-coverage.sh` on the pre-review logic: functions 100%, effective lines 100%, `normalize.c` 382 branch sides, 0 missed (`search_terms.c` 176, `unicode.c` 80, `output.c` 678). Character-class tails (`1a`, `ab~`, `ab:`, `~a`, a zero-size key buffer) were added after that coverage run. The settings tests suppress magic-number tidy on those plan examples, and each registry row is checked for NULL before a field read, because `ASSERT_TRUE` does not count as a sink for the analyzer.

A false duration or size parse now empties a full output buffer before it rejects a NULL argument. A shorter buffer is not written. The command was `./build/remember_tests ./build/remember --only settings` after the change, 7 tests, 0 failed, 246 asserts. A mutant that skips `out[0] = '\0'` in `measure_args_ok` fails `duration_rejects_what_is_not_a_whole_millisecond` and `size_rejects_a_fraction_of_a_byte`. The tree was restored and the suite passed again. `check-coverage.sh` on this tree exited 0: functions 100%, effective lines 100%, `normalize.c` 384 branch sides, 0 missed.

No sentence was invented. INDEX row 20 stays unticked.

### Stage 2 — user.db on the first ensure (2026-10-27)

The settings suite was red before these symbols existed: `remember_tests` failed to compile on implicit declarations of `util_resolve_user_db_path` and `store_user_db_ensure`.

`util_resolve_user_db_path` does not create a file. A non-empty `REMEMBER_USER_DB` wins, else a non-empty `$XDG_DATA_HOME/remember/user.db`, else `$HOME/.remember/user.db`. An empty variable counts as unset. `:memory:` and `file:` reuse `database path must be a regular file (not :memory: or file: URI)`. A missing or empty `HOME` is `HOME is not set`. The brain resolver's missing-HOME sentence still names `--db`, because user.db has no `--db`. The copy, the join, and the HOME read are shared with `util_resolve_db_path`. There is no third `getenv("HOME")`.

`store_user_db_ensure` creates the file. `store_open` does not. The file is opened with `O_NOFOLLOW`, mode 0600, and `fchmod` runs only when the mode is looser. Schema is `user_version` 1, one `user_meta` row `0.3.0`, and empty `settings` and `settings_history`. The settings table has no `device_id`, `version_vector`, `deleted_at`, or `change_seq`. A second call leaves the rows in place and restores mode 0600. `user_version` above 1 reuses `database is newer than this remember`. Below 0 reuses `unsupported database version`. A symlink is rejected and the target is not created.

Sanitizer build, `./build/remember_tests ./build/remember --only settings`: 9 tests, 0 failed, 300 asserts. `check-coverage.sh` exited 0: functions 100%, effective lines 100%, `normalize.c` 384 branch sides, 0 missed. `src/util.c` and `src/store_sqlite.c` stay on the line gate, not the pure-TU branch list.

No sentence was invented. INDEX row 20 stays unticked. Stage 3 has not started.

## Review Notes

**2026-10-27, stage 1 deep review.** In-session, on `be3ad6a` plus this fix. Itay asked for the review, a simplify pass, and the next stage in the same session, so this note closes stage 1. Local Bugbot is not installed.

| # | Severity | Finding | Outcome |
|---|---|---|---|
| 1 | important | `norm_duration_canonical` said a false return empties a large enough buffer. NULL `src` and NULL `whole` returned first, so the caller's bytes stayed (`xxxxxxxxxxxxxxxxxxxxxxx`). | Fixed. `measure_args_ok` empties a full buffer, then rejects NULL. A 23-byte buffer stays untouched (`Q`). Mutant that skips the clear fails the two reject tests. |
| 2 | nit | `default_text` was described as "unset until written" for every NULL default. Prefixes still resolve to `project:` and `status:` with no row until set. | Comment only. The list stays a stage 3 resolution rule, not a stored string. |

Confirmed by running, and already true before the fix: `90m` is `1.5h`, `61m` stays `61m`, `0s` and `0h` are `0ms`, `1.000s` is `1s`, `1024KiB` is `1MiB`, `1536KiB` is `1.5MiB`, `0KiB` and `0MiB` are `0B`. `+1s`, `1 s`, `1S`, and `1.2.3s` are rejected. A failed integer parse leaves `9223372036854775807` in the out-parameter. `1ms` does not match the `m` suffix. The registry is the six plan rows. No SQLite, no CLI, no invented sentence.

Simplification. Applied: the buffer rule is one helper, because leaving it inline put `measure_canonical` at cognitive complexity 27 (limit 25). Declined: one digit loop for integers and measures (a duration allows `01s`, an integer rejects `01`, so the helper grows flags and saves no reading); dropping `ScaledParts` (clang-tidy treats the adjacent integers as swappable); one macro for the six registry NULL checks (the analyzer only sees the sink when the check is written at the field read).

Fraction digits stay capped at 15, which the header already states. A 16th digit is rejected even when it is zero. That is the overflow bound, not a second grammar.

GitHub's Linux clang-tidy 18 (on `be3ad6a`) rejected `out[i] = (src[i] == '-') ? '_' : src[i]` as a narrowing conversion into `char`. The assignment is now an explicit `(char)` cast. macOS clang-tidy 22 did not flag that line.

**2026-10-28, stage 2 deep review.** In-session, on `9d86269` after PR #46 merged. Itay said to proceed with the next stage, review or coding. This note closes stage 2. Local Bugbot is not installed.

| # | Severity | Finding | Outcome |
|---|---|---|---|
| 1 | important | `open_user_db_file` called `fchmod` on every regular file whose mode was not `0600`, before SQLite looked at it. A non-empty text file at the path was chmod'd to `0600` and then rejected. | Fixed. `fchmod` runs only for an empty file or a file whose header is `SQLite format 3`. A text file keeps its bytes and its mode. |
| 2 | important | `database path is missing`, `HOME is not set`, and `cannot create user.db` were not in `code_for_store_text`, so a later CLI report would call them `internal`. | Fixed. An empty path reuses `empty database path` (`usage`). `HOME is not set` is `usage`. `cannot create user.db` is `cannot_open`. A SQLite open failure reuses `cannot open database` (`database`). |

Confirmed by running, sanitizer build. `./build/remember_tests ./build/remember --only settings,errors` on the first form of the fix: 116 tests, 0 failed, 1758 asserts. After the file check moved to `open`/`read`, `./build/remember_tests ./build/remember --only settings`: 10 tests, 0 failed, 314 asserts. That includes `ensure_leaves_a_non_database_file_unchanged` (`notes\n` stays, mode stays `0644`, error `cannot read user_version: file is not a database`) and the second ensure still returning the inserted `remember.search.max_results` value `20`. The `HOME is not set` and `cannot create user.db` lines in `store_text_codes_follow_the_locked_split` passed in that combined run; those classifier lines did not change after it.

Mutants, then the tree restored and the same suites passed again. `fd_is_sqlite` returning true on a short read chmods the text file: `ensure_leaves_a_non_database_file_unchanged` fails `ASSERT_EQ_INT(actual=384, expected=420)` (`0600` vs `0644`). Deleting the `HOME is not set` classifier makes `store_text_codes_follow_the_locked_split` fail `ASSERT_EQ_INT(actual=4, expected=0)` (`internal` vs `usage`).

Simplification. Applied: one empty-path sentence, the one the brain already uses; one SQLite-open sentence, `cannot open database`. Declined: a `goto` cleanup in `store_user_db_ensure` (the descriptor is closed inside `open_user_db_file`, and four `sqlite3_close` returns read as the failure); splicing `k_min_reader_version` into the schema SQL (a format buffer, while the test locks the literal `0.3.0`); one opener shared with `ensure_sidecar_dir` (a directory at `0700` versus a file at `0600` plus a header check).

The SQLite open is still by path after `close`, so a swapped directory entry can be opened. Holding the descriptor open does not keep the journal names on the real path. `store_open` has the same window. No second opener.

No new sentence. INDEX row 20 stays unticked. Stage 3 has not started.

GitHub CodeQL on `be4beb2` flagged `chmod` on the text-file path after `close` as a time-of-check race. The test now `fchmod`s the descriptor it still holds, and reads the mode back with `fstat` on the descriptor it opens to check the bytes.
