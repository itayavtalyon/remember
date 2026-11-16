#!/usr/bin/env bash
# Plan 28 (release 0.3.0): the version text, the C-40 and C-41 copy, and specs/RELEASE-0.3.0.md.
# Usage: test_release.sh [PATH-TO-remember]   (default build/remember)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${1:-$ROOT/build/remember}"
SKILL="$ROOT/skills/remember/SKILL.md"
README="$ROOT/README.md"
SPEC="$ROOT/specs/RELEASE-0.3.0.md"
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

TMP="$(mktemp -d "${TMPDIR:-/tmp}/remember-release-test.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

# ---- the version: one text in CMakeLists.txt, the same text in the fallback ----
cmake_ver="$(sed -n 's/^project(remember VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
fallback_ver="$(sed -n 's/^#define REMEMBER_VERSION "\(.*\)"$/\1/p' "$ROOT/src/commands_common.h")"
assert_eq "CMakeLists.txt project VERSION is 0.3.0" "$cmake_ver" "0.3.0"
assert_eq "the REMEMBER_VERSION fallback is the CMake text" "$fallback_ver" "$cmake_ver"
assert_eq "the fallback is defined once" \
  "$(grep -rc '^#define REMEMBER_VERSION' "$ROOT/src" | awk -F: '{s += $2} END {print s}')" "1"

# Build 1: the CMake constant (the binary under test). Build 2: a bare compile
# that gets the fallback (the lint and analyzer builds).
assert_eq "--version prints 0.3.0 from the CMake constant" "$("$BIN" --version)" "remember 0.3.0"
cat >"$TMP/fallback.c" <<'EOF'
#include "commands_common.h"

#include <stdio.h>

int main(void)
{
    return puts(REMEMBER_VERSION) < 0;
}
EOF
cc -std=c11 -I"$ROOT/src" -I"$ROOT/third_party/sqlite" -o "$TMP/fallback" "$TMP/fallback.c"
assert_eq "--version text from the fallback build is 0.3.0" "$("$TMP/fallback")" "0.3.0"

# ---- C-40: the skill's one bucket sentence ----
C40='Tags that start with a bucket prefix group memories into buckets, for example `project:fortkeeps`. Bucket prefixes are the brain setting `remember.buckets.prefixes`, which is `project:` and `status:` by default; run `remember settings get remember.buckets.prefixes` to see the list in use.'
assert "the skill bucket sentence is the C-40 paragraph" grep -qxF -- "$C40" "$SKILL"
assert_eq "the skill has no other bucket sentence" "$(grep -ci bucket "$SKILL")" "1"
assert "the skill has no remember config get bucket-prefixes" \
  test "$(grep -cF 'remember config get bucket-prefixes' "$SKILL")" = 0

# ---- C-41 ----
cat >"$TMP/readme-bullets" <<'EOF'
- `--json` now puts success and failure in one `remember-message` version 1 envelope on stdout, and stderr stays empty. `--json-errors` is gone.
- Lists page with a cursor. Search doesn't page. `--offset` is gone.
- A memory's id in output is `id`, a lowercase hyphenated UUID. `--sync-id` is gone. The integer `local_id` appears only in CLI output.
- Actors replace `source`. `--source` is gone completely.
- `--trash` now means the Trash: memories you deleted. For expired memories, use `--expired`. For this release, `--trash` prints: `remember: warning: --trash now means the Trash (deleted memories). For expired memories, use --expired.`
- `purge-trash` is gone. To delete memories in the Trash for good, run `remember purge <locator>` for each one.
EOF
cat >"$TMP/skill-bullets" <<'EOF'
- Use `--json` to get one `remember-message` version 1 envelope on stdout, for success and failure alike. There is no `--json-errors`.
- To page a list, pass back its `next_cursor`. Search returns its top results and doesn't page. There is no `--offset`.
- Refer to a memory by `id`, a lowercase hyphenated UUID. The number `local_id` is for this device's CLI only. There is no `--sync-id`.
- Writes record actors, not `source`. There is no `--source`.
- `--trash` is the Trash (deleted memories). `--expired` is expired memories.
- To empty the Trash, run `remember purge <locator>` once for each memory in it. There is no `purge-trash`.
- For scripts and agents, use `--json`, because human dates are local.
EOF

# What's New for 0.3.0 is the README section from its heading to the next release.
sed -n '/^### v0\.3\.0 /,/^### v0\.2\.0 /p' "$README" >"$TMP/whats-new"

has_all_lines() { # has_all_lines <lines file> <file>
  local missing=0 line
  while IFS= read -r line; do
    grep -qxF -- "$line" "$2" || {
      echo "     missing: $line" >&2
      missing=1
    }
  done <"$1"
  return "$missing"
}
# The removed names appear in the C-41 lines and nowhere else of the same text.
names_outside() { # names_outside <lines file> <file>
  grep -vxF -f "$1" "$2" | grep -cE -e '--json-errors|--offset|--sync-id|--source|purge-trash' || true
}

assert "README What's New has the C-41 bullets" has_all_lines "$TMP/readme-bullets" "$TMP/whats-new"
assert_eq "README What's New names a removed flag only in a C-41 bullet" \
  "$(names_outside "$TMP/readme-bullets" "$TMP/whats-new")" "0"
schema="$(sed -n 's/^enum { SCHEMA_VERSION = \([0-9]*\),.*/\1/p' "$ROOT/src/store_sqlite.c")"
assert "README What's New names the schema that a migrated brain gets" \
  grep -qF "migrates it to \`user_version\` **$schema**." "$TMP/whats-new"
assert "README What's New does not list wiki-links" \
  test "$(grep -ci 'wiki' "$TMP/whats-new")" = 0
assert "the skill has the C-41 bullets" has_all_lines "$TMP/skill-bullets" "$SKILL"
assert_eq "the skill names a removed flag only in a C-41 bullet" \
  "$(names_outside "$TMP/skill-bullets" "$SKILL")" "0"
assert "the skill does not contain 'see C-13'" test "$(grep -cF 'see C-13' "$SKILL")" = 0

# ---- specs/RELEASE-0.3.0.md ----
assert "specs/RELEASE-0.3.0.md exists" test -f "$SPEC"
if [[ -f "$SPEC" ]]; then
  for p in 22 23 25 26 27 27a 27b; do
    line="$(grep -E "^- Plan $p[ ,:]" "$SPEC" || true)"
    assert_eq "RELEASE-0.3.0 has one line for plan $p" "$(grep -cE "^- Plan $p[ ,:]" "$SPEC")" "1"
    files="$(grep -oE '[A-Z][A-Z0-9-]*\.md' <<<"$line" || true)"
    assert "RELEASE-0.3.0 plan $p line names a spec file" test -n "$files"
    for f in $files; do
      assert "RELEASE-0.3.0 plan $p names specs/$f, which exists" test -f "$ROOT/specs/$f"
    done
  done
  assert "RELEASE-0.3.0 lists R27b-2 and R27b-3 with PR #75" \
    grep -qE '^- R27b-2 and R27b-3.*#75' "$SPEC"
fi

if [[ "$FAILS" -ne 0 ]]; then
  echo "$FAILS failure(s)" >&2
  exit 1
fi
echo "ALL OK"
