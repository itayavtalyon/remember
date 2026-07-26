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
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Extra edges for production line coverage: util/output/cli unit paths and
 * black-box NYI/help topics. Keep this suite small and intentional.
 */

/* ---- util ---------------------------------------------------------------- */

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
    char buf[REMEMBER_PATH_MAX];
    char err[64];
    const char *home = getenv("HOME");
    ASSERT_TRUE(home != NULL && home[0] != '\0');
    ASSERT_EQ_INT(setenv("REMEMBER_DB", "", 1), 0);
    ASSERT_EQ_INT(util_resolve_db_path(NULL, buf, sizeof(buf), err, sizeof(err)), 0);
    ASSERT_STR_CONTAINS(buf, "/.remember/remember.db");
    unsetenv("REMEMBER_DB");
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

static char *dup_home_or_null(void)
{
    const char *h = getenv("HOME");
    char *saved;
    size_t n;
    if (h == NULL) {
        return NULL;
    }
    n = strlen(h);
    saved = malloc(n + 1U);
    if (saved == NULL) {
        return NULL;
    }
    memcpy(saved, h, n + 1U);
    return saved;
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

static FILE *open_write_fail(void)
{
    FILE *f = NULL;
    int p[2];

#ifdef __linux__
    f = fopen("/dev/full", "w");
    if (f != NULL) {
        return f;
    }
#endif
    /* Portable: write end of pipe with read end closed → EPIPE after ignore. */
    if (pipe(p) != 0) {
        return NULL;
    }
    close(p[0]);
    signal(SIGPIPE, SIG_IGN);
    f = fdopen(p[1], "w");
    if (f == NULL) {
        close(p[1]);
        return NULL;
    }
    return f;
}

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
    got = malloc((size_t)n + 1U);
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

TEST(output_write_fail_returns_error)
{
    FILE *f = open_write_fail();
    Entry e;
    ASSERT_TRUE(f != NULL);
    memset(&e, 0, sizeof(e));
    e.id = 1;
    e.body = (char *)"x";
    e.source = (char *)"unknown";
    e.created_at = (char *)"t";
    e.updated_at = (char *)"t";
    /* Force a large write so a closed-pipe or /dev/full fails. */
    {
        int i;
        for (i = 0; i < 10000; i++) {
            if (output_json_string(f, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx") != 0) {
                break;
            }
        }
        ASSERT_TRUE(i < 10000);
    }
    (void)output_entry_json(f, &e);
    (void)output_action_envelope(f, "created", &e);
    (void)output_get_envelope(f, &e);
    (void)output_list_envelope(f, 0, 20, 1, 1, &e);
    (void)output_id_human(f, 1);
    (void)output_body_human(f, "body");
    (void)output_entry_human_line(f, &e);
    fclose(f);
}

TEST(output_null_entry_fails)
{
    FILE *f = tmpfile();
    ASSERT_TRUE(f != NULL);
    ASSERT_EQ_INT(output_entry_json(f, NULL), -1);
    ASSERT_EQ_INT(output_action_envelope(f, "x", NULL), -1);
    ASSERT_EQ_INT(output_get_envelope(f, NULL), -1);
    ASSERT_EQ_INT(output_entry_human_line(f, NULL), -1);
    ASSERT_EQ_INT(output_list_envelope(f, 0, 1, 1, 1, NULL), -1);
    fclose(f);
}

TEST(output_list_empty_ok)
{
    FILE *f = tmpfile();
    ASSERT_TRUE(f != NULL);
    ASSERT_EQ_INT(output_list_envelope(f, 0, 20, 0, 0, NULL), 0);
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
    e.source = (char *)"unknown";
    e.created_at = (char *)"c";
    e.updated_at = (char *)"u";
    ASSERT_EQ_INT(output_entry_human_line(f, &e), 0);
    for (i = 0; i < 200U; i++) {
        body[i] = (char)0xC3;   /* start of 2-byte sequence */
        body[++i] = (char)0xA9; /* continuation é */
    }
    body[200] = '\0';
    e.body = body;
    ASSERT_EQ_INT(output_entry_human_line(f, &e), 0);
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

TEST(search_nyi_exits_three)
{
    char *db = make_temp_db_path();
    const char *args[] = {"search", "foo"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 3);
    ASSERT_STR_CONTAINS(r.err, "not implemented");
    cmd_result_free(&r);
    free(db);
}

TEST(update_nyi_exits_three)
{
    char *db = make_temp_db_path();
    const char *args[] = {"update", "1", "--text", "x"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 3);
    cmd_result_free(&r);
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
    r = run_remember(db, hg, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "--key");
    cmd_result_free(&r);
    r = run_remember(db, hl, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "--limit");
    cmd_result_free(&r);
    r = run_remember(db, hd, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "--key");
    cmd_result_free(&r);
    r = run_remember(db, hget, 2, NULL);
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
    r = run_remember(db, args, 3, NULL);
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
    const char *args[] = {"list", "--source", "nope"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "invalid source");
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
    const char *a5[] = {"list", "--source"};
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, a1, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, a2, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, a3, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, a4, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, a5, 2, NULL);
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
    r = run_remember(db, a1, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, a2, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, a3, 3, NULL);
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
    r = run_remember(db, a1, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, a2, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, a3, 3, NULL);
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
    r = run_remember(db, a1, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, a2, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, a3, 2, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, a4, 3, NULL);
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
    r = run_remember(db, a1, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, a2, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    r = run_remember(db, a3, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(get_invalid_key_token)
{
    char *db = make_temp_db_path();
    const char *args[] = {"get", "--key", "bad key"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 3, NULL);
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
    r = run_remember(db, args, 1, NULL);
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
    e.source = (char *)"unknown";
    e.created_at = (char *)"c";
    e.updated_at = (char *)"u";
    e.key = (char *)"k";
    e.tags = (char **)malloc(2U * sizeof(char *));
    ASSERT_TRUE(e.tags != NULL);
    e.tags[0] = (char *)"a";
    e.tags[1] = (char *)"b";
    e.ntags = 2U;
    ASSERT_EQ_INT(output_entry_human_line(f, &e), 0);
    e.body = bad;
    ASSERT_EQ_INT(output_entry_human_line(f, &e), 0);
    free(e.tags);
    fclose(f);
}

TEST(output_body_null_human)
{
    FILE *f = tmpfile();
    ASSERT_TRUE(f != NULL);
    ASSERT_EQ_INT(output_body_human(f, NULL), 0);
    fclose(f);
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
    r = run_remember(db, args, 4, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "key");
    cmd_result_free(&r);
    free(db);
}

TEST(list_invalid_tag_message)
{
    char *db = make_temp_db_path();
    const char *args[] = {"list", "--tag", "bad tag"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    cmd_result_free(&r);
    free(db);
}

TEST(add_second_tag_invalid_frees_first)
{
    /* First tag OK, second invalid → free loop over partial tags_norm. */
    char *db = make_temp_db_path();
    const char *args[] = {"add", "--tag", "good", "--tag", "bad tag", "body"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 6, NULL);
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
    r = run_remember(db, args, 3, NULL);
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
    r = run_remember(db, args, 2, NULL);
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
    r = run_remember(db, args, 5, NULL);
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
    e.source = (char *)"unknown";
    e.created_at = (char *)"c";
    e.updated_at = (char *)"u";
    ASSERT_EQ_INT(output_entry_human_line(f, &e), 0);
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
    /* invalid tag/key char uses is_key branch of message helper */
    char *db = make_temp_db_path();
    const char *args[] = {"list", "--key", "has space"};
    CmdResult r;
    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, 3, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STR_CONTAINS(r.err, "key");
    cmd_result_free(&r);
    free(db);
}

#ifdef REMEMBER_TEST_HOOKS
TEST(store_add_null_tag_slots_empty_join)
{
    char *db = make_temp_db_path();
    char err[256];
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
    ASSERT_EQ_INT((int)store_add(s, "nt", hash, NULL, tags, 2U, "unknown", &act, &e),
                  (int)STORE_OK);
    store_entry_free(&e);
    store_close(s);
    free(db);
}
#endif

void register_coverage_edges_tests(void)
{
    RUN_TEST(util_resolve_null_buffer_fails);
    RUN_TEST(util_resolve_zero_buflen_fails);
    RUN_TEST(util_resolve_empty_env_falls_through_to_home);
    RUN_TEST(util_resolve_path_too_long);
    RUN_TEST(util_resolve_home_missing);
    RUN_TEST(util_resolve_home_empty);
    RUN_TEST(util_resolve_default_path_buffer_too_small);
    RUN_TEST(util_set_err_truncates);
    RUN_TEST(util_read_stdin_null_out);
    RUN_TEST(output_json_string_null_out_fails);
    RUN_TEST(output_json_string_null_s_uses_empty);
    RUN_TEST(output_json_escapes_controls);
    RUN_TEST(output_write_fail_returns_error);
    RUN_TEST(output_null_entry_fails);
    RUN_TEST(output_list_empty_ok);
    RUN_TEST(output_preview_multibyte_and_truncate);
    RUN_TEST(cli_error_message_all_codes);
    RUN_TEST(cli_command_name_and_summary_unknown);
    RUN_TEST(cli_parse_double_dash_literal);
    RUN_TEST(cli_parse_end_opts_then_subcommand_token);
    RUN_TEST(norm_status_string_unknown);
    RUN_TEST(search_nyi_exits_three);
    RUN_TEST(update_nyi_exits_three);
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
    RUN_TEST(util_read_stdin_empty);
    RUN_TEST(util_read_stdin_over_cap);
    RUN_TEST(add_invalid_utf8_key_message);
    RUN_TEST(list_invalid_tag_message);
    RUN_TEST(add_second_tag_invalid_frees_first);
    RUN_TEST(list_end_opts_then_junk);
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
