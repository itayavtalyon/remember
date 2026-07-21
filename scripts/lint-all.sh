#!/usr/bin/env bash
# Lint suite for remember (project src/ only — not third_party).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${BUILD_DIR:-build}"
SRC_DIR="src"
REPORT_DIR="lint-reports"
mkdir -p "$REPORT_DIR"

# Prefer Homebrew LLVM tools when present
if [[ -d "$(brew --prefix llvm 2>/dev/null)/bin" ]]; then
  export PATH="$(brew --prefix llvm)/bin:$PATH"
fi

CLANG_TIDY=$(command -v clang-tidy || true)
CPPCHECK=$(command -v cppcheck || true)
SCAN_BUILD=$(command -v scan-build || true)
CLANG_FORMAT=$(command -v clang-format || true)

fail=0

echo "== format check =="
if [[ -n "$CLANG_FORMAT" ]]; then
  fmt_fail=0
  while IFS= read -r f; do
    [[ -z "$f" ]] && continue
    if ! "$CLANG_FORMAT" --dry-run -Werror "$f" >>"$REPORT_DIR/format.txt" 2>&1; then
      fmt_fail=1
    fi
  done <<EOF
$(find "$SRC_DIR" \( -name '*.c' -o -name '*.h' \) 2>/dev/null | sort)
EOF
  if [[ "$fmt_fail" -ne 0 ]]; then
    echo "format: FAIL (see $REPORT_DIR/format.txt)"
    fail=1
  else
    echo "format: PASS (or no src files)"
  fi
else
  echo "format: SKIP (clang-format missing — brew install llvm; PATH=\$(brew --prefix llvm)/bin:\$PATH)"
fi

echo "== no sqlite outside store_sqlite =="
if grep -RIn --include='*.c' --include='*.h' 'sqlite3\.h' "$SRC_DIR" 2>/dev/null \
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
fi

echo "== clang-tidy =="
if [[ -n "$CLANG_TIDY" ]] && [[ -f "$BUILD_DIR/compile_commands.json" ]]; then
  SDKROOT="$(xcrun --show-sdk-path 2>/dev/null || true)"
  EXTRA_ARGS=()
  if [[ -n "$SDKROOT" ]]; then
    EXTRA_ARGS+=(--extra-arg=-isysroot"--extra-arg=$SDKROOT")
    # split properly:
    EXTRA_ARGS=(--extra-arg=-isysroot --extra-arg="$SDKROOT")
  fi
  : >"$REPORT_DIR/clang-tidy.txt"
  tidy_fail=0
  # Portable find loop (no process substitution for bash 3)
  for f in "$SRC_DIR"/*.c; do
    [[ -f "$f" ]] || continue
    if ! "$CLANG_TIDY" -p "$BUILD_DIR" "${EXTRA_ARGS[@]}" "$f" >>"$REPORT_DIR/clang-tidy.txt" 2>&1; then
      tidy_fail=1
    fi
  done
  if [[ "$tidy_fail" -ne 0 ]]; then
    echo "clang-tidy: FAIL (see $REPORT_DIR/clang-tidy.txt)"
    fail=1
  else
    echo "clang-tidy: PASS (or no files)"
  fi
else
  echo "clang-tidy: SKIP (need compile_commands.json + clang-tidy)"
fi

echo "== scan-build (optional quick) =="
if [[ -n "$SCAN_BUILD" ]]; then
  echo "scan-build: available (run manually: scan-build cmake --build $BUILD_DIR)"
else
  echo "scan-build: SKIP"
fi

if [[ "$fail" -ne 0 ]]; then
  echo "LINT FAILED"
  exit 1
fi
echo "LINT OK"
exit 0
