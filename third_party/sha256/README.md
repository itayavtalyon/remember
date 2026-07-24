# SHA-256 (pinned)

| Field | Value |
|-------|--------|
| Implementation | Brad Conte (`crypto-algorithms`) |
| Files | `sha256.c`, `sha256.h` |
| Source | https://github.com/B-Con/crypto-algorithms |
| License | Public domain (author disclaimer; no copyright asserted) |

One-shot hashing for callers lives in `src/normalize.c` (`body_hash_hex`). Only that module should include these headers (or other pure helpers that need digests). Not used by the SQLite adapter.

## Local modifications

Not pristine upstream — two correctness fixes (each marked `Local fix:` in `sha256.c`):

1. **Signed-shift UB** in `sha256_transform`: `data[j] << 24` shifts an `unsigned char` that promotes to `int`, which overflows for bytes ≥ 0x80 (UBSan: "left shift of 255 by 24 places cannot be represented in type 'int'"). Each byte is cast to `WORD` (unsigned) before shifting.
2. **32-bit length counter** in `sha256_update`: the loop index was `WORD`, truncating `len` on inputs ≥ 4 GiB. Now `size_t`.

Both are exercised under UBSan by the NIST-vector tests in `tests/test_normalize.c`, which build the digest instrumented in `remember_store_tests` (see the root `CMakeLists.txt` — this TU is *not* the `-w` static lib in that binary). That is the guard against reintroducing this class of bug in a vendored TU.
