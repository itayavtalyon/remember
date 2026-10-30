#include "harness.h"
#include "register.h"
#include "test.h"

#include <stdlib.h>

/* One memory with a key, so get can find it by number 1. */
static void given_one_memory(const char *db)
{
    const char *add[] = {"add", "--key", "k", "a body"};
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
    given_one_memory(db);
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

void register_envelope_tests(void)
{
    RUN_TEST(envelope_get_head_is_format_version_type_then_data_memory);
    RUN_TEST(envelope_add_leaves_out_a_neighbor_in_the_trash_like_get);
}
