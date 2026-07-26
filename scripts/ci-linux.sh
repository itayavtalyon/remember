#!/usr/bin/env bash
# Linux CI gate (same shape as .github/workflows/ci.yml linux job).
#
# One-liner from the host (Docker method A):
#   ./scripts/ci-linux.sh
#
# Re-runs itself inside ubuntu:24.04 with the tree mounted at /src.
# On GitHub Actions / already-Linux hosts (packages preinstalled):
#   ./scripts/ci-linux.sh --inner
# With apt install inside the script (fresh container without Dockerfile):
#   ./scripts/ci-linux.sh --inner --install
#
# Always runs the *current* green suite list from tests/gate-suites (not a
# hard-coded step-01 filter), plus store_asan_gate and full lint.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

INNER=0
DO_INSTALL=0
for arg in "$@"; do
  case "$arg" in
    --inner) INNER=1 ;;
    --install) DO_INSTALL=1 ;;
    -h | --help)
      sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      echo "usage: $0 [--inner] [--install]" >&2
      exit 2
      ;;
  esac
done

# Host entry: re-exec in Docker (method A).
if [[ "$INNER" -eq 0 ]]; then
  if ! command -v docker >/dev/null 2>&1; then
    echo "error: docker not found; install Docker or run: $0 --inner --install" >&2
    exit 1
  fi
  echo "== ci-linux: launching ubuntu:24.04 =="
  exec docker run --rm \
    -v "$ROOT":/src:rw \
    -w /src \
    -e REMEMBER_CI_LINUX_INNER=1 \
    ubuntu:24.04 \
    bash /src/scripts/ci-linux.sh --inner --install
fi

# --- inside Linux (container or CI runner) -----------------------------------

export DEBIAN_FRONTEND=noninteractive
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1:abort_on_error=1:allocator_may_return_null=1}"

if [[ "$DO_INSTALL" -eq 1 ]]; then
  echo "== install toolchain =="
  # Root in Docker method A; passwordless sudo on GHA runners.
  if [[ "$(id -u)" -eq 0 ]]; then
    apt-get update
    apt-get install -y \
      build-essential cmake ninja-build clang clang-tidy clang-tools \
      clang-format cppcheck sqlite3 \
      iwyu python3 \
      llvm
  else
    sudo DEBIAN_FRONTEND=noninteractive apt-get update
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
      build-essential cmake ninja-build clang clang-tidy clang-tools \
      clang-format cppcheck sqlite3 \
      iwyu python3 \
      llvm
  fi
fi

GATE_SUITES="$("$ROOT/scripts/read-gate-suites.sh")"
echo "== gate suites: ${GATE_SUITES} =="

# Never reuse a host macOS CMakeCache when running under Docker (mounted tree).
if [[ -f /.dockerenv || "${REMEMBER_CI_LINUX_INNER:-}" == "1" ]]; then
  BUILD_DIR="${BUILD_DIR:-build-linux-ci}"
  # Drop a stale cache if it still points at the host path.
  if [[ -f "$BUILD_DIR/CMakeCache.txt" ]] &&
    grep -q 'CMAKE_HOME_DIRECTORY:INTERNAL=/Users/' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null; then
    rm -rf "$BUILD_DIR"
  fi
else
  BUILD_DIR="${BUILD_DIR:-build}"
fi

CMAKE_GEN=(-G "Unix Makefiles")
if command -v ninja >/dev/null 2>&1; then
  CMAKE_GEN=(-G Ninja)
fi
echo "== configure (${BUILD_DIR}) =="
# Fresh configure each run inside Docker so generator/paths stay consistent.
if [[ -f /.dockerenv || "${REMEMBER_CI_LINUX_INNER:-}" == "1" ]]; then
  rm -rf "${BUILD_DIR:?}"
fi
cmake -S . -B "$BUILD_DIR" "${CMAKE_GEN[@]}" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang \
  -DREMEMBER_ENABLE_SANITIZERS=ON \
  -DREMEMBER_ENABLE_LSAN=ON

echo "== build =="
cmake --build "$BUILD_DIR"

echo "== ctest (step_gate + store_asan_gate) =="
ctest --test-dir "$BUILD_DIR" --output-on-failure

echo "== black-box gate (same suites as step_gate) =="
"./${BUILD_DIR}/remember_tests" "./${BUILD_DIR}/remember" --only "$GATE_SUITES"

echo "== store unit under ASan/LSan =="
"./${BUILD_DIR}/remember_store_tests"

echo "== lint (format, tidy, cppcheck, IWYU, scan-build) =="
export CI=true
export BUILD_DIR
export REMEMBER_SCAN_BUILD=1
export REMEMBER_IWYU=1
./scripts/lint-all.sh

echo "== ci-linux: OK =="
