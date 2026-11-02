#include "harness.h"
#include "register.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> /* mkdtemp on Darwin */

/* One memory with this key and the body "a body"; it is number 1 on a new brain. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void given_one_memory_with_key(const char *db, const char *key)
{
    const char *add[] = {"add", "--key", key, "a body"};
    CmdResult r = run_remember(db, add, sizeof add / sizeof add[0], NULL);

    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
}

TEST(envelope_get_head_is_format_version_type_then_data_memory)
{
    char *db = make_temp_db_path();
    const char *get[] = {"--json", "get", "1"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    given_one_memory_with_key(db, "k");
    r = run_remember(db, get, sizeof get / sizeof get[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_PREFIX(r.out, "{\"format\":\"remember-message\",\"version\":1,"
                             "\"type\":\"memory.get.result\",\"data\":{\"memory\":{");
    cmd_result_free(&r);
    free(db);
}

/* Memory 1 (key hub) is linked to memory 2, and memory 2 is in the Trash. */
static void given_hub_linked_to_a_deleted_memory(const char *db)
{
    const char *const steps[][4] = {{"add", "--key", "hub", "hub body"},
                                    {"add", "other body", NULL, NULL},
                                    {"link", "1", "2", NULL},
                                    {"delete", "2", NULL, NULL}};
    const size_t lens[] = {4, 2, 3, 2};
    size_t i = 0;

    for (i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        CmdResult r = run_remember(db, steps[i], lens[i], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
    }
}

TEST(envelope_add_leaves_out_a_neighbor_in_the_trash_like_get)
{
    char *db = make_temp_db_path();
    const char *add[] = {"--json", "add", "--key", "hub", "new hub body"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    given_hub_linked_to_a_deleted_memory(db);
    r = run_remember(db, add, sizeof add / sizeof add[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"version_vector\":{");
    ASSERT_STR_CONTAINS(r.out, "\"links\":[]}}}\n");
    cmd_result_free(&r);
    free(db);
}

/* A person at the CLI is human with a null id. The memory has no source. */
TEST(envelope_a_new_memory_has_created_by_and_updated_by_and_no_source)
{
    char *db = make_temp_db_path();
    const char *add[] = {"--json", "add", "a body"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, add, sizeof add / sizeof add[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"tags\":[],\"created_by\":{\"kind\":\"human\",\"id\":null},"
                               "\"updated_by\":{\"kind\":\"human\",\"id\":null},\"created_at\":");
    ASSERT_STR_NOT_CONTAINS(r.out, "\"source\"");
    cmd_result_free(&r);
    free(db);
}

enum { TYPE_STEP_ARGS = 7, TYPE_PREFIX_CAP = 128 };

/* One command in order on one brain, and the reply type its envelope names. */
typedef struct {
    const char *args[TYPE_STEP_ARGS];
    const char *type;
} TypeStep;

/* The one reply-type table (reply_noun_verb): every command names its type,
   on success and on failure (the last two fail). */
TEST(envelope_every_command_names_its_reply_type)
{
    static const TypeStep steps[] = {
        {{"--json", "add", "--key", "a", "first body"}, "memory.add"},
        {{"--json", "add", "--key", "b", "second body"}, "memory.add"},
        {{"--json", "search", "body"}, "memory.search"},
        {{"--json", "list"}, "memory.list"},
        {{"--json", "get", "1"}, "memory.get"},
        {{"--json", "update", "1", "--text", "new body"}, "memory.update"},
        {{"--json", "tags"}, "tag.list"},
        {{"--json", "link", "1", "2"}, "memory.link"},
        {{"--json", "related", "1"}, "memory.related"},
        {{"--json", "unlink", "1", "2"}, "memory.unlink"},
        {{"--json", "rekey", "--key", "a", "--to-key", "c"}, "memory.rekey"},
        {{"--json", "delete", "2"}, "memory.delete"},
        {{"--json", "purge", "--deleted"}, "memory.purge"},
        {{"--json", "purge-trash"}, "memory.purge"},
        {{"--json", "conflicts"}, "conflict.list"},
        {{"--json", "device"}, "device.get"},
        {{"--json", "device", "new"}, "device.new"},
        {{"--json", "repair", "search-index"}, "repair.search_index"},
        {{"--json", "conflict", "accept", "--id", "9", "--keep", "local"}, "conflict.accept"},
        {{"--json", "import", "--from-db", "/nonexistent/remember-type.db"}, "memory.import"},
    };
    char *db = make_temp_db_path();
    size_t i = 0;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    for (i = 0; i < sizeof steps / sizeof steps[0]; i++) {
        size_t n = 0;
        char want[TYPE_PREFIX_CAP];
        CmdResult r;

        while (n < (size_t)TYPE_STEP_ARGS && steps[i].args[n] != NULL) {
            n++;
        }
        r = run_remember(db, steps[i].args, n, NULL);
        ASSERT_TRUE((size_t)snprintf(want, sizeof want,
                                     "{\"format\":\"remember-message\",\"version\":1,"
                                     "\"type\":\"%s.result\",",
                                     steps[i].type) < sizeof want);
        ASSERT_STR_PREFIX(r.out, want);
        ASSERT_STREQ(r.err, "");
        cmd_result_free(&r);
    }
    free(db);
}

enum { DROPBOX_PATH_CAP = 4096 };

/* A brain under a Dropbox-named folder, so every open warns synced_folder.
   NULL when the folder cannot be made; the caller frees the path and removes dir. */
static char *given_synced_brain(char *dir_tmpl)
{
    char *path = NULL;

    if (mkdtemp(dir_tmpl) == NULL) {
        return NULL;
    }
    path = (char *)malloc(DROPBOX_PATH_CAP);
    if (path != NULL) {
        (void)snprintf(path, DROPBOX_PATH_CAP, "%s/b.db", dir_tmpl);
    }
    return path;
}

TEST(envelope_failure_keeps_its_type_and_carries_the_warnings_after_the_error)
{
    char dir[] = "/tmp/Dropbox-remember-env-XXXXXX";
    char *db = given_synced_brain(dir);
    const char *list[] = {"--json", "list", "--limit", "0"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, list, sizeof list / sizeof list[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(
        r.out, "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.list.result\","
               "\"error\":{\"code\":\"usage\",\"message\":\"invalid --limit (must be 1..1000)\","
               "\"fix\":\"Run 'remember list --help' to see how to use it.\",\"retryable\":false},"
               "\"warnings\":[{\"code\":\"synced_folder\",\"message\":\"database path looks "
               "like a synced folder (iCloud/Dropbox/Google Drive); corruption risk - prefer "
               "a local disk\"}]}\n");
    cmd_result_free(&r);
    free(db);
    harness_remove_tree(dir);
}

TEST(envelope_warnings_keep_their_order_with_a_comma_between)
{
    char dir[] = "/tmp/Dropbox-remember-env-XXXXXX";
    char *db = given_synced_brain(dir);
    const char *list[] = {"--json", "list", "--trash"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, list, sizeof list / sizeof list[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "\"entries\":[]},\"next_cursor\":null,\"has_more\":false,"
                               "\"warnings\":[{\"code\":\"synced_folder\",");
    ASSERT_STR_CONTAINS(r.out, "local disk\"},{\"code\":\"deprecated_flag\",\"message\":"
                               "\"--trash is deprecated; use --expired\"}]}\n");
    cmd_result_free(&r);
    free(db);
    harness_remove_tree(dir);
}

/* The shim test needs the CLI built with REMEMBER_TEST_VFS (the gate configures it ON).
   setenv/unsetenv drive the shim; the test runner is single-threaded. */
#ifdef REMEMBER_TEST_VFS
// NOLINTBEGIN(concurrency-mt-unsafe)
/* Reads 0..17 succeed; the 19th, the link read after the commit, fails (pinned SQLite;
   N 17..20 all land there). decision:plan21-add-link-read-fails-loud. */
TEST(envelope_add_link_read_error_after_the_write_exits_1_and_keeps_the_write)
{
    char *db = make_temp_db_path();
    const char *add[] = {"--json", "add", "--key", "hub", "new hub body"};
    const char *get[] = {"get", "--key", "hub"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    given_one_memory_with_key(db, "hub");
    ASSERT_EQ_INT(setenv("REMEMBER_TEST_FAIL_READ", "18", 1), 0);
    r = run_remember(db, add, sizeof add / sizeof add[0], NULL);
    ASSERT_EQ_INT(unsetenv("REMEMBER_TEST_FAIL_READ"), 0);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_PREFIX(r.out, "{\"format\":\"remember-message\",\"version\":1,"
                             "\"type\":\"memory.add.result\",\"error\":{\"code\":\"io_error\"");
    ASSERT_STR_CONTAINS(r.out, "SQLITE_IOERR_READ");
    cmd_result_free(&r);
    r = run_remember(db, get, sizeof get / sizeof get[0], NULL);
    ASSERT_STREQ(r.out, "new hub body\n");
    cmd_result_free(&r);
    free(db);
}
// NOLINTEND(concurrency-mt-unsafe)
#endif /* REMEMBER_TEST_VFS */

enum {
    UUID_CANON_LEN = 36,
    UUID_DASH_0 = 8,
    UUID_DASH_1 = 13,
    UUID_DASH_2 = 18,
    UUID_DASH_3 = 23,
    UUID_VER_CHAR = 14
};

/* 1 when s starts with a canonical lowercase UUID v7. */
static int uuid_v7_at(const char *s)
{
    size_t i = 0;

    if (s == NULL || strlen(s) < UUID_CANON_LEN) {
        return 0;
    }
    for (i = 0; i < UUID_CANON_LEN; i++) {
        char c = s[i];

        if (i == (size_t)UUID_DASH_0 || i == (size_t)UUID_DASH_1 || i == (size_t)UUID_DASH_2 ||
            i == (size_t)UUID_DASH_3) {
            if (c != '-') {
                return 0;
            }
            continue;
        }
        if ((c < '0' || c > '9') && (c < 'a' || c > 'f')) {
            return 0;
        }
    }
    return s[UUID_VER_CHAR] == '7';
}

enum { MEMORY_ID_LEN = 36 };

/* Copy data.memory.id. The bytes are the UUID the next get passes back. */
static int copy_memory_id(const char *json, char out[MEMORY_ID_LEN + 1])
{
    const char *key = "\"memory\":{\"id\":\"";
    const char *p = NULL;
    size_t i = 0;

    if (json == NULL) {
        return -1;
    }
    p = strstr(json, key);
    if (p == NULL) {
        return -1;
    }
    p += strlen(key);
    for (i = 0; i < (size_t)MEMORY_ID_LEN; i++) {
        if (p[i] == '\0' || p[i] == '"') {
            return -1;
        }
        out[i] = p[i];
    }
    if (p[MEMORY_ID_LEN] != '"') {
        return -1;
    }
    out[MEMORY_ID_LEN] = '\0';
    return 0;
}

/* 008-13 and the C-01 positional sentence (JE-3.4). get 1 and get <uuid> are
   the same memory. Anything else, including mem_…, is invalid_argument. */
TEST(envelope_get_by_digits_and_by_uuid_is_the_same_memory)
{
    static const char k_plain[] = "remember: \"nope\" isn't a memory id. Use a number from "
                                  "'remember list', a UUID, or --key KEY.\n";
    static const char k_json[] =
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.get.result\","
        "\"error\":{\"code\":\"invalid_argument\",\"message\":\"\\\"nope\\\" isn't a memory id.\","
        "\"fix\":\"Use a number from 'remember list', a UUID, or --key KEY.\","
        "\"retryable\":false}}\n";
    static const char k_mem_plain[] =
        "remember: \"mem_01a14513-6501-7928-b9c4-2cc649cb59a9\" isn't a memory id. Use a number "
        "from 'remember list', a UUID, or --key KEY.\n";
    char *db = make_temp_db_path();
    char id[MEMORY_ID_LEN + 1];
    const char *by_digits[] = {"--json", "get", "1"};
    CmdResult digits;
    CmdResult by_uuid;
    CmdResult bad;

    ASSERT_TRUE(db != NULL);
    given_one_memory_with_key(db, "k");
    digits = run_remember(db, by_digits, sizeof by_digits / sizeof by_digits[0], NULL);
    ASSERT_EQ_INT(digits.exit_code, 0);
    ASSERT_EQ_INT(copy_memory_id(digits.out, id), 0);
    ASSERT_STR_CONTAINS(digits.out, "\"local_id\":1,\"key\":\"k\"");

    {
        const char *args[] = {"--json", "get", id};

        by_uuid = run_remember(db, args, sizeof args / sizeof args[0], NULL);
    }
    ASSERT_EQ_INT(by_uuid.exit_code, 0);
    ASSERT_STR_CONTAINS(by_uuid.out, "\"local_id\":1,\"key\":\"k\"");
    ASSERT_STR_CONTAINS(by_uuid.out, id);
    cmd_result_free(&digits);
    cmd_result_free(&by_uuid);

    {
        const char *args[] = {"get", "nope"};

        bad = run_remember(db, args, sizeof args / sizeof args[0], NULL);
    }
    ASSERT_EQ_INT(bad.exit_code, 1);
    ASSERT_STREQ(bad.err, k_plain);
    ASSERT_STREQ(bad.out, "");
    cmd_result_free(&bad);

    {
        const char *args[] = {"--json", "get", "nope"};

        bad = run_remember(db, args, sizeof args / sizeof args[0], NULL);
    }
    ASSERT_EQ_INT(bad.exit_code, 1);
    ASSERT_STREQ(bad.out, k_json);
    ASSERT_STREQ(bad.err, "");
    cmd_result_free(&bad);

    {
        const char *args[] = {"get", "mem_01a14513-6501-7928-b9c4-2cc649cb59a9"};

        bad = run_remember(db, args, sizeof args / sizeof args[0], NULL);
    }
    ASSERT_EQ_INT(bad.exit_code, 1);
    ASSERT_STREQ(bad.err, k_mem_plain);
    cmd_result_free(&bad);
    free(db);
}

static const char k_locator_fix[] = "Use a number from 'remember list', a UUID, or --key KEY.";

/* The C-01 line for a positional whose bytes are shown unchanged. */
static char *plain_locator_line(const char *value)
{
    int wrote = snprintf(NULL, 0, "remember: \"%s\" isn't a memory id. %s\n", value, k_locator_fix);
    char *buf = NULL;

    if (wrote < 0) {
        return NULL;
    }
    buf = (char *)malloc((size_t)wrote + 1U);
    if (buf == NULL) {
        return NULL;
    }
    if (snprintf(buf, (size_t)wrote + 1U, "remember: \"%s\" isn't a memory id. %s\n", value,
                 k_locator_fix) != wrote) {
        free(buf);
        return NULL;
    }
    return buf;
}

static char *json_locator_line(const char *value)
{
    int wrote = snprintf(NULL, 0,
                         "{\"format\":\"remember-message\",\"version\":1,"
                         "\"type\":\"memory.get.result\",\"error\":{\"code\":\"invalid_argument\","
                         "\"message\":\"\\\"%s\\\" isn't a memory id.\",\"fix\":\"%s\","
                         "\"retryable\":false}}\n",
                         value, k_locator_fix);
    char *buf = NULL;

    if (wrote < 0) {
        return NULL;
    }
    buf = (char *)malloc((size_t)wrote + 1U);
    if (buf == NULL) {
        return NULL;
    }
    if (snprintf(buf, (size_t)wrote + 1U,
                 "{\"format\":\"remember-message\",\"version\":1,"
                 "\"type\":\"memory.get.result\",\"error\":{\"code\":\"invalid_argument\","
                 "\"message\":\"\\\"%s\\\" isn't a memory id.\",\"fix\":\"%s\","
                 "\"retryable\":false}}\n",
                 value, k_locator_fix) != wrote) {
        free(buf);
        return NULL;
    }
    return buf;
}

enum { JSON_ARGV_CAP = 6, LONG_POSITIONAL = 300 };

/* Plain and JSON for one positional. value must need no human mapping or JSON escape. */
static void assert_get_rejects_positional(const char *db, const char *const *human, size_t n,
                                          const char *value)
{
    char *plain = plain_locator_line(value);
    char *json = json_locator_line(value);
    const char *json_argv[JSON_ARGV_CAP];
    CmdResult r;
    size_t i = 0;

    ASSERT_TRUE(plain != NULL);
    ASSERT_TRUE(json != NULL);
    if (plain == NULL || json == NULL) {
        free(plain);
        free(json);
        return;
    }
    r = run_remember(db, human, n, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, plain);
    ASSERT_STREQ(r.out, "");
    cmd_result_free(&r);

    json_argv[0] = "--json";
    for (i = 0; i < n && i + 1U < sizeof json_argv / sizeof json_argv[0]; i++) {
        json_argv[i + 1U] = human[i];
    }
    r = run_remember(db, json_argv, n + 1U, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, json);
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
    free(plain);
    free(json);
}

/* The approved plain line, as a literal, for one command's positional. */
static void assert_nope_is_not_a_memory_id(const char *db, const char *const *args, size_t n)
{
    static const char k_plain[] = "remember: \"nope\" isn't a memory id. Use a number from "
                                  "'remember list', a UUID, or --key KEY.\n";
    CmdResult r = run_remember(db, args, n, NULL);

    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, k_plain);
    ASSERT_STREQ(r.out, "");
    cmd_result_free(&r);
}

/* 008-13 on delete, update, rekey, and both graph ends. Digits 01 are local_id 1. */
TEST(envelope_uuid_positional_updates_deletes_and_links)
{
    char *db = make_temp_db_path();
    char id_a[MEMORY_ID_LEN + 1];
    char id_b[MEMORY_ID_LEN + 1];
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    given_one_memory_with_key(db, "k");
    {
        const char *add[] = {"--json", "add", "other"};

        r = run_remember(db, add, sizeof add / sizeof add[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_EQ_INT(copy_memory_id(r.out, id_b), 0);
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"--json", "get", "01"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_EQ_INT(copy_memory_id(r.out, id_a), 0);
        ASSERT_STR_CONTAINS(r.out, "\"local_id\":1,\"key\":\"k\"");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"update", id_a, "--text", "changed"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"--json", "get", "1"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STR_CONTAINS(r.out, "\"body\":\"changed\"");
        ASSERT_STR_CONTAINS(r.out, id_a);
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"rekey", id_a, "--to-key", "nk"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"--json", "get", "--key", "nk"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STR_CONTAINS(r.out, id_a);
        ASSERT_STR_CONTAINS(r.out, "\"key\":\"nk\"");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"--json", "link", id_a, id_b, "--kind", "cites"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STR_CONTAINS(r.out, "\"action\":\"created\"");
        ASSERT_STR_CONTAINS(r.out, "\"type\":\"cites\"");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"--json", "related", id_a};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STR_CONTAINS(r.out, id_b);
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"unlink", id_a, id_b};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"delete", id_a};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"--json", "get", id_a};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 3);
        ASSERT_STR_CONTAINS(r.out, "\"code\":\"deleted\"");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"--json", "link", "--deleted", "--from", id_b, "--to", id_a};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STR_CONTAINS(r.out, "\"action\":\"created\"");
        cmd_result_free(&r);
    }
    free(db);
}

/* A canonical UUID that is not in the brain is not_found. Anything that is not
   a UUID or digits, including a sign, a space, and mem_, is invalid_argument. */
TEST(envelope_positional_rejects_what_is_not_a_memory_id)
{
    static const char k_missing[] = "018f6b2e-4c3a-7b2e-8f1a-2b3c4d5e6f70";
    static const char k_upper[] = "018F6B2E-4C3A-7B2E-8F1A-2B3C4D5E6F70";
    static const char k_nl_plain[] = "remember: \"nl?\" isn't a memory id. Use a number from "
                                     "'remember list', a UUID, or --key KEY.\n";
    static const char k_nl_json[] =
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.get.result\","
        "\"error\":{\"code\":\"invalid_argument\",\"message\":\"\\\"nl\\n\\\" isn't a memory "
        "id.\",\"fix\":\"Use a number from 'remember list', a UUID, or --key KEY.\","
        "\"retryable\":false}}\n";
    static const char k_esc_plain[] = "remember: \"esc?\" isn't a memory id. Use a number from "
                                      "'remember list', a UUID, or --key KEY.\n";
    static const char k_esc_json[] =
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.get.result\","
        "\"error\":{\"code\":\"invalid_argument\",\"message\":\"\\\"esc\\u001b\\\" isn't a "
        "memory id.\",\"fix\":\"Use a number from 'remember list', a UUID, or --key KEY.\","
        "\"retryable\":false}}\n";
    char *db = make_temp_db_path();
    char braces[MEMORY_ID_LEN + 3];
    char long_id[LONG_POSITIONAL + 1];
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    given_one_memory_with_key(db, "k");
    memset(long_id, 'a', sizeof long_id - 1U);
    long_id[sizeof long_id - 1U] = '\0';
    ASSERT_TRUE(snprintf(braces, sizeof braces, "{%s}", k_missing) > 0);

    {
        const char *human[] = {"get", "0"};

        assert_get_rejects_positional(db, human, sizeof human / sizeof human[0], "0");
    }
    {
        const char *human[] = {"get", "+1"};

        assert_get_rejects_positional(db, human, sizeof human / sizeof human[0], "+1");
    }
    {
        const char *human[] = {"get", " 1"};

        assert_get_rejects_positional(db, human, sizeof human / sizeof human[0], " 1");
    }
    {
        const char *human[] = {"get", ""};

        assert_get_rejects_positional(db, human, sizeof human / sizeof human[0], "");
    }
    {
        const char *human[] = {"get", "--", "-1"};

        assert_get_rejects_positional(db, human, sizeof human / sizeof human[0], "-1");
    }
    {
        const char *human[] = {"get", k_upper};

        assert_get_rejects_positional(db, human, sizeof human / sizeof human[0], k_upper);
    }
    {
        const char *human[] = {"get", braces};

        assert_get_rejects_positional(db, human, sizeof human / sizeof human[0], braces);
    }
    {
        const char *human[] = {"get", long_id};

        assert_get_rejects_positional(db, human, sizeof human / sizeof human[0], long_id);
    }
    {
        char newline_id[] = {'n', 'l', '\n', '\0'};
        const char *human[] = {"get", newline_id};
        const char *json_get[] = {"--json", "get", newline_id};

        r = run_remember(db, human, sizeof human / sizeof human[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STREQ(r.err, k_nl_plain);
        ASSERT_STREQ(r.out, "");
        cmd_result_free(&r);
        r = run_remember(db, json_get, sizeof json_get / sizeof json_get[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STREQ(r.out, k_nl_json);
        ASSERT_STREQ(r.err, "");
        cmd_result_free(&r);
    }
    {
        char escape_id[] = {'e', 's', 'c', '\x1b', '\0'};
        const char *human[] = {"get", escape_id};
        const char *json_get[] = {"--json", "get", escape_id};

        r = run_remember(db, human, sizeof human / sizeof human[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STREQ(r.err, k_esc_plain);
        ASSERT_STREQ(r.out, "");
        cmd_result_free(&r);
        r = run_remember(db, json_get, sizeof json_get / sizeof json_get[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STREQ(r.out, k_esc_json);
        ASSERT_STREQ(r.err, "");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"--json", "get", k_missing};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 2);
        ASSERT_STR_CONTAINS(r.out, "\"code\":\"not_found\"");
        ASSERT_STR_NOT_CONTAINS(r.out, "invalid_argument");
        ASSERT_STREQ(r.err, "");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"delete", k_missing};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 2);
        ASSERT_STR_CONTAINS(r.err, "not found");
        ASSERT_STR_NOT_CONTAINS(r.err, "isn't a memory id");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"link", k_missing, "1"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 2);
        ASSERT_STR_CONTAINS(r.err, "not found");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"link", "--from-key", "missing-key", "--to", "1"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 2);
        ASSERT_STR_CONTAINS(r.err, "not found");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"link", "--deleted", "--from-key", "missing-key", "--to", "1"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 2);
        ASSERT_STR_CONTAINS(r.err, "not found");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"update", k_missing, "--text", "x"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 2);
        ASSERT_STR_CONTAINS(r.err, "not found");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"get", "--offset", "1"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STR_CONTAINS(r.err, "unknown option '--offset'");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"get", "--json-errors"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STR_CONTAINS(r.err, "unknown option '--json-errors'");
        cmd_result_free(&r);
    }
    {
        const char *delete_args[] = {"delete", "nope"};
        const char *update_args[] = {"update", "nope", "--text", "x"};
        const char *rekey_args[] = {"rekey", "nope", "--to-key", "z"};
        const char *link_args[] = {"link", "nope", "1"};
        const char *unlink_args[] = {"unlink", "nope", "1"};
        const char *related_args[] = {"related", "nope"};

        assert_nope_is_not_a_memory_id(db, delete_args, sizeof delete_args / sizeof delete_args[0]);
        assert_nope_is_not_a_memory_id(db, update_args, sizeof update_args / sizeof update_args[0]);
        assert_nope_is_not_a_memory_id(db, rekey_args, sizeof rekey_args / sizeof rekey_args[0]);
        assert_nope_is_not_a_memory_id(db, link_args, sizeof link_args / sizeof link_args[0]);
        assert_nope_is_not_a_memory_id(db, unlink_args, sizeof unlink_args / sizeof unlink_args[0]);
        assert_nope_is_not_a_memory_id(db, related_args,
                                       sizeof related_args / sizeof related_args[0]);
    }
    free(db);
}

/* Unflagged delete of an expired memory, addressed by its UUID, soft-deletes.
   get of that UUID without a bin flag is the expired bin, not invalid_argument. */
TEST(envelope_delete_expired_by_uuid_is_a_soft_delete)
{
    char *db = make_temp_db_path();
    char id[MEMORY_ID_LEN + 1];
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    {
        const char *add[] = {"add", "--expires", "2020-01-01T00:00:00Z", "old"};

        r = run_remember(db, add, sizeof add / sizeof add[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"--json", "get", "--expired", "1"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_EQ_INT(copy_memory_id(r.out, id), 0);
        ASSERT_STR_CONTAINS(r.out, "\"bin\":\"expired\"");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"--json", "get", id};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 3);
        ASSERT_STR_CONTAINS(r.out, "\"code\":\"expired\"");
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"delete", id};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
    }
    {
        const char *args[] = {"--json", "get", "--deleted", id};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STR_CONTAINS(r.out, "\"bin\":\"deleted\"");
        cmd_result_free(&r);
    }
    free(db);
}

/* JSON id is the old sync_id. The integer is local_id. The three flags are gone.
   Stage 3 owns the C-01 locator sentence. */
TEST(envelope_memory_id_is_the_uuid_and_the_old_flags_are_unknown)
{
    static const char *const flags[] = {"--sync-id", "--local-id", "--id"};
    char *db = make_temp_db_path();
    const char *get[] = {"--json", "get", "1"};
    const char *memory = NULL;
    CmdResult r;
    size_t i = 0;

    ASSERT_TRUE(db != NULL);
    given_one_memory_with_key(db, "k");
    r = run_remember(db, get, sizeof get / sizeof get[0], NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    memory = strstr(r.out, "\"memory\":{\"id\":\"");
    ASSERT_TRUE(memory != NULL);
    if (memory != NULL) {
        ASSERT_EQ_INT(uuid_v7_at(memory + strlen("\"memory\":{\"id\":\"")), 1);
    }
    ASSERT_STR_CONTAINS(r.out, "\",\"local_id\":1,\"key\":\"k\"");
    ASSERT_STR_NOT_CONTAINS(r.out, "sync_id");
    cmd_result_free(&r);

    for (i = 0; i < sizeof flags / sizeof flags[0]; i++) {
        const char *args[] = {"get", flags[i], "1"};

        r = run_remember(db, args, sizeof args / sizeof args[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STR_CONTAINS(r.err, "unknown option '");
        ASSERT_STR_CONTAINS(r.err, flags[i]);
        cmd_result_free(&r);
    }
    {
        const char *from_sync[] = {"link", "--from-sync-id", "1", "--to", "2"};
        const char *to_sync[] = {"link", "--from", "1", "--to-sync-id", "2"};

        r = run_remember(db, from_sync, sizeof from_sync / sizeof from_sync[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STR_CONTAINS(r.err, "unknown option '--from-sync-id'");
        cmd_result_free(&r);
        r = run_remember(db, to_sync, sizeof to_sync / sizeof to_sync[0], NULL);
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STR_CONTAINS(r.err, "unknown option '--to-sync-id'");
        cmd_result_free(&r);
    }
    free(db);
}

void register_envelope_tests(void)
{
    RUN_TEST(envelope_get_by_digits_and_by_uuid_is_the_same_memory);
    RUN_TEST(envelope_uuid_positional_updates_deletes_and_links);
    RUN_TEST(envelope_positional_rejects_what_is_not_a_memory_id);
    RUN_TEST(envelope_delete_expired_by_uuid_is_a_soft_delete);
    RUN_TEST(envelope_memory_id_is_the_uuid_and_the_old_flags_are_unknown);
    RUN_TEST(envelope_get_head_is_format_version_type_then_data_memory);
    RUN_TEST(envelope_add_leaves_out_a_neighbor_in_the_trash_like_get);
    RUN_TEST(envelope_a_new_memory_has_created_by_and_updated_by_and_no_source);
    RUN_TEST(envelope_every_command_names_its_reply_type);
    RUN_TEST(envelope_failure_keeps_its_type_and_carries_the_warnings_after_the_error);
    RUN_TEST(envelope_warnings_keep_their_order_with_a_comma_between);
#ifdef REMEMBER_TEST_VFS
    RUN_TEST(envelope_add_link_read_error_after_the_write_exits_1_and_keeps_the_write);
#else
    (void)printf("SKIP the envelope VFS shim test (remember built without REMEMBER_TEST_VFS)\n");
#endif
}
