# 12 — TTL, expiry, and trash (P6)

## Goal

Optional `expires_at` on entries: durable by default; expired rows are a
virtual trash bin (hidden from default list/search/get); restore or
permanent delete. **Design:**
[`design-logs/003-ttl.md`](../design-logs/003-ttl.md).

## Scope

**In:** schema `user_version` 2; store filters; `--ttl` / `--expires` /
`--clear-expires` / `--trash`; `purge-trash`; revive-on-add; skill/help;
tests + coverage.

**Out:** auto-GC timer; mixed include-expired; local-tz dates; Mac UI
(remember-mac plan 11 after this pin bump).

## Must-pass tests

| Check | Proves |
|-------|--------|
| Fresh DB `user_version=2`, column present | bootstrap |
| Copy of a v1 file migrates; old rows `expires_at` NULL | migrate |
| Add without flags → JSON `expires_at: null`; in default list | default durable |
| `--ttl 1h` stored as ISO `> now`; in default list | relative |
| `--expires` past → only `--trash` list | absolute + trash |
| Default list/search/tags omit expired | hide |
| `get` expired → exit 1, stderr `expired` | error token |
| `get --trash` expired → 0 + row | trash read |
| `get --trash` active → exit 2 | trash-only get |
| `update --clear-expires` restores | restore |
| Keyless add of expired body → same id, not trash | revive |
| `purge-trash` deletes only expired; FTS/tags consistent | empty trash |
| `--ttl` + `--expires` → usage exit 1 | flag mutex |
| Invalid `--ttl` (`0`, `7`, `7x`) → usage | grammar |

## Stages

1. Schema + `Entry.expires_at` + migrate + index.
2. Store filters / get expired / add revive / update expiry / purge.
3. CLI flags + `purge-trash` + output JSON + help.
4. Skill + this plan Implementation Notes.
5. Full suite + lint + coverage.

## Definition of Done

- [ ] Design log 003 unchanged except Implementation Results
- [ ] Must-pass table green; `user_version` 2
- [ ] Public exits still 0/1/2; expired → 1 + `expired`
- [ ] Skill/help match flags
- [ ] remember-mac pin bump is a **follow-up** (plan 11)

## Implementation Notes

_(Fill while building.)_

## Review Notes

_(Fill after step review.)_
