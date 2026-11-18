#!/usr/bin/env bash
# Records which heavy checks passed on which git tree, so .githooks/pre-push can
# skip a check that already passed on exactly the tree being pushed.
#   gate-stamp.sh tree           print HEAD's tree id if the work tree is clean, else nothing
#   gate-stamp.sh mark NAME TREE record NAME passed on TREE (only if HEAD's tree is still TREE and clean)
#   gate-stamp.sh has NAME TREE  exit 0 if NAME passed on TREE
# Stamps live in <git-common-dir>/gate-stamps, shared by every worktree.
# ponytail: stamps are never pruned; they are empty files named by tree id.
set -euo pipefail
dir="$(git rev-parse --git-common-dir)/gate-stamps"
clean_tree() { [[ -z "$(git status --porcelain)" ]] && git rev-parse 'HEAD^{tree}'; }
case "${1:-}" in
  tree) clean_tree || true ;;
  mark)
    [[ -n "${3:-}" && "$(clean_tree || true)" == "$3" ]] || exit 0
    mkdir -p "$dir" && : >"$dir/$2-$3"
    ;;
  has) [[ -n "${3:-}" && -e "$dir/$2-$3" ]] ;;
  *)
    echo "usage: $0 tree | mark NAME TREE | has NAME TREE" >&2
    exit 2
    ;;
esac
