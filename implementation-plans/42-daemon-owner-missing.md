# 42 — Owner-missing check and the one-time prompt

**Area:** the pre-command owner check in `src/remember_app.c`, `src/state_file.c` (`state.json`), and the prompt and warning.
**Needs:** plans 39 and 41 (a `y` answer runs the takeover). No other plan.
**Blocked on:** copy item R30-4 (Story Weaver copy request, 2026-11-11, not yet approved) for the hint line and the `daemon_owner_missing` `message` and `fix`. The prompt line itself is locked (below).

## Goal

Every command checks for a missing owner (Fortkeeps removed but still registered). On a TTY the CLI asks once and records the answer; otherwise it warns until resolved.

## Source



Decisions built:

- decision:2026-11-03-takeover-order
- decision:2026-11-03-confirmation-required-exit-5
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
- **Copy.** The hint line, the y/N confirmation, and the `message` and `fix` of `daemon_owner_missing` and `confirmation_required` equal the R30-4 decision text.
- **Prerequisites.** Work starts only when each item below holds. When one does not hold, the work stops and Itay is told:
  - The copy item **R30-4 (Story Weaver)** is a decision: The hint line, the `daemon_owner_missing` `message` and `fix`, the y/N confirmation of `remember daemon takeover` on a TTY, and the `confirmation_required` `message` and `fix`.


## What must be true

The rules below hold. Each quote is decision text.

- Detection: "daemon.lock is free and the app.fortkeeps.daemon registration points at a missing binary."
- Program path: "Fortkeeps records the app.fortkeeps.daemon program path in state.json when it registers; the CLI reads it from there to detect owner-missing."
- Linux: "On Linux the owner-missing check inspects the systemd user unit, kept for a future Linux Fortkeeps daemon."
- Frequency: "The CLI checks for owner-missing on every command, with a cheap check cached per process."
- Prompt rule: "on a TTY, one prompt on stderr before the command, default No; y takes over then runs the command; the answer is recorded so it never asks again; both paths print the remember daemon takeover hint."
- Prompt copy (stderr, TTY only, default No), locked: `Fortkeeps was removed but still owns Remember's background service. Take it over? [y/N]`
- Non-TTY rule: "With --json, non-TTY or CI: never prompt; add warning {code: daemon_owner_missing, hint: remember daemon takeover} until resolved; exit code follows the command."
- CI: "There is no separate CI check: without a TTY the CLI never prompts."
- State keys: "never_ask.fortkeeps_takeover for the sheet answer and daemons.<label>.program for recorded daemon program paths."
- State file location: "device-local in ~/Library/Application Support/Remember/state.json (parent of the run dir), not in the brain."
- State file shape: "state.json is a versioned document {format: remember-state, version: 1, never_ask: {daemon_takeover: true}}, written by temp file and rename; a malformed file reads as empty and is rewritten on the next answer."

## Review points

- The owner check reads `state.json` and probes the lock once per process.
- A write of `state.json` keeps every key it does not own.
- A `y` answer calls the plan 41 takeover function; it does not copy its steps.

## Definition of Done

- [ ] `state_json_path`: `state.json` is `~/Library/Application Support/Remember/state.json` on macOS.
- [ ] `state_json_shape`: after a No answer, `state.json` equals `{"format":"remember-state","version":1,"never_ask":{"daemon_takeover":true}}`.
- [ ] `state_json_atomic`: a write goes through a temp file and `rename`.
- [ ] `state_json_malformed_reads_empty`: a truncated or malformed `state.json` reads as empty. The next answer rewrites it as a valid document.
- [ ] `state_json_keeps_other_keys`: a write of `never_ask.daemon_takeover` keeps `never_ask.fortkeeps_takeover` and `daemons.app.fortkeeps.daemon.program`.
- [ ] `owner_missing_detect_true`: lock free, and `daemons.app.fortkeeps.daemon.program` in `state.json` names a file that does not exist → owner missing.
- [ ] `owner_missing_lock_held`: lock held → not missing.
- [ ] `owner_missing_no_recorded_path`: `state.json` has no program path → not missing.
- [ ] `owner_missing_binary_present`: the recorded program path exists → not missing.
- [ ] `owner_missing_linux_systemd`: on Linux, the check reads the systemd user unit `app.fortkeeps.daemon.service`, with the program path from `daemons.app.fortkeeps.daemon.program`. Missing unit → not missing. Unit present and its program missing → missing.
- [ ] `owner_check_every_command_cached`: every command runs the check. Inside one process, the check reads `state.json` and probes the lock once.
- [ ] `prompt_tty_shown_once`: on a TTY, the locked prompt appears on stderr before the command. After any answer, `state.json` records `never_ask.daemon_takeover`, and a second command does not prompt.
- [ ] `prompt_default_no`: an empty line and EOF mean No. The command runs. The hint prints.
- [ ] `prompt_yes_takes_over_then_runs`: `y` runs the takeover, then the command.
- [ ] `prompt_hint_both_paths`: both answers print the `remember daemon takeover` hint with the copy that R30-4 sets.
- [ ] `json_never_prompts`: with `--json`, stdout `warnings[]` has `{code: daemon_owner_missing, hint: remember daemon takeover}`. The exit code follows the command.
- [ ] `non_tty_never_prompts`: stdin not a TTY → the warning on stderr, no prompt. The test sets `CI=true` too and asserts the same result.
- [ ] `warning_until_resolved`: the warning repeats on each command until the owner is no longer missing, also after a No answer.
- [ ] `specs/DAEMON.md` §9 and §14 name the decision keys, and the owner-missing decisions in Source are `state:implemented`.
- [ ] The full gate passes on the committed tree.

## Implementation Notes

## Review Notes
