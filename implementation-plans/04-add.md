# 04 — `add` (keyless dedupe + keyed upsert)

## Goal

Implement `remember add` end-to-end via `store_add(...)`.

## Behavior summary

- Keyless: insert or body_hash merge (union tags, bump `updated_at`, keep source/created_at/body).
- Keyed: upsert by key (replace body, union tags, keep id/source/created_at/key, bump `updated_at`); **no** body_hash uniqueness.
- Actions JSON: `created` | `merged` | `updated` (keyed upsert).
- Human: print id only; JSON: uniform envelope with `action`, `count`, `entries:[full]`.
- stdin body `-`; `--source`; `--tag` repeatable; `--key` optional.

## Store API additions

```c
typedef enum { STORE_ADD_CREATED, STORE_ADD_MERGED, STORE_ADD_UPDATED } StoreAddAction;

int store_add(Store *s,
              const char *body,          /* already trimmed */
              const char *body_hash,
              const char *key_or_null,   /* normalized or NULL */
              const char *const *tags, size_t ntags,
              const char *source,
              StoreAddAction *out_action,
              Entry *out_entry);         /* heap or caller buffer — pick one style */
```

FTS insert/resync inside the same transaction.

## Pragmatic notes

- Commands orchestrate normalize → store_add → output.
- **No SQL in `commands.c`.**

## Carried from step 02 review (wire `store_open` here)

- **`--db` path policy owns `:memory:` and `file:` handling.** `store_open` passes its path straight to `sqlite3_open_v2`, so `remember --db :memory: add ...` opens a throwaway store that discards everything on close — a silent data-loss footgun. Resolve when `main` first calls `store_open`: either reject non-file `--db` values, or document them as an explicit ephemeral mode. Add a black-box test for whichever you pick.
- **Sync-path warning** (`path_looks_synced()` in `util`, called from `main` before/after open) is still deferred; land it when the CLI gains the real DB path.
- Once `main` calls `store_open`, the black-box suite exercises the adapter under ASan (via `remember`), complementing the fast `store_asan_gate` unit target.

## Sub-step: output must not break on body content (from step 03 review)

A body is trimmed + UTF-8-validated but **not** stripped of control characters (design: only tags/keys forbid them — see [[output-escaping]]). The `add` JSON envelope echoes the full entry, so the first time a stored body reaches output is here.

- **JSON:** escape per RFC 8259 — `"` `\` and every U+0000–U+001F control char as `\uXXXX` (`\n` `\t` etc. for the short forms). The envelope must stay parseable when the body contains quotes, backslashes, newlines, or ESC.
- **Human:** a body with an embedded ESC etc. must not emit raw terminal escape sequences on a TTY. Decide the rule (the CLI is line-oriented; `add` only prints the id, so human exposure starts at `get`/`list` — carry the same escaping helper there).
- **NUL:** already handled upstream — `body_trim_copy` ends the body at the first NUL ([[nul-is-end-of-string]]), so output never sees an embedded NUL.

**Required test** (`test_add.c`, add once JSON output exists): `add_json_body_with_control_and_quotes_stays_valid` — add a body containing `"`, `\`, newline, and `0x1b`; assert stdout is valid JSON (parse it / check the escapes) and round-trips back to the original bytes.

## Tests that must pass

### Keyless add (`test_add.c`)

- `add_basic_prints_id_one`
- `add_with_tags_and_source_human`
- `add_default_source_is_unknown`
- `add_source_agent_tool_accepted`
- `add_invalid_source_rejected`
- `add_empty_body_rejected`
- `add_whitespace_only_body_rejected`
- `add_body_trimmed_before_store`
- `add_dedupe_same_body_merges_tags`
- `add_dedupe_trim_equivalent_bodies`
- `add_dedupe_keeps_original_source`
- `add_json_created_shape`
- `add_json_merged_shape`
- `add_stdin_body_dash`
- `add_body_over_64kib_rejected`
- `add_tag_ascii_casefold`
- `add_empty_tag_rejected`
- `add_tag_with_space_rejected`
- `add_tag_too_long_rejected`
- `add_tag_project_colon_style_allowed`
- `add_missing_body_rejected`
- `second_add_gets_id_two`

### Keyed add (`test_key.c`)

- `add_key_creates_slot`
- `add_key_upsert_same_id_replaces_body`
- `add_key_upsert_unions_tags`
- `add_key_keeps_original_source`
- `keyed_entries_may_share_body_text`
- `keyless_still_dedupes_among_keyless_only`
- `add_key_ascii_casefold`
- `add_empty_key_rejected`

### Normalization / UTF-8 / source / created_at (V21–22)

- `add_tag_casefold_merges_to_one_tag_name`
- `add_tag_control_char_rejected`
- `add_tag_invalid_utf8_rejected`
- `add_key_control_char_rejected`
- `add_invalid_utf8_body_rejected`
- `keyless_merge_keeps_created_at`

### Config / dir (partial)

- `db_parent_dir_mode_0700`
- `json_add_error_empty_stdout` (V34)
- Isolation tests: prefer thin `get` early or defer to 05

## More tests?

| Gap | Status |
|-----|--------|
| Invalid UTF-8 body/tag via stdin/argv | **Added** |
| Key with spaces | Covered by tag space + key normalize same rules — optional duplicate |

## Done checklist

- [ ] All listed add + key-add tests green
- [ ] FTS row written on add (search may still fail until 06)
- [ ] Format
