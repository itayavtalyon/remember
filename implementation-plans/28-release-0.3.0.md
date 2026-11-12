# 28 — Release 0.3.0

**Area:** the version constant, `README.md`, `skills/remember/SKILL.md`, and `scripts/remember-install-skill`.
**Needs:** plans 22, 23, 25, 26, 27, 27a and 27b merged. No other plan. (The text "every lower-numbered plan … still open" is gone: Itay's rule of 2026-11-11 21:38.)
**Blocked on:** nothing.

## Goal

The tree says `0.3.0`, and the skill and What's New use the approved sentences. This plan does not tag and does not publish.

## Source

- `decision:main-version-label` (one version constant), implemented by this plan with no `-dev` step (`decision:2026-11-11-no-dev-label`, P9 A).
- `decision:2026-11-11-skill-settings-bullet` (P8 A).
- `decision:2026-11-11-plugin-host-0-4-0` (P1 A): 0.3.0 has no plugin host; What's New names none of it.
- `decision:release-0-3-0-scope`
- `decision:2026-11-07-bg-rem-06-releases` (0.3.0 stops at this release train; wiki-links stay out).
- `decision:2026-11-11-codex-skill-path`
- `decision:min-reader-version` and `decision:g-20` (`min_reader_version` is already in the brain).
- Sentences: [`COPY-PENDING.md`](COPY-PENDING.md) C-40 and C-41. Itay approved that file on 2026-10-25.
- [`docs/RELEASING.md`](../docs/RELEASING.md). Itay tags. This plan does not.
- Spec: `specs/RELEASE-0.3.0.md` (new; kept per P25 B, uppercase per P24 A). It lists what 0.3.0 contains, one line per plan 22, 23, 25, 26, 27, 27a and 27b, each with its spec file name.

## Out of scope

A git tag, a GitHub release, and the Homebrew formula. Wiki-links. Any feature from a plan that is not extracted.

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md).

The version text is `0.3.0` in both of these, and the two texts match:

- `project(remember VERSION ...)` in `CMakeLists.txt`
- the `REMEMBER_VERSION` fallback in `src/remember_app.c`

Do not add a second constant.

## What must be true

- `--version` prints `0.3.0`.
- The skill's bucket sentence is C-40 and no other bucket sentence:

> Tags that start with a bucket prefix group memories into buckets, for example `project:fortkeeps`. Bucket prefixes are the brain setting `remember.buckets.prefixes`, which is `project:` and `status:` by default; run `remember settings get remember.buckets.prefixes` to see the list in use.

- The skill does not contain `remember config get bucket-prefixes`, `--json-errors`, `--offset`, `--sync-id`, `--source`, or `purge-trash`.
- README What's New for 0.3.0 contains the C-41 bullets and no others from this plan:

- `--json` now puts success and failure in one `remember-message` version 1 envelope on stdout, and stderr stays empty. `--json-errors` is gone.
- Lists page with a cursor. Search doesn't page. `--offset` is gone.
- A memory's id in output is `id`, a lowercase hyphenated UUID. `--sync-id` is gone. The integer `local_id` appears only in CLI output.
- Actors replace `source`. `--source` is gone completely.
- `--trash` now means the Trash: memories you deleted. For expired memories, use `--expired`. For this release, `--trash` prints: `remember: warning: --trash now means the Trash (deleted memories). For expired memories, use --expired.`
- `purge-trash` is gone. To delete memories in the Trash for good, run `remember purge <locator>` for each one.

- The skill contains the C-41 skill bullets:

- Use `--json` to get one `remember-message` version 1 envelope on stdout, for success and failure alike. There is no `--json-errors`.
- To page a list, pass back its `next_cursor`. Search returns its top results and doesn't page. There is no `--offset`.
- Refer to a memory by `id`, a lowercase hyphenated UUID. The number `local_id` is for this device's CLI only. There is no `--sync-id`.
- Writes record actors, not `source`. There is no `--source`.
- `--trash` is the Trash (deleted memories). `--expired` is expired memories.
- To empty the Trash, run `remember purge <locator>` once for each memory in it. There is no `purge-trash`.
- For scripts and agents, use `--json`, because human dates are local.

- `scripts/remember-install-skill` adds `~/.codex/skills` when `~/.codex` is a directory. The installed file is `~/.codex/skills/remember/SKILL.md`.
- The script uses the symlink mode and the copy mode it already uses for `~/.grok`, `~/.claude`, and `~/.cursor`. A missing `~/.codex` adds no Codex path.
- When no product directory exists, the error is `error: no agent skill roots found (~/.grok, ~/.claude, ~/.cursor, ~/.codex);`
- The git history of this plan contains no tag.

- **Warnings carry a `fix` (E3 B, `decision:2026-11-12-e3-warnings-have-fix-field`).** Every warning this plan prints has a `message` and a non-empty `fix`: `flag_meaning_changed` (the README line of C-41 quotes the same plain line). The wording is copy item E3B-1, entry 1, in `decision:2026-11-12-copy-batch-e3b-warning-splits`: the approved sentence stays word for word and only its next step moves into `fix`; the plain line is `remember: warning: <message> <fix>`. `code`, `detail`, exit codes and `--strict` do not change. Itay approved E3B-1 as a whole on 2026-11-12, including every `fix` that is Story Weaver wording (`decision:2026-11-12-copy-batch-e3b-warning-splits`).

## Review points

- The reviewer checks that `CMakeLists.txt` and `src/remember_app.c` contain the same version text.
- The reviewer checks that a home with only `~/.codex` receives `~/.codex/skills/remember/SKILL.md` and no other product path.
- The reviewer checks `git tag --points-at HEAD` and finds no release tag from this plan.
- The reviewer checks that wiki-links are not in the What's New list.

## Definition of Done

- [ ] `--version` prints `0.3.0` from the CMake constant and from the fallback, in two builds.
- [ ] `the skill bucket sentence is the C-40 paragraph`.
- [ ] `the skill and README contain the C-41 bullets and do not contain --json-errors, --offset, --sync-id, --source, or purge-trash`.
- [ ] `a fake home with only ~/.codex installs ~/.codex/skills/remember/SKILL.md` (`tests/test_install_skill.sh`).
- [ ] `a fake home with no product directory prints the error line that names ~/.codex`.
- [ ] `git tag --points-at HEAD` shows no tag created by this plan.
- [ ] `the skill does not contain "see C-13"`.
- [ ] `specs/RELEASE-0.3.0.md` is written, and the decisions this plan built are `state:implemented`.
- [ ] The gate passes on the committed tree.

## Implementation Notes

## Review Notes
