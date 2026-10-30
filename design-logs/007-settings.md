# 007 - Settings: dotted keys, layers, export

**Status:** Design (grill closed 2026-10-21). Amended 2026-10-22: agents keep read access to settings (#571 note, ST-9.5) and plugins read settings read-only (ST-9.6). 2026-10-22 merge answers applied: M-14 nit 1, M-17, M-19 N2/N3/N8. S-Q1 to S-Q14 closed 2026-10-19. S-Q15 to S-Q24 closed 2026-10-20. The deep review of 2026-10-21 (G-01 to G-36) reopened the log. Rounds 2 and 3 (simplification) closed the same day. Round 4 closed the same day. The grill is closed. Design states each winner. The implementation plan stays in this draft until it is extracted.
**Changes:** 2026-10-25 copy final (`ANSWERS-FINAL.md`, K-1): the plain lines in ST-7.10 and ST-10.2 are the Story Weaver wording, and ST-7.15 and §10 step 2 name their warning lines. Earlier the same day (Itay, `fk-review/copy-pending/ARCHITECT-ANSWERS-D01-D03-dates.md` approved as written, plus `ITAY-ANSWERS.md`): bucket prefixes have no item limit, and bad or duplicate prefixes have defined errors (D-01, ST-5.4); the v5 → v6 migration warns on a bad prefix (§10 step 2); one setting object for every settings command, with `action` on reset, history and import rows, and page sizes and cursors (D-03, ST-7.11, ST-7.14); the history field `at` becomes `changed_at`; human dates (C-13, ST-7.16); the `setting_invalid` warning-or-error rule (C-14, ST-7.15); `setting_import_skipped` (C-12, ST-8.5); the flag is named in `setting_out_of_range` (C-06, ST-2.5); the reset confirmation counts keys and fails with `confirmation_required` (C-11, ST-7.8); a bad value in `settings import` is a `setting_invalid` warning that skips the row, and an unknown key skips the row with `setting_import_skipped`, both overriding S-Q21 (ST-8.4).
**Author:** Itay (+ agent facilitation)
**Created:** 2026-10-18

Cross-refs: [001](001-foundations.md), [002](002-core-facade.md), [005](005-sync-foundations.md),
[006](006-ui-overhaul-engine.md) (E12 bucket prefixes, E6 export), [008](008-json-envelope.md) (JSON shapes,
identity rename), [009](009-plugin-protocol.md) (plugin protocol), [010](010-plugins-and-hooks.md) (plugins and hooks).
App logs in `remember-mac`: 010 and 011 (Settings window).
Sources: `fk-review/SETTINGS-SCHEME.md` (cited **SS**, revision of 2026-10-18 14:57), `fk-review/SETTINGS-RETHINK.md`
(cited **SR**), `drafts/remember/PLUGIN-PROTOCOL.md` revision 5 (cited **PP**), and the locked Remember decisions
named below as `[key]` (#id). When SS and a locked decision differ, the decision wins and Design states the winner.

> Drafting note: the grill closed on 2026-10-20. Design is the build surface. The Implementation plan
> section stays in this draft until it is extracted. It is not a second Design.

## Background

Baseline: schema `user_version` 5 (log 006, plan 15 landed). Verified facts:

- Plan 15 created two empty tables: `settings(name, value, updated_at)` and `bucket_prefixes(prefix, added_at, removed_at, device_id)` (006 E1.3).
- Plan 19 (`config get|set bucket-prefixes`, 006 §11) has not landed. No settings command has shipped.
- Itay's own brain is already at v5 (a dev build). The Homebrew `remember` refuses it with "database is newer than this remember".
- SR (2026-10-14) replaced E12's two tables with one generic `settings` table. SS (2026-10-16) replaced SR's colon keys and `device` scope with dotted keys and the `user` and `brain` scopes.
- The plugin protocol (PP) needs per-plugin settings, per-entry budgets and a chain cap.

## Problem

Remember, the Fortkeeps apps and plugins need one place for configuration. The place must:

- Hold per-user and per-brain values, with one visible winner.
- Never hold a command or a secret.
- Let a user see where a value came from and undo a change.
- Export and import without losing data (PRINCIPLES value 1).
- Give plugins settings without a second mechanism.

E12 has two special tables and colon keys. It cannot do this.

## Questions and Answers

Decisions that shape this log, in the words of the decisions:

- `[decision:settings-scheme]` (#443): "The DB is the single source of truth for settings. The CLI offers settings list, get, set, reset and explain. ... JSON export and import apply all or nothing. User-wide settings are stored in a SQLite user.db. ... Precedence is default, then user, then brain, then flag. set writes to the user layer by default. set --brain writes to the current brain. A brain-only setting is written to the brain automatically. Plugins declare their settings in the manifest settings block under plugin.<name>.*"
- `[decision:settings-scope]` (#444): "Each setting declares scope user, brain or both. User settings live only in user.db. Brain settings, such as sync and hook sync-events, live in the brain settings table and travel with the brain. ... A wrong layer gives setting_scope_mismatch with the correct layer."
- `[decision:settings-dotted-keys]` (#445): "Settings keys use the lowercase dotted form ... The rename happens before plan 19 ships. There are no aliases for the old colon keys."
- `[decision:settings-api-readonly]` (#447): MCP and HTTP list, get and explain settings, and never change them.
- `[decision:hybrid-enable-ask-once]` (#446): the app asks once whether to use hybrid search everywhere, and sets the user setting only when the user agrees. The CLI default stays off until then.
- `[decision:plugins-filter-budget]` (#430), `[decision:plugins-budget-defaults]` (#432), `[decision:plugins-chain-cap]` (#436), `[decision:plugins-budget-clip]` (#442): manifest budget, user override up to a 30 s hard cap, 800 ms warm, 10 s cold grace, 1.5 s chain cap, clip to the cap with a warning.
- `[decision:protocol-oq-1-2]` (#464), OQ-2: a manifest setting declares its scope (`user`, `brain` or `both`), default `user`.
- `[decision:protocol-nq-14]` (#506): each filter entry has its own budget, and every entry counts toward the chain cap.
- `[decision:settings-entry-budget-key]` (#511): "Each filter entry's budget setting is plugin.<name>.<entry>.budget. An entry name may not match one of the plugin's setting keys."
- `[decision:settings-chain-cap-covers-grace]` (#509): "The filter chain cap covers everything, including the cold-start grace. A cold filter's first search falls back to BM25 with code 4 while the filter keeps loading, and the plugin is not killed." Superseded in part by `[decision:plugins-degraded-exit-code]` (#516): the fallback exits 0 with a warning, and 4 only with `--strict` (`[decision:strict-warnings]` #574).
- `[decision:settings-export-scope]` (#508): "A brain export includes only that brain's own (brain-layer) settings. User-wide settings are exported separately with remember settings export."
- `[decision:json-export-plain]` (#500): the settings export is a plain document with its own `format` and `version`, `remember-settings-export`.
- `[decision:plugins-install-scope]` (#454): plugins install once per user. "Each brain keeps its own enabled list in its brain settings."
- `[decision:plugins-default-enabled]` (#448): built-in plugins ship disabled. A plugin that the user adds is enabled after the typed trust confirmation.
- `[decision:design-log-split]` (#485): settings is one of four logs. Pre-1.0 breaking changes are a clean break with a changelog note and no aliases.
- SR Q1 (2026-10-14): one generic `settings` table, plain key-value.

Decisions of 2026-10-18 and 2026-10-19 (Itay) that change this log:

- `[decision:settings-scope-flag]` (#589): the layer is chosen with `--scope brain|user` only, on set, get, list, reset and export. #443's "set --brain" becomes "set --scope brain".
- `[decision:settings-set-default-layer]` (#590): without `--scope`, `set` writes the user layer, unless the key allows only the brain layer. There is no per-key `default_scope`.
- `[decision:brain-export-settings-rows]` (#591), `[decision:export-settings-shape]` (#592), `[decision:export-settings-merge-member]` (#593): export rows and shape (§7).
- `[decision:plugin-enabled-default]` (#596): `plugin.<name>.enabled` defaults to `false`. `plugin add` writes `true` after the trust confirmation, source `human`.
- `[decision:warning-time-budget-capped]` (#597): a budget above the chain cap warns `time_budget_capped`. *(Superseded 2026-10-22 by S-06 #715 and M-22 A #763: no such warning; the budget is clipped silently.)* A capped-value code names what was capped (`<what>_capped`). `setting_overridden` keeps only "a higher layer wins". Amends #442, SS §5.3 and PP R2.13.
- `[decision:no-fortkeeps-settings-namespace]` (#571): no `fortkeeps.*` namespace and no `--ui` flag. `fortkeeps.library.bucket_prefixes` becomes `remember.buckets.prefixes` (brain scope, synced, add-wins). `fortkeeps.library.link_enrichment` becomes the URL-fetch plugin's own setting. `fortkeeps.cloud.token` stays Keychain-only in the app. Fortkeeps keeps its UI preferences in its own store, device-only in v1. **Amended 2026-10-22** `[decision:2026-10-22-agent-settings-read]` (#739, M-09): agents keep read access to settings. The app exposes its UI preferences to agents read-only (roadmap §16.4); only the GUI writes them, tagged `human`. Remember settings stay readable through `remember_settings` and `GET /v1/settings` (ST-9.1). No `fortkeeps.*` namespace and no `--ui` flag come back.
- `[decision:settings-sync]` (#548): sync v1 carries brain-layer `synced` keys only. Each key merges by version vector. A truly concurrent pair falls back to the later `updated_at`, then the larger `device_id`; the loser stays in settings history. `remember.buckets.prefixes` keeps E12.6 add-wins. Device rows, user.db and secrets never travel. An invalid value is skipped with `setting_invalid`.
- `[decision:hook-origins-filter]` (#577): hook entries get an `origins` filter. It replaces `remember.hooks.sync_events`.
- `[decision:sync-credential-helper]` (#547): the setting `remember.sync.credential_helper` names a program that prints the Cloud sync token. `REMEMBER_SYNC_TOKEN` works for CI. Remember stores no secrets (#423).
- `[decision:remember-env]` (#551): the host passes a `RememberEnv` to `remember_open` with `data_dir`, `user_db_path`, `runtime_dir`, `plugins_dir` and capability flags. The CLI fills it from HOME and XDG. No HOME lookup inside the engine.
- `[decision:schema-v6-scope]` (#524), `[decision:release-0-3-0-scope]` (#527): one v6 step carries logs 007, 008 and 010 plus sync groundwork (`brain_id`, a per-brain `change_seq` on entries, links and settings). 0.3.0 includes v6 and CLI JSON v2.
- `[decision:change-seq]` (#543): one monotonic per-brain `change_seq`, bumped on every write to entries, links and settings, tombstones included.
- `[decision:min-reader-version]` (#565): a schema step records `min_reader_version`. An older engine opens a newer file read-only, or read-write when the step is additive-only. Replaces the hard refusal of 006 E1.5.
- `[decision:quota-exceeded-envelope]` (#549): one code `quota_exceeded` with `detail.quota` (`settings_limit` among them).
- `[decision:mcp-tool-catalog]` (#539), `[decision:api-surface-log]` (#583): the MCP tool schemas and the error-code catalog live in the API surface v1 log. No settings writes over MCP in v1.

### Resolved questions (S-Q1 to S-Q14, 2026-10-19)

The 2026-10-18 options stay in the backup copy of this draft. Each item names the answer.

- **S-Q1. CLI flag for the layer.** Resolved: option (a), `--scope brain|user` only `[decision:settings-scope-flag]` (#589).
- **S-Q2. Default layer of `set`.** Resolved: option (a), #443 literally: the user layer, unless the key allows only the brain layer. No per-key `default_scope` `[decision:settings-set-default-layer]` (#590).
- **S-Q3. `remember.hooks.sync_events` travel.** Moot: the key is removed. Hook entries get an `origins` filter, default `["local"]` `[decision:hook-origins-filter]` (#577, FX-33).
- **S-Q4. What a brain export carries.** Resolved: option (b), every brain-layer row; `device`-sync rows only with `--include-device` `[decision:brain-export-settings-rows]` (#591).
- **S-Q5. Export `settings` shape.** Resolved: option (a), the array of `{key, scope, value, source, updated_at}` in both documents `[decision:export-settings-shape]` (#592).
- **S-Q6. Bucket-prefix merge data in exports.** Resolved: option (b), a separate `merge` member next to `value` `[decision:export-settings-merge-member]` (#593).
- **S-Q7. Scope of plugin enable state.** Per brain, in brain settings `[decision:plugins-install-scope]` (#454). S-Q15 closes the layer: the key is brain-only, so #590 writes the brain with no flag.
- **S-Q8. Default of `plugin.<name>.enabled`.** Resolved: option (a), default `false`; `plugin add` writes `true` after the trust confirmation, source `human` `[decision:plugin-enabled-default]` (#596).
- **S-Q9. Scope of manifest settings.** Resolved by the locked text: option (a), the manifest scope wins `[decision:protocol-oq-1-2]` (#464, OQ-2) (MX-9).
- **S-Q10. Warning code for a budget above the chain cap.** Resolved: Itay's own option, `time_budget_capped` `[decision:warning-time-budget-capped]` (#597). Superseded 2026-10-22: S-06 (#715) and M-22 A (#763) remove the warning.
- **S-Q11. `remember.search.hybrid` and the apps.** Resolved by the locked text: option (a), ask once, then the app writes the user setting `[decision:hybrid-enable-ask-once]` (#446) (MX-9).
- **S-Q12. `fortkeeps.*` key style.** Moot: no `fortkeeps.*` namespace exists. Fortkeeps keeps its UI preferences in its own store `[decision:no-fortkeeps-settings-namespace]` (#571, FX-26).
- **S-Q13. Migration timing.** Resolved: option (a), a v5 → v6 step that ships in 0.3.0 `[decision:release-0-3-0-scope]` (#527, FX-41) with the scope of `[decision:schema-v6-scope]` (#524, FX-6).
- **S-Q14. Where the store keeps user-scope rows.** Resolved: the host passes `user_db_path` in `RememberEnv`. The CLI fills it from HOME and XDG `[decision:remember-env]` (#551, FX-14). The exact path is S-Q16.

### Resolved questions (grill, 2026-10-20)

Transcript of the grill. Design states the winner without this section.

- **S-Q15. Scope and sync of `plugin.<name>.enabled`.** A brain-only key. ("sync `device`" dropped 2026-10-22, M-14 nit 1, per S-02.) One brain's crash loop does not turn the plugin off on another brain or another Mac. #454 and #590 together: a brain-only key writes the brain with no `--scope`.
- **S-Q16. CLI user.db path.** `~/.remember/user.db`. When `XDG_DATA_HOME` is set, `$XDG_DATA_HOME/remember/user.db`. `REMEMBER_USER_DB` overrides both. Mode 0600.
- **S-Q17. `remember.sync.credential_helper`.** The value is the id `env`, `keychain`, or `pass`. Scope `user`, sync `device`. The engine runs one lookup function with three adapters. This amends #423 for this lookup only, and it amends ST-2.8: the key holds an id, not a command. `set` accepts any of the three ids on every OS. Sync fails with `sync_credential_unavailable` when the store is missing, empty, or not on this OS. The token is never logged and is wiped after use. Export stores the id and never the token. A hook run cannot set it (S-Q23, log 010).
- **S-Q18. Bucket default.** Keep 006 E12.3. A new brain shows `project:` and `status:` and writes no row until the first set. The `[]` in the old ST-4.3 table is dropped.
- **S-Q19. Settings export row.** Every row has `key`, `scope`, `value`, `source`, `updated_at`, `device_id`, `version_vector`, and `deleted_at`. An add-wins row also has `merge`. `value` is the visible value. `remember-settings-export` stays version 1. `remember-export` becomes version 2. Import reads brain-export versions 1 and 2.
- **S-Q20. Where merge lives.** Column `settings.merge`, JSON, null when the key is not add-wins. Export copies the column to `merge`.
- **S-Q21. Import failures.** An unknown key or a bad value fails the import and writes nothing (amended 2026-10-25 for both: ST-8.4). Read-only and secret rows are skipped and named. With `--scope`, rows of the other scope are skipped and named. Tombstones, `device_id`, `version_vector`, `updated_at`, and `merge` are restored from the file.
- **S-Q22. `get --scope`.** With no flag, `get` shows the effective value and every layer. With `--scope`, `get` shows only that layer. A missing row is not set and exits 0.
- **S-Q23. Short names.** Only a unique final segment. `rerank.budget` is `setting_unknown`. Did-you-mean is edit distance 2 or less on the full key, at most 3 suggestions, plus the colon-to-dot rewrite.
- **S-Q24. Mechanical rules.** Duration and size require a unit. A fraction is allowed. No spaces. Booleans are `true` or `false` only. A history row is kept only when it is among the newest 50 for that key in that database and it is younger than 90 days. Brain settings have `change_seq`. user.db settings do not. Tombstones count toward 256. History does not. A synced settings write sets source `sync`. Bucket items follow 006 E12.4 through E12.7. Undo of a null old value is `reset` of that layer. Otherwise Undo is `set` of the old value on that layer. `error.fix` is a required non-empty string. The other generated-key scopes are the table in ST-4.3.

### Resolved questions (deep review, 2026-10-21)

From `fk-review/QUESTIONS-2026-10-21.md`, in Itay's words where he wrote words. Stored as `decision:g-NN`. Where a G answer differs from S-Q15 to S-Q24, the G answer wins.

- **G-01.** A, "with a fallback to `~/.remember/` if `XDG_DATA_HOME` is not set." Supersedes S-Q16 for `plugins/` and `brains.json` too.
- **G-02.** A (engine knows only `env`; the host registers more ids). `set` of an unregistered id: "b. We should allow the user to set their own credential helper. If it turns out to be invalid at the time of running, then that's when the error will pop." Supersedes S-Q17.
- **G-03.** A, "to allow overriding the settings for one specific sync or import."
- **G-04.** A: `plugin.<name>.enabled` has scopes `user` and `brain`. Supersedes S-Q15.
- **G-12.** A, "we should be consistent as much as possible."
- **G-14.** "No device ID for user.db."
- **G-15.** "Let's examine what user settings can be not device only." Answered by G2-04 and S-02.
- **G-16.** (1) B, "consistency again". (2) A.
- **G-19, G-20, G-21.** A.
- **G-24.** A: `remember.search.hybrid` is removed.
- **G-25.** A: `remember.search.max_results`.
- **G-33.** (1) A, "one source of data": the memory `source` field is dropped (log 008). (S-04 later gives settings rows `updated_by` too.)
- **G-34.** "all rec, but 7 should print (unset) or (no value)". The log uses `(unset)`.

### Resolved questions (deep review round 2, 2026-10-21)

From `fk-review/QUESTIONS-2026-10-21-round2.md`. Stored as `decision:g2-NN`.

- **G2-01.** A: an id matches `[a-z][a-z0-9_-]{0,39}`. "From my understanding the id is the alias on the machine for the credential helper. Remember will try to run it to get the token." Round 3 (R3-00a) settles who runs it.
- **G2-02.** (1) A: the update table is the standard (log 008). (2) A: a sync apply keeps the author's `updated_by`.
- **G2-03.** A: under `--strict`, every warning exits 4.
- **G2-04.** A: user rows always export. Superseded in part by S-02.
- **G2-12.** A: drop the `hook`, `hook_run_id` and `set_by.hook` history fields.
- **G2-13.** A, "but NULL does not equal a confirmation".
- **G2-14, G2-15.** A.

### Resolved questions (simplification pass, round 3, 2026-10-21)

From `fk-review/QUESTIONS-2026-10-21-round3-simplify.md`. Itay: "Consistency is key, as little special cases as possible. I have no problem with breaking changes in most cases." Stored as `decision:r3-00a`, `decision:r3-00b` and `decision:s-NN`.

- **R3-00a.** B: the git `credential.helper` model. The CLI host runs `remember-credential-<id>`. The engine runs nothing.
- **R3-00b.** Yes: `remember-export` ships as version 1. There is no old reader.
- **S-02.** Yes: no `sync` attribute. Brain rows travel, user rows do not. The crash-loop disable is runtime state. No `secret` class.
- **S-03.** Yes: bucket prefixes are a plain list. No add-wins, no `merge`, no overflow, no first-set defaults.
- **S-04.** Yes: settings rows store `updated_by {kind, id}`.
- **S-05.** Yes: `plugin.<name>.enabled` is an ordinary setting. Trust belongs to the code hash.
- **S-06.** Yes: budgets, grace and priority come from the manifest. `enabled` is the only generated key.
- **S-12.** Yes: full keys only, with a did-you-mean.
- **S-14.** Yes, "but the user can turn off the hooks for local writes and this flag will enable them for this write".
- **S-17.** Open: "lost the plot on the history, sorry" (round 4).

### Resolved questions (round 4, 2026-10-21)

From `fk-review/QUESTIONS-2026-10-21-round4.md`. Stored as `decision:q4-NN`. Q4-01 A (history: the newest 50 rows per key, no age limit). Q4-02 A (integer grammar). Q4-03 A (every plugin write, hooks included, is `tool`). Q4-04 A (`device_id` stays for the tie-break only). Q4-05 "all ok".

### Resolved questions (deep review round 5, 2026-10-21)

- Plugin names and entry names use the settings segment grammar (entry names since 2026-10-22, M-17). A typed hyphen rewrites to `_`.
- `hook add` and `plugin add` both write `enabled` true on the user layer.
- A successful `plugin trust` clears `disabled_reason` `untrusted` (log 010).
- A settings row that import writes keeps the file's `updated_by`.

## Design

Normative. Names: **key** (a setting key), **layer** (`default`, `user`, `brain`, `flag`), **scope** (the layers that a key allows), **registry** (the list of known keys), **actor** (`{kind, id}`, log 008 JE-4.4).

### 1. Keys

- ST-1.1. A key matches `namespace "." segment { "." segment }`. A segment matches `[a-z][a-z0-9_]{0,39}`. A key has at most 96 characters and at most 3 segments after the namespace `[decision:settings-dotted-keys]`. A plugin name is one segment. A manifest setting key is one segment. `plugin add` rejects any other name or setting key with `plugin_manifest_invalid` (log 009).
- ST-1.2. The namespaces are `remember.` (the engine) and `plugin.<plugin name>.` (one installed plugin). No `fortkeeps.` namespace exists. The Fortkeeps apps keep their UI preferences in their own store, device-only in v1 `[decision:no-fortkeeps-settings-namespace]` (#571).
- ST-1.3. Remember normalizes typed input before lookup or storage: `-` becomes `_`. `plugin.url-expand.enabled` is the key `plugin.url_expand.enabled`. `hook add url-expand` creates the plugin `url_expand` (log 010). A manifest is not typed input. Remember does not rewrite it: a hyphen in a manifest plugin name or entry name is `plugin_manifest_invalid`, with a fix that suggests the `_` form (2026-10-22, M-17).
- ST-1.4. Every settings command takes the full key `[decision:s-12]`. Any other input fails with exit 2 and `setting_unknown`. No short name, no colon key, and no alias exists (#445, #485).
- ST-1.5. The `setting_unknown` message offers a did-you-mean. When exactly one key has a final segment equal to the input, Remember offers that key. Otherwise Remember offers at most 3 keys at edit distance 2 or less from the input. With no candidate, the message has no did-you-mean. The plain text is SS §5.1.
- ST-1.6. A key is a value in JSON, never a JSON member name (JSON-STYLE §1.1).

### 2. Layers and scope

- ST-2.1. Remember stores a value in one of two layers: `user` (the `settings` table in user.db) or `brain` (the `settings` table in the brain DB) `[decision:settings-scheme]`.
- ST-2.2. Remember resolves a key in this order, and the last layer with a value wins: `default`, `user`, `brain`, `flag` (#443).
- ST-2.3. Each registry entry lists its scopes: `user`, `brain`, or both `[decision:settings-scope]`.
- ST-2.4. A `set` on a layer that the key does not allow fails with `setting_scope_mismatch`. The `detail` names the allowed layers (#444).
- ST-2.5. These flags apply to one command, and Remember never stores them:
  - `--no-filters` skips every search filter for that search (G-24).
  - `--candidate-pool N` overrides `remember.search.candidate_pool`.
  - `--limit N` overrides `remember.search.max_results`.
  - `--hooks` and `--no-hooks` apply to any write (log 010 H-4.1, `[decision:s-14]`).
  - A flag value outside the key's range fails with `setting_out_of_range`, the same as `set` (G-34.13). The message names the flag and the key, and `detail.flag` holds the flag, for example `--candidate-pool` (2026-10-25, C-06). `detail.key` stays the setting key.
- ST-2.6. A brain-layer row travels with the brain: sync v1 carries it, and a brain export carries it `[decision:s-02]`. No user-layer row travels. The registry has no per-key sync value.
- ST-2.7. Sync merges a brain row by its version vector `[decision:settings-sync]` (#548). A concurrent pair takes the later `updated_at`. A tie takes the larger `device_id`. The loser stays in settings history. A synced apply keeps the author's `updated_by` (G2-02). An invalid value is skipped with the warning `setting_invalid` and is not written (ST-7.15). Each local write to a brain row adds 1 to the local device's counter in its `version_vector`.
- ST-2.8. Remember creates user.db at mode 0600 on the first write to the user layer. The CLI data dir is `$XDG_DATA_HOME/remember/` when `XDG_DATA_HOME` is set, else `~/.remember/` (G-01). The data dir holds `user.db`, `brains.json` (log 010 H-1.6) and `plugins/` (log 010 H-10.1). `REMEMBER_USER_DB` overrides the user.db path only. The Fortkeeps app on macOS computes the same data dir and passes it in `RememberEnv`, so the app and the CLI share one user.db (R2-05). Phones pass the app-container path in `RememberEnv.user_db_path` (#551). The Fortkeeps app passes the same data dir in `RememberEnv`, so the app and the CLI share one user.db (R2-05, #655).

### 3. The sync credential helper

- ST-3.1. No key holds a command or a secret. `remember.sync.credential_helper` holds a helper id. An id matches `[a-z][a-z0-9_-]{0,39}` (G2-01). Any other value fails with `setting_invalid_value`. `set` does not check whether a helper exists (G-02). Scope is `user`. Remember stores no secret (#423).
- ST-3.2. The engine knows one id: `env`. `env` reads `REMEMBER_SYNC_TOKEN`. For every other id, the engine calls the host token callback in `RememberEnv` (log 010 H-0.6) `[decision:g-02]`, R2-04.
- ST-3.3. The CLI is a host. Its token callback runs the helper the way git runs `credential.helper` `[decision:r3-00a]`:
  - The program is `remember-credential-<id>`, found on `PATH`. The argv is `remember-credential-<id>`, `get`. Remember runs it with no shell.
  - stdin is empty. stderr goes to the user's stderr, so a helper can prompt. The token is the first stdout line, with leading and trailing whitespace removed.
  - At 30 seconds, Remember sends SIGKILL to the process group.
  - The callback returns -1 when the program is not on `PATH`. It returns -2 when the program exits non-zero, times out, or prints an empty first line.
- ST-3.4. The Fortkeeps app is a host. Its token callback answers the id `keychain` from its own Keychain item (ST-11.5).
- ST-3.5. When the key has no row, Remember uses `REMEMBER_SYNC_TOKEN` when that variable is non-empty. When the variable is missing or empty, the lookup returns no token and does not fail.
- ST-3.6. When the key is set and no token comes back, sync fails with exit 1 and `sync_credential_unavailable`. The plain message is `the sync credential helper "<id>" did not return a token.` The fix is `Check the helper, or set remember.sync.credential_helper to env and export REMEMBER_SYNC_TOKEN.`
- ST-3.7. The token never appears in stdout, stderr, logs, or JSON. Remember wipes the buffer after the request.

### 4. Storage

- ST-4.1. The brain DB and user.db each have a `settings` table and a `settings_history` table:

```sql
-- brain DB
CREATE TABLE settings (
  key            TEXT PRIMARY KEY,
  value          TEXT NOT NULL,   -- JSON
  updated_by     TEXT NOT NULL,   -- JSON actor {kind, id} (log 008 JE-4.4)
  updated_at     TEXT NOT NULL,
  device_id      TEXT NOT NULL,   -- the device of the last write; the ST-2.7 tie-break only (Q4-04)
  version_vector TEXT NOT NULL,
  deleted_at     TEXT,            -- set by reset (a tombstone)
  change_seq     INTEGER NOT NULL
);
-- user.db
CREATE TABLE settings (
  key TEXT PRIMARY KEY, value TEXT NOT NULL, updated_by TEXT NOT NULL, updated_at TEXT NOT NULL
);
-- both
CREATE TABLE settings_history (
  key TEXT NOT NULL, old_value TEXT, new_value TEXT, updated_by TEXT NOT NULL, at TEXT NOT NULL
);
```

- ST-4.2. `device_id` never filters a row (S-02). user.db has no device id (G-14).
- ST-4.3. Each database keeps its own history. Remember keeps the newest 50 history rows for each key in that database and deletes older ones. No age limit applies `[decision:q4-01]`.
- ST-4.4. A brain holds at most 256 `settings` rows, and user.db holds at most 256 (G-34.9). A tombstone counts. A history row does not. A value holds at most 16 KiB of UTF-8. A write over a limit fails with `quota_exceeded`, `detail.quota` `settings_limit`.
- ST-4.5. Every write to a brain row bumps `brain_meta.change_seq` and stores the new integer in `settings.change_seq` (log 008 JE-5.2). A tombstone bumps it (#543). A user-layer write does not bump it.
- ST-4.6. `reset KEY` without `--scope` acts on the layer that `set` would write (ST-7.2). On the brain layer, `reset` writes a tombstone: it sets `deleted_at`, keeps `value`, and bumps the vector and `change_seq`, so the reset travels. On the user layer, `reset` deletes the row. Resolution (ST-2.2) treats a tombstone as no row. A later `set` clears `deleted_at` on the same row.
- ST-4.7. Undo uses the newest history row for that key in that layer. It runs `set` of that row's `old_value` on the row's layer. When `old_value` is null, Undo runs `reset` on that layer. The history item's `scope` is the layer (ST-7.6).

### 5. Registry

- ST-5.1. The registry is the compiled-in entries plus the entries that Remember generates from each installed plugin manifest.
- ST-5.2. A registry entry has `key`, `type`, a range (`min` and `max`, or `values`, and `max_items` for lists), `default`, `scopes`, `title`, `description`, `effect`, `related`, `since` and `status` (SS §3.4, without `default_scope`, `sync` and `read_only`).
- ST-5.3. The v1 registry:

| Key | Type | Default | Range | Scopes |
|---|---|---|---|---|
| `remember.buckets.prefixes` | string_list | `["project:","status:"]` | any number of items, each a bucket prefix, no duplicates (ST-5.4) | brain |
| `remember.sync.credential_helper` | string | unset | a helper id (ST-3.1) | user |
| `remember.search.candidate_pool` | integer | `200` | 20–2000 | user, brain |
| `remember.search.max_results` | integer | `20` | 1–1000 | user, brain |
| `remember.plugins.chain_budget` | duration | `1.5s` | 100ms–30s | user |
| `remember.plugins.idle_timeout` | duration | `10m` | 1m–24h | user |
| `plugin.<name>.enabled` | boolean | `false` | `true` or `false` | user, brain |
| `plugin.<name>.<setting>` | from the manifest | from the manifest | from the manifest | the manifest scope (#464) |

- ST-5.4. A bucket prefix is 1 to 32 code points of `\p{L}`, `\p{Nd}`, `-`, `_` or `.`, then one `:` (006 E12.4). ASCII letters fold to lowercase. The list has no item limit (2026-10-25, D-01). The 16 KiB value limit (ST-4.4) still applies. Two items that are equal after folding are a duplicate. `remember.buckets.prefixes` merges like every brain row (ST-2.7). It has no add-wins rule, no item tombstones and no overflow `[decision:s-03]`. These rules replace the 006 E12.4 `usage` messages and `too many bucket prefixes (max 8)`. No ninth-prefix `setting_out_of_range` case exists.
  - A bad prefix fails with `setting_invalid_value`. The message is `"<item>" isn't a bucket prefix.` The fix is `Use 1 to 32 letters, digits, "-", "_" or ".", then one ":", like "project:".`
  - A duplicate fails with `setting_invalid_value`. The message is `"<item>" is in the list twice.` When folding made the two items match, the message is `"<item>" and "<other>" are the same prefix.` The fix is `List each prefix once.`
  - `detail` has `key`, `value`, `type`, `allowed` and `item`, plus `other` for a folded duplicate. `<item>` and `<other>` are the items as the user typed them, before folding.
  - `settings set` fails with exit 1 and writes nothing. `settings import` and a brain import skip that row with the warning `setting_invalid` and import the rest (ST-8.4). A sync apply, Cloud included, skips the row with the warning `setting_invalid`, with `detail.reason` `bad_prefix_shape` or `duplicate_prefix` (ST-7.15). The v5 → v6 migration warns the same way (§10 step 2).
- ST-5.5. `remember.plugins.chain_budget` covers every filter entry, warm or cold, including the cold-start grace (#509, PP R9.4). A manifest `idle_timeout` overrides `remember.plugins.idle_timeout` for that plugin (PP R2.20).
- ST-5.6. `remember.search.max_results` limits the final search output after the filter chain (#440). `--limit N` overrides it for one command (G-25).
- ST-5.7. For each installed plugin, Remember generates one key: `plugin.<name>.enabled` `[decision:s-06]`. Each filter's budget, cold-start grace and priority come from its manifest only (PP §2, §9). No user setting changes them.
- ST-5.8. `plugin.<name>.enabled` is an ordinary setting `[decision:s-05]`. `plugin add` and `hook add` write `true` on the user layer after confirmation (log 010 H-10.3, H-10.4). `settings set plugin.<name>.enabled true` fails with `plugin_untrusted` when the plugin's current code hash has no trust record (log 010 H-9.2). `plugin enable NAME [--scope brain]` and `plugin disable NAME [--scope brain]` are shortcuts for `settings set plugin.<name>.enabled true|false` (2026-10-22, M-19 N2). A built-in plugin is trusted.
- ST-5.9. Remember turns each manifest `settings` item into `plugin.<name>.<key>` with the manifest `scope` (#464). A manifest setting key `enabled` makes the manifest invalid (`plugin_manifest_invalid`). The URL-fetch plugin declares its own enrichment setting here (#571).
- ST-5.10. A key of a plugin that is not installed is `setting_unknown`, with the hint `plugin <name> isn't installed`.
- ST-5.11. Remember keeps the rows of a removed plugin. `settings reset --namespace plugin.<name>` resets each stored row by ST-4.6: a brain row becomes a tombstone, and a user row is deleted. The command matches stored rows, not registry entries, so it works when the plugin is not installed.

### 6. Values

- ST-6.1. A duration matches `^[0-9]+(\.[0-9]+)?(ms|s|m|h)$`. A size matches `^[0-9]+(\.[0-9]+)?(B|KiB|MiB)$`. A boolean is `true` or `false`. An integer matches `^(0|[1-9][0-9]*)$`: decimal, with no sign, no leading zero, no `_` and no exponent `[decision:q4-02]`. A bare number for a duration or size, a space, or any other token fails with `setting_invalid_value`. Output gives `value_ms` or `value_bytes` next to `value`.
- ST-6.2. A duration must be a whole number of milliseconds, and a size must be a whole number of bytes. `1.5s` is valid. `0.5ms` fails with `setting_invalid_value` (G-34.1).
- ST-6.3. Remember stores the canonical form (G-34.2). The canonical unit is the largest unit in which the number is at least 1 and has at most 3 fraction digits, exactly. The number has no trailing zeros and no trailing `.`. When no unit qualifies, the unit is `ms` or `B`. Examples: `1500ms` is `1.5s`, `90s` is `1.5m`, `61s` stays `61s`, and `100ms` stays `100ms`. Two inputs with the same `value_ms` or `value_bytes` are the same value.
- ST-6.4. A `string_list` takes one CLI argument per item: `settings set remember.buckets.prefixes project: status: area:`. `settings set KEY --clear` sets an empty list.

### 7. Commands

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

- ST-7.1. `--scope` is the only layer flag `[decision:settings-scope-flag]` (#589).
- ST-7.2. Without `--scope`, `set` and `reset` write the user layer, unless the key allows only the brain layer `[decision:settings-set-default-layer]` (#590).
- ST-7.3. `get` without `--scope` shows the effective value, the winning layer (`from`), every layer value (`layers`) and `updated_by` with `updated_at` (SS §2.4, with `updated_by` in place of `set_by`).
- ST-7.4. `get --scope` and `list --scope` show only that layer. A layer with no row shows `(unset)` and exits 0. In JSON, `value` is null, `from` is the requested scope, and `changed` is false. `get --value` on a layer with no row prints `(unset)` (G-34.7).
- ST-7.5. `list --changed` shows only keys that have a row in some layer (G-34.5).
- ST-7.6. `history` without `--scope` shows the rows of both databases. Each item includes `scope` `user` or `brain`, the database it came from. The list is newest `changed_at` first (JSON field `changed_at`; the ST-4.1 column `at` is unchanged, 2026-10-25, D-03). A tie breaks by `scope` `brain` then `user`, then by `key` in byte order. `--scope` shows one database (G-34.6).
- ST-7.7. A `set` of the value that the layer already stores is a no-op. It writes no history row, does not bump `change_seq`, and exits 0 (G-34.3). `reset` on a layer with no row exits 0, prints `(unset)`, and writes no history row (G-34.4).
- ST-7.8. `reset --namespace` and `reset --all` require `--yes`. Without it, nothing changes. The command fails with `confirmation_required` (2026-10-25, C-11). The refusal gives the number of settings keys that would change (keys, not rows) and the command to confirm.
- ST-7.9. `--as` (in place of `--source`, 2026-10-22, M-19 N8) and `--actor` follow log 008 JE-4.4 and set the row's `updated_by` `[decision:s-04]`. Without them, `updated_by` is `{kind: human, id: null}`. A write that changes a row stores one history row with the same `updated_by`. The no-op in ST-7.7 stores none.
- ST-7.10. A hook or plugin run (`REMEMBER_HOOK_DEPTH` 1 or more) cannot run `settings set`, `settings reset` or `settings import`. The command fails with `setting_from_hook` before key lookup. The plain message is `a hook or plugin can't change settings. Change them yourself in a terminal or in Settings.` (2026-10-25, K-1; replaces the 2026-10-22 M-19 N3 line). `message` is `a hook or plugin can't change settings.` The fix is `Change them yourself in a terminal or in Settings.` `detail` has `key` and `hook`. Log 010 H-5.3 lists the other banned commands.
- ST-7.11. `--json` output is the log 008 envelope with types `setting.list.result`, `setting.get.result`, `setting.set.result`, `setting.reset.result`, `setting.explain.result`, `setting.history.result` and `setting.import.result`. `list` and `history` page by cursor (log 008 JE-2.10, ST-7.14).
- ST-7.11a. Every command that shows a setting uses one **setting object** (2026-10-25, D-03). It extends the ST-7.3 `get` shape:

```json
{"key":"remember.search.candidate_pool","type":"integer","value":400,"from":"brain","changed":true,
 "layers":{"default":{"value":200},"user":null,
           "brain":{"value":400,"updated_by":{"kind":"human","id":null},"updated_at":"2026-10-21T10:04:00.000Z"}}}
```

  - `value` is the effective value. A duration or size also has `value_ms` or `value_bytes` (JSON-STYLE §1.4). Each layer has the same twins.
  - `from` is the winning layer: `default`, `user`, `brain` or `flag`. `changed` is true when the key has a row in some layer (ST-7.5).
  - `layers.<layer>` is null when that layer has no row. A tombstone is null too (ST-4.6). A layer that the key does not allow is absent, so a brain-only key has no `user` member. Only a stored layer has `updated_by` and `updated_at`.
  - With `--scope X` (ST-7.4), `from` is X, `value` is that layer's value or null, and `layers` has only X.
- ST-7.11b. Each type wraps the setting object in `data`:

| Type | `data` | Paging |
|---|---|---|
| `setting.get.result` | `{setting}` | none |
| `setting.set.result` | `{setting, scope, old_value, changed}` | none |
| `setting.reset.result` | `{setting, scope, old_value, changed, action}` | none |
| `setting.list.result` | `{count, settings[]}` (setting objects) | `next_cursor`, `has_more` |
| `setting.history.result` | `{count, changes[]}` | `next_cursor`, `has_more` |
| `setting.import.result` | `{count, written, unchanged, skipped, rows[]}` | none |

  - In `set` and `reset`, `setting` is the object after the write. `scope` is the layer that was written. `old_value` is that layer's value before the write, or null. `changed` is false for the ST-7.7 no-op.
  - `action` is `tombstone` for a brain reset, `delete` for a user reset, and `none` for a layer with no row (ST-4.6, ST-7.7).
  - A history item is `{key, scope, action, old_value, new_value, updated_by, changed_at}`, with `_ms` or `_bytes` twins where the type needs them. `action` is `set`, `tombstone` or `delete`. Remember derives it from the row: a null `new_value` is a reset, a `tombstone` in a brain database and a `delete` in user.db. Any other row is `set`.
  - An import row is `{key, scope, result, action, reason?}`. `result` is `written`, `unchanged`, `skipped` or `kept_local` (the local row won the ST-8.6 comparison). `action` is `set` or `tombstone` for a written row and `none` for any other row. `reason` is present only on `skipped`: `plugin_not_installed`, `other_scope`, `layer_not_allowed` (ST-8.5), `unknown_key` or `invalid_value` (ST-8.4). `written`, `unchanged` and `skipped` are counts. A `kept_local` row counts as `unchanged`. `count` is the length of `rows`. `--dry-run` returns the same shape, where `result` and `action` say what would happen. No row fails the import (ST-8.4).
- ST-7.12. The error and warning codes and their copy are SS §5.1 and §5.3, without `setting_ambiguous`, `setting_read_only`, `time_budget_capped` and `setting_overridden`. `time_budget_capped` is gone for good: a budget larger than the time left in the chain cap is clipped silently, and the clipped value is the `budget_ms` Remember sends (2026-10-22, M-22 A, `decision:2026-10-22-M-22` #763; PP R2.13). No command emits `setting_overridden`. `get` reports the winning layer in `from` (ST-7.3). The code catalog lives in the API surface v1 log `[decision:api-surface-log]` (#583).
- ST-7.13. With `--json`, a failure goes in the one envelope on stdout, and stderr is empty `[decision:json-errors-in-envelope]` (#572).
- ST-7.14. Paging (2026-10-25, D-03; JSON-STYLE §1.11). `list` returns 100 items by default and `history` returns 50. `--limit` is 1 to 1000 for both. A `--limit` outside that range fails with `setting_out_of_range`, and the message names the flag (ST-2.5). `next_cursor` is an opaque string at the top level of the envelope, together with `has_more`, and null on the last page. Inside, it is base64url JSON, and a client must not parse it:
  - `list`: `{"v":1,"k":"<last key>","s":"<scope or all>","c":<--changed flag>}`. The list is sorted by key in byte order.
  - `history`: `{"v":1,"t":"<changed_at of the last item>","s":"<scope>","k":"<key>","r":<row id>}`, in the ST-7.6 order.
  - The cursor carries the filters. A cursor reused with a different `--scope` or `--changed` fails with `invalid_argument`, and so does a bad or old cursor.
- ST-7.15. `setting_invalid` (2026-10-25, C-14). When an invalid value is one row of a batch, the row (or the item) is skipped with the warning `setting_invalid` and the rest of the batch is applied. `detail` has `key`, `value` and `reason`. The reasons are an open list, including `bad_prefix_shape` and `duplicate_prefix` (ST-5.4). The batches are a sync apply, Cloud included, `settings import` and a brain import (ST-8.4), the v5 → v6 migration, and `import --from-db` of E12 tables (ST-10.4). Any command that runs one of these batches prints it, exit 0 (4 with `--strict`). The plain line is `remember: warning: skipped "<key>": "<value>" isn't a valid value. This brain wasn't changed.` (2026-10-25, C-14). `detail.reason` includes `bad_prefix_shape`. When the value is the subject of one command, as in `settings set`, it is an error: `setting_invalid_value`, exit 1, not this warning.
- ST-7.16. Dates in human output (2026-10-25, C-13). `get`, `list`, `history` and `explain` show an instant in the short local form `21 Oct at 12:04`: the local time zone, a 24-hour clock and English month names. When the year is not the current year, the year is shown: `21 Oct 2025 at 12:04`. When the local zone can't be read, Remember uses UTC and adds ` UTC`: `21 Oct at 10:04 UTC`. JSON always uses the RFC 3339 UTC `_at` fields (JSON-STYLE §1.3). Scripts and agents read `--json`, and the plain text is not a stable interface. Every other CLI command that prints a date uses this form.

### 8. Export and import

- ST-8.1. `remember settings export` prints one plain document `[decision:json-export-plain]`:

```json
{"format":"remember-settings-export","version":1,"exported_at":"2026-10-16T08:00:00.000Z",
 "settings":[{"key":"remember.plugins.chain_budget","scope":"user","value":"2s","value_ms":2000,"updated_by":{"kind":"human","id":null},"updated_at":"2026-10-16T07:58:00.000Z","device_id":"","version_vector":{},"deleted_at":null}]}
```

- ST-8.2. Each element has `key`, `scope`, `value`, `updated_by`, `updated_at`, `device_id`, `version_vector` and `deleted_at`. A duration or size row also has `value_ms` or `value_bytes`. A user row exports `device_id` `""`, `version_vector` `{}` and `deleted_at` null (G2-15). `settings` is sorted by `key`, then `scope`. `--scope` defaults to `all`. `remember.sync.credential_helper` appears as its id.
- ST-8.3. `remember export` writes `remember-export` version 1 (log 008 JE-7.2, `[decision:r3-00b]`). It carries every brain row of that brain, tombstones included, in the ST-8.2 shape (#508, #591). User rows never appear.
- ST-8.4. `settings import` validates the whole file first. An unknown key is skipped with the warning `setting_import_skipped`, reason `unknown_key`, and the import continues. A bad value is a warning: the row is skipped with `setting_invalid`, its import row has `result` `skipped` and `reason` `invalid_value`, and the import continues (2026-10-25, Itay, overrides S-Q21, #443). A `version` above 1 fails with the JSON-STYLE §1.7 message.
- ST-8.5. `settings import` skips three kinds of row, names each skipped row, and writes the rest (G-16):
  - a row of a plugin that is not installed;
  - with `--scope`, a row of the other scope;
  - a row that the target layer does not allow.

  Each skipped row is the warning `setting_import_skipped` (2026-10-25, C-12), with `detail` `key`, `scope` and `reason` (`plugin_not_installed`, `other_scope`, `layer_not_allowed` or `unknown_key`), and an import row with `result` `skipped` (ST-7.11b).
- ST-8.6. When the target has no row for that key, import writes the file row. When the target has a row, the winner is the ST-2.7 comparison of version vector and `updated_at` only. A user row has no vector, so the later `updated_at` wins. When `updated_at` ties and `device_id` does not pick a winner, the local row stays. The losing value stays in history `[decision:g-16]`.
- ST-8.7. A row that import writes keeps the file's `updated_by`. The history row for that write stores the same actor. Import does not replace it with `{kind: import, id: null}`. Memory import is log 008 JE-4.4 and is unchanged.

### 9. API, MCP and agents

- ST-9.1. HTTP has `GET /v1/settings` and `GET /v1/settings/{key}`. MCP has one tool, `remember_settings`, with `action` `list`, `get` or `explain` `[decision:settings-api-readonly]`. The route and tool schemas live in the API surface v1 log `[decision:api-surface-log]` (#583), `[decision:mcp-tool-catalog]` (#539).
- ST-9.2. No HTTP or MCP call changes a setting.
- ST-9.3. The library call `remember_call` accepts `setting.set`, `setting.reset` and `setting.import`. That is how the Fortkeeps apps write settings. #447 applies to the remote surfaces (HTTP and MCP) `[decision:g-21]`.
- ST-9.4. The CLI treats humans and agents the same (PRINCIPLES 7). The history shows who wrote each value.
- ST-9.5. Agents keep read access to settings, the app's UI preferences included, read-only (ST-11.5) `[decision:2026-10-22-agent-settings-read]` (#739, 2026-10-22).
- ST-9.6. Plugins read settings, never write them. Each request carries the plugin's own `plugin.<name>.*` values and each `remember.*` key its entry lists in `permissions.needs`, for example `remember.search.max_results` (PP R2.16, R3.10). No protocol message writes a setting, and a run that calls `settings set`, `reset` or `import` fails with `setting_from_hook` (ST-7.10) `[decision:2026-10-22-plugin-settings-read]` (#740, 2026-10-22).

### 10. Migration (schema v6, shared with logs 008 and 010)

The v5 → v6 step runs on open. It ships in 0.3.0 `[decision:release-0-3-0-scope]` (#527). v6 is not additive `[decision:schema-v6-scope]` (#524). The order is:

1. Before the transaction, copy each sidecar file into `<brain>.d/` `[decision:g-19]`. The device id file is `<brain>.d/device_id`. Keep the old files.
2. In one `BEGIN IMMEDIATE` transaction, run log 008 §7, then the settings steps below, then log 010 §17 steps 1 to 3, then set `user_version = 6`. No earlier step sets `user_version`.
3. After `COMMIT`, delete the old sidecar files.
4. On every open of a v6 brain: when `<brain>.d/device_id` is missing and `<brain>.device_id` exists, Remember finishes steps 1 and 3 before it reads the device id.

The settings steps:

1. Create the ST-4.1 tables under temporary names.
2. When the E12 tables hold any prefix row, write one `remember.buckets.prefixes` row. `value` is every prefix of the 006 E12.7 effective list, in that order, with no item limit (2026-10-25, D-01). A prefix that fails ST-5.4 is left out of the row and warns `setting_invalid` with `detail.reason` `bad_prefix_shape` and `detail.item` the prefix. The plain line is `remember: warning: dropped "<item>" from "remember.buckets.prefixes": it isn't a bucket prefix.` (2026-10-25, C-05). A prefix is never dropped silently. When every prefix fails, no row is written. `updated_by` is `{kind: human, id: null}`. `updated_at` is the old order time, or the migration time when there is none. `device_id` is the local device id. `version_vector` is `{"<local device id>":1}`. `change_seq` is 1, and `brain_meta.change_seq` becomes 1 (G-34.12, S-03).
3. Drop the E12 tables `settings` and `bucket_prefixes`.
4. Rename the new tables to `settings` and `settings_history`.

- ST-10.1. A brain with empty E12 tables gets empty new tables and no row.
- ST-10.2. The v6 step stores `0.3.0` in `brain_meta.min_reader_version` (log 008 §7 step 3) `[decision:g-20]`. 0.2.0 refuses the file on `user_version`. From 0.3.0 on, an engine older than a brain's `min_reader_version` opens it read-only. A write then fails with exit 1 and `schema_too_new`. The plain message is `this brain was made by a newer version of remember, so it is read-only here. You can still read and export it. Upgrade remember to make changes.` (2026-10-25, K-1; replaces the #565 line). `message` is `this brain was made by a newer version of remember, so it is read-only here. You can still read and export it.` The fix is `Upgrade remember to make changes.`
- ST-10.3. user.db has `PRAGMA user_version = 1` and a one-row `user_meta(min_reader_version TEXT NOT NULL)` table with `0.3.0` (G-20).
- ST-10.4. `import --from-db` reads v4, v5 and v6 sources. It reads bucket prefixes from the E12 tables (as step 2 does) or from the v6 row.

### 11. Fortkeeps apps (cross-repo)

- ST-11.1. The Settings window builds its rows from `remember settings explain --all --json` (SS §3.4).
- ST-11.2. The app writes settings only through `remember_call` (ST-9.3), with `updated_by` `{kind: human, id: null}`.
- ST-11.3. Next to a value whose `updated_by.kind` is `agent`, the app shows `Changed by an agent, 2 hours ago` and **Undo**. Next to a value whose `updated_by.kind` is `tool`, the app shows `Changed by <name>, 2 hours ago` and **Undo**, where `<name>` is the plugin name from `updated_by.id` `[decision:q4-03]`. **Undo** follows ST-4.7.
- ST-11.4. The app asks the hybrid question only when the user layer has no `plugin.hybrid.enabled` row. The dialog carries the log 010 H-9.6 sentence (G2-14). **Yes** sets `plugin.hybrid.enabled` to `true` on the user layer. **No** sets it to `false` on the user layer, so the app does not ask again. Closing the window writes nothing, and the app asks again on the next launch (#446 as amended by G-24).
- ST-11.5. The app keeps its own UI preferences in its own store, device-only in v1 (#571), and exposes them to agents read-only; only the GUI writes them, tagged `human` (amended 2026-10-22, #739). The app keeps the Cloud token in its own Keychain item, answers the id `keychain` through its token callback (ST-3.4), and sets `remember.sync.credential_helper` to `keychain`. Remember stores the id and never the token.

## Cross-repo dependencies

| Repo | Depends on this log | Change |
|---|---|---|
| remember | itself | Plan 19 becomes `remember settings`; schema v6; registry; user.db; export and import plans 20 and 21. |
| remember-mac | ST-7, ST-8, ST-11 | Settings window rows from the registry (log 011), settings writes through `remember_call` (ST-9.3), the hybrid ask-once prompt, the Plugins pane, the `keychain` token callback (ST-3.4). The app reads user.db (ST-2.8) through the engine only. |
| fortkeeps-cloud | ST-9 | `GET /v1/settings` and `GET /v1/settings/{key}` only. No HTTP call writes a setting (ST-9.2). Cloud log 001 has no settings route today. Settings sync v1 (#548). |

## Examples

```
$ remember settings set candidate_pool 400
remember: unknown setting "candidate_pool". Did you mean "remember.search.candidate_pool"? Run 'remember settings list' to see all settings.
$ remember settings set remember.search.candidate_pool 400 --scope brain
$ remember settings get remember.search.candidate_pool
remember.search.candidate_pool = 400
  from brain, set by human on 21 Oct at 12:04
  user: (unset) · default: 200
```

```
$ remember settings set remember.buckets.prefixes project: status: area:
$ remember plugin enable hybrid --scope brain
```

## Trade-offs

- A user DB over per-brain only: one value for all brains. The cost is a second file and a sandbox grant for the Mac app.
- A registry over free keys: the table cannot become a hidden note store. The cost is a release for each new key.
- No settings file to edit: one source of truth, validated writes, history. The cost is that a user cannot `vi` a setting.
- One travel rule (brain rows travel, user rows stay) over a per-key sync value: no device filter and no hidden rows. The cost is that a brain-layer value applies on every device of that brain.
- A plain list for bucket prefixes over add-wins: one merge rule. The cost is that a concurrent edit on two devices keeps one list (the other stays in history).
- Manifest-only budgets over user budgets: one place for each number. The cost is that a user tunes only the chain cap.

## Verification Criteria

1. `set` on each layer, then `get`: `from` and `layers` match ST-2.2.
2. `candidate_pool` is `setting_unknown` (exit 2) with the did-you-mean `remember.search.candidate_pool`. `remember:search/candidate_pool` is `setting_unknown`.
3. `-` input stores `_`.
4. `set --scope user` on a brain-only key fails with `setting_scope_mismatch`.
5. A brain row set on device A appears on device B after sync. A user row never leaves user.db.
6. A bare number for a duration key fails with `setting_invalid_value`.
7. `remember.plugins.chain_budget` of 45s fails with `setting_out_of_range`.
8. A manifest with a setting key `enabled` is `plugin_manifest_invalid`.
9. Any `settings set` with `REMEMBER_HOOK_DEPTH=1` fails with `setting_from_hook`, including a known key. The message is the ST-7.10 sentence.
10. `settings set plugin.hybrid.enabled true` works (built-in, trusted). For an added plugin whose folder changed after trust, it fails with `plugin_untrusted`.
11. `settings export` then `settings import` into an empty user.db gives an equal export, except `exported_at` and `updated_by`.
12. A file with one bad value imports every other row, warns `setting_invalid`, and gives that row `result` `skipped`, `reason` `invalid_value`. A file with one unknown key imports every other row, warns `setting_import_skipped`, and gives that row `reason` `unknown_key`.
13. A brain export holds no user row.
14. v5 → v6: a brain with bucket prefixes gets one `remember.buckets.prefixes` row equal to the old E12.7 effective list, with no item limit. A brain with 10 prefixes keeps 10. A bad old prefix is left out and warns `setting_invalid` with reason `bad_prefix_shape`.
15. HTTP and MCP have no settings write.
16. After 51 writes of one key, history holds the newest 50 rows of that key. A 2-year-old row among the newest 50 stays.
17. `settings set remember.buckets.prefixes project: area:` without `--scope` writes the brain layer. `--scope user` fails with `setting_scope_mismatch`. A new brain with no row shows `project:` and `status:`.
18. `reset` of a brain row writes a tombstone that syncs, and `get` then shows the user or default value.
19. No `fortkeeps.*` key exists: `settings get fortkeeps.library.link_enrichment` is `setting_unknown` (#571).
20. Every brain-layer settings write bumps `change_seq` (#543).
21. `settings get --scope user` on a key that has only a brain row prints `(unset)` and exits 0. JSON `value` is null.
22. `remember.sync.credential_helper` rejects `~/bin/helper` and accepts `env` and `pass`. With `pass` and `remember-credential-pass` on `PATH`, sync reads its first stdout line. With no such program, sync fails with `sync_credential_unavailable`.
23. `plugin enable hybrid` writes the user layer. `plugin disable hybrid --scope brain` turns it off in this brain only, and another brain still runs it.
24. A user.db row written while brain A is open is effective when brain B is open.
25. `set` of `1500ms`, then `get --value`, prints `1.5s`. `0.5ms` fails with `setting_invalid_value`. A second `set` of the same value writes no history row.
26. `settings import` of a file with rows of a plugin that is not installed writes the other rows and names the skipped ones. Importing an older brain row over a newer local row keeps the local value.
27. A crash after the v6 sidecar copy and before `COMMIT` leaves `<brain>.device_id` in place, and the next open keeps the same device id.
28. `--limit 5` returns at most 5 results. `remember.search.max_results` set to 3 returns at most 3 results without `--limit`.
29. A settings write with `--as agent --actor bot1` stores `updated_by` `{agent, bot1}` in the row and in history.
30. `hook add url-expand` creates the plugin `url_expand` and writes `plugin.url_expand.enabled` true on the user layer. A manifest whose name is `url-expand` fails with `plugin_manifest_invalid`.
31. `settings import` of a newer file row writes that row's `updated_by` unchanged. An older file row leaves the local row and its `updated_by` in place. Two user rows with the same `updated_at` keep the local row.
32. `history` without `--scope` includes `scope` on every item. Undo of a brain item resets or sets the brain layer.
33. A filter whose `permissions.needs` lists `remember.search.max_results` receives its effective value in `settings`. A run of that plugin that calls `settings set` fails with `setting_from_hook` and writes nothing (ST-9.6).
34. `settings set remember.buckets.prefixes` with 20 valid prefixes succeeds. `my project:` fails with `setting_invalid_value` and the ST-5.4 bad-prefix message. `status: Status:` fails with the folded-duplicate message. A synced row with a bad prefix is skipped with `setting_invalid`, and the brain row is unchanged.
35. `set`, `reset`, `get` and `list` with `--json` carry the same setting object (ST-7.11a). `reset` of a brain row gives `action` `tombstone`, of a user row `delete`, and of a missing row `none`. History items and import rows carry `action`.
36. `settings list` returns 100 items and a `next_cursor` when more exist. `history` returns 50. `--limit 0` and `--limit 1001` fail with `setting_out_of_range`, naming the flag. A list cursor reused with a different `--scope` fails with `invalid_argument`.
37. Human `history` prints `21 Oct at 12:04` for a row of this year and `21 Oct 2025 at 12:04` for an older row. `--json` prints `changed_at` in RFC 3339 UTC.
38. `settings import` of a file with a row of an uninstalled plugin warns `setting_import_skipped` with reason `plugin_not_installed`.
39. `reset --all --scope user` without `--yes` fails with `confirmation_required`, changes nothing, and names the number of keys.

## Known Limitations

- A same-user program can write `user.db` and the brain DB with SQL. Validation runs only through Remember.
- History is device-local. A synced change is a history row only on the device that applied it.
- A concurrent edit of one brain row on two devices keeps one value. The other stays in that device's history.

## Future Work

- **Out of scope / see future log:** per-bot tokens and the MCP activity log (roadmap §16.6), and the docs line "locally this is not a security boundary" (roadmap §16.4), will be covered in the upcoming local MCP and bot-auth design log, which is not written yet (Business Developer re-review nit, 2026-10-22).

- Settings sync wire details (the sync protocol log). The merge rule is locked (#548).
- Writable settings over MCP and HTTP with a person in the loop.
- A settings UI on the phones beyond the Fortkeeps rows.
- Web (remote HTTP) plugins and their settings (log 010 Future Work).

## Implementation plan

The plans are [`implementation-plans/19-schema-v6.md`](../implementation-plans/19-schema-v6.md) and [`implementation-plans/20-settings.md`](../implementation-plans/20-settings.md). This log does not contain a second plan. HTTP route schemas stay in the API surface v1 log. The Mac app stays in remember-mac.
