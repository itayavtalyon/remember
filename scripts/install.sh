#!/usr/bin/env bash
# Install remember binary and/or agent skill(s).
# Canonical skill source: skills/remember/SKILL.md
#
# Binary always comes from a dedicated Release tree (default build-release),
# never from the developer ASan tree (build/). That keeps install artifacts
# free of sanitizer dylibs after a normal Debug configure.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

PREFIX="${REMEMBER_PREFIX:-$HOME/.local}"
DO_BIN=1
DO_SKILL=1
NO_BUILD=0
FORCE_BUILD=0
SYMLINK_SKILL=0
# Dedicated Release tree only -- do not default to "build" (often ASan).
RELEASE_DIR="${REMEMBER_RELEASE_DIR:-build-release}"

usage() {
  cat <<'EOF'
Usage: ./scripts/install.sh [options]

Install the remember CLI binary and agent skill (SKILL.md).

The binary is always built as Release (no sanitizers) into build-release/
(or REMEMBER_RELEASE_DIR). This never reconfigures the developer build/ tree.

Options:
  --prefix DIR       Binary prefix (default: $REMEMBER_PREFIX or ~/.local)
  --bin-only         Install binary only
  --skill-only       Install skill(s) only
  --no-build         Require existing $REMEMBER_RELEASE_DIR/remember
  --force-build      Clean reconfigure + rebuild of the Release tree
  --symlink-skill    Symlink skill instead of copy (dev)
  -h, --help         Show this help

Environment:
  REMEMBER_PREFIX         Same as --prefix
  REMEMBER_RELEASE_DIR    Release build dir (default: build-release)
  REMEMBER_SKILL_DIRS     Colon-separated skill parent dirs (each gets
                          remember/SKILL.md). Default: product skill roots
                          under ~/.grok, ~/.claude, ~/.cursor when present.

Examples:
  ./scripts/install.sh
  ./scripts/install.sh --prefix /usr/local
  ./scripts/install.sh --skill-only
  REMEMBER_SKILL_DIRS=/tmp/skills ./scripts/install.sh --skill-only
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix)
      PREFIX="${2:?--prefix requires DIR}"
      shift 2
      ;;
    --bin-only)
      DO_SKILL=0
      shift
      ;;
    --skill-only)
      DO_BIN=0
      shift
      ;;
    --no-build)
      NO_BUILD=1
      shift
      ;;
    --force-build)
      FORCE_BUILD=1
      shift
      ;;
    --symlink-skill)
      SYMLINK_SKILL=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "error: unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ "$DO_BIN" -eq 0 && "$DO_SKILL" -eq 0 ]]; then
  echo "error: nothing to install" >&2
  exit 1
fi

# Refuse to treat the developer ASan tree as an install source.
if [[ "$RELEASE_DIR" == "build" || "$RELEASE_DIR" == "./build" ]]; then
  echo "error: REMEMBER_RELEASE_DIR must not be 'build' (that tree is for ASan/dev)" >&2
  echo "       use build-release (default) or another dedicated Release dir" >&2
  exit 1
fi

SKILL_SRC="$ROOT/skills/remember/SKILL.md"
if [[ "$DO_SKILL" -eq 1 && ! -f "$SKILL_SRC" ]]; then
  echo "error: missing skill source: $SKILL_SRC" >&2
  exit 1
fi

# True if the Mach-O/ELF binary links a sanitizer runtime (install must not ship these).
binary_looks_sanitized() {
  local bin="$1"
  if [[ ! -x "$bin" ]]; then
    return 1
  fi
  if command -v otool >/dev/null 2>&1; then
    otool -L "$bin" 2>/dev/null | grep -Eqi 'asan|ubsan|libclang_rt\.(a|ub)san' && return 0
  fi
  if command -v ldd >/dev/null 2>&1; then
    ldd "$bin" 2>/dev/null | grep -Eqi 'asan|ubsan|libclang_rt\.(a|ub)san' && return 0
  fi
  # Fallback: symbol strings (covers static links / odd toolchains).
  if command -v strings >/dev/null 2>&1; then
    strings "$bin" 2>/dev/null | grep -Eqi 'AddressSanitizer|UndefinedBehaviorSanitizer' && return 0
  fi
  return 1
}

release_tree() {
  # Absolute REMEMBER_RELEASE_DIR is used as-is; relative is under $ROOT.
  case "$RELEASE_DIR" in
    /*) printf '%s\n' "$RELEASE_DIR" ;;
    *) printf '%s\n' "$ROOT/$RELEASE_DIR" ;;
  esac
}

install_binary() {
  local tree
  local bin_src
  local bin_dst="$PREFIX/bin/remember"
  local need_build=0

  tree="$(release_tree)"
  bin_src="$tree/remember"

  if [[ "$NO_BUILD" -eq 1 ]]; then
    if [[ ! -x "$bin_src" ]]; then
      echo "error: no binary at $bin_src and --no-build set" >&2
      exit 1
    fi
    if binary_looks_sanitized "$bin_src"; then
      echo "error: $bin_src looks sanitizer-linked; refuse to install" >&2
      echo "       rebuild with: $0 --force-build (or drop --no-build)" >&2
      exit 1
    fi
  else
    need_build=1
    if [[ "$FORCE_BUILD" -eq 1 ]]; then
      echo "== clean Release tree ($tree) =="
      rm -rf "$tree"
    elif [[ -x "$bin_src" ]] && ! binary_looks_sanitized "$bin_src"; then
      # Reuse a prior clean Release binary unless forced.
      need_build=0
    fi
  fi

  if [[ "$need_build" -eq 1 ]]; then
    echo "== build Release (no sanitizers) -> $tree =="
    cmake -S "$ROOT" -B "$tree" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER="${CC:-clang}" \
      -DREMEMBER_ENABLE_SANITIZERS=OFF
    cmake --build "$tree" --target remember
  fi

  if [[ ! -x "$bin_src" ]]; then
    echo "error: build did not produce $bin_src" >&2
    exit 1
  fi
  if binary_looks_sanitized "$bin_src"; then
    echo "error: $bin_src is sanitizer-linked after build; refusing install" >&2
    exit 1
  fi

  echo "== install binary -> $bin_dst =="
  mkdir -p "$PREFIX/bin"
  install -m 755 "$bin_src" "$bin_dst"
  echo "installed: $bin_dst"
  # Report the install dir itself; do not trust an unrelated PATH hit for remember.
  case ":$PATH:" in
    *":$PREFIX/bin:"*)
      echo "on PATH: $bin_dst"
      ;;
    *)
      echo "note: $PREFIX/bin is not on PATH; add:"
      echo "  export PATH=\"$PREFIX/bin:\$PATH\""
      ;;
  esac
  "$bin_dst" --version || true
}

default_skill_dirs() {
  local dirs=()
  # Only touch skill roots for products that already exist on this machine.
  if [[ -d "$HOME/.grok" ]]; then
    dirs+=("$HOME/.grok/skills")
  fi
  if [[ -d "$HOME/.claude" ]]; then
    dirs+=("$HOME/.claude/skills")
  fi
  if [[ -d "$HOME/.cursor" ]]; then
    dirs+=("$HOME/.cursor/skills")
  fi
  # bash 3.2 (macOS): empty "${dirs[@]}" aborts under set -u.
  if [[ ${#dirs[@]} -gt 0 ]]; then
    printf '%s\n' "${dirs[@]}"
  fi
}

install_skills() {
  local dirs=()
  local d dest ok=0

  if [[ -n "${REMEMBER_SKILL_DIRS:-}" ]]; then
    IFS=':' read -r -a dirs <<< "$REMEMBER_SKILL_DIRS"
  else
    while IFS= read -r d; do
      [[ -n "$d" ]] && dirs+=("$d")
    done < <(default_skill_dirs)
  fi

  if [[ ${#dirs[@]} -eq 0 ]]; then
    echo "error: no agent skill roots found (~/.grok, ~/.claude, ~/.cursor);" >&2
    echo "       set REMEMBER_SKILL_DIRS=/path/to/skills or create one first" >&2
    exit 1
  fi

  echo "== install skill =="
  for d in "${dirs[@]}"; do
    [[ -z "$d" ]] && continue
    dest="$d/remember"
    if ! mkdir -p "$dest" 2>/dev/null; then
      echo "warn: cannot create $dest" >&2
      continue
    fi
    if [[ "$SYMLINK_SKILL" -eq 1 ]]; then
      ln -sfn "$SKILL_SRC" "$dest/SKILL.md"
    else
      install -m 644 "$SKILL_SRC" "$dest/SKILL.md"
    fi
    echo "skill: $dest/SKILL.md"
    ok=1
  done

  if [[ "$ok" -eq 0 ]]; then
    echo "error: no skill destination succeeded" >&2
    exit 1
  fi
}

if [[ "$DO_BIN" -eq 1 ]]; then
  install_binary
fi
if [[ "$DO_SKILL" -eq 1 ]]; then
  install_skills
fi

echo "OK"
exit 0
