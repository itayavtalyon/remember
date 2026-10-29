# 018 - v1 roadmap decisions

**Status:** Decisions recorded (2026-10-29). Not a feature design: this log records Itay's answers to the v1 roadmap sheet so the feature logs can cite one source. Open items are listed in Known Limitations › Open questions and are not decided here.
**Author:** Itay (answers) + Chief Architect (recording)
**Created:** 2026-10-29

Numbering: the roadmap names its feature logs R 011 (archive, amends, export), R 012 (API surface), R 013 (daemon, local MCP, bots), R 014 (sync), R 015 (hybrid search), R 016 (URL expansion) and R 017 (packaging). Those numbers stay reserved for them, so this log is 018.

Cross-refs: [001](001-foundations.md), [003](003-ttl.md), [005](005-sync-foundations.md), [006](006-ui-overhaul-engine.md), [007](007-settings.md), [008](008-json-envelope.md), [009](009-plugin-protocol.md), [010](010-plugins-and-hooks.md).
Other repos (not edited by this log): remember-mac logs 010-013, fortkeeps-cloud log 001 and its amendments file, fortkeeps-markdown log 001.
Source: `fk-review/QUESTIONS-2026-10-26-v1-roadmap-ANSWERED.md` (Itay's answers to the trimmed sheet of 2026-10-26; question IDs are those of the 2026-10-22 sheet).

## Background

The 2026-10-22 roadmap sheet had 54 questions. Four were settled on 2026-10-26 and dropped (V1-02, V1-05, V1-44, V1-54); 50 were answered by Itay on the 2026-10-26 sheet.
Context decided on 2026-10-26 and not asked again: staged shipping (the Mac app can ship before Cloud and iPhone); FK Scroll ships first; Itay tests the server and MCP first, then other people; billing is decided last; the first launch is free; archiving is a normal edit (no `memory.archived` event); opt-in encryption does not exist yet; Hebrew UI comes later, but v1 handles Hebrew text; Cloud is $4/mo or $40/yr, and a lapsed account is read-only for 90 days with export working.

## Problem

The feature logs R 011-R 017 and the app and Cloud logs need the roadmap answers before they can be written or re-extracted. The answers live only in a review sheet outside the repo. Design logs are the source of truth only once they are in the repo.

## Questions and Answers

How to read this section: "A", "B" and so on are Itay's letters, written out. "rec" means Itay took the recommendation. Free text is quoted exactly as Itay wrote it. Items marked **OPEN** are not decided; see Known Limitations.

### Settled 2026-10-26 (dropped from the sheet)

- **V1-02. Is the phone app in v1?** Yes. Phase one = Mac, iPhone, iPad and Android. Windows and Linux are phase 2.
- **V1-05. Is the Cloud web app in v1?** Yes. Phase one includes the web app with cloud sync.
- **V1-44. Bundle IDs?** `app.fortkeeps.*` everywhere. Scroll = `app.fortkeeps.scroll`. (The Scroll patch of 2026-10-26 logged this as "V1-43"; the correct ID is V1-44.)
- **V1-54. Accessibility bar for v1?** VoiceOver, Dynamic Type, keyboard-only and WCAG AA from day one, on every app and site.

### Round 1 (2026-10-29: Itay, the v1 roadmap sheet of 2026-10-26)

#### 1. Platform, stack and accounts

- **V1-42 (reopened). Where the phone, Android and web code lives, and its stack?**
  A: **A.** The Apple apps (Mac, iPhone, iPad) stay in remember-mac as one multiplatform SwiftUI app. Android is a separate native repo (Kotlin + Jetpack Compose). The web app lives in fortkeeps-cloud (HTML and vanilla JS). All three share the core through the Remember engine (`libremember`, C ABI) and the open sync protocol, not through shared UI code. Rationale: native quality and accessibility on each OS; the real shared core is already the C engine and the protocol.
- **V1-43. When to buy the Apple Developer membership?**
  A: **B** (buy it later, not now), with Itay's words: "We will buy it when most of the development is done and all is left depends on getting the memebership". Consequence: signing, notarization, TestFlight, the App Store, App Groups and Keychain sharing wait until then. FK Scroll signing (fortkeeps-markdown plan 02, stages 2c and 2d) is deferred until the membership exists; unsigned builds are used for tests.
- **V1-01. What "v1" means as a version number?**
  A: **A.** 0.3.0 = schema v6 + plugins + archive/export. 0.4.0 = daemon, MCP, sync client. 1.0.0 = the whole v1 target; from 1.0.0 the CLI and JSON are stable. Rationale: 0.3.0 unblocks the Mac app early, and 1.0.0 marks the stability promise.
- **V1-45. Mac v1 feature list beyond Mac 011?**
  A: **A** (Mac v1 = Mac 011 + Mac 014-017: alignment, agents and plugins, account and sync, archive and export; the sync UI can be hidden until Cloud ships), with Itay's caveat: "but I am not sure the accounts and sync will be included in v1. I want to release that when the offering of Fortkeeps cloud is ready." **OPEN** for Mac 016 (account and sync): see OQ-1. **Closed in Round 2:** Mac v1 is local-only.

#### 2. Schema v6

- **V1-34. `archived_at` and the `amends` link kind in v6?**
  A: **A.** Both go into schema v6, in one migration. The archive log R 011 must close before plan 19 is re-extracted. Rationale: a second migration costs more than waiting for one log.
- **V1-35. Where the sync `peers` table lands?**
  A: **A.** A v7 step in the sync log (R 014), shipped with the sync client. v6 keeps "no peer table".

#### 3. Archive, amends and export (R 011)

- **V1-06. CLI verbs for archive?**
  A: **A**, with Itay's condition: "provided Interface approves - we want our commands to be consistent and unified and not feel like a patchwork." The proposal is `remember archive <locator>` and `remember unarchive <locator>`, safe to repeat. **Pending Interface approval** (OQ-2).
- **V1-07. MCP and HTTP shape for archive?**
  A: **B.** Separate tools `remember_archive` / `remember_unarchive`, and the route `POST /v1/memories/{id}/archive` (and its unarchive counterpart). Itay's reason: "because `archived` is not an actual a memory field we expose, is it?" The reason is phrased as a question; see OQ-3.
- **V1-08. Archive edge rules?**
  A: **A.** All five rules as written: (1) setting an expiry on an archived memory fails with "unarchive it first"; (2) deleting an archived memory keeps `archived_at`, and restore puts it back in Archived; (3) `archived_at` counts in the identical-content rule; (4) purge never targets archived memories; (5) the export header lists the bins and counts.
- **V1-09. Export flags?**
  A: **A.** Exactly `--include-expired`, `--include-trash` and `--exclude-archived`. No `--bins`.
- **V1-10. Copy for archive and export?**
  A: **B.** "Archived memories stay in your brain and are included in exports by default."
- **V1-11. Migrate old `cites` + "[Amended …]" rows to `amends`?**
  A: **A**, with Itay's scope: "but it's something personal to be done for my Brain file, not part of the software we ship." So one guarded script converts Itay's own brain rows after v6 ships. It is not a shipped migration and not part of the engine.
- **V1-12. Mac keyboard shortcut for Archive?**
  A: **B.** Archive = ⌘E; Export = ⇧⌘E. (The sheet notes that ⌘E is the macOS "Use Selection for Find" shortcut; the Mac log must say how that system item is handled. See follow-up work.) **Replaced in Round 2:** Archive = ⌃⌘A.

#### 4. API surface and library (R 012)

- **V1-13. The MCP tool catalog for v1?**
  A: **B.** The 6 core tools + `remember_settings` + `remember_link` / `remember_unlink` + `remember_tags` + `remember_conflicts` (read) + `remember_related`. Itay: "we want the humans to be able to have the tools to solve conflicts and do everything. We also agreed that that there is one API surface for everything so we need to allow everything".
- **V1-14. One route set for the daemon and Cloud?**
  A: Not decided. Itay asked: "Is there a value in serving the regular HTTP API endpoints and not just MCP? The cloud will offer an MCP service too. Tradeoff and product reasons please". **OPEN** (OQ-4).
- **V1-15. Library ABI promise?**
  A: **A.** From 1.0, the C ABI of `remember_open` / `remember_call` / `remember_close` and the message types are stable. `remember_run` is internal and not in the public header.
- **V1-16. Publish an OpenAPI file?**
  A: **A.** Generate `openapi.json` and the MCP tool schemas from the engine table, and publish them on GitHub Pages (V1-48). Rationale: a generated file cannot drift from the code.

#### 5. Sync and Cloud

- **V1-36. Fold the C001a amendments into Cloud log 001 now?**
  A: **A.** One dated round in fortkeeps-cloud log 001, then re-extract Cloud plan 001, before C 002 starts.
- **V1-37. Sync cursor and pull format?**
  A: **A.** The cursor is the server's `change_seq` as an opaque base64url string. Pull returns rows after it in pages of up to 100 rows / 4 MiB, the same limits as push.
- **V1-39. Sign-in in the apps?**
  A: **B.** The apps use an OAuth web sheet (`ASWebAuthenticationSession` on Apple, Custom Tabs on Android). The CLI and bots use the device-code flow. One server account supports both.
- **V1-40. Cloud remote MCP auth for bots?**
  A: **A.** Both: OAuth 2.1 for MCP connectors, and scoped `rk_cloud_…` tokens (the same scopes as local bot tokens) for scripts.
- **V1-38. Device list and revoke?**
  A: **A**, plus Itay's addition: "we need to keep the name of each device, not just the ID". Settings → Sync in every app lists devices (name, last seen) with Revoke; the web app shows the same list. A revoked device must sign in again and do a full re-sync. The device name is stored with the device record, not only the device ID.
- **V1-41. Sync conflicts on the phone?**
  A: **A.** iPhone and Android show the same conflict inbox and actions (keep mine, keep theirs, keep both).
- **V1-03. Self-hosted reference sync server in v1?**
  A: **A.** Yes, in R 014, built before Cloud sync, so the protocol is tested against an open server first.
- **V1-04. Cloud enhanced (server-side hybrid) search in Cloud v1?**
  A: **B**, conditional. Itay: "B, assuming the schedule is to release the cloud and sync _before_ the built in hynrids. Otherwise we will start with C, and then keep enhancing it." So: if Cloud and sync ship before the built-in hybrid plugin, Cloud v1 launches with keyword search only and says so, and enhanced search follows at no extra price. If the hybrid plugin is ready first, Cloud runs the same open `hybrid` plugin on the server and improves it later. The branch depends on the release order (OQ-5).
- **V1-33. Does Cloud expand URLs that bots add through the Cloud API?**
  A: Itay rejected the premise: "The premise is wrong, I think. An MCP write is not considered a sync write! Therefore, it should write anything that should be written on local write." Recorded rule: a write through the Cloud API or Cloud MCP is a write on the Cloud copy, not a sync write. It runs the same write-time behaviour as a local write, including `url_expand` when that plugin is on. The cost and where the plugin runs are for the Cloud log (OQ-6).

#### 6. Daemon, local MCP and bots (R 013)

- **V1-17. The hourly `remember-sweep` LaunchAgent?**
  A: **A.** Drop it. The app installs one LaunchAgent, `app.fortkeeps.daemon`, and the daemon sweeps. The CLI still sweeps after each command.
- **V1-18. Who starts the daemon for CLI-only users?**
  A: **B.** Docs only: the user writes the LaunchAgent or systemd unit. `remember daemon` runs in the foreground. The engine installs no background job; Fortkeeps does that for app users.
- **V1-19. Default port for the local HTTP endpoint?**
  A: **A.** 7337, with a fallback to a free port written to `remember-http.json`.
- **V1-20. Bot rights for archive and links?**
  A: **A.** Archive, unarchive, link and unlink need `write`. Delete and restore need `delete`.
- **V1-21. Where the "local is not a security boundary" warning appears?**
  A: **A.** In the `remember bot add` output, the MCP docs page and the Fortkeeps permissions screen.
- **V1-22. Activity log: log query text?**
  A: **B.** No setting in v1. Query text is never logged.
- **V1-23. Clients that `remember bot connect` supports in v1?**
  A: **A.** Claude Desktop, Claude Code, Cursor and Codex CLI. Other clients get a copy-paste block.

#### 7. URL expansion (R 016)

- **V1-31. HTML parser?**
  A: **A.** lexbor (Apache-2.0). FK Scroll's paste converter uses it if it is ready in time (see follow-up work: the Scroll log currently says hand-written). **Replaced in Round 2:** the hand-written parser stays primary; lexbor is the backup.
- **V1-28. One name for the URL plugin?**
  A: **A.** Plugin `url_expand` (display name "URL expansion"); the C library is `remember-urlfetch`.
- **V1-29. Where the plugin lives and how CLI users get it?**
  A: **B.** A separate public repo `fortkeeps-plugins` (MIT) with its own releases. Itay: "to strengthen the brand. Also, since they are optional". How Homebrew users and the apps get it is not stated (OQ-7).
- **V1-30. On or off by default?**
  A: **A.** Off until the user turns it on. The app asks once, at the first saved URL.
- **V1-32. Settings keys and the new memory layout?**
  A: **A.** `plugin.url_expand.max_bytes` (512 KiB) and `plugin.url_expand.timeout` (10 s). The new memory has line 1 the title, then the description, then the canonical URL, and the tag `source:url`.

#### 8. Hybrid search (R 015)

- **V1-24. How the hybrid plugin gets its model?**
  A: **A** as the working rule (`small` bundled in Homebrew and the Mac, iPhone and Android apps; `base` an optional download), with Itay's research brief: "but we need to do the research and see what models are available for each platform and future platform and how we could utilize them. I believe that we have decided that the embedding and vectors are not synchronized, and are in fact in an external database as far as Remember is concerned, so we can actually use different models for different platforms if we need to; consider using Apple built in capabilities." The R 015 spike researches per-platform models, including Apple's built-in capabilities, and may choose a different model per platform (OQ-8).
- **V1-25. Embedding runtime and model license?**
  A: **rec = C, with A as the first candidate.** The R 015 spike picks the runtime; Itay approves only the license. The first candidate is a GGUF embedding model (MiniLM / bge-small class, Apache-2.0 or MIT) on vendored llama.cpp.
- **V1-26. How keyword and vector results combine?**
  A: **A.** Reciprocal-rank fusion with k = 60.
- **V1-27. Hybrid search on phones in v1?**
  A: **A.** Yes, the same as desktop, gated on the phone spike passing (limits set in R 015) on iPhone and Android.

#### 9. Packaging and release (R 017, Mac 018)

- **V1-46. How the Fortkeeps Mac app is distributed?**
  A: Not decided. Itay: "Haven't we solved the sandboxed problem yet? I need tradeoffs, though I lean to the App Store (also for the brand)". **OPEN** (OQ-9). **Closed in Round 2:** direct download, not sandboxed.
- **V1-47. CLI channels and Linux?**
  A: **B.** Homebrew (macOS and Linux), a static Linux tarball on each GitHub release, and `.deb` / `.rpm` packages. Itay: "If we are going to support Linux, let's not do it half measures."
- **V1-48. What GitHub Pages hosts?**
  A: **A**, plus a mirror. All public schemas under `/schema/<family>/v<n>/`, never changed once published, built by a GitHub Action on each tag. Itay adds: "Let's also consider having a mirror on Fortkeeps.com - since we have this domain as well. Buisness Manager our product manager should come up with a distinct use for each domain," The mirror and the domain split are for Business Manager (OQ-10).

#### 10. Onboarding and principles

- **V1-53. First run on the Mac (and phones)?**
  A: **A.** One short welcome: create or open the brain; nothing else until use. Hybrid asks at the first search, URL expansion at the first saved URL, bots from a "Connect an agent" row, Cloud only in Settings and on a second device.
- **V1-52. Update the PRINCIPLES feature matrix for the daemon?**
  A: **A.** The row becomes "Local MCP (stdio + localhost HTTP), per-bot tokens, activity log | Yes | Connect, permissions and activity UI | Remote HTTPS MCP".

#### 11. Docs and brain hygiene

- **V1-51. What `delete --trash <locator>` does now?**
  A: **A.** It fails with "It's already in the Trash. To delete it forever, run remember purge <locator>." `purge` stays the only wipe.
- **V1-50. Old logs that still describe `purge-trash` and `--trash` = expired?**
  A: **A.** New logs list the old lines in a "Supersedes" table, and `design-logs/index.md` marks 003 and 005 "amended by 011" when R 011 lands. Closed logs stay as written.
- **V1-49. Where the contradiction lint runs?**
  A: **A.** `remember/scripts/lint-decisions.py`, run by the design-log-grill workflow before a log closes.

### Round 2 (2026-10-29 evening: Itay, closing OQ-1 and OQ-9, and two changes)

These answers replace the Round 1 answers to the same IDs. Items marked **PROPOSED** are recommendations, not decisions.

- **V1-12. Mac keyboard shortcut for Archive?** (replaces Round 1 B)
  A: Archive is ⌃⌘A, not ⌘E (⌘E clashes with macOS's Use Selection for Find, and ⌘E stays with that system item). The menu item stays `Archive`, and help text reads `Archive (⌃⌘A)`. Any tooltip reads the same.
- **V1-46. How the Fortkeeps Mac app is distributed?** (closes OQ-9)
  A: Decided: the Fortkeeps Mac app is distributed outside the Mac App Store (direct download: a DMG from fortkeeps.com) and is NOT sandboxed. This keeps the LaunchAgent background service (V1-17) and `bot connect` editing other apps' settings (V1-23) possible. iPhone still goes through the App Store. The release (update) feed is hosted by us on fortkeeps.app, e.g. fortkeeps.app/appcast.xml.
  - **PROPOSED** (Interface's recommendation, awaiting Itay's OK): updates use Sparkle (MIT, open source) with a signed appcast feed. The update UI is a quiet window with version, release notes and `Install Update`, `Remind Me Later` and `Skip This Version`; nothing installs without consent, no nagging; `Check for Updates…` in the app menu; Settings › General has `Automatically check for updates` (on by default) and `Automatically download and install` (off by default); full keyboard and VoiceOver support.
  - Signing: public Mac downloads need Developer ID signing and notarization, which need the $99 Apple Developer membership (V1-43). Until then the app is dogfood-only.
  - Business Developer: Mac v1 is free with no account, so no billing or Merchant of Record is needed until Mac v2 and Cloud. The MoR is Paddle or Polar, only once paid Cloud arrives with sync. Exact fees and the Israeli sole-trader registration steps are still open.
  - Art (Leonardo): the art list changes to a download-page hero, a DMG install-window background and an update-available image (iPhone keeps App Store screenshots). The Mac art is built from Interface's brand set: the tower mark and the colors Keep blue, Stone and Ink.
  - Copy (Story Weaver): Story Weaver will write the update prompt, release notes, download page and DMG copy. The update prompt and download-page copy wait for the updates design.
  - The exact update and new-version-discovery design needs its own future log.
- **V1-31. HTML parser?** (replaces Round 1 A)
  A: Scroll's hand-written paste-HTML parser stays primary. lexbor is the backup, and we take lexbor's tests and make our parser pass them too.
- **V1-45. Mac v1 feature list beyond Mac 011?** (closes OQ-1)
  A: Mac v1 is local-only, with no sign-in, accounts or sync. Sync is revealed only when the offering is ready. The sequence is Mac v1, then sync and cloud, then Mac v2. Interface notes that v1 has no sign-in, account or Cloud screens. So Mac 016 (account and sync) is not in Mac v1.
- **V1-14** (MCP only, or REST and MCP) stays **OPEN** (OQ-4).
- Still open: V1-04, V1-06, V1-07, V1-14, V1-24, V1-29, V1-33 and V1-48 (OQ-2 to OQ-8 and OQ-10).

## Design

This log decides no new engine behaviour of its own. Each answer above is the input to the feature log named in its section:

| Log | Takes these answers |
|---|---|
| R 011 archive, amends, export | V1-06, V1-07, V1-08, V1-09, V1-10, V1-34, V1-50, V1-51 |
| R 012 API surface | V1-13, V1-14 (open), V1-15, V1-16, V1-20 |
| R 013 daemon, local MCP, bots | V1-17, V1-18, V1-19, V1-21, V1-22, V1-23, V1-52 |
| R 014 sync | V1-03, V1-35, V1-37, V1-38, V1-41 |
| R 015 hybrid search | V1-24, V1-25, V1-26, V1-27 |
| R 016 URL expansion | V1-28, V1-29, V1-30, V1-31, V1-32, V1-33 |
| R 017 packaging | V1-01, V1-43, V1-46 (Round 2; update design PROPOSED, own future log), V1-47, V1-48 |
| Not engine | V1-11 (Itay's own brain), V1-49 (script, not yet written) |

When a feature log is written, it cites the question ID and this log. Where a feature log needs to change an answer, it amends this log with a dated round.

## Trade-offs

- **Chosen:** one decision log for the roadmap, separate from the feature logs.
  - Pros: one place to cite; the feature logs can be written in any order.
  - Cons: an answer and its feature design live in two files until the feature log exists.

## Verification Criteria

1. Every one of the 50 answered question IDs and the 4 dropped IDs appears once in Questions and Answers.
2. Every letter answer is written out; every free-text answer is quoted.
3. Every answer that is blank, conditional or phrased as a question is marked OPEN and listed in Open questions.

## Known Limitations

### Open questions

| ID | Question | Why it is open |
|---|---|---|
| OQ-1 | V1-45: is Mac 016 (account and sync) in the first Mac release? | **Closed 2026-10-29 (Round 2):** no; Mac v1 is local-only. |
| OQ-2 | V1-06: `archive` / `unarchive` verbs. | A is conditional on Interface approving a unified command set. |
| OQ-3 | V1-07: does the message format expose `archived` / `archived_at` on a memory? | The reason for B is a question. V1-34 puts `archived_at` in schema v6; whether log 008 exposes it is not stated. B stands either way unless Itay changes it. |
| OQ-4 | V1-14: does the daemon serve REST routes as well as `/mcp`? | Itay asked for trade-offs and product reasons. |
| OQ-5 | V1-04: B or C for Cloud search. | Depends on whether Cloud and sync ship before the hybrid plugin. |
| OQ-6 | V1-33: where `url_expand` runs for Cloud API/MCP writes, and its cost. | The rule is decided (an MCP write is not a sync write); the execution and cost are not. Also interacts with V1-29 (plugin in a separate repo). |
| OQ-7 | V1-29: how Homebrew users and the apps get plugins from `fortkeeps-plugins`. | B replaced A, which had said Homebrew installs it and the apps bundle it. |
| OQ-8 | V1-24: per-platform embedding models, Apple built-in capabilities. | A is provisional pending research. |
| OQ-9 | V1-46: Mac distribution. | **Closed 2026-10-29 (Round 2):** direct DMG download, not sandboxed; iPhone on the App Store. Sparkle and the update UI are PROPOSED. |
| OQ-10 | V1-48: the fortkeeps.com mirror and the use of each domain. | Business Manager is to propose. |

## Future Work

- R 011-R 017 feature logs, citing this log.
- A log for the Mac update and new-version-discovery design (V1-46; Sparkle and the update UI are PROPOSED until then).
- Business Developer: MoR fees (Paddle or Polar) and the Israeli sole-trader registration steps, before paid Cloud.
- `scripts/lint-decisions.py` (V1-49).
- Itay's personal `cites` → `amends` script (V1-11), outside the shipped software.
