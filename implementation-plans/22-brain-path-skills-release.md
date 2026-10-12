# 22 — Brain path `default-db`, Codex skill, 0.3.0 release prep (006 §8, §13, X-E01)

## Goal

Close 0.3.0 on the CLI side:

- The engine finds the brain through `~/.remember/default-db` (written by the
  apps) after `--db` and `REMEMBER_DB`.
- `remember-install-skill` also installs the skill for Codex.
- `SKILL.md` and help cover every 0.3.0 change, including the bucket-prefix
  line E12.10.
- The version becomes 0.3.0 and README What's New is complete, ready for Itay
  to tag and publish Homebrew per `docs/RELEASING.md`.

## Source of truth

[`design-logs/006-ui-overhaul-engine.md`](../design-logs/006-ui-overhaul-engine.md)
§0 (EN-01..EN-04, X-E01), §8 (E9.1, X-E04, E12.10), §13 (E15.1–E15.4),
Verification E15, plus the locked decisions below. Release mechanics:
[`docs/RELEASING.md`](../docs/RELEASING.md). Do not invent. If 006 and the
locked decisions do not decide a case, stop and ask Itay (grill), then record
the answer under Implementation Notes.

## Context (standalone summary)

Baseline: `main` with plans 15–21 merged.

- `util_resolve_db_path` (`src/util.c`) picks `--db` > `REMEMBER_DB` >
  `$HOME/.remember/remember.db`. Today an **empty** `REMEMBER_DB` is treated
  as unset. It rejects `:memory:` and `file:` URIs.
- `scripts/remember-install-skill` installs `SKILL.md` (symlink by default,
  `--copy` for snapshots) into `~/.grok/skills`, `~/.claude/skills`,
  `~/.cursor/skills` when the product folder exists, or into
  `REMEMBER_SKILL_DIRS`. `scripts/install.sh` uses it and documents the same
  list. `tests/test_install_skill.sh` (ctest `install_skill`) covers it.
- Version lives in `CMakeLists.txt` line 2 and the `REMEMBER_VERSION`
  fallback in `src/remember_app.c`; they must match the tag (Homebrew test).
- Each earlier 0.3.0 plan added its own What's New lines under a v0.3.0
  heading (EN-04).

Rules this plan implements (restated from 006):

| ID | Rule |
|----|------|
| E15.1 | Brain path, first that applies: (1) `--db PATH`; (2) `REMEMBER_DB` when present (**D24: empty counts as unset, as in 0.2.0**); (3) the absolute path in `~/.remember/default-db`; (4) `~/.remember/remember.db`. |
| E15.2 | `~` is the home directory the engine already uses for the built-in path. |
| E15.3 | `default-db` is a UTF-8 file with one line. The app writes a trailing newline. The engine trims ASCII whitespace and accepts a missing trailing newline. |
| E15.4 | Ignore `default-db` when the file is missing, the line is empty, or the path is not absolute; then use the built-in path. The engine never creates `default-db`. |
| E9.1 | `remember-install-skill` also installs `~/.codex/skills/remember/SKILL.md` when `~/.codex` exists. |
| X-E04 | Spike E9-S: the first E9 step checks the Codex documentation for the user skill folder. If it is not `~/.codex/skills/`, the step writes the real path into 006's Implementation Results and uses it. |
| E12.10 | `skills/remember/SKILL.md` gains: "Tags that start with a bucket prefix (`remember config get bucket-prefixes`) group memories into buckets, for example `project:fortkeeps`." |
| X-E01 | The release is 0.3.0. Homebrew publishes it before any app release that links schema v5. |
| EN-04 | Every change ships with tests and a line in README What's New. |

## Locked decisions (grill 2026-10-12; index in [`006-locked-decisions.md`](006-locked-decisions.md))

| # | Decision |
|---|----------|
| D24 | An empty `REMEMBER_DB` counts as unset (0.2.0 behavior). Resolution continues with `default-db`, then the built-in path. (Amends E15.1 step 2.) |
| D26 | E11 (Android presets) is not part of these plans. |
| D36 | `default-db` content that is not valid UTF-8 is ignored like a missing or empty line (E15.4): silent fallback to the built-in path. Only the first line is read; later lines are ignored. |

Also locked here, from existing behavior (no new choice):
- A `default-db` path still goes through the existing `:memory:` / `file:`
  refusal and length check.
- `~/.codex` handling mirrors the other products exactly (same symlink/copy
  modes, same "only if the product folder exists" rule).
- Tagging, the GitHub release and the Homebrew formula bump are **Itay's
  steps** (`docs/RELEASING.md`: "You only release when you decide to"). This
  plan prepares and verifies; it does not tag or publish.

Anything 006 and this table do not decide: stop and ask Itay. Do not choose.

## Prerequisites

- **Plans 15–21 merged** (the skill/help/What's New sweep covers them all).

## Scope

**In:** E15 resolution in `util_resolve_db_path` + tests; X-E04 spike + E9.1
in `remember-install-skill`, `scripts/install.sh` docs, `test_install_skill.sh`;
SKILL.md full 0.3.0 sweep incl. E12.10; general and per-command help sweep;
README What's New v0.3.0 consolidation (incl. E2.8 verbatim, the E2.6 JSON
order exception, the 006 amendments users can see, upgrade notes: v5
migrates silently, 0.2.x refuses v5, `devices` dropped, `device new` after a
restore); version bump 0.3.0 in both places; design-log index status;
`implementation-plans/INDEX.md`; full-suite + facade sweep; a release
checklist handed to Itay.

**Out:** tagging/publishing; Homebrew tap edits; app logs; Android (D26).

## Files

`src/util.c/.h` (E15), `src/remember_app.c` (help: path order; version
fallback), `CMakeLists.txt` (version), `scripts/remember-install-skill`,
`scripts/install.sh` (comments/usage list), `tests/test_install_skill.sh`,
`tests/test_schema_config.c` (path resolution tests; suite `schema_config`),
`tests/test_facade.c` (sweep), `skills/remember/SKILL.md`, `README.md`,
`docs/getting-started.md` (path order, export/import mention),
`docs/engineering-notes.md` (Path resolve bullet), `design-logs/index.md`
(006 status → implemented), `design-logs/006-ui-overhaul-engine.md`
(Implementation Results only), `implementation-plans/INDEX.md`.

## Must-pass tests

| Check | Proves |
|-------|--------|
| `--db` beats `REMEMBER_DB` beats `default-db` beats built-in | E15.1 |
| A set `REMEMBER_DB` hides `default-db` | Verification E15 |
| `REMEMBER_DB=""` → `default-db` is used (or the built-in path when absent) | D24 |
| Absolute `default-db` line (with and without trailing `\n`, with surrounding spaces/tabs/CRLF) is used | E15.3, Verification E15 |
| Missing file, empty line, whitespace-only line, relative path, invalid UTF-8 → built-in `~/.remember/remember.db`; a second line is ignored | E15.4, Verification E15, D36 |
| The engine never creates `~/.remember/default-db` (absent after any command) | E15.4 |
| `default-db` pointing at `:memory:` or a `file:` URI → existing refusal | existing policy |
| `remember-install-skill` with `~/.codex` present (fake `HOME`) → `~/.codex/skills/remember/SKILL.md` (symlink; `--copy` copies); absent → untouched | E9.1 |
| SKILL.md contains the E12.10 sentence verbatim | E12.10 |
| `remember --version` → `remember 0.3.0`; CMake and fallback agree | X-E01, RELEASING |
| Facade byte-match sweep passes for every command incl. `device`, `repair`, `config`, `export`, `import --from-*` | EN-03 |
| Full suite, every 006 verification row automated in earlier plans still green | DoD |

## Stages

TDD, one failing test at a time. After each stage: gate green, in-session deep
review, Implementation Notes, commit.

1. **E15 path resolution.** Tests first with a fake `HOME`. Then code in
   `util_resolve_db_path` (read `default-db` with ISO C `fopen`/`fgets`
   bounded by the path buffer; trim ASCII whitespace; absolute = starts with
   `/`). Help + engineering notes.
2. **X-E04 spike, then E9.1.** Read the current Codex documentation for the
   user skill folder. Record the finding (URL, date, path) in 006
   Implementation Results. If the path is not `~/.codex/skills/`, use the
   documented path and tell Itay before merging. Then extend
   `default_skill_dirs`, usage text, `install.sh`, and the shell test.
3. **Docs sweep.** SKILL.md for every 0.3.0 change (search union + `keys` /
   `tags` + `note:` warnings; `--json-errors` and codes; `busy`/`corrupt` →
   `repair search-index`; `--since/--until/--sort`; `--if-updated-at` +
   `stale`; `device` / `device new`; `config` bucket prefixes + E12.10;
   export/import formats + `conflicts` origin). Help sweep. getting-started.
4. **Release prep.** What's New v0.3.0 consolidated (New / Changed /
   Breaking-for-parsers: E2.6 key/tag position, blank search succeeds,
   `devices` table gone, empty `REMEMBER_DB` unchanged / `default-db` new;
   Upgrade notes). Version 0.3.0 in `CMakeLists.txt` + `remember_app.c`.
   `design-logs/index.md` status. `INDEX.md` rows ✅. Run the full gate and
   `scripts/ci-linux.sh`.
5. **Hand Itay the release checklist** (below) in the PR description.

**Release checklist for Itay** (from `docs/RELEASING.md`; not done by the
implementer): merge to `main` → `git tag -a v0.3.0` → `gh release create` →
compute tarball `sha256` → bump the tap formula → `brew install --build-from-source`
+ `brew test` → only then any app release that links schema v5 (X-E01).

## How to work (workflow + coding standards)

**Workflow**

1. Branch from an up-to-date, clean `main`. One branch and one PR for this plan.
2. If the brain is reachable, load `workflow:deep-review`,
   `pref:code-review-workflow`, `lesson:review-run-edge-cases` and
   `status:remember` (`workflow:project-status`). If it is not, the in-repo
   docs below are the reference.
3. TDD: a red test first, then the smallest change that turns it green, then
   refactor. One failing test at a time.
4. Do not invent. A case 006 and the locked decisions do not decide → stop,
   grill Itay, record the decision (plan Implementation Notes; brain
   `decision:<slug>` if reachable).
5. After each stage: full gate, in-session review against
   [`docs/STEP_REVIEW_CHECKLIST.md`](../docs/STEP_REVIEW_CHECKLIST.md),
   append Implementation Notes, commit.
6. On pause or block, update the `status:remember` card.

**Coding standards** ([`docs/engineering-notes.md`](../docs/engineering-notes.md),
[`docs/QUALITY.md`](../docs/QUALITY.md))

- Path policy stays in `util.c`; the store stays path-pure.
- Prefer ISO C file reads; bound every buffer; no `getline`.
- Shell scripts: `set -euo pipefail`, quote every expansion, keep the
  existing style of `remember-install-skill`.
- Never write `$HOME` from CMake or Homebrew (engineering notes, step 10).
- User-facing strings ASCII; README/SKILL.md follow the existing tone.
- Release rules in `docs/RELEASING.md` (version in two places; tag only after
  merge).

**Quality gate** (before every commit; on macOS prefix
`PATH="$(brew --prefix llvm)/bin:$PATH"`)

```bash
cmake -S . -B build -DCMAKE_C_COMPILER=clang -DREMEMBER_ENABLE_SANITIZERS=ON -DREMEMBER_TEST_VFS=ON
cmake --build build
ctest --test-dir build --output-on-failure        # includes install_skill
./scripts/lint-all.sh
./scripts/check-coverage.sh                       # 100% functions + 100% effective lines
./scripts/ci-linux.sh
./scripts/install.sh --force-build && remember --version   # Release tree, no ASan, prints 0.3.0
cmake --build build --target leaks-macos          # macOS only
```

## Definition of Done

- [ ] Design log 006 unchanged except Implementation Results (E15, E9.1,
      X-E04 finding, E12.10, X-E01 prep) and the index status.
- [ ] Every must-pass row green and in `tests/gate-suites` / ctest.
- [ ] D24 and D36 implemented exactly.
- [ ] X-E04 spike recorded with source and date.
- [ ] SKILL.md, help, getting-started and README What's New cover all of
      plans 15–22; E2.8 text verbatim; E12.10 sentence verbatim.
- [ ] Version 0.3.0 in both places; Release build prints `remember 0.3.0`.
- [ ] `implementation-plans/INDEX.md`: plans 15–22 ✅.
- [ ] Gate: ctest, lint, coverage 100/100, `ci-linux.sh` all green.
- [ ] Second review done; no open **blocking** item.
- [ ] Release checklist in the PR for Itay; nothing tagged or published.

## Second review handover

Hand this to an independent reviewer (a fresh session that did not write the
code). The reviewer runs `workflow:deep-review` if the brain is reachable,
else [`docs/STEP_REVIEW_CHECKLIST.md`](../docs/STEP_REVIEW_CHECKLIST.md),
and verifies by **running**, not reading.

**Package to hand over**

- Branch, PR link, commit range, this plan, 006 §8/§13 + X-E01, locked
  decisions.
- Gate output (ctest, lint, coverage, `ci-linux.sh`, Release `--version`).
- X-E04 finding (URL + quoted path).
- Rendered README What's New and SKILL.md diff.

**Probe these (expected result in brackets)**

1. `HOME=$(mktemp -d)`; write `/tmp/x.db` into `$HOME/.remember/default-db`
   without a newline; `remember --json list` [uses `/tmp/x.db`].
2. Same with `REMEMBER_DB=/tmp/y.db` [y]; with `REMEMBER_DB=` [x]; with
   `--db /tmp/z.db` [z].
3. `default-db` = `relative.db` [built-in path; nothing created besides
   the brain at the built-in path].
4. `mkdir $HOME/.codex && remember-install-skill` [symlink under the
   documented Codex path].
5. Read SKILL.md top to bottom against plans 15–21 [every new command, flag,
   code and output field is described correctly].
6. `./scripts/install.sh --force-build && remember --version` [0.3.0, no
   ASan, no test VFS shim symbols].
7. Facade sweep [green].

**Hot spots**

- Path precedence regressions (an empty `REMEMBER_DB` must not error).
- Reading `default-db` with CRLF or a BOM-less UTF-8 path with spaces.
- What's New accuracy: every breaking-for-parsers change named.
- No tag/publish performed by the implementer.

**Do not re-open:** path order; D24; D36; Codex install only when `~/.codex`
exists; 0.3.0 as the version; release order (Homebrew before apps).

**Review output:** append `### <date> — second-opinion deep review (stage N |
FINAL)` under Review Notes. Tag each finding **blocking / important / nit /
praise**. Auto-fix small, local, gate-green issues and say so; send product
or architecture forks to Itay as a grill. Re-run the full gate after any fix.

## Implementation Notes

_(empty; fill per stage)_

## Review Notes

_(empty; second review appends here)_
