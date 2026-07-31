#include "harness.h"
#include "register.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

/* Two entries: entry 1 has apple+zebra, entry 2 has apple. So apple=2, zebra=1. */
static void seed_tagged(const char *db)
{
    const char *a1[] = {"add", "--tag", "zebra", "--tag", "apple", "first body"};
    const char *a2[] = {"add", "--tag", "apple", "second body"};
    CmdResult r;

    r = run_remember(db, a1, 6, NULL);
    cmd_result_free(&r);
    r = run_remember(db, a2, 4, NULL);
    cmd_result_free(&r);
}

TEST(tags_empty_db_json)
{
    char *db = make_temp_db_path();
    const char *args[] = {"tags", "--json"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.out, "{\"version\":1,\"count\":0,\"tags\":[]}\n");
    cmd_result_free(&r);
    free(db);
}

TEST(tags_counts_and_sorted_json)
{
    char *db = make_temp_db_path();
    const char *args[] = {"tags", "--json"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    r = run_remember(db, args, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"count\":2");
    ASSERT_STR_CONTAINS(r.out, "{\"name\":\"apple\",\"count\":2}");
    ASSERT_STR_CONTAINS(r.out, "{\"name\":\"zebra\",\"count\":1}");
    /* Sorted by name: apple before zebra. */
    {
        const char *pa = strstr(r.out, "\"apple\"");
        const char *pz = strstr(r.out, "\"zebra\"");
        ASSERT_TRUE(pa != NULL && pz != NULL);
        ASSERT_TRUE(pa < pz);
    }
    cmd_result_free(&r);
    free(db);
}

TEST(tags_human_shape)
{
    char *db = make_temp_db_path();
    const char *args[] = {"tags"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    seed_tagged(db);
    r = run_remember(db, args, 1, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "apple\t2");
    ASSERT_STR_CONTAINS(r.out, "zebra\t1");
    cmd_result_free(&r);
    free(db);
}

/* Deleting the only entry with a tag drops it (orphan-tag GC is reflected). */
TEST(tags_reflects_delete_gc)
{
    char *db = make_temp_db_path();
    const char *add[] = {"add", "--tag", "solo", "only body"};
    const char *del[] = {"delete", "1"};
    const char *args[] = {"tags", "--json"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, add, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, del, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, args, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"count\":0");
    ASSERT_STR_NOT_CONTAINS(r.out, "solo");
    cmd_result_free(&r);
    free(db);
}

TEST(tags_rejects_arguments)
{
    char *db = make_temp_db_path();
    const char *args[] = {"tags", "extra"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "takes no arguments");
    cmd_result_free(&r);
    free(db);
}

void register_tags_tests(void)
{
    RUN_TEST(tags_empty_db_json);
    RUN_TEST(tags_counts_and_sorted_json);
    RUN_TEST(tags_human_shape);
    RUN_TEST(tags_reflects_delete_gc);
    RUN_TEST(tags_rejects_arguments);
}
