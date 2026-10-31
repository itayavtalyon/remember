#include "harness.h"
#include "register.h"
#include "test.h"

#include <stdlib.h>
#ifndef REMEMBER_TEST_VFS
#include <stdio.h> /* the skip line */
#endif

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
    ASSERT_STREQ(r.out, "");
    ASSERT_STR_CONTAINS(r.err, "SQLITE_IOERR_READ");
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
#ifdef REMEMBER_TEST_VFS
    RUN_TEST(envelope_add_link_read_error_after_the_write_exits_1_and_keeps_the_write);
#else
    (void)printf("SKIP the envelope VFS shim test (remember built without REMEMBER_TEST_VFS)\n");
#endif
}
