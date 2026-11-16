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

# 1. No agent product dirs → exit 1, mention the four roots.
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
assert "a fake home with no product directory prints the error line that names ~/.codex" \
  grep -qxF 'error: no agent skill roots found (~/.grok, ~/.claude, ~/.cursor, ~/.codex);' <<<"$out"

# 1b. Only ~/.codex → ~/.codex/skills/remember/SKILL.md, nothing else (plan 28).
HOME="$TMP/codex"
mkdir -p "$HOME/.codex"
set +e
out="$(HOME="$HOME" "$TOOL" 2>&1)"
rc=$?
set -e
assert_eq "codex-only exits 0" "$rc" "0"
cdx="$HOME/.codex/skills/remember/SKILL.md"
assert "a fake home with only ~/.codex installs ~/.codex/skills/remember/SKILL.md" test -L "$cdx"
assert "codex skill target is the repo SKILL.md" test "$(readlink "$cdx")" = "$SRC"
assert_eq "codex-only prints the codex path" "$out" "skill: $cdx"
assert "codex-only does not invent ~/.grok" test ! -e "$HOME/.grok"
assert "codex-only does not invent ~/.claude" test ! -e "$HOME/.claude"
assert "codex-only does not invent ~/.cursor" test ! -e "$HOME/.cursor"

# 1c. Without ~/.codex no Codex path appears, with the other products present.
HOME="$TMP/nocodex"
mkdir -p "$HOME/.claude"
set +e
out="$(HOME="$HOME" "$TOOL" 2>&1)"
set -e
assert "a missing ~/.codex adds no Codex path" test ! -e "$HOME/.codex"
assert "a missing ~/.codex prints no codex line" test "$(grep -c '\.codex' <<<"$out")" = 0

# 1d. --copy writes a regular file under ~/.codex, the same bytes as the source.
HOME="$TMP/codexcopy"
mkdir -p "$HOME/.codex"
set +e
HOME="$HOME" "$TOOL" --copy >/dev/null 2>&1
rc=$?
set -e
assert_eq "codex --copy exits 0" "$rc" "0"
assert "codex --copy is a regular file" test -f "$HOME/.codex/skills/remember/SKILL.md"
assert "codex --copy is not a symlink" test ! -L "$HOME/.codex/skills/remember/SKILL.md"
assert "codex --copy bytes match source" cmp -s "$HOME/.codex/skills/remember/SKILL.md" "$SRC"

# 1e. A ~/.codex that is a file is not a product directory (no mkdir under a file).
HOME="$TMP/codexfile"
mkdir -p "$HOME"
: >"$HOME/.codex"
set +e
out="$(HOME="$HOME" "$TOOL" 2>&1)"
rc=$?
set -e
assert_eq "a ~/.codex file exits 1 as no root" "$rc" "1"
assert "a ~/.codex file stays a file" test -f "$HOME/.codex"
assert "a ~/.codex file is no root, not a failed mkdir" \
  grep -qF 'error: no agent skill roots found' <<<"$out"
assert "a ~/.codex file prints no cannot-create warning" test "$(grep -c '^warn: ' <<<"$out")" = 0

# 1f. A ~/.codex symlink to a directory is a directory: the skill lands in the target.
HOME="$TMP/codexlink"
mkdir -p "$HOME" "$TMP/codexlink-target"
ln -s "$TMP/codexlink-target" "$HOME/.codex"
set +e
HOME="$HOME" "$TOOL" >/dev/null 2>&1
rc=$?
set -e
assert_eq "a ~/.codex symlink to a directory exits 0" "$rc" "0"
assert "a ~/.codex symlink installs into its target" \
  test -L "$TMP/codexlink-target/skills/remember/SKILL.md"

# 1g. A dangling ~/.codex symlink is not a directory.
HOME="$TMP/codexdangling"
mkdir -p "$HOME"
ln -s "$TMP/does-not-exist" "$HOME/.codex"
set +e
HOME="$HOME" "$TOOL" >/dev/null 2>&1
rc=$?
set -e
assert_eq "a dangling ~/.codex symlink exits 1 as no root" "$rc" "1"
assert "a dangling ~/.codex symlink creates no target" test ! -e "$TMP/does-not-exist"

# 1h. A HOME with spaces keeps one path per root.
HOME="$TMP/home with spaces"
mkdir -p "$HOME/.codex" "$HOME/.claude"
set +e
out="$(HOME="$HOME" "$TOOL" 2>&1)"
rc=$?
set -e
assert_eq "a HOME with spaces exits 0" "$rc" "0"
assert "a HOME with spaces installs codex" test -L "$HOME/.codex/skills/remember/SKILL.md"
assert "a HOME with spaces installs claude" test -L "$HOME/.claude/skills/remember/SKILL.md"
assert_eq "a HOME with spaces prints two skill lines" "$(grep -c '^skill: ' <<<"$out")" "2"

# 1i. A skill root that cannot take the file fails loudly: ~/.codex/skills is a file, or
#     SKILL.md is a real directory (ln and install would nest the link inside it and
#     report success).
HOME="$TMP/codexskillsfile"
mkdir -p "$HOME/.codex"
: >"$HOME/.codex/skills"
set +e
out="$(HOME="$HOME" "$TOOL" 2>&1)"
rc=$?
set -e
assert_eq "a ~/.codex/skills file exits 1" "$rc" "1"
assert "a ~/.codex/skills file prints no skill line" test "$(grep -c '^skill: ' <<<"$out")" = 0
HOME="$TMP/codexskilldir"
mkdir -p "$HOME/.codex/skills/remember/SKILL.md"
for flag in --symlink --copy; do
  set +e
  out="$(HOME="$HOME" "$TOOL" "$flag" 2>&1)"
  rc=$?
  set -e
  assert_eq "a SKILL.md directory exits 1 ($flag)" "$rc" "1"
  assert "a SKILL.md directory prints no skill line ($flag)" \
    test "$(grep -c '^skill: ' <<<"$out")" = 0
  assert "a SKILL.md directory stays empty ($flag)" \
    test -z "$(ls -A "$HOME/.codex/skills/remember/SKILL.md")"
done

# 1j. A skill dir that is a symlink to the source's own directory must not turn the
#     source into a link to itself (ln -sfn would, and the skill file is gone).
HOME="$TMP/codexself"
mkdir -p "$TMP/selfrepo/skills/remember" "$HOME/.codex/skills"
echo "precious skill" >"$TMP/selfrepo/skills/remember/SKILL.md"
ln -s "$TMP/selfrepo/skills/remember" "$HOME/.codex/skills/remember"
for flag in --symlink --copy; do
  set +e
  out="$(HOME="$HOME" REMEMBER_SKILL_SRC="$TMP/selfrepo/skills/remember/SKILL.md" "$TOOL" "$flag" 2>&1)"
  rc=$?
  set -e
  assert_eq "a skill dir that is the source dir exits 1 ($flag)" "$rc" "1"
  assert "the source skill file stays a regular file ($flag)" \
    test -f "$TMP/selfrepo/skills/remember/SKILL.md" -a ! -L "$TMP/selfrepo/skills/remember/SKILL.md"
  assert_eq "the source skill file keeps its bytes ($flag)" \
    "$(cat "$TMP/selfrepo/skills/remember/SKILL.md")" "precious skill"
  assert "the self-install names the source directory ($flag)" \
    grep -qF 'is the source directory' <<<"$out"
done

# 1k. An old SKILL.md link (to a file the user owns elsewhere, or dangling) is replaced,
#     never written through, in both modes.
for mode in --symlink --copy; do
  HOME="$TMP/oldlink$mode"
  mkdir -p "$HOME/.codex/skills/remember"
  echo "someone else's file" >"$TMP/elsewhere$mode"
  ln -s "$TMP/elsewhere$mode" "$HOME/.codex/skills/remember/SKILL.md"
  set +e
  HOME="$HOME" "$TOOL" "$mode" >/dev/null 2>&1
  rc=$?
  set -e
  assert_eq "an old SKILL.md link is replaced ($mode)" "$rc" "0"
  assert_eq "the file an old link pointed at is untouched ($mode)" \
    "$(cat "$TMP/elsewhere$mode")" "someone else's file"
  rm -f "$HOME/.codex/skills/remember/SKILL.md" "$TMP/elsewhere$mode"
  ln -s "$TMP/dangling$mode" "$HOME/.codex/skills/remember/SKILL.md"
  set +e
  HOME="$HOME" "$TOOL" "$mode" >/dev/null 2>&1
  rc=$?
  set -e
  assert_eq "a dangling SKILL.md link is replaced ($mode)" "$rc" "0"
  assert "a dangling SKILL.md link creates no target ($mode)" test ! -e "$TMP/dangling$mode"
done

# 1l. --copy over a link that symlink mode left to the same source: install would copy the
#     file onto itself ("same file", exit 64) and stop before the next product.
HOME="$TMP/linkthencopy"
mkdir -p "$HOME/.claude" "$HOME/.codex"
HOME="$HOME" "$TOOL" >/dev/null 2>&1
set +e
out="$(HOME="$HOME" "$TOOL" --copy 2>&1)"
rc=$?
set -e
assert_eq "--copy over a symlink-mode install exits 0" "$rc" "0"
for p in .claude .codex; do
  assert "--copy over a symlink-mode install leaves a regular file ($p)" \
    test -f "$HOME/$p/skills/remember/SKILL.md" -a ! -L "$HOME/$p/skills/remember/SKILL.md"
done
assert "--copy over a symlink-mode install keeps the source" cmp -s "$HOME/.codex/skills/remember/SKILL.md" "$SRC"

# 1m. A relative REMEMBER_SKILL_SRC: the link must resolve from the skill dir, not dangle.
HOME="$TMP/relsrc"
mkdir -p "$HOME/.codex" "$TMP/relsrc-repo/skills/remember"
echo "relative source" >"$TMP/relsrc-repo/skills/remember/SKILL.md"
set +e
(cd "$TMP/relsrc-repo" && HOME="$HOME" REMEMBER_SKILL_SRC=skills/remember/SKILL.md "$TOOL" >/dev/null 2>&1)
rc=$?
set -e
assert_eq "a relative REMEMBER_SKILL_SRC exits 0" "$rc" "0"
assert_eq "a relative REMEMBER_SKILL_SRC link resolves to the source" \
  "$(cat "$HOME/.codex/skills/remember/SKILL.md" 2>&1)" "relative source"

# 1n. A HOME with a newline is one path: nothing lands under its first line or in the cwd.
HOME="$TMP/nl"$'\n'"home"
mkdir -p "$HOME/.codex" "$TMP/nl-cwd"
set +e
(cd "$TMP/nl-cwd" && HOME="$HOME" "$TOOL" >/dev/null 2>&1)
rc=$?
set -e
assert_eq "a HOME with a newline exits 0" "$rc" "0"
assert "a HOME with a newline installs under that HOME" test -L "$HOME/.codex/skills/remember/SKILL.md"
assert "a HOME with a newline writes nothing under its first line" test ! -e "$TMP/nl/remember"
assert "a HOME with a newline writes nothing in the cwd" test -z "$(ls -A "$TMP/nl-cwd")"

# 1o. REMEMBER_SKILL_DIRS: a dir with a newline is one dir, not its first line.
HOME="$TMP/nldirs-home"
mkdir -p "$HOME"
nldir="$TMP/nldir"$'\n'"two"
set +e
HOME="$HOME" REMEMBER_SKILL_DIRS="$nldir:$TMP/nldir-other" "$TOOL" >/dev/null 2>&1
rc=$?
set -e
assert_eq "REMEMBER_SKILL_DIRS with a newline exits 0" "$rc" "0"
assert "REMEMBER_SKILL_DIRS keeps a dir with a newline whole" test -L "$nldir/remember/SKILL.md"
assert "REMEMBER_SKILL_DIRS reads the dir after it" test -L "$TMP/nldir-other/remember/SKILL.md"
assert "REMEMBER_SKILL_DIRS writes nothing under a newline dir's first line" test ! -e "$TMP/nldir"

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
