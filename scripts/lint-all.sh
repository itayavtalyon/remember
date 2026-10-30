#!/usr/bin/env bash
# Lint suite for remember (project src/ + tests; not third_party amalgamation).
# Exit 0 only when every enabled gate passes.
#
# Optional env:
#   BUILD_DIR          default: build
#   REMEMBER_SCAN_BUILD=0   skip the scan-build gate (on by default, as in CI)
#   REMEMBER_IWYU=0         skip the IWYU gate (on by default when installed, as in CI)
#   REMEMBER_ALLOW_OLD_TIDY=1  accept a clang-tidy older than LLVM_PIN (it misses
#                           checks macOS CI runs; the run says so)
#   CI=true                 a missing tool fails its gate instead of skipping
#
# A local run checks what CI checks, so CI does not find a lint failure first.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${BUILD_DIR:-build}"
SRC_DIR="src"
TESTS_DIR="tests"
REPORT_DIR="lint-reports"
mkdir -p "$REPORT_DIR"

# The LLVM major the macOS CI job installs (brew install llvm@N in ci.yml). Each
# release adds clang-tidy and analyzer checks, so an older clang-tidy passes code
# that CI rejects. Raise it together with ci.yml.
LLVM_PIN=22

# Put the pinned LLVM first: Homebrew llvm@N (macOS), then the apt.llvm.org
# layout (Linux), then an unpinned Homebrew llvm.
for llvm_bin in \
    "$(brew --prefix "llvm@$LLVM_PIN" 2>/dev/null || true)/bin" \
    "/usr/lib/llvm-$LLVM_PIN/bin" \
    "$(brew --prefix llvm 2>/dev/null || true)/bin"; do
  if [[ "$llvm_bin" != "/bin" && -x "$llvm_bin/clang-tidy" ]]; then
    export PATH="$llvm_bin:$PATH"
    break
  fi
done

# Major LLVM version of a clang-tidy binary, or empty.
tidy_major() {
  "$1" --version 2>/dev/null | sed -nE 's/.*LLVM version ([0-9]+).*/\1/p' | sed -n 1p || true
}
# The pinned clang-tidy wherever it is (PATH above, a versioned name, or a pip
# --user install), else whatever clang-tidy is on PATH.
CLANG_TIDY=""
for cand in clang-tidy "clang-tidy-$LLVM_PIN" "$HOME/.local/bin/clang-tidy"; do
  path=$(command -v "$cand" || true)
  if [[ -n "$path" && "$(tidy_major "$path")" == "$LLVM_PIN" ]]; then
    CLANG_TIDY="$path"
    break
  fi
done
if [[ -z "$CLANG_TIDY" ]]; then
  CLANG_TIDY=$(command -v clang-tidy || true)
fi
CPPCHECK=$(command -v cppcheck || true)
SCAN_BUILD=$(command -v scan-build || true)
if [[ -z "$SCAN_BUILD" ]]; then
  # Ubuntu's clang-tools installs only the versioned name (scan-build-18).
  for cand in /usr/bin/scan-build-[0-9]*; do
    if [[ -x "$cand" && "$cand" =~ scan-build-[0-9]+$ ]]; then
      SCAN_BUILD="$cand"
    fi
  done
fi
CLANG_FORMAT=$(command -v clang-format || true)
IWYU=$(command -v include-what-you-use || true)
IWYU_TOOL=$(command -v iwyu_tool.py || true)

# A real GCC (not the Apple clang shim named `gcc`) for the -fanalyzer gate — a
# second, independent static analyzer alongside clang/clang-tidy/cppcheck.
GCC_ANALYZER=""
for cand in gcc-16 gcc-15 gcc-14 gcc-13 gcc; do
  path=$(command -v "$cand" || true)
  if [[ -n "$path" ]] && "$path" --version 2>/dev/null | grep -qiv clang \
     && "$path" --version 2>/dev/null | grep -qi 'gcc\|free software'; then
    GCC_ANALYZER="$path"
    break
  fi
done

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

# Fail when a src/ line matches pattern and does not match the allow ERE.
check_boundary() {
  local name="$1" pattern="$2" allow="$3" leaks
  leaks="$(grep -RIn --include='*.c' --include='*.h' -E "$pattern" "$SRC_DIR" 2>/dev/null \
    | grep -vE "$allow" || true)"
  if [[ -n "$leaks" ]]; then
    echo "$name boundary: FAIL"
    echo "$leaks"
    fail=1
  else
    echo "$name boundary: PASS"
  fi
}

echo "== no sqlite outside store_sqlite.c =="
# test_vfs.c is the X-E02 shim. It is the one other TU allowed to include sqlite.
check_boundary sqlite 'sqlite3\.h|sqlite3_' 'store_sqlite\.c|test_vfs\.c|third_party'

echo "== no error writes outside diag.c =="
# app_err() is the emitter's stream. Command code calls err_msg / diag_*.
# Raw stderr and perror bypass it too; remember_main is the one place that binds stderr.
check_boundary diag 'app_err\(|[(, ]stderr[,)]|perror\(' \
  'diag\.c|appio\.|remember_run\(argc, argv, stdout, stderr\)'

echo "== no sha256 outside normalize.c =="
# Allow REMEMBER_SHA256_HEX_LEN (public normalize API); forbid vendor symbols/includes.
check_boundary sha256 'sha256\.h|SHA256_|sha256_' 'normalize\.c|normalize\.h|REMEMBER_SHA256'

echo "== no utf8proc outside unicode.c =="
check_boundary utf8proc 'utf8proc\.h|utf8proc_|UTF8PROC_' 'unicode\.c'

echo "== no shell commands outside the sqlite3 harness helper =="
# A command string built from a path is CodeQL's "uncontrolled data used in OS
# command". Spawn with fork/execv or copy in process instead (harness.h).
shell_calls="$(grep -RIn --include='*.c' --include='*.h' -E '\b(system|popen)\(' \
  "$SRC_DIR" "$TESTS_DIR" 2>/dev/null | grep -vE '^tests/harness\.c:' || true)"
if [[ -n "$shell_calls" ]]; then
  echo "shell commands: FAIL"
  echo "$shell_calls"
  fail=1
else
  echo "shell commands: PASS"
fi

echo "== mkdtemp callers include unistd.h =="
# Darwin declares mkdtemp in unistd.h, glibc in stdlib.h. A Linux build cannot see
# a missing include, and Linux clang-tidy calls it unused; keep it under __APPLE__.
missing_unistd=""
for f in $(grep -lE '\bmkdtemp\(' "$SRC_DIR"/*.c "$TESTS_DIR"/*.c 2>/dev/null || true); do
  grep -q '^#include <unistd\.h>' "$f" || missing_unistd+="$f "
done
if [[ -n "$missing_unistd" ]]; then
  echo "mkdtemp header: FAIL (add <unistd.h>, under #ifdef __APPLE__ if unused on Linux): $missing_unistd"
  fail=1
else
  echo "mkdtemp header: PASS"
fi

echo "== cppcheck =="
if [[ -n "$CPPCHECK" ]] && [[ -d "$SRC_DIR" ]]; then
  # --enable=all, whole-program per scan (all TUs in a scan together so
  # unusedFunction/staticFunction resolve across files). Suppressed families are
  # confirmed structural FPs for this codebase shape, not real defects:
  #   unusedFunction        a single-target scan cannot see callers in the other
  #                         target, the linked GUI, or the function-pointer command
  #                         dispatch — every hit is a FP.
  #   unusedStructMember    only the deliberate `char pad_[]` tail-padding fields (see
  #                         the -Wpadded=explicit-fields policy) are ever flagged.
  #   normalCheckLevelMaxBranches / checkersReport / toomanyconfigs / missingIncludeSystem
  #                         informational notes, not defects.
  # Genuine should-be-static publics (used only via tests) carry per-line
  # `cppcheck-suppress staticFunction`; everything else is a real fix.
  cppcheck_families=(
    --suppress=missingIncludeSystem
    --suppress=unusedFunction
    --suppress=unusedStructMember
    --suppress=normalCheckLevelMaxBranches
    --suppress=checkersReport
    --suppress=toomanyconfigs
  )
  # --suppress=unmatchedSuppression: some suppressed checks (e.g. normalCheckLevelMaxBranches)
  # only exist in newer cppcheck; on the older cppcheck in CI they are unmatched, which is
  # not itself a defect. Keep one family list working across cppcheck versions.
  cppcheck_families+=(--suppress=unmatchedSuppression)
  if ! "$CPPCHECK" --enable=all \
      --error-exitcode=1 --inline-suppr \
      "${cppcheck_families[@]}" \
      -I "$SRC_DIR" \
      -I third_party/sqlite \
      -I third_party/sha256 \
      -I third_party/utf8proc \
      $(find "$SRC_DIR" -name '*.c' 2>/dev/null) \
      2>"$REPORT_DIR/cppcheck.txt"; then
    echo "cppcheck (src): FAIL"
    cat "$REPORT_DIR/cppcheck.txt" || true
    fail=1
  else
    echo "cppcheck (src): PASS"
  fi
  # tests scan, held to the same bar. cppcheck_families already carries
  # --suppress=unmatchedSuppression (the shared family list is broader than what any
  # one scan triggers); do NOT repeat it — cppcheck 2.21 errors on a duplicate suppression.
  if [[ -d "$TESTS_DIR" ]]; then
    if ! "$CPPCHECK" --enable=all \
        --error-exitcode=1 --inline-suppr \
        "${cppcheck_families[@]}" \
        -I "$SRC_DIR" \
        -I "$TESTS_DIR" \
        -I third_party/sqlite \
        -I third_party/sha256 \
        -I third_party/utf8proc \
        $(find "$TESTS_DIR" -name '*.c' 2>/dev/null) \
        2>"$REPORT_DIR/cppcheck-tests.txt"; then
      echo "cppcheck (tests): FAIL"
      cat "$REPORT_DIR/cppcheck-tests.txt" || true
      fail=1
    else
      echo "cppcheck (tests): PASS"
    fi
  fi
else
  echo "cppcheck: SKIP"
  if [[ "$in_ci" -eq 1 ]]; then
    fail=1
  fi
fi

echo "== gcc -fanalyzer =="
# Second, independent static analyzer (GCC's) over the src TUs, in addition to
# clang / clang-tidy / cppcheck. -Werror makes any -Wall/-Wextra/-Wanalyzer
# finding fatal. third_party is -isystem so vendored headers stay quiet.
if [[ -n "$GCC_ANALYZER" ]] && [[ -d "$SRC_DIR" ]]; then
  : >"$REPORT_DIR/gcc-analyzer.txt"
  an_fail=0
  while IFS= read -r f; do
    [[ -z "$f" ]] && continue
    # POSIX feature macros must match the CMake build (remember_apply_posix_feature_macros):
    # glibc hides gmtime_r et al. under pure -std=c11 without them.
    if ! "$GCC_ANALYZER" -std=c11 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
        -Wall -Wextra -Werror -fanalyzer -fsyntax-only \
        -I "$SRC_DIR" \
        -isystem third_party/sqlite \
        -isystem third_party/sha256 \
        -isystem third_party/utf8proc \
        "$f" >>"$REPORT_DIR/gcc-analyzer.txt" 2>&1; then
      an_fail=1
    fi
  done <<EOF
$(find "$SRC_DIR" -name '*.c' 2>/dev/null | sort)
EOF
  if [[ "$an_fail" -ne 0 ]]; then
    echo "gcc -fanalyzer: FAIL (see $REPORT_DIR/gcc-analyzer.txt)"
    cat "$REPORT_DIR/gcc-analyzer.txt" || true
    fail=1
  else
    echo "gcc -fanalyzer: PASS"
  fi
else
  echo "gcc -fanalyzer: SKIP"
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
  TIDY_MAJOR="$(tidy_major "$CLANG_TIDY")"
  # The distro clang-tidy (Linux CI's) when the pinned one runs on Linux. The
  # pinned build's include-cleaner has no glibc symbol map (it wants EINTR from
  # an asm/ header), so on Linux that one check runs on the distro build instead.
  CLEANER_TIDY=""
  if [[ "$(uname -s)" == "Linux" && "$TIDY_MAJOR" == "$LLVM_PIN" ]]; then
    for cand in /usr/bin/clang-tidy /usr/bin/clang-tidy-[0-9]*; do
      if [[ -x "$cand" && "$(tidy_major "$cand")" != "$LLVM_PIN" ]]; then
        CLEANER_TIDY="$cand"
        break
      fi
    done
    EXTRA_ARGS+=(--checks=-misc-include-cleaner)
  fi
  echo "clang-tidy: $CLANG_TIDY (LLVM ${TIDY_MAJOR:-?}; CI pin $LLVM_PIN)"
  if [[ -z "$TIDY_MAJOR" || "$TIDY_MAJOR" -lt "$LLVM_PIN" ]]; then
    tidy_old="clang-tidy is LLVM ${TIDY_MAJOR:-?}, older than the macOS CI pin $LLVM_PIN: CI runs checks this one lacks. Install it: brew install llvm@$LLVM_PIN (macOS) or python3 -m pip install --user 'clang-tidy==$LLVM_PIN.*' (Linux)"
    if [[ "$in_ci" -eq 1 || "${REMEMBER_ALLOW_OLD_TIDY:-0}" == "1" ]]; then
      echo "clang-tidy: WARN ($tidy_old)"
    else
      echo "clang-tidy: FAIL ($tidy_old; REMEMBER_ALLOW_OLD_TIDY=1 runs it anyway)"
      fail=1
    fi
  fi
  for f in "$SRC_DIR"/*.c tests/*.c; do
    [[ -f "$f" ]] || continue
    if ! "$CLANG_TIDY" -p "$BUILD_DIR" "${EXTRA_ARGS[@]}" "$f" >>"$REPORT_DIR/clang-tidy.txt" 2>&1; then
      tidy_fail=1
    fi
    if [[ -n "$CLEANER_TIDY" ]] &&
        ! "$CLEANER_TIDY" -p "$BUILD_DIR" --checks='-*,misc-include-cleaner' "$f" \
          >>"$REPORT_DIR/clang-tidy.txt" 2>&1; then
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

# IWYU (include-what-you-use). Layer B. On whenever installed (Linux CI runs it);
# REMEMBER_IWYU=0 skips it (macOS CI, which has no package).
echo "== include-what-you-use =="
run_iwyu=1
if [[ "${REMEMBER_IWYU:-1}" == "0" ]]; then
  run_iwyu=0
fi
if [[ "$run_iwyu" -eq 1 ]]; then
  if [[ -n "$IWYU" || -n "$IWYU_TOOL" ]]; then
    : >"$REPORT_DIR/iwyu.txt"
    iwyu_fail=0
    # Vendor headers used by src/ (PUBLIC includes of linked libs). Keep in sync
    # with CMake targets: sqlite3 → third_party/sqlite, sha256 → third_party/sha256,
    # utf8proc → third_party/utf8proc.
    # Required for the direct-iwyu fallback; also passed after -- for iwyu_tool so
    # a stale/partial compile_commands still finds sha256.h (normalize.c).
    IWYU_VENDOR_INCLUDES=(-I"$ROOT/third_party/sqlite" -I"$ROOT/third_party/sha256" -I"$ROOT/third_party/utf8proc")
    # Match CMake Linux project targets: glibc + pure -std=c11 hides POSIX
    # prototypes unless feature-test macros are visible to the IWYU clang parse.
    IWYU_POSIX_FLAGS=()
    if [[ "$(uname -s 2>/dev/null || true)" == "Linux" ]]; then
      IWYU_POSIX_FLAGS=(-D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700)
    fi
    # Prefer iwyu_tool.py over compile_commands when available.
    if [[ -n "$IWYU_TOOL" && -f "$BUILD_DIR/compile_commands.json" ]]; then
      if ! python3 "$IWYU_TOOL" -p "$BUILD_DIR" \
          $(find "$SRC_DIR" -name '*.c' | sort) \
          -- -Xiwyu --error=1 -Xiwyu --no_fwd_decls \
          "${IWYU_VENDOR_INCLUDES[@]}" \
          "${IWYU_POSIX_FLAGS[@]}" \
          >>"$REPORT_DIR/iwyu.txt" 2>&1; then
        iwyu_fail=1
      fi
    else
      for f in "$SRC_DIR"/*.c; do
        [[ -f "$f" ]] || continue
        if ! include-what-you-use -Xiwyu --error=1 -Xiwyu --no_fwd_decls \
            -I"$SRC_DIR" "${IWYU_VENDOR_INCLUDES[@]}" "${IWYU_POSIX_FLAGS[@]}" \
            -std=c11 "$f" \
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
  elif [[ "$(uname -s)" == "Linux" ]]; then
    # Linux CI runs IWYU, so a Linux run without it would miss what CI finds.
    echo "iwyu: FAIL (include-what-you-use not installed: apt-get install iwyu, or REMEMBER_IWYU=0)"
    fail=1
  else
    echo "iwyu: SKIP (include-what-you-use not installed; Linux CI runs it)"
  fi
else
  echo "iwyu: SKIP (REMEMBER_IWYU=0)"
fi

# scan-build: on by default, as in CI; REMEMBER_SCAN_BUILD=0 skips it.
echo "== scan-build =="
run_scan=1
if [[ "${REMEMBER_SCAN_BUILD:-1}" == "0" ]]; then
  run_scan=0
fi
if [[ "$run_scan" -eq 1 ]]; then
  if [[ -n "$SCAN_BUILD" ]]; then
    # Dedicated build tree for analysis. Prefer /tmp inside Docker/CI so a
    # host-mounted tree (e.g. iCloud) cannot leave undeletable "CMakeFiles 2"
    # siblings that make `rm -rf ${BUILD_DIR}-scan` fail under set -e.
    if [[ -f /.dockerenv || "${REMEMBER_CI_LINUX_INNER:-}" == "1" || "${CI:-}" == "true" ]]; then
      ANALYZE_DIR="${TMPDIR:-/tmp}/remember-scan-build-$$"
    else
      ANALYZE_DIR="${BUILD_DIR}-scan"
    fi
    rm -rf "$ANALYZE_DIR" 2>/dev/null || true
    if [[ -e "$ANALYZE_DIR" ]]; then
      # Last-ditch: unique name if a previous tree refuses to die.
      ANALYZE_DIR="${ANALYZE_DIR}-$$"
    fi
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
    # Best-effort cleanup of /tmp trees (ignore sticky host-mount leftovers).
    rm -rf "$ANALYZE_DIR" 2>/dev/null || true
  else
    echo "scan-build: FAIL (scan-build not installed: brew install llvm@$LLVM_PIN or apt-get install clang-tools; REMEMBER_SCAN_BUILD=0 skips it)"
    fail=1
  fi
else
  echo "scan-build: SKIP (REMEMBER_SCAN_BUILD=0)"
fi

if [[ "$fail" -ne 0 ]]; then
  echo "LINT FAILED"
  exit 1
fi
echo "LINT OK"
exit 0
