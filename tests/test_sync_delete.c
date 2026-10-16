#include "harness.h"
#include "register.h"
#include "test.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// NOLINTBEGIN(readability-identifier-length,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

TEST(cli_unflagged_delete_is_soft)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *a[] = {"--json", "add", "--key", "k", "--tag", "t", "keep body"};
    const char *d[] = {"--json", "delete", "--key", "k"};
    const char *g[] = {"get", "--key", "k"};
    const char *get_deleted[] = {"--json", "get", "--deleted", "--key", "k"};
    const char *lst[] = {"--json", "list"};

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, d, sizeof(d) / sizeof(d[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"action\":\"deleted\"");
    ASSERT_STR_CONTAINS(r.out, "\"bin\":\"deleted\"");
    ASSERT_STR_CONTAINS(r.out, "\"expires_at\":null");
    ASSERT_STR_CONTAINS(r.out, "keep body");
    cmd_result_free(&r);
    r = run_remember(db, g, sizeof(g) / sizeof(g[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 3);
    ASSERT_STREQ(r.err, "remember: deleted\n");
    cmd_result_free(&r);
    r = run_remember(db, get_deleted, sizeof(get_deleted) / sizeof(get_deleted[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"bin\":\"deleted\"");
    ASSERT_STR_CONTAINS(r.out, "keep body");
    cmd_result_free(&r);
    r = run_remember(db, lst, sizeof(lst) / sizeof(lst[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_NOT_CONTAINS(r.out, "keep body");
    cmd_result_free(&r);
    free(db);
}

TEST(cli_unflagged_delete_expired_clears_expires)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *a[] = {"--json", "add", "--expires", "2020-01-01T00:00:00Z", "old"};
    const char *d[] = {"--json", "delete", "1"};
    const char *g[] = {"get", "--expired", "1"};

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, d, sizeof(d) / sizeof(d[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"bin\":\"deleted\"");
    ASSERT_STR_CONTAINS(r.out, "\"expires_at\":null");
    cmd_result_free(&r);
    r = run_remember(db, g, sizeof(g) / sizeof(g[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 3);
    ASSERT_STREQ(r.err, "remember: deleted\n");
    cmd_result_free(&r);
    free(db);
}

TEST(cli_delete_expired_hard_wipes_one_row)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *a[] = {"add", "--expires", "2020-01-01T00:00:00Z", "gone"};
    const char *keep[] = {"add", "stay"};
    const char *d[] = {"--json", "delete", "--expired", "1"};
    const char *alias[] = {"delete", "--trash", "2"};
    const char *g[] = {"get", "--expired", "1"};
    const char *keyed[] = {"add",       "--key", "gonek", "--expires", "2020-01-01T00:00:00Z",
                           "keyed-gone"};
    const char *dk[] = {"delete", "--expired", "--key", "gonek"};
    const char *gk[] = {"get", "--expired", "--key", "gonek"};

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, keep, sizeof(keep) / sizeof(keep[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, d, sizeof(d) / sizeof(d[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "gone");
    cmd_result_free(&r);
    r = run_remember(db, g, sizeof(g) / sizeof(g[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 2);
    cmd_result_free(&r);
    r = run_remember(db, keyed, sizeof(keyed) / sizeof(keyed[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, dk, sizeof(dk) / sizeof(dk[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, gk, sizeof(gk) / sizeof(gk[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 2);
    cmd_result_free(&r);
    r = run_remember(db, alias, sizeof(alias) / sizeof(alias[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 3);
    ASSERT_STR_CONTAINS(r.err, "remember: warning: --trash is deprecated; use --expired");
    ASSERT_STR_CONTAINS(r.err, "not_expired");
    cmd_result_free(&r);
    free(db);
}

TEST(cli_delete_deleted_hard_wipes_the_row)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *a[] = {"add", "wipe"};
    const char *soft[] = {"delete", "1"};
    const char *hard[] = {"delete", "--deleted", "1"};
    const char *gone[] = {"get", "--deleted", "1"};

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, soft, sizeof(soft) / sizeof(soft[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, hard, sizeof(hard) / sizeof(hard[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, gone, sizeof(gone) / sizeof(gone[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 2);
    cmd_result_free(&r);
    free(db);
}

TEST(cli_update_undelete)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *a[] = {"add", "--key", "slot", "gone"};
    const char *soft[] = {"delete", "--key", "slot"};
    const char *bad[] = {"update", "--key", "slot", "--undelete"};
    const char *undelete_ok[] = {"--json", "update", "--deleted", "--key", "slot", "--undelete"};
    const char *mutex[] = {"update", "--deleted", "1", "--undelete", "--clear-expires"};

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, soft, sizeof(soft) / sizeof(soft[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, bad, sizeof(bad) / sizeof(bad[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 3);
    ASSERT_STREQ(r.err, "remember: deleted\n");
    cmd_result_free(&r);
    r = run_remember(db, undelete_ok, sizeof(undelete_ok) / sizeof(undelete_ok[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"bin\":\"live\"");
    ASSERT_STR_CONTAINS(r.out, "\"deleted_at\":null");
    cmd_result_free(&r);
    r = run_remember(db, mutex, sizeof(mutex) / sizeof(mutex[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err,
                        "cannot combine --undelete with --ttl, --expires, or --clear-expires");
    cmd_result_free(&r);
    free(db);
}

TEST(cli_add_revives_deleted)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *a[] = {"--json", "add", "--key", "slot", "first"};
    const char *soft[] = {"delete", "--key", "slot"};
    const char *again[] = {"--json", "add", "--key", "slot", "second"};

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"sync_id\":");
    cmd_result_free(&r);
    r = run_remember(db, soft, sizeof(soft) / sizeof(soft[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, again, sizeof(again) / sizeof(again[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"action\":\"updated\"");
    ASSERT_STR_CONTAINS(r.out, "\"id\":1");
    ASSERT_STR_CONTAINS(r.out, "second");
    ASSERT_STR_CONTAINS(r.out, "\"bin\":\"live\"");
    cmd_result_free(&r);

    {
        const char *keyless[] = {"--json", "add", "hashbody"};
        const char *keyless_soft[] = {"delete", "2"};
        const char *keyless_again[] = {"--json", "add", "hashbody"};

        r = run_remember(db, keyless, sizeof(keyless) / sizeof(keyless[0]), NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
        r = run_remember(db, keyless_soft, sizeof(keyless_soft) / sizeof(keyless_soft[0]), NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
        r = run_remember(db, keyless_again, sizeof(keyless_again) / sizeof(keyless_again[0]), NULL);
        ASSERT_EQ_INT(r.exit_code, 0);
        ASSERT_STR_CONTAINS(r.out, "\"action\":\"merged\"");
        ASSERT_STR_CONTAINS(r.out, "\"id\":2");
        ASSERT_STR_CONTAINS(r.out, "\"bin\":\"live\"");
        cmd_result_free(&r);
    }
    free(db);
}

TEST(cli_purge_requires_one_flag)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *none[] = {"purge"};
    const char *both[] = {"purge", "--expired", "--deleted"};
    const char *unk[] = {"purge", "--nope"};
    const char *pos[] = {"purge", "--expired", "1"};

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, none, sizeof(none) / sizeof(none[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "purge requires --expired or --deleted");
    cmd_result_free(&r);
    r = run_remember(db, both, sizeof(both) / sizeof(both[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "cannot combine --expired and --deleted");
    cmd_result_free(&r);
    r = run_remember(db, unk, sizeof(unk) / sizeof(unk[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "unknown option '--nope'");
    cmd_result_free(&r);
    r = run_remember(db, pos, sizeof(pos) / sizeof(pos[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "purge takes no positional arguments");
    cmd_result_free(&r);
    free(db);
}

TEST(cli_purge_deleted_and_purge_trash_alias)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *del[] = {"add", "wipe-me"};
    const char *exp[] = {"add", "--expires", "2020-01-01T00:00:00Z", "old"};
    const char *soft[] = {"delete", "1"};
    const char *pdel[] = {"--json", "purge", "--deleted"};
    const char *ptrash[] = {"purge-trash"};
    const char *g[] = {"get", "--deleted", "1"};
    const char *get_expired[] = {"get", "--expired", "2"};

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, del, sizeof(del) / sizeof(del[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, exp, sizeof(exp) / sizeof(exp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, soft, sizeof(soft) / sizeof(soft[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, pdel, sizeof(pdel) / sizeof(pdel[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"count\":1");
    ASSERT_STR_CONTAINS(r.out, "wipe-me");
    cmd_result_free(&r);
    r = run_remember(db, g, sizeof(g) / sizeof(g[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 2);
    cmd_result_free(&r);
    r = run_remember(db, ptrash, sizeof(ptrash) / sizeof(ptrash[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.err, "remember: warning: purge-trash is deprecated; use purge --expired");
    trim_trailing_newlines(r.out);
    ASSERT_STREQ(r.out, "1");
    cmd_result_free(&r);
    r = run_remember(db, get_expired, sizeof(get_expired) / sizeof(get_expired[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 2);
    cmd_result_free(&r);
    free(db);
}

TEST(cli_invalid_sidecar_exits_1_and_keeps_the_file)
{
    char *db = make_temp_db_path();
    char side[512];
    char got[64];
    CmdResult r;
    FILE *f = NULL;
    const char *a[] = {"add", "x"};
    int sn = 0;

    ASSERT_TRUE(db != NULL);
    got[0] = '\0';
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    sn = snprintf(side, sizeof(side), "%s.device_id", db);
    ASSERT_TRUE(sn > 0 && (size_t)sn < sizeof(side));
    /* NOLINTNEXTLINE(android-cloexec-fopen) */
    f = fopen(side, "w");
    ASSERT_TRUE(f != NULL);
    if (f != NULL) {
        (void)fputs("not-a-uuid\n", f);
        (void)fclose(f);
    }
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "remember: invalid device_id sidecar\n");
    cmd_result_free(&r);

    /* NOLINTNEXTLINE(android-cloexec-fopen) */
    f = fopen(side, "r");
    ASSERT_TRUE(f != NULL);
    ASSERT_TRUE(f != NULL && fgets(got, (int)sizeof(got), f) != NULL);
    if (f != NULL) {
        (void)fclose(f);
    }
    ASSERT_STREQ(got, "not-a-uuid\n");
    free(db);
}

TEST(cli_purge_expired_wipes_without_device_hatch)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *a[] = {"add", "--expires", "2020-01-01T00:00:00Z", "old"};
    const char *p[] = {"purge", "--expired"};
    const char *gone[] = {"get", "--expired", "1"};

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, p, sizeof(p) / sizeof(p[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_TRUE(strstr(r.err, "exactly one registered device") == NULL);
    cmd_result_free(&r);
    r = run_remember(db, gone, sizeof(gone) / sizeof(gone[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 2);
    cmd_result_free(&r);
    free(db);
}

/* Child of the parallel-open race: a real CLI process (its own PRNG seed),
   output discarded, exit status = the add's. */
_Noreturn static void race_cli_add(const char *db, const char *key)
{
    char *argv[] = {(char *)g_remember_bin, (char *)"--db", (char *)db,  (char *)"add",
                    (char *)"--key",        (char *)key,    (char *)key, NULL};
    /* NOLINTNEXTLINE(android-cloexec-open,hicpp-signed-bitwise) */
    int fd = open("/dev/null", O_WRONLY);

    if (fd >= 0) {
        (void)dup2(fd, STDOUT_FILENO);
        (void)dup2(fd, STDERR_FILENO);
    }
    (void)execv(g_remember_bin, argv);
    _exit(127);
}

/* A v5 brain whose sidecar is gone: parallel opens mint under the write lock,
   so no open fails and every racer writes under the id the sidecar keeps. */
TEST(cli_parallel_opens_mint_one_sidecar_id)
{
    enum { RACERS = 16, ROUNDS = 8 };
    char *db = make_temp_db_path();
    char side[512];
    CmdResult r;
    const char *a[] = {"add", "seed"};
    int failures = 0;
    int round = 0;
    int sn = 0;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    sn = snprintf(side, sizeof(side), "%s.device_id", db);
    ASSERT_TRUE(sn > 0 && (size_t)sn < sizeof(side));

    for (round = 0; round < ROUNDS; round++) {
        pid_t pids[RACERS];
        char keys[RACERS][32];
        char id[64];
        char sql[256];
        char *got = NULL;
        FILE *f = NULL;
        int i = 0;

        id[0] = '\0';
        ASSERT_EQ_INT(remove(side), 0);
        (void)fflush(stdout);
        (void)fflush(stderr);
        for (i = 0; i < RACERS; i++) {
            (void)snprintf(keys[i], sizeof(keys[i]), "race:%d:%d", round, i);
            pids[i] = fork();
            if (pids[i] == 0) {
                race_cli_add(db, keys[i]);
            }
        }
        for (i = 0; i < RACERS; i++) {
            int status = 0;
            if (pids[i] < 0 || waitpid(pids[i], &status, 0) < 0 || !WIFEXITED(status) ||
                WEXITSTATUS(status) != 0) {
                failures++;
            }
        }
        /* NOLINTNEXTLINE(android-cloexec-fopen) */
        f = fopen(side, "r");
        ASSERT_TRUE(f != NULL && fgets(id, (int)sizeof(id), f) != NULL);
        if (f != NULL) {
            (void)fclose(f);
        }
        id[strcspn(id, "\n")] = '\0';
        (void)snprintf(sql, sizeof(sql),
                       "SELECT COUNT(*) || '|' || SUM(j.key != '%s') FROM entries, "
                       "json_each(version_vector) j WHERE entries.key LIKE 'race:%d:%%';",
                       id, round);
        got = harness_sqlite_query_line(db, sql);
        ASSERT_STREQ(got != NULL ? got : "", "16|0");
        free(got);
    }
    ASSERT_EQ_INT(failures, 0);
    free(db);
}

void register_sync_delete_tests(void)
{
    RUN_TEST(cli_unflagged_delete_is_soft);
    RUN_TEST(cli_unflagged_delete_expired_clears_expires);
    RUN_TEST(cli_delete_expired_hard_wipes_one_row);
    RUN_TEST(cli_delete_deleted_hard_wipes_the_row);
    RUN_TEST(cli_update_undelete);
    RUN_TEST(cli_add_revives_deleted);
    RUN_TEST(cli_purge_requires_one_flag);
    RUN_TEST(cli_purge_deleted_and_purge_trash_alias);
    RUN_TEST(cli_invalid_sidecar_exits_1_and_keeps_the_file);
    RUN_TEST(cli_purge_expired_wipes_without_device_hatch);
    RUN_TEST(cli_parallel_opens_mint_one_sidecar_id);
}

// NOLINTEND(readability-identifier-length,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
