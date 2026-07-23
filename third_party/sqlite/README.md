# SQLite amalgamation (pinned)

| Field | Value |
|-------|--------|
| Version | **3.53.3** (year-month encoding `3530300`) |
| Files | `sqlite3.c`, `sqlite3.h` |
| Source | https://www.sqlite.org/2026/sqlite-amalgamation-3530300.zip |
| License | Public domain (see header blessing in `sqlite3.c`) |

Built with FTS5 and a single-threaded / hardened flag set (see root `CMakeLists.txt` and `docs/engineering-notes.md`). Only `src/store_sqlite.c` may include these headers.
