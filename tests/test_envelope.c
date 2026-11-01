#include "harness.h"
#include "register.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
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

void register_envelope_tests(void)
{
    RUN_TEST(envelope_get_head_is_format_version_type_then_data_memory);
    RUN_TEST(envelope_add_leaves_out_a_neighbor_in_the_trash_like_get);
    RUN_TEST(envelope_every_command_names_its_reply_type);
    RUN_TEST(envelope_failure_keeps_its_type_and_carries_the_warnings_after_the_error);
    RUN_TEST(envelope_warnings_keep_their_order_with_a_comma_between);
#ifdef REMEMBER_TEST_VFS
    RUN_TEST(envelope_add_link_read_error_after_the_write_exits_1_and_keeps_the_write);
#else
    (void)printf("SKIP the envelope VFS shim test (remember built without REMEMBER_TEST_VFS)\n");
#endif
}
