#include "fixture_roundtrip.h"

#include "harness.h"
#include "test.h"

#include <stdlib.h>

/* One remember run whose ids count up from start (decimal) on a stopped clock. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) db, then the first id
static void fixed_run(const char *db, const char *start, const char *const *args, size_t nargs)
{
    CmdResult r;

    // NOLINTBEGIN(concurrency-mt-unsafe) the child inherits this process-global
    (void)setenv("REMEMBER_TEST_FIXED_IDS", start, 1);
    r = run_remember(db, args, nargs, NULL);
    (void)unsetenv("REMEMBER_TEST_FIXED_IDS");
    // NOLINTEND(concurrency-mt-unsafe)
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
}

#define FIXED_RUN(db, start, ...)                                                                  \
    do {                                                                                           \
        const char *const args_[] = {__VA_ARGS__};                                                 \
        fixed_run((db), (start), args_, sizeof args_ / sizeof args_[0]);                           \
    } while (0)

static void sql(const char *db, const char *stmt)
{
    free(harness_sqlite_query_line(db, stmt));
}

void fixture_roundtrip_build(const char *db)
{
    FIXED_RUN(db, "1", "add", "--key", "project:alpha", "--tag", "zeta", "--tag", "alpha",
              "Alpha \"quoted\", body\nline two");
    FIXED_RUN(db, "16", "add", "--tag", "b", "--tag", "a", "# Weekly Notes \nBody of the week.");
    FIXED_RUN(db, "32", "add", "one two  three\tfour five six seven eight");
    FIXED_RUN(db, "48", "add", "--key", "old:thing", "Expired memory.");
    FIXED_RUN(db, "64", "add", "--key", "gone", "Deleted memory.");
    FIXED_RUN(db, "80", "delete", "--key", "gone");
    FIXED_RUN(db, "96", "link", "--from-key", "project:alpha", "--to", "2", "--kind", "cites");
    FIXED_RUN(db, "112", "link", "3", "2");
    FIXED_RUN(db, "128", "link", "--from-key", "project:alpha", "--to-key", "gone", "--kind",
              "supersedes", "--deleted");
    FIXED_RUN(db, "144", "link", "--from-key", "project:alpha", "--to-key", "old:thing");
    FIXED_RUN(db, "160", "link", "1", "3");
    FIXED_RUN(db, "176", "unlink", "1", "3");
    FIXED_RUN(db, "192", "update", "--key", "project:alpha", "--as", "agent", "--actor", "bot-1",
              "--tag", "zeta", "--tag", "alpha", "--tag", "beta");
    /* Time passes: old:thing expired before the frozen clock. A peer's merge
       left the vector's keys out of order. */
    sql(db, "UPDATE entries SET expires_at = '2025-06-01T00:00:00.000Z' WHERE key = 'old:thing';");
    sql(db, "UPDATE entries SET version_vector = "
            "'{\"00000000-0000-7000-8000-0000000000ff\":2,"
            "\"00000000-0000-7000-8000-000000000001\":5}' WHERE key = 'project:alpha';");
}

void fixture_roundtrip_add_tombstone(const char *db)
{
    char *got = NULL;

    sql(db, "INSERT INTO settings (key, value, updated_by, updated_at, device_id, "
            "version_vector, deleted_at, change_seq) VALUES ("
            "'remember.search.candidate_pool', '400', "
            "'{\"kind\":\"human\",\"id\":null}', "
            "'2026-10-24T09:12:00.000Z', "
            "'01a0d90a-e71c-7a0a-bb2c-3b377482fa51', "
            "'{\"01a0d90a-e71c-7a0a-bb2c-3b377482fa51\":3}', "
            "'2026-10-24T09:12:00.000Z', 1);");
    got = harness_sqlite_query_line(
        db, "SELECT deleted_at FROM settings WHERE key = 'remember.search.candidate_pool';");
    ASSERT_STREQ(got, "2026-10-24T09:12:00.000Z");
    free(got);
}
