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

## Carried from step 03 review (must do in this step)

1. **Map `NormStatus` → user-facing stderr** — do **not** print `norm_status_string()` labels as the sole UX (`"empty"`, `"too long"`). Commands (or a thin `norm_error_message` for CLI) own full phrases, e.g. `empty body after trim`, `body exceeds 64 KiB`, `invalid UTF-8 in body`, `invalid tag`, `invalid key`. Exit 1. Keep `norm_status_string` for tests/debug if useful.
2. **Body pipeline uses normalize once** — read argv/stdin → `body_trim_copy` → `body_hash_hex` → store; tags/keys via `normalize_tag` / `normalize_key` (never reimplement trim/casefold/UTF-8).
3. **Output escaping** — see sub-step below (already required).
4. **Digest sanitizer gate stays the unit suite** — production `remember` may keep linking the `-w` `sha256` static lib; do not reintroduce a second untested digest path. Multi-block/long-pad vectors live in `test_normalize.c` (`body_hash_long_pad_and_multiblock`).

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

- [x] All listed add + key-add tests green
- [x] FTS row written on add (search may still fail until 06)
- [x] Format

## Review notes (2026-07-24)

- **Layers:** `commands.c` (add/get/list) → `store_*` port → `store_sqlite.c` only; `output.c` for JSON/human; `util.c` for path + stdin.
- **Store:** `store_add` (created/merged/updated), `store_get` / `store_get_by_key`, minimal `store_list`; FTS rewrite in same txn as entry/tag mutation.
- **UX:** Norm errors are full phrases; JSON envelope with escaped bodies; human add prints id.
- **Path policy:** reject `:memory:` / `file:` at resolve time.
- **Thin get/list:** enough for step-04 verification tests; filters stay step 05.
- **Tests:** `step04_gate` = `cli_global,store,normalize,add,json_db_config,key_add` (98). Update/delete/search remain red.
- **Lint:** `REMEMBER_SHA256_HEX_LEN` allowed outside normalize (public API constant); vendor symbols still gated.

## Review round 2 (2026-07-25) — applied

1. **stdin/argv body validation now identical.** `util_read_stdin` capped raw bytes at the *post-trim* limit, so a body that trims to exactly 64 KiB was rejected on stdin (surrounding whitespace pushed it over) while argv accepted it. stdin now reads with a generous hard cap (`REMEMBER_STDIN_MAX`, memory guard only) and `body_trim_copy` is the sole enforcer of "64 KiB after trim". Tests: `add_stdin_body_at_limit_accepted`, `add_stdin_body_over_limit_rejected`.
2. **`get` human output goes through `output.c`.** Was a raw `fputs(body)` that could emit stored ESC/control bytes to the terminal (injection vector for `agent`/`tool`-sourced bodies). New `output_body_human` neutralizes terminal-control bytes to `?` while keeping `\n`/`\t`; exact bytes remain on the `--json` path. Tests: `get_human_body_neutralizes_control_chars`, `get_human_body_preserves_newlines`.
3. **Keyed-add is now gated.** `add_key_*` + get-by-key cases split into a `key_add` suite (green) joined to `step04_gate`; the WIP update/delete/search cases stay in `key` (red). Fixed the test-group matcher to exact comma-token matching so `key` cannot select `key_add` (a substring match would have silently pulled the red suite into the gate).
4. **Nits:** `parse_id_token` now rejects `strtoll` overflow as "invalid id" (was a lookup miss / exit 2); comment on the post-COMMIT `load_entry_by_id` failure (durable write, reporting-only error).

## Review notes (2026-07-25) — formal step review (PR readiness)

**Verdict: Approve with nits** — ready for PR after committing; no blocking defects.

### Blocking
(none)

### Important (prefer before merge, or land as follow-up PR commits)
1. **Missing black-box test for ephemeral `--db` reject.** Plan carried from step 02: pick reject-or-document and *add a test*. Implementation rejects `:memory:` / `file:` in `util_resolve_db_path` (manual smoke OK) but no harness case asserts exit 1 + message.
2. **`step04_gate` omits plan-required verification edges.** `add_tag_casefold_*`, control/utf8 tag/key, `keyless_merge_keeps_created_at`, `db_parent_dir_mode_0700`, `json_add_error_empty_stdout` all pass under `--only verification_edges` but are not in ctest. A regression can green the gate while red those. Prefer a `verification_add` slice or fold the green cases into `add`/`json_db_config`.
3. **JSON control-char test is escape-shape only.** Plan asked for parse + round-trip of original bytes; `add_json_body_with_control_and_quotes_stays_valid` checks substrings/`\\u001b` but does not `get --json` and compare body bytes.

### Nits
- Generic `"store error"` hides SQLite detail (OOM is special-cased; other `STORE_ERR_*` are opaque).
- `cmd_list` teardown is duplicated (success vs JSON-fail); a single cleanup path would match engineering-notes style.
- Human list still `fputs` keys/tags raw — safe today because tokens forbid controls; keep that invariant when filters land.
- Uncommitted tree on `step/04-add` — commit before opening the PR.

### Confirmed OK
- Port boundary: only `store_sqlite.c` includes SQLite; lint gate clean.
- Commands → normalize → store → output; no SQL in commands; FTS resync in same txn as mutators.
- Norm UX phrases (not bare `norm_status_string`); body pipeline once; digest still unit-suite sanitized.
- Output owns JSON RFC 8259 + human control neutralization (`output_body_human` / preview).
- Gates: build `-Werror`, `step04_gate` (98), `store_asan_gate`, `scripts/lint-all.sh` LINT OK.

## Review follow-ups applied (2026-07-25)

1. **`db_rejects_memory_uri` / `db_rejects_file_uri`** in `json_db_config` (path policy black-box).
2. **`verification_add` suite** + ctest gate includes it (plan V21/V22/V33/V34 + thin get/list edges).
3. **JSON control-char test** round-trips via `get --json` escapes + `SELECT body` original bytes.
4. **Nits:** `store_status_message`; `cmd_list` single cleanup; `open_store` rename; token raw-fputs comment.
5. **Plans:** carry-overs noted on `05-get-list-delete.md` and `09-polish.md`.
