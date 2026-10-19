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

## Implementation Notes

Branch `plan17a-plain-line-and-duplicate-vv` from `main` at `9cb97b9` (PR #32 merged).

**Validity grill (Itay 2026-10-19, `decision:plan17a-notice-shared-classifier`).** Open PR #31 (`fix/plain-stderr-control-bytes`) fixed the same split line under `decision:plain-stderr-control-bytes`, but mapped tab to `?`. A1 wins on the rule: tab stays. The implementation follows #31's shape, not this plan's own loop in `diag.c`. `output.c`'s `is_terminal_ctrl` takes `keep_newline`, `output_text_human` is the one exported writer, and `write_notice` and `output_body_human` both call it. That keeps one C0/DEL classifier for bodies and notices. Close #31 as superseded when this PR opens.

**Two plan claims were wrong. The probes:**
- The plan's row-side test (stored `{"d":1,"d":2}` against a given `{"d":1}`) was green before the fix. The existing compare already finds the second clock. The bypass is the *agreeing* stored copy: stored `{"<device>":1,"<device>":1}` against a given `{"<device>":1}` returned `STORE_OK` and wrote `new`. `store_update_stored_repeated_device_key_is_stale` asserts both stored forms.
- "A mutant that drops only the `GROUP BY` term must fail the agreeing-duplicate test" cannot hold. Without `GROUP BY` the term rejects every vector with two keys, so the agreeing duplicate is still rejected. The two-device tests kill that mutant (15 red, `store_update_two_device_version_vector_matches` among them).

**Red runs before each fix.**
- Stage 1: `plain_error_turns_a_newline_into_a_question_mark`, `…_carriage_return_…`, `…_an_escape_…`, and `…_del_…` failed (`remember: bad\ninjected`, `remember: bad\rinjected`, and so on). `plain_error_keeps_a_tab` passed before and after, because it guards against over-replacement.
- Stage 2: `store_update_repeated_device_key_is_bad` got `STORE_OK` (0) for the agreeing copy and the body became `new`. `store_update_stored_repeated_device_key_is_stale` got `STORE_OK` and the body became `new`.

**One-clause mutants.** Each was built and run. Every mutant goes red.

| Mutant | Red |
|--------|-----|
| `write_notice` writes the message with `fputs` (raw echo) | 5: the four diag `turns_*` tests and `list_rejects_a_newline_timestamp_as_one_plain_line` |
| drop `c == '\t' \|\|` | `plain_error_keeps_a_tab`, `output_body_human_keeps_newline_and_tab` |
| ignore `keep_newline` (always keep `\n`) | the diag newline test and the CLI plain test |
| `keep_newline && c == '\v'` (never keep `\n`) | `output_body_human_keeps_newline_and_tab` |
| drop `\|\| c == ASCII_DEL` | `plain_error_turns_del_into_a_question_mark` |
| drop `c < ASCII_FIRST_PRINTABLE` | the newline, CR, ESC, and CLI tests |
| `write_notice` passes `keep_newline = true` | the diag newline test and the CLI plain test |
| `output_body_human` passes `keep_newline = false` | `output_body_human_keeps_newline_and_tab` |
| drop the duplicate-key `EXISTS` | both repeated-key store tests |
| drop `GROUP BY` | 15 store guard tests |
| `COUNT(*) > 2` | both repeated-key store tests |
| `COUNT(*) > 0` | 11 store guard tests |
| skip the stored-vector check | `store_update_int64_overflow_does_not_match_max`, `store_update_stored_repeated_device_key_is_stale` |

The body-side mutants first survived. No test checked `output_body_human`'s mapping, and this plan routes it through the new helper, so `output_body_human_keeps_newline_and_tab` (`tests/test_coverage_edges.c`) now locks the documented body contract.

**Probes on `build/remember`** (`--db` a fresh temp brain, `cat -vet`):
- `list --since $'bad\ninjected'` → `remember: invalid timestamp: bad?injected$`, exit 1. The same for `\r`. `$'bad\x1b[2Jx'` → `bad?[2Jx`. `$'d\x7fel'` → `d?el`. A tab stays a tab. An empty token gives `remember: invalid timestamp: $`. `'say "hi"\'` is unchanged.
- `--json-errors list --since $'bad\ninjected'` → one line with `bad\ninjected` escaped.
- A 300-byte token plus `\n` → 1 line plain (331 bytes), 1 line JSON (373 bytes).
- `$'bogus\nremember: forged'` as a command → one line, `'bogus?remember: forged'`.
- `update --if-version-vector '{"<dev>":1,"<dev>":1}'` → `remember: invalid version vector: {…}`, exit 1. The same with `:1,…:2` under `--json-errors` → one `usage` line. `'{"other":0,"<dev>":1}'` → exit 0, and the body updates.
- `sqlite3` 3.54 also rejects `{"a":1,"a":1}`: `json_each` decodes the key before `GROUP BY`. `{"a":1,"A":1}` passes (binary collation).

**Simplify pass.**
1. Applied: `output_body_human`'s header doc no longer restates the classifier and points at `output_text_human` (1 line).
2. Applied: `given_row_version_vector` replaces the two hand-rolled `UPDATE … version_vector` blocks in the int64 tests and serves the two new tests (−4 lines net in the old tests).
3. Declined: one table-driven test for the five diag cases would save about 40 lines, but `quality:test-the-contract` wants one behavior per test with no loops. Each byte class is its own clause, and its mutant names one test.
4. Declined: `write_preview` keeps its own `c < 0x20 || c == DEL` branch. It also maps tab, because a list line is one pipe-delimited row, so it is a different rule. Sharing it would need a third mode for one caller.
5. Declined: no helper for the store tests' `device` NULL guard. The guard is an early return, and a helper cannot carry one.

**Not covered here.** Bytes `>= 0x80` pass through unchanged (both decisions say so), so a raw C1 `0x9b` reaches an 8-bit terminal. Modern UTF-8 terminals do not act on it. Plain warnings share `write_notice` with errors, so the diag error tests prove both.

`vv_parse_pairs`: `git diff origin/main -- src/store_sqlite.c | grep -c vv_parse_pairs` → 0.

**Gate on `9fb7274`** (the code tree that is pushed; only these notes change after it): ctest 5/5 (ASan/UBSan, `REMEMBER_TEST_VFS=ON`), `lint-all.sh` LINT OK, `check-coverage.sh` functions 100% and effective lines 100%, `leaks-macos` PASS, `ci-linux.sh` OK (iwyu and scan-build PASS). The first lint run failed on `bugprone-easily-swappable-parameters` for `given_row_version_vector(db, vector)`. That got a per-line NOLINT, as in `tests/test_device.c`. Bugbot is not installed here.

## Review Notes

### 2026-10-19 — in-session review (coder)

**Verdict:** ready for the second opinion. No open blocking item. One grill, answered (`decision:plan17a-notice-shared-classifier`).

- DRY: one C0/DEL classifier for bodies and notices. `write_preview` keeps its own branch, a different rule (it maps tab).
- Easy-to-replace: the SQL stays in `store_sqlite.c`. `diag.c` reaches the classifier through `output.h`.
- Orthogonal: JSON escaping, `vv_parse_pairs`, import, and `vv_compare` have no diff.
- Fail fast: `plain_usage_line` returns NULL when the emitter fails, so a test cannot read a half-written line.
- Leftovers, not this plan: bytes `>= 0x80` pass through unchanged, and `write_preview` maps controls on its own.

Definition of Done, each box with its proof:
- A1: `plain_error_turns_a_newline_into_a_question_mark`, `…_carriage_return_…`, `plain_error_keeps_a_tab`, `…_an_escape_…`, `…_del_…`, `list_rejects_a_newline_timestamp_as_one_plain_line`.
- A2: `store_update_repeated_device_key_is_bad`, `store_update_stored_repeated_device_key_is_stale`, `store_update_two_device_version_vector_matches`.
- No change for ordinary bytes: `plain_error_keeps_quote_and_backslash`, `json_error_escapes_message_bytes`, `list_rejects_a_newline_timestamp_as_one_json_line`, `output_body_human_keeps_newline_and_tab`.
- `vv_parse_pairs` has no diff (grep above).
- Engineering notes: the Diagnostics and `store_update` bullets.
- Gate green: above.
- Still open: the second review and the INDEX ✅ tick.
