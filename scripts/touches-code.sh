#!/usr/bin/env bash
# Reads changed paths on stdin, one per line. Exits 0 if any is code, 1 if all
# are docs. Docs: docs/, design-logs/, implementation-plans/, any *.md.
# skills/ ships and is tested (tests/test_install_skill.sh), so it counts as code.
# Used by the ci.yml `changes` job and .githooks/pre-push.
exec awk '
  /^skills\// || !(/^(docs|design-logs|implementation-plans)\// || /\.md$/) { code = 1 }
  END { exit !code }'
