#include "harness.h"
#include "register.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>

/* ---- global CLI / meta --------------------------------------------------- */

TEST(no_subcommand_exits_usage)
{
    char *db = make_temp_db_path();
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, NULL, 0, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_TRUE(!str_is_blank(r.err));
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
    ASSERT_STR_CONTAINS(r.err, "frobnicate");
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

TEST(help_subcommand_exits_zero)
{
    char *db = make_temp_db_path();
    const char *args[] = {"help"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 1, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "add");
    cmd_result_free(&r);
    free(db);
}

TEST(version_subcommand_exits_zero)
{
    char *db = make_temp_db_path();
    const char *args[] = {"version"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 1, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "remember");
    cmd_result_free(&r);
    free(db);
}

TEST(db_without_value_exits_usage)
{
    char *db = make_temp_db_path();
    /* remember --db PATH --db   (second --db missing value) */
    const char *args[] = {"--db"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 1, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "--db");
    cmd_result_free(&r);
    free(db);
}

TEST(unknown_global_option_before_command_exits_usage)
{
    char *db = make_temp_db_path();
    const char *args[] = {"--not-a-real-flag", "add", "x"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "--not-a-real-flag");
    cmd_result_free(&r);
    free(db);
}

TEST(json_global_before_command_accepted)
{
    /* Not implemented yet, but must parse: exit 3, not usage 1. */
    char *db = make_temp_db_path();
    const char *args[] = {"--json", "add", "hello"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 3);
    cmd_result_free(&r);
    free(db);
}

TEST(json_global_after_command_accepted)
{
    char *db = make_temp_db_path();
    const char *args[] = {"add", "--json", "hello"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 3);
    cmd_result_free(&r);
    free(db);
}

TEST(db_equals_form_accepted)
{
    char *db = make_temp_db_path();
    char arg[512];
    const char *args[3];
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    (void)snprintf(arg, sizeof(arg), "--db=%s", db);
    args[0] = arg;
    args[1] = "add";
    args[2] = "hello";
    /* run_remember also injects --db; extra --db= is fine */
    r = run_remember(db, args, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 3);
    cmd_result_free(&r);
    free(db);
}

TEST(subcommand_help_add)
{
    char *db = make_temp_db_path();
    const char *args[] = {"add", "--help"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "add");
    ASSERT_STR_CONTAINS(r.out, "Store a memory");
    cmd_result_free(&r);
    free(db);
}

TEST(help_topic_subcommand)
{
    char *db = make_temp_db_path();
    const char *args[] = {"help", "search"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "search");
    ASSERT_STR_CONTAINS(r.out, "Full-text");
    cmd_result_free(&r);
    free(db);
}

TEST(help_unknown_topic_exits_usage)
{
    char *db = make_temp_db_path();
    const char *args[] = {"help", "frobnicate"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "frobnicate");
    cmd_result_free(&r);
    free(db);
}

TEST(command_specific_option_before_command_is_unknown_global)
{
    /* --tag before subcommand is not a global -> usage error */
    char *db = make_temp_db_path();
    const char *args[] = {"--tag", "x", "add", "body"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "--tag");
    cmd_result_free(&r);
    free(db);
}

TEST(command_specific_option_after_command_not_usage_error)
{
    /* --tag after add is for later steps; parse must not fail */
    char *db = make_temp_db_path();
    const char *args[] = {"add", "--tag", "x", "body"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 3);
    cmd_result_free(&r);
    free(db);
}

void register_cli_global_tests(void)
{
    RUN_TEST(no_subcommand_exits_usage);
    RUN_TEST(unknown_subcommand_exits_usage);
    RUN_TEST(help_exits_zero_and_prints_usage);
    RUN_TEST(version_exits_zero);
    RUN_TEST(help_subcommand_exits_zero);
    RUN_TEST(version_subcommand_exits_zero);
    RUN_TEST(db_without_value_exits_usage);
    RUN_TEST(unknown_global_option_before_command_exits_usage);
    RUN_TEST(json_global_before_command_accepted);
    RUN_TEST(json_global_after_command_accepted);
    RUN_TEST(db_equals_form_accepted);
    RUN_TEST(subcommand_help_add);
    RUN_TEST(help_topic_subcommand);
    RUN_TEST(help_unknown_topic_exits_usage);
    RUN_TEST(command_specific_option_before_command_is_unknown_global);
    RUN_TEST(command_specific_option_after_command_not_usage_error);
}
