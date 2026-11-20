#!/usr/bin/env bash
# Reads changed paths on stdin, one per line. Exits 0 if any is code, 1 if all
# are docs. Docs: docs/, specs/, design-logs/, implementation-plans/, any *.md.
# skills/ ships and is tested (tests/test_install_skill.sh), so it counts as code.
# specs/API-SURFACE.md is read by the cli suite (tests/test_cli.c), so it counts too.
# Used by the ci.yml `changes` job and .githooks/pre-push.
exec awk '
  /^skills\// || /^specs\/API-SURFACE\.md$/ || !(/^(docs|specs|design-logs|implementation-plans)\// || /\.md$/) { code = 1 }
  END { exit !code }'
