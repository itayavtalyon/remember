#!/usr/bin/env bash
# Print the current green --only list from tests/gate-suites (one line, no comments).
# Usage: GATE=$(./scripts/read-gate-suites.sh)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GATE_FILE="${ROOT}/tests/gate-suites"
if [[ ! -f "$GATE_FILE" ]]; then
  echo "error: missing ${GATE_FILE}" >&2
  exit 1
fi
# shellcheck disable=SC2002
suites="$(
  grep -v '^[[:space:]]*#' "$GATE_FILE" | grep -v '^[[:space:]]*$' | tr -d '[:space:]' | tr '\n' ','
)"
# trim trailing commas
suites="${suites%,}"
if [[ -z "$suites" ]]; then
  echo "error: ${GATE_FILE} has no suite list" >&2
  exit 1
fi
printf '%s\n' "$suites"
