#!/usr/bin/env bash
# Lint suite for remember (project src/ + tests; not third_party amalgamation).
# Exit 0 only when every enabled gate passes.
#
# Optional env:
#   BUILD_DIR          default: build
#   REMEMBER_SCAN_BUILD=1   force scan-build gate when scan-build is installed
#   REMEMBER_IWYU=1         force IWYU gate when include-what-you-use is installed
#   CI=true                 enables scan-build + IWYU when tools exist
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${BUILD_DIR:-build}"
SRC_DIR="src"
REPORT_DIR="lint-reports"
mkdir -p "$REPORT_DIR"

# Prefer Homebrew LLVM tools when present (macOS). Skip quietly on Linux CI.
if command -v brew >/dev/null 2>&1; then
  brew_llvm_bin="$(brew --prefix llvm 2>/dev/null)/bin"
  if [[ -d "$brew_llvm_bin" ]]; then
    export PATH="$brew_llvm_bin:$PATH"
  fi
fi

CLANG_TIDY=$(command -v clang-tidy || true)
CPPCHECK=$(command -v cppcheck || true)
SCAN_BUILD=$(command -v scan-build || true)
CLANG_FORMAT=$(command -v clang-format || true)
IWYU=$(command -v include-what-you-use || true)
IWYU_TOOL=$(command -v iwyu_tool.py || true)

fail=0
in_ci=0
if [[ "${CI:-}" == "true" || "${GITHUB_ACTIONS:-}" == "true" ]]; then
  in_ci=1
fi

echo "== format check =="
if [[ -n "$CLANG_FORMAT" ]]; then
  : >"$REPORT_DIR/format.txt"
  fmt_fail=0
  while IFS= read -r f; do
    [[ -z "$f" ]] && continue
    if ! "$CLANG_FORMAT" --dry-run -Werror "$f" >>"$REPORT_DIR/format.txt" 2>&1; then
      fmt_fail=1
    fi
  done <<EOF
$(find "$SRC_DIR" tests \( -name '*.c' -o -name '*.h' \) 2>/dev/null | sort)
EOF
  if [[ "$fmt_fail" -ne 0 ]]; then
    echo "format: FAIL (see $REPORT_DIR/format.txt)"
    fail=1
  else
    echo "format: PASS"
  fi
else
  echo "format: SKIP (clang-format missing — brew install llvm; PATH=\$(brew --prefix llvm)/bin:\$PATH)"
  if [[ "$in_ci" -eq 1 ]]; then
    fail=1
  fi
fi

echo "== no sqlite outside store_sqlite.c =="
if grep -RIn --include='*.c' --include='*.h' -E 'sqlite3\.h|sqlite3_' "$SRC_DIR" 2>/dev/null \
  | grep -v 'store_sqlite\.c' | grep -v 'third_party' >/tmp/remember-sqlite-leak.txt 2>/dev/null; then
  if [[ -s /tmp/remember-sqlite-leak.txt ]]; then
    echo "sqlite leak: FAIL"
    cat /tmp/remember-sqlite-leak.txt
    fail=1
  else
    echo "sqlite boundary: PASS"
  fi
else
  echo "sqlite boundary: PASS (no leaks)"
fi

echo "== cppcheck =="
if [[ -n "$CPPCHECK" ]] && [[ -d "$SRC_DIR" ]]; then
  if ! "$CPPCHECK" --enable=warning,style,performance,portability \
      --error-exitcode=1 --inline-suppr \
      --suppress=missingIncludeSystem \
      -I "$SRC_DIR" \
      $(find "$SRC_DIR" -name '*.c' 2>/dev/null) \
      2>"$REPORT_DIR/cppcheck.txt"; then
    echo "cppcheck: FAIL"
    cat "$REPORT_DIR/cppcheck.txt" || true
    fail=1
  else
    echo "cppcheck: PASS"
  fi
else
  echo "cppcheck: SKIP"
  if [[ "$in_ci" -eq 1 ]]; then
    fail=1
  fi
fi

echo "== clang-tidy =="
if [[ -n "$CLANG_TIDY" ]] && [[ -f "$BUILD_DIR/compile_commands.json" ]]; then
  SDKROOT="$(xcrun --show-sdk-path 2>/dev/null || true)"
  EXTRA_ARGS=()
  if [[ -n "$SDKROOT" ]]; then
    EXTRA_ARGS=(--extra-arg=-isysroot --extra-arg="$SDKROOT")
  fi
  : >"$REPORT_DIR/clang-tidy.txt"
  tidy_fail=0
  for f in "$SRC_DIR"/*.c tests/*.c; do
    [[ -f "$f" ]] || continue
    if ! "$CLANG_TIDY" -p "$BUILD_DIR" "${EXTRA_ARGS[@]}" "$f" >>"$REPORT_DIR/clang-tidy.txt" 2>&1; then
      tidy_fail=1
    fi
  done
  if [[ "$tidy_fail" -ne 0 ]]; then
    echo "clang-tidy: FAIL (see $REPORT_DIR/clang-tidy.txt)"
    # Show non-suppressed lines for triage.
    grep -E 'error:|warning:' "$REPORT_DIR/clang-tidy.txt" | head -50 || true
    fail=1
  else
    echo "clang-tidy: PASS"
  fi
else
  echo "clang-tidy: SKIP (need compile_commands.json + clang-tidy)"
  if [[ "$in_ci" -eq 1 ]]; then
    fail=1
  fi
fi

# IWYU (include-what-you-use). Layer B. Optional locally; on in CI when tool present
# or when REMEMBER_IWYU=1.
echo "== include-what-you-use =="
run_iwyu=0
if [[ "${REMEMBER_IWYU:-0}" == "1" || "$in_ci" -eq 1 ]]; then
  run_iwyu=1
fi
if [[ "$run_iwyu" -eq 1 ]]; then
  if [[ -n "$IWYU" || -n "$IWYU_TOOL" ]]; then
    : >"$REPORT_DIR/iwyu.txt"
    iwyu_fail=0
    # Prefer iwyu_tool.py over compile_commands when available.
    if [[ -n "$IWYU_TOOL" && -f "$BUILD_DIR/compile_commands.json" ]]; then
      if ! python3 "$IWYU_TOOL" -p "$BUILD_DIR" \
          $(find "$SRC_DIR" -name '*.c' | sort) \
          -- -Xiwyu --error=1 -Xiwyu --no_fwd_decls \
          >>"$REPORT_DIR/iwyu.txt" 2>&1; then
        iwyu_fail=1
      fi
    else
      for f in "$SRC_DIR"/*.c; do
        [[ -f "$f" ]] || continue
        if ! include-what-you-use -Xiwyu --error=1 -Xiwyu --no_fwd_decls \
            -I"$SRC_DIR" -Ithird_party/sqlite -std=c11 "$f" \
            >>"$REPORT_DIR/iwyu.txt" 2>&1; then
          iwyu_fail=1
        fi
      done
    fi
    if [[ "$iwyu_fail" -ne 0 ]]; then
      echo "iwyu: FAIL (see $REPORT_DIR/iwyu.txt)"
      tail -80 "$REPORT_DIR/iwyu.txt" || true
      fail=1
    else
      echo "iwyu: PASS"
    fi
  else
    echo "iwyu: SKIP (include-what-you-use not installed)"
    if [[ "$in_ci" -eq 1 ]]; then
      # Soft on CI until package is in the image; prefer clang-tidy include-cleaner.
      echo "iwyu: WARN (install iwyu for full Layer B; misc-include-cleaner covers much of this)"
    fi
  fi
else
  echo "iwyu: SKIP (set REMEMBER_IWYU=1 or run in CI)"
fi

# scan-build: real gate when REMEMBER_SCAN_BUILD=1 or CI=true and tool exists.
echo "== scan-build =="
run_scan=0
if [[ "${REMEMBER_SCAN_BUILD:-0}" == "1" || "$in_ci" -eq 1 ]]; then
  run_scan=1
fi
if [[ "$run_scan" -eq 1 ]]; then
  if [[ -n "$SCAN_BUILD" ]]; then
    # Analyze project TUs only (skip amalgamation noise) via a dedicated build tree.
    ANALYZE_DIR="${BUILD_DIR}-scan"
    rm -rf "$ANALYZE_DIR"
    if ! "$SCAN_BUILD" --status-bugs -o "$REPORT_DIR/scan-build" \
        cmake -S . -B "$ANALYZE_DIR" \
        -DCMAKE_C_COMPILER=clang \
        -DREMEMBER_ENABLE_SANITIZERS=OFF \
        >"$REPORT_DIR/scan-build-configure.txt" 2>&1; then
      echo "scan-build configure: FAIL"
      cat "$REPORT_DIR/scan-build-configure.txt" || true
      fail=1
    elif ! "$SCAN_BUILD" --status-bugs -o "$REPORT_DIR/scan-build" \
        cmake --build "$ANALYZE_DIR" \
        >"$REPORT_DIR/scan-build-build.txt" 2>&1; then
      echo "scan-build: FAIL (bugs reported; see $REPORT_DIR/scan-build)"
      tail -40 "$REPORT_DIR/scan-build-build.txt" || true
      fail=1
    else
      echo "scan-build: PASS"
    fi
  else
    echo "scan-build: SKIP (scan-build not installed)"
    if [[ "$in_ci" -eq 1 ]]; then
      fail=1
    fi
  fi
else
  if [[ -n "$SCAN_BUILD" ]]; then
    echo "scan-build: available (enable with REMEMBER_SCAN_BUILD=1 or CI=true)"
  else
    echo "scan-build: SKIP"
  fi
fi

if [[ "$fail" -ne 0 ]]; then
  echo "LINT FAILED"
  exit 1
fi
echo "LINT OK"
exit 0
