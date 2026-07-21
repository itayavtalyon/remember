# 10 — Agent skill + dogfood

## Goal

`skills/remember/SKILL.md` teaches agents correct use per design (when to write, `--key` lifecycle, no secrets, paging, exit codes).

## Deliverables

- `skills/remember/SKILL.md`
- README pointer + install note (copy/link into agent skill path)
- Optional: dogfood entries in your real DB

## Tests

No automated tests. Manual: agent can add/search/update from skill alone.

## Done checklist

- [ ] Skill matches live CLI (flags, JSON shapes, key protocol)
- [ ] Mentions local-disk DB / no secrets
