# utf8proc (pinned)

| Field | Value |
|-------|--------|
| Version | 2.12.0 |
| Unicode | 18.0.0 (`utf8proc_unicode_version` in `utf8proc.c`) |
| Upstream | https://github.com/JuliaStrings/utf8proc |
| Release | https://github.com/JuliaStrings/utf8proc/releases/tag/v2.12.0 |
| Tarball | `v2.12.0.tar.gz` sha256 `f564011d38b2888d583d510b08e69ffa15aa117155db1b9b49ef1dfe1fa25111` |
| License | MIT (Expat). See `LICENSE.md`. |
| Files | `utf8proc.h`, `utf8proc.c`, `utf8proc_data.c` |

`utf8proc.c` includes `utf8proc_data.c`. The CMake target compiles `utf8proc.c` only.

The project port is `src/unicode.c` (`unicode_nfc`, grapheme clusters, general category, White_Space). No other project file includes `utf8proc.h`.

## Local modifications

None.
