#!/usr/bin/env bash
# Black-box tests for scripts/remember-install-skill.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TOOL="$ROOT/scripts/remember-install-skill"
SRC="$ROOT/skills/remember/SKILL.md"
FAILS=0

assert() {
  local msg="$1"
  shift
  if "$@"; then
    echo "ok  $msg"
  else
    echo "FAIL $msg" >&2
    FAILS=$((FAILS + 1))
  fi
}

assert_eq() {
  local msg="$1" got="$2" want="$3"
  if [[ "$got" == "$want" ]]; then
    echo "ok  $msg"
  else
    echo "FAIL $msg" >&2
    echo "     got:  $got" >&2
    echo "     want: $want" >&2
    FAILS=$((FAILS + 1))
  fi
}

if [[ ! -f "$SRC" ]]; then
  echo "error: missing skill source $SRC" >&2
  exit 1
fi

TMP="$(mktemp -d "${TMPDIR:-/tmp}/remember-skill-test.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

# 1. No agent product dirs → exit 1, mention the three roots.
HOME="$TMP/empty"
mkdir -p "$HOME"
set +e
out="$(HOME="$HOME" "$TOOL" 2>&1)"
rc=$?
set -e
assert_eq "no agent dirs exits 1" "$rc" "1"
assert "stderr mentions grok" grep -q '\.grok' <<<"$out"
assert "stderr mentions claude" grep -q '\.claude' <<<"$out"
assert "stderr mentions cursor" grep -q '\.cursor' <<<"$out"

# 2. Existing ~/.claude → symlink into skills/remember/SKILL.md.
HOME="$TMP/claude"
mkdir -p "$HOME/.claude"
set +e
out="$(HOME="$HOME" "$TOOL" 2>&1)"
rc=$?
set -e
assert_eq "claude-only exits 0" "$rc" "0"
skill="$HOME/.claude/skills/remember/SKILL.md"
assert "claude skill is a symlink" test -L "$skill"
assert "claude skill target is the repo SKILL.md" test "$(readlink "$skill")" = "$SRC"
assert "does not invent ~/.grok" test ! -e "$HOME/.grok"
assert "does not invent ~/.cursor" test ! -e "$HOME/.cursor"

# 3. Multiple existing product dirs all get the skill.
HOME="$TMP/multi"
mkdir -p "$HOME/.grok" "$HOME/.cursor"
set +e
out="$(HOME="$HOME" "$TOOL" 2>&1)"
rc=$?
set -e
assert_eq "multi exits 0" "$rc" "0"
assert "grok skill symlink" test -L "$HOME/.grok/skills/remember/SKILL.md"
assert "cursor skill symlink" test -L "$HOME/.cursor/skills/remember/SKILL.md"
assert "does not invent ~/.claude" test ! -e "$HOME/.claude"

# 4. --copy writes a regular file, not a symlink.
HOME="$TMP/copy"
mkdir -p "$HOME/.claude"
set +e
out="$(HOME="$HOME" "$TOOL" --copy 2>&1)"
rc=$?
set -e
assert_eq "--copy exits 0" "$rc" "0"
cskill="$HOME/.claude/skills/remember/SKILL.md"
assert "--copy is a regular file" test -f "$cskill"
assert "--copy is not a symlink" test ! -L "$cskill"
assert "--copy bytes match source" cmp -s "$cskill" "$SRC"

# 5. Prefix layout: bin/remember-install-skill + share/remember/SKILL.md
#    (Homebrew / cmake --install). Invoked via PATH so BASH_SOURCE is prefix/bin.
prefix="$TMP/prefix"
mkdir -p "$prefix/bin" "$prefix/share/remember" "$TMP/prefix-home/.claude"
# Placeholder until cmake installs the real script; tests still need the tool.
if [[ -x "$TOOL" ]]; then
  cp "$TOOL" "$prefix/bin/remember-install-skill"
  chmod +x "$prefix/bin/remember-install-skill"
fi
echo "from-prefix" >"$prefix/share/remember/SKILL.md"
set +e
out="$(
  HOME="$TMP/prefix-home" PATH="$prefix/bin:$PATH" \
    remember-install-skill 2>&1
)"
rc=$?
set -e
assert_eq "prefix layout exits 0" "$rc" "0"
pskill="$TMP/prefix-home/.claude/skills/remember/SKILL.md"
assert "prefix skill is a symlink" test -L "$pskill"
assert "prefix symlink points at share/remember/SKILL.md" \
  test "$(readlink "$pskill")" -ef "$prefix/share/remember/SKILL.md"

# 6. REMEMBER_SKILL_DIRS replaces the default list.
HOME="$TMP/envhome"
mkdir -p "$HOME/.claude" "$TMP/custom-skills"
set +e
out="$(
  HOME="$HOME" REMEMBER_SKILL_DIRS="$TMP/custom-skills" \
    "$TOOL" 2>&1
)"
rc=$?
set -e
assert_eq "REMEMBER_SKILL_DIRS exits 0" "$rc" "0"
assert "custom dest got the skill" test -L "$TMP/custom-skills/remember/SKILL.md"
assert "default ~/.claude was not touched" test ! -e "$HOME/.claude/skills"

# 7. scripts/install.sh --skill-only still copies (clone install, not brew).
HOME="$TMP/installsh"
mkdir -p "$HOME/.claude"
set +e
out="$(HOME="$HOME" "$ROOT/scripts/install.sh" --skill-only 2>&1)"
rc=$?
set -e
assert_eq "install.sh --skill-only exits 0" "$rc" "0"
ishill="$HOME/.claude/skills/remember/SKILL.md"
assert "install.sh copies a regular file" test -f "$ishill"
assert "install.sh copy is not a symlink" test ! -L "$ishill"

# 8-10. install.sh --bin-only never installs a stale Release binary.
#       A stub cmake on PATH stands in for the real build: `--build TREE`
#       writes TREE/remember printing "remember $STUB_VERSION".
want_ver="$(sed -n 's/^project(remember VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
stubbin="$TMP/stubbin"
mkdir -p "$stubbin"
cat >"$stubbin/cmake" <<'EOF'
#!/usr/bin/env bash
echo "$*" >>"$STUB_LOG"
[[ "$1" == "--build" && -n "$STUB_VERSION" ]] || exit 0
printf '#!/bin/sh\necho "remember %s"\n' "$STUB_VERSION" >"$2/remember"
chmod +x "$2/remember"
EOF
chmod +x "$stubbin/cmake"

stale_tree() {
  mkdir -p "$1"
  printf '#!/bin/sh\necho "remember 0.0.1"\n' >"$1/remember"
  chmod +x "$1/remember"
}

run_bin_install() {
  set +e
  out="$(
    PATH="$stubbin:$PATH" STUB_LOG="$TMP/cmake.log" STUB_VERSION="$1" \
      REMEMBER_RELEASE_DIR="$TMP/rel" "$ROOT/scripts/install.sh" \
      --bin-only --prefix "$TMP/binprefix" "${@:2}" 2>&1
  )"
  rc=$?
  set -e
}

# 8. Existing clean stale binary is rebuilt (incremental build always runs).
rm -rf "$TMP/rel" "$TMP/binprefix" "$TMP/cmake.log"
stale_tree "$TMP/rel"
run_bin_install "$want_ver"
assert_eq "stale binary: install exits 0" "$rc" "0"
assert "stale binary: cmake --build ran" grep -q -- '--build' "$TMP/cmake.log"
assert_eq "stale binary: installed version is current" \
  "$("$TMP/binprefix/bin/remember" --version)" "remember $want_ver"

# 9. Build leaves a mismatched binary -> fail loudly, install nothing.
rm -rf "$TMP/rel" "$TMP/binprefix" "$TMP/cmake.log"
stale_tree "$TMP/rel"
run_bin_install ""
assert_eq "version mismatch exits 1" "$rc" "1"
assert "version mismatch names both versions" \
  grep -q "0.0.1.*$want_ver" <<<"$out"
assert "version mismatch installs nothing" test ! -e "$TMP/binprefix/bin/remember"

# 10. --no-build skips cmake but still refuses a stale binary.
rm -rf "$TMP/rel" "$TMP/binprefix" "$TMP/cmake.log"
stale_tree "$TMP/rel"
run_bin_install "$want_ver" --no-build
assert_eq "--no-build stale exits 1" "$rc" "1"
assert "--no-build does not run cmake" test ! -e "$TMP/cmake.log"

if [[ "$FAILS" -ne 0 ]]; then
  echo "$FAILS failure(s)" >&2
  exit 1
fi
echo "ALL OK"
exit 0
