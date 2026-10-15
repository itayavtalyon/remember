#include "appio.h"
#include "commands_common.h"
#include "diag.h"
#include "harness.h"
#include "normalize.h"
#include "register.h"
#include "remember_app.h"
#include "store.h"
#include "test.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Longest E4.3 token plus the JSON envelope around "sample text". */
enum { EXPECTED_JSON_CAP = 160 };

/* E4.3, in catalog order. The emitter's string table must match this list. */
static const char *const k_error_codes[] = {
    "usage",
    "not_found",
    "database",
    "out_of_memory",
    "internal",
    "invalid_query",
    "key_conflict",
    "body_conflict",
    "expired",
    "not_expired",
    "deleted",
    "not_deleted",
    "not_single_device",
    "self_link",
    "cycle",
    "source_too_old",
    "unique_taken",
    "stale",
    "newer_database",
    "unsupported_database",
    "import_parse",
    "busy",
    "read_only",
    "disk_full",
    "cannot_open",
    "not_a_database",
    "corrupt",
    "io_error",
};

/* Unit tests pick the --json-errors mode per call; the flag is cleared after. */
static int diag_error_as(bool json, DiagCode code, const char *message)
{
    int rc = 0;

    diag_set_json_errors(json);
    rc = diag_error(code, message);
    diag_set_json_errors(false);
    return rc;
}

static int diag_warning_as(bool json, DiagWarning code, const char *message)
{
    int rc = 0;

    diag_set_json_errors(json);
    rc = diag_warning(code, message);
    diag_set_json_errors(false);
    return rc;
}

TEST(error_catalog_round_trips_every_e4_3_code)
{
    size_t i = 0;
    size_t count = sizeof(k_error_codes) / sizeof(k_error_codes[0]);

    ASSERT_EQ_INT((int)DIAG_CODE_COUNT, (int)count);
    for (i = 0; i < count; i++) {
        DiagCode code = (DiagCode)i;
        DiagCode parsed = DIAG_CODE_COUNT;

        ASSERT_STREQ(diag_code_name(code), k_error_codes[i]);
        ASSERT_TRUE(diag_code_from_name(k_error_codes[i], &parsed));
        ASSERT_EQ_INT((int)parsed, (int)i);
    }
}

TEST(error_catalog_rejects_names_outside_e4_3)
{
    DiagCode parsed = DIAG_USAGE;

    ASSERT_TRUE(diag_code_name(DIAG_CODE_COUNT) == NULL);
    ASSERT_FALSE(diag_code_from_name(NULL, &parsed));
    ASSERT_EQ_INT((int)parsed, (int)DIAG_USAGE);
    ASSERT_FALSE(diag_code_from_name("usage", NULL));
    ASSERT_FALSE(diag_code_from_name("Usage", &parsed));
    ASSERT_EQ_INT((int)parsed, (int)DIAG_USAGE);
    ASSERT_FALSE(diag_code_from_name("raw_query_ignored", &parsed));
    ASSERT_FALSE(diag_code_from_name("", &parsed));
}

static const char *const k_warning_codes[] = {
    "raw_query_ignored",
    "deprecated_flag",
    "synced_folder",
};

TEST(warning_catalog_round_trips_the_three_codes)
{
    size_t i = 0;
    size_t count = sizeof(k_warning_codes) / sizeof(k_warning_codes[0]);

    ASSERT_EQ_INT((int)DIAG_WARN_COUNT, (int)count);
    for (i = 0; i < count; i++) {
        DiagWarning code = (DiagWarning)i;
        DiagWarning parsed = DIAG_WARN_COUNT;

        ASSERT_STREQ(diag_warning_name(code), k_warning_codes[i]);
        ASSERT_TRUE(diag_warning_from_name(k_warning_codes[i], &parsed));
        ASSERT_EQ_INT((int)parsed, (int)i);
    }
}

TEST(warning_catalog_rejects_names_outside_the_three)
{
    DiagWarning parsed = DIAG_WARN_RAW_QUERY_IGNORED;

    ASSERT_TRUE(diag_warning_name(DIAG_WARN_COUNT) == NULL);
    ASSERT_FALSE(diag_warning_from_name(NULL, &parsed));
    ASSERT_EQ_INT((int)parsed, (int)DIAG_WARN_RAW_QUERY_IGNORED);
    ASSERT_FALSE(diag_warning_from_name("raw_query_ignored", NULL));
    ASSERT_FALSE(diag_warning_from_name("usage", &parsed));
    ASSERT_EQ_INT((int)parsed, (int)DIAG_WARN_RAW_QUERY_IGNORED);
    ASSERT_FALSE(diag_warning_from_name("Raw_query_ignored", &parsed));
}

TEST(plain_error_is_remember_prefix_and_the_message)
{
    char *buf = NULL;
    size_t len = 0U;
    FILE *err = open_memstream(&buf, &len);

    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        return;
    }
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_error_as(false, DIAG_USAGE, "missing command (try --help)"), 0);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf, "remember: missing command (try --help)\n");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
}

TEST(json_error_is_one_line_and_not_the_plain_line)
{
    char *buf = NULL;
    size_t len = 0U;
    FILE *err = open_memstream(&buf, &len);

    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        return;
    }
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_error_as(true, DIAG_NOT_FOUND, "not found"), 0);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf,
                 "{\"version\":1,\"error\":{\"code\":\"not_found\",\"message\":\"not found\"}}\n");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
}

TEST(error_outside_the_catalog_writes_nothing)
{
    char *buf = NULL;
    size_t len = 0U;
    FILE *err = open_memstream(&buf, &len);

    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        return;
    }
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_error_as(false, DIAG_CODE_COUNT, "missing command (try --help)"), -1);
    ASSERT_EQ_INT(diag_error_as(true, DIAG_CODE_COUNT, "missing command (try --help)"), -1);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf, "");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
}

TEST(json_error_escapes_message_bytes)
{
    char *buf = NULL;
    size_t len = 0U;
    FILE *err = open_memstream(&buf, &len);
    const char message[] = {'a', '"', 'b', '\\', 'c', '\n', '\x01', '\0'};

    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        return;
    }
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_error_as(true, DIAG_USAGE, message), 0);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(
        buf,
        "{\"version\":1,\"error\":{\"code\":\"usage\",\"message\":\"a\\\"b\\\\c\\n\\u0001\"}}\n");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
}

TEST(every_error_code_json_message_matches_the_plain_line)
{
    size_t i = 0;
    const char *sample = "sample text";

    for (i = 0; i < (size_t)DIAG_CODE_COUNT; i++) {
        char *plain_buf = NULL;
        char *json_buf = NULL;
        size_t plain_len = 0U;
        size_t json_len = 0U;
        FILE *plain = open_memstream(&plain_buf, &plain_len);
        FILE *json = open_memstream(&json_buf, &json_len);
        char expected_json[EXPECTED_JSON_CAP];
        DiagCode code = (DiagCode)i;
        int wrote = 0;

        ASSERT_TRUE(plain != NULL);
        ASSERT_TRUE(json != NULL);
        if (plain == NULL || json == NULL) {
            if (plain != NULL) {
                (void)fclose(plain);
            }
            if (json != NULL) {
                (void)fclose(json);
            }
            free(plain_buf);
            free(json_buf);
            return;
        }
        app_set_streams((AppStreams){.out = NULL, .err = plain});
        ASSERT_EQ_INT(diag_error_as(false, code, sample), 0);
        app_set_streams((AppStreams){.out = NULL, .err = json});
        ASSERT_EQ_INT(diag_error_as(true, code, sample), 0);
        ASSERT_EQ_INT(fflush(plain), 0);
        ASSERT_EQ_INT(fflush(json), 0);
        ASSERT_STREQ(plain_buf, "remember: sample text\n");
        wrote =
            snprintf(expected_json, sizeof(expected_json),
                     "{\"version\":1,\"error\":{\"code\":\"%s\",\"message\":\"sample text\"}}\n",
                     k_error_codes[i]);
        ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(expected_json));
        ASSERT_STREQ(json_buf, expected_json);
        app_set_streams((AppStreams){.out = NULL, .err = NULL});
        ASSERT_EQ_INT(fclose(plain), 0);
        ASSERT_EQ_INT(fclose(json), 0);
        free(plain_buf);
        free(json_buf);
    }
}

TEST(plain_warning_is_remember_warning)
{
    char *buf = NULL;
    size_t len = 0U;
    FILE *err = open_memstream(&buf, &len);

    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        return;
    }
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_warning_as(false, DIAG_WARN_RAW_QUERY_IGNORED, "x"), 0);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf, "remember: warning: x\n");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
}

TEST(json_warning_is_one_line_without_the_prefix)
{
    char *buf = NULL;
    size_t len = 0U;
    FILE *err = open_memstream(&buf, &len);

    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        return;
    }
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_warning_as(true, DIAG_WARN_DEPRECATED_FLAG, "x"), 0);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf,
                 "{\"version\":1,\"warning\":{\"code\":\"deprecated_flag\",\"message\":\"x\"}}\n");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
}

/* deprecated_flag and synced_folder keep their 0.2.0 text after "warning: "
   (Itay 2026-10-15). An unknown warning code writes nothing. */
TEST(every_warning_uses_the_warning_prefix)
{
    char *buf = NULL;
    size_t len = 0U;
    FILE *err = open_memstream(&buf, &len);

    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        return;
    }
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(
        diag_warning_as(false, DIAG_WARN_DEPRECATED_FLAG, "--trash is deprecated; use --expired"),
        0);
    ASSERT_EQ_INT(
        diag_warning_as(false, DIAG_WARN_SYNCED_FOLDER, "database path looks like a synced folder"),
        0);
    ASSERT_EQ_INT(diag_warning_as(false, DIAG_WARN_COUNT, "x"), -1);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf, "remember: warning: --trash is deprecated; use --expired\n"
                      "remember: warning: database path looks like a synced folder\n");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
}

TEST(null_message_is_empty_text)
{
    char *buf = NULL;
    size_t len = 0U;
    FILE *err = open_memstream(&buf, &len);

    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        return;
    }
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_error_as(false, DIAG_INTERNAL, NULL), 0);
    ASSERT_EQ_INT(diag_error_as(true, DIAG_INTERNAL, NULL), 0);
    ASSERT_EQ_INT(diag_warning_as(false, DIAG_WARN_SYNCED_FOLDER, NULL), 0);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf, "remember: \n"
                      "{\"version\":1,\"error\":{\"code\":\"internal\",\"message\":\"\"}}\n"
                      "remember: warning: \n");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
}

TEST(plain_error_keeps_quote_and_backslash)
{
    char *buf = NULL;
    size_t len = 0U;
    FILE *err = open_memstream(&buf, &len);

    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        return;
    }
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_error_as(false, DIAG_USAGE, "say \"hi\"\\"), 0);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf, "remember: say \"hi\"\\\n");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
}

TEST(error_and_warning_leave_stdout_empty)
{
    char *err_buf = NULL;
    char *out_buf = NULL;
    size_t err_len = 0U;
    size_t out_len = 0U;
    FILE *err = open_memstream(&err_buf, &err_len);
    FILE *out = open_memstream(&out_buf, &out_len);

    ASSERT_TRUE(err != NULL);
    ASSERT_TRUE(out != NULL);
    if (err == NULL || out == NULL) {
        if (err != NULL) {
            (void)fclose(err);
        }
        if (out != NULL) {
            (void)fclose(out);
        }
        free(err_buf);
        free(out_buf);
        return;
    }
    app_set_streams((AppStreams){.out = out, .err = err});
    ASSERT_EQ_INT(diag_error_as(false, DIAG_USAGE, "missing command (try --help)"), 0);
    ASSERT_EQ_INT(diag_warning_as(true, DIAG_WARN_RAW_QUERY_IGNORED, "x"), 0);
    ASSERT_EQ_INT(fflush(out), 0);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(out_buf, "");
    ASSERT_STREQ(
        err_buf,
        "remember: missing command (try --help)\n"
        "{\"version\":1,\"warning\":{\"code\":\"raw_query_ignored\",\"message\":\"x\"}}\n");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(err), 0);
    ASSERT_EQ_INT(fclose(out), 0);
    free(err_buf);
    free(out_buf);
}

TEST(warning_outside_the_catalog_writes_nothing)
{
    char *buf = NULL;
    size_t len = 0U;
    FILE *err = open_memstream(&buf, &len);

    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        return;
    }
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_warning_as(false, DIAG_WARN_COUNT, "x"), -1);
    ASSERT_EQ_INT(diag_warning_as(true, DIAG_WARN_COUNT, "x"), -1);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf, "");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
}

TEST(plain_missing_command_is_unchanged)
{
    char *db = make_temp_db_path();
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, NULL, 0, NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, "remember: missing command (try --help)\n");
    cmd_result_free(&r);
    free(db);
}

TEST(json_errors_usage_is_one_stderr_line)
{
    char *db = make_temp_db_path();
    const char *args[] = {"--json-errors"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, "{\"version\":1,\"error\":{\"code\":\"usage\",\"message\":\"missing "
                        "command (try --help)\"}}\n");
    cmd_result_free(&r);
    free(db);
}

TEST(json_errors_not_found_is_one_line)
{
    char *db = make_temp_db_path();
    const char *args[] = {"get", "--json-errors", "--key", "nope"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 2);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err,
                 "{\"version\":1,\"error\":{\"code\":\"not_found\",\"message\":\"not found\"}}\n");
    cmd_result_free(&r);
    free(db);
}

TEST(plain_not_found_is_unchanged)
{
    char *db = make_temp_db_path();
    const char *args[] = {"get", "--key", "nope"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 2);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, "remember: not found\n");
    cmd_result_free(&r);
    free(db);
}

TEST(plain_trash_warning_uses_warning_prefix)
{
    char *db = make_temp_db_path();
    const char *args[] = {"list", "--trash"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "remember: warning: --trash is deprecated; use --expired\n");
    cmd_result_free(&r);
    free(db);
}

TEST(json_errors_trash_is_deprecated_flag)
{
    char *db = make_temp_db_path();
    const char *args[] = {"--json-errors", "list", "--trash"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "{\"version\":1,\"warning\":{\"code\":\"deprecated_flag\",\"message\":\"--"
                        "trash is deprecated; use --expired\"}}\n");
    cmd_result_free(&r);
    free(db);
}

TEST(json_errors_invalid_search_query_keeps_its_text)
{
    char *db = make_temp_db_path();
    const char *args[] = {"--json-errors", "search", "\""};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err,
                 "{\"version\":1,\"error\":{\"code\":\"invalid_query\",\"message\":\"invalid "
                 "search query\"}}\n");
    cmd_result_free(&r);
    free(db);
}

TEST(json_errors_synced_folder_is_one_warning)
{
    enum { PATH_CAP = 4096 };
    char tmpl[] = "/tmp/Dropbox-remember-XXXXXX";
    char *dir = mkdtemp(tmpl);
    char path[PATH_CAP];
    char sidecar[PATH_CAP];
    const char *args[] = {"--json-errors", "list"};
    CmdResult r;
    int wrote = 0;

    ASSERT_TRUE(dir != NULL);
    if (dir == NULL) {
        return;
    }
    wrote = snprintf(path, sizeof(path), "%s/test.db", dir);
    ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(path));
    r = run_remember(path, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err,
                 "{\"version\":1,\"warning\":{\"code\":\"synced_folder\",\"message\":\"database "
                 "path looks like a synced folder "
                 "(iCloud/Dropbox/Google Drive); corruption risk - prefer a local disk\"}}\n");
    cmd_result_free(&r);
    wrote = snprintf(sidecar, sizeof(sidecar), "%s.device_id", path);
    ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(sidecar));
    (void)unlink(path);
    (void)unlink(sidecar);
    (void)rmdir(dir);
}

static void expect_diag(DiagCode got, DiagCode want)
{
    ASSERT_EQ_INT((int)got, (int)want);
}

TEST(store_text_codes_follow_the_locked_split)
{
    expect_diag(code_for_store_text("sqlite: disk I/O error"), DIAG_DATABASE);
    expect_diag(code_for_store_text("cannot read user_version: bad"), DIAG_DATABASE);
    expect_diag(code_for_store_text("cannot rebuild search index"), DIAG_DATABASE);
    expect_diag(code_for_store_text("cannot open database"), DIAG_DATABASE);
    expect_diag(code_for_store_text("cannot open database: unable to open"), DIAG_DATABASE);
    expect_diag(code_for_store_text("cannot create database directory: denied"), DIAG_CANNOT_OPEN);
    expect_diag(code_for_store_text("database path component is not a directory"),
                DIAG_CANNOT_OPEN);
    expect_diag(code_for_store_text("invalid database path"), DIAG_USAGE);
    expect_diag(code_for_store_text("database path is too long"), DIAG_USAGE);
    expect_diag(code_for_store_text("empty database path"), DIAG_USAGE);
    expect_diag(code_for_store_text("HOME is not set; pass --db PATH"), DIAG_USAGE);
    expect_diag(
        code_for_store_text("database path must be a regular file (not :memory: or file: URI)"),
        DIAG_USAGE);
    expect_diag(code_for_store_text("out of memory"), DIAG_OUT_OF_MEMORY);
    expect_diag(code_for_store_text("database is newer than this remember"), DIAG_NEWER_DATABASE);
    expect_diag(code_for_store_text("unsupported database version"), DIAG_UNSUPPORTED_DATABASE);
    expect_diag(code_for_store_text("migration left a dangling reference"), DIAG_CORRUPT);
    expect_diag(code_for_store_text("internal error"), DIAG_INTERNAL);
    expect_diag(code_for_store_text("internal error: path buffer missing"), DIAG_INTERNAL);
    expect_diag(code_for_store_text("cannot write device_id sidecar"), DIAG_INTERNAL);
    expect_diag(code_for_store_text("invalid device_id sidecar"), DIAG_INTERNAL);
    expect_diag(code_for_store_text("cannot mint device_id"), DIAG_INTERNAL);
    expect_diag(code_for_store_text("cannot mint sync_id"), DIAG_INTERNAL);
    expect_diag(code_for_store_text(NULL), DIAG_INTERNAL);
    expect_diag(code_for_store_text(""), DIAG_INTERNAL);
    expect_diag(code_for_store_text("not a known store sentence"), DIAG_INTERNAL);
    expect_diag(code_for_cmd_text("missing body"), DIAG_USAGE);
    expect_diag(code_for_cmd_text("out of memory"), DIAG_OUT_OF_MEMORY);
    expect_diag(code_for_cmd_text("failed to read body from stdin"), DIAG_INTERNAL);
    expect_diag(code_for_norm(NORM_ERR_OOM), DIAG_OUT_OF_MEMORY);
    expect_diag(code_for_norm(NORM_ERR_EMPTY), DIAG_USAGE);
    expect_diag(code_for_store_status(STORE_OK), DIAG_INTERNAL);
    expect_diag(code_for_store_status(STORE_ERR_NOT_FOUND), DIAG_NOT_FOUND);
    expect_diag(code_for_store_status(STORE_ERR_SQLITE), DIAG_DATABASE);
    expect_diag(code_for_store_status(STORE_ERR_OOM), DIAG_OUT_OF_MEMORY);
    expect_diag(code_for_store_status(STORE_ERR_INTERNAL), DIAG_INTERNAL);
    expect_diag(code_for_store_status(STORE_ERR_QUERY), DIAG_INVALID_QUERY);
    expect_diag(code_for_store_status(STORE_ERR_CONFLICT), DIAG_BODY_CONFLICT);
    expect_diag(code_for_store_status(STORE_ERR_EXPIRED), DIAG_EXPIRED);
    expect_diag(code_for_store_status(STORE_ERR_NOT_EXPIRED), DIAG_NOT_EXPIRED);
    expect_diag(code_for_store_status(STORE_ERR_DELETED), DIAG_DELETED);
    expect_diag(code_for_store_status(STORE_ERR_NOT_DELETED), DIAG_NOT_DELETED);
    expect_diag(code_for_store_status(STORE_ERR_NOT_SINGLE_DEVICE), DIAG_NOT_SINGLE_DEVICE);
    expect_diag(code_for_store_status(STORE_ERR_SELF_LINK), DIAG_SELF_LINK);
    expect_diag(code_for_store_status(STORE_ERR_CYCLE), DIAG_CYCLE);
    expect_diag(code_for_store_status(STORE_ERR_SOURCE_TOO_OLD), DIAG_SOURCE_TOO_OLD);
    expect_diag(code_for_store_status(STORE_ERR_SOURCE_TOO_NEW), DIAG_NEWER_DATABASE);
    expect_diag(code_for_store_status(STORE_ERR_UNIQUE_TAKEN), DIAG_UNIQUE_TAKEN);
    {
        enum { UNKNOWN_STORE_STATUS = 1000 };
        /* NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) */
        StoreStatus unknown = (StoreStatus)UNKNOWN_STORE_STATUS;

        expect_diag(code_for_store_status(unknown), DIAG_INTERNAL);
    }
}

TEST(internal_parse_stays_a_plain_internal_line)
{
    char *err_buf = NULL;
    char *out_buf = NULL;
    size_t err_len = 0U;
    size_t out_len = 0U;
    FILE *err = open_memstream(&err_buf, &err_len);
    FILE *out = open_memstream(&out_buf, &out_len);
    int rc = 0;

    ASSERT_TRUE(err != NULL);
    ASSERT_TRUE(out != NULL);
    if (err == NULL || out == NULL) {
        if (err != NULL) {
            (void)fclose(err);
        }
        if (out != NULL) {
            (void)fclose(out);
        }
        free(err_buf);
        free(out_buf);
        return;
    }
    rc = remember_run(0, NULL, out, err);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_EQ_INT(fflush(out), 0);
    ASSERT_EQ_INT(rc, 1);
    ASSERT_STREQ(out_buf, "");
    ASSERT_STREQ(err_buf, "remember: internal argument error\n");
    ASSERT_EQ_INT(fclose(err), 0);
    ASSERT_EQ_INT(fclose(out), 0);
    free(err_buf);
    free(out_buf);
}

TEST(err_fmt_keeps_a_long_unknown_option)
{
    enum {
        OPT_LEN = 600, /* longer than any fixed stderr scratch buffer */
        EXPECT_SLACK = 64
    };
    char *err_buf = NULL;
    size_t err_len = 0U;
    FILE *err = NULL;
    char long_opt[OPT_LEN];
    char expect[OPT_LEN + EXPECT_SLACK];
    int wrote = 0;

    memset(long_opt, 'a', sizeof(long_opt) - 1U);
    long_opt[sizeof(long_opt) - 1U] = '\0';
    err = open_memstream(&err_buf, &err_len);
    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        return;
    }
    app_set_streams((AppStreams){.out = NULL, .err = err});
    diag_set_json_errors(false);
    err_fmt(DIAG_USAGE, "unknown option '%s'", long_opt);
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_EQ_INT(fclose(err), 0);
    wrote = snprintf(expect, sizeof(expect), "remember: unknown option '%s'\n", long_opt);
    ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(expect));
    ASSERT_STREQ(err_buf, expect);
    free(err_buf);
}

enum { GOLDEN_MAX_ARGS = 6, GOLDEN_STEP_ARGS = 4, GOLDEN_LINE_BUF = 512 };

/* One golden row: argv after --db, the 0.2.0 message, its exit, its D9 code. */
typedef struct {
    const char *args[GOLDEN_MAX_ARGS];
    const char *message;
    const char *code;
    int exit_code;
    /* NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers) */
    char pad_[4]; /* explicit tail padding (kept -Wpadded-clean) */
} GoldenError;

/* Count leading non-NULL args (rows are NULL-padded). */
static size_t args_count(const char *const *args, size_t cap)
{
    size_t n = 0;

    while (n < cap && args[n] != NULL) {
        n++;
    }
    return n;
}

static void given_golden_brain(const char *db)
{
    const char *const steps[][GOLDEN_STEP_ARGS] = {
        {"add", "--key", "k1", "one"},   {"add", "--key", "k2", "two"},
        {"delete", "--key", "k2", NULL}, {"add", "gamma", NULL, NULL},
        {"add", "delta", NULL, NULL},
    };
    size_t i = 0;

    for (i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        CmdResult r =
            run_remember(db, steps[i], args_count(steps[i], (size_t)GOLDEN_STEP_ARGS), NULL);

        ASSERT_EQ_INT(r.exit_code, 0);
        cmd_result_free(&r);
    }
}

/* E4.2 golden + D9 table: plain stderr is the 0.2.0 line byte for byte, and
   --json-errors gives the D9 code with the same message, one line, empty stdout. */
TEST(golden_error_lines_and_d9_codes)
{
    static const GoldenError rows[] = {
        {{"bogus"}, "unknown command 'bogus'", "usage", 1, {0}},
        {{"--bogus", "list"}, "unknown option '--bogus'", "usage", 1, {0}},
        {{"list", "--limit", "0"}, "invalid --limit (must be 1..1000)", "usage", 1, {0}},
        {{"get", "abc"}, "invalid id", "usage", 1, {0}},
        {{"add", "--source", "martian", "x"},
         "invalid source (use human, agent, tool, share, or unknown)",
         "usage",
         1,
         {0}},
        {{"add", "--key", "", "x"}, "empty key", "usage", 1, {0}},
        {{"conflict", "accept", "--id", "1", "--keep", "sideways"},
         "--keep must be local, incoming, or both",
         "usage",
         1,
         {0}},
        {{"device", "extra"}, "device takes 'new' or no arguments", "usage", 1, {0}},
        {{"get", "999"}, "not found", "not_found", 2, {0}},
        {{"get", "--key", "k2"}, "deleted", "deleted", 3, {0}},
        {{"get", "--deleted", "--key", "k1"}, "not_deleted", "not_deleted", 3, {0}},
        {{"search", "\""}, "invalid search query", "invalid_query", 1, {0}},
        {{"link", "1", "1"}, "self-link", "self_link", 1, {0}},
        {{"rekey", "--key", "k1", "--to-key", "k2"},
         "key conflicts with entry 2",
         "key_conflict",
         1,
         {0}},
        {{"update", "4", "--text", "gamma"},
         "body hash conflicts with entry 3",
         "body_conflict",
         1,
         {0}},
        {{"import", "--from-db", "/nonexistent/remember-golden.db"},
         "database error",
         "database",
         1,
         {0}},
    };
    char *db = make_temp_db_path();
    size_t i = 0;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    given_golden_brain(db);
    for (i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        const GoldenError *row = &rows[i];
        const char *json_args[GOLDEN_MAX_ARGS + 1] = {"--json-errors"};
        char want[GOLDEN_LINE_BUF];
        size_t n = args_count(row->args, (size_t)GOLDEN_MAX_ARGS);
        size_t j = 0;
        CmdResult r = run_remember(db, row->args, n, NULL);

        (void)snprintf(want, sizeof(want), "remember: %s\n", row->message);
        ASSERT_STREQ(r.err, want);
        ASSERT_EQ_INT(r.exit_code, row->exit_code);
        ASSERT_STREQ(r.out, "");
        cmd_result_free(&r);

        for (j = 0; j < n; j++) {
            json_args[j + 1U] = row->args[j];
        }
        r = run_remember(db, json_args, n + 1U, NULL);
        (void)snprintf(want, sizeof(want),
                       "{\"version\":1,\"error\":{\"code\":\"%s\",\"message\":\"%s\"}}\n",
                       row->code, row->message);
        ASSERT_STREQ(r.err, want);
        ASSERT_EQ_INT(r.exit_code, row->exit_code);
        ASSERT_STREQ(r.out, "");
        cmd_result_free(&r);
    }
    free(db);
}

void register_errors_tests(void)
{
    RUN_TEST(error_catalog_round_trips_every_e4_3_code);
    RUN_TEST(error_catalog_rejects_names_outside_e4_3);
    RUN_TEST(warning_catalog_round_trips_the_three_codes);
    RUN_TEST(warning_catalog_rejects_names_outside_the_three);
    RUN_TEST(plain_error_is_remember_prefix_and_the_message);
    RUN_TEST(json_error_is_one_line_and_not_the_plain_line);
    RUN_TEST(error_outside_the_catalog_writes_nothing);
    RUN_TEST(json_error_escapes_message_bytes);
    RUN_TEST(every_error_code_json_message_matches_the_plain_line);
    RUN_TEST(plain_warning_is_remember_warning);
    RUN_TEST(json_warning_is_one_line_without_the_prefix);
    RUN_TEST(every_warning_uses_the_warning_prefix);
    RUN_TEST(null_message_is_empty_text);
    RUN_TEST(plain_error_keeps_quote_and_backslash);
    RUN_TEST(error_and_warning_leave_stdout_empty);
    RUN_TEST(warning_outside_the_catalog_writes_nothing);
    RUN_TEST(plain_missing_command_is_unchanged);
    RUN_TEST(json_errors_usage_is_one_stderr_line);
    RUN_TEST(json_errors_not_found_is_one_line);
    RUN_TEST(plain_not_found_is_unchanged);
    RUN_TEST(plain_trash_warning_uses_warning_prefix);
    RUN_TEST(json_errors_trash_is_deprecated_flag);
    RUN_TEST(json_errors_invalid_search_query_keeps_its_text);
    RUN_TEST(json_errors_synced_folder_is_one_warning);
    RUN_TEST(store_text_codes_follow_the_locked_split);
    RUN_TEST(err_fmt_keeps_a_long_unknown_option);
    RUN_TEST(internal_parse_stays_a_plain_internal_line);
    RUN_TEST(golden_error_lines_and_d9_codes);
}
