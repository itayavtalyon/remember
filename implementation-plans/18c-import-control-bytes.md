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
2. **Envelope.** JSON gains `"rejected":N` after `"conflicts"`. The plain line
   gains ` rejected N`.
3. **Human output.** Every raw print of stored text goes through
   `output_text_human(out, s, false)`: `list` key, tags and `updated_at`;
   `tags` name; `related` neighbor key. C0 (except tab) and DEL print as `?`.
   JSON is unchanged (it already escapes).

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

**Left open (not in the decision):** import does not check a peer's
`sync_id` shape or timestamps. Output masks them now; a non-UUID `sync_id`
is still unaddressable by `--sync-id`. `fix/plain-stderr-control-bytes`
predates 17a's `output_text_human(…, keep_newline)` and conflicts with it
(different flag meaning: it also maps tab).

**Toolchain skew.** The first gate run had ci-linux lint green while macOS
clang-tidy (brew LLVM) failed `readability-implicit-bool-conversion` on
`return a == b && c == 0;` in a `bool` function. The predicate now uses the
file's `if (...) { return false; } return true;` form. M1 and M2 were re-run on
that form and are still red.
