# 10 — Agent skill + install + dogfood

## Goal

Ship an agent skill that teaches correct use of `remember`, and a single install script that puts the **binary** and **skill** where humans and agents can find them.

## Deliverables

| Artifact | Role |
|----------|------|
| `skills/remember/SKILL.md` | Canonical skill (repo source of truth) |
| `scripts/install.sh` | Build (if needed) + install binary + install skill into agent paths |
| README | User install section: one-liner, `PREFIX`, skill destinations |
| Optional dogfood | Real entries in your local DB |

## Skill content (must match design)

From `design-logs/001-foundations.md` (Agent skill v1):

1. What it is — personal durable memory, not chat log
2. When to write — durable facts/decisions/prefs/corrections
3. When **not** to write — secrets, tokens, passwords, transient todos, huge dumps
4. When to search — before re-asking prefs; session start with tags
5. Commands — exact CLI with `--json` examples
6. IDs and keys — cite ids; address by id **or** `--key`; never invent either
7. Tags on update — omit leaves alone; `--tag` replaces; `--clear-tags` clears
8. Bodies on update — only `--text`; empty rejected
9. Source — agents pass `--source agent` on add only
10. Exit codes — 0 / 1 / 2
11. DB — default path; tests use `--db`; **local disk only** (warn on sync paths)
12. Evolving facts — prefer `--key` slots
13. Paging — honor `count`/`total`; use `--offset` / higher `--limit`

Frontmatter: `name: remember` + description with trigger phrases so agents auto-load it.

## Install script (`scripts/install.sh`)

One script, two jobs (orthogonal axes; do not mix into step 08/09 quality tooling).

```text
Usage: ./scripts/install.sh [--prefix DIR] [--bin-only] [--skill-only] [--no-build]

Defaults:
  PREFIX           ${REMEMBER_PREFIX:-$HOME/.local}
  RELEASE_DIR      ${REMEMBER_RELEASE_DIR:-build-release}
  binary           $PREFIX/bin/remember
  skill src        $ROOT/skills/remember/SKILL.md
```

### Binary

1. Always use a dedicated **Release** tree (`build-release/` by default). Never reuse developer `build/` (ASan).
2. Configure Release + `-DREMEMBER_ENABLE_SANITIZERS=OFF` unless a clean Release binary already exists; `--force-build` cleans that tree only.
3. Refuse to install if the binary links asan/ubsan.
4. `install -m 755 "$RELEASE_DIR/remember" "$PREFIX/bin/remember"`.
5. Print whether `$PREFIX/bin` is on `PATH`.

Prefer CMake `install(TARGETS remember …)` for packaging; the script remains the user-facing entry (skill install is not CMake’s job).

### Skill destinations

Copy (or symlink with `--symlink-skill`) the **same** `SKILL.md` into each existing agent skill root:

| Agent | Destination |
|-------|-------------|
| Grok | `~/.grok/skills/remember/SKILL.md` |
| Claude | `~/.claude/skills/remember/SKILL.md` |
| Cursor (optional) | `~/.cursor/skills/remember/SKILL.md` if `~/.cursor` exists |

Rules:

- Create only under skill roots that already exist **or** create `~/.grok/skills` / `~/.claude/skills` when those product dirs exist (do not invent unrelated product trees).
- Always create the `remember/` subdirectory under each chosen root.
- Never edit the installed skill after copy — re-run install after skill updates.
- Env override: `REMEMBER_SKILL_DIRS=path1:path2` replaces the default list (for CI/tests).

### Flags

| Flag | Effect |
|------|--------|
| `--prefix DIR` | Install prefix for binary (default `~/.local`) |
| `--bin-only` | Skip skill install |
| `--skill-only` | Skip binary build/install |
| `--no-build` | Require existing `$REMEMBER_RELEASE_DIR/remember` (must not be ASan) |
| `--force-build` | Clean rebuild of the Release tree only |
| `--symlink-skill` | Symlink skill instead of copy (dev) |
| `-h` / `--help` | Usage |

Exit non-zero on build failure, missing skill source, or binary install failure. Skill targets that cannot be written: warn and continue; if **no** skill target succeeded and not `--bin-only`, exit non-zero.

## CMake (optional but preferred)

```cmake
install(TARGETS remember RUNTIME DESTINATION bin)
# skill install stays in scripts/install.sh (multi-agent paths)
```

## README

Document:

```bash
./scripts/install.sh                 # binary + skills
./scripts/install.sh --prefix /usr/local
./scripts/install.sh --skill-only    # refresh agent skill only
```

Point at `skills/remember/SKILL.md` as the canonical skill and `docs/QUALITY.md` for developer gates (not end-user install).

## Tests

No automated suite tests for the skill body. Manual:

1. `./scripts/install.sh --prefix /tmp/remember-prefix --skill-only` with `REMEMBER_SKILL_DIRS=/tmp/…` installs skill.
2. Agent with skill loaded can add/search/update from skill alone.
3. `remember --version` works from `$PREFIX/bin` after full install.

## Done checklist

- [x] `skills/remember/SKILL.md` matches live CLI (flags, JSON shapes, key protocol)
- [x] Mentions local-disk DB / no secrets / exit codes / paging / `--key`
- [x] `scripts/install.sh` installs binary + skill(s); documented in README
- [x] INDEX / README status reflect step 10 complete

## Review notes (2026-07-29)

### Implementation

- Canonical skill: `skills/remember/SKILL.md` (design agent checklist 1–13).
- `scripts/install.sh`: Release build (if needed) → `$PREFIX/bin/remember`; copies/symlinks skill into Grok/Claude/Cursor skill trees (or `REMEMBER_SKILL_DIRS`).
- CMake `install(TARGETS remember RUNTIME DESTINATION bin)` for packaging; multi-agent skill paths stay in the script.
- README: install section + developer build/lint unchanged in spirit.

### Pragmatic write-gate

- **DRY:** one skill file; install only copies. Cleared.
- **Orthogonal:** binary install ≠ skill install (`--bin-only` / `--skill-only`). Cleared.
- **Easy-to-replace:** skill path list overridable via env; binary via `--prefix`. Cleared.

## Review notes (2026-07-29) — final v1 review (steps 08–10)

### Verdict for step 10

**Request changes** (install ships non-Release/ASan by default when `build/remember` already exists).

### Findings (addressed 2026-07-30)

- **blocking (fixed):** Install uses dedicated `build-release/` (or `REMEMBER_RELEASE_DIR`); refuses `build/` and sanitizer-linked binaries; `--force-build` cleans only the Release tree.
- **important (fixed):** Skill JSON action line quotes fixed; docs/README/engineering notes match Release-tree behavior.

## Non-goals

- Homebrew formula / package managers (follow-up)
- Installing dev tools (llvm, cppcheck) — that is step 08 / QUALITY.md
- Overwriting user DB or config
