#include "appio.h"
#include "cmd_settings.h"
#include "diag.h"
#include "harness.h"
#include "normalize.h"
#include "register.h"
#include "settings.h"
#include "store.h"
#include "sync_credential.h"
#include "test.h"
#include "test_alloc.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* The numbers are the plan's examples (1500ms, 1.5KiB, the registry bounds).
   A name for each one hides that table. */
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

enum { SEGMENT_MAX = 40 };

enum { CANON = NORM_MEASURE_CANON_CAP };

static void assert_duration(const char *src, long long ms, const char *canon)
{
    char out[CANON];
    long long got = -1;

    ASSERT_TRUE(norm_duration_canonical(src, &got, out, sizeof(out)));
    ASSERT_EQ_INT(got, ms);
    ASSERT_STREQ(out, canon);
}

static void assert_size(const char *src, long long bytes, const char *canon)
{
    char out[CANON];
    long long got = -1;

    ASSERT_TRUE(norm_size_canonical(src, &got, out, sizeof(out)));
    ASSERT_EQ_INT(got, bytes);
    ASSERT_STREQ(out, canon);
}

TEST(duration_canonical_examples)
{
    assert_duration("1500ms", 1500, "1.5s");
    assert_duration("90s", 90000, "1.5m");
    assert_duration("61s", 61000, "61s");
    assert_duration("100ms", 100, "100ms");
    assert_duration("1.5s", 1500, "1.5s");
    assert_duration("2.50s", 2500, "2.5s");
    assert_duration("2.00s", 2000, "2s");
    assert_duration("0ms", 0, "0ms");
    assert_duration("0.5s", 500, "500ms");
    assert_duration("01s", 1000, "1s");
    assert_duration("1000ms", 1000, "1s");
    assert_duration("60000ms", 60000, "1m");
    assert_duration("3600000ms", 3600000, "1h");
    assert_duration("1.001s", 1001, "1.001s");
    assert_duration("1.010s", 1010, "1.01s");
    assert_duration("1ms", 1, "1ms");
    assert_duration("1m", 60000, "1m");
    assert_duration("10m", 600000, "10m");
    assert_duration("90m", 5400000, "1.5h");
    assert_duration("1.5h", 5400000, "1.5h");
    assert_duration("61m", 3660000, "61m");
    assert_duration("0s", 0, "0ms");
    assert_duration("0h", 0, "0ms");
    assert_duration("1.000s", 1000, "1s");
}

TEST(duration_rejects_what_is_not_a_whole_millisecond)
{
    char out[CANON];
    long long got = 1;
    char tiny[CANON - 1];

    memset(out, 'x', sizeof(out) - 1U);
    out[sizeof(out) - 1U] = '\0';
    ASSERT_FALSE(norm_duration_canonical(NULL, &got, out, sizeof(out)));
    ASSERT_STREQ(out, "");
    ASSERT_EQ_INT(got, 1);
    memset(out, 'x', sizeof(out) - 1U);
    out[sizeof(out) - 1U] = '\0';
    ASSERT_FALSE(norm_duration_canonical("1s", NULL, out, sizeof(out)));
    ASSERT_STREQ(out, "");
    ASSERT_EQ_INT(got, 1);
    ASSERT_FALSE(norm_duration_canonical("1s", &got, NULL, sizeof(out)));
    memset(tiny, 'Q', sizeof(tiny));
    ASSERT_FALSE(norm_duration_canonical("1s", &got, tiny, sizeof(tiny)));
    ASSERT_EQ_INT((unsigned char)tiny[0], 'Q');
    ASSERT_FALSE(norm_duration_canonical("0.5ms", &got, out, sizeof(out)));
    ASSERT_STREQ(out, "");
    ASSERT_FALSE(norm_duration_canonical("1.s", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("1.", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical(".5s", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("1.5", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("1.5x", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("1mx", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("s", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("99999999999999999999s", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("9223372036854775807s", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("0.999999999999999h", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("9223372036854775.9s", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("1.1234567890123456s", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("+1s", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("1 s", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("1S", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_duration_canonical("1.2.3s", &got, out, sizeof(out)));
    ASSERT_EQ_INT(got, 1);
}

TEST(size_canonical_examples)
{
    assert_size("1536B", 1536, "1.5KiB");
    assert_size("1024B", 1024, "1KiB");
    assert_size("512B", 512, "512B");
    assert_size("1025B", 1025, "1025B");
    assert_size("1048576B", 1048576, "1MiB");
    assert_size("1.5KiB", 1536, "1.5KiB");
    assert_size("1.5MiB", 1572864, "1.5MiB");
    assert_size("0.5KiB", 512, "512B");
    assert_size("0B", 0, "0B");
    assert_size("2.00KiB", 2048, "2KiB");
    assert_size("1024KiB", 1048576, "1MiB");
    assert_size("1536KiB", 1572864, "1.5MiB");
    assert_size("0KiB", 0, "0B");
    assert_size("0MiB", 0, "0B");
}

TEST(size_rejects_a_fraction_of_a_byte)
{
    char out[CANON];
    long long got = 4;

    ASSERT_FALSE(norm_size_canonical("0.5B", &got, out, sizeof(out)));
    ASSERT_FALSE(norm_size_canonical("1X", &got, out, sizeof(out)));
    ASSERT_STREQ(out, "");
    ASSERT_EQ_INT(got, 4);
}

TEST(integer_grammar)
{
    long long v = -1;

    ASSERT_TRUE(setting_integer("0", &v));
    ASSERT_EQ_INT(v, 0);
    ASSERT_TRUE(setting_integer("200", &v));
    ASSERT_EQ_INT(v, 200);
    ASSERT_TRUE(setting_integer("9223372036854775807", &v));
    ASSERT_EQ_INT(v, 9223372036854775807LL);
    ASSERT_FALSE(setting_integer("9223372036854775808", &v));
    ASSERT_FALSE(setting_integer("00", &v));
    ASSERT_FALSE(setting_integer("01", &v));
    ASSERT_FALSE(setting_integer("-1", &v));
    ASSERT_FALSE(setting_integer("+1", &v));
    ASSERT_FALSE(setting_integer("1.0", &v));
    ASSERT_FALSE(setting_integer("1a", &v));
    ASSERT_FALSE(setting_integer("", &v));
    ASSERT_FALSE(setting_integer(NULL, &v));
    ASSERT_FALSE(setting_integer("1", NULL));
    ASSERT_EQ_INT(v, 9223372036854775807LL);
}

TEST(key_grammar_folds_hyphens_and_requires_a_full_key)
{
    char out[SETTING_KEY_MAX + 1];
    char segment[SEGMENT_MAX + 2];
    char key[SETTING_KEY_MAX + 2];
    char small[SETTING_KEY_MAX];
    int n = 0;

    ASSERT_TRUE(setting_key_fold("remember.buckets.prefixes", out, sizeof(out)));
    ASSERT_STREQ(out, "remember.buckets.prefixes");
    ASSERT_TRUE(setting_key_fold("remember.search.candidate-pool", out, sizeof(out)));
    ASSERT_STREQ(out, "remember.search.candidate_pool");
    ASSERT_TRUE(setting_key_fold("remember.search.a1_b", out, sizeof(out)));
    ASSERT_TRUE(setting_key_fold("remember.a.b.c", out, sizeof(out)));

    memset(segment, 'b', (size_t)SEGMENT_MAX);
    segment[SEGMENT_MAX] = '\0';
    n = snprintf(key, sizeof(key), "remember.%s", segment);
    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(key));
    ASSERT_TRUE(setting_key_fold(key, out, sizeof(out)));

    memset(out, 'x', sizeof(out));
    ASSERT_FALSE(setting_key_fold("remember", out, sizeof(out)));
    ASSERT_STREQ(out, "");
    ASSERT_FALSE(setting_key_fold("remember.a.b.c.d", out, sizeof(out)));
    ASSERT_FALSE(setting_key_fold("Remember.buckets.prefixes", out, sizeof(out)));
    ASSERT_FALSE(setting_key_fold("remember..x", out, sizeof(out)));
    ASSERT_FALSE(setting_key_fold("remember.", out, sizeof(out)));
    ASSERT_FALSE(setting_key_fold(".remember.x", out, sizeof(out)));
    ASSERT_FALSE(setting_key_fold("remember.1abc", out, sizeof(out)));
    memset(out, 'x', sizeof(out));
    ASSERT_FALSE(setting_key_fold("remember.abC", out, sizeof(out)));
    ASSERT_STREQ(out, "");
    memset(out, 'x', sizeof(out));
    ASSERT_FALSE(setting_key_fold("remember.ab~", out, sizeof(out)));
    ASSERT_STREQ(out, "");
    memset(out, 'x', sizeof(out));
    ASSERT_FALSE(setting_key_fold("remember.ab:", out, sizeof(out)));
    ASSERT_STREQ(out, "");
    memset(out, 'x', sizeof(out));
    ASSERT_FALSE(setting_key_fold("remember.~a", out, sizeof(out)));
    ASSERT_STREQ(out, "");
    ASSERT_FALSE(setting_key_fold("remember.-a", out, sizeof(out)));
    ASSERT_FALSE(setting_key_fold("", out, sizeof(out)));
    ASSERT_FALSE(setting_key_fold(NULL, out, sizeof(out)));
    ASSERT_FALSE(setting_key_fold("remember.x", NULL, sizeof(out)));
    ASSERT_FALSE(setting_key_fold("remember.x", small, sizeof(small)));
    {
        char one = 'Q';

        ASSERT_FALSE(setting_key_fold("remember.x", &one, 0));
        ASSERT_EQ_INT(one, 'Q');
    }

    memset(segment, 'b', (size_t)SEGMENT_MAX + 1U);
    segment[SEGMENT_MAX + 1U] = '\0';
    n = snprintf(key, sizeof(key), "remember.%s", segment);
    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(key));
    ASSERT_FALSE(setting_key_fold(key, out, sizeof(out)));

    /* 96 fits: remember. + 40 + . + 40 + . + 5. One more character is 97. */
    n = snprintf(key, sizeof(key), "remember.%.*s.%.*s.ccccc", SEGMENT_MAX, segment, SEGMENT_MAX,
                 segment);
    ASSERT_EQ_INT(n, 96);
    ASSERT_TRUE(setting_key_fold(key, out, sizeof(out)));
    ASSERT_STREQ(out, key);
    n = snprintf(key, sizeof(key), "remember.%.*s.%.*s.cccccc", SEGMENT_MAX, segment, SEGMENT_MAX,
                 segment);
    ASSERT_EQ_INT(n, 97);
    ASSERT_FALSE(setting_key_fold(key, out, sizeof(out)));
}

TEST(registry_lists_the_six_keys)
{
    size_t n = 0;
    const SettingSpec *all = setting_registry(&n);
    const SettingSpec *pool = NULL;
    const SettingSpec *prefixes = NULL;
    const SettingSpec *budget = NULL;
    const SettingSpec *max_results = NULL;
    const SettingSpec *idle = NULL;
    const SettingSpec *helper = NULL;

    ASSERT_EQ_INT((long long)n, 6);
    ASSERT_TRUE(setting_registry(NULL) == all);
    pool = setting_by_key("remember.search.candidate-pool");
    ASSERT_TRUE(pool != NULL);
    if (pool == NULL) {
        return;
    }
    ASSERT_EQ_INT(pool->type, SETTING_INTEGER);
    ASSERT_STREQ(pool->default_text, "200");
    ASSERT_EQ_INT(pool->min_value, 20);
    ASSERT_EQ_INT(pool->max_value, 2000);
    ASSERT_TRUE(pool->allow_brain);
    ASSERT_TRUE(pool->allow_user);
    prefixes = setting_by_key("remember.buckets.prefixes");
    ASSERT_TRUE(prefixes != NULL);
    if (prefixes == NULL) {
        return;
    }
    ASSERT_EQ_INT(prefixes->type, SETTING_STRING_LIST);
    ASSERT_TRUE(prefixes->default_text == NULL);
    ASSERT_TRUE(prefixes->allow_brain);
    ASSERT_FALSE(prefixes->allow_user);
    budget = setting_by_key("remember.plugins.chain_budget");
    ASSERT_TRUE(budget != NULL);
    if (budget == NULL) {
        return;
    }
    ASSERT_EQ_INT(budget->type, SETTING_DURATION);
    ASSERT_STREQ(budget->default_text, "1.5s");
    ASSERT_EQ_INT(budget->min_value, 100);
    ASSERT_EQ_INT(budget->max_value, 30000);
    ASSERT_FALSE(budget->allow_brain);
    ASSERT_TRUE(budget->allow_user);
    max_results = setting_by_key("remember.search.max_results");
    ASSERT_TRUE(max_results != NULL);
    if (max_results == NULL) {
        return;
    }
    ASSERT_EQ_INT(max_results->type, SETTING_INTEGER);
    ASSERT_STREQ(max_results->default_text, "20");
    ASSERT_EQ_INT(max_results->min_value, 1);
    ASSERT_EQ_INT(max_results->max_value, 1000);
    ASSERT_TRUE(max_results->allow_brain);
    ASSERT_TRUE(max_results->allow_user);
    idle = setting_by_key("remember.plugins.idle_timeout");
    ASSERT_TRUE(idle != NULL);
    if (idle == NULL) {
        return;
    }
    ASSERT_EQ_INT(idle->type, SETTING_DURATION);
    ASSERT_STREQ(idle->default_text, "10m");
    ASSERT_EQ_INT(idle->min_value, 60000);
    ASSERT_EQ_INT(idle->max_value, 86400000);
    ASSERT_FALSE(idle->allow_brain);
    ASSERT_TRUE(idle->allow_user);
    helper = setting_by_key("remember.sync.credential_helper");
    ASSERT_TRUE(helper != NULL);
    if (helper == NULL) {
        return;
    }
    ASSERT_EQ_INT(helper->type, SETTING_STRING);
    ASSERT_TRUE(helper->default_text == NULL);
    ASSERT_EQ_INT(helper->min_value, 0);
    ASSERT_EQ_INT(helper->max_value, 0);
    ASSERT_FALSE(helper->allow_brain);
    ASSERT_TRUE(helper->allow_user);
    ASSERT_TRUE(setting_by_key("remember.search.other") == NULL);
    ASSERT_TRUE(setting_by_key("candidate_pool") == NULL);
}

/* Single-threaded tests. getenv and setenv have no portable reentrant form. */
static void restore_env(const char *name, bool had, const char *saved)
{
    if (had) {
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        (void)setenv(name, saved, 1);
    } else {
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        (void)unsetenv(name);
    }
}

TEST(user_db_path_follows_remember_user_db_then_xdg_then_home)
{
    char buf[REMEMBER_PATH_MAX];
    char err[128];
    char saved_user[REMEMBER_PATH_MAX] = {0};
    char saved_xdg[REMEMBER_PATH_MAX] = {0};
    char saved_home[REMEMBER_PATH_MAX] = {0};
    char expect[REMEMBER_PATH_MAX];
    const char *home = NULL;
    bool had_user = false;
    bool had_xdg = false;
    bool had_home = false;
    const char *env = NULL;

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    env = getenv("REMEMBER_USER_DB");
    if (env != NULL) {
        had_user = true;
        (void)snprintf(saved_user, sizeof(saved_user), "%s", env);
    }
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    env = getenv("XDG_DATA_HOME");
    if (env != NULL) {
        had_xdg = true;
        (void)snprintf(saved_xdg, sizeof(saved_xdg), "%s", env);
    }
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    home = getenv("HOME");
    ASSERT_TRUE(home != NULL && home[0] != '\0');
    if (home != NULL) {
        had_home = true;
        (void)snprintf(saved_home, sizeof(saved_home), "%s", home);
    }

    (void)unlink("/tmp/remember-explicit-user.db");
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    ASSERT_EQ_INT(setenv("REMEMBER_USER_DB", "/tmp/remember-explicit-user.db", 1), 0);
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    ASSERT_EQ_INT(setenv("XDG_DATA_HOME", "/tmp/remember-xdg", 1), 0);
    ASSERT_EQ_INT(util_resolve_user_db_path(buf, sizeof(buf), err, sizeof(err)), 0);
    ASSERT_STREQ(buf, "/tmp/remember-explicit-user.db");
    ASSERT_TRUE(access(buf, F_OK) != 0);

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    ASSERT_EQ_INT(setenv("REMEMBER_USER_DB", "", 1), 0);
    ASSERT_EQ_INT(util_resolve_user_db_path(buf, sizeof(buf), err, sizeof(err)), 0);
    ASSERT_STREQ(buf, "/tmp/remember-xdg/remember/user.db");

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    (void)unsetenv("REMEMBER_USER_DB");
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    ASSERT_EQ_INT(setenv("XDG_DATA_HOME", "", 1), 0);
    ASSERT_EQ_INT(util_resolve_user_db_path(buf, sizeof(buf), err, sizeof(err)), 0);
    (void)snprintf(expect, sizeof(expect), "%s/.remember/user.db", saved_home);
    ASSERT_STREQ(buf, expect);

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    (void)unsetenv("XDG_DATA_HOME");
    ASSERT_EQ_INT(util_resolve_user_db_path(buf, sizeof(buf), err, sizeof(err)), 0);
    ASSERT_STREQ(buf, expect);

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    ASSERT_EQ_INT(setenv("REMEMBER_USER_DB", ":memory:", 1), 0);
    ASSERT_TRUE(util_resolve_user_db_path(buf, sizeof(buf), err, sizeof(err)) != 0);
    ASSERT_STREQ(err, "database path must be a regular file (not :memory: or file: URI)");
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    ASSERT_EQ_INT(setenv("REMEMBER_USER_DB", "file:user.db", 1), 0);
    ASSERT_TRUE(util_resolve_user_db_path(buf, sizeof(buf), err, sizeof(err)) != 0);
    ASSERT_STREQ(err, "database path must be a regular file (not :memory: or file: URI)");

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    (void)unsetenv("REMEMBER_USER_DB");
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    (void)unsetenv("HOME");
    ASSERT_TRUE(util_resolve_user_db_path(buf, sizeof(buf), err, sizeof(err)) != 0);
    ASSERT_STREQ(err, "HOME is not set");

    restore_env("HOME", had_home, saved_home);
    restore_env("REMEMBER_USER_DB", had_user, saved_user);
    restore_env("XDG_DATA_HOME", had_xdg, saved_xdg);
}

TEST(user_db_is_created_on_ensure_not_when_a_brain_opens)
{
    char tmpl[] = "/tmp/remember-udb-XXXXXX";
    const char *dir = NULL;
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char linkpath[REMEMBER_PATH_MAX];
    char target[REMEMBER_PATH_MAX];
    char err[256];
    struct stat st;
    Store *store = NULL;
    char *line = NULL;

    ASSERT_TRUE(store_user_db_ensure(NULL, err, sizeof(err)) != 0);
    ASSERT_STREQ(err, "empty database path");
    ASSERT_TRUE(store_user_db_ensure("", err, sizeof(err)) != 0);
    ASSERT_STREQ(err, "empty database path");

    dir = mkdtemp(tmpl);
    ASSERT_TRUE(dir != NULL);
    if (dir == NULL) {
        return;
    }
    (void)snprintf(brain, sizeof(brain), "%s/brain.db", dir);
    (void)snprintf(userdb, sizeof(userdb), "%s/nested/user.db", dir);

    store = store_open(brain, err, sizeof(err));
    ASSERT_TRUE(store != NULL);
    if (store == NULL) {
        harness_remove_tree(dir);
        return;
    }
    store_close(store);
    ASSERT_TRUE(access(userdb, F_OK) != 0);

    ASSERT_EQ_INT(store_user_db_ensure(userdb, err, sizeof(err)), 0);
    ASSERT_EQ_INT(stat(userdb, &st), 0);
    ASSERT_EQ_INT((int)(st.st_mode & (unsigned)0777), 0600);
    line = harness_sqlite_query_line(userdb, "PRAGMA user_version;");
    ASSERT_STREQ(line, "1");
    free(line);
    line = harness_sqlite_query_line(userdb, "SELECT min_reader_version FROM user_meta;");
    ASSERT_STREQ(line, "0.3.0");
    free(line);
    line = harness_sqlite_query_line(userdb, "SELECT COUNT(*) FROM user_meta;");
    ASSERT_STREQ(line, "1");
    free(line);
    line = harness_sqlite_query_line(userdb, "SELECT COUNT(*) FROM settings;");
    ASSERT_STREQ(line, "0");
    free(line);
    free(harness_sqlite_query_line(
        userdb, "INSERT INTO settings (key, value, updated_by, updated_at) VALUES "
                "('remember.search.max_results', '20', 'user', '2020-01-01T00:00:00Z');"));
    line = harness_sqlite_query_line(userdb, "SELECT COUNT(*) FROM settings_history;");
    ASSERT_STREQ(line, "0");
    free(line);
    line = harness_sqlite_query_line(
        userdb, "SELECT COUNT(*) FROM pragma_table_info('settings') WHERE name IN "
                "('device_id','version_vector','deleted_at','change_seq');");
    ASSERT_STREQ(line, "0");
    free(line);

    ASSERT_EQ_INT(chmod(userdb, 0644), 0);
    ASSERT_EQ_INT(store_user_db_ensure(userdb, err, sizeof(err)), 0);
    ASSERT_EQ_INT(stat(userdb, &st), 0);
    ASSERT_EQ_INT((int)(st.st_mode & (unsigned)0777), 0600);
    line = harness_sqlite_query_line(userdb, "SELECT COUNT(*) FROM user_meta;");
    ASSERT_STREQ(line, "1");
    free(line);
    line = harness_sqlite_query_line(
        userdb, "SELECT value FROM settings WHERE key = 'remember.search.max_results';");
    ASSERT_STREQ(line, "20");
    free(line);

    free(harness_sqlite_query_line(userdb, "PRAGMA user_version = 2;"));
    ASSERT_TRUE(store_user_db_ensure(userdb, err, sizeof(err)) != 0);
    ASSERT_STREQ(err, "database is newer than this remember");
    line = harness_sqlite_query_line(userdb, "PRAGMA user_version;");
    ASSERT_STREQ(line, "2");
    free(line);
    line = harness_sqlite_query_line(userdb, "SELECT COUNT(*) FROM user_meta;");
    ASSERT_STREQ(line, "1");
    free(line);

    free(harness_sqlite_query_line(userdb, "PRAGMA user_version = -1;"));
    ASSERT_TRUE(store_user_db_ensure(userdb, err, sizeof(err)) != 0);
    ASSERT_STREQ(err, "unsupported database version");
    line = harness_sqlite_query_line(userdb, "PRAGMA user_version;");
    ASSERT_STREQ(line, "-1");
    free(line);

    (void)snprintf(target, sizeof(target), "%s/secret.db", dir);
    (void)snprintf(linkpath, sizeof(linkpath), "%s/link.db", dir);
    ASSERT_EQ_INT(symlink(target, linkpath), 0);
    ASSERT_TRUE(store_user_db_ensure(linkpath, err, sizeof(err)) != 0);
    ASSERT_STREQ(err, "cannot create user.db");
    ASSERT_TRUE(access(target, F_OK) != 0);

    harness_remove_tree(dir);
}

TEST(ensure_leaves_a_non_database_file_unchanged)
{
    char tmpl[] = "/tmp/remember-udb-XXXXXX";
    const char *dir = NULL;
    char path[REMEMBER_PATH_MAX];
    char err[256];
    char got[16];
    struct stat st;
    int fd = -1;
    ssize_t n = 0;

    dir = mkdtemp(tmpl);
    ASSERT_TRUE(dir != NULL);
    if (dir == NULL) {
        return;
    }
    (void)snprintf(path, sizeof(path), "%s/notes.txt", dir);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    ASSERT_TRUE(fd >= 0);
    if (fd < 0) {
        harness_remove_tree(dir);
        return;
    }
    n = write(fd, "notes\n", 6);
    ASSERT_EQ_INT(fchmod(fd, 0644), 0);
    ASSERT_EQ_INT(close(fd), 0);
    ASSERT_EQ_INT((int)n, 6);

    ASSERT_TRUE(store_user_db_ensure(path, err, sizeof(err)) != 0);
    ASSERT_STREQ(err, "cannot read user_version: file is not a database");
    fd = open(path, O_RDONLY | O_CLOEXEC);
    ASSERT_TRUE(fd >= 0);
    if (fd < 0) {
        harness_remove_tree(dir);
        return;
    }
    ASSERT_EQ_INT(fstat(fd, &st), 0);
    ASSERT_EQ_INT((int)(st.st_mode & (unsigned)0777), 0644);
    n = read(fd, got, sizeof(got) - 1U);
    ASSERT_EQ_INT(close(fd), 0);
    ASSERT_TRUE(n >= 0);
    if (n < 0) {
        harness_remove_tree(dir);
        return;
    }
    got[n] = '\0';
    ASSERT_STREQ(got, "notes\n");
    harness_remove_tree(dir);
}

static const char k_human[] = "{\"kind\":\"human\",\"id\":null}";
static const char k_brain_quota[] =
    "this brain has 256 settings, the most it can hold. Reset settings you don't need with "
    "'remember settings reset'.";
static const char k_idle_scope[] =
    "remember.plugins.idle_timeout is a user setting; it can't be set for one brain. Run the "
    "command again with --scope user, or without --scope.";
static const char k_newer_brain[] =
    "this brain was written by a newer remember. You can read it. Upgrade to write to it.";

// NOLINTBEGIN(bugprone-easily-swappable-parameters) paths, then the SQL or the key
static Store *open_pair(char *tmpl, char *brain, size_t brain_cap, char *userdb, size_t user_cap)
{
    const char *dir = NULL;
    char err[256];
    Store *store = NULL;

    dir = mkdtemp(tmpl);
    ASSERT_TRUE(dir != NULL);
    if (dir == NULL) {
        return NULL;
    }
    (void)snprintf(brain, brain_cap, "%s/brain.db", dir);
    (void)snprintf(userdb, user_cap, "%s/user.db", dir);
    store = store_open(brain, err, sizeof(err));
    ASSERT_TRUE(store != NULL);
    if (store == NULL) {
        harness_remove_tree(tmpl);
    }
    return store;
}

static char *q(const char *db, const char *sql)
{
    return harness_sqlite_query_line(db, sql);
}

static void close_pair(Store *store, const char *tmpl)
{
    store_close(store);
    harness_remove_tree(tmpl);
}

static void exec_fixture(const char *db, const char *sql)
{
    free(q(db, sql));
}

static void assert_query(const char *db, const char *sql, const char *expect)
{
    char *line = q(db, sql);

    ASSERT_STREQ(line, expect);
    free(line);
}

static SettingStatus set_items(Store *store, const char *userdb, const char *key,
                               const char *const *items, size_t nitems, SettingLayer layer,
                               const char *actor, SettingWriteResult *wr, char *err, size_t errlen)
{
    SettingWrite w;

    memset(&w, 0, sizeof(w));
    w.key = key;
    w.items = items;
    w.nitems = nitems;
    w.layer = layer;
    w.updated_by = actor;
    return store_setting_set(store, userdb, &w, wr, err, errlen);
}

static SettingStatus set_one(Store *store, const char *userdb, const char *key, const char *value,
                             SettingLayer layer, const char *actor, SettingWriteResult *wr,
                             char *err, size_t errlen)
{
    const char *item = value;

    return set_items(store, userdb, key, &item, 1U, layer, actor, wr, err, errlen);
}
// NOLINTEND(bugprone-easily-swappable-parameters)

static int dummy_settings_head(char *sql, size_t cap, bool brain)
{
    if (brain) {
        return snprintf(sql, cap,
                        "INSERT INTO settings (key, value, updated_by, updated_at, device_id, "
                        "version_vector, deleted_at, change_seq) VALUES ");
    }
    return snprintf(sql, cap, "INSERT INTO settings (key, value, updated_by, updated_at) VALUES ");
}

/* cap and pos are adjacent sizes. The order is buffer, then the write offset. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static int dummy_settings_row(char *sql, size_t cap, int pos, bool brain, const char *sep,
                              int index)
{
    if (brain) {
        return snprintf(sql + pos, cap - (size_t)pos,
                        "%s('q%03d','1','{\"kind\":\"human\",\"id\":null}',"
                        "'2020-01-01T00:00:00.000Z','d','{}',NULL,0)",
                        sep, index);
    }
    return snprintf(sql + pos, cap - (size_t)pos,
                    "%s('q%03d','1','{\"kind\":\"human\",\"id\":null}',"
                    "'2020-01-01T00:00:00.000Z')",
                    sep, index);
}

static void insert_dummy_settings(const char *db, bool brain, int count)
{
    enum { BATCH = 40, SQL_CAP = 7000 };
    char sql[SQL_CAP];
    int base = 0;

    while (base < count) {
        int n = count - base;
        int i = 0;
        int pos = 0;
        int wrote = 0;
        const char *sep = "";

        if (n > BATCH) {
            n = BATCH;
        }
        wrote = dummy_settings_head(sql, sizeof(sql), brain);
        if (wrote < 0 || (size_t)wrote >= sizeof(sql)) {
            ASSERT_TRUE(wrote > 0);
            return;
        }
        pos = wrote;
        for (i = 0; i < n; i++) {
            wrote = dummy_settings_row(sql, sizeof(sql), pos, brain, sep, base + i);
            if (wrote < 0 || (size_t)wrote >= sizeof(sql) - (size_t)pos) {
                ASSERT_TRUE(wrote > 0);
                return;
            }
            pos += wrote;
            sep = ",";
        }
        if ((size_t)pos + 2U > sizeof(sql)) {
            ASSERT_TRUE(pos > 0);
            return;
        }
        sql[pos] = ';';
        sql[pos + 1] = '\0';
        exec_fixture(db, sql);
        base += n;
    }
}

static void insert_dummy_history(const char *db, int count)
{
    enum { BATCH = 40, SQL_CAP = 7000 };
    char sql[SQL_CAP];
    int base = 0;

    while (base < count) {
        int n = count - base;
        int i = 0;
        int pos = 0;
        int wrote = 0;

        if (n > BATCH) {
            n = BATCH;
        }
        wrote = snprintf(sql, sizeof(sql),
                         "INSERT INTO settings_history (key, old_value, new_value, updated_by, at) "
                         "VALUES ");
        if (wrote < 0 || (size_t)wrote >= sizeof(sql)) {
            ASSERT_TRUE(wrote > 0);
            return;
        }
        pos = wrote;
        for (i = 0; i < n; i++) {
            wrote = snprintf(sql + pos, sizeof(sql) - (size_t)pos,
                             "%s('h','1','2','{\"kind\":\"human\",\"id\":null}',"
                             "'2020-01-01T00:00:00.000Z')",
                             i == 0 ? "" : ",");
            if (wrote < 0 || (size_t)wrote >= sizeof(sql) - (size_t)pos) {
                ASSERT_TRUE(wrote > 0);
                return;
            }
            pos += wrote;
        }
        if ((size_t)pos + 2U > sizeof(sql)) {
            ASSERT_TRUE(pos > 0);
            return;
        }
        sql[pos] = ';';
        sql[pos + 1] = '\0';
        exec_fixture(db, sql);
        base += n;
    }
}

/* n prefixes "a00:" .. of three name characters. Caller frees the block and *items_out. */
static char *make_prefixes(size_t n, const char ***items_out)
{
    enum { SLOT = 8 };
    char *block = NULL;
    const char **items = NULL;
    size_t i = 0;

    block = (char *)malloc(n * (size_t)SLOT);
    items = (const char **)malloc(n * sizeof(*items));
    if (block == NULL || items == NULL) {
        free(block);
        free((void *)items);
        ASSERT_TRUE(block != NULL && items != NULL);
        return NULL;
    }
    for (i = 0; i < n; i++) {
        char *p = block + (i * (size_t)SLOT);

        (void)snprintf(p, (size_t)SLOT, "%c%02d:", 'a' + (int)(i / 100U), (int)(i % 100U));
        items[i] = p;
    }
    *items_out = items;
    return block;
}

static void write_notes(const char *path)
{
    int fd = -1;
    ssize_t n = 0;

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    ASSERT_TRUE(fd >= 0);
    if (fd < 0) {
        return;
    }
    n = write(fd, "notes\n", 6);
    ASSERT_EQ_INT(fchmod(fd, 0644), 0);
    ASSERT_EQ_INT(close(fd), 0);
    ASSERT_EQ_INT((int)n, 6);
}

static void assert_notes(const char *path)
{
    char got[16];
    struct stat st;
    int fd = -1;
    ssize_t n = 0;

    fd = open(path, O_RDONLY | O_CLOEXEC);
    ASSERT_TRUE(fd >= 0);
    if (fd < 0) {
        return;
    }
    ASSERT_EQ_INT(fstat(fd, &st), 0);
    ASSERT_EQ_INT((int)(st.st_mode & (unsigned)0777), 0644);
    n = read(fd, got, sizeof(got) - 1U);
    ASSERT_EQ_INT(close(fd), 0);
    ASSERT_TRUE(n >= 0);
    if (n < 0) {
        return;
    }
    got[n] = '\0';
    ASSERT_STREQ(got, "notes\n");
}

TEST(resolution_user_then_brain_then_tombstone_and_change_seq)
{
    char tmpl[] = "/tmp/remember-set-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[256];
    Store *store = NULL;
    SettingWrite w;
    SettingWriteResult wr;
    SettingRead got;
    char *seq = NULL;
    char *line = NULL;
    const char *one = "30";
    const char *two = "40";
    const char *again = "40";
    char expect[80];

    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    if (store == NULL) {
        return;
    }
    ASSERT_EQ_INT(store_setting_read(store, userdb, "remember.search.max_results", NULL, &got, err,
                                     sizeof(err)),
                  SETTING_OK);
    ASSERT_EQ_INT(got.from, SETTING_FROM_DEFAULT);
    ASSERT_STREQ(got.value, "20");
    ASSERT_FALSE(got.user_live);
    ASSERT_FALSE(got.brain_live);
    ASSERT_TRUE(access(userdb, F_OK) != 0);

    memset(&w, 0, sizeof(w));
    w.key = "remember.search.max_results";
    w.items = &one;
    w.nitems = 1;
    w.layer = SETTING_LAYER_USER;
    ASSERT_EQ_INT(store_setting_set(store, userdb, &w, &wr, err, sizeof(err)), SETTING_OK);
    ASSERT_TRUE(wr.changed);
    ASSERT_EQ_INT(wr.layer, SETTING_LAYER_USER);
    seq = q(brain, "SELECT change_seq FROM brain_meta;");
    ASSERT_STREQ(seq, "0");
    free(seq);
    line = q(userdb, "SELECT value FROM settings WHERE key = 'remember.search.max_results';");
    ASSERT_STREQ(line, "30");
    free(line);
    line = q(userdb, "SELECT updated_by FROM settings WHERE key = 'remember.search.max_results';");
    ASSERT_STREQ(line, k_human);
    free(line);

    w.items = &two;
    w.layer = SETTING_LAYER_BRAIN;
    ASSERT_EQ_INT(store_setting_set(store, userdb, &w, &wr, err, sizeof(err)), SETTING_OK);
    ASSERT_TRUE(wr.changed);
    seq = q(brain, "SELECT change_seq FROM brain_meta;");
    ASSERT_STREQ(seq, "1");
    free(seq);
    line = q(brain, "SELECT value FROM settings WHERE key = 'remember.search.max_results';");
    ASSERT_STREQ(line, "40");
    free(line);
    line = q(brain, "SELECT change_seq FROM settings WHERE key = 'remember.search.max_results';");
    ASSERT_STREQ(line, "1");
    free(line);
    (void)snprintf(expect, sizeof(expect), "{\"%s\":1}", store_device_id(store));
    line =
        q(brain, "SELECT version_vector FROM settings WHERE key = 'remember.search.max_results';");
    ASSERT_STREQ(line, expect);
    free(line);
    ASSERT_EQ_INT(store_setting_read(store, userdb, "remember.search.max_results", NULL, &got, err,
                                     sizeof(err)),
                  SETTING_OK);
    ASSERT_EQ_INT(got.from, SETTING_FROM_BRAIN);
    ASSERT_STREQ(got.value, "40");
    ASSERT_STREQ(got.user_value, "30");

    w.items = &again;
    ASSERT_EQ_INT(store_setting_set(store, userdb, &w, &wr, err, sizeof(err)), SETTING_OK);
    ASSERT_FALSE(wr.changed);
    seq = q(brain, "SELECT change_seq FROM brain_meta;");
    ASSERT_STREQ(seq, "1");
    free(seq);
    line = q(brain, "SELECT COUNT(*) FROM settings_history WHERE key = "
                    "'remember.search.max_results';");
    ASSERT_STREQ(line, "1");
    free(line);

    ASSERT_EQ_INT(store_setting_reset(store, userdb, "remember.search.max_results",
                                      SETTING_LAYER_BRAIN, NULL, &wr, err, sizeof(err)),
                  SETTING_OK);
    ASSERT_TRUE(wr.changed);
    seq = q(brain, "SELECT change_seq FROM brain_meta;");
    ASSERT_STREQ(seq, "2");
    free(seq);
    line = q(brain, "SELECT value FROM settings WHERE key = 'remember.search.max_results';");
    ASSERT_STREQ(line, "40");
    free(line);
    line = q(brain, "SELECT deleted_at FROM settings WHERE key = 'remember.search.max_results';");
    ASSERT_TRUE(line != NULL && line[0] != '\0');
    free(line);
    ASSERT_EQ_INT(store_setting_read(store, userdb, "remember.search.max_results", NULL, &got, err,
                                     sizeof(err)),
                  SETTING_OK);
    ASSERT_EQ_INT(got.from, SETTING_FROM_USER);
    ASSERT_STREQ(got.value, "30");
    ASSERT_FALSE(got.brain_live);

    line = q(brain, "SELECT COUNT(*) FROM settings_history WHERE key = "
                    "'remember.search.max_results';");
    ASSERT_STREQ(line, "2");
    free(line);
    ASSERT_EQ_INT(store_setting_reset(store, userdb, "remember.search.max_results",
                                      SETTING_LAYER_BRAIN, NULL, &wr, err, sizeof(err)),
                  SETTING_OK);
    ASSERT_FALSE(wr.changed);
    seq = q(brain, "SELECT change_seq FROM brain_meta;");
    ASSERT_STREQ(seq, "2");
    free(seq);
    line = q(brain, "SELECT COUNT(*) FROM settings_history WHERE key = "
                    "'remember.search.max_results';");
    ASSERT_STREQ(line, "2");
    free(line);

    w.items = &two;
    ASSERT_EQ_INT(store_setting_set(store, userdb, &w, &wr, err, sizeof(err)), SETTING_OK);
    ASSERT_TRUE(wr.changed);
    seq = q(brain, "SELECT change_seq FROM brain_meta;");
    ASSERT_STREQ(seq, "3");
    free(seq);
    line = q(brain, "SELECT change_seq FROM settings WHERE key = 'remember.search.max_results';");
    ASSERT_STREQ(line, "3");
    free(line);
    line = q(brain, "SELECT COUNT(*) FROM settings WHERE key = 'remember.search.max_results' AND "
                    "deleted_at IS NULL;");
    ASSERT_STREQ(line, "1");
    free(line);
    (void)snprintf(expect, sizeof(expect), "{\"%s\":3}", store_device_id(store));
    line = q(brain, "SELECT version_vector FROM settings WHERE key = "
                    "'remember.search.max_results';");
    ASSERT_STREQ(line, expect);
    free(line);
    line = q(brain, "SELECT COUNT(*) FROM settings_history WHERE key = "
                    "'remember.search.max_results' AND rowid = (SELECT MAX(rowid) FROM "
                    "settings_history WHERE key = 'remember.search.max_results') AND "
                    "old_value IS NULL AND new_value = '40';");
    ASSERT_STREQ(line, "1");
    free(line);
    ASSERT_EQ_INT(store_setting_read(store, userdb, "remember.search.max_results", NULL, &got, err,
                                     sizeof(err)),
                  SETTING_OK);
    ASSERT_EQ_INT(got.from, SETTING_FROM_BRAIN);
    ASSERT_STREQ(got.value, "40");
    ASSERT_TRUE(got.brain_live);

    store_close(store);
    harness_remove_tree(tmpl);
}

TEST(user_history_keeps_the_newest_fifty_by_rowid)
{
    char tmpl[] = "/tmp/remember-set-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[256];
    char num[8];
    Store *store = NULL;
    SettingWriteResult wr;
    SettingRead got;
    SettingStatus st = SETTING_OK;
    char *oldest = NULL;
    char *line = NULL;
    const char *actor = "{\"kind\":\"agent\",\"id\":\"bot1\"}";
    int i = 0;

    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    if (store == NULL) {
        return;
    }
    st = store_setting_reset(store, userdb, "remember.search.max_results", SETTING_LAYER_USER, NULL,
                             &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_FALSE(wr.changed);
    ASSERT_TRUE(access(userdb, F_OK) != 0);

    for (i = 1; i <= 51; i++) {
        (void)snprintf(num, sizeof(num), "%d", i);
        st = set_one(store, userdb, "remember.search.max_results", num, SETTING_LAYER_USER, NULL,
                     &wr, err, sizeof(err));
        ASSERT_EQ_INT(st, SETTING_OK);
        ASSERT_TRUE(wr.changed);
    }
    assert_query(userdb,
                 "SELECT COUNT(*) FROM settings_history WHERE key = "
                 "'remember.search.max_results';",
                 "50");
    assert_query(userdb,
                 "SELECT COUNT(*) FROM settings_history WHERE key = "
                 "'remember.search.max_results' AND new_value = '1';",
                 "0");
    assert_query(userdb,
                 "SELECT new_value FROM settings_history WHERE key = "
                 "'remember.search.max_results' ORDER BY rowid DESC LIMIT 1;",
                 "51");
    st = set_one(store, userdb, "remember.search.max_results", "51", SETTING_LAYER_USER, NULL, &wr,
                 err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_FALSE(wr.changed);
    assert_query(userdb,
                 "SELECT COUNT(*) FROM settings_history WHERE key = "
                 "'remember.search.max_results';",
                 "50");

    for (i = 52; i <= 61; i++) {
        (void)snprintf(num, sizeof(num), "%d", i);
        st = set_one(store, userdb, "remember.search.max_results", num, SETTING_LAYER_USER, NULL,
                     &wr, err, sizeof(err));
        ASSERT_EQ_INT(st, SETTING_OK);
    }
    oldest = q(userdb, "SELECT new_value FROM settings_history WHERE key = "
                       "'remember.search.max_results' ORDER BY rowid ASC LIMIT 1;");
    ASSERT_TRUE(oldest != NULL);
    exec_fixture(userdb, "UPDATE settings_history SET at = '2020-01-01T00:00:00.000Z' WHERE rowid "
                         "= (SELECT MAX(rowid) FROM settings_history WHERE key = "
                         "'remember.search.max_results');");
    st = set_one(store, userdb, "remember.search.max_results", "62", SETTING_LAYER_USER, NULL, &wr,
                 err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_TRUE(wr.changed);
    assert_query(userdb,
                 "SELECT COUNT(*) FROM settings_history WHERE key = "
                 "'remember.search.max_results' AND at = '2020-01-01T00:00:00.000Z';",
                 "1");
    if (oldest != NULL) {
        char probe[160];

        (void)snprintf(probe, sizeof(probe),
                       "SELECT COUNT(*) FROM settings_history WHERE key = "
                       "'remember.search.max_results' AND new_value = '%s';",
                       oldest);
        assert_query(userdb, probe, "0");
    }
    free(oldest);
    assert_query(userdb,
                 "SELECT COUNT(*) FROM settings_history WHERE key = "
                 "'remember.search.max_results';",
                 "50");
    assert_query(userdb,
                 "SELECT new_value FROM settings_history WHERE key = "
                 "'remember.search.max_results' ORDER BY rowid DESC LIMIT 1;",
                 "62");

    st = store_setting_reset(store, userdb, "remember.search.max_results", SETTING_LAYER_USER, NULL,
                             &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_TRUE(wr.changed);
    assert_query(userdb, "SELECT COUNT(*) FROM settings WHERE key = 'remember.search.max_results';",
                 "0");
    assert_query(userdb,
                 "SELECT COUNT(*) FROM settings_history WHERE key = "
                 "'remember.search.max_results' AND rowid = (SELECT MAX(rowid) FROM "
                 "settings_history WHERE key = 'remember.search.max_results') AND "
                 "new_value IS NULL;",
                 "1");
    st = store_setting_read(store, userdb, "remember.search.max_results", NULL, &got, err,
                            sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_EQ_INT(got.from, SETTING_FROM_DEFAULT);
    ASSERT_STREQ(got.value, "20");
    ASSERT_FALSE(got.user_live);
    st = store_setting_reset(store, userdb, "remember.search.max_results", SETTING_LAYER_USER, NULL,
                             &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_FALSE(wr.changed);
    assert_query(userdb,
                 "SELECT COUNT(*) FROM settings_history WHERE key = "
                 "'remember.search.max_results';",
                 "50");
    assert_query(brain, "SELECT change_seq FROM brain_meta;", "0");

    st = set_one(store, userdb, "remember.search.max_results", "7", SETTING_LAYER_USER, actor, &wr,
                 err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_TRUE(wr.changed);
    assert_query(userdb,
                 "SELECT updated_by FROM settings WHERE key = 'remember.search.max_results';",
                 actor);
    line = q(userdb, "SELECT updated_at FROM settings WHERE key = 'remember.search.max_results';");
    ASSERT_TRUE(line != NULL && utc_stamp_is_canonical(line));
    free(line);
    assert_query(userdb,
                 "SELECT updated_by FROM settings WHERE key = 'remember.search.max_results';",
                 actor);
    close_pair(store, tmpl);
}

TEST(quota_counts_settings_rows_not_history)
{
    char tmpl[] = "/tmp/remember-set-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[256];
    Store *store = NULL;
    SettingWriteResult wr;
    SettingStatus st = SETTING_OK;

    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    if (store == NULL) {
        return;
    }
    st = set_one(store, userdb, "remember.search.max_results", "40", SETTING_LAYER_BRAIN, NULL, &wr,
                 err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    st = store_setting_reset(store, userdb, "remember.search.max_results", SETTING_LAYER_BRAIN,
                             NULL, &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_TRUE(wr.changed);
    insert_dummy_settings(brain, true, SETTING_ROW_QUOTA - 1);
    st = set_one(store, userdb, "remember.search.candidate_pool", "30", SETTING_LAYER_BRAIN, NULL,
                 &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_QUOTA);
    ASSERT_STREQ(setting_status_code(st), "quota_exceeded");
    ASSERT_STREQ(err, k_brain_quota);
    ASSERT_FALSE(wr.changed);
    assert_query(brain, "SELECT change_seq FROM brain_meta;", "2");
    assert_query(
        brain, "SELECT COUNT(*) FROM settings WHERE key = 'remember.search.candidate_pool';", "0");
    st = set_one(store, userdb, "remember.search.max_results", "41", SETTING_LAYER_BRAIN, NULL, &wr,
                 err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_TRUE(wr.changed);
    assert_query(brain, "SELECT value FROM settings WHERE key = 'remember.search.max_results';",
                 "41");
    assert_query(brain,
                 "SELECT COUNT(*) FROM settings WHERE key = 'remember.search.max_results' "
                 "AND deleted_at IS NULL;",
                 "1");
    assert_query(brain, "SELECT change_seq FROM brain_meta;", "3");
    st = set_one(store, userdb, "remember.search.candidate_pool", "30", SETTING_LAYER_BRAIN, NULL,
                 &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_QUOTA);
    exec_fixture(brain, "DELETE FROM settings WHERE key = 'q000';");
    st = set_one(store, userdb, "remember.search.candidate_pool", "30", SETTING_LAYER_BRAIN, NULL,
                 &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_TRUE(wr.changed);
    assert_query(brain, "SELECT change_seq FROM brain_meta;", "4");

    ASSERT_EQ_INT(store_user_db_ensure(userdb, err, sizeof(err)), 0);
    insert_dummy_history(userdb, 300);
    insert_dummy_settings(userdb, false, SETTING_ROW_QUOTA - 1);
    st = set_one(store, userdb, "remember.search.max_results", "6", SETTING_LAYER_USER, NULL, &wr,
                 err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_TRUE(wr.changed);
    assert_query(userdb, "SELECT COUNT(*) FROM settings_history WHERE key = 'h';", "300");
    st = set_one(store, userdb, "remember.search.candidate_pool", "20", SETTING_LAYER_USER, NULL,
                 &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_QUOTA);
    ASSERT_STREQ(setting_status_code(st), "quota_exceeded");
    ASSERT_STREQ(err, "");
    assert_query(brain, "SELECT change_seq FROM brain_meta;", "4");
    exec_fixture(userdb, "DELETE FROM settings WHERE key = 'q000';");
    st = set_one(store, userdb, "remember.search.candidate_pool", "20", SETTING_LAYER_USER, NULL,
                 &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    assert_query(userdb, "SELECT value FROM settings WHERE key = 'remember.search.candidate_pool';",
                 "20");
    close_pair(store, tmpl);
}

TEST(prefixes_default_scope_and_sixteen_kib)
{
    enum { PREFIX_FITS = 2340, PREFIX_OVER = 2341, PREFIX_BEFORE = 2339, PREFIX_JSON_FITS = 16381 };
    char tmpl[] = "/tmp/remember-set-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[256];
    char tail[16];
    Store *store = NULL;
    SettingWriteResult wr;
    SettingRead got;
    SettingStatus st = SETTING_OK;
    const char *pair[] = {"project:", "area:"};
    const char *bad = "my project:";
    const char *twice[] = {"status:", "status:"};
    const char *same[] = {"Status:", "status:"};
    const char **shortest = NULL;
    const char **items = NULL;
    const char **extra = NULL;
    char *block = NULL;
    size_t nshort = ((size_t)SETTING_VALUE_MAX / 5U) + 1U;
    size_t i = 0;

    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    if (store == NULL) {
        return;
    }
    st = store_setting_read(store, userdb, "remember.buckets.prefixes", NULL, &got, err,
                            sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_EQ_INT(got.from, SETTING_FROM_DEFAULT);
    ASSERT_STREQ(got.value, "[\"project:\",\"status:\"]");
    ASSERT_TRUE(access(userdb, F_OK) != 0);

    st = set_items(store, NULL, "remember.buckets.prefixes", pair, 2U, SETTING_LAYER_AUTO, NULL,
                   &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_TRUE(wr.changed);
    ASSERT_EQ_INT(wr.layer, SETTING_LAYER_BRAIN);
    assert_query(brain, "SELECT value FROM settings WHERE key = 'remember.buckets.prefixes';",
                 "[\"project:\",\"area:\"]");
    assert_query(brain, "SELECT change_seq FROM brain_meta;", "1");
    ASSERT_TRUE(access(userdb, F_OK) != 0);

    st = set_items(store, userdb, "remember.buckets.prefixes", pair, 2U, SETTING_LAYER_USER, NULL,
                   &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_SCOPE);
    ASSERT_STREQ(setting_status_code(st), "setting_scope_mismatch");
    ASSERT_STREQ(err, "");
    assert_query(brain, "SELECT change_seq FROM brain_meta;", "1");
    ASSERT_TRUE(access(userdb, F_OK) != 0);

    st = set_items(store, NULL, "remember.buckets.prefixes", &bad, 1U, SETTING_LAYER_AUTO, NULL,
                   &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_INVALID);
    ASSERT_STREQ(err, "\"my project:\" isn't a bucket prefix.");
    st = set_items(store, NULL, "remember.buckets.prefixes", twice, 2U, SETTING_LAYER_AUTO, NULL,
                   &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_INVALID);
    ASSERT_STREQ(err, "\"status:\" is in the list twice.");
    st = set_items(store, NULL, "remember.buckets.prefixes", same, 2U, SETTING_LAYER_AUTO, NULL,
                   &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_INVALID);
    ASSERT_STREQ(err, "\"status:\" and \"Status:\" are the same prefix.");
    assert_query(brain, "SELECT value FROM settings WHERE key = 'remember.buckets.prefixes';",
                 "[\"project:\",\"area:\"]");

    shortest = (const char **)malloc(nshort * sizeof(*shortest));
    ASSERT_TRUE(shortest != NULL);
    if (shortest == NULL) {
        close_pair(store, tmpl);
        return;
    }
    for (i = 0; i < nshort; i++) {
        shortest[i] = "a:";
    }
    st = set_items(store, NULL, "remember.buckets.prefixes", shortest, nshort, SETTING_LAYER_AUTO,
                   NULL, &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_QUOTA);
    ASSERT_STREQ(setting_status_code(st), "quota_exceeded");
    ASSERT_STREQ(err, "");
    free((void *)shortest);

    block = make_prefixes((size_t)PREFIX_OVER, &items);
    if (block == NULL) {
        close_pair(store, tmpl);
        return;
    }
    st = set_items(store, NULL, "remember.buckets.prefixes", items, (size_t)PREFIX_OVER,
                   SETTING_LAYER_AUTO, NULL, &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_QUOTA);
    ASSERT_STREQ(err, "");
    free(block);
    free((void *)items);

    block = make_prefixes((size_t)PREFIX_BEFORE, &items);
    extra = (const char **)malloc(((size_t)PREFIX_BEFORE + 1U) * sizeof(*extra));
    if (block == NULL || extra == NULL) {
        free(block);
        free((void *)items);
        free((void *)extra);
        close_pair(store, tmpl);
        return;
    }
    for (i = 0; i < (size_t)PREFIX_BEFORE; i++) {
        extra[i] = items[i];
    }
    extra[PREFIX_BEFORE] = "abcdefg:";
    st = set_items(store, NULL, "remember.buckets.prefixes", extra, (size_t)PREFIX_BEFORE + 1U,
                   SETTING_LAYER_AUTO, NULL, &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_QUOTA);
    ASSERT_STREQ(err, "");
    free(block);
    free((void *)items);
    free((void *)extra);
    assert_query(brain, "SELECT value FROM settings WHERE key = 'remember.buckets.prefixes';",
                 "[\"project:\",\"area:\"]");
    assert_query(brain, "SELECT change_seq FROM brain_meta;", "1");

    block = make_prefixes((size_t)PREFIX_FITS, &items);
    if (block == NULL) {
        close_pair(store, tmpl);
        return;
    }
    st = set_items(store, NULL, "remember.buckets.prefixes", items, (size_t)PREFIX_FITS,
                   SETTING_LAYER_AUTO, NULL, &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_TRUE(wr.changed);
    st = store_setting_read(store, NULL, "remember.buckets.prefixes", NULL, &got, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_EQ_INT(got.from, SETTING_FROM_BRAIN);
    ASSERT_EQ_INT((int)strlen(got.value), PREFIX_JSON_FITS);
    ASSERT_TRUE(strncmp(got.value, "[\"a00:\"", 7) == 0);
    (void)snprintf(tail, sizeof(tail), "\"%c%02d:\"]", 'a' + ((PREFIX_FITS - 1) / 100),
                   (PREFIX_FITS - 1) % 100);
    ASSERT_STREQ(got.value + PREFIX_JSON_FITS - (int)strlen(tail), tail);
    assert_query(brain, "SELECT change_seq FROM brain_meta;", "2");
    ASSERT_TRUE(access(userdb, F_OK) != 0);
    free(block);
    free((void *)items);
    close_pair(store, tmpl);
}

TEST(rejected_values_do_not_create_user_db)
{
    char tmpl[] = "/tmp/remember-set-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char user2[REMEMBER_PATH_MAX];
    char err[256];
    Store *store = NULL;
    SettingWriteResult wr;
    SettingRead got;
    SettingStatus st = SETTING_OK;
    const char *project = "project:";

    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    if (store == NULL) {
        return;
    }
    (void)snprintf(user2, sizeof(user2), "%s/real.db", tmpl);
    st = store_setting_reset(store, userdb, "remember.search.max_results", SETTING_LAYER_BRAIN,
                             NULL, &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_FALSE(wr.changed);
    assert_query(brain, "SELECT change_seq FROM brain_meta;", "0");

    st = set_one(store, userdb, "remember.plugins.idle_timeout", "10m", SETTING_LAYER_BRAIN, NULL,
                 &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_SCOPE);
    ASSERT_STREQ(setting_status_code(st), "setting_scope_mismatch");
    ASSERT_STREQ(err, k_idle_scope);
    st = set_one(store, userdb, "remember.plugins.chain_budget", "1.5s", SETTING_LAYER_BRAIN, NULL,
                 &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_SCOPE);
    ASSERT_STREQ(err, "");
    st = set_items(store, userdb, "remember.buckets.prefixes", &project, 1U, SETTING_LAYER_USER,
                   NULL, &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_SCOPE);
    ASSERT_STREQ(err, "");
    ASSERT_TRUE(access(userdb, F_OK) != 0);
    assert_query(brain, "SELECT COUNT(*) FROM settings;", "0");

    st = set_one(store, userdb, "remember.plugins.chain_budget", "0.5ms", SETTING_LAYER_USER, NULL,
                 &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_INVALID);
    ASSERT_STREQ(setting_status_code(st), "setting_invalid_value");
    ASSERT_STREQ(err, "");
    st = set_one(store, userdb, "remember.plugins.chain_budget", "45s", SETTING_LAYER_USER, NULL,
                 &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_RANGE);
    ASSERT_STREQ(setting_status_code(st), "setting_out_of_range");
    ASSERT_STREQ(err, "");
    st = set_one(store, userdb, "remember.search.candidate_pool", "19", SETTING_LAYER_USER, NULL,
                 &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_RANGE);
    ASSERT_STREQ(err, "");
    st = set_one(store, userdb, "remember.search.max_results", "abc", SETTING_LAYER_USER, NULL, &wr,
                 err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_INVALID);
    ASSERT_STREQ(err, "");
    st = store_setting_read(store, NULL, "remember.sync.credential_helper", NULL, &got, err,
                            sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_EQ_INT(got.from, SETTING_FROM_DEFAULT);
    ASSERT_STREQ(got.value, "");
    ASSERT_FALSE(got.user_live);
    st = set_one(store, userdb, "remember.sync.credential_helper", "~/bin/helper",
                 SETTING_LAYER_USER, NULL, &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_INVALID);
    ASSERT_STREQ(err, "");
    st = set_one(store, userdb, "candidate_pool", "20", SETTING_LAYER_USER, NULL, &wr, err,
                 sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_UNKNOWN);
    ASSERT_STREQ(setting_status_code(st), "setting_unknown");
    ASSERT_STREQ(err, "");
    st = set_one(store, userdb, "remember.search.hybrid", "1", SETTING_LAYER_USER, NULL, &wr, err,
                 sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_UNKNOWN);
    ASSERT_STREQ(err, "");
    st = store_setting_read(store, userdb, "candidate_pool", NULL, &got, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_UNKNOWN);
    ASSERT_STREQ(err, "");
    ASSERT_TRUE(access(userdb, F_OK) != 0);

    st = set_one(store, "", "remember.search.max_results", "3", SETTING_LAYER_USER, NULL, &wr, err,
                 sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    ASSERT_STREQ(err, "empty database path");
    st = set_one(store, NULL, "remember.search.max_results", "8", SETTING_LAYER_BRAIN, NULL, &wr,
                 err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_TRUE(wr.changed);
    assert_query(brain, "SELECT change_seq FROM brain_meta;", "1");
    st = store_setting_read(store, NULL, "remember.search.max_results", NULL, &got, err,
                            sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_EQ_INT(got.from, SETTING_FROM_BRAIN);
    ASSERT_STREQ(got.value, "8");
    ASSERT_TRUE(access(userdb, F_OK) != 0);

    write_notes(userdb);
    st = set_one(store, userdb, "remember.search.max_results", "3", SETTING_LAYER_USER, NULL, &wr,
                 err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    ASSERT_STREQ(err, "cannot read user_version: file is not a database");
    assert_notes(userdb);
    err[0] = '\0';
    st = store_setting_read(store, userdb, "remember.search.max_results", NULL, &got, err,
                            sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    ASSERT_STREQ(err, "sqlite: file is not a database");
    assert_notes(userdb);

    st = store_setting_read(store, user2, "remember.plugins.chain_budget", "1500ms", &got, err,
                            sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_EQ_INT(got.from, SETTING_FROM_FLAG);
    ASSERT_STREQ(got.value, "\"1.5s\"");
    ASSERT_TRUE(access(user2, F_OK) != 0);
    st = store_setting_read(store, user2, "remember.search.max_results", "0", &got, err,
                            sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_RANGE);
    ASSERT_STREQ(err, "");
    assert_query(brain, "SELECT change_seq FROM brain_meta;", "1");

    st = set_one(store, user2, "remember.plugins.chain_budget", "1500ms", SETTING_LAYER_USER, NULL,
                 &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    assert_query(user2, "SELECT value FROM settings WHERE key = 'remember.plugins.chain_budget';",
                 "\"1.5s\"");
    st = set_one(store, user2, "remember.plugins.chain_budget", "1.5s", SETTING_LAYER_USER, NULL,
                 &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_FALSE(wr.changed);
    assert_query(user2,
                 "SELECT COUNT(*) FROM settings_history WHERE key = "
                 "'remember.plugins.chain_budget';",
                 "1");
    st = set_one(store, user2, "remember.sync.credential_helper", "env", SETTING_LAYER_AUTO, NULL,
                 &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    assert_query(user2, "SELECT value FROM settings WHERE key = 'remember.sync.credential_helper';",
                 "\"env\"");
    st = store_setting_read(store, user2, "remember.sync.credential_helper", NULL, &got, err,
                            sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_EQ_INT(got.from, SETTING_FROM_USER);
    ASSERT_STREQ(got.value, "\"env\"");
    st = store_setting_read(store, user2, "remember.plugins.idle_timeout", NULL, &got, err,
                            sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_EQ_INT(got.from, SETTING_FROM_DEFAULT);
    ASSERT_STREQ(got.value, "\"10m\"");
    st = store_setting_read(store, NULL, "remember.search.candidate_pool", NULL, &got, err,
                            sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_EQ_INT(got.from, SETTING_FROM_DEFAULT);
    ASSERT_STREQ(got.value, "200");
    assert_query(brain, "SELECT change_seq FROM brain_meta;", "1");

    exec_fixture(brain, "UPDATE brain_meta SET min_reader_version = '99.0.0';");
    store_close(store);
    store = store_open(brain, err, sizeof(err));
    ASSERT_TRUE(store != NULL);
    if (store == NULL) {
        harness_remove_tree(tmpl);
        return;
    }
    st = store_setting_read(store, user2, "remember.search.max_results", NULL, &got, err,
                            sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_EQ_INT(got.from, SETTING_FROM_BRAIN);
    ASSERT_STREQ(got.value, "8");
    st = set_one(store, user2, "remember.search.max_results", "9", SETTING_LAYER_BRAIN, NULL, &wr,
                 err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    ASSERT_STREQ(err, k_newer_brain);
    assert_query(brain, "SELECT value FROM settings WHERE key = 'remember.search.max_results';",
                 "8");
    assert_query(brain, "SELECT change_seq FROM brain_meta;", "1");
    st = set_one(store, user2, "remember.plugins.chain_budget", "2s", SETTING_LAYER_USER, NULL, &wr,
                 err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    assert_query(user2, "SELECT value FROM settings WHERE key = 'remember.plugins.chain_budget';",
                 "\"2s\"");
    close_pair(store, tmpl);
}

TEST(programmer_errors_and_a_failed_prepare)
{
    char tmpl[] = "/tmp/remember-set-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[256];
    char tiny[8];
    char one_byte[1];
    Store *store = NULL;
    SettingWrite w;
    SettingWriteResult wr;
    SettingRead got;
    SettingLayer layer = SETTING_LAYER_AUTO;
    SettingStatus st = SETTING_OK;
    const char *three = "3";
    const char *project = "project:";
    const char *blank = NULL;

    memset(&w, 0, sizeof(w));
    w.key = "remember.search.max_results";
    w.items = &three;
    w.nitems = 1;
    st = store_setting_set(NULL, NULL, &w, &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    ASSERT_STREQ(err, "internal error");
    st = store_setting_reset(NULL, NULL, "remember.search.max_results", SETTING_LAYER_USER, NULL,
                             &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    ASSERT_STREQ(err, "internal error");
    st =
        store_setting_read(NULL, NULL, "remember.search.max_results", NULL, &got, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    ASSERT_STREQ(err, "internal error");
    st = setting_choose_layer("remember.search.max_results", SETTING_LAYER_USER, NULL, NULL, err,
                              sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    ASSERT_STREQ(err, "internal error");
    st = setting_prepare("remember.search.max_results", &three, 1U, SETTING_LAYER_USER, tiny,
                         sizeof(tiny), NULL, &layer, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    ASSERT_STREQ(err, "internal error");
    one_byte[0] = 'x';
    st = setting_choose_layer("remember.plugins.idle_timeout", SETTING_LAYER_BRAIN, NULL, &layer,
                              one_byte, sizeof(one_byte));
    ASSERT_EQ_INT(st, SETTING_ERR_SCOPE);
    ASSERT_EQ_INT(one_byte[0], '\0');

    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    if (store == NULL) {
        return;
    }
    st = store_setting_set(store, userdb, &w, NULL, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    ASSERT_STREQ(err, "internal error");
    st = set_items(store, userdb, "remember.search.max_results", NULL, 1U, SETTING_LAYER_USER, NULL,
                   &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_INVALID);
    ASSERT_STREQ(err, "");
    st = set_items(store, NULL, "remember.buckets.prefixes", NULL, 1U, SETTING_LAYER_BRAIN, NULL,
                   &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_INVALID);
    ASSERT_STREQ(err, "");
    st = set_items(store, NULL, "remember.buckets.prefixes", &blank, 1U, SETTING_LAYER_BRAIN, NULL,
                   &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_INVALID);
    ASSERT_STREQ(err, "\"\" isn't a bucket prefix.");
    ASSERT_TRUE(access(userdb, F_OK) != 0);

    test_alloc_fail_after(0);
    st = set_items(store, NULL, "remember.buckets.prefixes", &project, 1U, SETTING_LAYER_BRAIN,
                   NULL, &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    ASSERT_STREQ(err, "out of memory");
    ASSERT_FALSE(wr.changed);
    test_alloc_fail_after(-1);

    store_test_fail_prepare_after(0);
    st = set_one(store, NULL, "remember.search.max_results", "4", SETTING_LAYER_BRAIN, NULL, &wr,
                 err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    store_test_fail_prepare_after(-1);
    st = set_one(store, NULL, "remember.search.max_results", "4", SETTING_LAYER_BRAIN, NULL, &wr,
                 err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_TRUE(wr.changed);
    /* Load's prepare succeeds. The new-key COUNT prepare is the one that fails. */
    store_test_fail_prepare_after(1);
    st = set_one(store, NULL, "remember.search.candidate_pool", "30", SETTING_LAYER_BRAIN, NULL,
                 &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    store_test_fail_prepare_after(-1);
    st = set_one(store, NULL, "remember.search.candidate_pool", "30", SETTING_LAYER_BRAIN, NULL,
                 &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_TRUE(wr.changed);
    st = store_setting_read(store, tmpl, "remember.search.max_results", NULL, &got, err,
                            sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    ASSERT_TRUE(err[0] != '\0');
    st = store_setting_reset(store, tmpl, "remember.search.max_results", SETTING_LAYER_USER, NULL,
                             &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    ASSERT_TRUE(err[0] != '\0');
    store_test_fail_prepare_after(0);
    st = store_setting_read(store, NULL, "remember.search.max_results", NULL, &got, err,
                            sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    store_test_fail_prepare_after(-1);
    st = store_setting_reset(store, NULL, "candidate_pool", SETTING_LAYER_USER, NULL, &wr, err,
                             sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_UNKNOWN);
    ASSERT_STREQ(err, "");

    st = set_one(store, userdb, "remember.search.max_results", "6", SETTING_LAYER_USER, NULL, &wr,
                 err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    /* ensure's two pragmas, then the write connection's two, then BEGIN. */
    store_test_fail_exec_after(4);
    st = set_one(store, userdb, "remember.search.max_results", "7", SETTING_LAYER_USER, NULL, &wr,
                 err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    store_test_fail_exec_after(-1);
    st = set_one(store, userdb, "remember.search.max_results", "7", SETTING_LAYER_USER, NULL, &wr,
                 err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_TRUE(wr.changed);
    /* Two pragmas on the existing file, then BEGIN. */
    store_test_fail_exec_after(2);
    st = store_setting_reset(store, userdb, "remember.search.max_results", SETTING_LAYER_USER, NULL,
                             &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    store_test_fail_exec_after(-1);
    write_notes(userdb);
    st = store_setting_reset(store, userdb, "remember.search.max_results", SETTING_LAYER_USER, NULL,
                             &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    ASSERT_STREQ(err, "sqlite: file is not a database");
    assert_notes(userdb);
    exec_fixture(brain, "UPDATE brain_meta SET min_reader_version = '99.0.0';");
    store_close(store);
    store = store_open(brain, err, sizeof(err));
    ASSERT_TRUE(store != NULL);
    if (store == NULL) {
        harness_remove_tree(tmpl);
        return;
    }
    st = store_setting_reset(store, NULL, "remember.search.max_results", SETTING_LAYER_BRAIN, NULL,
                             &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    ASSERT_STREQ(err, k_newer_brain);
    close_pair(store, tmpl);
}

/* Single-threaded tests. getenv, setenv, and unsetenv have no portable
   reentrant form. macOS fopen has no "e" mode. */
// NOLINTBEGIN(concurrency-mt-unsafe,android-cloexec-fopen)

/* Matches remember_token_fn. This stub does not write buf. */
// NOLINTNEXTLINE(readability-non-const-parameter)
static int fail_if_token_called(void *ctx, const char *helper_id, char *buf, size_t buflen)
{
    int *calls = (int *)ctx;

    (void)helper_id;
    (void)buf;
    (void)buflen;
    if (calls != NULL) {
        *calls += 1;
    }
    return -2;
}

/* ST-3.5. No credential-helper row: a non-empty REMEMBER_SYNC_TOKEN is the
   token. The host callback stays unused and nothing is diagnosed. */
TEST(no_row_reads_remember_sync_token)
{
    char tmpl[] = "/tmp/remember-cred-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[64];
    char token[64];
    char *saved = NULL;
    char *diag_buf = NULL;
    size_t diag_len = 0U;
    const char *live = NULL;
    FILE *diag = NULL;
    Store *store = NULL;
    RememberEnv host;
    SyncCredentialLookup lookup;
    int calls = 0;
    int rc = 99;

    live = getenv("REMEMBER_SYNC_TOKEN");
    if (live != NULL) {
        saved = strdup(live);
        ASSERT_TRUE(saved != NULL);
        if (saved == NULL) {
            return;
        }
    }
    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    if (store == NULL) {
        free(saved);
        return;
    }
    ASSERT_TRUE(access(userdb, F_OK) != 0);
    ASSERT_EQ_INT(setenv("REMEMBER_SYNC_TOKEN", "s3cret-token", 1), 0);
    memset(&host, 0, sizeof(host));
    host.token = fail_if_token_called;
    host.callback_ctx = &calls;
    memset(token, 'Z', sizeof(token) - 1U);
    token[sizeof(token) - 1U] = '\0';
    err[0] = '\0';
    diag = open_memstream(&diag_buf, &diag_len);
    ASSERT_TRUE(diag != NULL);
    if (diag == NULL) {
        if (saved == NULL) {
            (void)unsetenv("REMEMBER_SYNC_TOKEN");
        } else {
            (void)setenv("REMEMBER_SYNC_TOKEN", saved, 1);
        }
        free(saved);
        close_pair(store, tmpl);
        return;
    }
    app_set_streams((AppStreams){.out = NULL, .err = diag});
    memset(&lookup, 0, sizeof(lookup));
    lookup.store = store;
    lookup.user_db = userdb;
    lookup.host = &host;
    lookup.token = token;
    lookup.token_cap = sizeof(token);
    lookup.err = err;
    lookup.err_cap = sizeof(err);
    rc = sync_credential_lookup(&lookup);
    ASSERT_EQ_INT(fflush(diag), 0);
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(diag), 0);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_STREQ(token, "s3cret-token");
    ASSERT_TRUE(memchr(token + strlen(token) + 1U, 'Z', sizeof(token) - strlen(token) - 1U) ==
                NULL);
    ASSERT_EQ_INT(calls, 0);
    ASSERT_STREQ(diag_buf, "");
    ASSERT_STR_NOT_CONTAINS(diag_buf, "s3cret-token");
    if (saved == NULL) {
        (void)unsetenv("REMEMBER_SYNC_TOKEN");
    } else {
        (void)setenv("REMEMBER_SYNC_TOKEN", saved, 1);
    }
    free(saved);
    free(diag_buf);
    close_pair(store, tmpl);
}

/* ST-3.5. No row and no REMEMBER_SYNC_TOKEN: no token, and the lookup does
   not fail. */
TEST(no_row_without_remember_sync_token_returns_no_token)
{
    char tmpl[] = "/tmp/remember-cred-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[64];
    char token[64];
    char *saved = NULL;
    char *diag_buf = NULL;
    size_t diag_len = 0U;
    const char *live = NULL;
    FILE *diag = NULL;
    Store *store = NULL;
    RememberEnv host;
    SyncCredentialLookup lookup;
    int calls = 0;
    int rc = 99;

    live = getenv("REMEMBER_SYNC_TOKEN");
    if (live != NULL) {
        saved = strdup(live);
        ASSERT_TRUE(saved != NULL);
        if (saved == NULL) {
            return;
        }
    }
    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    if (store == NULL) {
        free(saved);
        return;
    }
    ASSERT_EQ_INT(unsetenv("REMEMBER_SYNC_TOKEN"), 0);
    memset(&host, 0, sizeof(host));
    host.token = fail_if_token_called;
    host.callback_ctx = &calls;
    memset(token, 'Z', sizeof(token) - 1U);
    token[sizeof(token) - 1U] = '\0';
    diag = open_memstream(&diag_buf, &diag_len);
    ASSERT_TRUE(diag != NULL);
    if (diag == NULL) {
        if (saved == NULL) {
            (void)unsetenv("REMEMBER_SYNC_TOKEN");
        } else {
            (void)setenv("REMEMBER_SYNC_TOKEN", saved, 1);
        }
        free(saved);
        close_pair(store, tmpl);
        return;
    }
    app_set_streams((AppStreams){.out = NULL, .err = diag});
    memset(&lookup, 0, sizeof(lookup));
    lookup.store = store;
    lookup.user_db = userdb;
    lookup.host = &host;
    lookup.token = token;
    lookup.token_cap = sizeof(token);
    lookup.err = err;
    lookup.err_cap = sizeof(err);
    rc = sync_credential_lookup(&lookup);
    ASSERT_EQ_INT(fflush(diag), 0);
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(diag), 0);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_STREQ(token, "");
    ASSERT_EQ_INT(calls, 0);
    ASSERT_STREQ(diag_buf, "");
    if (saved == NULL) {
        (void)unsetenv("REMEMBER_SYNC_TOKEN");
    } else {
        (void)setenv("REMEMBER_SYNC_TOKEN", saved, 1);
    }
    free(saved);
    free(diag_buf);
    close_pair(store, tmpl);
}

/* ST-3.5. An empty REMEMBER_SYNC_TOKEN is the same as a missing one. */
TEST(no_row_with_empty_remember_sync_token_returns_no_token)
{
    char tmpl[] = "/tmp/remember-cred-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[64];
    char token[64];
    char *saved = NULL;
    char *diag_buf = NULL;
    size_t diag_len = 0U;
    const char *live = NULL;
    FILE *diag = NULL;
    Store *store = NULL;
    SyncCredentialLookup lookup;
    int rc = 99;

    live = getenv("REMEMBER_SYNC_TOKEN");
    if (live != NULL) {
        saved = strdup(live);
        ASSERT_TRUE(saved != NULL);
        if (saved == NULL) {
            return;
        }
    }
    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    if (store == NULL) {
        free(saved);
        return;
    }
    ASSERT_EQ_INT(setenv("REMEMBER_SYNC_TOKEN", "", 1), 0);
    memset(token, 'Z', sizeof(token) - 1U);
    token[sizeof(token) - 1U] = '\0';
    diag = open_memstream(&diag_buf, &diag_len);
    ASSERT_TRUE(diag != NULL);
    if (diag == NULL) {
        if (saved == NULL) {
            (void)unsetenv("REMEMBER_SYNC_TOKEN");
        } else {
            (void)setenv("REMEMBER_SYNC_TOKEN", saved, 1);
        }
        free(saved);
        close_pair(store, tmpl);
        return;
    }
    app_set_streams((AppStreams){.out = NULL, .err = diag});
    memset(&lookup, 0, sizeof(lookup));
    lookup.store = store;
    lookup.user_db = userdb;
    lookup.host = NULL;
    lookup.token = token;
    lookup.token_cap = sizeof(token);
    lookup.err = err;
    lookup.err_cap = sizeof(err);
    rc = sync_credential_lookup(&lookup);
    ASSERT_EQ_INT(fflush(diag), 0);
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(diag), 0);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_STREQ(token, "");
    ASSERT_STREQ(diag_buf, "");
    if (saved == NULL) {
        (void)unsetenv("REMEMBER_SYNC_TOKEN");
    } else {
        (void)setenv("REMEMBER_SYNC_TOKEN", saved, 1);
    }
    free(saved);
    free(diag_buf);
    close_pair(store, tmpl);
}

/* ST-3.2. The id env reads REMEMBER_SYNC_TOKEN and does not call the host. */
TEST(env_helper_reads_remember_sync_token)
{
    char tmpl[] = "/tmp/remember-cred-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[64];
    char token[64];
    char *saved = NULL;
    char *diag_buf = NULL;
    size_t diag_len = 0U;
    const char *live = NULL;
    FILE *diag = NULL;
    Store *store = NULL;
    RememberEnv host;
    SyncCredentialLookup lookup;
    SettingWriteResult wr;
    SettingStatus st = SETTING_OK;
    int calls = 0;
    int rc = 99;

    live = getenv("REMEMBER_SYNC_TOKEN");
    if (live != NULL) {
        saved = strdup(live);
        ASSERT_TRUE(saved != NULL);
        if (saved == NULL) {
            return;
        }
    }
    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    if (store == NULL) {
        free(saved);
        return;
    }
    st = set_one(store, userdb, "remember.sync.credential_helper", "env", SETTING_LAYER_USER, NULL,
                 &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_EQ_INT(setenv("REMEMBER_SYNC_TOKEN", "s3cret-token", 1), 0);
    memset(&host, 0, sizeof(host));
    host.token = fail_if_token_called;
    host.callback_ctx = &calls;
    memset(token, 'Z', sizeof(token) - 1U);
    token[sizeof(token) - 1U] = '\0';
    diag = open_memstream(&diag_buf, &diag_len);
    ASSERT_TRUE(diag != NULL);
    if (diag == NULL) {
        if (saved == NULL) {
            (void)unsetenv("REMEMBER_SYNC_TOKEN");
        } else {
            (void)setenv("REMEMBER_SYNC_TOKEN", saved, 1);
        }
        free(saved);
        close_pair(store, tmpl);
        return;
    }
    app_set_streams((AppStreams){.out = NULL, .err = diag});
    memset(&lookup, 0, sizeof(lookup));
    lookup.store = store;
    lookup.user_db = userdb;
    lookup.host = &host;
    lookup.token = token;
    lookup.token_cap = sizeof(token);
    lookup.err = err;
    lookup.err_cap = sizeof(err);
    rc = sync_credential_lookup(&lookup);
    ASSERT_EQ_INT(fflush(diag), 0);
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(diag), 0);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_STREQ(token, "s3cret-token");
    ASSERT_EQ_INT(calls, 0);
    ASSERT_STREQ(diag_buf, "");
    if (saved == NULL) {
        (void)unsetenv("REMEMBER_SYNC_TOKEN");
    } else {
        (void)setenv("REMEMBER_SYNC_TOKEN", saved, 1);
    }
    free(saved);
    free(diag_buf);
    close_pair(store, tmpl);
}

static bool bytes_are_zero(const char *p, size_t n)
{
    size_t i = 0U;

    for (i = 0U; i < n; i++) {
        if (p[i] != '\0') {
            return false;
        }
    }
    return true;
}

typedef struct {
    Store *store;
    const char *user_db;
    RememberEnv *host;
    char *token;
    size_t token_cap;
    char *err;
    size_t err_cap;
} CredIn;

static int cred_lookup(CredIn in, char **diag_out)
{
    char *diag_buf = NULL;
    size_t diag_len = 0U;
    FILE *diag = open_memstream(&diag_buf, &diag_len);
    SyncCredentialLookup lookup;
    int rc = 99;

    *diag_out = NULL;
    if (diag == NULL) {
        return 99;
    }
    app_set_streams((AppStreams){.out = NULL, .err = diag});
    memset(&lookup, 0, sizeof(lookup));
    lookup.store = in.store;
    lookup.user_db = in.user_db;
    lookup.host = in.host;
    lookup.token = in.token;
    lookup.token_cap = in.token_cap;
    lookup.err = in.err;
    lookup.err_cap = in.err_cap;
    rc = sync_credential_lookup(&lookup);
    ASSERT_EQ_INT(fflush(diag), 0);
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(diag), 0);
    *diag_out = diag_buf;
    return rc;
}

/* ST-3.6. The key is env and REMEMBER_SYNC_TOKEN is missing or empty. */
TEST(env_helper_without_token_is_sync_credential_unavailable)
{
    char tmpl[] = "/tmp/remember-cred-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[64];
    char token[64];
    char *saved = NULL;
    char *diag_buf = NULL;
    const char *live = NULL;
    Store *store = NULL;
    RememberEnv host;
    SettingWriteResult wr;
    SettingStatus st = SETTING_OK;
    CredIn in;
    int calls = 0;
    int rc = 99;
    const char *plain =
        "remember: the sync credential helper \"env\" did not return a token. "
        "Check the helper, or set remember.sync.credential_helper to env and export "
        "REMEMBER_SYNC_TOKEN.\n";

    live = getenv("REMEMBER_SYNC_TOKEN");
    if (live != NULL) {
        saved = strdup(live);
        ASSERT_TRUE(saved != NULL);
        if (saved == NULL) {
            return;
        }
    }
    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    if (store == NULL) {
        free(saved);
        return;
    }
    st = set_one(store, userdb, "remember.sync.credential_helper", "env", SETTING_LAYER_USER, NULL,
                 &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_EQ_INT(unsetenv("REMEMBER_SYNC_TOKEN"), 0);
    memset(&host, 0, sizeof(host));
    host.token = fail_if_token_called;
    host.callback_ctx = &calls;
    memset(token, 0, sizeof(token));
    memcpy(token, "LEAKED-SECRET", sizeof("LEAKED-SECRET"));
    memset(&in, 0, sizeof(in));
    in.store = store;
    in.user_db = userdb;
    in.host = &host;
    in.token = token;
    in.token_cap = sizeof(token);
    in.err = err;
    in.err_cap = sizeof(err);
    rc = cred_lookup(in, &diag_buf);
    ASSERT_EQ_INT(rc, 1);
    ASSERT_EQ_INT(calls, 0);
    ASSERT_STREQ(diag_buf, plain);
    ASSERT_STR_NOT_CONTAINS(diag_buf, "LEAKED-SECRET");
    ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
    free(diag_buf);
    ASSERT_EQ_INT(setenv("REMEMBER_SYNC_TOKEN", "", 1), 0);
    memset(token, 0, sizeof(token));
    memcpy(token, "LEAKED-SECRET", sizeof("LEAKED-SECRET"));
    calls = 0;
    rc = cred_lookup(in, &diag_buf);
    ASSERT_EQ_INT(rc, 1);
    ASSERT_EQ_INT(calls, 0);
    ASSERT_STREQ(diag_buf, plain);
    ASSERT_STR_NOT_CONTAINS(diag_buf, "LEAKED-SECRET");
    ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
    free(diag_buf);
    if (saved == NULL) {
        (void)unsetenv("REMEMBER_SYNC_TOKEN");
    } else {
        (void)setenv("REMEMBER_SYNC_TOKEN", saved, 1);
    }
    free(saved);
    close_pair(store, tmpl);
}

typedef struct {
    const char *give;
    int calls;
    int other;
    int ret;
    char pad_[4];
    char seen[48];
} TokGive;

static int give_token(void *ctx, const char *helper_id, char *buf, size_t buflen)
{
    TokGive *g = (TokGive *)ctx;
    size_t n = 0U;

    g->calls += 1;
    g->seen[0] = '\0';
    if (helper_id != NULL) {
        (void)snprintf(g->seen, sizeof(g->seen), "%s", helper_id);
    }
    if (buf == NULL || buflen == 0U || g->give == NULL) {
        return g->ret;
    }
    n = strlen(g->give);
    if (n >= buflen) {
        return -2;
    }
    memcpy(buf, g->give, n + 1U);
    return g->ret;
}

/* Matches remember_confirm_fn. kind, name, and body are different roles. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static bool confirm_must_not_run(void *ctx, const char *kind, const char *name, const char *body)
{
    TokGive *g = (TokGive *)ctx;

    (void)kind;
    (void)name;
    (void)body;
    if (g != NULL) {
        g->other += 1;
    }
    return false;
}

static void dispatch_must_not_run(void *ctx, const char *brain_path)
{
    TokGive *g = (TokGive *)ctx;

    (void)brain_path;
    if (g != NULL) {
        g->other += 1;
    }
}

/* ST-3.2. Any id other than env goes to the host. The env var is not the token. */
TEST(pass_helper_reads_the_callback_not_the_env_var)
{
    char tmpl[] = "/tmp/remember-cred-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[64];
    char token[64];
    char *saved = NULL;
    char *diag_buf = NULL;
    const char *live = NULL;
    Store *store = NULL;
    RememberEnv host;
    SettingWriteResult wr;
    SettingStatus st = SETTING_OK;
    CredIn in;
    TokGive give;
    int rc = 99;

    live = getenv("REMEMBER_SYNC_TOKEN");
    if (live != NULL) {
        saved = strdup(live);
        ASSERT_TRUE(saved != NULL);
        if (saved == NULL) {
            return;
        }
    }
    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    if (store == NULL) {
        free(saved);
        return;
    }
    st = set_one(store, userdb, "remember.sync.credential_helper", "pass", SETTING_LAYER_USER, NULL,
                 &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_EQ_INT(setenv("REMEMBER_SYNC_TOKEN", "from-env", 1), 0);
    memset(&give, 0, sizeof(give));
    give.ret = 0;
    give.give = "from-helper";
    memset(&host, 0, sizeof(host));
    host.confirm = confirm_must_not_run;
    host.dispatch = dispatch_must_not_run;
    host.token = give_token;
    host.callback_ctx = &give;
    memset(token, 'Z', sizeof(token) - 1U);
    token[sizeof(token) - 1U] = '\0';
    memset(&in, 0, sizeof(in));
    in.store = store;
    in.user_db = userdb;
    in.host = &host;
    in.token = token;
    in.token_cap = sizeof(token);
    in.err = err;
    in.err_cap = sizeof(err);
    rc = cred_lookup(in, &diag_buf);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_STREQ(token, "from-helper");
    ASSERT_EQ_INT(give.calls, 1);
    ASSERT_STREQ(give.seen, "pass");
    ASSERT_EQ_INT(give.other, 0);
    ASSERT_STREQ(diag_buf, "");
    ASSERT_STR_NOT_CONTAINS(diag_buf, "from-helper");
    ASSERT_STR_NOT_CONTAINS(diag_buf, "from-env");
    free(diag_buf);
    if (saved == NULL) {
        (void)unsetenv("REMEMBER_SYNC_TOKEN");
    } else {
        (void)setenv("REMEMBER_SYNC_TOKEN", saved, 1);
    }
    free(saved);
    close_pair(store, tmpl);
}

/* ST-3.2 / ST-3.6. A NULL token callback leaves only env. pass has no token. */
TEST(pass_without_a_callback_is_sync_credential_unavailable)
{
    char tmpl[] = "/tmp/remember-cred-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[64];
    char token[64];
    char *diag_buf = NULL;
    Store *store = NULL;
    SettingWriteResult wr;
    SettingStatus st = SETTING_OK;
    CredIn in;
    int rc = 99;
    const char *plain =
        "remember: the sync credential helper \"pass\" did not return a token. "
        "Check the helper, or set remember.sync.credential_helper to env and export "
        "REMEMBER_SYNC_TOKEN.\n";

    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    if (store == NULL) {
        return;
    }
    st = set_one(store, userdb, "remember.sync.credential_helper", "pass", SETTING_LAYER_USER, NULL,
                 &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    memset(token, 0, sizeof(token));
    memcpy(token, "LEAKED-SECRET", sizeof("LEAKED-SECRET"));
    memset(&in, 0, sizeof(in));
    in.store = store;
    in.user_db = userdb;
    in.host = NULL;
    in.token = token;
    in.token_cap = sizeof(token);
    in.err = err;
    in.err_cap = sizeof(err);
    rc = cred_lookup(in, &diag_buf);
    ASSERT_EQ_INT(rc, 1);
    ASSERT_STREQ(diag_buf, plain);
    ASSERT_STR_NOT_CONTAINS(diag_buf, "LEAKED-SECRET");
    ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
    free(diag_buf);
    close_pair(store, tmpl);
}

static int fill_no_nul(void *ctx, const char *helper_id, char *buf, size_t buflen)
{
    int *calls = (int *)ctx;
    size_t i = 0U;

    (void)helper_id;
    if (calls != NULL) {
        *calls += 1;
    }
    for (i = 0U; i < buflen; i++) {
        buf[i] = 'A';
    }
    return 0;
}

/* A token that does not fit, or has no NUL, is not returned truncated. */
TEST(callback_without_a_nul_is_sync_credential_unavailable)
{
    char tmpl[] = "/tmp/remember-cred-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[64];
    char token[8];
    char *diag_buf = NULL;
    Store *store = NULL;
    RememberEnv host;
    SettingWriteResult wr;
    SettingStatus st = SETTING_OK;
    CredIn in;
    int calls = 0;
    int rc = 99;

    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    if (store == NULL) {
        return;
    }
    st = set_one(store, userdb, "remember.sync.credential_helper", "pass", SETTING_LAYER_USER, NULL,
                 &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    memset(&host, 0, sizeof(host));
    host.token = fill_no_nul;
    host.callback_ctx = &calls;
    memset(&in, 0, sizeof(in));
    in.store = store;
    in.user_db = userdb;
    in.host = &host;
    in.token = token;
    in.token_cap = sizeof(token);
    in.err = err;
    in.err_cap = sizeof(err);
    rc = cred_lookup(in, &diag_buf);
    ASSERT_EQ_INT(rc, 1);
    ASSERT_EQ_INT(calls, 1);
    ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
    ASSERT_STR_NOT_CONTAINS(diag_buf, "AAAAAAAA");
    ASSERT_STR_CONTAINS(diag_buf, "the sync credential helper \"pass\" did not return a token.");
    free(diag_buf);
    close_pair(store, tmpl);
}

/* -2 and an empty buffer are both "no token". The secret does not leak. */
TEST(callback_failure_wipes_the_token)
{
    char tmpl[] = "/tmp/remember-cred-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[64];
    char token[64];
    char *diag_buf = NULL;
    Store *store = NULL;
    RememberEnv host;
    SettingWriteResult wr;
    SettingStatus st = SETTING_OK;
    CredIn in;
    TokGive give;
    int rc = 99;

    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    if (store == NULL) {
        return;
    }
    st = set_one(store, userdb, "remember.sync.credential_helper", "pass", SETTING_LAYER_USER, NULL,
                 &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    memset(&give, 0, sizeof(give));
    give.ret = -2;
    give.give = "from-helper";
    memset(&host, 0, sizeof(host));
    host.token = give_token;
    host.callback_ctx = &give;
    memset(&in, 0, sizeof(in));
    in.store = store;
    in.user_db = userdb;
    in.host = &host;
    in.token = token;
    in.token_cap = sizeof(token);
    in.err = err;
    in.err_cap = sizeof(err);
    rc = cred_lookup(in, &diag_buf);
    ASSERT_EQ_INT(rc, 1);
    ASSERT_EQ_INT(give.calls, 1);
    ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
    ASSERT_STR_NOT_CONTAINS(diag_buf, "from-helper");
    ASSERT_STR_CONTAINS(diag_buf, "helper \"pass\"");
    free(diag_buf);
    give.calls = 0;
    give.ret = 0;
    give.give = NULL;
    rc = cred_lookup(in, &diag_buf);
    ASSERT_EQ_INT(rc, 1);
    ASSERT_EQ_INT(give.calls, 1);
    ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
    free(diag_buf);
    close_pair(store, tmpl);
}

/* ST-3.5 does not trim REMEMBER_SYNC_TOKEN. Trim is only for helper stdout. */
TEST(no_row_keeps_whitespace_in_remember_sync_token)
{
    char tmpl[] = "/tmp/remember-cred-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[64];
    char token[64];
    char *saved = NULL;
    char *diag_buf = NULL;
    const char *live = NULL;
    Store *store = NULL;
    CredIn in;
    int rc = 99;

    live = getenv("REMEMBER_SYNC_TOKEN");
    if (live != NULL) {
        saved = strdup(live);
        ASSERT_TRUE(saved != NULL);
        if (saved == NULL) {
            return;
        }
    }
    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    if (store == NULL) {
        free(saved);
        return;
    }
    ASSERT_EQ_INT(setenv("REMEMBER_SYNC_TOKEN", "  tok\t", 1), 0);
    memset(&in, 0, sizeof(in));
    in.store = store;
    in.user_db = userdb;
    in.token = token;
    in.token_cap = sizeof(token);
    in.err = err;
    in.err_cap = sizeof(err);
    rc = cred_lookup(in, &diag_buf);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_STREQ(token, "  tok\t");
    ASSERT_STREQ(diag_buf, "");
    free(diag_buf);
    if (saved == NULL) {
        (void)unsetenv("REMEMBER_SYNC_TOKEN");
    } else {
        (void)setenv("REMEMBER_SYNC_TOKEN", saved, 1);
    }
    free(saved);
    close_pair(store, tmpl);
}

TEST(unavailable_json_names_the_fix_and_hides_the_token)
{
    char tmpl[] = "/tmp/remember-cred-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[64];
    char token[64];
    char *diag_buf = NULL;
    Store *store = NULL;
    SettingWriteResult wr;
    SettingStatus st = SETTING_OK;
    CredIn in;
    int rc = 99;
    const char *json =
        "{\"version\":1,\"error\":{\"code\":\"sync_credential_unavailable\",\"message\":\"the "
        "sync credential helper \\\"pass\\\" did not return a "
        "token.\",\"fix\":\"Check the helper, or set remember.sync.credential_helper to env and "
        "export REMEMBER_SYNC_TOKEN.\"}}\n";

    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    if (store == NULL) {
        return;
    }
    st = set_one(store, userdb, "remember.sync.credential_helper", "pass", SETTING_LAYER_USER, NULL,
                 &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    memset(token, 0, sizeof(token));
    memcpy(token, "LEAKED-SECRET", sizeof("LEAKED-SECRET"));
    diag_set_json_errors(true);
    memset(&in, 0, sizeof(in));
    in.store = store;
    in.user_db = userdb;
    in.token = token;
    in.token_cap = sizeof(token);
    in.err = err;
    in.err_cap = sizeof(err);
    rc = cred_lookup(in, &diag_buf);
    diag_set_json_errors(false);
    ASSERT_EQ_INT(rc, 1);
    ASSERT_STREQ(diag_buf, json);
    ASSERT_STR_NOT_CONTAINS(diag_buf, "retryable");
    ASSERT_STR_NOT_CONTAINS(diag_buf, "LEAKED-SECRET");
    ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
    free(diag_buf);
    close_pair(store, tmpl);
}

TEST(store_failure_keeps_the_store_sentence)
{
    char tmpl[] = "/tmp/remember-cred-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[128];
    char token[32];
    char *diag_buf = NULL;
    Store *store = NULL;
    CredIn in;
    int rc = 99;

    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    if (store == NULL) {
        return;
    }
    write_notes(userdb);
    memset(token, 0, sizeof(token));
    memcpy(token, "LEAKED-SECRET", sizeof("LEAKED-SECRET"));
    memset(&in, 0, sizeof(in));
    in.store = store;
    in.user_db = userdb;
    in.token = token;
    in.token_cap = sizeof(token);
    in.err = err;
    in.err_cap = sizeof(err);
    rc = cred_lookup(in, &diag_buf);
    ASSERT_EQ_INT(rc, -1);
    ASSERT_STREQ(err, "sqlite: file is not a database");
    ASSERT_STREQ(diag_buf, "");
    ASSERT_STR_NOT_CONTAINS(diag_buf, "LEAKED-SECRET");
    ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
    assert_notes(userdb);
    free(diag_buf);
    close_pair(store, tmpl);
}

/* A stored id outside the grammar is still "key set", and the callback is not run. */
TEST(non_grammar_helper_does_not_call_the_callback)
{
    char tmpl[] = "/tmp/remember-cred-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[64];
    char token[64];
    const char raw[] = {'a', '\x01', 'b', '\0'};
    char *diag_buf = NULL;
    Store *store = NULL;
    RememberEnv host;
    SettingWriteResult wr;
    SettingStatus st = SETTING_OK;
    CredIn in;
    TokGive give;
    int rc = 99;

    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    if (store == NULL) {
        return;
    }
    st = set_one(store, userdb, "remember.sync.credential_helper", "pass", SETTING_LAYER_USER, NULL,
                 &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    exec_fixture(userdb, "UPDATE settings SET value = '\"~/bin/helper\"' WHERE key = "
                         "'remember.sync.credential_helper';");
    memset(&give, 0, sizeof(give));
    give.ret = 0;
    give.give = "from-helper";
    memset(&host, 0, sizeof(host));
    host.token = give_token;
    host.callback_ctx = &give;
    memset(&in, 0, sizeof(in));
    in.store = store;
    in.user_db = userdb;
    in.host = &host;
    in.token = token;
    in.token_cap = sizeof(token);
    in.err = err;
    in.err_cap = sizeof(err);
    rc = cred_lookup(in, &diag_buf);
    ASSERT_EQ_INT(rc, 1);
    ASSERT_EQ_INT(give.calls, 0);
    ASSERT_STR_CONTAINS(diag_buf, "helper \"~/bin/helper\"");
    ASSERT_STR_NOT_CONTAINS(diag_buf, "from-helper");
    free(diag_buf);
    exec_fixture(userdb, "UPDATE settings SET value = '\"a' || char(1) || 'b\"' WHERE key = "
                         "'remember.sync.credential_helper';");
    rc = cred_lookup(in, &diag_buf);
    ASSERT_EQ_INT(rc, 1);
    ASSERT_EQ_INT(give.calls, 0);
    ASSERT_STR_CONTAINS(diag_buf, "helper \"a?b\"");
    ASSERT_STR_NOT_CONTAINS(diag_buf, raw);
    free(diag_buf);
    exec_fixture(userdb, "UPDATE settings SET value = 'not-quoted' WHERE key = "
                         "'remember.sync.credential_helper';");
    rc = cred_lookup(in, &diag_buf);
    ASSERT_EQ_INT(rc, 1);
    ASSERT_EQ_INT(give.calls, 0);
    ASSERT_STR_CONTAINS(diag_buf, "helper \"not-quoted\"");
    free(diag_buf);
    close_pair(store, tmpl);
}

/* A secret that does not fit is not truncated. No row stays success with no
   token. A set key is unavailable, and the secret is not in the notice. */
TEST(token_that_does_not_fit_is_not_truncated)
{
    char tmpl[] = "/tmp/remember-cred-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[128];
    char token[4];
    char exact[6];
    char *saved = NULL;
    char *diag_buf = NULL;
    const char *live = NULL;
    Store *store = NULL;
    SettingWriteResult wr;
    SettingStatus st = SETTING_OK;
    CredIn in;
    int rc = 99;

    live = getenv("REMEMBER_SYNC_TOKEN");
    if (live != NULL) {
        saved = strdup(live);
        ASSERT_TRUE(saved != NULL);
        if (saved == NULL) {
            return;
        }
    }
    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    if (store == NULL) {
        free(saved);
        return;
    }
    ASSERT_EQ_INT(setenv("REMEMBER_SYNC_TOKEN", "hello", 1), 0);
    memset(token, 'Z', sizeof(token));
    memset(&in, 0, sizeof(in));
    in.store = store;
    in.user_db = userdb;
    in.token = token;
    in.token_cap = sizeof(token);
    in.err = err;
    in.err_cap = sizeof(err);
    rc = cred_lookup(in, &diag_buf);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
    ASSERT_STREQ(diag_buf, "");
    free(diag_buf);
    in.token = exact;
    in.token_cap = sizeof(exact);
    rc = cred_lookup(in, &diag_buf);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_STREQ(exact, "hello");
    free(diag_buf);
    st = set_one(store, userdb, "remember.sync.credential_helper", "env", SETTING_LAYER_USER, NULL,
                 &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    memset(token, 'Z', sizeof(token));
    in.token = token;
    in.token_cap = sizeof(token);
    rc = cred_lookup(in, &diag_buf);
    ASSERT_EQ_INT(rc, 1);
    ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
    ASSERT_STR_CONTAINS(diag_buf, "helper \"env\"");
    ASSERT_STR_NOT_CONTAINS(diag_buf, "hello");
    free(diag_buf);
    if (saved == NULL) {
        (void)unsetenv("REMEMBER_SYNC_TOKEN");
    } else {
        (void)setenv("REMEMBER_SYNC_TOKEN", saved, 1);
    }
    free(saved);
    close_pair(store, tmpl);
}

/* The built helper sits next to the remember binary under test. */
static int credential_helper_src(char *dst, size_t cap)
{
    const char *bin = g_remember_bin;
    const char *slash = NULL;
    char raw[REMEMBER_PATH_MAX];
    char *resolved = NULL;
    size_t n = 0U;
    static const char suffix[] = "/remember-credential-pass";

    if (bin == NULL || cap == 0U) {
        return -1;
    }
    slash = strrchr(bin, '/');
    if (slash == NULL) {
        return -1;
    }
    n = (size_t)(slash - bin);
    if (n + sizeof(suffix) > sizeof(raw)) {
        return -1;
    }
    memcpy(raw, bin, n);
    memcpy(raw + n, suffix, sizeof(suffix));
    /* A relative symlink is resolved from the temp dir, not the cwd. */
    resolved = realpath(raw, NULL);
    if (resolved == NULL || strlen(resolved) + 1U > cap) {
        free(resolved);
        return -1;
    }
    memcpy(dst, resolved, strlen(resolved) + 1U);
    free(resolved);
    return 0;
}

/* ST-3.3. The CLI callback runs remember-credential-<id> get, no shell. */
TEST(cli_runner_reads_the_trimmed_first_line)
{
    char dir_tmpl[] = "/tmp/remember-helper-XXXXXX";
    char helper_src[REMEMBER_PATH_MAX];
    char helper[REMEMBER_PATH_MAX];
    char err_path[REMEMBER_PATH_MAX];
    char out_path[REMEMBER_PATH_MAX];
    char token[64];
    char err_got[256];
    char out_got[64];
    char *saved_path = NULL;
    const char *live = NULL;
    const char *dir = NULL;
    FILE *errf = NULL;
    FILE *outf = NULL;
    int rc = 99;
    ssize_t n = 0;

    live = getenv("PATH");
    if (live != NULL) {
        saved_path = strdup(live);
        ASSERT_TRUE(saved_path != NULL);
        if (saved_path == NULL) {
            return;
        }
    }
    dir = mkdtemp(dir_tmpl);
    ASSERT_TRUE(dir != NULL);
    if (dir == NULL) {
        free(saved_path);
        return;
    }
    ASSERT_EQ_INT(credential_helper_src(helper_src, sizeof(helper_src)), 0);
    ASSERT_TRUE(snprintf(helper, sizeof(helper), "%s/remember-credential-pass", dir) > 0);
    ASSERT_TRUE(snprintf(err_path, sizeof(err_path), "%s/err", dir) > 0);
    ASSERT_TRUE(snprintf(out_path, sizeof(out_path), "%s/out", dir) > 0);
    ASSERT_EQ_INT(symlink(helper_src, helper), 0);
    ASSERT_EQ_INT(setenv("PATH", dir, 1), 0);
    errf = fopen(err_path, "w+");
    outf = fopen(out_path, "w+");
    ASSERT_TRUE(errf != NULL && outf != NULL);
    if (errf == NULL || outf == NULL) {
        if (errf != NULL) {
            (void)fclose(errf);
        }
        if (outf != NULL) {
            (void)fclose(outf);
        }
        if (saved_path == NULL) {
            (void)unsetenv("PATH");
        } else {
            (void)setenv("PATH", saved_path, 1);
        }
        free(saved_path);
        harness_remove_tree(dir_tmpl);
        return;
    }
    app_set_streams((AppStreams){.out = outf, .err = errf});
    memset(token, 'Z', sizeof(token) - 1U);
    token[sizeof(token) - 1U] = '\0';
    rc = cli_credential_token(NULL, "pass", token, sizeof(token));
    ASSERT_EQ_INT(fflush(errf), 0);
    ASSERT_EQ_INT(fflush(outf), 0);
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fseek(errf, 0, SEEK_SET), 0);
    ASSERT_EQ_INT(fseek(outf, 0, SEEK_SET), 0);
    n = (ssize_t)fread(err_got, 1, sizeof(err_got) - 1U, errf);
    ASSERT_TRUE(n >= 0);
    err_got[n > 0 ? (size_t)n : 0U] = '\0';
    n = (ssize_t)fread(out_got, 1, sizeof(out_got) - 1U, outf);
    ASSERT_TRUE(n >= 0);
    out_got[n > 0 ? (size_t)n : 0U] = '\0';
    ASSERT_EQ_INT(fclose(errf), 0);
    ASSERT_EQ_INT(fclose(outf), 0);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_STREQ(token, "secret");
    ASSERT_STR_CONTAINS(err_got, "enter passphrase");
    ASSERT_STR_CONTAINS(err_got, "argv0=remember-credential-pass");
    ASSERT_STR_CONTAINS(err_got, "argv1=get");
    ASSERT_STR_CONTAINS(err_got, "stdin=0");
    ASSERT_STR_NOT_CONTAINS(err_got, "secret");
    ASSERT_STR_NOT_CONTAINS(out_got, "secret");
    ASSERT_STR_NOT_CONTAINS(token, "ignored");
    if (saved_path == NULL) {
        (void)unsetenv("PATH");
    } else {
        (void)setenv("PATH", saved_path, 1);
    }
    free(saved_path);
    harness_remove_tree(dir_tmpl);
}

typedef struct {
    char dir_tmpl[64];
    char *saved_path;
    FILE *errf;
    FILE *outf;
    char err_got[512];
    char out_got[256];
    int armed;
    char pad_[4];
} RunFx;

static int write_mode(const char *path, mode_t mode, const char *body)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    ssize_t n = 0;

    if (fd < 0) {
        return -1;
    }
    n = write(fd, body, strlen(body));
    if (fchmod(fd, mode) != 0) {
        (void)close(fd);
        return -1;
    }
    if (close(fd) != 0 || n < 0 || (size_t)n != strlen(body)) {
        return -1;
    }
    return 0;
}

static int helper_path(const RunFx *run, char *dst, size_t cap)
{
    int wrote = snprintf(dst, cap, "%s/remember-credential-pass", run->dir_tmpl);

    return (wrote > 0 && (size_t)wrote < cap) ? 0 : -1;
}

/* PATH is only the temp dir. errf is a real file so fileno works. */
static int run_begin(RunFx *run)
{
    const char *live = getenv("PATH");
    const char *dir = NULL;
    char err_path[REMEMBER_PATH_MAX];
    char out_path[REMEMBER_PATH_MAX];

    memset(run, 0, sizeof(*run));
    memcpy(run->dir_tmpl, "/tmp/remember-helper-XXXXXX", sizeof("/tmp/remember-helper-XXXXXX"));
    if (live != NULL) {
        run->saved_path = strdup(live);
        if (run->saved_path == NULL) {
            return -1;
        }
    }
    dir = mkdtemp(run->dir_tmpl);
    if (dir == NULL) {
        free(run->saved_path);
        run->saved_path = NULL;
        return -1;
    }
    run->armed = 1;
    if (snprintf(err_path, sizeof(err_path), "%s/err", dir) <= 0 ||
        snprintf(out_path, sizeof(out_path), "%s/out", dir) <= 0 || setenv("PATH", dir, 1) != 0) {
        return -1;
    }
    run->errf = fopen(err_path, "w+");
    run->outf = fopen(out_path, "w+");
    if (run->errf == NULL || run->outf == NULL) {
        return -1;
    }
    app_set_streams((AppStreams){.out = run->outf, .err = run->errf});
    return 0;
}

static void run_capture(RunFx *run)
{
    if (run->errf != NULL) {
        (void)fflush(run->errf);
    }
    if (run->outf != NULL) {
        (void)fflush(run->outf);
    }
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    if (run->errf != NULL) {
        (void)fseek(run->errf, 0, SEEK_SET);
        {
            ssize_t n = (ssize_t)fread(run->err_got, 1, sizeof(run->err_got) - 1U, run->errf);

            run->err_got[n > 0 ? (size_t)n : 0U] = '\0';
            (void)fclose(run->errf);
            run->errf = NULL;
        }
    }
    if (run->outf != NULL) {
        (void)fseek(run->outf, 0, SEEK_SET);
        {
            ssize_t n = (ssize_t)fread(run->out_got, 1, sizeof(run->out_got) - 1U, run->outf);

            run->out_got[n > 0 ? (size_t)n : 0U] = '\0';
            (void)fclose(run->outf);
            run->outf = NULL;
        }
    }
}

static void run_finish(RunFx *run)
{
    run_capture(run);
    if (run->saved_path == NULL) {
        (void)unsetenv("PATH");
    } else {
        (void)setenv("PATH", run->saved_path, 1);
        free(run->saved_path);
        run->saved_path = NULL;
    }
    if (run->armed) {
        harness_remove_tree(run->dir_tmpl);
        run->armed = 0;
    }
    cli_credential_test_set_timeout_ms(0);
}

static void preload(char *token, size_t cap)
{
    memset(token, 'Z', cap - 1U);
    token[cap - 1U] = '\0';
}

static int install_script(const RunFx *run, const char *body, mode_t mode)
{
    char path[REMEMBER_PATH_MAX];

    if (helper_path(run, path, sizeof(path)) != 0) {
        return -1;
    }
    return write_mode(path, mode, body);
}

static long long test_mono_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return ((long long)ts.tv_sec * 1000LL) + ((long long)ts.tv_nsec / 1000000LL);
}

static pid_t read_pid_file(const char *path)
{
    char buf[32];
    char *end = NULL;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    ssize_t n = 0;
    long value = 0;

    if (fd < 0) {
        return 0;
    }
    n = read(fd, buf, sizeof(buf) - 1U);
    (void)close(fd);
    if (n <= 0) {
        return 0;
    }
    buf[n] = '\0';
    value = strtol(buf, &end, 10);
    if (end == buf || value <= 0) {
        return 0;
    }
    return (pid_t)value;
}

/* Poll. A zombie still answers kill(pid, 0). Then SIGKILL so a failed test does not leak. */
static int wait_until_gone(pid_t pid)
{
    int i = 0;

    for (i = 0; i < 50; i++) {
        struct timespec pause = {.tv_sec = 0, .tv_nsec = 20000000L};

        if (kill(pid, 0) < 0 && errno == ESRCH) {
            return 1;
        }
        (void)nanosleep(&pause, NULL);
    }
    (void)kill(pid, SIGKILL);
    return 0;
}

TEST(cli_credential_timeout_ms_is_thirty_seconds)
{
    cli_credential_test_set_timeout_ms(0);
    ASSERT_EQ_INT(cli_credential_timeout_ms(), 30000);
    cli_credential_test_set_timeout_ms(250);
    ASSERT_EQ_INT(cli_credential_timeout_ms(), 250);
    cli_credential_test_set_timeout_ms(0);
    ASSERT_EQ_INT(cli_credential_timeout_ms(), 30000);
}

/* ST-3.3. Not on PATH is -1. A non-executable file is -2. */
TEST(helper_not_on_path_is_unknown)
{
    RunFx run;
    char token[32];
    int rc = 99;

    ASSERT_EQ_INT(run_begin(&run), 0);
    preload(token, sizeof(token));
    rc = cli_credential_token(NULL, "pass", token, sizeof(token));
    ASSERT_EQ_INT(rc, -1);
    ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
    run_finish(&run);
    ASSERT_STREQ(run.err_got, "");
    ASSERT_STR_NOT_CONTAINS(run.out_got, "Z");
}

TEST(helper_not_executable_is_lookup_failed)
{
    RunFx run;
    char token[32];
    int rc = 99;

    ASSERT_EQ_INT(run_begin(&run), 0);
    ASSERT_EQ_INT(install_script(&run, "#!/bin/sh\nprintf 'secret\\n'\n", 0644), 0);
    preload(token, sizeof(token));
    rc = cli_credential_token(NULL, "pass", token, sizeof(token));
    ASSERT_EQ_INT(rc, -2);
    ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
    run_finish(&run);
    ASSERT_STR_NOT_CONTAINS(run.err_got, "secret");
    ASSERT_STR_NOT_CONTAINS(run.out_got, "secret");
}

/* A non-zero exit drops the line it printed. */
TEST(helper_nonzero_exit_wipes_the_token)
{
    RunFx run;
    char token[32];
    int rc = 99;
    const char *body = "#!/bin/sh\nprintf '  secret  \\n'\necho noise >&2\nexit 1\n";

    ASSERT_EQ_INT(run_begin(&run), 0);
    ASSERT_EQ_INT(install_script(&run, body, 0755), 0);
    preload(token, sizeof(token));
    rc = cli_credential_token(NULL, "pass", token, sizeof(token));
    ASSERT_EQ_INT(rc, -2);
    ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
    run_finish(&run);
    ASSERT_STR_CONTAINS(run.err_got, "noise");
    ASSERT_STR_NOT_CONTAINS(run.err_got, "secret");
    ASSERT_STR_NOT_CONTAINS(run.out_got, "secret");
}

/* An empty or whitespace-only first line is -2. A later line is not the token. */
TEST(empty_stdout_is_lookup_failed)
{
    RunFx run;
    char token[32];
    int rc = 99;

    ASSERT_EQ_INT(run_begin(&run), 0);
    ASSERT_EQ_INT(install_script(&run, "#!/bin/sh\nexit 0\n", 0755), 0);
    preload(token, sizeof(token));
    rc = cli_credential_token(NULL, "pass", token, sizeof(token));
    ASSERT_EQ_INT(rc, -2);
    ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
    run_finish(&run);

    ASSERT_EQ_INT(run_begin(&run), 0);
    ASSERT_EQ_INT(install_script(&run, "#!/bin/sh\nprintf '\\nLEAKED-SECRET\\n'\nexit 0\n", 0755),
                  0);
    preload(token, sizeof(token));
    rc = cli_credential_token(NULL, "pass", token, sizeof(token));
    ASSERT_EQ_INT(rc, -2);
    ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
    run_finish(&run);
    ASSERT_STR_NOT_CONTAINS(run.err_got, "LEAKED-SECRET");
    ASSERT_STR_NOT_CONTAINS(run.out_got, "LEAKED-SECRET");

    ASSERT_EQ_INT(run_begin(&run), 0);
    ASSERT_EQ_INT(install_script(&run,
                                 "#!/bin/sh\nprintf ' \\t\\r\\v\\f \\nLEAKED-SECRET\\n'\nexit 0\n",
                                 0755),
                  0);
    preload(token, sizeof(token));
    rc = cli_credential_token(NULL, "pass", token, sizeof(token));
    ASSERT_EQ_INT(rc, -2);
    ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
    run_finish(&run);
    ASSERT_STR_NOT_CONTAINS(run.err_got, "LEAKED-SECRET");
}

/* Leading and trailing ASCII whitespace go. An internal space stays. No newline is still a line. */
TEST(first_line_keeps_an_internal_space)
{
    RunFx run;
    char token[32];
    int rc = 99;

    ASSERT_EQ_INT(run_begin(&run), 0);
    ASSERT_EQ_INT(
        install_script(&run, "#!/bin/sh\nprintf '\\v\\f  hello  world  \\t\\r'\nexit 0\n", 0755),
        0);
    preload(token, sizeof(token));
    rc = cli_credential_token(NULL, "pass", token, sizeof(token));
    ASSERT_EQ_INT(rc, 0);
    ASSERT_STREQ(token, "hello  world");
    run_finish(&run);
    ASSERT_STR_NOT_CONTAINS(run.err_got, "hello");
    ASSERT_STR_NOT_CONTAINS(run.out_got, "hello");
}

/* A line that does not fit, including the NUL, is -2. No truncated secret remains. */
TEST(token_line_longer_than_the_buffer_is_lookup_failed)
{
    RunFx run;
    char token[8];
    int rc = 99;
    const char *body = "#!/bin/sh\nprintf '0123456789abcdef\\n'\nexit 0\n";

    ASSERT_EQ_INT(run_begin(&run), 0);
    ASSERT_EQ_INT(install_script(&run, body, 0755), 0);
    memset(token, 'Z', sizeof(token));
    rc = cli_credential_token(NULL, "pass", token, sizeof(token));
    ASSERT_EQ_INT(rc, -2);
    ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
    run_finish(&run);
    ASSERT_STR_NOT_CONTAINS(run.err_got, "0123456789");
    ASSERT_STR_NOT_CONTAINS(run.out_got, "0123456789");
}

/* The hook shortens the 30s deadline. SIGKILL hits the process group, then the child is reaped. */
TEST(hanging_helper_group_is_killed)
{
    RunFx run;
    char token[32];
    char body[512];
    char parent_path[REMEMBER_PATH_MAX];
    char child_path[REMEMBER_PATH_MAX];
    long long started = 0;
    long long elapsed = 0;
    pid_t parent = 0;
    pid_t child = 0;
    int rc = 99;
    int wrote = 0;

    ASSERT_EQ_INT(run_begin(&run), 0);
    wrote = snprintf(body, sizeof(body),
                     "#!/bin/sh\n"
                     "echo $$ > %s/parent.pid\n"
                     "( echo $$ > %s/child.pid; while true; do :; done ) &\n"
                     "i=0\n"
                     "while [ ! -s %s/child.pid ]; do\n"
                     "i=$((i + 1))\n"
                     "if [ \"$i\" -gt 5000 ]; then exit 0; fi\n"
                     "done\n"
                     "while true; do :; done\n",
                     run.dir_tmpl, run.dir_tmpl, run.dir_tmpl);
    ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(body));
    ASSERT_EQ_INT(install_script(&run, body, 0755), 0);
    ASSERT_TRUE(snprintf(parent_path, sizeof(parent_path), "%s/parent.pid", run.dir_tmpl) > 0);
    ASSERT_TRUE(snprintf(child_path, sizeof(child_path), "%s/child.pid", run.dir_tmpl) > 0);
    cli_credential_test_set_timeout_ms(3000);
    preload(token, sizeof(token));
    started = test_mono_ms();
    rc = cli_credential_token(NULL, "pass", token, sizeof(token));
    elapsed = test_mono_ms() - started;
    parent = read_pid_file(parent_path);
    child = read_pid_file(child_path);
    ASSERT_EQ_INT(rc, -2);
    ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
    ASSERT_TRUE(elapsed < 10000);
    ASSERT_TRUE(parent > 1);
    ASSERT_TRUE(child > 1);
    ASSERT_TRUE(wait_until_gone(parent));
    ASSERT_TRUE(wait_until_gone(child));
    run_finish(&run);
}

/* Stdout is already closed, so the wait for exit is what hits the deadline and kills the group. */
TEST(helper_closes_stdout_then_hangs_is_killed)
{
    RunFx run;
    char token[32];
    char body[640];
    char parent_path[REMEMBER_PATH_MAX];
    char child_path[REMEMBER_PATH_MAX];
    long long started = 0;
    long long elapsed = 0;
    pid_t parent = 0;
    pid_t child = 0;
    int rc = 99;
    int wrote = 0;

    ASSERT_EQ_INT(run_begin(&run), 0);
    wrote = snprintf(body, sizeof(body),
                     "#!/bin/sh\n"
                     "echo $$ > %s/parent.pid\n"
                     "( echo $$ > %s/child.pid; while true; do :; done ) >/dev/null &\n"
                     "i=0\n"
                     "while [ ! -s %s/child.pid ]; do\n"
                     "i=$((i + 1))\n"
                     "if [ \"$i\" -gt 5000 ]; then exit 0; fi\n"
                     "done\n"
                     "printf '  secret  \\n'\n"
                     "exec 1>&-\n"
                     "while true; do :; done\n",
                     run.dir_tmpl, run.dir_tmpl, run.dir_tmpl);
    ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(body));
    ASSERT_EQ_INT(install_script(&run, body, 0755), 0);
    ASSERT_TRUE(snprintf(parent_path, sizeof(parent_path), "%s/parent.pid", run.dir_tmpl) > 0);
    ASSERT_TRUE(snprintf(child_path, sizeof(child_path), "%s/child.pid", run.dir_tmpl) > 0);
    cli_credential_test_set_timeout_ms(2000);
    preload(token, sizeof(token));
    started = test_mono_ms();
    rc = cli_credential_token(NULL, "pass", token, sizeof(token));
    elapsed = test_mono_ms() - started;
    parent = read_pid_file(parent_path);
    child = read_pid_file(child_path);
    ASSERT_EQ_INT(rc, -2);
    ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
    ASSERT_TRUE(elapsed < 8000);
    ASSERT_TRUE(parent > 1);
    ASSERT_TRUE(child > 1);
    ASSERT_TRUE(wait_until_gone(parent));
    ASSERT_TRUE(wait_until_gone(child));
    run_finish(&run);
    ASSERT_STR_NOT_CONTAINS(run.err_got, "secret");
    ASSERT_STR_NOT_CONTAINS(run.out_got, "secret");
}

/* A token printed before the hang is still wiped when the deadline wins. */
TEST(helper_that_prints_then_hangs_is_wiped)
{
    RunFx run;
    char token[32];
    long long started = 0;
    long long elapsed = 0;
    int rc = 99;
    const char *body = "#!/bin/sh\nprintf '  secret  \\n'\nwhile true; do :; done\n";

    ASSERT_EQ_INT(run_begin(&run), 0);
    ASSERT_EQ_INT(install_script(&run, body, 0755), 0);
    cli_credential_test_set_timeout_ms(1000);
    preload(token, sizeof(token));
    started = test_mono_ms();
    rc = cli_credential_token(NULL, "pass", token, sizeof(token));
    elapsed = test_mono_ms() - started;
    ASSERT_EQ_INT(rc, -2);
    ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
    ASSERT_TRUE(elapsed < 8000);
    run_finish(&run);
    ASSERT_STR_NOT_CONTAINS(run.err_got, "secret");
    ASSERT_STR_NOT_CONTAINS(run.out_got, "secret");
}

/* A deadline past the poll clamp still returns the helper's line. */
TEST(deadline_past_one_million_milliseconds_still_returns_the_token)
{
    RunFx run;
    char src[REMEMBER_PATH_MAX];
    char path[REMEMBER_PATH_MAX];
    char token[32];
    int rc = 99;

    ASSERT_EQ_INT(run_begin(&run), 0);
    ASSERT_EQ_INT(credential_helper_src(src, sizeof(src)), 0);
    ASSERT_EQ_INT(helper_path(&run, path, sizeof(path)), 0);
    ASSERT_EQ_INT(symlink(src, path), 0);
    cli_credential_test_set_timeout_ms(2000000);
    preload(token, sizeof(token));
    rc = cli_credential_token(NULL, "pass", token, sizeof(token));
    ASSERT_EQ_INT(rc, 0);
    ASSERT_STREQ(token, "secret");
    run_finish(&run);
    ASSERT_STR_NOT_CONTAINS(run.out_got, "secret");
}

TEST(non_grammar_id_does_not_spawn)
{
    RunFx run;
    char token[32];
    char sentinel[REMEMBER_PATH_MAX];
    char body[256];
    int rc = 99;
    int wrote = 0;

    ASSERT_EQ_INT(run_begin(&run), 0);
    wrote = snprintf(body, sizeof(body),
                     "#!/bin/sh\nprintf x > %s/sentinel\nprintf ' secret \\n'\n", run.dir_tmpl);
    ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(body));
    ASSERT_EQ_INT(install_script(&run, body, 0755), 0);
    ASSERT_TRUE(snprintf(sentinel, sizeof(sentinel), "%s/sentinel", run.dir_tmpl) > 0);
    preload(token, sizeof(token));
    rc = cli_credential_token(NULL, "Pass", token, sizeof(token));
    ASSERT_EQ_INT(rc, -1);
    rc = cli_credential_token(NULL, "../x", token, sizeof(token));
    ASSERT_EQ_INT(rc, -1);
    rc = cli_credential_token(NULL, "", token, sizeof(token));
    ASSERT_EQ_INT(rc, -1);
    rc = cli_credential_token(NULL, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", token,
                              sizeof(token));
    ASSERT_EQ_INT(rc, -1);
    ASSERT_TRUE(access(sentinel, F_OK) != 0);
    rc = cli_credential_token(NULL, "pass", token, sizeof(token));
    ASSERT_EQ_INT(rc, 0);
    ASSERT_STREQ(token, "secret");
    ASSERT_EQ_INT(access(sentinel, F_OK), 0);
    run_finish(&run);
    ASSERT_STR_NOT_CONTAINS(run.out_got, "secret");
}

TEST(runner_rejects_a_missing_buffer_without_spawning)
{
    RunFx run;
    char token[32];
    char sentinel[REMEMBER_PATH_MAX];
    char body[256];
    int rc = 99;
    int wrote = 0;

    ASSERT_EQ_INT(run_begin(&run), 0);
    wrote = snprintf(body, sizeof(body),
                     "#!/bin/sh\nprintf x > %s/sentinel\nprintf ' secret \\n'\n", run.dir_tmpl);
    ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(body));
    ASSERT_EQ_INT(install_script(&run, body, 0755), 0);
    ASSERT_TRUE(snprintf(sentinel, sizeof(sentinel), "%s/sentinel", run.dir_tmpl) > 0);
    rc = cli_credential_token(NULL, NULL, token, sizeof(token));
    ASSERT_EQ_INT(rc, -1);
    rc = cli_credential_token(NULL, "pass", NULL, sizeof(token));
    ASSERT_EQ_INT(rc, -1);
    preload(token, sizeof(token));
    rc = cli_credential_token(NULL, "pass", token, 0U);
    ASSERT_EQ_INT(rc, -1);
    ASSERT_EQ_INT(token[0], 'Z');
    ASSERT_TRUE(access(sentinel, F_OK) != 0);
    run_finish(&run);
}

/* fileno of a memory stream fails. That is -2, and the helper does not run. */
TEST(stderr_fileno_failure_is_lookup_failed)
{
    RunFx run;
    char token[32] = {0};
    char sentinel[REMEMBER_PATH_MAX];
    char body[256];
    char *mem = NULL;
    size_t mem_len = 0U;
    FILE *memf = NULL;
    int rc = 99;
    int wrote = 0;

    ASSERT_EQ_INT(run_begin(&run), 0);
    wrote = snprintf(body, sizeof(body),
                     "#!/bin/sh\nprintf x > %s/sentinel\nprintf ' secret \\n'\n", run.dir_tmpl);
    ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(body));
    ASSERT_EQ_INT(install_script(&run, body, 0755), 0);
    ASSERT_TRUE(snprintf(sentinel, sizeof(sentinel), "%s/sentinel", run.dir_tmpl) > 0);
    memf = open_memstream(&mem, &mem_len);
    ASSERT_TRUE(memf != NULL);
    if (memf == NULL) {
        run_finish(&run);
        return;
    }
    app_set_streams((AppStreams){.out = run.outf, .err = memf});
    preload(token, sizeof(token));
    rc = cli_credential_token(NULL, "pass", token, sizeof(token));
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(memf), 0);
    ASSERT_EQ_INT(rc, -2);
    ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
    ASSERT_TRUE(access(sentinel, F_OK) != 0);
    free(mem);
    run_finish(&run);
}

/* 007-22. The lookup uses the CLI callback. No sync command. */
TEST(pass_on_path_lookup_reads_the_trimmed_line)
{
    RunFx run;
    char tmpl[] = "/tmp/remember-cred-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[64];
    char token[64];
    char src[REMEMBER_PATH_MAX];
    char path[REMEMBER_PATH_MAX];
    char *saved = NULL;
    const char *live = NULL;
    Store *store = NULL;
    RememberEnv host;
    TokGive give;
    SettingWriteResult wr;

    live = getenv("REMEMBER_SYNC_TOKEN");
    if (live != NULL) {
        saved = strdup(live);
        ASSERT_TRUE(saved != NULL);
    }
    ASSERT_EQ_INT(setenv("REMEMBER_SYNC_TOKEN", "LEAKED-SECRET", 1), 0);
    ASSERT_EQ_INT(run_begin(&run), 0);
    ASSERT_EQ_INT(credential_helper_src(src, sizeof(src)), 0);
    ASSERT_EQ_INT(helper_path(&run, path, sizeof(path)), 0);
    ASSERT_EQ_INT(symlink(src, path), 0);
    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    ASSERT_TRUE(store != NULL);
    if (store != NULL) {
        SyncCredentialLookup lookup;
        SettingStatus st = set_one(store, userdb, "remember.sync.credential_helper", "pass",
                                   SETTING_LAYER_USER, NULL, &wr, err, sizeof(err));

        ASSERT_EQ_INT(st, SETTING_OK);
        memset(&give, 0, sizeof(give));
        memset(&host, 0, sizeof(host));
        host.confirm = confirm_must_not_run;
        host.dispatch = dispatch_must_not_run;
        host.token = cli_credential_token;
        host.callback_ctx = &give;
        preload(token, sizeof(token));
        memset(&lookup, 0, sizeof(lookup));
        lookup.store = store;
        lookup.user_db = userdb;
        lookup.host = &host;
        lookup.token = token;
        lookup.token_cap = sizeof(token);
        lookup.err = err;
        lookup.err_cap = sizeof(err);
        {
            int rc = sync_credential_lookup(&lookup);

            ASSERT_EQ_INT(rc, 0);
            ASSERT_STREQ(token, "secret");
            ASSERT_EQ_INT(give.other, 0);
        }
        close_pair(store, tmpl);
    }
    run_finish(&run);
    ASSERT_STR_CONTAINS(run.err_got, "enter passphrase");
    ASSERT_STR_NOT_CONTAINS(run.err_got, "secret");
    ASSERT_STR_NOT_CONTAINS(run.err_got, "LEAKED-SECRET");
    ASSERT_STR_NOT_CONTAINS(run.out_got, "secret");
    if (saved == NULL) {
        (void)unsetenv("REMEMBER_SYNC_TOKEN");
    } else {
        (void)setenv("REMEMBER_SYNC_TOKEN", saved, 1);
    }
    free(saved);
}

TEST(pass_missing_on_path_lookup_is_sync_credential_unavailable)
{
    RunFx run;
    char tmpl[] = "/tmp/remember-cred-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[64];
    char token[64];
    char *saved = NULL;
    const char *live = NULL;
    Store *store = NULL;
    RememberEnv host;
    SettingWriteResult wr;
    const char *plain =
        "remember: the sync credential helper \"pass\" did not return a token. "
        "Check the helper, or set remember.sync.credential_helper to env and export "
        "REMEMBER_SYNC_TOKEN.\n";

    live = getenv("REMEMBER_SYNC_TOKEN");
    if (live != NULL) {
        saved = strdup(live);
        ASSERT_TRUE(saved != NULL);
    }
    ASSERT_EQ_INT(setenv("REMEMBER_SYNC_TOKEN", "LEAKED-SECRET", 1), 0);
    ASSERT_EQ_INT(run_begin(&run), 0);
    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    ASSERT_TRUE(store != NULL);
    if (store != NULL) {
        SyncCredentialLookup lookup;
        SettingStatus st = set_one(store, userdb, "remember.sync.credential_helper", "pass",
                                   SETTING_LAYER_USER, NULL, &wr, err, sizeof(err));

        ASSERT_EQ_INT(st, SETTING_OK);
        memset(&host, 0, sizeof(host));
        host.token = cli_credential_token;
        memset(token, 0, sizeof(token));
        memcpy(token, "LEAKED-SECRET", sizeof("LEAKED-SECRET"));
        memset(&lookup, 0, sizeof(lookup));
        lookup.store = store;
        lookup.user_db = userdb;
        lookup.host = &host;
        lookup.token = token;
        lookup.token_cap = sizeof(token);
        lookup.err = err;
        lookup.err_cap = sizeof(err);
        {
            int rc = sync_credential_lookup(&lookup);

            ASSERT_EQ_INT(rc, 1);
            ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
        }
        close_pair(store, tmpl);
    }
    run_finish(&run);
    ASSERT_STREQ(run.err_got, plain);
    ASSERT_STR_NOT_CONTAINS(run.err_got, "LEAKED-SECRET");
    ASSERT_STR_NOT_CONTAINS(run.out_got, "LEAKED-SECRET");
    if (saved == NULL) {
        (void)unsetenv("REMEMBER_SYNC_TOKEN");
    } else {
        (void)setenv("REMEMBER_SYNC_TOKEN", saved, 1);
    }
    free(saved);
}

/* The message allocation fails. The existing out-of-memory sentence, not a new one. */
TEST(out_of_memory_while_naming_the_helper)
{
    char tmpl[] = "/tmp/remember-cred-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[64];
    char token[64];
    char diag_path[REMEMBER_PATH_MAX];
    char diag_got[128];
    Store *store = NULL;
    RememberEnv host;
    SettingWriteResult wr;
    FILE *diag = NULL;
    int saw = 0;
    int n = 0;

    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    ASSERT_TRUE(store != NULL);
    if (store == NULL) {
        return;
    }
    {
        SettingStatus st = set_one(store, userdb, "remember.sync.credential_helper", "pass",
                                   SETTING_LAYER_USER, NULL, &wr, err, sizeof(err));

        ASSERT_EQ_INT(st, SETTING_OK);
    }
    ASSERT_TRUE(snprintf(diag_path, sizeof(diag_path), "%s/diag", tmpl) > 0);
    memset(&host, 0, sizeof(host));
    host.token = fail_if_token_called;
    for (n = 0; n < 40 && saw == 0; n++) {
        SyncCredentialLookup lookup;

        diag = fopen(diag_path, "w+");
        ASSERT_TRUE(diag != NULL);
        if (diag == NULL) {
            break;
        }
        app_set_streams((AppStreams){.out = NULL, .err = diag});
        memset(token, 0, sizeof(token));
        memcpy(token, "LEAKED-SECRET", sizeof("LEAKED-SECRET"));
        memset(&lookup, 0, sizeof(lookup));
        lookup.store = store;
        lookup.user_db = userdb;
        lookup.host = &host;
        lookup.token = token;
        lookup.token_cap = sizeof(token);
        lookup.err = err;
        lookup.err_cap = sizeof(err);
        test_alloc_fail_after(n);
        {
            int rc = sync_credential_lookup(&lookup);

            test_alloc_fail_after(-1);
            ASSERT_EQ_INT(fflush(diag), 0);
            app_set_streams((AppStreams){.out = NULL, .err = NULL});
            ASSERT_EQ_INT(fseek(diag, 0, SEEK_SET), 0);
            {
                ssize_t got = (ssize_t)fread(diag_got, 1, sizeof(diag_got) - 1U, diag);

                diag_got[got > 0 ? (size_t)got : 0U] = '\0';
            }
            ASSERT_EQ_INT(fclose(diag), 0);
            if (rc == 1 && strcmp(diag_got, "remember: out of memory\n") == 0) {
                ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
                ASSERT_STR_NOT_CONTAINS(diag_got, "LEAKED-SECRET");
                saw = 1;
            }
        }
    }
    test_alloc_fail_after(-1);
    ASSERT_EQ_INT(saw, 1);
    close_pair(store, tmpl);
}

// NOLINTEND(concurrency-mt-unsafe,android-cloexec-fopen)

void register_settings_tests(void)
{
    RUN_TEST(duration_canonical_examples);
    RUN_TEST(duration_rejects_what_is_not_a_whole_millisecond);
    RUN_TEST(size_canonical_examples);
    RUN_TEST(size_rejects_a_fraction_of_a_byte);
    RUN_TEST(integer_grammar);
    RUN_TEST(key_grammar_folds_hyphens_and_requires_a_full_key);
    RUN_TEST(registry_lists_the_six_keys);
    RUN_TEST(user_db_path_follows_remember_user_db_then_xdg_then_home);
    RUN_TEST(user_db_is_created_on_ensure_not_when_a_brain_opens);
    RUN_TEST(ensure_leaves_a_non_database_file_unchanged);
    RUN_TEST(resolution_user_then_brain_then_tombstone_and_change_seq);
    RUN_TEST(user_history_keeps_the_newest_fifty_by_rowid);
    RUN_TEST(quota_counts_settings_rows_not_history);
    RUN_TEST(prefixes_default_scope_and_sixteen_kib);
    RUN_TEST(rejected_values_do_not_create_user_db);
    RUN_TEST(programmer_errors_and_a_failed_prepare);
    RUN_TEST(no_row_reads_remember_sync_token);
    RUN_TEST(no_row_without_remember_sync_token_returns_no_token);
    RUN_TEST(no_row_with_empty_remember_sync_token_returns_no_token);
    RUN_TEST(env_helper_reads_remember_sync_token);
    RUN_TEST(env_helper_without_token_is_sync_credential_unavailable);
    RUN_TEST(pass_helper_reads_the_callback_not_the_env_var);
    RUN_TEST(pass_without_a_callback_is_sync_credential_unavailable);
    RUN_TEST(callback_without_a_nul_is_sync_credential_unavailable);
    RUN_TEST(callback_failure_wipes_the_token);
    RUN_TEST(no_row_keeps_whitespace_in_remember_sync_token);
    RUN_TEST(unavailable_json_names_the_fix_and_hides_the_token);
    RUN_TEST(store_failure_keeps_the_store_sentence);
    RUN_TEST(non_grammar_helper_does_not_call_the_callback);
    RUN_TEST(token_that_does_not_fit_is_not_truncated);
    RUN_TEST(cli_runner_reads_the_trimmed_first_line);
    RUN_TEST(cli_credential_timeout_ms_is_thirty_seconds);
    RUN_TEST(helper_not_on_path_is_unknown);
    RUN_TEST(helper_not_executable_is_lookup_failed);
    RUN_TEST(helper_nonzero_exit_wipes_the_token);
    RUN_TEST(empty_stdout_is_lookup_failed);
    RUN_TEST(first_line_keeps_an_internal_space);
    RUN_TEST(token_line_longer_than_the_buffer_is_lookup_failed);
    RUN_TEST(hanging_helper_group_is_killed);
    RUN_TEST(helper_closes_stdout_then_hangs_is_killed);
    RUN_TEST(helper_that_prints_then_hangs_is_wiped);
    RUN_TEST(deadline_past_one_million_milliseconds_still_returns_the_token);
    RUN_TEST(non_grammar_id_does_not_spawn);
    RUN_TEST(runner_rejects_a_missing_buffer_without_spawning);
    RUN_TEST(stderr_fileno_failure_is_lookup_failed);
    RUN_TEST(pass_on_path_lookup_reads_the_trimmed_line);
    RUN_TEST(pass_missing_on_path_lookup_is_sync_credential_unavailable);
    RUN_TEST(out_of_memory_while_naming_the_helper);
}
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
