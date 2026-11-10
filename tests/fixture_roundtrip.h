#ifndef REMEMBER_FIXTURE_ROUNDTRIP_H
#define REMEMBER_FIXTURE_ROUNDTRIP_H

/*
 * The export/import round-trip brain (plan 22, reused by plan 23). Built
 * through the test CLI with REMEMBER_TEST_FIXED_IDS, so every id is known and
 * the clock stands at 2026-01-01T00:00:00.000Z. Needs a remember built with
 * REMEMBER_TEST_VFS (the gate configures it ON).
 *
 * Memories (local id, id suffix, bin):
 *   1 ...003 live    key project:alpha, tags alpha beta zeta, updated by
 *                    agent bot-1, a two-device version vector stored unsorted
 *   2 ...010 live    keyless "# Weekly Notes " heading, tags a b
 *   3 ...020 live    keyless, more than six words
 *   4 ...030 expired key old:thing
 *   5 ...040 deleted key gone
 * Live links: 1 cites 2, 1 related 4, 1 supersedes 5, 2 related 3. A
 * tombstoned 1 related 3 link exists and must not export.
 */
void fixture_roundtrip_build(const char *db);

/* "00000000-0000-7000-8000-" + 12 hex: the fixed id prefix. */
#define FIXTURE_ID(hex12) "00000000-0000-7000-8000-" hex12
#define FIXTURE_AT "2026-01-01T00:00:00.000Z"

#endif /* REMEMBER_FIXTURE_ROUNDTRIP_H */
