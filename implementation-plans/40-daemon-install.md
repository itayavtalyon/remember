# 40 — Daemon service install and the plugin dir

**Area:** `remember daemon install`, `src/launchd_plist.c`, `src/systemd_user.c` (unit writer), and the `--plugin-dir` argument of `remember daemon`.
**Needs:** plans 39 (daemon process) and 34 (the plugin search path). No other plan.
**Blocked on:** nothing.

## Goal

The open-source daemon runs as a user service with no Fortkeeps. `remember daemon install` writes `~/Library/LaunchAgents/io.github.itayavtalyon.remember.plist` and bootstraps it; on Linux it writes and starts the systemd user unit. `remember daemon --plugin-dir PATH` searches that dir before every other plugin dir.

## Source

Decisions built (subject:daemon, `state:accepted`):

- decision:2026-10-31-daemon-install-command (amends design-logs/018 V1-18, "engine installs no background job")
- decision:2026-10-31-trashed-app-handback (the docs half: "the docs cover remember daemon install")
- decision:2026-10-31-daemon-plugin-dir-arg
- decision:2026-10-31-plugins-bundled-win (subject:plugins-and-hooks; the engine half: the plugin dir that Fortkeeps passes wins)
- decision:2026-10-31-daemon-two-labels (the brew label)
- decision:2026-11-01-daemon-restart-policy (the brew plist half)
- decision:2026-11-01-takeover-missing-brew-plist (context: the takeover area calls install)
- decision:2026-10-31-path-brew-first (docs)

Spec sections this plan changes: `specs/DAEMON.md` §12 `remember daemon install`, §13 Plugin dir argument. When the plugin search order lives in a plugins spec, that section changes in the same PR.
- decision:2026-11-03-daemon-install-bootstraps (R29a-2 to R29a-4)

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
- **Copy.** This plan prints no new sentence. `daemon.install.result` is JSON only; the human line is `changed: <true|false>  path: <path>`, the existing key-value form of `src/output.c`.


## What must be true

The rules below hold. Each quote is decision text.

- Install: "New command remember daemon install writes ~/Library/LaunchAgents/io.github.itayavtalyon.remember.plist; amends V1-18 (engine installs no background job)."
- Labels: "The Fortkeeps app installs app.fortkeeps.daemon; open-source docs and Homebrew use io.github.itayavtalyon.remember."
- Plugin dir: "The app.fortkeeps.daemon plist passes a plugin dir argument that the engine searches first."
- Bundled plugins: "Fortkeeps' bundled copy runs while Fortkeeps runs the daemon."
- Restart policy: "Both daemon plists use KeepAlive {SuccessfulExit:false}; after a migration the migrating process restarts the daemon itself (a losing daemon exits 0 and is not relaunched)."
- Missing plist: "If the brew plist is missing, remember daemon takeover runs daemon install first." The takeover area calls the install function.

## Review points

- Install's `launchctl bootstrap` is the only bootstrap in the whole command family (decision:2026-11-03-takeover-order).
- An identical plist is not rewritten, and a differing one is replaced through a temp file and `rename`.
- The plugin dir argument comes first in the search order and changes nothing when it is absent.
- The diff stays inside these files. A change outside them is a finding. Each change in them follows What must be true:
  - `src/cmd_daemon.c`, `src/cmd_daemon.h` (new).
  - `src/launchd_plist.c`, `src/launchd_plist.h` (new).
  - `src/daemon.c`.
  - The plan 34 plugin search code.
  - `tests/test_daemon_install.c`, `tests/test_daemon_plugin_dir.c` (new).
  - `docs/getting-started.md`.
  - `specs/DAEMON.md` §12–§13.

## Definition of Done

- [ ] `install_writes_plist_path`: with `HOME=/tmp/h`, `remember daemon install` writes `/tmp/h/Library/LaunchAgents/io.github.itayavtalyon.remember.plist`.
- [ ] `install_plist_label`: the plist `Label` is `io.github.itayavtalyon.remember`.
- [ ] `install_plist_program`: `ProgramArguments` is the resolved path of the running binary, followed by `daemon`.
- [ ] `install_plist_keepalive`: `KeepAlive` is the dictionary `{SuccessfulExit: false}`. `RunAtLoad` is true.
- [ ] `install_plist_atomic`: the write goes through a temp file and `rename`. No partial file is visible.
- [ ] `install_bootstraps`: after the write, the command runs `launchctl bootstrap gui/<uid> <plist>` through the injectable runner.
- [ ] `install_existing_plist`: an identical existing plist → `changed:false` and no rewrite. A differing one → rewritten, `changed:true`.
- [ ] `install_json`: with `--json`, the reply `type` is `daemon.install.result`, and `data` is `{changed, path}`.
- [ ] `install_linux`: on Linux, the command writes a systemd user unit `~/.config/systemd/user/io.github.itayavtalyon.remember.service` with the running binary's path and `daemon`, then starts it with `systemctl --user`. The same `changed` rules apply.
- [ ] `plugin_dir_searched_first`: when a plugin named `x` exists in the plugin dir argument and in the default plugin dir, the daemon loads the copy from the plugin dir argument.
- [ ] `plugin_dir_missing`: when the plugin dir argument names a dir that does not exist, the daemon skips it with a log line and keeps running.
- [ ] `plugin_dir_absent`: with no plugin dir argument, the search order is the plan 34 order, unchanged.


## Implementation Notes

## Review Notes
