#include "test.h"
#include "harness.h"
#include "register.h"

#include <stdlib.h>
#include <unistd.h>

/* ---- global CLI / meta --------------------------------------------------- */

TEST(no_subcommand_exits_usage)
{
    char *db = make_temp_db_path();
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, NULL, 0, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(unknown_subcommand_exits_usage)
{
    char *db = make_temp_db_path();
    const char *args[] = {"frobnicate"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 1, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(help_exits_zero_and_prints_usage)
{
    char *db = make_temp_db_path();
    const char *args[] = {"--help"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 1, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "add");
    ASSERT_STR_CONTAINS(r.out, "search");
    ASSERT_STR_CONTAINS(r.out, "update");
    cmd_result_free(&r);
    free(db);
}

TEST(version_exits_zero)
{
    char *db = make_temp_db_path();
    const char *args[] = {"--version"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 1, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_TRUE(r.out != NULL && r.out[0] != '\0');
    cmd_result_free(&r);
    free(db);
}

void register_cli_global_tests(void)
{
    RUN_TEST(no_subcommand_exits_usage);
    RUN_TEST(unknown_subcommand_exits_usage);
    RUN_TEST(help_exits_zero_and_prints_usage);
    RUN_TEST(version_exits_zero);
}
