# 002 - Core façade: in-process entrypoint for a native GUI

**Status:** Implemented (step 11) — 2026-07-30
**Author:** Itay (solo) + agent
**Created:** 2026-07-30
**Depends on:** [`001-foundations`](001-foundations.md) (CLI surface, JSON contract, exit codes)

## Background

The CLI is complete through step 10 (`add`, `get`, `list`, `search`, `update`,
`delete`, skill, install). The next product stage is a **native macOS app**
(SwiftUI) that adds OS integration (browse window, menu-bar item, Spotlight,
global quick-capture) while the CLI stays the brain. The app lives in a
**separate private repo** (`remember-mac`); this repo stays the open-source CLI
source of truth and is consumed there as a git submodule.

The app targets the **Mac App Store**, which means the **App Sandbox**. A
sandboxed app can only exec binaries inside its own bundle and the child
inherits the sandbox, so it **cannot spawn** the `remember` process to reach the
shared `~/.remember/remember.db`. Therefore the app must reuse the brain as a
**linked library**, not a spawned process, and reach the shared DB via a
security-scoped bookmark to `~/.remember/`. (Full app design lives in the
`remember-mac` repo; this log covers only what this repo must expose.)

## Problem

How does a linked, in-process caller reuse the CLI's exact behavior — including
its **byte-for-byte `--json` output** — without forking a second orchestration
path that can drift from the CLI (violating our DRY-reuse-first rule)?

## Decision

Expose the CLI pipeline as a single in-process entrypoint with **injectable
output/error streams** — not a new per-command struct API.

```c
/* remember_app.h — the one public entrypoint the GUI links. */
#include <stdio.h>
/* Run one remember invocation in-process. argv is the same token vector the CLI
 * receives (WITHOUT argv[0]-as-program assumptions the caller must respect —
 * see contract). Writes normal output to `out`, diagnostics to `err`, and
 * returns the stable process exit code (0 ok / 1 usage|error / 2 not found). */
int remember_run(int argc, const char *const argv[], FILE *out, FILE *err);
```

The GUI calls it with two `open_memstream` buffers and an argv it builds itself,
e.g. `["--db", "/Users/x/.remember/remember.db", "--json", "add",
"--source", "human", "--tag", "idea", "body text"]`. On return it reads the
`out` buffer (the identical JSON envelope the CLI prints) and maps the exit code.
Errors stay plain text on `err` + exit code, exactly like the CLI.

### Why this shape (not a `rc_add`/`rc_list`/… struct API)

- **Byte-identical by construction.** It *is* the CLI pipeline (`cli_parse` →
  `run` → command → `output_*`), so its JSON cannot drift from the CLI's. A
  parallel struct API would re-implement normalize/paging/merge orchestration —
  a second site of the same knowledge (our `arch:dry-reuse-first` calls 2+ sites
  an Improvement, ignoring an existing path Critical).
- **Least new code.** `output_*` already take a `FILE *`; the store already has a
  path-based open via the `--db` global. The only missing capability is making
  the *few* hardcoded `stdout`/`stderr` writes injectable. No new structs, no
  new normalize glue.
- **Thin wrapper, real brain in the CLI** — the app's stated architecture.
- **DB sharing is automatic:** the GUI passes `--db <bookmarked path>`; the CLI
  resolves the same file by default. Same brain, same file, same output.

Ergonomics cost on the Swift side (building argv arrays) is a thin, well-typed
helper per operation — cheaper than maintaining a second C API surface.

## What this repo must add/change

1. **Injectable streams.** Introduce a process-global `out`/`err` `FILE *` pair
   (default `stdout`/`stderr`) that command/help/error code paths write through
   instead of hardcoding `stdout`/`stderr`. `remember_run` swaps them for the
   caller's streams around one invocation and restores after.
   - `ponytail:` process-global streams — the CLI is single-threaded
     (`SQLITE_THREADSAFE=0`) and the GUI serializes calls through an actor.
     Thread a `FILE *` through the command signatures only if concurrent
     in-process callers ever appear.
   - Call sites to convert: `main.c` help/version/parse-error printers,
     `err_msg` (`commands_common.c`), `emit_entry_page` + scattered
     `fprintf(stderr, …)` in `cmd_*.c`, and the `output_*` call sites that pass
     `stdout`. `output.c` itself is unchanged (already `FILE *`-parameterized).
2. **Extract `remember_run`.** Move `run()` + its helpers out of `main.c` into a
   new TU `remember_app.c` (public `remember_app.h`). `main()` becomes: set
   streams to `stdout`/`stderr`, `cli_parse`, `remember_run`-equivalent, free.
   The GUI links `remember_app.c` + the existing `REMEMBER_LIB_SOURCES`.
3. **New `tags` command** (needed by the GUI tag chips + add-form autocomplete):
   - `store_tags(Store *, TagCount **out, size_t *out_n)` in `store.h` /
     `store_sqlite.c` — distinct tag names with entry counts
     (`SELECT name, COUNT(*) … GROUP BY name ORDER BY name`).
   - `output_tags_envelope(FILE *, …)` — `{"version":1,"count":N,"tags":[{"name":…,"count":…}]}`.
   - `cmd_tags` + `CLI_CMD_TAGS` registry/help/dispatch, so `remember tags`
     (and `remember --json tags`) exist on the CLI surface too.
4. **`libremember` static lib target** in CMake so the GUI (and future
   consumers) can link the core without `main`. Wire the new TUs into
   `REMEMBER_LIB_SOURCES` **and** the `-Wcast-qual` source list (engineering
   note rule 3), and keep the invariant: **only `store_sqlite.c` includes
   sqlite**.

## Non-goals

- No stdin injection into the façade — the GUI passes bodies directly as argv
  tokens (never `-`). Add an input stream only if a real need appears (YAGNI).
- No change to the JSON schema, exit codes, or command semantics — this is a
  reuse/packaging change, not a behavior change. The existing black-box suite is
  the guardrail that behavior did **not** change.
- No app/Swift concerns here — those live in `remember-mac`.

## Invariants preserved

- Only `store_sqlite.c` includes SQLite; façade talks `store.h`/`output.h`.
- `output.c` owns all JSON escaping; the façade emits through it (no second
  serializer).
- Stable exit codes 0/1/2; `remember_run` returns them unchanged.
- CLI user strings stay ASCII (`style:user-strings-ascii`).

## Open questions (resolve in review)

- **Q: `remember_run` vs a typed `rc_*` struct API?** Proposed: `remember_run`
  (above). Alternative kept on record only if argv-marshaling proves too awkward
  for Swift in practice.
- **Q: global streams vs threading `FILE *` through signatures?** Proposed:
  global pair with the ponytail marker; revisit only under real concurrency.
- **Q: `tags` output — counts included?** Proposed yes (cheap, and the filter UI
  can show counts). Drop to names-only if it complicates the store query.

## Implementation Notes

- Landed on `remember_run` exactly as proposed (user chose it over `rc_*` — "saves
  us from exposing each new API"). New TUs: `appio.{c,h}` (stream pair),
  `remember_app.{c,h}` (moved `run()` + helpers out of `main.c`; `main.c` is now
  3 lines → `remember_main`), `cmd_tags.c`. New store op `store_tags` +
  `output_tags_*`. CLI gained `CLI_CMD_TAGS` + registry/help.
- **Open questions resolved:** (1) `remember_run` over `rc_*` — done. (2) global
  streams over threaded `FILE*` — done, `ponytail:` marker in `appio.h`. (3) `tags`
  includes counts — done.
- **Deviation from the sketch:** added `remember_main(argc, argv)` (not in the
  original `remember_app.h` sketch) so `main.c` needs no `<stdio.h>` — a Linux
  clang-tidy `misc-include-cleaner` version flags `<stdio.h>` as "not used
  directly" for a `main` that only touches `stdout`/`stderr`. macOS tidy did not.
- **Store-tags counts semantics:** INNER JOIN `tags`↔`entry_tags`, so a tag with
  zero entries never appears — which is what we want (orphans are GC'd anyway).
- Full gate green incl. new `facade` (11) + `tags` (5) suites; ASan/UBSan store
  gate incl. `store_tags` OOM/prepare/step/realloc fault paths; lint + IWYU +
  scan-build clean on macOS and Linux (`ci-linux.sh`); coverage 100% functions +
  100% effective lines.

## Review Notes

- Self-review (comprehensive + ponytail + DRY), findings addressed:
  - misc-include-cleaner (direct `<stdio.h>` in `appio.c`, `<stddef.h>` in
    `cmd_tags.c`); Linux-only `main.c` `<stdio.h>` (→ `remember_main`).
  - clang-analyzer null-deref in a new store test → collect-all guard + early
    return (the documented pattern).
  - coverage: two `store_tags` cleanup lines (realloc-fail, step-error) added as
    deterministic fault tests rather than left defensive.
- Verified empirically (not by reading): byte-parity asserted for
  list/search/get/tags/help/errors; timestamp-masked parity for add/update/delete;
  `tags` counts/sort/GC checked against a live DB.
- No behavior change to the CLI: all 314 pre-existing black-box tests still green,
  which is the guardrail that the stream refactor is transparent.
