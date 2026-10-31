# 020 — v1 roadmap corrections: domains, Mac updates, sweep, daemon labels

**Status:** Decisions recorded (2026-10-31). Corrects log 018 and log 010 H-18.1. Logs 018, 019 and 010 are not edited.
**Author:** Itay (answers) + Chief Architect (recording)
**Created:** 2026-10-31

Cross-refs: [010](010-plugins-and-hooks.md), [018](018-v1-roadmap-decisions.md), [019](019-v1-roadmap-round3-decisions.md); remember-mac log 020 (Mac app updates); fortkeeps-markdown log 003 (Scroll distribution).

## Background

Log 018 Round 2 (V1-46) put the update feed on fortkeeps.app and left Sparkle and the update UI as PROPOSED. Later decisions settled both: remember-mac log 020 (2026-10-29, B-3 and option B) and fortkeeps-markdown log 003. Log 010 H-18.1 still describes the hourly sweep LaunchAgent that 018 V1-17 dropped. Log 010 names two launchd labels for the daemon without saying how they live together.

## Problem

A reader of the remember logs alone gets the wrong feed host, a Sparkle decision that looks open, a LaunchAgent that will not ship, and no rule that stops two daemons from running on one brain.

## Questions and Answers

- **Q1. Where is the Sparkle appcast?**
  A: On fortkeeps.com, not fortkeeps.app. This supersedes the feed sentence of 018 V1-46 (`fortkeeps.app/appcast.xml`).
- **Q2. What does each domain serve?**
  A: fortkeeps.com serves the site, the downloads, the docs, the Sparkle appcast and the schema mirror (019 V1-48). fortkeeps.app serves the web app. api.fortkeeps.app serves Cloud sync and the Cloud MCP. The canonical schema `$id` stays on GitHub Pages, `https://itayavtalyon.github.io/remember/schema/plugin/v1/` (009, 019 V1-48).
- **Q3. Is Sparkle decided?**
  A: Yes. The Fortkeeps Mac app updates with Sparkle and an EdDSA-signed appcast. Only the Fortkeeps Mac app uses Sparkle. It ships as a DMG outside the Mac App Store and is not sandboxed. Fortkeeps Scroll ships on the Mac App Store only, is sandboxed and has no update UI. The update design is remember-mac log 020; Scroll distribution is fortkeeps-markdown log 003. This supersedes the PROPOSED bullet under 018 V1-46, the "Sparkle and the update UI are PROPOSED" note in 018 OQ-9, and the Future Work item for an update log in 018.
- **Q4. Does the hourly sweep LaunchAgent ship?**
  A: No. 018 V1-17 dropped it: the app installs one LaunchAgent, `app.fortkeeps.daemon`, the daemon sweeps, and the CLI sweeps after each command. Log 010 H-18.1 (`app.fortkeeps.remember-sweep`, `StartInterval` 3600) is superseded. The Linux systemd sweep units in H-18.1 are superseded the same way; Linux docs show the daemon unit only.
- **Q5. Which launchd label does the daemon use?**
  A: Two labels stay. The Fortkeeps app installs `app.fortkeeps.daemon` (010 H-Q20). The open-source docs and any Homebrew service use `io.github.itayavtalyon.remember` (010 #581, #582). A single-daemon lock makes sure only one daemon runs per user and brain, whichever label started it.
- **Q6. How does the lock work?**
  A: Deferred to R 013 (daemon). R 013 decides the lock file, the runtime directory, the handshake, which daemon wins, and what the losing daemon does. Proposal for R 013, not decided: the 2026-10-30 daemon conflict map §3 — a per-user runtime directory, `daemon.lock` held with `flock(LOCK_EX|LOCK_NB)` for the daemon's lifetime (the kernel releases it on crash), `daemon.json` with the holder's pid and engine version, a handshake on `daemon.sock`, and a loser that exits 0 with `daemon_already_running` when the holder is the same or newer.

## Design

- D-1. fortkeeps.com: site, downloads, docs, Sparkle appcast, schema mirror. fortkeeps.app: web app. api.fortkeeps.app: Cloud sync and MCP.
- D-2. Sparkle with an EdDSA appcast on fortkeeps.com, in the Fortkeeps Mac app only (non-sandboxed DMG). Scroll: Mac App Store only, sandboxed, no update UI.
- D-3. No sweep LaunchAgent. 010 H-18.1 is superseded by 018 V1-17.
- D-4. Two daemon labels, `app.fortkeeps.daemon` and `io.github.itayavtalyon.remember`, and one daemon per user and brain, enforced by a lock. The lock mechanism is deferred to R 013.

## Trade-offs

- Two labels keep the open-source daemon free of the Fortkeeps name, at the cost of needing the lock; one label would have stopped duplicates through launchd alone.

## Known Limitations

- Until R 013 lands, nothing stops a user from starting two daemons. Deferred to R 013.

## Implementation plan

- remember plan 25: drop the sweep LaunchAgent from the app contract.
- `implementation-plans/DESIGN-PENDING.md`: drop the hourly LaunchAgent from the engine contract list.
- remember-mac plan 22 already uses fortkeeps.com for the appcast.
