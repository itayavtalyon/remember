#!/usr/bin/env bash
# X-E02: the test VFS is linked into the test CLI only.
# remember_plain, remember_core, and a Release tree stay free of it,
# even when that Release configure asks for REMEMBER_TEST_VFS=ON.
set -euo pipefail

remember="${1:?remember binary}"
plain="${2:?remember_plain binary}"
core="${3:?remember_core archive}"
root="${4:?source root}"

defined() {
  # A defined text symbol. ASan binders and undefined refs do not count.
  # Mach-O prefixes an underscore; ELF does not.
  local out
  if ! out="$(nm "$1" 2>&1)"; then
    echo "no_shim: nm failed on $1" >&2
    printf '%s\n' "$out" >&2
    return 1
  fi
  grep -Eq '[[:space:]][Tt][[:space:]]+_*remember_test_vfs_install$' <<<"$out"
}

if ! defined "$remember"; then
  echo "no_shim: test CLI is missing remember_test_vfs_install" >&2
  exit 1
fi
if defined "$plain"; then
  echo "no_shim: remember_plain links the test VFS" >&2
  exit 1
fi
if defined "$core"; then
  echo "no_shim: remember_core links the test VFS" >&2
  exit 1
fi

rel="${REMEMBER_SHIM_RELEASE_DIR:-$root/build-shim-release}"
# bash 3.2 (macOS /usr/bin/env bash) errors on an empty "${arr[@]}" under set -u.
if command -v ninja >/dev/null 2>&1; then
  cmake -S "$root" -B "$rel" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="${CC:-clang}" \
    -DREMEMBER_ENABLE_SANITIZERS=OFF \
    -DREMEMBER_TEST_VFS=ON
else
  cmake -S "$root" -B "$rel" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="${CC:-clang}" \
    -DREMEMBER_ENABLE_SANITIZERS=OFF \
    -DREMEMBER_TEST_VFS=ON
fi
cmake --build "$rel" --target remember
if defined "$rel/remember"; then
  echo "no_shim: Release remember links the test VFS" >&2
  exit 1
fi
echo "no_shim: PASS (plain, core, Release)"
