#include "fixture_roundtrip.h"
#include "harness.h"
#include "register.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Plan 23 import formats (suite import_formats). Stage 2: memories through
 * the shared merge, no settings, no links yet.
 */

enum { PATH_BUF = 1024, TEXT_BUF = 2048 };

static CmdResult fixed_import(const char *db, const char *const *args, size_t nargs)
{
    CmdResult r;

    // NOLINTBEGIN(concurrency-mt-unsafe) the child inherits this process-global
    (void)setenv("REMEMBER_TEST_FIXED_IDS", "5000", 1);
    r = run_remember(db, args, nargs, NULL);
    (void)unsetenv("REMEMBER_TEST_FIXED_IDS");
    // NOLINTEND(concurrency-mt-unsafe)
    return r;
}

static int write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    size_t n = 0;

    if (f == NULL) {
        return -1;
    }
    n = strlen(text);
    if (fwrite(text, 1, n, f) != n || fclose(f) != 0) {
        return -1;
    }
    return 0;
}

static char *open_empty_brain(void)
{
    char *dst = make_temp_db_path();
    CmdResult open;

    ASSERT_TRUE(dst != NULL);
    {
        const char *args[] = {"device"};
        open = run_remember(dst, args, 1, NULL);
    }
    ASSERT_EQ_INT(open.exit_code, 0);
    cmd_result_free(&open);
    return dst;
}

/* One keyed memory export document (version 1) with a known id. */
static int write_one_memory_json(const char *path)
{
    char buf[TEXT_BUF];
    int n = snprintf(
        buf, sizeof(buf),
        "{\"format\":\"remember-export\",\"version\":1,\"brain_id\":\"%s\","
        "\"exported_at\":\"%s\",\"engine\":\"0.2.0\",\"schema\":6,"
        "\"bins\":{\"live\":1},\"entries\":[{"
        "\"id\":\"%s\",\"key\":\"project:alpha\",\"body\":\"Alpha body\","
        "\"tags\":[\"zeta\",\"alpha\"],\"created_by\":{\"kind\":\"human\",\"id\":null},"
        "\"updated_by\":{\"kind\":\"agent\",\"id\":\"bot-1\"},"
        "\"created_at\":\"%s\",\"updated_at\":\"%s\","
        "\"expires_at\":null,\"deleted_at\":null,\"bin\":\"live\","
        "\"version_vector\":{\"%s\":1}}],\"links\":[]}\n",
        FIXTURE_ID("000000000002"), FIXTURE_AT, FIXTURE_ID("000000000003"), FIXTURE_AT,
        FIXTURE_AT, FIXTURE_ID("000000000001"));

    if (n < 0 || (size_t)n >= sizeof(buf)) {
        return -1;
    }
    return write_text(path, buf);
}

TEST(import_json_inserts_a_keyed_memory_into_an_empty_brain)
{
    char *dst = open_empty_brain();
    char *src = NULL;
    char *got = NULL;
    CmdResult r;

    src = harness_side_path(dst, "-in.json");
    ASSERT_EQ_INT(write_one_memory_json(src), 0);
    {
        const char *args[] = {"--json", "import", "--from-json", src};
        r = fixed_import(dst, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "\"action\":\"imported\"");
    ASSERT_STR_CONTAINS(r.out, "\"inserted\":1");
    got = harness_sqlite_query_line(
        dst, "SELECT key || '|' || body || '|' || updated_by FROM entries WHERE id = '" FIXTURE_ID(
                 "000000000003") "';");
    ASSERT_STREQ(got, "project:alpha|Alpha body|{\"kind\":\"import\",\"id\":null}");
    free(got);
    got = harness_sqlite_query_line(dst, "SELECT created_by FROM entries WHERE id = '" FIXTURE_ID(
                                             "000000000003") "';");
    ASSERT_STREQ(got, "{\"kind\":\"human\",\"id\":null}");
    free(got);
    got = harness_sqlite_query_line(dst, "SELECT brain_id FROM brain_meta;");
    ASSERT_STREQ(got, FIXTURE_ID("000000000002"));
    free(got);
    cmd_result_free(&r);
    free(src);
    free(dst);
}

TEST(import_json_keeps_brain_id_when_the_target_is_not_empty)
{
    char *dst = open_empty_brain();
    char *src = NULL;
    char *before = NULL;
    char *after = NULL;
    CmdResult r;
    CmdResult add;

    before = harness_sqlite_query_line(dst, "SELECT brain_id FROM brain_meta;");
    ASSERT_TRUE(before != NULL && before[0] != '\0');
    {
        const char *args[] = {"add", "seed", "--key", "seed:one"};
        add = run_remember(dst, args, 4, NULL);
    }
    ASSERT_EQ_INT(add.exit_code, 0);
    cmd_result_free(&add);
    src = harness_side_path(dst, "-in.json");
    ASSERT_EQ_INT(write_one_memory_json(src), 0);
    {
        const char *args[] = {"--json", "import", "--from-json", src};
        r = fixed_import(dst, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    after = harness_sqlite_query_line(dst, "SELECT brain_id FROM brain_meta;");
    ASSERT_STREQ(after, before);
    free(before);
    free(after);
    cmd_result_free(&r);
    free(src);
    free(dst);
}

TEST(import_json_key_clash_does_not_upsert)
{
    char *dst = open_empty_brain();
    char *src = NULL;
    char *got = NULL;
    CmdResult r;
    CmdResult add;

    {
        const char *args[] = {"add", "local body", "--key", "project:alpha"};
        add = run_remember(dst, args, 4, NULL);
    }
    ASSERT_EQ_INT(add.exit_code, 0);
    cmd_result_free(&add);
    src = harness_side_path(dst, "-in.json");
    ASSERT_EQ_INT(write_one_memory_json(src), 0);
    {
        const char *args[] = {"--json", "import", "--from-json", src};
        r = fixed_import(dst, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":{\"count\":1");
    ASSERT_STR_CONTAINS(r.out, "\"reason\":\"key_clash\"");
    got = harness_sqlite_query_line(dst, "SELECT body FROM entries WHERE key = 'project:alpha';");
    ASSERT_STREQ(got, "local body");
    free(got);
    got = harness_sqlite_query_line(dst, "SELECT COUNT(*) FROM entries;");
    ASSERT_STREQ(got, "1");
    free(got);
    cmd_result_free(&r);
    free(src);
    free(dst);
}

TEST(import_json_keyed_without_id_inserts_and_does_not_union)
{
    char *dst = open_empty_brain();
    char *src = NULL;
    char *got = NULL;
    char buf[TEXT_BUF];
    CmdResult r;
    CmdResult add;
    int n = 0;

    {
        const char *args[] = {"add", "Same body text", "--tag", "old"};
        add = run_remember(dst, args, 4, NULL);
    }
    ASSERT_EQ_INT(add.exit_code, 0);
    cmd_result_free(&add);
    n = snprintf(buf, sizeof(buf),
                 "{\"format\":\"remember-export\",\"version\":1,\"brain_id\":\"%s\","
                 "\"exported_at\":\"%s\",\"engine\":\"0.2.0\",\"schema\":6,"
                 "\"bins\":{\"live\":1},\"entries\":[{"
                 "\"key\":\"keyed:one\",\"body\":\"Same body text\",\"tags\":[\"new\"],"
                 "\"created_by\":{\"kind\":\"human\",\"id\":null},"
                 "\"created_at\":\"%s\",\"updated_at\":\"%s\","
                 "\"expires_at\":null,\"deleted_at\":null,\"bin\":\"live\","
                 "\"version_vector\":{}}],\"links\":[]}\n",
                 FIXTURE_ID("000000000002"), FIXTURE_AT, FIXTURE_AT, FIXTURE_AT);
    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(buf));
    src = harness_side_path(dst, "-in.json");
    ASSERT_EQ_INT(write_text(src, buf), 0);
    {
        const char *args[] = {"--json", "import", "--from-json", src};
        r = fixed_import(dst, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"inserted\":1");
    got = harness_sqlite_query_line(dst, "SELECT COUNT(*) FROM entries;");
    ASSERT_STREQ(got, "2");
    free(got);
    got = harness_sqlite_query_line(dst, "SELECT key FROM entries WHERE key = 'keyed:one';");
    ASSERT_STREQ(got, "keyed:one");
    free(got);
    got = harness_sqlite_query_line(
        dst,
        "SELECT group_concat(name, ',') FROM (SELECT t.name AS name FROM tags t "
        "JOIN entry_tags et ON et.tag_id = t.id JOIN entries e ON e.local_id = et.entry_id "
        "WHERE e.key IS NULL ORDER BY t.name);");
    ASSERT_STREQ(got, "old");
    free(got);
    cmd_result_free(&r);
    free(src);
    free(dst);
}

TEST(import_json_keyless_without_id_unions_tags)
{
    char *dst = open_empty_brain();
    char *src = NULL;
    char *got = NULL;
    char *id = NULL;
    CmdResult r;
    CmdResult add;
    char buf[TEXT_BUF];
    int n = 0;

    {
        const char *args[] = {"add", "Same body text", "--tag", "old"};
        add = run_remember(dst, args, 4, NULL);
    }
    ASSERT_EQ_INT(add.exit_code, 0);
    cmd_result_free(&add);
    id = harness_sqlite_query_line(dst, "SELECT id FROM entries WHERE key IS NULL;");
    ASSERT_TRUE(id != NULL && strlen(id) == 36U);
    n = snprintf(buf, sizeof(buf),
                 "{\"format\":\"remember-export\",\"version\":1,\"brain_id\":\"%s\","
                 "\"exported_at\":\"%s\",\"engine\":\"0.2.0\",\"schema\":6,"
                 "\"bins\":{\"live\":1},\"entries\":[{"
                 "\"body\":\"Same body text\",\"tags\":[\"new\",\"old\"],"
                 "\"created_by\":{\"kind\":\"human\",\"id\":null},"
                 "\"created_at\":\"%s\",\"updated_at\":\"%s\","
                 "\"expires_at\":null,\"deleted_at\":null,\"bin\":\"live\","
                 "\"version_vector\":{}}],\"links\":[]}\n",
                 FIXTURE_ID("000000000002"), FIXTURE_AT, FIXTURE_AT, FIXTURE_AT);
    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(buf));
    src = harness_side_path(dst, "-in.json");
    ASSERT_EQ_INT(write_text(src, buf), 0);
    {
        const char *args[] = {"--json", "import", "--from-json", src};
        r = fixed_import(dst, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"updated\":1");
    got = harness_sqlite_query_line(dst, "SELECT id FROM entries WHERE key IS NULL;");
    ASSERT_STREQ(got, id);
    free(got);
    got = harness_sqlite_query_line(
        dst,
        "SELECT group_concat(name, ',') FROM (SELECT t.name AS name FROM tags t "
        "JOIN entry_tags et ON et.tag_id = t.id JOIN entries e ON e.local_id = et.entry_id "
        "WHERE e.key IS NULL ORDER BY t.name);");
    ASSERT_STREQ(got, "new,old");
    free(got);
    got = harness_sqlite_query_line(dst, "SELECT updated_by FROM entries WHERE key IS NULL;");
    ASSERT_STREQ(got, "{\"kind\":\"import\",\"id\":null}");
    free(got);
    free(id);
    cmd_result_free(&r);
    free(src);
    free(dst);
}

TEST(import_json_keyless_identical_tags_count_unchanged)
{
    char *dst = open_empty_brain();
    char *src = NULL;
    char buf[TEXT_BUF];
    CmdResult r;
    CmdResult add;
    int n = 0;

    {
        const char *args[] = {"add", "Same body text", "--tag", "old"};
        add = run_remember(dst, args, 4, NULL);
    }
    ASSERT_EQ_INT(add.exit_code, 0);
    cmd_result_free(&add);
    n = snprintf(buf, sizeof(buf),
                 "{\"format\":\"remember-export\",\"version\":1,\"brain_id\":\"%s\","
                 "\"exported_at\":\"%s\",\"engine\":\"0.2.0\",\"schema\":6,"
                 "\"bins\":{\"live\":1},\"entries\":[{"
                 "\"body\":\"Same body text\",\"tags\":[\"old\"],"
                 "\"created_by\":{\"kind\":\"human\",\"id\":null},"
                 "\"created_at\":\"%s\",\"updated_at\":\"%s\","
                 "\"expires_at\":null,\"deleted_at\":null,\"bin\":\"live\","
                 "\"version_vector\":{}}],\"links\":[]}\n",
                 FIXTURE_ID("000000000002"), FIXTURE_AT, FIXTURE_AT, FIXTURE_AT);
    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(buf));
    src = harness_side_path(dst, "-in.json");
    ASSERT_EQ_INT(write_text(src, buf), 0);
    {
        const char *args[] = {"--json", "import", "--from-json", src};
        r = fixed_import(dst, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"unchanged\":1");
    cmd_result_free(&r);
    free(src);
    free(dst);
}

TEST(import_json_bad_record_commits_the_good_ones)
{
    char *dst = open_empty_brain();
    char *src = NULL;
    char *got = NULL;
    char buf[TEXT_BUF];
    CmdResult r;
    int n = 0;

    n = snprintf(
        buf, sizeof(buf),
        "{\"format\":\"remember-export\",\"version\":1,\"brain_id\":\"%s\","
        "\"exported_at\":\"%s\",\"engine\":\"0.2.0\",\"schema\":6,"
        "\"bins\":{\"live\":2},\"entries\":["
        "{\"id\":\"%s\",\"key\":\"good:one\",\"body\":\"ok\","
        "\"tags\":[],\"created_by\":{\"kind\":\"human\",\"id\":null},"
        "\"created_at\":\"%s\",\"updated_at\":\"%s\","
        "\"expires_at\":null,\"deleted_at\":null,\"bin\":\"live\","
        "\"version_vector\":{}},"
        "{\"id\":\"%s\",\"key\":\"bad:one\",\"body\":\"\","
        "\"tags\":[],\"created_by\":{\"kind\":\"human\",\"id\":null},"
        "\"created_at\":\"%s\",\"updated_at\":\"%s\","
        "\"expires_at\":null,\"deleted_at\":null,\"bin\":\"live\","
        "\"version_vector\":{}}"
        "],\"links\":[]}\n",
        FIXTURE_ID("000000000002"), FIXTURE_AT, FIXTURE_ID("000000000003"), FIXTURE_AT,
        FIXTURE_AT, FIXTURE_ID("000000000004"), FIXTURE_AT, FIXTURE_AT);
    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(buf));
    src = harness_side_path(dst, "-in.json");
    ASSERT_EQ_INT(write_text(src, buf), 0);
    {
        const char *args[] = {"--json", "import", "--from-json", src};
        r = fixed_import(dst, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"inserted\":1");
    got = harness_sqlite_query_line(dst, "SELECT COUNT(*) FROM entries;");
    ASSERT_STREQ(got, "1");
    free(got);
    got = harness_sqlite_query_line(dst, "SELECT key FROM entries;");
    ASSERT_STREQ(got, "good:one");
    free(got);
    cmd_result_free(&r);
    free(src);
    free(dst);
}

TEST(import_json_same_id_with_unsorted_tags_is_unchanged)
{
    char *dst = open_empty_brain();
    char *src = NULL;
    CmdResult r;

    /* File tags are zeta then alpha; load_tags returns alpha then zeta. */
    src = harness_side_path(dst, "-in.json");
    ASSERT_EQ_INT(write_one_memory_json(src), 0);
    {
        const char *args[] = {"--json", "import", "--from-json", src};
        r = fixed_import(dst, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    {
        const char *args[] = {"--json", "import", "--from-json", src};
        r = fixed_import(dst, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"unchanged\":1");
    ASSERT_STR_CONTAINS(r.out, "\"inserted\":0");
    cmd_result_free(&r);
    free(src);
    free(dst);
}

TEST(import_json_refuses_version_above_1)
{
    char *dst = open_empty_brain();
    char *src = NULL;
    char *got = NULL;
    char buf[TEXT_BUF];
    char want[PATH_BUF];
    CmdResult r;
    int n = 0;
    const char *base = NULL;

    n = snprintf(buf, sizeof(buf),
                 "{\"format\":\"remember-export\",\"version\":2,\"brain_id\":\"%s\","
                 "\"exported_at\":\"%s\",\"entries\":[],\"links\":[]}\n",
                 FIXTURE_ID("000000000002"), FIXTURE_AT);
    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(buf));
    src = harness_side_path(dst, "-too-new.json");
    ASSERT_EQ_INT(write_text(src, buf), 0);
    base = strrchr(src, '/');
    base = (base != NULL) ? (base + 1) : src;
    n = snprintf(want, sizeof(want),
                 "remember: \"%s\" is remember-export version 2; this remember reads "
                 "version 1. Update remember, then try again.\n",
                 base);
    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(want));
    {
        const char *args[] = {"import", "--from-json", src};
        r = fixed_import(dst, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, want);
    got = harness_sqlite_query_line(dst, "SELECT COUNT(*) FROM entries;");
    ASSERT_STREQ(got, "0");
    free(got);
    cmd_result_free(&r);
    free(src);
    free(dst);
}

TEST(import_json_writes_a_link_and_bumps_ends_without_memory_vectors)
{
    char *dst = open_empty_brain();
    char *src = NULL;
    char *got = NULL;
    char *vv_before = NULL;
    char buf[TEXT_BUF * 2];
    CmdResult r;
    int n = 0;

    n = snprintf(
        buf, sizeof(buf),
        "{\"format\":\"remember-export\",\"version\":1,\"brain_id\":\"%s\","
        "\"exported_at\":\"%s\",\"engine\":\"0.2.0\",\"schema\":6,"
        "\"bins\":{\"live\":2},\"entries\":["
        "{\"id\":\"%s\",\"key\":\"a\",\"body\":\"A\","
        "\"tags\":[],\"created_by\":{\"kind\":\"human\",\"id\":null},"
        "\"created_at\":\"%s\",\"updated_at\":\"%s\","
        "\"expires_at\":null,\"deleted_at\":null,\"bin\":\"live\","
        "\"version_vector\":{\"%s\":1}},"
        "{\"id\":\"%s\",\"key\":\"b\",\"body\":\"B\","
        "\"tags\":[],\"created_by\":{\"kind\":\"human\",\"id\":null},"
        "\"created_at\":\"%s\",\"updated_at\":\"%s\","
        "\"expires_at\":null,\"deleted_at\":null,\"bin\":\"live\","
        "\"version_vector\":{\"%s\":1}}"
        "],\"links\":[{\"from\":\"%s\",\"to\":\"%s\",\"kind\":\"cites\","
        "\"created_at\":\"%s\",\"updated_at\":\"%s\"}]}\n",
        FIXTURE_ID("000000000002"), FIXTURE_AT, FIXTURE_ID("000000000003"), FIXTURE_AT,
        FIXTURE_AT, FIXTURE_ID("000000000001"), FIXTURE_ID("000000000004"), FIXTURE_AT,
        FIXTURE_AT, FIXTURE_ID("000000000001"), FIXTURE_ID("000000000003"),
        FIXTURE_ID("000000000004"), FIXTURE_AT, FIXTURE_AT);
    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(buf));
    src = harness_side_path(dst, "-links.json");
    ASSERT_EQ_INT(write_text(src, buf), 0);
    {
        const char *args[] = {"--json", "import", "--from-json", src};
        r = fixed_import(dst, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    got = harness_sqlite_query_line(
        dst, "SELECT kind FROM entry_links WHERE deleted_at IS NULL;");
    ASSERT_STREQ(got, "cites");
    free(got);
    got = harness_sqlite_query_line(
        dst, "SELECT updated_by FROM entries WHERE id = '" FIXTURE_ID("000000000003") "';");
    ASSERT_STREQ(got, "{\"kind\":\"import\",\"id\":null}");
    free(got);
    vv_before = harness_sqlite_query_line(
        dst, "SELECT version_vector FROM entries WHERE id = '" FIXTURE_ID("000000000003") "';");
    ASSERT_STR_CONTAINS(vv_before, FIXTURE_ID("000000000001"));
    free(vv_before);
    got = harness_sqlite_query_line(dst, "SELECT COUNT(*) FROM entry_links;");
    ASSERT_STREQ(got, "1");
    free(got);
    /* Re-import: live triple unchanged. */
    {
        const char *args[] = {"--json", "import", "--from-json", src};
        r = fixed_import(dst, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    got = harness_sqlite_query_line(dst, "SELECT COUNT(*) FROM entry_links;");
    ASSERT_STREQ(got, "1");
    free(got);
    free(src);
    free(dst);
}

TEST(import_json_skips_a_supersedes_cycle)
{
    char *dst = open_empty_brain();
    char *src = NULL;
    char *got = NULL;
    char buf[TEXT_BUF * 2];
    CmdResult r;
    int n = 0;

    n = snprintf(
        buf, sizeof(buf),
        "{\"format\":\"remember-export\",\"version\":1,\"brain_id\":\"%s\","
        "\"exported_at\":\"%s\",\"engine\":\"0.2.0\",\"schema\":6,"
        "\"bins\":{\"live\":2},\"entries\":["
        "{\"id\":\"%s\",\"key\":\"a\",\"body\":\"A\","
        "\"tags\":[],\"created_by\":{\"kind\":\"human\",\"id\":null},"
        "\"created_at\":\"%s\",\"updated_at\":\"%s\","
        "\"expires_at\":null,\"deleted_at\":null,\"bin\":\"live\","
        "\"version_vector\":{}},"
        "{\"id\":\"%s\",\"key\":\"b\",\"body\":\"B\","
        "\"tags\":[],\"created_by\":{\"kind\":\"human\",\"id\":null},"
        "\"created_at\":\"%s\",\"updated_at\":\"%s\","
        "\"expires_at\":null,\"deleted_at\":null,\"bin\":\"live\","
        "\"version_vector\":{}}"
        "],\"links\":["
        "{\"from\":\"%s\",\"to\":\"%s\",\"kind\":\"supersedes\","
        "\"created_at\":\"%s\",\"updated_at\":\"%s\"},"
        "{\"from\":\"%s\",\"to\":\"%s\",\"kind\":\"supersedes\","
        "\"created_at\":\"%s\",\"updated_at\":\"%s\"}"
        "]}\n",
        FIXTURE_ID("000000000002"), FIXTURE_AT, FIXTURE_ID("000000000003"), FIXTURE_AT,
        FIXTURE_AT, FIXTURE_ID("000000000004"), FIXTURE_AT, FIXTURE_AT,
        FIXTURE_ID("000000000003"), FIXTURE_ID("000000000004"), FIXTURE_AT, FIXTURE_AT,
        FIXTURE_ID("000000000004"), FIXTURE_ID("000000000003"), FIXTURE_AT, FIXTURE_AT);
    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(buf));
    src = harness_side_path(dst, "-cycle.json");
    ASSERT_EQ_INT(write_text(src, buf), 0);
    {
        const char *args[] = {"--json", "import", "--from-json", src};
        r = fixed_import(dst, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    got = harness_sqlite_query_line(dst, "SELECT COUNT(*) FROM entries;");
    ASSERT_STREQ(got, "2");
    free(got);
    got = harness_sqlite_query_line(
        dst, "SELECT COUNT(*) FROM entry_links WHERE deleted_at IS NULL;");
    ASSERT_STREQ(got, "1");
    free(got);
    free(src);
    free(dst);
}

TEST(import_json_canonicalizes_related_to_min_max)
{
    char *dst = open_empty_brain();
    char *src = NULL;
    char *got = NULL;
    char buf[TEXT_BUF * 2];
    CmdResult r;
    int n = 0;

    /* from > to as local insertion order: ids 004 then 003 → store (003,004). */
    n = snprintf(
        buf, sizeof(buf),
        "{\"format\":\"remember-export\",\"version\":1,\"brain_id\":\"%s\","
        "\"exported_at\":\"%s\",\"engine\":\"0.2.0\",\"schema\":6,"
        "\"bins\":{\"live\":2},\"entries\":["
        "{\"id\":\"%s\",\"key\":\"lo\",\"body\":\"L\","
        "\"tags\":[],\"created_by\":{\"kind\":\"human\",\"id\":null},"
        "\"created_at\":\"%s\",\"updated_at\":\"%s\","
        "\"expires_at\":null,\"deleted_at\":null,\"bin\":\"live\","
        "\"version_vector\":{}},"
        "{\"id\":\"%s\",\"key\":\"hi\",\"body\":\"H\","
        "\"tags\":[],\"created_by\":{\"kind\":\"human\",\"id\":null},"
        "\"created_at\":\"%s\",\"updated_at\":\"%s\","
        "\"expires_at\":null,\"deleted_at\":null,\"bin\":\"live\","
        "\"version_vector\":{}}"
        "],\"links\":[{\"from\":\"%s\",\"to\":\"%s\",\"kind\":\"related\","
        "\"created_at\":\"%s\",\"updated_at\":\"%s\"}]}\n",
        FIXTURE_ID("000000000002"), FIXTURE_AT, FIXTURE_ID("000000000003"), FIXTURE_AT,
        FIXTURE_AT, FIXTURE_ID("000000000004"), FIXTURE_AT, FIXTURE_AT,
        FIXTURE_ID("000000000004"), FIXTURE_ID("000000000003"), FIXTURE_AT, FIXTURE_AT);
    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(buf));
    src = harness_side_path(dst, "-related.json");
    ASSERT_EQ_INT(write_text(src, buf), 0);
    {
        const char *args[] = {"--json", "import", "--from-json", src};
        r = fixed_import(dst, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    got = harness_sqlite_query_line(
        dst,
        "SELECT (SELECT id FROM entries WHERE local_id = from_id) || '>' || "
        "(SELECT id FROM entries WHERE local_id = to_id) FROM entry_links;");
    ASSERT_STREQ(got, FIXTURE_ID("000000000003") ">" FIXTURE_ID("000000000004"));
    free(got);
    free(src);
    free(dst);
}

TEST(import_json_leaves_the_destination_device_id_unchanged)
{
    char *dst = open_empty_brain();
    char *src = NULL;
    char *side = NULL;
    char *before = NULL;
    char *after = NULL;
    size_t blen = 0;
    size_t alen = 0;
    CmdResult r;

    side = harness_sidecar_path(dst);
    before = harness_read_file(side, &blen);
    ASSERT_TRUE(before != NULL && blen > 0U);
    src = harness_side_path(dst, "-in.json");
    ASSERT_EQ_INT(write_one_memory_json(src), 0);
    {
        const char *args[] = {"--json", "import", "--from-json", src};
        r = fixed_import(dst, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    after = harness_read_file(side, &alen);
    ASSERT_TRUE(after != NULL);
    ASSERT_EQ_INT((int)alen, (int)blen);
    ASSERT_TRUE(memcmp(after, before, blen) == 0);
    free(before);
    free(after);
    free(side);
    cmd_result_free(&r);
    free(src);
    free(dst);
}

void register_import_formats_tests(void)
{
    RUN_TEST(import_json_inserts_a_keyed_memory_into_an_empty_brain);
    RUN_TEST(import_json_keeps_brain_id_when_the_target_is_not_empty);
    RUN_TEST(import_json_key_clash_does_not_upsert);
    RUN_TEST(import_json_keyed_without_id_inserts_and_does_not_union);
    RUN_TEST(import_json_keyless_without_id_unions_tags);
    RUN_TEST(import_json_keyless_identical_tags_count_unchanged);
    RUN_TEST(import_json_bad_record_commits_the_good_ones);
    RUN_TEST(import_json_same_id_with_unsorted_tags_is_unchanged);
    RUN_TEST(import_json_refuses_version_above_1);
    RUN_TEST(import_json_writes_a_link_and_bumps_ends_without_memory_vectors);
    RUN_TEST(import_json_skips_a_supersedes_cycle);
    RUN_TEST(import_json_canonicalizes_related_to_min_max);
    RUN_TEST(import_json_leaves_the_destination_device_id_unchanged);
}
