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

- [ ] Design log 006 unchanged except Implementation Results rows for E2.*,
      E3.*, E10.1 (and the utf8proc version used).
- [ ] Every must-pass row green and in `tests/gate-suites`.
- [ ] D1, D2, D12, D13, D27, D28, P2 implemented exactly.
- [ ] utf8proc vendored per the full checklist; IWYU run green.
- [ ] No search input exits non-zero except real E4 errors and usage flags.
- [ ] Help, SKILL.md, README What's New (E2.8 verbatim + the E2.6 order
      exception + blank search now succeeds).
- [ ] Engineering notes: `store_search` and CLI search bullets rewritten.
- [ ] Gate: ctest, lint, IWYU, coverage 100/100, `ci-linux.sh` all green.
- [ ] Second review done; no open **blocking** item.
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

_(empty; fill per stage)_

## Review Notes

_(empty; second review appends here)_
