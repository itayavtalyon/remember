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
- **Tests:** pure unit suite `normalize` (20 cases) in black-box harness and under ASan+UBSan via `remember_store_tests`. ctest gate: `--only cli_global,store,normalize`.
- **Pragmatic:** no store/cli includes; tag/key share one path; third_party digest is swappable without touching callers of `body_hash_hex`.

## Review round 2 (2026-07-24) — applied

Fixes to findings from the formal review:

1. **SHA-256 signed-shift UB** (`sha256.c:49`, `byte << 24` overflows `int` for bytes ≥ 0x80): cast each byte to `WORD` first. Also `sha256_update`'s loop counter `WORD` → `size_t` (was truncating lengths ≥ 4 GiB). Both marked `Local fix:` and documented in `third_party/sha256/README.md`.
2. **Detection flow for that class of bug.** A `-w` static lib is never instrumented, so the sanitizer gate couldn't see it. The digest TU is now compiled **into** `remember_store_tests` (sanitized) with a per-source `-w`, and `-fno-sanitize-recover=all` makes UB abort on any run. Verified: reverting the cast makes `store_asan_gate` abort (exit 134). Rule generalized in `docs/engineering-notes.md`.
3. **UTF-8 edge tests added** (`body_trim_utf8_edge_rejections`): overlong 2/3-byte, surrogate, > U+10FFFF, lead > F4, truncated, bare continuation, bad continuation — the validator branches plain `0xff` never reached.
4. **NUL = end-of-string** ([[nul-is-end-of-string]]): `body_trim_copy` now caps at the first NUL so `*out_len == strlen(*out)`; text/length/hash stay consistent. Tests: `body_trim_nul_terminates`, `body_hash_ignores_bytes_after_nul`.
5. **Accurate errors:** new `NORM_ERR_INTERNAL` for missing/too-small output buffers (was `TOO_LONG` in `normalize_token`, `OOM` in `body_trim_copy`); a genuinely over-long token stays `TOO_LONG`. Test: `normalize_reports_bad_output_buffer`.
6. **Control chars in body kept** (design: only tokens forbid them) — recorded as an **output**-escaping obligation, pushed to the step 04 plan with a required JSON test ([[output-escaping]]). Test here documents the behavior: `body_trim_keeps_control_chars`.

## Formal re-review (2026-07-24) — tip `164b5d4`

**Verdict: Approve with nits** (no blockers for step 04). Round-2 correctness fixes hold; residual items are coverage/doc strength.

### Important — resolved (same day follow-up)

1. **SHA-256 UBSan paths:** `body_hash_long_pad_and_multiblock` — NIST 56-byte vector (long final-pad) + 64-byte message (multi-block update).
2. **`body_trim_keeps_control_chars`:** asserts SOH/`0x1b` byte values, not only length.

### Nits — disposition

| Nit | Disposition |
|-----|-------------|
| `normalize.h` NULL/`src_len` comment | **Fixed** — documents NULL → EMPTY for any `src_len`. |
| `int` vs `bool` for class helpers | **Fixed** — `bool` + `<stdbool.h>`. |
| Production links uninstrumented `sha256.a` | **Keep** — unit binary owns the digest UBSan gate; multi-block vectors complete it. Documented in step 04 carry-over. |
| Short `norm_status_string` labels | **Defer to step 04** — map `NormStatus` → full stderr phrases at the command layer (see `04-add.md`). |

### Confirmed OK

- Design rules match (trim set, body max, token max, ASCII casefold only, token ws/control reject, body allows controls, NUL = EOS).
- UTF-8 RFC 3629 structure + edge tests; pure module (no store/cli/sqlite).
- Tag/key single path; `NORM_ERR_INTERNAL` vs `TOO_LONG`/`OOM` split.
- SHA-256 signed-shift local fix + size_t update loop; lint boundaries; ctest + LINT OK.
