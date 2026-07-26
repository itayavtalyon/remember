#!/usr/bin/env bash
# Point this clone at versioned hooks under .githooks/ (includes pre-push Linux CI).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ ! -d .githooks ]]; then
  echo "error: missing .githooks/" >&2
  exit 1
fi

chmod +x .githooks/* 2>/dev/null || true
chmod +x scripts/*.sh 2>/dev/null || true

git config core.hooksPath .githooks
echo "Installed: core.hooksPath=.githooks"
echo "pre-push will run: ./scripts/ci-linux.sh (Docker Linux CI)"
echo "Skip one push: SKIP_LINUX_CI=1 git push   or   git push --no-verify"
