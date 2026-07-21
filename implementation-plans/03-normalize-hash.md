# 03 — Normalize + hash

## Goal

Pure functions for body trim, tag/key normalization, UTF-8 checks, SHA-256 hex. No I/O.

## Deliverables

| File | Role |
|------|------|
| `src/normalize.c` / `normalize.h` | `body_trim_copy`, `normalize_tag`, `normalize_key` (key = same rules as tag) |
| `third_party/sha256/` | Public-domain SHA-256 |
| Optional | `tests/test_normalize.c` unit tests (not CLI) |

## Rules (from design)

- Body: trim ASCII ws; empty → error; max 64 KiB; valid UTF-8; hash lowercase hex SHA-256 of stored bytes.
- Tag/key: trim; valid UTF-8; no ws/control; ≤64 bytes; ASCII casefold; allow `:_./-` etc.

## Pragmatic notes

- **DRY:** one normalize path for tags and keys (`normalize_token` + alias).
- **Orthogonal:** no store/cli includes here.
- **Replaceable:** pure C; easy to unit test without DB.

## Tests (CLI already cover most; green with step 04)

| Test | Criterion |
|------|-----------|
| `add_tag_ascii_casefold`, `add_tag_casefold_merges_to_one_tag_name` | V21 |
| `add_tag_control_char_rejected`, `add_tag_invalid_utf8_rejected` | V21 |
| `add_key_control_char_rejected`, `add_empty_key_rejected` | V21 |
| `add_invalid_utf8_body_rejected`, `update_invalid_utf8_text_rejected` | V30 |
| space / length / project:colon tag tests | V21 |

**More tests?** Optional pure unit `test_normalize.c` for trim tables — not blocking if CLI covers.

**Must pass this step:** helpers compile; CLI green deferred to 04.

## Done checklist

- [ ] API stable for commands
- [ ] SHA-256 in-tree
- [ ] Format + lint-ready
