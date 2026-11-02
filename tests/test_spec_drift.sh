#!/usr/bin/env bash
# scripts/spec-drift.sh: src/ without specs/ fails unless the PR body says why.
set -uo pipefail
check="$(dirname "$0")/../scripts/spec-drift.sh"
fails=0

expect() { # expect <exit> <paths> <body>
  local got
  printf '%s\n' "$2" | PR_BODY="$3" bash "$check" 2>/dev/null
  got=$?
  if [ "$got" != "$1" ]; then
    echo "spec_drift: paths [$2] body [$3]: exit $got, want $1" >&2
    fails=$((fails + 1))
  fi
}

expect 0 'docs/a.md' ''
expect 0 'tests/test_add.c' ''
expect 1 'src/a.c' ''
expect 1 $'src/a.c\ntests/b.c' 'Refactor.'
expect 0 $'src/a.c\nspecs/JSON-STYLE.md' ''
expect 0 'src/a.c' $'Summary\r\nNo spec change: rename only\r\n'
expect 1 'src/a.c' 'No spec change:'
expect 1 'src/a.c' $'No spec change:\r\n'
expect 1 'src/a.c' 'No spec change:   '
expect 1 'src/a.c' '  No spec change: indented'
expect 0 'lib/src/a.c' ''  # a nested src/ is not src/
exit "$fails"
