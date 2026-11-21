# Release 0.3.0

What `remember` 0.3.0 contains. Release notes for users are the What's New section of [README.md](../README.md). The skill text is [`skills/remember/SKILL.md`](../skills/remember/SKILL.md). This file lists the plans that ship in 0.3.0 and the spec each one wrote or changed. The version is `0.3.0` in `CMakeLists.txt` and in the `REMEMBER_VERSION` fallback in `src/commands_common.h`, and `remember --version` prints `remember 0.3.0` (`decision:main-version-label`, `decision:2026-11-11-no-dev-label`).

## What 0.3.0 contains

- Plan 22, export: `remember export` writes the brain to a JSON, Markdown, or CSV file. Spec: [JSON-ENVELOPE.md](JSON-ENVELOPE.md) (the `export` reply), [JSON-STYLE.md](JSON-STYLE.md) (the stored export document).
- Plan 23, import formats: JSON import and `--from-db` for brains of schema 4 to 6. Spec: [JSON-ENVELOPE.md](JSON-ENVELOPE.md) (the `import` reply).
- Plan 25, plugin manifest: `plugin.json` parses to the protocol schema. Spec: [PLUGIN-MANIFEST.md](PLUGIN-MANIFEST.md).
- Plan 26, brain list: `brains.json`, `remember brain list`, `remember brain prune`. Spec: [BRAIN-LIST.md](BRAIN-LIST.md).
- Plan 27, default brain path: the `~/.remember/default-db` file. Spec: [DEFAULT-DB.md](DEFAULT-DB.md).
- Plan 27a, outbox delivery: the recipient state machine, backoff retry, and a pre-flight failure that is `failed` at once. Spec: [OUTBOX-DELIVERY.md](OUTBOX-DELIVERY.md).
- Plan 27b, older CLI read-only: a CLI that finds a newer brain opens it read-only, prints the read warning, and exits 75 on a write. Spec: [OLDER-CLI.md](OLDER-CLI.md).
- Plan 32, expiry sweep: the sweep after every command and `remember brain sweep [--all]`, `remember purge <locator>` (the one permanent wipe, for one expired or deleted memory), `delete --expired` (every expired memory to the Trash), and the one wrong-bin error code `wrong_bin`. Spec: [EVENTS.md](EVENTS.md) (Expiry), [JSON-ENVELOPE.md](JSON-ENVELOPE.md).
- R27b-2 and R27b-3 (pull request #75): the `min_reader_version` line of that warning and error, and the exit codes 5 and 75 in `--help`. Spec: [OLDER-CLI.md](OLDER-CLI.md).

Every plan merged after the `v0.2.0` tag ships in 0.3.0 too: plans 15 to 18 (with 17a, and 18b and 18c, which landed with 18), 19 to 21, and 24. The spec table is [README.md](README.md).

## What 0.3.0 does not contain

- The plugin host (trust, commands, the dispatcher, processes, search filters, `plugin test`) ships as 0.4.0 (`decision:2026-11-11-plugin-host-0-4-0`). 0.3.0 has no plugin host.
- Wiki-links ship in 0.4.0 (`decision:2026-11-07-bg-rem-06-releases`).

## Install

The installer writes the skill into `~/.grok`, `~/.claude`, `~/.cursor`, and `~/.codex` when each directory exists. The Codex file is `~/.codex/skills/remember/SKILL.md` (`decision:2026-11-11-codex-skill-path`). A missing `~/.codex` adds no Codex path. With none of the four, `remember-install-skill` exits 1 with `error: no agent skill roots found (~/.grok, ~/.claude, ~/.cursor, ~/.codex);`.

## Tag and publish

A person tags and publishes ([`docs/RELEASING.md`](../docs/RELEASING.md)). The `v0.3.0` tag waits until plan 32 (`remember purge <locator>`) merges, because the C-41 lines name that command (`decision:2026-11-16-plan28-review-grill`). No plan creates a tag, a GitHub release, or the Homebrew formula (`decision:release-0-3-0-scope`).

## Decisions

`decision:main-version-label`, `decision:2026-11-11-no-dev-label`, `decision:2026-11-11-keep-release-spec`, `decision:2026-11-11-skill-settings-bullet`, `decision:2026-11-11-plugin-host-0-4-0`, `decision:release-0-3-0-scope`, `decision:2026-11-07-bg-rem-06-releases`, `decision:2026-11-11-codex-skill-path`, `decision:2026-11-12-e3-warnings-have-fix-field`, `decision:2026-11-12-copy-batch-e3b-warning-splits`.
