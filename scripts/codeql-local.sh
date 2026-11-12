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

# One line per alert: "BLOCK|allowed<TAB>rule<TAB>path:line<TAB>message". An
# allow entry matches on rule and path prefix, and on its text appearing in
# EVERY input source the message names (or in the message, when it names none).
jq -r --rawfile allow "$ROOT/scripts/codeql-allow.tsv" '
  ($allow | split("\n") | map(select(length > 0 and (startswith("#") | not)) | split("\t"))) as $rows
  | .runs[].results[]
  | (.locations[0].physicalLocation | "\(.artifactLocation.uri):\(.region.startLine)") as $loc
  | .ruleId as $rule | .message.text as $msg
  | ([$msg | scan("\\[([^\\]]*)\\]") | .[0]]) as $srcs
  | (any($rows[]; . as $r |
      $r[0] == $rule and ($loc | startswith($r[1])) and
      (if ($srcs | length) == 0 then ($msg | contains($r[2]))
       else all($srcs[]; contains($r[2])) end))) as $ok
  | [(if $ok then "allowed" else "BLOCK" end), $rule, $loc, ($msg | gsub("\n"; " "))]
  | @tsv' "$WORK/out.sarif" | sort >"$WORK/alerts.tsv"

grep '^BLOCK' "$WORK/alerts.tsv" || true
blocked=$(grep -c '^BLOCK' "$WORK/alerts.tsv" || true)
allowed=$(grep -c '^allowed' "$WORK/alerts.tsv" || true)
if [[ "$blocked" != 0 ]]; then
  echo "codeql-local: FAIL ($blocked alert(s); fix them, or allow a reviewed one in scripts/codeql-allow.tsv)" >&2
  exit 1
fi
echo "codeql-local: OK ($allowed allowed by scripts/codeql-allow.tsv)"
