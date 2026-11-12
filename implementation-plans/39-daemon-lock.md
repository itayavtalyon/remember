# 39 — Daemon process: run dir, lock, daemon.json, daemon.sock

**Module:** remember engine (C): the daemon process (`src/run_dir.c`, `src/daemon*.c`, the `remember daemon` foreground command).
**Roadmap line:** R 013 Daemon, local MCP, bots — subject:daemon. Itay has not ordered R 013 against the other v1 features (roadmap:remember ORDER). Itay orders R 013 before work starts.
**Needs:** plans 21 (merged) and 35 (the dispatcher whose runs the drain cancels). No other plan (G7 A).
**Blocked on:** copy item R28-2 (Story Weaver copy request, 2026-11-11, not yet approved).

## Goal

One Remember daemon runs per user. The daemon takes an exclusive lock, publishes `daemon.json`, serves `daemon.sock` with `daemon.hello` and `daemon.shutdown`, and drains in 5 s. A losing daemon exits 0. The migrating process stops the daemon and restarts it. This plan binds no port and ships no REST route, no MCP tool, no sync, no sweep job, and no event subscription.

## Source

Decisions built (all `state:accepted`, subject:daemon):

- decision:2026-10-31-lock-run-dir
- decision:2026-10-31-linux-run-dir, amended by decision:2026-10-31-daemon-xdg-runtime-invalid
- decision:2026-10-31-lock-per-user
- decision:2026-10-31-lock-flock
- decision:2026-10-31-daemon-json-lifecycle
- decision:2026-10-31-daemon-json-discovery, with decision:2026-10-31-daemon-json-shape and decision:2026-11-01-daemon-json-keys
- decision:2026-10-31-socket-wire
- decision:2026-10-31-socket-hello, with decision:2026-10-31-daemon-socket-message-types
- decision:2026-10-31-daemon-socket-bad-input
- decision:2026-10-31-shutdown-auth
- decision:2026-10-31-shutdown-reply, amended by decision:2026-10-31-shutdown-reply-after-drain (timing)
- decision:2026-10-31-shutdown-drain
- decision:2026-10-31-drain-cap-busy
- decision:2026-10-31-sigterm-drain (the daemon half: SIGTERM runs the same drain)
- decision:2026-10-31-migrate-lock, with decision:2026-11-01-daemon-restart-policy (the migrating process restarts the daemon)
- decision:2026-10-31-daemon-owner-from-label, with decision:2026-11-01-daemon-owner-unknown-label
- decision:2026-10-31-lock-loser-exit-0
- decision:2026-10-31-manual-daemon-start
- decision:2026-10-31-cli-direct (the CLI does not need the daemon)
- decision:2026-10-31-daemon-port-deferred (scope: no port in this plan)

Not built here, for context only: decision:2026-11-01-daemon-restart-policy (the `KeepAlive` half; plans 40 and 41 and Mac plans M29 and M30 write the plists), decision:2026-11-01-loser-label-disabled (the callers run `launchctl disable`; plans 40 and 41 and Mac plans M29 and M30), decision:2026-10-31-http-port (moves to the first REST/MCP plan), decision:2026-10-31-daemon-subscribe-protocol and decision:2026-10-31-daemon-event-subscribe (a future plan, see Out of scope).

Legacy context (frozen; a decision wins over a log): design-logs/008 §1–§2 (envelope), design-logs/010 H-Q20, H-Q25, G-07, design-logs/018 V1-18, V1-19, design-logs/020 Q5–Q6.

Spec sections this plan changes:

- New `specs/DAEMON.md`: §1 Run dir, §2 Lock, §3 daemon.json, §4 daemon.sock, §5 Shutdown and drain, §6 migrate.lock, §7 Owner. Each section names the decision keys behind it.
- `specs/README.md`: the `daemon.md` row.

Workflow adoption (this is the first plan in this repo after 2026-10-31, per workflow:design-log-grill LEGACY): this branch already holds `specs/README.md` and the frozen banner on `design-logs/index.md` and on each log. They stay in this PR. decision:2026-10-31-design-in-remember requires "a banner that points to the spec and the Remember subjects". The drafted banner does that.

## Out of scope

- The HTTP listener, port 7337, REST routes, `/mcp`, bot tokens (decision:2026-10-31-daemon-port-deferred, R 012, decision:mcp-tool-catalog, decision:mcp-bot-connect).
- `daemon.subscribe` and `daemon.event` (decision:2026-10-31-daemon-subscribe-protocol). A future plan builds them after Itay grills the event list. That plan is not drafted.
- Sync, the expiry sweep, warm filters, backfills, the hook queue inside the daemon (decision:daemon-jobs-registry). A later plan adds each job.
- The plugin dir argument and `remember daemon install` (plan 40).
- `remember daemon takeover`, owner-missing detection, launchctl calls, old-CLI read-only (plans 29 and 30).
- Fortkeeps UI (Mac plans M29 and M30).

## Standards

- **Coding bar.** `standard:coding` and its matching narrow keys. The decisions in Source and the accepted decisions of their subject are loaded before work starts (`workflow:decision-tags` READ).
- **Decisions are law.** The plan builds only what an accepted decision or a frozen log states. A choice that no decision makes is a grill, recorded as a decision before it is built.
- **Spec.** The spec sections in Source ship in the same pull request, each naming its decision keys. Each decision built here is `state:implemented` in the same pull request.
- **Gate.** `just check` (or the repo gate) passes on the exact committed tree with no warnings. `scripts/check-coverage.sh` stays at 100% effective. Linux CI (`scripts/ci-linux.sh`) runs every test that is not macOS-only.
- **Proof.** TDD: the Definition of Done boxes are the must-pass tests, in TDD order. Each test is red before its production change. Every rule has a test that a one-clause mutant turns red. Expected values are literals or an independent computation.
- **OS integrations.** Share, Spotlight, login items, Shortcuts, and other OS integrations are Fortkeeps-only. They never go in the open-source engine or daemon (decision:2026-11-03-os-integrations-fortkeeps-only).
- **Copy.** The `message`, `fix`, and `retryable` of each new error code equal the R28-2 decision text. JE-2.6 requires a non-empty `fix`.
- **Prerequisites.** Work starts only when each item below holds. When one does not hold, the work stops and Itay is told:
  - The copy item **R28-2 (Story Weaver)** is a decision: The `message`, `fix`, and `retryable` of `daemon_already_running`, `daemon_busy`, and `daemon_unresponsive`, and the human line that a losing daemon prints. JE-2.6 requires a non-empty `fix`.

## What must be true

The rules below hold. Each quote is decision text.

Exact rules, quoted from the decisions:

- Run dir, macOS: "~/Library/Application Support/Remember/run/ on macOS, computed by the engine so the app and every CLI agree."
- Run dir, Linux: "$XDG_RUNTIME_DIR/remember/, falling back to ~/.local/state/remember/run/."
- Invalid XDG: "If XDG_RUNTIME_DIR is set but empty or not writable, treat it as unset and use ~/.local/state/remember/run/."
- Scope: "one daemon per user, whatever brains it serves."
- Lock: "flock(LOCK_EX|LOCK_NB) on <run dir>/daemon.lock, held for the daemon's lifetime; the kernel releases it on crash, so it never goes stale."
- daemon.json lifecycle: "written via temp file + rename after taking daemon.lock; deleted on clean exit; readers trust it only while daemon.lock is held (a non-blocking flock attempt fails)."
- daemon.json content: "<run dir>/daemon.json (pid, engine and schema versions, owner, socket, port) replaces remember-http.json."
- daemon.json shape: "daemon.json uses the key format: remember-daemon (version 1) and has no port field until the REST plan." Use the `daemon.hello` field names for the other keys: `pid`, `engine_version`, `schema_version`, `owner`, and `socket`.
- Wire: "newline-delimited JSON, one 008 envelope object per line."
- Message types: "Socket message types are daemon.hello and daemon.shutdown; replies are daemon.hello.result and daemon.shutdown.result (008 envelope)."
- Hello: "the client sends hello {protocol:1, engine_version, schema_version, owner: fortkeeps|homebrew|cli, pid}; the daemon replies with the same shape about itself."
- Bad input: "On daemon.sock, a malformed line gets an invalid_argument error reply; a connection from a foreign uid is closed silently."
- Auth: "daemon.sock is mode 0600; the daemon checks the peer uid with getpeereid and accepts only the same user."
- Shutdown drain: "finish the current write, cancel running plugin runs, with a 5 s cap inside the 10 s takeover budget."
- Shutdown reply: "after the drain. Success: {ok:true, changed:true}, then release daemon.lock and exit 0. Drain over 5 s: error daemon_busy and keep running."
- SIGTERM: "yes, the same drain with a 5 s cap." (The 10 s wait and the no-SIGKILL rule belong to the callers: plans 40 and 41 and Mac plans M29 and M30.)
- Loser: "a daemon that finds daemon.lock held exits 0 with daemon_already_running."
- Owner: "The daemon infers its owner from its launchd label: app.fortkeeps.daemon is fortkeeps, io.github.itayavtalyon.remember is homebrew, and no label is cli."
- Unknown label: "A daemon with an unknown launchd label (e.g. homebrew.mxcl.remember) reports owner homebrew."
- Manual start: "anyone; the lock decides; a manual daemon reports owner:cli."
- Migration: "the migrating process takes <run dir>/migrate.lock, stops the daemon through daemon.sock, migrates, then restarts it."
- Migration restart (decision:2026-11-03-migration-kickstart): the migrating process restarts the daemon with `launchctl kickstart` of the owner's label; on Linux, with `systemctl --user`; for owner `cli`, it restarts nothing. A daemon started mid-migration waits for `migrate.lock`.
- Restart policy: "Both daemon plists use KeepAlive {SuccessfulExit:false}; after a migration the migrating process restarts the daemon itself (a losing daemon exits 0 and is not relaunched)."
- Dormant loser: "The losing daemon stays dormant until hand-back or uninstall." The callers disable the losing label with `launchctl disable` (decision:2026-11-01-loser-label-disabled). The engine does not call `launchctl` in this plan.
- Port: "The port 7337 rule (fall back to a free port recorded in daemon.json) moves to the first REST/MCP plan; this plan binds no port."

Envelope rules that apply (design-logs/008 JE-2.1–JE-2.9): field order `format`, `version`, `type`, `request_id`, `data` or `error`, `warnings`. `error` has `code`, `message`, `fix`, `retryable`. The sender sets `request_id` (UUID v7). The reply copies it. JE-2.4: when the daemon cannot tell the request, the failure `type` is `unknown.result`. JE-1.5: a stored file starts with `format` and `version` and has no `type`.

## Review points

- No path trusts `daemon.json` while a non-blocking flock attempt on `daemon.lock` succeeds.
- The drain cap and the 2 s and 10 s budgets come from an injectable clock, and no test sleeps for real time.
- A foreign uid gets no byte back, and a malformed line never ends the daemon.
- The owner comes from the launchd label only. No flag or environment variable sets it.
- The daemon opens no TCP socket and writes no `remember-http.json`.
- The engine itself runs no `launchctl` command except the migration kickstart through the injectable runner.
- The diff stays inside these files. A change outside them is a finding. Each change in them follows What must be true:
  - `src/run_dir.c`, `src/run_dir.h` (new).
  - `src/daemon.c`, `src/daemon.h` (new).
  - `src/daemon_client.c`, `src/daemon_client.h` (new).
  - `src/cli.c`, `src/commands.h`.
  - `src/diag.c`, `src/diag.h`.
  - `tests/test_daemon_run_dir.c`, `tests/test_daemon_lock.c`, `tests/test_daemon_socket.c`, `tests/test_daemon_shutdown.c`, `tests/test_daemon_migrate.c` (new).
  - `CMakeLists.txt`.
  - `specs/DAEMON.md` (new), `specs/README.md`.
  - `tests/COVERAGE.md`, `docs/engineering-notes.md`.

## Definition of Done

- [ ] `run_dir_macos`: when `HOME=/tmp/h` on macOS, the run dir is `/tmp/h/Library/Application Support/Remember/run/`.
- [ ] `run_dir_linux_xdg`: when `XDG_RUNTIME_DIR=/run/user/501` on Linux and the dir is writable, the run dir is `/run/user/501/remember/`.
- [ ] `run_dir_linux_fallback`: when `XDG_RUNTIME_DIR` is unset on Linux, the run dir is `$HOME/.local/state/remember/run/`.
- [ ] `run_dir_linux_empty_xdg`: when `XDG_RUNTIME_DIR` is the empty string, the run dir is `$HOME/.local/state/remember/run/`.
- [ ] `run_dir_linux_unwritable_xdg`: when `XDG_RUNTIME_DIR` names a dir that is not writable, the run dir is `$HOME/.local/state/remember/run/`.
- [ ] `run_dir_created_0700`: the engine creates a missing run dir with mode 0700.
- [ ] `lock_first_wins`: the first daemon takes `daemon.lock` with `flock(LOCK_EX|LOCK_NB)`.
- [ ] `lock_loser_exit_0`: a second daemon for the same user exits 0 with `daemon_already_running`. Assert the exact stdout and stderr that R28-2 sets.
- [ ] `lock_released_on_crash`: after the holder gets SIGKILL from the test, a new daemon takes the lock.
- [ ] `daemon_json_after_lock`: `daemon.json` appears only after the lock is held, through a temp file and `rename`. Assert that no partial file is ever visible.
- [ ] `daemon_json_versioned_document`: `daemon.json` equals `{"format":"remember-daemon","version":1,"pid":…,"engine_version":…,"schema_version":…,"owner":…,"socket":…}` in that key order. Assert that it has no `type` key and no `port` key.
- [ ] `daemon_json_deleted_on_clean_exit`.
- [ ] `daemon_json_untrusted_without_lock`: when `daemon.json` exists and a non-blocking flock attempt succeeds, the client reports no daemon.
- [ ] `owner_label_fortkeeps`: when the launchd label is `app.fortkeeps.daemon`, `daemon.json` and the `daemon.hello.result` reply carry owner `fortkeeps`.
- [ ] `owner_label_homebrew`: when the label is `io.github.itayavtalyon.remember`, the owner is `homebrew`.
- [ ] `owner_no_label_cli`: when no launchd label is present, the owner is `cli`. This also covers `manual_start_owner_cli`.
- [ ] `owner_unknown_label`: when the label is `homebrew.mxcl.remember` or any other unknown string, the owner is `homebrew`.
- [ ] `sock_mode_0600`.
- [ ] `sock_peer_uid_rejected`: when `getpeereid` returns another uid, the daemon closes the connection and sends no reply. Use a test seam for the uid.
- [ ] `sock_ndjson_one_envelope_per_line`: two requests in one write get two replies, one per line.
- [ ] `sock_malformed_line`: a line that is not JSON, a truncated line, and a 1 MiB line each get an error reply with `code` `invalid_argument`, `type` `unknown.result` (JE-2.4), and a non-empty `fix`. The daemon keeps the connection and keeps running.
- [ ] `hello_roundtrip`: the client sends `type` `daemon.hello` with `{protocol:1, engine_version, schema_version, owner, pid}`. The reply has `type` `daemon.hello.result` and the same shape with the daemon values. The reply copies `request_id`.
- [ ] `shutdown_idle`: `daemon.shutdown` with no write in flight replies `daemon.shutdown.result` with `{ok:true, changed:true}`, releases `daemon.lock`, deletes `daemon.json`, and exits 0. Assert that the reply arrives before the lock is free.
- [ ] `shutdown_finishes_write`: a write in flight completes and commits. Then the reply arrives.
- [ ] `shutdown_cancels_plugin_runs`: a running plugin run is cancelled (plan 35 dispatcher).
- [ ] `shutdown_drain_cap_busy`: when the drain passes 5 s, the reply is `daemon.shutdown.result` with error `daemon_busy`. The daemon keeps the lock and keeps running. Use an injectable clock.
- [ ] `sigterm_same_drain`: SIGTERM runs the same drain with the same 5 s cap.
- [ ] `migrate_lock_stops_daemon`: the migrating process takes `<run dir>/migrate.lock`, sends `daemon.shutdown`, migrates, and releases `migrate.lock`.
- [ ] `migrate_restarts_daemon`: after the migration, the migrating process restarts the daemon that it stopped, with the same owner: owner `fortkeeps` or `homebrew` → `launchctl kickstart gui/<uid>/<label of that owner>` on macOS, `systemctl --user start <unit>` on Linux; owner `cli` → nothing. When no daemon ran before the migration, it starts none. Assert the fake runner calls.
- [ ] `start_waits_on_migrate_lock`: a daemon that starts while `migrate.lock` is held does not take `daemon.lock` until `migrate.lock` is free.
- [ ] `no_port_bound`: the daemon opens no TCP socket.
- [ ] `remember_http_json_gone`: no code path writes `remember-http.json`.

- [ ] `scripts/check-coverage.sh` reports 100% effective coverage for each new file.
- [ ] Linux CI (`scripts/ci-linux.sh`) runs every test above except `run_dir_macos`.
- [ ] `specs/DAEMON.md` describes the shipped behavior and names the decision keys per section.
- [ ] Each decision in Source under "Decisions built" has `state:implemented`.
- [ ] `docs/engineering-notes.md` names `daemon_client.h` as the API that plans 40 to 42 call.
- [ ] `README.md` status is truthful.
- [ ] The full gate passes on the committed tree.
- [ ] The Implementation Notes record the mutants and their red tests, the probes past this plan, and every deviation.
- [ ] `.claude/REVIEW_REQUEST` is written per `workflow:stage-review-handoff`.

## Implementation Notes

- Note: launchd sets `XPC_SERVICE_NAME` to the job label in the job environment. The owner decision names the label and does not name the read mechanism. Record the mechanism you use here.
- (Record mutants, probes, and deviations here.)

## Review Notes

- Contradiction cleared (2026-11-01): the v2 draft cited decision:2026-10-31-lock-loser-exit-1 and decision:daemon-channel in Source. Both are `state:superseded`. This draft cites neither. decision:2026-10-31-lock-loser-exit-0 replaces the first. decision:2026-10-31-socket-hello, decision:2026-10-31-daemon-socket-message-types, and decision:2026-10-31-cli-direct replace the second.
- decision:2026-10-31-daemon-migration-restart is `state:superseded` by decision:2026-11-01-daemon-restart-policy. The 2026-11-03 answer to R28-4 brings back its "a daemon started mid-migration waits for migrate.lock" rule (test 30). Record that answer as a new decision; do not revive the superseded entry.
- Reshaped 2026-11-03 to the no-steps plan shape (decision:2026-11-03-plan-no-steps). The Files list is a review point, the Context rules are What must be true, the must-pass tests are the Definition of Done, and the open items are Prerequisites.
