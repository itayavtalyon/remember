#include "appio.h"
#include "diag.h"
#include "register.h"
#include "test.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

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
    ASSERT_EQ_INT(diag_error(DIAG_USAGE, "missing command (try --help)", false), 0);
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
    ASSERT_EQ_INT(diag_error(DIAG_NOT_FOUND, "not found", true), 0);
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
    ASSERT_EQ_INT(diag_error(DIAG_CODE_COUNT, "missing command (try --help)", false), -1);
    ASSERT_EQ_INT(diag_error(DIAG_CODE_COUNT, "missing command (try --help)", true), -1);
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
    ASSERT_EQ_INT(diag_error(DIAG_USAGE, message, true), 0);
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
        ASSERT_EQ_INT(diag_error(code, sample, false), 0);
        app_set_streams((AppStreams){.out = NULL, .err = json});
        ASSERT_EQ_INT(diag_error(code, sample, true), 0);
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
    ASSERT_EQ_INT(diag_warning(DIAG_WARN_RAW_QUERY_IGNORED, "x", false), 0);
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
    ASSERT_EQ_INT(diag_warning(DIAG_WARN_DEPRECATED_FLAG, "x", true), 0);
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
        diag_warning(DIAG_WARN_DEPRECATED_FLAG, "--trash is deprecated; use --expired", false), 0);
    ASSERT_EQ_INT(
        diag_warning(DIAG_WARN_SYNCED_FOLDER, "database path looks like a synced folder", false),
        0);
    ASSERT_EQ_INT(diag_warning(DIAG_WARN_COUNT, "x", false), -1);
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
    ASSERT_EQ_INT(diag_error(DIAG_INTERNAL, NULL, false), 0);
    ASSERT_EQ_INT(diag_error(DIAG_INTERNAL, NULL, true), 0);
    ASSERT_EQ_INT(diag_warning(DIAG_WARN_SYNCED_FOLDER, NULL, false), 0);
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
    ASSERT_EQ_INT(diag_error(DIAG_USAGE, "say \"hi\"\\", false), 0);
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
    ASSERT_EQ_INT(diag_error(DIAG_USAGE, "missing command (try --help)", false), 0);
    ASSERT_EQ_INT(diag_warning(DIAG_WARN_RAW_QUERY_IGNORED, "x", true), 0);
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
    ASSERT_EQ_INT(diag_warning(DIAG_WARN_COUNT, "x", false), -1);
    ASSERT_EQ_INT(diag_warning(DIAG_WARN_COUNT, "x", true), -1);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf, "");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
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
}
