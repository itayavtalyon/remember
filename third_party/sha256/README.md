# SHA-256 (pinned)

| Field | Value |
|-------|--------|
| Implementation | Brad Conte (`crypto-algorithms`) |
| Files | `sha256.c`, `sha256.h` |
| Source | https://github.com/B-Con/crypto-algorithms |
| License | Public domain (author disclaimer; no copyright asserted) |

One-shot hashing for callers lives in `src/normalize.c` (`body_hash_hex`). Only that module should include these headers (or other pure helpers that need digests). Not used by the SQLite adapter.
