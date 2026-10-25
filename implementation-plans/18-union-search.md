# 18 — Union search with keys and tags (006 §2, §9 query side)

## Goal

One `remember search QUERY` that works as you type, for people, apps and
agents:

- It matches word **prefixes** (`boo` finds "Book") over keys, bodies and tags.
- Raw FTS5 syntax still works (`book OR zed`).
- It **never fails on its input**. A raw part that FTS5 cannot parse becomes a
  warning on exit 0, and the prefix results still print.
- It returns the matching **keys** and **tags** in scope (facets), with a
  `--facet-limit`.
- Hebrew niqqud is folded on the query side too.

## Source of truth

[`design-logs/006-ui-overhaul-engine.md`](../design-logs/006-ui-overhaul-engine.md)
§0 (EN-01, EN-02 and the stated E2.6 exception), §2 (E2.1–E2.8, E3.1–E3.3,
E5.3), §9 (E10.1), Examples, Verification E2/E10, plus the locked decisions
below. Do not invent. If 006 and the locked decisions do not decide a case,
stop and ask Itay (grill), then record the answer under Implementation Notes.

## Context (standalone summary)

Baseline: `main` at `a27efca` with plans 15, 16 and 17 merged (required:
see Prerequisites).

- After plan 15, `entries_fts` is `fts5(key, body, tags, tokenize='unicode61
  remove_diacritics 2')`, and FTS rows already have the Hebrew marks folded.
- Today `search` trims the query (ASCII whitespace), rejects an empty query
  with `usage` `empty search query`, passes the text as a raw FTS5 MATCH, ranks
  `bm25(entries_fts)` then `updated_at DESC, id DESC`, and maps FTS syntax
  errors to `STORE_ERR_QUERY` (`invalid search query`).
- `store_search` runs COUNT + page in one read transaction. The JSON envelope
  is `{"version":1,"offset","limit","count","total","entries":[…]}`
  (`output_list_envelope`).
- FTS5 needs the real table name in `MATCH` and `bm25()`; an alias fails with
  `no such column`.
- After plan 16, there is a diag emitter with warnings: plain `remember: note:
  <text>`; with `--json-errors` `{"version":1,"warning":{"code","message"}}`.
- After plan 17, `search` takes `--since` / `--until` on `updated_at` and
  validates, then ignores, `--sort`.

Rules this plan implements (restated from 006):

| ID | Rule |
|----|------|
| E2.1 | `search QUERY` is the only search. Filters `--tag`, `--source`, `--key`, `--limit`, `--offset`, `--expired`, `--deleted`, `--since`, `--until` apply. `--facet-limit N` (default 50, max 200) caps the `keys` and `tags` items. Outside 1–200 → exit 1 `usage` `invalid --facet-limit (must be 1..200)`. |
| E2.1 (union) | P = prefix part (E2.2). R = QUERY trimmed of ASCII whitespace. (3) R non-empty and parses alone → run `(<P>) OR (<R>)`; P empty → R alone. (4) R empty → P alone, no warning; P also empty → empty result: exit 0, `count` 0, `total` 0, empty `keys` and `tags`. (5) R non-empty and does not parse → P alone + warning `raw_query_ignored`; P empty → empty result, warning still written. |
| E2.1a | "Parses alone": prepare `SELECT 1 FROM entries_fts WHERE entries_fts MATCH ?1 LIMIT 1`, bind R, step once on the same connection and transaction. `SQLITE_ROW` or `SQLITE_DONE` → parses. `SQLITE_ERROR` with a message starting `fts5:`, `no such column`, `unknown special query` (**or `unterminated string`, D1**) → does not parse; keep the message. Anything else → real error (E3.2). |
| E2.1b | Each row appears once. `count`, `total`, `--offset`, `--limit` apply to the de-duplicated set. |
| E2.2 | P: (1) NFC; split on Unicode whitespace and on every ASCII character outside `[A-Za-z0-9]`; non-ASCII stays inside tokens. (2) Drop empty tokens; keep the first 16 tokens and the first 64 code points of each. (3) Remove the E10.1 Hebrew marks. (4) Per token: double every `"`, wrap in `"…"`, append `*`. (5) Join with one space (implicit AND). (6) No token → P empty. |
| E2.3 | Unparseable raw part: exit 0, prefix results, one warning: plain `remember: note: raw query ignored: <fts message>`; JSON `{"version":1,"warning":{"code":"raw_query_ignored","message":"<fts message>"}}`. |
| E2.4 | Order, with q = QUERY trimmed and ASCII-lowercased: (1) `e.key = q`; (2) `substr(e.key, 1, length(q)) = q`; (3) `bm25(entries_fts, 10.0, 1.0, 4.0) ASC`; (4) `e.updated_at DESC, e.id DESC`. |
| E2.5 | P tokens ASCII-lowercased = t1…tn in order. A key or tag splits into segments on `:`, `-`, `_`, `.`, `/` and whitespace; segment order kept. |
| E2.5a | A token matches a segment when the segment starts with the token. One segment may match several tokens. |
| E2.5b | A key or tag text-matches when every token matches one of its segments, **or** q is a prefix of the whole key or tag. |
| E2.5c | Word order does not decide matching; it decides key rank (E2.6). |
| E2.5d | A key or tag is returned only when it text-matches **and** belongs to at least one memory in the active bin and filters. A key belongs to its memory; a tag to every memory that carries it. (Scope = bin + filters, **not** the query match.) |
| E2.6 | `--json` adds `keys` and `tags` **between `total` and `entries`** (stated exception to EN-01/EN-02; What's New names it): `{"version":1,"offset":0,"limit":20,"count":2,"total":2,"keys":{"total":1,"items":[{"key":"recs:books","sync_id":"…","label":"Book recommendations","source":"agent","updated_at":"…Z"}]},"tags":{"total":1,"items":[{"tag":"type:books","count":1}]},"entries":[…]}`. |
| E2.6 keys | Tier 1: q is a prefix of the whole key. Tier 2 (not tier 1): tokens match segments in order — t1 at index i1, each later token at an index ≥ the previous one. Tier 3: every other text-match. Inside a tier: ascending Unicode code point order of the stored key. |
| E2.6 tags | Exact tag first, then `count DESC`, then tag (**amended by D28: tiers between exact and count**). `count` = memories in the active bin and filters that carry it. |
| E2.6 misc | `total` = number of scoped matches; `items` ≤ `--facet-limit`. `label` = the `# ` title, else the key, else the first 60 graphemes of the plain text + "…". `--offset`/`--limit` page `entries` only. |
| E2.7 | Human output: `keys: k1, k2, …` and `tags: t1 (n), …` lines before the entry lines, only when not empty, at most 10 each, then `… and N more`. Same orders as E2.6. |
| E2.8 | Release note text, verbatim: "`remember search` now also matches word prefixes and returns matching keys and tags. FTS5 queries work as before; a query that FTS5 cannot parse no longer fails: it returns prefix matches and prints a note on stderr." |
| E3.1–E3.3 | Unparseable input is never an error. A real FTS/SQLite error is never swallowed: exit 1, empty stdout, E4 code + text. `search` no longer produces `invalid search query`; code `invalid_query` stays reserved. |
| E10.1 | Prefix tokens drop U+0591–U+05BD, U+05BF, U+05C1–U+05C2, U+05C4–U+05C5, U+05C7. The raw part R is **not** folded. |

## Locked decisions (grill 2026-10-12; index in [`006-locked-decisions.md`](006-locked-decisions.md))

| # | Decision |
|---|----------|
| D1 | E2.1a also treats a message starting `unterminated string` as "does not parse". Observed on the vendored SQLite 3.53.3: `"` and `"abc` → `unterminated string`; `C++` → `fts5: syntax error near "+"`; `foo AND`, `NEAR(`, `(` → `fts5: syntax error near ""`; `recs:books`, `-x` → `no such column: …`; `*` → `unknown special query: `; 2000 nested `(` → `fts5: parser stack overflow`. `key:recs` parses (column filter). |
| D2 | Vendor **utf8proc** (MIT) through the vendoring checklist (`docs/engineering-notes.md` § Vendoring). Single consumer: new `src/unicode.c` behind a project port `src/unicode.h` exposing NFC, a grapheme-break iterator, general category, and an "is Unicode whitespace" test (White_Space property). No other TU includes `utf8proc.h`. |
| D12 | Title = the body's first line when it starts with `# `: the text after `# `, ASCII-trimmed; if that is empty there is no title. Plain text = the body with every run of whitespace or control characters collapsed to one space (then trimmed). No markdown stripping. |
| D13 | Pure TU `src/search_terms.c/.h` (no I/O, no SQLite): QUERY → token list (E2.2 steps 1–3, 16/64 caps); key/tag segmenting and E2.5 match; E2.6 key tiers + code-point order; tag order; `label`. The adapter (`store_sqlite.c`) owns: FTS5 quoting of P (E2.2 steps 4–5), the E2.1a parse probe, the union query, the E2.4 ranking SQL, and fetching the candidate keys and tags in scope (bin + filters). The C side then filters and ranks those candidates with `search_terms.c`. |
| D27 | A QUERY that is not valid UTF-8 → exit 1 `usage` `invalid UTF-8 in search query` (checked before any other work). |
| D28 | Tag item order: exact tag, then tier 1/2/3 using the E2.6 key tier rules on the tag text, then `count DESC`, then tag (code-point order). Human `tags:` line uses the same order. |
| P2 | The `…` suffix on `label` is written only when the 60-grapheme cut actually truncates. |

Anything 006 and this table do not decide: stop and ask Itay. Do not choose.

## Prerequisites

- **Plan 15 merged** (`entries_fts` has `key`; rows folded).
- **Plan 16 merged** (warning emitter, E4 codes for real errors).
- **Plan 17 merged** (`--since`/`--until`/`--sort` on search).
- **Plan 17a merged** (one plain notice is one line; a repeated device key is malformed).
- If any is missing, stop.

## Scope

**In:** utf8proc vendoring + `unicode.c` port; `search_terms.c`; union query +
dedup; parse probe; warning; new ranking; facets (`keys`, `tags`) with scope,
tiers, counts, `--facet-limit`; JSON envelope change; human facet lines;
blank-query success (replaces the `empty search query` usage error);
`STORE_ERR_QUERY` no longer produced by search; help, skill, What's New
(E2.8 text + the E2.6 order exception); tests; facade parity.

**Out:** `list` changes; `tags` command output (`tags --json` stays as is,
006 §14); bucket-prefix awareness in search (none in 006); a `--prefix` flag
(rejected in 006).

## Files

`third_party/utf8proc/` (pinned release, README with version, URL, license,
local modifications = none), `CMakeLists.txt` (vendor target with `-w`; also
compiled into `remember_store_tests` under sanitizers per the "vendored code
gets a sanitizer gate" rule), `scripts/lint-all.sh` (IWYU/cppcheck include
paths + boundary grep: only `unicode.c` includes `utf8proc.h`), new
`src/unicode.c/.h`, new `src/search_terms.c/.h` (both in
`REMEMBER_LIB_SOURCES` + `-Wcast-qual`), `src/store.h` (`SearchQuery` gains
`facet_limit`; `SearchResult` with entries page + `keys`/`tags` facets +
`warning` text), `src/store_sqlite.c` (P quoting, probe, union, rank,
candidates), `src/cmd_query.c` (blank query, `--facet-limit`, warning call),
`src/output.c/.h` (search envelope with facets; human facet lines),
`src/commands_common.c` (drop `empty search query` from search),
tests: new `tests/test_union_search.c` (suite `union_search`), new unit tests
for `search_terms.c` and `unicode.c` in `tests/test_store_main.c` /
a new `tests/test_search_terms.c` linked into `remember_store_tests`,
`tests/test_search.c` (update 0.2.0 expectations that change; list each in
Implementation Notes), `tests/test_facade.c`, `tests/gate-suites`,
`tests/COVERAGE.md`; docs: help, `skills/remember/SKILL.md`, `README.md`,
`docs/engineering-notes.md` (search bullets), `docs/QUALITY.md` (vendor).

## Must-pass tests

Fixture (006 Examples): `add --key recs:books --tag recs --tag type:books "#
Book recommendations"`; `add --tag family "Book the dentist"`; `add --key
pref:editor "Use Zed"`.

| Check | Proves |
|-------|--------|
| `--json search boo` → 2 entries, first is `recs:books`; `keys` = [`recs:books`]; `tags` = [`type:books` (1)] | Example 1 |
| `--json search "recs:bo"` → 1 entry; keys [`recs:books`]; tags []; warning `no such column: recs` | Example 2 |
| `--json search 'book OR zed'` → 3 entries; no warning | Example 3 |
| `--json search boo --tag family` → entries ["Book the dentist"]; keys []; tags [] | Example 4, E2.5d scope |
| `--json --json-errors search 'foo AND'` → exit 0; one stderr JSON warning `raw_query_ignored` | Example 5 |
| Inputs `C++`, `"`, `recs:books`, `key:recs`, `foo AND`, `a AND`, `NEAR(`, `*`, `-x`, `(`, `ספר`, an emoji, 10,000 characters → each exit 0 | E2 verification |
| 2000 nested `(` → exit 0 with warning (`fts5: parser stack overflow`) | D1 |
| Whitespace-only query → exit 0, empty result, `keys`/`tags` empty, **no** warning | E2.1(4) |
| Query of only punctuation that does not parse (e.g. `"`) → empty result + warning | E2.1(5) |
| 60 matching tags → `tags.total` 60, 50 items; `--facet-limit 200` → 60 items; `--facet-limit 0`/`201`/`x` → usage | E2.1, E2.6 |
| Key `recs:books`: `book` matches; `boo book` matches; `recs games` does not; `recs:bo` is tier 1 | E2.5, E2 verification |
| Tag `project:fortkeeps-cloud`: `fort cloud` tier 2; `cloud fortkeeps` tier 3; tier decides tag order before count | E2.6, D28 |
| Invalid UTF-8 query bytes → `usage` `invalid UTF-8 in search query` | D27 |
| Tier 1 before tier 2; inside a tier, code-point order (`B` before `a`, `z` before `é`) | E2.6 |
| Tag order: exact tag, then tier, then count DESC, then tag | D28 |
| A row matched by both P and R appears once; `count`/`total`/paging over the de-duplicated set | E2.1b |
| Ranking: exact key, then key prefix, then bm25 (key weight 10 beats body), then recency | E2.4 |
| `label`: `# ` title; else key; else first 60 graphemes of plain text + `…` only when cut (test with combining marks and a ZWJ emoji sequence) | E2.6, D12, P2 |
| JSON member order `version, offset, limit, count, total, keys, tags, entries` | E2.6 |
| Human: `keys:` / `tags: t (n)` lines before entries, max 10 each, `… and N more`; absent when empty | E2.7 |
| `ספר` prefix-matches a body with niqqud (`סֵפֶר`) | E10 verification |
| Raw part is not folded: `search 'סֵפֶר'` still finds the row through P | E10.1 |
| Damaged FTS (plan 16 shim or corrupted shadow table) → exit 1, empty stdout, `corrupt` (never a warning) | E3.2 |
| `invalid search query` never printed by search | E3.3 |
| Bins and filters: `--expired`, `--deleted`, `--source`, `--key`, `--since/--until` scope entries **and** facets | E2.1, E2.5d |
| `search editor` finds `pref:editor` (plan 15 behavior kept) | E1 |
| Facade byte-match (stdout + stderr incl. warning, with and without `--json-errors`) | EN-03 |
| Unit (`search_terms`): NFC composes `e`+U+0301; split on U+3000; 17th token dropped; 65th code point dropped; `"` doubled; Hebrew marks removed | E2.2 |

## Stages

TDD, one failing test at a time. After each stage: gate green, in-session deep
review, Implementation Notes, commit.

1. **Vendor utf8proc + `unicode.c` port** (D2). Follow every row of the
   vendoring checklist (tree + pin, CMake `-w` target, single consumer, lint
   boundary, IWYU/cppcheck include paths, sanitizer gate with real test
   vectors: NFC, graphemes, categories). `REMEMBER_IWYU=1 CI=true
   ./scripts/lint-all.sh` must pass.
2. **`search_terms.c`** (D13): tokens (E2.2 1–3), segment match, tiers, tag
   order, label (D12, P2). Pure unit tests under ASan.
3. **Adapter: P + probe + union.** Quote P (E2.2 4–5); E2.1a probe with D1
   prefixes on the same read transaction; union query; dedup; E2.4 ranking;
   real errors keep their E4 code.
4. **Facets.** Fetch candidate keys (with `sync_id`, `source`, `updated_at`,
   body for `label`) and tags (with scoped counts) for the active bin +
   filters; filter and rank with `search_terms.c`; `total` + cap.
5. **CLI + output.** Blank query success; `--facet-limit`; envelope order;
   human facet lines; warning via the plan-16 emitter. Update 0.2.0 search
   tests whose behavior 006 changes (blank query, invalid query, ranking)
   and list each in Implementation Notes.
6. **Docs + full gate.** Help, SKILL.md (search is prefix-friendly; read
   `keys` for slots; a `note` on stderr is not an error), What's New (E2.8
   verbatim + "JSON `keys` and `tags` now sit between `total` and
   `entries`"), engineering notes, QUALITY.md, COVERAGE.md, gate-suites.

**Insert (Itay 2026-10-20, `decision:coverage-branch-gate-followup`):** after stage 2 and before stage 3, run [plan 18b](18b-branch-coverage-gate.md) on this branch. Stages 3–6 then meet the branch gate for `search_terms.c`, `unicode.c`, `normalize.c`, and `output.c`, and this plan's Definition of Done gains that gate (18b stage 6).

## How to work (workflow + coding standards)

**Workflow**

1. Branch from an up-to-date, clean `main`. One branch and one PR for this plan.
2. If the brain is reachable, load `workflow:deep-review`,
   `pref:code-review-workflow`, `lesson:review-run-edge-cases` and
   `status:remember` (`workflow:project-status`). If it is not, the in-repo
   docs below are the reference.
3. TDD: a red test first, then the smallest change that turns it green, then
   refactor. One failing test at a time.
4. Do not invent. A case 006 and the locked decisions do not decide → stop,
   grill Itay, record the decision (plan Implementation Notes; brain
   `decision:<slug>` if reachable).
5. After each stage: full gate, in-session review against
   [`docs/STEP_REVIEW_CHECKLIST.md`](../docs/STEP_REVIEW_CHECKLIST.md),
   append Implementation Notes, commit.
6. On pause or block, update the `status:remember` card.

**Coding standards** ([`docs/engineering-notes.md`](../docs/engineering-notes.md),
[`docs/QUALITY.md`](../docs/QUALITY.md))

- SQLite only in `store_sqlite.c`; utf8proc only in `unicode.c`.
- Pure modules (`search_terms.c`, `unicode.c`) have no I/O and no store
  knowledge, like `normalize.c`.
- Bind R and P as parameters; never splice user text into SQL.
- Bounded work: 16 tokens × 64 code points; facet candidates are fetched
  once per search, not per row.
- `goto cleanup` single exit; NULL-safe frees; no reads past invalid UTF-8.
- Write through `app_out()` / `app_err()` and the diag emitter only.
- User-facing messages ASCII (the `…` in labels and in `… and N more` is
  data/format text given by 006, not a message).
- New TU: `REMEMBER_LIB_SOURCES` **and** `-Wcast-qual`; direct includes.

**Quality gate** (before every commit that touches C; on macOS prefix
`PATH="$(brew --prefix llvm)/bin:$PATH"`)

```bash
cmake -S . -B build -DCMAKE_C_COMPILER=clang -DREMEMBER_ENABLE_SANITIZERS=ON -DREMEMBER_TEST_VFS=ON
cmake --build build
ctest --test-dir build --output-on-failure
./scripts/lint-all.sh
REMEMBER_IWYU=1 CI=true ./scripts/lint-all.sh     # required after vendoring
./scripts/check-coverage.sh                       # 100% functions + 100% effective lines
./scripts/ci-linux.sh
cmake --build build --target leaks-macos          # macOS only
```

## Definition of Done

- [x] Design log 006 unchanged except Implementation Results rows for E2.*,
      E3.*, E10.1 (and the utf8proc version used). Proof: stage 6 notes
      (`git diff --numstat`: 16 added, 0 removed, one Implementation Results block).
- [x] Every must-pass row green and in `tests/gate-suites`. Proof: the stage 6
      must-pass table (each row's tests and suite).
- [x] D1, D2, D12, D13, D27, D28, P2 implemented exactly. Proof: D1
      `union_search_deep_nesting_is_a_warning` + M9a–d (stage 3); D2 the
      utf8proc boundary (`lint-all.sh`) + stage 1 conformance; D12/P2
      `search_label_*`; D13 the stage 2–4 split (`search_terms.c` pure,
      SQLite in `store_sqlite.c`); D27 `union_search_invalid_utf8_is_usage`;
      D28 `search_tag_order_is_exact_tier_count_then_code_point`.
- [x] utf8proc vendored per the full checklist; IWYU run green. Proof: stage 1
      notes and second review; iwyu PASS in this stage's `ci-linux.sh`.
- [x] No search input exits non-zero except real E4 errors and usage flags.
      Proof: `union_search_no_input_fails`, the stage 3/5 fuzz runs (1,000 and
      3,000 queries), the stage 5 review's 2,000-run grammar fuzz.
- [x] Help, SKILL.md, README What's New (E2.8 verbatim + the E2.6 order
      exception + blank search now succeeds). Proof:
      `union_search_help_describes_prefixes_facets_and_the_warning` and the
      stage 6 runs of every SKILL.md and README claim.
- [x] Engineering notes: `store_search` and CLI search bullets rewritten.
      Proof: stage 3–5 notes; `docs/engineering-notes.md` "CLI search (plan 18 stage 5)".
- [x] Gate: ctest, lint, IWYU, coverage 100/100, `ci-linux.sh` all green.
      Proof: the stage 6 gate block.
- [x] Branch gate (plan 18b): `check-coverage.sh` shows 0 missed branch sides in `search_terms.c`, `unicode.c`, `normalize.c`, `output.c`, on macOS and in GitHub Actions `linux-coverage`. Proof: this review's macOS run (176/80/268/678, 0 missed) and PR #37 job `linux (line coverage on src/)` (the same counts, 0 missed).
- [x] Second review done; no open **blocking** item. Proof: the FINAL
      review below (2026-10-24). The CodeQL high on `search_prefix_part`
      is fixed in the follow-up on that review.
- [ ] `implementation-plans/INDEX.md` row marked ✅.

## Second review handover

Hand this to an independent reviewer (a fresh session that did not write the
code). The reviewer runs `workflow:deep-review` if the brain is reachable,
else [`docs/STEP_REVIEW_CHECKLIST.md`](../docs/STEP_REVIEW_CHECKLIST.md),
and verifies by **running**, not reading.

**Package to hand over**

- Branch, PR link, commit range, this plan, 006 §2/§9, locked decisions.
- Gate output (ctest, lint, IWYU, coverage, `ci-linux.sh`).
- The list of 0.2.0 search tests changed and why.
- utf8proc version + binary size delta of `remember` and `remember_core`.

**Probe these (expected result in brackets)**

1. Run all six 006 Examples by hand [exact match to the must-pass rows].
2. Fuzz: 1,000 random strings (ASCII punctuation, quotes, parentheses,
   `NEAR`, `AND`, `OR`, `NOT`, `^`, `*`, `:`, Hebrew, emoji, invalid UTF-8
   bytes) through `search` [exit 0 always, except invalid UTF-8 → `usage`
   (D27); never a crash; ASan clean].
3. `remember search "$(python3 -c 'print("a "*10000)')"` [exit 0; fast].
4. Keys facet scope: memory with key `recs:books` moved to the deleted bin;
   `search boo` [key absent]; `search boo --deleted` [key present].
5. Tier order with keys `a:b`, `b:a`, `B:a`, `é:x` and query `a` [as the
   E2.6 rules give; code-point order inside a tier].
6. `remember --json search boo --limit 1 --offset 1` [`keys`/`tags`
   unchanged by paging].
7. Corrupt `entries_fts_data`; `search boo` [exit 1 `corrupt`, not a
   warning].
8. Facade parity with the warning on stderr.

**Hot spots**

- Parse probe must run on the same connection and read transaction as the
  real query (E2.1a).
- Classifying every SQLITE_ERROR as "does not parse" would swallow real
  errors; only the four prefixes (D1) count.
- Dedup + COUNT must count rows once when both P and R match.
- `bm25()` must name the real table, not an alias.
- Grapheme truncation must never split a cluster or emit invalid UTF-8.
- Tier 2 index rule is "≥ previous index" (same segment may serve two
  tokens).

**Do not re-open:** union search without a flag; warning on exit 0; facet
JSON position (Itay's stated exception); tier rules (keys and tags, D28); Hebrew fold only on P;
utf8proc as the Unicode source.

**Review output:** append `### <date> — second-opinion deep review (stage N |
FINAL)` under Review Notes. Tag each finding **blocking / important / nit /
praise**. Auto-fix small, local, gate-green issues and say so; send product
or architecture forks to Itay as a grill. Re-run the full gate after any fix.

## Implementation Notes

### 2026-10-20 — stage 1 (vendor utf8proc + `unicode.c` port, D2)

No new grill. No new `decision:*`. Definition of Done boxes stay unticked; this stage does not finish the plan.

Vendoring checklist (`docs/engineering-notes.md` § Vendoring), row by row:

1. Tree + pin. `third_party/utf8proc/` holds `utf8proc.h`, `utf8proc.c`, `utf8proc_data.c`, `LICENSE.md`, and a `README.md` (2.12.0, Unicode 18.0.0, URL, MIT, local modifications none). Proof: `shasum -a 256 utf8proc-2.12.0.tar.gz` is `f564011d…25111`, and `tar -xOzf … utf8proc-2.12.0/<f> | cmp - third_party/utf8proc/<f>` is identical for all four files. `utf8proc_unicode_version()` returns `"18.0.0"`.
2. CMake. `add_library(utf8proc STATIC third_party/utf8proc/utf8proc.c)` with `-w` and a `SYSTEM PUBLIC` include. `remember`, `remember_plain`, `remember_core`, and `remember_tests` link it. `utf8proc.c` includes `utf8proc_data.c`, so the data file is not compiled on its own.
3. Single consumer. Only `src/unicode.c` includes `utf8proc.h`. `unicode.h` exposes `unicode_nfc`, `unicode_graphemes_open` / `_next`, `unicode_category`, and `unicode_is_whitespace`. Thirty `_Static_assert`s pin each `UnicodeCategory` value to the vendor enum.
4. Lint boundary. `scripts/lint-all.sh` has a utf8proc rule. The four boundary rules (sqlite, diag, sha256, utf8proc) now share one `check_boundary` function, and the function keeps the leaks in a variable instead of `/tmp/remember-*-leak.txt` (the overlap risk noted in plan 16).
5. Include paths. `-I third_party/utf8proc` is on cppcheck (src and tests), `gcc -fanalyzer` (`-isystem`), and `IWYU_VENDOR_INCLUDES`.
6. Sanitizer gate. `remember_store_tests` compiles `utf8proc.c` under ASan/UBSan with a per-source `-w` and does not link the static lib. `tests/test_unicode.c` (39 tests) drives NFC, grapheme, category, and White_Space vectors from the 18.0.0 UCD files.
7. Local fixes. None.

`unicode_is_whitespace`: utf8proc has no White_Space bit. On Unicode 18 the property is Zs + Zl + Zp plus U+0009..U+000D and U+0085. The bidi class is a different set: utf8proc gives U+0009 `S`, U+00A0 `CS`, U+2029 `B`, and only 17 scalars have bidi `WS`.

Conformance probe, thrown away before this commit (`probe.c` against `unicode.c` + `utf8proc.c`, clang `-fsanitize=address,undefined -fno-sanitize-recover=all`), using `PropList-18.0.0.txt`, `GraphemeBreakTest-18.0.0.txt`, and `NormalizationTest-18.0.0.txt`:

- White_Space: all 0x110000 scalars; 25 in the property; 0 mismatches.
- Graphemes: 775 test lines, 0 failures. The other 78 lines contain U+0000; the port ends input at the first NUL by contract, so those lines split differently.
- NFC: 20,171 lines × 5 columns (c2 = NFC(c1..c3), c4 = NFC(c4..c5)); 0 failures.
- Each GraphemeBreakTest line number and NormalizationTest row cited in a test comment was checked with `sed -n` / `grep` against the file.

Mutations: one clause at a time on a copy of `src/unicode.c`, `cmake --build build --target remember_store_tests`, run, restore. 69 runs: 53 in the first pass, then 16 reruns. Clauses that a plain deletion turned into a `-Werror` compile failure (unused constant, unreachable code) were rerun as valid-but-wrong rewrites (for example `src == NULL` → `src == NULL && src_len == 0`). Red tests, abbreviated:

- `nul != NULL` → `&& nul == src`, and `nul - src` + 1: `*_stops_at_an_interior_nul` (both).
- `*out = NULL` dropped: `unicode_nfc_null_source_is_internal_and_resets_out`. `*out_len = 0` dropped: all eight `assert_nfc_fails` tests.
- Inject `!= 0` → `> 1`, reset dropped, `==` → `!=`: `unicode_nfc_injected_*`.
- `COMPOSE` → `DECOMPOSE`: the four compose tests. `+ COMPAT`: `unicode_nfc_does_not_compatibility_decompose_fi`.
- `n < 0` → `n < -1`, and each error map (`INVALIDUTF8`, `NOMEM`) → `OVERFLOW`: the invalid-UTF-8 and inject tests.
- `*out = dst` dropped, `*out_len = n - 1`: every OK-path NFC test.
- Open reset dropped: `*_failed_reopen_drops_the_previous_string`, `*_reopen_restarts_at_the_first_byte`. Validation `n < 0` → `n < -3`, and `bytes` set before validation: `*_invalid_open_is_not_iterable`, `*_failed_reopen_*`. Each bounded-bytes status check → `&& src == NULL`: the PTRDIFF_MAX tests.
- `next`: `iter->bytes == NULL` dropped: unopened / invalid-open / failed-reopen. `>=` → `>` on DONE: every cluster test. First `n <= 0` → `n < -3`: `*_next_rejects_invalidated_bytes`. `n2 <= 0` → `n2 < -3`: `*_next_rejects_a_later_invalid_byte`. `state` made `static` (not reset per cluster): combining mark, regional indicators, ZWJ. `i += n2` → `i += 1`, `*start = i`, `*nbytes = i`, offset not advanced: the cluster tests.
- Whitespace: each of Zs, Zl, Zp dropped; `>=` → `>`; `<=` → `<`; first − 1; last + 1; NEL + 1; NEL dropped: `unicode_whitespace_*`.
- Crash kills (ASan abort in the named test, a NULL dereference is the meaning of the guard): `src == NULL`, PTRDIFF_MAX, `out == NULL`, open `iter == NULL`, `next` `iter`/`start`/`nbytes` NULL, `out_len != NULL` → `true`.

Equivalent mutants, kept on purpose:

- `UTF8PROC_STABLE` dropped. utf8proc 2.12.0 never reads that flag (`grep UTF8PROC_STABLE utf8proc.c` hits only the five public wrappers). The flags match upstream `utf8proc_NFC`.
- First `n <= 0` → `n < 0` (and the same for `n2`). `utf8proc_iterate` returns 0 only for an empty span, and `offset < length` rules that out. `<= 0` also stops an endless loop if that ever changes.
- `prev = next_cp` dropped. In 2.12.0 `grapheme_break_extended` reads `c1` only while `state == 0`, the first call per cluster. The header requires consecutive code points, so the port follows the contract.
- `unicode_category` `>` → `>=`, or `> CP_MAX + 1`. U+10FFFF is Cn and utf8proc maps out-of-range values to Cn. The guard stays because `(int32_t)cp` is implementation-defined above INT32_MAX (C11 6.3.1.3); the code says so.

Probes beyond the plan:

- Empty input, a 65,536-byte ASCII string, an interior NUL, a length past PTRDIFF_MAX, NULL for each pointer, an iterator reused after success and after failure: each has a test.
- NFC cost on `a` + N alternating ccc-230/220 marks (`-O2`, no sanitizers): 10,000 marks 0.044 s, 50,000 0.807 s, 100,000 3.236 s. Grapheme iteration on 100,000 marks: 0.001 s. NFC is quadratic on one long combining run (utf8proc's canonical reorder). E2.2 runs NFC on the whole QUERY before the 16 × 64 cap, so a large pasted query pays this cost. `ARG_MAX` bounds it, not the cap. Grilled in the review below: accepted (`decision:plan18-nfc-cost-accept`).

Simplify pass:

- Applied: `assert_nfc`, `assert_nfc_fails`, `assert_clusters`, and `assert_next_is_internal` in `tests/test_unicode.c`. The file went from 722 to 491 lines while the suite grew from 32 to 39 tests (three invalid-UTF-8 cases and the NULL cases are now one behavior per test; `reopen_restarts_at_the_first_byte` is new, found by the open-reset mutant). The `still_ok` guards are gone because `ASSERT_STREQ` is NULL-safe.
- Applied: `check_boundary` in `scripts/lint-all.sh`, 4 copy-pasted blocks → 1 function, about 29 lines saved against the copy-paste version. Each rule was mutated (`utf8proc_map`, `sqlite3_open`, `app_err(`, `perror(`, `sha256_init`, `fprintf(stderr` appended to `src/output.c`; `#include "utf8proc.h"` appended to `src/unicode.h`; the allow ERE replaced): each went `FAIL`, and the clean tree is `PASS` for all four.
- Applied: the three iterator field resets → one compound-literal assignment (2 lines).
- Declined: delete the `unicode_category` range guard (4 lines). Reason above.
- Declined: delete `prev = next_cp` (1 line). It relies on a vendor internal that the header does not promise.

Lint fixes from the first gate run: `it` → `iter` (readability-identifier-length), magic sentinels → `UNWRITTEN` / `INVALID_UTF8_BYTE`, a per-line `NOLINT(hicpp-signed-bitwise)` on the vendor flag OR (the vendor enum is `int`, as with `SQLITE_OPEN_*` in `store_sqlite.c`), and the `||` of a parenthesized `&&` → two `if`s (readability-implicit-bool-conversion).

Pre-existing, not this stage: `shellcheck scripts/lint-all.sh` reports three SC2046 warnings on the unquoted `$(find …)` lists (also on `HEAD`). shellcheck is not a `lint-all.sh` gate.

Gate on this tree (2026-10-20), each run after the last C edit:

- `ctest --test-dir build` (ASan/UBSan, `REMEMBER_TEST_VFS=ON`): 5/5. `remember_store_tests`: 208 tests, 0 failed.
- `./scripts/lint-all.sh`: LINT OK (format, four boundaries, cppcheck src + tests, `gcc -fanalyzer`, clang-tidy). `REMEMBER_IWYU=1 CI=true ./scripts/lint-all.sh` on macOS: LINT OK with scan-build PASS; IWYU is not installed on this Mac, so it SKIPs there.
- `./scripts/ci-linux.sh` (ubuntu 24.04, iwyu 8.21): 5/5 suites; format, boundaries, cppcheck, `gcc -fanalyzer`, clang-tidy, **iwyu PASS**, scan-build PASS; exit 0. This is the IWYU run the stage requires.
- `./scripts/check-coverage.sh`: functions 100%, effective lines 100%; `unicode.c` 100% raw lines and branches.
- `cmake --build build --target leaks-macos`: PASS.

### 2026-10-20 — stage 2 (`search_terms.c`, D13)

Three grills, all answered with the recommended option and saved as keys:

- `decision:plan18-empty-token-no-vacuous`: a token that the E10.1 fold empties is dropped, and it still used one of the 16 slots. With no tokens, a key or tag text-matches only by the whole-prefix rule. FTS5 3.53.3 was probed first: `""*` alone matches 0 rows, and `"boo"* ""*` matches the same row as `"boo"*`.
- `decision:plan18-segment-normalize`: key and tag text is NFC'd and folded before segment matching. q stays literal.
- `decision:plan18-segment-unicode-whitespace`: segments split on Unicode White_Space.

API (`src/search_terms.h`): `search_tokens_build`, `search_q`, `search_facet_classify` (tier + `exact`), `search_key_order` / `search_tag_order` (qsort), `search_label`. `SearchTokens` is a fixed 16 × 257-byte block that the caller owns, and there is no nested allocation. A first draft bound q to the tag comparator through a global. `search_facet_classify` fills `exact` instead, so both comparators are pure.

Reuse instead of new copies:

- `fold_hebrew_marks` folds segments. `is_hebrew_mark` is now public (it was static) and folds token code points while the 64-code-point cap is counted. Its edges are already pinned by `fold_hebrew_marks_drops_the_listed_points`, so it gets no second test (round 3.1).
- `ascii_ws_trim` (in `normalize.c`) is now the one ASCII trim. It replaced `normalize.c`'s `ascii_ws_trim_span` + `Span`, the trim loops in `cmd_query.c` (whose `intentional:` copy of `is_ascii_ws` is gone), and a draft `ascii_trim` in `search_terms.c`. `is_ascii_ws` stays static.
- `unicode_decode` is new in the port (5 tests). Only `unicode.c` may call utf8proc, and search_terms needs code points.

**Plan correction (stage 3 grill).** E2.2 step 4, "double every `\"`", can never fire. `"` is an ASCII character outside `[A-Za-z0-9]`, so step 1 always splits on it. `search_tokens_split_on_a_double_quote` proves it (`a"b` → `a`, `b`). The must-pass unit row "`\"` doubled" therefore has no input that reaches it. Stage 3 owns the quoting code, and Itay decides there between a dead doubling loop, an assert, or dropping the step.

Must-pass rows → tests (`tests/test_search_terms.c`, 67 tests, in `remember_store_tests` under ASan/UBSan):

| Plan row | Test |
|---|---|
| NFC composes `e`+U+0301 | `search_tokens_compose_to_nfc` |
| split on U+3000 | `search_tokens_split_on_ideographic_space` |
| 17th token dropped | `search_tokens_keep_the_first_16` |
| 65th code point dropped | `search_tokens_keep_the_first_64_code_points`, `search_tokens_cap_counts_code_points_not_bytes` |
| `"` doubled | none possible (above); `search_tokens_split_on_a_double_quote` |
| Hebrew marks removed | `search_tokens_remove_hebrew_marks`, `search_tokens_cap_before_the_fold` |
| `book` / `boo book` match, `recs games` does not, `recs:bo` tier 1 | `search_tier_word_prefix_of_a_key_segment_matches`, `search_tier_one_segment_serves_two_tokens`, `search_tier_a_token_without_a_segment_does_not_match`, `search_tier_whole_key_prefix_is_tier_1` |
| `fort cloud` tier 2, `cloud fortkeeps` tier 3 | `search_tier_in_order_tokens_are_tier_2`, `search_tier_out_of_order_tokens_are_tier_3` |
| tier 1 before tier 2; `B` before `a`, `z` before `é` | `search_key_order_is_tier_then_code_point` |
| tag order exact, tier, count DESC, tag (D28) | `search_tag_order_is_exact_tier_count_then_code_point` |
| label title / key / 60 graphemes + `…` only when cut, combining marks, ZWJ sequence | `search_label_*` (15 tests) |

The CLI-level rows (Examples, facets in JSON, scope) belong to stages 3–5.

TDD: the 63 first tests ran against stubs. 57 were red. The 6 that passed check a NULL-argument `INTERNAL` that a stub also returns, and their mutants below prove them. The `unicode_decode` tests were red against a stub (14 asserts) before the implementation.

Mutations: one clause at a time on copies of `src/search_terms.c` and `src/unicode.c`, `cmake --build build --target remember_store_tests`, run, restore. Each was killed. Highlights, with the red tests:

- Each `[A-Za-z0-9]` edge (`>=` → `>`, `<=` → `<` on 0, 9, a, z, A, Z): `search_tokens_keep_ascii_letters_and_digits_together`. The `z` mutant survived at first: no test had a lowercase `z`. The test now uses `AZaz09`. `ascii_lower` edges: `search_q_lowercases_only_a_to_z`. ASCII limit `<` → `<=`: `search_tokens_split_on_del_but_keep_u0080`.
- Each segment separator dropped (`:`, `-`, `_`, `.`, `/`, White_Space): `search_tier_splits_on_every_listed_separator` (and `..._unicode_whitespace`). Token White_Space dropped: `search_tokens_split_on_ideographic_space`, `..._no_break_space_and_nel`, `..._only_separators_is_no_token`.
- Cap `<` → `<=`, `cps++` dropped: the three cap tests. Fold dropped (`&& cp != 0U`): four fold tests. Emptied token kept: `search_tokens_drop_a_token_the_fold_empties`, `..._an_emptied_token_still_used_a_slot`.
- Segment prefix `>=` → `>`, segment start not advanced, a missing token not fatal, in-order search from 0, `in_order` never cleared, cursor not advanced, tier 2/3 swapped: the tier tests, each named in the run log.
- `q_len > 0` dropped: `search_tier_blank_query_matches_nothing`. Prefix → equality: `search_tier_whole_key_prefix_is_tier_1`. The no-token early exit dropped: `search_tier_no_tokens_is_none_without_a_prefix`. Segment NFC skipped: `search_tier_matches_an_nfd_key_through_nfc`. Segment fold skipped: `search_tier_matches_a_vocalized_key_through_the_fold`.
- Every comparator clause (tier direction, text direction, exact first, tier before count, count DESC): the two order tests.
- Plain text White_Space or Cc dropped, Cc → Cf, leading gap kept: the collapse, trim, and ZWJ label tests. Cut `<` → `<=`, 60 → 59, cut inside a cluster: the four 60-grapheme tests. Title `"# "` → `"#"`, newline ignored, blank title kept, key fallback skipped: the title tests. `*out = NULL` dropped: it survived until `search_label_invalid_utf8_plain_text_is_rejected` started from a non-NULL sentinel.
- `from_unicode` INVALID and OOM mapped to INTERNAL: the invalid-UTF-8 and NFC-OOM tests.
- `unicode_decode`: `len == 0`, PTRDIFF_MAX, `n < 0` → `n < -3`, `nbytes` off by one: the decode tests.
- Crash kills, where an ASan abort in the named test is the meaning: each NULL guard; `slots` `<` → `<=` and slots counted only for kept tokens (a 17th token past the array, `search_tokens_keep_the_first_16`); q `out_cap <= end` → `<` (`search_q_short_buffer_is_internal`); token NFC skipped; the plain-text gap never reset (output grows past its buffer).
- Three plain deletions were `-Werror` compile kills (`token_len >= 0U`, `q_len >= 0U` tautologies, unused `cursor`). Each was rerun as a valid-but-wrong rewrite and killed.

Coverage: `check-coverage.sh` gained `^\s*return\s+SEARCH_ERR_`, the same exclusion as `NORM_ERR_` and `STORE_ERR_`. The 32 zero-hit lines in `search_terms.c` are all defensive: `case` labels, malloc OOM, the fold's OOM, and decode or grapheme failures on text that NFC already validated. `llvm-cov show … src/search_terms.c` lists them.

Probes beyond the plan, ASan/UBSan, `probe2.c` (thrown away):

- 1 MB of `a `: 16 tokens, 0.060 s. 1 MB of separators: 0 tokens, 0.061 s. 1 MB of one 4-byte emoji: one token of 256 bytes (the buffer edge), 0.019 s. `ab ` × 10,000: 16 tokens.
- `search_facet_classify` with 16 tokens against a 64 KiB text: 0.005 s.
- A label is 60 graphemes, not 60 bytes. `a` + 32,768 combining marks is one grapheme, so the label is 65,537 bytes. That follows D12 / P2 as written. The keys facet never reaches that path, because a keys-facet item always has a key.
- A cut can end on a space: 64 KiB of `x ` gives `x x … x ` + `…`. D12 / P2 do not trim after the cut.
- `# ` with no key gives `#` (the title is empty, and the plain text is `#`). `# ` + U+200B gives the 3-byte ZWSP as the title (ASCII trim only).

Simplify pass:

- Applied: `plain_label` dropped `k == 60 &&` from the ellipsis test. When fewer than 60 clusters exist, the loop consumed them all and `cut == text_len`. The suite stays green, and the mutant `cut < → <=` still dies (1 line).
- Applied (in-session review, DRY Critical: three sites): one `ascii_ws_trim` for `normalize.c`, `cmd_query.c`, and `search_terms.c`. `cmd_query.c` lost its private `is_ascii_ws` and its two loops. Mutants on the shared function (leading or trailing loop disabled) fail `body_trim_*`, `token_*`, `search_q_trims_*`, and `search_label_title_is_ascii_trimmed`.
- Applied: segment folding calls `fold_hebrew_marks` instead of a second fold loop (about −15 lines).
- Declined: `from_norm` (17 lines) serves one unreachable path. `-Wswitch-enum` needs every case, and the `case` form keeps those zero-hit lines defensive; an `if` chain would leave a must-cover zero-hit line.
- Declined: one comparator with a mode flag. `count` sits between tier and text for tags only; a flag would braid the two orders.
- Tests: 741 lines for 67 behaviors. Four `assert_*` helpers and two `given_*` builders carry the repetition. The fold contract appears in two layers on purpose: `normalize` proves the set, and here the tests prove the E2.2 step order (cap before fold, emptied slot).

Pre-existing, not this stage: `-Wallocator-wrappers` on `real_malloc` / `real_calloc` / `real_realloc` in `store_sqlite.c` (three warnings, `remember_tests` and `remember_store_tests` only, not `-Werror`). Proof: the same flags on `HEAD`'s `store_sqlite.c` give the same three.

Handover items for this stage: Release (`-DCMAKE_BUILD_TYPE=Release`, clang) `remember` 1,886,360 → 1,886,920 B (+560 B), `libremember_core.a` 270,936 → 276,408 B (+5,472 B), against `33f91c3`. Non-CMake consumers of `libremember_core.a`: `search_terms.o` and `unicode.o` are in the archive, but no `remember_core` entry point calls them yet, so nothing new must be linked. From stage 3 on, `store_search` calls them, and such a consumer must add `libutf8proc.a` next to `libsqlite3.a` and `libsha256.a`.

Gate on this tree (2026-10-20), after the last C edit:

- `ctest --test-dir build` (ASan/UBSan, `REMEMBER_TEST_VFS=ON`, `ctest -N` = 5): 5/5. `remember_store_tests`: 280 tests, 0 failed.
- `./scripts/lint-all.sh`: LINT OK (format, four boundaries, cppcheck src + tests, `gcc -fanalyzer`, clang-tidy).
- `./scripts/ci-linux.sh`: OK, **iwyu PASS**, 5/5 suites.
- `./scripts/check-coverage.sh`: functions 100%, effective lines 100%.
- `cmake --build build --target leaks-macos`: PASS.

One gate run failed `store_asan_gate` with eight trim tests red. That binary was the last mutant (`trim trailing dropped`): the mutation script restored the source but did not rebuild, and `ctest` does not build. A rebuild of the restored tree passed (the run above). The script now rebuilds after it restores.

### 2026-10-22 — stage 3 (adapter: P quoting, E2.1a probe, union, dedup, E2.4 ranking)

Start: fetched and merged (`plan18-union-search` = `origin`, `17f01e6`, plan 18b and 18c in). Build configured with the cached `/usr/bin/clang`; `ctest -N` lists 5.

Grills, all answered and filed:

- `decision:plan18-drop-quote-doubling`: E2.2 step 4 (double every `"`) is dropped. The adapter wraps each token as `"tok"*` and joins with one space. The must-pass unit row "`"` doubled" is replaced by `search_tokens_split_on_a_double_quote`.
- `decision:plan18-warning-wired-stage3`: `cmd_query.c` prints `raw_query_ignored` from this stage, so no commit drops a failure silently. Stage 5 keeps the E2.3 text question (plain `raw query ignored: <msg>` vs JSON `<msg>`; `diag_warning` writes one message for both today).
- `decision:plan18-delete-store-err-query`: `STORE_ERR_QUERY`, its `code_for_store_status` and `store_status_message` cases, and `store_status_from_sqlite` are deleted. The search mapper was the only producer (`grep -rn STORE_ERR_QUERY src` before the change). `invalid_query` stays in the diag catalog (E3.3). `run_count_and_page` lost its `map_err` parameter (both callers passed `store_status_plain`).
- `decision:plan18-dash-query-needs-double-dash`: `search -- -x` is a search for the literal term `-x`; callers do not write FTS5 syntax. `search -x` stays a usage error. FTS5 reads a leading `-` as a negated column filter (probe on 3.53.3: `-key : zed` 1 row, `key : zed` 1, `zed` 2; `-x` → `no such column: x`), so R does not parse and P (`"x"*`) carries the search.

Not stage-assigned, done here because the store contract needs it: D27. `cmd_query.c` rejects invalid UTF-8 before the trim (`invalid UTF-8 in search query`, usage). `normalize.c`'s `utf8_is_valid` is now public (was static); it adds no branch. The store treats invalid bytes as `STORE_ERR_INTERNAL` (contract).

Design (`src/store_sqlite.c`, `src/store.h`):

- `store_search` returns `SearchResult` = `PageResult` + heap `warning`. Every caller moved to `.page` (18 sites in `test_store.c`, `test_store_sync.c`, `test_schema_v5.c`; `cmd_query.c`).
- R = QUERY ASCII-trimmed (heap). q = `search_q(R)`. P from `search_tokens_build`. Token OOM → `STORE_ERR_OOM`; any other token or q failure → `STORE_ERR_INTERNAL`.
- Probe (E2.1a) after `BEGIN`, on the same connection, only when R is not empty. ROW/DONE parses. `SQLITE_ERROR` with a D1 prefix (`fts5:`, `no such column`, `unknown special query`, `unterminated string`) → the message is copied whole (heap, not the 256-byte `StoreError` field) into `warning`. Anything else → `store_status_plain`, so `capture_sqlite_error` keeps the E4 code (`cli_damaged_fts_search_reports_fts_only` stays green through the probe path: `corrupt`, `fts_only` true).
- MATCH text: `(P) OR (R)`, else whichever is non-empty; R only when it parsed. Empty → no statement runs and the page is empty (E2.1 (4), (5)). One MATCH, so a row both parts match is one FTS row (E2.1b): no SQL `UNION`, no `DISTINCT`.
- Binds: ?1 = q, ?2 = MATCH text, filters from ?3 (`SearchBinds`). The COUNT statement does not read ?1; binding it is in range because ?2 sets the parameter count (it runs: every search test binds both). `_Static_assert(LIST_BIND_CAP >= 2)` replaced a runtime guard on a constant.
- ORDER BY (E2.4): `e.key IS ?1 DESC, substr(e.key, 1, length(?1)) IS ?1 DESC, bm25(entries_fts, 10.0, 1.0, 4.0), e.updated_at DESC, e.id DESC`. `IS`, not `=`: with `=`, a NULL key gives NULL, which sorts below the 0 of a keyed miss, so every keyed row would outrank every keyless row before bm25 (mutants M18a/M18b fail `store_search_ranks_a_keyless_row_by_bm25`).

0.2.0 tests whose expectations changed (stage 5 lists the rest):

| Test | Was | Now |
|---|---|---|
| `test_search.c` `search_invalid_fts_syntax_rejected` (`AND AND`) | exit 1 | deleted in the simplify pass: V27 below and `union_search_*` pin the warning |
| `test_verification_edges.c` V27 `search_unbalanced_quote_rejected` | exit 1 | `search_unbalanced_quote_warns`: exit 0, `remember: warning: unterminated string` |
| `test_errors.c` golden row `search "\""` → `invalid_query` | exit 1 | row removed (not an error any more) |
| `test_errors.c` `json_errors_invalid_search_query_keeps_its_text` | exit 1 | deleted (E3.3) |
| `test_errors.c` `code_for_store_status(STORE_ERR_QUERY)` | `invalid_query` | line removed with the status |
| `test_store.c` `store_search_prepare_and_step_fail` | statement 0 = COUNT | the probe is statement 0; COUNT and SELECT are 1 and 2; a COUNT-step case was added |

Must-pass rows proven this stage (the rest need facets or stage 5 CLI):

| Plan row | Test |
|---|---|
| Example 2 warning `no such column: recs`, 1 entry | `store_search_warns_with_the_fts_message`, `store_search_runs_p_alone_when_r_does_not_parse` |
| Example 3: 3 entries, no warning | `store_search_unions_the_raw_part`, `union_search_parsed_raw_part_has_no_warning` |
| Example 5: exit 0, one JSON warning | `union_search_unparseable_raw_part_is_a_json_warning` |
| E2 inputs each exit 0 (`-x` as `-- -x`), emoji, 10,000 characters | `union_search_no_input_fails`, `union_search_long_query_succeeds` |
| 2000 nested `(` → warning `fts5: parser stack overflow` | `union_search_deep_nesting_is_a_warning` |
| Whitespace-only → empty, no warning (store; the CLI still says `empty search query` until stage 5) | `store_search_blank_query_is_empty_without_warning` |
| Only punctuation that does not parse → empty + warning | `store_search_without_p_and_unparseable_r_is_empty` |
| Invalid UTF-8 → usage (D27) | `union_search_invalid_utf8_is_usage`; store contract `store_search_invalid_utf8_is_internal` |
| Row matched by P and R once; count/total/paging de-duplicated | `store_search_counts_a_row_both_parts_match_once`, `store_search_pages_the_deduplicated_set` |
| Ranking: exact key, key prefix, bm25 (key 10 > body), recency | `store_search_ranks_exact_key_then_key_prefix_first`, `..._ranks_a_keyless_row_by_bm25`, `..._ranks_by_weighted_bm25`, `store_search_ignores_sort_for_order`, `store_search_ties_break_by_id` |
| `ספר` finds `סֵפֶר` (E10); vocalized query found through P (E10.1) | `store_search_hebrew_prefix_finds_niqqud`, `store_search_vocalized_query_matches_through_p` |
| Damaged FTS → exit 1, `corrupt`, never a warning (E3.2) | `cli_damaged_fts_search_reports_fts_only` (kept green); probe real errors `store_search_probe_error_without_a_d1_message_is_real`, `..._probe_prepare_failure_is_real` |
| `invalid search query` never printed (E3.3) | `union_search_no_input_fails` |
| `search editor` finds `pref:editor` | `store_search_finds_a_key_segment` |
| Facade byte-match incl. warning, both modes | `facade_search_bad_query_matches_cli` (plain stderr), `facade_search_json_warning_matches_cli` |

TDD: each behavior started red. Red runs: word prefix (empty result), unparseable R (`STORE_ERR_SQLITE`), warning (compile-red: no `SearchResult`), CLI JSON warning (empty stderr), `"` (status 5 = old `STORE_ERR_QUERY` path through an empty MATCH), blank (warning set, then `() OR ()`), exact/prefix ranking (bm25 order), D27 (`internal`), token OOM (`internal`). Tests that passed on arrival (union, dedup, R alone, weights, keyless, paging, Hebrew, failure paths) are proven by the mutants below.

Mutations (`scratchpad/mutate.py`): one clause at a time in `build-mut` (`/usr/bin/clang`, ASan/UBSan, VFS on), suites `store_asan_gate` + `store,search,union_search,errors,verification_edges,facade,coverage_edges`, source restored after each. 45 mutants in the first pass (M27b added later), then 10 reruns (M7, M17, M19, M19b, M20a, M20b, M27b, M29 after the fixture change; M7 and M8 after the guard). Killed, with the red tests:

- M1 `*` dropped from `"tok"*`: word prefix, Hebrew prefix, paging, P-alone. M4/M5 each guard of `(P) OR (R)`: R-alone; P-alone, warning, V27, union CLI tests. M6b one-side form drops P: P-alone.
- M7 ROW not a parse: first run aborted on an old `test_store.c` read of `rows[0]` after a failed search (round 2.4). Six such reads in search tests are now guarded; the rerun names semantic reds, among them `store_search_counts_a_row_both_parts_match_once`, `store_search_ranks_by_weighted_bm25`, and `store_list_and_search_bins`. M8 DONE not a parse: word prefix, Hebrew, R-alone, `search_no_matches_exits_zero`, CLI tables.
- M9a–d each D1 prefix: `fts5:` → plain/JSON/deep-nesting warnings; `no such column` → warning tests; `unknown special query` → `union_search_no_input_fails` (`*`); `unterminated string` → `"` tests.
- M12 warning not copied, M13 copy OOM ignored (`store_search_warning_copy_oom_is_oom`), M14 probe on blank R, M15 R kept after a warning, M16 empty MATCH queried, M30 R not trimmed (blank test).
- M17 exact rule dropped, M19 prefix rule dropped, M19b prefix one character short, M20a/b either key rule ASC: `store_search_ranks_exact_key_then_key_prefix_first`. M19 and M19b **survived** the first fixture: my comment said each later row had the better bm25, and the run showed the prefix row already beat the pref row. The fixture now comes from a probe where bm25 alone gives the full reverse (pref −4.16e-06, prefix −3.94e-06, exact −3.83e-06), plus a key `editox` that differs from q only in q's last character; all five die.
- M18a/b `IS` → `=`: keyless test. M21a–c each weight, M21d bm25 DESC: weighted test (probe: default weights give body, tags, key; (10,1,4) key, tags, body; each single change reorders). M22a/b recency and id ties: the two old order tests.
- M23 MATCH reads ?1, M24 binds swapped, M29 q not lowercased: many search tests / the exact-key test (query `Editor`).
- M25 warning never printed: V27 + union CLI tests. M25b guard dropped (a NULL warning printed): `union_search_parsed_raw_part_has_no_warning`, `cli_repair_search_index_rebuilds`.
- M27 D27 check `if (false)`: `-Wunreachable-code` compile kill; rerun as `utf8_is_valid(raw, 0U)`: `union_search_invalid_utf8_is_usage`. M28a/b status map: token-OOM and invalid-UTF-8 store tests. M31 COMMIT failure keeps the page, M32 warning kept on error: `store_search_commit_failure_drops_page_and_warning`.

Equivalent mutants, kept on purpose:

- M2 tokens joined with no space, M3 a leading space: FTS5 reads `"zed"*"other"*` as an implicit AND and ignores a leading space (probe: same counts with and without the space). The space follows E2.2 step 5.
- M6 the one-side form drops R when P is empty. P is empty only when every token was E10.1 marks, and FTS rows have those marks folded. Probe: a body that is only U+05B5 and a tag that is only U+05B5; queries `ֵ`, `ֵ*`, `"ֵ"`, `ֵ ַ` all total 0. `"%s%s"` keeps E2.1 (3) as written at no extra code.
- M10 `rc == SQLITE_ERROR &&` dropped: another step code with a D1 message would need FTS5 to set a D1 text on a non-ERROR code; `CORRUPT_VTAB` reads `database disk image is malformed`. Kept because E2.1a names SQLITE_ERROR.
- M11 prefix → substring match: every D1 text SQLite produces starts with its prefix.
- M33 the probe's bind failure unchecked: `sqlite3_bind_text(..., -1, SQLITE_STATIC)` on index 1 fails only with `SQLITE_TOOBIG` (≥ 1,000,000,000 bytes). The check reuses `list_bind_texts`, which the list path covers.

Coverage: functions 100%, effective lines 100%, 0 missed branch sides in the four pure TUs. Round 9.3 read of every zero-hit line in the touched code: in `store_sqlite.c` only `return -1;` / `return (PageResult){.st = STORE_ERR_INTERNAL};` for SQL-buffer truncation (fixed SQL into 8 KB buffers) and the probe's bind-failure `goto cleanup;` (M33). The first coverage run caught four real must-cover lines (NULL arguments, R/q/MATCH OOM, BEGIN failure, COMMIT failure); each got a test. In `cmd_query.c` the zero-hit lines are pre-existing (`trim_query_copy`'s NULL raw, `utc_now` and output failures).

Probes beyond the plan (`build/remember`, ASan):

- Fuzz: 1,000 strings from `"`, `(`, `)`, `*`, `:`, `^`, `-`, `+`, `,`, `.`, `{`, `}`, NEAR/AND/OR/NOT, column names, Hebrew, a lone mark, emoji, ESC, LF, TAB, `_`, with invalid UTF-8 (`ff`, `c0 80`, `ed a0 80`, `80`) in every tenth: 743 exit 0 with a warning, 141 exit 0 clean, 100 exit 1 `invalid UTF-8`, 16 exit 1 `empty search query` (whitespace-only; stage 5 makes those succeed). No sanitizer report. No input where R parsed alone but `(P) OR (R)` failed.
- Echoed values (round 3.3): `a\nb:c` → `no such column: b` (FTS5 splits on LF, so no newline reaches the message). ESC → plain `?`, JSON `\u001b`. U+009B → plain `?`, JSON raw (`decision:plan18c-mask-c1`). A 300-byte column name → the full 335-character line (heap copy, no truncation).
- `--db /nonexistent/dir/x.db --no-create` and a missing file with `--no-create`: `cannot_open`, exit 1. `search ''`: still `empty search query` (stage 5).
- `search -x` without `--`: `unknown option '-x'` on `main` and here (the grill above).
- Handover probe 3, `search "$(python3 -c 'print("a "*10000)')"`: exit 0, total 1; 0.47 s (ASan) and 0.68 s (Release), one cold run each. Probes 2 (fuzz), 7 (corrupt FTS) and 8 (facade with the warning) are above; 1, 4, 5 and 6 need facets (stage 4).

Simplify pass:

- Applied: `test_search.c` `search_unparseable_query_is_a_plain_warning` deleted (−18 lines): V27 already pins the plain warning line at the same layer.
- Applied: `*warning = NULL` in the probe (−1 line): the caller's initializer sets it; the comment says "NULL on entry".
- Applied: `store_status_from_sqlite` (−14) and `run_count_and_page`'s `map_err` parameter (one error map left); the runtime `bind_cap < 2U` guard → `_Static_assert` (−3); one rollback block for probe and query failures (−4).
- Declined: merging `search_build_where` into `search_query_exec` (about −8 lines). It mirrors `list_build_where` / `list_query_exec`; one shape for both paths.
- Declined: binding R by length instead of a heap copy (−3 lines). The NUL-terminated copy lets the probe reuse `list_bind_texts`; a length bind adds an `int` cast and its own failure line.
- Declined: inlining `search_match` (−6 lines). It is the E2.1 (3)–(5) rule in one place.
- Tests: `test_store_search.c` 24 tests, `test_union_search.c` 6. Store tests prove content; CLI tests prove only that the warning and exit reach the process (round 3.1).

Handover items: Release (`-DCMAKE_BUILD_TYPE=Release`, `/usr/bin/clang`) against `17f01e6`: `remember` 1,887,176 → 1,903,752 B (+16,576 B), `libremember_core.a` 279,248 → 281,632 B (+2,384 B). From this stage `store_search` calls `search_terms` → `unicode` → utf8proc, so a non-CMake consumer of `libremember_core.a` must link `libutf8proc.a` next to `libsqlite3.a` and `libsha256.a`. The Mac app ships the `remember` binary (`App/Resources/remember`) and is not such a consumer.

Docs: `docs/engineering-notes.md` `store_search` bullet and the diagnostics sentence on `STORE_ERR_QUERY`; `skills/remember/SKILL.md` FTS note (a query FTS5 cannot parse is a warning on exit 0); `tests/COVERAGE.md` (V27 row, two new files). Stage 6 still rewrites help, SKILL.md, and README.

Gate on this tree (2026-10-22), after the last code edit (no file under `src`, `tests`, `docs`, `skills` or `scripts` is newer than the gate start):

- `ctest --test-dir build` (ASan/UBSan, `REMEMBER_TEST_VFS=ON`, `ctest -N` = 5): 5/5. `remember_store_tests`: 411 tests, 0 failed.
- `./scripts/lint-all.sh`: LINT OK (format, four boundaries, cppcheck src + tests, `gcc -fanalyzer`, clang-tidy).
- `./scripts/check-coverage.sh`: functions 100%, effective lines 100%; `search_terms.c` 176, `unicode.c` 80, `normalize.c` 268, `output.c` 572 branch sides, 0 missed.
- `./scripts/ci-linux.sh`: OK, 5/5, **iwyu PASS**, scan-build PASS.
- `cmake --build build --target leaks-macos`: PASS.

### 2026-10-23 — stage 4 (facets: candidates in scope, classify, order, totals, cap)

Start: `git fetch` brought nothing for this branch. The stage 3 second-review commit `54f1047` sat detached in the reviewer's worktree; `plan18-union-search` was fast-forwarded to it (`fa3702a..54f1047`). Its verdict closed stage 3. The stale `.claude/REVIEW_REQUEST` was removed. The open nit (QUERY trimmed twice) stays with stage 5. `standard:coding` ROUND 13 loaded. Build configured with the cached `/usr/bin/clang`; `ctest -N` lists 5.

Grills, both answered and filed:

- `decision:plan18-facets-independent` (Itay chose against the recommendation): facets do not depend on the memory query. The store fetches and classifies them whenever the probe succeeds, even when no MATCH runs. With E2.1 (5) and P empty (query `(`), the entries are empty but key `(draft)` is still a facet item (`store_search_facets_do_not_need_a_memory_match`). E2.1 (4) still has empty facets, because q and the tokens are both empty (`store_search_blank_query_has_no_facets`).
- `decision:plan18-facet-invalid-utf8-no-match`: a stored key or tag that is not valid UTF-8 (only a hand-edited store; add, update and import validate) is a non-match when it is not tier 1. Search never fails on a stored row. Probe before the grill: such bytes print raw in `list`, `--json list` and `tags`, exit 0.

Design (`src/store.h`, `src/store_sqlite.c`, `src/cmd_query.c`):

- `SearchQuery.facet_limit` caps `keys.items` and `tags.items` as given, like `limit`. The CLI enforces 1..200 in stage 5. Zero returns the totals and no items, so the 18 callers that read only `.page` allocate nothing (`store_search_facet_limit_zero_returns_totals_only` pins items NULL).
- `SearchResult` gains `keys` (`SearchKeyFacet`: items of key, sync_id, label, source, updated_at; count; total) and `tags` (`SearchTagFacet`: items of tag, count). `store_search_result_free` releases everything. On any failure the store frees everything and sets only `page.st`. The COMMIT failure path now uses the same cleanup.
- One query per kind, built from `list_build_where(filters, now, "updated_at")` (the list WHERE without MATCH) between a fixed head and tail (`k_facet_head` / `k_facet_tail`): keyed memories (`key, sync_id, source, updated_at, body … AND e.key IS NOT NULL`), and tags with their scoped `COUNT(*)` (`… GROUP BY tg.name`). They run on the search's read transaction, after the probe and the page.
- `facet_append_row` classifies column 0 with `search_facet_classify`. Tier NONE and `SEARCH_ERR_INVALID_UTF8` are skipped. OOM is `STORE_ERR_OOM`, any other failure `STORE_ERR_INTERNAL`. A match is copied into a `FacetCandidate` (`SearchFacet` first, so the search_terms comparators read each element as one; `facet.text` points at the owned copy). Key rows also copy sync_id, source, updated_at and the label `search_label(body, key)`.
- `facet_fetch` sorts with `search_key_order` or `search_tag_order`. `facet_take_keys` / `facet_take_tags` set `total` to every match and move the first `facet_limit` into the result; a moved item is zeroed in the candidates, so one free path covers both.
- `cmd_search` holds the whole `SearchResult` and frees it with `store_search_result_free`. It sets no `facet_limit` yet, so the CLI computes totals only until stage 5 parses `--facet-limit` and prints the facets.

Ownership claim checked against every way data arrives (round 11.3): sync_id, source, updated_at and body are `NOT NULL` in the schema (`sqlite3 p.db "UPDATE entries SET source = NULL …"` → `NOT NULL constraint failed: entries.source`), so a NULL copy means out of memory. With a key, the label never decodes the body: a hand-edited body `# ok\xff` with a key gives the label `ok\xff` and exit 0 (probe binary against `libremember_core.a`).

0.2.0 test changed: `test_search.c` `search_json_paging_fields` now checks `"total":3` instead of only `"total":`. Mutant C3 (total passed as count) survived the old assert. 3 is a hand count: all three corpus rows match `pref OR helix OR FTS5 OR Font`.

Must-pass rows this stage proves at the store (the JSON, human and `--facet-limit` CLI rows belong to stage 5):

| Plan row | Test |
|---|---|
| Example 1: keys [`recs:books`], label `Book recommendations`; tags [`type:books` (1)] | `store_search_facets_a_matching_key`, `store_search_labels_a_key_with_its_title`, `store_search_facets_a_matching_tag` |
| Example 2: keys [`recs:books`]; tags [] | `store_search_facets_example_2` |
| Example 4: keys []; tags [] (E2.5d scope) | `store_search_filters_scope_the_facets` |
| Whitespace-only → keys and tags empty | `store_search_blank_query_has_no_facets` |
| 60 matching tags → total 60, 50 items; limit 200 → 60 | `store_search_caps_tag_items_at_the_facet_limit` (store; usage values are stage 5) |
| Tier 1 before tier 2; code point order inside a tier | `store_search_orders_keys_by_tier_then_code_point` (rules: stage 2 unit tests) |
| Tag order exact, tier, count DESC, tag (D28) | `store_search_orders_tags_exact_tier_then_count` |
| label title / key | `store_search_labels_a_key_with_its_title`, `store_search_labels_a_key_without_a_title_with_the_key` (60-grapheme rule: stage 2) |
| Bins and filters scope the facets | `store_search_bin_scopes_the_keys` (handover probe 4), `store_search_filters_scope_the_facets`, `store_search_filters_scope_the_tag_counts`, `store_search_since_scopes_the_keys_on_updated_at`. The facet query takes the whole `ListQuery` (mutants F6a/F6b); `--expired`, `--source`, `--key`, `--until` reach `list_build_where`, which the list tests pin per axis (round 3.1). Stage 5 adds the CLI row. |
| `--offset` / `--limit` page entries only (handover probe 6) | `store_search_paging_leaves_the_facets_whole` |

Other new tests: `store_search_caps_key_items_and_counts_every_match`, `store_search_key_item_carries_its_memory_fields` (re-added later, so updated_at ≠ created_at), `store_search_counts_the_memories_that_carry_a_tag`, `store_search_skips_a_stored_key_with_invalid_utf8`, three failure-sweep tests (pure-TU allocations, store allocations, unicode calls; each failure must be OOM or INTERNAL and the clean run must hold every item and the label), `store_search_facet_failure_drops_the_page`, `store_search_tags_prepare_failure_is_sqlite`, `store_search_facet_step_failure_is_sqlite`, `store_search_too_many_filters_for_the_facets_is_internal`, `store_search_result_free_accepts_null`, and `test_search.c` `search_invalid_source_is_usage`.

TDD red runs: matching key (link-red: no `store_search_result_free`), key order (`"a:b|b:z|…"` vs `"b:z|a:b|…"`; the first fixture passed without a sort because SQLite returned keys in index order, so it was rebuilt where index order and tier order differ), key cap (count 2 vs 1), both labels (`(null)`), facet failure dropping the page (entries non-NULL; the first fault index hit a row's tag load, because each page row prepares its own tag query), the pure-TU sweep (a classify OOM was swallowed as a non-match), invalid stored UTF-8 (status 4, INTERNAL), matching tag (empty). Tests that passed on arrival are proven by the mutants below.

Mutations (`scratchpad/mutate.py`, `build-mut`: `/usr/bin/clang`, ASan/UBSan, VFS on; `remember_store_tests` plus `remember_tests --only search,union_search,errors,facade,verification_search,coverage_edges,verification_gld,get_list_delete`; source restored and rebuilt after each). 63 one-clause mutants, red tests named:

- SQL: F1 `e.key IS NOT NULL` dropped: a keyless row reaches classify with NULL text and the search fails INTERNAL. The clause has no valid-but-wrong rewrite that still keeps NULL keys out, so this kill is the meaning. F2 `GROUP BY e.id`, F3 `COUNT(*)` → 1: the count and order tests. F5 time column `created_at`: `…since_scopes_the_keys_on_updated_at`. F6a filters dropped (bin kept): the filter and since tests. F6b bin forced live (filters kept): `…bin_scopes_the_keys`.
- Row: F7 count never read, F8 count read for keys: the count/order/tag tests. F9 invalid UTF-8 not skipped: `…skips_a_stored_key_with_invalid_utf8`. F10 tier NONE kept: 10+ tests. F10b `sst == SEARCH_OK &&` dropped, F11 OOM → INTERNAL: the pure-TU sweep. F11b INTERNAL → OOM **survived** at first (no test injected a unicode failure during classify) → killed by the new `store_search_unicode_failure_is_internal_not_a_missing_key`. F12 key copy OOM swallowed, F20 growth OOM swallowed, F27c/d item array OOM ignored: the store alloc sweep. F13 key fields for tags: many. F14/F14b/F15 columns swapped: `…key_item_carries_its_memory_fields`. F16 label from the key column: title test. F17 label without the key: key-label test. F18 label failure ignored **survived** at first (the sweep did not read the label) → the pure-TU sweep now asserts the label.
- Order, cap, move: F23b tags by the key order: tag order test. V24 sort only after an error: both order tests. F25/F25b limit ignored, F26/F26b total = keep: the cap tests. F27/F27b no early return on zero: `…facet_limit_zero_returns_totals_only`. F29 tag count 0: count tests. F28, F30 moved item not zeroed: ASan double free (the meaning: one owner per string).
- Flow: V24b step error checked only after an error: step failure test + sweeps. V31a/V31c take only after an error, V32b facets only after a page error: many. F31b tags fetched after a keys error: facet failure tests + sweeps. F32 facets after a page error: the stage 3 probe and fault tests. F33 error cleanup frees only the warning, F35 no memset: the drop-the-page, commit and prepare tests. F34 NULL guard dropped: UBSan in `…result_free_accepts_null` (the meaning). F38 COMMIT failure keeps OK: `store_search_commit_failure_drops_page_and_warning`. F21 growth when `count > cap`: UBSan store to NULL (memory-safety mutant).
- CLI: C3 total passed as count **survived** → `search_json_paging_fields` now asserts `"total":3`. C4 `list_prepare_query` failure ignored: `search_invalid_source_is_usage`. VC1 warning printed only when NULL: V27 and the union tests.
- Compile kills rerun as valid-but-wrong rewrites (round 8.1): F24 → V24, F24b → V24b, F31a → V31a, F31c → V31c, F32b → V32b, C1 → VC1, all killed as listed.
- Leak-only (macOS ASan has no LeakSanitizer): F31d keys candidates not freed, F36 / F37 `store_search_result_free` skips the key / tag items. Run in `ubuntu:24.04` with clang, `REMEMBER_ENABLE_LSAN=ON`, `ASAN_OPTIONS=detect_leaks=1`, as an unprivileged user (`scratchpad/lsan.py`): the control run exits 0 with no leak report; each of the three exits 134 with `LeakSanitizer: detected memory leaks`. `ci-linux.sh` runs the store suite that way.

Equivalent mutants, kept on purpose:

- F22 qsort guard `count > 1`: sorting one element is a no-op. The guard exists because `items` is NULL when nothing matched (the comment says so).
- F23a keys sorted by `search_tag_order`: key items carry count 0, and an exact key is q itself, which is a prefix of every tier 1 key and so already sorts first by code point. Differential (round 13.3, `scratchpad/diff/diff.c` linked against `search_terms.c`): 20,000 random key sets and queries over `a b ab ba z é A abc b2 x` joined by every separator, with q itself injected; both orders give identical sequences, 19,311 trials with an exact item beside another tier 1 item. Kept, because E2.6 states the key order without an exact rule.
- F25c `<` → `<=` in the min: equal operands give the same value.
- VC2 `err_store(s, STORE_ERR_SQLITE)`: every store error a CLI search can reach is `STORE_ERR_SQLITE`. Invalid UTF-8 stops at D27 and more than 50 `--tag` stops at the CLI (probe: 61–70 tags → `too many --tag filters (max 50)`). The line is the stage 3 line with the variable renamed.

Coverage (round 9.3 / 13.2): functions 100%, effective lines 100%, 0 missed branch sides in the four pure TUs. Zero-hit lines in the touched code, each trigger named by a run:

- `facet_fetch` `list_build_where` failure: reached by `store_search_too_many_filters_for_the_facets_is_internal` (64 tags, no page query). The CLI cannot reach it: `--tag` is capped at 50, so the WHERE needs at most 55 of 64 binds and 6,057 of 8,192 SQL bytes (computed from the clause texts).
- `facet_fetch` SQL truncation `return STORE_ERR_INTERNAL`: the WHERE is under 8,192 bytes and the margin (256) is larger than the longest head + tail (132 bytes, the tags query), so it cannot fire. It mirrors `list_query_exec`.
- The first run also left OOM lines (copies, growth, item arrays) and `store_search_result_free(NULL)` at zero; the store alloc sweep and `…result_free_accepts_null` now reach them.
- `cmd_search`: `list_prepare_query` failure is a user path, now `search_invalid_source_is_usage`. `utc_now` failure: not user-reachable (clock read). `load_page_neighbors` failure: a database error after the page, store fault injection only. `failed to write output`: reachable by a user. On Linux, `remember search body --limit 200 > /dev/full` with 300 rows printed `remember: failed to write output`, exit 1 (Release build in `ubuntu:24.04`). No CLI test can make stdout fail today; see the pre-existing finding below.

Probes beyond the plan:

- `facet_limit` through the store API (probe binary against Release `libremember_core.a`): `SIZE_MAX` → keys 2/2, tags 1/1; 1 → keys 1/2 (the tier 1 key first); 0 → keys 0/2, tags 0/1.
- Hostile stored text (round 3.3 / 12.1): a title `# esc \033[31mred\r\n\302\233x` gives the label `esc \x1b[31mred`: the line stops at LF, the ASCII trim drops CR, and the ESC stays. The facet items carry key, tag, sync_id, source, updated_at and label raw from the store. Import validates the key, tags, sync_id, source and timestamps (plan 18c); the label comes from the body, which may hold ESC, C1 and invalid UTF-8. **Stage 5 must write every facet field through the JSON escaper and the human mask** (`decision:plan18c-mask-c1`), like entry bodies.
- Cost (Release, 3,000 keyed memories under 300 key prefixes, 1,001 tags, `search boo`, 20 warm runs): stage 3 `fa3702a` 12.0 ms/run, this tree 16.6–17.1 ms/run. The facets add about 5 ms at that size.
- `search -- '('` with `(draft)` stored: exit 0, no entries, `remember: warning: fts5: syntax error near ""`. The key `(draft)` is a facet item (`store_search_facets_do_not_need_a_memory_match`); the CLI prints facets from stage 5.

Simplify pass:

- Applied: one fetch and one row appender for both kinds (`FacetKind` indexes the SQL head and tail). A keys-only draft had its own fetch; a second copy for tags would have been about 60 more lines.
- Applied: `store_search` error cleanup is `store_search_result_free` (the COMMIT path lost its own free, −2 lines).
- Applied: `cmd_search` keeps the `SearchResult` instead of five locals (−12 lines net).
- Applied (tests): the three failure sweeps share `when_each_failure_until_clean` (−27 lines); `store_search_keys_prepare_failure_is_sqlite` was deleted (−11): `store_search_facet_failure_drops_the_page` proves the same keys-prepare contract, and F31b still dies.
- Declined: one `facet_take` for both kinds (about −10 lines). A key item moves whole; a tag item is built from the key slot and the facet count. One function needs a kind switch on both the element type and the move, which braids two outputs.
- Declined: sorting keys with `search_tag_order` too (−1 line). It is equivalent (F23a), but E2.6 does not state an exact rule for keys, and a reader would have to repeat the differential to trust it.
- Declined: skipping the facet SQL when q is empty (blank query). It adds a branch for a speed-up, and the facets are already empty (`store_search_blank_query_has_no_facets`).
- Tests: `test_store_search.c` gained 28 tests (about 570 lines with helpers `assert_keys[_for]`, `assert_tags[_for]`, `given_facet_query`, `given_numbered_tags`, `given_facets_only`, `when_each_failure_until_clean`). The rules (tiers, segment match, label, order clauses) stay in the stage 2 unit tests. Each store test proves only that the store feeds them the right input; the order fixtures are chosen so that SQLite's own row order differs from the expected one.

Pre-existing findings (not this stage, reported per round 4.2):

- A failed final flush of stdout is never reported. On Linux, `remember search draft > /dev/full` and `remember list > /dev/full` with small output exit 0 and write nothing. No `fflush`/`ferror` check on stdout exists in `src/`. This affects every command (`style:fail-fast`).
- `test_store.c` `store_list_and_search_bins` reads `rows[0]` after a failed assert when the `sqlite3` CLI is missing (UBSan `applying zero offset to null pointer` at line 978, seen in a container without `sqlite3`). Its helper reports `sqlite3: not found`, and the test goes on (round 2.4 / 12.4).

Handover items: Release (`-DCMAKE_BUILD_TYPE=Release`, `/usr/bin/clang`) against `54f1047`: `remember` 1,903,752 → 1,904,104 B (+352 B), `libremember_core.a` 281,632 → 285,584 B (+3,952 B). There is no new library, so a non-CMake consumer links the same four archives as in stage 3.

Gate on this tree (2026-10-23), after the last code edit (no file under `src`, `tests`, `docs`, `skills` or `scripts` is newer than the gate start; only this file changed after it):

- `ctest --test-dir build` (ASan/UBSan, `REMEMBER_TEST_VFS=ON`, `ctest -N` = 5): 5/5. `remember_store_tests`: 440 tests, 0 failed.
- `./scripts/lint-all.sh`: LINT OK (format, four boundaries, cppcheck src + tests, `gcc -fanalyzer`, clang-tidy).
- `./scripts/check-coverage.sh`: functions 100%, effective lines 100%; `search_terms.c` 176, `unicode.c` 80, `normalize.c` 268, `output.c` 572 branch sides, 0 missed.
- `cmake --build build --target leaks-macos`: PASS.
- `./scripts/ci-linux.sh`: OK (LSan on), **iwyu PASS**, scan-build PASS.

Stage 5 inherits: set `facet_limit` from `--facet-limit` (default 50); print `keys`/`tags` in the envelope and the human lines with every field escaped or masked (above); the double trim of QUERY.

### 2026-10-24 — stage 5 (CLI + output: `--facet-limit`, envelope, human facet lines, blank query, E2.3 text, one trim owner)

Start: `git fetch`; the branch equalled `origin` (`0790950`, the stage 4 second review, in force). `origin/main` (`59e030d`, plans 19–26 and design logs 007–010) was merged (`4743b0e`). The one conflict was `implementation-plans/INDEX.md`: main's rows 18–26 kept, this branch's 18b/18c rows kept, and main's sentence "They are not files on this branch. Do not add them here" dropped, because on this branch they are files. `standard:coding` ROUND 14 and the stage 4 review's two instructions loaded. Build configured with the cached `/usr/bin/clang`; `ctest -N` lists 5; the baseline `ctest` was 5/5 before any edit.

**Not mine, reported:** one minute after that merge, `fix/stdout-flush-fail-fast` (`4895a60`, "Report a failed stdout flush instead of exiting 0") was merged into this branch as `12c4828` from another session (reflog `2026-10-23 15:11:40`). The status card said to keep that fix local. It is local only (not pushed). Every gate in this stage ran with it in the tree. Itay decides whether it stays on this branch.

Grill, answered and filed (`decision:plan18-human-facet-line`): E2.7's `… and N more` uses N = facet **total** − printed (not items − printed), so `--facet-limit` never changes the human count; the join is `, … and N more` (comma, space, U+2026); the key line shows the key only, masked. A line prints only when its facet has items (with the CLI's `--facet-limit ≥ 1` that is total > 0).

Not grilled, because 006 states it: the E2.3 plain text. E2.3 writes plain `raw query ignored: <fts message>` and JSON `<fts message>`; the E4.6 amendment changed only the prefix (`note:` → `warning:`). So plain is `remember: warning: raw query ignored: <msg>`, JSON unchanged. `diag.c` holds one plain lead per warning code (`k_warning_plain_prefix`); `deprecated_flag` and `synced_folder` keep `warning: `.

Design:

- `cmd_query.c`: `--facet-limit` is parsed in `parse_search_args` only, before the shared `list_handle_opt`, so `list --facet-limit 5` stays `unknown option '--facet-limit'` (probe). One `take_size_option` (`SizeOption`: missing sentence, invalid sentence, max) parses `--facet-limit` and `--limit`; `list_take_limit` is gone. `SearchQuery.facet_limit` = the parsed value, default 50.
- Trim owner (stage 3 review nit, round 13.1): `trim_query_copy` is deleted and `SearchParse` holds the argv token. The CLI checks only D27 and passes QUERY as given; the store owns E2.1's trim and blank rule (D13). The store side is pinned by `store_search_trims_the_query_before_ranking` and `store_search_blank_query_is_empty_without_warning`; `search_query_trims_outer_whitespace` (CLI) now proves only that untrimmed input reaches the store and still matches.
- `output.c`: `write_page_head` / `write_page_entries` are the shared head and tail of `output_list_envelope` and the new `output_search_envelope` (`keys` then `tags` between `total` and `entries`, E2.6). `page_args_ok` is the one argument guard for both; `facets_ok` checks each facet's items against its count. Every facet string goes through `output_json_string` (key, sync_id, label, source, updated_at, tag). `output_search_facets_human` writes the E2.7 lines through `output_text_human` (C0 except tab, DEL, C1 → `?`, `decision:plan18c-mask-c1`).
- `cmd_search` → `emit_search`: JSON envelope, or facet lines then the entry lines. The warning still prints after the output (stage 3).

Ownership: `cmd_search` keeps the whole `SearchResult` and frees it with `store_search_result_free` on every path (stage 4); the output functions only read it.

0.2.0 (and plan 16/17) tests whose behavior changed:

| Test | Was | Now | Why |
|---|---|---|---|
| `test_search.c` `search_empty_query_rejected` | exit 1 `empty search query` | `search_empty_query_is_an_empty_result`: exit 0, empty stderr, the empty envelope with empty facets, on a seeded brain | E2.1 (4) |
| `test_search.c` `search_whitespace_only_query_rejected` | exit 1 `empty search query` | `search_whitespace_only_query_is_an_empty_result` (same) | E2.1 (4) |
| `test_coverage_edges.c` `search_empty_query_stderr` | exit 1 `empty search query` | deleted: the path is gone, the two rows above pin the new contract | E2.1 (4) |
| `test_search.c` `search_query_trims_outer_whitespace` | comment: CLI trim coverage | same assert; comment says the store trims | one trim owner |
| `test_verification_edges.c` V27 `search_unbalanced_quote_warns` | `remember: warning: unterminated string` | `remember: warning: raw query ignored: unterminated string` | E2.3 |
| `test_errors.c` `plain_warning_is_remember_warning` | `remember: warning: x` for `raw_query_ignored` | `remember: warning: raw query ignored: x` | E2.3 |
| `test_list_dates.c` `search_crossed_bounds_are_empty` | `…"total":0,"entries":[]}` | `…"total":0,"keys":{"total":0,"items":[]},"tags":{"total":0,"items":[]},"entries":[]}` | E2.6 |

Ranking needed no 0.2.0 test change in this stage: stage 3 moved the order tests to E2.4, and no CLI test asserts a bm25-only order.

New tests:

- `test_output.c` (pure, under ASan, branch-gated TU): `output_search_envelope_writes_empty_facets`, `…_writes_the_facet_items`, `…_escapes_every_facet_field`, `…_rejects_missing_arguments`, `…_reports_every_failed_write`; `output_search_facets_human_lists_keys_then_tags`, `…_caps_at_ten_and_counts_the_rest_of_the_total`, `…_shows_ten_without_more`, `…_says_more_when_the_total_passes_the_items`, `…_omits_an_empty_keys_line`, `…_omits_an_empty_tags_line`, `…_masks_keys_and_tags`, `…_rejects_missing_arguments`, `…_reports_every_failed_write`. Helpers `given_search_facets`, `given_key_items`.
- `test_union_search.c` (CLI wiring only, round 3.1): `union_search_facet_limit_out_of_range_is_usage` (`0`, `201`, `x`, `-1`, empty, 2^64), `…_without_a_value_is_usage`, `…_defaults_to_50`, `…_accepts_both_bounds` (`1` → 1 item, `200` → 60), `union_search_json_carries_the_facets_before_entries` (Example 1), `union_search_human_prints_the_facets_before_entries`, `union_search_example_2_has_facets_and_the_plain_warning`, `union_search_example_4_filter_scopes_the_facets`.
- `test_errors.c` `search_reports_a_failed_facet_line`, `search_reports_a_failed_entry_line_without_facets` (zero-hit lines below; helper `given_search_brain`).
- `test_get_list_delete.c` `list_limit_without_a_value_is_usage` (mutant C10) and `list_limit_accepts_the_hard_cap` (mutant C9): the `--limit` missing sentence and the upper bound had no test before the shared parser.

TDD red runs: `--facet-limit 0` (`unknown option`), the envelope (compile-red: no `output_search_envelope`), the facet items (empty arrays), the argument guards (ASan SEGV on a NULL stream), the human lines (link-red), the human guards (SEGV), Example 1 CLI JSON (no `keys`), V27 plain text (old text), the two blank-query rows (`empty search query`). Tests that passed on arrival (cap and "more" rules, written with the human writer; masking; both bounds; default 50; Examples 2 and 4; the missing value) are proven by the mutants below.

Mutations (`scratchpad/mutate.py`, `build-mut`: `/usr/bin/clang`, ASan/UBSan, VFS on; `remember_store_tests` plus `remember_tests --only search,union_search,errors,facade,verification_search,coverage_edges,verification_gld,get_list_delete,list_dates,verification_edges,cli_global,tags,ttl,device`; source restored and rebuilt after each). 49 one-clause mutants. Killed, with the red tests:

- JSON: O1 key raw, O2b label not the item's, O6 tag raw → `output_search_envelope_escapes_every_facet_field` (O2b also the Example 1 CLI test). O2 `updated_at` dropped, O3/O4 total → count, O5 tag count 0, O7/O8 no comma after the first item → `…_writes_the_facet_items` (O4 also `union_search_facet_limit_defaults_to_50`). O9 tags before keys → the envelope tests, `search_crossed_bounds_are_empty`, both blank-query rows. O10 keys facet dropped: compile kill (`-Wunused-function`); the meaning is pinned by O9's reds and the items test. O11/O12 each `facets_ok` side, O13 `page_args_ok` skipped, O14 `facets_ok` skipped, O15 the list envelope's entries guard: UBSan/ASan in the guard tests (each mutant removes the guard, so the guarded NULL is read: the meaning).
- Human: H1 cap 11, H2 cap ignored → `…_caps_at_ten_…`. H3 `>=` (writes "and 0 more") → six tests incl. `…_shows_ten_without_more`. H4 keys N from count → `…_caps_at_ten_…`, `…_says_more_…`; H5 tags N from count → `…_says_more_…`. H6/H7 unmasked → `…_masks_keys_and_tags`. H8 `(0)` → five tests. H9/H10 an empty line printed → the omit tests and two CLI human tests (`search_ignores_sort_for_order`, `search_since_filters_updated_at`). H11/H12 separator late → the line tests. H13/H14 a guard dropped: ASan/UBSan in `…_rejects_missing_arguments`. H15 tags line first → four tests incl. the CLI human test.
- CLI: C1 default 49 → `…_defaults_to_50`. C2 max 199 → `…_accepts_both_bounds`; C2b max 201 → `…_out_of_range_is_usage`. C3 zero accepted → `…_out_of_range_is_usage`, `list_limit_zero_rejected`, `golden_error_lines_and_d9_codes`; C3b one rejected → `…_accepts_both_bounds`, `list_offset_pages`, `search_json_paging_fields`. C4 limit not passed → `…_accepts_both_bounds`. C5 list envelope for search → the blank rows, Examples 2 and 4, crossed bounds. C6 human facets skipped → `union_search_human_prints_the_facets_before_entries`. C8 sentences swapped → `…_without_a_value_is_usage`. C9 `--limit` max 999: the first run named only `cli_busy_timeout_zero_is_busy_immediately`, a timing test (`waited < 250` ms, no `--limit`) that failed from load during the mutation run; in fact no test accepted `--limit 1000`. Added `list_limit_accepts_the_hard_cap`; the rerun named it, and the busy test passed. C10 `--limit` missing sentence **survived** → `list_limit_without_a_value_is_usage` added; rerun killed. C11 `--facet-limit` after `--` read as a flag → three facet-limit tests.
- Warning: D1 raw prefix lost → `plain_warning_is_remember_warning`, V27, Example 2. D2/D3 the other codes given the raw words → `every_warning_uses_the_warning_prefix` and two deprecation tests.

Equivalent mutants, kept on purpose:

- C3c `parse_size_token` failure ignored: `util_parse_nonneg` writes `*out` only on success, and `value` starts at 0, so every parse failure is then rejected by `value == 0U` with the same sentence. Kept: the `== 0U` clause would otherwise carry a parse rule it does not state.
- C7 a failed facet write ignored in `emit_search`: a failed write leaves the stream's error flag set, so the entry writes fail too (`failed to write output`), and with no entries `remember_run`'s final `ferror` check (the merged `4895a60`) reports it. Same exit and text either way.
- C12 the CLI trims leading spaces again: the store trims the whole ASCII set. Differential (round 13.3, `scratchpad/diff_c12.sh`): base and mutant binaries over 400 seeded queries (pads of space, TAB, LF, VT, FF, CR around Example terms, FTS syntax and `-x`), each plain and `--json`, against the Examples brain plus a key ` x`: 800 runs, 0 byte differences in stdout, stderr and exit code.

Probes beyond the plan (`build/remember`, ASan, the Examples brain):

- `--facet-limit` values: ` 5`, `+5`, `05`, `200` accepted; `201`, `1e2`, `5 `, empty rejected with the E2.1 sentence. The leading space and `+` come from `strtoull` in `util_parse_nonneg`, the same as `--limit` today (pre-existing; see below). A repeated `--facet-limit` keeps the last, like `--limit`. `search -- --facet-limit` searches for the text (exit 0, warning `fts5: syntax error near "-"`). `list --facet-limit 5` → `unknown option '--facet-limit'`. `--db /nonexistent/d/x.db --no-create search boo` → `cannot open`, exit 1.
- Examples by hand: Ex1 human `keys: recs:books` / `tags: type:books (1)` then both entry rows; Ex2 `keys: recs:books`, no tags line, plain `remember: warning: raw query ignored: no such column: recs`; Ex3 three rows, no facet lines; Ex4 the dentist row only, no facet lines; Ex5 exit 0, one JSON warning. Blank `''` and `'   '`: exit 0, no output; `--json ''` the empty envelope.
- Echoed values (round 3.3) in the new warning text: `a\nb:c` → `…raw query ignored: no such column: b`; ESC → plain `?`, JSON `\u001b`; a 300-byte column name → one 355-byte line.
- Hostile stored facet text (round 12.1/12.2, a hand-edited brain): a key set by `sqlite3` to `evil:` ESC `[2J` U+00C2 U+009B prints `keys: evil:?[2JÂ?` (ESC and the C1 masked, the printable `Â` kept); a title `# t` ESC `[31mred` U+009B `x` gives JSON label `t\u001b[31mred` + raw U+009B + `x` (C1 is legal JSON, `decision:plan18c-mask-c1`). Raw writers of facet data: `grep` of `output.c` finds every facet string written through `output_json_string` or `output_text_human`; counts are `%zu` numbers.

Zero-hit lines in the touched code (round 9.3 / 13.2), from `llvm-cov show build-cov/remember -object build-cov/remember_store_tests -object build-cov/remember_tests -instr-profile=build-cov/prof/merged.profdata <file> | awk -F'|' '$2 ~ /^ *0$/'` after the first full coverage run:

- `src/output.c`: none.
- `src/cmd_query.c`: three were search output failures a user can reach (`remember search boo > /dev/full`): the facet line in `emit_search`, the entry line in `emit_entry_page` (human), and `failed to write output` in `cmd_search`. Each got a test in `test_errors.c` on the rejecting stdout that came with `4895a60`: `search_reports_a_failed_facet_line` (`boo`: the keys line is the first write) and `search_reports_a_failed_entry_line_without_facets` (`dentist`: no facet matches, so the first write is an entry line); both exit 1 with one `remember: failed to write output`. The rest are not this stage's: the `--tag` push OOM, `load_page_neighbors` OOM and store failure (store fault injection only), `utc_now` failure (clock read), and `cmd_list`'s same three lines (list output failure; pre-existing, left for a list change).
- `src/diag.c`: only stderr write failures in `write_notice` and the SQLite emitter, unchanged by this stage (the table it gained has no branch).

Lint on the first full run: cppcheck asked for `const FILE *` in `page_args_ok` and a `const` names table in a test, and flagged four writes through `keys[0]` / `tags[0]` that the test reads through `res.keys.items` (cppcheck does not follow the alias). The tests now write through `res.keys.items[0]` / `res.tags.items[0]`; no suppression.

Machine load: this run's `ctest` took 3,681 s (stage 4: about 100 s) at a load average of 338 with no CPU hog (`ps -Ao pcpu -r`); the repo sits in iCloud Drive and `bird` was syncing. `cli_busy_timeout_500_reports_busy_within_half_a_second` and `cli_busy_timeout_zero_is_busy_immediately` (wall-clock bounds) each failed once under that load and passed on rerun.

Simplify pass:

- Applied: one `take_size_option` for `--limit` and `--facet-limit`, and `list_take_limit` inlined into its one call (`cmd_query.c` is 12 lines shorter than before the stage, `git diff --stat`: +62 −74, with `--facet-limit` added and `trim_query_copy` removed).
- Applied: one envelope head, one entries tail and one argument guard for the list and search envelopes, so the search envelope adds only the two facet writers instead of a second copy of the list envelope.
- Applied (tests): `union_search_bin_flag_scopes_the_keys` deleted: the CLI's bin flag fills the same `ListQuery` the page uses, and `store_search_bin_scopes_the_keys` (stage 4, mutant F6b) proves the facet side. `search_empty_query_stderr` deleted with its path.
- Declined: one facet writer for keys and tags in each format (it would save one of the two loops per format). The item types differ (a key item has five strings; a tag item a string and a count), so one loop needs a callback over `const void *` items; two short typed loops read straight.
- Declined: dropping the `parse_size_token` check (C3c, −1 clause): equivalent only through `util_parse_nonneg`'s write-on-success rule.
- Tests: 14 `test_output.c` tests prove the formats; the 8 CLI tests prove only that the flag, the envelope, the lines and the warning reach the process.

Pre-existing (not this stage, round 4.2):

- `util_parse_nonneg` accepts a leading space and `+` (`strtoull`): `--limit ' 5'`, `--offset +5`, `--busy-timeout ' 9'` and now `--facet-limit ' 5'` all parse. A trailing space is rejected. Harmless, but the asymmetry is real; not changed here.

Handover items: Release (`-DCMAKE_BUILD_TYPE=Release`, `/usr/bin/clang`) against `12c4828` (this stage's base, with the stdout fix): `remember` 1,904,152 → 1,904,392 B (+240 B), `libremember_core.a` 285,760 → 290,272 B (+4,512 B). No new library or build step, so a non-CMake consumer links the same four archives as in stage 3.

Must-pass rows this stage proves at the CLI (the store rows are stage 3 and 4):

| Plan row | Test |
|---|---|
| Example 1 `--json` keys/tags | `union_search_json_carries_the_facets_before_entries` |
| Example 2 keys [`recs:books`], tags [], warning | `union_search_example_2_has_facets_and_the_plain_warning` |
| Example 4 entries, keys [], tags [] | `union_search_example_4_filter_scopes_the_facets` |
| Whitespace-only → exit 0, empty, facets empty, no warning | `search_whitespace_only_query_is_an_empty_result` (and `search_empty_query_is_an_empty_result`) |
| 60 tags: 50 items; `--facet-limit 200` → 60; `0`/`201`/`x` → usage | `union_search_facet_limit_defaults_to_50`, `…_accepts_both_bounds`, `…_out_of_range_is_usage` |
| JSON member order `version, offset, limit, count, total, keys, tags, entries` | `output_search_envelope_writes_empty_facets`, `…_writes_the_facet_items` |
| Human `keys:` / `tags: t (n)` before entries, max 10, `… and N more`, absent when empty | `output_search_facets_human_*` (7 tests), `union_search_human_prints_the_facets_before_entries` |
| Bins and filters scope entries and facets | `union_search_example_4_filter_scopes_the_facets` (CLI); bins at the store (`store_search_bin_scopes_the_keys`) |

Gate on this tree (2026-10-24). After the full run below, one comment line changed in `tests/test_output.c` (the `cppcheck-suppress` with its reason); the store suite, `lint-all.sh` and `ci-linux.sh` ran again on the final tree.

- `ctest --test-dir build` (ASan/UBSan, `REMEMBER_TEST_VFS=ON`, `ctest -N` = 5): 5/5 (3,390 s under the load above). `remember_store_tests`: 455 tests, 0 failed (also on the final tree).
- `./scripts/lint-all.sh` (final tree): LINT OK (format, four boundaries, cppcheck src + tests, `gcc -fanalyzer`, clang-tidy).
- `./scripts/check-coverage.sh`: functions 100%, effective lines 100%; `search_terms.c` 176, `unicode.c` 80, `normalize.c` 268, `output.c` 678 branch sides, 0 missed.
- `cmake --build build --target leaks-macos`: PASS.
- `./scripts/ci-linux.sh` (final tree): Docker's resolver answered nothing for `ports.ubuntu.com` (`getent hosts` empty in the container, with and without `--dns 1.1.1.1`; the host fetched the mirror, HTTP 200). The script's own inner command, `docker run --rm --add-host ports.ubuntu.com:91.189.91.104 -v "$PWD":/src:rw -w /src -e REMEMBER_CI_LINUX_INNER=1 ubuntu:24.04 bash /src/scripts/ci-linux.sh --inner --install`, on the unchanged tree: 5/5 as an unprivileged user, **iwyu PASS**, scan-build PASS, LINT OK, `ci-linux: OK`.

Stage 6 inherits: help (`--facet-limit`, blank query success, the warning text), SKILL.md, README What's New (E2.8 verbatim + the E2.6 order exception + blank search succeeds), QUALITY.md, COVERAGE.md, gate-suites.

### 2026-10-24 — stage 6 (docs + full gate)

Start: `git fetch`; the branch equalled `origin` (`bb317cf`, the stage 5 second review, in force) and `origin/main` was already merged. Loaded `standard:coding` ROUND 15 and the decisions it cites (`plan18-keep-stdout-flush-merge`, `plan18-merge-harness-fail-loud`, `plan18-push-after-green-gate`, `size-option-digits-only`). Build configured with the cached `/usr/bin/clang`; `ctest -N` lists 5.

Grill, answered and filed (`decision:plan18-gate-all-green-suites`): `tests/gate-suites` left out two green suites, so ctest never ran them: `verification_edges` (26 tests, holds V27, which plan 18 rewrote) and `key` (20 tests). Both passed before the change (`remember_tests build/remember --only verification_edges` and `--only key`: 0 failed). Itay chose both. Every suite `test_main.c` registers is now gated (a script compared the `register_` table with the gate line: 28 registered, 28 gated).

Help (`src/remember_app.c`, `remember help search`): the QUERY line, a `--facet-limit N` line, and nine lines on prefixes, the warning, the blank query, `--`, the order, and the facets in both formats replace "Ranked by FTS relevance (bm25), then updated_at." Test: `union_search_help_describes_prefixes_facets_and_the_warning` (byte-exact lines, and the old line is gone). Each sentence was run first: `search boo` finds "Book"; `search fam` lists `tags: family (1)`; `search edit` lists `keys: pref:editor`; `book OR zed` 3 rows; `foo AND` the warning line; `''` and `'   '` exit 0, no output; `search -x` → `unknown option '-x'`; `search -- -x` exit 0 with the warning; the order is the stage 3 ranking tests; the JSON members are the stage 5 envelope tests.

ROUND 15.1 (a probe gets a test): `search -x` without `--` had only stage 3's probe. New `union_search_dash_query_without_double_dash_is_an_unknown_option` (exit 1, empty stdout, `remember: unknown option '-x'`).

SKILL.md (`skills/remember/SKILL.md`): search is prefix-friendly (with examples), the order, `--` for a leading `-`, number options digits only, the search envelope with `keys` / `tags` and their item fields, "read `keys` before `add --key`" (a slot, not a rival), the human facet lines, and the warning line in both formats with "a warning on stderr is not an error". Run against the binary: every example command (`search boo`, `search 'book OR zed'`, the `--tag … --facet-limit 50` line, `"editor preference"`), the member order (`['version', 'offset', 'limit', 'count', 'total', 'keys', 'tags', 'entries']` for search, no facets for `list`), `list --limit +5` and `list --limit ' 5'` → `invalid --limit (must be 1..1000)`, both warning lines byte for byte.

README What's New (v0.3.0): E2.8 verbatim, then the exact warning line in both formats, the order, "JSON `keys` and `tags` now sit between `total` and `entries`" with `--facet-limit`, the blank query (was `empty search query`), digits-only number options, and a failed output write exits 1. The last line ships with plan 18 (`decision:plan18-keep-stdout-flush-merge`); run: `bash -c 'trap "" PIPE; remember --db h.db search boo | true; echo ${PIPESTATUS[0]}'` → `remember: failed to write output`, 1; `exec 1>&-; remember --db h.db list` → the same. "A full disk" was not run here, so the README names only the closed pipe.

Design log 006: one "Shipped in plan 18" block under Implementation Results (E2.1–E2.7, E3, E10.1 query side, D27, the utf8proc version); `git diff --numstat` shows 16 lines added, 0 removed; no earlier plan 18 commit touched 006.

QUALITY.md: the vendor-includes paragraph sat inside the gate table and split it; it now follows the table. New "Vendored C" table (SQLite, sha256, utf8proc 2.12.0: build, the one consumer, the sanitizer gate), from `CMakeLists.txt` and `scripts/lint-all.sh` (`check_boundary`). The boundary claim was run: a temporary `src/zz_probe.c` naming `utf8proc_NFC` made `lint-all.sh` print `utf8proc boundary: FAIL` with that line; the file was deleted. The raw line percentage was refreshed from this gate's coverage run.

COVERAGE.md: rows for `test_search_terms.c`, `test_unicode.c`, `failing_stream.c`; updated rows for `test_union_search.c`, `test_output.c`, `test_key.c`, `test_verification_edges.c` (both gated now); V28 names the blank-query tests. Each row's content was grepped (`test_alloc_fail_after` in `test_search_terms.c`, both-argument-order comparator tests, `unicode_test_fail_after_fails_only_the_nth_call`).

Engineering notes: a "Gate suites (plan 18 stage 6)" bullet. The `store_search` and CLI search bullets were rewritten in stages 3–5.

Mutations (`scratchpad/mutate6.py`, `build-mut`: `/usr/bin/clang`, ASan/UBSan, VFS on; restored from memory, then compared with saved copies: `restored-ok`):

| Mutant | Red tests |
|---|---|
| S1 help without the `--facet-limit` line | `union_search_help_describes_prefixes_facets_and_the_warning` |
| S2 the old bm25 line back | same |
| S3 the warning words changed to `note:` | same |
| S4 the blank-query line dropped | same |
| S5 a leading `-` no longer an option (`arg[1] == '!'`) | `union_search_dash_query_without_double_dash_is_an_unknown_option`, `list_rejects_facet_limit` |

Must-pass rows, each with its tests and their suite (`store` tests also run as ctest `store_asan_gate`):

| Row | Tests | Suite |
|---|---|---|
| Example 1 | `store_search_matches_a_word_prefix`, `store_search_facets_a_matching_key`, `store_search_facets_a_matching_tag`, `union_search_json_carries_the_facets_before_entries` | store, union_search |
| Example 2 | `store_search_runs_p_alone_when_r_does_not_parse`, `store_search_facets_example_2`, `union_search_example_2_has_facets_and_the_plain_warning` | store, union_search |
| Example 3 | `store_search_unions_the_raw_part`, `union_search_parsed_raw_part_has_no_warning` | store, union_search |
| Example 4, E2.5d | `store_search_filters_scope_the_facets`, `union_search_example_4_filter_scopes_the_facets` | store, union_search |
| Example 5 | `union_search_unparseable_raw_part_is_a_json_warning` | union_search |
| E2 verification inputs, 10,000 characters | `union_search_no_input_fails`, `union_search_long_query_succeeds` | union_search |
| 2000 nested `(` | `union_search_deep_nesting_is_a_warning` | union_search |
| Whitespace-only, E2.1 (4) | `store_search_blank_query_is_empty_without_warning`, `store_search_blank_query_has_no_facets`, `search_whitespace_only_query_is_an_empty_result` | store, search |
| Only punctuation that does not parse, E2.1 (5) | `store_search_without_p_and_unparseable_r_is_empty`, `search_unbalanced_quote_warns` | store, verification_edges |
| 60 tags, `--facet-limit` | `store_search_caps_tag_items_at_the_facet_limit`, `union_search_facet_limit_defaults_to_50`, `…_accepts_both_bounds`, `…_out_of_range_is_usage` | store, union_search |
| Key `recs:books` matching | `search_tier_word_prefix_of_a_key_segment_matches`, `…_one_segment_serves_two_tokens`, `…_a_token_without_a_segment_does_not_match`, `…_whole_key_prefix_is_tier_1` | store |
| Tag `project:fortkeeps-cloud` tiers | `search_tier_in_order_tokens_are_tier_2`, `search_tier_out_of_order_tokens_are_tier_3`, `store_search_orders_tags_exact_tier_then_count` | store |
| D27 | `union_search_invalid_utf8_is_usage`, `store_search_invalid_utf8_is_internal` | union_search, store |
| Tier then code point | `search_key_order_is_tier_then_code_point`, `store_search_orders_keys_by_tier_then_code_point` | store |
| D28 tag order | `search_tag_order_is_exact_tier_count_then_code_point` and the four `…_from_either_side` tests, `store_search_orders_tags_exact_tier_then_count` | store |
| E2.1b | `store_search_counts_a_row_both_parts_match_once`, `store_search_pages_the_deduplicated_set` | store |
| E2.4 ranking | `store_search_ranks_exact_key_then_key_prefix_first`, `…_ranks_a_keyless_row_by_bm25`, `…_ranks_by_weighted_bm25`, `store_search_ties_break_by_id` | store |
| `label` (D12, P2) | `search_label_*` (combining marks, ZWJ sequence, 60/61 graphemes), `store_search_labels_a_key_with_its_title`, `…_without_a_title_with_the_key` | store |
| JSON member order | `output_search_envelope_writes_empty_facets`, `…_writes_the_facet_items`, `search_crossed_bounds_are_empty` | store, list_dates |
| Human E2.7 | `output_search_facets_human_*`, `union_search_human_prints_the_facets_before_entries` | store, union_search |
| E10 `ספר` / E10.1 | `store_search_hebrew_prefix_finds_niqqud`, `store_search_vocalized_query_matches_through_p` | store |
| E3.2 damaged FTS | `cli_damaged_fts_search_reports_fts_only`, `store_search_probe_error_without_a_d1_message_is_real` | errors, store |
| E3.3 | `union_search_no_input_fails` | union_search |
| Bins and filters | `store_search_bin_scopes_the_keys`, `…_filters_scope_the_facets`, `…_filters_scope_the_tag_counts`, `…_since_scopes_the_keys_on_updated_at`, `union_search_example_4_filter_scopes_the_facets` | store, union_search |
| `search editor` | `store_search_finds_a_key_segment` | store |
| Facade byte-match incl. warning | `facade_search_matches_cli`, `facade_search_bad_query_matches_cli`, `facade_search_json_warning_matches_cli` | facade |
| Unit (`search_terms`) E2.2 | `search_tokens_compose_to_nfc`, `…_split_on_ideographic_space`, `…_keep_the_first_16`, `…_keep_the_first_64_code_points`, `…_split_on_a_double_quote` (replaces "`"` doubled", `decision:plan18-drop-quote-doubling`), `…_remove_hebrew_marks` | store |

`search_unbalanced_quote_warns` (row E2.1 (5)) ran in no ctest suite until this stage put `verification_edges` in the gate.

Gate on this tree (2026-10-24, after the last edit to `src`, `tests`, `scripts` and `skills`; only Markdown changed after it: this file, `docs/QUALITY.md`'s raw line figure):

- `ctest --test-dir build` (ASan/UBSan, `REMEMBER_TEST_VFS=ON`, `ctest -N` = 5): 5/5 in 71 s (load back to normal). `remember_tests --only "$(./scripts/read-gate-suites.sh)"`: 815 tests, 0 failed (now with `key` and `verification_edges`). `remember_store_tests`: 455 tests, 0 failed.
- `./scripts/lint-all.sh`: LINT OK (format, four boundaries, cppcheck src + tests, `gcc -fanalyzer`, clang-tidy).
- `./scripts/check-coverage.sh`: functions 100%, effective lines 100%, raw lines 85.88%; `search_terms.c` 176, `unicode.c` 80, `normalize.c` 268, `output.c` 678 branch sides, 0 missed.
- `cmake --build build --target leaks-macos`: PASS.
- `./scripts/ci-linux.sh`, unmodified (Docker DNS works again): 5/5 as an unprivileged user, iwyu PASS, scan-build PASS, LINT OK, `ci-linux: OK`.

No new C logic this stage (help strings only), so no Release size delta is listed.

Definition of Done: ticked above with the proof named beside each box. Not ticked: the branch gate in GitHub Actions `linux-coverage` (needs a push and a CI run; macOS shows 0 missed), the FINAL second review, and the INDEX row (main's INDEX says "Itay finishes this plan and merges it. Do not tick it here.").

## Review Notes

### 2026-10-20 — in-session deep review (stage 1)

Lenses: `pref:review-lenses` 1–9, comprehensive, ponytail, `docs/STEP_REVIEW_CHECKLIST.md`. Bugbot is not installed. Target: `d1e8668` plus the fix below.

- **nit, fixed.** `unicode.h` said its category values "match the pinned utf8proc enum". That names the vendor in the port. The header now says only "Unicode general categories". The reason for the pins moved to the `PIN_CAT` block in `unicode.c`. `grep utf8proc src/unicode.h` finds nothing.
- **nit, deferred.** `check_boundary` matches its allow ERE against the whole grep line, so a file outside the allow list that names the allowed file in the same line passes (for example a comment with `unicode.c` and `utf8proc_`). That is the old semantics of all four rules, kept as is. Anchoring the file names (`^src/unicode\.c:`) would also need a separate content allow for `REMEMBER_SHA256` and the `remember_run` line. Not this stage.
- **important, grilled.** NFC is quadratic on one long combining-mark run (numbers under Implementation Notes). E2.2 normalizes the whole QUERY before the 16 × 64 cap. Itay chose: accept and document (`decision:plan18-nfc-cost-accept`). Stage 2 keeps E2.2 as written, adds a `ponytail:` comment that names the ceiling and the upgrade path, and adds an engineering-notes line.
- **praise.** One consumer, a vendor-free port, 30 pinned category values, an exhaustive UCD 18 probe, and every non-equivalent mutant red.

Gate after the fix, on this tree: ctest 5/5, `lint-all.sh` LINT OK, coverage 100/100, `ci-linux.sh` OK (iwyu PASS), `leaks-macos` PASS.

Pragmatic write gate: DRY PASS (one `bounded_bytes` for both entry points; one `check_boundary` for four rules). Easy-to-replace PASS (only `unicode.c` knows utf8proc; a swap rewrites one file, and the pins fail the build if the numbering differs). Orthogonal PASS (the CLI, store, and output have no diff). Simplify: see the pass under Implementation Notes.


### 2026-10-20 — second-opinion deep review (stage 1)

**Verdict:** stage 1 approved. No open blocking item. No grill. No code change. One handover item filled in, and one integration note.

**Run, not read.**

- **Gate.** Baseline `3b4eb1f`: ctest 5/5 (ASan/UBSan, `REMEMBER_TEST_VFS=ON`), LINT OK, coverage 100/100, `leaks-macos` PASS, `ci-linux.sh` OK with **iwyu PASS** (the IWYU run the stage requires), `GATE-ALL-GREEN`. This review changes only this plan file, so that gate covers the pushed C tree.
- **Vendor pin, checked against upstream without a download.** For `utf8proc.h`, `utf8proc.c`, `utf8proc_data.c`, and `LICENSE.md`, `git hash-object` on each file equals the blob `sha` that `gh api repos/JuliaStrings/utf8proc/contents/<f>?ref=v2.12.0` returns. All four match, so "local modifications: none" holds byte for byte.
- **Mutants**, re-run under ASan in a clean worktree. Each failed the tests the coder lists:
  - grapheme `state` made `static`: 3 cluster tests.
  - `i += n2` → `i += 1`: 4 cluster tests.
  - NEL test shifted by one: both whitespace tests.

**Findings:**

1. **nit → filled in here (handover item).** The handover asks for the binary size delta, and the notes did not have it. Release builds (`-DCMAKE_BUILD_TYPE=Release`, clang), in clean worktrees:

   | Binary | `main` `bc06fcb` | stage 1 `3b4eb1f` | Delta |
   |--------|------------------|-------------------|-------|
   | `remember` | 1,536,824 B | 1,886,056 B | +349,232 B (+22.7%) |
   | `libremember_core.a` | 267,760 B | 269,776 B | +2,016 B |

   `__TEXT` grows by 344,064 B, almost all of it utf8proc's tables. The CLI pays this already in stage 1, because `unicode.c` is in `REMEMBER_LIB_SOURCES` and pulls `utf8proc.o` in. D2 locks utf8proc, so this is a record, not a finding against it.
2. **nit, open (integration).** `utf8proc` is its own static lib, not part of `libremember_core.a`. CMake consumers get it through `remember_core`'s `PUBLIC` link. A consumer that links the `.a` files by hand, as the Mac app may, must add `libutf8proc.a` next to `libsqlite3.a` and `libsha256.a` once stage 2 calls the port. Before that, nothing pulls `unicode.o` out of the archive, so the gap does not show yet.
3. **nit, agreed.** The coder's deferred `check_boundary` note (the allow pattern matches the whole grep line) is the old semantics of all four rules. It is fine to leave.

**Praise:**

- The conformance probe ran every scalar against White_Space, all 775 grapheme lines, and every NFC line.
- 69 mutant runs, with the equivalent mutants each explained against the vendor source.
- A port header that never names the vendor, and 30 pinned category values that fail the build if the vendor renumbers.
- The NFC cost was measured and grilled before it shipped (`decision:plan18-nfc-cost-accept`).

**Coder rating (plan 18 stage 1): 9.5/10.** This is the strongest stage yet. It verified everything a reviewer would check: the tarball hash, per-file identity, the UCD conformance files, one-clause mutants including the equivalent ones, a simplify pass that shrank the tests from 722 to 491 lines while adding 7, and a cost grill. The one gap was the size delta the handover names.

**Paste-ready for the coder:**

```text
1. Before REVIEW_REQUEST, go through the plan's "Package to hand over" list item by item, as you did the vendoring checklist. This stage missed the binary size delta (Release remember +341 KB).
2. When a stage adds a static lib, note in the Implementation Notes what a non-CMake consumer of libremember_core.a must now link.
```

### 2026-10-20 — in-session deep review (stage 2)

Lenses: `pref:review-lenses` 1–9, comprehensive, ponytail, `docs/STEP_REVIEW_CHECKLIST.md`. Bugbot is not installed.

- **critical (DRY), fixed.** The ASCII trim loop lived in three places: `normalize.c` `ascii_ws_trim_span`, `cmd_query.c` `trim_query_copy`, and the new `search_terms.c` `ascii_trim`. Now there is one `ascii_ws_trim` in `normalize.h`. Mutants and red tests are under Implementation Notes.
- **important (process), fixed.** The first stage 2 `ctest` ran 4 of 5 tests: the `build` cache had `REMEMBER_TEST_VFS=OFF` (what set it before this stage is not reproduced). The first reconfigure did not fix it, and that cause is reproduced: passing `-DCMAKE_C_COMPILER=clang` while Homebrew clang is first on `PATH` changes the cached absolute path, so CMake deletes the cache, and the `-D` options on that same line are lost. Fix: configure with the cached absolute compiler path. `ctest -N` now lists 5 tests, including `no_shim_symbols`. The gate below ran on that cache.
- **nit, kept.** `segment_tier` calls `find_segment` twice per token while the order still holds. One call from the cursor would do when it succeeds. The saving is microseconds on 64-byte keys, and the two calls read as "is it there" and "is it in order".
- **grill carried to stage 3.** E2.2 step 4 (`"` doubling) can never fire; see Implementation Notes.
- **praise.** Pure comparators, a caller-owned token block with no nested allocation, every mutant red, and three grills asked before any code.

Pragmatic write gate: DRY PASS after the fix (one trim, one fold, one decoder, one Hebrew set). Easy-to-replace PASS (search_terms knows only `unicode.h` and `normalize.h`; utf8proc stays behind the port). Orthogonal PASS (no store, CLI, or output change; the stage 3 adapter is the next consumer).


### 2026-10-20 — second-opinion deep review (stage 2)

**Verdict:** stage 2 approved. No open blocking item. One grill, answered (`decision:coverage-branch-gate-followup`, a follow-up plan, not a change here). One test added. No production code change.

**Run, not read.**

- **Gate.** Baseline `4454df0` (gate root clean): ctest 5/5, LINT OK, coverage 100/100, `leaks-macos` PASS, `ci-linux.sh` OK, `GATE-ALL-GREEN`. Fixed tree: ctest 5/5 (ASan/UBSan, `REMEMBER_TEST_VFS=ON`), LINT OK, coverage 100/100, `leaks-macos` PASS, `ci-linux.sh` OK with iwyu PASS, `GATE-ALL-GREEN`, in a clean worktree of `4454df0` plus the new test and these notes.
- **Stored keys and tags are ASCII-lowercased.** `add --key Recs:Books --tag Type:Books` stores `recs:books` and `type:books`. Lowercased tokens against unlowered segment text therefore never mismatch on ASCII.
- **The `ascii_ws_trim` refactor.** All three callers (`body_trim_copy`, `normalize_token`, `trim_query_copy`) set `start = 0` right before the call, so the new in/out form is behavior-neutral.
- **Mutants**, one clause each, under ASan in a clean worktree:

  | Mutant | Result |
  |--------|--------|
  | same segment cannot serve two tokens (`cursor = found + 1`) | caught: `search_tier_one_segment_serves_two_tokens` |
  | ellipsis even when not cut (`<=`) | caught: 5 label tests |
  | tokens not lowercased | caught: 2 token tests |
  | slots counted by kept tokens, so an emptied token is free | caught: `search_tokens_an_emptied_token_still_used_a_slot` |
  | no exact-first rule in the tag order | caught: `search_tag_order_is_exact_tier_count_then_code_point` |
  | `slots++` dropped | ASan abort (token array overflow); the row above is the semantic kill |
  | tier checked before exact | equivalent: an exact tag equals q, so it is always tier 1 |
  | exact tag on the **left** returns 1 | **survived** → finding 1 |

**Findings:**

1. **important → fixed. A real comparator branch never ran, and coverage still read 100%.** In `search_tag_order`, `if (lhs->exact) return -1;` had 0 hits. `qsort` only ever passed the exact tag as the right-hand argument, so only `return 1` ran. `check-coverage.sh` excludes every zero-hit `return -1;` as defensive (line 148), so the effective line number stayed 100%. The new `search_tag_order_puts_exact_first_from_either_side` calls the comparator both ways, and the left-side mutant now fails it. I read all zero-hit lines in `search_terms.c`: the rest are OOM, unreachable status mappings, and decode errors on text that NFC has already validated.
2. **Grill → follow-up plan (Itay 2026-10-20, `decision:coverage-branch-gate-followup`).** The regex exclusion (2,169 zero-hit lines today) cannot tell a defensive return from a rule branch. A plan after 18 adds an llvm-cov branch-coverage gate for the pure TUs (`search_terms.c`, `unicode.c`, `normalize.c`, `output.c`). The line regex stays.
3. **nit, considered and not applied (simplify).** I tried `return (cp >= '0' && …);` in `is_ascii_alnum` and `return !is_ascii_alnum(cp);` in `splits_token` (−6 lines). clang-tidy `readability-implicit-bool-conversion` rejects both, because C relational operators yield `int`. The coder's `if`/`return true` shape is what lint forces.

**Praise:**

- Three grills before writing code, each recorded as a decision with its reason: the empty-token slot, segment normalization, and Unicode whitespace in segments.
- Reuse instead of copies: `unicode_decode` joins the port, and `is_hebrew_mark` and one `ascii_ws_trim` come out of `normalize`.
- The handover package is itemized in the marker, including the Release size delta (last round's advice).
- It flagged E2.2 step 4 as unable to fire, for stage 3's grill.

**Coder rating (plan 18 stage 2): 9/10.**

- Every mutant it claimed was red, I could reproduce.
- It applied both instructions from stage 1 the same day.

**Miss:** it did not read the zero-hit lines that the coverage regex excluded. One of them was a rule branch.

**Paste-ready for the coder:**

```text
1. After check-coverage, list the zero-hit lines in the TUs you touched (llvm-cov show ... | awk -F'|' '$2 ~ /^ *0$/') and read each one. The regex excludes every "return -1;" and *_ERR_ return, so a real branch (a comparator side, a rule) can hide there.
2. A qsort comparator gets a direct test with each argument order; qsort decides which side it calls, not your test.
```

### 2026-10-22 — in-session deep review (stage 3)

Lenses: `pref:review-lenses` 1–9, comprehensive, ponytail, `docs/STEP_REVIEW_CHECKLIST.md`. Bugbot is not installed.

- **important, fixed.** M19 and M19b survived: the ranking fixture did not pin the key-prefix rule, and its comment claimed a bm25 order nobody had run. The fixture now comes from a probe (Implementation Notes).
- **important, fixed.** Four must-cover failure lines in `store_search` (NULL arguments, OOM, BEGIN, COMMIT) had no test; the first coverage run named them.
- **important, fixed (round 2.4).** Old search tests read `entries[0]` after a failed assert, so mutant M7 crashed the store suite instead of naming red tests. Six reads are guarded.
- **nit, fixed.** Stale comments: `store_status_plain` ("non-FTS"), `list_sort_column` and `list_append_cmp` (search now passes `updated_at`), the CLI positional ("FTS query"); docs that still said `invalid search query`.
- **grill ×4**, answered: `decision:plan18-drop-quote-doubling`, `decision:plan18-warning-wired-stage3`, `decision:plan18-delete-store-err-query`, `decision:plan18-dash-query-needs-double-dash`.
- **praise.** One MATCH gives the dedup for free; a probe that keeps every non-D1 error real; `IS` closes the NULL-key ranking trap before it ships.

Pragmatic write gate: DRY PASS (the probe reuses `list_bind_texts`, `store_status_plain`, `dup_str`; one `utf8_is_valid`; one error map in `run_count_and_page`). Easy-to-replace PASS (tokens and q stay in `search_terms.c`; SQLite stays in `store_sqlite.c`; the CLI sees only `SearchResult`). Orthogonal PASS (`list` code paths unchanged except the dropped `map_err` argument; output untouched).

### 2026-10-22 — second-opinion deep review (stage 3)

Ref `fa3702a` (range `17f01e6..fa3702a`). Baseline gate on a clean worktree of the ref: `GATE-ALL-GREEN` (ctest, `lint-all.sh`, `check-coverage.sh` with search_terms 176 / unicode 80 / normalize 268 / output 572 sides and 0 missed, `leaks-macos`, `ci-linux.sh`). Decisions loaded and checked in the code: `plan18-drop-quote-doubling` (no doubling loop; `"tok"*`), `plan18-warning-wired-stage3` (`cmd_search` prints `raw_query_ignored` on success), `plan18-delete-store-err-query` (`STORE_ERR_QUERY` and `store_status_from_sqlite` gone; `invalid_query` stays in `diag.c`), `plan18-dash-query-needs-double-dash` (`search -x` → `unknown option '-x'`; `search -- -x` → exit 0, `no such column: x`).

**Verdict: stage 3 is done. No open blocking item, no grill.**

Findings:

- **important (fixed): the store's trim of QUERY was untested.** E2.1 defines R and q as QUERY trimmed, and `store_search` trims, but every test reaches the store with an already-trimmed query, because the CLI trims first (`trim_query_copy`). Mutant X3 (`search_q` fed the untrimmed span, which makes `search_q` return INTERNAL on any trailing whitespace) survived the whole suite. Added `store_search_trims_the_query_before_ranking`: `"  Editor \t"` must put the exact key first, where bm25 alone puts `pref:editor` first. X3 now fails it.
- **nit (fixed): a user error path was listed as pre-existing failure noise.** In `cmd_query.c` the zero-hit `return -1;` at the second positional (`search a b` → `unexpected argument`, exit 1) is a plain user path, not an OOM or output failure as the notes grouped it. Added `search_second_query_is_unexpected`; the mutant that lets a second positional overwrite the first fails it.
- **nit (open, for stage 5): QUERY is trimmed twice**, by `trim_query_copy` in the CLI and by `store_search` (E2.1's R). Stage 5 reworks the CLI blank-query path; leave one owner there. The store owns E2.1 per D13, so the CLI trim is the one to go, unless stage 5 needs it for the usage text.
- **praise:** one MATCH gives the dedup; the probe keeps every non-D1 error real and frees the warning when the query later fails (damaged index: exit 1, empty stdout, no warning, on every path that reads the index); `IS` instead of `=` caught before shipping; the coder found and fixed their own M19/M19b survivors and backed every equivalent mutant with a probe.

Verification by running (ASan `remember` from the baseline build):

- 006 Examples that stage 3 covers: Ex1 `boo` 2 entries, `recs:books` first; Ex2 `recs:bo` 1 entry + `remember: warning: no such column: recs`; Ex3 `book OR zed` 3 entries, empty stderr; Ex4 `boo --tag family` the dentist row only; Ex5 `--json --json-errors 'foo AND'` exit 0 with one `raw_query_ignored` JSON warning.
- Grammar-aware fuzz, 3,000 queries built from FTS5 pieces (column filters, `{cols}:`, `-col:`, `NEAR(…)`, `^`, `+`, `*`, AND/OR/NOT, parentheses, quotes, Hebrew, emoji, a combining accent), each passed after `--`: 1,155 exit 0 clean, 1,845 exit 0 with a warning, 0 other exits, no sanitizer report. So no R that parses alone fails inside `(P) OR (R)`.
- Damaged index (`entries_fts_data` blocks overwritten): `boo`, `book OR zed`, `recs:bo` → exit 1, empty stdout, `SQLITE_CORRUPT_VTAB` text. `"` → exit 0 + warning + empty result: R fails to compile and P is empty, so no statement reads the damaged data (no error exists to swallow).
- Hostile input: ESC → plain `?`; `a\nb:c` → `no such column: b` (FTS5 splits on LF); a 300-byte column name → the full 336-byte line; `--db /nonexistent/x.db --no-create` → `cannot open`, exit 1; no query → `missing search query`; `\xff` → `invalid UTF-8 in search query` (D27); 10,000 tokens → exit 0 in 0.38 s.

Mutation sample of the coder's table, re-run (one clause each, `remember_store_tests` + the coder's CLI suites): M1 `*` dropped, M9c `unknown special query`, M14 probe on blank R, M18a `IS` → `=`, M21a key weight 10 → 1, M31 COMMIT failure keeps the page: each killed by the test the notes name. My own:

| Mutant | Result |
|---|---|
| X2 `(%s) OR (%s)` → `%s OR %s` | survived; equivalent in practice: base and mutant gave byte-identical stdout, stderr and exit codes on the same 3,000 fuzz queries (FTS5 binds NOT > AND > OR, and P uses only implicit AND). Kept, because E2.1 writes `(<P>) OR (<R>)`. |
| X3 q from the untrimmed span | survived → killed by `store_search_trims_the_query_before_ranking` |
| second positional overwrites the first (`cmd_query.c`) | survived (no test) → killed by `search_second_query_is_unexpected` |

Handover size delta re-run (Release, `/usr/bin/clang`, clean worktrees of `17f01e6` and `fa3702a`): `remember` 1,887,176 → 1,903,752 B (+16,576), `libremember_core.a` 279,248 → 281,632 B (+2,384), the same numbers as the coder's.

Zero-hit lines read in the touched code (round 9.3): `store_sqlite.c` search functions: only SQL-buffer truncation guards on fixed SQL and the probe's bind failure (the coder's M33). `cmd_query.c`: the second-positional path (now tested) and pre-existing `list_prepare_query` / `utc_now` / output failures.

Simplify pass: nothing to shrink. `store_search`'s two equal-size buffers (`r`, `q_low`) could share one allocation (−2 lines, less clear); `SearchBinds` exists because two adjacent `const char *` parameters trip `bugprone-easily-swappable-parameters`; the coder's four applied simplifications (−22 lines) and three declines (one shape with the list path, the probe reusing `list_bind_texts`, the E2.1 rule kept in `search_match`) hold. Tests: `test_union_search.c` (6 tests) proves only that exit and warning reach the process; content is in `test_store_search.c`.

Coder rating: **8.5 / 10.** Strengths: the most complete notes so far (every decision grilled and filed, 45+ one-clause mutants with the red test named, equivalents argued with probes, a 1,000-input fuzz, the size delta measured); found and fixed their own survivors (M19, M19b) and a test that read past a failed assert. Misses: the store's half of a rule both layers apply (the trim) had no test, so X3 survived; a user-reachable error path was filed under failure noise in the zero-hit list; the double trim went unnoticed.

Instructions for the coder:

1. When two layers apply the same rule to one input (trim, case fold, validation), test the inner layer directly with input the outer layer would have fixed, and name one owner of the rule in the notes.
2. In the zero-hit list, name each line's trigger by running it. A path a user can reach is a test, not "pre-existing".
3. For an "equivalent mutant" in a parser or query builder, back the claim with a differential run: base and mutant over the same fuzz corpus, outputs compared byte for byte.

Gate (fix tree, 2026-10-23; clang, `REMEMBER_ENABLE_SANITIZERS=ON`, `REMEMBER_TEST_VFS=ON`, configured twice, cache flags checked): build, ctest, `lint-all.sh`, `check-coverage.sh` (search_terms 176, unicode 80, normalize 268, output 572 sides, 0 missed), `leaks-macos` all green. `ci-linux.sh` first failed on infrastructure: Docker's resolver returned only IPv6 for `ports.ubuntu.com`, so apt installed nothing (`Unable to locate package`). The same inner command (`docker run … ubuntu:24.04 bash /src/scripts/ci-linux.sh --inner --install`) with `--dns 1.1.1.1` on the unchanged tree: exit 0, iwyu PASS, scan-build PASS, LINT OK. Default DNS recovered before the push; the pre-push hook ran `ci-linux.sh` unmodified. After the gate only this file changed (Markdown). Standard filed: `standard:coding` ROUND 13.

### 2026-10-23 — in-session deep review (stage 4)

Lenses: `pref:review-lenses` 1–9, comprehensive, ponytail, `docs/STEP_REVIEW_CHECKLIST.md`. Bugbot is not installed.

- **critical (fail-fast), fixed before the gate.** The first draft dropped every `search_facet_classify` failure as a non-match, so an out-of-memory dropped a key silently. The pure-TU allocation sweep caught it; OOM is now `STORE_ERR_OOM`, any other failure is INTERNAL, and only invalid stored UTF-8 is skipped (grilled).
- **important, fixed.** A facet failure after the page leaked the page entries. `store_search_facet_failure_drops_the_page` found it; the error cleanup is now `store_search_result_free`.
- **important, fixed.** Three survivors: F11b (no unicode failure during classify), F18 (the label was never checked after a sweep), C3 (no CLI test with total ≠ count). Each has a test now.
- **nit, fixed.** A comment sat inside the `if` condition of the key copies; it now precedes the `if` and states the NOT NULL reason that was probed.
- **grill ×2**, answered: `decision:plan18-facets-independent`, `decision:plan18-facet-invalid-utf8-no-match`.
- **finding, pre-existing (not fixed here):** stdout flush failures exit 0 (above). Proposed as its own task.

Pragmatic write gate: DRY PASS (one fetch and one appender for both kinds; the WHERE is `list_build_where`; `dup_col_req`, `store_status_plain`, `list_bind_texts` reused; one free path). Easy-to-replace PASS (SQLite stays in `store_sqlite.c`; classification and order stay in `search_terms.c`; the CLI sees only `SearchResult`). Orthogonal PASS (`list` untouched; the page query and the facet queries share only the read transaction and the filters).

### 2026-10-23 — second-opinion deep review (stage 4)

Ref `62ab14c` (range `54f1047..62ab14c`). Decisions loaded and checked in the code: `plan18-facets-independent` (`store_search` runs `search_facets` whenever the probe succeeds, with or without a MATCH), `plan18-facet-invalid-utf8-no-match` (`facet_append_row` skips `SEARCH_ERR_INVALID_UTF8`).

**Verdict: stage 4 is done. No open blocking item, no grill.**

Findings:

- **important (fixed): facets were never tested with an uppercase query.** E2.5b and E2.6 compare the whole key with q (QUERY lowercased), and `FacetScope.q` is `q_low`, but every facet test used a lowercase query. Mutant Y6 (facets classify against R, the trimmed but not lowercased query) survived the suite. Added `store_search_facets_compare_the_lowercased_query`: key `(draft)` matches only through the whole-prefix rule (the token `draft` cannot match the segment `(draft)`), and `(DRAFT` must return it. Y6 now fails it.
- **nit (fixed, pre-existing, the coder's report): reads past a failed assert.** `test_store.c` `store_list_and_search_bins` read `rows[0]` after `ASSERT_EQ_INT(total, 1)` in its two list sections; when the harness's `sqlite3` UPDATE does not run, `rows` is NULL. Both reads are now guarded like the search section below them (round 2.4).
- **important (pre-existing, confirmed; follow-up proposed): a failed final flush of stdout exits 0.** Reproduced on macOS with a closed pipe and SIGPIPE ignored: `search dentist`, `list`, and `--json get --key pref:editor` each exit 0 with no message and nothing written. `src/` has no final `fflush`/`ferror` check on stdout. It affects every command (`style:fail-fast`); out of this stage's scope.
- **nit (pre-existing, follow-up proposed): test helpers that do nothing silently.** `harness_sqlite_query_line` returns NULL for "no row", "SQL longer than `CMD_BUFSIZE`" and "no `sqlite3` on PATH" alike, so fixture SQL can fail without failing the test (seen in plan 18c and in the finding above; `standard:coding` 12.4).
- **praise:** one fetch and one appender for both facet kinds; a single owner per moved string (zeroed in the candidates, so one free path); leak-only mutants killed under LSan in Linux instead of left as "survived"; a 20,000-trial differential behind the one equivalent mutant that needed it (round 13.3, applied the day it was filed); both pre-existing bugs reported with a run.

Verification by running:

- Store-API probe (`probe.c` linked against the Release `libremember_core.a`, `libutf8proc.a`, `libsqlite3.a`, `libsha256.a`; the CLI prints no facets until stage 5):
  - Example 1 `boo`: entries 2, keys `[recs:books | Book recommendations]`, tags `[type:books (1)]`. Example 2 `recs:bo`: entries 1, keys `[recs:books]`, tags `[]`, warning `no such column: recs`.
  - Handover probe 4: `recs:books` deleted; `boo` live → keys 0/0; `boo` with `--deleted` → keys 1/1 and tags `type:books (1)`.
  - Handover probe 5: keys `a:b`, `b:a`, `B:a`, `é:x` → stored `a:b`, `b:a`, `é:x` (`B:a` folds to the existing `b:a`); query `a` → `a:b` (tier 1), `b:a` (tier 2), `é:x` no match.
  - Handover probe 6: `book --deleted` with limit 1 offset 1 → entries 0/1, keys 1/1, tags 1/1, the same facets as without paging.
- Real data: a `.backup` copy of Itay's brain (760 rows), Release, six queries × 10 warm runs: stage 3 `54f1047` 6.5 ms/search, this tree 8.8 ms/search (+2.3 ms; the CLI already computes facet totals with `facet_limit` 0).
- Size delta re-run (Release, `/usr/bin/clang`): `remember` 1,903,752 → 1,904,104 B (+352), `libremember_core.a` 281,632 → 285,584 B (+3,952), the coder's numbers.
- Schema facts the facet SQL relies on: `ux_entries_key` makes `key` unique across bins, so the keys facet never lists one key twice; the bin clause is always emitted, so the `AND e.key IS NOT NULL` tail always follows a WHERE.

Mutation sample of the coder's table, re-run: F1 key filter dropped (63 reds), F3 `COUNT(*)` → 1, F9 invalid UTF-8 not skipped, F29 tag count 0: each killed by the tests named. F25 first failed to compile (the rewrite left `limit` unused); rerun valid-but-wrong as "limit applies only past 1,000 items": killed by the cap and zero-limit tests. Mine:

| Mutant | Result |
|---|---|
| Y4 facets run only when a MATCH runs (against `decision:plan18-facets-independent`) | killed (`…facets_do_not_need_a_memory_match`, the step-failure and alloc sweep tests) |
| Y6 facets classify against R instead of q | survived → killed by `store_search_facets_compare_the_lowercased_query` |

Simplify pass: nothing to shrink in production code. The coder's declined `facet_take` merge holds (a key item moves whole, a tag item is rebuilt from two sources). The key query reads every keyed body in scope while only matches need a label; at 760 rows that costs nothing measurable, so no change (a second query for matched bodies would be the upgrade if a brain grows to hundreds of thousands of keyed rows).

Coder rating: **9 / 10.** Strengths: the facet design reuses one fetch, one appender and the list WHERE; 63 one-clause mutants with reds named, compile kills rerun as valid-but-wrong, leak-only mutants killed under LSan; ROUND 13 applied the same day (triggers named for every zero-hit line, a 20,000-trial differential); two grills before code; two pre-existing bugs reported with a run; the handover size delta and a cost probe. Miss: the facets take a derived input (q, lowercased), and no test made the derived value differ from the raw one, so Y6 survived. That is the stage 3 X3 pattern again (a rule applied through a derived value, tested only with input where raw and derived agree).

Instructions for the coder:

1. For every derived input a function uses (q lowercased, R trimmed, tokens folded), write at least one test where the derived value differs from the raw input (uppercase, padding, marks), so passing the raw value instead fails.
2. After a fixture step that can silently do nothing (an external tool, a helper that returns NULL for both "no row" and "not run"), assert its effect before the test relies on it.

Gate: baseline `62ab14c` and the fix tree (2026-10-23; clang, `REMEMBER_ENABLE_SANITIZERS=ON`, `REMEMBER_TEST_VFS=ON`, configured twice, cache flags checked): build, ctest, `lint-all.sh`, `check-coverage.sh` (search_terms 176, unicode 80, normalize 268, output 572 sides, 0 missed), `leaks-macos`, `ci-linux.sh` → `GATE-ALL-GREEN` on both. After the fix-tree gate only this file changed (Markdown). Standard filed: `standard:coding` ROUND 14.

### 2026-10-24 — in-session deep review (stage 5)

Lenses: `pref:review-lenses` 1–9, comprehensive, ponytail, `docs/STEP_REVIEW_CHECKLIST.md`. Bugbot is not installed. ROUND 14 applied: the derived input this stage feeds (QUERY to the store) is tested raw and padded (`search_query_trims_outer_whitespace`, the blank rows, the C12 differential); the fixture steps (`given_add`, `given_search_brain`) assert their exit code before the test relies on them.

- **important, fixed.** Two survivors: C10 (the `--limit` missing sentence had no test) and C9 (no test accepted `--limit 1000`; its first "kill" was a wall-clock test failing under load). Both got tests; reruns name them.
- **important, fixed (round 13.2).** Three search output-failure lines a user can reach had no test; `test_errors.c` now drives them through a rejecting stdout.
- **nit, fixed.** cppcheck `constParameterPointer` / `constVariable` / four alias false positives; fixed in code, one justified suppression.
- **grill ×1**, answered: `decision:plan18-human-facet-line`.
- **finding, not mine:** `fix/stdout-flush-fail-fast` was merged into this branch (`12c4828`) from another session against the status card's "keep it local". Reported to Itay; not pushed.
- **finding, pre-existing:** `util_parse_nonneg` accepts a leading space and `+` (above).

Pragmatic write gate: DRY PASS (one size-option parser, one envelope head/tail/guard, one masked writer, one plain-lead table). Easy-to-replace PASS (the CLI sees `SearchResult` only; formats live in `output.c`; the warning words live in `diag.c`). Orthogonal PASS (`list` output bytes unchanged: `output_list_envelope` tests and the list suites pass as before; `--facet-limit` is not a list option).

### 2026-10-24 — second-opinion deep review (stage 5)

Ref `a89a22e` (range `0790950..a89a22e`; `4743b0e` merges `origin/main`, docs and plans only; `12c4828` merges `fix/stdout-flush-fail-fast`, made from another session). Two reviewer sessions: the first read the code, probed, and ran a mutant sample, then stopped; this one reused those results and finished. Decisions loaded and checked in the code: `plan18-human-facet-line` (`write_facet_line_end`: N = total − shown; key only; `output_text_human`), `plan18-facets-independent` (blank query still gets the empty-facet envelope), `plan18-warning-wired-stage3` (the warning prints after the output), `plan18c-mask-c1` (facet lines mask C1). Every earlier review commit (`0790950`, `54f1047`, `17f01e6`, `a3f27d6`, `e38eca8`, …) is an ancestor of the ref.

**Verdict: stage 5 is done. No open blocking item.** Four grills, answered and filed.

Findings:

- **important (fixed): `list` accepting `--facet-limit` had no test.** The coder probed `list --facet-limit 5` → `unknown option` and wrote it in the notes, but no test held it. Mutant Y7 (the shared `list_handle_opt` takes `--facet-limit` and drops it) survived the suite. Added `test_get_list_delete.c` `list_rejects_facet_limit` (exit 1, empty stdout, stderr exactly `remember: unknown option '--facet-limit'`); Y7 now fails it.
- **important (fixed, round 13.2): `cmd_list`'s "failed to write output" had no test.** The notes filed it as "pre-existing, left for a list change", but a user reaches it (`remember list > /dev/full`), and the search twin of the line got a test this stage. Added `test_errors.c` `list_reports_a_failed_entry_line` (rejecting stdout → exit 1, one `remember: failed to write output`). Mutant Z3 (the message dropped, the failure kept) fails it. Dropping the whole check instead is equivalent through `remember_run`'s final `ferror` check (the coder's C7 argument).
- **grill → fixed (`decision:size-option-digits-only`, asked twice).** `util_parse_nonneg` accepted a leading space and `+` (`strtoull`), so `--limit ' 5'`, `--offset +5`, `--busy-timeout ' 9'` and `--facet-limit ' 5'` parsed while `'5 '` did not. The second ask named plan 16's "kept for 0.2.0 compatibility"; Itay chose digits only. The guard is now `isdigit((unsigned char)raw[0])`, which also stops `strtoull` from skipping space or taking a sign, so the old "minus anywhere" check and the `end == raw` clause (unreachable after a leading digit) are gone. Tests: `util_parse_nonneg_rejects_every_sign` (`+5`, ` 7`, `\t7`, `7 ` rejected; `007` accepted), `cli_busy_timeout_out_of_range_is_usage` (` 9`), `union_search_facet_limit_out_of_range_is_usage` (` 5`, `+5`). The only callers are those four argv flags; no stored value goes through the parser. Stage 6 adds a What's New line (a leading space or `+` in a number option is now usage).
- **grill (answered): keep `12c4828`** (`decision:plan18-keep-stdout-flush-merge`). The flush fix ships with plan 18. Its review: `search`, `list`, `--json get` on a closed pipe exit 1 `failed to write output`; a search with no output stays 0.
- **grill (answered): merge `fix/harness-fail-loud`** (`95346f8`, tests only; `decision:plan18-merge-harness-fail-loud`). Merged in the fix tree before the gate, so the gate below covers it.
- **grill (answered): push after a green gate** (`decision:plan18-push-after-green-gate`). Docker networking had recovered: `apt-get update && apt-cache show llvm` in `ubuntu:24.04` passed, and both gates ran `ci-linux.sh` unmodified.
- **nit (fixed, simplify):** `output_search_envelope`'s two guard `if`s are one (`!page_args_ok(...) || !facets_ok(res)`), −3 lines, the same shape as `output_search_facets_human`'s guard.
- **praise:** one size-option parser and one envelope head, tail and guard for list and search; the plain-lead table keeps E2.3's words in `diag.c`; the coder grilled the human line before writing it, reported the foreign merge instead of hiding it, and backed C12 with an 800-run differential.

Verification by running (the fix-tree ASan `remember`, copied to the scratchpad):

- Facets: 13 keyed matches; `search boo --facet-limit 3 --limit 1` prints three keys then `, … and 10 more` (3 + 10 = total 13); without `--facet-limit`, ten then `, … and 3 more`; JSON `keys.total` 13 with 3 items; `--limit 1` leaves both facets unchanged.
- Hostile input: blank and `'   '` exit 0, no output; `--json '   ' --facet-limit 1` the empty envelope; `boo\n` matches (the store trims); `\xff` usage (D27); 300 bytes and 10,000 tokens exit 0 (0.39 s); ESC in the query → the warning masks it; `--db /nonexistent/x.db --no-create` → `cannot open`, exit 1; `--facet-limit` missing / repeated (last wins) / after `--` (searched as text).
- Hostile stored row (`sqlite3` on a copy; the key's bytes checked with `hex(key)` first, round 14.2): `boo:evil` ESC `[2J` U+009B `x` prints `keys: boo:evil?[2J?x`.
- Grammar fuzz (round 13.3 style): 1,000 queries built from FTS5 pieces (column filters, `{cols}:`, `-col:`, `NEAR`, `^`, `*`, quotes, parentheses, AND/OR/NOT, Hebrew, emoji, edge whitespace), each with a random `--facet-limit`, plain and `--json`, on that hostile brain: 2,000 runs, all exit 0, no sanitizer report, every JSON object parses with the E2.6 member order and `items ≤ min(facet-limit, total)`, and no control byte or C1 in human stdout.
- Size delta re-run (Release, `/usr/bin/clang`, clean worktrees of `12c4828` and `a89a22e`): `remember` 1,904,152 → 1,904,392 B (+240), `libremember_core.a` 285,760 → 290,272 B (+4,512), the coder's numbers.

Mutants (one clause each, ASan, `remember_store_tests` plus the CLI suites; restored in a `finally`, each rebuilt):

| Mutant | Result |
|---|---|
| Coder sample O1 key raw, H3 `>=`, H4 keys N from count, C1 default 49, D1 raw prefix lost (first session) | killed by the tests the notes name |
| Y4 entries before the facets in the JSON envelope (first session) | killed (the envelope tests) |
| Y8 flush check skipped after a failed command (first session) | killed |
| Y7 `list` takes `--facet-limit` and drops it | survived → killed by `list_rejects_facet_limit` |
| Y16 human entries before the facet lines | killed (`union_search_human_prints_the_facets_before_entries`) |
| Y20 human cap from `total` instead of the items held | killed (ASan stack overflow in `test_output.c`) |
| Y21 search envelope skips `facets_ok` | killed (UBSan NULL member access in the guard test) |
| Z1 digit guard back to "first byte not `-`" | killed (4 tests) |
| Z2 digit guard back to "minus anywhere" | killed (3 tests) |
| Z3 `cmd_list` write failure without its message | survived → killed by `list_reports_a_failed_entry_line` |
| `end == raw` dropped after the digit guard | equivalent (a leading digit is always consumed); the clause is deleted |

Zero-hit lines read in the touched code (baseline profile): `cmd_query.c` the `--tag` push OOM, `load_page_neighbors` OOM and store failure, both `utc_now` failures (clock read), `cmd_list`'s store and neighbor failures (store fault injection only), and line 560 (now tested, above). `diag.c`: stderr write failures only. `util.c`: OOM and `getcwd` failures. `output.c`: none (678 sides, 0 missed).

Simplify pass: applied the guard merge and the dead `end == raw`. Considered and kept: one facet writer per format with a callback (the coder's decline holds: two typed loops read straight); `k_warning_plain_prefix` as a ternary on the one special code (the table states the rule per code and fails `every_warning_uses_the_warning_prefix` if a new code has no entry); `facet_shown`'s `<` vs `<=` (equivalent at 10, kept as written).

Coder rating: **8.5 / 10.** Strengths: a grill before the human line, 49 one-clause mutants with reds named and three equivalents argued (C12 by an 800-run differential), ROUND 14 applied (padded raw input, fixture steps asserted), hostile stored rows probed through both formats, the size delta exact, and the foreign merge reported, not hidden. Misses: a negative contract it probed (`list --facet-limit`) was left as a note instead of a test, so Y7 survived; `cmd_list`'s write-failure line was called pre-existing while its search twin got a test (round 13.2); the `+5` / leading-space asymmetry was reported but not grilled, though it changes what a new flag accepts.

Instructions for the coder:

1. A contract you prove by hand in the notes (a probe) gets a test in the same stage; a probe alone does not stop a regression.
2. When a new option goes through a parser that several commands share, test that each command that must not take it still rejects it.
3. When you test a failure path in one command, test the same path in every sibling command with the same shape.
4. When a new input inherits an odd rule from shared code (a parser that takes `+5` and ` 5` but not `5 `), grill the rule before the new input ships with it.

Gate (2026-10-24; clang, `REMEMBER_ENABLE_SANITIZERS=ON`, `REMEMBER_TEST_VFS=ON`, configured twice, cache flags checked): baseline `a89a22e` and the fix tree (`a89a22e` + the `95346f8` merge + these fixes) each ran build, ctest 5/5, `lint-all.sh` LINT OK, `check-coverage.sh` (functions and effective lines 100%; search_terms 176, unicode 80, normalize 268, output 678 branch sides, 0 missed), `leaks-macos` PASS, and `ci-linux.sh` unmodified (Docker `aarch64`, container apt healthy again: ctest 5/5 as an unprivileged user, iwyu PASS, scan-build PASS, LINT OK) → `GATE-ALL-GREEN` on both. ctest took 97 s and 80 s (machine load back to normal). This plan file was written while the fix-tree gate ran; nothing else changed after it (Markdown only). Standard filed: `standard:coding` ROUND 15.

Stage 6 inherits, in addition to the list above: a What's New line that a number option (`--limit`, `--offset`, `--busy-timeout`, `--facet-limit`) now takes ASCII digits only (`decision:size-option-digits-only`).

### 2026-10-24 — in-session deep review (stage 6)

Lenses: `pref:review-lenses` 1–9, comprehensive, ponytail, `docs/STEP_REVIEW_CHECKLIST.md`. Bugbot is not installed. ROUND 15 applied: the one hand-probed contract left (`search -x` without `--`) got a test; the help and docs claims were each run before they were written.

- **important, fixed.** Help still said "FTS5 full-text query" and "Ranked by FTS relevance (bm25), then updated_at", and SKILL.md said an empty query exits 1: all false since stages 3–5. Rewritten and pinned by `union_search_help_describes_prefixes_facets_and_the_warning` (mutants S1–S4).
- **important, fixed (grilled).** Two green suites were outside the gate; `search_unbalanced_quote_warns` (must-pass E2.1 (5)) ran in no ctest suite (`decision:plan18-gate-all-green-suites`).
- **nit, fixed.** A paragraph in `docs/QUALITY.md` split the gate table; `tests/COVERAGE.md` had no rows for the plan 18 unit files.
- **open for the FINAL review.** The branch gate in GitHub Actions `linux-coverage` needs a push; the INDEX row stays unticked per main's INDEX text.

Pragmatic write gate: DRY PASS (help, SKILL.md and README state each fact once per audience; the README warning line and the help line are the same bytes as `diag.c`). Easy-to-replace PASS (no code shape changed). Orthogonal PASS (help strings only in `src`; `list` help untouched).

### 2026-10-24 — second-opinion deep review (stage 6 | FINAL)

**Verdict:** approve. No code change. No grill. Do not start plan 19. INDEX stays unticked until Itay merges.

**Ran, not read.** Clean worktree of `e8776e2` (not the coder checkout). `cmake` with `-DCMAKE_C_COMPILER=/usr/bin/clang`; `ctest -N` lists 5; `REMEMBER_TEST_VFS=ON`. Bugbot is not installed. Docker DNS resolved `ports.ubuntu.com`, so `ci-linux.sh` ran unmodified.

Stage 6 claims, on `build/remember`:

- `help search` contains each new line the stage 6 test pins. `Ranked by FTS relevance` and `FTS5 full-text query` are gone. The global-options line is 99 columns; that line is older than this stage.
- README What's New starts with the E2.8 sentence. The next line is the warning, byte for byte: `remember: warning: raw query ignored: no such column: x` and `{"version":1,"warning":{"code":"raw_query_ignored","message":"fts5: syntax error near \"\""}}`.
- `search --json boo` member order is `version`, `offset`, `limit`, `count`, `total`, `keys`, `tags`, `entries`. A key item is `key`, `sync_id`, `label`, `source`, `updated_at`. `label` for `# Book recommendations` is `Book recommendations`. `list --json` has no `keys` or `tags`.
- Blank and whitespace queries exit 0 with no human output. `--json search '   '` is the empty envelope, facets included, totals 0.
- `search boo`, `search editor`, `search fam`, `search edit`, and `search 'book OR zed'` (3 rows, empty stderr) match the SKILL examples. `search -- -x` exits 0 with the warning. `search -x` is `unknown option '-x'`.
- `+5` and a leading space are usage on `--limit`, `--offset`, `--facet-limit`, and `--busy-timeout`. `--facet-limit 0` and `201` are `invalid --facet-limit (must be 1..200)`. `list`, `get`, `add`, `update`, and `tags` reject `--facet-limit`. `search --sort created boo` exits 0. `search --sort nope` is `invalid --sort`.
- A closed pipe exits 1 with `remember: failed to write output`.
- A newline inside the query stays one stderr line. ESC and DEL in the query become `?` inside that line. A 300-byte column name is one line (358 bytes). Invalid UTF-8 is `invalid UTF-8 in search query`.
- A deleted `pref:editor` is absent from live `search edit` and present with `--deleted`. Query `(` returns key `(draft)` and no entries.
- A stored key with ESC and UTF-8 U+009B prints one `?` for each (`hex` of stdout: `3f` for both). JSON is unchanged by that mask.
- Design log 006: stage 6 commit `e8776e2` only, `git show --numstat` is 16 added, 0 removed. No earlier plan 18 commit touches it.
- `tests/test_main.c` registers 28 suites. `tests/gate-suites` lists those 28, including `key` and `verification_edges`. Configure printed that list. `ci-linux` ran 815 tests, 0 failed, and `remember_store_tests` 455/0.

**Safety.** The MATCH text is bound (`?2`), not pasted into SQL. P tokens are split on `"`, so a token cannot break the prefix quotes. A warning that echoes the query stays one line. A hand-edited key does not hide another live match.

**Simplicity.** Nothing to cut.

- `write_keys_human` and `write_tags_human` could share a callback (about 15 lines). The two item shapes would get harder to read. Kept, as stage 5 did.
- `is_c1_control_at` as one `return` saves 3 lines. The `if` makes the two-byte read obvious. Kept.
- The new help lines are already one fact each.

**Pragmatic write-gate:** DRY PASS. Easy-to-replace PASS (utf8proc stays behind `unicode.c`; the lint boundary passed). Orthogonal PASS (list output bytes unchanged; `list --json` has no facets).

**Grades.** Coding agent 9/10. Stage reviewers 9/10. No pointer beyond rounds 9–15, which already name the holes those reviews fixed (the store trim, facets against the lowercased query, `list` accepting `--facet-limit`, digits-only). Stage 6's claims matched the binary.

**Gate** on this tree, before this note (Markdown only after it): ctest 5/5 in 101 s, `lint-all.sh` LINT OK, `check-coverage.sh` functions and effective lines 100%, raw lines 85.88% (the QUALITY.md figure), branch sides `search_terms.c` 176, `unicode.c` 80, `normalize.c` 268, `output.c` 678, 0 missed, `leaks-macos` PASS, `ci-linux.sh` OK (iwyu PASS, scan-build PASS, 815/0 and 455/0). GitHub Actions `linux (line coverage on src/)` on PR #37 passed with the same four counts and 0 missed (raw lines there 85.92%).

### 2026-10-24 — CodeQL follow-up on the review head

**Verdict:** approve, after one fix. No grill. Grades stay 9/10 and 9/10. No new `standard:coding` round.

CodeQL on PR #37 (head `ec89d37`) reported one high alert: `search_prefix_part` added `snprintf`'s return to `pos`. That return is the length the call would have written, so a short write wraps the next size. `search_tokens_build` keeps the count at 16 and each token at 256 bytes, so the write does not happen on a real query. The function now returns -1 when the count is above 16 or the write is short or failed. `store_search` folds that into the existing internal-error arm with `||`, which does not read the token buffer when the build left it untouched. A successful query still produces the same prefix bytes.

**Gate** on this tree: ctest 5/5 in 75 s (`/usr/bin/clang`, `ctest -N` lists 5, `REMEMBER_TEST_VFS=ON`), `lint-all.sh` LINT OK, `leaks-macos` PASS, `check-coverage.sh` functions and effective lines 100%, raw lines 85.86%, branch sides 176/80/268/678, 0 missed. `build-cov` is a fresh cache: Homebrew clang, `REMEMBER_TEST_VFS=ON`. `ci-linux.sh` OK unmodified (DNS resolved `ports.ubuntu.com`; iwyu PASS, scan-build PASS, 815/0 and 455/0). `docs/QUALITY.md` raw-line cell is 85.86% to match this run.

Pointer, not filed: when the next `snprintf` size is `cap - pos` and `pos` grows by a previous `snprintf` return, check that return before adding it. Rounds 9–15 do not say this.

### 2026-10-24 — simplicity round and review lenses (whole plan)

The FINAL note above named simplicity in one paragraph and the pragmatic gate in one line. This is those two passes, run on `4cb65c8` (the snprintf fix included). No code change. No grill. Grades stay 9/10 and 9/10.

**(S) Simplicity.** net 0 lines. Lean already.

1. `write_keys_human` and `write_tags_human` (`output.c`). The repeated part is the empty return, the label, the comma, and the line end, about 12 lines. Sketch: one writer plus an item callback. The tag line prints ` (n)` and the key line does not. The callback is longer than the two loops and hides the two shapes. Kept.
2. `is_c1_control_at`. Sketch: one `return` of the lead-byte test, −3 lines. Stage 2 hit `readability-implicit-bool-conversion` on that shape in `is_ascii_alnum`. Kept.
3. `search_prefix_part`. Sketch: drop the count check and add `snprintf`'s return straight into `pos`, −8 lines. That is the wrap CodeQL reported. Kept.
4. `search_build_where` beside the list WHERE builder. The search function already calls `list_append_filters` and `list_append_bin`. The rest is the MATCH head and binds `?1` / `?2`. Folding the heads together would hide that slot split. Kept.
5. The new help lines are already one fact each. 0 lines.

**(1) Clean Code.** Pass. `search_prefix_part` names why the return is checked. `store_search` is the long function. The form that clang-tidy rejected was cognitive complexity 26, limit 25. The `||` form is the one that passed on this head. Pulling the transaction into another function is a larger move, not a local shrink.

**(2) SOLID.** Pass. `search_terms.c` stays free of SQL and I/O. The store adapter owns the MATCH. utf8proc stays behind `unicode.c`.

**(3) DRY.** Pass. Entry filters have one implementation, `list_append_filters`. The warning text has one implementation, `diag.c`. The two human facet writers are candidate 1. No third copy.

**(4) Pragmatic write-gate.** DRY pass. Easy-to-replace pass: `sqlite3.h` stays in `store_sqlite.c` and `test_vfs.c`, utf8proc stays in `unicode.c`, and the lint boundary passed on this head. Orthogonal pass: `list` JSON has no `keys` or `tags`, and `list` help has no `--facet-limit`.

**(5) C11.** Pass. The room is a `size_t`. A negative `snprintf` returns before that value is cast and added.

**(6) Fail fast.** Pass. One site considered: `search_match` ignores its `snprintf` return. The caller sizes the buffer as `sizeof(p)` plus the raw length plus the `() OR ()` frame, which holds the whole text, so the call does not truncate on a real query. A check would add a decision to `store_search` on top of the form that just passed the complexity limit. No user-facing silent failure.

**(7) Resource ownership.** Pass. `store_search` frees the query copy, the lowercased query, and the MATCH buffer on every exit. A prefix failure returns before `BEGIN`. The warning allocation is freed with the result.

**(8) Tests and coverage.** Pass. The union-search suite runs the success prefix (`boo`, `editor`, the warning path). The new `-1` returns are defensive exits. This head's coverage gate was 100% functions, 100% effective lines, and 0 missed branch sides in the four pure TUs. No new contract.

**(9) Simplicity.** The round above. No auto-fix.

**(10) Trust claims.** Pass. A prefix token cannot break its quotes: `splits_token` drops every ASCII byte outside `[A-Za-z0-9]`, including `"` and `\`, before the quotes are added. The MATCH text is bound as `?2`. `is_c1_control_at` reads the second byte only after the lead byte matches, and a string that ends on that lead has NUL there, so the range check fails. A hand-edited key is masked on the human path. JSON is left as stored, which is the locked rule.

No Critical. No open Improvement. Bugbot is not installed. Ponytail: lean already.
