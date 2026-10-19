# 17a — One-line plain notices, duplicate version-vector keys

Addendum to [plan 17](17-list-dates-stale-guard.md). The whole-plan review on
2026-10-19 fixed the int64 saturation bypass and left two defects. This plan
fixes those two. It does not reopen plan 17.

## Goal

- A plain notice stays one stderr line when the message contains a control byte.
- A version vector with the same device key twice is malformed. Nothing is written.

## Why these two, and why the other leftovers stay

The review's "left as they are" list mixed defects with locked behavior.

| Leftover | What it is | This plan |
|----------|------------|-----------|
| Plain `remember: …%s` splits on a newline in the token | Defect. One notice is no longer one line. A CR rewrites the prefix on a terminal. JSON already escapes the byte. | Fix |
| A version vector may repeat a device key | Defect in the shape check. Agreeing copies compare as that clock, so `{"d":1,"d":1}` authorizes the same write as `{"d":1}`. Disagreeing copies look up the first clock, so the second copy makes the vector compare stale against itself. | Fix |
| Omitting `--if-version-vector` overwrites | The flag is opt-in. `decision:plan17-stale-guard-version-vector`. | Stay |
| Update help line is 95 columns | Plan 22 owns the help wrap. | Stay |
| `handle_update_ttl_flag` parses `--if-version-vector` | The name is old. The parse is the right flag. | Stay |
| `maybe_revive` ignores `expires_at` when the row is already live | The engineering notes say expiry is applied when an expired or deleted row is revived. A live re-add keeps the expiry it has. | Stay |
| `vv_parse_pairs` still saturates `CAST` | Import dominance uses that parser. The guard rejects the overflow before parse. Changing the parser would change sync. | Stay |

## Source of truth

- `decision:plan17a-plain-notice-one-line`
- `decision:plan17a-duplicate-vv-key`
- Plan 17's version-vector rule stays: one JSON object of device id to non-negative integer, missing device counts as 0, compare inside the write transaction after the bin check.
- E5.2d stays for an ordinary token: plain `invalid timestamp: <ts>` and the same words in JSON. A token with no control byte is byte-identical to today, including quotes and backslashes (`plain_error_keeps_quote_and_backslash`).

Do not invent. A case these decisions do not cover → stop and ask Itay.

## Context

Baseline: plan 17 at `52a8802` (PR #32).

- `write_notice` in `src/diag.c` prints a plain notice with `fprintf("%s\n", message)`. JSON goes through `output_json_string`.
- `output_body_human` maps C0 and DEL to `?` and keeps `\n` and `\t`, because a body is allowed to be several lines. A notice is one line, so this plan does not call that function. It uses the same `?` for every other control, and it also maps `\n` and `\r`.
- `vv_reject_malformed` is one `CASE`. `json_valid` guards `json_type` and `json_each`. A counter that does not fit in int64 already fails `value != CAST(value AS INTEGER)`. The guard runs that check on the given token and on the stored vector. Stored `STORE_ERR_BAD_VV` becomes `STORE_ERR_STALE`.
- Probed with Homebrew `sqlite3` on 2026-10-19: the `CASE` below returns 1 for `{"a":1,"a":2}` and `{"a":1,"a":1}`, 0 for `{"a":1,"b":2}`, and 1 for `not-json`, with `?1` bound. Invalid JSON does not reach `json_each`.

## Locked decisions (Itay 2026-10-19)

| # | Decision |
|---|----------|
| A1 | Plain notices and plain warnings substitute `?` for every byte below `0x20` except tab, and for DEL (`0x7f`). The writer then adds one `\n`. Quotes and backslashes stay. JSON is unchanged, including `\n` and `\u0001` inside a message. |
| A2 | A repeated device key is malformed, on the given token and on the stored vector. Given → `STORE_ERR_BAD_VV` → `usage` `invalid version vector: <vv>`. Stored → `STORE_ERR_STALE`, nothing written. Agreeing copies (`{"d":1,"d":1}`) are malformed too. `vv_parse_pairs` stays as it is. |

## Scope

**In:** `write_notice`'s plain path; the duplicate-key term in `vv_reject_malformed`; tests; one sentence each in the engineering notes (diag plain line, version-vector shape).

**Out:** JSON escaping, import and `vv_compare`, help wrapping, the TTL flag's function name, live-row expiry, the opt-in guard, plan 18.

## Files

`src/diag.c`, `src/store_sqlite.c`, `tests/test_errors.c`, `tests/test_list_dates.c`, `tests/test_store.c`, `docs/engineering-notes.md`.

## Must-pass tests

| Check | Proves |
|-------|--------|
| `diag_error_as(false, DIAG_USAGE, "bad\ninjected")` is one line, `remember: bad?injected\n` | A1, newline |
| The same with `bad\rinjected` | A1, CR does not rewrite the prefix |
| The same with a tab stays `remember: bad\tinjected\n` | A1, tab kept |
| The same with `0x1b` is `remember: bad?injected\n` | A1, ESC |
| `plain_error_keeps_quote_and_backslash` stays green | A1, no JSON escaping in plain |
| `json_error_escapes_message_bytes` stays green | A1, JSON untouched |
| `list --since $'bad\ninjected'` plain, no `--json-errors`: exit 1, one stderr line, `remember: invalid timestamp: bad?injected\n`, empty stdout | A1 at the CLI |
| Existing `list_rejects_a_newline_timestamp_as_one_json_line` stays green | A1, JSON still embeds the newline escaped |
| `{"<device>":1,"<device>":1}` and `{"<device>":1,"<device>":2}` in the malformed guard list: `STORE_ERR_BAD_VV`, body unchanged | A2, given side, both copies |
| Stored `{"<device>":1,"<device>":2}` against a given `{"<device>":1}`: `STORE_ERR_STALE`, body unchanged | A2, row side |
| `{"<device>":1,"<other>":2}` still matches | A2 does not reject two devices |

Mutate one clause at a time. A mutant that keeps the raw `fprintf("%s")` must fail the newline test. A mutant that drops only the `GROUP BY` term must fail the agreeing-duplicate test. Record the red run in the Implementation Notes.

## Stages

TDD. One red test, then the smallest change.

1. **Plain line.** The diag tests above, red first. In `write_notice`, write the plain prefix and the message byte by byte, then one `\n`. JSON stays on `output_json_string`. Then the CLI newline test in `tests/test_list_dates.c`.
2. **Duplicate key.** Extend the `CASE` in `vv_reject_malformed` with:

```sql
OR EXISTS (SELECT key FROM json_each(?1) GROUP BY key HAVING COUNT(*) > 1)
```

   The term sits in the `ELSE` branch, beside the existing `EXISTS`, so `json_valid` still guards it. Store tests red first. A clean two-device vector stays equal.

3. **Notes.** Engineering-notes sentences. Full gate. In-session review. Stop. Do not start plan 18.

## How to work

Branch from an up-to-date `main` after plan 17 has merged. One branch, one PR. Load `standard:coding` (round 7 is in force), `workflow:deep-review`, and `status:remember`. TDD. After the last stage: the plan 17 gate (sanitizers, `REMEMBER_TEST_VFS=ON`, ctest, `lint-all.sh`, coverage 100/100, `ci-linux.sh`, `leaks-macos` on macOS). Append Implementation Notes and Review Notes here. On pause, update `status:remember`.

## Definition of Done

- [ ] A1 and A2 hold, each proved by the test named above.
- [ ] Ordinary tokens, quotes, backslashes, tabs, and JSON error bytes are unchanged.
- [ ] `vv_parse_pairs` has no diff.
- [ ] Engineering notes mention the plain-line rule and the duplicate-key rule.
- [ ] Gate green on the commit that is pushed.
- [ ] Second review done, no open blocking item.
- [ ] INDEX row 17a marked ✅.

## Second review handover

Run the tests. Then, on `build/remember`: a plain `--since` with a newline and a CR, a JSON `--since` with a newline, a guarded update with `{"<device>":1,"<device>":1}`, and a guarded update with two different devices. Bugbot if it is installed.
