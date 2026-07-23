#!/usr/bin/env bash
# macOS heuristic leak gate using leaks(1).
# Usage: scripts/run-leaks-macos.sh /path/to/remember_plain
#
# Must NOT be an ASan-instrumented binary — leaks cannot inspect ASan heaps.
# Prefer CMake target: cmake --build build --target leaks-macos
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "run-leaks-macos: skip (not macOS)"
  exit 0
fi

REMEMBER_BIN="${1:-}"
if [[ -z "$REMEMBER_BIN" || ! -x "$REMEMBER_BIN" ]]; then
  echo "usage: $0 /path/to/remember_plain" >&2
  exit 2
fi

if ! command -v leaks >/dev/null 2>&1; then
  echo "run-leaks-macos: SKIP (leaks(1) not found)"
  exit 0
fi

# Refuse obviously sanitized binaries (dyld inserts ASan runtime).
if nm -gU "$REMEMBER_BIN" 2>/dev/null | grep -q '__asan\|___asan'; then
  echo "run-leaks-macos: FAIL — binary looks ASan-instrumented; use remember_plain" >&2
  exit 1
fi

export MallocStackLogging=1

fail=0
run_one() {
  local name="$1"
  shift
  echo "== leaks: $name ($*) =="
  # --atExit: report after clean exit. We only pass paths that exit 0.
  if ! leaks --atExit -- "$REMEMBER_BIN" "$@" >/tmp/remember-leaks-out.txt 2>/tmp/remember-leaks-err.txt; then
    echo "leaks: FAIL ($name)" >&2
    cat /tmp/remember-leaks-err.txt >&2 || true
    fail=1
    return 0
  fi
  # leaks prints "0 leaks for 0 total leaked bytes" on success; also exits 0.
  if grep -qE 'leaks for .* total leaked bytes' /tmp/remember-leaks-out.txt 2>/dev/null; then
    if ! grep -qE '^0 leaks for' /tmp/remember-leaks-out.txt 2>/dev/null \
       && ! grep -qE '0 leaks for 0 total leaked bytes' /tmp/remember-leaks-out.txt 2>/dev/null; then
      # Some macOS versions phrase the summary differently; treat non-zero counts as fail.
      if grep -qE '^[1-9][0-9]* leaks' /tmp/remember-leaks-out.txt 2>/dev/null; then
        echo "leaks: FAIL ($name) — non-zero leaks" >&2
        cat /tmp/remember-leaks-out.txt >&2 || true
        fail=1
      fi
    fi
  fi
  echo "leaks: ok ($name)"
}

run_one version --version
run_one help --help

if [[ "$fail" -ne 0 ]]; then
  echo "leaks-macos: FAIL"
  exit 1
fi
echo "leaks-macos: PASS"
exit 0
