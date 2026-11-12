# Implementation plans — `remember`

Source of truth for product behavior: [`specs/`](../specs/README.md). It lists the specs and, for a subject with no spec yet, the frozen design log that still describes it.  
Test matrix: [`tests/COVERAGE.md`](../tests/COVERAGE.md).

## Pragmatic architecture (all steps)

- **Easy-to-replace**. Rule for this project: **Only `store_sqlite.c` may `#include "sqlite3.h"`** (or amalgamation header). Everything else talks to **`store.h`** (opaque `Store *`, domain structs, error codes). Swap storage later without touching CLI/commands.
- **Orthogonal**. Rule for this project: CLI parse ≠ command policy ≠ store persistence ≠ output format. Change FTS without rewriting argv parser; change JSON without rewriting SQL.
- **DRY**. Rule for this project: Tag/key normalization once (`normalize.c`). Entry JSON/human rendering once (`output.c`). Body pipeline once. FTS resync one function in the store adapter.

```text
main → cli_parse → commands.h (cmd_add | cmd_locator | cmd_query) → store_* → [store_sqlite only]
                      ↘ output_* (stdout)
         commands_common (internal helpers only; not public)
```

## Steps (do in order)

- 01. Plan: [Scaffold + meta CLI](01-scaffold-cli-meta.md). Done when: Listed global tests green; format/lint clean on touched `src/` ✅.
- 02. Plan: [Store port + SQLite schema](02-store-sqlite-schema.md). Done when: Open/create/version/pragmas work; unit smoke; **no sqlite outside adapter** ✅.
- 03. Plan: [Normalize + hash](03-normalize-hash.md). Done when: Unit tests for trim/tags/keys/hash; used by add ✅.
- 04. Plan: [add](04-add.md). Done when: All `add_*` + key add/upsert tests green ✅.
- 05. Plan: [get / list / delete](05-get-list-delete.md). Done when: get/list/delete + paging + key locator tests green ✅.
- 06. Plan: [search FTS](06-search-fts.md). Done when: All search_* + search key filter green ✅.
- 07. Plan: [update](07-update.md). Done when: All update_* + keyed update green ✅.
- 08. Plan: [Lint + format toolchain](08-lint-format.md). Done when: `lint` + `format` targets work; src clean ✅.
- 09. Plan: [Polish](09-polish.md). Done when: Help text, defaults dir 0700, warnings, remaining config tests ✅.
- 10. Plan: [Skill + install + dogfood](10-skill.md). Done when: `SKILL.md` + `scripts/install.sh` (binary + agent skills) ✅.
- 11. Plan: [Core façade lib + `tags`](11-core-facade-lib.md). Done when: `remember_run` byte-equals CLI `--json`; `tags` cmd; `libremember` links w/o `main` ✅.
- 12. Plan: [TTL / trash](12-ttl.md). Done when: `expires_at`; `--ttl`/`--expires`/`--trash`; `purge-trash`; exit 3; v1→v2 migrate.
- 13. Plan: [Related memories](13-related-memories.md). Done when: `entry_links` v3; `link`/`unlink`/`related`/`rekey`; one-row `related`; stubs; skill.
- 14. Plan: [Sync foundations](14-sync-foundations.md). Done when: `sync_id` v7; soft-delete bins; per-file sidecar + local-only devices + VV; import merge + conflicts; `--sync-id` locators (design 005 Round 7).
- 15. Plan: [Schema v5 + device id](15-schema-v5-device-id.md). Done when: `user_version` 5; FTS `key, body, tags` + Hebrew fold; `bucket_prefixes`/`settings`/`conflicts.origin`; `devices` dropped; sidecar-only id; `device` / `device new` (006 §1, §12) ✅.
- 16. Plan: [Actionable errors](16-actionable-errors.md). Done when: `--json-errors`; code catalog; SQLite cause split + fix text + `detail`; warnings; `--busy-timeout`; `--no-create`; `repair search-index`; test VFS shim (006 §3) ✅.
- 17. Plan: [List dates + stale guard](17-list-dates-stale-guard.md). Done when: `list --sort/--since/--until`; search date filters; `update --if-version-vector` → `stale` (006 §4, §7) ✅.
- 17a. Plan: [Plain notices + duplicate VV keys](17a-plain-line-and-duplicate-vv.md). Done when: One plain notice stays one line; a repeated device key is a malformed version vector. After 17, before 18 ✅.
- 18. Plan: [Union search](18-union-search.md). Done when: Prefix ∪ raw FTS; never fails on input; `keys`/`tags` facets with tiers; utf8proc vendored (006 §2, §9). Itay finishes this plan and merges it. Do not tick it here.
- 18b. Plan: [Branch-coverage gate](18b-branch-coverage-gate.md). Done when: `check-coverage.sh` fails on any untaken branch side in `search_terms.c`, `unicode.c`, `normalize.c`, `output.c`; no excuses. Runs on `plan18-union-search` between plan 18 stages 2 and 3 ✅.
- 18c. Plan: [Import control bytes](18c-import-control-bytes.md). Done when: `import --from-db` skips and counts (`rejected`) rows whose key or tag is not canonical; human output masks C0/DEL in every stored string it prints. On `fix/import-control-bytes` (off plan 18b) ✅.
- 19. Plan: [Schema v6](19-schema-v6.md). Done when: One v5→v6 migration for 007 §10, 008 §8, and 010 §17. Copy approved 2026-10-25 (C-01 `schema_too_new`). ✅.
- 20. Plan: [Settings](20-settings.md). Done when: Registry, user.db, resolution, commands, credential helper, settings export/import. Copy approved 2026-10-25. After plan 19. Command JSON after plan 21. The `unknown_key` and `bad_field` import-skip lines are written (copy item C-43), and every warning has a `fix` (E3 B).
- 21. Plan: [JSON envelope](21-json-envelope.md). Done when: `remember-message` 1, locators, actors, `--trash` means Trash. Copy approved 2026-10-25. After plan 19. Does not implement `brains.json`. The hook-depth `--as`/`--actor` line is a C-42-style draft. ✅.
- 22. Plan: [Export](22-export.md). Done when: JSON, Markdown, CSV memory export. READY. D-04 is answered (2026-10-25).
- 23. Plan: [Import formats](23-import-formats.md). Done when: JSON, Markdown, CSV, and `--from-db` v4–v6. Copy approved 2026-10-25. D-04 is answered (2026-10-25). No old-name reader. No add-wins.
- 24. Plan: [Plugin protocol](24-plugin-protocol.md). Done when: Docs CI, schema extraction, fixtures. READY. Spec revision 6 is already in the repo. ✅.
- 25. Plan: [Plugin manifest](25-plugin-manifest.md). Done when: `plugin.json` parses to the protocol schema. Needs 24.
- 26. Plan: [Brain list](26-brain-list.md). Done when: `brains.json`, `brain list`, `brain prune`. Needs 19, 25 (cut from the same branch).
- 27. Plan: [Default brain path](27-default-db.md). Done when: `~/.remember/default-db`. Needs 19.
- 27a. Plan: [Outbox delivery](27a-outbox-delivery.md). Done when: Recipient state machine, backoff retry (X3 A), and a pre-flight failure that is `failed` at once (E1 A). Needs 24.
- 27b. Plan: [Older CLI read-only](27b-older-cli-read-only.md). Done when: `schema_too_new` read-only with a warning, writes exit 75. Needs 19, 21. Copy: R27b-1 (no command in the upgrade line, SW1 A).
- 28. Plan: [Release 0.3.0](28-release-0.3.0.md). Done when: Version, What's New, Codex skill. Needs 22, 23, 25, 26, 27, 27a, 27b.
- 29. Plan: [Handle API](29-handle-api.md). Done when: `remember_open`/`remember_call`/`remember_close`, RememberEnv, no HOME in the engine. Needs 20, 21. X1 answered (`decision:2026-11-12-x1-remember-call-requests`); two details wait for Itay (see the plan).
- 30. Plan: [Plugin settings keys](30-plugin-settings-keys.md). Done when: Boolean type, generated `plugin.<name>.*` keys. Needs 25.
- 31. Plan: [Events and outbox insert](31-events-and-outbox-insert.md). Done when: Five write events, the write-time filter, the outbox row. Needs 25, 27a, 30.
- 32. Plan: [Expiry sweep](32-expiry-sweep.md). Done when: `expired_at`, `sweep`, `purge`, `delete --expired`. Needs 26, 31.
- 33. Plan: [Plugin trust](33-plugin-trust.md). Done when: `trust.json`, confirm, hash check. Needs 25.
- 34. Plan: [Plugin commands](34-plugin-commands.md). Done when: `hook add`, `plugin add/list/show/enable/disable/trust/remove/log/status`. Needs 30, 33.
- 35. Plan: [Dispatcher](35-dispatcher.md). Done when: Pre-flight, detached dispatch, once-runs, allow list, kill switch, rerun. Needs 27a, 29, 31, 32, 33.
- 36. Plan: [Persistent processes](36-persistent-processes.md). Done when: Sockets, ping, strikes, crash loop, `index.json`, backfill. Needs 35. Not blocked: X1 answered.
- 37. Plan: [Search filter chain](37-search-filter-chain.md). Done when: Three stages, budget, cold start. Needs 36. Not blocked: X5 answered.
- 38. Plan: [Plugin test](38-plugin-test.md). Done when: `plugin test NAME|DIR`. Needs 35, 36.
- 39. Plan: [Daemon process](39-daemon-lock.md). Done when: `daemon.lock`, `daemon.json`, drain. Needs 21, 35.
- 40. Plan: [Daemon install](40-daemon-install.md). Done when: `remember daemon install`, `--plugin-dir`. Needs 34, 39.
- 41. Plan: [Daemon takeover](41-daemon-takeover.md). Done when: `remember daemon takeover [--yes]`. Needs 39, 40. Copy: R30-4; `confirmation_required` exits 5 (E2 A).
- 42. Plan: [Owner-missing check](42-daemon-owner-missing.md). Done when: `state.json`, the one-time prompt, the warning. Needs 39, 41. Copy: R30-4; the hint prints after No only and `y` prints a done confirmation (E4 A).

**Design 006 plans 15–18:** do them in order. Plan 18 is not done. Plans 18b
and 18c land with the plan 18 merge. Do not tick plan 18.

**Plans 19–24** keep the staged files they already have. Plan 20 stages 1–4 run
after plan 19 and before plan 21. Plan 20's JSON command stages run after plan
21. Approved sentences are in [`COPY-PENDING.md`](COPY-PENDING.md). Nothing in
that file is pending. A sentence it marks as unwritten stays unprinted. Missing
decisions are in [`DESIGN-PENDING.md`](DESIGN-PENDING.md). Do not invent either.

## Plans 25 and later

Each plan is one module or one area, and one pull request. A plan has no stages. A plan depends only on a lower number, and only on the plans its own Needs line names. A plan never waits for a release or a product (Itay, 2026-11-11 21:38). A plan's Blocked on line names the copy keys, the proposed records and the open questions (tag `open-questions` in Remember) that must close before work starts.

`27-v1-roadmap-round3.md` is a roadmap stub. It is not row 27. No plan waits for it.

The coder implements the plan by test-driven development, one red test at a time. The coder then reviews the change in session against `standard:coding`, `standard:c`, and the plan. An independent reviewer reviews the code against the plan and those standards, and opens the pull request. Itay merges.

Standards on every one of these plans:

- **Coding bar.** `standard:coding` and `standard:c`.
- **Decisions are law.** The plan quotes them. A choice that no decision makes stops the coder. The coder does not choose a product rule or a technical rule.
- **Specs.** The plan writes its spec in `specs/` in the same pull request. Each section names its decision keys. Each decision the plan built moves to `state:implemented`.
- **Gate.** The plan 19 gate: configure and build with sanitizers, `ctest`, `scripts/lint-all.sh`, `scripts/check-coverage.sh`, `scripts/ci-linux.sh`, and the `leaks-macos` target. Coverage stays 100% functions and 100% effective lines.
- **Proof.** Every new clause has a test that a one-clause mutant turns red. Expected values are literals.
- **Copy.** The plan quotes every sentence. The coder prints that sentence and no other. The approved copy batch is `decision:2026-11-12-copy-batch-final`, and the message and fix of every warning are in `decision:2026-11-12-copy-batch-e3b-warning-splits` (E3 B: a warning has a `message` and a non-empty `fix`, as an error does). `confirmation_required` exits 5 everywhere (E2 A).
- **Hook failures.** A run that started and failed is retried with backoff from 1 second, doubling to a 1 hour cap, then `failed` (X3 A). A pre-flight failure is `failed` at once and is never retried (E1 A).
- **Prerequisite.** The decisions the plan lists are accepted in Remember before the coder starts.

Decided on 2026-11-11 and now built by a plan: `decision:2026-11-11-filter-degraded-exit` (plan 37), `decision:2026-11-11-plugin-enabled-bool` (plan 30).

The deleted files `25-plugins-and-hooks.md` and `26-release-0.3.0.md` are not plans. Do not implement them.

Locked decisions from the 2026-10-12 plan grill remain law for behavior that
007–010 did not replace:
[`006-locked-decisions.md`](006-locked-decisions.md). Add-wins prefix merge
(E12.6) and the E12.9 settings object are replaced. E11 (Android presets) is
not planned here. Each plan carries its own workflow, coding standards,
quality gate, Definition of Done, and second-review handover.

The old plan files `19-bucket-prefixes.md`, `20-export.md`,
`21-import-formats.md`, and `22-brain-path-skills-release.md` are deleted.
Do not implement them. Log 008's draft line that says to re-extract plans
16–18 and 22 is not a plan. Do not re-extract those plans.

**Definition of done for the whole tool:** full suite green (all verification criteria 1–34 automated where practical), `cmake --build build --target lint` clean on `src/`, no `#include` of sqlite outside the store adapter. Plan 14 adds criteria from design 005.

## Design append note (criteria 21–34)

Design log verification was expanded. New/edge cases live mainly in `tests/test_verification_edges.c` (plus earlier key/update/search files). Each step plan lists which of those must pass.

**Dev dependency:** some GC/schema inspect tests call the **`sqlite3` CLI** on PATH. Install: `brew install sqlite` if missing.

## Cross-cutting quality gate (every step)

1. `cmake --build build && ./build/remember_tests ./build/remember` — required tests for that step pass; none regressed.
2. Format: `cmake --build build --target format` (or clang-format on touched files).
3. Lint: after step 08, full lint; before that, at least compile with `-Werror`.
4. No new SQLite knowledge outside `store_sqlite.c`.
