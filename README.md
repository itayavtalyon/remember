# remember

**Local-first personal second brain, on the command line.** Capture notes,
facts, and decisions as durable *memories* — each with an optional named key,
tags, an optional expiry, and typed links to related memories — then find them
again with full-text search. One SQLite file: no server, no account, no network.
Every command also speaks JSON, so agents and scripts share the surface you use.

- **Store** — `remember add "…"` with optional `--key`, `--tags`, `--expires`.
- **Find** — `remember search "…"` (SQLite FTS5 over the body) or `remember list`.
- **Organize** — tags as facets; **keys** as stable named slots (upsert by key).
- **Connect** — link memories as `related`, `cites`, or `supersedes`.
- **Expire** — optional TTL with a trash bin (restore or purge); nothing vanishes silently.
- **Automate** — `--json` on every command; ships an agent skill for Claude / Cursor / others.

Design: [`design-logs/001-foundations.md`](design-logs/001-foundations.md) ·
Plans: [`implementation-plans/INDEX.md`](implementation-plans/INDEX.md).

## Install

### Homebrew (recommended)

```bash
brew install itayavtalyon/remember/remember
```

Builds the release binary from source (needs `cmake`; no other dependencies).
Upgrade or uninstall the usual way (`brew upgrade remember` /
`brew uninstall remember`). The formula installs the binary only — for the agent
skill too, use the [from-source install](#build-from-source) below.

## What's New

### v0.1.0 — first tagged release

The initial public release bundles the full command surface built so far:

- **Related memories** — connect entries with `related`, `cites`, or
  `supersedes`; list a memory's neighbours and `rekey` in place.
- **TTL & trash** — optional `--expires`; expired memories move to a trash bin
  you can restore from or purge. No silent data loss.
- **Full-text search** — SQLite **FTS5** over memory bodies (`remember search`).
- **Keys & tags** — named-slot **keys** (upsert by key) and multi-tag faceting;
  internal spaces allowed in both.
- **JSON everywhere** — every command emits a stable JSON envelope, so agents
  and scripts drive the same surface as the CLI.
- **Agent skill** — installs into Claude / Cursor / Grok skill trees.

## Build from source

Prefer Homebrew (above) for a plain install. Build from source for the agent
skill, for development, or on a platform without the tap.

### Requirements

- **CMake** ≥ 3.20
- A **C11** compiler — clang is the primary target; gcc also works
- **git** (to clone)
- **No external libraries** — SQLite is vendored in `third_party/sqlite/`, so there is nothing else to install to build the binary
- Platforms: **macOS** and **Linux**

Pinned SQLite amalgamation: **3.53.3** in `third_party/sqlite/` (see that README).

### Binary + agent skill (install script)

Builds a **Release** binary (no sanitizers) into a dedicated `build-release/` tree
(never reuses developer `build/`, which is often ASan), installs it, and copies
the agent skill into product skill roots that already exist:

```bash
./scripts/install.sh                 # Release -> ~/.local/bin/remember + skills
./scripts/install.sh --prefix /usr/local
./scripts/install.sh --skill-only    # refresh agent skill only
./scripts/install.sh --bin-only      # binary only
./scripts/install.sh --force-build   # clean rebuild of build-release/
```

| What | Where |
|------|--------|
| Binary | `$PREFIX/bin/remember` (default `~/.local/bin`) |
| Release build tree | `build-release/` (override with `REMEMBER_RELEASE_DIR`) |
| Skill (source) | [`skills/remember/SKILL.md`](skills/remember/SKILL.md) |
| Skill (installed) | `remember/SKILL.md` under whichever of `~/.grok/skills`, `~/.claude/skills`, `~/.cursor/skills` already exist |

Override skill destinations: `REMEMBER_SKILL_DIRS=/path/a:/path/b ./scripts/install.sh --skill-only`.

Ensure the install prefix is on your `PATH`. Agents load the skill automatically when installed into their skill tree.

### Compile and install manually

To build and install the binary without the script (skips the agent skill):

```bash
git clone git@github.com:itayavtalyon/remember.git && cd remember
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DREMEMBER_ENABLE_SANITIZERS=OFF
cmake --build build-release --target remember
cmake --install build-release --prefix ~/.local   # -> ~/.local/bin/remember
remember --version                                 # if ~/.local/bin is on PATH
```

`cmake --install` handles only the binary; run `./scripts/install.sh --skill-only`
if you also want the agent skill.

## Architecture (pragmatic)

Only **`store_sqlite.c`** may include SQLite. All other code uses **`store.h`** (port). CLI → commands → store → SQLite adapter.

Engineering rules and post-step review: [`docs/engineering-notes.md`](docs/engineering-notes.md), [`docs/STEP_REVIEW_CHECKLIST.md`](docs/STEP_REVIEW_CHECKLIST.md).

## Build & test (developers)

```bash
cmake -S . -B build -DCMAKE_C_COMPILER=clang \
  -DREMEMBER_ENABLE_SANITIZERS=ON
cmake --build build
ctest --test-dir build --output-on-failure   # step_gate + store_asan_gate

GATE=$(./scripts/read-gate-suites.sh)        # grows via tests/gate-suites
./build/remember_tests ./build/remember --only "$GATE"
./build/remember_store_tests   # store port under ASan

# Full Linux CI locally (Docker one-liner — same as GHA linux job)
./scripts/ci-linux.sh

# Auto-run that check on git push (once per clone)
./scripts/install-hooks.sh
# Skip: SKIP_LINUX_CI=1 git push  |  git push --no-verify

# Production line coverage (src/*.c; target 100%)
PATH="$(brew --prefix llvm)/bin:$PATH" ./scripts/check-coverage.sh
```

Quality matrix (sanitizers, LSan on Linux CI, scan-build, IWYU, macOS `leaks`, coverage):  
[`docs/QUALITY.md`](docs/QUALITY.md). CI: [`.github/workflows/ci.yml`](.github/workflows/ci.yml).

## Format & lint

```bash
export PATH="$(brew --prefix llvm)/bin:$PATH"   # clang-tidy, clang-format, scan-build
brew install cppcheck llvm                      # once
cmake -S . -B build -DCMAKE_C_COMPILER=clang    # reconfigure so format finds brew llvm
cmake --build build --target format
cmake --build build --target lint
# Optional heavy gates:
REMEMBER_SCAN_BUILD=1 ./scripts/lint-all.sh
cmake --build build --target leaks-macos        # macOS only; uses remember_plain
```

Coverage: [`tests/COVERAGE.md`](tests/COVERAGE.md).

## License

[MIT](LICENSE) © Itay Avtalyon. Vendored dependencies in `third_party/` keep
their own licenses (SQLite is public domain; see each subdirectory's notice).
