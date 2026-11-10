# 20 — Settings: registry, user.db, commands, credential helper

## Goal

Store settings in the brain and in user.db. Resolve them by layer. Expose `remember settings`. Resolve the sync token through a helper id.

## Status

Copy approved 2026-10-25. Print C-01 and C-03 through C-14 from [`COPY-PENDING.md`](COPY-PENDING.md). Do not invent a sentence past that file.

Storage, resolution, the credential helper, and `settings export` are specified. Do those stages.

[`DESIGN-PENDING.md`](DESIGN-PENDING.md) D-01 and D-03 are answered (007 ST-5.4 and ST-7.11a–7.16). The prefix errors, the success JSON, the page sizes, the cursor and the human date form are specified below. Two import questions stay open. The words of the `unknown_key` skip reason are a copy hole. The copy editor writes that one phrase. Do not invent it. Whether a bad imported value stays `setting_invalid` (ST-8.4) or moves to `setting_import_skipped` is D-15. Until the log changes, follow ST-8.4 and print C-14 for that row. History `action` for a set stays `set` (ST-7.11b). C-13's `none` does not replace it until D-18. Do not generate `plugin.<name>.*` keys in this plan. Plan 25 generates them after the manifest parser exists.

This plan starts after plan 19. Command JSON starts after plan 21.

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

Quota: 256 `settings` rows per brain and 256 per user.db. A value is at most 16 KiB of UTF-8. Over the cap, fail with `quota_exceeded` and `detail.quota` `settings_limit`. The brain sentence is locked: `this brain has 256 settings, the most it can hold. Reset settings you don't need with 'remember settings reset'.` The user.db sentence and the 16 KiB sentence are C-09. Print them.

No-op: `set` of the value that the layer already stores writes no history row, does not bump `change_seq`, and exits 0. `reset` on a layer with no row exits 0, prints `(unset)`, and writes no history row.

Brain `reset` sets `deleted_at`, keeps `value`, and bumps the vector and `change_seq`. User `reset` deletes the row. A later `set` on a brain tombstone clears `deleted_at` on that same row.

Undo is not a command. The newest history row for that key in that layer is the undo input. `old_value` null means `reset` on that layer. Any other `old_value` means `set` of that value on that layer. The app calls this. The CLI does not grow an undo command.

Actor on a settings row is `updated_by`. Without `--as` and without `--actor`, store `{"kind":"human","id":null}`. `--as KIND` replaces `kind` and allows only `human`, `agent`, `tool`, or `share`. `--actor ID` replaces only `id`. `ID` is 1 to 64 characters of `[A-Za-z0-9_./:-]`. `--as` does not clear an `--actor` id. When `REMEMBER_HOOK_DEPTH` is 1 or more, `settings set`, `settings reset`, and `settings import` fail with `setting_from_hook` before key lookup. The plain line, message, and fix are the C-01 `setting_from_hook` row. Also reject `--as` and `--actor` when depth is 1 or more, with `invalid_argument`, before the settings ban when both apply. The sentence for that rejection is not written. Do not invent it. The copy editor writes it. The code stays `invalid_argument`.

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

There is no `remember.search.hybrid` key. There is no `fortkeeps.` namespace. There is no `plugin.<name>.budget`, `plugin.<name>.priority`, or `plugin.<name>.cold_start_grace`. `settings set` of a budget or grace key is `setting_unknown` exit 2. The fix is `Budgets and cold-start grace are set by the plugin's manifest. Raise remember.plugins.chain_budget instead.` The message is the `setting_unknown` line. That line's no-candidate form is C-03. Print it. Use the did-you-mean line when ST-1.5 finds a candidate. The candidate line is locked: `unknown setting "<input>". Did you mean "<full key>"? Run 'remember settings list' to see all settings.`

Keys: `namespace "." segment { "." segment }`. A segment matches `[a-z][a-z0-9_]{0,39}`. At most 96 characters. At most 3 segments after the namespace. Typed input rewrites `-` to `_` before lookup. A full key only. A short name, a colon key, or an alias is exit 2 `setting_unknown`. Did-you-mean: when exactly one key has that final segment, offer that key. Otherwise offer at most 3 keys at edit distance 2 or less. With no candidate, the message has no did-you-mean (C-03). Print that line.

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

Flags that are not stored: `--no-filters` skips search filters for one search (plan 25). `--candidate-pool N` overrides `remember.search.candidate_pool`. `--limit N` overrides `remember.search.max_results`. A flag outside the key's range is `setting_out_of_range`. The message names the flag and the key. `detail.flag` is the flag and `detail.key` is the setting key (ST-2.5). The sentence is C-06. Print it.

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

Use that layout for `get`. The other human layouts are C-13. Print them.

Human dates (ST-7.16), in `get`, `list`, `history` and `explain`: the short local form `21 Oct at 12:04`, with the local time zone, a 24-hour clock and English month names. When the year is not the current year, show it: `21 Oct 2025 at 12:04`. When the local zone can't be read, use UTC and add ` UTC`. JSON always carries the RFC 3339 UTC `_at` fields.

`get --scope` and `list --scope` show only that layer. No row: print `(unset)`, exit 0. JSON: `value` null, `from` the requested scope, `changed` false. `get --value` on an empty layer prints `(unset)`.

`list --changed` shows only keys that have a row in some layer.

`history` without `--scope` reads both databases. Each item includes `scope` `user` or `brain`. Newest `changed_at` first (the JSON field; the column stays `at`). A tie breaks by scope `brain` then `user`, then by `key` in byte order.

`reset --namespace` and `reset --all` require `--yes`. Without it, nothing changes, and the command fails with `confirmation_required`. The refusal names the number of settings keys that would change (keys, not rows) and the command to confirm. The refusal sentence is C-11. Print it.

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

Import validates the whole file first, and no row fails the import (ST-8.4, overriding S-Q21). An unknown key skips the row with the warning `setting_import_skipped`, reason `unknown_key`. A bad value skips the row with the warning `setting_invalid`, reason `invalid_value`, and the ST-7.15 plain line. The import writes the other rows. A `version` above 1 fails with the JSON-STYLE §1.7 message. Do not implement `setting_import_invalid`. It is deleted (ST-8.4, D-16). A file that is not a settings document fails with `import_parse`, exit 1, and the brain does not change. The cases are: the JSON does not parse, the root is not an object, the root `format` is not `remember-settings-export`, or `settings` is absent or not an array (ST-8.4). An element that is not an object, or has no `key` string, is skipped with `setting_invalid`, reason `bad_shape`, and the import continues. Its import row has `key` and `scope` null when absent, `result` `skipped`, `action` `none`. Print `remember: warning: skipped row <row> in the settings file because it isn't a setting (each row needs a "key").`, where `<row>` is the 1-based position in `settings`. With no row number, print `remember: warning: skipped a row in the settings file because it isn't a setting (each row needs a "key").` (007 ST-7.15, C-14a). Quote that message from `specs/JSON-STYLE.md` §1.7. Do not paraphrase it.

Import also skips three kinds of row, names each skipped row, and writes the rest. Each is the warning `setting_import_skipped`, with `detail` `key`, `scope` and `reason`, and an import row with `result` `skipped`. The name sentences are C-12. Print the ones whose reason words are written. Do not invent the `unknown_key` reason. The three kinds are: a plugin that is not installed (`plugin_not_installed`), the other scope when `--scope` is set (`other_scope`), and a layer the target does not allow (`layer_not_allowed`). This plan has no plugin keys, so the first kind does not fire until plan 25. Implement the skip so plan 25 does not add a second merge.

When the target has no row, write the file row and keep the file's `updated_by`. Do not stamp `{kind: import, id: null}` on a settings row. When the target has a row, compare version vector and `updated_at` only (ST-2.7). A user row has no vector, so the later `updated_at` wins. When `updated_at` ties and `device_id` does not pick a winner, the local row stays. The losing value stays in history.

HTTP and MCP are read-only: `GET /v1/settings` and `GET /v1/settings/{key}`, and MCP tool `remember_settings` with `action` `list`, `get`, or `explain`. No HTTP or MCP call writes a setting. `remember_call` accepts `setting.set`, `setting.reset`, and `setting.import`. Route schemas and HTTP status numbers are DESIGN-PENDING D-10. This plan does not add an HTTP server. It accepts those `remember_call` actions in the library. [2026-11-09: moved to the plan that adds `remember_call`, which does not exist yet (`decision:plan20-remember-call-deferred`).]

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

In: user.db, compiled-in registry, pure resolution, brain and user writes, history cap, credential callback and the CLI runner, `settings export`, import merge at the store. (`remember_call` setting actions moved out on 2026-11-09, `decision:plan20-remember-call-deferred`.)

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

Assert stored rows, codes, exits, the approved COPY-PENDING lines, and the ST-7.11b success JSON. Skip only the `unknown_key` reason words and the `invalid_argument` sentence for `--as` during a hook.

| Test | Proves |
|---|---|
| 007-1 | Set on user and on brain. `get` reports `from` and `layers` in ST-2.2 order. |
| 007-2 | `candidate_pool` is exit 2 `setting_unknown` and the locked did-you-mean line. `remember:search/candidate_pool` is `setting_unknown`. |
| 007-3 | Typed `-` is stored as `_`. |
| 007-4 | `--scope user` on `remember.buckets.prefixes` is `setting_scope_mismatch`. The sentence is C-07. |
| 007-6 | A bare number for a duration is `setting_invalid_value`. |
| 007-7 | `remember.plugins.chain_budget` `45s` is `setting_out_of_range`. The sentence is C-06. |
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
| 007-26 | An older brain row does not replace a newer local row. A skipped row whose reason words are in C-12 uses that line. |
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
6. Add `set` and `reset` storage flags (`--scope`, `--as`, `--actor`, `--clear`, `--yes`). Assert codes, stored rows, the ST-7.11b success `data`, including `action` and `confirmation_required`, and the C-03, C-06, C-07, C-09, C-10, and C-11 lines.
7. Add `settings export` as the ST-8.1 document. Test 007-11 at the file level.
8. Add import merge per ST-8.4 through ST-8.7. Assert that a bad value or an unknown key skips only that row with its warning code, and that the other rows are written. Assert the C-12 lines whose reason words are written. Do not invent the `unknown_key` reason.
9. Add the remaining approved sentences and the success payloads from COPY-PENDING C-03 through C-14. Then tick the matching tests. Leave the `unknown_key` reason words unprinted.
10. Spec (`decision:2026-11-01-legacy-plans-spec-step`). Do this stage last, also when stage 9 has not started, and before `.claude/WHOLE_PLAN_REVIEW`. Merge `origin/main` first, so `specs/` exists. Write `specs/SETTINGS.md`. Describe only the behavior this branch ships: the registry and the key grammar, the layers and the resolution order, the user.db path, history and tombstones, the token callback and the credential helper, the `settings` commands with their flags and JSON fields, and export and import. Each section names the `decision:settings-*` keys it describes (`remember --json list --tag subject:settings`). When no key exists, name the 007 ST section. Do not put a sentence that is still open in COPY-PENDING into the spec. Move the Settings row in `specs/README.md` from "Not written yet" to "Specs". Set `state:implemented` on each `subject:settings` decision this plan built, per `workflow:decision-tags`. This stage has no red test. The whole-plan review checks the spec against the code.

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

Do not invent a sentence, a JSON field, a page size, or a prefix error. A hole is a stop. The approved sentences are in COPY-PENDING.md. A hole that file does not fill is in DESIGN-PENDING.md.

Quality gate: the same commands as plan 19. Coverage stays 100% functions and 100% effective lines.

SQLite only in `store_sqlite.c`. Pure logic in `settings.c` has no I/O. Write CLI text through `app_out` and `app_err` and the diag emitter only.

## Definition of done

- [ ] Stages 1–9 assert the approved sentences they print.
- [ ] The `unknown_key` reason words are unprinted.
- [ ] Stage 9 quotes COPY-PENDING for every sentence it adds.
- [ ] No `config` command, no add-wins, no `remember.search.hybrid`, no plugin budget keys.
- [ ] `specs/SETTINGS.md` describes what this branch ships, and `specs/README.md` lists it (stage 10).
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

No sentence was invented. INDEX row 20 stays unticked.

### Stage 3 — resolution and writes (2026-10-29)

Resolution walks flag, then a live brain row, then a user row, then the registry default. A tombstone is no value. The prefix default, when no row exists, is the JSON `["project:","status:"]`, and nothing is written until a set. The credential helper default is empty. Integer defaults are the registry text (`200`, `20`). Duration defaults are JSON strings of that text (`"1.5s"`, `"10m"`).

A set or reset with no scope writes the user layer, except a brain-only key, which writes the brain. `remember.buckets.prefixes` is that key. An explicit user scope on a brain-only key, or a brain scope on a user-only key, is `setting_scope_mismatch`. The idle-timeout brain sentence is the one locked for that key. Every other scope mismatch returns the code and an empty sentence. Unknown keys (`candidate_pool`, `remember.search.hybrid`) return `setting_unknown` and an empty sentence on set, reset, and read. Did-you-mean stays in the CLI. Invalid values and out-of-range values return their codes and an empty sentence. C-05 and C-06 are still open.

The value column is JSON. An integer is a JSON number. A duration and the helper id are JSON strings of the canonical form. A prefix list is a compact JSON array in input order, after the ST-5.4 fold. The locked prefix sentences are asserted: `"my project:" isn't a bucket prefix.`, `"status:" is in the list twice.`, and `"status:" and "Status:" are the same prefix.`

An omitted actor is stored as `{"kind":"human","id":null}`. A caller-supplied actor string is stored as given. This stage does not check `--as`.

History keeps the newest 50 rows per key per database, ordered by insertion `rowid`. There is no age limit. The test writes 51 user values, checks that `1` is gone and `51` remains, backdates the newest row's `at` to `2020-01-01T00:00:00.000Z`, writes again, and checks that the 2020 row stays while the oldest `new_value` is gone. History rows do not count toward the 256-row quota. A tombstone does.

A set of the value the live layer already stores writes no history and does not bump `change_seq`. A set on a brain tombstone clears `deleted_at` and writes history with `old_value` NULL, even when the kept text matches. A user reset deletes the row. A brain reset sets `deleted_at`, keeps `value`, bumps the local version vector, and bumps `change_seq`. A reset of a missing layer, or of a row that is already a tombstone, changes nothing. The store does not print `(unset)`.

A brain write calls `vv_increment` and `UPDATE brain_meta SET change_seq = change_seq + 1`, then stores that integer on the settings row. It does not call `stamp_change_seq`. A fresh brain starts at `change_seq` 0. The first brain settings write makes it 1. A user write does not bump `change_seq` and does not open a brain write transaction. A read does not call `store_user_db_ensure`. An invalid set does not create user.db. A user set validates first, then ensures.

The row cap is 256 `settings` rows in the brain and in user.db. An update of an existing row, including a tombstone, is allowed at the cap. The brain failure uses `quota_exceeded` and the locked sentence. The user failure uses `quota_exceeded` and an empty sentence. C-09 still holds that prose, and `detail.quota` waits for the JSON stage. The 16 KiB cap is 16384 bytes of the stored JSON. A prefix list past that cap is `quota_exceeded` with an empty sentence on either layer. The brain sentence belongs to the 256-row cap only. The early refuse is `nitems > 16384/5`. 2340 prefixes of length 4 store 16381 bytes. The 2341st is refused in the loop. 2339 of those plus one 8-character prefix is 16385 bytes and is refused after the buffer is filled.

`schema_too_new` is refused by `begin_write` with `SETTING_ERR_STORE` and the locked store sentence (`this brain was written by a newer remember. You can read it. Upgrade to write to it.`). The flag is set at open. The JSON fix stays COPY-PENDING C-01. `setting_status_code` returns an empty string for `SETTING_ERR_STORE`.

A set against a text file at the user path fails in ensure with `cannot read user_version: file is not a database` and leaves the bytes and the mode. A read of that file, and a reset of that file, fail with `sqlite: file is not a database`. The reset reaches that sentence from `BEGIN IMMEDIATE`, because `PRAGMA foreign_keys` and `PRAGMA busy_timeout` do not read the database header. A directory path fails in `sqlite3_open_v2` and is `cannot open database` plus SQLite's own reason. No new sentence was written for it. The test checks that the sentence is non-empty.

`remember_store_tests` links `src/settings.c`. Sanitizer build, `./build/remember_tests ./build/remember --only settings`: 16 tests, 0 failed, 747 asserts. `./build/remember_store_tests`: 491 tests, 0 failed, 19263 asserts. `CI=true BUILD_DIR=build REMEMBER_SCAN_BUILD=1 REMEMBER_IWYU=0 ./scripts/lint-all.sh` exited 0 (IWYU skipped). `./scripts/check-coverage.sh` exited 0: functions 100%, effective lines 100%, `normalize.c` 384 branch sides, 0 missed (`output.c` 678, `search_terms.c` 176, `unicode.c` 80). `settings.c` and `store_sqlite.c` stay on the line gate.

No sentence was invented. INDEX row 20 stays unticked. The definition of done stays unticked. Stages 4–9 have not started.

### Stage 4 — token callback and CLI runner (2026-10-29)

`RememberEnv` holds the three H-0.6 callbacks and one `callback_ctx`. Confirm and dispatch are plan 25. This stage does not call them. `sync_credential.c` is the lookup. It has no process spawn. `cmd_settings.c` is the only translation unit that spawns, and it is the CLI host callback.

The lookup reads `remember.sync.credential_helper`. No row is `SETTING_FROM_DEFAULT` with an empty value, including a missing user.db. That path reads `REMEMBER_SYNC_TOKEN` with ASCII whitespace trimmed. A missing, empty, or whitespace-only variable returns an empty token and does not fail. A control byte or DEL inside, or a value too long for the buffer, returns 2 and prints nothing (C-42). (Changed by the 2026-10-30 second opinion; the first reading kept the variable untrimmed.) The id `env` is the same read, and it does not call the host. Every other id goes to the host `token` callback. A NULL host or a NULL callback can serve only `env`. The engine runs no helper.

A stored value is a JSON string. `json_quote` does not escape, so a grammar id is stored as `"id"`. The id in the message is the unquoted inside when that fits the 41-byte buffer. Otherwise it is the raw stored text. A hand-edited id outside `[a-z][a-z0-9_-]{0,39}` is still a set key: the callback is not called and nothing is spawned. `set` still does not check that a helper exists. The grammar is not narrowed to `env`, `keychain`, and `pass`. Diag maps C0 except tab, and DEL, to `?`.

A set key with no usable token returns 1. The code is `sync_credential_unavailable`. The message is `the sync credential helper "<id>" did not return a token.` The fix is `Check the helper, or set remember.sync.credential_helper to env and export REMEMBER_SYNC_TOKEN.` `diag_error` stays byte-identical and calls `diag_error_fix` with a NULL fix. `diag_error_fix` appends that fix on the plain line and adds a JSON `fix` string after `message`. It does not add `retryable`. The JE-2.6 envelope waits for plan 21. An allocation failure while naming the helper uses the existing `remember: out of memory` line. A store failure returns -1 with the existing store sentence in `err` and does not emit `sync_credential_unavailable`.

The token is absent from stdout, stderr, logs, and JSON. `sync_credential_wipe` clears the caller buffer. Failure wipes before the notice. Success leaves the token in the caller buffer so the test can read it, and the caller wipes. A shorter `REMEMBER_SYNC_TOKEN` also clears the bytes after the new NUL. A token that does not fit the caller buffer is wiped and is not truncated. With no row, that is return 2 and no notice (C-42). With the key set, it is the locked unavailable line, and the secret is not in that line.

The CLI callback runs `remember-credential-<id>` from `PATH`. Argv is that bare name and `get`. There is no shell. Stdin is empty. Stderr is the user stderr. The token is the first stdout line with ASCII whitespace trimmed (space, tab, CR, LF, VT, FF). The process group is `SIGKILL` at 30000 ms. Return -1 when the program is not on `PATH`. Return -2 when it exits non-zero, times out, prints an empty first line, the line does not fit, or exec fails for a reason other than `ENOENT`. A non-grammar id is not spawned. `REMEMBER_TEST_HOOKS` may shorten the deadline. The `remember` binary does not get that hook unless the option is on. The production constant stays 30000. There is no sync command. The tests call the lookup. `tests/credential_helper.c` is an unsanitized Mach-O stand-in named `remember-credential-pass`.

Sanitizer build, `./build/remember_tests ./build/remember --only settings,errors`: 154 tests, 0 failed, 2576 asserts. The store did not change, so `remember_store_tests` was not part of this stage. `CI=true BUILD_DIR=build REMEMBER_SCAN_BUILD=1 REMEMBER_IWYU=0 ./scripts/lint-all.sh` exited 0 (IWYU skipped). `./scripts/check-coverage.sh` exited 0: functions 100%, effective lines 100%, must-cover 0, `normalize.c` 384 branch sides, 0 missed (`output.c` 678, `search_terms.c` 176, `unicode.c` 80). `sync_credential.c` and `cmd_settings.c` stay on the line gate.

No sentence was invented. INDEX row 20 stays unticked. The definition of done stays unticked. Stage 4 is not marked done. Stages 5–9 have not started. Stage 5 waits until plan 21's envelope writer has merged. Linux CI runs on this commit before the push.

### Stage 5 — settings get (2026-11-07)

`remember settings get KEY [--scope brain|user] [--value]` prints the ST-7.3 human layout and the ST-7.11a setting object inside the plan 21 envelope (`setting.get.result`). The branch is `plan/20-stage5-get`. Its merge parent is `fa84e27`; the first parent of that merge is the unmerged plan 21 tip `9dcd717`. Itay overrode the wait in the stage 4 note for this branch. That sentence stays. This section supersedes it.

`--scope=user` is an unknown option. `--` ends options. A key the registry does not have is `setting_unknown`, exit 2, with the C-03 fix `Run 'remember settings list' to see all settings.` One ST-1.5 candidate adds `Did you mean "<key>"?`. Two or more candidates add nothing. Distance is the typed bytes. A colon is not rewritten to a dot. `remember:search/candidate_pool` is the one suggestion. `remember:search:candidate_poo` is not.

A layer the key does not allow is absent from `layers`, including a hand-edited brain row for `remember.sync.credential_helper`. An empty requested layer is `(unset)`, exit 0. JSON `value` is null, `from` is that scope, `changed` is false, and `layers` holds only that layer. `--value` on that shape is also `(unset)`. `--value` is one human line. JSON stays the full object.

The human date is the local ST-7.16 form. The separator between the other layers is U+00B7, the character in the locked 007 example. An actor id is appended only when the canonical actor has one. A stamp that is not a real calendar day omits `on <date>` and JSON `updated_at`, and keeps `set by`. `2024-02-29` and `2000-02-29` are shown. A NUL in `updated_by` or `updated_at`, or a stamp that does not fit the column, fails the read with `internal error`.

`settings` without `get` is `settings takes 'get'`. Set, reset, list, history, explain, export, and import are not this stage. `specs/SETTINGS.md` stays as the stage 10 commit left it. Stage 10 rewrites it after stages 6–9.

No new error sentence. COPY-PENDING C-42 is still the unwritten plural suggestion. INDEX row 20 stays unticked.

### Stage 6 — settings set and reset (2026-11-08)

Another coder wrote the stage in the stage 5 worktree and left it uncommitted. This session reviewed it, applied five grill answers, and committed it as `82ba046`. Plan 21 merged as `9495f48` (PR #58) the same day. `f0608fb` merges `origin/main` by taking main's tree and replaying this plan's own delta (`9dcd717..82ba046`), so the plan 21 review fixes are in.

`remember settings set KEY VALUE... [--scope brain|user] [--as KIND] [--actor ID] [--hooks|--no-hooks]` and `set KEY --clear` write one layer. Without `--scope`, the user layer, unless the key allows only the brain (ST-7.2). `reset KEY`, `reset --namespace NS [--scope] --yes`, and `reset --all --scope X --yes` reset. A brain reset writes a tombstone. A user reset deletes the row. A missing row is `action` `none` and writes nothing. Bulk reset uses `store_setting_list_live` and `store_setting_reset_stored`, so a stored key the registry does not know is reset too (ST-5.11). The two databases are not one transaction: a failure leaves the earlier keys reset.

`REMEMBER_HOOK_DEPTH` 1 or more fails `set` and `reset` with `setting_from_hook` after a clean parse and before key lookup and actor JSON. `--hooks` and `--no-hooks` go through `cmd_take_write_flag` (H-16.11) and change nothing until plan 25. `settings get` rejects them.

Plain `set` prints nothing on success. The C-13 set lines are stage 9. Plain `reset` prints C-10 for each key. JSON is ST-7.11b: `setting.set.result` `{setting, scope, old_value, changed}`, `setting.reset.result` `{setting, scope, old_value, changed, action}`, and with `--yes` `{count, settings[]}`. A stored key the registry does not know has `type`, `value` null and `from` `default` in that list.

Sentences: C-03, C-05 (every row), C-06, C-07, C-09, C-10, C-11, and the ST-7.10 hook line, from COPY-PENDING. The C-43 drafts, from the 2026-11-08 grill: `--scope brain` on a user-only key (`decision:plan20-user-only-scope-draft`, replacing the SETTINGS-SCHEME §5.1 `idle_timeout` line that `settings.c` and `cmd_settings.c` still built); two values or `--clear` on a key that is not a list (`decision:plan20-scalar-shape-draft`); a bulk reset that matches no row asks nothing, and one key is `1 setting` (`decision:plan20-reset-count-zero-one`); string `detail` on `setting_from_hook` `{key?, hook}`, `setting_scope_mismatch` `{key, allowed}` and `setting_out_of_range` `{key, value, min, max}` (`decision:plan20-error-detail-strings`); and the catalog fallbacks of the six new codes. `--namespace` followed by a flag is `missing value for --namespace`.

Not in stage 6, on purpose: the C-13 set lines, `list`, `history`, `explain` (stage 9); `export` (stage 7); `import` (stage 8). `setting_invalid_value` has no `detail` beyond the prefix `item` and `other`: C-05 names no other member.

### Stage 7 — settings export (2026-11-08)

`remember settings export [--scope brain|user|all]` prints the ST-8.1 document (`format` `remember-settings-export`, `version` 1, `exported_at`, `settings[]`) on stdout. Rows sort by key in byte order. On one key, the brain row comes before the user row. A brain row carries `device_id`, the `version_vector` with sorted keys, and `deleted_at`, so a tombstone travels. A user row carries `device_id` `""` and `version_vector` `{}`. `--scope brain` does not resolve the user.db path, so it works without `HOME`.

With `--json`, a success is the same plain document (`app_mark_document`), not an envelope. A failure is the envelope with type `setting.export.result` and an `error` member, and stderr is empty (`decision:plan20-export-json-error`). `remember_run` clears the document mark at the start of each run, so one process can run a plain export and then a `--json` command.

A failed read is `fail_store`. A short stdout is `internal error` with `failed to write output`.

### Stage 8 — settings import (2026-11-08)

`remember settings import FILE [--dry-run] [--scope brain|user] [--hooks|--no-hooks]`. `REMEMBER_HOOK_DEPTH` 1 or more fails with `setting_from_hook` after the parse (ST-7.10). The file is read whole. A file that cannot be read is `import_parse` with the C-43 draft `can't read "<path>": <reason>.` (`decision:plan20-import-parse-draft`).

SQLite checks the document and splits the rows (`json_valid`, `json_each`). Not a settings document: `"<path>" isn't a remember settings file.`, with the catalog fix. A later version: `"<path>" is settings file version <n>; this remember reads version 1.` with `Update remember, then try again.` (`decision:plan20-import-version-draft`). Either way, nothing is written.

Each row is checked in ST-8.2 member order (`decision:plan20-import-bad-field`): shape, then key, scope, `--scope`, layer, value, then `updated_by`, `updated_at`, `device_id`, `version_vector`, `deleted_at`. A user row must have a null `deleted_at` and does not check `device_id` or the vector. A skipped row does not stop the import (ST-8.4). The warnings:

- `bad_shape` is C-14a.
- `invalid_value` is C-14. ` This brain wasn't changed.` is added only when the file has one row.
- `other_scope` and `layer_not_allowed` are C-12.
- `unknown_key` is C-12 with the C-43 draft reason `remember has no setting by that name` (`decision:plan20-import-unknown-key-draft`, Itay 2026-11-08). This answer overrides the "unprinted" line in the definition of done.
- `bad_field` is the C-43 draft `skipped "<key>": its "<field>" isn't valid.` (`setting_invalid`).

A value is checked by `setting_prepare`, the same check `set` uses. A key, value, or list item that holds a NUL is not that string, so it is invalid.

Merge (ST-8.6, ST-2.7): the version vector first (brain only), then the later `updated_at`, then the larger `device_id` (brain only). A tie keeps the local row (`kept_local`, counted as `unchanged`). A row equal to the local row is `unchanged` and writes nothing. A brain write stores the file's fields with the pointwise-max vector, does not bump the local counter, and bumps `change_seq` (`decision:plan20-import-brain-merge`). Every write adds one history row with the file actor and the file `updated_at` (`decision:plan20-import-history-at`). A tombstone writes a null new value. A local tombstone gives a null old value. A new key over the 256-row quota fails the whole import with the C-09 sentence of that layer, and nothing is written (`decision:plan20-import-quota`).

The brain and user.db each run one transaction. The brain commits first. If user.db's `COMMIT` fails after that, the brain rows stay written and the command fails. This is the stage 6 bulk-reset limit: two files do not commit as one. [2026-11-09: superseded for import. A file with both scopes needs `--scope`, so a run writes one database (`decision:plan20-import-one-file-per-run`).] `--dry-run` runs the same merge, rolls both back, and does not create user.db. It prints `Would import "<path>": <n> settings written, <n> unchanged, <n> skipped. Nothing was changed.` (`decision:plan20-import-dry-run-draft`). A real run prints the C-13 `Imported` line. Warnings come first.

JSON `setting.import.result` is ST-7.11b: `{count, written, unchanged, skipped, rows[]}`. Each row is `{key, scope, result, action, reason?}`. `key` and `scope` are null when the file row has none. A warning's `detail` writes a missing value as `null`.

### Stage 9 — set lines, list, history, explain (2026-11-08)

Plain `set` prints the C-13 lines: `"<key>" is now <v> for this brain.` or `for your user.`, and `"<key>" is already <v>. Nothing changed.` for a no-op. An empty list prints `(empty)` (`decision:plan20-empty-list-related`).

`remember settings list [PREFIX] [--namespace NS] [--scope brain|user] [--changed] [--limit N] [--cursor C]` prints `<key> = <value>   (<layer>)` for each registry key in byte order (ST-7.14). `--scope` shows that layer only and `(unset)` when it has no row. `--changed` keeps keys whose shown value is not the default. JSON `setting.list.result` is `{count, settings[]}`, each the `get` setting object, with the page cursor in the envelope. The default limit is 100. Six keys cannot reach it, so no test pages at the default.

`remember settings history [KEY] [--scope brain|user] [--limit N] [--cursor C]` reads both databases newest first (ST-7.6). On one `changed_at`, brain comes before user, then key in byte order, then the newer row. `store_setting_history` asks each database for `limit + 1` rows from the cursor, merges, and keeps `limit + 1`, so the extra row says there is a next page. The plain line is `<when>  <scope>  <key>  <old> → <new>  by <actor>`. `<when>` is the stage 5 date form, or the raw stamp when it does not parse. JSON `setting.history.result` is `{count, changes[]}`. Each change is `{key, scope, old_value, new_value, updated_by, changed_at, action}` with `_ms` twins for durations (007-32). `action` is `set`, `tombstone` (brain) or `delete` (user). A key the registry does not know shows its stored text as a JSON string. The default limit is 50. The undo command 007-32 mentions is not this plan.

`remember settings explain KEY|--all` prints the block from `decision:plan20-explain-human-form`, with the registry `description` and `related` (`decision:plan20-explain-description-drafts`). JSON `setting.explain.result` is `{count, entries[]}`, each the `get` setting object plus `description`, `scopes`, `default`, `min`, `max` and `related` (`decision:plan20-explain-json-shape`).

`--limit` is 1 to 1000. Out of range is `setting_out_of_range` with detail `{flag, value, min, max}` (`decision:plan20-list-limit-draft`). The cursor is base64url JSON. It holds the last row and every filter (`decision:plan20-cursor-carries-filters`). An omitted filter is taken from the cursor. The cursor is rebuilt from the decoded members and the given filters and compared byte for byte, so a changed filter, a forged member or another verb's cursor is `invalid_argument` (`decision:plan20-cursor-fix-draft`). The plain more-pages line is on stderr, through the new `diag_notice` (the diag boundary keeps `app_err()` in `diag.c`). `base64url_encode` and `base64url_decode` moved from `cmd_query.c` to `commands_common.c`, which now includes `test_alloc.h`, so the sweeps reach the cursor's allocations.

### Stage 10 — spec (2026-11-09)

`origin/main` (`9495f48`) was already in the branch through `f0608fb`. The fetch on 2026-11-09 found nothing newer, so no merge was needed. `specs/SETTINGS.md` describes what this branch ships: the registry, key and value grammar, layers and resolution, the brain table and user.db, history and tombstones, the sync token and the CLI helper, every `settings` verb with its flags and JSON `data`, paging, export, import, hooks and the error codes. Each section names its `decision:*` keys or the 007 ST section. No C-42 or C-43 sentence is quoted. The Settings row moved to the Specs table in `specs/README.md`.

`state:implemented` is set on 36 `subject:settings` decisions: the 21 `plan20-*` keys, `settings-scope`, `settings-scope-flag`, `settings-set-default-layer`, `s-02`, `s-04`, `s-12`, `g-02`, `g-14`, `g-16`, `g-25`, `g2-12`, `q4-01`, `q4-02`, `q4-04` and `r3-00a`. Left `state:accepted` on purpose: `settings-scheme` (plugin manifest settings are plan 25, and its all-or-nothing import is changed by `g-16`), `settings-export-scope` (the brain export is not this plan), `g2-13` (confirm and dispatch are plan 25), `g-34` (17 mechanics this note does not list one by one), and `s-03` (it says at most 8 prefixes; the code has no item count, see the stage 9 review). The plugin, agent, MCP and HTTP decisions are other plans.

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

No new sentence. INDEX row 20 stays unticked.

GitHub CodeQL on `be4beb2` flagged `chmod` on the text-file path after `close` as a time-of-check race. The test now `fchmod`s the descriptor it still holds, and reads the mode back with `fstat` on the descriptor it opens to check the bytes.

**2026-10-29, stage 3 deep review.** In-session, on `plan/20-stage3-writes` before its commit. Itay said to proceed with the next stage. This note records that review. It does not mark stage 3 done. A second Claude opinion did not happen. Local Bugbot is not installed.

| # | Severity | Finding | Outcome |
|---|---|---|---|
| 1 | important | `store_setting_reset` returned from `open_user_existing` on failure, and that return had 0 hits. A text file at the user path gets past `sqlite3_open_v2` and past the two pragmas, because those pragmas do not read the header. `BEGIN IMMEDIATE` is what reports `sqlite: file is not a database`. | Fixed. The reset of the temp directory, the same path the read already rejects, takes the open-failure return. |
| 2 | important | The injected `BEGIN` failure was ordered after `write_notes` replaced user.db. The next set cannot recreate a non-database file, so that assertion could not pass. | Fixed. The `BEGIN` injection runs while user.db is still a database. `write_notes` runs after it. |

Confirmed by running, sanitizer build. `./build/remember_tests ./build/remember --only settings`: 16 tests, 0 failed, 747 asserts. `./build/remember_store_tests`: 491 tests, 0 failed, 19263 asserts. `lint-all.sh` with `REMEMBER_IWYU=0` exited 0. `check-coverage.sh` exited 0: functions 100%, effective lines 100%, `normalize.c` 384 branch sides, 0 missed. The first coverage run on this stage named `store_sqlite.c` lines 9984 and 9985, the open-failure return in `store_setting_reset`. The rerun after the directory reset exited 0.

Mutant, then the tree restored and the settings suite passed again (16 tests, 747 asserts). Treating every failed user.db open as a missing file makes `programmer_errors_and_a_failed_prepare` fail `ASSERT_EQ_INT(actual=0, expected=6)` on the directory read and on the directory reset.

Simplification. Applied: the unreachable quota returns in `canon_integer`, `canon_duration`, and `canon_helper` (the caller passes `SETTING_VALUE_CAP`, and those forms fit); a NULL source in `copy_cap` shares the existing false return; `open_user_write`'s sqlite-open failure uses the same NULL-safe `sqlite3_errmsg` and `sqlite3_close` as `store_open`, which the coverage script already treats as defensive; `canon_prefixes` and `mutate_setting` were split so each function stays under cognitive complexity 25. Declined: replacing the `goto` in `mutate_setting` (the rollback labels match the rest of this file); sending the settings counter through `stamp_change_seq` (that helper stamps entries and links; a settings write adds one to `brain_meta.change_seq` and copies that integer onto the row); one opener for a user.db read and a user.db write (a write calls `store_user_db_ensure`; a read of a missing file is an absent layer and must leave the path absent).

No product or architecture question. The path-reopen window recorded in the stage 2 note is unchanged.

INDEX row 20 stays unticked. The definition of done stays unticked. Stages 4–9 have not started.

**2026-10-29, stage 4 deep review.** In-session, on `plan/20-stage4-token` before its commit. This note records that review. It does not mark stage 4 done. The second opinion has not happened. Local Bugbot is not installed. Stage 3's second opinion did not happen.

| # | Severity | Finding | Outcome |
|---|---|---|---|
| 1 | important | `take_env_token` copied `REMEMBER_SYNC_TOKEN` and left the rest of the caller buffer. A shorter token kept the preloaded bytes after the NUL. | Fixed. When the buffer is longer than the token, the bytes after the NUL are wiped. `no_row_reads_remember_sync_token` checks that no `Z` remains after the NUL. A mutant that deletes that wipe fails the assert at `tests/test_settings.c:1770`. The tree was restored. `./build/remember_tests ./build/remember --only settings,errors`: 154 tests, 0 failed, 2576 asserts. |
| 2 | important | Linux cppcheck reported `resourceLeak` on `errf` and `outf` at the end of `cli_runner_reads_the_trimmed_first_line`. The success path closed them inside `ASSERT_EQ_INT`, which that cppcheck does not treat as a close. macOS cppcheck had not reported it. | Fixed. `fclose` is a statement, the pointers are cleared, and the saved results are asserted. |

Confirmed by running, sanitizer build, the gate in the stage 4 implementation note. After the restore and before that combined run, `./build/remember_tests ./build/remember --only settings` was 48 tests, 0 failed, 1117 asserts.

No product or architecture question was opened. The readings in the implementation note stay: a JSON `fix` string and no `retryable` (plan 21 owns the envelope); `RememberEnv` keeps the unused plan 25 pointers; the deadline is the 30000 ms constant, and `REMEMBER_TEST_HOOKS` may shorten it; an unquoted stored value is shown raw; `set` does not check that a helper exists; the grammar stays `[a-z][a-z0-9_-]{0,39}`.

Simplification. Applied earlier in the stage: `read_first_line` was split so the function stays under cognitive complexity 25, and the runner tests share one fixture. Declined: one save/restore helper for every `getenv` in the settings tests (the variables differ, and the runner tests already share a fixture); a larger stdout read, which would be a second secret buffer; closing every inherited descriptor (the law names stdin, stdout, stderr, and the process group).

INDEX row 20 stays unticked. The definition of done stays unticked. Stage 4 is not marked done. Stages 5–9 have not started.

**2026-10-30, stage 4 second opinion (Claude).** On `9cfaba6`, fixed on `review/plan20-stage4`. Local Bugbot is not installed. Stage 3 had no second opinion, so this pass also read the stage 3 resolution code that the lookup relies on. Itay answered three questions (rows 6–8).

| # | Severity | Finding | Outcome |
|---|---|---|---|
| 1 | critical | `setting_pick` did not check the key's allowed layers. A brain row for `remember.sync.credential_helper`, which is user only, beat the user row. That row can come from sync, a peer brain, or a hand edit. So the brain chose which `remember-credential-<id>` the CLI runs. A brain row for `remember.plugins.chain_budget` also resolved. Probe before the fix: the host saw `evil`, not `pass`, and `chain_budget` came from the brain as `"30s"`. | Fixed. A row in a layer the key does not allow is no value. Tests: `pick_ignores_a_layer_the_key_does_not_allow` (both sides) and `brain_row_does_not_pick_the_credential_helper`. Mutants: removing `allow_brain` turns 4 pick asserts and the helper test red. Removing `allow_user` turns 2 pick asserts red. Stage 5 must still leave a disallowed layer out of `layers`. |
| 2 | important | Nothing pinned the runner's wipe of the moved-from tail after the trim. With the wipe replaced by a NUL write, the suite had 0 failures, and the bytes after the token kept the end of `  secret  `. | Fixed. `cli_runner_reads_the_trimmed_first_line` asserts that every byte after the NUL is zero. The same mutant now fails it. |
| 3 | important (DRY) | `ascii_space` and the two loops in `trim_line` copied `ascii_ws_trim`, which these notes call the one ASCII trim. | Fixed. `trim_line` calls `ascii_ws_trim`. One wipe from the new end writes the NUL and clears the tail. The control-byte check reuses `is_ascii_control`, now public in `normalize.h`. |
| 4 | improvement | Test duplication. The `REMEMBER_SYNC_TOKEN` save block had 8 copies and the restore had 10. `CredIn` copied `SyncCredentialLookup` field for field. Four tests repeated `cred_lookup` inline. The stage note declined a helper because "the variables differ". For these sites they did not. | Fixed. Added `save_sync_token` and `restore_sync_token`. `cred_lookup` now takes the lookup struct. The fold removed 204 lines before the new tests. |
| 5 | nit | `take_env_token` wrote the NUL and wiped the tail in two steps. `read_first_line` ended with a compare, then `return 0` / `return -2`. | Fixed. One wipe from the end, and `return drain_stdout(...)`. |
| 6 | grill → fixed | With no row, a `REMEMBER_SYNC_TOKEN` too long for the buffer gave success with no token. That fails silently. | Itay: fail, with the sentence pending. The lookup returns 2 and prints nothing. COPY-PENDING C-42 holds the sentence. |
| 7 | grill → fixed | Token bytes were not checked. A NUL in the helper line cut the token. CR, LF, ESC and DEL passed through to the future header writer. | Itay: reject at the lookup. The lookup owns the rule that no token has a C0 control or DEL, from env or a callback. The runner refuses a NUL in the line, because the lookup cannot see one. Tests: `remember_sync_token_is_trimmed_and_checked`, `callback_token_with_a_control_byte_is_unavailable`, `nul_in_the_first_line_is_lookup_failed`. |
| 8 | grill → fixed | `REMEMBER_SYNC_TOKEN` was used untrimmed, while the helper line was trimmed. | Itay: trim env too. `ascii_ws_trim` runs on the variable. Whitespace only counts as no token. The trimmed length is what must fit (` hello ` fits a 6-byte buffer). |
| 9 | nit | The coverage gate treats a `goto fail;` line as defensive by its text. The read-error `goto` in `read_first_line` has 0 hits. As `return -2;` it is a must-cover line. | Kept as `goto`. A read on a poll-ready blocking pipe fails only on EIO or EBADF. Recorded here so the exclusion is not mistaken for a test (round 9.3). |
| 10 | improvement (DRY), deferred | `emit_unavailable` measures, allocates and formats the way `err_fmt` does. | `TODO(pragmatic:)` at the function. Give `err_fmt` a fix argument when stage 9 adds more fix sentences. |
| 11 | nit, not fixed | A closed fd 0, 1 or 2 at start can make `pipe()` reuse that number. The file actions then close the child's stdin or stdout. The result is fail-closed (`-2`), with no leak. An empty `PATH` entry searches the cwd, the same as git's `credential.helper`. | Recorded. Revisit when a sync command calls the runner. |

Confirmed by running, sanitizer build with the CI flags (`-DREMEMBER_TEST_VFS=ON`). The coder's gate reproduced on `9cfaba6`: 154 tests, 2576 asserts. Mutants, one clause each, every build checked for success, tree restored after each: dropping the trim, the whitespace-only check, the trimmed-length check, DEL, C0, return 2, the env-id `!= 1`, the callback byte check, the env byte check, and the NUL refusal. All 10 turned at least one named test red. A 64-byte message buffer fails `unavailable_echo_stays_one_line_in_both_formats`. That test proves a newline and an ESC in a hand-edited id stay on one plain line, are escaped in JSON (`\n`, `\u001b`), and that a 300-byte id is not cut.

Simplification. Applied: rows 3, 4 and 5 (`cmd_settings.c` −26 lines, tests −204 before the new ones). Declined: the `if … return true; return false;` predicates. They match `normalize.c`, and the line gate sees each branch.

Gate on the committed tree. Sanitizer build: `./build/remember_tests ./build/remember --only settings,errors` gave 159 tests, 0 failed, 2679 asserts, and `ctest --test-dir build` passed 5 of 5. `CI=true BUILD_DIR=build REMEMBER_SCAN_BUILD=1 REMEMBER_IWYU=0 ./scripts/lint-all.sh` exited 0. `./scripts/check-coverage.sh` exited 0: functions 100%, effective lines 100%, `normalize.c` 384 branch sides, 0 missed. `./scripts/ci-linux.sh` exited 0. Its first run caught a NULL read in the new `count_char` test helper (clang-tidy 18), and that helper is now NULL-safe.

Stage 4 is done. INDEX row 20 stays unticked. The definition of done stays unticked. Stage 5 waits until plan 21's envelope writer has merged.

**2026-11-07, stage 5 deep review.** In-session, on `plan/20-stage5-get` before its commit. The branch is based on the unmerged plan 21 tip `9dcd717` (merge `fa84e27`). Itay overrode the wait. This note supersedes the two stage 4 sentences that say stage 5 waits. Those sentences stay. Local Bugbot is not installed. `decision:plan21-stage-flow` says no per-stage `.claude/REVIEW_REQUEST`. This note does not mark the plan done.

| # | Severity | Finding | Outcome |
|---|---|---|---|
| 1 | — | No defect in the get path, the suggestion, or `load_stamp` needed a change after the gate. | Readings below stay. |

Readings a later stage must not undo:

- The other-layer separator is U+00B7 (` \xC2\xB7 `). That disagrees with `style:user-strings-ascii`. The locked 007 example wins.
- More than one key within distance 2 returns no suggestion and uses the C-03 list fix. There is no plural sentence. COPY-PENDING C-42 stays unwritten. The header still says nothing is pending.
- ST-1.5 and `decision:s-12` measure the typed bytes. The colon-to-dot rewrite is gone. `remember:search/candidate_pool` is still the unique distance-2 hit. `remember:search:candidate_poo` is distance 3 and returns NULL.
- A shape-valid impossible date (`2026-02-31`, `2026-02-29`, `2026-04-31`, `1900-02-29`) omits the human date and JSON `updated_at` and keeps `set by`. `2024-02-29` and `2000-02-29` are shown. `utc_stamp_is_canonical` stays a shape check.
- A bad actor omits `set by` and the human date. JSON omits `updated_by` and `updated_at`.
- Stage 4's review required a disallowed layer to stay out of `layers`. `get_credential_helper_omits_a_disallowed_brain_row` pins that: no `evil`, no `brain` member.
- `changed` is true when an allowed layer is live. With `--scope`, only that layer counts. An empty scoped get is `(unset)` even with `--value`.
- M-24 on get: an unknown key that starts with `plugin.` and ends with `.budget` or `.cold_start_grace` keeps the `setting_unknown` line and uses the manifest fix. That fix is not the catalog default.

Zero-hit lines in the touched files are defensive. Triggers, with no new test:

- `from_token` returns `"flag"`. Get never produces a flag layer.
- An empty list `[]` takes `array_plain`'s `n == 2` return and `fill_list_display`'s `return 0`. A hand edit only. Set is stage 6. The human value would be empty.
- `write_others` returns 0 when no other layer remains. Every current key still has one non-winner line.
- `cmd_settings_reply` returns `"unknown"`. `settings` without `get` hits that branch. The literal return is excluded.
- `load_stamp`'s `SQLITE_DONE`, NULL text, and negative `column_bytes` arms. The columns are `TEXT NOT NULL`. The NUL and the 32-byte stamp hit the same `return SETTING_ERR_STORE`.
- `fill_layer_json` returns without marking the layer live when the value does not fit `SETTING_VALUE_CAP`. A hand-edited overlong value. The existing stage 3 path.

Mutants, one clause each, then the tree restored. The first pass killed: dropping the leap-day assignment (1 fail in `get_shows_a_leap_day_and_omits_an_impossible_date`); the brain-layer guard (3 fails); `dist <= 1` (2 fails); treating `--` as not ending options (1 fail); accepting a NUL in `updated_by` (1 fail); `has_id = false` (1 fail); treating a colon as a dot (1 fail). Two mutants did not compile. Replacing `days_in_month` with `part->day > 31` left `days_in_month` unused. `if (false && opt->scope_set)` tripped a constant warning. The compiling replacements, on this tree: dropping `year % 400 == 0` while still calling `days_in_month` makes `2000-02-29` omit the date and the JSON stamp (2 asserts, `get_shows_a_leap_day_and_omits_an_impossible_date`). Deleting the duplicate `--scope` return makes that invocation exit 0 with an empty stderr (2 asserts, `get_usage_rejects_a_bad_invocation`). Both killed. `cmd_settings.c` was copied back and compared. `./build/remember_tests ./build/remember --only settings` on the restored tree: 79 tests, 0 failed, 1741 asserts.

Simplification. Declined: sharing `days_in_month` with `commands_common.c` (that copy is static); a plural did-you-mean sentence (C-42 is unwritten); putting the colon rewrite back.

Gate on this tree, before the mutation pass. Sanitizer build. `CI=true BUILD_DIR=build REMEMBER_SCAN_BUILD=1 REMEMBER_IWYU=0 ./scripts/lint-all.sh` exited 0 (`LINT OK`; IWYU skipped, Linux CI runs it). `./scripts/check-coverage.sh` exited 0: functions 100%, effective lines 100%, zero-hit 2286, defensive-excluded 2286, must-cover 0. Pure-TU branches, 0 missed: `search_terms.c` 176, `unicode.c` 80, `normalize.c` 504, `output.c` 736. `ctest --test-dir build` passed 6 of 6. The settings suite above is the same 79 tests and 1741 asserts.

Stage 5 get is done on this branch. INDEX row 20 stays unticked. The definition of done stays unticked. Stages 6–9 have not started. Stage 10 still rewrites `specs/SETTINGS.md` after them. Plan 21 tip `9dcd717` is not on origin/main.

**2026-11-08, stage 6 deep review.** In-session, on the other coder's uncommitted work in the stage 5 worktree, before `82ba046`. Itay asked to review what was done, deep review it, and finish the plan. `decision:plan21-stage-flow`: no per-stage `.claude/REVIEW_REQUEST`. Local Bugbot is not installed.

| # | Severity | Finding | Outcome |
|---|---|---|---|
| 1 | critical | `report_scope` printed a sentence the copy files do not have for `--scope brain` on a user-only key, and `settings.c` still built the SETTINGS-SCHEME §5.1 `idle_timeout` line (`k_idle_scope`). | Grilled. Draft in C-43 (`decision:plan20-user-only-scope-draft`) for every user-only key. `k_idle_scope` and the `err` argument of `setting_layer_of` are gone. |
| 2 | critical | Two values, or `--clear`, on a key that is not a list printed `remember: ` and nothing else. A test locked that empty line. | Grilled. Drafts in C-43 (`decision:plan20-scalar-shape-draft`). The test asserts the drafts. |
| 3 | important | A bulk reset that matched no row asked for confirmation of `0 settings`. One key printed `1 settings`. | Grilled (`decision:plan20-reset-count-zero-one`). No row: no confirmation, `count` 0. One key: `this resets 1 setting.` (C-43 draft). |
| 4 | important | `--namespace --yes` took `--yes` as the namespace. | Fixed. A value that starts with `-` is `missing value for --namespace`. |
| 5 | important | `set` and `reset` rejected `--hooks` and `--no-hooks`. H-16.11 says every write takes them. | Fixed. Both go through `cmd_take_write_flag`. `set_and_reset_take_the_hook_flags`. |
| 6 | important | Error `detail` was missing on `setting_from_hook`, `setting_scope_mismatch`, and `setting_out_of_range`. | Grilled (`decision:plan20-error-detail-strings`): string members. `{key?, hook}`, `{key, allowed}`, `{key, value, min, max}`. Listed in C-43. |
| 7 | nit | No test showed that a `set` after a brain reset has no `old_value` (a tombstone is not a value). | Added `set_after_a_brain_reset_has_no_old_value`. |

Mutants, one clause each, settings suite, tree restored after each: 22 mutants over the new set and reset code. 20 killed. Four did not compile on the first pass; their compiling forms in a second pass were all killed. Equivalent (2): `report_scope`'s `!spec->allow_brain` check (the first branch already took every key that allows the brain), and `copy_live`'s `!live` check (a layer that is not live has an empty text, which the same `if` rejects). Both checks stay, because they state the rule at the line that reads it.

Simplification. Applied: `emit_scope` derives `allowed` from the spec instead of taking it as an argument. Declined: one parser for `set` and `reset` options (they share `take_write_scope` and `take_write_actor`; the rest differs).

Gate on `f0608fb` plus the review fixes, sanitizer build: the CLI suite passed, `lint-all.sh` exited 0, `check-coverage.sh` exited 0 after OOM sweeps were added for the three draft paths and `copy_msg` moved to `snprintf`. The second-parent merge of `origin/main` is recorded in the stage 6 implementation note.

INDEX row 20 stays unticked. Stage 6 is done on this branch.

**2026-11-08, stages 7 and 8 deep review.** In-session, on the uncommitted stage 7 and stage 8 work, before the commit. `decision:plan21-stage-flow`: no per-stage `.claude/REVIEW_REQUEST`.

| # | Severity | Finding | Outcome |
|---|---|---|---|
| 1 | important | Under `--json`, the import parse errors had no fix sentence. | Fixed. They pass `diag_code_fix(DIAG_IMPORT_PARSE)`. |
| 2 | important | `export_without_home_fails_unless_the_scope_is_brain` leaked `REMEMBER_TEST_FIXED_IDS`. Three envelope tests then failed on UNIQUE ids. | Fixed. The test unsets it before `env_release`. |
| 3 | important | The file reader called `fread` again after a short read, which is EOF or an error (analyzer `unix.Stream`). | Fixed. A short read ends the loop. |
| 4 | important | The exec fault sweep found a failed user.db `COMMIT` after the brain committed. Two brain rows stay written, and the command fails. | Kept, and recorded in the stage 8 note. This is the stage 6 two-file limit. `import_survives_every_store_fault` asserts it. |
| 5 | nit | Untested: a NUL in a key, a value, or a list item; a non-string list item; a local brain tombstone under an import; the document mark across two runs in one process. | Added to `import_skips_bad_shapes_and_bad_fields` and `import_survives_every_store_fault`. New tests: `import_over_a_local_tombstone_has_no_old_value`, `a_plain_export_does_not_mark_the_next_json_run`. |
| 6 | nit | The test allocator hooks only `malloc`, so the reader's `realloc` failure could not be reached. | The reader grows with `malloc` and `memcpy`. `import_reports_home_store_and_memory_failures` sweeps it, and a 6 KiB file covers the copy. |

Mutants, one clause each, settings suite, in a copy of the tree: 33 over export and import. 31 killed. Two did not compile on the first pass (a constant condition, an unused helper); their compiling forms were killed. Covered: the export layer order and vector key order; the document-mark reset; each merge rule (vector, time, device, tie, device on user rows); the one-row sentence; `kept_local` counting; dry-run commit; quota; the tombstone old value; the user `deleted_at` check; `other_scope`; file row order; the tombstone flag on user rows; the `change_seq` bump; `same` (actor, device, deleted, vector); the device and vector validators; dry run creating user.db. No equivalent mutant.

Simplification. Declined: one reader for import and plan 21's `--from-db` (different limits and errors); merging `import_same` into `import_file_wins` (an equal row writes nothing and is `unchanged`, not `kept_local`).

Gate on this tree, sanitizer build: ctest passed 6 of 6, `check-coverage.sh` exited 0 (functions 100%, effective lines 100%, zero-hit 2384, defensive-excluded 2384, must-cover 0, pure-TU branches 0 missed), `lint-all.sh` exited 0 (`LINT OK`; IWYU skipped, Linux CI runs it), `leaks-macos` passed. Settings suite: 137 tests, 5297 asserts, 0 failed.

INDEX row 20 stays unticked. Stages 7 and 8 are done on this branch.

**2026-11-09, stage 9 deep review.** In-session, on the uncommitted stage 9 work, before the commit. `decision:plan21-stage-flow`.

| # | Severity | Finding | Outcome |
|---|---|---|---|
| 1 | important | The cursor's base64url code moved to `commands_common.c`, which the test allocator did not hook, so no test reached its out-of-memory paths. | Fixed. `commands_common.c` includes `test_alloc.h`. The full suite still passes with the hook. `views_survive_memory_store_and_write_faults` sweeps memory, store prepare, step and allocation, and stdout faults over every view. |
| 2 | important | A history store result could hold one row more than `want` without any test failing (mutant M7): the CLI shows `limit` rows, so the extra row was hidden. | Fixed. `view_cursor_edges_and_home` asks the store for one row from two databases and asserts one. |
| 3 | nit | Three failure branches cannot happen: the registry clamp at 64 keys, a list cursor that does not fit (keys and filters that matched a key always fit), and `fill_display` after `values_ok`. | Kept as checks in the defensive form, each with a comment. The `--json` writes go to the envelope buffer, which fails only out of memory. They are one-line ternaries. |
| 4 | nit | Untested: a list cursor that keeps a prefix and `--changed`, a forged cursor with other member names or a stamp that cannot be rebuilt, a history cursor reused with another `--scope`, a stored stamp too long for a cursor, and a missing `HOME` on the three views. | Added to `view_cursor_edges_and_home`. |
| 5 | nit | `cursor_split` was over the complexity limit and incremented inside conditions. | Split into `split_take`, `split_name`, `value_ends` and `split_value`. Same accepted input. |
| 6 | important | The plain more-pages line called `fprintf(app_err(), …)` from `cmd_settings.c`, which the lint diag boundary forbids. The gate log showed only the last 25 lint lines, so this and three cppcheck notes were first missed. | Fixed. `diag_notice` in `diag.c` writes the line. cppcheck: a local renamed in `base64url_decode`, the bound buffers moved into their block, and the registry array initialised. The gate now prints every lint verdict. |
| 7 | question | `decision:s-03` says `remember.buckets.prefixes` holds at most 8 items. The code (stage 3) caps the list at 16384 bytes of JSON and has no item count. | Not changed here. The spec describes the code. Raised for the whole-plan review. |

Mutants, one clause each, settings suite, tree restored after each: 24 over the history store (sort, tie mode, SQL bounds, layer filter, merge truncation) and the views (limit range, `want`, scope filter, page cut, cursor filter adoption, cursor compare, registry sort, explain range, history action, set lines, date fallback, explain scopes). All 24 killed. M7 survived the first pass and was killed by finding 2. Two did not apply on the first pass (one no longer matched after the lint rewrite, one did not compile). Their compiling forms were killed. No equivalent mutant.

Simplification. Applied: `publish_page` reports a failed page copy the way `cmd_query.c` does. Declined: one option parser for `list`, `history` and `explain` (they share `take_layer`, `take_limit` and `take_flag_value`; the rest differs).

Gate on this tree, sanitizer build: ctest passed 6 of 6, `check-coverage.sh` exited 0 (functions 100%, effective lines 100%, zero-hit 2431, defensive-excluded 2431, must-cover 0, pure-TU branches 0 missed), `lint-all.sh` exited 0 (every check `PASS`, `LINT OK`; IWYU skipped, Linux CI runs it), `leaks-macos` passed. Settings suite: 146 tests, 21051 asserts, 0 failed.

INDEX row 20 stays unticked. Stage 9 is done on this branch. C-43 holds the stage 9 drafts.

**2026-11-09, whole-plan deep review (LEGACY PLAN).** A separate session, on `plan/20-stage5-get` at `1a5ee6b`, base `origin/main` `9495f48`. It followed `workflow:whole-plan-deep-review`, `workflow:deep-review` and the keys they cite, `pref:review-lenses` (each lens as its own pass), `pref:code-review-workflow`, and `workflow:pragmatic-review` for lens 4. Local Bugbot is not installed. Itay answered five grills (D1, A3, D2, D6, and the D6 copy). This note does not tick INDEX row 20 or the definition of done.

Open items from `.claude/WHOLE_PLAN_REVIEW`:
1. `decision:s-03`: amended in Remember. The prefix list has no item limit; the cap is 16384 bytes of JSON (D-01, Itay 2026-10-25). Tagged `state:implemented`.
2. `decision:settings-scheme`: amended so import skips bad rows (g-16, ST-8.4) and `set --brain` is `--scope brain`. It cites g-16. It stays `state:accepted` for the plan 25 manifest part.
3. `decision:g-34`: a status line per item. It stays `state:accepted` until items 13 and 17 ship.
4. C-42 and C-43 stay open. C-43 gains the two drafts below.
5. Two-file limit: closed for import by D6. Bulk reset keeps it (stage 6 note).
6. INDEX row 20 and the definition of done stay unticked.

Security pass (lens A). Each finding has a probe or a test.

| # | Severity | Finding | Outcome |
|---|---|---|---|
| A1 | important | `get`, `reset`, `list`, `export`, `history` and `import` opened user.db by path and followed a symlink. Only `set` refused one. A probe reset rows in the link's target. The spec's "opened without following a symlink" was false for six verbs. | Fixed. `open_user_existing` checks the path with `open(O_NOFOLLOW)` first. `SQLITE_OPEN_NOFOLLOW` was not used: it refuses a symlink in any component, which breaks `/tmp` and `/var` on macOS. `user_db_symlink_is_refused_by_every_opener`. A check-then-open window remains, as in `store_user_db_ensure`; the 0700 parent is the guard. |
| A2 | important | The C-10 reset line printed a stored value or key raw. A hostile synced brain row `300\x1b[2J` cleared the terminal. | Fixed. The line goes through `output_text_human`. `reset_and_set_never_echo_a_stored_value_raw`. |
| A10 | important | `old_value` in the set and reset envelopes was written raw. A user.db value `1,"injected":true` added a JSON member; `1}]` made the envelope invalid. | Fixed. `write_old_json` writes raw only text `json_text_ok` accepts for the key's type, else a JSON string. An unregistered key's old value is a string, as in history. Same test. |
| A3 | improvement | `settings import /dev/zero` grew memory until malloc failed. | Fixed per grill: a file over 16 MiB (`REMEMBER_STDIN_MAX`) is `can't read "<path>": File too large.` (`decision:plan20-import-size-cap`). A file of exactly 16 MiB is read. |
| A4 | improvement | A canonical but wide `version_vector` (over 4095 bytes) failed the whole import with `internal error`, against ST-8.4. | Fixed. It is `bad_field` `version_vector`. `import_skips_a_vector_too_wide_to_merge` uses a vector of exactly 4096 bytes. A merge with a wide local vector that overflows is still a store error; recorded, not reachable from one import file. |
| A6 | nit | One malformed stored row makes `get`, `list`, `explain` and `history --json` fail with `internal error`. | Recorded. It fails closed. |
| A7 | nit | The credential runner left the last token byte in a stack local. | Fixed. `byte` is wiped on both exits. No test can see a stack slot. |
| A8 | nit | The history plain line used a fixed 33 KB stack buffer and cut a long stored value. | Fixed. The line streams. `history_prints_a_long_stored_row_whole`. |
| A9 | improvement | The prefix error was parsed back out of an 8 KiB sentence. A 9000-byte item gave a cut sentence, the wrong fix and no detail; an item with `"` gave a wrong `item`. | Fixed. `setting_prefix_issue` returns indices; the CLI words the sentence from argv. `quoted_span` is gone. `assert_prefix_items_survive_any_length`. |
| A5 | nit | A version past int64 is echoed as 9223372036854775807. | Recorded. |

Checked and clean: cursor decode bounds, base64url, bound SQL everywhere, the helper id grammar, the token wipe, NUL handling, an INT64 vector entry, file mode 0600 and parent 0700, importing a directory or `/dev/null`, duplicate JSON members, a far-future stamp. The hook depth `1x` bypass is in shared code and advisory.

Spec and decisions.

| # | Severity | Finding | Outcome |
|---|---|---|---|
| D1 | important | `setting_invalid_value` detail lacked `key`, `value` and `type` (SETTINGS-SCHEME §5.1, 007 ST-5.4). Stage 6 read C-05 as "adds item". | Fixed per grill (`decision:plan20-invalid-value-detail`). `value` appears when one value was given. Prefix errors keep `item` and `other`. |
| D2 | important | `remember_call` setting actions are in scope, but `remember_call` does not exist. | Moved to the plan that adds `remember_call` (`decision:plan20-remember-call-deferred`). Plan text notes it. |
| D4 | nit | The spec said every verb's `--json` writes an envelope. A successful `export` writes the document. | Fixed in SETTINGS.md. |
| D6 | important | Open item 5 was never grilled. | Grilled: one file per run (`decision:plan20-import-one-file-per-run`). A file with user and brain rows needs `--scope`, else `invalid_argument` and nothing written. Copy draft A (`decision:plan20-import-mixed-scope-draft`, C-43). Tests now import per scope; the exec fault sweep no longer asserts a half-written import. |
| D7 | nit | The stage 9 note says `--changed` is "not the default". The code and spec agree with each other; only the note's wording is loose. | Recorded. |

HEALTH: every decision SETTINGS.md cites resolves; none is stale.

Lens passes. Lenses 1, 2, 5, 6, 7, 10 and 12 found nothing beyond the rows above. Lens 8: every fix above has a test, mutants below. Lens 10 (trust claims) found A1 and A2. Lens 11 found A8 and A9 (sentences cut by a fixed buffer) and the A1 check-then-open window.

Lens 4, pragmatic review (`workflow:pragmatic-review`).
- Summary: one Critical (DRY), fixed; two Improvements recorded; nits.
- DRY. Critical, fixed: the two-pass `vsnprintf` formatter was written twice (`fmt_alloc` in `cmd_settings.c`, `err_fmt` in `commands_common.c`). Now one `fmt_valloc` in `commands_common.c` behind `fmt_alloc` and `err_fmt`; `err_fmt` reports a format failure as out of memory, as `fmt_alloc` callers already did. Improvement, fixed: `days_in_month` existed twice; `commands_common.c` exports the bounds-checked one. `emit_unavailable` in `sync_credential.c` keeps its own `strlen` sizing with an `intentional:` comment: the library does not include the CLI's `commands_common`. Its stale `TODO(pragmatic:)` is gone. Nit, kept: `import_at` aliases `export_at` in the tests so call sites name the verb.
- Easy to replace. Improvement, recorded: the credential runner (posix_spawn, poll, signals) lives in `cmd_settings.c`; it would swap more easily in its own file behind `SyncCredentialHost`. Moving it is plan wording, not a fix here. Improvement, recorded: `getenv("REMEMBER_SYNC_TOKEN")` in the library has no env port. It matches G-02.
- Orthogonal. `cmd_settings.c` (about 4600 lines) mixes the runner, option parsing, output and the cursor codec; `test_settings.c` is about 9.5k lines. Recorded for a split when either next grows.
- Write gate: the Critical is fixed on this branch; nothing new was written around it.

Simplify (lens S). Applied: the prefix sentence moved to the CLI and `quoted_span`, `k_and_quote`, `prefix_bad_item` and `prefix_dup_item` went away (about 60 lines). Applied: one formatter and one `days_in_month` (about 45 lines). Applied: `canon_prefixes` folds through the shared `prefix_fold_all`. Declined: `apply_set` discards `prepared`; the store returns QUOTA for both caps, so the CLI keeps its own read.

Mutants, one clause each, settings and errors suites, in a copy of the tree: 21 over the review fixes. 19 killed: the `O_NOFOLLOW` flag and the ENOENT-is-absent branch (A1), the old-value type check (A10), the C-10 escape (A2), the bad and duplicate prefix branches, the twice-or-same choice, the fix choice, the duplicate scan, the item cap and the out-of-memory mapping (A9), `>=` on the vector cap (A4), `>` on the file cap (A3), the mixed-scope rule, `--scope` and the check itself (D6), `value` and `type` in the detail (D1), and the leap and range rules of `days_in_month`. The A9 out-of-memory mutant survived the first pass; the sweep now asserts no `internal error`. A crash counts as killed: the harness first counted a SIGSEGV as a survivor. One mutant is equivalent: the history line's `output_text_human` on a valid display, which never holds a control byte. The mutant with no file cap was not run to the end: it reads `/dev/zero` until memory runs out, the exploit A3 closes. A7's wipe has no observable effect for a test.

Gate on this tree, sanitizer build: ctest passed 6 of 6; `check-coverage.sh` exited 0 (functions 100%, effective lines 100%, must-cover 0, pure-TU branches 0 missed); `lint-all.sh` exited 0 (every check `PASS`, `LINT OK`; IWYU skipped, Linux CI runs it); `leaks-macos` passed. Settings suite: 151 tests, 21563 asserts, 0 failed. The first gate run caught a format miss, seven clang-tidy items and three uncovered lines (a `sqlite3_open_v2` ENOENT branch the precheck made dead, now removed; the C-10 write failure and the unreadable-import out-of-memory path, now swept). Not pushed; no PR.

Grades.

Coder (plan 20, stages 1–10): 7/10. Strengths: a registry and resolution core that held under every probe; bound SQL and cursor decoding with no hole found; tests that sweep store, memory and write faults over every view; every draft sentence filed in C-43. Misses: the symlink rule applied to one opener of six (A1); stored text echoed raw in two places (A2, A10); two fixed buffers that cut caller text (A8, A9); a spec detail table read as a different rule (D1); a scope item with no target API (D2); a known two-file limit carried forward instead of grilled (D6).
Pointers:
- When a path rule (no symlink, mode, owner) protects one opener, grep every opener of the same file and give them one helper.
- Any text read back from a store or a synced peer goes through the human or JSON escaper, even when the write path validated it.
- Before writing a detail object, copy the member list from the spec table, not from the copy note.
- A limit recorded in two stage notes is a grill, not a note.

Stage reviewers (the in-session deep reviews of stages 3–9): 6/10. They caught real defects (the unhooked allocator in stage 9, the M7 history mutant, the lint boundary) and their mutant passes were thorough. They missed the cross-verb symlink gap, both raw echoes, and the D1 detail mismatch, and they recorded the two-file limit twice without a grill. Their locks held: nothing they fixed regressed.
Pointers:
- Run the trust-claim lens (10) on every reader, not only the verb under review.
- Probe each new output path with a hostile stored value (ESC, quote, `}`) in plain and `--json`.
- Check each `detail` object against the SETTINGS-SCHEME §5.1 row for its code.

INDEX row 20 stays unticked.

**2026-11-09, exit 5 (after the whole-plan review).** A check of the remaining plans against Remember found `decision:2026-11-03-confirmation-required-exit-5` (R30-6) owned by no plan. Plan 21 left exit 5 out. Itay chose to land it in this PR. `confirmation_required` from `settings reset --all` and `--namespace` now exits 5 (`REMEMBER_CONFIRM` in `exit_codes.h`). Exit 4 still wins under `--strict` with a warning. The four refusal tests assert 5; they failed on exit 1 first. JSON-ENVELOPE, JSON-STYLE, SETTINGS.md, the skill and the engineering notes name exit 5. No sentence changed.
