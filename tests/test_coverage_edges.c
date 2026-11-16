/* Test-only edge suite: analyzer noise on intentional fault/IO paths is not useful. */
// NOLINTBEGIN

#include "cli.h"
#include "harness.h"
#include "normalize.h"
#include "output.h"
#include "register.h"
#include "store.h"
#include "test.h"
#include "util.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * Extra edges for production line coverage: util/output/cli unit paths and
 * black-box NYI/help topics. Keep this suite small and intentional.
 */

/* ---- util ---------------------------------------------------------------- */

static char *dup_home_or_null(void)
{
    const char *h = getenv("HOME");
    char *saved;
    size_t n;
    if (h == NULL) {
        return NULL;
    }
    n = strlen(h);
    saved = (char *)malloc(n + 1U);
    if (saved == NULL) {
        return NULL;
    }
    memcpy(saved, h, n + 1U);
    return saved;
}

/* A temp HOME. content NULL leaves ~/.remember/default-db missing. The old
   HOME comes back in given_home_release. REMEMBER_DB and XDG_DATA_HOME start
   unset so the dev machine's own environment cannot reach a test. */
typedef struct {
    char dir[64];
    char file[96]; /* ~/.remember/default-db */
    char *saved_home;
} GivenHome;

static void write_bytes_file(const char *path, const char *bytes, size_t len)
{
    FILE *f = fopen(path, "wb");
    size_t wrote = 0;
    int closed = 0;
    if (f == NULL) {
        ASSERT_TRUE(f != NULL);
        return;
    }
    wrote = fwrite(bytes, 1U, len, f);
    closed = fclose(f);
    ASSERT_TRUE(wrote == len);
    ASSERT_EQ_INT(closed, 0);
}

static void write_text_file(const char *path, const char *content)
{
    write_bytes_file(path, content, strlen(content));
}

static void given_home(GivenHome *h, const char *content)
{
    (void)snprintf(h->dir, sizeof(h->dir), "/tmp/remember-defdb-XXXXXX");
    ASSERT_TRUE(mkdtemp(h->dir) != NULL);
    h->saved_home = dup_home_or_null();
    ASSERT_EQ_INT(setenv("HOME", h->dir, 1), 0);
    unsetenv("REMEMBER_DB");
    unsetenv("XDG_DATA_HOME");
    (void)snprintf(h->file, sizeof(h->file), "%s/.remember", h->dir);
    ASSERT_EQ_INT(mkdir(h->file, 0700), 0);
    (void)snprintf(h->file, sizeof(h->file), "%s/.remember/default-db", h->dir);
    if (content != NULL) {
        write_text_file(h->file, content);
    }
}

static void given_home_release(GivenHome *h)
{
    unsetenv("REMEMBER_DB");
    unsetenv("XDG_DATA_HOME");
    if (h->saved_home != NULL) {
        (void)setenv("HOME", h->saved_home, 1);
        free(h->saved_home);
    }
    harness_remove_tree(h->dir);
}

/* The brain path under the temp HOME when no default-db line is used. */
static void given_builtin_path(const GivenHome *h, char *out, size_t outlen)
{
    (void)snprintf(out, outlen, "%s/.remember/remember.db", h->dir);
}

static int default_db_exists(const GivenHome *h)
{
    struct stat st;
    return stat(h->file, &st) == 0;
}

static int default_db_is(const GivenHome *h, const char *want)
{
    char *got;
    size_t len = 0;
    int same;
    got = harness_read_file(h->file, &len);
    same = got != NULL && strcmp(got, want) == 0;
    free(got);
    return same;
}

TEST(util_resolve_null_buffer_fails)
{
    char err[64];
    err[0] = '\0';
    ASSERT_EQ_INT(util_resolve_db_path("/tmp/x.db", NULL, 16, err, sizeof(err)), -1);
    ASSERT_STR_CONTAINS(err, "internal");
}

TEST(util_resolve_zero_buflen_fails)
{
    char buf[8];
    char err[64];
    ASSERT_EQ_INT(util_resolve_db_path("/tmp/x.db", buf, 0, err, sizeof(err)), -1);
}

TEST(util_resolve_empty_env_falls_through_to_home)
{
    GivenHome h;
    char buf[REMEMBER_PATH_MAX];
    char err[64];
    given_home(&h, NULL);
    ASSERT_EQ_INT(setenv("REMEMBER_DB", "", 1), 0);
    ASSERT_EQ_INT(util_resolve_db_path(NULL, buf, sizeof(buf), err, sizeof(err)), 0);
    ASSERT_STR_CONTAINS(buf, "/.remember/remember.db");
    given_home_release(&h);
}

TEST(util_resolve_path_too_long)
{
    char longp[REMEMBER_PATH_MAX + 32];
    char buf[32];
    char err[128];
    size_t i;
    for (i = 0; i < sizeof(longp) - 1U; i++) {
        longp[i] = 'a';
    }
    longp[sizeof(longp) - 1U] = '\0';
    ASSERT_EQ_INT(util_resolve_db_path(longp, buf, sizeof(buf), err, sizeof(err)), -1);
    ASSERT_STR_CONTAINS(err, "too long");
}

TEST(util_resolve_home_missing)
{
    char buf[REMEMBER_PATH_MAX];
    char err[128];
    char *saved = dup_home_or_null();
    unsetenv("HOME");
    unsetenv("REMEMBER_DB");
    ASSERT_EQ_INT(util_resolve_db_path(NULL, buf, sizeof(buf), err, sizeof(err)), -1);
    ASSERT_STR_CONTAINS(err, "HOME");
    if (saved != NULL) {
        (void)setenv("HOME", saved, 1);
        free(saved);
    }
}

TEST(util_resolve_home_empty)
{
    char buf[REMEMBER_PATH_MAX];
    char err[128];
    char *saved = dup_home_or_null();
    (void)setenv("HOME", "", 1);
    unsetenv("REMEMBER_DB");
    ASSERT_EQ_INT(util_resolve_db_path(NULL, buf, sizeof(buf), err, sizeof(err)), -1);
    ASSERT_STR_CONTAINS(err, "HOME");
    if (saved != NULL) {
        (void)setenv("HOME", saved, 1);
        free(saved);
    } else {
        unsetenv("HOME");
    }
}

TEST(util_resolve_default_path_buffer_too_small)
{
    char buf[8];
    char err[128];
    char *saved = dup_home_or_null();
    ASSERT_TRUE(saved != NULL);
    if (saved == NULL) {
        return;
    }
    (void)setenv("HOME", "/Users/verylonghomeopathfortest", 1);
    unsetenv("REMEMBER_DB");
    ASSERT_EQ_INT(util_resolve_db_path(NULL, buf, sizeof(buf), err, sizeof(err)), -1);
    ASSERT_STR_CONTAINS(err, "too long");
    (void)setenv("HOME", saved, 1);
    free(saved);
}

/* ---- util: default-db (plan 27) ------------------------------------------ */

TEST(util_resolve_env_hides_default_db)
{
    GivenHome h;
    char buf[REMEMBER_PATH_MAX];
    char err[128];
    given_home(&h, "/tmp/from-file.db\n");
    ASSERT_EQ_INT(setenv("REMEMBER_DB", "/tmp/from-env.db", 1), 0);
    ASSERT_EQ_INT(util_resolve_db_path(NULL, buf, sizeof(buf), err, sizeof(err)), 0);
    ASSERT_STREQ(buf, "/tmp/from-env.db");
    given_home_release(&h);
}

TEST(util_resolve_cli_db_hides_default_db)
{
    GivenHome h;
    char buf[REMEMBER_PATH_MAX];
    char err[128];
    given_home(&h, "/tmp/from-file.db\n");
    ASSERT_EQ_INT(util_resolve_db_path("/tmp/from-cli.db", buf, sizeof(buf), err, sizeof(err)), 0);
    ASSERT_STREQ(buf, "/tmp/from-cli.db");
    given_home_release(&h);
}

TEST(util_resolve_default_db_absolute_line_is_the_brain)
{
    GivenHome h;
    char buf[REMEMBER_PATH_MAX];
    char err[128];
    given_home(&h, "/tmp/from-file.db\n");
    ASSERT_EQ_INT(util_resolve_db_path(NULL, buf, sizeof(buf), err, sizeof(err)), 0);
    ASSERT_STREQ(buf, "/tmp/from-file.db");
    given_home_release(&h);
}

TEST(util_resolve_default_db_missing_file_uses_builtin_and_creates_nothing)
{
    GivenHome h;
    char buf[REMEMBER_PATH_MAX];
    char err[128];
    char want[128];
    given_home(&h, NULL);
    ASSERT_EQ_INT(util_resolve_db_path(NULL, buf, sizeof(buf), err, sizeof(err)), 0);
    given_builtin_path(&h, want, sizeof(want));
    ASSERT_STREQ(buf, want);
    ASSERT_FALSE(default_db_exists(&h));
    given_home_release(&h);
}

TEST(util_resolve_default_db_empty_line_uses_builtin_and_creates_nothing)
{
    GivenHome h;
    char buf[REMEMBER_PATH_MAX];
    char err[128];
    char want[128];
    given_home(&h, "\n");
    ASSERT_EQ_INT(util_resolve_db_path(NULL, buf, sizeof(buf), err, sizeof(err)), 0);
    given_builtin_path(&h, want, sizeof(want));
    ASSERT_STREQ(buf, want);
    ASSERT_TRUE(default_db_is(&h, "\n"));
    given_home_release(&h);
    given_home(&h, " \t\r\n");
    ASSERT_EQ_INT(util_resolve_db_path(NULL, buf, sizeof(buf), err, sizeof(err)), 0);
    given_builtin_path(&h, want, sizeof(want));
    ASSERT_STREQ(buf, want);
    given_home_release(&h);
}

TEST(util_resolve_default_db_relative_line_uses_builtin_and_creates_nothing)
{
    GivenHome h;
    char buf[REMEMBER_PATH_MAX];
    char err[128];
    char want[128];
    given_home(&h, "rel/brain.db\n");
    ASSERT_EQ_INT(util_resolve_db_path(NULL, buf, sizeof(buf), err, sizeof(err)), 0);
    given_builtin_path(&h, want, sizeof(want));
    ASSERT_STREQ(buf, want);
    ASSERT_TRUE(default_db_is(&h, "rel/brain.db\n"));
    given_home_release(&h);
}

TEST(util_resolve_default_db_trims_ascii_whitespace_and_needs_no_newline)
{
    GivenHome h;
    char buf[REMEMBER_PATH_MAX];
    char err[128];
    given_home(&h, " \t/tmp/trimmed.db \r\n");
    ASSERT_EQ_INT(util_resolve_db_path(NULL, buf, sizeof(buf), err, sizeof(err)), 0);
    ASSERT_STREQ(buf, "/tmp/trimmed.db");
    given_home_release(&h);
    given_home(&h, "/tmp/no-newline.db");
    ASSERT_EQ_INT(util_resolve_db_path(NULL, buf, sizeof(buf), err, sizeof(err)), 0);
    ASSERT_STREQ(buf, "/tmp/no-newline.db");
    given_home_release(&h);
}

TEST(util_resolve_empty_remember_db_does_not_hide_default_db)
{
    GivenHome h;
    char buf[REMEMBER_PATH_MAX];
    char err[128];
    given_home(&h, "/tmp/from-file.db\n");
    ASSERT_EQ_INT(setenv("REMEMBER_DB", "", 1), 0);
    ASSERT_EQ_INT(util_resolve_db_path(NULL, buf, sizeof(buf), err, sizeof(err)), 0);
    ASSERT_STREQ(buf, "/tmp/from-file.db");
    given_home_release(&h);
}

/* Both spellings fail with the sentence --db gives, and the file survives. */
TEST(util_resolve_default_db_ephemeral_fails_like_cli_db)
{
    static const char *const lines[] = {":memory:\n", "file:/tmp/x.db\n"};
    size_t i;
    for (i = 0; i < 2U; i++) {
        GivenHome h;
        char buf[REMEMBER_PATH_MAX];
        char err[128];
        char want[128];
        char line[32];
        given_home(&h, lines[i]);
        (void)snprintf(line, sizeof(line), "%s", lines[i]);
        line[strlen(line) - 1U] = '\0';
        ASSERT_EQ_INT(util_resolve_db_path(line, buf, sizeof(buf), want, sizeof(want)), -1);
        ASSERT_STR_CONTAINS(want, "must be a regular file");
        ASSERT_EQ_INT(util_resolve_db_path(NULL, buf, sizeof(buf), err, sizeof(err)), -1);
        ASSERT_STREQ(err, want);
        ASSERT_TRUE(default_db_is(&h, lines[i]));
        given_home_release(&h);
    }
}

TEST(util_resolve_default_db_xdg_data_home_does_not_move_it)
{
    GivenHome h;
    char buf[REMEMBER_PATH_MAX];
    char err[128];
    char want[128];
    char xdg[128];
    char dir[160];
    given_home(&h, "/tmp/from-home.db\n");
    (void)snprintf(xdg, sizeof(xdg), "%s/xdg", h.dir);
    ASSERT_EQ_INT(mkdir(xdg, 0700), 0);
    (void)snprintf(dir, sizeof(dir), "%s/.remember", xdg);
    ASSERT_EQ_INT(mkdir(dir, 0700), 0);
    (void)snprintf(dir, sizeof(dir), "%s/.remember/default-db", xdg);
    write_text_file(dir, "/tmp/from-xdg.db\n");
    ASSERT_EQ_INT(setenv("XDG_DATA_HOME", xdg, 1), 0);
    ASSERT_EQ_INT(util_resolve_db_path(NULL, buf, sizeof(buf), err, sizeof(err)), 0);
    ASSERT_STREQ(buf, "/tmp/from-home.db");
    ASSERT_EQ_INT(unlink(h.file), 0);
    ASSERT_EQ_INT(util_resolve_db_path(NULL, buf, sizeof(buf), err, sizeof(err)), 0);
    given_builtin_path(&h, want, sizeof(want));
    ASSERT_STREQ(buf, want);
    given_home_release(&h);
}

TEST(util_resolve_default_db_line_too_long_fails)
{
    GivenHome h;
    char buf[16];
    char err[128];
    char big[REMEMBER_PATH_MAX + 64];
    given_home(&h, "/a/path/longer/than/sixteen.db\n");
    ASSERT_EQ_INT(util_resolve_db_path(NULL, buf, sizeof(buf), err, sizeof(err)), -1);
    ASSERT_STR_CONTAINS(err, "too long");
    given_home_release(&h);
    memset(big, 'a', sizeof(big) - 1U);
    big[0] = '/';
    big[sizeof(big) - 1U] = '\0';
    given_home(&h, big);
    {
        char wide[REMEMBER_PATH_MAX * 2];
        ASSERT_EQ_INT(util_resolve_db_path(NULL, wide, sizeof(wide), err, sizeof(err)), -1);
        ASSERT_STR_CONTAINS(err, "too long");
    }
    given_home_release(&h);
}

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* default-db is read up to PATH_MAX + 1 bytes: the longest path PATH_MAX
   holds, with its newline (PATH_MAX bytes), is read whole; one byte more is
   too long. */
TEST(util_resolve_default_db_read_limit_is_path_max_bytes)
{
    GivenHome h;
    char line[PATH_MAX + 2];
    char wide[REMEMBER_PATH_MAX * 2];
    char err[128];
    memset(line, 'a', sizeof(line));
    line[0] = '/';
    line[PATH_MAX - 1] = '\n';
    line[PATH_MAX] = '\0';
    given_home(&h, line);
    ASSERT_EQ_INT(util_resolve_db_path(NULL, wide, sizeof(wide), err, sizeof(err)), 0);
    ASSERT_EQ_INT((int)strlen(wide), PATH_MAX - 1);
    given_home_release(&h);
    line[PATH_MAX - 1] = 'a';
    line[PATH_MAX] = '\n';
    line[PATH_MAX + 1] = '\0';
    given_home(&h, line);
    ASSERT_EQ_INT(util_resolve_db_path(NULL, wide, sizeof(wide), err, sizeof(err)), -1);
    ASSERT_STREQ(err, "database path is too long");
    given_home_release(&h);
}

/* Itay 2026-11-13 (decision:2026-11-13-default-db-unreadable): a default-db
   that exists but cannot be read fails at once, with no fallback to
   remember.db. A directory fails on the read, a mode 000 file on the open. */
TEST(util_resolve_default_db_directory_fails_with_no_fallback)
{
    GivenHome h;
    char buf[REMEMBER_PATH_MAX];
    char err[128];
    given_home(&h, NULL);
    ASSERT_EQ_INT(mkdir(h.file, 0700), 0);
    ASSERT_EQ_INT(util_resolve_db_path(NULL, buf, sizeof(buf), err, sizeof(err)), -1);
    ASSERT_STREQ(err, "cannot read default-db");
    given_home_release(&h);
}

/* A symlink that loops fails the open for every user, root included. */
TEST(util_resolve_default_db_symlink_loop_fails_with_no_fallback)
{
    GivenHome h;
    char buf[REMEMBER_PATH_MAX];
    char err[128];
    given_home(&h, NULL);
    ASSERT_EQ_INT(symlink("default-db", h.file), 0);
    ASSERT_EQ_INT(util_resolve_db_path(NULL, buf, sizeof(buf), err, sizeof(err)), -1);
    ASSERT_STREQ(err, "cannot read default-db");
    given_home_release(&h);
}

TEST(util_resolve_default_db_without_permission_fails_with_no_fallback)
{
    GivenHome h;
    char buf[REMEMBER_PATH_MAX];
    char err[128];
    if (geteuid() == 0) {
        (void)printf("SKIP %s (running as root; file mode bits are ignored)\n",
                     "util_resolve_default_db_without_permission_fails_with_no_fallback");
        return;
    }
    given_home(&h, "/tmp/from-file.db\n");
    ASSERT_EQ_INT(chmod(h.file, 0000), 0);
    ASSERT_EQ_INT(util_resolve_db_path(NULL, buf, sizeof(buf), err, sizeof(err)), -1);
    ASSERT_STREQ(err, "cannot read default-db");
    given_home_release(&h);
}

/* decision:2026-11-14-plan27-review-grill G1: a default-db that is not a
   regular file fails at once. A FIFO must fail, not block the open. */
TEST(util_resolve_default_db_not_a_regular_file_fails)
{
    GivenHome h;
    char buf[REMEMBER_PATH_MAX];
    char err[128];
    int next_fd = -1;
    int probe_fd = -1;
    given_home(&h, NULL);
    ASSERT_EQ_INT(symlink("/dev/null", h.file), 0);
    ASSERT_EQ_INT(util_resolve_db_path(NULL, buf, sizeof(buf), err, sizeof(err)), -1);
    ASSERT_STREQ(err, "cannot read default-db");
    ASSERT_EQ_INT(unlink(h.file), 0);
    ASSERT_EQ_INT(mkfifo(h.file, 0600), 0);
    next_fd = dup(0);
    ASSERT_EQ_INT(close(next_fd), 0);
    ASSERT_EQ_INT(util_resolve_db_path(NULL, buf, sizeof(buf), err, sizeof(err)), -1);
    ASSERT_STREQ(err, "cannot read default-db");
    probe_fd = dup(0); /* the lowest free descriptor: the resolver closed its own */
    ASSERT_EQ_INT(probe_fd, next_fd);
    ASSERT_EQ_INT(close(probe_fd), 0);
    given_home_release(&h);
}

/* G2 of the same decision: a NUL byte would cut the path short, so it fails. */
TEST(util_resolve_default_db_nul_byte_fails)
{
    static const char line[] = "/tmp/a\0/b\n";
    GivenHome h;
    char buf[REMEMBER_PATH_MAX];
    char err[128];
    given_home(&h, NULL);
    write_bytes_file(h.file, line, sizeof(line) - 1U);
    ASSERT_EQ_INT(util_resolve_db_path(NULL, buf, sizeof(buf), err, sizeof(err)), -1);
    ASSERT_STREQ(err, "cannot read default-db");
    given_home_release(&h);
}

/* A name that points nowhere, or a ~/.remember that is not a folder, is no
   file: the file does not exist, so nothing is unreadable. */
TEST(util_resolve_default_db_that_does_not_exist_uses_builtin)
{
    GivenHome h;
    char buf[REMEMBER_PATH_MAX];
    char err[128];
    char want[REMEMBER_PATH_MAX];
    char dir[96];
    given_home(&h, NULL);
    given_builtin_path(&h, want, sizeof(want));
    ASSERT_EQ_INT(symlink("/nonexistent/remember-target", h.file), 0);
    ASSERT_EQ_INT(util_resolve_db_path(NULL, buf, sizeof(buf), err, sizeof(err)), 0);
    ASSERT_STREQ(buf, want);
    ASSERT_EQ_INT(unlink(h.file), 0);
    (void)snprintf(dir, sizeof(dir), "%s/.remember", h.dir);
    ASSERT_EQ_INT(rmdir(dir), 0);
    write_text_file(dir, "not a folder");
    ASSERT_EQ_INT(util_resolve_db_path(NULL, buf, sizeof(buf), err, sizeof(err)), 0);
    ASSERT_STREQ(buf, want);
    given_home_release(&h);
}

TEST(util_set_err_truncates)
{
    /* Path too long with tiny err buffer exercises set_err truncation. */
    char longp[64];
    char buf[8];
    char err[8];
    size_t i;
    for (i = 0; i < sizeof(longp) - 1U; i++) {
        longp[i] = 'p';
    }
    longp[sizeof(longp) - 1U] = '\0';
    ASSERT_EQ_INT(util_resolve_db_path(longp, buf, sizeof(buf), err, sizeof(err)), -1);
    ASSERT_TRUE(err[sizeof(err) - 1U] == '\0');
    ASSERT_TRUE(strlen(err) < sizeof(err));
}

TEST(util_read_stdin_null_out)
{
    ASSERT_EQ_INT(util_read_stdin(NULL, NULL, 100), -1);
}

/* ---- output -------------------------------------------------------------- */

TEST(output_json_string_null_out_fails)
{
    ASSERT_EQ_INT(output_json_string(NULL, "x"), -1);
}

TEST(output_json_string_null_s_uses_empty)
{
    FILE *f = tmpfile();
    char buf[16];
    ASSERT_TRUE(f != NULL);
    if (f == NULL) {
        return;
    }
    ASSERT_EQ_INT(output_json_string(f, NULL), 0);
    ASSERT_EQ_INT(fseek(f, 0, SEEK_SET), 0);
    ASSERT_TRUE(fgets(buf, sizeof(buf), f) != NULL);
    ASSERT_STREQ(buf, "\"\"");
    (void)fclose(f);
}

TEST(output_json_escapes_controls)
{
    FILE *f = tmpfile();
    const char *s = "a\b\f\r\t\"\\";
    char *got;
    long n;
    ASSERT_TRUE(f != NULL);
    if (f == NULL) {
        return;
    }
    ASSERT_EQ_INT(output_json_string(f, s), 0);
    ASSERT_EQ_INT(fflush(f), 0);
    n = ftell(f);
    ASSERT_TRUE(n > 0);
    ASSERT_EQ_INT(fseek(f, 0, SEEK_SET), 0);
    got = (char *)malloc((size_t)n + 1U);
    ASSERT_TRUE(got != NULL);
    if (got == NULL) {
        (void)fclose(f);
        return;
    }
    ASSERT_TRUE(fread(got, 1, (size_t)n, f) == (size_t)n);
    got[n] = '\0';
    ASSERT_STR_CONTAINS(got, "\\b");
    ASSERT_STR_CONTAINS(got, "\\f");
    ASSERT_STR_CONTAINS(got, "\\r");
    ASSERT_STR_CONTAINS(got, "\\t");
    ASSERT_STR_CONTAINS(got, "\\\"");
    ASSERT_STR_CONTAINS(got, "\\\\");
    free(got);
    (void)fclose(f);
}

TEST(output_null_entry_fails)
{
    FILE *f = tmpfile();
    ASSERT_TRUE(f != NULL);
    ASSERT_EQ_INT(output_entry_json(f, NULL, "t"), -1);
    ASSERT_EQ_INT(output_action_data(f, "y", NULL, "t"), -1);
    ASSERT_EQ_INT(output_memory_data(f, NULL, NULL, NULL, 0, "t"), -1);
    ASSERT_EQ_INT(output_entry_human_line(f, NULL, NULL, 0, "t"), -1);
    ASSERT_EQ_INT(output_list_data(f, 1, 1, NULL, NULL, 0, "t"), -1);
    // cppcheck-suppress nullPointerOutOfResources ; test-only: tmpfile()/malloc exhaustion is
    // asserted above, not defended
    fclose(f);
}

TEST(output_list_empty_ok)
{
    FILE *f = tmpfile();
    ASSERT_TRUE(f != NULL);
    ASSERT_EQ_INT(output_list_data(f, 0, 0, NULL, NULL, 0, "t"), 0);
    // cppcheck-suppress nullPointerOutOfResources ; test-only: tmpfile()/malloc exhaustion is
    // asserted above, not defended
    fclose(f);
}

TEST(output_preview_multibyte_and_truncate)
{
    FILE *f = tmpfile();
    Entry e;
    char body[400];
    size_t i;
    ASSERT_TRUE(f != NULL);
    memset(&e, 0, sizeof(e));
    e.id = 1;
    e.body = (char *)"café\nmore"; /* utf-8 multi-byte in preview path */
    e.created_at = (char *)"c";
    e.updated_at = (char *)"u";
    ASSERT_EQ_INT(output_entry_human_line(f, &e, NULL, 0, "t"), 0);
    for (i = 0; i < 200U; i++) {
        body[i] = (char)0xC3;   /* start of 2-byte sequence */
        body[++i] = (char)0xA9; /* continuation é */
    }
    body[200] = '\0';
    e.body = body;
    ASSERT_EQ_INT(output_entry_human_line(f, &e, NULL, 0, "t"), 0);
    // cppcheck-suppress nullPointerOutOfResources ; test-only: tmpfile()/malloc exhaustion is
    // asserted above, not defended
    fclose(f);
}

/* ---- cli messages -------------------------------------------------------- */

TEST(cli_error_message_all_codes)
{
    ASSERT_STREQ(cli_error_message(CLI_ERR_OK), "ok");
    ASSERT_STREQ(cli_error_message(CLI_ERR_MISSING_COMMAND), "missing command (try --help)");
    ASSERT_STREQ(cli_error_message(CLI_ERR_UNKNOWN_COMMAND), "unknown command");
    ASSERT_STREQ(cli_error_message(CLI_ERR_UNKNOWN_OPTION), "unknown option");
    ASSERT_STREQ(cli_error_message(CLI_ERR_MISSING_OPTION_VALUE), "option requires a value");
    ASSERT_STREQ(cli_error_message(CLI_ERR_INTERNAL), "internal argument error");
    ASSERT_STREQ(cli_error_message((CliError)99), "unknown error");
}

TEST(cli_command_name_and_summary_unknown)
{
    ASSERT_TRUE(cli_command_name(CLI_CMD_NONE) == NULL);
    ASSERT_TRUE(cli_command_summary(CLI_CMD_NONE) == NULL);
    ASSERT_TRUE(cli_command_name(CLI_CMD_UNKNOWN) == NULL);
}

TEST(cli_parse_double_dash_literal)
{
    CliArgs a;
    char a0[] = "remember";
    char a1[] = "add";
    char a2[] = "--";
    char a3[] = "--tag";
    char a4[] = "body text";
    char *argv[] = {a0, a1, a2, a3, a4, NULL};
    cli_parse(5, argv, &a);
    ASSERT_EQ_INT((int)a.error, (int)CLI_ERR_OK);
    ASSERT_EQ_INT((int)a.command, (int)CLI_CMD_ADD);
    /* After --, --tag is rest, not a global. */
    ASSERT_TRUE(a.rest_argc >= 2);
    cli_args_free(&a);
}

TEST(cli_parse_end_opts_then_subcommand_token)
{
    /* "--" before any subcommand: next token is the command name. */
    CliArgs a;
    char a0[] = "remember";
    char a1[] = "--";
    char a2[] = "version";
    char *argv[] = {a0, a1, a2, NULL};
    cli_parse(3, argv, &a);
    ASSERT_EQ_INT((int)a.error, (int)CLI_ERR_OK);
    ASSERT_EQ_INT((int)a.command, (int)CLI_CMD_VERSION);
    cli_args_free(&a);
}

/* ---- normalize edges ----------------------------------------------------- */

TEST(norm_status_string_unknown)
{
    ASSERT_STREQ(norm_status_string((NormStatus)99), "unknown normalize error");
}

/* ---- black-box: main help + NYI ------------------------------------------ */

TEST(help_update_topic)
{
    char *db = make_temp_db_path();
    const char *args[] = {"help", "update"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "--text");
    ASSERT_STR_CONTAINS(r.out, "--clear-tags");
    cmd_result_free(&r);
    free(db);
}

TEST(update_missing_text_value_rejected)
{
    char *db = make_temp_db_path();
    const char *a[] = {"add", "body"};
    const char *uargs[] = {"update", "1", "--text"};
    CmdResult r;
    CmdResult u;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    cmd_result_free(&r);
    u = run_remember(db, uargs, sizeof(uargs) / sizeof(uargs[0]), NULL);
    ASSERT_EQ_INT(u.exit_code, 1);
    cmd_result_free(&u);
    free(db);
}

TEST(update_invalid_id_token_rejected)
{
    char *db = make_temp_db_path();
    const char *a[] = {"add", "body"};
    const char *uargs[] = {"update", "not-a-number", "--text", "x"};
    CmdResult r;
    CmdResult u;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    cmd_result_free(&r);
    u = run_remember(db, uargs, sizeof(uargs) / sizeof(uargs[0]), NULL);
    ASSERT_EQ_INT(u.exit_code, 1);
    cmd_result_free(&u);
    free(db);
}

TEST(help_get_list_delete_topics)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *hg[] = {"help", "get"};
    const char *hl[] = {"help", "list"};
    const char *hd[] = {"help", "delete"};
    const char *hget[] = {"get", "--help"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, hg, sizeof(hg) / sizeof(hg[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "--key");
    cmd_result_free(&r);
    r = run_remember(db, hl, sizeof(hl) / sizeof(hl[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "--limit");
    cmd_result_free(&r);
    r = run_remember(db, hd, sizeof(hd) / sizeof(hd[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "--key");
    cmd_result_free(&r);
    r = run_remember(db, hget, sizeof(hget) / sizeof(hget[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    free(db);
}

TEST(add_json_body_with_all_json_escapes)
{
    char *db = make_temp_db_path();
    const char *args[] = {"add", "--json", "line\b\f\r\t\"\\ok"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\\b");
    ASSERT_STR_CONTAINS(r.out, "\\f");
    ASSERT_STR_CONTAINS(r.out, "\\r");
    ASSERT_STR_CONTAINS(r.out, "\\t");
    cmd_result_free(&r);
    free(db);
}

TEST(list_invalid_source_rejected)
{
    char *db = make_temp_db_path();
    const char *args[] = {"list", "--created-by", "nope"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "invalid --created-by (use human, agent, tool, share, or import)");
    cmd_result_free(&r);
    free(db);
}

TEST(list_missing_option_values)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *a1[] = {"list", "--tag"};
    const char *a2[] = {"list", "--limit"};
    const char *a3[] = {"list", "--offset"};
    const char *a4[] = {"list", "--key"};
    const char *a5[] = {"list", "--created-by"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a1, sizeof(a1) / sizeof(a1[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, a2, sizeof(a2) / sizeof(a2[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, a3, sizeof(a3) / sizeof(a3[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, a4, sizeof(a4) / sizeof(a4[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, a5, sizeof(a5) / sizeof(a5[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(list_unknown_option_and_positional)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *a1[] = {"list", "--bogus"};
    const char *a2[] = {"list", "positional"};
    const char *a3[] = {"list", "--", "x"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a1, sizeof(a1) / sizeof(a1[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, a2, sizeof(a2) / sizeof(a2[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, a3, sizeof(a3) / sizeof(a3[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(get_key_missing_value_and_unknown_opt)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *a1[] = {"get", "--key"};
    const char *a2[] = {"get", "--nope"};
    const char *a3[] = {"get", "1", "2"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a1, sizeof(a1) / sizeof(a1[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, a2, sizeof(a2) / sizeof(a2[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, a3, sizeof(a3) / sizeof(a3[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(add_too_many_args_and_unknown_opt)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *a1[] = {"add", "one", "two"};
    const char *a2[] = {"add", "--nope", "body"};
    const char *a3[] = {"add", "--tag"};
    const char *a4[] = {"add", "--", "body ok"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a1, sizeof(a1) / sizeof(a1[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, a2, sizeof(a2) / sizeof(a2[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, a3, sizeof(a3) / sizeof(a3[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, a4, sizeof(a4) / sizeof(a4[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    free(db);
}

TEST(list_invalid_limit_and_offset_tokens)
{
    char *db = make_temp_db_path();
    CmdResult r;
    const char *a1[] = {"list", "--limit", "abc"};
    const char *a2[] = {"list", "--offset", "x"};
    const char *a3[] = {"list", "--limit", "1001"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a1, sizeof(a1) / sizeof(a1[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, a2, sizeof(a2) / sizeof(a2[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, a3, sizeof(a3) / sizeof(a3[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(get_invalid_key_token)
{
    char *db = make_temp_db_path();
    const char *args[] = {"get", "--key", "bad\tkey"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(delete_missing_locator)
{
    char *db = make_temp_db_path();
    const char *args[] = {"delete"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(cli_db_equals_missing_value)
{
    /* --db= with empty value → missing option value path */
    CliArgs a;
    char a0[] = "remember";
    char a1[] = "--db=";
    char a2[] = "list";
    char *argv[] = {a0, a1, a2, NULL};
    cli_parse(3, argv, &a);
    /* empty --db= may be missing value or empty path depending on parser */
    ASSERT_TRUE(a.error != CLI_ERR_OK || a.globals.db_path != NULL);
    cli_args_free(&a);
}

TEST(cli_help_with_rest_topic)
{
    CliArgs a;
    char a0[] = "remember";
    char a1[] = "help";
    char a2[] = "add";
    char *argv[] = {a0, a1, a2, NULL};
    cli_parse(3, argv, &a);
    ASSERT_EQ_INT((int)a.error, (int)CLI_ERR_OK);
    ASSERT_EQ_INT((int)a.command, (int)CLI_CMD_HELP);
    cli_args_free(&a);
}

TEST(cli_help_bad_topic_token)
{
    CliArgs a;
    char a0[] = "remember";
    char a1[] = "help";
    char a2[] = "notacommand";
    char *argv[] = {a0, a1, a2, NULL};
    cli_parse(3, argv, &a);
    ASSERT_EQ_INT((int)a.error, (int)CLI_ERR_UNKNOWN_COMMAND);
    cli_args_free(&a);
}

TEST(output_utf8_3_and_4_byte_preview)
{
    FILE *f = tmpfile();
    Entry e;
    /* U+20AC euro is E2 82 AC (3-byte); U+1F600 needs 4-byte f0 9f 98 80 */
    char body[] = {(char)0xE2, (char)0x82, (char)0xAC, ' ', (char)0xF0,
                   (char)0x9F, (char)0x98, (char)0x80, '\0'};
    char bad[] = {(char)0xE2, '\0'}; /* truncated multi-byte → clen=1 path */
    ASSERT_TRUE(f != NULL);
    memset(&e, 0, sizeof(e));
    e.id = 1;
    e.body = body;
    e.created_at = (char *)"c";
    e.updated_at = (char *)"u";
    e.key = (char *)"k";
    e.tags = (char **)malloc(2U * sizeof(char *));
    ASSERT_TRUE(e.tags != NULL);
    e.tags[0] = (char *)"a";
    e.tags[1] = (char *)"b";
    e.ntags = 2U;
    ASSERT_EQ_INT(output_entry_human_line(f, &e, NULL, 0, "t"), 0);
    e.body = bad;
    ASSERT_EQ_INT(output_entry_human_line(f, &e, NULL, 0, "t"), 0);
    free(e.tags);
    // cppcheck-suppress nullPointerOutOfResources ; test-only: tmpfile()/malloc exhaustion is
    // asserted above, not defended
    fclose(f);
}

TEST(output_body_null_human)
{
    FILE *f = tmpfile();
    ASSERT_TRUE(f != NULL);
    ASSERT_EQ_INT(output_body_human(f, NULL), 0);
    // cppcheck-suppress nullPointerOutOfResources ; test-only: tmpfile()/malloc exhaustion is
    // asserted above, not defended
    fclose(f);
}

/* A body may be several lines: \n and \t stay, other controls become '?'. */
TEST(output_body_human_keeps_newline_and_tab)
{
    char *buf = NULL;
    size_t len = 0U;
    FILE *f = open_memstream(&buf, &len);

    ASSERT_TRUE(f != NULL);
    if (f == NULL) {
        return;
    }
    ASSERT_EQ_INT(output_body_human(f, "a\nb\tc\x1b"
                                       "d"),
                  0);
    ASSERT_EQ_INT(fclose(f), 0);
    ASSERT_STREQ(buf, "a\nb\tc?d\n");
    free(buf);
}

TEST(util_read_stdin_empty)
{
    /* Empty stdin → empty buffer path (malloc 1). */
    FILE *old = stdin;
    FILE *empty = tmpfile();
    char *out = NULL;
    size_t len = 99U;
    ASSERT_TRUE(empty != NULL);
    stdin = empty;
    ASSERT_EQ_INT(util_read_stdin(&out, &len, 100), 0);
    ASSERT_TRUE(out != NULL);
    ASSERT_EQ_INT((int)len, 0);
    free(out);
    stdin = old;
    // cppcheck-suppress nullPointerOutOfResources ; test-only: tmpfile()/malloc exhaustion is
    // asserted above, not defended
    fclose(empty);
}

TEST(util_read_stdin_over_cap)
{
    FILE *old = stdin;
    FILE *in = tmpfile();
    char *out = NULL;
    size_t len = 0U;
    int i;
    ASSERT_TRUE(in != NULL);
    if (in == NULL) {
        return;
    }
    for (i = 0; i < 20; i++) {
        ASSERT_TRUE(fputc('x', in) != EOF);
    }
    rewind(in);
    stdin = in;
    ASSERT_EQ_INT(util_read_stdin(&out, &len, 10), -2);
    ASSERT_TRUE(out == NULL);
    stdin = old;
    fclose(in);
}

TEST(add_invalid_utf8_key_message)
{
    char *db = make_temp_db_path();
    char key[] = {(char)0xff, 'k', '\0'};
    const char *args[] = {"add", "--key", key, "body"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "key");
    cmd_result_free(&r);
    free(db);
}

TEST(list_invalid_tag_message)
{
    char *db = make_temp_db_path();
    const char *args[] = {"list", "--tag", "bad\ttag"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(add_second_tag_invalid_frees_first)
{
    /* First tag OK, second invalid → free loop over partial tags_norm. */
    char *db = make_temp_db_path();
    const char *args[] = {"add", "--tag", "good", "--tag", "bad\ttag", "body"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(list_end_opts_then_junk)
{
    char *db = make_temp_db_path();
    const char *args[] = {"list", "--", "junk"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(search_end_opts_then_query)
{
    /*
     * After a subcommand, top-level cli_parse forwards "--" into rest_argv so
     * cmd_search's end-opts path (list_handle_opt kind==1) runs; the following
     * token is the FTS query (not an option).
     */
    char *db = make_temp_db_path();
    CmdResult r;
    const char *a[] = {"add", "body with helix token"};
    const char *s[] = {"search", "--json", "--", "helix"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a, sizeof(a) / sizeof(a[0]), NULL);
    cmd_result_free(&r);
    r = run_remember(db, s, sizeof(s) / sizeof(s[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "helix");
    cmd_result_free(&r);
    free(db);
}

TEST(search_end_opts_extra_positional_rejected)
{
    /* After command-local end-opts, a second positional is an error. */
    char *db = make_temp_db_path();
    const char *args[] = {"search", "--", "helix", "extra"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(help_topic_unknown_name_falls_back)
{
    /* help for a command whose name lookup fails → general help. */
    char *db = make_temp_db_path();
    const char *args[] = {"help", "version"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    free(db);
}

TEST(add_double_dash_end_opts)
{
    char *db = make_temp_db_path();
    const char *args[] = {"add", "--tag", "t", "--", "body after end opts"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    free(db);
}

TEST(output_invalid_utf8_lead_byte)
{
    FILE *f = tmpfile();
    Entry e;
    char body[] = {(char)0xFF, 'x', '\0'}; /* invalid lead → utf8_clen returns 1 */
    ASSERT_TRUE(f != NULL);
    memset(&e, 0, sizeof(e));
    e.id = 1;
    e.body = body;
    e.created_at = (char *)"c";
    e.updated_at = (char *)"u";
    ASSERT_EQ_INT(output_entry_human_line(f, &e, NULL, 0, "t"), 0);
    // cppcheck-suppress nullPointerOutOfResources ; test-only: tmpfile()/malloc exhaustion is
    // asserted above, not defended
    fclose(f);
}

TEST(cli_after_command_double_dash_rest)
{
    CliArgs a;
    char a0[] = "remember";
    char a1[] = "add";
    char a2[] = "--";
    char a3[] = "hello";
    char *argv[] = {a0, a1, a2, a3, NULL};
    cli_parse(4, argv, &a);
    ASSERT_EQ_INT((int)a.error, (int)CLI_ERR_OK);
    ASSERT_TRUE(a.rest_argc >= 1);
    cli_args_free(&a);
}

TEST(norm_token_message_invalid_char_is_key)
{
    /* invalid tag/key char uses is_key branch of message helper (spaces are
       allowed now; a tab is still rejected) */
    char *db = make_temp_db_path();
    const char *args[] = {"list", "--key", "has\ttab"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "key");
    cmd_result_free(&r);
    free(db);
}

#ifdef REMEMBER_TEST_HOOKS
TEST(store_add_null_tag_slots_empty_join)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s;
    Entry e;
    StoreAddAction act;
    const char *tags[] = {NULL, NULL};
    static const char hash[] = "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
    ASSERT_TRUE(db != NULL);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    memset(&e, 0, sizeof(e));
    /* ntags>0 but NULL tag pointers → join_tags_space empty-buffer path */
    ASSERT_EQ_STATUS(store_add(s, "nt", hash, NULL, tags, 2U, "unknown", NULL,
                               "2026-06-15T12:00:00.000Z", &act, &e),
                     STORE_OK);
    store_entry_free(&e);
    store_close(s);
    free(db);
}
#endif

/* The harness drains sqlite3's whole output before pclose: a result longer
   than the pipe buffer must not die of SIGPIPE and fail the calling test. */
TEST(harness_sqlite_reads_a_result_longer_than_the_pipe)
{
    char *db = make_temp_db_path();
    char *first = NULL;

    ASSERT_TRUE(db != NULL);
    first = harness_sqlite_query_line(db, "WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 "
                                          "FROM n WHERE i < 100000) SELECT i FROM n;");
    ASSERT_STREQ(first != NULL ? first : "", "1");
    free(first);
    free(db);
}

void register_coverage_edges_tests(void)
{
    RUN_TEST(harness_sqlite_reads_a_result_longer_than_the_pipe);
    RUN_TEST(util_resolve_null_buffer_fails);
    RUN_TEST(util_resolve_zero_buflen_fails);
    RUN_TEST(util_resolve_empty_env_falls_through_to_home);
    RUN_TEST(util_resolve_path_too_long);
    RUN_TEST(util_resolve_home_missing);
    RUN_TEST(util_resolve_home_empty);
    RUN_TEST(util_resolve_default_path_buffer_too_small);
    RUN_TEST(util_resolve_env_hides_default_db);
    RUN_TEST(util_resolve_cli_db_hides_default_db);
    RUN_TEST(util_resolve_default_db_absolute_line_is_the_brain);
    RUN_TEST(util_resolve_default_db_missing_file_uses_builtin_and_creates_nothing);
    RUN_TEST(util_resolve_default_db_empty_line_uses_builtin_and_creates_nothing);
    RUN_TEST(util_resolve_default_db_relative_line_uses_builtin_and_creates_nothing);
    RUN_TEST(util_resolve_default_db_trims_ascii_whitespace_and_needs_no_newline);
    RUN_TEST(util_resolve_empty_remember_db_does_not_hide_default_db);
    RUN_TEST(util_resolve_default_db_ephemeral_fails_like_cli_db);
    RUN_TEST(util_resolve_default_db_xdg_data_home_does_not_move_it);
    RUN_TEST(util_resolve_default_db_line_too_long_fails);
    RUN_TEST(util_resolve_default_db_read_limit_is_path_max_bytes);
    RUN_TEST(util_resolve_default_db_directory_fails_with_no_fallback);
    RUN_TEST(util_resolve_default_db_symlink_loop_fails_with_no_fallback);
    RUN_TEST(util_resolve_default_db_without_permission_fails_with_no_fallback);
    RUN_TEST(util_resolve_default_db_not_a_regular_file_fails);
    RUN_TEST(util_resolve_default_db_nul_byte_fails);
    RUN_TEST(util_resolve_default_db_that_does_not_exist_uses_builtin);
    RUN_TEST(util_set_err_truncates);
    RUN_TEST(util_read_stdin_null_out);
    RUN_TEST(output_json_string_null_out_fails);
    RUN_TEST(output_json_string_null_s_uses_empty);
    RUN_TEST(output_json_escapes_controls);
    RUN_TEST(output_null_entry_fails);
    RUN_TEST(output_list_empty_ok);
    RUN_TEST(output_preview_multibyte_and_truncate);
    RUN_TEST(cli_error_message_all_codes);
    RUN_TEST(cli_command_name_and_summary_unknown);
    RUN_TEST(cli_parse_double_dash_literal);
    RUN_TEST(cli_parse_end_opts_then_subcommand_token);
    RUN_TEST(norm_status_string_unknown);
    RUN_TEST(help_update_topic);
    RUN_TEST(update_missing_text_value_rejected);
    RUN_TEST(update_invalid_id_token_rejected);
    RUN_TEST(help_get_list_delete_topics);
    RUN_TEST(add_json_body_with_all_json_escapes);
    RUN_TEST(list_invalid_source_rejected);
    RUN_TEST(list_missing_option_values);
    RUN_TEST(list_unknown_option_and_positional);
    RUN_TEST(get_key_missing_value_and_unknown_opt);
    RUN_TEST(add_too_many_args_and_unknown_opt);
    RUN_TEST(list_invalid_limit_and_offset_tokens);
    RUN_TEST(get_invalid_key_token);
    RUN_TEST(delete_missing_locator);
    RUN_TEST(cli_db_equals_missing_value);
    RUN_TEST(cli_help_with_rest_topic);
    RUN_TEST(cli_help_bad_topic_token);
    RUN_TEST(output_utf8_3_and_4_byte_preview);
    RUN_TEST(output_body_null_human);
    RUN_TEST(output_body_human_keeps_newline_and_tab);
    RUN_TEST(util_read_stdin_empty);
    RUN_TEST(util_read_stdin_over_cap);
    RUN_TEST(add_invalid_utf8_key_message);
    RUN_TEST(list_invalid_tag_message);
    RUN_TEST(add_second_tag_invalid_frees_first);
    RUN_TEST(list_end_opts_then_junk);
    RUN_TEST(search_end_opts_then_query);
    RUN_TEST(search_end_opts_extra_positional_rejected);
    RUN_TEST(help_topic_unknown_name_falls_back);
    RUN_TEST(add_double_dash_end_opts);
    RUN_TEST(output_invalid_utf8_lead_byte);
    RUN_TEST(cli_after_command_double_dash_rest);
    RUN_TEST(norm_token_message_invalid_char_is_key);
#ifdef REMEMBER_TEST_HOOKS
    RUN_TEST(store_add_null_tag_slots_empty_join);
#endif
}

// NOLINTEND
