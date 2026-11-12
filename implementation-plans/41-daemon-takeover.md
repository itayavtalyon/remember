# 41 — CLI daemon takeover

**Area:** `remember daemon takeover [--yes]` in `src/cmd_daemon.c`, `src/launchd.c`, `src/systemd_user.c` (enable/disable), `src/exit_codes.h`.
**Needs:** plans 39 and 40. No other plan.
**Blocked on:** nothing. The copy is item R30-4 in `decision:2026-11-12-copy-batch-final`.

## Goal

`remember daemon takeover [--yes]` installs the brew plist when it is missing, disables `app.fortkeeps.daemon`, enables the brew label, and reports `{changed, previous_owner}`.

## Source



Decisions built:

- decision:2026-11-03-takeover-order
- decision:2026-11-03-confirmation-required-exit-5 (amended by decision:2026-11-12-e2-confirmation-required-exits-5-everywhere: `confirmation_required` exits 5 everywhere)
- decision:2026-11-03-naming-defaults (`daemon.takeover.result` `{changed, previous_owner}`)

- decision:2026-10-31-cli-takeover-offer
- decision:2026-10-31-trashed-app-handback (the CLI half)
- decision:2026-10-31-owner-missing-detect, with decision:2026-10-31-owner-missing-program-path
- decision:2026-10-31-owner-missing-linux
- decision:2026-10-31-daemon-owner-missing-check
- decision:2026-10-31-never-ask-state, with decision:2026-10-31-state-json-shape and decision:2026-11-01-state-json-keys
- decision:2026-10-31-takeover-prompt-ci
- decision:2026-10-31-daemon-takeover-yes-flag, scoped by decision:2026-11-01-takeover-yes-scope
- decision:2026-11-01-takeover-missing-brew-plist
- decision:2026-11-01-loser-label-disabled (the CLI half)
- decision:2026-10-31-daemon-takeover-noop
- decision:2026-10-31-hung-daemon
- decision:2026-10-31-old-daemon-shutdown-fallback (CLI side)
- decision:2026-10-31-sigterm-drain (caller side: 10 s, never SIGKILL)
- decision:2026-10-31-takeover-timeout
- decision:2026-10-31-daemon-two-labels
- decision:2026-10-31-coexistence-copy (CLI prompt copy)

Spec sections this plan changes: `specs/DAEMON.md` §9 Owner missing, §10 `remember daemon takeover`, §11 Unresponsive daemon, §14 state.json.

## Out of scope

- The `app.fortkeeps.daemon` plist itself (Mac plan M29).
- A Homebrew formula `service` block.
- `daemon.subscribe` and `daemon.event` (no plan; event list not grilled).
- The Fortkeeps takeover and Remove (Mac plans M30, M31).
- A Linux Fortkeeps daemon. This plan only reads the systemd user unit.

## Standards

- **Coding bar.** `standard:coding` and its matching narrow keys. The decisions in Source and the accepted decisions of their subject are loaded before work starts (`workflow:decision-tags` READ).
- **Decisions are law.** The plan builds only what an accepted decision or a frozen log states. A choice that no decision makes is a grill, recorded as a decision before it is built.
- **Spec.** The spec sections in Source ship in the same pull request, each naming its decision keys. Each decision built here is `state:implemented` in the same pull request.
- **Gate.** `just check` (or the repo gate) passes on the exact committed tree with no warnings. `scripts/check-coverage.sh` stays at 100% effective. Linux CI (`scripts/ci-linux.sh`) runs every test that is not macOS-only. The real `launchctl` and `systemctl` never run in a test, and no test writes to the real `~/Library/LaunchAgents`.
- **Proof.** TDD: the Definition of Done boxes are the must-pass tests, in TDD order. Each test is red before its production change. Every rule has a test that a one-clause mutant turns red. Expected values are literals or an independent computation.
- **OS integrations.** Share, Spotlight, login items, Shortcuts, and other OS integrations are Fortkeeps-only. They never go in the open-source engine or daemon (decision:2026-11-03-os-integrations-fortkeeps-only).
- **Copy.** The y/N confirmation of `remember daemon takeover` and the `message` and `fix` of `confirmation_required` equal copy item R30-4 in `decision:2026-11-12-copy-batch-final`.
  - y/N confirmation (TTY, stderr, default No): `Take over Remember's background service? Your memories and settings stay where they are. [y/N]`
  - `confirmation_required`: exit 5, `retryable` false, nothing changes. `message`: `taking over Remember's background service needs your confirmation.` `fix`: `Run 'remember daemon takeover --yes' to confirm.` Plain line: `remember: taking over Remember's background service needs your confirmation. Run 'remember daemon takeover --yes' to confirm.`
- **Prerequisites.** Work starts only when each item below holds. When one does not hold, the work stops and Itay is told:
  - Copy item R30-4 is approved (2026-11-12). The `fix` reuses the approved C-11 form with the command written out.


## What must be true

The rules below hold. Each quote is decision text.

- `--yes`: "--yes applies only to remember daemon takeover; without a TTY and without --yes, takeover fails."
- Missing brew plist: "If the brew plist is missing, remember daemon takeover runs daemon install first."
- Dormant loser: "remember daemon takeover (trashed app) disables app.fortkeeps.daemon and enables the brew label. Combined with KeepAlive {SuccessfulExit:false}, a losing daemon never retries."
- Command: "New command remember daemon takeover (and --yes) reports changed true/false and the previous owner."
- No-op: "remember daemon takeover when Homebrew already runs the daemon returns ok with changed:false."
- Hung daemon: "2 s connect/hello timeout, then error daemon_unresponsive with the pid from daemon.json."
- Old daemon: "fall back to launchctl bootout, then SIGTERM to the pid in daemon.json." Then: "If the process is still alive after 10 s, the takeover fails. Never SIGKILL."
- Labels: Fortkeeps installs `app.fortkeeps.daemon`. Open-source docs and Homebrew use `io.github.itayavtalyon.remember`.

## Review points

- The takeover order is asserted on the fake runner calls: bootout, disable `app.fortkeeps.daemon`, enable the brew label, then install with one bootstrap.
- No path sends SIGKILL.
- `--yes` is accepted by `daemon takeover` only.
- Exit code 5 belongs to `confirmation_required` only, and exit 4 stays the `--strict` warning code (G2-03).
- The owner check reads `state.json` and probes the lock once per process.
- A write of `state.json` keeps every key it does not own.
- The diff stays inside these files. A change outside them is a finding. Each change in them follows What must be true:
  - `src/cmd_daemon.c`.
  - `src/exit_codes.h`.
  - `src/launchd.c`, `src/launchd.h` (new).
  - `src/systemd_user.c`, `src/systemd_user.h` (new, Linux).
  - `src/state_file.c`, `src/state_file.h` (new).
  - `src/remember_app.c`.
  - `src/diag.c`, `src/diag.h`.
  - `tests/test_daemon_takeover.c`, `tests/test_owner_missing.c`, `tests/test_state_file.c` (new).
  - `specs/DAEMON.md` §9–§11, §14.

## Definition of Done

- [ ] `yes_flag_takeover_only`: `remember daemon takeover --yes` takes over without a prompt. Any other command with `--yes` fails with the usage error for an unknown option.
- [ ] `takeover_changed_true`: `remember daemon takeover --yes` boots out `app.fortkeeps.daemon`, runs `launchctl disable gui/<uid>/app.fortkeeps.daemon`, runs `launchctl enable gui/<uid>/io.github.itayavtalyon.remember`, then calls the install of this plan (which writes the plist when needed and runs the only `launchctl bootstrap`), and replies `daemon.takeover.result` with `data` `{changed: true, previous_owner: "fortkeeps"}`. Assert the order of the fake runner calls, and assert exactly one `bootstrap` call.
- [ ] `takeover_changed_false`: when Homebrew already runs the daemon, the command returns ok with `changed` false.
- [ ] `takeover_no_brew_plist`: when `~/Library/LaunchAgents/io.github.itayavtalyon.remember.plist` does not exist, the command runs the install of this plan first, then the takeover.
- [ ] `takeover_without_yes`: on a TTY, `remember daemon takeover` asks the y/N confirmation (copy: R30-4). Without a TTY and without `--yes`, it fails with error `confirmation_required`, exits 5 (`REMEMBER_CONFIRMATION_REQUIRED`), and changes nothing. Assert the value 5 and the R30-4 `message` and `fix`. The exit is 5 on every path that returns `confirmation_required` (E2 A, `decision:2026-11-12-e2-confirmation-required-exits-5-everywhere`).
- [ ] `exit_code_unique`: no other error or warning path exits 5, and the spec exit code table lists 5 = `confirmation_required`.
- [ ] `takeover_hung_daemon_2s`: no `daemon.hello.result` in 2 s → error `daemon_unresponsive` with the pid from `daemon.json`. Use an injectable clock.
- [ ] `takeover_old_daemon_fallback`: the daemon does not know `daemon.shutdown` → `launchctl bootout`, then SIGTERM to the pid.
- [ ] `takeover_10s_fail_no_sigkill`: the process lives past 10 s → the takeover fails. Assert that no SIGKILL is sent.
- [ ] `takeover_daemon_busy`: the daemon replies `daemon_busy` → the takeover fails with that code.
- [ ] `specs/DAEMON.md` §10 and §11 name the decision keys, and the takeover decisions in Source are `state:implemented`.
- [ ] The full gate passes on the committed tree.

## Implementation Notes

## Review Notes
