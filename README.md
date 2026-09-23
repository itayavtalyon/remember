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
- **Expire / delete** — optional TTL (`--expired` bin) and soft-delete (`--deleted`); hard wipe via `purge` / flagged `delete`. Nothing vanishes silently.
- **Automate** — `--json` on every command; ships an agent skill for Claude / Cursor / others.
- **Merge** — `import --from-db` merges another brain by `sync_id`; run `conflicts` after.

Design: [`design-logs/001-foundations.md`](design-logs/001-foundations.md) ·
Plans: [`implementation-plans/INDEX.md`](implementation-plans/INDEX.md).

## Install

### Homebrew (recommended)

```bash
brew install itayavtalyon/remember/remember
remember-install-skill   # required: agent skill (Homebrew cannot write ~/.claude)
```

You also need to run `remember-install-skill` after `brew install`. The formula
ships the binary and the skill file; Homebrew cannot write into `~/.claude` /
`~/.grok` / `~/.cursor`, so that second command symlinks the skill into agent
trees that already exist. Re-run after installing a new agent. Upgrade or
uninstall the usual way (`brew upgrade remember` / `brew uninstall remember`).

## What's New

### v0.2.0 — local sync foundations

Existing databases migrate in place on first open (`user_version` 1/2/3 → **4**).
Every memory gets an immutable `sync_id` (UUID v7). Local numeric `id` is still
the short handle on one machine; after a merge, cite `--sync-id` or `--key`.

**New**

- Locators: positional `id` | `--key` | `--sync-id` (graph:
  `--from-sync-id` / `--to-sync-id`).
- Three bins: **live** (default), **expired** (`--expired`), **deleted**
  (`--deleted`). Unflagged `delete` is reversible; `update --deleted --undelete`
  restores.
- `import --from-db PATH` merges another remember DB by `sync_id`. Then run
  `conflicts` and `conflict accept --id N --keep local|incoming|both`.
- Device identity is a sidecar file `<db-path>.device_id` (not copied on
  import). Hard wipe is allowed only when this DB has exactly one registered
  device.

```bash
remember --json delete --key pref:editor          # soft-delete
remember --json update --deleted --key pref:editor --undelete
remember --json import --from-db /path/other.db
remember --json conflicts
remember --json conflict accept --id 1 --keep both
remember --json purge --deleted                   # whole deleted bin; 1 device
```

**Breaking** (scripts, agents, RememberKit)

| 0.1.x | 0.2.0 |
|-------|--------|
| JSON `"trash": true/false` on entries and stubs | `"bin": "live"\|"expired"\|"deleted"` (always present, never null). Stubs also carry `sync_id`. |
| Exit 3 token `not_in_trash` | `not_expired`. Soft-delete adds `deleted` / `not_deleted`. |
| Unflagged `delete` permanently CASCADE-wiped the row | Soft-delete (`deleted_at` set, body and edges kept). |
| `delete --trash` / `purge-trash` were the only hard paths | Hard wipe is `delete --expired\|--deleted` or `purge --expired\|--deleted` (single-device hatch). |
| JSON fields ended at `expires_at` then optional `links` | Order is `id, sync_id, key, body, tags, source, created_at, updated_at, expires_at, deleted_at, bin, version_vector` then `links` on list/search/get. |

`--trash` and `purge-trash` still run as aliases of `--expired`, with one
stderr deprecation line per invocation. They never select the deleted bin.

**Upgrade notes**

- Opening an old DB is enough; do not copy `<db>.device_id` along with a file
  copy that is meant to be a different replica.
- JSON parsers must stop reading `"trash"` and must accept `"bin"` +
  `"sync_id"` (kits should hard-require `sync_id`).
- Scripts that used `delete` as "forget forever" should switch to
  `delete --deleted` after a soft-delete, or `purge --deleted`.
- After `import --from-db`, always run `conflicts` (import exits 0 even when
  conflicts were recorded).

### v0.1.1 — Homebrew agent skill

`remember-install-skill` plus `share/remember/SKILL.md` in the prefix, so
`brew install` can ship the skill without writing into `~/.claude`. Symlink into
existing Grok / Claude / Cursor skill trees; `brew upgrade` stays current.

### v0.1.0 — first tagged release

The initial public release bundles the full command surface built so far:

- **Related memories** — connect entries with `related`, `cites`, or
  `supersedes`; list a memory's neighbours and `rekey` in place.
- **TTL** — optional `--expires`; expired rows live in a `--trash` /
  `--expired` bin until `purge-trash`. (Soft-delete is **v0.2.0**.)
- **Full-text search** — SQLite **FTS5** over memory bodies (`remember search`).
- **Keys & tags** — named-slot **keys** (upsert by key) and multi-tag faceting;
  internal spaces allowed in both.
- **JSON everywhere** — every command emits a stable JSON envelope, so agents
  and scripts drive the same surface as the CLI.
- **Agent skill** — installs into Claude / Cursor / Grok skill trees.

## Build from source

Prefer Homebrew (above) for a plain install. Build from source for development
or on a platform without the tap.

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
| Skill (prefix) | `$PREFIX/share/remember/SKILL.md` (cmake / Homebrew) |
| Skill (installed) | `remember/SKILL.md` under whichever of `~/.grok/skills`, `~/.claude/skills`, `~/.cursor/skills` already exist (`remember-install-skill`) |

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

`cmake --install` installs the binary, `remember-install-skill`, and
`share/remember/SKILL.md`. Then run `remember-install-skill` (or
`./scripts/install.sh --skill-only`) to symlink/copy into agent trees.

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

## Releasing

Cutting a new version and publishing it to Homebrew (tag → GitHub release →
formula bump): [`docs/RELEASING.md`](docs/RELEASING.md).

## License

[MIT](LICENSE) © Itay Avtalyon. Vendored dependencies in `third_party/` keep
their own licenses (SQLite is public domain; see each subdirectory's notice).
