# 31 — Memory events and the outbox insert

**Area:** event creation and the outbox table insert inside the write transaction (`src/events.c` new, `src/events.h` new, `src/store_sqlite.c`).
**Needs:** plans 25, 27a and 30. No other plan.
**Blocked on:** nothing.

## Goal

Every write that changes a memory creates its events in the same transaction, and inserts one outbox row per event when at least one enabled entry's full filter matches. No process starts in this plan.

## Source

- Log 010 H-3 (event message), H-4.1 to H-4.3 (insert), H-7 (origins, `skipped_source`), the event list.
- Old plan 25 sections "Events" (first two paragraphs), "Delivery" (first two paragraphs), "Event message".
- `decision:2026-11-08-sync-record-order-hooks-delivery` (ordered, durable, at least once).
- `decision:s-14`: `--hooks` and `--no-hooks` are global write flags (log 010 H-Q16 says `--hooks` does not exist; that line is stale against `decision:s-14` and H-4.3, and the spec drops it). Help lines H-16.11.
- Spec: `specs/EVENTS.md` (new).

## Out of scope

The expiry sweep and `memory.expired` (plan 32). Dispatch, pre-flight, runs (plan 35). Trust (plan 33).

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md). Tests in `tests/test_events.c` (new, suite `plugins`). SQL stays in `src/store_sqlite.c`.

## What must be true

Events: `memory.created` (a new memory row), `memory.updated` (body, tags, key, `expires_at`, or a link changes), `memory.deleted` (moves to Deleted, including `delete --expired`), `memory.restored` (leaves Deleted), `memory.purged` (`remember purge <locator>`). There is no `memory.linked` and no `memory.unlinked`. `link` and `unlink` emit one `memory.updated` per end, with `changes.link` `{kind, direction, other}`, and one shared `batch_id`. `memory.updated` fires only when a field or a link changed. N memories in one command share one `batch_id`.

Outbox table: the v6 table already on main (`k_v6_outbox_sql` in `src/store_sqlite.c`, log 010 §17): `outbox(event_id TEXT PRIMARY KEY, type TEXT NOT NULL, payload TEXT NOT NULL, recipients TEXT NOT NULL, created_at TEXT NOT NULL)`. This plan adds no column and no migration. `type` is the event type, `payload` the frozen event JSON. The comment in `src/commands_common.h` line 112 ("until plan 25 adds the outbox writer") is replaced by a pointer to `src/events.h`.

Insert: in the write transaction, insert one outbox row per event when an enabled entry's full filter matches. The filter is `events`, `origins`, `tags.any`, `tags.all`, `match_before`, `created_by`, `updated_by`, `bins`, and `not_caused_by`. A miss increments the in-memory counter `skipped_filter` and writes no log line. The row stores the frozen event JSON in `payload` and `recipients`: a JSON array of `{"entry":"plugin/<name>/<entry>","state":"pending"}`. A plugin added later does not see the event.

`--no-hooks` inserts nothing. `--hooks` matches every action for that write and ignores `origins`. It does not ignore `enabled` false.

Origins: `cause.origin` is `local`, `sync`, or `import`. Default action origins are `["local"]`. An action skipped only because of `origins` writes one `skipped_source` line per skipped action and event type to `<brain>.d/hooks.log` with the H-7.2 fields `{"version":1,"at","event":"skipped_source","event_type","command","origin","plugin","action","count"}`.

Event message (H-3): `type` `event`. `data` always has `event_id`, `event_type`, `occurred_at`, `detected_at`, `batch_id`, `cause` (with `origin`), `brain`, `memory`, `changes`, `truncated`, `truncated_fields`, `limits`, `plugin`, `entry`, `data_dir`, `settings`. `data.memory` is `remember --json get` without `local_id` and without `version_vector`, plus `change_seq`. `data` is at most 262144 bytes. Over the limit, drop `changes.before.body`, then `links`, then cut `memory.body` to 16384 bytes on a UTF-8 boundary, and set `truncated` and `truncated_fields`. `cause.hook` is `plugin/<name>/<entry>`. There is no `cause.plugin`. The stored `payload` omits `plugin`, `entry`, `data_dir` and `settings`; plan 35 fills them per recipient.

`REMEMBER_PROCESSES=0` (capability `processes` off): writes insert no outbox row. A write that matches an enabled folder-plugin action is saved and warns `plugin_unsupported`, exit 0 (4 with `--strict`). Print C-23.

No synced, imported, or exported data creates a plugin.

- **Warnings carry a `fix` (E3 B, `decision:2026-11-12-e3-warnings-have-fix-field`).** Every warning this plan prints has a `message` and a non-empty `fix`: `plugin_unsupported` (dispatch in a build that cannot start programs). The wording is copy item E3B-1, entry 15, in `decision:2026-11-12-copy-batch-e3b-warning-splits`: the approved sentence stays word for word and only its next step moves into `fix`; the plain line is `remember: warning: <message> <fix>`. `code`, `detail`, exit codes and `--strict` do not change. Itay approved E3B-1 as a whole on 2026-11-12, including every `fix` that is Story Weaver wording (`decision:2026-11-12-copy-batch-e3b-warning-splits`).

## Review points

- Outbox and memory commit or roll back together.
- No body or tag text goes into argv or env (nothing is spawned here; the test checks the stored row only).

## Definition of Done

- [x] `six_event_types_created` (010 test 1, without `memory.expired`; the link half: two `memory.updated`, one `batch_id`, no version-vector change on the ends).
- [x] `no_enabled_action_no_row` (010 test 2).
- [x] `outbox_commits_with_memory` (010 test 3).
- [x] `filter_miss_inserts_nothing_counts_skipped` (010 test 6).
- [x] `truncation_order_256k` (010 test 10).
- [x] `default_origins_skip_sync_and_import_and_log_skipped_source` and `hooks_flag_ignores_origins` (010 test 15).
- [x] `no_hooks_inserts_nothing`.
- [x] `processes_off_warns_plugin_unsupported_c23` (010 test 17, write half).
- [x] `specs/EVENTS.md` written; the decisions above are `state:implemented`.
- [ ] The gate passes on the committed tree.

## Implementation Notes

Coder: Sonnet 5.5 session, 2026-11-17 and 2026-11-18. Branch `claude/festive-northcutt-a64f0a`, stacked on plan 30 (STACK_BASE in `.claude/REVIEW_REQUEST`). The spec is `specs/EVENTS.md`. Tests are in `tests/test_events.c` (suite `plugins`; the store and pure parts also run in the ASan `remember_store_tests`).

**Built.**
- `src/events.c`, `src/events.h`: rules (one `EventAction` per enabled plugin action, with its manifest entry), the whole filter (`events_match`), planning (`events_plan`: what a touched memory's before and after give), the payload with the H-3.4 cuts, the recipients JSON, the cause from the environment, the `hooks.log` writer, and the `REMEMBER_PROCESSES` switch.
- `src/store_sqlite.c`: the store finds what a write changed with temp tables and triggers (the first image of each touched memory, its tags and its live links), and `commit_write` inserts the outbox rows just before `COMMIT` at the eleven places that write memories. Nothing is armed unless an enabled plugin has an action (`store_set_events`).
- `src/remember_app.c`: loads the enabled plugins (plan 30's loader now keeps the manifests), arms the store for a command that changes memories, then raises `plugin_unsupported` and writes `skipped_source` lines after a command that succeeded. `src/commands_common.c` and the write commands pass `--hooks`/`--no-hooks`; both together is a usage error.
- `src/output.c`: `output_event_memory_json` (the `get` memory without `local_id` and `version_vector`, plus `change_seq`); link stubs without `local_id`.
- `src/diag.c`: the warning `plugin_unsupported` (E3B-1 entry 15, message and fix as approved).

**Grills answered by Itay** (all filed): `decision:2026-11-17-plan31-grill-purged-processes-cause`, `decision:2026-11-18-plan31-grill-filter-semantics`, `decision:2026-11-18-plan31-grill-link-direction-batch-id`, `decision:2026-11-18-plan31-grill-chain-env-conflict-accept`, `decision:2026-11-18-plan31-grill-hooks-flags-together`. Open items filed: `decision:open-2026-11-18-plan31-conflict-accept-command` (Itay), `decision:open-2026-11-18-plan31-payload-over-limit` (Itay), `decision:open-2026-11-18-plan31-hooks-flags-together-copy` (Story Weaver; the sentence printed is a draft).

**Plan corrections and design calls.**
- The plan says "Every write that changes a memory creates its events". Instead of a hook at each write function, the store compares each touched memory before and after (temp triggers). A no-op `add --key` and a `link` (which only touches `updated_at`) make no event for the memory itself, with no per-command rule. `import`, `conflict accept` and the old whole-bin `purge` are covered with no extra code.
- `memory.purged` is emitted by the store (`store_purge`, `store_hard_delete_*`), so the existing `purge --expired|--deleted` writes one per row. Plan 32 reuses it.
- A purge that cascades over links makes a `memory.updated` for each neighbour: its link went (H-1.3 read as written).
- `changes.link` does not say whether a link came or went (R3.16 as written); a reader compares `memory.links`. The link stub is `get`'s (`type`, not the schema's `kind`) without `local_id`. Neither is changed here.
- `cause.command` for `conflict accept` is `update` (open item). `cause.command` reuses `cli_command_name`.
- `events_rules_load` reads `plugin.<name>.enabled` through `store_setting_read`, so the user layer (`user.db`) counts when it exists.
- Known limit, filed: after the three H-3.4 cuts `data` can still pass 262144 bytes (thousands of tags).

**Evidence.**
- `ctest`/gate suites (`scripts/read-gate-suites.sh`): 1816 tests, 0 failed on the stack; `remember_store_tests` (ASan): 0 failed. Plugins suite: 382 tests.
- DoD tests, by name: `six_event_types_created`, `a_link_writes_two_updated_events_with_one_batch_and_no_vector_change` (the link half), `no_enabled_action_no_row`, `outbox_commits_with_memory` (in-process, failing the exec, step, prepare and both allocators at each point: the counts are always 0/0 or 1/1) and `a_failed_write_commits_neither_the_memory_nor_its_outbox_row` (the CLI under the failing VFS), `filter_miss_inserts_nothing_counts_skipped`, `truncation_order_256k`, `default_origins_skip_sync_and_import_and_count_skipped_source` and `default_origins_skip_sync_and_import_and_log_skipped_source`, `hooks_flag_ignores_origins`, `no_hooks_inserts_nothing`, `processes_off_warns_plugin_unsupported_c23`.
- Review point "no body or tag text in argv or env": `no_body_or_tag_text_is_in_a_column_a_dispatcher_puts_in_argv_or_env` (only `payload` holds text).
- Lint: macOS `scripts/lint-all.sh` (LLVM 22) clean. The Linux half of the gate (tidy 18 and 21, cppcheck, IWYU) found cppcheck, tidy and IWYU findings in three rounds, all fixed; logs in `.claude/gates/plan31/`.

**Mutants.** `.claude/gates/p31/mutants.py` (about 55 one-clause mutants: each filter test, the planning rules, each cut and its order, the UTF-8 boundary, each trigger, the order of flush and COMMIT, batch id, the hooks flags, the app wiring). First run on an earlier tree: 36 killed by a named test, 8 more died in a crash, 8 did not build (rewritten), two survivors that are not mutants: `depth-zero-is-run` (the value 0 means "not in a run" anyway) and `app-writable-only-ignored` (the store already refuses to arm a read-only brain; the check in the app was removed), and `chain-allows-100-items`, which showed the loop bound and the final guard were the same test (one bound now). The crashes were tests indexing a missing draft; they are guarded now. The second run on the final tree is `.claude/gates/p31/mut-results.txt`.

**Reviewer's list.** Review point "outbox and memory commit or roll back together": `outbox_commits_with_memory`, `a_failed_write_commits_neither_the_memory_nor_its_outbox_row`, `a_failed_write_leaves_no_trace_in_the_next_one`. "No body or tag text in argv or env": see above.

**Simplify (considered).** (1) `BeforeLink` and `EventLink` hold the same four strings: `EventLink` could own them, about 40 lines, but the diff then borrows from two sources (the store rows and the neighbour rows). (2) `events_rules_add` and `events_rules_load` could be one function; kept apart so the filter tests need no store. (3) The test helpers `assert_db`, `assert_query` and `assert_rows` differ only in what they open. (4) `recipients` and the payload both use `open_memstream`; a shared writer saves about 10 lines.

**Not verified here.** The expiry sweep and `memory.expired` (plan 32); a real dispatcher reading the row (plan 35); `sync` origin (plan 45); the Linux LSan unit run on the final tree (the gate's).

## Review Notes
