#include "test.h"
#include "harness.h"
#include "register.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * DB path resolution and JSON field presence.
 * REMEMBER_DB is tested by invoking the binary without --db via a local helper.
 */

static CmdResult run_remember_raw(const char *const *argv, size_t argc,
                                  const char *stdin_data)
{
    /* Like run_remember but does not inject --db (for env tests). */
    CmdResult result = {0, NULL, NULL};
    int out_pipe[2];
    int err_pipe[2];
    pid_t pid;
    char **av;
    size_t i;

    if (g_remember_bin == NULL) {
        result.exit_code = 127;
        result.out = strdup("");
        result.err = strdup("no bin");
        return result;
    }
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
        result.exit_code = 127;
        result.out = strdup("");
        result.err = strdup("pipe");
        return result;
    }
    av = calloc(argc + 2U, sizeof(*av));
    if (av == NULL) {
        result.exit_code = 127;
        result.out = strdup("");
        result.err = strdup("oom");
        return result;
    }
    av[0] = (char *)g_remember_bin;
    for (i = 0; i < argc; i++) {
        av[i + 1U] = (char *)argv[i];
    }
    av[argc + 1U] = NULL;

    pid = fork();
    if (pid == 0) {
        (void)dup2(out_pipe[1], STDOUT_FILENO);
        (void)dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        if (stdin_data == NULL) {
            int dn = open("/dev/null", O_RDONLY);
            if (dn >= 0) {
                (void)dup2(dn, STDIN_FILENO);
                close(dn);
            }
        }
        execv(g_remember_bin, av);
        _exit(127);
    }
    free(av);
    close(out_pipe[1]);
    close(err_pipe[1]);
    /* simplified: ignore stdin_data for raw helper */
    (void)stdin_data;
    {
        char buf[4096];
        ssize_t n;
        size_t olen = 0, elen = 0;
        char *ob = malloc(1);
        char *eb = malloc(1);
        if (ob) {
            ob[0] = '\0';
        }
        if (eb) {
            eb[0] = '\0';
        }
        while ((n = read(out_pipe[0], buf, sizeof(buf))) > 0) {
            char *nb = realloc(ob, olen + (size_t)n + 1U);
            if (!nb) {
                break;
            }
            ob = nb;
            memcpy(ob + olen, buf, (size_t)n);
            olen += (size_t)n;
            ob[olen] = '\0';
        }
        while ((n = read(err_pipe[0], buf, sizeof(buf))) > 0) {
            char *nb = realloc(eb, elen + (size_t)n + 1U);
            if (!nb) {
                break;
            }
            eb = nb;
            memcpy(eb + elen, buf, (size_t)n);
            elen += (size_t)n;
            eb[elen] = '\0';
        }
        close(out_pipe[0]);
        close(err_pipe[0]);
        result.out = ob ? ob : strdup("");
        result.err = eb ? eb : strdup("");
    }
    {
        int st = 0;
        (void)waitpid(pid, &st, 0);
        result.exit_code = WIFEXITED(st) ? WEXITSTATUS(st) : 127;
    }
    return result;
}

TEST(db_flag_isolates_stores)
{
    char *db1 = make_temp_db_path();
    char *db2 = make_temp_db_path();
    CmdResult r, g;
    const char *a[] = {"add", "only in db1"};
    const char *gargs[] = {"get", "--json", "1"};
    ASSERT_TRUE(db1 != NULL && db2 != NULL);
    r = run_remember(db1, a, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    g = run_remember(db2, gargs, 3, NULL);
    ASSERT_EQ_INT(g.exit_code, 2);
    cmd_result_free(&g);
    g = run_remember(db1, gargs, 3, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "only in db1");
    cmd_result_free(&g);
    free(db1);
    free(db2);
}

TEST(remember_db_env_used_when_no_flag)
{
    char *db = make_temp_db_path();
    CmdResult r, g;
    const char *a[] = {"add", "via env db"};
    const char *gargs[] = {"get", "--json", "1"};
    char *old;
    ASSERT_TRUE(db != NULL);
    old = getenv("REMEMBER_DB");
    ASSERT_EQ_INT(setenv("REMEMBER_DB", db, 1), 0);
    r = run_remember_raw(a, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    g = run_remember_raw(gargs, 3, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "via env db");
    cmd_result_free(&g);
    if (old != NULL) {
        (void)setenv("REMEMBER_DB", old, 1);
    } else {
        (void)unsetenv("REMEMBER_DB");
    }
    free(db);
}

TEST(db_flag_wins_over_env)
{
    char *db_flag = make_temp_db_path();
    char *db_env = make_temp_db_path();
    CmdResult r, g;
    const char *a[] = {"add", "flag wins"};
    const char *gargs[] = {"get", "--json", "1"};
    ASSERT_TRUE(db_flag != NULL && db_env != NULL);
    ASSERT_EQ_INT(setenv("REMEMBER_DB", db_env, 1), 0);
    r = run_remember(db_flag, a, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    g = run_remember(db_env, gargs, 3, NULL);
    ASSERT_EQ_INT(g.exit_code, 2);
    cmd_result_free(&g);
    g = run_remember(db_flag, gargs, 3, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    cmd_result_free(&g);
    (void)unsetenv("REMEMBER_DB");
    free(db_flag);
    free(db_env);
}

TEST(json_entry_has_required_fields)
{
    char *db = make_temp_db_path();
    CmdResult r, g;
    const char *a[] = {"add", "--tag", "t", "--source", "tool", "fields check"};
    const char *gargs[] = {"get", "--json", "1"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, 6, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    g = run_remember(db, gargs, 3, NULL);
    ASSERT_EQ_INT(g.exit_code, 0);
    ASSERT_STR_CONTAINS(g.out, "\"version\":1");
    ASSERT_STR_CONTAINS(g.out, "\"id\":");
    ASSERT_STR_CONTAINS(g.out, "\"body\":");
    ASSERT_STR_CONTAINS(g.out, "\"tags\":");
    ASSERT_STR_CONTAINS(g.out, "\"source\":");
    ASSERT_STR_CONTAINS(g.out, "\"created_at\":");
    ASSERT_STR_CONTAINS(g.out, "\"updated_at\":");
    /* ISO-8601 UTC ends with Z */
    ASSERT_STR_CONTAINS(g.out, "Z");
    cmd_result_free(&g);
    free(db);
}

TEST(second_add_gets_id_two)
{
    char *db = make_temp_db_path();
    CmdResult r1, r2;
    const char *a1[] = {"add", "first"};
    const char *a2[] = {"add", "second"};
    ASSERT_TRUE(db != NULL);
    r1 = run_remember(db, a1, 2, NULL);
    r2 = run_remember(db, a2, 2, NULL);
    ASSERT_EQ_INT(r1.exit_code, 0);
    ASSERT_EQ_INT(r2.exit_code, 0);
    ASSERT_EQ_INT(parse_id_stdout(r1.out), 1);
    ASSERT_EQ_INT(parse_id_stdout(r2.out), 2);
    cmd_result_free(&r1);
    cmd_result_free(&r2);
    free(db);
}

void register_json_db_config_tests(void)
{
    RUN_TEST(db_flag_isolates_stores);
    RUN_TEST(remember_db_env_used_when_no_flag);
    RUN_TEST(db_flag_wins_over_env);
    RUN_TEST(json_entry_has_required_fields);
    RUN_TEST(second_add_gets_id_two);
}
