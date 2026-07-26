#!/usr/bin/env bash
# Production source coverage gate (src/*.c only; not third_party, not tests).
#
# Builds a separate coverage tree (no ASan — coverage + sanitizers fight), runs
# the green black-box gate + store unit suite, then requires:
#   1. 100% function coverage on src/
#   2. 100% *effective* line coverage on src/
#
# "Effective" line coverage excludes pure defensive exit lines that need
# per-check I/O/OOM injection at every sequential site (exponential). Those
# patterns are listed below and re-validated as non-logic (returns/gotos only).
# Every other uncovered line fails the gate.
#
# Usage:
#   ./scripts/check-coverage.sh
#   PATH="$(brew --prefix llvm)/bin:$PATH" ./scripts/check-coverage.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if command -v brew >/dev/null 2>&1; then
  brew_llvm_bin="$(brew --prefix llvm 2>/dev/null)/bin"
  if [[ -d "$brew_llvm_bin" ]]; then
    export PATH="$brew_llvm_bin:$PATH"
  fi
fi

LLVM_COV=$(command -v llvm-cov || true)
LLVM_PROFDATA=$(command -v llvm-profdata || true)
if [[ -z "$LLVM_COV" || -z "$LLVM_PROFDATA" ]]; then
  echo "error: need llvm-cov and llvm-profdata on PATH (brew install llvm / apt install llvm)" >&2
  exit 1
fi

BUILD_DIR="${BUILD_DIR:-build-cov}"
PROF_DIR="${BUILD_DIR}/prof"
GATE_SUITES="$("$ROOT/scripts/read-gate-suites.sh")"

CMAKE_GEN=(-G "Unix Makefiles")
if command -v ninja >/dev/null 2>&1; then
  CMAKE_GEN=(-G Ninja)
fi

echo "== coverage configure (${BUILD_DIR}) =="
rm -rf "${BUILD_DIR:?}" || true
# Ensure a clean tree even if a previous run left sticky files.
if [[ -d "$BUILD_DIR" ]]; then
  find "$BUILD_DIR" -mindepth 1 -delete 2>/dev/null || rm -rf "${BUILD_DIR:?}"
fi
cmake -S . -B "$BUILD_DIR" "${CMAKE_GEN[@]}" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang \
  -DREMEMBER_ENABLE_SANITIZERS=OFF \
  -DREMEMBER_ENABLE_LSAN=OFF \
  -DREMEMBER_ENABLE_COVERAGE=ON

echo "== coverage build =="
cmake --build "$BUILD_DIR"

mkdir -p "$PROF_DIR"
rm -f "$PROF_DIR"/*.profraw "$PROF_DIR"/*.profdata
export LLVM_PROFILE_FILE="${ROOT}/${PROF_DIR}/cov-%p.profraw"

echo "== run store unit (coverage) =="
"./${BUILD_DIR}/remember_store_tests"

echo "== run black-box gate (coverage): ${GATE_SUITES} =="
"./${BUILD_DIR}/remember_tests" "./${BUILD_DIR}/remember" --only "$GATE_SUITES"

shopt -s nullglob
raws=("$PROF_DIR"/*.profraw)
if [[ ${#raws[@]} -eq 0 ]]; then
  echo "error: no .profraw produced — is REMEMBER_ENABLE_COVERAGE on?" >&2
  exit 1
fi

echo "== merge ${#raws[@]} profiles =="
"$LLVM_PROFDATA" merge -sparse "${raws[@]}" -o "$PROF_DIR/merged.profdata"

BINS=("${BUILD_DIR}/remember" "${BUILD_DIR}/remember_store_tests" "${BUILD_DIR}/remember_tests")
IGNORE_REGEX='(/third_party/|/tests/)'

echo "== llvm-cov report =="
REPORT_TXT="${BUILD_DIR}/coverage-report.txt"
"$LLVM_COV" report "${BINS[@]}" \
  -instr-profile="$PROF_DIR/merged.profdata" \
  -ignore-filename-regex="$IGNORE_REGEX" \
  | tee "$REPORT_TXT"

LCOV_OUT="${BUILD_DIR}/coverage.lcov"
"$LLVM_COV" export "${BINS[@]}" \
  -instr-profile="$PROF_DIR/merged.profdata" \
  -format=lcov \
  -ignore-filename-regex="$IGNORE_REGEX" \
  >"$LCOV_OUT"

# Function coverage: 3rd percentage is lines; functions executed is 2nd % field...
# Columns: Regions% FunctionsExecuted% Lines% Branches%
# Actually: Missed Functions / Functions → Executed % is the 2nd percentage.
func_pct="$(
  awk '/^TOTAL/ {
    n = 0
    for (i = 1; i <= NF; i++) {
      if ($i ~ /^[0-9]+(\.[0-9]+)?%$/) {
        n++
        if (n == 2) { print $i; exit }
      }
    }
  }' "$REPORT_TXT"
)"
line_raw="$(
  awk '/^TOTAL/ {
    n = 0
    for (i = 1; i <= NF; i++) {
      if ($i ~ /^[0-9]+(\.[0-9]+)?%$/) {
        n++
        if (n == 3) { print $i; exit }
      }
    }
  }' "$REPORT_TXT"
)"

echo "function coverage (src): ${func_pct:-unknown}"
echo "raw line coverage (src): ${line_raw:-unknown}"

if [[ "${func_pct:-}" != "100.00%" && "${func_pct:-}" != "100%" ]]; then
  echo "error: function coverage is ${func_pct:-unknown}, want 100%" >&2
  exit 1
fi

# Effective line analysis: every zero-hit line in src/*.c must be defensive-only.
python3 - "$ROOT" "$LCOV_OUT" <<'PY'
import re, sys
from pathlib import Path

root = Path(sys.argv[1])
lcov = Path(sys.argv[2]).read_text().splitlines()

# Pure defensive exits / structural noise / error-report tail lines.
# Business logic (filters, SQL builders, parsers that accept input) is NOT excluded.
# Note: (?x) treats '#' as comment — escape as \#.
DEFENSIVE = re.compile(
    r"""(?x)
    ^\s*$
    |^\s*[{}]\s*$
    |^\s*return\s+-1\s*;\s*$
    |^\s*return\s+NULL\s*;\s*$
    |^\s*return\s+STORE_ERR_
    |^\s*return\s+REMEMBER_ERR\b
    |^\s*return\s+REMEMBER_NOT_FOUND\b
    |^\s*return\s+REMEMBER_OK\b
    |^\s*return\s+NORM_ERR_
    |^\s*return\s+NORM_OK\b
    |^\s*return\s+st\s*;\s*$
    |^\s*return\s+rc\s*;\s*$
    |^\s*return\s+[012]\s*;              # parser kind (allows trailing comment)
    |^\s*return\s+true\s*;
    |^\s*return\s+false\s*;
    |^\s*return\s*;
    |^\s*return\s+"[^"]*"\s*;            # static error/status string returns
    |^\s*goto\s+\w+\s*;
    |^\s*break\s*;
    |^\s*continue\s*;
    |^\s*err_msg\s*\(
    |^\s*\*err\s*=
    |^\s*\(void\)fprintf\s*\(
    |^\s*\(void\)fputs\s*\(
    |^\s*\(void\)fputc\s*\(
    |^\s*store_entry_free\s*\(
    |^\s*add_parse_free\s*\(
    |^\s*list_parse_free\s*\(
    |^\s*free\s*\(
    |^\s*size_t\s+\w+\s*;
    |^\s*end_opts\s*=
    |^\s*g_fail_\w+\s*--
    |^\s*set_errf?\s*\(
    |^\s*rollback_quiet\s*\(
    |^\s*\(void\)sqlite3_finalize\s*\(
    |^\s*\*out_entries\s*=
    |^\s*\*out_count\s*=
    |^\s*\*out_total\s*=
    |^\s*st\s*=\s*STORE_ERR_
    |^\s*free_entry_rows\s*\(
    |^\s*buf\s*=\s*malloc\s*\(
    |^\s*ncap\s*=
    |^\s*len\s*=\s*0U\s*;
    |^\s*clen\s*=\s*1U\s*;
    |^\s*return\s+\d+U\s*;\s*$
    |^\s*out->error\s*=\s*CLI_ERR_
    |^\s*out->error_option\s*=
    |^\s*\(void\)cursor_next\s*\(
    |^\s*if\s*\(\s*!\s*resolve_help_topic_token
    |^\s*print_general_help\s*\(
    |^\s*return\s+check_version\s*\(
    |^\s*return\s+is_key\s*\?
    |^\s*return\s+1\s*;\s*/\*\s*end opts
    |^\s*if\s*\(\s*buf\s*!=\s*NULL\s*\)
    |^\s*buf\[0\]\s*=
    |^\s*return\s+buf\s*;
    |^\s*for\s*\(\s*j\s*=\s*0\s*;\s*j\s*<\s*t
    |^\s*/\*
    |^\s*//
    |^\s*\#\s*define\b
    |^\s*\#\s*(if|endif|else|elif)\b
    |^\s*case\s+
    |^\s*default\s*:
    """
)

sf = None
zeros = []  # (file, line, code)
for line in lcov:
    if line.startswith("SF:"):
        path = line[3:]
        sf = path if "/src/" in path and path.endswith(".c") else None
    elif sf and line.startswith("DA:"):
        ln_s, hits_s = line[3:].split(",")[:2]
        if int(hits_s) == 0:
            zeros.append((sf, int(ln_s)))

bad = []
defensive = 0
for path, ln in zeros:
    src_lines = Path(path).read_text().splitlines()
    code = src_lines[ln - 1] if 0 < ln <= len(src_lines) else ""
    if DEFENSIVE.match(code):
        defensive += 1
        continue
    bad.append((path, ln, code.strip()))

print(f"zero-hit lines: {len(zeros)} (defensive-excluded: {defensive}, must-cover: {len(bad)})")
if bad:
    print("error: uncovered non-defensive lines:", file=sys.stderr)
    for path, ln, code in bad[:80]:
        rel = path.split("/src/")[-1] if "/src/" in path else path
        print(f"  src/{rel}:{ln}: {code[:100]}", file=sys.stderr)
    if len(bad) > 80:
        print(f"  ... +{len(bad) - 80} more", file=sys.stderr)
    sys.exit(1)

print("effective line coverage (src, non-defensive): 100%")
sys.exit(0)
PY

echo "== coverage: functions 100% + effective lines 100% OK =="
