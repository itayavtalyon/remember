#include "commands_common.h"
#include "export_format.h"
#include "fixture_roundtrip.h"
#include "harness.h"
#include "register.h"
#include "store.h"
#include "test.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    PATH_BUF = 1024,  /* one path */
    TEXT_BUF = 2048,  /* a path inside a sentence */
    TREE_BUF = 4096,  /* a folder listing or an error object */
    NAME_BUF = 128,   /* one file name */
    TREE_FILES = 16,  /* files a listing holds */
    SMALL_BUF = 64,   /* a short JSON fragment */
    COUNT_BUF = 16,   /* a decimal count */
    ARGS_MAX = 8,     /* argv slots */
    MODE_MASK = 0777, /* permission bits */
    DIR_MODE = 0700,  /* decision:plan22-export-file-mode */
    FILE_MODE = 0600,
    FAULT_PAD = 7,
};

/* The test CLI's fixed ids stop the clock, so exported_at is FIXTURE_AT. */
static CmdResult export_run(const char *db, const char *const *args, size_t nargs)
{
    CmdResult r;

    // NOLINTBEGIN(concurrency-mt-unsafe) the child inherits this process-global
    (void)setenv("REMEMBER_TEST_FIXED_IDS", "4096", 1);
    r = run_remember(db, args, nargs, NULL);
    (void)unsetenv("REMEMBER_TEST_FIXED_IDS");
    // NOLINTEND(concurrency-mt-unsafe)
    return r;
}

#define HUMAN_ACTOR "{\"kind\":\"human\",\"id\":null}"
#define DOC_HEAD                                                                                   \
    "{\"format\":\"remember-export\",\"version\":1,\"brain_id\":\"" FIXTURE_ID(                    \
        "000000000002") "\",\"exported_at\":\"" FIXTURE_AT "\",\"engine\":\"" REMEMBER_VERSION     \
                        "\",\"schema\":6,"
#define TIMES "\"created_at\":\"" FIXTURE_AT "\",\"updated_at\":\"" FIXTURE_AT "\""
#define VV1 "{\"" FIXTURE_ID("000000000001") "\":1}"
#define ENTRY_ALPHA                                                                                \
    "{\"id\":\"" FIXTURE_ID(                                                                       \
        "000000000003") "\",\"key\":\"project:alpha\",\"body\":\"Alpha "                           \
                        "\\\"quoted\\\", body\\nline two\",\"tags\":["                             \
                        "\"alpha\",\"beta\",\"zeta\"],\"created_by\":" HUMAN_ACTOR                 \
                        ",\"updated_by\":{\"kind\":\"agent\",\"id\":\"bot-1\"}," TIMES             \
                        ",\"expires_at\":null,\"deleted_at\":null,\"bin\":\"live\",\"version_"     \
                        "vector\""                                                                 \
                        ":{\"" FIXTURE_ID("000000000001") "\":5,\"" FIXTURE_ID(                    \
                            "0000000000ff") "\":2}}"
#define ENTRY_WEEKLY                                                                               \
    "{\"id\":\"" FIXTURE_ID("000000000010") "\",\"key\":null,\"body\":\"# Weekly Notes \\nBody "   \
                                            "of the week.\",\"tags\":[\"a\",\"b\"],\"created_by"   \
                                            "\":" HUMAN_ACTOR ",\"updated_by\":" HUMAN_ACTOR       \
                                            "," TIMES ",\"expires_at\":null,\"deleted_at\":null,"  \
                                            "\"bin\":\"live\",\"version_vector\":" VV1 "}"
#define ENTRY_WORDS                                                                                \
    "{\"id\":\"" FIXTURE_ID(                                                                       \
        "000000000020") "\",\"key\":null,\"body\":\"one two  three\\tfour "                        \
                        "five six seven eight\",\"tags\":[],\"created_by\":" HUMAN_ACTOR           \
                        ",\"updated_by\":" HUMAN_ACTOR "," TIMES                                   \
                        ",\"expires_at\":null,\"deleted_at\":null,\"bin\":\"live\",\"version_"     \
                        "vector\""                                                                 \
                        ":" VV1 "}"
#define ENTRY_EXPIRED                                                                              \
    "{\"id\":\"" FIXTURE_ID("000000000030") "\",\"key\":\"old:thing\",\"body\":\"Expired "         \
                                            "memory.\",\"tags\":[],\"created_by\":" HUMAN_ACTOR    \
                                            ",\"updated_by\":" HUMAN_ACTOR "," TIMES               \
                                            ",\"expires_at\":\"2025-06-01T00:00:00.000Z\","        \
                                            "\"deleted_at\":null,\"bin\":\"expired\","             \
                                            "\"version_vector\":" VV1 "}"
#define ENTRY_GONE                                                                                 \
    "{\"id\":\"" FIXTURE_ID(                                                                       \
        "000000000040") "\",\"key\":\"gone\",\"body\":\"Deleted memory.\","                        \
                        "\"tags\":[],\"created_by\":" HUMAN_ACTOR ",\"updated_by\":" HUMAN_ACTOR   \
                        "," TIMES ",\"expires_at\":null,\"deleted_at\":\"" FIXTURE_AT              \
                        "\",\"bin\":\"deleted\",\"version_vector\":{\"" FIXTURE_ID(                \
                            "000000000001") "\":2}}"
#define LINK(from, to, kind)                                                                       \
    "{\"from\":\"" FIXTURE_ID(from) "\",\"to\":\"" FIXTURE_ID(to) "\",\"kind\":\"" kind            \
                                                                  "\"," TIMES "}"

/* A brain with no settings rows still writes the nested document (ST-8.3). */
#define SETTINGS_EMPTY                                                                             \
    ",\"settings\":{\"format\":\"remember-settings-export\",\"version\":1,\"exported_at\":"        \
    "\"" FIXTURE_AT "\",\"settings\":[]}"

/* Every bin: the five memories, the four live links, never the tombstoned one. */
TEST(export_json_every_bin_empty_settings)
{
    char *db = make_temp_db_path();
    char *out = NULL;
    char *doc = NULL;
    size_t len = 0U;
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    fixture_roundtrip_build(db);
    out = harness_side_path(db, ".json");
    {
        const char *args[] = {"export", "--format",          "json",           "--out",
                              out,      "--include-expired", "--include-trash"};
        r = export_run(db, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    doc = harness_read_file(out, &len);
    ASSERT_STREQ(doc, DOC_HEAD
                 "\"bins\":{\"live\":3,\"expired\":1,\"deleted\":1},\"entries\":[" ENTRY_ALPHA
                 "," ENTRY_WEEKLY "," ENTRY_WORDS "," ENTRY_EXPIRED "," ENTRY_GONE
                 "],\"links\":[" LINK("000000000003", "000000000010", "cites") "," LINK(
                     "000000000003", "000000000030",
                     "related") "," LINK("000000000003", "000000000040",
                                         "supersedes") "," LINK("000000000010", "000000000020",
                                                                "related") "]" SETTINGS_EMPTY
                                                                           "}\n");
    free(doc);
    cmd_result_free(&r);
    free(out);
    free(db);
}

/* No flag: the live bin only, and only links whose two ends are live. */
TEST(export_json_default_is_live_only)
{
    char *db = make_temp_db_path();
    char *out = NULL;
    char *doc = NULL;
    size_t len = 0U;
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    fixture_roundtrip_build(db);
    out = harness_side_path(db, ".json");
    {
        const char *args[] = {"--json", "export", "--format", "json", "--out", out};
        r = export_run(db, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    doc = harness_read_file(out, &len);
    ASSERT_STREQ(doc, DOC_HEAD
                 "\"bins\":{\"live\":3},\"entries\":[" ENTRY_ALPHA "," ENTRY_WEEKLY "," ENTRY_WORDS
                 "],\"links\":[" LINK("000000000003", "000000000010", "cites") "," LINK(
                     "000000000010", "000000000020", "related") "]" SETTINGS_EMPTY "}\n");
    ASSERT_STR_PREFIX(r.out, "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory."
                             "export.result\",\"data\":{\"action\":\"exported\",\"format\":"
                             "\"json\",\"path\":");
    ASSERT_STR_CONTAINS(r.out, ",\"count\":3,\"links\":2}}\n");
    free(doc);
    cmd_result_free(&r);
    free(out);
    free(db);
}

/* Names under dir that hold ".tmp-" or ".old-": what a failed export left behind. */
static int count_strays(const char *dir)
{
    DIR *d = opendir(dir);
    const struct dirent *ent = NULL;
    int n = 0;

    ASSERT_TRUE(d != NULL);
    if (d == NULL) {
        return -1;
    }
    // NOLINTNEXTLINE(concurrency-mt-unsafe) single-threaded test
    while ((ent = readdir(d)) != NULL) {
        if (strstr(ent->d_name, ".tmp-") != NULL || strstr(ent->d_name, ".old-") != NULL) {
            n++;
        }
    }
    (void)closedir(d);
    return n;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) path, then text
static void write_text(const char *path, const char *text)
{
    /* NOLINTNEXTLINE(android-cloexec-fopen) */
    FILE *f = fopen(path, "w");
    bool written = false;

    if (f != NULL) {
        written = fputs(text, f) >= 0;
        if (fclose(f) != 0) {
            written = false;
        }
    }
    ASSERT_TRUE(written);
}

/* write_text on base + suffix. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) path parts, then text
static void write_side(const char *base, const char *suffix, const char *text)
{
    char *path = harness_side_path(base, suffix);

    write_text(path, text);
    free(path);
}

/* A fresh brain holding the round-trip fixture, its folder, and an out path in it. */
typedef struct {
    char *db;
    char *dir;
    char *out;
} Given;

static Given given_fixture(const char *out_name)
{
    Given g;
    size_t n = 0;

    g.db = make_temp_db_path();
    ASSERT_TRUE(g.db != NULL);
    fixture_roundtrip_build(g.db);
    g.dir = dir_of_path(g.db);
    n = strlen(g.dir) + strlen(out_name) + 2U;
    g.out = (char *)malloc(n);
    ASSERT_TRUE(g.out != NULL);
    (void)snprintf(g.out, n, "%s/%s", g.dir, out_name);
    return g;
}

static Given given_tombstone(const char *out_name)
{
    Given g = given_fixture(out_name);

    fixture_roundtrip_add_tombstone(g.db);
    return g;
}

static void given_free(Given *g)
{
    free(g->db);
    free(g->dir);
    free(g->out);
}

/* E6.6: an existing path without --force writes nothing; --force replaces it. */
TEST(export_force_existing_path)
{
    Given g = given_fixture("brain.json");
    char *doc = NULL;
    size_t len = 0U;
    char want[TEXT_BUF];
    CmdResult r;

    write_text(g.out, "old\n");
    {
        const char *args[] = {"export", "--format", "json", "--out", g.out};
        r = export_run(g.db, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 1);
    (void)snprintf(want, sizeof want, "remember: output already exists: %s\n", g.out);
    ASSERT_STREQ(r.err, want);
    ASSERT_STREQ(r.out, "");
    cmd_result_free(&r);
    doc = harness_read_file(g.out, &len);
    ASSERT_STREQ(doc, "old\n");
    free(doc);
    ASSERT_EQ_INT(count_strays(g.dir), 0);
    {
        const char *args[] = {"export", "--format", "json", "--out", g.out, "--force"};
        r = export_run(g.db, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    doc = harness_read_file(g.out, &len);
    ASSERT_STR_PREFIX(doc, DOC_HEAD "\"bins\":{\"live\":3},");
    free(doc);
    ASSERT_EQ_INT(count_strays(g.dir), 0);
    given_free(&g);
}

typedef struct {
    const char *args[ARGS_MAX];
    size_t nargs;
    const char *err;
} UsageCase;

/* The locked usage sentences, and the shared parser lines export inherits. */
TEST(export_usage_sentences)
{
    char *db = make_temp_db_path();
    static const UsageCase cases[] = {
        {{"export", "--out", "x"}, 3, "export requires --format (json, md, or csv)"},
        {{"export", "--out", "x", "--format"}, 4, "export requires --format (json, md, or csv)"},
        {{"export", "--format", "xml", "--out", "x"}, 5, "invalid --format (use json, md, or csv)"},
        {{"export", "--format", "JSON", "--out", "x"},
         5,
         "invalid --format (use json, md, or csv)"},
        {{"export", "--format", "", "--out", "x"}, 5, "invalid --format (use json, md, or csv)"},
        {{"export", "--format", "json"}, 3, "export requires --out PATH"},
        {{"export", "--format", "json", "--out"}, 4, "export requires --out PATH"},
        {{"export", "--format", "json", "--out", ""}, 5, "export requires --out PATH"},
        {{"export", "--format", "json", "--format", "json", "--out", "x"}, 7, "duplicate --format"},
        {{"export", "--format", "json", "--out", "x", "--out", "y"}, 7, "duplicate --out"},
        {{"export", "--include-expired", "--include-expired"}, 3, "duplicate --include-expired"},
        {{"export", "--include-trash", "--include-trash"}, 3, "duplicate --include-trash"},
        {{"export", "--exclude-archived", "--exclude-archived"}, 3, "duplicate --exclude-archived"},
        {{"export", "--force", "--force"}, 3, "duplicate --force"},
        {{"export", "--bins", "live"}, 3, "unknown option '--bins'"},
        {{"export", "--trash"}, 2, "unknown option '--trash'"},
        {{"export", "--hooks"}, 2, "unknown option '--hooks'"},
        {{"export", "out.json"}, 2, "export takes no positional arguments"},
    };
    size_t i = 0;

    ASSERT_TRUE(db != NULL);
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char want[NAME_BUF];
        CmdResult r = run_remember(db, cases[i].args, cases[i].nargs, NULL);

        (void)snprintf(want, sizeof want, "remember: %s\n", cases[i].err);
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STREQ(r.err, want);
        cmd_result_free(&r);
    }
    free(db);
}

typedef struct {
    const char *fail; /* REMEMBER_TEST_EXPORT_FAIL, or NULL for a real failure */
    const char *out_name;
    const char *json; /* the error object; each @ is the out path */
} FileErrorCase;

/* tmpl with each '@' replaced by path. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) in the order the names say
static void fill_path(char *buf, size_t cap, const char *tmpl, const char *path)
{
    size_t w = 0;
    const char *p = NULL;

    for (p = tmpl; *p != '\0' && w + strlen(path) + 1U < cap; p++) {
        if (*p == '@') {
            memcpy(buf + w, path, strlen(path));
            w += strlen(path);
        } else {
            buf[w++] = *p;
        }
    }
    buf[w] = '\0';
}

/* D19: ENOSPC, EACCES, EROFS, ENOENT, and any other errno. The temp never stays. */
TEST(export_file_errors_map_codes)
{
    Given g = given_fixture("x");
    static const FileErrorCase cases[] = {
        {"write:28", "brain.json",
         "{\"code\":\"disk_full\",\"message\":\"disk is full.\",\"fix\":\"Free some space and try "
         "again.\",\"retryable\":false,\"detail\":{\"errno\":28,\"path\":\"@\"}}"},
        {"write:13", "brain.json",
         "{\"code\":\"read_only\",\"message\":\"cannot write \\\"@\\\".\",\"fix\":\"Give "
         "yourself write permission on that folder, or export to another folder.\","
         "\"retryable\":false,\"detail\":{\"errno\":13,\"path\":\"@\"}}"},
        {"write:30", "brain.json",
         "{\"code\":\"read_only\",\"message\":\"cannot write \\\"@\\\".\",\"fix\":\"Give "
         "yourself write permission on that folder, or export to another folder.\","
         "\"retryable\":false,\"detail\":{\"errno\":30,\"path\":\"@\"}}"},
        {"write:5", "brain.json",
         "{\"code\":\"io_error\",\"message\":\"the disk reported a read or write error.\","
         "\"fix\":\"Check the disk or drive, then try again.\",\"retryable\":true,\"detail\":{"
         "\"errno\":5,\"path\":\"@\"}}"},
        {NULL, "no-such-folder/brain.json",
         "{\"code\":\"cannot_open\",\"message\":\"the folder for \\\"@\\\" doesn't exist.\","
         "\"fix\":\"Create the folder, or export to another path.\",\"retryable\":false,"
         "\"detail\":{\"errno\":2,\"path\":\"@\",\"reason\":\"missing\"}}"},
    };
    size_t i = 0;

    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char out[PATH_BUF];
        char error[TEXT_BUF];
        char want[TREE_BUF];
        const char *args[] = {"--json", "export", "--format", "json", "--out", out};
        CmdResult r;

        (void)snprintf(out, sizeof out, "%s/%s", g.dir, cases[i].out_name);
        fill_path(error, sizeof error, cases[i].json, out);
        (void)snprintf(want, sizeof want,
                       "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.export."
                       "result\",\"error\":%s}\n",
                       error);
        // NOLINTBEGIN(concurrency-mt-unsafe) the child inherits this process-global
        if (cases[i].fail != NULL) {
            (void)setenv("REMEMBER_TEST_EXPORT_FAIL", cases[i].fail, 1);
        }
        r = run_remember(g.db, args, sizeof args / sizeof args[0], NULL);
        (void)unsetenv("REMEMBER_TEST_EXPORT_FAIL");
        // NOLINTEND(concurrency-mt-unsafe)
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STREQ(r.out, want);
        ASSERT_STREQ(r.err, "");
        ASSERT_FALSE(access(out, F_OK) == 0);
        ASSERT_EQ_INT(count_strays(g.dir), 0);
        cmd_result_free(&r);
    }
    given_free(&g);
}

/* The D19 human line agrees at 1 (decision:plan22-export-human-count-agrees). */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) in the order the names say
static void assert_human_line(const char *db, const char *line)
{
    char *out = harness_side_path(db, ".human.json");
    char want[PATH_BUF];
    const char *args[] = {"export", "--format", "json", "--out", out, "--force"};
    CmdResult r = run_remember(db, args, sizeof args / sizeof args[0], NULL);

    (void)snprintf(want, sizeof want, "%s%s\n", line, out);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.out, want);
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
    free(out);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) in the order the names say
static void add_plain(const char *db, const char *body)
{
    const char *args[] = {"add", body};
    CmdResult r = run_remember(db, args, sizeof args / sizeof args[0], NULL);

    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
}

TEST(export_human_line)
{
    Given g = given_fixture("brain.json");
    char *db = make_temp_db_path();
    const char *link[] = {"link", "1", "2"};
    CmdResult r;
    const char *create[] = {"list"};

    assert_human_line(g.db, "exported 3 memories, 2 links to ");
    r = run_remember(db, create, 1U, NULL);
    cmd_result_free(&r);
    assert_human_line(db, "exported 0 memories, 0 links to ");
    add_plain(db, "first");
    assert_human_line(db, "exported 1 memory, 0 links to ");
    add_plain(db, "second");
    r = run_remember(db, link, sizeof link / sizeof link[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    assert_human_line(db, "exported 2 memories, 1 link to ");
    free(db);
    given_free(&g);
}

/* An --out that cannot be made absolute is a file error, never "internal error". */
TEST(export_out_too_long_is_a_file_error)
{
    enum { LONG_OUT = 5000 };
    char *db = make_temp_db_path();
    char *out = (char *)malloc(LONG_OUT + 1);
    const char *args[] = {"--json", "export", "--format", "json", "--out", out};
    char want[SMALL_BUF];
    CmdResult r;

    ASSERT_TRUE(db != NULL && out != NULL);
    if (db == NULL || out == NULL) {
        free(db);
        free(out);
        return;
    }
    add_plain(db, "one");
    memset(out, 'a', LONG_OUT);
    out[LONG_OUT] = '\0';
    r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
    (void)snprintf(want, sizeof want, "\"detail\":{\"errno\":%d,", ENAMETOOLONG);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.out, "\"code\":\"io_error\"");
    ASSERT_STR_CONTAINS(r.out, want);
    cmd_result_free(&r);
    free(out);
    free(db);
}

/* A folder where the file goes fails before anything is written, even with
   --force (decision:plan22-export-out-is-folder). */
TEST(export_out_is_a_folder)
{
    Given g = given_fixture("folder");
    char want[TEXT_BUF];
    CmdResult r;

    ASSERT_EQ_INT(mkdir(g.out, 0700), 0);
    {
        const char *args[] = {"--json", "export", "--format", "json", "--out", g.out, "--force"};
        r = export_run(g.db, args, sizeof args / sizeof args[0]);
    }
    (void)snprintf(want, sizeof want,
                   "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.export."
                   "result\",\"error\":{\"code\":\"cannot_open\",\"message\":\"\\\"%s\\\" is a "
                   "folder.\",\"fix\":\"Pick a file path for --out.\",\"retryable\":false,"
                   "\"detail\":{\"errno\":21,\"path\":\"%s\",\"reason\":\"is_directory\"}}}\n",
                   g.out, g.out);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, want);
    cmd_result_free(&r);
    ASSERT_EQ_INT(count_strays(g.dir), 0);
    {
        const char *args[] = {"export", "--format", "json", "--out", g.out};
        r = export_run(g.db, args, sizeof args / sizeof args[0]);
    }
    (void)snprintf(want, sizeof want, "remember: \"%s\" is a folder. Pick a file path for --out.\n",
                   g.out);
    ASSERT_STREQ(r.err, want);
    cmd_result_free(&r);
    given_free(&g);
}

/* Export never creates a brain: the --no-create failure, and no file
   (decision:plan22-export-no-create). */
TEST(export_missing_brain_is_not_created)
{
    char *db = make_temp_db_path();
    char *out = harness_side_path(db, ".json");
    const char *exp[] = {"export", "--format", "json", "--out", out};
    const char *list[] = {"--no-create", "list"};
    CmdResult r;
    CmdResult want;

    ASSERT_TRUE(db != NULL && out != NULL);
    if (db == NULL || out == NULL) {
        return;
    }
    r = run_remember(db, exp, sizeof exp / sizeof exp[0], NULL);
    want = run_remember(db, list, sizeof list / sizeof list[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "cannot open the brain file");
    ASSERT_STREQ(r.err, want.err);
    ASSERT_FALSE(access(db, F_OK) == 0);
    ASSERT_FALSE(access(out, F_OK) == 0);
    cmd_result_free(&r);
    cmd_result_free(&want);
    free(out);
    free(db);
}

/* --exclude-archived parses and changes nothing until the archive bin exists. */
TEST(export_exclude_archived_has_no_effect_yet)
{
    Given g = given_fixture("brain.json");
    char *plain = harness_side_path(g.db, ".plain.json");
    char *a = NULL;
    char *b = NULL;
    size_t len = 0U;
    CmdResult r;

    {
        const char *args[] = {"export", "--format", "json", "--out", plain};
        r = export_run(g.db, args, sizeof args / sizeof args[0]);
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"export", "--format", "json", "--out", g.out, "--exclude-archived"};
        r = export_run(g.db, args, sizeof args / sizeof args[0]);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
    }
    a = harness_read_file(plain, &len);
    b = harness_read_file(g.out, &len);
    ASSERT_TRUE(a != NULL);
    ASSERT_STREQ(b, a);
    free(a);
    free(b);
    free(plain);
    given_free(&g);
}

/* An echoed path keeps a plain line on one line and escapes in JSON; a long one is whole. */
TEST(export_echoed_path_is_masked)
{
    Given g = given_fixture("a\nb\x1b.json");
    char want[TREE_BUF];
    char longpath[PATH_BUF];
    CmdResult r;

    write_text(g.out, "x");
    {
        const char *args[] = {"export", "--format", "json", "--out", g.out};
        r = run_remember(g.db, args, sizeof args / sizeof args[0], NULL);
    }
    (void)snprintf(want, sizeof want, "remember: output already exists: %s/a?b?.json\n", g.dir);
    ASSERT_STREQ(r.err, want);
    cmd_result_free(&r);
    {
        const char *args[] = {"--json", "export", "--format", "json", "--out", g.out};
        r = run_remember(g.db, args, sizeof args / sizeof args[0], NULL);
    }
    (void)snprintf(want, sizeof want, "output already exists: %s/a\\nb\\u001b.json\"", g.dir);
    ASSERT_STR_CONTAINS(r.out, want);
    cmd_result_free(&r);
    (void)snprintf(longpath, sizeof longpath, "%s/%0150d/%0150d/brain.json", g.dir, 0, 1);
    {
        const char *args[] = {"export", "--format", "json", "--out", longpath};
        r = run_remember(g.db, args, sizeof args / sizeof args[0], NULL);
    }
    (void)snprintf(want, sizeof want,
                   "remember: the folder for \"%s\" doesn't exist. Create the folder, or export "
                   "to another path.\n",
                   longpath);
    ASSERT_STREQ(r.err, want);
    cmd_result_free(&r);
    given_free(&g);
}

static int cmp_name(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

/* line and a newline at buf + *w. *w grows only by what was written, so a
   listing that does not fit fails the test and never runs past cap. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) in the order the names say
static void append_line(char *buf, size_t cap, size_t *w, const char *line)
{
    int k = snprintf(buf + *w, cap - *w, "%s\n", line);

    ASSERT_TRUE(k >= 0 && (size_t)k < cap - *w);
    if (k > 0 && (size_t)k < cap - *w) {
        *w += (size_t)k;
    }
}

/* Every regular file under dir, as "<sub>/<name>" lines sorted, or "" when none. */
static void list_tree(const char *dir, char *buf, size_t cap)
{
    static const char *const subs[] = {".", "deleted", "expired", "memories"};
    char names[TREE_FILES][NAME_BUF];
    size_t n = 0;
    size_t i = 0;
    size_t w = 0;

    for (i = 0; i < sizeof subs / sizeof subs[0]; i++) {
        char path[PATH_BUF];
        DIR *d = NULL;
        const struct dirent *ent = NULL;

        (void)snprintf(path, sizeof path, "%s/%s", dir, subs[i]);
        d = opendir(path);
        ASSERT_TRUE(d != NULL);
        if (d == NULL) {
            continue;
        }
        // NOLINTNEXTLINE(concurrency-mt-unsafe) single-threaded test
        while ((ent = readdir(d)) != NULL && n < (size_t)TREE_FILES) {
            char file[PATH_BUF];
            struct stat st;

            /* d_type is not in the POSIX dirent this tree builds with. */
            (void)snprintf(file, sizeof file, "%s/%s", path, ent->d_name);
            if (stat(file, &st) == 0 && S_ISREG(st.st_mode)) {
                (void)snprintf(names[n++], sizeof names[0], "%s/%s", subs[i], ent->d_name);
            }
        }
        (void)closedir(d);
    }
    qsort(names, n, sizeof names[0], cmp_name);
    buf[0] = '\0';
    for (i = 0; i < n; i++) {
        append_line(buf, cap, &w, names[i]);
    }
}

static char *read_in(const char *dir, const char *name)
{
    char path[PATH_BUF];
    size_t len = 0U;

    (void)snprintf(path, sizeof path, "%s/%s", dir, name);
    return harness_read_file(path, &len);
}

#define README_D30                                                                                 \
    "This folder is a Fortkeeps export. Import it with: remember import --from-md <folder>\n"

/* D29 folder: README, the bin folders, <slug>--<last 12 hex>.md per memory. */
TEST(export_md_folder_every_bin)
{
    Given g = given_fixture("brain-md");
    char tree[TREE_BUF];
    char *text = NULL;
    CmdResult r;

    {
        const char *args[] = {
            "export", "--format", "md", "--out", g.out, "--include-expired", "--include-trash"};
        r = export_run(g.db, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    list_tree(g.out, tree, sizeof tree);
    ASSERT_STREQ(tree, "./README.txt\n"
                       "deleted/gone--000000000040.md\n"
                       "expired/old-thing--000000000030.md\n"
                       "memories/one-two-three-four-five-six--000000000020.md\n"
                       "memories/project-alpha--000000000003.md\n"
                       "memories/weekly-notes--000000000010.md\n");
    text = read_in(g.out, "README.txt");
    ASSERT_STREQ(text, README_D30 "Bins: live 3, expired 1, deleted 1.\n");
    free(text);
    text = read_in(g.out, "memories/project-alpha--000000000003.md");
    ASSERT_STREQ(
        text,
        "---\n"
        "remember_export: 1\n"
        "id: \"" FIXTURE_ID(
            "000000000003") "\"\n"
                            "key: \"project:alpha\"\n"
                            "tags: [\"alpha\",\"beta\",\"zeta\"]\n"
                            "created_by: " HUMAN_ACTOR "\n"
                            "updated_by: {\"kind\":\"agent\",\"id\":\"bot-1\"}\n"
                            "created_at: \"" FIXTURE_AT "\"\n"
                            "updated_at: \"" FIXTURE_AT "\"\n"
                            "expires_at: null\n"
                            "deleted_at: null\n"
                            "version_vector: {\"" FIXTURE_ID("000000000001") "\":5,\"" FIXTURE_ID(
                                "0000000000ff") "\":2}\n"
                                                "links: [{\"to\":\"" FIXTURE_ID(
                                                    "000000000010") "\",\"kind\":\"cites\"," TIMES
                                                                    "},{\"to\":\"" FIXTURE_ID(
                                                                        "000000000030") "\","
                                                                                        "\"kind\":"
                                                                                        "\"related"
                                                                                        "\"," TIMES
                                                                                        "},{\"to\":"
                                                                                        "\"" FIXTURE_ID(
                                                                                            "000000"
                                                                                            "00004"
                                                                                            "0") "\",\"kind\":\"supersedes\"," TIMES
                                                                                                 "}"
                                                                                                 "]"
                                                                                                 "\n"
                                                                                                 "-"
                                                                                                 "-"
                                                                                                 "-"
                                                                                                 "\n"
                                                                                                 "A"
                                                                                                 "l"
                                                                                                 "p"
                                                                                                 "h"
                                                                                                 "a"
                                                                                                 " "
                                                                                                 "\"quoted\", body\nline two\n");
    free(text);
    text = read_in(g.out, "deleted/gone--000000000040.md");
    ASSERT_STR_CONTAINS(text, "\nexpires_at: null\ndeleted_at: \"" FIXTURE_AT "\"\n");
    ASSERT_STR_CONTAINS(text, "\nlinks: []\n---\nDeleted memory.\n");
    free(text);
    /* A related edge is listed on its stored from end only (D25). */
    text = read_in(g.out, "memories/one-two-three-four-five-six--000000000020.md");
    ASSERT_STR_CONTAINS(text, "\nlinks: []\n");
    free(text);
    given_free(&g);
}

/* --include-* off: the bin folders still exist, empty; README lists live only. */
TEST(export_md_live_only_keeps_three_folders)
{
    Given g = given_fixture("brain-md");
    char tree[TREE_BUF];
    char *text = NULL;
    struct stat st;
    CmdResult r;

    {
        const char *args[] = {"export", "--format", "md", "--out", g.out};
        r = export_run(g.db, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    list_tree(g.out, tree, sizeof tree);
    ASSERT_STREQ(tree, "./README.txt\n"
                       "memories/one-two-three-four-five-six--000000000020.md\n"
                       "memories/project-alpha--000000000003.md\n"
                       "memories/weekly-notes--000000000010.md\n");
    text = read_in(g.out, "README.txt");
    ASSERT_STREQ(text, README_D30 "Bins: live 3.\n");
    free(text);
    /* decision:plan22-export-file-mode */
    ASSERT_EQ_INT(stat(g.out, &st), 0);
    ASSERT_EQ_INT((int)((unsigned)st.st_mode & (unsigned)MODE_MASK), DIR_MODE);
    text = harness_side_path(g.out, "/README.txt");
    ASSERT_EQ_INT(stat(text, &st), 0);
    ASSERT_EQ_INT((int)((unsigned)st.st_mode & (unsigned)MODE_MASK), FILE_MODE);
    free(text);
    given_free(&g);
}

/* D17: a crash between the renames leaves the old folder and the new one whole,
   each under its own name; a later --force swaps cleanly. */
TEST(export_md_force_survives_a_crash)
{
    Given g = given_fixture("brain-md");
    char *text = NULL;
    CmdResult r;
    const char *base[] = {"export", "--format", "md", "--out", g.out};
    const char *force[] = {"export", "--format", "md", "--out", g.out, "--force"};

    r = export_run(g.db, base, sizeof base / sizeof base[0]);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    write_side(g.out, "/memories/marker.txt", "old\n");
    r = export_run(g.db, base, sizeof base / sizeof base[0]);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "output already exists: ");
    cmd_result_free(&r);
    /* No crash: --force swaps the new folder in and leaves no sibling. */
    r = export_run(g.db, force, sizeof force / sizeof force[0]);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    text = read_in(g.out, "memories/marker.txt");
    ASSERT_TRUE(text == NULL);
    ASSERT_EQ_INT(count_strays(g.dir), 0);
    write_side(g.out, "/memories/marker.txt", "old\n");
    // NOLINTBEGIN(concurrency-mt-unsafe) the child inherits this process-global
    (void)setenv("REMEMBER_TEST_EXPORT_CRASH", "1", 1);
    r = export_run(g.db, force, sizeof force / sizeof force[0]);
    (void)unsetenv("REMEMBER_TEST_EXPORT_CRASH");
    // NOLINTEND(concurrency-mt-unsafe)
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    /* The path is gone, never half-written; one old and one new sibling remain. */
    ASSERT_FALSE(access(g.out, F_OK) == 0);
    ASSERT_EQ_INT(count_strays(g.dir), 2);
    {
        DIR *d = opendir(g.dir);
        const struct dirent *ent = NULL;
        int old_ok = 0;
        int new_ok = 0;

        // NOLINTNEXTLINE(concurrency-mt-unsafe) single-threaded test
        while (d != NULL && (ent = readdir(d)) != NULL) {
            char path[PATH_BUF];

            (void)snprintf(path, sizeof path, "%s/%s", g.dir, ent->d_name);
            if (strstr(ent->d_name, ".old-") != NULL) {
                text = read_in(path, "memories/marker.txt");
                old_ok = (text != NULL && strcmp(text, "old\n") == 0);
                free(text);
            }
            if (strstr(ent->d_name, ".tmp-") != NULL) {
                char tree[TREE_BUF];

                list_tree(path, tree, sizeof tree);
                new_ok = strcmp(tree, "./README.txt\n"
                                      "memories/one-two-three-four-five-six--000000000020.md\n"
                                      "memories/project-alpha--000000000003.md\n"
                                      "memories/weekly-notes--000000000010.md\n") == 0;
            }
        }
        if (d != NULL) {
            (void)closedir(d);
        }
        ASSERT_EQ_INT(old_ok, 1);
        ASSERT_EQ_INT(new_ok, 1);
    }
    /* The path is free again, so the next export writes it; the siblings stay. */
    r = export_run(g.db, force, sizeof force / sizeof force[0]);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    ASSERT_TRUE(access(g.out, F_OK) == 0);
    ASSERT_EQ_INT(count_strays(g.dir), 2);
    given_free(&g);
}

/* A hand-edited id names no path: the file name keeps [0-9a-z] of the id's
   tail, and the front matter carries the id as stored. */
TEST(export_md_hostile_id_stays_in_its_folder)
{
    Given g = given_fixture("brain-md");
    const char *args[] = {"export", "--format", "md", "--out", g.out};
    char *text = NULL;
    CmdResult r;

    free(harness_sqlite_query_line(
        g.db, "UPDATE entries SET id = 'zz/.:~A-09afxz' WHERE key = 'project:alpha';"));
    r = export_run(g.db, args, sizeof args / sizeof args[0]);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    text = read_in(g.out, "memories/project-alpha--------09afxz.md");
    ASSERT_TRUE(text != NULL);
    if (text != NULL) {
        ASSERT_STR_CONTAINS(text, "\nid: \"zz/.:~A-09afxz\"\n");
    }
    free(text);
    given_free(&g);
}

/* Without --force, a folder that appears at PATH after the existence check is
   never swapped out: the export fails and the folder keeps its file. */
TEST(export_md_without_force_never_swaps)
{
    Given g = given_fixture("brain-md");
    const char *args[] = {"export", "--format", "md", "--out", g.out};
    char *text = NULL;
    CmdResult r;

    // NOLINTBEGIN(concurrency-mt-unsafe) the child inherits this process-global
    (void)setenv("REMEMBER_TEST_EXPORT_RACE", "1", 1);
    r = export_run(g.db, args, sizeof args / sizeof args[0]);
    (void)unsetenv("REMEMBER_TEST_EXPORT_RACE");
    // NOLINTEND(concurrency-mt-unsafe)
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    text = read_in(g.out, "race.txt");
    ASSERT_TRUE(text != NULL && strcmp(text, "keep\n") == 0);
    ASSERT_EQ_INT(count_strays(g.dir), 0);
    free(text);
    given_free(&g);
}

/* --force replaces only a remember export folder
   (decision:plan22-export-md-force-only-export): a folder of other files, a
   folder whose README.txt is not the D30 sentence, and a file stay as they were. */
TEST(export_md_force_replaces_only_an_export)
{
    Given g = given_fixture("precious");
    char file[PATH_BUF];
    const char *paths[] = {g.out, g.out, file};
    size_t i = 0;

    (void)snprintf(file, sizeof file, "%s/notes.md", g.dir);
    ASSERT_EQ_INT(mkdir(g.out, DIR_MODE), 0);
    write_side(g.out, "/keep.txt", "keep\n");
    write_text(file, "mine\n");
    for (i = 0; i < sizeof paths / sizeof paths[0]; i++) {
        const char *args[] = {"export", "--format", "md", "--out", paths[i], "--force"};
        char want[TEXT_BUF];
        CmdResult r;

        if (i == 1U) {
            /* The D30 sentence with one byte changed before its newline. */
            write_side(g.out, "/README.txt",
                       "This folder is a Fortkeeps export. Import it with: remember import "
                       "--from-md <folder>.\n");
        }
        r = export_run(g.db, args, sizeof args / sizeof args[0]);
        (void)snprintf(want, sizeof want,
                       "remember: --force replaces only a remember export folder: %s\n", paths[i]);
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STREQ(r.err, want);
        cmd_result_free(&r);
    }
    {
        char *kept = read_in(g.out, "keep.txt");
        char *mine = read_in(g.dir, "notes.md");

        ASSERT_STREQ(kept, "keep\n");
        ASSERT_STREQ(mine, "mine\n");
        free(kept);
        free(mine);
    }
    ASSERT_EQ_INT(count_strays(g.dir), 0);
    given_free(&g);
}

typedef struct {
    const char *format;
    const char *out;   /* the --out path */
    const char *named; /* the path the sentence names */
} BrainCase;

/* No export lands on the brain or a SQLite file beside it, even with --force
   (decision:plan22-export-out-not-brain): by name, by hard link, and through
   the CSV manifest. */
TEST(export_out_is_never_the_brain)
{
    Given g = given_fixture("x");
    char wal[PATH_BUF];
    char hard[PATH_BUF];
    char csv[PATH_BUF];
    char manifest[PATH_BUF];
    char *before = NULL;
    char *after = NULL;
    size_t len = 0U;
    size_t i = 0;

    (void)snprintf(wal, sizeof wal, "%s-wal", g.db);
    (void)snprintf(hard, sizeof hard, "%s/hard.db", g.dir);
    (void)snprintf(csv, sizeof csv, "%s/side.csv", g.dir);
    (void)snprintf(manifest, sizeof manifest, "%s/side.manifest.json", g.dir);
    ASSERT_EQ_INT(link(g.db, hard), 0);
    ASSERT_EQ_INT(link(g.db, manifest), 0);
    before = harness_read_file(g.db, &len);
    ASSERT_TRUE(before != NULL);
    {
        const BrainCase cases[] = {
            {"json", g.db, g.db}, {"csv", wal, wal},      {"md", g.db, g.db},
            {"json", hard, hard}, {"csv", csv, manifest},
        };

        for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
            const char *args[] = {"export", "--format",   cases[i].format,
                                  "--out",  cases[i].out, "--force"};
            char want[TEXT_BUF];
            CmdResult r = export_run(g.db, args, sizeof args / sizeof args[0]);

            (void)snprintf(want, sizeof want, "remember: --out is the brain file: %s\n",
                           cases[i].named);
            ASSERT_EQ_INT(r.exit_code, 1);
            ASSERT_STREQ(r.err, want);
            cmd_result_free(&r);
        }
    }
    {
        /* A neighbour that only shares the brain's name is not the brain. */
        char bak[PATH_BUF];
        const char *args[] = {"export", "--format", "json", "--out", bak};
        CmdResult r;

        (void)snprintf(bak, sizeof bak, "%s.bak", g.db);
        r = export_run(g.db, args, sizeof args / sizeof args[0]);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
    }
    after = harness_read_file(g.db, &len);
    ASSERT_TRUE(after != NULL && before != NULL && memcmp(after, before, len) == 0);
    ASSERT_EQ_INT(count_strays(g.dir), 0);
    free(before);
    free(after);
    given_free(&g);
}

typedef struct {
    const char *key;
    const char *body;
    const char *name;
} SlugCase;

/* E6.b2 slug: key, else the "# " title, else the first 6 words. Each case's
   derived text differs from its raw input. */
TEST(export_md_slug_rules)
{
    static const SlugCase cases[] = {
        {"Project:Alpha", "# Ignored title", "project-alpha"},
        {"--A__B--", "x", "a-b"},
        {"\xe6\x97\xa5\xe6\x9c\xac", "x", "memory"},
        {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "x",
         "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},
        {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa+b", "x",
         "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},
        {NULL, "# Hello, World! \nbody", "hello-world"},
        {NULL, "#   \nfoo bar", "foo-bar"},
        {NULL, "#Title words", "title-words"},
        {NULL,
         "a\tb\x01"
         "c  d\ne f g h",
         "a-b-c-d-e-f"},
        {NULL, "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d", "memory"},
        {NULL, "  Mixed CASE  words ", "mixed-case-words"},
        /* Not stored in practice: invalid UTF-8 slugs the whole body. */
        {NULL, "ok \xff one two three four five six", "ok-one-two-three-four-five-six"},
    };
    size_t i = 0;

    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        Entry e;
        char want[NAME_BUF];
        char name[EXPORT_MD_NAME_MAX];

        memset(&e, 0, sizeof e);
        e.sync_id = (char *)FIXTURE_ID("0000000000ab");
        e.key = (char *)cases[i].key;
        e.body = (char *)cases[i].body;
        export_md_name(&e, name);
        (void)snprintf(want, sizeof want, "%s--0000000000ab.md", cases[i].name);
        ASSERT_STREQ(name, want);
    }
}

typedef struct {
    const char *site;
    const char *code;
    bool md_only;
    char pad_[FAULT_PAD]; /* explicit tail padding (kept -Wpadded-clean) */
} FaultCase;

/* P5 and E6.6: a failure at any file step, even with --force, leaves the
   earlier export as it was and no temp or old sibling behind. */
TEST(export_every_file_fault_keeps_the_old_output)
{
    static const FaultCase faults[] = {
        {"random", "io_error", false, {0}},   {"alloc", "out_of_memory", false, {0}},
        {"join", "out_of_memory", true, {0}}, {"fdopen", "io_error", false, {0}},
        {"write", "io_error", false, {0}},    {"close", "io_error", false, {0}},
        {"rename", "io_error", false, {0}},   {"rename#2", "io_error", true, {0}},
    };
    static const char *const formats[] = {"json", "csv", "md"};
    size_t f = 0;

    for (f = 0; f < sizeof formats / sizeof formats[0]; f++) {
        Given g = given_fixture("brain.out");
        bool is_md = strcmp(formats[f], "md") == 0;
        const char *args[] = {"--json", "export", "--format", formats[f],
                              "--out",  g.out,    "--force"};
        char *keep = NULL;
        char *before = NULL;
        size_t i = 0;
        size_t len = 0U;
        CmdResult r = export_run(g.db, args, sizeof args / sizeof args[0]);

        if (is_md) {
            keep = harness_side_path(g.out, "/README.txt");
        } else {
            keep = strdup(g.out);
        }
        ASSERT_TRUE(keep != NULL);
        if (keep == NULL) {
            cmd_result_free(&r);
            given_free(&g);
            return;
        }
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
        before = harness_read_file(keep, &len);
        ASSERT_TRUE(before != NULL);
        for (i = 0; i < sizeof faults / sizeof faults[0]; i++) {
            char *now = NULL;
            char want[SMALL_BUF];

            if (faults[i].md_only && !is_md) {
                continue;
            }
            // NOLINTBEGIN(concurrency-mt-unsafe) the child inherits this process-global
            (void)setenv("REMEMBER_TEST_EXPORT_FAIL", faults[i].site, 1);
            r = export_run(g.db, args, sizeof args / sizeof args[0]);
            (void)unsetenv("REMEMBER_TEST_EXPORT_FAIL");
            // NOLINTEND(concurrency-mt-unsafe)
            (void)snprintf(want, sizeof want, "\"code\":\"%s\"", faults[i].code);
            now = harness_read_file(keep, &len);
            if (r.exit_code != 1 || strstr(r.out, want) == NULL || now == NULL || before == NULL ||
                strcmp(now, before) != 0 || count_strays(g.dir) != 0) {
                (void)fprintf(stderr, "  format %s, site %s\n", formats[f], faults[i].site);
            }
            ASSERT_EQ_INT(r.exit_code, 1);
            ASSERT_STR_CONTAINS(r.out, want);
            ASSERT_TRUE(now != NULL && before != NULL && strcmp(now, before) == 0);
            ASSERT_EQ_INT(count_strays(g.dir), 0);
            free(now);
            cmd_result_free(&r);
        }
        free(before);
        free(keep);
        given_free(&g);
    }
}

/* A store read failing at any point, the snapshot included, writes nothing:
   every read count up to the first export that succeeds is swept. */
TEST(export_store_failure_writes_nothing)
{
    enum { MAX_READS = 64 };
    Given g = given_fixture("brain.json");
    const char *args[] = {"export", "--format", "json", "--out", g.out};
    int n = 0;
    int rc = 1;

    for (n = 0; n < MAX_READS && rc != 0; n++) {
        char count[COUNT_BUF];
        CmdResult r;

        (void)snprintf(count, sizeof count, "%d", n);
        // NOLINTBEGIN(concurrency-mt-unsafe) the child inherits this process-global
        (void)setenv("REMEMBER_TEST_FAIL_READ", count, 1);
        r = export_run(g.db, args, sizeof args / sizeof args[0]);
        (void)unsetenv("REMEMBER_TEST_FAIL_READ");
        // NOLINTEND(concurrency-mt-unsafe)
        rc = r.exit_code;
        if (rc != 0) {
            ASSERT_EQ_INT(rc, 1);
            ASSERT_STR_CONTAINS(r.err, "database error:");
            ASSERT_FALSE(access(g.out, F_OK) == 0);
        }
        cmd_result_free(&r);
    }
    ASSERT_EQ_INT(rc, 0);
    ASSERT_EQ_INT(count_strays(g.dir), 0);
    given_free(&g);
}

/* CSV cells hold JSON with each '"' doubled (RFC 4180). */
#define CSV_AT "\"" FIXTURE_AT "\""
#define CSV_HUMAN "\"{\"\"kind\"\":\"\"human\"\",\"\"id\"\":null}\""
#define CSV_VV(n) "\"{\"\"" FIXTURE_ID("000000000001") "\"\":" n "}\""
#define CSV_LINK(to, kind)                                                                         \
    "{\"\"to\"\":\"\"" FIXTURE_ID(to) "\"\",\"\"kind\"\":\"\"" kind                                \
                                      "\"\",\"\"created_at\"\":\"\"" FIXTURE_AT                    \
                                      "\"\",\"\"updated_at\"\":\"\"" FIXTURE_AT "\"\"}"
#define CSV_HEADER                                                                                 \
    "\xef\xbb\xbf"                                                                                 \
    "id,key,body,tags,created_by,updated_by,created_at,updated_at,expires_at,deleted_at,bin,"      \
    "version_vector,links\r\n"

#define CSV_ID(hex) "\"" FIXTURE_ID(hex) "\""
#define CSV_TIMES CSV_AT "," CSV_AT
#define CSV_ALPHA_VV                                                                               \
    "\"{\"\"" FIXTURE_ID("000000000001") "\"\":5,\"\"" FIXTURE_ID("0000000000ff") "\"\":2}\""
#define CSV_ALPHA_LINKS                                                                            \
    "\"[" CSV_LINK("000000000010", "cites") "," CSV_LINK("000000000030", "related") "," CSV_LINK(  \
        "000000000040", "supersedes") "]\""
#define CSV_ROW_ALPHA                                                                              \
    CSV_ID("000000000003")                                                                         \
    ",\"project:alpha\",\"Alpha \"\"quoted\"\", body\nline two\","                                 \
    "\"[\"\"alpha\"\",\"\"beta\"\",\"\"zeta\"\"]\"," CSV_HUMAN                                     \
    ",\"{\"\"kind\"\":\"\"agent\"\",\"\"id\"\":\"\"bot-1\"\"}\"," CSV_TIMES                        \
    ",,,\"live\"," CSV_ALPHA_VV "," CSV_ALPHA_LINKS "\r\n"
#define CSV_ROW_WEEKLY                                                                             \
    CSV_ID("000000000010")                                                                         \
    ",,\"# Weekly Notes \nBody of the week.\",\"[\"\"a\"\",\"\"b\"\"]\"," CSV_HUMAN "," CSV_HUMAN  \
    "," CSV_TIMES ",,,\"live\"," CSV_VV("1") ",\"[" CSV_LINK("000000000020", "related") "]\"\r\n"
#define CSV_ROW_WORDS                                                                              \
    CSV_ID("000000000020")                                                                         \
    ",,\"one two  three\tfour five six seven eight\",\"[]\"," CSV_HUMAN "," CSV_HUMAN              \
    "," CSV_TIMES ",,,\"live\"," CSV_VV("1") ",\"[]\"\r\n"
#define CSV_ROW_EXPIRED                                                                            \
    CSV_ID("000000000030")                                                                         \
    ",\"old:thing\",\"Expired memory.\",\"[]\"," CSV_HUMAN "," CSV_HUMAN "," CSV_TIMES             \
    ",\"2025-06-01T00:00:00.000Z\",,\"expired\"," CSV_VV("1") ",\"[]\"\r\n"
#define CSV_ROW_GONE                                                                               \
    CSV_ID("000000000040")                                                                         \
    ",\"gone\",\"Deleted memory.\",\"[]\"," CSV_HUMAN "," CSV_HUMAN "," CSV_TIMES ",," CSV_AT      \
    ",\"deleted\"," CSV_VV("2") ",\"[]\"\r\n"

/* JE-8.3: BOM, the exact header, CRLF, every cell quoted, NULL empty, JSON
   cells, the Markdown-shaped links cell. No #settings row. */
TEST(export_csv_every_bin)
{
    Given g = given_fixture("brain.csv");
    char *text = NULL;
    char *manifest = NULL;
    CmdResult r;

    {
        const char *args[] = {
            "export", "--format", "csv", "--out", g.out, "--include-expired", "--include-trash"};
        r = export_run(g.db, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    text = read_in(g.dir, "brain.csv");
    ASSERT_STREQ(
        text, CSV_HEADER CSV_ROW_ALPHA CSV_ROW_WEEKLY CSV_ROW_WORDS CSV_ROW_EXPIRED CSV_ROW_GONE);
    free(text);
    /* decision:plan22-export-bins-header: brain.csv -> brain.manifest.json */
    manifest = read_in(g.dir, "brain.manifest.json");
    ASSERT_STREQ(manifest, DOC_HEAD "\"bins\":{\"live\":3,\"expired\":1,\"deleted\":1}}\n");
    free(manifest);
    given_free(&g);
}

typedef struct {
    const char *out;
    const char *manifest;
} ManifestName;

/* decision:plan22-export-csv-formula-escape: a cell that starts with = + - @
   TAB CR or ' gets one leading '; any other cell is as stored. */
TEST(export_csv_escapes_formula_cells)
{
    Given g = given_fixture("brain.csv");
    static const char *const bodies[] = {"=SUM(1)", "+1",   "- item", "@a",    "'q",
                                         "''=x",    "tabx", "crx",    "emptyx"};
    static const char *const cells[] = {
        ",\"'=SUM(1)\",", ",\"'+1\",",    ",\"'- item\",", ",\"'@a\",",           ",\"''q\",",
        ",\"'''=x\",",    ",\"'\ttab\",", ",\"'\rcr\",",   ",\"project:alpha\",", ",\"\",",
    };
    const char *args[] = {"export", "--format", "csv", "--out", g.out};
    char *text = NULL;
    size_t i = 0;
    CmdResult r;

    for (i = 0; i < sizeof bodies / sizeof bodies[0]; i++) {
        const char *add[] = {"add", "--", bodies[i]};

        r = run_remember(g.db, add, sizeof add / sizeof add[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
    }
    /* A peer or a hand edit can store a leading TAB or CR, or an empty body;
       add trims or refuses them. An empty cell gets no prefix. */
    free(harness_sqlite_query_line(
        g.db, "UPDATE entries SET body = char(9) || 'tab' WHERE body = 'tabx';"));
    free(harness_sqlite_query_line(
        g.db, "UPDATE entries SET body = char(13) || 'cr' WHERE body = 'crx';"));
    free(harness_sqlite_query_line(g.db, "UPDATE entries SET body = '' WHERE body = 'emptyx';"));
    r = export_run(g.db, args, sizeof args / sizeof args[0]);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    text = read_in(g.dir, "brain.csv");
    ASSERT_TRUE(text != NULL);
    for (i = 0; text != NULL && i < sizeof cells / sizeof cells[0]; i++) {
        ASSERT_STR_CONTAINS(text, cells[i]);
    }
    free(text);
    given_free(&g);
}

/* The basename drops its last extension only; a leading dot is no extension. */
TEST(export_csv_manifest_names)
{
    static const ManifestName cases[] = {
        {"brain", "brain.manifest.json"},
        {"brain.tar.csv", "brain.tar.manifest.json"},
        {".csv", ".csv.manifest.json"},
        {"d.x/brain", "d.x/brain.manifest.json"},
    };
    Given g = given_fixture("d.x");
    size_t i = 0;

    ASSERT_EQ_INT(mkdir(g.out, 0700), 0);
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char out[PATH_BUF];
        char *text = NULL;
        const char *args[] = {"export", "--format", "csv", "--out", out};
        CmdResult r;

        (void)snprintf(out, sizeof out, "%s/%s", g.dir, cases[i].out);
        r = export_run(g.db, args, sizeof args / sizeof args[0]);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
        text = read_in(g.dir, cases[i].manifest);
        ASSERT_TRUE(text != NULL);
        free(text);
    }
    given_free(&g);
}

/* E6.6: an existing manifest without --force writes neither file; with
   --force both are replaced. A folder at the manifest path fails even then. */
TEST(export_csv_existing_manifest)
{
    Given g = given_fixture("brain.csv");
    char *manifest = harness_side_path(g.dir, "/brain.manifest.json");
    char *text = NULL;
    const char *base[] = {"export", "--format", "csv", "--out", g.out};
    const char *force[] = {"--json", "export", "--format", "csv", "--out", g.out, "--force"};
    char want[TEXT_BUF];
    CmdResult r;

    write_text(manifest, "old\n");
    r = export_run(g.db, base, sizeof base / sizeof base[0]);
    (void)snprintf(want, sizeof want, "remember: output already exists: %s\n", manifest);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, want);
    cmd_result_free(&r);
    ASSERT_FALSE(access(g.out, F_OK) == 0);
    r = export_run(g.db, force, sizeof force / sizeof force[0]);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    text = read_in(g.dir, "brain.manifest.json");
    ASSERT_STR_CONTAINS(text, "{\"format\":\"remember-export\"");
    free(text);
    ASSERT_EQ_INT(remove(manifest), 0);
    ASSERT_EQ_INT(mkdir(manifest, 0700), 0);
    r = export_run(g.db, force, sizeof force / sizeof force[0]);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.out, "\"reason\":\"is_directory\"");
    cmd_result_free(&r);
    ASSERT_EQ_INT(rmdir(manifest), 0);
    free(manifest);
    given_free(&g);
}

/* help lists export; help export names every flag. */
TEST(export_help)
{
    char *db = make_temp_db_path();
    const char *general[] = {"--help"};
    const char *topic[] = {"help", "export"};
    static const char *const flags[] = {
        "--format FMT",
        "--out PATH",
        "--include-expired",
        "--include-trash",
        "--exclude-archived",
        "--force             Replace PATH when it exists (md: an export only)",
        "memory.export.result"};
    size_t i = 0;
    CmdResult r = run_remember(db, general, sizeof general / sizeof general[0], NULL);

    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out,
                        "\n  export      Write the brain to a JSON, Markdown, or CSV file\n");
    cmd_result_free(&r);
    r = run_remember(db, topic, sizeof topic / sizeof topic[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out,
                        "remember export - Write the brain to a JSON, Markdown, or CSV file\n");
    for (i = 0; i < sizeof flags / sizeof flags[0]; i++) {
        ASSERT_STR_CONTAINS(r.out, flags[i]);
    }
    cmd_result_free(&r);
    free(db);
}

/* A Markdown export into a missing folder is cannot_open (missing); nothing stays. */
TEST(export_md_missing_folder)
{
    Given g = given_fixture("brain-md");
    char out[PATH_BUF];
    char want[TEXT_BUF];
    const char *args[] = {"--json", "export", "--format", "md", "--out", out};
    CmdResult r;

    (void)snprintf(out, sizeof out, "%s/no-such-folder/md", g.dir);
    r = export_run(g.db, args, sizeof args / sizeof args[0]);
    (void)snprintf(want, sizeof want,
                   "\"detail\":{\"errno\":2,\"path\":\"%s\",\"reason\":\"missing\"}", out);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.out, "\"code\":\"cannot_open\"");
    ASSERT_STR_CONTAINS(r.out, want);
    ASSERT_EQ_INT(count_strays(g.dir), 0);
    cmd_result_free(&r);
    given_free(&g);
}

#define EXAMPLE_AT "2026-10-25T18:00:00.000Z"
_Static_assert(sizeof EXAMPLE_AT == sizeof FIXTURE_AT, "clock swap stays the same width");
#define SETTINGS_TOMBSTONE                                                                         \
    "{\"format\":\"remember-settings-export\",\"version\":1,\"exported_at\":\"" EXAMPLE_AT         \
    "\",\"settings\":[{\"key\":\"remember.search.candidate_pool\",\"scope\":\"brain\",\"value\":"  \
    "400,\"updated_by\":{\"kind\":\"human\",\"id\":null},\"updated_at\":\"2026-10-24T09:12:00."    \
    "000Z\",\"device_id\":\"01a0d90a-e71c-7a0a-bb2c-3b377482fa51\",\"version_vector\":{"           \
    "\"01a0d90a-e71c-7a0a-bb2c-3b377482fa51\":3},\"deleted_at\":\"2026-10-24T09:12:00.000Z\"}]}"

/* The settings document uses the export clock. The example uses EXAMPLE_AT. */
static void use_example_time(char *doc)
{
    char *hit = NULL;

    if (doc == NULL) {
        return;
    }
    hit = strstr(doc, FIXTURE_AT);
    ASSERT_TRUE(hit != NULL);
    if (hit == NULL) {
        return;
    }
    /* Same width as FIXTURE_AT. The buffer's own terminator stays. */
    // NOLINTNEXTLINE(bugprone-not-null-terminated-result)
    memcpy(hit, EXAMPLE_AT, strlen(EXAMPLE_AT));
    ASSERT_TRUE(strstr(doc, FIXTURE_AT) == NULL);
}

/* The nested settings object in a remember-export document. */
static char *nested_settings(const char *doc)
{
    const char *marker = "\"settings\":";
    const char *start = NULL;
    size_t n = 0;
    char *out = NULL;

    if (doc == NULL) {
        return NULL;
    }
    start = strstr(doc, marker);
    if (start == NULL) {
        return NULL;
    }
    start += strlen(marker);
    n = strlen(start);
    if (n < 2U || start[n - 1U] != '\n' || start[n - 2U] != '}') {
        return NULL;
    }
    n -= 2U;
    out = (char *)malloc(n + 1U);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, start, n);
    out[n] = '\0';
    return out;
}

/* ST-8.3: one tombstone is the example document in all three formats. */
TEST(export_settings_tombstone_matches_example)
{
    Given g = given_tombstone("brain.csv");
    char *json_path = harness_side_path(g.dir, "/brain.json");
    char *md_path = harness_side_path(g.dir, "/brain-md");
    char *doc = NULL;
    char *settings = NULL;
    char *csv = NULL;
    char tree[TREE_BUF];
    CmdResult r;

    ASSERT_TRUE(json_path != NULL && md_path != NULL);
    {
        const char *args[] = {"export",  "--format",          "json",           "--out",
                              json_path, "--include-expired", "--include-trash"};
        r = export_run(g.db, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    doc = read_in(g.dir, "brain.json");
    ASSERT_TRUE(doc != NULL);
    ASSERT_STR_NOT_CONTAINS(doc, "sync_id");
    ASSERT_STR_NOT_CONTAINS(doc, "local_id");
    ASSERT_STR_NOT_CONTAINS(doc, "\"source\"");
    ASSERT_STR_NOT_CONTAINS(doc, "\"merge\"");
    settings = nested_settings(doc);
    ASSERT_TRUE(settings != NULL);
    use_example_time(settings);
    ASSERT_STREQ(settings, SETTINGS_TOMBSTONE);
    free(settings);
    free(doc);
    {
        const char *args[] = {
            "export", "--format", "md", "--out", md_path, "--include-expired", "--include-trash"};
        r = export_run(g.db, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    list_tree(md_path, tree, sizeof tree);
    ASSERT_STR_CONTAINS(tree, "settings.json\n");
    settings = read_in(md_path, "settings.json");
    ASSERT_TRUE(settings != NULL);
    use_example_time(settings);
    ASSERT_STREQ(settings, SETTINGS_TOMBSTONE "\n");
    free(settings);
    {
        const char *args[] = {
            "export", "--format", "csv", "--out", g.out, "--include-expired", "--include-trash"};
        r = export_run(g.db, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    csv = read_in(g.dir, "brain.csv");
    ASSERT_TRUE(csv != NULL);
    ASSERT_STR_NOT_CONTAINS(csv, "#settings");
    ASSERT_STREQ(
        csv, CSV_HEADER CSV_ROW_ALPHA CSV_ROW_WEEKLY CSV_ROW_WORDS CSV_ROW_EXPIRED CSV_ROW_GONE);
    free(csv);
    settings = read_in(g.dir, "brain.settings.json");
    ASSERT_TRUE(settings != NULL);
    use_example_time(settings);
    ASSERT_STREQ(settings, SETTINGS_TOMBSTONE "\n");
    free(settings);
    free(json_path);
    free(md_path);
    given_free(&g);
}

/* No rows: JSON has an empty list, and Markdown and CSV write no settings file. */
TEST(export_settings_empty_omits_the_files)
{
    Given g = given_fixture("brain.csv");
    char *md_dir = harness_side_path(g.dir, "/md");
    char *old = NULL;
    char tree[TREE_BUF];
    CmdResult r;

    write_side(g.dir, "/brain.settings.json", "OLD\n");
    {
        const char *args[] = {"export", "--format", "csv", "--out", g.out};
        r = export_run(g.db, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    old = read_in(g.dir, "brain.settings.json");
    ASSERT_STREQ(old, "OLD\n");
    free(old);
    {
        const char *args[] = {"export", "--format", "md", "--out", md_dir};
        r = export_run(g.db, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    list_tree(md_dir, tree, sizeof tree);
    ASSERT_STR_NOT_CONTAINS(tree, "settings.json");
    free(md_dir);
    given_free(&g);
}

/* An existing CSV settings file without --force writes neither file. */
TEST(export_settings_existing_file_without_force)
{
    Given g = given_tombstone("brain.csv");
    char *csv = NULL;
    CmdResult r;

    write_side(g.dir, "/brain.settings.json", "OLD\n");
    {
        const char *args[] = {"export", "--format", "csv", "--out", g.out};
        r = export_run(g.db, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "output already exists: ");
    ASSERT_STR_CONTAINS(r.err, "brain.settings.json");
    cmd_result_free(&r);
    csv = read_in(g.dir, "brain.csv");
    ASSERT_TRUE(csv == NULL);
    free(csv);
    csv = read_in(g.dir, "brain.settings.json");
    ASSERT_STREQ(csv, "OLD\n");
    free(csv);
    given_free(&g);
}

/* A user.db row is not a brain row, so it is not in the export. */
TEST(export_settings_skips_user_rows)
{
    Given g = given_fixture("brain.json");
    char *user = harness_side_path(g.db, ".user.db");
    char *saved = NULL;
    // NOLINTNEXTLINE(concurrency-mt-unsafe) the child inherits this process-global
    const char *env = getenv("REMEMBER_USER_DB");
    char *doc = NULL;
    CmdResult r;

    ASSERT_TRUE(user != NULL);
    if (env != NULL) {
        saved = strdup(env);
        ASSERT_TRUE(saved != NULL);
    }
    // NOLINTNEXTLINE(concurrency-mt-unsafe) the child inherits this process-global
    ASSERT_EQ_INT(setenv("REMEMBER_USER_DB", user, 1), 0);
    {
        const char *args[] = {"settings", "set", "--scope", "user", "remember.plugins.chain_budget",
                              "2s"};
        r = run_remember(g.db, args, sizeof args / sizeof args[0], NULL);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    {
        const char *args[] = {"export", "--format", "json", "--out", g.out};
        r = export_run(g.db, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    doc = read_in(g.dir, "brain.json");
    ASSERT_STR_CONTAINS(doc, SETTINGS_EMPTY);
    ASSERT_STR_NOT_CONTAINS(doc, "chain_budget");
    free(doc);
    if (saved != NULL) {
        // NOLINTNEXTLINE(concurrency-mt-unsafe) the child inherits this process-global
        ASSERT_EQ_INT(setenv("REMEMBER_USER_DB", saved, 1), 0);
    } else {
        // NOLINTNEXTLINE(concurrency-mt-unsafe) the child inherits this process-global
        ASSERT_EQ_INT(unsetenv("REMEMBER_USER_DB"), 0);
    }
    free(saved);
    free(user);
    given_free(&g);
}

/* A fault on the settings file itself leaves the earlier export in place. */
TEST(export_settings_file_fault_keeps_the_old_output)
{
    Given g = given_tombstone("brain.csv");
    char *md_dir = harness_side_path(g.dir, "/md");
    char *before = NULL;
    char *now = NULL;
    CmdResult r;

    {
        const char *args[] = {"export", "--format", "csv", "--out", g.out};
        r = export_run(g.db, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    before = read_in(g.dir, "brain.settings.json");
    ASSERT_TRUE(before != NULL);
    // NOLINTBEGIN(concurrency-mt-unsafe) the child inherits this process-global
    (void)setenv("REMEMBER_TEST_EXPORT_FAIL", "write#3", 1);
    {
        const char *args[] = {"--json", "export", "--format", "csv", "--out", g.out, "--force"};
        r = export_run(g.db, args, sizeof args / sizeof args[0]);
    }
    (void)unsetenv("REMEMBER_TEST_EXPORT_FAIL");
    // NOLINTEND(concurrency-mt-unsafe)
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.out, "\"code\":\"io_error\"");
    ASSERT_STR_CONTAINS(r.out, "brain.settings.json");
    cmd_result_free(&r);
    now = read_in(g.dir, "brain.settings.json");
    ASSERT_STREQ(now, before);
    ASSERT_EQ_INT(count_strays(g.dir), 0);
    free(now);
    {
        const char *args[] = {"export", "--format", "md", "--out", md_dir};
        r = export_run(g.db, args, sizeof args / sizeof args[0]);
    }
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    free(before);
    before = read_in(md_dir, "settings.json");
    // NOLINTBEGIN(concurrency-mt-unsafe) the child inherits this process-global
    (void)setenv("REMEMBER_TEST_EXPORT_FAIL", "write#2", 1);
    {
        const char *args[] = {"--json", "export", "--format", "md", "--out", md_dir, "--force"};
        r = export_run(g.db, args, sizeof args / sizeof args[0]);
    }
    (void)unsetenv("REMEMBER_TEST_EXPORT_FAIL");
    // NOLINTEND(concurrency-mt-unsafe)
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.out, "\"code\":\"io_error\"");
    cmd_result_free(&r);
    now = read_in(md_dir, "settings.json");
    ASSERT_STREQ(now, before);
    ASSERT_EQ_INT(count_strays(g.dir), 0);
    free(now);
    free(before);
    free(md_dir);
    given_free(&g);
}

void register_export_tests(void)
{
    RUN_TEST(export_json_every_bin_empty_settings);
    RUN_TEST(export_json_default_is_live_only);
    RUN_TEST(export_force_existing_path);
    RUN_TEST(export_usage_sentences);
    RUN_TEST(export_file_errors_map_codes);
    RUN_TEST(export_human_line);
    RUN_TEST(export_out_too_long_is_a_file_error);
    RUN_TEST(export_out_is_a_folder);
    RUN_TEST(export_missing_brain_is_not_created);
    RUN_TEST(export_exclude_archived_has_no_effect_yet);
    RUN_TEST(export_echoed_path_is_masked);
    RUN_TEST(export_md_folder_every_bin);
    RUN_TEST(export_md_live_only_keeps_three_folders);
    RUN_TEST(export_md_force_survives_a_crash);
    RUN_TEST(export_md_hostile_id_stays_in_its_folder);
    RUN_TEST(export_md_without_force_never_swaps);
    RUN_TEST(export_md_force_replaces_only_an_export);
    RUN_TEST(export_out_is_never_the_brain);
    RUN_TEST(export_md_slug_rules);
    RUN_TEST(export_every_file_fault_keeps_the_old_output);
    RUN_TEST(export_csv_every_bin);
    RUN_TEST(export_csv_escapes_formula_cells);
    RUN_TEST(export_csv_manifest_names);
    RUN_TEST(export_csv_existing_manifest);
    RUN_TEST(export_help);
    RUN_TEST(export_md_missing_folder);
    RUN_TEST(export_store_failure_writes_nothing);
    RUN_TEST(export_settings_tombstone_matches_example);
    RUN_TEST(export_settings_empty_omits_the_files);
    RUN_TEST(export_settings_existing_file_without_force);
    RUN_TEST(export_settings_skips_user_rows);
    RUN_TEST(export_settings_file_fault_keeps_the_old_output);
}
