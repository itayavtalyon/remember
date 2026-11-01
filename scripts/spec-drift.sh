#!/usr/bin/env bash
# Reads changed paths on stdin, one per line, and the PR body from $PR_BODY.
# Exits 1 when a path under src/ changed and none under specs/ did, unless a
# body line starts with "No spec change:" and gives a reason. The spec ships
# with the code (specs/README.md). Used by .github/workflows/spec-drift.yml.
paths=$(cat)
grep -q '^src/' <<<"$paths" || exit 0
grep -q '^specs/' <<<"$paths" && exit 0
tr -d '\r' <<<"${PR_BODY-}" | grep -qE '^No spec change: *[^ ]' && exit 0
echo "spec-drift: src/ changed and specs/ did not. Edit the spec this change affects, or add a PR body line 'No spec change: <reason>'." >&2
exit 1
