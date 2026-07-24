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

- [x] API stable for commands
- [x] SHA-256 in-tree
- [x] Format + lint-ready

## Review notes (2026-07-24)

- **API:** `body_trim_copy` (length-based, heap result), `normalize_token` + `normalize_tag`/`normalize_key` aliases, `body_hash_hex`, `NormStatus` + `norm_status_string`.
- **Rules:** ASCII ws trim (`space`/`tab`/`LF`/`CR`/`VT`/`FF`); body max 64 KiB; token max 64; UTF-8 RFC 3629 (no overlong/surrogate/>U+10FFFF); token rejects ASCII ws/control; ASCII A–Z casefold only.
- **SHA-256:** Brad Conte public-domain in `third_party/sha256/`; CMake target `sha256` with `-w`; one-shot hex in `normalize.c`.
- **Tests:** pure unit suite `normalize` (15 cases) in black-box harness and under ASan via `remember_store_tests`. ctest gate: `--only cli_global,store,normalize`.
- **Pragmatic:** no store/cli includes; tag/key share one path; third_party digest is swappable without touching callers of `body_hash_hex`.
