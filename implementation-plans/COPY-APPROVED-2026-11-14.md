# Approved copy: R25-2, R27b-2 and R27b-3 (2026-11-14)

Itay approved the two Story Weaver items below on 2026-11-14, with option A on every open question. The records are `decision:2026-11-14-copy-r25-2-manifest-approved` (R25-2, plan 25) and `decision:2026-11-14-copy-r27b-2-r27b-3-approved` (R27b-2 and R27b-3, plan 27b). The sentences are the drafts of 2026-11-13, word for word, with two edits: the phrase table of R25-2 is a list, and each open question holds only the answer Itay chose. A plan prints these lines and does not invent another. Where this file and a plan disagree, the plan stops and asks Itay.

The older approved sentences stay in [`COPY-PENDING.md`](COPY-PENDING.md) and in `decision:2026-11-12-copy-batch-final`.

# R25-2. The plugin.json parser sentences (plan 25)



Source: R25-2 in `implementation-plans/COPY-REQUEST.md` on `claude/plan-25-manifest-02506a`, plan 25, `schema/plugin/v1/manifest.json`, PLUGIN-PROTOCOL R2. Decisions: `decision:2026-11-12-plugin-manifest-copy-r25-2` (G1 A), `decision:2026-11-12-manifest-version-unsupported` (G2 A), `decision:2026-11-12-plugin-command-no-dotdot` (G4 A), `decision:2026-11-13-plugin-manifest-limits`, `decision:2026-11-13-plugin-origins-empty`, `decision:plugin-manifest-errors`, `decision:2026-11-12-sw1-upgrade-line-no-command`. Voice and shapes follow R25-1 (`decision:2026-11-12-copy-batch-final`) and C-35 (`decision:2026-11-12-copy-batch-e3b-warning-splits`, item 26).

How to read this file:
- A line that starts with a bold label is one string to print. Everything else is a note.
- The plain line is `remember: <message> <fix>`. JSON `message` is the text after the prefix up to the fix. `fix` is the last next-action sentence and is never empty (JE-2.6).
- Keys and values are in double quotes, commands in single quotes. CLI text uses straight quotes and apostrophes.
- Not in this item, and printed word for word as approved: R25-1 (`protocol` with a usable name), C-35 (the `unknown_field` warning on an open object), C-36 (the hyphen line and the reserved `enabled` line, both with a usable name).

## Common to every sentence

- **Code:** `plugin_manifest_invalid`, unless the sentence says otherwise. Exit 1. `retryable` false.
- **detail:** `{"path": "<pointer>", "rule": "<rule>"}`. `path` is a JSON Pointer into plugin.json (R2.5a). `rule` is the PLUGIN-PROTOCOL R-number, or `budget_range` for `budget` (P5 A). Where a sentence gives one rule, it is that rule. Elsewhere it is the R2 rule of that field.
- One parse returns every error in the file (`decision:plugin-manifest-errors`). The order is document order.
- `<where>` is `plugin.json <pointer>`, or `plugin.json` alone when the pointer is empty. In a pointer, `~` is `~0` and `/` is `~1`.
- `<field>` is a member name. `<name>` is a plugin, entry, or setting name as the file wrote it. `<what>` is one phrase from the list below.
- `<field>`, `<name>`, `<x>` and pointer segments are plugin-controlled text. How they print is answer Q2 below: JSON string escaping.

## The sentences

### 1. Not JSON
- **When:** plugin.json is not one valid JSON value: a syntax error, invalid UTF-8, or trailing text. Pointer empty. Rule R2.2.
- **message:** `plugin.json isn't valid JSON.`
- **fix:** `Fix the JSON syntax in plugin.json.`
- **Plain line:** `remember: plugin.json isn't valid JSON. Fix the JSON syntax in plugin.json.`
- Note: nothing else in the file is checked after this error.

### 1b. Nested too deep (new, L14)
- **When:** the JSON is valid but nests deeper than 64 levels (jsonread, plan 23). Pointer empty. Rule R2.2.
- **message:** `plugin.json is nested more than 64 levels deep.`
- **fix:** `Reduce the nesting in plugin.json to 64 levels or fewer.`
- **Plain line:** `remember: plugin.json is nested more than 64 levels deep. Reduce the nesting in plugin.json to 64 levels or fewer.`
- Note: sentence 1 would say "isn't valid JSON" about valid JSON, so L14 gets its own line with its exact number.

### 2. Top level is not an object
- **When:** the file parses, and its top-level value is not an object. Pointer empty. Rule R2.2.
- **message:** `plugin.json must hold one JSON object.`
- **fix:** `Make the top level of plugin.json an object.`
- **Plain line:** `remember: plugin.json must hold one JSON object. Make the top level of plugin.json an object.`

### 3. A required member is missing
- **When:** an object lacks a member the schema requires. `<where>` and `detail.path` name the object, because a pointer must point into the file.
- **message:** `<where>: "<field>" is missing.`
- **fix:** `Add "<field>".`
- **Example:** `remember: plugin.json /filters/0: "point" is missing. Add "point".`
- **Example (top level):** `remember: plugin.json: "plugin_version" is missing. Add "plugin_version".`

### 4. Neither list of entries is present
- **When:** the top level has no `actions` and no `filters`. Pointer empty. Rule R2.5.
- **message:** `plugin.json: "actions" and "filters" are both missing.`
- **fix:** `Add "actions" or "filters".`
- **Plain line:** `remember: plugin.json: "actions" and "filters" are both missing. Add "actions" or "filters".`

### 5. A value has the wrong type or is out of range
- **When:** a value has the wrong type, a number or duration is out of range, a list is empty or over its limit, a string is over its limit, or a value is not in its fixed list. `<where>` and `detail.path` name the value. At a list limit, the error names the list, and items past the limit are not read (`decision:2026-11-13-plugin-manifest-limits`).
- **message:** `<where>: must be <what>.`
- **fix:** `Use <what>.`
- **Example:** `remember: plugin.json /filters/0/budget: must be a duration from 1ms to 30s. Use a duration from 1ms to 30s.`
- **Example (fixed list):** `remember: plugin.json /filters/0/point: must be "search.query" or "search.results". Use "search.query" or "search.results".`
- **Example (list limit):** `remember: plugin.json /actions/0/run/command: must be a list of 1 to 8 strings. Use a list of 1 to 8 strings.`
- `detail.rule`: `budget_range` for `budget`, `R2.13` for `cold_start_grace`, `R2.18` for the two `backfill` limits.

### 6. A name that is not a valid name (R2.22)
- **When:** a plugin name, an entry name, or a setting key is a string that does not match `^[a-z][a-z0-9_]{0,39}$`. A name with a hyphen uses the approved C-36 hyphen line instead. A name that is not a string uses sentence 5.
- **message:** `<where>: "<name>" isn't a valid name.`
- **fix:** `Use a lowercase letter, then lowercase letters, digits, or "_", up to 40 characters.`
- **Example:** `remember: plugin.json /name: "Wake" isn't a valid name. Use a lowercase letter, then lowercase letters, digits, or "_", up to 40 characters.`
- Note: R2.22 gives setting keys the same pattern, so the C-36 hyphen line also covers a setting key.

### 7. Unknown field on a closed object
- **When:** an object that the schema closes has a field it does not know: an entry's `run`, an entry's `permissions`, or an action's `filter` block. `<where>` names the object. `detail.path` names the field. Rule R2.5 (`run`, `permissions`) or R2.21 (`filter`).
- **message:** `<where>: unknown field "<field>".`
- **fix:** `Remove "<field>".`
- **fix, when exactly one known field of that object is one edit away:** `Did you mean "<known>"?`
- **Example:** `remember: plugin.json /filters/0/run: unknown field "proces". Did you mean "process"?`
- **Example:** `remember: plugin.json /filters/0/run: unknown field "timeout". Remove "timeout".`
- Note: one edit is one inserted, deleted, or changed character. When two known fields are one edit away, the fix is `Remove "<field>".`

### 7b. `run`, `permissions` or `filter` at the top level
- **When:** the top level has a `run`, `permissions` or `filter` member. Pointer `/<field>`. Rule R2.5 (`run`, `permissions`) or R2.21 (`filter`).
- **message:** `plugin.json: "<field>" can't be at the top level.`
- **fix (`run`, `permissions`):** `Move "<field>" into an action or a filter.`
- **fix (`filter`):** `Move "filter" into an action.`
- **Example:** `remember: plugin.json: "run" can't be at the top level. Move "run" into an action or a filter.`
- Note: the field is known, only misplaced, so the message does not say "unknown". Only actions have a `filter` block, so its fix names only an action.

### 8. A member or an item appears twice
- **When:** an object repeats a member, or a list that must not repeat holds an item twice (for example `permissions.needs`), or two entries in one list, or two settings, share a name. For a repeated member, `<where>` names the object and `detail.path` names the member. For a repeated item or name, `<where>` names the list and `detail.path` names the second one.
- **message:** `<where>: "<x>" appears more than once.`
- **fix (member or item):** `Keep one "<x>".`
- **fix (entry name or setting key):** `Rename one of them.`
- **Example:** `remember: plugin.json /actions/0/permissions/needs: "body" appears more than once. Keep one "body".`
- **Example:** `remember: plugin.json /actions: "notify" appears more than once. Rename one of them.`
- Note: "Keep one" would delete an entry or a setting, so a repeated name gets "Rename one of them."

### 9. Protocol unsupported, no usable name
- **Code:** `plugin_protocol_unsupported`. Exit 1. `retryable` false. `detail.supported` is `[1]`, as in R25-1.
- **When:** `protocol` is a whole number other than 1, and `name` is missing or is not a string. With a usable name, R25-1 prints. Rule R2.4.
- **message:** `plugin.json /protocol: this plugin was written for plugin protocol <n>, and this remember reads protocol <range>.`
- **fix:** `Update remember, or install an older version of the plugin.`
- **Example:** `remember: plugin.json /protocol: this plugin was written for plugin protocol 2, and this remember reads protocol 1. Update remember, or install an older version of the plugin.`
- `<n>` is the number. `<range>` is `1`, or `1 to 2` once a second protocol exists.

### 10. Manifest format version unsupported (G2 A)
- **Code:** `plugin_protocol_unsupported`. Exit 1. `retryable` false. `detail.supported` is `[1]`. Rule R2.4.
- **When:** `version` is a whole number other than 1.
- **message:** `plugin.json /version: "<name>" uses plugin.json format <n>, and this remember reads format <range>.`
- **fix:** `Update remember, or install an older version of "<name>".`
- **Example:** `remember: plugin.json /version: "router" uses plugin.json format 2, and this remember reads format 1. Update remember, or install an older version of "router".`
- **message, no usable name:** `plugin.json /version: this plugin uses plugin.json format <n>, and this remember reads format <range>.`
- **fix, no usable name:** `Update remember, or install an older version of the plugin.`
- **Example, no usable name:** `remember: plugin.json /version: this plugin uses plugin.json format 2, and this remember reads format 1. Update remember, or install an older version of the plugin.`
- `<range>` is `1`, or `1 to 2` once a second format exists.

### 11. `memory.*` listed with other events (R2.21)
- **When:** an action's `events` holds `memory.*` and any other event. `<where>` is `plugin.json <entry>/events`.
- **message:** `<where>: "memory.*" can't be listed with other events.`
- **fix:** `List "memory.*" alone.`
- **Example:** `remember: plugin.json /actions/0/events: "memory.*" can't be listed with other events. List "memory.*" alone.`

### 12. Persistent entries with different commands (R2.10)
- **When:** a persistent entry's `run.command` differs from the command of the first persistent entry in document order. Each entry that differs gets one error. `<where>` is `plugin.json <that entry>/run/command`. `<first>` is the pointer of the first persistent entry.
- **message:** `<where>: this entry runs a different command than <first>. All persistent entries share one process.`
- **fix:** `Give every persistent entry the same command.`
- **Example:** `remember: plugin.json /actions/1/run/command: this entry runs a different command than /filters/0. All persistent entries share one process. Give every persistent entry the same command.`
- Note: `<first>` replaces "the first persistent entry", so the line names both ends of the mismatch.

### 13. Backfill with no persistent entry (R2.17)
- **When:** `backfill.enabled` is true, there are no filters, and no action has `run.process` `"persistent"`. Pointer `/backfill/enabled`.
- **message:** `plugin.json /backfill/enabled: backfill is on, and no entry is persistent.`
- **fix:** `Set "process" to "persistent" in an action's "run", or set "enabled" to false.`
- **Plain line:** `remember: plugin.json /backfill/enabled: backfill is on, and no entry is persistent. Set "process" to "persistent" in an action's "run", or set "enabled" to false.`
- Note: every filter is persistent, so this can only happen with actions alone. The fix names an action for that reason.

### 14. `min` above `max` in a setting range (R2.11)
- **When:** a setting's `range.min` is greater than its `range.max`. `<where>` is `plugin.json /settings/<i>/range`.
- **message:** `<where>: "min" is greater than "max".`
- **fix:** `Set "min" to "max" or lower.`
- **Example:** `remember: plugin.json /settings/1/range: "min" is greater than "max". Set "min" to "max" or lower.`

### 15. A relative command that leaves the plugin folder (G4 A, R2.8)
- **When:** `run.command[0]` does not start with `/`, and one of its path segments is exactly `..`. `<where>` is `plugin.json <entry>/run/command/0`.
- **message:** `<where>: a relative command can't contain "..".`
- **fix:** `Use a path inside the plugin folder, or an absolute path.`
- **Example:** `remember: plugin.json /actions/0/run/command/0: a relative command can't contain "..". Use a path inside the plugin folder, or an absolute path.`

### 16. Setting key `enabled`, no usable name (C-36 needs `<name>`)
- **When:** a setting's `key` is `enabled`, and `name` is missing or is not a string. With a usable name, the approved C-36 line prints. Pointer `/settings/<i>/key`. Rule R2.11.
- **message:** `this plugin declares a setting named "enabled", and that name is reserved.`
- **fix:** `Rename that setting in plugin.json.`
- **Plain line:** `remember: this plugin declares a setting named "enabled", and that name is reserved. Rename that setting in plugin.json.`
- Note: no `<where>` prefix, to match the approved C-36 line it stands in for.

## The `<what>` phrases (sentence 5)

Final text. Each item is: where, then the phrase, then its source. A phrase that lists allowed values is built from the same list that checks the value. A phrase with a number uses the number from `decision:2026-11-13-plugin-manifest-limits` (L-number in the last column), the schema, or R2.

- `/format`: `"remember-plugin"` (R2.2)
- `/version`, `/protocol`, not a whole number: `a whole number` (R2.2)
- `/name`, an entry `name`, a setting `key`, not a string: `a string` (R2.22)
- `/plugin_version`, not a version: `a version like "1.0.0"` (schema)
- `/plugin_version`, over the limit: `a version of at most 63 bytes` (L12)
- `/description`, a setting `description`: `a string of at most 200 characters` (schema)
- `/data_version`: `a whole number of 1 or more` (R2.14)
- `/idle_timeout`, an action `run.timeout`, a duration setting `default`: `a duration like "10m"` (schema)
- `/backfill`: `an object` (schema)
- `/backfill/enabled`, a boolean setting `default`, `tags.match_before`: `true or false` (schema)
- `/backfill/max_batch_entries`: `a whole number from 1 to 100` (R2.18)
- `/backfill/max_batch_size`: `a size of at most 4MiB` (R2.18)
- `/actions`, `/filters`: `a list of 1 to 16 entries` (L1)
- an entry, `run`, `permissions`, `filter`, `tags`, a setting, `range`: `an object` (schema)
- `events`: `a list of 1 to 8 of "memory.created", "memory.updated", "memory.deleted", "memory.restored", "memory.purged", "memory.expired", or "memory.*"` (L2)
- `point`: `"search.query" or "search.results"` (schema)
- `priority`: `a whole number from 0 to 1000` (schema)
- `budget`, `cold_start_grace`: `a duration from 1ms to 30s` (P5 A, R2.13)
- action `run.process`: `"once" or "persistent"` (R2.9)
- filter `run.process`: `"persistent"` (R2.9)
- `run.command`: `a list of 1 to 8 strings` (L3)
- a `command` item: `a string of 1 to 255 bytes` (L4)
- `permissions.needs`: `a list of at most 16 items` (L5)
- a `needs` item: `a memory field ("key", "body", "tags", "bin", "created_at", "updated_at", "expires_at", "deleted_at", "change_seq", "created_by", "updated_by", or "links"), or a "remember." setting key` (R2.16)
- `tags.any`, `tags.all`: `a list of at most 8 strings` (L6)
- a tag: `a string of at most 63 bytes` (L6)
- `filter.created_by`, `filter.updated_by`: `a list of at most 5 of "human", "agent", "tool", "share", or "import"` (L7)
- `filter.bins`: `a list of at most 4 of "live", "expired", "deleted", or "purged"` (L7)
- `filter.not_caused_by`: `a list of at most 8 strings` (L8)
- a `not_caused_by` item: `a string of at most 79 bytes` (L8)
- `filter.origins`: `a list of at most 3 of "local", "sync", or "import"` (L7)
- `/settings`: `a list of at most 16 settings` (L9)
- a setting `type`: `"boolean", "integer", "enum", "duration", "size", "string", or "string_list"` (schema)
- a setting `scope`: `"user", "brain", or "both"` (R2.12)
- setting `default`, integer: `a whole number` (schema)
- setting `default`, size: `a size like "4MiB"` (schema)
- setting `default`, enum: `one of the "values" in "range"` (schema)
- setting `default`, string: `a string of at most 511 bytes` (L11)
- setting `default`, string_list: `a list of strings of at most 511 bytes as written` (L11)
- setting `default`, number-like, outside `range`: `a value inside "range"` (R2.11)
- `range.min`, `range.max`, integer: `a whole number` (schema)
- `range.min`, `range.max`, duration: `a duration like "10m"` (schema)
- `range.min`, `range.max`, size: `a size like "4MiB"` (schema)
- `range.values`: `a list of at most 8 strings` (L10)
- a `range.values` item: `a string of at most 63 bytes` (L10)
- `range.max_items`: `a whole number of 1 or more` (schema)

Notes on the list:
- `filter.origins`: "at most 3" includes the empty list. An explicit `[]` parses, and the action never fires (`decision:2026-11-13-plugin-origins-empty`). An omitted `origins` is `["local"]`. No sentence is printed for `[]`.
- The `not_caused_by` row of the request held two phrases. It is now two rows, so each row has one phrase.
- `/plugin_version` is now two rows, so the 63-byte limit (L12) has its exact number.
- The enum-default phrase was `one of range.values`, and the outside-range phrase was `inside range.min and range.max`. Neither made a sentence after "Use", and neither quoted its keys. Both are rewritten.
- The string_list default phrase now says "as written", because L11 counts the bytes of the source JSON.
- New rows: a name that is not a string (sentence 6 needs a string to quote), and `tags` that is not an object.

## The [ASSUMPTION] notes, resolved

- One pair (`<where>: must be <what>.` / `Use <what>.`) and the list replace the 40 sentences. Kept, because the list keeps every rule in one place and cannot drift from the checks. The fix repeats `<what>` on purpose, so an agent that reads only `fix` still knows the allowed value.
- `<where>` for a missing member names the object. Kept, because a JSON Pointer must point into the file (`decision:plugin-manifest-errors`), and the member is not in the file.
- Sentence 7 names the object in `<where>` and the field in the message, and `detail.path` points at the field. Kept, because it matches the locked `decision:plugin-manifest-errors` example word for word.
- A `string_list` setting default may hold strings. Kept, because the schema allows it, and L11 gives the limit (511 bytes of source JSON).
- `1.0`, `-0` and `1e0` for `protocol` or `version`: not resolved here. It is parser behavior, and Q1 A below settles it: they are read by value.

## Changes from the request

- Sentence 9 gets code `plugin_protocol_unsupported` and `detail.supported`. The request left it under the `plugin_manifest_invalid` default.
- New sentence 1b for the nesting limit (L14).
- Sentence 4 says "both missing". "`actions` or `filters` is missing" reads as if one of them is missing.
- Sentence 7b replaces "unknown field" for top-level `run`, `permissions` and `filter`, which are known fields in the wrong place. The `filter` fix names only an action.
- Sentence 8 has a second fix for a repeated name.
- Sentence 12 names the first persistent entry by its pointer (`<first>`).
- Sentence 13 points at `/backfill/enabled`, and the fix names an action and `"enabled"`.
- Sentence 14 fix is `Set "min" to "max" or lower.`
- Sentence 15 says "contain" instead of "use".

## Answers (Itay, 2026-11-14: A on every question)

**Q1. A whole number written as `1.0`, `1e0` or `-0`.** jsonread keeps the raw number text, so the parser must choose. This applies to every whole-number field (`version`, `protocol`, `priority`, `data_version`, `max_batch_entries`, an integer setting).
- **Approved (A):** read it by value. `1.0` and `1e0` are 1 and parse. `-0` is 0. `1.5` gets sentence 5 (`must be a whole number`). Why: the schema accepts `1.0` as 1, and plan 25 says a document the schema accepts parses. `<n>` in sentences 9 and 10 prints the value, never the source text, so the line never says "protocol 1.0, and this remember reads protocol 1".

**Q2. Plugin-controlled text in a line.** A field name, a name, or a pointer segment can hold a quote, a backslash, or a control character (an escape sequence or a line break) in the terminal line.
- **Approved (A):** inside the double quotes and in a pointer, print the text with JSON string escaping (`\"`, `\\`, `\n`, `\u001b`). The plain line and the JSON `message` hold the same text. Why: the line stays one line, a terminal escape cannot run, and a reader can copy the escaped name back into the file.

**Q3. A duration or size string over 23 bytes (L13).**
- **Approved (A):** use the same phrase as any bad duration or size (`a duration like "10m"`, `a size like "4MiB"`, or the range phrase where one exists). Why: a 23-byte duration is already far past every range, so the cap only stops overflow, and "at most 23 bytes" next to a size value reads as a size of 23 bytes.


---

# R27b-2 and R27b-3. Newer brain by min_reader_version, and the --help exit codes (plan 27b)



Source: `decision:2026-11-13-plan27b-review-grill` (G2 A, G4), `decision:2026-11-03-schema-read-only`, R27b-1 in `decision:2026-11-12-copy-batch-final`, `decision:2026-11-12-sw1-upgrade-line-no-command` (SW1 A), `decision:2026-11-12-e3-warnings-have-fix-field` (E3 B), 007 ST-10.2, `decision:2026-11-03-confirmation-required-exit-5`. Code read: `src/diag.c`, `src/store_sqlite.c` (`reader_too_old`) and `src/remember_app.c` (`--help`) on `claude/plan-27b-review-254602`.

How to read this file:
- A line that starts with a bold label is one string to print. Everything else is a note.
- The plain line is `remember: <message> <fix>`. A warning's plain line is `remember: warning: <message> <fix>`. JSON `message` is the text after the prefix up to the fix. `fix` is the last next-action sentence and is never empty (JE-2.6).
- Keys and values are in double quotes, commands in single quotes. CLI text uses straight quotes and apostrophes.

## R27b-2. Newer only by min_reader_version (plan 27b, G2 A)

Context: `brain_meta.min_reader_version` is an app version, such as `0.3.0`, not a schema number (log 008 §7 step 3, ST-10.2). A brain can ask for a newer reader while its `schema_version` equals the CLI's. Then the R27b-1 line says "This brain uses schema 6; this remember reads up to schema 6.", which gives no reason. The two schema numbers cannot explain this case. The two app versions can.

Codes, exits and the fix do not change from R27b-1:
- **Read warning code:** `schema_too_new_read_only`. A warning with a `fix` (E3 B). Exit follows the command (0, or 4 with `--strict`).
- **Write failure code:** `schema_too_new`. Exit 75. `retryable` false.
- **fix (both):** `Upgrade remember to make changes.` (SW1 A: no command.)

**When:** this remember opens the brain read-only because of `min_reader_version`, and the brain's `schema_version` is not greater than the CLI's. When the brain's `schema_version` is greater, R27b-1 prints as it is, whatever `min_reader_version` says (the store already ranks the schema first).

Placeholders:
- `<min>` is the brain's `min_reader_version` as stored, for example `0.4.0`.
- `<version>` is this remember's version, for example `0.3.2`.

### Variant 1: `min_reader_version` is a version this remember can read
- **message (both):** `this brain was made by a newer version of remember, so it is read-only here. You can still read and export it. This brain needs remember <min> or later; this is remember <version>.`
- **Read warning plain line:** `remember: warning: this brain was made by a newer version of remember, so it is read-only here. You can still read and export it. This brain needs remember <min> or later; this is remember <version>. Upgrade remember to make changes.`
- **Write failure plain line:** `remember: this brain was made by a newer version of remember, so it is read-only here. You can still read and export it. This brain needs remember <min> or later; this is remember <version>. Upgrade remember to make changes.`
- **Example:** `remember: this brain was made by a newer version of remember, so it is read-only here. You can still read and export it. This brain needs remember 0.4.0 or later; this is remember 0.3.2. Upgrade remember to make changes.`

### Variant 2: `min_reader_version` is not a version this remember can read
The store counts a value it cannot parse as newer (`reader_too_old`). The value is brain bytes of unknown form, so the line does not print it.
- **message (both):** `this brain was made by a newer version of remember, so it is read-only here. You can still read and export it.`
- **Read warning plain line:** `remember: warning: this brain was made by a newer version of remember, so it is read-only here. You can still read and export it. Upgrade remember to make changes.`
- **Write failure plain line:** `remember: this brain was made by a newer version of remember, so it is read-only here. You can still read and export it. Upgrade remember to make changes.`
- Note: this message is the approved C-01 `schema_too_new` text word for word.

### detail (both variants, both codes)
`{"brain_schema_version": "<brain>", "cli_schema_version": "<cli>", "min_reader_version": "<min>", "cli_version": "<version>"}`
- The two schema members stay, as `decision:2026-11-03-schema-read-only` requires. They are equal here.
- `min_reader_version` is the stored value with JSON string escaping, in Variant 2 also. `cli_version` is this remember's version.
- [ASSUMPTION] The two new member names. `min_reader_version` is the `brain_meta` column name. `cli_version` pairs with `cli_schema_version`.

### Notes
- The third sentence copies the R27b-1 shape ("This brain uses schema <brain>; this remember reads up to schema <cli>."), so the two variants read as one family.
- R27b-1 says `<brain>` and `<cli>` are "schema numbers, never app versions". That note is about R27b-1's numbers, which used to print as `v7` and read as app versions. Here the stored value is an app version, so the line names it as one ("remember 0.4.0"). The `diag.h` comment "never an app version" must change with this item.
- "remember <min>" is a version, not a command, so SW1 A holds.

## R27b-3. `remember --help` "Exit codes" lines for 5 and 75 (G4)

The current block (`src/remember_app.c`, same on `main` and the 27b branch):
```
Exit codes:
  0  success (including empty search/list)
  1  usage or error
  2  not found (get/delete/update)
  3  wrong bin (expired / not_expired / deleted / not_deleted)
  4  a warning under --strict
```

New lines, after the 4 line:
- **Exit 5:** `  5  confirmation required (run again with --yes)`
- **Exit 75:** `  75 brain is read-only (made by a newer version of remember)`

The block after this item:
```
Exit codes:
  0  success (including empty search/list)
  1  usage or error
  2  not found (get/delete/update)
  3  wrong bin (expired / not_expired / deleted / not_deleted)
  4  a warning under --strict
  5  confirmation required (run again with --yes)
  75 brain is read-only (made by a newer version of remember)
```

Notes:
- Shape: a short lowercase noun phrase, then the detail in parentheses, as the 0, 2 and 3 lines have it.
- 5: "confirmation required" is the code name `confirmation_required` in words. "run again with --yes" is the approved C-11 fix (`Run '<command> --yes' to confirm.`) cut to fit a help line. Exit 5 is used by every command that asks first (C-11 amended), so the line names no command.
- 75: the words "read-only" and "made by a newer version of remember" are the approved C-01 and R27b-1 terms. Exit 75 comes only from a write, and a read on the same brain exits 0 (or 4 with --strict), so the line names the brain's state, not one command. The line names no upgrade command (SW1 A).
- 75 has one space after it, not two. Then its text starts in the same column as every other line, and the five old lines do not change.

## Answers (Itay, 2026-11-14: A on every question)

**Q1. R27b-2: name the two app versions, or not.**
- **Approved (A):** Variant 1 and Variant 2 above. The line says which remember the user needs ("This brain needs remember 0.4.0 or later; this is remember 0.3.2."). Why: it is the only fact that explains this case, and it tells the user how far to upgrade.

**Q2. user.db.** `user_meta.min_reader_version` (ST-10.3) can also be newer than this remember. This item covers only a brain. If plan 27b also opens user.db read-only, that needs its own line (it is not "this brain"). **Approved (A):** a separate item when a plan prints it; nothing in 27b today.
