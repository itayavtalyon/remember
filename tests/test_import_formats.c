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
    ASSERT_STR_CONTAINS(r.out, "\"origin\":");
    ASSERT_STR_CONTAINS(r.out, "-in.json");
    got = harness_sqlite_query_line(dst, "SELECT body FROM entries WHERE key = 'project:alpha';");
    ASSERT_STREQ(got, "local body");
    free(got);
    got = harness_sqlite_query_line(dst, "SELECT COUNT(*) FROM entries;");
    ASSERT_STREQ(got, "1");
    free(got);
    got = harness_sqlite_query_line(dst, "SELECT origin FROM conflicts;");
    ASSERT_TRUE(got != NULL && strstr(got, "-in.json") != NULL);
    free(got);
    cmd_result_free(&r);
    free(src);
    free(dst);
}

TEST(import_json_origin_flag_overrides_basename_and_rejects_long_labels)
{
    char *dst = open_empty_brain();
    char *src = NULL;
    char *got = NULL;
    CmdResult r;
    CmdResult add;
    char long_label[80];

    {
        const char *args[] = {"add", "local body", "--key", "project:alpha"};
        add = run_remember(dst, args, 4, NULL);
    }
    ASSERT_EQ_INT(add.exit_code, 0);
    cmd_result_free(&add);
    src = harness_side_path(dst, "-in.json");
    ASSERT_EQ_INT(write_one_memory_json(src), 0);
    {
        const char *args[] = {"--json", "import", "--from-json", src, "--origin", "peer-label"};
        r = fixed_import(dst, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"origin\":\"peer-label\"");
    got = harness_sqlite_query_line(dst, "SELECT origin FROM conflicts;");
    ASSERT_STREQ(got, "peer-label");
    free(got);
    cmd_result_free(&r);
    memset(long_label, 'x', sizeof(long_label) - 1U);
    long_label[sizeof(long_label) - 1U] = '\0';
    {
        const char *args[] = {"--json", "import", "--from-json", src, "--origin", long_label};
        r = fixed_import(dst, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.out, "origin is too long (max 64 bytes)");
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
    ASSERT_STR_CONTAINS(r.out, "\"skipped\":1");
    ASSERT_STR_CONTAINS(r.out, "\"errors\":[{\"file\":");
    ASSERT_STR_CONTAINS(r.out, "\"message\":\"body is empty\"}");
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

/* ST-8.3a: nested brain settings document, including a tombstone (plan 22 example). */
TEST(import_json_settings_tombstone_lands_in_an_empty_brain)
{
    char *dst = open_empty_brain();
    char *src = NULL;
    char *got = NULL;
    char buf[4096];
    CmdResult r;
    int n = 0;

    src = harness_side_path(dst, "-settings.json");
    n = snprintf(
        buf, sizeof(buf),
        "{\"format\":\"remember-export\",\"version\":1,\"brain_id\":\"%s\","
        "\"exported_at\":\"%s\",\"engine\":\"0.2.0\",\"schema\":6,"
        "\"bins\":{\"live\":0},\"entries\":[],\"links\":[],"
        "\"settings\":{\"format\":\"remember-settings-export\",\"version\":1,"
        "\"exported_at\":\"%s\",\"settings\":[{"
        "\"key\":\"remember.search.candidate_pool\",\"scope\":\"brain\",\"value\":400,"
        "\"updated_by\":{\"kind\":\"human\",\"id\":null},\"updated_at\":\"%s\","
        "\"device_id\":\"%s\",\"version_vector\":{\"%s\":3},"
        "\"deleted_at\":\"%s\"}]}}\n",
        FIXTURE_ID("000000000002"), FIXTURE_AT, FIXTURE_AT, FIXTURE_AT,
        FIXTURE_ID("000000000001"), FIXTURE_ID("000000000001"), FIXTURE_AT);
    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(buf));
    ASSERT_EQ_INT(write_text(src, buf), 0);
    {
        const char *args[] = {"--json", "import", "--from-json", src};
        r = fixed_import(dst, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"settings\":{\"written\":1,\"unchanged\":0,\"skipped\":0}");
    cmd_result_free(&r);
    got = harness_sqlite_query_line(
        dst, "SELECT value || '|' || deleted_at || '|' || updated_by FROM settings WHERE key = "
             "'remember.search.candidate_pool';");
    ASSERT_STREQ(got, "400|" FIXTURE_AT "|{\"kind\":\"human\",\"id\":null}");
    free(got);
    free(src);
    free(dst);
}

/* Absent settings member writes no settings row and reports zero counts. */
TEST(import_json_without_settings_writes_no_settings_row)
{
    char *dst = open_empty_brain();
    char *src = NULL;
    char *got = NULL;
    CmdResult r;

    src = harness_side_path(dst, "-noset.json");
    ASSERT_EQ_INT(write_one_memory_json(src), 0);
    {
        const char *args[] = {"--json", "import", "--from-json", src};
        r = fixed_import(dst, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"settings\":{\"written\":0,\"unchanged\":0,\"skipped\":0}");
    cmd_result_free(&r);
    got = harness_sqlite_query_line(dst, "SELECT COUNT(*) FROM settings;");
    ASSERT_STREQ(got, "0");
    free(got);
    free(src);
    free(dst);
}

/* A present settings member that is not a settings document fails the whole import. */
TEST(import_json_bad_settings_document_leaves_the_brain_unchanged)
{
    char *dst = open_empty_brain();
    char *src = NULL;
    char *got = NULL;
    char buf[TEXT_BUF];
    CmdResult r;
    int n = 0;

    src = harness_side_path(dst, "-badset.json");
    n = snprintf(buf, sizeof(buf),
                 "{\"format\":\"remember-export\",\"version\":1,\"brain_id\":\"%s\","
                 "\"exported_at\":\"%s\",\"engine\":\"0.2.0\",\"schema\":6,"
                 "\"bins\":{\"live\":1},\"entries\":[{"
                 "\"id\":\"%s\",\"key\":\"project:alpha\",\"body\":\"Alpha body\","
                 "\"tags\":[],\"created_by\":{\"kind\":\"human\",\"id\":null},"
                 "\"updated_by\":{\"kind\":\"human\",\"id\":null},"
                 "\"created_at\":\"%s\",\"updated_at\":\"%s\","
                 "\"expires_at\":null,\"deleted_at\":null,\"bin\":\"live\","
                 "\"version_vector\":{\"%s\":1}}],\"links\":[],\"settings\":{}}\n",
                 FIXTURE_ID("000000000002"), FIXTURE_AT, FIXTURE_ID("000000000003"), FIXTURE_AT,
                 FIXTURE_AT, FIXTURE_ID("000000000001"));
    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(buf));
    ASSERT_EQ_INT(write_text(src, buf), 0);
    {
        const char *args[] = {"--json", "import", "--from-json", src};
        r = fixed_import(dst, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.out, "\"code\":\"import_parse\"");
    ASSERT_STR_CONTAINS(r.out, "\\\"settings\\\" isn't a settings document.");
    cmd_result_free(&r);
    got = harness_sqlite_query_line(dst, "SELECT COUNT(*) FROM entries;");
    ASSERT_STREQ(got, "0");
    free(got);
    got = harness_sqlite_query_line(dst, "SELECT COUNT(*) FROM settings;");
    ASSERT_STREQ(got, "0");
    free(got);
    free(src);
    free(dst);
}

/* A bad settings value is skipped; a good row and the memories still write. */
TEST(import_json_bad_setting_value_is_skipped_and_others_write)
{
    char *dst = open_empty_brain();
    char *src = NULL;
    char *got = NULL;
    char buf[4096];
    CmdResult r;
    int n = 0;

    src = harness_side_path(dst, "-skipset.json");
    n = snprintf(
        buf, sizeof(buf),
        "{\"format\":\"remember-export\",\"version\":1,\"brain_id\":\"%s\","
        "\"exported_at\":\"%s\",\"engine\":\"0.2.0\",\"schema\":6,"
        "\"bins\":{\"live\":1},\"entries\":[{"
        "\"id\":\"%s\",\"key\":\"project:alpha\",\"body\":\"Alpha body\","
        "\"tags\":[],\"created_by\":{\"kind\":\"human\",\"id\":null},"
        "\"updated_by\":{\"kind\":\"human\",\"id\":null},"
        "\"created_at\":\"%s\",\"updated_at\":\"%s\","
        "\"expires_at\":null,\"deleted_at\":null,\"bin\":\"live\","
        "\"version_vector\":{\"%s\":1}}],\"links\":[],"
        "\"settings\":{\"format\":\"remember-settings-export\",\"version\":1,"
        "\"exported_at\":\"%s\",\"settings\":["
        "{\"key\":\"remember.search.candidate_pool\",\"scope\":\"brain\",\"value\":\"nope\","
        "\"updated_by\":{\"kind\":\"human\",\"id\":null},\"updated_at\":\"%s\","
        "\"device_id\":\"%s\",\"version_vector\":{\"%s\":1},\"deleted_at\":null},"
        "{\"key\":\"remember.search.max_results\",\"scope\":\"brain\",\"value\":9,"
        "\"updated_by\":{\"kind\":\"agent\",\"id\":\"bot\"},\"updated_at\":\"%s\","
        "\"device_id\":\"%s\",\"version_vector\":{\"%s\":1},\"deleted_at\":null}]}}\n",
        FIXTURE_ID("000000000002"), FIXTURE_AT, FIXTURE_ID("000000000003"), FIXTURE_AT, FIXTURE_AT,
        FIXTURE_ID("000000000001"), FIXTURE_AT, FIXTURE_AT, FIXTURE_ID("000000000001"),
        FIXTURE_ID("000000000001"), FIXTURE_AT, FIXTURE_ID("000000000001"),
        FIXTURE_ID("000000000001"));
    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(buf));
    ASSERT_EQ_INT(write_text(src, buf), 0);
    {
        const char *args[] = {"--json", "import", "--from-json", src};
        r = fixed_import(dst, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"settings\":{\"written\":1,\"unchanged\":0,\"skipped\":1}");
    ASSERT_STR_CONTAINS(r.out, "\"code\":\"setting_invalid\"");
    ASSERT_STR_CONTAINS(r.out, "isn't a valid value.");
    cmd_result_free(&r);
    got = harness_sqlite_query_line(dst, "SELECT COUNT(*) FROM entries;");
    ASSERT_STREQ(got, "1");
    free(got);
    got = harness_sqlite_query_line(
        dst, "SELECT value || '|' || updated_by FROM settings WHERE key = "
             "'remember.search.max_results';");
    ASSERT_STREQ(got, "9|{\"kind\":\"agent\",\"id\":\"bot\"}");
    free(got);
    got = harness_sqlite_query_line(
        dst, "SELECT COUNT(*) FROM settings WHERE key = 'remember.search.candidate_pool';");
    ASSERT_STREQ(got, "0");
    free(got);
    free(src);
    free(dst);
}

/* C-35: first unknown member warns once; the rest of the file still imports. */
TEST(import_json_unknown_field_warns_once_and_still_imports)
{
    char *dst = open_empty_brain();
    char *src = NULL;
    char *got = NULL;
    char buf[TEXT_BUF];
    char want[PATH_BUF + 160];
    CmdResult r;
    int n = 0;

    src = harness_side_path(dst, "-extra.json");
    n = snprintf(buf, sizeof(buf),
                 "{\"format\":\"remember-export\",\"version\":1,\"brain_id\":\"%s\","
                 "\"exported_at\":\"%s\",\"engine\":\"0.2.0\",\"schema\":6,"
                 "\"bins\":{\"live\":1},\"sync_id\":\"ignored\","
                 "\"entries\":[{"
                 "\"id\":\"%s\",\"key\":\"project:alpha\",\"body\":\"Alpha body\","
                 "\"tags\":[],\"created_by\":{\"kind\":\"human\",\"id\":null},"
                 "\"updated_by\":{\"kind\":\"human\",\"id\":null},"
                 "\"created_at\":\"%s\",\"updated_at\":\"%s\","
                 "\"expires_at\":null,\"deleted_at\":null,\"bin\":\"live\","
                 "\"version_vector\":{\"%s\":1},\"source\":\"old\"}],\"links\":[]}\n",
                 FIXTURE_ID("000000000002"), FIXTURE_AT, FIXTURE_ID("000000000003"), FIXTURE_AT,
                 FIXTURE_AT, FIXTURE_ID("000000000001"));
    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(buf));
    ASSERT_EQ_INT(write_text(src, buf), 0);
    {
        const char *args[] = {"import", "--from-json", src};
        r = fixed_import(dst, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    n = snprintf(want, sizeof(want),
                 "remember: warning: \"%s\" has a field remember doesn't know, \"sync_id\". "
                 "The rest of the file was read.\n",
                 src);
    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(want));
    ASSERT_STREQ(r.err, want);
    ASSERT_STR_CONTAINS(r.out, "inserted 1");
    cmd_result_free(&r);
    got = harness_sqlite_query_line(dst, "SELECT key FROM entries;");
    ASSERT_STREQ(got, "project:alpha");
    free(got);
    free(src);
    free(dst);
}

/* ---- Whole-plan review exploits (2026-11-12): each test failed before its fix. ---- */

#define EXPORT_HEAD "{\"format\":\"remember-export\",\"version\":1,"
#define ONE_ENTRY(id, body, vv)                                                                    \
    "{\"id\":\"" id "\",\"body\":\"" body "\",\"version_vector\":" vv "}"

/* Write text beside dst and import it with --json. */
static CmdResult import_text(const char *dst, const char *suffix, const char *text)
{
    char *src = harness_side_path(dst, suffix);
    CmdResult r;
    const char *args[] = {"--json", "import", "--from-json", src};

    ASSERT_EQ_INT(write_text(src, text), 0);
    r = fixed_import(dst, args, sizeof args / sizeof args[0]);
    free(src);
    return r;
}

static void given_keyless_memory(const char *dst, const char *body)
{
    const char *args[] = {"add", body};
    CmdResult add = run_remember(dst, args, 2, NULL);

    ASSERT_EQ_INT(add.exit_code, 0);
    cmd_result_free(&add);
}

/* A settings member inside an entry is an unknown field, never the root settings. */
TEST(import_json_reads_settings_from_the_root_member_only)
{
    char *dst = open_empty_brain();
    char *got = NULL;
    CmdResult r = import_text(
        dst, "-nested.json",
        EXPORT_HEAD "\"entries\":[{\"id\":\"" FIXTURE_ID("000000000003") "\",\"body\":\"b\","
                    "\"settings\":{\"format\":\"remember-settings-export\",\"version\":1,"
                    "\"settings\":[{\"key\":\"remember.buckets.prefixes\",\"scope\":\"brain\","
                    "\"value\":[\"evil:\"],\"updated_by\":{\"kind\":\"human\",\"id\":null},"
                    "\"updated_at\":\"" FIXTURE_AT "\",\"device_id\":\"" FIXTURE_ID(
                        "000000000001") "\",\"version_vector\":{\"" FIXTURE_ID("000000000001") "\":"
                                        "1},\"deleted_at\":null}]}}],"
                    "\"settings\":{\"format\":\"remember-settings-export\",\"version\":1,"
                    "\"settings\":[]}}");

    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"settings\":{\"written\":0,\"unchanged\":0,\"skipped\":0}");
    cmd_result_free(&r);
    got = harness_sqlite_query_line(dst, "SELECT COUNT(*) FROM settings;");
    ASSERT_STREQ(got, "0");
    free(got);
    free(dst);
}

/* The root member name may be written with an escape; it is still the settings member. */
TEST(import_json_reads_an_escaped_settings_member_name)
{
    char *dst = open_empty_brain();
    CmdResult r = import_text(dst, "-escaped.json",
                              EXPORT_HEAD "\"entries\":[],\"\\u0073ettings\":{\"format\":"
                                          "\"remember-settings-export\",\"version\":1,"
                                          "\"settings\":[]}}");

    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"settings\":{\"written\":0,\"unchanged\":0,\"skipped\":0}");
    cmd_result_free(&r);
    free(dst);
}

/* A keyless record whose body another keyless id holds is hash_clash, not a failed import. */
TEST(import_json_keyless_body_held_by_another_id_is_hash_clash)
{
    char *dst = open_empty_brain();
    char *got = NULL;
    CmdResult r;

    given_keyless_memory(dst, "same body");
    r = import_text(dst, "-clash.json",
                    EXPORT_HEAD "\"entries\":[" ONE_ENTRY(FIXTURE_ID("000000000003"), "same body",
                                                          "{}") "]}");
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"reason\":\"hash_clash\"");
    cmd_result_free(&r);
    got = harness_sqlite_query_line(dst, "SELECT COUNT(*) FROM entries;");
    ASSERT_STREQ(got, "1");
    free(got);
    free(dst);
}

/* Two keyed-by-id keyless records with one body in one file: the first lands, the second clashes. */
TEST(import_json_two_ids_with_one_keyless_body_commit_the_first)
{
    char *dst = open_empty_brain();
    char *got = NULL;
    CmdResult r = import_text(
        dst, "-twin.json",
        EXPORT_HEAD "\"entries\":[" ONE_ENTRY(FIXTURE_ID("000000000003"), "twin", "{}") "," ONE_ENTRY(
            FIXTURE_ID("000000000004"), "twin", "{}") "]}");

    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"inserted\":1");
    ASSERT_STR_CONTAINS(r.out, "\"reason\":\"hash_clash\"");
    cmd_result_free(&r);
    got = harness_sqlite_query_line(dst, "SELECT id FROM entries;");
    ASSERT_STREQ(got, FIXTURE_ID("000000000003"));
    free(got);
    free(dst);
}

/* A version_vector counter must be greater than 0 (plan 23 bad-record rule). */
TEST(import_json_vector_counter_zero_is_invalid)
{
    char *dst = open_empty_brain();
    char *got = NULL;
    CmdResult r = import_text(dst, "-vv0.json",
                              EXPORT_HEAD "\"entries\":[" ONE_ENTRY(
                                  FIXTURE_ID("000000000003"), "b",
                                  "{\"" FIXTURE_ID("000000000001") "\":0}") "]}");

    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"skipped\":1");
    ASSERT_STR_CONTAINS(r.out, "\"message\":\"invalid version_vector\"");
    cmd_result_free(&r);
    got = harness_sqlite_query_line(dst, "SELECT COUNT(*) FROM entries;");
    ASSERT_STREQ(got, "0");
    free(got);
    free(dst);
}

/* ndev distinct v7 device ids, each at counter 1, as one JSON object. */
static char *given_wide_vector(size_t ndev)
{
    size_t cap = ndev * 48U + 8U;
    char *out = (char *)malloc(cap);
    size_t n = 0;
    size_t i = 0;

    ASSERT_TRUE(out != NULL);
    out[n++] = '{';
    for (i = 0; i < ndev; i++) {
        n += (size_t)snprintf(out + n, cap - n, "%s\"01a1d000-0000-7000-8000-%012zx\":1",
                              i > 0U ? "," : "", i);
    }
    out[n++] = '}';
    out[n] = '\0';
    return out;
}

/* A vector wider than the merge buffer is one bad record, not an internal error. */
TEST(import_json_vector_wider_than_the_merge_buffer_is_a_bad_record)
{
    char *dst = open_empty_brain();
    char *vv = given_wide_vector(200U);
    char *doc = NULL;
    char *got = NULL;
    CmdResult r;

    r = import_text(dst, "-first.json",
                    EXPORT_HEAD "\"entries\":[" ONE_ENTRY(FIXTURE_ID("000000000003"), "first",
                                                          "{}") "]}");
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    doc = (char *)malloc(strlen(vv) + (size_t)TEXT_BUF);
    ASSERT_TRUE(doc != NULL);
    (void)snprintf(doc, strlen(vv) + (size_t)TEXT_BUF,
                   EXPORT_HEAD "\"entries\":[{\"id\":\"%s\",\"body\":\"second\","
                               "\"version_vector\":%s}]}",
                   FIXTURE_ID("000000000003"), vv);
    r = import_text(dst, "-wide.json", doc);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"message\":\"invalid version_vector\"");
    cmd_result_free(&r);
    got = harness_sqlite_query_line(dst, "SELECT body FROM entries;");
    ASSERT_STREQ(got, "first");
    free(got);
    free(doc);
    free(vv);
    free(dst);
}

/* Unlink is not an import operation: a file tombstone leaves a live local link live. */
TEST(import_json_tombstone_link_does_not_unlink_a_live_link)
{
    char *dst = open_empty_brain();
    char *got = NULL;
    CmdResult r;

#define TWO_ENTRIES                                                                                \
    "\"entries\":[" ONE_ENTRY(FIXTURE_ID("000000000003"), "A", "{}") "," ONE_ENTRY(               \
        FIXTURE_ID("000000000004"), "B", "{}") "]"
#define CITES_LINK(extra)                                                                          \
    "\"links\":[{\"from\":\"" FIXTURE_ID("000000000003") "\",\"to\":\"" FIXTURE_ID(               \
        "000000000004") "\",\"kind\":\"cites\"" extra "}]"
    r = import_text(dst, "-live.json", EXPORT_HEAD TWO_ENTRIES "," CITES_LINK("") "}");
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = import_text(dst, "-tomb.json",
                    EXPORT_HEAD TWO_ENTRIES "," CITES_LINK(",\"deleted_at\":\"" FIXTURE_AT "\"") "}");
#undef CITES_LINK
#undef TWO_ENTRIES
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    got = harness_sqlite_query_line(dst,
                                    "SELECT COUNT(*) FROM entry_links WHERE deleted_at IS NULL;");
    ASSERT_STREQ(got, "1");
    free(got);
    free(dst);
}

/* ---- Bad-record contract (plan 23 message list), one message per test. ---- */

#define ID3 FIXTURE_ID("000000000003")
#define ID4 FIXTURE_ID("000000000004")

/* Import entries_json as the whole entries array; expect exactly one bad record. */
static void then_one_bad_record(const char *entries_json, const char *message)
{
    char *dst = open_empty_brain();
    char doc[TEXT_BUF * 2];
    char want[TEXT_BUF];
    CmdResult r;

    (void)snprintf(doc, sizeof doc, EXPORT_HEAD "\"entries\":[%s]}", entries_json);
    r = import_text(dst, "-bad.json", doc);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"skipped\":1,\"errors\":[{\"file\":");
    (void)snprintf(want, sizeof want, "\"message\":\"%s\"}]", message);
    ASSERT_STR_CONTAINS(r.out, want);
    cmd_result_free(&r);
    free(dst);
}

TEST(import_json_bad_record_body_missing)
{
    then_one_bad_record("{\"id\":\"" ID3 "\"}", "body is missing");
}

TEST(import_json_bad_record_body_null_is_missing)
{
    then_one_bad_record("{\"id\":\"" ID3 "\",\"body\":null}", "body is missing");
}

TEST(import_json_bad_record_body_too_long)
{
    char *dst = open_empty_brain();
    size_t n = 65537U;
    char *doc = (char *)malloc(n + (size_t)TEXT_BUF);
    size_t at = 0;
    CmdResult r;

    ASSERT_TRUE(doc != NULL);
    at = (size_t)snprintf(doc, (size_t)TEXT_BUF, EXPORT_HEAD "\"entries\":[{\"body\":\"");
    memset(doc + at, 'x', n);
    (void)snprintf(doc + at + n, (size_t)TEXT_BUF - at, "\"}]}");
    r = import_text(dst, "-long.json", doc);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"message\":\"body is too long\"");
    cmd_result_free(&r);
    free(doc);
    free(dst);
}

TEST(import_json_bad_record_body_at_the_limit_inserts)
{
    char *dst = open_empty_brain();
    size_t n = 65536U;
    char *doc = (char *)malloc(n + (size_t)TEXT_BUF);
    size_t at = 0;
    CmdResult r;

    ASSERT_TRUE(doc != NULL);
    at = (size_t)snprintf(doc, (size_t)TEXT_BUF, EXPORT_HEAD "\"entries\":[{\"body\":\"");
    memset(doc + at, 'x', n);
    (void)snprintf(doc + at + n, (size_t)TEXT_BUF - at, "\"}]}");
    r = import_text(dst, "-limit.json", doc);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"inserted\":1");
    cmd_result_free(&r);
    free(doc);
    free(dst);
}

TEST(import_json_bad_record_id_not_a_uuid)
{
    then_one_bad_record("{\"id\":\"nope\",\"body\":\"b\"}", "id is not a UUID");
}

TEST(import_json_bad_record_id_a_number_is_not_a_uuid)
{
    then_one_bad_record("{\"id\":7,\"body\":\"b\"}", "id is not a UUID");
}

TEST(import_json_bad_record_invalid_timestamp)
{
    static const char *const fields[] = {"created_at", "updated_at", "expires_at", "deleted_at"};
    char entry[128];
    size_t i = 0;

    /* Each field on its own: the reader checks only the type, validate the shape. */
    for (i = 0; i < sizeof fields / sizeof fields[0]; i++) {
        (void)snprintf(entry, sizeof entry, "{\"body\":\"b\",\"%s\":\"2026-01-01\"}", fields[i]);
        then_one_bad_record(entry, "invalid timestamp");
    }
}

TEST(import_json_bad_record_invalid_actor_kind)
{
    then_one_bad_record("{\"body\":\"b\",\"created_by\":{\"kind\":\"robot\",\"id\":null}}",
                        "invalid actor");
}

TEST(import_json_bad_record_actor_not_an_object)
{
    then_one_bad_record("{\"body\":\"b\",\"created_by\":\"human\"}", "invalid actor");
}

TEST(import_json_bad_record_invalid_key)
{
    then_one_bad_record("{\"body\":\"b\",\"key\":\"a\\u0001b\"}", "invalid key or tag");
}

TEST(import_json_bad_record_invalid_tag)
{
    then_one_bad_record("{\"body\":\"b\",\"tags\":[\"ok\",\"   \"]}", "invalid key or tag");
}

TEST(import_json_bad_record_entry_not_an_object)
{
    then_one_bad_record("7", "entry is not an object");
}

TEST(import_json_bad_record_duplicate_id)
{
    then_one_bad_record("{\"id\":\"" ID3 "\",\"body\":\"a\"},{\"id\":\"" ID3 "\",\"body\":\"b\"}",
                        "duplicate id");
}

/* D35: a valid UUID is lowercased before duplicate detection. */
TEST(import_json_bad_record_uppercase_copy_is_a_duplicate_id)
{
    then_one_bad_record("{\"id\":\"" ID3 "\",\"body\":\"a\"},"
                        "{\"id\":\"00000000-0000-7000-8000-00000000000A\",\"body\":\"x\"},"
                        "{\"id\":\"00000000-0000-7000-8000-00000000000a\",\"body\":\"b\"}",
                        "duplicate id");
}

TEST(import_json_uppercase_id_is_stored_lowercase)
{
    char *dst = open_empty_brain();
    char *got = NULL;
    CmdResult r = import_text(dst, "-upper.json",
                              EXPORT_HEAD "\"entries\":[{\"id\":\"00000000-0000-7000-8000-"
                                          "00000000000A\",\"body\":\"b\"}]}");

    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    got = harness_sqlite_query_line(dst, "SELECT id FROM entries;");
    ASSERT_STREQ(got, "00000000-0000-7000-8000-00000000000a");
    free(got);
    free(dst);
}

/* D20: the error line is the line of the entry's '{'. */
TEST(import_json_bad_record_names_the_line_of_its_brace)
{
    char *dst = open_empty_brain();
    CmdResult r = import_text(dst, "-lines.json",
                              EXPORT_HEAD "\"entries\":[\n{\"body\":\"ok\"},\n\n  {\"body\":\"\"}]}");

    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "-lines.json\",\"line\":4,\"message\":\"body is empty\"");
    cmd_result_free(&r);
    free(dst);
}

/* Import entries plus one link; expect that link to be the one bad record. */
static void then_one_bad_link(const char *link_json)
{
    char *dst = open_empty_brain();
    char doc[TEXT_BUF * 2];
    CmdResult r;

    (void)snprintf(doc, sizeof doc,
                   EXPORT_HEAD "\"entries\":[" ONE_ENTRY(ID3, "A", "{}") "," ONE_ENTRY(
                       ID4, "B", "{}") "],\"links\":[%s]}",
                   link_json);
    r = import_text(dst, "-link.json", doc);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"inserted\":2");
    ASSERT_STR_CONTAINS(r.out, "\"skipped\":1");
    ASSERT_STR_CONTAINS(r.out, "\"message\":\"invalid link\"");
    cmd_result_free(&r);
    free(dst);
}

TEST(import_json_bad_link_without_from)
{
    then_one_bad_link("{\"to\":\"" ID4 "\",\"kind\":\"cites\"}");
}

TEST(import_json_bad_link_without_kind)
{
    then_one_bad_link("{\"from\":\"" ID3 "\",\"to\":\"" ID4 "\"}");
}

TEST(import_json_bad_link_unknown_kind)
{
    then_one_bad_link("{\"from\":\"" ID3 "\",\"to\":\"" ID4 "\",\"kind\":\"owns\"}");
}

TEST(import_json_bad_link_missing_target)
{
    then_one_bad_link("{\"from\":\"" ID3 "\",\"to\":\"" FIXTURE_ID(
        "000000000009") "\",\"kind\":\"cites\"}");
}

TEST(import_json_bad_link_to_itself)
{
    then_one_bad_link("{\"from\":\"" ID3 "\",\"to\":\"" ID3 "\",\"kind\":\"cites\"}");
}

TEST(import_json_bad_link_invalid_timestamp)
{
    char *dst = open_empty_brain();
    char doc[TEXT_BUF * 2];
    CmdResult r;

    (void)snprintf(doc, sizeof doc,
                   EXPORT_HEAD "\"entries\":[" ONE_ENTRY(ID3, "A", "{}") "," ONE_ENTRY(
                       ID4, "B", "{}") "],\"links\":[{\"from\":\"" ID3 "\",\"to\":\"" ID4
                               "\",\"kind\":\"cites\",\"created_at\":\"yesterday\"}]}");
    r = import_text(dst, "-linkts.json", doc);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"message\":\"invalid timestamp\"");
    cmd_result_free(&r);
    free(dst);
}

/* ---- Must-pass: round trip and no old names. ---- */

/* Every memory row that must survive export then import, as one line. */
#define RT_MEMORIES                                                                                \
    "SELECT group_concat(id || '|' || coalesce(key, '') || '|' || body || '|' || "               \
    "created_by || '|' || coalesce(expires_at, '') || '|' || coalesce(deleted_at, ''), ';') "    \
    "FROM (SELECT * FROM entries ORDER BY id);"
#define RT_TAGS                                                                                    \
    "SELECT group_concat(x, ';') FROM (SELECT e.id || ':' || t.name AS x FROM entry_tags AS j "  \
    "JOIN entries AS e ON e.local_id = j.entry_id JOIN tags AS t ON t.id = j.tag_id ORDER BY x);"
#define RT_LINKS                                                                                   \
    "SELECT group_concat(x, ';') FROM (SELECT f.id || '>' || t.id || ':' || l.kind AS x FROM "   \
    "entry_links AS l JOIN entries AS f ON f.local_id = l.from_id JOIN entries AS t ON "         \
    "t.local_id = l.to_id WHERE l.deleted_at IS NULL ORDER BY x);"

static void then_same(const char *src, const char *dst, const char *query)
{
    char *want = harness_sqlite_query_line(src, query);
    char *got = harness_sqlite_query_line(dst, query);

    ASSERT_TRUE(want != NULL);
    ASSERT_STREQ(got, want);
    free(want);
    free(got);
}

TEST(import_json_round_trip_keeps_memories_and_links)
{
    char *src = make_temp_db_path();
    char *dst = open_empty_brain();
    char *out = NULL;
    char *got = NULL;
    CmdResult r;

    ASSERT_TRUE(src != NULL);
    fixture_roundtrip_build(src);
    out = harness_side_path(src, "-rt.json");
    {
        const char *args[] = {"export",          "--format",        "json", "--out",
                              out,               "--include-expired", "--include-trash"};
        r = run_remember(src, args, sizeof args / sizeof args[0], NULL);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    {
        const char *args[] = {"import", "--from-json", out};
        r = run_remember(dst, args, sizeof args / sizeof args[0], NULL);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "inserted 5 ");
    cmd_result_free(&r);
    then_same(src, dst, RT_MEMORIES);
    then_same(src, dst, RT_TAGS);
    then_same(src, dst, RT_LINKS);
    got = harness_sqlite_query_line(dst, "SELECT group_concat(DISTINCT updated_by) FROM entries;");
    ASSERT_STREQ(got, "{\"kind\":\"import\",\"id\":null}");
    free(got);
    free(out);
    free(src);
    free(dst);
}

TEST(import_json_sync_id_member_is_not_an_id)
{
    char *dst = open_empty_brain();
    char *got = NULL;
    CmdResult r;

    r = import_text(dst, "-old.json",
                    EXPORT_HEAD "\"entries\":[{\"sync_id\":\"" ID3 "\",\"body\":\"old\"}]}");
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"inserted\":1,");
    cmd_result_free(&r);
    got = harness_sqlite_query_line(dst, "SELECT COUNT(*) FROM entries WHERE id = '" ID3 "';");
    ASSERT_STREQ(got, "0");
    free(got);
    free(dst);
}

/* ---- P7 / P9 usage lines, every sibling flag. ---- */

static void then_usage(const char *const *args, size_t nargs, const char *want_err)
{
    char *dst = open_empty_brain();
    CmdResult r = run_remember(dst, args, nargs, NULL);

    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, want_err);
    cmd_result_free(&r);
    free(dst);
}

TEST(import_rejects_each_repeated_source_flag)
{
    static const char *const flags[] = {"--from-json", "--from-md", "--from-csv", "--from-db"};
    char want[128];
    size_t i = 0;

    for (i = 0; i < sizeof flags / sizeof flags[0]; i++) {
        const char *args[] = {"import", flags[i], "a", flags[i], "b"};

        (void)snprintf(want, sizeof want, "remember: duplicate %s\n", flags[i]);
        then_usage(args, 5, want);
    }
}

TEST(import_rejects_each_source_flag_without_a_path)
{
    static const char *const flags[] = {"--from-json", "--from-md", "--from-csv", "--from-db"};
    char want[128];
    size_t i = 0;

    for (i = 0; i < sizeof flags / sizeof flags[0]; i++) {
        const char *args[] = {"import", flags[i]};

        (void)snprintf(want, sizeof want, "remember: %s requires a path\n", flags[i]);
        then_usage(args, 2, want);
    }
}

TEST(import_rejects_two_different_source_flags)
{
    const char *args[] = {"import", "--from-json", "a", "--from-db", "b"};

    then_usage(args, 5,
               "remember: import requires exactly one of --from-json, --from-md, --from-csv, "
               "--from-db\n");
}

TEST(import_rejects_a_repeated_origin)
{
    const char *args[] = {"import", "--origin", "a", "--origin", "b", "--from-json", "x"};

    then_usage(args, 7, "remember: duplicate --origin\n");
}

/* ---- Mutation-pass tests (whole-plan review): each kills a surviving mutant. ---- */

#define D1 FIXTURE_ID("000000000001")
#define D2 FIXTURE_ID("0000000000ff")
#define HUMAN "{\"kind\":\"human\",\"id\":null}"
#define ENTRY_BY(id, body, vv) "{\"id\":\"" id "\",\"body\":\"" body "\",\"created_by\":" HUMAN ",\"version_vector\":" vv "}"

static char *query(const char *db, const char *sql)
{
    return harness_sqlite_query_line(db, sql);
}

static void then_query(const char *db, const char *sql, const char *want)
{
    char *got = query(db, sql);

    ASSERT_STREQ(got, want);
    free(got);
}

static void import_ok(const char *dst, const char *suffix, const char *text)
{
    CmdResult r = import_text(dst, suffix, text);

    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
}

/* M4: of two copies of one id, the first in the file is the one kept. */
TEST(import_json_duplicate_id_keeps_the_first_copy)
{
    char *dst = open_empty_brain();

    import_ok(dst, "-dup.json",
              EXPORT_HEAD "\"entries\":[" ONE_ENTRY(ID3, "first", "{}") "," ONE_ENTRY(
                  ID3, "second", "{}") "]}");
    then_query(dst, "SELECT body FROM entries;", "first");
    free(dst);
}

/* M10: a file tombstone over a local tombstone leaves the link dead. */
TEST(import_json_tombstone_link_over_a_tombstone_stays_dead)
{
    char *dst = open_empty_brain();
    const char *doc =
        EXPORT_HEAD "\"entries\":[" ONE_ENTRY(ID3, "A", "{}") "," ONE_ENTRY(ID4, "B", "{}") "],"
                    "\"links\":[{\"from\":\"" ID3 "\",\"to\":\"" ID4 "\",\"kind\":\"cites\","
                    "\"deleted_at\":\"" FIXTURE_AT "\"}]}";

    import_ok(dst, "-t1.json", doc);
    import_ok(dst, "-t2.json", doc);
    then_query(dst, "SELECT COUNT(*) || '/' || COUNT(deleted_at) FROM entry_links;", "1/1");
    free(dst);
}

/* M14: a new link between existing memories bumps both ends' change_seq, not their vectors. */
TEST(import_json_new_link_bumps_existing_ends)
{
    char *dst = open_empty_brain();
    static const char *const seq[] = {"SELECT change_seq FROM entries WHERE id = '" ID3 "';",
                                      "SELECT change_seq FROM entries WHERE id = '" ID4 "';"};
    const char *vvs = "SELECT group_concat(version_vector, ';') FROM (SELECT * FROM entries "
                      "ORDER BY id);";
    char *before[2] = {NULL, NULL};
    char *vv_before = NULL;
    size_t i = 0;

    import_ok(dst, "-ends.json",
              EXPORT_HEAD "\"entries\":[" ONE_ENTRY(ID3, "A", "{}") "," ONE_ENTRY(ID4, "B",
                                                                                  "{}") "]}");
    for (i = 0; i < 2U; i++) {
        before[i] = query(dst, seq[i]);
    }
    vv_before = query(dst, vvs);
    import_ok(dst, "-link.json",
              EXPORT_HEAD "\"entries\":[],\"links\":[{\"from\":\"" ID3 "\",\"to\":\"" ID4
                          "\",\"kind\":\"cites\"}]}");
    for (i = 0; i < 2U; i++) {
        char *after = query(dst, seq[i]);

        ASSERT_TRUE(before[i] != NULL && after != NULL && strtoll(after, NULL, 10) > strtoll(before[i], NULL, 10));
        free(after);
        free(before[i]);
    }
    then_query(dst, vvs, vv_before);
    free(vv_before);
    free(dst);
}

/* M15: a live file link over a local tombstone revives it and bumps both ends. */
TEST(import_json_reviving_a_link_bumps_its_ends)
{
    char *dst = open_empty_brain();
    static const char *const seq[] = {"SELECT change_seq FROM entries WHERE id = '" ID3 "';",
                                      "SELECT change_seq FROM entries WHERE id = '" ID4 "';"};
    char *before[2] = {NULL, NULL};
    size_t i = 0;

#define AB "\"entries\":[" ONE_ENTRY(ID3, "A", "{}") "," ONE_ENTRY(ID4, "B", "{}") "],"
#define AB_LINK(extra)                                                                             \
    "\"links\":[{\"from\":\"" ID3 "\",\"to\":\"" ID4 "\",\"kind\":\"cites\"" extra "}]}"
    import_ok(dst, "-dead.json", EXPORT_HEAD AB AB_LINK(",\"deleted_at\":\"" FIXTURE_AT "\""));
    for (i = 0; i < 2U; i++) {
        before[i] = query(dst, seq[i]);
    }
    import_ok(dst, "-live.json", EXPORT_HEAD AB AB_LINK(""));
#undef AB_LINK
#undef AB
    then_query(dst, "SELECT COUNT(*) FROM entry_links WHERE deleted_at IS NULL;", "1");
    for (i = 0; i < 2U; i++) {
        char *after = query(dst, seq[i]);

        ASSERT_TRUE(before[i] != NULL && after != NULL &&
                    strtoll(after, NULL, 10) > strtoll(before[i], NULL, 10));
        free(after);
        free(before[i]);
    }
    free(dst);
}

/* M16: a newer id'd record whose body another keyless row holds is a hash clash. */
TEST(import_json_update_onto_a_held_body_is_hash_clash)
{
    char *dst = open_empty_brain();
    CmdResult r;

    import_ok(dst, "-v1.json", EXPORT_HEAD "\"entries\":[" ONE_ENTRY(ID3, "one", "{\"" D1 "\":1}") "]}");
    given_keyless_memory(dst, "two");
    r = import_text(dst, "-v2.json",
                    EXPORT_HEAD "\"entries\":[" ONE_ENTRY(ID3, "two", "{\"" D1 "\":2}") "]}");
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"reason\":\"hash_clash\"");
    cmd_result_free(&r);
    then_query(dst, "SELECT body FROM entries WHERE id = '" ID3 "';", "one");
    free(dst);
}

/* M17: an update from the file stamps updated_by import, not the file's actor (G-33). */
TEST(import_json_update_stamps_the_import_actor)
{
    char *dst = open_empty_brain();

    import_ok(dst, "-u1.json", EXPORT_HEAD "\"entries\":[" ENTRY_BY(ID3, "one", "{\"" D1 "\":1}") "]}");
    import_ok(dst, "-u2.json", EXPORT_HEAD "\"entries\":[" ENTRY_BY(ID3, "two", "{\"" D1 "\":2}") "]}");
    then_query(dst, "SELECT body || ' ' || updated_by FROM entries;",
               "two {\"kind\":\"import\",\"id\":null}");
    free(dst);
}

/* M18, M19: concurrent vectors with different bodies open a conflict, and its incoming
   side carries the import actor. */
TEST(import_json_concurrent_change_is_a_conflict_with_the_import_actor)
{
    char *dst = open_empty_brain();
    CmdResult r;

    import_ok(dst, "-c1.json", EXPORT_HEAD "\"entries\":[" ENTRY_BY(ID3, "one", "{\"" D1 "\":1}") "]}");
    r = import_text(dst, "-c2.json",
                    EXPORT_HEAD "\"entries\":[" ENTRY_BY(ID3, "two", "{\"" D2 "\":1}") "]}");
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"conflicts\":{\"count\":1,");
    cmd_result_free(&r);
    then_query(dst, "SELECT json_extract(incoming_json, '$.updated_by.kind') FROM conflicts;",
               "import");
    then_query(dst, "SELECT body FROM entries;", "one");
    free(dst);
}

/* M20: the origin limit is exactly 64 bytes. */
TEST(import_origin_of_64_bytes_passes_and_65_is_too_long)
{
    char *dst = open_empty_brain();
    char *src = harness_side_path(dst, "-o.json");
    char label[66];
    CmdResult r;

    ASSERT_EQ_INT(write_text(src, EXPORT_HEAD "\"entries\":[]}"), 0);
    memset(label, 'x', 65U);
    label[64] = '\0';
    {
        const char *args[] = {"import", "--from-json", src, "--origin", label};
        r = run_remember(dst, args, 5, NULL);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    label[64] = 'x';
    label[65] = '\0';
    {
        const char *args[] = {"import", "--from-json", src, "--origin", label};
        r = run_remember(dst, args, 5, NULL);
    }
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "remember: origin is too long (max 64 bytes)\n");
    cmd_result_free(&r);
    free(src);
    free(dst);
}

/* ---- Grill answers (2026-11-12): G3, G4, G6 and the P8 rule. ---- */

/* The whole file is import_parse and the brain stays empty. */
static void then_import_parse(const char *doc)
{
    char *dst = open_empty_brain();
    CmdResult r = import_text(dst, "-parse.json", doc);

    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.out, "\"import_parse\"");
    cmd_result_free(&r);
    then_query(dst, "SELECT COUNT(*) FROM entries;", "0");
    free(dst);
}

/* G3: a NUL would cut the string short; the file is refused instead. */
TEST(import_json_nul_in_a_body_is_import_parse)
{
    then_import_parse(EXPORT_HEAD "\"entries\":[{\"body\":\"visible\\u0000hidden\"}]}");
}

TEST(import_json_nul_in_the_format_is_import_parse)
{
    then_import_parse("{\"format\":\"remember-export\\u0000x\",\"version\":1,"
                      "\"entries\":[{\"body\":\"a\"}]}");
}

/* G4: wrong root shapes are not a remember export. */
TEST(import_json_entries_object_is_import_parse)
{
    then_import_parse(EXPORT_HEAD "\"entries\":{\"body\":\"a\"}}");
}

TEST(import_json_links_string_is_import_parse)
{
    then_import_parse(EXPORT_HEAD "\"entries\":[{\"body\":\"a\"}],\"links\":\"none\"}");
}

TEST(import_json_bad_brain_id_is_import_parse)
{
    then_import_parse(EXPORT_HEAD "\"brain_id\":\"nope\",\"entries\":[{\"body\":\"a\"}]}");
}

TEST(import_json_null_brain_id_entries_and_links_mean_absent)
{
    char *dst = open_empty_brain();

    import_ok(dst, "-nulls.json",
              EXPORT_HEAD "\"brain_id\":null,\"entries\":null,\"links\":null}");
    then_query(dst, "SELECT COUNT(*) FROM entries;", "0");
    free(dst);
}

/* P8: a member name twice in one entry or link is duplicate field. */
TEST(import_json_bad_record_duplicate_member)
{
    then_one_bad_record("{\"body\":\"first\",\"body\":\"second\"}", "duplicate field");
}

TEST(import_json_bad_link_duplicate_member)
{
    char *dst = open_empty_brain();
    CmdResult r = import_text(
        dst, "-duplink.json",
        EXPORT_HEAD "\"entries\":[" ONE_ENTRY(ID3, "A", "{}") "," ONE_ENTRY(
            ID4, "B", "{}") "],\"links\":[{\"from\":\"" ID3 "\",\"to\":\"" ID4
                            "\",\"kind\":\"cites\",\"kind\":\"supersedes\"}]}");

    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"skipped\":1");
    ASSERT_STR_CONTAINS(r.out, "\"message\":\"duplicate field\"");
    cmd_result_free(&r);
    then_query(dst, "SELECT COUNT(*) FROM entry_links;", "0");
    free(dst);
}

/* G6: key and tags are stored the way add stores them. */
#define NORM_DOC                                                                                   \
    EXPORT_HEAD "\"entries\":[{\"id\":\"" ID3 "\",\"body\":\"n\",\"key\":\"  Project:Alpha \","       \
                "\"tags\":[\"  Pad  \",\"pad\",\"B\"]}]}"
TEST(import_json_normalizes_key_and_tags_like_add)
{
    char *dst = open_empty_brain();
    CmdResult r;

    import_ok(dst, "-norm.json", NORM_DOC);
    then_query(dst, "SELECT key FROM entries;", "project:alpha");
    then_query(dst,
               "SELECT group_concat(name, ',') FROM (SELECT t.name FROM entry_tags AS j JOIN tags "
               "AS t ON t.id = j.tag_id ORDER BY t.name);",
               "b,pad");
    /* The same record again: "Pad" and "pad" are one tag, so nothing changed. */
    r = import_text(dst, "-norm2.json", NORM_DOC);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"inserted\":0,\"updated\":0,\"unchanged\":1,");
    cmd_result_free(&r);
    free(dst);
}

/* A source that cannot be read names the path and the OS reason (import_parse). */
TEST(import_json_missing_file_names_the_path)
{
    char *dst = open_empty_brain();
    char *src = harness_side_path(dst, "-missing.json");
    char want[PATH_BUF + 128];
    CmdResult r;
    const char *args[] = {"import", "--from-json", src};

    r = run_remember(dst, args, 3, NULL);
    (void)snprintf(want, sizeof want,
                   "remember: can't read \"%s\": No such file or directory. Check the import "
                   "source, then try again.\n",
                   src);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, want);
    cmd_result_free(&r);
    then_query(dst, "SELECT COUNT(*) FROM entries;", "0");
    free(src);
    free(dst);
}

void register_import_formats_tests(void)
{
    RUN_TEST(import_json_inserts_a_keyed_memory_into_an_empty_brain);
    RUN_TEST(import_json_keeps_brain_id_when_the_target_is_not_empty);
    RUN_TEST(import_json_key_clash_does_not_upsert);
    RUN_TEST(import_json_origin_flag_overrides_basename_and_rejects_long_labels);
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
    RUN_TEST(import_json_settings_tombstone_lands_in_an_empty_brain);
    RUN_TEST(import_json_without_settings_writes_no_settings_row);
    RUN_TEST(import_json_bad_settings_document_leaves_the_brain_unchanged);
    RUN_TEST(import_json_bad_setting_value_is_skipped_and_others_write);
    RUN_TEST(import_json_unknown_field_warns_once_and_still_imports);
    RUN_TEST(import_json_reads_settings_from_the_root_member_only);
    RUN_TEST(import_json_reads_an_escaped_settings_member_name);
    RUN_TEST(import_json_keyless_body_held_by_another_id_is_hash_clash);
    RUN_TEST(import_json_two_ids_with_one_keyless_body_commit_the_first);
    RUN_TEST(import_json_vector_counter_zero_is_invalid);
    RUN_TEST(import_json_vector_wider_than_the_merge_buffer_is_a_bad_record);
    RUN_TEST(import_json_tombstone_link_does_not_unlink_a_live_link);
    RUN_TEST(import_json_bad_record_body_missing);
    RUN_TEST(import_json_bad_record_body_null_is_missing);
    RUN_TEST(import_json_bad_record_body_too_long);
    RUN_TEST(import_json_bad_record_body_at_the_limit_inserts);
    RUN_TEST(import_json_bad_record_id_not_a_uuid);
    RUN_TEST(import_json_bad_record_id_a_number_is_not_a_uuid);
    RUN_TEST(import_json_bad_record_invalid_timestamp);
    RUN_TEST(import_json_bad_record_invalid_actor_kind);
    RUN_TEST(import_json_bad_record_actor_not_an_object);
    RUN_TEST(import_json_bad_record_invalid_key);
    RUN_TEST(import_json_bad_record_invalid_tag);
    RUN_TEST(import_json_bad_record_entry_not_an_object);
    RUN_TEST(import_json_bad_record_duplicate_id);
    RUN_TEST(import_json_bad_record_uppercase_copy_is_a_duplicate_id);
    RUN_TEST(import_json_uppercase_id_is_stored_lowercase);
    RUN_TEST(import_json_bad_record_names_the_line_of_its_brace);
    RUN_TEST(import_json_bad_link_without_from);
    RUN_TEST(import_json_bad_link_without_kind);
    RUN_TEST(import_json_bad_link_unknown_kind);
    RUN_TEST(import_json_bad_link_missing_target);
    RUN_TEST(import_json_bad_link_to_itself);
    RUN_TEST(import_json_bad_link_invalid_timestamp);
    RUN_TEST(import_rejects_each_repeated_source_flag);
    RUN_TEST(import_rejects_each_source_flag_without_a_path);
    RUN_TEST(import_rejects_two_different_source_flags);
    RUN_TEST(import_rejects_a_repeated_origin);
    RUN_TEST(import_json_missing_file_names_the_path);
    RUN_TEST(import_json_round_trip_keeps_memories_and_links);
    RUN_TEST(import_json_sync_id_member_is_not_an_id);
    RUN_TEST(import_json_duplicate_id_keeps_the_first_copy);
    RUN_TEST(import_json_tombstone_link_over_a_tombstone_stays_dead);
    RUN_TEST(import_json_new_link_bumps_existing_ends);
    RUN_TEST(import_json_reviving_a_link_bumps_its_ends);
    RUN_TEST(import_json_update_onto_a_held_body_is_hash_clash);
    RUN_TEST(import_json_update_stamps_the_import_actor);
    RUN_TEST(import_json_concurrent_change_is_a_conflict_with_the_import_actor);
    RUN_TEST(import_origin_of_64_bytes_passes_and_65_is_too_long);
    RUN_TEST(import_json_nul_in_a_body_is_import_parse);
    RUN_TEST(import_json_nul_in_the_format_is_import_parse);
    RUN_TEST(import_json_entries_object_is_import_parse);
    RUN_TEST(import_json_links_string_is_import_parse);
    RUN_TEST(import_json_bad_brain_id_is_import_parse);
    RUN_TEST(import_json_null_brain_id_entries_and_links_mean_absent);
    RUN_TEST(import_json_bad_record_duplicate_member);
    RUN_TEST(import_json_bad_link_duplicate_member);
    RUN_TEST(import_json_normalizes_key_and_tags_like_add);
}
