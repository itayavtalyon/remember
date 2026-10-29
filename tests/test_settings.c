#include "harness.h"
#include "normalize.h"
#include "register.h"
#include "settings.h"
#include "store.h"
#include "test.h"
#include "util.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
}
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
