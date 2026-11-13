#!/usr/bin/env bash
# Local CodeQL: the same scan as .github/workflows/codeql.yml (default
# code-scanning suite, plain clang Debug build, no sanitizers), failing at the
# main ruleset's thresholds: any error-level alert, or security severity high
# or above (>= 7.0). Run it before pushing a C change; CI's CodeQL blocks the
# merge on the same alerts.
#
# Usage: scripts/codeql-local.sh            (needs `brew install --cask codeql`)
# CODEQL_RAM (MB, default 4096) caps the extractor and the queries.
set -euo pipefail

if ! command -v codeql >/dev/null 2>&1; then
  echo "codeql-local: FAIL (codeql not on PATH; brew install --cask codeql)" >&2
  exit 1
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/remember-codeql.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
RAM="${CODEQL_RAM:-4096}"

cmake -S "$ROOT" -B "$WORK/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang \
  -DREMEMBER_ENABLE_SANITIZERS=OFF \
  -DREMEMBER_ENABLE_LSAN=OFF >/dev/null

codeql database create "$WORK/db" --language=c-cpp --source-root="$ROOT" \
  -q --ram="$RAM" --command="cmake --build $WORK/build" --overwrite >/dev/null

codeql database analyze "$WORK/db" \
  codeql/cpp-queries:codeql-suites/cpp-code-scanning.qls \
  -q --download --ram="$RAM" --format=sarif-latest --output="$WORK/out.sarif" >/dev/null

# Every alert as "severity<TAB>rule<TAB>path:line<TAB>message"; the level and
# security severity come from the rule when the result does not carry them.
jq -r '
  .runs[] | (.tool.driver.rules // []) as $rules | .results[]
  | ($rules[.rule.index // -1] // {}) as $r
  | (.level // $r.defaultConfiguration.level // "warning") as $level
  | ($r.properties["security-severity"] // "0" | tonumber) as $sec
  | [(if $level == "error" or $sec >= 7.0 then "BLOCK" else "note" end),
     .ruleId,
     (.locations[0].physicalLocation | "\(.artifactLocation.uri):\(.region.startLine)"),
     .message.text]
  | @tsv' "$WORK/out.sarif" | sort >"$WORK/alerts.tsv"

cat "$WORK/alerts.tsv"
if grep -q '^BLOCK' "$WORK/alerts.tsv"; then
  echo "codeql-local: FAIL ($(grep -c '^BLOCK' "$WORK/alerts.tsv") alert(s) would block the merge)" >&2
  exit 1
fi
echo "codeql-local: OK ($(wc -l <"$WORK/alerts.tsv" | tr -d ' ') non-blocking)"
