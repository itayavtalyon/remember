#include "appio.h"
#include "cmd_settings.h"
#include "diag.h"
#include "harness.h"
#include "normalize.h"
#include "register.h"
#include "remember_app.h"
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
    "remember: \"remember.plugins.idle_timeout\" is a user setting; it can't be set for one "
    "brain. Run the command again with --scope user, or without --scope.\n";
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

/* A row in a layer the key does not allow is no value, on each side. */
TEST(pick_ignores_a_layer_the_key_does_not_allow)
{
    const SettingSpec *budget = setting_by_key("remember.plugins.chain_budget");
    const SettingSpec *prefixes = setting_by_key("remember.buckets.prefixes");
    char out[SETTING_VALUE_CAP];

    ASSERT_TRUE(budget != NULL && prefixes != NULL);
    if (budget == NULL || prefixes == NULL) {
        return;
    }
    ASSERT_EQ_INT(setting_pick(budget, "\"2s\"", "\"30s\"", NULL, out, sizeof(out)),
                  SETTING_FROM_USER);
    ASSERT_STREQ(out, "\"2s\"");
    ASSERT_EQ_INT(setting_pick(budget, NULL, "\"30s\"", NULL, out, sizeof(out)),
                  SETTING_FROM_DEFAULT);
    ASSERT_STREQ(out, "\"1.5s\"");
    ASSERT_EQ_INT(setting_pick(prefixes, "[\"a:\"]", NULL, NULL, out, sizeof(out)),
                  SETTING_FROM_DEFAULT);
    ASSERT_STREQ(out, "[\"project:\",\"status:\"]");
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
    ASSERT_STREQ(err, "");
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

/* Copy REMEMBER_SYNC_TOKEN so the test can change it. False after a failed
   assert when the copy cannot be made. */
static bool save_sync_token(char **saved)
{
    const char *live = getenv("REMEMBER_SYNC_TOKEN");

    *saved = NULL;
    if (live == NULL) {
        return true;
    }
    *saved = strdup(live);
    ASSERT_TRUE(*saved != NULL);
    return *saved != NULL;
}

static void restore_sync_token(char *saved)
{
    if (saved == NULL) {
        (void)unsetenv("REMEMBER_SYNC_TOKEN");
    } else {
        (void)setenv("REMEMBER_SYNC_TOKEN", saved, 1);
    }
    free(saved);
}

static void preload(char *token, size_t cap)
{
    memset(token, 'Z', cap - 1U);
    token[cap - 1U] = '\0';
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

/* Run one lookup with the diag stream captured into *diag_out.
   json writes the envelope error object (plan 21) instead of a plain line. */
static int cred_lookup_mode(SyncCredentialLookup in, char **diag_out, bool json)
{
    char *diag_buf = NULL;
    size_t diag_len = 0U;
    FILE *diag = open_memstream(&diag_buf, &diag_len);
    int rc = 99;

    *diag_out = NULL;
    if (diag == NULL) {
        return 99;
    }
    if (json && diag_json_begin("settings") != 0) {
        ASSERT_EQ_INT(fclose(diag), 0);
        free(diag_buf);
        return 99;
    }
    app_set_streams((AppStreams){.out = NULL, .err = diag});
    rc = sync_credential_lookup(&in);
    if (json && diag_json_write_error(diag) != 0) {
        rc = 99;
    }
    if (json) {
        diag_json_end();
    }
    ASSERT_EQ_INT(fflush(diag), 0);
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(diag), 0);
    *diag_out = diag_buf;
    return rc;
}

static int cred_lookup(SyncCredentialLookup in, char **diag_out)
{
    return cred_lookup_mode(in, diag_out, false);
}

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
    Store *store = NULL;
    RememberEnv host;
    int calls = 0;
    int rc = 99;

    if (!save_sync_token(&saved)) {
        return;
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
    rc = cred_lookup((SyncCredentialLookup){.store = store,
                                            .user_db = userdb,
                                            .host = &host,
                                            .token = token,
                                            .token_cap = sizeof(token),
                                            .err = err,
                                            .err_cap = sizeof(err)},
                     &diag_buf);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_STREQ(token, "s3cret-token");
    ASSERT_TRUE(memchr(token + strlen(token) + 1U, 'Z', sizeof(token) - strlen(token) - 1U) ==
                NULL);
    ASSERT_EQ_INT(calls, 0);
    ASSERT_STREQ(diag_buf, "");
    ASSERT_STR_NOT_CONTAINS(diag_buf, "s3cret-token");
    restore_sync_token(saved);
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
    Store *store = NULL;
    RememberEnv host;
    int calls = 0;
    int rc = 99;

    if (!save_sync_token(&saved)) {
        return;
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
    rc = cred_lookup((SyncCredentialLookup){.store = store,
                                            .user_db = userdb,
                                            .host = &host,
                                            .token = token,
                                            .token_cap = sizeof(token),
                                            .err = err,
                                            .err_cap = sizeof(err)},
                     &diag_buf);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_STREQ(token, "");
    ASSERT_EQ_INT(calls, 0);
    ASSERT_STREQ(diag_buf, "");
    restore_sync_token(saved);
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
    Store *store = NULL;
    int rc = 99;

    if (!save_sync_token(&saved)) {
        return;
    }
    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    if (store == NULL) {
        free(saved);
        return;
    }
    ASSERT_EQ_INT(setenv("REMEMBER_SYNC_TOKEN", "", 1), 0);
    memset(token, 'Z', sizeof(token) - 1U);
    token[sizeof(token) - 1U] = '\0';
    rc = cred_lookup((SyncCredentialLookup){.store = store,
                                            .user_db = userdb,
                                            .host = NULL,
                                            .token = token,
                                            .token_cap = sizeof(token),
                                            .err = err,
                                            .err_cap = sizeof(err)},
                     &diag_buf);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_STREQ(token, "");
    ASSERT_STREQ(diag_buf, "");
    restore_sync_token(saved);
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
    Store *store = NULL;
    RememberEnv host;
    SettingWriteResult wr;
    SettingStatus st = SETTING_OK;
    int calls = 0;
    int rc = 99;

    if (!save_sync_token(&saved)) {
        return;
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
    rc = cred_lookup((SyncCredentialLookup){.store = store,
                                            .user_db = userdb,
                                            .host = &host,
                                            .token = token,
                                            .token_cap = sizeof(token),
                                            .err = err,
                                            .err_cap = sizeof(err)},
                     &diag_buf);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_STREQ(token, "s3cret-token");
    ASSERT_EQ_INT(calls, 0);
    ASSERT_STREQ(diag_buf, "");
    restore_sync_token(saved);
    free(diag_buf);
    close_pair(store, tmpl);
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
    Store *store = NULL;
    RememberEnv host;
    SettingWriteResult wr;
    SettingStatus st = SETTING_OK;
    SyncCredentialLookup in;
    int calls = 0;
    int rc = 99;
    const char *plain =
        "remember: the sync credential helper \"env\" did not return a token. "
        "Check the helper, or set remember.sync.credential_helper to env and export "
        "REMEMBER_SYNC_TOKEN.\n";

    if (!save_sync_token(&saved)) {
        return;
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
    restore_sync_token(saved);
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
    Store *store = NULL;
    RememberEnv host;
    SettingWriteResult wr;
    SettingStatus st = SETTING_OK;
    SyncCredentialLookup in;
    TokGive give;
    int rc = 99;

    if (!save_sync_token(&saved)) {
        return;
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
    restore_sync_token(saved);
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
    SyncCredentialLookup in;
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
    SyncCredentialLookup in;
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
    SyncCredentialLookup in;
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

/* Itay 2026-10-30. REMEMBER_SYNC_TOKEN is ASCII-trimmed like the helper line.
   Only whitespace is no token. A control byte or DEL inside is refused: 2 with
   no row (C-42), the locked unavailable line with the id env. */
TEST(remember_sync_token_is_trimmed_and_checked)
{
    char tmpl[] = "/tmp/remember-cred-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[64];
    char token[64];
    char *saved = NULL;
    char *diag_buf = NULL;
    Store *store = NULL;
    SettingWriteResult wr;
    SyncCredentialLookup in;
    static const char *const refused[] = {"a\x1b[31mb", "a\rb", "a\nb", "a\tb", "\x7f"};
    size_t i = 0U;
    int rc = 99;

    if (!save_sync_token(&saved)) {
        return;
    }
    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    if (store == NULL) {
        free(saved);
        return;
    }
    memset(&in, 0, sizeof(in));
    in.store = store;
    in.user_db = userdb;
    in.token = token;
    in.token_cap = sizeof(token);
    in.err = err;
    in.err_cap = sizeof(err);
    ASSERT_EQ_INT(setenv("REMEMBER_SYNC_TOKEN", "  tok\t\r\n", 1), 0);
    preload(token, sizeof(token));
    rc = cred_lookup(in, &diag_buf);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_STREQ(token, "tok");
    ASSERT_TRUE(bytes_are_zero(token + 3, sizeof(token) - 3U));
    ASSERT_STREQ(diag_buf, "");
    free(diag_buf);
    ASSERT_EQ_INT(setenv("REMEMBER_SYNC_TOKEN", " \t\v\f ", 1), 0);
    preload(token, sizeof(token));
    rc = cred_lookup(in, &diag_buf);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
    ASSERT_STREQ(diag_buf, "");
    free(diag_buf);
    for (i = 0U; i < sizeof(refused) / sizeof(refused[0]); i++) {
        ASSERT_EQ_INT(setenv("REMEMBER_SYNC_TOKEN", refused[i], 1), 0);
        preload(token, sizeof(token));
        rc = cred_lookup(in, &diag_buf);
        ASSERT_EQ_INT(rc, 2);
        ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
        ASSERT_STREQ(diag_buf, "");
        free(diag_buf);
    }
    ASSERT_EQ_INT(set_one(store, userdb, "remember.sync.credential_helper", "env",
                          SETTING_LAYER_USER, NULL, &wr, err, sizeof(err)),
                  SETTING_OK);
    ASSERT_EQ_INT(setenv("REMEMBER_SYNC_TOKEN", "a\rLEAKED-SECRET", 1), 0);
    preload(token, sizeof(token));
    rc = cred_lookup(in, &diag_buf);
    ASSERT_EQ_INT(rc, 1);
    ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
    ASSERT_STR_CONTAINS(diag_buf, "helper \"env\" did not return a token.");
    ASSERT_STR_NOT_CONTAINS(diag_buf, "LEAKED-SECRET");
    free(diag_buf);
    ASSERT_EQ_INT(setenv("REMEMBER_SYNC_TOKEN", "   ", 1), 0);
    rc = cred_lookup(in, &diag_buf);
    ASSERT_EQ_INT(rc, 1);
    ASSERT_STR_CONTAINS(diag_buf, "helper \"env\" did not return a token.");
    free(diag_buf);
    restore_sync_token(saved);
    close_pair(store, tmpl);
}

/* A host callback that answers with a control byte gives no token. */
TEST(callback_token_with_a_control_byte_is_unavailable)
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
    SyncCredentialLookup in;
    TokGive give;
    int rc = 99;

    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    if (store == NULL) {
        return;
    }
    ASSERT_EQ_INT(set_one(store, userdb, "remember.sync.credential_helper", "pass",
                          SETTING_LAYER_USER, NULL, &wr, err, sizeof(err)),
                  SETTING_OK);
    memset(&give, 0, sizeof(give));
    give.give = "tok\r\nX-Evil: 1";
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
    ASSERT_STR_CONTAINS(diag_buf, "helper \"pass\" did not return a token.");
    ASSERT_STR_NOT_CONTAINS(diag_buf, "X-Evil");
    free(diag_buf);
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
    SyncCredentialLookup in;
    int rc = 99;
    const char *json =
        "\"error\":{\"code\":\"sync_credential_unavailable\",\"message\":\"the "
        "sync credential helper \\\"pass\\\" did not return a "
        "token.\",\"fix\":\"Check the helper, or set remember.sync.credential_helper to env and "
        "export REMEMBER_SYNC_TOKEN.\",\"retryable\":false}";

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
    in.token = token;
    in.token_cap = sizeof(token);
    in.err = err;
    in.err_cap = sizeof(err);
    rc = cred_lookup_mode(in, &diag_buf, true);
    ASSERT_EQ_INT(rc, 1);
    ASSERT_STREQ(diag_buf, json);
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
    SyncCredentialLookup in;
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
    SyncCredentialLookup in;
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

static size_t count_char(const char *s, char c)
{
    size_t n = 0U;

    if (s == NULL) {
        return 0U;
    }
    for (; *s != '\0'; s++) {
        n += (*s == c) ? 1U : 0U;
    }
    return n;
}

/* Round 3.3. A hand-edited id with a newline, an ESC, or 300 bytes is echoed
   on one plain line, escaped in JSON, and not cut. */
TEST(unavailable_echo_stays_one_line_in_both_formats)
{
    char tmpl[] = "/tmp/remember-cred-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[64];
    char token[64];
    char longid[301];
    char sql[512];
    char *diag_buf = NULL;
    Store *store = NULL;
    SettingWriteResult wr;
    SyncCredentialLookup in;
    int rc = 99;

    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    if (store == NULL) {
        return;
    }
    ASSERT_EQ_INT(set_one(store, userdb, "remember.sync.credential_helper", "pass",
                          SETTING_LAYER_USER, NULL, &wr, err, sizeof(err)),
                  SETTING_OK);
    memset(&in, 0, sizeof(in));
    in.store = store;
    in.user_db = userdb;
    in.token = token;
    in.token_cap = sizeof(token);
    in.err = err;
    in.err_cap = sizeof(err);
    exec_fixture(userdb, "UPDATE settings SET value = '\"a' || char(10) || 'b' || char(27) || "
                         "'[31mc\"' WHERE key = 'remember.sync.credential_helper';");
    rc = cred_lookup(in, &diag_buf);
    ASSERT_EQ_INT(rc, 1);
    ASSERT_STR_CONTAINS(diag_buf, "helper \"a?b?[31mc\" did not");
    ASSERT_EQ_INT((int)count_char(diag_buf, '\n'), 1);
    ASSERT_EQ_INT((int)count_char(diag_buf, '\x1b'), 0);
    free(diag_buf);
    rc = cred_lookup_mode(in, &diag_buf, true);
    ASSERT_EQ_INT(rc, 1);
    ASSERT_STR_CONTAINS(diag_buf, "helper \\\"a\\nb\\u001b[31mc\\\" did not");
    ASSERT_EQ_INT((int)count_char(diag_buf, '\n'), 0);
    free(diag_buf);
    memset(longid, 'x', sizeof(longid) - 1U);
    longid[sizeof(longid) - 1U] = '\0';
    ASSERT_TRUE(snprintf(sql, sizeof(sql),
                         "UPDATE settings SET value = '\"%s\"' WHERE key = "
                         "'remember.sync.credential_helper';",
                         longid) < (int)sizeof(sql));
    exec_fixture(userdb, sql);
    rc = cred_lookup(in, &diag_buf);
    ASSERT_EQ_INT(rc, 1);
    ASSERT_STR_CONTAINS(diag_buf, longid);
    free(diag_buf);
    close_pair(store, tmpl);
}

/* A secret that does not fit is not truncated. No row is 2 (C-42), with no
   notice. A set key is unavailable, and the secret is not in the notice. */
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
    Store *store = NULL;
    SettingWriteResult wr;
    SettingStatus st = SETTING_OK;
    SyncCredentialLookup in;
    int rc = 99;

    if (!save_sync_token(&saved)) {
        return;
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
    ASSERT_EQ_INT(rc, 2);
    ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
    ASSERT_STREQ(diag_buf, "");
    free(diag_buf);
    in.token = exact;
    in.token_cap = sizeof(exact);
    rc = cred_lookup(in, &diag_buf);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_STREQ(exact, "hello");
    free(diag_buf);
    /* Seven bytes raw, five after the trim: the trimmed length is what must fit. */
    ASSERT_EQ_INT(setenv("REMEMBER_SYNC_TOKEN", " hello ", 1), 0);
    rc = cred_lookup(in, &diag_buf);
    ASSERT_EQ_INT(rc, 0);
    ASSERT_STREQ(exact, "hello");
    free(diag_buf);
    ASSERT_EQ_INT(setenv("REMEMBER_SYNC_TOKEN", "hello", 1), 0);
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
    restore_sync_token(saved);
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
    {
        /* fclose as a statement. Linux cppcheck does not treat fclose inside
           ASSERT_EQ_INT as a close, so the success path looked like a leak. */
        int err_close = fclose(errf);
        int out_close = fclose(outf);

        errf = NULL;
        outf = NULL;
        ASSERT_EQ_INT(err_close, 0);
        ASSERT_EQ_INT(out_close, 0);
    }
    ASSERT_EQ_INT(rc, 0);
    ASSERT_STREQ(token, "secret");
    /* "  secret  " moved left two bytes. Nothing of it stays after the NUL. */
    ASSERT_TRUE(bytes_are_zero(token + 6, sizeof(token) - 6U));
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

/* A NUL inside the first line would cut the token short. It is refused. */
TEST(nul_in_the_first_line_is_lookup_failed)
{
    RunFx run;
    char token[32];

    ASSERT_EQ_INT(run_begin(&run), 0);
    ASSERT_EQ_INT(install_script(&run, "#!/bin/sh\nprintf 'ab\\000cd\\n'\nexit 0\n", 0755), 0);
    preload(token, sizeof(token));
    ASSERT_EQ_INT(cli_credential_token(NULL, "pass", token, sizeof(token)), -2);
    ASSERT_TRUE(bytes_are_zero(token, sizeof(token)));
    run_finish(&run);
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
    restore_sync_token(saved);
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
    restore_sync_token(saved);
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

/* ST-3.1 scope is user. A brain row (sync, a peer, a hand edit) never picks the helper. */
TEST(brain_row_does_not_pick_the_credential_helper)
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
    SyncCredentialLookup in;
    TokGive give;
    int rc = 99;

    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    if (store == NULL) {
        return;
    }
    exec_fixture(brain, "INSERT INTO settings (key, value, updated_by, updated_at, device_id, "
                        "version_vector, deleted_at, change_seq) VALUES "
                        "('remember.sync.credential_helper','\"evil\"',"
                        "'{\"kind\":\"human\",\"id\":null}','2030-01-01T00:00:00.000Z','peer',"
                        "'{}',NULL,1)");
    assert_query(brain, "SELECT value FROM settings", "\"evil\"");
    ASSERT_EQ_INT(set_one(store, userdb, "remember.sync.credential_helper", "pass",
                          SETTING_LAYER_USER, NULL, &wr, err, sizeof(err)),
                  SETTING_OK);
    memset(&give, 0, sizeof(give));
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
    ASSERT_EQ_INT(rc, 0);
    ASSERT_EQ_INT(give.calls, 1);
    ASSERT_STREQ(give.seen, "pass");
    ASSERT_STREQ(token, "from-helper");
    free(diag_buf);
    close_pair(store, tmpl);
}

/* 007 example and ST-7.3. TZ=UTC makes 10:04Z print as 21 Oct at 10:04.
   The middle dot is the locked example character. */
TEST(get_reports_the_winning_layer_and_every_other_layer)
{
    char tmpl[] = "/tmp/remember-get-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[256];
    char user_copy[REMEMBER_PATH_MAX];
    char tz_copy[64];
    char human_want[320];
    const char *saved_user = getenv("REMEMBER_USER_DB");
    const char *saved_tz = getenv("TZ");
    const char *when_text = "21 Oct 2026 at 10:04";
    bool had_user = saved_user != NULL;
    bool had_tz = saved_tz != NULL;
    Store *store = NULL;
    SettingWriteResult wr;
    const char *human_args[] = {"settings", "get", "remember.search.candidate_pool"};
    const char *json_args[] = {"--json", "settings", "get", "remember.search.candidate_pool"};
    CmdResult human;
    CmdResult json;
    time_t now_sec = 0;
    struct tm now_tm;

    if (had_user) {
        ASSERT_TRUE(snprintf(user_copy, sizeof(user_copy), "%s", saved_user) > 0);
        saved_user = user_copy;
    }
    if (had_tz) {
        ASSERT_TRUE(snprintf(tz_copy, sizeof(tz_copy), "%s", saved_tz) > 0);
        saved_tz = tz_copy;
    }
    store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    if (store == NULL) {
        return;
    }
    ASSERT_EQ_INT(set_one(store, userdb, "remember.search.candidate_pool", "400",
                          SETTING_LAYER_BRAIN, NULL, &wr, err, sizeof(err)),
                  SETTING_OK);
    exec_fixture(brain, "UPDATE settings SET updated_at = '2026-10-21T10:04:00.000Z' "
                        "WHERE key = 'remember.search.candidate_pool';");
    store_close(store);
    ASSERT_EQ_INT(setenv("REMEMBER_USER_DB", userdb, 1), 0);
    ASSERT_EQ_INT(setenv("TZ", "UTC", 1), 0);
    now_sec = time(NULL);
    ASSERT_TRUE(localtime_r(&now_sec, &now_tm) != NULL);
    if (now_tm.tm_year + 1900 == 2026) {
        when_text = "21 Oct at 10:04";
    }
    human = run_remember(brain, human_args, sizeof(human_args) / sizeof(human_args[0]), NULL);
    json = run_remember(brain, json_args, sizeof(json_args) / sizeof(json_args[0]), NULL);
    restore_env("REMEMBER_USER_DB", had_user, saved_user);
    restore_env("TZ", had_tz, saved_tz);
    ASSERT_TRUE(snprintf(human_want, sizeof(human_want),
                         "remember.search.candidate_pool = 400\n"
                         "  from brain, set by human on %s\n"
                         "  user: (unset) · default: 200\n",
                         when_text) > 0);
    ASSERT_EQ_INT(human.exit_code, 0);
    ASSERT_STREQ(human.err, "");
    ASSERT_STREQ(human.out, human_want);
    ASSERT_EQ_INT(json.exit_code, 0);
    ASSERT_STREQ(json.err, "");
    ASSERT_STREQ(json.out,
                 "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.get.result\","
                 "\"data\":{\"setting\":{\"key\":\"remember.search.candidate_pool\","
                 "\"type\":\"integer\",\"value\":400,\"from\":\"brain\",\"changed\":true,"
                 "\"layers\":{\"default\":{\"value\":200},\"user\":null,"
                 "\"brain\":{\"value\":400,\"updated_by\":{\"kind\":\"human\",\"id\":null},"
                 "\"updated_at\":\"2026-10-21T10:04:00.000Z\"}}}}}\n");
    cmd_result_free(&human);
    cmd_result_free(&json);
    harness_remove_tree(tmpl);
}

typedef struct {
    char user_copy[REMEMBER_PATH_MAX];
    char home_copy[REMEMBER_PATH_MAX];
    char xdg_copy[REMEMBER_PATH_MAX];
    char tz_copy[128];
    bool had_user;
    bool had_home;
    bool had_xdg;
    bool had_tz;
    char pad_[4];
} EnvHold;

static void env_hold_one(char *copy, size_t cap, const char *name, bool *had)
{
    const char *value = getenv(name);

    *had = value != NULL;
    copy[0] = '\0';
    if (value != NULL) {
        ASSERT_TRUE(snprintf(copy, cap, "%s", value) > 0);
    }
}

static void env_hold(EnvHold *hold)
{
    env_hold_one(hold->user_copy, sizeof(hold->user_copy), "REMEMBER_USER_DB", &hold->had_user);
    env_hold_one(hold->home_copy, sizeof(hold->home_copy), "HOME", &hold->had_home);
    env_hold_one(hold->xdg_copy, sizeof(hold->xdg_copy), "XDG_DATA_HOME", &hold->had_xdg);
    env_hold_one(hold->tz_copy, sizeof(hold->tz_copy), "TZ", &hold->had_tz);
}

static void env_release(const EnvHold *hold)
{
    restore_env("REMEMBER_USER_DB", hold->had_user, hold->user_copy);
    restore_env("HOME", hold->had_home, hold->home_copy);
    restore_env("XDG_DATA_HOME", hold->had_xdg, hold->xdg_copy);
    restore_env("TZ", hold->had_tz, hold->tz_copy);
}

/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — brain path, then user db path */
static CmdResult get_at(const char *brain, const char *userdb, const char *const *args, size_t n)
{
    EnvHold hold;
    CmdResult got;

    env_hold(&hold);
    ASSERT_EQ_INT(setenv("REMEMBER_USER_DB", userdb, 1), 0);
    ASSERT_EQ_INT(setenv("TZ", "UTC", 1), 0);
    got = run_remember(brain, args, n, NULL);
    env_release(&hold);
    return got;
}

static const char k_agent[] = "{\"kind\":\"agent\",\"id\":\"bot1\"}";
static const char k_past[] = "2020-01-02T03:04:00.000Z";

TEST(format_when_prints_the_locked_date_and_the_utc_fallback)
{
    SettingWhen now_year = {.year = 2026,
                            .month = 10,
                            .day = 21,
                            .hour = 10,
                            .minute = 4,
                            .year_now = 2026,
                            .utc_suffix = false};
    SettingWhen other = {.year = 2025,
                         .month = 10,
                         .day = 21,
                         .hour = 12,
                         .minute = 4,
                         .year_now = 2026,
                         .utc_suffix = true};
    SettingWhen day = {.year = 2026,
                       .month = 1,
                       .day = 2,
                       .hour = 3,
                       .minute = 4,
                       .year_now = 2026,
                       .utc_suffix = false};
    char buf[64];

    ASSERT_TRUE(setting_format_when(now_year, buf, sizeof(buf)));
    ASSERT_STREQ(buf, "21 Oct at 10:04");
    ASSERT_TRUE(setting_format_when(other, buf, sizeof(buf)));
    ASSERT_STREQ(buf, "21 Oct 2025 at 12:04 UTC");
    ASSERT_TRUE(setting_format_when(day, buf, sizeof(buf)));
    ASSERT_STREQ(buf, "2 Jan at 03:04");
    ASSERT_FALSE(setting_format_when(day, buf, 4));
}

TEST(suggestion_names_one_key_or_nothing)
{
    char long_key[128];

    memset(long_key, 'a', sizeof(long_key) - 1U);
    long_key[sizeof(long_key) - 1U] = '\0';
    ASSERT_TRUE(setting_suggestion(NULL) == NULL);
    ASSERT_TRUE(setting_suggestion("") == NULL);
    ASSERT_STREQ(setting_suggestion("candidate_pool"), "remember.search.candidate_pool");
    ASSERT_STREQ(setting_suggestion("remember:search/candidate_pool"),
                 "remember.search.candidate_pool");
    ASSERT_TRUE(setting_suggestion("remember:search:candidate_poo") == NULL);
    ASSERT_STREQ(setting_suggestion("remember.search.candidate_poo"),
                 "remember.search.candidate_pool");
    ASSERT_TRUE(setting_suggestion("remember.search.hybrid") == NULL);
    ASSERT_TRUE(setting_suggestion("remember.search.max_resXXXX") == NULL);
    ASSERT_TRUE(setting_suggestion(long_key) == NULL);
}

TEST(get_default_only_names_every_allowed_layer)
{
    char tmpl[] = "/tmp/remember-get-def-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *args[] = {"settings", "get", "remember.search.candidate_pool"};
    const char *json_args[] = {"--json", "settings", "get", "remember.search.candidate_pool"};
    CmdResult human;
    CmdResult json;

    if (store == NULL) {
        return;
    }
    store_close(store);
    human = get_at(brain, userdb, args, sizeof(args) / sizeof(args[0]));
    json = get_at(brain, userdb, json_args, sizeof(json_args) / sizeof(json_args[0]));
    ASSERT_EQ_INT(human.exit_code, 0);
    ASSERT_STREQ(human.err, "");
    ASSERT_STREQ(human.out, "remember.search.candidate_pool = 200\n"
                            "  from default\n"
                            "  user: (unset) · brain: (unset)\n");
    ASSERT_EQ_INT(json.exit_code, 0);
    ASSERT_STREQ(json.err, "");
    ASSERT_STREQ(json.out,
                 "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.get.result\","
                 "\"data\":{\"setting\":{\"key\":\"remember.search.candidate_pool\","
                 "\"type\":\"integer\",\"value\":200,\"from\":\"default\",\"changed\":false,"
                 "\"layers\":{\"default\":{\"value\":200},\"user\":null,\"brain\":null}}}}\n");
    cmd_result_free(&human);
    cmd_result_free(&json);
    harness_remove_tree(tmpl);
}

TEST(get_duration_quotes_the_text_and_adds_value_ms)
{
    char tmpl[] = "/tmp/remember-get-dur-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[256];
    char sql[256];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    SettingWriteResult wr;
    const char *args[] = {"settings", "get", "remember.plugins.chain_budget"};
    CmdResult human;

    if (store == NULL) {
        return;
    }
    ASSERT_EQ_INT(set_one(store, userdb, "remember.plugins.chain_budget", "2s", SETTING_LAYER_USER,
                          NULL, &wr, err, sizeof(err)),
                  SETTING_OK);
    ASSERT_TRUE(snprintf(sql, sizeof(sql),
                         "UPDATE settings SET updated_at = '%s' WHERE key = "
                         "'remember.plugins.chain_budget';",
                         k_past) > 0);
    exec_fixture(userdb, sql);
    store_close(store);
    human = get_at(brain, userdb, args, sizeof(args) / sizeof(args[0]));
    ASSERT_EQ_INT(human.exit_code, 0);
    ASSERT_STREQ(human.err, "");
    ASSERT_STREQ(human.out, "remember.plugins.chain_budget = 2s\n"
                            "  from user, set by human on 2 Jan 2020 at 03:04\n"
                            "  default: 1.5s\n");
    cmd_result_free(&human);
    {
        const char *json_args[] = {"--json", "settings", "get", "remember.plugins.chain_budget"};
        CmdResult json = get_at(brain, userdb, json_args, sizeof(json_args) / sizeof(json_args[0]));

        ASSERT_EQ_INT(json.exit_code, 0);
        ASSERT_STREQ(json.err, "");
        ASSERT_STREQ(json.out, "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting."
                               "get.result\",\"data\":{\"setting\":{\"key\":\"remember.plugins."
                               "chain_budget\",\"type\":\"duration\",\"value\":\"2s\","
                               "\"value_ms\":2000,\"from\":\"user\",\"changed\":true,\"layers\":{"
                               "\"default\":{\"value\":\"1.5s\",\"value_ms\":1500},\"user\":{"
                               "\"value\":\"2s\",\"value_ms\":2000,\"updated_by\":{\"kind\":"
                               "\"human\",\"id\":null},\"updated_at\":\"2020-01-02T03:04:00.000Z\""
                               "}}}}}\n");
        cmd_result_free(&json);
    }
    harness_remove_tree(tmpl);
}

TEST(get_prefixes_join_with_a_space)
{
    char tmpl[] = "/tmp/remember-get-pre-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *args[] = {"settings", "get", "remember.buckets.prefixes"};
    const char *json_args[] = {"--json", "settings", "get", "remember.buckets.prefixes"};
    CmdResult human;
    CmdResult json;

    if (store == NULL) {
        return;
    }
    store_close(store);
    human = get_at(brain, userdb, args, sizeof(args) / sizeof(args[0]));
    json = get_at(brain, userdb, json_args, sizeof(json_args) / sizeof(json_args[0]));
    ASSERT_EQ_INT(human.exit_code, 0);
    ASSERT_STREQ(human.out, "remember.buckets.prefixes = project: status:\n"
                            "  from default\n"
                            "  brain: (unset)\n");
    ASSERT_STREQ(json.out, "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.get."
                           "result\",\"data\":{\"setting\":{\"key\":\"remember.buckets.prefixes\","
                           "\"type\":\"string_list\",\"value\":[\"project:\",\"status:\"],"
                           "\"from\":\"default\",\"changed\":false,\"layers\":{\"default\":{"
                           "\"value\":[\"project:\",\"status:\"]},\"brain\":null}}}}\n");
    cmd_result_free(&human);
    cmd_result_free(&json);
    harness_remove_tree(tmpl);
}

TEST(get_string_with_no_compiled_default_is_null)
{
    char tmpl[] = "/tmp/remember-get-str-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[256];
    char sql[256];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    SettingWriteResult wr;
    const char *args[] = {"settings", "get", "remember.sync.credential_helper"};
    CmdResult human;

    if (store == NULL) {
        return;
    }
    ASSERT_EQ_INT(set_one(store, userdb, "remember.sync.credential_helper", "pass",
                          SETTING_LAYER_USER, NULL, &wr, err, sizeof(err)),
                  SETTING_OK);
    ASSERT_TRUE(snprintf(sql, sizeof(sql),
                         "UPDATE settings SET updated_at = '%s' WHERE key = "
                         "'remember.sync.credential_helper';",
                         k_past) > 0);
    exec_fixture(userdb, sql);
    store_close(store);
    human = get_at(brain, userdb, args, sizeof(args) / sizeof(args[0]));
    ASSERT_EQ_INT(human.exit_code, 0);
    ASSERT_STREQ(human.out, "remember.sync.credential_helper = pass\n"
                            "  from user, set by human on 2 Jan 2020 at 03:04\n"
                            "  default: (unset)\n");
    cmd_result_free(&human);
    {
        const char *scope_args[] = {"settings", "get", "--scope", "user",
                                    "remember.sync.credential_helper"};
        const char *scope_json[] = {"--json",  "settings", "get",
                                    "--scope", "user",     "remember.sync.credential_helper"};
        CmdResult scoped =
            get_at(brain, userdb, scope_args, sizeof(scope_args) / sizeof(scope_args[0]));
        CmdResult scoped_json =
            get_at(brain, userdb, scope_json, sizeof(scope_json) / sizeof(scope_json[0]));

        ASSERT_EQ_INT(scoped.exit_code, 0);
        ASSERT_STREQ(scoped.out, "remember.sync.credential_helper = pass\n"
                                 "  from user, set by human on 2 Jan 2020 at 03:04\n");
        ASSERT_STREQ(scoped.err, "");
        ASSERT_EQ_INT(scoped_json.exit_code, 0);
        ASSERT_STREQ(scoped_json.out,
                     "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.get."
                     "result\",\"data\":{\"setting\":{\"key\":\"remember.sync.credential_helper\","
                     "\"type\":\"string\",\"value\":\"pass\",\"from\":\"user\",\"changed\":true,"
                     "\"layers\":{\"user\":{\"value\":\"pass\",\"updated_by\":{\"kind\":\"human\","
                     "\"id\":null},\"updated_at\":\"2020-01-02T03:04:00.000Z\"}}}}}\n");
        cmd_result_free(&scoped);
        cmd_result_free(&scoped_json);
    }
    {
        const char *json_args[] = {"--json", "settings", "get", "remember.sync.credential_helper"};
        CmdResult json = get_at(brain, userdb, json_args, sizeof(json_args) / sizeof(json_args[0]));

        ASSERT_EQ_INT(json.exit_code, 0);
        ASSERT_STREQ(json.out,
                     "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.get."
                     "result\",\"data\":{\"setting\":{\"key\":\"remember.sync.credential_helper\","
                     "\"type\":\"string\",\"value\":\"pass\",\"from\":\"user\",\"changed\":true,"
                     "\"layers\":{\"default\":null,\"user\":{\"value\":\"pass\",\"updated_by\":{"
                     "\"kind\":\"human\",\"id\":null},\"updated_at\":\"2020-01-02T03:04:00.000Z\""
                     "}}}}}\n");
        cmd_result_free(&json);
    }
    harness_remove_tree(tmpl);
}

TEST(get_brain_beats_user_and_scope_hides_the_other_layers)
{
    char tmpl[] = "/tmp/remember-get-win-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[256];
    char sql[320];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    SettingWriteResult wr;
    const char *args[] = {"settings", "get", "remember.search.candidate_pool"};
    const char *scope_args[] = {"settings", "get", "--scope", "brain",
                                "remember.search.candidate_pool"};
    CmdResult human;
    CmdResult scoped;

    if (store == NULL) {
        return;
    }
    ASSERT_EQ_INT(set_one(store, userdb, "remember.search.candidate_pool", "30", SETTING_LAYER_USER,
                          NULL, &wr, err, sizeof(err)),
                  SETTING_OK);
    ASSERT_EQ_INT(set_one(store, userdb, "remember.search.candidate_pool", "400",
                          SETTING_LAYER_BRAIN, NULL, &wr, err, sizeof(err)),
                  SETTING_OK);
    ASSERT_TRUE(snprintf(sql, sizeof(sql),
                         "UPDATE settings SET updated_at = '%s' WHERE key = "
                         "'remember.search.candidate_pool';",
                         k_past) > 0);
    exec_fixture(userdb, sql);
    exec_fixture(brain, sql);
    store_close(store);
    human = get_at(brain, userdb, args, sizeof(args) / sizeof(args[0]));
    scoped = get_at(brain, userdb, scope_args, sizeof(scope_args) / sizeof(scope_args[0]));
    ASSERT_EQ_INT(human.exit_code, 0);
    ASSERT_STREQ(human.out, "remember.search.candidate_pool = 400\n"
                            "  from brain, set by human on 2 Jan 2020 at 03:04\n"
                            "  user: 30 · default: 200\n");
    ASSERT_EQ_INT(scoped.exit_code, 0);
    ASSERT_STREQ(scoped.out, "remember.search.candidate_pool = 400\n"
                             "  from brain, set by human on 2 Jan 2020 at 03:04\n");
    cmd_result_free(&human);
    cmd_result_free(&scoped);
    {
        const char *scope_json[] = {"--json",  "settings", "get",
                                    "--scope", "brain",    "remember.search.candidate_pool"};
        CmdResult brain_json =
            get_at(brain, userdb, scope_json, sizeof(scope_json) / sizeof(scope_json[0]));

        ASSERT_EQ_INT(brain_json.exit_code, 0);
        ASSERT_STREQ(brain_json.err, "");
        ASSERT_STREQ(brain_json.out,
                     "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.get."
                     "result\",\"data\":{\"setting\":{\"key\":\"remember.search.candidate_pool\","
                     "\"type\":\"integer\",\"value\":400,\"from\":\"brain\",\"changed\":true,"
                     "\"layers\":{\"brain\":{\"value\":400,\"updated_by\":{\"kind\":\"human\","
                     "\"id\":null},\"updated_at\":\"2020-01-02T03:04:00.000Z\"}}}}}\n");
        cmd_result_free(&brain_json);
    }
    harness_remove_tree(tmpl);
}

TEST(get_scope_on_an_empty_layer_prints_unset)
{
    char tmpl[] = "/tmp/remember-get-empty-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *user_args[] = {"settings", "get", "remember.buckets.prefixes", "--scope", "user"};
    const char *brain_args[] = {"--json",  "settings", "get",
                                "--scope", "brain",    "remember.plugins.chain_budget"};
    CmdResult human;
    CmdResult json;

    if (store == NULL) {
        return;
    }
    store_close(store);
    human = get_at(brain, userdb, user_args, sizeof(user_args) / sizeof(user_args[0]));
    json = get_at(brain, userdb, brain_args, sizeof(brain_args) / sizeof(brain_args[0]));
    ASSERT_EQ_INT(human.exit_code, 0);
    ASSERT_STREQ(human.err, "");
    ASSERT_STREQ(human.out, "(unset)\n");
    cmd_result_free(&human);
    {
        const char *value_args[] = {"settings", "get",  "--value",
                                    "--scope",  "user", "remember.buckets.prefixes"};
        CmdResult valued =
            get_at(brain, userdb, value_args, sizeof(value_args) / sizeof(value_args[0]));

        ASSERT_EQ_INT(valued.exit_code, 0);
        ASSERT_STREQ(valued.err, "");
        ASSERT_STREQ(valued.out, "(unset)\n");
        cmd_result_free(&valued);
    }
    ASSERT_EQ_INT(json.exit_code, 0);
    ASSERT_STREQ(json.err, "");
    ASSERT_STREQ(json.out,
                 "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.get.result\","
                 "\"data\":{\"setting\":{\"key\":\"remember.plugins.chain_budget\","
                 "\"type\":\"duration\",\"value\":null,\"from\":\"brain\",\"changed\":false,"
                 "\"layers\":{\"brain\":null}}}}\n");
    cmd_result_free(&json);
    harness_remove_tree(tmpl);
}

TEST(get_tombstone_is_not_a_value)
{
    char tmpl[] = "/tmp/remember-get-tomb-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[256];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    SettingWriteResult wr;
    const char *args[] = {"settings", "get", "remember.search.candidate_pool"};
    CmdResult human;

    if (store == NULL) {
        return;
    }
    ASSERT_EQ_INT(set_one(store, userdb, "remember.search.candidate_pool", "400",
                          SETTING_LAYER_BRAIN, NULL, &wr, err, sizeof(err)),
                  SETTING_OK);
    ASSERT_EQ_INT(store_setting_reset(store, userdb, "remember.search.candidate_pool",
                                      SETTING_LAYER_BRAIN, NULL, &wr, err, sizeof(err)),
                  SETTING_OK);
    store_close(store);
    human = get_at(brain, userdb, args, sizeof(args) / sizeof(args[0]));
    ASSERT_EQ_INT(human.exit_code, 0);
    ASSERT_STREQ(human.out, "remember.search.candidate_pool = 200\n"
                            "  from default\n"
                            "  user: (unset) · brain: (unset)\n");
    cmd_result_free(&human);
    harness_remove_tree(tmpl);
}

TEST(get_value_is_one_line_and_json_stays_the_object)
{
    char tmpl[] = "/tmp/remember-get-val-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *plain[] = {"settings", "get", "--value", "remember.search.candidate_pool"};
    const char *full[] = {"--json", "settings", "get", "remember.search.candidate_pool"};
    const char *valued[] = {"--json", "settings", "get", "--value",
                            "remember.search.candidate_pool"};
    CmdResult human;
    CmdResult json;
    CmdResult same;

    if (store == NULL) {
        return;
    }
    store_close(store);
    human = get_at(brain, userdb, plain, sizeof(plain) / sizeof(plain[0]));
    json = get_at(brain, userdb, full, sizeof(full) / sizeof(full[0]));
    same = get_at(brain, userdb, valued, sizeof(valued) / sizeof(valued[0]));
    ASSERT_EQ_INT(human.exit_code, 0);
    ASSERT_STREQ(human.out, "200\n");
    ASSERT_STREQ(human.err, "");
    ASSERT_EQ_INT(json.exit_code, 0);
    ASSERT_STREQ(same.out, json.out);
    ASSERT_STREQ(same.err, "");
    cmd_result_free(&human);
    cmd_result_free(&json);
    cmd_result_free(&same);
    harness_remove_tree(tmpl);
}

TEST(get_names_an_actor_id)
{
    char tmpl[] = "/tmp/remember-get-actor-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[256];
    char sql[320];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    SettingWriteResult wr;
    const char *args[] = {"settings", "get", "remember.search.candidate_pool"};
    CmdResult human;

    if (store == NULL) {
        return;
    }
    ASSERT_EQ_INT(set_one(store, userdb, "remember.search.candidate_pool", "400",
                          SETTING_LAYER_BRAIN, k_agent, &wr, err, sizeof(err)),
                  SETTING_OK);
    ASSERT_TRUE(snprintf(sql, sizeof(sql),
                         "UPDATE settings SET updated_at = '%s' WHERE key = "
                         "'remember.search.candidate_pool';",
                         k_past) > 0);
    exec_fixture(brain, sql);
    store_close(store);
    human = get_at(brain, userdb, args, sizeof(args) / sizeof(args[0]));
    ASSERT_EQ_INT(human.exit_code, 0);
    ASSERT_STREQ(human.out, "remember.search.candidate_pool = 400\n"
                            "  from brain, set by agent \"bot1\" on 2 Jan 2020 at 03:04\n"
                            "  user: (unset) · default: 200\n");
    cmd_result_free(&human);
    harness_remove_tree(tmpl);
}

TEST(get_omits_a_stamp_that_is_not_canonical)
{
    char tmpl[] = "/tmp/remember-get-bad-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[256];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    SettingWriteResult wr;
    const char *args[] = {"settings", "get", "remember.search.max_results"};
    const char *pool[] = {"--json", "settings", "get", "remember.search.candidate_pool"};
    CmdResult human;
    CmdResult json;

    if (store == NULL) {
        return;
    }
    ASSERT_EQ_INT(set_one(store, userdb, "remember.search.max_results", "20", SETTING_LAYER_USER,
                          NULL, &wr, err, sizeof(err)),
                  SETTING_OK);
    ASSERT_EQ_INT(set_one(store, userdb, "remember.search.candidate_pool", "400",
                          SETTING_LAYER_BRAIN, NULL, &wr, err, sizeof(err)),
                  SETTING_OK);
    exec_fixture(userdb, "UPDATE settings SET updated_at = 'yesterday' WHERE key = "
                         "'remember.search.max_results';");
    exec_fixture(brain, "UPDATE settings SET updated_by = 'nope' WHERE key = "
                        "'remember.search.candidate_pool';");
    store_close(store);
    human = get_at(brain, userdb, args, sizeof(args) / sizeof(args[0]));
    json = get_at(brain, userdb, pool, sizeof(pool) / sizeof(pool[0]));
    ASSERT_EQ_INT(human.exit_code, 0);
    ASSERT_STREQ(human.out, "remember.search.max_results = 20\n"
                            "  from user, set by human\n"
                            "  brain: (unset) · default: 20\n");
    ASSERT_EQ_INT(json.exit_code, 0);
    ASSERT_STREQ(json.out,
                 "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.get.result\","
                 "\"data\":{\"setting\":{\"key\":\"remember.search.candidate_pool\","
                 "\"type\":\"integer\",\"value\":400,\"from\":\"brain\",\"changed\":true,"
                 "\"layers\":{\"default\":{\"value\":200},\"user\":null,\"brain\":{\"value\":"
                 "400}}}}}\n");
    cmd_result_free(&human);
    cmd_result_free(&json);
    harness_remove_tree(tmpl);
}

TEST(get_unknown_offers_one_key)
{
    char tmpl[] = "/tmp/remember-get-unk-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *short_args[] = {"settings", "get", "candidate_pool"};
    const char *slash_args[] = {"--json", "settings", "get", "remember:search/candidate_pool"};
    CmdResult human;
    CmdResult json;

    if (store == NULL) {
        return;
    }
    store_close(store);
    human = get_at(brain, userdb, short_args, sizeof(short_args) / sizeof(short_args[0]));
    json = get_at(brain, userdb, slash_args, sizeof(slash_args) / sizeof(slash_args[0]));
    ASSERT_EQ_INT(human.exit_code, 2);
    ASSERT_STREQ(human.out, "");
    ASSERT_STREQ(human.err, "remember: unknown setting \"candidate_pool\". Did you mean "
                            "\"remember.search.candidate_pool\"? Run 'remember settings list' to "
                            "see all settings.\n");
    ASSERT_EQ_INT(json.exit_code, 2);
    ASSERT_STREQ(json.err, "");
    ASSERT_STREQ(json.out,
                 "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.get.result\","
                 "\"error\":{\"code\":\"setting_unknown\",\"message\":\"unknown setting "
                 "\\\"remember:search/candidate_pool\\\". Did you mean "
                 "\\\"remember.search.candidate_pool\\\"?\",\"fix\":\"Run 'remember settings list' "
                 "to see all settings.\",\"retryable\":false}}\n");
    cmd_result_free(&human);
    cmd_result_free(&json);
    harness_remove_tree(tmpl);
}

TEST(get_unknown_without_one_candidate_uses_the_list_fix)
{
    char tmpl[] = "/tmp/remember-get-c03-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *args[] = {"settings", "get", "remember.search.hybrid"};
    const char *far[] = {"settings", "get", "remember.search.max_resXXXX"};
    CmdResult human;
    CmdResult none;

    if (store == NULL) {
        return;
    }
    store_close(store);
    human = get_at(brain, userdb, args, sizeof(args) / sizeof(args[0]));
    none = get_at(brain, userdb, far, sizeof(far) / sizeof(far[0]));
    ASSERT_EQ_INT(human.exit_code, 2);
    ASSERT_STREQ(human.err, "remember: unknown setting \"remember.search.hybrid\". Run 'remember "
                            "settings list' to see all settings.\n");
    ASSERT_EQ_INT(none.exit_code, 2);
    ASSERT_STREQ(none.err, "remember: unknown setting \"remember.search.max_resXXXX\". Run "
                           "'remember settings list' to see all settings.\n");
    ASSERT_STR_NOT_CONTAINS(human.err, "Did you mean");
    ASSERT_STR_NOT_CONTAINS(none.err, "Did you mean");
    cmd_result_free(&human);
    cmd_result_free(&none);
    harness_remove_tree(tmpl);
}

TEST(get_plugin_budget_uses_the_manifest_fix)
{
    char tmpl[] = "/tmp/remember-get-plug-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *budget[] = {"settings", "get", "plugin.foo.budget"};
    const char *grace[] = {"--json", "settings", "get", "plugin.foo.cold_start_grace"};
    CmdResult human;
    CmdResult json;

    if (store == NULL) {
        return;
    }
    store_close(store);
    human = get_at(brain, userdb, budget, sizeof(budget) / sizeof(budget[0]));
    json = get_at(brain, userdb, grace, sizeof(grace) / sizeof(grace[0]));
    ASSERT_EQ_INT(human.exit_code, 2);
    ASSERT_STREQ(human.err, "remember: unknown setting \"plugin.foo.budget\". Budgets and "
                            "cold-start grace are set by the plugin's manifest. Raise "
                            "remember.plugins.chain_budget instead.\n");
    ASSERT_EQ_INT(json.exit_code, 2);
    ASSERT_STREQ(json.err, "");
    ASSERT_STR_CONTAINS(json.out, "\"code\":\"setting_unknown\"");
    ASSERT_STR_CONTAINS(json.out,
                        "\"message\":\"unknown setting \\\"plugin.foo.cold_start_grace\\\".\"");
    ASSERT_STR_CONTAINS(json.out, "Raise remember.plugins.chain_budget instead.");
    cmd_result_free(&human);
    cmd_result_free(&json);
    harness_remove_tree(tmpl);
}

TEST(get_usage_rejects_a_bad_invocation)
{
    char tmpl[] = "/tmp/remember-get-use-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *bare[] = {"settings"};
    const char *missing[] = {"settings", "get"};
    const char *two[] = {"settings", "get", "remember.search.candidate_pool", "extra"};
    const char *bogus[] = {"--json", "settings", "get", "--bogus"};
    const char *scope[] = {"settings", "get", "--scope", "all", "remember.search.candidate_pool"};
    const char *dup[] = {"settings", "get", "--value", "--value", "remember.search.candidate_pool"};
    const char *noscope[] = {"settings", "get", "--scope"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    got = get_at(brain, userdb, bare, sizeof(bare) / sizeof(bare[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: settings takes 'get', 'set', or 'reset'\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, missing, sizeof(missing) / sizeof(missing[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: settings get requires a key\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, two, sizeof(two) / sizeof(two[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: settings get takes one key\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, bogus, sizeof(bogus) / sizeof(bogus[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "");
    ASSERT_STR_CONTAINS(got.out, "\"type\":\"setting.get.result\"");
    ASSERT_STR_CONTAINS(got.out, "\"message\":\"unknown option '--bogus'\"");
    cmd_result_free(&got);
    got = get_at(brain, userdb, scope, sizeof(scope) / sizeof(scope[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: invalid --scope (use brain or user)\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, dup, sizeof(dup) / sizeof(dup[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: duplicate --value\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, noscope, sizeof(noscope) / sizeof(noscope[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: missing value for --scope\n");
    cmd_result_free(&got);
    {
        const char *dupscope[] = {"settings",
                                  "get",
                                  "--scope",
                                  "user",
                                  "--scope",
                                  "user",
                                  "remember.search.candidate_pool"};
        const char *equals[] = {"settings", "get", "--scope=user",
                                "remember.search.candidate_pool"};
        const char *end[] = {"settings", "get", "--", "--value"};

        got = get_at(brain, userdb, dupscope, sizeof(dupscope) / sizeof(dupscope[0]));
        ASSERT_EQ_INT(got.exit_code, 1);
        ASSERT_STREQ(got.err, "remember: duplicate --scope\n");
        cmd_result_free(&got);
        got = get_at(brain, userdb, equals, sizeof(equals) / sizeof(equals[0]));
        ASSERT_EQ_INT(got.exit_code, 1);
        ASSERT_STREQ(got.err, "remember: unknown option '--scope=user'\n");
        cmd_result_free(&got);
        got = get_at(brain, userdb, end, sizeof(end) / sizeof(end[0]));
        ASSERT_EQ_INT(got.exit_code, 2);
        ASSERT_STREQ(got.err, "remember: unknown setting \"--value\". Run 'remember settings list' "
                              "to see all settings.\n");
        ASSERT_STR_NOT_CONTAINS(got.err, "Did you mean");
        cmd_result_free(&got);
    }
    harness_remove_tree(tmpl);
}

TEST(get_help_names_the_command)
{
    char tmpl[] = "/tmp/remember-get-help-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *args[] = {"settings", "--help"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    got = get_at(brain, userdb, args, sizeof(args) / sizeof(args[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STREQ(got.err, "");
    ASSERT_STR_CONTAINS(got.out, "Show or change a setting");
    ASSERT_STR_CONTAINS(got.out, "remember settings get KEY");
    ASSERT_STR_CONTAINS(got.out, "remember settings set KEY");
    ASSERT_STR_CONTAINS(got.out, "remember settings reset");
    ASSERT_STR_CONTAINS(got.out, "Without --scope, set and reset write the user layer");
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

TEST(get_stamp_that_does_not_fit_is_a_store_error)
{
    char tmpl[] = "/tmp/remember-get-fit-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[256];
    char by[97];
    char sql[256];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    SettingWriteResult wr;
    const char *user_args[] = {"settings", "get", "remember.search.max_results"};
    const char *brain_args[] = {"settings", "get", "remember.search.candidate_pool"};
    CmdResult user;
    CmdResult brain_got;

    if (store == NULL) {
        return;
    }
    memset(by, 'a', sizeof(by) - 1U);
    by[sizeof(by) - 1U] = '\0';
    ASSERT_EQ_INT(set_one(store, userdb, "remember.search.max_results", "20", SETTING_LAYER_USER,
                          NULL, &wr, err, sizeof(err)),
                  SETTING_OK);
    ASSERT_EQ_INT(set_one(store, userdb, "remember.search.candidate_pool", "400",
                          SETTING_LAYER_BRAIN, NULL, &wr, err, sizeof(err)),
                  SETTING_OK);
    ASSERT_TRUE(snprintf(sql, sizeof(sql),
                         "UPDATE settings SET updated_by = '%s' WHERE key = "
                         "'remember.search.max_results';",
                         by) > 0);
    exec_fixture(userdb, sql);
    ASSERT_TRUE(snprintf(sql, sizeof(sql),
                         "UPDATE settings SET updated_by = '%s' WHERE key = "
                         "'remember.search.candidate_pool';",
                         by) > 0);
    exec_fixture(brain, sql);
    store_close(store);
    user = get_at(brain, userdb, user_args, sizeof(user_args) / sizeof(user_args[0]));
    brain_got = get_at(brain, userdb, brain_args, sizeof(brain_args) / sizeof(brain_args[0]));
    ASSERT_EQ_INT(user.exit_code, 1);
    ASSERT_STREQ(user.out, "");
    ASSERT_STREQ(user.err, "remember: internal error\n");
    ASSERT_EQ_INT(brain_got.exit_code, 1);
    ASSERT_STREQ(brain_got.err, "remember: internal error\n");
    cmd_result_free(&user);
    cmd_result_free(&brain_got);
    harness_remove_tree(tmpl);
}

TEST(get_reports_home_only_after_the_key_is_known)
{
    char tmpl[] = "/tmp/remember-get-home-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char dir[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    EnvHold hold;
    const char *known[] = {"settings", "get", "remember.search.candidate_pool"};
    const char *unknown[] = {"settings", "get", "remember.search.hybrid"};
    const char *dir_args[] = {"--json", "settings", "get", "remember.search.candidate_pool"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    env_hold(&hold);
    ASSERT_EQ_INT(unsetenv("HOME"), 0);
    ASSERT_EQ_INT(unsetenv("XDG_DATA_HOME"), 0);
    ASSERT_EQ_INT(unsetenv("REMEMBER_USER_DB"), 0);
    got = run_remember(brain, unknown, sizeof(unknown) / sizeof(unknown[0]), NULL);
    ASSERT_EQ_INT(got.exit_code, 2);
    ASSERT_STR_CONTAINS(got.err, "unknown setting \"remember.search.hybrid\"");
    ASSERT_STR_NOT_CONTAINS(got.err, "HOME");
    cmd_result_free(&got);
    got = run_remember(brain, known, sizeof(known) / sizeof(known[0]), NULL);
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: HOME is not set\n");
    cmd_result_free(&got);
    env_release(&hold);
    ASSERT_TRUE(snprintf(dir, sizeof(dir), "%s/not-a-file", tmpl) > 0);
    ASSERT_EQ_INT(mkdir(dir, 0700), 0);
    env_hold(&hold);
    ASSERT_EQ_INT(setenv("REMEMBER_USER_DB", dir, 1), 0);
    got = run_remember(brain, dir_args, sizeof(dir_args) / sizeof(dir_args[0]), NULL);
    env_release(&hold);
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "");
    ASSERT_STR_CONTAINS(got.out, "\"code\":\"database\"");
    ASSERT_STR_CONTAINS(got.out, "cannot open database");
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

TEST(get_rejects_a_value_that_is_not_json)
{
    char tmpl[] = "/tmp/remember-get-json-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[256];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    SettingWriteResult wr;
    const char *args[] = {"settings", "get", "remember.search.candidate_pool"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    ASSERT_EQ_INT(set_one(store, userdb, "remember.search.candidate_pool", "400",
                          SETTING_LAYER_BRAIN, NULL, &wr, err, sizeof(err)),
                  SETTING_OK);
    exec_fixture(brain, "UPDATE settings SET value = 'nope' WHERE key = "
                        "'remember.search.candidate_pool';");
    store_close(store);
    got = get_at(brain, userdb, args, sizeof(args) / sizeof(args[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.out, "");
    ASSERT_STREQ(got.err, "remember: internal error\n");
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

/* The columns are NOT NULL, so this is the hand edit the schema does allow. */
TEST(get_rejects_a_nul_or_overlong_stamp)
{
    char tmpl[] = "/tmp/remember-get-nul-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[256];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    SettingWriteResult wr;
    const char *args[] = {"settings", "get", "remember.search.max_results"};
    const char *sqls[] = {
        "UPDATE settings SET updated_by = 'ab' || char(0) || 'c', "
        "updated_at = '2020-01-02T03:04:00.000Z' WHERE key = 'remember.search.max_results';",
        "UPDATE settings SET updated_by = '{\"kind\":\"human\",\"id\":null}', "
        "updated_at = '2020-01-02T03:04:00.000Z' || char(0) WHERE key = "
        "'remember.search.max_results';",
        "UPDATE settings SET updated_by = '{\"kind\":\"human\",\"id\":null}', "
        "updated_at = '2020-01-02T03:04:00.000Zxxxxxxxx' WHERE key = "
        "'remember.search.max_results';",
    };
    size_t i = 0;

    if (store == NULL) {
        return;
    }
    ASSERT_EQ_INT(set_one(store, userdb, "remember.search.max_results", "20", SETTING_LAYER_USER,
                          NULL, &wr, err, sizeof(err)),
                  SETTING_OK);
    store_close(store);
    for (i = 0; i < sizeof(sqls) / sizeof(sqls[0]); i++) {
        CmdResult got;

        exec_fixture(userdb, sqls[i]);
        got = get_at(brain, userdb, args, sizeof(args) / sizeof(args[0]));
        ASSERT_EQ_INT(got.exit_code, 1);
        ASSERT_STREQ(got.out, "");
        ASSERT_STREQ(got.err, "remember: internal error\n");
        cmd_result_free(&got);
    }
    harness_remove_tree(tmpl);
}

/* 1900 is not a leap year and 2000 is. A shape-valid 31 Feb must not print a date. */
TEST(get_shows_a_leap_day_and_omits_an_impossible_date)
{
    char tmpl[] = "/tmp/remember-get-cal-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[256];
    char sql[160];
    char expect[192];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    SettingWriteResult wr;
    const char *args[] = {"settings", "get", "remember.search.max_results"};
    const char *json_args[] = {"--json", "settings", "get", "remember.search.max_results"};
    struct {
        const char *stamp;
        const char *date;
    } cases[] = {
        {"2026-02-31T03:04:00.000Z", NULL},
        {"2026-02-29T03:04:00.000Z", NULL},
        {"2026-04-31T03:04:00.000Z", NULL},
        {"1900-02-29T03:04:00.000Z", NULL},
        {"2024-02-29T03:04:00.000Z", "29 Feb 2024 at 03:04"},
        {"2000-02-29T03:04:00.000Z", "29 Feb 2000 at 03:04"},
    };
    size_t i = 0;

    if (store == NULL) {
        return;
    }
    ASSERT_EQ_INT(set_one(store, userdb, "remember.search.max_results", "20", SETTING_LAYER_USER,
                          NULL, &wr, err, sizeof(err)),
                  SETTING_OK);
    store_close(store);
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CmdResult human;
        CmdResult json;

        ASSERT_TRUE(snprintf(sql, sizeof(sql),
                             "UPDATE settings SET updated_at = '%s' WHERE key = "
                             "'remember.search.max_results';",
                             cases[i].stamp) > 0);
        exec_fixture(userdb, sql);
        human = get_at(brain, userdb, args, sizeof(args) / sizeof(args[0]));
        json = get_at(brain, userdb, json_args, sizeof(json_args) / sizeof(json_args[0]));
        if (cases[i].date == NULL) {
            ASSERT_TRUE(snprintf(expect, sizeof(expect),
                                 "remember.search.max_results = 20\n"
                                 "  from user, set by human\n"
                                 "  brain: (unset) · default: 20\n") > 0);
        } else {
            ASSERT_TRUE(snprintf(expect, sizeof(expect),
                                 "remember.search.max_results = 20\n"
                                 "  from user, set by human on %s\n"
                                 "  brain: (unset) · default: 20\n",
                                 cases[i].date) > 0);
        }
        ASSERT_EQ_INT(human.exit_code, 0);
        ASSERT_STREQ(human.err, "");
        ASSERT_STREQ(human.out, expect);
        ASSERT_EQ_INT(json.exit_code, 0);
        ASSERT_STREQ(json.err, "");
        ASSERT_STR_CONTAINS(json.out, "\"updated_by\":{\"kind\":\"human\",\"id\":null}");
        if (cases[i].date == NULL) {
            ASSERT_STR_NOT_CONTAINS(json.out, "updated_at");
            ASSERT_STR_NOT_CONTAINS(human.out, " on ");
        } else {
            ASSERT_STR_CONTAINS(json.out, cases[i].stamp);
        }
        cmd_result_free(&human);
        cmd_result_free(&json);
    }
    harness_remove_tree(tmpl);
}

TEST(get_credential_helper_omits_a_disallowed_brain_row)
{
    char tmpl[] = "/tmp/remember-get-cred-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *args[] = {"settings", "get", "remember.sync.credential_helper"};
    const char *json_args[] = {"--json", "settings", "get", "remember.sync.credential_helper"};
    CmdResult human;
    CmdResult json;

    if (store == NULL) {
        return;
    }
    exec_fixture(brain, "INSERT INTO settings (key, value, updated_by, updated_at, device_id, "
                        "version_vector, deleted_at, change_seq) VALUES "
                        "('remember.sync.credential_helper','\"evil\"',"
                        "'{\"kind\":\"human\",\"id\":null}','2030-01-01T00:00:00.000Z','peer',"
                        "'{}',NULL,1)");
    store_close(store);
    human = get_at(brain, userdb, args, sizeof(args) / sizeof(args[0]));
    json = get_at(brain, userdb, json_args, sizeof(json_args) / sizeof(json_args[0]));
    ASSERT_EQ_INT(human.exit_code, 0);
    ASSERT_STREQ(human.err, "");
    ASSERT_STREQ(human.out, "remember.sync.credential_helper = (unset)\n"
                            "  from default\n"
                            "  user: (unset)\n");
    ASSERT_STR_NOT_CONTAINS(human.out, "evil");
    ASSERT_EQ_INT(json.exit_code, 0);
    ASSERT_STREQ(json.err, "");
    ASSERT_STREQ(json.out,
                 "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.get.result\","
                 "\"data\":{\"setting\":{\"key\":\"remember.sync.credential_helper\","
                 "\"type\":\"string\",\"value\":null,\"from\":\"default\",\"changed\":false,"
                 "\"layers\":{\"default\":null,\"user\":null}}}}\n");
    ASSERT_STR_NOT_CONTAINS(json.out, "evil");
    ASSERT_STR_NOT_CONTAINS(json.out, "brain");
    cmd_result_free(&human);
    cmd_result_free(&json);
    harness_remove_tree(tmpl);
}

TEST(get_folds_a_hyphen_and_ends_options)
{
    char tmpl[] = "/tmp/remember-get-fold-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *fold[] = {"settings", "get", "remember.search.candidate-pool"};
    const char *end[] = {"settings", "get", "--", "remember.search.candidate_pool"};
    const char *value[] = {"settings", "get", "--value", "--", "remember.search.candidate_pool"};
    CmdResult folded;
    CmdResult stopped;
    CmdResult one;

    if (store == NULL) {
        return;
    }
    store_close(store);
    folded = get_at(brain, userdb, fold, sizeof(fold) / sizeof(fold[0]));
    stopped = get_at(brain, userdb, end, sizeof(end) / sizeof(end[0]));
    one = get_at(brain, userdb, value, sizeof(value) / sizeof(value[0]));
    ASSERT_EQ_INT(folded.exit_code, 0);
    ASSERT_STREQ(folded.err, "");
    ASSERT_STREQ(folded.out, "remember.search.candidate_pool = 200\n"
                             "  from default\n"
                             "  user: (unset) · brain: (unset)\n");
    ASSERT_STREQ(stopped.out, folded.out);
    ASSERT_EQ_INT(stopped.exit_code, 0);
    ASSERT_EQ_INT(one.exit_code, 0);
    ASSERT_STREQ(one.err, "");
    ASSERT_STREQ(one.out, "200\n");
    cmd_result_free(&folded);
    cmd_result_free(&stopped);
    cmd_result_free(&one);
    harness_remove_tree(tmpl);
}

TEST(get_quotes_the_typed_key)
{
    char tmpl[] = "/tmp/remember-get-quote-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char long_key[301];
    char expect[512];
    char nl_key[] = {'a', '\n', 'b', '\0'};
    char esc_key[] = {'a', '\033', 'b', '\0'};
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *long_args[] = {"settings", "get", long_key};
    const char *nl_args[] = {"settings", "get", nl_key};
    const char *esc_args[] = {"settings", "get", esc_key};
    const char *nl_json[] = {"--json", "settings", "get", nl_key};
    const char *esc_json[] = {"--json", "settings", "get", esc_key};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    memset(long_key, 'a', sizeof(long_key) - 1U);
    long_key[sizeof(long_key) - 1U] = '\0';
    ASSERT_TRUE(snprintf(expect, sizeof(expect),
                         "remember: unknown setting \"%s\". Run 'remember settings list' to see "
                         "all settings.\n",
                         long_key) > 0);
    got = get_at(brain, userdb, long_args, sizeof(long_args) / sizeof(long_args[0]));
    ASSERT_EQ_INT(got.exit_code, 2);
    ASSERT_STREQ(got.out, "");
    ASSERT_STREQ(got.err, expect);
    cmd_result_free(&got);
    got = get_at(brain, userdb, nl_args, sizeof(nl_args) / sizeof(nl_args[0]));
    ASSERT_EQ_INT(got.exit_code, 2);
    ASSERT_STREQ(got.err, "remember: unknown setting \"a?b\". Run 'remember settings list' to see "
                          "all settings.\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, esc_args, sizeof(esc_args) / sizeof(esc_args[0]));
    ASSERT_EQ_INT(got.exit_code, 2);
    ASSERT_STREQ(got.err, "remember: unknown setting \"a?b\". Run 'remember settings list' to see "
                          "all settings.\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, nl_json, sizeof(nl_json) / sizeof(nl_json[0]));
    ASSERT_EQ_INT(got.exit_code, 2);
    ASSERT_STREQ(got.err, "");
    ASSERT_STR_CONTAINS(got.out, "unknown setting \\\"a\\nb\\\".");
    ASSERT_EQ_INT((int)(strchr(got.out, '\n') - got.out), (int)strlen(got.out) - 1);
    cmd_result_free(&got);
    got = get_at(brain, userdb, esc_json, sizeof(esc_json) / sizeof(esc_json[0]));
    ASSERT_EQ_INT(got.exit_code, 2);
    ASSERT_STR_CONTAINS(got.out, "unknown setting \\\"a\\u001bb\\\".");
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

/* 007 example: a successful set prints nothing. ST-7.11b data is the setting
   after the write, plus scope, old_value, and changed. */
TEST(set_writes_the_user_layer_and_prints_nothing)
{
    char tmpl[] = "/tmp/remember-set-ok-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char expect[1024];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *args[] = {"settings", "set", "remember.search.candidate_pool", "400"};
    const char *json_args[] = {"--json", "settings", "set", "remember.search.max_results", "20"};
    CmdResult human;
    CmdResult json;
    char *stamp = NULL;

    if (store == NULL) {
        return;
    }
    store_close(store);
    human = get_at(brain, userdb, args, sizeof(args) / sizeof(args[0]));
    json = get_at(brain, userdb, json_args, sizeof(json_args) / sizeof(json_args[0]));
    ASSERT_EQ_INT(human.exit_code, 0);
    ASSERT_STREQ(human.err, "");
    ASSERT_STREQ(human.out, "");
    assert_query(userdb, "SELECT value FROM settings WHERE key = 'remember.search.candidate_pool'",
                 "400");
    assert_query(userdb, "SELECT COUNT(*) FROM settings_history", "2");
    stamp = q(userdb, "SELECT updated_at FROM settings WHERE key = 'remember.search.max_results'");
    ASSERT_TRUE(stamp != NULL);
    ASSERT_TRUE(snprintf(expect, sizeof(expect),
                         "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.set."
                         "result\",\"data\":{\"setting\":{\"key\":\"remember.search.max_results\","
                         "\"type\":\"integer\",\"value\":20,\"from\":\"user\",\"changed\":true,"
                         "\"layers\":{\"default\":{\"value\":20},\"user\":{\"value\":20,"
                         "\"updated_by\":{\"kind\":\"human\",\"id\":null},\"updated_at\":\"%s\"},"
                         "\"brain\":null}},\"scope\":\"user\",\"old_value\":null,\"changed\":"
                         "true}}\n",
                         stamp != NULL ? stamp : "") > 0);
    ASSERT_EQ_INT(json.exit_code, 0);
    ASSERT_STREQ(json.err, "");
    ASSERT_STREQ(json.out, expect);
    free(stamp);
    cmd_result_free(&human);
    cmd_result_free(&json);
    harness_remove_tree(tmpl);
}

TEST(set_same_value_writes_nothing_and_reports_the_old_value)
{
    char tmpl[] = "/tmp/remember-set-noop-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char expect[1024];
    char *stamp = NULL;
    char *again = NULL;
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *first[] = {"settings", "set", "remember.search.candidate_pool", "400"};
    const char *json_args[] = {"--json", "settings", "set", "remember.search.candidate_pool",
                               "400"};
    CmdResult human;
    CmdResult json;

    if (store == NULL) {
        return;
    }
    store_close(store);
    human = get_at(brain, userdb, first, sizeof(first) / sizeof(first[0]));
    ASSERT_EQ_INT(human.exit_code, 0);
    ASSERT_STREQ(human.out, "");
    cmd_result_free(&human);
    stamp =
        q(userdb, "SELECT updated_at FROM settings WHERE key = 'remember.search.candidate_pool'");
    json = get_at(brain, userdb, json_args, sizeof(json_args) / sizeof(json_args[0]));
    again =
        q(userdb, "SELECT updated_at FROM settings WHERE key = 'remember.search.candidate_pool'");
    ASSERT_EQ_INT(json.exit_code, 0);
    ASSERT_STREQ(json.err, "");
    ASSERT_STREQ(stamp, again);
    assert_query(userdb, "SELECT COUNT(*) FROM settings_history", "1");
    ASSERT_TRUE(snprintf(expect, sizeof(expect),
                         "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.set."
                         "result\",\"data\":{\"setting\":{\"key\":\"remember.search.candidate_"
                         "pool\",\"type\":\"integer\",\"value\":400,\"from\":\"user\",\"changed\":"
                         "true,\"layers\":{\"default\":{\"value\":200},\"user\":{\"value\":400,"
                         "\"updated_by\":{\"kind\":\"human\",\"id\":null},\"updated_at\":\"%s\"},"
                         "\"brain\":null}},\"scope\":\"user\",\"old_value\":400,\"changed\":"
                         "false}}\n",
                         stamp != NULL ? stamp : "") > 0);
    ASSERT_STREQ(json.out, expect);
    free(stamp);
    free(again);
    cmd_result_free(&json);
    harness_remove_tree(tmpl);
}

TEST(set_stores_the_actor)
{
    char tmpl[] = "/tmp/remember-set-actor-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *args[] = {"settings", "set", "remember.search.max_results", "20", "--as", "agent",
                          "--actor",  "bot1"};
    const char *human_only[] = {"settings", "set",     "remember.search.candidate_pool",
                                "400",      "--actor", "bot1"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    got = get_at(brain, userdb, args, sizeof(args) / sizeof(args[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STREQ(got.out, "");
    ASSERT_STREQ(got.err, "");
    assert_query(userdb,
                 "SELECT updated_by FROM settings WHERE key = 'remember.search.max_results'",
                 "{\"kind\":\"agent\",\"id\":\"bot1\"}");
    cmd_result_free(&got);
    got = get_at(brain, userdb, human_only, sizeof(human_only) / sizeof(human_only[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    assert_query(userdb,
                 "SELECT updated_by FROM settings WHERE key = 'remember.search.candidate_pool'",
                 "{\"kind\":\"human\",\"id\":\"bot1\"}");
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

TEST(set_without_scope_writes_the_only_allowed_layer)
{
    char tmpl[] = "/tmp/remember-set-brain-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char expect[1200];
    char *stamp = NULL;
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *prefixes[] = {"settings", "set", "remember.buckets.prefixes", "project:", "extra:"};
    const char *json_args[] = {
        "--json", "settings", "set", "--scope", "brain", "remember.search.candidate_pool", "400"};
    CmdResult human;
    CmdResult json;

    if (store == NULL) {
        return;
    }
    store_close(store);
    human = get_at(brain, userdb, prefixes, sizeof(prefixes) / sizeof(prefixes[0]));
    ASSERT_EQ_INT(human.exit_code, 0);
    ASSERT_STREQ(human.out, "");
    assert_query(brain, "SELECT value FROM settings WHERE key = 'remember.buckets.prefixes'",
                 "[\"project:\",\"extra:\"]");
    ASSERT_TRUE(access(userdb, F_OK) != 0);
    cmd_result_free(&human);
    json = get_at(brain, userdb, json_args, sizeof(json_args) / sizeof(json_args[0]));
    stamp =
        q(brain, "SELECT updated_at FROM settings WHERE key = 'remember.search.candidate_pool'");
    ASSERT_TRUE(stamp != NULL);
    ASSERT_TRUE(
        snprintf(expect, sizeof(expect),
                 "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.set."
                 "result\",\"data\":{\"setting\":{\"key\":\"remember.search.candidate_pool\","
                 "\"type\":\"integer\",\"value\":400,\"from\":\"brain\",\"changed\":true,"
                 "\"layers\":{\"default\":{\"value\":200},\"user\":null,\"brain\":{\"value\":"
                 "400,\"updated_by\":{\"kind\":\"human\",\"id\":null},\"updated_at\":\"%s\"}}},"
                 "\"scope\":\"brain\",\"old_value\":null,\"changed\":true}}\n",
                 stamp != NULL ? stamp : "") > 0);
    ASSERT_EQ_INT(json.exit_code, 0);
    ASSERT_STREQ(json.out, expect);
    ASSERT_TRUE(access(userdb, F_OK) != 0);
    free(stamp);
    cmd_result_free(&json);
    harness_remove_tree(tmpl);
}

TEST(set_clear_writes_an_empty_prefix_list)
{
    char tmpl[] = "/tmp/remember-set-clear-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *args[] = {"settings", "set", "remember.buckets.prefixes", "--clear"};
    const char *bad[] = {"--json", "settings", "set", "remember.search.max_results", "--clear"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    got = get_at(brain, userdb, args, sizeof(args) / sizeof(args[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STREQ(got.out, "");
    assert_query(brain, "SELECT value FROM settings WHERE key = 'remember.buckets.prefixes'", "[]");
    cmd_result_free(&got);
    got = get_at(brain, userdb, bad, sizeof(bad) / sizeof(bad[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "");
    ASSERT_STREQ(got.out,
                 "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.set."
                 "result\",\"error\":{\"code\":\"setting_invalid_value\",\"message\":\"\\\""
                 "remember.search.max_results\\\" isn't a list, so --clear can't empty "
                 "it.\",\"fix\":\"Set a value, or run 'remember settings reset "
                 "remember.search.max_results'.\",\"retryable\":false}}\n");
    ASSERT_TRUE(access(userdb, F_OK) != 0);
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

TEST(set_invalid_value_names_the_kind)
{
    char tmpl[] = "/tmp/remember-set-bad-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *whole[] = {"settings", "set", "remember.search.candidate_pool", "nope"};
    const char *zero[] = {"settings", "set", "remember.search.candidate_pool", "020"};
    const char *huge[] = {"settings", "set", "remember.search.max_results", "99999999999999999999"};
    const char *unit[] = {"settings", "set", "remember.plugins.chain_budget", "45"};
    const char *sub[] = {"settings", "set", "remember.plugins.chain_budget", "0.5ms"};
    const char *helper[] = {"settings", "set", "remember.sync.credential_helper", "Nope"};
    const char *two[] = {"settings", "set", "remember.search.candidate_pool", "20", "21"};
    const char *ended[] = {"settings", "set", "remember.search.candidate_pool", "--", "--clear"};
    const char *cleared[] = {"settings", "set", "remember.search.candidate_pool", "--clear"};
    const char *cleared_json[] = {"--json", "settings", "set", "remember.search.candidate_pool",
                                  "--clear"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    got = get_at(brain, userdb, whole, sizeof(whole) / sizeof(whole[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: \"remember.search.candidate_pool\" needs a whole number; you "
                          "gave \"nope\". Write it in plain digits, like 200, with no sign, "
                          "leading zero, \"_\", or exponent.\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, zero, sizeof(zero) / sizeof(zero[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STR_CONTAINS(got.err, "you gave \"020\".");
    cmd_result_free(&got);
    got = get_at(brain, userdb, huge, sizeof(huge) / sizeof(huge[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STR_CONTAINS(got.err, "needs a whole number");
    ASSERT_STR_NOT_CONTAINS(got.err, "can be");
    cmd_result_free(&got);
    got = get_at(brain, userdb, unit, sizeof(unit) / sizeof(unit[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: \"remember.plugins.chain_budget\" needs a duration with a "
                          "unit; you gave \"45\". Use a number with ms, s, m, or h, like 800ms or "
                          "2s.\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, sub, sizeof(sub) / sizeof(sub[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err,
                 "remember: \"remember.plugins.chain_budget\" can't be \"0.5ms\": a duration must "
                 "be a whole number of milliseconds. Use a whole number of milliseconds, like "
                 "800ms or 1.5s.\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, helper, sizeof(helper) / sizeof(helper[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: \"Nope\" isn't a helper id. Use a lowercase name that starts "
                          "with a letter, up to 40 letters, digits, \"_\" or \"-\", like "
                          "\"keychain\".\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, two, sizeof(two) / sizeof(two[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: \"remember.search.candidate_pool\" takes one value; you gave "
                          "2. Give one value.\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, cleared, sizeof(cleared) / sizeof(cleared[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: \"remember.search.candidate_pool\" isn't a list, so --clear "
                          "can't empty it. Set a value, or run 'remember settings reset "
                          "remember.search.candidate_pool'.\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, cleared_json, sizeof(cleared_json) / sizeof(cleared_json[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.out,
                 "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.set.result\","
                 "\"error\":{\"code\":\"setting_invalid_value\",\"message\":\"\\\"remember.search."
                 "candidate_pool\\\" isn't a list, so --clear can't empty it.\",\"fix\":\"Set a "
                 "value, or run 'remember settings reset remember.search.candidate_pool'.\","
                 "\"retryable\":false}}\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, ended, sizeof(ended) / sizeof(ended[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STR_CONTAINS(got.err, "you gave \"--clear\".");
    ASSERT_TRUE(access(userdb, F_OK) != 0);
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

TEST(set_prefix_errors_name_the_item)
{
    char tmpl[] = "/tmp/remember-set-pre-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *bad[] = {"--json", "settings", "set", "remember.buckets.prefixes", "nope"};
    const char *twice[] = {"settings", "set", "remember.buckets.prefixes", "project:", "project:"};
    const char *folded[] = {"--json",   "settings", "set", "remember.buckets.prefixes",
                            "project:", "Project:"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    got = get_at(brain, userdb, bad, sizeof(bad) / sizeof(bad[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.out,
                 "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.set.result\","
                 "\"error\":{\"code\":\"setting_invalid_value\",\"message\":\"\\\"nope\\\" isn't a "
                 "bucket prefix.\",\"fix\":\"Use 1 to 32 letters, digits, \\\"-\\\", \\\"_\\\" or "
                 "\\\".\\\", then one \\\":\\\", like \\\"project:\\\".\",\"retryable\":false,"
                 "\"detail\":{\"item\":\"nope\"}}}\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, twice, sizeof(twice) / sizeof(twice[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: \"project:\" is in the list twice. List each prefix once.\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, folded, sizeof(folded) / sizeof(folded[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.out,
                 "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.set.result\","
                 "\"error\":{\"code\":\"setting_invalid_value\",\"message\":\"\\\"Project:\\\" "
                 "and \\\"project:\\\" are the same prefix.\",\"fix\":\"List each prefix once.\","
                 "\"retryable\":false,\"detail\":{\"item\":\"Project:\",\"other\":\"project:\"}}"
                 "}\n");
    ASSERT_TRUE(access(userdb, F_OK) != 0);
    assert_query(brain, "SELECT COUNT(*) FROM settings", "0");
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

TEST(set_out_of_range_names_the_bounds)
{
    char tmpl[] = "/tmp/remember-set-range-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *low[] = {"settings", "set", "remember.search.candidate_pool", "19"};
    const char *zero[] = {"--json", "settings", "set", "remember.search.max_results", "0"};
    const char *high[] = {"settings", "set", "remember.plugins.chain_budget", "45s"};
    const char *idle[] = {"settings", "set", "remember.plugins.idle_timeout", "59s"};
    const char *ms[] = {"settings", "set", "remember.plugins.chain_budget", "99ms"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    got = get_at(brain, userdb, low, sizeof(low) / sizeof(low[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: \"remember.search.candidate_pool\" can be 20 to 2000; you "
                          "gave 19. Set a value from 20 to 2000.\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, zero, sizeof(zero) / sizeof(zero[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.out, "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.set."
                          "result\",\"error\":{\"code\":\"setting_out_of_range\",\"message\":\"\\"
                          "\"remember.search.max_results\\\" can be 1 to 1000; you gave 0.\","
                          "\"fix\":\"Set a value from 1 to 1000.\",\"retryable\":false,"
                          "\"detail\":{\"key\":\"remember.search.max_results\",\"value\":\"0\","
                          "\"min\":\"1\",\"max\":\"1000\"}}}\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, high, sizeof(high) / sizeof(high[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: \"remember.plugins.chain_budget\" can be 100ms to 30s; you "
                          "gave 45s. Set a value from 100ms to 30s.\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, idle, sizeof(idle) / sizeof(idle[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: \"remember.plugins.idle_timeout\" can be 1m to 24h; you gave "
                          "59s. Set a value from 1m to 24h.\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, ms, sizeof(ms) / sizeof(ms[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STR_CONTAINS(got.err, "can be 100ms to 30s; you gave 99ms.");
    ASSERT_STR_NOT_CONTAINS(got.err, "whole number of milliseconds");
    ASSERT_TRUE(access(userdb, F_OK) != 0);
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

TEST(set_scope_mismatch_names_the_layer)
{
    char tmpl[] = "/tmp/remember-set-scope-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *brain_key[] = {"--json",  "settings", "set",     "remember.buckets.prefixes",
                               "--scope", "user",     "project:"};
    const char *idle[] = {"settings", "set",     "remember.plugins.idle_timeout",
                          "10m",      "--scope", "brain"};
    const char *budget[] = {"--json", "settings", "set",  "remember.plugins.chain_budget",
                            "2s",     "--scope",  "brain"};
    const char *helper[] = {
        "settings", "set", "--scope", "brain", "remember.sync.credential_helper", "pass"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    got = get_at(brain, userdb, brain_key, sizeof(brain_key) / sizeof(brain_key[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.out,
                 "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.set.result\","
                 "\"error\":{\"code\":\"setting_scope_mismatch\",\"message\":\"\\\"remember."
                 "buckets.prefixes\\\" is a brain setting; it can't be set for your user.\","
                 "\"fix\":\"Run the command again with --scope brain, or without --scope.\","
                 "\"retryable\":false,\"detail\":{\"key\":\"remember.buckets.prefixes\","
                 "\"allowed\":\"brain\"}}}\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, idle, sizeof(idle) / sizeof(idle[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, k_idle_scope);
    cmd_result_free(&got);
    got = get_at(brain, userdb, budget, sizeof(budget) / sizeof(budget[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.out,
                 "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.set."
                 "result\",\"error\":{\"code\":\"setting_scope_mismatch\",\"message\":\"\\\""
                 "remember.plugins.chain_budget\\\" is a user setting; it can't be set for one "
                 "brain.\",\"fix\":\"Run the command again with --scope user, or without "
                 "--scope.\",\"retryable\":false,\"detail\":{\"key\":\"remember.plugins."
                 "chain_budget\",\"allowed\":\"user\"}}}\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, helper, sizeof(helper) / sizeof(helper[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: \"remember.sync.credential_helper\" is a user setting; it "
                          "can't be set for one brain. Run the command again with --scope user, "
                          "or without --scope.\n");
    ASSERT_TRUE(access(userdb, F_OK) != 0);
    assert_query(brain, "SELECT COUNT(*) FROM settings", "0");
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

TEST(set_value_over_sixteen_kib_is_quota)
{
    enum { PREFIX_OVER = 2341, HEAD = 4 };
    char tmpl[] = "/tmp/remember-set-kib-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    const char **items = NULL;
    const char **argv = NULL;
    char *block = NULL;
    size_t i = 0;
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    block = make_prefixes((size_t)PREFIX_OVER, &items);
    argv = (const char **)malloc(((size_t)PREFIX_OVER + (size_t)HEAD) * sizeof(*argv));
    if (block == NULL || argv == NULL) {
        free(block);
        free((void *)items);
        free((void *)argv);
        harness_remove_tree(tmpl);
        return;
    }
    argv[0] = "--json";
    argv[1] = "settings";
    argv[2] = "set";
    argv[3] = "remember.buckets.prefixes";
    for (i = 0; i < (size_t)PREFIX_OVER; i++) {
        argv[(size_t)HEAD + i] = items[i];
    }
    got = get_at(brain, userdb, (const char *const *)argv, (size_t)PREFIX_OVER + (size_t)HEAD);
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "");
    ASSERT_STREQ(got.out,
                 "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.set.result\","
                 "\"error\":{\"code\":\"quota_exceeded\",\"message\":\"the value for \\\"remember."
                 "buckets.prefixes\\\" is over 16 KiB, the most a setting can hold.\",\"fix\":"
                 "\"Use a shorter value.\",\"retryable\":false,\"detail\":{\"quota\":"
                 "\"settings_limit\"}}}\n");
    assert_query(brain, "SELECT COUNT(*) FROM settings", "0");
    ASSERT_TRUE(access(userdb, F_OK) != 0);
    free(block);
    free((void *)items);
    free((void *)argv);
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

TEST(set_user_quota_refuses_a_new_key_and_keeps_an_existing_one)
{
    char tmpl[] = "/tmp/remember-set-uquota-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *first[] = {"settings", "set", "remember.search.max_results", "20"};
    const char *again[] = {"settings", "set", "remember.search.max_results", "30"};
    const char *fresh[] = {"--json", "settings", "set", "remember.search.candidate_pool", "400"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    got = get_at(brain, userdb, first, sizeof(first) / sizeof(first[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    cmd_result_free(&got);
    insert_dummy_settings(userdb, false, SETTING_ROW_QUOTA - 1);
    got = get_at(brain, userdb, again, sizeof(again) / sizeof(again[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STREQ(got.out, "");
    cmd_result_free(&got);
    got = get_at(brain, userdb, fresh, sizeof(fresh) / sizeof(fresh[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.out,
                 "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.set.result\","
                 "\"error\":{\"code\":\"quota_exceeded\",\"message\":\"you have 256 user settings, "
                 "the most remember can hold.\",\"fix\":\"Reset settings you don't need with "
                 "'remember settings reset --scope user'.\",\"retryable\":false,\"detail\":{"
                 "\"quota\":\"settings_limit\"}}}\n");
    assert_query(userdb, "SELECT value FROM settings WHERE key = 'remember.search.max_results'",
                 "30");
    assert_query(userdb,
                 "SELECT COUNT(*) FROM settings WHERE key = 'remember.search.candidate_pool'", "0");
    assert_query(userdb, "SELECT COUNT(*) FROM settings", "256");
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

TEST(set_brain_quota_names_the_brain)
{
    char tmpl[] = "/tmp/remember-set-bquota-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *args[] = {
        "--json", "settings", "set", "--scope", "brain", "remember.search.candidate_pool", "400"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    insert_dummy_settings(brain, true, SETTING_ROW_QUOTA);
    got = get_at(brain, userdb, args, sizeof(args) / sizeof(args[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.out, "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.set."
                          "result\",\"error\":{\"code\":\"quota_exceeded\",\"message\":\"this "
                          "brain has 256 settings, the most it can hold.\",\"fix\":\"Reset "
                          "settings you don't need with 'remember settings reset'.\","
                          "\"retryable\":false,\"detail\":{\"quota\":\"settings_limit\"}}}\n");
    assert_query(brain,
                 "SELECT COUNT(*) FROM settings WHERE key = 'remember.search.candidate_pool'", "0");
    assert_query(brain, "SELECT change_seq FROM brain_meta", "0");
    ASSERT_TRUE(access(userdb, F_OK) != 0);
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

TEST(set_unknown_key_offers_one_suggestion)
{
    char tmpl[] = "/tmp/remember-set-unk-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *short_key[] = {"settings", "set", "candidate_pool", "400"};
    const char *plugin[] = {"settings", "set", "plugin.foo.budget", "1"};
    const char *folded[] = {"settings", "set", "remember.search.candidate-pool", "400"};
    const char *none[] = {"settings", "set", "remember.search.hybrid", "1"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    got = get_at(brain, userdb, short_key, sizeof(short_key) / sizeof(short_key[0]));
    ASSERT_EQ_INT(got.exit_code, 2);
    ASSERT_STREQ(got.err, "remember: unknown setting \"candidate_pool\". Did you mean "
                          "\"remember.search.candidate_pool\"? Run 'remember settings list' to "
                          "see all settings.\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, plugin, sizeof(plugin) / sizeof(plugin[0]));
    ASSERT_EQ_INT(got.exit_code, 2);
    ASSERT_STREQ(got.err, "remember: unknown setting \"plugin.foo.budget\". Budgets and "
                          "cold-start grace are set by the plugin's manifest. Raise "
                          "remember.plugins.chain_budget instead.\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, folded, sizeof(folded) / sizeof(folded[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STREQ(got.out, "");
    assert_query(userdb, "SELECT value FROM settings WHERE key = 'remember.search.candidate_pool'",
                 "400");
    cmd_result_free(&got);
    got = get_at(brain, userdb, none, sizeof(none) / sizeof(none[0]));
    ASSERT_EQ_INT(got.exit_code, 2);
    ASSERT_STR_NOT_CONTAINS(got.err, "Did you mean");
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

TEST(set_usage_rejects_a_bad_invocation)
{
    char tmpl[] = "/tmp/remember-set-use-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *bare[] = {"settings", "set"};
    const char *novalue[] = {"settings", "set", "remember.search.max_results"};
    const char *clear[] = {"settings", "set", "--clear"};
    const char *both[] = {"settings", "set", "remember.search.max_results", "--clear", "1"};
    const char *yes[] = {"settings", "set", "remember.search.max_results", "1", "--yes"};
    const char *equals[] = {"settings", "set", "--scope=user", "remember.search.max_results", "1"};
    const char *bad_scope[] = {"settings", "set", "--scope", "all", "remember.search.max_results",
                               "1"};
    const char *no_scope[] = {"settings", "set", "--scope"};
    const char *dup_clear[] = {"settings", "set", "remember.buckets.prefixes", "--clear",
                               "--clear"};
    const char *dup_as[] = {"settings", "set",  "remember.search.max_results", "1", "--as", "human",
                            "--as",     "agent"};
    const char *no_as[] = {"settings", "set", "remember.search.max_results", "1", "--as"};
    const char *bad_as[] = {"settings", "set", "remember.search.max_results", "1", "--as", "nope"};
    const char *bad_actor[] = {"settings", "set",     "remember.search.max_results",
                               "1",        "--actor", "bad id"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    got = get_at(brain, userdb, bare, sizeof(bare) / sizeof(bare[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: settings set requires a key\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, novalue, sizeof(novalue) / sizeof(novalue[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: settings set requires a value\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, clear, sizeof(clear) / sizeof(clear[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: settings set requires a key\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, both, sizeof(both) / sizeof(both[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: cannot combine a value with --clear\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, yes, sizeof(yes) / sizeof(yes[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: unknown option '--yes'\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, equals, sizeof(equals) / sizeof(equals[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: unknown option '--scope=user'\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, bad_scope, sizeof(bad_scope) / sizeof(bad_scope[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: invalid --scope (use brain or user)\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, no_scope, sizeof(no_scope) / sizeof(no_scope[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: missing value for --scope\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, dup_clear, sizeof(dup_clear) / sizeof(dup_clear[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: duplicate --clear\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, dup_as, sizeof(dup_as) / sizeof(dup_as[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: duplicate --as\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, no_as, sizeof(no_as) / sizeof(no_as[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: missing value for --as\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, bad_as, sizeof(bad_as) / sizeof(bad_as[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: invalid --as (use human, agent, tool, or share)\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, bad_actor, sizeof(bad_actor) / sizeof(bad_actor[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: invalid --actor (1 to 64 of A-Za-z0-9_./:-)\n");
    ASSERT_TRUE(access(userdb, F_OK) != 0);
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

TEST(a_hook_cannot_set_or_reset)
{
    char tmpl[] = "/tmp/remember-set-hook-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char saved[64];
    bool had = false;
    const char *depth = NULL;
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *seed[] = {"settings", "set", "remember.search.candidate_pool", "400"};
    const char *again[] = {"--json", "settings", "set",  "remember.search.candidate_pool",
                           "500",    "--as",     "agent"};
    const char *unknown[] = {"settings", "set", "nope", "1"};
    const char *usage[] = {"settings", "set"};
    const char *reset[] = {"settings", "reset", "remember.search.candidate_pool"};
    const char *reset_all[] = {"--json", "settings", "reset", "--all", "--scope", "user", "--yes"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    got = get_at(brain, userdb, seed, sizeof(seed) / sizeof(seed[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    cmd_result_free(&got);
    depth = getenv("REMEMBER_HOOK_DEPTH");
    had = depth != NULL;
    saved[0] = '\0';
    if (depth != NULL) {
        ASSERT_TRUE(snprintf(saved, sizeof(saved), "%s", depth) > 0);
    }
    ASSERT_EQ_INT(setenv("REMEMBER_HOOK_DEPTH", "1", 1), 0);
    got = get_at(brain, userdb, again, sizeof(again) / sizeof(again[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.out, "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.set."
                          "result\",\"error\":{\"code\":\"setting_from_hook\",\"message\":\"a hook "
                          "or plugin can't change settings.\",\"fix\":\"Change them yourself in a "
                          "terminal or in Settings.\",\"retryable\":false,\"detail\":{\"key\":"
                          "\"remember.search.candidate_pool\",\"hook\":\"1\"}}}\n");
    ASSERT_STR_NOT_CONTAINS(got.out, "invalid_argument");
    cmd_result_free(&got);
    got = get_at(brain, userdb, unknown, sizeof(unknown) / sizeof(unknown[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STR_CONTAINS(got.err, "a hook or plugin can't change settings.");
    ASSERT_STR_NOT_CONTAINS(got.err, "unknown setting");
    cmd_result_free(&got);
    got = get_at(brain, userdb, usage, sizeof(usage) / sizeof(usage[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: settings set requires a key\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, reset, sizeof(reset) / sizeof(reset[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STR_CONTAINS(got.err, "a hook or plugin can't change settings.");
    assert_query(userdb, "SELECT value FROM settings WHERE key = 'remember.search.candidate_pool'",
                 "400");
    cmd_result_free(&got);
    /* No key was typed, so detail has only the hook depth. */
    got = get_at(brain, userdb, reset_all, sizeof(reset_all) / sizeof(reset_all[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STR_CONTAINS(got.out, "\"retryable\":false,\"detail\":{\"hook\":\"1\"}}}\n");
    assert_query(userdb, "SELECT COUNT(*) FROM settings", "1");
    cmd_result_free(&got);
    restore_env("REMEMBER_HOOK_DEPTH", had, saved);
    harness_remove_tree(tmpl);
}

TEST(reset_user_layer_prints_the_effective_value)
{
    char tmpl[] = "/tmp/remember-reset-user-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *set[] = {"settings", "set", "remember.search.candidate_pool", "400"};
    const char *human_args[] = {"settings", "reset", "remember.search.candidate_pool"};
    const char *json_args[] = {"--json", "settings", "reset", "remember.search.candidate_pool"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    got = get_at(brain, userdb, set, sizeof(set) / sizeof(set[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    cmd_result_free(&got);
    got = get_at(brain, userdb, human_args, sizeof(human_args) / sizeof(human_args[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STREQ(got.err, "");
    ASSERT_STREQ(got.out, "\"remember.search.candidate_pool\" was reset. It's now 200.\n");
    assert_query(userdb,
                 "SELECT COUNT(*) FROM settings WHERE key = 'remember.search.candidate_pool'", "0");
    cmd_result_free(&got);
    got = get_at(brain, userdb, set, sizeof(set) / sizeof(set[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    cmd_result_free(&got);
    got = get_at(brain, userdb, json_args, sizeof(json_args) / sizeof(json_args[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STREQ(got.out,
                 "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.reset."
                 "result\",\"data\":{\"setting\":{\"key\":\"remember.search.candidate_pool\","
                 "\"type\":\"integer\",\"value\":200,\"from\":\"default\",\"changed\":false,"
                 "\"layers\":{\"default\":{\"value\":200},\"user\":null,\"brain\":null}},"
                 "\"scope\":\"user\",\"old_value\":400,\"changed\":true,\"action\":\"delete\"}}\n");
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

TEST(reset_scope_prints_unset)
{
    char tmpl[] = "/tmp/remember-reset-scope-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char expect[1400];
    char *stamp = NULL;
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *user_set[] = {"settings", "set", "remember.search.candidate_pool", "400"};
    const char *brain_set[] = {
        "settings", "set", "--scope", "brain", "remember.search.candidate_pool", "500"};
    const char *human_args[] = {"settings", "reset", "--scope", "user",
                                "remember.search.candidate_pool"};
    const char *json_args[] = {"--json",  "settings", "reset",
                               "--scope", "user",     "remember.search.candidate_pool"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    got = get_at(brain, userdb, user_set, sizeof(user_set) / sizeof(user_set[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    cmd_result_free(&got);
    got = get_at(brain, userdb, brain_set, sizeof(brain_set) / sizeof(brain_set[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    cmd_result_free(&got);
    stamp =
        q(brain, "SELECT updated_at FROM settings WHERE key = 'remember.search.candidate_pool'");
    got = get_at(brain, userdb, human_args, sizeof(human_args) / sizeof(human_args[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STREQ(got.out, "\"remember.search.candidate_pool\" was reset. It's now (unset).\n");
    assert_query(brain, "SELECT value FROM settings WHERE key = 'remember.search.candidate_pool'",
                 "500");
    cmd_result_free(&got);
    got = get_at(brain, userdb, user_set, sizeof(user_set) / sizeof(user_set[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    cmd_result_free(&got);
    got = get_at(brain, userdb, json_args, sizeof(json_args) / sizeof(json_args[0]));
    ASSERT_TRUE(
        snprintf(expect, sizeof(expect),
                 "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.reset."
                 "result\",\"data\":{\"setting\":{\"key\":\"remember.search.candidate_pool\","
                 "\"type\":\"integer\",\"value\":500,\"from\":\"brain\",\"changed\":true,"
                 "\"layers\":{\"default\":{\"value\":200},\"user\":null,\"brain\":{\"value\":"
                 "500,\"updated_by\":{\"kind\":\"human\",\"id\":null},\"updated_at\":\"%s\"}}},"
                 "\"scope\":\"user\",\"old_value\":400,\"changed\":true,\"action\":\"delete\"}"
                 "}\n",
                 stamp != NULL ? stamp : "") > 0);
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STREQ(got.out, expect);
    free(stamp);
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

TEST(reset_brain_keeps_a_tombstone)
{
    char tmpl[] = "/tmp/remember-reset-tomb-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *set[] = {"settings", "set",  "--scope", "brain",   "remember.search.candidate_pool",
                         "400",      "--as", "agent",   "--actor", "bot1"};
    const char *human_args[] = {
        "settings", "reset", "--scope", "brain", "remember.search.candidate_pool",
        "--as",     "agent", "--actor", "bot1"};
    const char *json_args[] = {"--json",  "settings", "reset",
                               "--scope", "brain",    "remember.search.candidate_pool"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    got = get_at(brain, userdb, set, sizeof(set) / sizeof(set[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    cmd_result_free(&got);
    got = get_at(brain, userdb, human_args, sizeof(human_args) / sizeof(human_args[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STREQ(got.out, "\"remember.search.candidate_pool\" was reset. It's now (unset).\n");
    assert_query(brain, "SELECT value FROM settings WHERE key = 'remember.search.candidate_pool'",
                 "400");
    assert_query(brain,
                 "SELECT deleted_at IS NOT NULL FROM settings WHERE key = "
                 "'remember.search.candidate_pool'",
                 "1");
    assert_query(brain,
                 "SELECT updated_by FROM settings WHERE key = 'remember.search.candidate_pool'",
                 "{\"kind\":\"agent\",\"id\":\"bot1\"}");
    cmd_result_free(&got);
    exec_fixture(brain, "DELETE FROM settings WHERE key = 'remember.search.candidate_pool';");
    got = get_at(brain, userdb, set, sizeof(set) / sizeof(set[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    cmd_result_free(&got);
    got = get_at(brain, userdb, json_args, sizeof(json_args) / sizeof(json_args[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STREQ(got.out,
                 "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.reset."
                 "result\",\"data\":{\"setting\":{\"key\":\"remember.search.candidate_pool\","
                 "\"type\":\"integer\",\"value\":200,\"from\":\"default\",\"changed\":false,"
                 "\"layers\":{\"default\":{\"value\":200},\"user\":null,\"brain\":null}},"
                 "\"scope\":\"brain\",\"old_value\":400,\"changed\":true,\"action\":"
                 "\"tombstone\"}}\n");
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

TEST(reset_of_a_missing_layer_prints_the_line)
{
    char tmpl[] = "/tmp/remember-reset-none-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *human_args[] = {"settings", "reset", "remember.search.candidate_pool"};
    const char *json_args[] = {"--json",  "settings", "reset",
                               "--scope", "brain",    "remember.search.candidate_pool"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    got = get_at(brain, userdb, human_args, sizeof(human_args) / sizeof(human_args[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STREQ(got.out, "\"remember.search.candidate_pool\" was reset. It's now 200.\n");
    ASSERT_TRUE(access(userdb, F_OK) != 0);
    assert_query(brain, "SELECT COUNT(*) FROM settings", "0");
    assert_query(brain, "SELECT change_seq FROM brain_meta", "0");
    cmd_result_free(&got);
    got = get_at(brain, userdb, json_args, sizeof(json_args) / sizeof(json_args[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STREQ(got.out,
                 "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.reset."
                 "result\",\"data\":{\"setting\":{\"key\":\"remember.search.candidate_pool\","
                 "\"type\":\"integer\",\"value\":200,\"from\":\"default\",\"changed\":false,"
                 "\"layers\":{\"default\":{\"value\":200},\"user\":null,\"brain\":null}},"
                 "\"scope\":\"brain\",\"old_value\":null,\"changed\":false,\"action\":\"none\"}"
                 "}\n");
    assert_query(brain, "SELECT COUNT(*) FROM settings", "0");
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

TEST(reset_prefixes_shows_the_default_list)
{
    char tmpl[] = "/tmp/remember-reset-pre-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *set[] = {"settings", "set", "remember.buckets.prefixes", "project:"};
    const char *args[] = {"settings", "reset", "remember.buckets.prefixes"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    got = get_at(brain, userdb, set, sizeof(set) / sizeof(set[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    cmd_result_free(&got);
    got = get_at(brain, userdb, args, sizeof(args) / sizeof(args[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STREQ(got.out, "\"remember.buckets.prefixes\" was reset. It's now project: status:.\n");
    assert_query(brain,
                 "SELECT deleted_at IS NOT NULL FROM settings WHERE key = "
                 "'remember.buckets.prefixes'",
                 "1");
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

TEST(reset_without_yes_counts_unique_keys)
{
    char tmpl[] = "/tmp/remember-reset-yes-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *none[] = {"settings", "reset", "--all", "--scope", "user"};
    const char *none_json[] = {"--json", "settings", "reset", "--all", "--scope", "user"};
    const char *one_set[] = {"settings", "set", "remember.search.max_results", "20"};
    const char *two_set[] = {"settings", "set", "remember.search.candidate_pool", "400"};
    const char *looks[] = {"settings", "reset", "--namespace", "--yes"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    got = get_at(brain, userdb, none, sizeof(none) / sizeof(none[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STREQ(got.out, "");
    ASSERT_STREQ(got.err, "");
    ASSERT_TRUE(access(userdb, F_OK) != 0);
    cmd_result_free(&got);
    got = get_at(brain, userdb, none_json, sizeof(none_json) / sizeof(none_json[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STREQ(got.out, "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting."
                          "reset.result\",\"data\":{\"count\":0,\"settings\":[]}}\n");
    ASSERT_STREQ(got.err, "");
    cmd_result_free(&got);
    got = get_at(brain, userdb, one_set, sizeof(one_set) / sizeof(one_set[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    cmd_result_free(&got);
    got = get_at(brain, userdb, none, sizeof(none) / sizeof(none[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: this resets 1 setting. Run 'remember settings reset --all "
                          "--scope user --yes' to confirm.\n");
    assert_query(userdb, "SELECT value FROM settings WHERE key = 'remember.search.max_results'",
                 "20");
    cmd_result_free(&got);
    got = get_at(brain, userdb, two_set, sizeof(two_set) / sizeof(two_set[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    cmd_result_free(&got);
    got = get_at(brain, userdb, none, sizeof(none) / sizeof(none[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: this resets 2 settings. Run 'remember settings reset --all "
                          "--scope user --yes' to confirm.\n");
    assert_query(userdb, "SELECT COUNT(*) FROM settings", "2");
    cmd_result_free(&got);
    got = get_at(brain, userdb, looks, sizeof(looks) / sizeof(looks[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: missing value for --namespace\n");
    assert_query(userdb, "SELECT COUNT(*) FROM settings", "2");
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

TEST(reset_same_key_on_both_layers_confirms_once)
{
    char tmpl[] = "/tmp/remember-reset-both-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *brain_set[] = {
        "settings", "set", "--scope", "brain", "remember.search.candidate_pool", "400"};
    const char *user_set[] = {"settings", "set", "remember.search.candidate_pool", "500"};
    const char *ask[] = {"settings", "reset", "--namespace", "remember.search.candidate_pool"};
    const char *human_args[] = {"settings", "reset", "--namespace",
                                "remember.search.candidate_pool", "--yes"};
    const char *json_args[] = {
        "--json", "settings", "reset", "--namespace", "remember.search.candidate_pool", "--yes"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    got = get_at(brain, userdb, brain_set, sizeof(brain_set) / sizeof(brain_set[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    cmd_result_free(&got);
    got = get_at(brain, userdb, user_set, sizeof(user_set) / sizeof(user_set[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    cmd_result_free(&got);
    got = get_at(brain, userdb, ask, sizeof(ask) / sizeof(ask[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: this resets 1 setting. Run 'remember settings reset "
                          "--namespace remember.search.candidate_pool --yes' to confirm.\n");
    assert_query(brain, "SELECT value FROM settings WHERE key = 'remember.search.candidate_pool'",
                 "400");
    assert_query(userdb, "SELECT value FROM settings WHERE key = 'remember.search.candidate_pool'",
                 "500");
    cmd_result_free(&got);
    got = get_at(brain, userdb, human_args, sizeof(human_args) / sizeof(human_args[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STREQ(got.out, "\"remember.search.candidate_pool\" was reset. It's now 200.\n"
                          "\"remember.search.candidate_pool\" was reset. It's now 200.\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, brain_set, sizeof(brain_set) / sizeof(brain_set[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    cmd_result_free(&got);
    got = get_at(brain, userdb, user_set, sizeof(user_set) / sizeof(user_set[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    cmd_result_free(&got);
    got = get_at(brain, userdb, json_args, sizeof(json_args) / sizeof(json_args[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STREQ(got.out,
                 "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.reset."
                 "result\",\"data\":{\"count\":2,\"settings\":[{\"setting\":{\"key\":\"remember."
                 "search.candidate_pool\",\"type\":\"integer\",\"value\":200,\"from\":\"default\","
                 "\"changed\":false,\"layers\":{\"default\":{\"value\":200},\"user\":null,"
                 "\"brain\":null}},\"scope\":\"brain\",\"old_value\":400,\"changed\":true,"
                 "\"action\":\"tombstone\"},{\"setting\":{\"key\":\"remember.search.candidate_"
                 "pool\",\"type\":\"integer\",\"value\":200,\"from\":\"default\",\"changed\":"
                 "false,\"layers\":{\"default\":{\"value\":200},\"user\":null,\"brain\":null}},"
                 "\"scope\":\"user\",\"old_value\":500,\"changed\":true,\"action\":\"delete\"}]}"
                 "}\n");
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

TEST(reset_namespace_matches_the_typed_prefix)
{
    char tmpl[] = "/tmp/remember-reset-ns-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *pool[] = {"settings", "set", "remember.search.candidate_pool", "400"};
    const char *maxr[] = {"settings", "set", "remember.search.max_results", "20"};
    const char *budget[] = {"settings", "set", "remember.plugins.chain_budget", "2s"};
    const char *empty_ns[] = {"--json", "settings", "reset", "--namespace", "", "--yes"};
    const char *hyphen[] = {"--json",          "settings", "reset", "--namespace",
                            "remember-search", "--scope",  "user",  "--yes"};
    const char *match[] = {"settings", "reset", "--namespace", "remember.search",
                           "--scope",  "user",  "--yes"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    got = get_at(brain, userdb, pool, sizeof(pool) / sizeof(pool[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    cmd_result_free(&got);
    got = get_at(brain, userdb, maxr, sizeof(maxr) / sizeof(maxr[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    cmd_result_free(&got);
    got = get_at(brain, userdb, budget, sizeof(budget) / sizeof(budget[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    cmd_result_free(&got);
    got = get_at(brain, userdb, empty_ns, sizeof(empty_ns) / sizeof(empty_ns[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STREQ(got.out, "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting."
                          "reset.result\",\"data\":{\"count\":0,\"settings\":[]}}\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, hyphen, sizeof(hyphen) / sizeof(hyphen[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STR_CONTAINS(got.out, "\"count\":0");
    cmd_result_free(&got);
    assert_query(userdb, "SELECT COUNT(*) FROM settings", "3");
    got = get_at(brain, userdb, match, sizeof(match) / sizeof(match[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STREQ(got.out, "\"remember.search.candidate_pool\" was reset. It's now (unset).\n"
                          "\"remember.search.max_results\" was reset. It's now (unset).\n");
    assert_query(userdb, "SELECT value FROM settings WHERE key = 'remember.plugins.chain_budget'",
                 "\"2s\"");
    assert_query(userdb, "SELECT COUNT(*) FROM settings", "1");
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

/* An unregistered stored key is still reset. Its type is JSON null: a declined
   type question is not a reason to invent "string" or to skip the row. */
TEST(reset_unregistered_key_has_a_null_type)
{
    char tmpl[] = "/tmp/remember-reset-unreg-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[256];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *keep[] = {"settings", "set", "remember.search.max_results", "20"};
    const char *human_args[] = {"settings", "reset", "--namespace", "plugin",
                                "--scope",  "user",  "--yes"};
    const char *json_args[] = {"--json", "settings", "reset", "--namespace",
                               "plugin", "--scope",  "user",  "--yes"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    got = get_at(brain, userdb, keep, sizeof(keep) / sizeof(keep[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    cmd_result_free(&got);
    ASSERT_EQ_INT(store_user_db_ensure(userdb, err, sizeof(err)), 0);
    exec_fixture(userdb, "INSERT INTO settings (key, value, updated_by, updated_at) VALUES "
                         "('plugin.gone','1','{\"kind\":\"human\",\"id\":null}',"
                         "'2020-01-02T03:04:00.000Z');");
    got = get_at(brain, userdb, human_args, sizeof(human_args) / sizeof(human_args[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STREQ(got.out, "\"plugin.gone\" was reset. It's now (unset).\n");
    assert_query(userdb, "SELECT COUNT(*) FROM settings WHERE key = 'plugin.gone'", "0");
    assert_query(userdb, "SELECT value FROM settings WHERE key = 'remember.search.max_results'",
                 "20");
    cmd_result_free(&got);
    exec_fixture(userdb, "INSERT INTO settings (key, value, updated_by, updated_at) VALUES "
                         "('plugin.gone','1','{\"kind\":\"human\",\"id\":null}',"
                         "'2020-01-02T03:04:00.000Z');");
    got = get_at(brain, userdb, json_args, sizeof(json_args) / sizeof(json_args[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STREQ(got.out,
                 "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting.reset."
                 "result\",\"data\":{\"count\":1,\"settings\":[{\"setting\":{\"key\":\"plugin."
                 "gone\",\"type\":null,\"value\":null,\"from\":\"default\",\"changed\":false,"
                 "\"layers\":{\"user\":null}},\"scope\":\"user\",\"old_value\":1,\"changed\":true,"
                 "\"action\":\"delete\"}]}}\n");
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

TEST(reset_all_yes_on_an_empty_layer_prints_nothing)
{
    char tmpl[] = "/tmp/remember-reset-empty-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *human_args[] = {"settings", "reset", "--all", "--scope", "user", "--yes"};
    const char *json_args[] = {"--json", "settings", "reset", "--all", "--scope", "brain", "--yes"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    got = get_at(brain, userdb, human_args, sizeof(human_args) / sizeof(human_args[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STREQ(got.out, "");
    ASSERT_STREQ(got.err, "");
    ASSERT_TRUE(access(userdb, F_OK) != 0);
    cmd_result_free(&got);
    got = get_at(brain, userdb, json_args, sizeof(json_args) / sizeof(json_args[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STREQ(got.out, "{\"format\":\"remember-message\",\"version\":1,\"type\":\"setting."
                          "reset.result\",\"data\":{\"count\":0,\"settings\":[]}}\n");
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

TEST(reset_stored_hyphen_uses_the_registry_name)
{
    char tmpl[] = "/tmp/remember-reset-hyphen-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *args[] = {"settings", "reset", "--all", "--scope", "brain", "--yes"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    exec_fixture(brain,
                 "INSERT INTO settings (key, value, updated_by, updated_at, device_id, "
                 "version_vector, deleted_at, change_seq) VALUES "
                 "('remember.search.candidate-pool','400','{\"kind\":\"human\",\"id\":null}',"
                 "'2020-01-02T03:04:00.000Z','d','{}',NULL,0);");
    got = get_at(brain, userdb, args, sizeof(args) / sizeof(args[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STREQ(got.out, "\"remember.search.candidate_pool\" was reset. It's now (unset).\n");
    assert_query(brain,
                 "SELECT deleted_at IS NOT NULL FROM settings WHERE key = "
                 "'remember.search.candidate-pool'",
                 "1");
    assert_query(brain,
                 "SELECT COUNT(*) FROM settings WHERE key = 'remember.search.candidate_pool'", "0");
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

TEST(reset_usage_rejects_a_bad_invocation)
{
    char tmpl[] = "/tmp/remember-reset-use-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *bare[] = {"settings", "reset"};
    const char *yes_only[] = {"settings", "reset", "--yes"};
    const char *two[] = {"settings", "reset", "remember.search.max_results", "extra"};
    const char *all_key[] = {"settings", "reset", "remember.search.max_results", "--all"};
    const char *ns_key[] = {"settings", "reset", "remember.search.max_results", "--namespace",
                            "remember.search"};
    const char *both[] = {"settings", "reset",   "--all", "--namespace",
                          "remember", "--scope", "user"};
    const char *all[] = {"settings", "reset", "--all"};
    const char *all_yes[] = {"settings", "reset", "--all", "--yes"};
    const char *yes_key[] = {"settings", "reset", "remember.search.max_results", "--yes"};
    const char *dup_all[] = {"settings", "reset", "--all", "--all", "--scope", "user"};
    const char *dup_yes[] = {"settings", "reset", "--yes", "--yes", "--all", "--scope", "user"};
    const char *dup_ns[] = {"settings", "reset", "--namespace", "a", "--namespace", "b"};
    const char *no_ns[] = {"settings", "reset", "--namespace"};
    const char *clear[] = {"settings", "reset", "remember.search.max_results", "--clear"};
    const char *value[] = {"settings", "reset", "remember.search.max_results", "--value"};
    const char *dup_actor[] = {"settings", "reset", "remember.search.max_results", "--actor", "a",
                               "--actor",  "b"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    got = get_at(brain, userdb, bare, sizeof(bare) / sizeof(bare[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: settings reset requires a key\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, yes_only, sizeof(yes_only) / sizeof(yes_only[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: settings reset requires a key\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, two, sizeof(two) / sizeof(two[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: settings reset takes one key\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, all_key, sizeof(all_key) / sizeof(all_key[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: cannot combine --all with a key\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, ns_key, sizeof(ns_key) / sizeof(ns_key[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: cannot combine --namespace with a key\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, both, sizeof(both) / sizeof(both[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: cannot combine --all and --namespace\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, all, sizeof(all) / sizeof(all[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: reset --all requires --scope\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, all_yes, sizeof(all_yes) / sizeof(all_yes[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: reset --all requires --scope\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, yes_key, sizeof(yes_key) / sizeof(yes_key[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: cannot combine --yes with a key\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, dup_all, sizeof(dup_all) / sizeof(dup_all[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: duplicate --all\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, dup_yes, sizeof(dup_yes) / sizeof(dup_yes[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: duplicate --yes\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, dup_ns, sizeof(dup_ns) / sizeof(dup_ns[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: duplicate --namespace\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, no_ns, sizeof(no_ns) / sizeof(no_ns[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: missing value for --namespace\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, clear, sizeof(clear) / sizeof(clear[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: unknown option '--clear'\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, value, sizeof(value) / sizeof(value[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: unknown option '--value'\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, dup_actor, sizeof(dup_actor) / sizeof(dup_actor[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: duplicate --actor\n");
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

TEST(list_live_and_reset_stored_reject_a_programmer_error)
{
    char tmpl[] = "/tmp/remember-reset-api-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[256];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    SettingStoredRow *rows = NULL;
    SettingWriteResult wr;
    size_t n = 1;
    SettingStatus st = SETTING_OK;

    st = store_setting_list_live(NULL, NULL, SETTING_LAYER_USER, &rows, &n, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    ASSERT_STREQ(err, "internal error");
    st = store_setting_reset_stored(NULL, NULL, "k", SETTING_LAYER_USER, NULL, &wr, err,
                                    sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    store_setting_rows_free(NULL, 0);
    if (store == NULL) {
        return;
    }
    n = 1;
    st = store_setting_list_live(store, userdb, SETTING_LAYER_AUTO, &rows, &n, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    ASSERT_EQ_INT((int)n, 0);
    st = store_setting_list_live(store, userdb, SETTING_LAYER_USER, &rows, &n, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_EQ_INT((int)n, 0);
    ASSERT_TRUE(rows == NULL);
    st = store_setting_reset_stored(store, userdb, "", SETTING_LAYER_USER, NULL, &wr, err,
                                    sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    st = store_setting_reset_stored(store, userdb, "remember.search.max_results",
                                    SETTING_LAYER_AUTO, NULL, &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    st = store_setting_reset_stored(store, userdb, "remember.search.max_results",
                                    SETTING_LAYER_USER, NULL, &wr, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_FALSE(wr.changed);
    ASSERT_TRUE(access(userdb, F_OK) != 0);
    close_pair(store, tmpl);
}

/* The C-05 sentence is malloc'd. remember_run is in-process here so the shared
   alloc hook can fail that malloc. The loop skips earlier hooked allocations. */
TEST(set_reports_out_of_memory_when_the_sentence_cannot_be_built)
{
    char tmpl[] = "/tmp/remember-set-oom-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *argv[] = {
        "remember", "--db", brain, "settings", "set", "remember.search.candidate_pool", "nope"};
    EnvHold hold;
    int saw = 0;
    int n = 0;

    if (store == NULL) {
        return;
    }
    store_close(store);
    env_hold(&hold);
    ASSERT_EQ_INT(setenv("REMEMBER_USER_DB", userdb, 1), 0);
    for (n = 0; n < 40 && saw == 0; n++) {
        char *out = NULL;
        char *err = NULL;
        size_t olen = 0U;
        size_t elen = 0U;
        FILE *of = open_memstream(&out, &olen);
        FILE *ef = open_memstream(&err, &elen);
        int rc = 0;

        if (of == NULL || ef == NULL) {
            if (of != NULL) {
                (void)fclose(of);
            }
            if (ef != NULL) {
                (void)fclose(ef);
            }
            free(out);
            free(err);
            break;
        }
        test_alloc_fail_after(n);
        rc = remember_run((int)(sizeof(argv) / sizeof(argv[0])), (char *const *)argv, of, ef);
        test_alloc_fail_after(-1);
        ASSERT_EQ_INT(fclose(of), 0);
        ASSERT_EQ_INT(fclose(ef), 0);
        if (rc == 1 && err != NULL && strcmp(err, "remember: out of memory\n") == 0) {
            saw = 1;
        }
        free(out);
        free(err);
    }
    test_alloc_fail_after(-1);
    env_release(&hold);
    ASSERT_EQ_INT(saw, 1);
    harness_remove_tree(tmpl);
}

typedef struct {
    char *out;
    char *err;
    int rc;
    char pad_[4];
} Captured;

typedef struct {
    int oom;
    int bare_detail;
    int failed;
} SweepSaw;

/* remember_run, in this process, so the alloc and prepare hooks apply. */
static Captured capture_run(const char *const *argv, int argc)
{
    Captured got = {.rc = -1, .out = NULL, .err = NULL};
    size_t olen = 0U;
    size_t elen = 0U;
    FILE *of = open_memstream(&got.out, &olen);
    FILE *ef = open_memstream(&got.err, &elen);

    if (of == NULL || ef == NULL) {
        if (of != NULL) {
            (void)fclose(of);
        }
        if (ef != NULL) {
            (void)fclose(ef);
        }
        free(got.out);
        free(got.err);
        got.out = NULL;
        got.err = NULL;
        return got;
    }
    got.rc = remember_run(argc, (char *const *)argv, of, ef);
    ASSERT_EQ_INT(fclose(of), 0);
    ASSERT_EQ_INT(fclose(ef), 0);
    return got;
}

/* Every n, including the ones before the malloc the test is aiming at. */
static SweepSaw sweep_alloc(const char *const *argv, int argc)
{
    enum { FAULT_SWEEP = 40 };
    SweepSaw saw = {0, 0, 0};
    int n = 0;

    for (n = 0; n < FAULT_SWEEP; n++) {
        Captured got;

        test_alloc_fail_after(n);
        got = capture_run(argv, argc);
        test_alloc_fail_after(-1);
        if (got.rc != 0) {
            saw.failed = 1;
        }
        if (got.rc != 0 && ((got.err != NULL && strstr(got.err, "out of memory") != NULL) ||
                            (got.out != NULL && strstr(got.out, "out of memory") != NULL))) {
            saw.oom = 1;
        }
        /* A quoted-span malloc failure still prints the prefix sentence and skips detail. */
        if (got.out != NULL && strstr(got.out, "are the same prefix") != NULL &&
            strstr(got.out, "\"item\"") == NULL) {
            saw.bare_detail = 1;
        }
        free(got.out);
        free(got.err);
    }
    test_alloc_fail_after(-1);
    return saw;
}

static int sweep_prepare(const char *const *argv, int argc)
{
    enum { FAULT_SWEEP = 40 };
    int saw = 0;
    int n = 0;

    for (n = 0; n < FAULT_SWEEP; n++) {
        Captured got;

        store_test_fail_prepare_after(n);
        got = capture_run(argv, argc);
        store_test_fail_prepare_after(-1);
        if (got.rc != 0) {
            saw = 1;
        }
        free(got.out);
        free(got.err);
    }
    store_test_fail_prepare_after(-1);
    return saw;
}

static void hold_fixture_env(EnvHold *hold, const char *userdb)
{
    env_hold(hold);
    ASSERT_EQ_INT(setenv("REMEMBER_USER_DB", userdb, 1), 0);
    ASSERT_EQ_INT(setenv("TZ", "UTC", 1), 0);
}

TEST(set_and_reset_report_home_when_it_is_unset)
{
    char tmpl[] = "/tmp/remember-set-home-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *set_args[] = {"settings", "set", "remember.search.candidate_pool", "400"};
    const char *reset_args[] = {"settings", "reset", "remember.search.candidate_pool"};
    EnvHold hold;
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    env_hold(&hold);
    ASSERT_EQ_INT(unsetenv("HOME"), 0);
    ASSERT_EQ_INT(unsetenv("XDG_DATA_HOME"), 0);
    ASSERT_EQ_INT(unsetenv("REMEMBER_USER_DB"), 0);
    got = run_remember(brain, set_args, sizeof(set_args) / sizeof(set_args[0]), NULL);
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: HOME is not set\n");
    cmd_result_free(&got);
    got = run_remember(brain, reset_args, sizeof(reset_args) / sizeof(reset_args[0]), NULL);
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "remember: HOME is not set\n");
    cmd_result_free(&got);
    env_release(&hold);
    harness_remove_tree(tmpl);
}

TEST(reset_after_dash_dash_unknown_key_and_idle_scope)
{
    char tmpl[] = "/tmp/remember-reset-edge-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    const char *ended[] = {"settings", "reset", "--", "remember.search.candidate_pool"};
    const char *unknown[] = {"settings", "reset", "remember.search.hybrid"};
    const char *idle[] = {"settings", "reset", "remember.plugins.idle_timeout", "--scope", "brain"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    store_close(store);
    got = get_at(brain, userdb, ended, sizeof(ended) / sizeof(ended[0]));
    ASSERT_EQ_INT(got.exit_code, 0);
    ASSERT_STREQ(got.err, "");
    ASSERT_STREQ(got.out, "\"remember.search.candidate_pool\" was reset. It's now 200.\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, unknown, sizeof(unknown) / sizeof(unknown[0]));
    ASSERT_EQ_INT(got.exit_code, 2);
    ASSERT_STREQ(got.err, "remember: unknown setting \"remember.search.hybrid\". Run 'remember "
                          "settings list' to see all settings.\n");
    cmd_result_free(&got);
    got = get_at(brain, userdb, idle, sizeof(idle) / sizeof(idle[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, k_idle_scope);
    cmd_result_free(&got);
    harness_remove_tree(tmpl);
}

/* The write commits, then the report reads every live layer. A hand-edited
   brain value that is not JSON fails that report. */
TEST(json_report_fails_when_another_layer_is_not_json)
{
    char tmpl[] = "/tmp/remember-set-badjson-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[256];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    SettingWriteResult wr;
    const char *set_args[] = {"--json", "settings", "set", "remember.search.candidate_pool", "400"};
    const char *reset_args[] = {"--json", "settings", "reset", "remember.search.candidate_pool"};
    CmdResult got;

    if (store == NULL) {
        return;
    }
    ASSERT_EQ_INT(set_one(store, userdb, "remember.search.candidate_pool", "400",
                          SETTING_LAYER_BRAIN, NULL, &wr, err, sizeof(err)),
                  SETTING_OK);
    exec_fixture(brain, "UPDATE settings SET value = 'nope' WHERE key = "
                        "'remember.search.candidate_pool';");
    store_close(store);
    got = get_at(brain, userdb, set_args, sizeof(set_args) / sizeof(set_args[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "");
    ASSERT_STR_CONTAINS(got.out, "\"code\":\"internal\"");
    ASSERT_STR_CONTAINS(got.out, "failed to write output");
    cmd_result_free(&got);
    assert_query(userdb, "SELECT value FROM settings WHERE key = 'remember.search.candidate_pool'",
                 "400");
    got = get_at(brain, userdb, reset_args, sizeof(reset_args) / sizeof(reset_args[0]));
    ASSERT_EQ_INT(got.exit_code, 1);
    ASSERT_STREQ(got.err, "");
    ASSERT_STR_CONTAINS(got.out, "failed to write output");
    cmd_result_free(&got);
    assert_query(userdb, "SELECT COUNT(*) FROM settings", "0");
    assert_query(brain, "SELECT value FROM settings WHERE key = 'remember.search.candidate_pool'",
                 "nope");
    harness_remove_tree(tmpl);
}

TEST(list_live_reports_a_failed_read)
{
    char tmpl[] = "/tmp/remember-list-live-fail-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char null_db[REMEMBER_PATH_MAX];
    char dir[REMEMBER_PATH_MAX];
    char err[256];
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    SettingWriteResult wr;
    SettingStoredRow *rows = NULL;
    size_t n = 0;
    SettingStatus st = SETTING_OK;
    int fault = 0;

    if (store == NULL) {
        return;
    }
    ASSERT_EQ_INT(set_one(store, userdb, "remember.search.candidate_pool", "400",
                          SETTING_LAYER_BRAIN, NULL, &wr, err, sizeof(err)),
                  SETTING_OK);
    store_test_fail_prepare_after(0);
    st = store_setting_list_live(store, userdb, SETTING_LAYER_BRAIN, &rows, &n, err, sizeof(err));
    store_test_fail_prepare_after(-1);
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    ASSERT_TRUE(strstr(err, "sqlite:") == err);
    store_test_fail_step_after(0);
    st = store_setting_list_live(store, userdb, SETTING_LAYER_BRAIN, &rows, &n, err, sizeof(err));
    store_test_fail_step_after(-1);
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    ASSERT_TRUE(strstr(err, "sqlite:") == err);
    for (fault = 0; fault < 3; fault++) {
        store_test_fail_alloc_after(fault);
        st = store_setting_list_live(store, userdb, SETTING_LAYER_BRAIN, &rows, &n, err,
                                     sizeof(err));
        store_test_fail_alloc_after(-1);
        ASSERT_EQ_INT(st, SETTING_ERR_STORE);
        ASSERT_STREQ(err, "out of memory");
    }
    st = store_setting_list_live(store, userdb, SETTING_LAYER_BRAIN, &rows, &n, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_OK);
    ASSERT_EQ_INT((int)n, 1);
    store_setting_rows_free(rows, n);
    rows = NULL;
    ASSERT_TRUE(snprintf(dir, sizeof(dir), "%s/not-a-file", tmpl) > 0);
    ASSERT_EQ_INT(mkdir(dir, 0700), 0);
    st = store_setting_list_live(store, dir, SETTING_LAYER_USER, &rows, &n, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    ASSERT_TRUE(strstr(err, "cannot open database") != NULL);
    /* The real settings columns are NOT NULL. This file is not that schema. */
    ASSERT_TRUE(snprintf(null_db, sizeof(null_db), "%s/null.db", tmpl) > 0);
    exec_fixture(null_db, "CREATE TABLE settings(key TEXT, value TEXT); "
                          "INSERT INTO settings(key, value) VALUES (NULL, '1');");
    st = store_setting_list_live(store, null_db, SETTING_LAYER_USER, &rows, &n, err, sizeof(err));
    ASSERT_EQ_INT(st, SETTING_ERR_STORE);
    ASSERT_STREQ(err, "out of memory");
    ASSERT_TRUE(rows == NULL);
    close_pair(store, tmpl);
}

TEST(set_and_reset_hit_alloc_and_prepare_faults)
{
    /* 3276 still allocates. One more returns quota before that malloc. */
    enum { QUOTA_OVER = (SETTING_VALUE_MAX / 5) + 1, QUOTA_HEAD = 6 };
    char tmpl[] = "/tmp/remember-set-fault-XXXXXX";
    char brain[REMEMBER_PATH_MAX];
    char userdb[REMEMBER_PATH_MAX];
    char err[256];
    const char **quota = NULL;
    Store *store = open_pair(tmpl, brain, sizeof(brain), userdb, sizeof(userdb));
    SettingWriteResult wr;
    EnvHold hold;
    SweepSaw saw;
    Captured got;
    size_t i = 0;
    const char *range[] = {
        "remember", "--db", brain, "settings", "set", "remember.search.candidate_pool", "19"};
    const char *bad_prefix[] = {
        "remember", "--db", brain, "settings", "set", "remember.buckets.prefixes", "nope"};
    const char *folded[] = {
        "remember", "--db",    brain, "--json", "settings", "set", "remember.buckets.prefixes",
        "Project:", "project:"};
    const char *confirm[] = {"remember", "--db",  brain,     "settings",
                             "reset",    "--all", "--scope", "user"};
    const char *clear_scalar[] = {
        "remember", "--db", brain, "settings", "set", "remember.search.candidate_pool", "--clear"};
    const char *two_values[] = {
        "remember", "--db", brain, "settings", "set", "remember.search.candidate_pool", "20", "21"};
    const char *user_only[] = {"remember", "--db",    brain,   "settings",
                               "set",      "--scope", "brain", "remember.plugins.idle_timeout",
                               "5m"};
    const char *missing[] = {"remember", "--db",  brain,
                             "settings", "reset", "remember.search.candidate_pool"};
    const char *again[] = {
        "remember", "--db", brain, "settings", "set", "remember.search.candidate_pool", "500"};
    const char *reset_one[] = {"remember", "--db",  brain,
                               "settings", "reset", "remember.search.candidate_pool"};
    const char *set_ok[] = {
        "remember", "--db", brain, "settings", "set", "remember.search.candidate_pool", "400"};
    const char *brain_set[] = {"remember", "--db",    brain,   "settings",
                               "set",      "--scope", "brain", "remember.search.candidate_pool",
                               "400"};
    const char *bulk[] = {"remember", "--db",    brain,   "settings", "reset",
                          "--all",    "--scope", "brain", "--yes"};

    if (store == NULL) {
        return;
    }
    store_close(store);
    quota = (const char **)malloc(((size_t)QUOTA_OVER + (size_t)QUOTA_HEAD) * sizeof(*quota));
    if (quota == NULL) {
        harness_remove_tree(tmpl);
        return;
    }
    quota[0] = "remember";
    quota[1] = "--db";
    quota[2] = brain;
    quota[3] = "settings";
    quota[4] = "set";
    quota[5] = "remember.buckets.prefixes";
    for (i = 0; i < (size_t)QUOTA_OVER; i++) {
        quota[(size_t)QUOTA_HEAD + i] = "a:";
    }
    hold_fixture_env(&hold, userdb);
    saw = sweep_alloc(range, (int)(sizeof(range) / sizeof(range[0])));
    ASSERT_EQ_INT(saw.oom, 1);
    got = capture_run(range, (int)(sizeof(range) / sizeof(range[0])));
    ASSERT_EQ_INT(got.rc, 1);
    ASSERT_STREQ(got.err, "remember: \"remember.search.candidate_pool\" can be 20 to 2000; you "
                          "gave 19. Set a value from 20 to 2000.\n");
    free(got.out);
    free(got.err);
    saw = sweep_alloc(clear_scalar, (int)(sizeof(clear_scalar) / sizeof(clear_scalar[0])));
    ASSERT_EQ_INT(saw.oom, 1);
    saw = sweep_alloc(two_values, (int)(sizeof(two_values) / sizeof(two_values[0])));
    ASSERT_EQ_INT(saw.oom, 1);
    saw = sweep_alloc(user_only, (int)(sizeof(user_only) / sizeof(user_only[0])));
    ASSERT_EQ_INT(saw.oom, 1);
    saw = sweep_alloc(bad_prefix, (int)(sizeof(bad_prefix) / sizeof(bad_prefix[0])));
    ASSERT_EQ_INT(saw.oom, 1);
    saw = sweep_alloc(folded, (int)(sizeof(folded) / sizeof(folded[0])));
    ASSERT_EQ_INT(saw.bare_detail, 1);
    got = capture_run(folded, (int)(sizeof(folded) / sizeof(folded[0])));
    ASSERT_EQ_INT(got.rc, 1);
    ASSERT_STR_CONTAINS(got.out, "\"item\":\"project:\"");
    ASSERT_STR_CONTAINS(got.out, "\"other\":\"Project:\"");
    free(got.out);
    free(got.err);
    saw = sweep_alloc(quota, QUOTA_OVER + QUOTA_HEAD);
    ASSERT_EQ_INT(saw.oom, 1);
    got = capture_run(quota, QUOTA_OVER + QUOTA_HEAD);
    ASSERT_EQ_INT(got.rc, 1);
    ASSERT_STREQ(got.err, "remember: the value for \"remember.buckets.prefixes\" is over 16 KiB, "
                          "the most a setting can hold. Use a shorter value.\n");
    free(got.out);
    free(got.err);
    got = capture_run(set_ok, (int)(sizeof(set_ok) / sizeof(set_ok[0])));
    ASSERT_EQ_INT(got.rc, 0);
    free(got.out);
    free(got.err);
    saw = sweep_alloc(confirm, (int)(sizeof(confirm) / sizeof(confirm[0])));
    ASSERT_EQ_INT(saw.oom, 1);
    got = capture_run(confirm, (int)(sizeof(confirm) / sizeof(confirm[0])));
    ASSERT_EQ_INT(got.rc, 1);
    ASSERT_STREQ(got.err, "remember: this resets 1 setting. Run 'remember settings reset --all "
                          "--scope user --yes' to confirm.\n");
    free(got.out);
    free(got.err);
    saw = sweep_alloc(missing, (int)(sizeof(missing) / sizeof(missing[0])));
    ASSERT_EQ_INT(saw.oom, 1);
    got = capture_run(missing, (int)(sizeof(missing) / sizeof(missing[0])));
    ASSERT_EQ_INT(got.rc, 0);
    ASSERT_STREQ(got.out, "\"remember.search.candidate_pool\" was reset. It's now 200.\n");
    free(got.out);
    free(got.err);
    env_release(&hold);
    store = store_open(brain, err, sizeof(err));
    ASSERT_TRUE(store != NULL);
    if (store == NULL) {
        free((void *)quota);
        harness_remove_tree(tmpl);
        return;
    }
    ASSERT_EQ_INT(set_one(store, userdb, "remember.search.candidate_pool", "400",
                          SETTING_LAYER_USER, NULL, &wr, err, sizeof(err)),
                  SETTING_OK);
    store_close(store);
    hold_fixture_env(&hold, userdb);
    saw = sweep_alloc(again, (int)(sizeof(again) / sizeof(again[0])));
    ASSERT_EQ_INT(saw.oom, 1);
    saw = sweep_alloc(reset_one, (int)(sizeof(reset_one) / sizeof(reset_one[0])));
    ASSERT_EQ_INT(saw.oom, 1);
    got = capture_run(set_ok, (int)(sizeof(set_ok) / sizeof(set_ok[0])));
    ASSERT_EQ_INT(got.rc, 0);
    free(got.out);
    free(got.err);
    ASSERT_EQ_INT(sweep_prepare(set_ok, (int)(sizeof(set_ok) / sizeof(set_ok[0]))), 1);
    got = capture_run(set_ok, (int)(sizeof(set_ok) / sizeof(set_ok[0])));
    ASSERT_EQ_INT(got.rc, 0);
    free(got.out);
    free(got.err);
    ASSERT_EQ_INT(sweep_prepare(reset_one, (int)(sizeof(reset_one) / sizeof(reset_one[0]))), 1);
    got = capture_run(brain_set, (int)(sizeof(brain_set) / sizeof(brain_set[0])));
    ASSERT_EQ_INT(got.rc, 0);
    free(got.out);
    free(got.err);
    ASSERT_EQ_INT(sweep_prepare(bulk, (int)(sizeof(bulk) / sizeof(bulk[0]))), 1);
    got = capture_run(bulk, (int)(sizeof(bulk) / sizeof(bulk[0])));
    ASSERT_EQ_INT(got.rc, 0);
    ASSERT_STREQ(got.out, "");
    ASSERT_STREQ(got.err, "");
    free(got.out);
    free(got.err);
    test_alloc_fail_after(-1);
    store_test_fail_prepare_after(-1);
    store_test_fail_alloc_after(-1);
    store_test_fail_step_after(-1);
    env_release(&hold);
    free((void *)quota);
    harness_remove_tree(tmpl);
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
    RUN_TEST(pick_ignores_a_layer_the_key_does_not_allow);
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
    RUN_TEST(remember_sync_token_is_trimmed_and_checked);
    RUN_TEST(callback_token_with_a_control_byte_is_unavailable);
    RUN_TEST(unavailable_json_names_the_fix_and_hides_the_token);
    RUN_TEST(store_failure_keeps_the_store_sentence);
    RUN_TEST(non_grammar_helper_does_not_call_the_callback);
    RUN_TEST(unavailable_echo_stays_one_line_in_both_formats);
    RUN_TEST(token_that_does_not_fit_is_not_truncated);
    RUN_TEST(cli_runner_reads_the_trimmed_first_line);
    RUN_TEST(cli_credential_timeout_ms_is_thirty_seconds);
    RUN_TEST(helper_not_on_path_is_unknown);
    RUN_TEST(helper_not_executable_is_lookup_failed);
    RUN_TEST(helper_nonzero_exit_wipes_the_token);
    RUN_TEST(empty_stdout_is_lookup_failed);
    RUN_TEST(nul_in_the_first_line_is_lookup_failed);
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
    RUN_TEST(brain_row_does_not_pick_the_credential_helper);
    RUN_TEST(get_reports_the_winning_layer_and_every_other_layer);
    RUN_TEST(format_when_prints_the_locked_date_and_the_utc_fallback);
    RUN_TEST(suggestion_names_one_key_or_nothing);
    RUN_TEST(get_default_only_names_every_allowed_layer);
    RUN_TEST(get_duration_quotes_the_text_and_adds_value_ms);
    RUN_TEST(get_prefixes_join_with_a_space);
    RUN_TEST(get_string_with_no_compiled_default_is_null);
    RUN_TEST(get_brain_beats_user_and_scope_hides_the_other_layers);
    RUN_TEST(get_scope_on_an_empty_layer_prints_unset);
    RUN_TEST(get_tombstone_is_not_a_value);
    RUN_TEST(get_value_is_one_line_and_json_stays_the_object);
    RUN_TEST(get_names_an_actor_id);
    RUN_TEST(get_omits_a_stamp_that_is_not_canonical);
    RUN_TEST(get_unknown_offers_one_key);
    RUN_TEST(get_unknown_without_one_candidate_uses_the_list_fix);
    RUN_TEST(get_plugin_budget_uses_the_manifest_fix);
    RUN_TEST(get_usage_rejects_a_bad_invocation);
    RUN_TEST(get_help_names_the_command);
    RUN_TEST(get_stamp_that_does_not_fit_is_a_store_error);
    RUN_TEST(get_rejects_a_nul_or_overlong_stamp);
    RUN_TEST(get_reports_home_only_after_the_key_is_known);
    RUN_TEST(get_rejects_a_value_that_is_not_json);
    RUN_TEST(get_shows_a_leap_day_and_omits_an_impossible_date);
    RUN_TEST(get_credential_helper_omits_a_disallowed_brain_row);
    RUN_TEST(get_folds_a_hyphen_and_ends_options);
    RUN_TEST(get_quotes_the_typed_key);
    RUN_TEST(set_writes_the_user_layer_and_prints_nothing);
    RUN_TEST(set_same_value_writes_nothing_and_reports_the_old_value);
    RUN_TEST(set_stores_the_actor);
    RUN_TEST(set_without_scope_writes_the_only_allowed_layer);
    RUN_TEST(set_clear_writes_an_empty_prefix_list);
    RUN_TEST(set_invalid_value_names_the_kind);
    RUN_TEST(set_reports_out_of_memory_when_the_sentence_cannot_be_built);
    RUN_TEST(set_prefix_errors_name_the_item);
    RUN_TEST(set_out_of_range_names_the_bounds);
    RUN_TEST(set_scope_mismatch_names_the_layer);
    RUN_TEST(set_value_over_sixteen_kib_is_quota);
    RUN_TEST(set_user_quota_refuses_a_new_key_and_keeps_an_existing_one);
    RUN_TEST(set_brain_quota_names_the_brain);
    RUN_TEST(set_unknown_key_offers_one_suggestion);
    RUN_TEST(set_usage_rejects_a_bad_invocation);
    RUN_TEST(a_hook_cannot_set_or_reset);
    RUN_TEST(reset_user_layer_prints_the_effective_value);
    RUN_TEST(reset_scope_prints_unset);
    RUN_TEST(reset_brain_keeps_a_tombstone);
    RUN_TEST(reset_of_a_missing_layer_prints_the_line);
    RUN_TEST(reset_prefixes_shows_the_default_list);
    RUN_TEST(reset_without_yes_counts_unique_keys);
    RUN_TEST(reset_same_key_on_both_layers_confirms_once);
    RUN_TEST(reset_namespace_matches_the_typed_prefix);
    RUN_TEST(reset_unregistered_key_has_a_null_type);
    RUN_TEST(reset_all_yes_on_an_empty_layer_prints_nothing);
    RUN_TEST(reset_stored_hyphen_uses_the_registry_name);
    RUN_TEST(reset_usage_rejects_a_bad_invocation);
    RUN_TEST(list_live_and_reset_stored_reject_a_programmer_error);
    RUN_TEST(set_and_reset_report_home_when_it_is_unset);
    RUN_TEST(reset_after_dash_dash_unknown_key_and_idle_scope);
    RUN_TEST(json_report_fails_when_another_layer_is_not_json);
    RUN_TEST(list_live_reports_a_failed_read);
    RUN_TEST(set_and_reset_hit_alloc_and_prepare_faults);
}
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
