#!/usr/bin/env bash
# Local CodeQL, stricter than CI's: the security-extended suite (CI runs the
# default code-scanning suite), and ANY alert fails unless
# scripts/codeql-allow.tsv lets it through. Same plain clang Debug build as
# .github/workflows/codeql.yml. Vendored third_party/ is built before tracing
# starts, so it never enters the database.
#
# Usage: scripts/codeql-local.sh            (needs `brew install --cask codeql`)
# CODEQL_RAM (MB, default 4096) caps the extractor and the queries.
set -euo pipefail

if ! command -v codeql >/dev/null 2>&1; then
  echo "codeql-local: FAIL (codeql not on PATH; brew install --cask codeql)" >&2
  exit 1
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# The allow filter checks itself first: known alerts against the real allowlist.
if ! jq -r --rawfile allow "$ROOT/scripts/codeql-allow.tsv" -f "$ROOT/scripts/codeql-allow.jq" \
  "$ROOT/tests/codeql-allow/alerts.sarif" | cut -f1,3 | diff -u "$ROOT/tests/codeql-allow/expected.tsv" - >&2; then
  echo "codeql-local: FAIL (scripts/codeql-allow.jq or .tsv no longer gives tests/codeql-allow/expected.tsv)" >&2
  exit 1
fi
WORK="$(mktemp -d "${TMPDIR:-/tmp}/remember-codeql.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
RAM="${CODEQL_RAM:-4096}"

cmake -S "$ROOT" -B "$WORK/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang \
  -DREMEMBER_ENABLE_SANITIZERS=OFF \
  -DREMEMBER_ENABLE_LSAN=OFF >/dev/null
cmake --build "$WORK/build" --target sqlite3 sha256 utf8proc >/dev/null

codeql database create "$WORK/db" --language=c-cpp --source-root="$ROOT" \
  -q --ram="$RAM" --command="cmake --build $WORK/build" --overwrite >/dev/null

codeql database analyze "$WORK/db" \
  codeql/cpp-queries:codeql-suites/cpp-security-extended.qls \
  -q --download --ram="$RAM" --format=sarif-latest --output="$WORK/out.sarif" >/dev/null

jq -r --rawfile allow "$ROOT/scripts/codeql-allow.tsv" -f "$ROOT/scripts/codeql-allow.jq" \
  "$WORK/out.sarif" | sort >"$WORK/alerts.tsv"

grep '^BLOCK' "$WORK/alerts.tsv" || true
blocked=$(grep -c '^BLOCK' "$WORK/alerts.tsv" || true)
allowed=$(grep -c '^allowed' "$WORK/alerts.tsv" || true)
if [[ "$blocked" != 0 ]]; then
  echo "codeql-local: FAIL ($blocked alert(s); fix them, or allow a reviewed one in scripts/codeql-allow.tsv)" >&2
  exit 1
fi
echo "codeql-local: OK ($allowed allowed by scripts/codeql-allow.tsv)"
