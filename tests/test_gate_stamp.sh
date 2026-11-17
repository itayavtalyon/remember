#!/usr/bin/env bash
# scripts/gate-stamp.sh: a stamp marks one check as passed on one clean tree, and
# nothing else (a dirty tree, a changed tree, or another check does not match).
set -uo pipefail
stamp="$(cd "$(dirname "$0")/.." && pwd)/scripts/gate-stamp.sh"
repo="$(mktemp -d "${TMPDIR:-/tmp}/remember-gate-stamp.XXXXXX")"
trap 'rm -rf "$repo"' EXIT
fails=0
want() { # want <exit> <command...>
  local w="$1"
  shift
  "$@" >/dev/null 2>&1
  local got=$?
  if [ "$got" != "$w" ]; then
    echo "gate_stamp: [$*]: exit $got, want $w" >&2
    fails=$((fails + 1))
  fi
}
cd "$repo" || exit 1
git init -q . && git config user.email t@t && git config user.name t
echo a >f && git add f && git commit -qm a
t1="$("$stamp" tree)"
[ -n "$t1" ] || { echo "gate_stamp: clean tree printed nothing" >&2; fails=$((fails + 1)); }
want 1 "$stamp" has ci-linux "$t1"
"$stamp" mark ci-linux "$t1"
want 0 "$stamp" has ci-linux "$t1"
want 1 "$stamp" has codeql "$t1"
echo b >f # dirty: no tree, and mark records nothing
[ -z "$("$stamp" tree)" ] || { echo "gate_stamp: dirty tree printed a tree" >&2; fails=$((fails + 1)); }
"$stamp" mark codeql "$t1"
want 1 "$stamp" has codeql "$t1"
git commit -qam b # tree changed since the check started: mark records nothing
"$stamp" mark codeql "$t1"
want 1 "$stamp" has codeql "$t1"
echo u >untracked # an untracked file is dirty too
[ -z "$("$stamp" tree)" ] || { echo "gate_stamp: untracked file not dirty" >&2; fails=$((fails + 1)); }
want 1 "$stamp" has ci-linux ""
want 2 "$stamp" bogus
exit "$fails"
