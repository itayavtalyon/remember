# Copy request for Story Weaver

For Itay to approve before it goes to Story Weaver. Each item names the code or UI spot, the plan that prints it, and the context. Rules from COPY-PENDING apply: a plain line has the `remember: ` prefix; JSON `message` is the text after the prefix up to the fix; `fix` is the last next-action sentence and must not be empty (JE-2.6); keys and values in double quotes, commands in single quotes. A plan that waits for an item marks the tests that wait with `COPY-PENDING R<id>` and prints no unapproved text once the item is approved.

The first version of this file lived outside the repository, at `fkm-staging/plans-rewrite-2026-11-11/COPY-REQUEST.md`. Its items were answered by `decision:2026-11-12-copy-batch-final`. This file starts again with the items that batch does not answer.

## Remember

### R25-2. The plugin manifest parser: every schema-violation sentence (plan 25)

**Approved by Itay on 2026-11-14** (`decision:2026-11-14-copy-r25-2-manifest-approved`). The approved text is Story Weaver's revised draft (`R25-2.md`, 2026-11-13), word for word, and it replaces the first draft below. Its changes: sentence 1b (nesting over 64 levels), sentence 4 says "both missing", sentence 7b (a known field at the wrong level), a second fix for a repeated name, the first persistent entry named in sentence 12, new fixes for sentences 13 and 14, and "contain" in sentence 15. Rules that came with it: Q1 whole numbers are read by value, Q2 plugin-controlled text prints with JSON string escaping, Q3 a duration or size over 23 bytes gets the ordinary phrase. The plan 25 tests and `specs/PLUGIN-MANIFEST.md` hold the approved text.

- Spot: every `plugin_manifest_invalid` error and every `plugin_protocol_unsupported` error of `plugin.json` that no approved sentence covers (`message` and `fix`; the plain line is `remember: <message> <fix>`).
- Need: `message` and `fix` for each sentence below, or a rewrite of the shape.
- Context: the parser reports every error of a file at once, each with a JSON Pointer (`detail.path`) and an R-number (`detail.rule`). The first draft of the parser had about 40 sentences of its own. This item cuts them to 16 and one table of phrases.

Decision: `decision:2026-11-12-plugin-manifest-copy-r25-2` (G1 A), with the `version` line from `decision:2026-11-12-manifest-version-unsupported` (G2 A) and the `..` line from `decision:2026-11-12-plugin-command-no-dotdot` (G4 A).

Already approved, and not part of this item: R25-1 (`protocol`, `decision:2026-11-12-copy-batch-final`), C-35 (the unknown-field warning, with its E3B-1 `fix`), and C-36 (the hyphen and the reserved `enabled` lines). Plan 25 prints those word for word.

Every sentence below is `plugin_manifest_invalid` (exit 1, `retryable` false) unless it says otherwise. Each has `detail.path` (a JSON Pointer into plugin.json) and `detail.rule` (the R-number of PLUGIN-PROTOCOL R2). The plain line of each is `remember: <message> <fix>`. One manifest can return many of these at once (`decision:plugin-manifest-errors`).

**Placeholders**
- `<where>` is `plugin.json <pointer>`, or `plugin.json` alone when the pointer is empty (the top level). The pointer is the JSON Pointer of the thing the sentence is about. `<where>` never holds a plugin-controlled name unescaped: `~` is `~0` and `/` is `~1`.
- `<field>` is the member name. `<name>` is a plugin, entry, or setting name as the file wrote it.
- `<what>` is one phrase from the table at the end of this item.

#### The sentences

1. **Not JSON.** Pointer: empty. Rule: R2.2.
   - **message:** `plugin.json isn't valid JSON.`
   - **fix:** `Fix the JSON syntax in plugin.json.`
2. **Top level is not an object.** Pointer: empty. Rule: R2.2.
   - **message:** `plugin.json must hold one JSON object.`
   - **fix:** `Make the top level of plugin.json an object.`
3. **A required member is missing.** `<where>` names the object that lacks it.
   - **message:** `<where>: "<field>" is missing.`
   - **fix:** `Add "<field>".`
   - Example: `remember: plugin.json /filters/0: "point" is missing. Add "point".`
4. **Neither list of entries is present.** Pointer: empty. Rule: R2.5.
   - **message:** `plugin.json: "actions" or "filters" is missing.`
   - **fix:** `Add "actions" or "filters".`
5. **A value has the wrong type, or is outside its range.** `<where>` names the value itself.
   - **message:** `<where>: must be <what>.`
   - **fix:** `Use <what>.`
   - Example: `remember: plugin.json /filters/0/budget: must be a duration from 1ms to 30s. Use a duration from 1ms to 30s.`
   - This one pair covers wrong type, a number or duration out of range, an empty list, a list or string over its limit, and a value outside a fixed list. `detail.rule` for `budget` is `budget_range` (`decision:2026-11-11-manifest-budget-range`). For `cold_start_grace` it is `R2.13`.
6. **A name that is not a valid name** (R2.22). The hyphen case has its own approved line (C-36). Any other bad name uses this.
   - **message:** `<where>: "<name>" isn't a valid name.`
   - **fix:** `Use a lowercase letter, then lowercase letters, digits, or "_", up to 40 characters.`
   - Example: `remember: plugin.json /name: "Wake" isn't a valid name. Use a lowercase letter, then lowercase letters, digits, or "_", up to 40 characters.`
7. **Unknown field on a closed object** (`run`, `permissions`, an action's `filter` block, and the top-level members `run`, `permissions`, `filter`). `<where>` names the object. `detail.path` names the field.
   - **message:** `<where>: unknown field "<field>".`
   - **fix:** `Remove "<field>".`
   - **fix when one known name of that object is one edit away:** `Did you mean "<known>"?`
   - Example (`decision:plugin-manifest-errors`): `remember: plugin.json /filters/0/run: unknown field "proces". Did you mean "process"?`
   - Top-level `run`, `permissions`, `filter`: **fix** `Put it inside an action or a filter.`
8. **A member or an item appears twice.** For a repeated key, `<where>` names the object and `detail.path` names the member. For a repeated item, `<where>` names the list.
   - **message:** `<where>: "<x>" appears more than once.`
   - **fix:** `Keep one "<x>".`
9. **Protocol or manifest version unsupported when the file has no usable name.** R25-1 needs `<name>`. These two lines cover a manifest with no `name`, or one that is not a string. `<n>` is the number as written.
   - **message:** `plugin.json /protocol: this plugin was written for plugin protocol <n>, and this remember reads protocol <range>.`
   - **fix:** `Update remember, or install an older version of the plugin.`
10. **Manifest format version unsupported** (G2 A). Code `plugin_protocol_unsupported`. Exit 1. `retryable` false. `detail.supported` is `[1]`. Same shape as R25-1.
    - **message:** `plugin.json /version: "<name>" uses plugin.json format <n>, and this remember reads format <range>.`
    - **fix:** `Update remember, or install an older version of "<name>".`
    - With no usable name: **message** `plugin.json /version: this plugin uses plugin.json format <n>, and this remember reads format <range>.` **fix** `Update remember, or install an older version of the plugin.`
    - `<range>` is `1`, or `1 to 2` once a second format exists.
11. **`memory.*` listed with other events** (R2.21). `<where>` is `plugin.json <entry>/events`.
    - **message:** `<where>: "memory.*" can't be listed with other events.`
    - **fix:** `List "memory.*" alone.`
12. **Persistent entries with different commands** (R2.10). `<where>` is `plugin.json <the second entry>/run/command`.
    - **message:** `<where>: this entry runs a different command than the first persistent entry. All persistent entries share one process.`
    - **fix:** `Give every persistent entry the same command.`
13. **Backfill with no persistent entry** (R2.17).
    - **message:** `plugin.json /backfill: backfill is on, and no entry is persistent.`
    - **fix:** `Set "process" to "persistent" on an entry, or turn backfill off.`
14. **`min` above `max` in a setting range** (R2.11).
    - **message:** `<where>: "min" is greater than "max".`
    - **fix:** `Put "min" at or below "max".`
15. **A relative command that leaves the plugin folder** (G4 A, R2.8). `<where>` is `plugin.json <entry>/run/command/0`. A relative path is one that does not start with `/`. A `..` segment is a path piece that is exactly `..`.
    - **message:** `<where>: a relative command can't use "..".`
    - **fix:** `Use a path inside the plugin folder, or an absolute path.`

16. **A setting key `enabled` in a manifest with no usable plugin name** (C-36 needs `<name>`). The approved C-36 line is used when the plugin has a valid name.
    - **message:** `this plugin declares a setting named "enabled", and that name is reserved.`
    - **fix:** `Rename that setting in plugin.json.`

#### The `<what>` phrases

Each phrase is used in sentence 5. A phrase that ends in a list of allowed values is built from the same table that checks the value, so the two cannot drift.

| Where | `<what>` |
|---|---|
| `/format` | `"remember-plugin"` |
| `/version`, `/protocol` (not a whole number) | `a whole number` |
| `/plugin_version` | `a version like "1.0.0"` |
| `/description`, a setting `description` | `a string of at most 200 characters` |
| `/data_version` | `a whole number of 1 or more` |
| `/idle_timeout`, an action `run.timeout`, a duration setting | `a duration like "10m"` |
| `/backfill` | `an object` |
| `/backfill/enabled`, a boolean setting `default` | `true or false` |
| `/backfill/max_batch_entries` | `a whole number from 1 to 100` |
| `/backfill/max_batch_size` | `a size of at most 4MiB` |
| `/actions`, `/filters` | `a list of 1 to 16 entries` |
| an entry, `run`, `permissions`, `filter`, a setting, `range` | `an object` |
| `events` | `a list of 1 to 8 of "memory.created", "memory.updated", "memory.deleted", "memory.restored", "memory.purged", "memory.expired", or "memory.*"` |
| `point` | `"search.query" or "search.results"` |
| `priority` | `a whole number from 0 to 1000` |
| `budget`, `cold_start_grace` | `a duration from 1ms to 30s` |
| action `run.process` | `"once" or "persistent"` |
| filter `run.process` | `"persistent"` |
| `run.command` | `a list of 1 to 8 strings` |
| a `command` item | `a string of 1 to 255 bytes` |
| `permissions.needs` | `a list of at most 16 items` |
| a `needs` item | `a memory field ("key", "body", "tags", "bin", "created_at", "updated_at", "expires_at", "deleted_at", "change_seq", "created_by", "updated_by", or "links"), or a "remember." setting key` |
| `tags.any`, `tags.all` | `a list of at most 8 strings` |
| a tag | `a string of at most 63 bytes` |
| `tags.match_before` | `true or false` |
| `filter.created_by`, `filter.updated_by` | `a list of at most 5 of "human", "agent", "tool", "share", or "import"` |
| `filter.bins` | `a list of at most 4 of "live", "expired", "deleted", or "purged"` |
| `filter.not_caused_by` | `a list of at most 8 strings`; each item `a string of at most 79 bytes` |
| `filter.origins` | `a list of at most 3 of "local", "sync", or "import"` |
| `/settings` | `a list of at most 16 settings` |
| a setting `type` | `"boolean", "integer", "enum", "duration", "size", "string", or "string_list"` |
| a setting `scope` | `"user", "brain", or "both"` |
| setting `default`, integer | `a whole number` |
| setting `default`, size | `a size like "4MiB"` |
| setting `default`, enum | `one of range.values` |
| setting `default`, string | `a string of at most 511 bytes` |
| setting `default`, string_list | `a list of strings, 511 bytes or fewer in all` |
| setting `default`, number-like, outside `range` | `inside range.min and range.max` |
| `range.min`, `range.max`, integer | `a whole number` |
| `range.min`, `range.max`, duration | `a duration like "10m"` |
| `range.min`, `range.max`, size | `a size like "4MiB"` |
| `range.values` | `a list of at most 8 strings` |
| a `range.values` item | `a string of at most 63 bytes` |
| `range.max_items` | `a whole number of 1 or more` |

#### Notes

- [ASSUMPTION] The shape `<where>: must be <what>.` replaces about 40 separate sentences with one pair and a table. Say if you want each field to have its own sentence instead.
- [ASSUMPTION] `<where>` for a missing member names the object, not the missing member, because a pointer must point into the file (`decision:plugin-manifest-errors`).
- [ASSUMPTION] Sentence 7 uses the object in `<where>` and the field in the message, as the `decision:plugin-manifest-errors` example does. `detail.path` holds the pointer of the field itself.
- The numbers in the table (16 entries, 8 events, 8 command arguments, 255 bytes, 16 needs, 8 tags, 63 bytes, 5, 4, 3, 79 bytes, 16 settings, 8 enum values, 511 bytes) are implementation limits that no decision states. They are grill items L1 to L14 for Itay, not copy. Change a number and its row changes with it.
- [ASSUMPTION] A `string_list` setting default may hold strings. The first draft of the parser refused any non-empty list, which the schema allows.
- [ASSUMPTION] A `protocol` or `version` written as `1.0`, `-0` or `1e0` is not the number `1`, so it gets sentence 9 or 10 with `<n>` as written. JSON Schema reads `1.0` as the integer 1. Say if `1.0` should parse.
- The words "plugin protocol", "protocol" and "format" in sentences 9 and 10 follow R25-1. The word "format" names the `format`/`version` pair of plugin.json (PLUGIN-PROTOCOL R2.2).
