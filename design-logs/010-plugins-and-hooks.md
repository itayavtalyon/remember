# 010 - Plugins and hooks

**Status:** Design (grill closed 2026-10-21). Amended 2026-10-22: the daemon stays in v1 (S-11 reversed), Empty Trash has no Touch ID, `--trash` means the Trash. 2026-10-22 merge answers applied: M-13, M-14, M-17 to M-21. H-Q1 to H-Q25 closed 2026-10-20. The deep review of 2026-10-21 (G-01 to G-36) reopened the log. Rounds 2 and 3 (simplification) closed the same day. Round 4 closed the same day. The grill is closed. Design states each winner. Daemon IPC is Future Work (S-11).
**Changes:** 2026-10-25 copy final (`ANSWERS-FINAL.md`, K-1): H-2.3, H-5.5, H-9.4, H-12.8 and H-16.7 use the Story Weaver lines. Earlier the same day (Itay, copy-pending answers 2, 4, 5 and 13; PP revision 7): a pre-flight hash mismatch warns `plugin_untrusted`, not `plugin_preflight_failed` (C-20, H-6.2); a capped chain warns `plugin_chain_capped` as well as setting the status (D-02, H-12.4); the non-timeout filter code is `plugin_filter_failed` (D-08, H-12.5); dispatch in a `REMEMBER_PROCESSES=0` build is a warning, and the write is saved (C-23, H-0.7); action timeout and failure warnings print only in a command that runs the action synchronously (C-30/C-31, H-6.4). 2026-10-25 (Itay, `chief-architect-unblock` D-14, D-17): one `skipped_source` line per skipped action and event type, with `at` and `event_type` (H-7.2, H-15.3); the commands that print `plugin_action_timeout` and `plugin_action_failed` are named, and `plugin rerun` is synchronous (H-5.7), and no other command prints them (H-6.4, PP R3.21, R8.16).
**Author:** Itay (+ agent facilitation)
**Created:** 2026-10-15 (as 007-event-hooks), merged and renumbered 2026-10-18

Cross-refs: [001](001-foundations.md), [002](002-core-facade.md), [003](003-ttl.md), [004](004-related-memories.md),
[005](005-sync-foundations.md), [006](006-ui-overhaul-engine.md), [007](007-settings.md), [008](008-json-envelope.md),
[009](009-plugin-protocol.md) and its spec `docs/PLUGIN-PROTOCOL.md` (cited **PP**, revision 7).
remember-mac [003](../../remember-mac/design-logs/003-url-fetch.md) (URL fetch, moves into a plugin, §14).
Sources: `fk-review/HOOKS-INTERFACE.md` (cited **HK**), `fk-review/SETTINGS-SCHEME.md` (**SS**), `fk-review/JSON-STYLE.md`
(**JS**), the old draft `drafts/remember/design-logs/007-event-hooks.md` (**old 007**), `fortkeeps/PRINCIPLES.md`, and the
locked decisions named as `[key]` (#id). Where HK and a locked decision differ, the decision wins. The resolved questions list each case.

> Drafting note: the live grill rule (#81) was read on 2026-10-18 from Itay's Mac (read-only). The old 007 draft did not
> read it. This log replaces old 007. Removed from old 007, because locked decisions replace them: wait mode (§6),
> terminal mode and the terminal lookup (§8), Keychain, libsecret and polkit for trust records (§9), "link and unlink emit no event"
> (E1.1), the sweep before each command (E1.3a), the bare `remember-event` with `entries[0]` (§2), the `device` scope
> and `unset` (§11), and the colon key in the migration (§12 step 4). Fixed: test 19 (§Verification).

## Background

Baseline: schema `user_version` 5 (log 006). Verified facts:

- Every write goes through `remember_run` (log 002). The CLI and the Fortkeeps apps share that entrypoint.
- Expired is a bin derived from the clock at read time (003, 005). The engine stores no state change when a memory expires.
- `store_search` (box copy of `src/store_sqlite.c`, `search_query_exec`) runs one `COUNT` and one `SELECT` of full rows, ordered by `bm25(entries_fts)`, with `LIMIT` and `OFFSET`. It has no stage that returns ids only.
- remember-mac log 003 (Done) fetches page metadata in the main app and **updates the body** of the saved memory (`store.update(body:)`).
- PRINCIPLES: local hooks and plugins are in Remember (free). The hooks and plugins UI is in the Fortkeeps apps. Webhooks are in Cloud. Hybrid search is a Remember built-in; URL expansion is a Fortkeeps plugin. Plugins never change writes.
- `[decision:design-log-split]` (#485): one log for plugins and hooks, merged with the old 007.

## Problem

Agents on one Mac want to hand work to each other through memories, without polling. Users want better search (hybrid) and richer saved links (URL expansion) without making Remember slower, less reliable or OS-specific. Both needs run a user's or a vendor's program when something happens in a brain. One mechanism must do this:

- A write never waits for, fails because of, or changes because of a hook or plugin.
- No memory data chooses what runs.
- A program runs only after a human approves it.
- Search always returns, also when a plugin is slow, cold or broken.
- The engine stays OS-agnostic (`principle:engine-os-agnostic` #418).

## Questions and Answers

Locked decisions that shape this log (words of the decisions, shortened):

Hooks:
- `[decision:hooks-sweep-runner]` (#415), still in force for: one memory per event, no batch events; link and unlink send events; `memory.expired` fires every time a memory expires. Its runner part is superseded by #423.
- `[decision:hooks-never-wait]` (#417): neither the app nor the CLI waits for a hook. Hooks always run after commit.
- `[decision:hooks-os-agnostic-relock]` (#423): Remember exposes `remember hook sweep` and installs no background job. Fortkeeps installs a launchd agent. "The registry of brains moves to Fortkeeps." (The registry clause is amended by #533, see below.) Terminal mode leaves Remember. Remember keeps an OS-neutral hash check plus `remember hook trust` confirmed by typing. No Keychain, no libsecret. Supersedes #415 (runner) and `[decision:hooks-job-registry]` (#416).
- `[decision:hooks-log-rotation]` (#424): `<brain>.hooks.log` (now in `<brain>.d/`, #566), rotate at 5 MB, keep 3, delete entries older than 30 days.
- `principle:plugins-never-change-writes` (#425).
- `[decision:hooks-exit-code-4]` (#426): after the commit, pre-flight checks per matching hook (file exists, executable, hash matches). A failed check skips the hook, logs it, and exits 4 with a warning. A dispatcher that cannot launch also gives exit 4. The CLI never waits for a handler. Its exit-4 part is superseded for writes by `[decision:plugins-degraded-exit-code]` (#516) and `[decision:strict-warnings]` (#574): exit 0 with the warning, 4 with `--strict`.
- `[decision:hooks-plugins-one-mechanism]` (#452): hooks and plugin actions share the outbox, dispatcher, payload, trust check, logs, exit code 4 and chain limit.
- `[decision:hooks-registration-package]` (#453): a plugin is a folder with a manifest and a folder hash; a hook is a local entry in `hooks.json` with a script hash and actions only. `remember hook list` shows both. `remember hook package <name> <dir>` turns hooks into a plugin folder.
- HK decisions of 2026-10-14 to 2026-10-16: eight events; outbox and detached dispatcher; at most 4 runs per brain; at-least-once; 7-day stale drop; the runaway kill switch at chain length 100; handlers own echo-skipping; 256 KiB event data; typed approval; stdout of a run is discarded (OQ-13). (`remember.hooks.sync_events` `off`/`each` is replaced by #577.)

Plugins:
- `[decision:plugins-process-model]` (#427), `[decision:plugins-process-kinds]` (#438): one JSON protocol; `once` and `persistent` processes; every plugin is isolated.
- `[decision:plugins-filter-lifecycle]` (#428), `[decision:plugins-liveness]` (#434), `[decision:plugins-runtime-dir]` (#431): start on first use, reuse through a local socket, stop after idle; alive only if `ping` answers in about 200 ms; a lock while starting; one state file per process.
- `[decision:plugins-manifest]` (#429): a folder with `plugin.json`; `remember plugin add <dir>`; a hash and a typed trust confirmation.
- `[decision:plugins-filter-budget]` (#430), `[decision:plugins-budget-defaults]` (#432), `[decision:plugins-chain-cap]` (#436), `[decision:plugins-budget-clip]` (#442), `[decision:settings-chain-cap-covers-grace]` (#509), `[decision:settings-entry-budget-key]` (#511): budgets (log 007 §5).
- `[decision:plugins-strikes]` (#433): a timeout or crash returns BM25 results with exit 4 (exit 0 with a warning since #516; 4 with `--strict`, #574); 3 consecutive strikes stop the process; 3 crashes in 10 minutes disable the plugin until `remember plugin enable`.
- `[decision:plugins-filter-chain]` (#435): filters on one point run as an ordered chain by priority; a failed filter is skipped and its input passes on.
- `[decision:plugins-filter-points]` (#439), `[decision:plugins-filter-needs]` (#463): `search.query` and `search.results`. "Search runs in three stages: find candidate ids and BM25 scores; load only the fields the chain needs and run the filter chain ...; load the final memories by id."
- `[decision:plugins-candidate-pool]` (#440), `[decision:search-no-paging]` (#441), `[decision:plugins-result-provenance]` (#462).
- `[decision:plugins-default-enabled]` (#448): built-ins ship disabled; an added plugin is enabled after the typed trust.
- `[decision:plugins-install-scope]` (#454): install and trust once per user; each brain keeps its own enabled list in its brain settings; a listed plugin that is not installed shows as missing (a warning).
- `[decision:plugins-sidecar-data]` (#456), `[decision:plugins-backfill]` (#457), `[decision:plugins-rebuild-policy]` (#460), `[decision:plugins-partial-index-owned-by-plugin]` (#461), `[decision:protocol-nq-16]` (#507), `[decision:protocol-cf-10]` (#513), `[decision:protocol-cf-6]` (#514), `[decision:protocol-index-version-report]` (#515).
- `[decision:plugins-mobile-builtin-only]` (#458), `[decision:plugins-open-core-adapters]` (#459), `[decision:plugins-open-builtins]` (#484).
- `[decision:url-expansion-linked-memory]` (#449): the URL expansion plugin stores fetched content in a new linked memory tagged `source:url`, never edits the original, and skips its own memories with `not_caused_by`. "Today remember-mac edits the original memory and appends the content, so that code must change."
- `[decision:hybrid-enable-ask-once]` (#446).

Decisions of 2026-10-18 and 2026-10-19 (Itay) that change this log:
- `[decision:plugins-degraded-exit-code]` (#516), `[decision:strict-warnings]` (#574): a degraded search or write exits 0 with the warning. With `--strict` or `REMEMBER_STRICT=1`, every plugin and hook degradation warning gives exit 4, including `hook_preflight_failed` and `hook_dispatch_failed` on writes. Exit 4 by default stays only where a command's own job failed in part (`plugin reindex`, the `plugin enable` backfill). *(Superseded 2026-10-22: S-09 changed this; an incomplete `plugin reindex` or `plugin enable` backfill exits 1 with `plugin_backfill_incomplete`, and exit 4 means only that strict saw a warning.)*
- `[decision:remember-daemon]` (#517), `[decision:remember-daemon-scope]` (#518), `[decision:daemon-jobs-registry]` (#533): an optional daemon owns warm filter processes, backfills, timed sync, the hourly expiry sweep and the hook dispatcher queue. Without it, the CLI does these jobs. "Remember keeps the brain registry in an OS-neutral file in the user data dir (amends the registry clause of 423)." Fortkeeps installs one LaunchAgent that keeps `remember daemon` alive. Supersedes the no-daemon part of #507.
- `[decision:hook-origins-filter]` (#577): every event carries `cause.origin` (`local`, `sync` or `import`) and the actor. `hooks.json` entries get an `origins` filter, default `["local"]`. Replaces `remember.hooks.sync_events`.
- `[decision:provenance-actors]` (#563): `created_by` and `updated_by` actor objects on every entry.
- `[decision:expiry-local-marker]` (#570): `expired_at` is a device-local marker. It never bumps `updated_at`, the version vector or `change_seq`, and never syncs.
- `[decision:brain-sidecar-dir]` (#566): one sibling folder `<brain>.d/` holds every per-brain file (device id, hooks config, trust and log, plugin sidecars, plugin state), migrated by the v6 step.
- `[decision:schema-v6-scope]` (#524): one v6 step carries logs 007, 008 and 010 (`hook_outbox`, `expired_at`).
- `[decision:change-seq]` (#543), `[decision:protocol-backfill-seq]` (#599): the hook outbox and plugins read changes since a `change_seq`. No version vectors in the plugin protocol.
- `[decision:purge-command]` (#573): `remember purge <locator>` is the only permanent wipe. `delete --deleted` is removed outright. `delete --expired` moves expired records to the Trash (the Deleted bin).
- `[decision:plugin-cli-table]` (#602), `[decision:plugin-dev-mode]` (#603), `[decision:plugin-protocol-small-fixes]` (#604), `[decision:plugin-version-glossary]` (#605), `[decision:plugin-manifest-errors]` (#606), `[decision:plugin-permission-prompt]` (#607), `[decision:plugin-provenance-human]` (#608), `[decision:plugin-cold-start-warmup]` (#600), `[decision:plugin-enabled-default]` (#596): PP revision 5.
- `[decision:url-core-language]` (#578): the URL fetch core is C, on the engine platform layer and the FX-15 C ABI, in the public repo. HTTP goes through the host transport adapter (#544). Head-only with a fixed byte cap. A vetted open-source C HTML5 parser, a CI fuzz target, the SSRF policy in the core.
- `[decision:mac-url-enrichment-linked]` (#580): the remember-mac URL enrichment now writes a separate linked memory tagged `source:url` and never changes the user's write. When the URL plugin ships, the app's coordinator is removed.
- `[decision:no-fortkeeps-settings-namespace]` (#571): `fortkeeps.library.link_enrichment` becomes the URL-fetch plugin's own setting.
- `[decision:builtin-plugins-phone]` (#567), `[decision:builtin-plugins-phone-host-bridge]` (#568): built-ins on phones are C-ABI libraries on a worker thread; the Fortkeeps app schedules long jobs in the OS background slots.
- `[decision:bundle-id-namespace]` (#581), `[decision:bundle-id-roots]` (#582): launchd labels come from one constant per repo. The open-source daemon uses `io.github.itayavtalyon.remember`; the Fortkeeps apps use `app.fortkeeps.*`.

### Resolved questions (2026-10-19)

- **H-Q1. Exit code 4 for hooks.** Moot: a failed pre-flight or dispatch is a warning with exit 0, and 4 only with `--strict` (#516, #574, MX-5). HK §3.5a "no exit code 4" is now right for the default.
- **H-Q4. Default layer of `remember.hooks.sync_events`.** Moot: the key is removed; hook entries get an `origins` filter (#577).
- **H-Q5. Expiry sweep and sync.** Resolved: option (a), a device-local marker `[decision:expiry-local-marker]` (#570, FX-24).
- **H-Q7. Where Remember stores the reported `index_version` and pending backfills.** Plugin state lives in `<brain>.d/plugins/<name>/` (#566). H-Q15 names the file `index.json`.
- **H-Q8. Who runs a pending backfill on desktop.** Resolved: the daemon runs it when it runs; without the daemon, the next `plugin reindex`, `plugin enable` or app launch does `[decision:remember-daemon-scope]` (#518, MX-6).
- **H-Q12. Warning prefix.** Resolved: `remember: warning: <text>` (#409 locked, MX-7). HK, SS and JS are fixed in the same update.

### Resolved questions (grill, 2026-10-20)

- **H-Q2.** #533 wins. Remember writes `<user-data>/brains.json` beside user.db. Fortkeeps does not keep a second list. The file is `{"format":"remember-brains","version":1,"brains":[{"path":"/absolute","added_at":"...Z"}]}`, mode 0600. `hook add` inserts the path once. `hook remove` deletes that path. `hook sweep --all` reads only this file. A missing path warns `brain_missing` and the row stays. The plain warning is `brain "<path>" is in the registry and is not on disk.`
- **H-Q3.** `hook add` stores the hook, sets `needs_trust`, and does not prompt. The hook does not run until `hook trust`. `plugin add` and `plugin enable` do ask, because #607 requires the permission prompt.
- **H-Q6.** Restore clears `expired_at` and emits `memory.restored`. The post-command sweep then emits `memory.expired` when `expires_at` is still due. Two events, two transactions. The next command emits nothing for that row.
- **H-Q9.** A plugin action creates a memory by running the `remember` CLI with `REMEMBER_DB`. Remember does not add a write reply. The engine forces source `tool` (log 008 J-Q9).
- **H-Q10.** `cause.hook` for a plugin action is `plugin/<name>/<action>`. There is no `cause.plugin` field. `not_caused_by` matches that string.
- **H-Q11.** The detached dispatcher retries exit 75 once, after 60 seconds. The CLI has already exited. The 4-wide slot is free during the wait. A second 75 is `failed` and is not retried. The same rule applies when the daemon owns the queue.
- **H-Q13. Recipients.** The write stores the event JSON and the names of the hooks and actions that matched. Dispatch runs that list. A hook added later does not see the event. A removed hook is skipped and logged. The handler sees the memory as it was at the write.
- **H-Q14. Search offset.** Stage 1 ignores `--offset` and takes the top `candidate_pool`. The chain runs on that pool. `--offset` and the result limit slice the chain's ids. An offset past that list returns empty. `next_cursor` stays null.
- **H-Q15. File names.** `<brain>.d/hooks.json`, `hooks.log` plus `.1` `.2` `.3`, `trust` (0600), `out/<run_id>.log`, `plugins/<name>/stderr.log`, `plugins/<name>/index.json`, and `plugins/<name>/` for sidecar data. The pid file is `<runtime>/plugins/<name>/<brain-id>.state`. The registry is `<user-data>/brains.json`.
- **H-Q16. `--hooks`.** `--hooks` does not exist. `--no-hooks` on any write, including sync and import, skips the outbox for that command.
- **H-Q17. Hook powers.** A hook run may `add`, `update`, `delete` one locator, `link`, `unlink`, `rekey`, restore, and read. It may not `settings set`, `settings reset`, `settings import`, `plugin add`, `plugin enable`, `plugin disable`, `plugin remove`, `plugin reindex`, `hook add`, `hook remove`, `hook trust`, `hook enable`, `hook disable`, `delete --expired`, or `purge`. Those fail with `setting_from_hook` or the command's usage error.
- **H-Q18. `REMEMBER_HOOKS=0`.** Hook commands, dispatch, and `plugin add` of a folder return `hooks_unsupported`. Built-in C-ABI plugins still filter search. `plugin enable` and `plugin disable` of a built-in work.
- **H-Q19. Trust.** The CLI has no `--confirm` flag. In a terminal, `hook trust`, `plugin add`, and `plugin enable` ask the person to type the name. With no terminal they exit 1 with `hook_approval_required` or `plugin_approval_required`. The Fortkeeps app does not shell out for trust. It calls `remember_call`. That call skips the typed name only when the main executable is code-signed `app.fortkeeps.*`, and only after the OS dialog. Cancel does not trust. Plugin dialog body: `Allow "<name>" to read your memories? It can send them off this Mac.` Hook dialog body: `Allow hook "<name>" to run <path>?` Release builds have no environment variable that skips this. Test builds omit the dialog and the signature check. Linux v1 uses the typed name on a terminal. A same-user agent with a terminal remains the known limitation. This amends the Mac half of #423.
- **H-Q20. LaunchAgent.** The label is `app.fortkeeps.daemon`. The app installs the plist. The plist runs `remember daemon`. Linux docs show a user unit named `remember.daemon`. The engine installs neither job.
- **H-Q21. `--strict`.** Every warning whose code starts with `hook_` or `plugin_` exits 4 under `--strict` or `REMEMBER_STRICT=1`. `setting_overridden` and `unknown_field` stay exit 0. *(Superseded by G2-03: any warning exits 4 under strict. `time_budget_capped` no longer exists, 2026-10-22, M-22 A.)* `plugin reindex` and the `plugin enable` backfill still exit 4 when their own job is partial, with or without `--strict`. *(Superseded 2026-10-22: S-09 changed this to exit 1 with `plugin_backfill_incomplete`.)*
- **H-Q22. `plugin show` and remove.** `plugin show NAME` prints the manifest, trust state, and index state. `plugin remove NAME` keeps the sidecar and says so. `plugin remove NAME --delete-data` deletes it. There is no prompt. This amends the "asks" sentence in #456.
- **H-Q23. Priority.** A lower number runs first. The default is 500. A tie breaks by plugin name, then entry name, in byte order. The same order applies to `search.query` and `search.results`.
- **H-Q24. `delete --expired`.** One transaction. Every row with `deleted_at` unset and `expires_at` <= now moves to Deleted. One `memory.deleted` per row, one shared `batch_id`. This command emits no `memory.expired`. Rows already deleted, and live rows that are not due, stay.
- **H-Q25. Daemon boundary.** This log names the jobs the daemon owns and the CLI fallback. The socket, the handshake, the crash rule, and stale locks belong to the daemon log. No stage that hands work to the daemon starts until that log is closed.

### Resolved questions (deep review, 2026-10-21)

From `fk-review/QUESTIONS-2026-10-21.md`, in Itay's words where he wrote words. Stored as `decision:g-NN`. Where a G answer differs from H-Q1 to H-Q25, the G answer wins.

- **G-01.** A, "with a fallback to `~/.remember/` if `XDG_DATA_HOME` is not set." Plugins and `brains.json` live in that data dir.
- **G-02.** A: no Keychain or libsecret code in the engine.
- **G-03.** A, "to allow overriding the settings for one specific sync or import. Trusting the users and giving them power." Supersedes H-Q16.
- **G-04.** A: `plugin.<name>.enabled` has scopes `user` and `brain`.
- **G-05.** B. Supersedes the `plugin show` half of H-Q22.
- **G-07.** A: `io.github.itayavtalyon.remember.daemon.service`. Supersedes the Linux half of H-Q20.
- **G-08.** A: a link change updates both ends and fires `memory.updated` and `memory.linked` (R3-01).
- **G-11.** A. **G-19.** A. **G-22.** B. **G-24.** A. **G-25.** A. **G-27.** (1) A, (2) A. **G-28.** A. **G-29.** A. **G-30.** A.
- **G-17.** B (detect brain copies). **G-23.** "A. whitelist only. How can we enforce it?" (round 2, G2-07). **G-26.** "I am not sure what hook add has to do with the brain list. It should be a list of know brains" (round 2, G2-08). **G-31.** "Let's think of other ways to protect the file" (round 2, G2-09).
- **G-32.** A, "I would pass a callback for confirmation that the host provides and returns a boolean. Each host can supply a different one and it keeps the Remember code system agnostic." Supersedes H-Q19's signing check.
- **G-35.** "all rec, but 10 - adding an existing one updates it."
- **G-36.** "all rec".

### Resolved questions (deep review rounds 2 and 3, 2026-10-21)

From `fk-review/QUESTIONS-2026-10-21-round2.md` and `QUESTIONS-2026-10-21-round3-simplify.md`. Stored as `decision:g2-NN` and `decision:s-NN`. Itay: "Consistency is key, as little special cases as possible. I have no problem with breaking changes in most cases." Where these differ from H-Q1 to H-Q25 or G-NN, they win.

- **G2-02.** A: the update table (log 008 JE-5). **G2-03.** A: under `--strict`, every warning exits 4.
- **G2-06.** B: a persistent plugin passes the cause on each write. **G2-07.** A: an env check with a default-deny table; a guard, not a boundary.
- **G2-08.** A: `brains.json` lists every brain opened read-write. **G2-09.** A: no HMAC.
- **G2-10.** C: explicit `created_by` and `updated_by` filters. **G2-11.** (1) A, (2) A.
- **G2-13.** A, "but NULL does not equal a confirmation". **G2-14.** A.
- **S-01.** Yes: hooks are plugins. `hook add` writes a one-action plugin. Supersedes #453's two registration forms, H-Q3 (`hook add` now confirms, S-05) and H-Q15's hook files.
- **S-05.** Yes: trust belongs to the code hash, built-ins are trusted, and `enabled` is a plain setting.
- **S-06, S-08, S-09, S-10.** Yes.
- **S-11.** Yes: no daemon in v1. Supersedes #517, #518 and the daemon parts of #533 and H-Q25. **Reversed 2026-10-22** [decision:2026-10-22-daemon-v1] (#738): the local daemon stays in v1 to serve local HTTP MCP, and #517, #518 and H-Q25 are back in force. Every rule below that says "no daemon" is amended by this line.
- **S-14.** Yes, "but the user can turn off the hooks for local writes and this flag will enable them for this write".
- **S-15.** Yes: no exit-75 retry; `plugin rerun`.
- **S-16.** Yes: the pool is loaded once.

### Resolved questions (deep review round 5, 2026-10-21)

- `hook add` writes `enabled` true on the user layer, the same write as `plugin add`.
- A successful `plugin trust` clears `disabled_reason` `untrusted` and leaves `enabled` unchanged.
- A plugin name is a settings segment. A typed hyphen rewrites to `_` (log 007 ST-1.1, ST-1.3).

### Resolved questions (round 4, 2026-10-21)

From `fk-review/QUESTIONS-2026-10-21-round4.md`. Stored as `decision:q4-NN`. Q4-01 A (history: the newest 50 rows per key, no age limit). Q4-02 A (integer grammar). Q4-03 A (every plugin write, hooks included, is `tool`). Q4-04 A (`device_id` stays for the tie-break only). Q4-05 "all ok".

## Design

Normative. PP (revision 6, log 009) is the normative wire format. This section gives the engine behaviour. Names:

- **plugin**: an installed folder with `plugin.json`, or a built-in.
- **entry**: one action or one filter of a plugin.
- **hook**: a plugin that `remember hook add` wrote. It has one once-action named `run`.
- **run**: one execution of an action for one event.
- **outbox**: the table of events that wait for their runs.
- **data dir**: log 007 ST-2.8. **sidecar folder**: `<brain>.d/`. **runtime dir**: H-11.1.

### 0. Rules

- H-0.1. A plugin never changes, blocks or rolls back a write (#425, #417).
- H-0.2. Memory data reaches an action only on stdin or the socket. argv and env carry ids, names and numbers only.
- H-0.3. A plugin exists only after `remember plugin add`, after `remember hook add`, or as a built-in. No synced, imported or exported data creates or changes a plugin.
- H-0.4. The engine has no loop rule except the kill switch (§5).
- H-0.5. The engine code uses only POSIX and the platform abstraction layer (#437), with no exception.
- H-0.6. The host passes three callbacks in `RememberEnv`, with one `callback_ctx` `[decision:g2-13]`. Each may be NULL. Remember calls each on the thread that made the call:

```c
typedef bool (*remember_confirm_fn)(void *ctx, const char *kind /* "plugin" */,
                                    const char *name, const char *body);
typedef void (*remember_dispatch_fn)(void *ctx, const char *brain_path);
typedef int  (*remember_token_fn)(void *ctx, const char *helper_id, char *buf, size_t buflen);
/* token: 0 ok, -1 unknown id, -2 lookup failed; buf holds a NUL-terminated token */
```

  - A NULL confirm callback never counts as a confirmation (H-9.4).
  - A NULL dispatch callback leaves the outbox rows for the next CLI command or `remember sweep` (H-4.3).
  - A NULL token callback leaves only the id `env` (log 007 ST-3.2).
- H-0.7. Builds without process support set `REMEMBER_PROCESSES=0`. Those builds return `plugin_unsupported` for `hook add` and `plugin add` of a folder. Writes insert no outbox rows. A write that matches an enabled folder-plugin action is saved and warns `plugin_unsupported`, exit 0 (4 with `--strict`) (2026-10-25, C-23). Built-in C-ABI plugins still filter search, and their `enabled` setting still works. Phones never run folder plugins.
- H-0.8. A plugin problem during a search or a write is a warning, with exit code 0. With `--strict` or `REMEMBER_STRICT=1`, any warning gives exit code 4 `[decision:g2-03]`. Exit code 4 has no other meaning `[decision:s-09]`.

### 1. Events

| Event | Fires when | Commands |
|---|---|---|
| `memory.created` | a new memory row is stored | `add` (new), `import` (new row), sync (new row) |
| `memory.updated` | a write changes body, tags, key, `expires_at`, or a link to or from the memory | `add --key` on an existing slot, a merging `add`, `update`, `rekey`, `link`, `unlink`, sync |
| `memory.deleted` | a memory moves to Deleted | `delete`, `delete --expired` (#573) |
| `memory.restored` | a memory leaves Deleted | `update --trash --undelete` |
| `memory.purged` | a memory is removed for good | `remember purge <locator>` (#573) |
| `memory.expired` | the sweep marks a memory expired | the post-command sweep, `remember sweep` |

- H-1.1. Every write changes the fields in log 008 JE-5.1 and fires the event in that table `[decision:g2-02]`.
- H-1.2. `link` and `unlink` change both ends. Each end emits one `memory.updated` with `changes.link` (`kind`, `direction`, `other`). Both events share one `batch_id` (HK §1.2, R2-07, `[decision:g-08]`, `[decision:s-10]`). No `memory.linked` or `memory.unlinked` event exists.
- H-1.3. `memory.updated` fires only when a field or a link changed.
- H-1.4. The engine stores the expiry transition in `entries.expired_at` (§17). `NULL` means not yet marked. `expired_at` is a device-local marker: it never bumps `updated_at`, the version vector or `change_seq`, and never syncs `[decision:expiry-local-marker]` (#570).
- H-1.5. The sweep runs after every command that opened the brain read-write, reads included, once that command's own work commits (G-35.6). A read-only open skips it. The sweep is one transaction. It sets `expired_at = now` on each row with `deleted_at IS NULL AND expires_at <= now AND expired_at IS NULL`, and it writes one `memory.expired` outbox row per changed row. `data.occurred_at` is `expires_at`. `data.detected_at` is the sweep time. `data.cause.command` is `sweep` (HK §1.3).
- H-1.6. A write that sets a later or null `expires_at`, or restores from Deleted, sets `expired_at` to null in the same transaction. Restore of a row whose `expires_at` is already due emits `memory.restored`, then the sweep emits `memory.expired` (H-Q6).
- H-1.7. A command that changes N memories emits N events with one `batch_id`.

### 2. The brain list

- H-2.1. `<data dir>/brains.json` lists every brain that Remember opened read-write on this machine `[decision:g2-08]`. The document is `{"format":"remember-brains","version":1,"brains":[{"path":"/absolute/realpath","brain_id":"…","added_at":"…Z"}]}`, mode 0600. Remember is the only writer. Fortkeeps does not keep a second list (#533).
- H-2.2. On each read-write open, Remember reads the file. It writes the file only when log 008 JE-3.10 adds or changes an entry. It writes a temp file and renames it, under an advisory lock on `brains.json.lock`.
- H-2.3. `remember brain list` lists the entries. `remember brain prune` removes each entry whose path has no brain. `remember sweep --all` reads only this file. A missing path warns `brain_missing`, and the entry stays until `prune`. The plain warning is `remember: warning: brain "<path>" is in the list but isn't on disk. If you moved it, open it once from its new place. Otherwise run 'remember brain prune' to remove it from the list.` (2026-10-25, K-1; replaces the 2026-10-22 M-19 N5 line). `remember brain list|prune|new-id` replaces `remember brains [prune]`.

### 3. Event message

- H-3.1. An event is a `remember-message` (log 008 JE-1.2) with `type` `event` and a `request_id` (PP §3).
- H-3.2. `data` holds `event_id`, `event_type`, `occurred_at`, `detected_at`, `batch_id`, `cause` (with `origin`, #577), `brain`, `memory`, `changes`, `truncated`, `truncated_fields`, `limits`, `plugin`, `entry` (the routing key, #604), `data_dir`, and `settings`. Every field is always present.
- H-3.3. `data.memory` has the fields of `remember --json get` without `local_id` and without `version_vector` (#599), plus `change_seq` (#543). A plugin gets `id`, `bin`, and the fields in its `needs` (PP R3.7). A hook's `needs` is every memory field.
- H-3.4. `data` is at most 262,144 bytes. Over the limit, the engine drops `changes.before.body`, then `links`, then cuts `memory.body` to 16,384 bytes at a UTF-8 boundary, and sets `truncated` and `truncated_fields` (HK §2.2).
- H-3.5. Handlers deduplicate on `data.event_id`.
- H-3.6. Cloud webhooks send the same message (NQ-13, #505).

### 4. Delivery

- H-4.1. In the write transaction, the engine inserts one `outbox` row per event, only when an enabled entry matches that event. Remember evaluates the whole filter of each action at write time: `events`, `origins`, `tags.any`, `tags.all`, `match_before`, `created_by`, `updated_by`, `bins` and `not_caused_by` `[decision:g-27]`, `[decision:g2-10]`. A filter miss adds 1 to that entry's `skipped_filter` count and writes no log line.
- H-4.2. The row stores the frozen event JSON and `recipients`: a JSON array of `{"entry":"plugin/<name>/<entry>","state":"pending"}` (H-Q13). A plugin added later does not see the event.
- H-4.3. `--no-hooks` on any write inserts no outbox row. `--hooks` on any write runs every matching action for that write and ignores `origins`. So a hook whose `origins` leaves out `local` runs for that one local write `[decision:s-14]`.
- H-4.4. After the commit, the engine runs the pre-flight checks (§6). Then the CLI starts `remember dispatch` detached (double fork, `setsid`, stdio to `/dev/null`) and returns. A write through `remember_call` runs the same checks and then calls the host dispatch callback with the brain path. The engine never forks inside a host process `[decision:g-22]`.
- H-4.5. `remember dispatch` takes the advisory outbox lock in `<brain>.d/` and drains the outbox in `rowid` order (G-35.14). One entry runs its events strictly in outbox order, one run at a time. Runs of different entries run in parallel, at most 4 at once per brain `[decision:g-27]`. The dispatcher drains local events before sync and import events.
- H-4.6. Each run sets its recipient `state` to `done`, `failed` or `halted`, and the run is logged. The dispatcher deletes a row when every recipient is `done`. A row with a `failed` or `halted` recipient stays for `plugin rerun` (§5) until it is 7 days old. Then the dispatcher deletes it with log result `dropped_stale`.
- H-4.7. Delivery is at-least-once.
- H-4.8. A run that exits non-zero is `failed`. Remember never retries a run by itself `[decision:s-15]`.
- H-4.9. A once-process gets the event on stdin. Its stdout is discarded. Its stderr goes to `<brain>.d/out/<run_id>.log`, truncated at 1 MiB, and the run continues. The dispatcher deletes `out/*.log` files older than 7 days when it starts (G-36.5).
- H-4.10. A once-process `run.timeout` starts when the process starts. At the timeout, Remember sends SIGTERM to the process group, and 5 seconds later SIGKILL (PP R9.7).
- H-4.11. A persistent action gets the event on its socket and acks with `event.result` (PP R3.18). A missing ack is a strike. A failed `event.result` is not a strike (PP-08). `plugin status` reports a missed ack in `last_problems` (#602).

### 5. Chaining, the kill switch and rerun

- H-5.1. A write inside a run is a normal write, and it emits events to every match, including the writer.
- H-5.2. Every once-run gets `REMEMBER_HOOK_NAME` (`plugin/<name>/<entry>`), `REMEMBER_HOOK_RUN_ID`, `REMEMBER_HOOK_DEPTH`, `REMEMBER_HOOK_CHAIN`, `REMEMBER_CAUSE_HOOK`, `REMEMBER_CAUSE_RUN_ID`, `REMEMBER_CAUSE_EVENT_ID`, `REMEMBER_EVENT`, `REMEMBER_EVENT_ID`, `REMEMBER_ID`, `REMEMBER_KEY` and `REMEMBER_DB` (HK §5). `plugin test` also sets `REMEMBER_HOOK_TEST`.
- H-5.3. A persistent process gets the PP R6.1 variables plus `REMEMBER_HOOK_DEPTH=1` and `REMEMBER_HOOK_NAME=plugin/<name>`. It may write memories through the `remember` CLI. For each write that an event causes, it must set `REMEMBER_CAUSE_EVENT_ID` and `REMEMBER_HOOK_CHAIN` from that event's `data.cause`. Remember then extends that chain. A write without them counts as depth 1 `[decision:g2-06]`.
- H-5.4. Actor stamps on a plugin write follow log 008 JE-4.4: every plugin write, hooks included, is `{kind: tool, id: "plugin/<name>/<entry>"}` `[decision:q4-03]`. Remember rejects `--as` and `--actor` on those writes (`--as` replaces `--source`, 2026-10-22, M-19 N8).
- H-5.5. A run (`REMEMBER_HOOK_DEPTH` 1 or more) may `add`, `update`, `delete` one locator, `link`, `unlink`, `rekey`, restore, and read. This allow list is complete `[decision:g-23]`:
  - Settings writes fail with `setting_from_hook` (log 007 ST-7.10).
  - Every other command fails with exit 1 and `plugin_run_forbidden`. The plain message is `a hook or plugin can't run "<command>". Run it yourself in a terminal.` (2026-10-25, K-1). `message` is `a hook or plugin can't run "<command>".` The fix is `Run it yourself in a terminal.`
  - The CLI checks the variable when it dispatches a command, against one table in which each command has an `allowed_in_run` flag. A new command defaults to not allowed `[decision:g2-07]`.
- H-5.6. When `cause.chain` reaches 100, the dispatcher does not run that entry for that event, sets the recipient `state` to `halted`, logs `halted`, and keeps the plugin enabled. Every later command in that brain warns `plugin_loop_halted` until a rerun, `plugin status --ack NAME`, or until the row is dropped (G-35.4). (2026-10-22, M-19 N6/N7: renamed from `plugin_chain_halted`; `--ack` clears the sticky warning so `--strict` does not fail every command.) The plain warning is `remember: warning: "<name>" was stopped after 100 hook runs in a row set each other off. The write was saved. To run it once more, run 'remember plugin rerun <name> <event-id>' within 7 days. To clear this warning without rerunning, run 'remember plugin status --ack <name>'.` (Story Weaver final, COPY-STORY-WEAVER entry 19, 2026-10-22).
- H-5.7. `remember plugin rerun NAME EVENT_ID` runs a `failed` or `halted` recipient of that event once, with the chain reset `[decision:s-15]`. The command is synchronous: it runs the recipient in the foreground and waits for the run to end before it exits (2026-10-25, D-17). It prints `plugin_action_timeout` or `plugin_action_failed` for a persistent action (H-6.4). When the event has no such recipient for that plugin, the command exits 1 with `plugin_not_rerunnable`.

### 6. Pre-flight checks

- H-6.1. After the commit, for each matching once-action, the engine checks that each trusted file (H-9.1) exists and matches its hash, and that `run.command[0]` is executable `[decision:hooks-exit-code-4]`, `[decision:g-29]`.
- H-6.2. A failed check sets that recipient's state to `failed`, logs the failure, and warns, naming the plugin. A missing file or a `run.command[0]` that is not executable warns `plugin_preflight_failed`. A hash mismatch is its own warning, `plugin_untrusted`, and follows H-9.5 (2026-10-25, C-20). Remember does not run it again by itself (H-4.8). `plugin rerun` runs it (H-5.7). The write exits 0, or 4 with `--strict`.
- H-6.3. When the dispatcher cannot start, the command warns `plugin_dispatch_failed`. It exits 0, or 4 with `--strict`.
- H-6.4. Remember never waits for a handler. After a successful start, the handler owns the result. A failed run shows only in `remember plugin log` and the app toast. `plugin_action_timeout` and `plugin_action_failed` print only in a command that runs the action synchronously (2026-10-25, C-30/C-31, D-17; PP R3.21, R8.16). These are the commands, and no other command prints them:
  - `remember plugin test NAME`, which runs an installed entry in the foreground (H-16.3). Its exit code stays H-16.3.
  - `remember plugin rerun NAME EVENT_ID`, which runs in the foreground and waits for the run (H-5.7). Its exit code stays H-5.7.
  - The backfill in `remember plugin reindex NAME` (H-13.4). When the backfill does not finish, the command exits 1 with `plugin_backfill_incomplete` (H-13.6).
  - The backfill in `remember plugin enable NAME` (H-10.5, H-13.4). When the backfill does not finish, the command exits 1 with `plugin_backfill_incomplete` (H-13.6).
  - A write never waits and never prints them. A write's exit code does not change, also with `--strict` (PP R8.16).

### 7. Synced and imported memories

- H-7.1. Every event carries `cause.origin`: `local`, `sync` or `import`. The actor is `data.memory.updated_by` (#563, C-11). Every action has an `origins` filter, default `["local"]` `[decision:hook-origins-filter]` (#577), `[decision:g-11]`. Sharing later adds a trusted origin.
- H-7.2. An action whose `origins` does not list the event's origin does not run. A sync or import command writes one `skipped_source` line for each action and event type that it skipped, with the count (2026-10-25, D-14). An action that the command did not skip gets no line. When one command skips `memory.created` and `memory.updated` events for one action, it writes two lines.
  - Remember writes the line to the run log (H-15.1) after the command commits. `remember plugin log <plugin>` shows it, because the line names that plugin.
  - `remember plugin log <plugin>` prints the line as `<at>  skipped "<action>" for <count> <event_type> events from 'remember <command>', because its origins don't include <origin>.` When `count` is 1, it prints `<at>  skipped "<action>" for 1 <event_type> event from 'remember <command>', because its origins don't include <origin>.` The line has no plugin name, because the output holds only that plugin. Output that mixes plugins writes `"<plugin>/<action>"` in place of `"<action>"`. `<event_type>` is the field value, verbatim. `<at>` is the record's `at` in the human date form of log 007 ST-7.16. JSON output keeps `at` in RFC 3339 UTC (2026-10-25, Story Weaver).
  - The line is this JSON object, with the members in this order:

```json
{"version":1,"at":"2026-10-25T18:00:00.000Z","event":"skipped_source","event_type":"memory.created","command":"sync","origin":"sync","plugin":"wake_coder","action":"run","count":12}
```

  - `version` is the log record version, `1` (H-15.3).
  - `at` is the time that Remember wrote the line, in RFC 3339 UTC with milliseconds (JSON-STYLE §1.3).
  - `event` is `skipped_source`.
  - `event_type` is the H-3.2 `event_type` of the skipped events: `memory.created`, `memory.updated`, `memory.deleted`, `memory.restored`, `memory.purged` or `memory.expired` (PP R3.3). It is never `memory.*`.
  - `command` is the `cause.command` of the skipped events: `sync` or `import` (H-7.3).
  - `origin` is the `cause.origin` of the skipped events: `sync` or `import`.
  - `plugin` is the plugin name.
  - `action` is the entry name of the action, the last part of `plugin/<name>/<entry>`.
  - `count` is the number of events of that type in that command that the action did not get. It is 1 or more.
- H-7.3. A sync or import gets one event per memory, a shared `batch_id`, and `cause.command` `sync` or `import`.

### 8. Platforms

- H-8.1. Remember runs every once-action the same way: detached, with no terminal. Remember has no terminal mode (#423). A visible session is the handler's job (for example, the Fortkeeps `fk-terminal` helper).
- H-8.2. macOS and Linux run folder plugins and hooks. Windows (AF_UNIX through the platform layer, OQ-10) is later. iOS and Android run only built-in plugins in process (#458), as C-ABI libraries on a worker thread (#567). The Fortkeeps app schedules their long jobs (backfill, model download, sync) in the OS background slots, from capability flags in `RememberEnv` (#568).

### 9. Trust

- H-9.1. Trust belongs to the code `[decision:s-05]`. A trust record holds the hash of the plugin folder and the manifest, and the realpath, SHA-256, size and mtime of each file outside the folder that `run.command` names: `command[0]` and every later argument that names an existing regular file, at most 8 files in argv order `[decision:g-29]`. When `command[0]` is a known interpreter (`sh`, `bash`, `zsh`, `python`, `python3`, `node`, `ruby`, `perl`, `deno`, `bun`, or `/usr/bin/env` followed by one of those), the record also holds the interpreter's realpath and SHA-256. For `sh -c "<inline>"`, the inline text is part of the manifest hash (HK §3B.2).
- H-9.2. The records live in `<data dir>/plugins/trust.json` (mode 0600): `{"format":"remember-plugin-trust","version":1,"plugins":[{name, folder_sha256, manifest_sha256, files:[{path, sha256, size, mtime}], interpreter:{path, sha256}|null, needs, trusted_at, dev}]}` (G-36.3). The records have no HMAC `[decision:g2-09]`. Trust uses no Keychain, no libsecret and no polkit (#423).
- H-9.3. A built-in plugin is trusted and has no record.
- H-9.4. Remember records trust at `plugin add`, `hook add`, `plugin trust`, and `plugin add` of an installed name (an update). Each of these calls the host confirm callback with the plugin name and the dialog body. It records trust only when the callback returns true:
  - The CLI is a host. On a terminal, its callback prints the body, then `Type "<name>" to allow it, or press Enter to cancel:`, and returns true when the typed text equals the name. Enter with no name returns false (2026-10-25, K-1). With no terminal, the CLI passes no callback.
  - With no callback, the command exits 1 with `plugin_approval_required` and records nothing.
  - A false return exits 1 with `plugin_approval_denied` and records nothing (HK §3B.3).
  - The Fortkeeps app supplies its own callback. It shows its own dialog (Touch ID or device password on macOS) and returns true only after **Allow** `[decision:g-32]`.
  - The body for a folder plugin is `Allow "<name>" to read your memories? It can send them off this Mac.`, then the needs, permissions and settings (H-9.6).
  - The body for a hook is `Allow hook "<name>" to run <path>?`
  - The CLI has no `--confirm` flag. A same-user agent with a terminal can still type the name (Known Limitations).
- H-9.5. Before each process start and each once-run, Remember checks the record against the files. A mismatch does not run the entry, sets `disabled_reason` `untrusted` in that brain's `index.json`, logs `untrusted`, and warns `plugin_untrusted` (G-35.8, `[decision:g2-11]`). `remember plugin trust NAME` re-hashes the current files, shows the changed needs, and asks the confirm callback. A true return stores the new hash and sets `disabled_reason` to null. `enabled` stays as it was. The next use runs the plugin when `enabled` resolves to true.
- H-9.6. `plugin add` and an update show the declared `needs`, permissions and settings before the confirmation (#463, OQ-15). An update shows only the changed needs. The body says in plain words that a plugin that reads memory text can send it elsewhere `[decision:plugin-permission-prompt]` (#607).
- H-9.7. `plugin add --dev <dir>` uses the folder in place, re-hashes it at each process start, needs a terminal, and marks searches with `plugin_dev_mode` (PP R2.19, #603).

### 10. Install, enable and remove

- H-10.1. Plugins install once per user into `<data dir>/plugins/<name>/` (#454). `plugin add DIR` copies the folder there. `plugin add --dev DIR` uses the folder in place (G-35.9).
- H-10.2. `plugin add DIR` of a name that is already installed updates that plugin: it asks for confirmation (H-9.4), copies the new folder, records the new hash, and sends `shutdown` with reason `upgrade` to its running processes (G-35.10). `plugin add` of a built-in name fails with `plugin_exists` `[decision:g2-11]`.
- H-10.3. `remember hook add NAME --command PATH [ARG…] --event EVENT…` writes `<data dir>/plugins/<name>/plugin.json` with one once-action named `run`. `<name>` is `NAME` after the log 007 ST-1.3 rewrite, and it must match log 007 ST-1.1. The action holds the command, the events, the filter flags, `timeout` (default `60s`, range 1s–1h) and `working_directory` (default `~`) `[decision:s-01]`. `hook add` asks for confirmation (H-9.4). After a true return, it writes `plugin.<name>.enabled` true on the user layer, the same write as `plugin add` (H-10.4). `hook add --disabled` writes nothing to `enabled`.
- H-10.4. `plugin.<name>.enabled` is an ordinary setting with scopes `user` and `brain` (log 007 ST-5.8). `plugin add` writes `true` on the user layer after the confirmation (#596). `plugin enable NAME [--scope brain]` and `plugin disable NAME [--scope brain]` are shortcuts for `settings set plugin.NAME.enabled true|false`, with no prompt (2026-10-22, M-19 N2).
- H-10.5. `plugin enable` also clears `disabled_reason` `crash_loop` for the current brain (H-11.8), starts the warm-up (H-11.6), and runs a pending backfill (H-13.4).
- H-10.6. A brain whose `plugin.<name>.enabled` is true for a plugin that is not installed lists it as `missing` and warns `plugin_missing`.
- H-10.7. `plugin show NAME` prints the manifest, version, entries, requested needs, trust hash, its settings with values, and which brains in `brains.json` enable it (R2-03).
- H-10.8. `plugin remove NAME` sends `shutdown` with reason `removed`, deletes the install and its trust record, and keeps every `<brain>.d/plugins/<name>/`. It says so. `plugin remove NAME --delete-data` also deletes `<brain>.d/plugins/<name>/` in the brains of `brains.json`. There is no prompt (H-Q22).
- H-10.9. Built-in plugins (hybrid search) ship disabled (#448). The app ask-once prompt (log 007 ST-11.4) or `plugin enable` turns them on.

### 11. Plugin processes

- H-11.1. The CLI runtime dir is `$XDG_RUNTIME_DIR/remember/` when that variable is set, else `/tmp/remember-<uid>/`. Remember creates it at mode 0700 and refuses it when another user owns it `[decision:g-30]`. A host passes its own `RememberEnv.runtime_dir`.
- H-11.2. Each entry declares `process` `once` or `persistent`. Actions default to `once`. Filters are `persistent` (#438).
- H-11.3. Remember starts a persistent process on first use, with `REMEMBER_PLUGIN_SOCKET` set to `<runtime>/s/<16 hex>.sock`. The hex is the first 16 hex characters of the SHA-256 of `<brain_id>/<plugin name>`. The process binds the socket and writes `ready` on stdout (OQ-7). One process serves one plugin and one brain (OQ-9).
- H-11.4. Remember keeps one state file per process, `<runtime>/plugins/<name>/<brain-id>.state`: `{"format":"remember-plugin-state","version":1,"pid","started_at","strikes","crashes":[at…]}`. `crashes` holds the crash instants of the last 10 minutes (G-36.4).
- H-11.5. Remember judges a process alive only when `ping` answers within 200 ms. It holds an OS advisory lock while it checks and starts (#434).
- H-11.6. Remember starts persistent filter processes in the background at `plugin enable`, after a backfill, at app launch, and on a write or search when no live process exists. A search never waits for that start (PP R9.6a, #600).
- H-11.7. Every CLI run sends `shutdown` with reason `idle` to every process in the runtime dir that is past its idle timeout (`remember.plugins.idle_timeout`, default 10 min, or the manifest `idle_timeout`), for every brain (G-35.15).
- H-11.8. Strikes: a timeout, a crash, malformed output or oversize output is a strike. A well-formed failed reply is not (#433, NQ-11, OQ-22). 3 consecutive strikes stop the process. 3 crashes in 10 minutes set `disabled_reason` `crash_loop` in `<brain>.d/plugins/<name>/index.json` for that brain and warn `plugin_disabled_crash_loop`. The `enabled` setting does not change `[decision:s-02]`.
- H-11.9. A `ready.protocol` that differs from the manifest is neither a strike nor a crash. Remember sets `disabled_reason` `protocol_mismatch` (PP R6.2a, #605).
- H-11.10. A persistent process's stderr goes to `<brain>.d/plugins/<name>/stderr.log`, rotated like the run log (#604 PP-12).
- H-11.11. A plugin runs in a brain only when all of these hold: `plugin.<name>.enabled` resolves to true, the trust record matches (or the plugin is built-in), and the stored `disabled_reason` is null for that brain. `plugin status` reports `disabled_reason` `user` when the setting resolves to false and the stored reason is null. That value is computed. It is not written to `index.json`. The stored values are `untrusted`, `crash_loop` and `protocol_mismatch` (H-13.2).

### 12. Search filters and search

- H-12.1. Filters run in ascending manifest `priority`. The default is 500. A tie breaks by plugin name, then entry name, in byte order (H-Q23). No user setting changes the order `[decision:s-06]`.
- H-12.2. Search runs in three stages `[decision:plugins-filter-needs]`:
  - Before stage 1, the `search.query` chain runs on the query text and filters (#439). Stage 1 uses the rewritten query.
  1. **Candidates.** Find up to `remember.search.candidate_pool` (default 200) candidate ids with their BM25 scores, after the bin, tag, actor and key filters.
  2. **Chain.** Load the full rows of the pool once. Send each filter only the fields in its `needs`. Run the `search.results` chain on the pool (#435) `[decision:s-16]`. Each filter returns ids, scores and an optional note.
  3. **Result.** Keep the chain's order. Drop ids that do not exist or are in Deleted. Apply the limit: `--limit N` when given, else `remember.search.max_results` (default 20, log 007 ST-5.6, #440). `data.total` is the length before the limit.
- H-12.3. Without enabled filters, or with `--no-filters`, both chains are empty, and the result equals today's BM25 order.
- H-12.4. Budgets and the chain cap follow PP R9.4. Each budget and grace comes from the manifest. `remember.plugins.chain_budget` caps the chain (log 007 ST-5.5). When the chain reaches the cap, Remember does not run the remaining filters. Their status is `skipped` with reason `chain_capped`, and the command warns `plugin_chain_capped`, so the user sees both (2026-10-25, D-02). There is no `time_budget_capped` warning (log 007 ST-7.12; 2026-10-22, M-22 A, `decision:2026-10-22-M-22`): a budget larger than the time left in the chain cap is clipped silently, and the clipped value is the `budget_ms` Remember sends. A filter's `cold_start_grace` defaults to 10 s, must be 1 ms to 30 s (`0ms` makes the manifest invalid), and `plugin status` shows it next to `budget`. `settings set` on a budget or grace key is `setting_unknown` (exit 2) with the fix `Budgets and cold-start grace are set by the plugin's manifest. Raise remember.plugins.chain_budget instead.` (2026-10-22, M-24 A, `decision:2026-10-22-M-24`). A cold filter that does not answer in time gives the BM25 order and `plugin_filter_cold_start`. The filter keeps loading (#509 as amended by #516).
- H-12.5. A failed or timed-out filter is skipped, and its input passes on. The command warns with a code that names the filter (#435, #516): `plugin_filter_timeout` for a timeout, and `plugin_filter_failed` for any other failure (2026-10-25, D-08).
- H-12.6. Every search response has `data.filters[]`: name, version, status (`applied`, `skipped`, `failed`, `timeout`), time and an optional note (#462, PP §8).
- H-12.7. The `warnings` that a plugin sends in its replies go only to the plugin log and `remember plugin status`, never into command output (NQ-17, PP R9.21). The warnings that Remember raises about a plugin (H-12.4, H-12.5) go into command output.
- H-12.8. Human search prints no extra line when every filter applied. `remember search --explain` prints the filter table and `ranked by <plugin>/<entry>`. A cold start of hybrid/rerank prints `remember: warning: hybrid/rerank is still starting, so these results are in keyword order. Search again in a moment.` (2026-10-25, K-1; replaces the #608 line). Any other cold filter uses the same sentence with `<plugin>/<entry>` in place of `hybrid/rerank` (C-29).

### 13. Backfill and the index

- H-13.1. Each plugin keeps derived data in `<brain>.d/plugins/<name>/data/`. Remember passes that path as `data_dir`. Remember's own files for that plugin (`index.json`, `stderr.log`) sit in `<brain>.d/plugins/<name>/`, outside `data_dir` `[decision:g-28]`. The folder is never synced, exported or backed up (#456). `data_dir` is never synced or exported, and can be rebuilt from the `.db` (2026-10-22, M-14 should-fix 1).
- H-13.2. `index.json` is `{"format":"remember-plugin-index","version":1,"index_version":{…}|null,"disabled_reason":null|"crash_loop"|"protocol_mismatch"|"untrusted"}`. The plugin owns its `applied_through_seq` checkpoint (PP R7.10).
- H-13.3. Index states: `ready`, `building`, `stale`, `missing`, `failed`, `disabled` (#457). The plugin reports `index_version` in every `ready` and `ping.result`. After a completed full backfill, Remember stores it in `index.json`. A different `index_version` later marks the index `stale` (#514, #515).
- H-13.4. A backfill runs on `plugin enable`, on `plugin reindex <name>`, and at app launch for the brain that the app opens (G-35.16). A plugin request or a `missing` or `stale` index seen during a search or a write only marks the backfill pending (#513). A catch-up sends the changes and the purge tombstones with a `change_seq` above the plugin's `applied_through_seq` (#599, log 008 JE-5.2).
- H-13.5. The runner holds a per-plugin, per-brain lock and resumes from the last answered batch (#507).
- H-13.6. A backfill or reindex that does not finish exits 1 with `plugin_backfill_incomplete`. A rerun resumes it `[decision:s-09]`.
- H-13.7. Search never blocks or fails while an index builds. The plugin decides whether a partial index is good enough and says how it ranked (#461).
- H-13.8. On phones, the Fortkeeps app decides when a rebuild runs, in the OS background slots (#568). Remember offers `backfill.pause` and `backfill.resume` and resumable library calls (#460, #568).

### 14. URL expansion moves into a plugin

- H-14.1. URL expansion is a Fortkeeps plugin (PRINCIPLES) with one once-action on `memory.created`, `needs: ["body", "tags"]`, and the default `origins` `["local"]`, so a synced memory is not fetched again on another device (G-11).
- H-14.2. The action fetches the page and creates a new memory tagged `source:url`. The original memory cites the new memory (link kind `cites`, G-35.11). The action never edits the original memory (#449, #425).
- H-14.3. The action calls the `remember` CLI. Its filter has `not_caused_by` `plugin/<name>/<action>`. The engine stamps the new memory `created_by` `{kind: tool, id: "plugin/<name>/<action>"}` (H-Q9, log 008 JE-4.4).
- H-14.4. The fetch library is one open-source core in C, in the public remember repo (#459, #542, #578). It shares the engine platform layer and the C ABI (#567). HTTP goes through the host transport adapter (#544). The scope is head-only with a fixed byte cap: title, description, OpenGraph, canonical, icon. HTML parsing uses a vetted open-source C HTML5 parser (chosen in the URL plugin log, with a license check); a parser fuzz target runs in CI with ASan and UBSan (#578). On desktop it ships as a plugin folder. On iOS and Android it is linked in process with the same messages (#458, #567).
- H-14.5. The SSRF policy lives in the core (#578). The opt-out and the rules of remember-mac log 003 move into the core library.
- H-14.6. Until the plugin ships, the remember-mac coordinator writes a separate linked memory tagged `source:url` and never edits the user's write. When the plugin ships, the coordinator is removed (#580).
- H-14.7. The URL-fetch plugin declares its own enrichment setting in its manifest (`plugin.<name>.<key>`, #571). Its exact key is for the URL expansion log.
- H-14.8. The URL-fetch library is open source. The URL-expansion plugin is shipped and branded by Fortkeeps, and it is free (2026-10-22, M-14 should-fix 2).

### 15. Logs

- H-15.1. The run log is `<brain>.d/hooks.log`, JSON Lines, mode 0600 (#566). It holds every run of every plugin. Authors read it with `remember plugin log <name>`.
- H-15.2. Rotation at 5 MB, 3 rotated files kept, entries older than 30 days deleted, whichever comes first (#424).
- H-15.3. A log record has `version` and the HK §6.3 fields, with `event_type`. A `skipped_source` record has the H-7.2 fields, `event_type` included (2026-10-25, D-14). `remember plugin log <plugin>` prints it as the H-7.2 human line, with `<at>` in the log 007 ST-7.16 human date form. `plugin log --follow --json` emits one `remember-message` per line: `type` `plugin.run` for a run record, and `type` `plugin.stderr` with `data {plugin, line, at}` for a `stderr.log` line (G-35.7).

### 16. CLI

```
remember hook add NAME --command PATH [ARG…] --event EVENT [--event …] [--tag-any P]… [--tag-all P]…
                 [--created-by KIND]… [--updated-by KIND]… [--bin BIN]… [--origin ORIGIN]…
                 [--not-caused-by NAME]… [--timeout 60s] [--cwd PATH] [--disabled]
remember plugin add DIR [--dev]
remember plugin list | show NAME | status [NAME]
remember plugin enable NAME [--scope brain] | disable NAME [--scope brain]
remember plugin trust NAME
remember plugin reindex NAME [--full]
remember plugin rerun NAME EVENT_ID
remember plugin remove NAME [--delete-data]
remember plugin log NAME [--follow] [--since DURATION]
remember plugin test NAME|DIR [--entry ENTRY] [--event EVENT] [--query TEXT] [--dry-run]
remember dispatch
remember sweep [--all]
remember brain list | brain prune | brain new-id
remember purge LOCATOR
```

- H-16.1. `hook add` is the only `hook` command (S-01). Every other operation on a hook uses the `plugin` commands. No `remember hooks`, `hook list`, `hook package`, `hook trust` or `hook resume` command exists.
- H-16.2. The `hook add` filter flags set the action filter: `--tag-any` and `--tag-all` (with the prefix `*`), `--created-by` and `--updated-by` (actor kinds), `--bin`, `--origin` (`local`, `sync`, `import`; default `["local"]`), and `--not-caused-by` (a plugin name, which matches every entry of that plugin, or `plugin/<name>/<entry>`). `--command` takes the rest of argv up to the next `hook add` flag. `--` passes arguments that start with `-` (HK §4).
- H-16.3. `plugin test NAME` runs an installed entry now, in the foreground, with a sample event or a real memory (`--event`, the positional locator rules of log 008 JE-3.4), and prints the result. `--dry-run` runs nothing and prints the filter decision, argv, env and payload (HK §4 `hook test`). `plugin test DIR` runs the conformance suite against a folder in a scratch brain, with no install and no prompt (PP §10).
- H-16.4. `remember dispatch` drains the outbox of one brain (H-4.5). `remember sweep` runs the sweep (H-1.5) and then dispatch for one brain. `sweep --all` does that for each brain in `brains.json`.
- H-16.5. Exit codes: 0 ok, also when a plugin degraded the command, with a warning; 1 usage or error; 2 not found; 3 wrong bin; 4 a warning under `--strict` or `REMEMBER_STRICT=1` (H-0.8). `plugin status` exits 0 whenever it can report (#602).
- H-16.6. JSON output uses log 008 types: `plugin.add.result`, `plugin.list.result`, `plugin.status.result`, `plugin.rerun.result`, `hook.add.result`, `sweep.result`, `brain.list.result`, and the rest by the `<noun>.<verb>.result` rule. A hook appears in `plugin.list.result` like any plugin, with `timeout` (a duration string with the `timeout_ms` twin) on its action.
- H-16.7. `delete --deleted` does not exist. `remember purge <locator>` is the only permanent wipe, for one row that is already expired or deleted (#573). Purge of a live row exits 3 with `wrong_bin`. The plain message uses the key when the row has one, and the id when it does not: `"<key>" isn't deleted, so it can't be purged. Run 'remember delete <key>' first.` (2026-10-25, K-1; replaces the G-35.5 line). `message` is `"<key>" isn't deleted, so it can't be purged.` The fix is `Run 'remember delete <key>' first.` Purge writes a `tombstones` row (log 008 JE-5.2).
- H-16.8. `delete --expired` runs in one transaction. Every row with `deleted_at` unset and `expires_at` at or before now moves to Deleted. Remember emits one `memory.deleted` per row and one shared `batch_id`. That command emits no `memory.expired`. Rows already deleted stay. Live rows that are not due stay. There is no cap.
- H-16.9. Bots never get purge (#573). A run cannot call `delete --expired` or `purge` (H-5.5). `purge-trash` does not exist. The Fortkeeps **Empty Trash** button runs `purge` once for each memory in Trash, the engine's Deleted bin (C-02). Its only confirmation is the alert; it asks for no Touch ID or password `[decision:2026-10-22-empty-trash]` (#741, M-16).
- H-16.9a. `--trash` selects the Trash (the Deleted bin, the same list as the app's Trash). `--expired` is its own flag and selects expired memories. `--trash` is no longer an alias of `--expired` `[decision:2026-10-22-trash-flag]` (#742, 2026-10-22). For one release, `--trash` prints this stderr warning, once per command that uses it: `remember: warning: --trash now means the Trash (deleted memories). For expired memories, use --expired.` (Story Weaver final, COPY-STORY-WEAVER entry 24, 2026-10-22); this amends #742's no-warning line (2026-10-22, M-21 B, `decision:2026-10-22-M-21`). The warning's code is `flag_meaning_changed`, with `detail {"flag":"--trash","use":"--expired"}`. With `--json` it is an item in `warnings[]` and stderr stays empty (008 JE-2.8); without `--json` it is the stderr line above. Like every warning it exits 4 under `--strict` (G2-03). It is removed in the release after 0.3.0 (2026-10-22, M-23 A, `decision:2026-10-22-M-23`). Help: `--trash    Use the Trash: memories you deleted. Restore them, or purge them for good.` / `--expired  Use expired memories: ones past their expiry time. They are not in the Trash.` A wrong-list error (exit 3) names the other flag: `remember: "<key>" is in the Trash. Add --trash to use it.` / `remember: "<key>" has expired. Add --expired to use it.` `purge-trash` does not come back; a bulk wipe, if one is added, uses the flag style (for example `empty --trash`) (2026-10-22, M-20 A).
- H-16.10. CLI accessibility (2026-10-22, M-18 A). Color only on a TTY, when `NO_COLOR` is unset and `TERM` isn't `dumb`; `--color=auto|always|never` overrides. Every status is a word (`applied`, `failed`); no ✓/✗, and no meaning carried by color alone. Progress goes to stderr only, when stderr is a TTY, as whole lines at most once a second, with no spinner; `--quiet` turns it off, and it never goes into stdout or `--json`. Prompts echo what you type: no hidden input and no redraws. `--color` and `--quiet` are global flags: `remember --help` lists them with the other global options (`--db`, `--json`, `--strict`, `--busy-timeout`, `--no-create`) (2026-10-22, Interface final review).
- H-16.11. Help text (2026-10-22, M-19 N4, N5): `--hooks     Run matching hooks for this write, also ones you turned off for local writes.` / `--no-hooks  Run no hooks for this write.` Help lists `dispatch` and `sweep` under "Maintenance".

### 17. Schema (part of the v6 step)

In the v5 → v6 transaction, after the log 008 and log 007 steps (log 007 §10):

1. `CREATE TABLE outbox(event_id TEXT PRIMARY KEY, type TEXT NOT NULL, payload TEXT NOT NULL, recipients TEXT NOT NULL, created_at TEXT NOT NULL)`. `recipients` is the H-4.2 JSON array.
2. `ALTER TABLE entries ADD COLUMN expired_at TEXT`.
3. Set `expired_at = expires_at` on every row with `deleted_at IS NULL AND expires_at <= now`, so an upgrade emits no backlog.

Outside the transaction, log 007 §10 steps 1, 3 and 4 copy the sidecar files into `<brain>.d/` (#566, `[decision:g-19]`). `<brain>.d/` is mode 0700 (G-36.1).

- H-17.1. `import --from-db` reads v4 to v6 sources and ignores `outbox`.

### 18. Fortkeeps layer (macOS app)

- H-18.1. Fortkeeps installs one user-level LaunchAgent with label `app.fortkeeps.remember-sweep` that runs `remember sweep --all` every hour (`StartInterval` 3600) `[decision:s-11]` (amended 2026-10-22 [decision:2026-10-22-daemon-v1] (#738): the sweep stays as the fallback when the daemon does not run), `[decision:q4-05]`, #582. Linux docs show the systemd user units `io.github.itayavtalyon.remember.sweep.service` and `io.github.itayavtalyon.remember.sweep.timer` (`OnUnitActiveSec=1h`), which run the same command (R2-06). The engine installs neither job.
- H-18.2. `fk-terminal` opens a visible session for a handler. It is an ordinary command in a hook.
- H-18.3. The app supplies the confirm, dispatch and token callbacks (H-0.6). **Trust…** calls `remember_call`, and the app's confirm callback shows the H-9.4 dialog.
- H-18.4. Settings → Plugins: one row per plugin, hooks included (name, events or index state, last run, on/off), **Test**, **Open Log**, **Trust…**, **Rebuild** for plugins with an index, and data size. No Add or Edit.
- H-18.5. A failed run on an app write shows the non-blocking toast "Saved. Hook “router” failed (exit 3)." with **Show Log**.

## Cross-repo dependencies

| Repo | Change |
|---|---|
| remember | Outbox, dispatcher, sweep, the brain list, `hook add`, plugin runtime and trust, the three-stage search, the v6 schema part (§17), the hybrid built-in plugin (#484), the conformance suite (log 009). |
| remember-mac | Remove the in-app URL fetch that edits the body (log 003, `URLEnrichment`, the coordinator's `store.update(body:)`). Ship the URL expansion plugin (desktop folder) and its in-app adapter for iOS. The hourly LaunchAgent, `fk-terminal`, the three `RememberEnv` callbacks (Touch ID confirm dialog, dispatch, `keychain` token), the Plugins pane, the toast, the hybrid ask-once prompt, **Empty Trash**. The app a11y items move to remember-mac log 011 as tests (2026-10-22, M-19 N9). |
| fortkeeps-cloud | Webhooks send the H-3 event message (NQ-13). Cloud-run plugins use PP. The Cloud-built index stays on the server as part of Cloud v1 enhanced search; nothing Cloud-built is downloaded into `plugins/<name>/data/` (2026-10-22, M-13; amends #456's cache sentence). |

## Examples

Itay's agents on one Mac (an example, not a feature):

```
# creates the plugin wake_coder (ST-1.3)
remember hook add wake-coder --command /Applications/Fortkeeps.app/Contents/Helpers/fk-terminal -- ~/bin/fk-wake coder \
  --event memory.created --event memory.updated --tag-any to:coder --cwd ~/src/fortkeeps
```

```sh
[ "${REMEMBER_CAUSE_HOOK:-}" = "$REMEMBER_HOOK_NAME" ] && exit 0
[ "${REMEMBER_HOOK_DEPTH:-1}" -gt 5 ] && { remember add --tag to:itay "Loop on $REMEMBER_ID"; exit 0; }
```

A hook whose script was deleted:

```
$ remember add --tag to:coder "ready for review"
Saved.
remember: warning: hook "wake_coder" did not start: /Users/itay/bin/fk-wake doesn't exist. Run 'remember plugin show wake_coder'.
$ echo $?
0
$ remember --strict add --tag to:coder "ready again"; echo $?
…
4
```

## Trade-offs

- One plugin model for hooks and plugins (S-01): one trust file, one listing, one set of codes, one filter block. The cost is that a hook is a per-user install with a per-brain switch, not a file inside the brain.
- Trust on the code over trust on every enable (S-05): `enabled` is a plain setting. The cost is that an enabled plugin needs no new confirmation in a second brain.
- The daemon stays in v1 to serve local HTTP MCP [decision:2026-10-22-daemon-v1] (#738), reversing S-11. When it runs it also owns warm filters, backfills, timed sync and the queue (#517, #518). Without it, the CLI and host run each job as written here, so `memory.expired` can be up to an hour late and a filter process can start cold after a reboot.
- Pre-flight checks with a warning (exit 4 with `--strict`) over wait mode: a script learns that its hook could not start, and no write waits. The cost is that a hook that starts and then fails gives exit 0 even with `--strict`.
- A dumb engine over loop rules: handlers decide. The cost is a loop until disable or 100 hops.
- An outbox in the write transaction: survives crashes. The cost is one insert per matching event.
- Filters evaluated at write time: no rows for events that no entry wants. The cost is that a filter change does not apply to events already queued.
- A linked memory for URL content over editing the body: the user's text never changes (PRINCIPLES 1). The cost is one more memory per link.

## Verification Criteria

Must-pass tests (`tests/`).

1. Each of the six events is emitted once per matching write with the H-3 message shape. A no-op `add --key` emits nothing. `link` emits two `memory.updated` events (one per end) with `changes.link` and one `batch_id`, and both ends get a new `change_seq` and no version-vector change.
2. No enabled action: a write inserts no `outbox` row.
3. Outbox row and memory row commit or roll back together (failing-VFS shim).
4. Kill the dispatcher mid-run: the next write re-delivers the same `data.event_id`.
5. Two concurrent dispatchers: only one holds the outbox lock in `<brain>.d/`.
6. Filters `tags.any`, `tags.all`, prefix `*`, `match_before`, `created_by`, `updated_by`, `bins`, `not_caused_by`, `origins`: a miss inserts no row, writes no log line, and increments `skipped_filter`.
7. A hook that writes the memory it listens for gets its own event with `REMEMBER_CAUSE_HOOK` equal to `plugin/<name>/run`.
8. A chain of 100 hops: run 100 is `halted`, the plugin stays enabled, the write commits, `plugin_loop_halted` appears on the next command, `plugin rerun` runs it with depth 1, and `plugin rerun` of an event with no failed or halted run exits 1 `plugin_not_rerunnable`.
9. Pre-flight: a missing script and a non-executable script each give exit 0 with `plugin_preflight_failed`; a changed hash gives exit 0 with `plugin_untrusted` (C-20); each with the memory saved, the recipient `failed`, and no run. The next `remember dispatch` does not start that recipient. `plugin rerun` does. With `--strict`, the write exits 4. A dispatcher that cannot start gives exit 0 with `plugin_dispatch_failed` (4 with `--strict`). A hook that starts and exits 3 gives exit 0 and a `failed` log line, also with `--strict`.
10. Event data over 256 KiB: truncation order and `truncated_fields`.
11. argv and env contain no body or tag text.
12. A failed run is not retried. Its outbox row stays, `plugin rerun` runs it, and after 7 days the row is dropped with `dropped_stale`.
13. Script edited after trust: no run, `disabled_reason` `untrusted`, `plugin_untrusted`. A successful `plugin trust` sets `disabled_reason` back to null, leaves `enabled` unchanged, and the next use runs the plugin. `plugin trust` without a terminal exits 1 `plugin_approval_required`. A wrong typed name exits 1 `plugin_approval_denied`. A confirm callback that returns false records nothing.
14. `memory.expired`: the first command after `expires_at` emits one event after its own commit; a second command emits nothing; a revival clears `expired_at` and a later expiry emits again; a purge of an unmarked expired row emits only `memory.purged`; the v6 migration marks already-expired rows and emits nothing; setting `expired_at` changes neither `updated_at`, the version vector nor `change_seq` (#570).
15. Sync and import: an action with the default `origins` does not run for a sync or import event, and the log gets one `skipped_source` line for that action and event type, with the H-7.2 fields and the count; an action with `origins: ["local", "sync"]` gets one event per synced memory with a shared `batch_id` and `cause.origin` `sync`. `sync --hooks` runs the default-`origins` action too.
16. `remember sweep` on a brain with no plugins sweeps and inserts no outbox rows.
17. `REMEMBER_PROCESSES=0` build: `hook add` and `plugin add DIR` return `plugin_unsupported`; a write that matches an enabled folder-plugin action is saved and warns `plugin_unsupported`; writes insert no outbox rows; a built-in filter still runs.
18. A sync of 1,000 memories with only default-`origins` actions inserts no outbox row.
19. Every event's `cause.origin` matches how the change arrived. From a run (`REMEMBER_HOOK_DEPTH=1`), `settings set` fails with `setting_from_hook`; `delete --expired`, `purge`, `import` and `sweep` fail with `plugin_run_forbidden`.
20. Three-stage search: with no filter, the result equals the BM25 order of today's `store_search` for the 006 fixture; stage 3 drops an id that a filter added but that is in Deleted.
21. A filter that sleeps past its budget: BM25 order, exit 0 (4 with `--strict`), `plugin_filter_timeout`, `filters[].status` `timeout`.
22. A cold filter: first search gives BM25 order, exit 0 (4 with `--strict`), `plugin_filter_cold_start`; the process keeps loading; the next search is `applied`.
23. Three crashes in 10 minutes in brain A: A's `index.json` has `disabled_reason` `crash_loop`; brain B still runs the plugin; `plugin.<name>.enabled` is unchanged; `plugin enable` in A clears it.
24. A `ready` with an `index_version` different from the stored one marks the index `stale` and the backfill pending; a search does not start the backfill.
25. URL expansion: a shared URL creates one linked memory tagged `source:url`; the original memory is byte-equal before and after; the plugin's own memory triggers no second fetch; a synced copy on another device triggers no fetch.
26. The conformance suite passes for the hybrid plugin as a desktop folder and as a C-ABI library (#567), and for the PP §2.4 hello and log-ids fixtures (#601).
27. `remember plugin status --json` exits 0 with a missed ack in `last_problems` (#602).
28. `delete --deleted` is an unknown option; `remember purge <locator>` on a live row exits 3 `wrong_bin`; on a deleted row it emits `memory.purged` and writes a `tombstones` row (#573).
29. One plugin gets `memory.created` then `memory.updated` for one memory: the second run starts only after the first one ends.
30. A hook `fk-terminal -- ~/bin/fk-wake coder`: editing `fk-wake` after trust gives `plugin_untrusted` and no run.
31. A write through `remember_call` with a dispatch callback calls it once with the brain path, and runs no fork in the host process.
32. `plugin add` of an installed name with a new folder shows only the changed needs, asks for confirmation, and restarts the running process. `plugin add` of the name `hybrid` fails with `plugin_exists`.
33. A persistent plugin that sets `REMEMBER_CAUSE_EVENT_ID` and `REMEMBER_HOOK_CHAIN` on its write extends the chain; a loop through it halts at 100.
34. `plugin reindex` that loses its process mid-stream exits 1 with `plugin_backfill_incomplete`, and a second `reindex` resumes from the last answered batch.
35. `remember brain list` lists a brain after its first read-write open. `remember brain prune` removes a deleted brain's entry.
36. `hook add wake --command /bin/echo --event memory.created` writes `plugin.wake.enabled` true on the user layer. A second brain with no brain row runs that hook. `hook add --disabled` writes no `enabled` row.

## Known Limitations

- A same-user agent can edit plugin folders and scripts, and it can run the CLI in a terminal and type a name. Trust makes changes visible and stopped, not impossible. Agent sandboxing is the full protection (HK §3B.1).
- The hook allow list is a guard. A run that unsets `REMEMBER_HOOK_DEPTH` can run any command.
- At most 8 files per hook are hashed. Files that a script imports or sources, and programs it starts, are not covered (HK §3B.2).
- `memory.expired` is late by up to the time to the next command or the next hourly sweep.
- A hook whose `origins` lists `sync` or `import` runs once per synced or imported memory (at most 4 at once).
- Delivery is at-least-once.
- A CLI-only desktop user must run `remember plugin reindex` after a stale index (#518, MX-6).
- When the daemon is not running [decision:2026-10-22-daemon-v1] (#738), a filter process for a brain that is not used again stops only when a later CLI run finds it idle.
- Phones run built-in plugins only.

## Future Work

- **Out of scope / see future log:** per-bot tokens and the MCP activity log (roadmap §16.6), and the docs line "locally this is not a security boundary" (roadmap §16.4), will be covered in the upcoming local MCP and bot-auth design log, which is not written yet (Business Developer re-review nit, 2026-10-22).

- **The daemon log** (#517): the daemon ships in v1 [decision:2026-10-22-daemon-v1] (#738) (local HTTP MCP, warm filters, backfills, timed sync and the queue in one long-lived process). Its own log, with its IPC, owns the details. No longer Future Work.
- **A host sandbox callback**: the host starts runs in an OS sandbox, so the allow list becomes a boundary.
- **A host-held trust key**: trust records that a same-user agent cannot forge.
- **Web (remote HTTP) plugins** (#458): plugins on phones and in browsers through HTTPS, with their own trust model.
- **A search result cache**: keep the chain output per query, filter versions and `index_version`, so a repeated search skips the plugins and survives a cold start.
- **Multiplexing**: several requests per socket connection (PP OQ-8 is one request per connection in v1).
- Cloud webhooks (retries, signing, replay, metering): a fortkeeps-cloud log.
- The sync protocol and a durable change feed built from the outbox: a later remember log.
- Hybrid search and URL expansion: their own logs after these four (#485).
- A trusted `share` origin (#577, with sharing).

## Implementation plan

The plan is [`implementation-plans/25-plugins-and-hooks.md`](../implementation-plans/25-plugins-and-hooks.md). The v6 table steps are [`implementation-plans/19-schema-v6.md`](../implementation-plans/19-schema-v6.md). This log does not contain a second plan. The URL plugin waits for its own log. The hybrid ranker waits for its own log. The daemon waits for its own log. This extract implements the CLI and the host only.
