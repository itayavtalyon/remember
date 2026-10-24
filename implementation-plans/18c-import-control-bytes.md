# 18c — Import rejects non-canonical keys and tags; human output masks control bytes

Follow-up to [plan 18b](18b-branch-coverage-gate.md). The 18b final review found
that `import --from-db` copies a peer's key and tags without re-checking them.
A source row with key `evil<ESC>[2J` imports (`inserted 1`, exit 0), and human
`list` / `tags` print the ESC raw. A probe on 0.2.0 showed the peer's
`updated_at` reaches `list` raw too.

## Source of truth

- `decision:import-control-bytes-followup` (Itay 2026-10-22: both fixes; skip and count; every peer string).
- Plan 18b's branch gate holds for `output.c`: every branch side tested, no
  `assert`, write failures through `tests/failing_stream.c`.

Baseline: `origin/plan18-union-search` at `20e2d3d` (main lacks 18b).

## Behavior

1. **Import.** A source row is canonical when its key (if any) and every tag
   pass `normalize_token` and come back byte-identical. A non-canonical row is
   skipped before the insert/merge step and counted in
   `StoreImportCounts.rejected`. Nothing about that row is written. Links to it
   are skipped the way links to a missing row already are. Exit 0.
   Amended by the deep review (`decision:plan18c-validate-peer-fields`): the
   row must also have a canonical UUID v7 `sync_id`, fixed-shape timestamps,
   a compact canonical version vector, a valid `source`, and a `body_hash` that
   matches the body.
2. **Envelope.** JSON gains `"rejected":N` after `"conflicts"`. The plain line
   gains ` rejected N`.
3. **Human output.** Every raw print of stored text goes through
   `output_text_human(out, s, false)`: `list` key, tags and `updated_at`;
   `tags` name; `related` neighbor key. C0 (except tab) and DEL print as `?`.
   JSON is unchanged (it already escapes). Amended by the deep review:
   UTF-8 C1 controls (U+0080..U+009F) also print as `?`
   (`decision:plan18c-mask-c1`); JSON refuses a non-canonical version vector
   and `conflicts` refuses a stored snapshot that is not a JSON object.

## Steps (TDD, one red test each)

1. Store: `store_import_rejects_noncanonical_key_and_tag` (ESC key, ESC tag,
   uppercase key, valid row alongside: counts 1 inserted / 2+ rejected; dst has
   only the valid row). Then envelope and plain-line tests.
2. Output: `output_entry_human_line` / `output_tags_human` /
   `output_related_human` with ESC in each field print `?`; failing-stream tests
   for each new write path. Branch gate stays 0 missed.
3. Docs: SKILL.md import line, engineering notes import paragraph, `output.c`
   comments.

## Definition of done

- The repro prints no raw ESC (`list | od -c`).
- Full gate green: clang + sanitizers + `REMEMBER_TEST_VFS`, `ctest`,
  `scripts/lint-all.sh`, `scripts/check-coverage.sh`, `leaks-macos`,
  `scripts/ci-linux.sh`.
- Decision recorded (done 2026-10-22).

## Implementation Notes

**Probes before the fix** (installed 0.2.0 and this branch's first build):
a peer key `evil<ESC>[2J`, tag `t<ESC>[1m` and `updated_at ...Z<ESC>[31m`
all reached `list` / `tags` raw (`cat -v` showed `^[`). A peer `sync_id`
with ESC reached plain `conflicts` raw (`1	01a1...^[[2J	key_clash`), so
that site is masked too ("every peer string"). JSON already escaped all of it.

**After:** the repro prints `inserted 0 updated 0 unchanged 0 conflicts 0
rejected 1` and `list | od -c` has no `033`. An `updated_at`-only peer
imports and lists as `...Z?[31m`.

**Canonical check.** `import_row_canonical` in `store_sqlite.c`: the key (if
not NULL) and every tag must pass `normalize_token` and come back byte-identical.
It runs at the top of `import_one_entry`, so it covers insert and merge.
`normalize_token`'s rules are ASCII-only (trim, controls, length 64, A-Z fold),
so a row written by any local build round-trips.

**Tests.** `cli_import_skips_noncanonical_key_and_tag_rows` (ESC key, ESC tag,
uppercase key, uppercase tag, 300-byte key, empty key; a valid keyed row and a
valid keyless row import; JSON counts literal),
`cli_import_skips_noncanonical_row_already_present` (a dominating VV that
otherwise gave `updated 1`; plain line literal),
`cli_conflicts_human_masks_a_control_byte_in_the_sync_id`,
`output_entry_human_line_masks_control_bytes_in_key_tags_and_updated_at`,
`output_tags_human_masks_control_bytes`, `output_related_human_masks_a_neighbor_key`.
Each masked field carries a `\n` so a `keep_newline=true` mutant goes red. The
existing `*_reports_every_failed_write` tests drive the rerouted writes through
`tests/failing_stream.c`.

**One-clause mutants (all red):** M1 drop `strcmp` (key_and_tag_rows);
M2 drop `== NORM_OK` (key_and_tag_rows, via the empty key); M3 drop the key
check (both import tests); M4 drop `key != NULL` (7 keyless import tests);
M5 drop the tag check; M6 drop `rejected++`; M7 guard never true
(`&& row->foreign_id < 0`); M8 JSON prints `inserted` for `rejected`; M9 plain
line the same; O1-O10 each `output_text_human` call back to raw `fputs`, or to
`keep_newline=true` (the matching output test); C1/C2 the same for the
`conflicts` sync_id.

**Simplify pass.** (1) `write_text_or_nothing` deleted: its one remaining
caller passes a stack buffer, so its NULL side could not run (-8 lines, one
branch pair). (2) `output_tags_human` drops the `name != NULL ? : empty_str()`
ternary; `output_text_human` already writes nothing for NULL (-1 branch pair).
(3) The tests share `given_added` instead of five open-coded run/assert/free
triples (-15 lines). Not shrunk: the test that checks the import counts and
the per-field output tests prove different layers (store skip vs. writer masking).

**Left open (not in the decision; closed by the deep review below):** import does not check a peer's
`sync_id` shape or timestamps. Output masks them now; a non-UUID `sync_id`
is still unaddressable by `--sync-id`. `fix/plain-stderr-control-bytes`
predates 17a's `output_text_human(…, keep_newline)` and conflicts with it
(different flag meaning: it also maps tab).

**Toolchain skew.** The first gate run had ci-linux lint green while macOS
clang-tidy (brew LLVM) failed `readability-implicit-bool-conversion` on
`return a == b && c == 0;` in a `bool` function. The predicate now uses the
file's `if (...) { return false; } return true;` form. M1 and M2 were re-run on
that form and are still red.

## Review Notes

### 2026-10-22 — deep review (Claude, second opinion on `81eb777`)

Baseline gate on a clean worktree of `81eb777`: `GATE-ALL-GREEN`. The key/tag fix and the C0 mask work as claimed (repro re-run; the coder's M1 and M4 re-run red in `cli_import_skips_noncanonical_key_and_tag_rows` and the keyless import tests). The review found the same trust gap in the peer fields 18c did not check, and Itay widened the plan. Four grills, all answered:

- **blocking (fixed): JSON injection through a peer version vector.** Import stored `version_vector` byte for byte and `output.c` writes it raw (`write_json_raw_object`, and the store's conflict snapshots). Probe: a peer VV `{"a":1},"pwn":true,"z":{` made `--json get` print an injected `"pwn":true` (invalid JSON overall) and `--json conflicts` failed `json.load`; a VV with ESC put a raw `033` in JSON. A last-key-wins parser would let a peer forge any field an agent reads. Itay: validate at import and guard the output, with every device id a UUID and every value an integer (`decision:plan18c-validate-peer-fields`).
- **important (fixed): peer `sync_id` and timestamps.** A tab in a peer `sync_id` passed the mask (tab is kept by design) and forged columns in plain `conflicts` (`1⇥<uuid>⇥999⇥forged⇥key_clash`). Itay: validate both at import.
- **important (fixed): `source` and `body_hash`.** A peer could store any `source` string (a closed enum in JSON) and any `body_hash`, which keyless merge and `hash_clash` trust. Itay: validate both.
- **important (fixed): UTF-8 C1 controls.** `normalize_token` accepts U+0080..U+009F (so even a local key can hold U+009B, the 8-bit CSI), and the C0/DEL mask passed them. Itay: mask them in human output now (`decision:plan18c-mask-c1`); the token rule is unchanged.
- **nit (open):** the harness's `sqlite_exec` silently does nothing when the SQL is longer than `CMD_BUFSIZE` (1024): a first version of the new import test failed only because its fixture SQL never ran. `harness_sqlite_query_line` returns NULL for both "no row" and "not run", so the helper cannot tell; a follow-up should make it fail loud.
- **nit (open):** a peer body is not checked against the local body rules (trim, 64 KiB, valid UTF-8). Not decided; output masks it and JSON escapes it.

Assumptions checked before locking (so no legitimate row is rejected): `normalize_token`'s rules are unchanged since v0.1.0; every release writes the fixed millisecond stamp (step 07, 2026-07-29, before v0.1.0); device ids are sidecar-validated UUID v7, so every VV key remember writes is one; the plan 17 `STORE_ERR_BAD_VV` rule already says "non-negative integers", so the grammar takes those; `decision:plan17a-duplicate-vv-key` makes a repeated device malformed. Itay's brain, read-only: 743 rows, 0 that would fail any rule (timestamps, sync_id, VV keys and values, VV whitespace, source, body hash).

What changed:

- `normalize.c` (branch-gated): `uuid_v7_is_canonical` (moved from the store's private `uuid_is_canonical`, which is gone), `utc_stamp_is_canonical`, `vv_json_is_canonical` (strict compact grammar, no device twice, counters of at most 18 digits so no range check), and `source_is_valid` (moved from `commands_common.c`, so the vocabulary has one home the store can use).
- `store_sqlite.c`: `import_row_valid` (was `import_row_canonical`) checks every stored peer field; the store's own snapshot serializer requires a canonical VV; `store_conflicts_list` refuses a snapshot that is not a JSON object (SQLite `json_valid` + `json_type`), because output prints snapshots raw.
- `output.c`: `write_entry_core` requires a canonical VV instead of `[0] == '{'`; `output_text_human` and the preview mask C2 80..C2 9F as one `?`.
- Fixtures: several import tests forged `body_hash` (`dddd…`, `aaaa…`, `abab…`); they now use real hashes (`force_body` computes it; literals from `shasum`). Two `hash_clash` fixtures gave a different body another row's hash, which is the forgery the check stops; they now copy the local row's body and hash together. `cli_import_bad_vv_fails_cleanly` became `cli_import_bad_vv_row_is_rejected` (exit 0, `rejected 1`): skip-and-count replaces the old abort. The conflicts-mask test now plants the ESC in a stored conflict (the residue case), since import no longer admits it.
- New tests: 9 validator tests, 4 output tests, `cli_import_skips_rows_with_a_non_canonical_peer_field` (11 rows, each breaking one rule, plus one valid), `cli_conflicts_refuses_a_snapshot_that_is_not_an_object`, `cli_import_refuses_to_snapshot_a_non_canonical_local_vector`.
- Docs: SKILL.md (`rejected`), engineering notes (import paragraph).

Mutation table (one clause each; `remember_store_tests` + `remember_tests --only sync_import,sync_output`, ASan build; restored and diffed after each):

| Mutant | Result |
|---|---|
| I1–I8: drop each `import_row_valid` clause (sync_id, source, created_at, updated_at, expires, deleted, VV, hash compared to itself) | each killed by `cli_import_skips_rows_with_a_non_canonical_peer_field` (VV also by `cli_import_bad_vv_row_is_rejected`) |
| I9: a NULL stamp rejected | killed (11+ tests) |
| N1/N2 hex bounds, N3 one dash position, N4 version, N5 a variant, N6 trailing NUL | killed (unit + import tests) |
| N7 stamp digit bound, N8 stamp trailing byte | killed |
| N9 18→19 digits, N10 leading zero, N11 empty counter | killed (`vv_json_rejects_a_counter_…`) |
| N12 repeat after a comma unseen, N13 repeat never seen | killed (`vv_json_rejects_a_repeated_device`) |
| N14 trailing bytes, N16 closing quote, N17 colon | killed |
| N15 a non-comma separator accepted | survived; killed after adding `…:1;"<uuid>":2}` |
| N18 a source token dropped | killed |
| O1 VV guard back to NULL-only | killed |
| O2/O3/O4 C1 range and lead byte, O5 second byte not skipped, O6 preview clause | killed |
| S1 snapshot guard never fires, S2/S3 one snapshot's type unchecked | killed (`cli_conflicts_refuses_…`) |
| S4 store snapshot serializer VV weakened | killed (`cli_import_refuses_to_snapshot_…`) |

Reviewer's own misses, recorded: I ran clang-format but not clang-tidy before the first fix-tree gate, which failed lint on three findings (a 2-letter parameter name, two swappable `const char *` parameters, bool → int); fixed (an offset replaces the second pointer) and the gate re-run from the start. The second run passed macOS lint and coverage, then Linux include-cleaner flagged `<ctype.h>` in `store_sqlite.c` (its only users left with `uuid_is_canonical`); removed, N12/N13 re-run on the refactored helper (both still red), gate re-run. The first mutation runner crashed on non-UTF-8 test output mid-mutant and left N3 applied (found, restored, runner fixed, resumed); a shell-quoted M1 re-run did not compile and proved nothing (redone with a script).

Coder rating (18c as handed in): **7.5 / 10.** Strengths: a clean, tested key/tag fix; probes on 0.2.0 before coding; masking every human print site it found, including the plain `conflicts` sync_id; honest "left open" notes; mutants per clause. Misses: stopped at the fields the decision named, while the same trust boundary covered every peer field import stores (the VV injection was the worst hole and was one grep away: `write_json_raw_object`); listed `sync_id`/timestamps as open instead of grilling them; fixtures forged hashes, which hid that import trusted `body_hash`.

Instructions for the coder:

1. When a fix closes one field at a trust boundary, list every field that crosses the same boundary and how each is written out (raw, escaped, masked), and grill the ones the decision does not name.
2. Grep for raw writers (`fputs`, `%s`, raw JSON) of any data that crosses a trust boundary before calling a fix complete.
3. Fixtures write data a real writer could produce (real hashes, real UUIDs, real stamps); a forged fixture hides the validation that is missing.

Gate (fix tree, 2026-10-22; clang, `REMEMBER_ENABLE_SANITIZERS=ON`, `REMEMBER_TEST_VFS=ON`, configured twice, cache flags checked): build, ctest, `lint-all.sh`, `check-coverage.sh` (search_terms 176, unicode 80, normalize 268, output 572 sides, 0 missed), `leaks-macos`, `ci-linux.sh` → `GATE-ALL-GREEN`. After the gate only this file changed (Markdown). Live probes on the fixed binary: the VV injection, a UUID-keyed VV forging a duplicate `"key"`, and a tab `sync_id` each import as `rejected 1`; `--json conflicts` and `--json list` parse; `list` prints no raw C2 9B. A copy of Itay's brain (`.backup`, read-only) imports into a fresh db as `inserted 760 … rejected 0`.
