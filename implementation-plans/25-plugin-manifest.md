# 25 — Plugin manifest

**Module:** `src/plugin_manifest.c` and `src/plugin_manifest.h`.
**Needs:** plans 23 (`src/jsonread.c`) and 24. Both are merged. No other plan.
**Blocked on:** nothing. The copy is item R25-1 in `decision:2026-11-12-copy-batch-final` and item R25-2 in `decision:2026-11-14-copy-r25-2-manifest-approved` (approved 2026-11-14). The R25-2 sentences are in [`COPY-APPROVED-2026-11-14.md`](COPY-APPROVED-2026-11-14.md).

## Goal

`plugin.json` parses to the protocol schema, and every refusal uses a locked code and a locked sentence.

## Source

- `decision:plugins-manifest`
- `decision:2026-10-22-m-17`
- `decision:plugin-manifest-errors`
- `decision:2026-10-22-m-24`
- `decision:s-06`
- `decision:plugins-budget-defaults`, with its exit-4 sentence replaced by `decision:2026-11-11-filter-degraded-exit` (this parser has no exit code)
- `decision:2026-11-11-manifest-budget-range` (P5 A)
- `decision:2026-11-11-plan25-branch` (P2 B, G1 A)
- `decision:2026-11-12-e3-warnings-have-fix-field`
- `decision:g-11`
- `decision:plugins-process-kinds`
- `decision:2026-11-14-copy-r25-2-manifest-approved`: the R25-2 parser sentences (1 to 16 and the `<what>` phrases), with Q1 A (whole numbers by value), Q2 A (JSON string escaping of plugin-controlled text) and Q3 A (strings over 23 bytes).
- `decision:2026-11-12-plugin-manifest-copy-r25-2`, `decision:2026-11-12-manifest-version-unsupported`, `decision:2026-11-12-plugin-command-no-dotdot`, `decision:2026-11-13-plugin-manifest-limits` and `decision:2026-11-13-plugin-origins-empty`: the rules behind the R25-2 sentences.
- `schema/plugin/v1/manifest.json` and `schema/plugin/v1/common.json` (plan 24). Where a decision and the schema disagree, the decision wins.
- Sentences: [`COPY-PENDING.md`](COPY-PENDING.md) C-35 and C-36 (Itay approved that file on 2026-10-25), R25-1 in `decision:2026-11-12-copy-batch-final`, and R25-2 in [`COPY-APPROVED-2026-11-14.md`](COPY-APPROVED-2026-11-14.md). The coder prints those sentences and no others.
- Spec: `specs/PLUGIN-MANIFEST.md` (new; uppercase per P24 A).

## Out of scope

Install, trust, hook add, settings rows, processes, search, and the outbox. A hyphen in a typed command normalizes in a later plan. This plan does not rewrite a hyphen inside `plugin.json`. The human `add` line stays as it is. The plain sentence for `plugin_protocol_unsupported` is copy item R25-1 in `decision:2026-11-12-copy-batch-final`. This plan prints exactly that text: the plain line, the JSON `message`, and the JSON `fix`. Exit 1, `retryable` false. `<name>` is the plugin name, `<n>` the protocol number in the manifest, `<range>` what this remember reads (`1`, or `1 to 2` once a second protocol exists). `message`: `plugin.json /protocol: "<name>" was written for plugin protocol <n>, and this remember reads protocol <range>.` `fix`: `Update remember, or install an older version of "<name>".` Plain line: `remember: plugin.json /protocol: "<name>" was written for plugin protocol <n>, and this remember reads protocol <range>. Update remember, or install an older version of "<name>".` (The `plugin.json /protocol:` prefix follows PLUGIN-MANIFEST R2.4; R25-1's [ASSUMPTION] is whether the prefix also stays in `message` when `detail.path` carries it.)

## Standards

The standards for plans 25 and later in [INDEX](INDEX.md).

The parser is hand-written in this module. It does not add a library. It does not read the filesystem except the bytes the caller passes. Tests live in `tests/test_plugins.c`, suite `plugins`, and that suite is added to `tests/gate-suites`.

Branch rule (P2 B, G1 A). The local branch `plan/25-plugins` stays one branch and lands three pull requests in order. This plan's pull request carries only `src/plugin_manifest.c`, `src/plugin_manifest.h`, `tests/test_plugins.c`, the `CMakeLists.txt` lines for them, the `tests/gate-suites` line, the suite registration in `tests/register.h` and `tests/test_main.c`, `specs/PLUGIN-MANIFEST.md`, `specs/README.md`, `implementation-plans/COPY-REQUEST.md`, and this plan file. This plan's code was cut from the reviewed base `0aa22f3` on `review/plan-25-base` onto `main` after plan 23 merged. The branch's `src/brains.c`, `src/brains.h`, `src/brain_note.c`, `src/brain_note.h` and the `src/cmd_brain.c` changes are plan 26's pull request, cut from the same branch after this one merges. The branch's changes to `src/settings.c`, `src/settings.h`, `src/cmd_settings.c` and `tests/test_settings.c` stay on the branch, uncommitted to any pull request, until plan 30. Before this plan's pull request opens, `git diff origin/main --stat` lists only the files named in this paragraph.

## What must be true

- A document that `schema/plugin/v1/manifest.json` accepts parses. `format` is `remember-plugin`. `version` is 1. `protocol` is 1.
- A plugin name and an entry name match `^[a-z][a-z0-9_]{0,39}$`. A hyphen in a manifest name is `plugin_manifest_invalid`. The message is `"<name>" in plugin.json can't contain "-".` The fix is `Use "<value>".` `<value>` is the name with each `-` replaced by `_`.
- A manifest setting whose key is `enabled` is `plugin_manifest_invalid`. The message is `"<name>" declares a setting named "enabled", and that name is reserved.` The fix is `Rename that setting in plugin.json.`
- An unknown field on an object the schema closes (`additionalProperties` false), including `run`, `permissions`, and `filter`, is `plugin_manifest_invalid` and prints R25-2 sentence 7. `run`, `permissions` or `filter` at the top level prints sentence 7b. `detail.path` is a JSON Pointer. `detail.rule` is the schema rule id. The message names the object and the field, and the fix names the known field when exactly one is one edit away (`decision:plugin-manifest-errors`).
- An unknown field on an object the schema leaves open warns with the C-35 file line: `remember: warning: "<path>" has a field remember doesn't know, "<name>". The rest of the file was read.` The rest of the document still parses.
- `plugin add` and `plugin test` report every manifest error in that file in one result. This plan's tests call the parser. They do not add the commands.
- A `protocol` other than 1 returns `plugin_protocol_unsupported`. With a usable `name` it prints the R25-1 `message` and `fix`. With no usable `name` (missing, or not a string) it prints R25-2 sentence 9. A `version` other than 1 returns `plugin_protocol_unsupported` and prints sentence 10, with or without a usable name. `detail.supported` is `[1]` in all three.
- Sentences (R25-2). Every other refusal prints the R25-2 sentence it maps to, in [`COPY-APPROVED-2026-11-14.md`](COPY-APPROVED-2026-11-14.md): sentences 1 to 8 and 11 to 16, with the `<what>` phrase list for sentence 5. Each has a `message`, a `fix`, a `detail` of `{"path", "rule"}`, and the plain line `remember: <message> <fix>`. The code is `plugin_manifest_invalid`. One parse returns every error in the file, in document order, and a list over its limit names the list and reads no item past the limit.
- Whole numbers by value (R25-2 Q1 A). The parser reads every whole-number field by its value: `version`, `protocol`, `priority`, `data_version`, `max_batch_entries` and an integer setting. `1.0` and `1e0` parse as 1. `-0` parses as 0. `1.5` prints sentence 5 with `a whole number`. `<n>` in sentences 9 and 10 prints the value, never the source text, so `2.0` prints `2`.
- Plugin-controlled text (R25-2 Q2 A). A field name, a plugin, entry or setting name, a list item and every pointer segment print inside the double quotes and inside a pointer with JSON string escaping: `\"`, `\\`, `\n`, and `\u001b` for a control character. The plain line and the JSON `message` hold the same text, so one line never holds a terminal escape or a line break. A pointer segment applies the `~0` and `~1` rule first and the JSON escaping after. This covers the R25-1, C-35 and C-36 lines too, which print a name or a path.
- Long duration and size strings (R25-2 Q3 A). A duration or size string over 23 bytes (L13) prints the same sentence 5 phrase as any bad duration or size for that field: `a duration like "10m"`, `a size like "4MiB"`, or the range phrase where the field has one. The line has no "23" and no byte wording. A string of 23 bytes or fewer is judged by its value.
- An omitted `budget` is `800ms`. An omitted `cold_start_grace` is `10s`. An omitted filter `priority` is 500. An omitted action `process` is `once`. An omitted `origins` is `["local"]`.
- `cold_start_grace` of `0ms` is `plugin_manifest_invalid`. `1ms` and `30s` parse. A grace above `30s` is `plugin_manifest_invalid`.
- `budget` follows the same range (P5 A): `1ms` to `30s` inclusive parse; `0ms` and anything above `30s` are `plugin_manifest_invalid` with `detail.path` `/filters/<i>/budget` and `detail.rule` `budget_range`. (The first draft of this plan said `/entries/<i>/budget`. A manifest has no `entries` member, and `decision:plugin-manifest-errors` requires a pointer into `plugin.json`.)
- `priority`, `budget`, and `cold_start_grace` are manifest fields. This plan does not read a setting for them.

## Stop and ask

The coder stops and asks Itay, through the architect, at each of these. The plan does not choose.

1. Whether the Q2 escaping also covers U+007F and U+0080 to U+009F, which JSON string escaping leaves as they are and some terminals read as controls (U+009B starts a control sequence).
2. Whether Q2 reaches the already approved R25-1, C-35 and C-36 lines. The plan escapes them, because they print a name or a path; their text does not change for an ordinary name.
3. What `<n>` prints for a whole value that does not fit 64 bits (`1e30`), and what a value like `1e999` does.
4. The `<what>` phrase for a duration or size string over 23 bytes in a field that has no phrase of its own.

## Review points

- The reviewer checks that a hyphen in `plugin.json` is rejected and is not rewritten.
- The reviewer checks that an unknown field on a closed object is an error and an unknown field on an open object is the C-35 warning.
- The reviewer checks that the parser prints no sentence that this plan does not quote or point to.
- The reviewer checks that no name, field or pointer segment from the file reaches a line without JSON string escaping, and that `1.0`, `1e0` and `-0` are read by value in every whole-number field.
- The reviewer checks that the branch does not add a process, a trust file, or an outbox write.
- The reviewer checks `git diff origin/main --stat` against the branch rule above.

## Definition of Done

- [x] `a schema-valid manifest parses` (`tests/test_plugins.c`, the plan 24 hello fixture): `a_schema_valid_manifest_parses`, `a_full_schema_valid_manifest_keeps_every_field`.
- [x] `a hyphen in a manifest name prints the C-36 hyphen lines`: `a_hyphen_in_a_manifest_name_prints_the_c36_hyphen_lines`.
- [x] `a setting key enabled prints the C-36 reserved-key lines`: `a_setting_key_enabled_prints_the_c36_reserved_key_lines`.
- [x] `an unknown field on run is plugin_manifest_invalid with a JSON Pointer`: `an_unknown_field_on_run_is_plugin_manifest_invalid_with_a_json_pointer`.
- [x] `an unknown root field prints the C-35 file line and the rest parses`: `an_unknown_root_field_prints_the_c35_file_line_and_the_rest_parses` (the line only; the `fix` is deviation 2).
- [x] `two errors in one file both come back from one parse`: `two_errors_in_one_file_both_come_back_from_one_parse`.
- [x] `protocol 2 returns plugin_protocol_unsupported with the R25-1 message and fix`: `protocol_2_returns_plugin_protocol_unsupported_with_the_r25_1_message_and_fix`.
- [ ] `each R25-2 sentence 1 to 8 and 11 to 16 prints its plain line, message, fix and detail` (one test per sentence, expected text as literals).
- [ ] `sentence 9 and 10 return plugin_protocol_unsupported with and without a usable name`.
- [ ] `whole numbers 1.0, 1e0 and -0 parse in version, protocol, priority, data_version, max_batch_entries and an integer setting; 1.5 prints sentence 5 with a whole number`.
- [ ] `protocol 2.0 prints 2 in the R25-1 line and in sentence 9, never 2.0`.
- [ ] `a field name, a plugin name and a pointer segment with a quote, a backslash, a newline and 0x1b print JSON-escaped, and the plain line and the JSON message hold the same text`.
- [ ] `the R25-1, C-35 and C-36 lines escape a hostile name the same way`.
- [ ] `a 24-byte duration and a 24-byte size print the normal sentence 5 phrase with no 23-byte wording; a 23-byte string is judged by its value`.
- [x] `budget 0ms and 31s are invalid; 1ms and 30s parse`: `budget_0ms_and_31s_are_invalid_and_1ms_and_30s_parse`.
- [x] `omitted budget, grace, priority, process, and origins are the defaults above`: `omitted_budget_grace_priority_process_and_origins_are_the_defaults`.
- [x] `0ms grace is invalid, 1ms and 30s parse, and 31s is invalid`: `grace_0ms_is_invalid_1ms_and_30s_parse_and_31s_is_invalid`.
- [ ] `specs/PLUGIN-MANIFEST.md` is written, and the decisions this plan built are `state:implemented`.
- [ ] The gate passes on the committed tree, and the notes record the mutants and their red tests.

## Implementation Notes

Built from the reviewed base `0aa22f3` (`review/plan-25-base`), cut onto `main` after plan 23 merged. Left out: plan 26 code (`brains.c`, `brain_note.c`, the `brain list` changes, the lock), the plan 30 settings registry and the `settings.h` type changes, the old diag codes, and every change to `store`, `output`, `fileio` and `export`. `src/plugin_manifest.c` no longer includes `settings.h` types. It calls `setting_integer` only.

### What changed against the base

- Every sentence is R25-1, C-36, or a sentence of copy item R25-2, which Itay approved on 2026-11-14 (`decision:2026-11-14-copy-r25-2-manifest-approved`). The tests keep the repeated shapes in `R25_2_*` macros. The base had about 40 sentences of its own.
- The three rules that came with the approval are built and tested. Q1: a JSON number is a whole number by value, so `1.0`, `1e0`, `10e-1` and `-0` parse and `1.5` is not whole; `<n>` prints the value as plain digits, never the source text; an exponent that needs more than 64 digits is not whole. Q2: plugin-controlled text (field names, names, repeated items, the plugin name) prints with JSON string escaping in the message and the fix. Q3: a duration or size over 23 bytes gets the ordinary phrase of its field.
- `budget` errors carry rule `budget_range`. An unknown field on a closed object gets `Did you mean "<known>"?` when exactly one known field is one edit away. `PluginError.supported` carries `[1]` for `protocol` and for `version`.
- New with the approved text: a nesting error of its own (sentence 1b), `"actions" and "filters" are both missing`, a top-level `run`, `permissions` or `filter` is "can't be at the top level" (7b), a name used twice in one list is refused (8), each persistent entry that differs names the first one (12), the backfill error points at `/backfill/enabled` (13).
- A missing `name` no longer skips the checks after it. All errors come back, unbounded.
- The manifest holds the settings declarations as the schema allows them: optional `min` and `max`, a `string_list` default of strings (kept as its source JSON), `range` ignored on `boolean` and `string`.
- Duration and size strings follow the schema (`^\d+(ms|s|m|h)$`, `^\d+(B|KiB|MiB)$`), not the wider settings grammar. SemVer follows the schema regex. `needs` items follow the schema: a memory field, or `remember.` with two or three name segments (at most 96 characters), each once.
- The malloc seam reaches the manifest allocation, the sort buffer and every heap sentence, so the out-of-memory test covers all of them (`plugin_manifest_survives_each_allocation_failure`).

### Bugs the tests found in the base

- `description` was cut at 200 bytes, so 150 Hebrew letters (300 bytes) were refused as "the wrong type". It counts characters now (`a_description_counts_characters_so_150_hebrew_letters_fit`).
- A 96-character `remember.*` need did not fit its 96-byte buffer (`a_global_setting_key_of_96_characters_fits_and_97_does_not`). `PLUGIN_NEED_CAP` is 97.
- A SemVer suffix with a second `+` or an empty suffix passed the check (`plugin_version_is_a_semver`).
- A manifest that gives `protocol` as a string had no sentence. It is `must be a whole number` now.
- `memory.*` with other events, and a persistent entry with another command, now point at a path that exists in the file (X15).

### Deviations from the plan and from the review

1. `COPY-REQUEST.md` did not exist in the repository. The last one lived outside it (`fkm-staging/plans-rewrite-2026-11-11/COPY-REQUEST.md`). This change creates `implementation-plans/COPY-REQUEST.md` with item R25-2, now marked approved. The approved text is Story Weaver's revised draft (`fkm-staging/copy-2026-11-13/R25-2.md`), word for word. The file keeps the first draft too, under the approval line, and the plan 25 tests and `specs/PLUGIN-MANIFEST.md` hold the final text.
2. C-35 `fix` is not emitted. `diag_unknown_field` prints the C-35 line and has no `fix` for warnings. The E3B-1 text (item 26, "Remove that field, or update remember if the file came from a newer version.") belongs to whichever plan adds the `fix` field to `diag.c`. Itay accepted this (2026-11-13). The test feeds the parser's warning to `diag_unknown_field` and checks the file line.
3. The plan wrote the budget pointer as `/entries/<i>/budget`. A manifest has no `entries` member. The pointer is `/filters/<i>/budget` (`decision:plugin-manifest-errors` needs a pointer into the file). The plan text is fixed.
4. The tests also run inside `remember_store_tests` (ASan and UBSan), through `CMakeLists.txt` and one line in `tests/test_store_main.c`. The C-35 test needs `diag.c`, so it is built only into `remember_tests` (`REMEMBER_PLUGINS_DIAG_TEST`). This widens the diff boundary by that one line.
5. X1 and X2 (a NUL in a string or a key): plan 23's `jsonread` refuses `\u0000` as a syntax error, so the base's own NUL checks in `plugin_manifest.c` are gone. The tests (`a_name_with_a_nul_escape_is_not_json_so_nothing_is_cut_short`, `a_key_with_a_nul_escape_is_not_json_so_it_cannot_alias_a_field`) keep the guarantee on the manifest.
6. A hyphenated name always gets the C-36 hyphen lines, as the plan says, even when the folded name is still invalid. The fix then names the folded text.
7. The "`<a> to <b>`" form of R25-1's `<range>` is not built. One protocol and one format exist, and the second form would have no test. It is a `ponytail:` note at `check_supported`.
8. `specs/PLUGIN-PROTOCOL.md` R2.4 still shows the old `protocol 2 isn't supported by remember 0.3.0` example, and R2.11 says no entry-name collision rule exists. R25-1 and R25-2 sentence 8 replaced both. The spec file is outside this plan's boundary. `specs/PLUGIN-MANIFEST.md` states the current rules.
9. R25-2 sentence 8 (a name used twice in one list) adds a check the schema does not have. Itay approved the sentence, so it is built: two actions, two filters, or two settings with one name. An action and a filter may share a name.
10. `1e30` and similar whole numbers past a long long print their digits (up to 64). A number that needs more than 64 digits is "must be a whole number".
11. Decisions set to `state:implemented`: `decision:plugin-manifest-errors`, `decision:2026-11-11-manifest-budget-range`, `decision:2026-11-13-plugin-manifest-limits`, `decision:2026-11-13-plugin-origins-empty`, `decision:2026-11-12-plugin-manifest-copy-r25-2`, `decision:2026-11-12-manifest-version-unsupported`, `decision:2026-11-12-plugin-command-no-dotdot`, `decision:2026-11-14-copy-r25-2-manifest-approved`. Left `state:accepted` because later plans finish them: `decision:2026-10-22-m-17` (typed hyphens normalize in a later plan), `decision:2026-10-22-m-24` (the `setting_unknown` fix and `plugin status` are later plans), `decision:s-06`, `decision:g-11` and `decision:plugins-process-kinds` (the engine side is later), `decision:plugins-manifest` (install and trust are later).

### Proof

- 184 tests in suite `plugins` (`remember_tests`), and the same tests minus the C-35 test inside `remember_store_tests` (710 tests, 0 failed, ASan and UBSan).
- Limits L1 to L13: one `l<N>_...` test each parses at the limit and refuses one past it (L14 is no cap in this module, and the nesting cap is plan 23's, with its own sentence). Origins `[]` stays empty: `an_empty_origins_list_stays_empty`. Q1, Q2 and Q3: the `q1_`, `q2_` and `q3_` tests.
- Hostile input run: NUL escape, repeated keys, a 300-byte unknown key, 20000 keys in one object, nesting past 64, invalid UTF-8, shell metacharacters in a command (inert), a control byte, DEL and a C1 control in a name (escaped), an exponent of 999999999 (costs nothing), a 65-digit integer, and an out-of-memory failure at every allocation.

### Deep review (in session)

**Simplification (S).** (1) The base's registry (about 190 lines) is gone: plan 30. (2) One reader per field kind (`read_text`, `read_whole`, `read_duration`, `read_bool`, `read_list`) replaces about 40 hand-written error blocks, and one `must be <what>` pair replaces about 40 sentences. (3) The base's NUL checks (`key_is`, `json_string`) are gone because `jsonread` refuses a NUL. (4) Three guards that mutants showed to be dead are deleted: the 96-character cap in `global_key_ok` (the needs buffer is the cap), the dot count in `semver_ok` (the final count check covers it), and the length guard in `one_edit`. (5) Left alone: `vfmt_alloc` repeats `fmt_valloc` in `commands_common.c`. It is a second copy, and a `ponytail:` note says to share it when a third appears.

**Security audit and exploit hunt (A).** `plugin.json` is untrusted. Each row has a test. A NUL escape or an aliasing key: refused by `jsonread`. A repeated key: refused in every object. A name or key with a terminal escape, a newline, a quote, DEL or a C1 control: escaped in the sentence (Q2). A 300-byte key: kept whole, never cut. Nesting past 64: its own error. 20000 keys in one object: sorted once, no quadratic scan. A number with an exponent of 999999999 or 65 digits: refused in one pass. A relative `command[0]` with `..`: refused. A command with shell metacharacters: an inert argv string. Out of memory at each allocation: `PLUGIN_PARSE_OOM` and a NULL result. Every fixed buffer has its limit and a test at limit plus one, and the store binary runs them under ASan and UBSan. Not covered here: the file reader (plan 33) must cap the file size and open only a regular file, because this module reads bytes it is given.

### Mutants (116 built; each compiled fresh and run against `remember_tests --only plugins`, survivors also against the ASan binary)

111 killed (110 by the plugins suite, 1 by ASan). 5 survived, and none changes behavior:

- M10 and M11 (`one_edit`, same-length case): the guard against two equal strings. `one_edit` runs only after `names_have` found no match, so a field never equals a known name.
- Q1a (`whole_of`, the sign): with the sign ignored, the "-" stays in the digit text, so the printed value and the range checks are the same.
- Q1i (`whole_of`, `cut > count`): when the cut is one past the digits, the loop's first test fails on the first digit, which is never "0", and the result is the same.
- S7a (`err_unknown`, `near_count == 1`): no object of the manifest has two known fields one edit apart, so two near matches cannot happen.

**Harness problems found on the way (2026-11-14).** (1) Mutant M15 ("hyphen fold writes the dash back") loops forever. My first harness timed out only its `bash` wrapper, so the `remember_tests` child was orphaned and ran for 27 hours (PID 34024). It was killed. The suite itself never hangs. The harness now runs each mutant in its own process group, kills the group at 60 s, and every heavy command has a `timeout`. (2) Alternate mutants were run against the previous mutant's object file, so their "killed" results were stale, and one leftover object (mutant O01) made `an_empty_origins_list_stays_empty` fail in a later gate run. Every job script now deletes the plugin objects before it builds. The table below comes from the run after that fix.

| Mutant | What it changes | First red test |
|---|---|---|
| M01 | budget minimum 1ms -> 0 | `budget_0ms_and_31s_are_invalid_and_1ms_and_30s_parse` |
| M02 | budget maximum 30s -> 30s+1ms | `budget_0ms_and_31s_are_invalid_and_1ms_and_30s_parse` |
| M03 | budget rule id -> R2.13 | `budget_0ms_and_31s_are_invalid_and_1ms_and_30s_parse` |
| M04 | budget default 800 -> 801 | `omitted_budget_grace_priority_process_and_origins_are_the_defaults` |
| M05 | grace default 10s -> 10.001s | `omitted_budget_grace_priority_process_and_origins_are_the_defaults` |
| M06 | priority default 500 -> 501 | `omitted_budget_grace_priority_process_and_origins_are_the_defaults` |
| M07 | priority max 1000 -> 1001 | `priority_runs_from_0_to_1000` |
| M08 | repeated key not reported | a crash on the first failed check |
| M09 | closed object treated as open | `a_manifest_with_neither_actions_nor_filters_is_refused` |
| M10 | did-you-mean accepts two edits | equivalent (see below) |
| M11 | did-you-mean same-length: ignore position check | equivalent (see below) |
| M12 | pointer escape: slash not escaped | ASan in `remember_store_tests` (a heap overflow; no value changes) |
| M13 | pointer escape: tilde digit swapped | `unknown_field_pointer_escapes_slash_and_tilde` |
| M14 | hyphen detection looks for underscore | `a_hyphen_in_a_manifest_name_prints_the_c36_hyphen_lines` |
| M15 | hyphen fold writes dash back | a hang, killed by the harness after 60 s |
| M16 | name length limit 40 -> 41 | a crash on the first failed check |
| M17 | name first char accepts uppercase | `every_error_comes_back_past_sixteen` |
| M18 | global key needs 1 segment | `a_need_that_is_no_field_and_no_global_setting_is_refused` |
| M19 | global key max 3 -> 4 segments | `a_need_that_is_no_field_and_no_global_setting_is_refused` |
| M21 | dotdot segment length 2 -> 3 | `a_relative_command_with_a_dotdot_segment_is_refused` |
| M22 | dotdot checked for absolute paths too | `a_relative_command_with_a_dotdot_segment_is_refused` |
| M23 | dotdot checked on every argument | `only_the_program_path_is_checked_for_dotdot_not_its_arguments` |
| M24 | persistent commands compared inverted | `a_full_schema_valid_manifest_keeps_every_field` |
| M25 | commands_equal ignores argc | `a_second_persistent_action_with_another_command_points_at_that_action` |
| M26 | backfill without persistent not reported | `a_full_schema_valid_manifest_keeps_every_field` |
| M28 | semver suffix charset loses '-' | `plugin_version_is_a_semver` |
| M29 | semver accepts empty suffix | `plugin_version_is_a_semver` |
| M30 | supported compare inverted | `a_schema_valid_manifest_parses` |
| M31 | measure shape accepts no digits | `a_full_schema_valid_manifest_keeps_every_field` |
| M32 | duration lower bound exclusive | `budget_0ms_and_31s_are_invalid_and_1ms_and_30s_parse` |
| M33 | size upper bound exclusive | `backfill_limits_are_100_entries_and_4mib` |
| M34 | whole_in lower bound exclusive | `q1_priority_written_as_a_float_or_minus_zero_parses_by_value` |
| M35 | description counts bytes | `a_description_counts_characters_so_150_hebrew_letters_fit` |
| M36 | description limit 200 -> 201 | a crash on the first failed check |
| M37 | default origins -> sync | `omitted_budget_grace_priority_process_and_origins_are_the_defaults` |
| M38 | filter process not required | `a_filter_must_declare_process_persistent` |
| M39 | persistent flag inverted | `a_schema_valid_manifest_parses` |
| M40 | memory.* alone check inverted | `memory_star_must_be_the_only_event` |
| M41 | duplicate need not reported | `a_full_schema_valid_manifest_keeps_every_field` |
| M42 | scope brain keeps user | `a_setting_scope_defaults_to_user_and_reads_user_brain_and_both` |
| M43 | scope user keeps brain | `a_setting_scope_defaults_to_user_and_reads_user_brain_and_both` |
| M44 | numeric default below min allowed | `an_integer_range_checks_its_default_and_its_order` |
| M45 | numeric default above max allowed | `an_integer_range_checks_its_default_and_its_order` |
| M46 | min above max accepted | `a_min_one_above_max_is_refused` |
| M47 | enum default membership inverted | `an_enum_needs_a_range_with_values_and_a_default_in_them` |
| M48 | data_version upper bound removed | `data_version_and_idle_timeout_defaults_and_limits` |
| M49 | batch entries max 100 -> 101 | `omitted_budget_grace_priority_process_and_origins_are_the_defaults` |
| M50 | batch size max +1 | `omitted_budget_grace_priority_process_and_origins_are_the_defaults` |
| M51 | list max exclusive | `all_four_bins_fit_in_the_bins_list` |
| M52 | list minimum off by one | `omitted_budget_grace_priority_process_and_origins_are_the_defaults` |
| M53 | misplaced root members not refused | `a_schema_valid_manifest_parses` |
| M54 | action timeout default 60s -> 61s | `omitted_budget_grace_priority_process_and_origins_are_the_defaults` |
| M55 | unsupported status never returned | `protocol_2_returns_plugin_protocol_unsupported_with_the_r25_1_message_and_fix` |
| M56 | needs cap 16 -> 17 | `a_needs_list_has_at_most_sixteen_items` |
| M57 | argv cap 8 -> 9 | `a_command_is_required_and_is_a_short_list_of_nonempty_strings` |
| M58 | empty command string accepted | `a_command_is_required_and_is_a_short_list_of_nonempty_strings` |
| M59 | reserved enabled not checked | a crash on the first failed check |
| M61 | string_list default copy loses a byte | `a_string_list_default_keeps_its_strings_as_source_json` |
| M62 | repeated detection compares only length | `a_schema_valid_manifest_parses` |
| M63 | order within equal keys unstable | `a_repeated_key_is_the_later_one_and_errors_come_in_document_order` |
| M64 | error list capacity never grows past 8 | a crash on the first failed check |
| M65 | origins explicit empty list replaced by default | `an_empty_origins_list_stays_empty` |
| L01 | L1 entries cap 16 -> 17 | `actions_and_filters_are_lists_of_one_to_sixteen_entries` |
| L02 | L2 events cap 8 -> 9 | `events_are_a_short_list_of_known_event_types` |
| L04 | L4 argument cap 255 -> 256 | a crash on the first failed check |
| L06a | L6 tags.any cap 8 -> 9 | `a_tag_list_holds_at_most_eight_strings_of_63_bytes` |
| L06b | L6 tags.all cap 8 -> 9 | `l6_tags_all_holds_eight_strings` |
| L06c | L6 tag length cap 63 -> 64 | `l6_a_tag_holds_63_bytes` |
| L07a | L7 created_by cap 5 -> 6 | `l7_created_by_and_updated_by_hold_five_kinds` |
| L07b | L7 updated_by cap 5 -> 6 | `actor_kinds_are_the_five_the_schema_lists` |
| L07c | L7 bins cap 4 -> 5 | `l7_bins_hold_four` |
| L07d | L7 origins cap 3 -> 4 | `l7_origins_hold_three` |
| L08a | L8 not_caused_by cap 8 -> 9 | `l8_not_caused_by_holds_eight_strings_of_79_bytes` |
| L08b | L8 not_caused_by length 79 -> 80 | `bins_origins_and_not_caused_by_are_checked` |
| L09 | L9 settings cap 16 -> 17 | a crash on the first failed check |
| L10a | L10 enum values cap 8 -> 9 | `enum_values_are_one_to_eight_strings_of_63_bytes` |
| L10b | L10 enum value length 63 -> 64 | `an_enum_needs_a_range_with_values_and_a_default_in_them` |
| L11a | L11 string_list default 511 -> 512 | `l11_a_string_list_default_holds_511_bytes_of_source_json` |
| L11b | L11 string default 511 -> 512 | a crash on the first failed check |
| L12 | L12 plugin_version 63 -> 64 | `l12_a_plugin_version_holds_63_bytes` |
| L13 | L13 duration text 23 -> 24 | a crash on the first failed check |
| Q1a | whole_of: negative sign ignored | equivalent (see below) |
| Q1b | whole_of: fraction digits not counted | `q1_protocol_written_as_1_0_or_1e0_is_protocol_1` |
| Q1c | whole_of: a trailing-zero check inverted | `q1_protocol_written_as_1_0_or_1e0_is_protocol_1` |
| Q1d | whole_of: exponent sign ignored | `q1_protocol_written_as_1_0_or_1e0_is_protocol_1` |
| Q1e | whole_of: 19 digits still fit | `q1_a_nineteen_digit_protocol_prints_its_digits_and_not_a_saturated_value` |
| Q1f | whole_of: leading zeros kept | `q1_an_absurd_exponent_is_not_a_whole_number_and_costs_nothing` |
| Q1g | whole_of: digit cap 64 -> 65 | `q1_a_65_digit_integer_is_not_a_whole_number_and_a_64_digit_one_is_printed` |
| Q1h | whole_of: exponent shift past the digit cap allowed | `q1_a_whole_number_of_64_digits_is_printed_and_one_of_65_is_not_whole` |
| Q1i | whole_of: cut longer than the digits allowed | equivalent (see below) |
| Q2a | escape: double quote not escaped | `q2_an_unknown_field_name_prints_escaped_in_the_message_and_the_fix` |
| Q2b | escape: backslash not escaped | `q2_an_unknown_field_name_prints_escaped_in_the_message_and_the_fix` |
| Q2c | escape: DEL not escaped | `q2_an_escape_a_delete_and_a_c1_control_print_as_u_escapes` |
| Q2d | escape: C1 controls not escaped | `q2_an_escape_a_delete_and_a_c1_control_print_as_u_escapes` |
| Q2e | escape: C0 limit 0x20 -> 0x1f | `q2_a_unit_separator_escapes_and_a_space_does_not` |
| Q2f | escape: C1 upper bound 0x9f -> 0xa0 | `q2_text_that_is_not_a_control_prints_as_written` |
| Q2g | escape: the unknown field name printed raw | `q2_an_unknown_field_name_prints_escaped_in_the_message_and_the_fix` |
| Q2h | escape: the repeated item printed raw | `q2_a_repeated_key_prints_escaped` |
| Q2i | escape: the bad name printed raw | `q2_an_invalid_name_and_a_hyphen_name_print_escaped` |
| Q2j | escape: the plugin name in a protocol error printed raw | `q2_the_plugin_name_in_a_protocol_error_prints_escaped` |
| Q2k | escape: the hyphen fix printed raw | `q2_an_invalid_name_and_a_hyphen_name_print_escaped` |
| Q2l | escape: a tab is not escaped as \t | `q2_an_escape_a_delete_and_a_c1_control_print_as_u_escapes` |
| S4 | sentence 4: only one list named | `a_manifest_with_neither_actions_nor_filters_is_refused` |
| S7a | unknown field fix: two near fields still get did-you-mean | equivalent (see below) |
| S7b | unknown field fix: none near still gets did-you-mean | `an_unknown_field_on_run_is_plugin_manifest_invalid_with_a_json_pointer` |
| S7c | closed filter block rule R2.21 -> R2.5 | `an_unknown_field_in_an_action_filter_block_is_invalid` |
| S7d | top-level filter rule R2.21 -> R2.5 | `top_level_run_permissions_and_filter_are_errors_not_warnings` |
| S7e | top-level filter fix names a filter | `top_level_run_permissions_and_filter_are_errors_not_warnings` |
| S8a | repeated name check skips earlier uses | `two_actions_with_one_name_are_refused` |
| S8b | repeated name fix says keep one | `two_actions_with_one_name_are_refused` |
| S8c | repeated setting key check skips earlier uses | `two_settings_with_one_key_are_refused` |
| S12a | persistent errors stop after the first | `each_persistent_entry_that_differs_gets_its_own_error` |
| S12b | persistent error names itself instead of the first | `a_second_persistent_command_points_at_its_own_entry` |
| S13 | backfill error at /backfill | `backfill_needs_a_persistent_entry` |
| S1b | nesting reported as invalid JSON | `nesting_past_64_levels_has_its_own_sentence` |
| O01 | origins [] replaced by the default | `an_empty_origins_list_stays_empty` |

### Gate

Run on commit `0c62431` (2026-11-15), every stage in the shared lock with a `timeout`, the machine rebooted once before it and no stage was skipped or reused. The only commit after it changes this plan file. Plan objects were deleted before each build (see the harness note above).

| Stage | Result |
|---|---|
| Build with `-Werror` (Debug, ASan and UBSan, `REMEMBER_TEST_VFS=ON`) and `ctest` | `step_gate`, `step05_gate`, `store_asan_gate` (710 sanitized tests) and `no_shim_symbols` pass. `install_skill` and `spec_drift` pass in a separate run. |
| `scripts/lint-all.sh` (CI=true) | LINT OK: format, boundaries, cppcheck, gcc `-fanalyzer`, clang-tidy at the LLVM 22 pin, scan-build. IWYU is skipped on macOS and runs in the Docker stage. |
| `scripts/check-coverage.sh` | Functions 100%, effective lines 100%, must-cover lines 0, branch sides of the pure TUs 100%. |
| `scripts/ci-linux.sh` (Docker, with IWYU) | OK |
| `cmake --build build --target leaks-macos` | PASS |
| `scripts/codeql-local.sh` (last) | OK. The first run found 10 `cpp/overflowing-snprintf` alerts (an `snprintf` result used as an offset): in `enum_phrase` and the test helpers. Fixed, not allowed: every offset comes from `strlen` now. The 43 allowed alerts were in `scripts/codeql-allow.tsv` before this change. |

The mutation pass ran on the sources of `bbd9ca3`. The commit after it (`0c62431`) changed only how `enum_phrase` and the test helpers compute an offset. The tests of those two pass in the gate above.

Plan DoD, last box: ticked below.

## Review Notes
