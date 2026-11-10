#include "appio.h"
#include "commands_common.h"
#include "diag.h"
#include "harness.h"
#include "normalize.h"
#include "register.h"
#include "remember_app.h"
#include "store.h"
#include "test.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Longest E4.3 token, fix and retryable around "sample text". */
enum { EXPECTED_JSON_CAP = 320 };

/* Biggest E4.9 JSON error line the CLI facade tests build (detail + path). */
enum { CLI_JSON_CAP = 1024 };

/* Out-of-range enum probe for the defensive default branches. */
enum { OUT_OF_RANGE_ENUM = 99 };

/* chmod/open modes for the permission tests. */
enum { MODE_DIR_OPEN = 0700, MODE_FILE_PRIVATE = 0600 };

/* Path join buffers in the open-classification tests. */
enum { TEST_PATH_CAP = 512 };

/* SQLite's smallest page. A root page smaller than this is not a real page. */
enum { MIN_PAGE_SIZE = 512 };

/* Fill byte so an untouched StoreError cannot look like STORE_ERROR_NONE. */
enum { POISON_BYTE = 0xA5 };

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
    "schema_too_new",
    "invalid_argument",
    "sync_credential_unavailable",
    "setting_unknown",
    "setting_invalid_value",
    "setting_out_of_range",
    "setting_scope_mismatch",
    "quota_exceeded",
    "confirmation_required",
    "setting_from_hook",
};

/* error.fix and retryable per code, in catalog order (decision:plan21-error-fix-drafts-c42,
   decision:plan21-retryable-codes). usage is the json_begin command's help. */
typedef struct {
    const char *fix;
    const char *retryable;
} ExpectedFix;
static const ExpectedFix k_error_fixes[] = {
    {"Run 'remember get --help' to see how to use it.", "false"},
    {"Run 'remember list' to see keys.", "false"},
    {"Try again. If it keeps happening, report the details from --json.", "false"},
    {"Close other apps to free memory, then try again.", "true"},
    {"This is a bug in remember. Report it with the command you ran.", "false"},
    {"Check the search words, or put the query in double quotes.", "false"},
    {"Pick another key, or rekey the other memory first.", "false"},
    {"Change the text, or update the memory that already has it.", "false"},
    {"Add --expired to use it.", "false"},
    {"Run the command without --expired.", "false"},
    {"Add --trash to use it.", "false"},
    {"Run 'remember delete' on it first.", "false"},
    {"Delete it instead; it stays in the Trash and syncs as deleted.", "false"},
    {"Link two different memories.", "false"},
    {"Remove the link that closes the loop, then try again.", "false"},
    {"Open the source brain once with this remember, then import it.", "false"},
    {"Rekey or delete the memory that holds the key or text, then try again.", "false"},
    {"Read the memory again, then retry the change.", "false"},
    {"Upgrade remember, then try again.", "false"},
    {"Open the brain with the remember that made it, and export it.", "false"},
    {"Check the import source, then try again.", "false"},
    {"Wait a moment and try again.", "true"},
    {"Give yourself write permission on the brain file and its folder.", "false"},
    {"Free some space and try again.", "false"},
    {"Check that the folder exists and that you can read and write it.", "false"},
    {"Point --db at a remember brain.", "false"},
    {"Copy the file somewhere safe, then restore a backup.", "false"},
    {"Check the disk or drive, then try again.", "true"},
    {"Upgrade remember to make changes.", "false"},
    {"Run 'remember list' again without --cursor.", "false"},
    {"Check the helper, or set remember.sync.credential_helper to env and export "
     "REMEMBER_SYNC_TOKEN.",
     "false"},
    {"Run 'remember settings list' to see all settings.", "false"},
    {"Write it in plain digits, like 200, with no sign, leading zero, \"_\", or exponent.",
     "false"},
    {"Set a value from the minimum to the maximum.", "false"},
    {"Run the command again with --scope brain, or without --scope.", "false"},
    {"Reset settings you don't need with 'remember settings reset'.", "false"},
    {"Run the command again with --yes to confirm.", "false"},
    {"Change them yourself in a terminal or in Settings.", "false"},
};

/* Unit tests pick the mode per call. JSON mode buffers the notice for the
   envelope; json_end writes what was buffered (`"error":{...}` and
   `,"warnings":[...]`) to the error stream so a test reads it there, then
   returns diag to plain mode. */
static void json_begin(bool json)
{
    if (json) {
        ASSERT_EQ_INT(diag_json_begin("get"), 0);
    }
}

static void json_end(bool json)
{
    if (json) {
        if (diag_json_has_error()) {
            ASSERT_EQ_INT(diag_json_write_error(app_err()), 0);
        }
        ASSERT_EQ_INT(diag_json_write_warnings(app_err()), 0);
        diag_json_end();
    }
}

static int diag_error_as(bool json, DiagCode code, const char *message)
{
    int rc = 0;

    json_begin(json);
    rc = diag_error(code, message);
    json_end(json);
    return rc;
}

static int diag_warning_as(bool json, DiagWarning code, const char *message)
{
    int rc = 0;

    json_begin(json);
    rc = diag_warning(code, message);
    json_end(json);
    return rc;
}

/* Plain (no --json) usage line for message; the caller frees it.
   NULL when the stream cannot open or the emitter fails. */
static char *plain_usage_line(const char *message)
{
    char *buf = NULL;
    size_t len = 0U;
    FILE *err = open_memstream(&buf, &len);
    int rc = 0;

    if (err == NULL) {
        return NULL;
    }
    app_set_streams((AppStreams){.out = NULL, .err = err});
    rc = diag_error_as(false, DIAG_USAGE, message);
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    if (fclose(err) != 0 || rc != 0) {
        free(buf);
        return NULL;
    }
    return buf;
}

/* E4.8/E4.9 unit tests call the SQLite emitter directly with a synthetic
   StoreError; no store handle is needed. The flag is cleared after. */
static int diag_sqlite_as(bool json, DiagCode code, const StoreError *detail, const char *path)
{
    int rc = 0;

    json_begin(json);
    rc = diag_error_sqlite(code, detail, path);
    json_end(json);
    return rc;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
static StoreError given_sqlite_detail(StoreErrorClass error_class, int code, int extended,
                                      const char *name, const char *message)
{
    StoreError detail;

    memset(&detail, 0, sizeof(detail));
    detail.error_class = error_class;
    detail.sqlite_code = code;
    detail.sqlite_extended = extended;
    (void)snprintf(detail.sqlite_name, sizeof(detail.sqlite_name), "%s", name);
    (void)snprintf(detail.sqlite_message, sizeof(detail.sqlite_message), "%s", message);
    return detail;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

TEST(error_catalog_round_trips_every_e4_3_code)
{
    size_t i = 0;
    size_t count = sizeof(k_error_codes) / sizeof(k_error_codes[0]);

    ASSERT_EQ_INT((int)DIAG_CODE_COUNT, (int)count);
    ASSERT_EQ_INT((int)count, (int)(sizeof(k_error_fixes) / sizeof(k_error_fixes[0])));
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
    "raw_query_ignored",    "deprecated_flag", "synced_folder",          "setting_invalid",
    "flag_meaning_changed", "unknown_field",   "setting_import_skipped",
};

TEST(warning_catalog_round_trips_every_code)
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

TEST(warning_catalog_rejects_names_outside_the_catalog)
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
    ASSERT_STREQ(
        buf, "\"error\":{\"code\":\"not_found\",\"message\":\"not found\",\"fix\":\"Run 'remember "
             "list' to see keys.\",\"retryable\":false}");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
}

/* The first error of a run is the cause; a later plain or SQLite error is dropped. */
TEST(json_keeps_only_the_first_error)
{
    char *buf = NULL;
    size_t len = 0U;
    FILE *err = open_memstream(&buf, &len);
    StoreError busy =
        given_sqlite_detail(STORE_ERROR_BUSY, SQLITE_BUSY, SQLITE_BUSY, "SQLITE_BUSY", "locked");

    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        return;
    }
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_json_begin(NULL), 0);
    ASSERT_FALSE(diag_json_has_error());
    ASSERT_EQ_INT(diag_error(DIAG_USAGE, "first"), 0);
    ASSERT_EQ_INT(diag_error(DIAG_INTERNAL, "second"), 0);
    ASSERT_EQ_INT(diag_error_sqlite(DIAG_BUSY, &busy, NULL), 0);
    ASSERT_TRUE(diag_json_has_error());
    ASSERT_EQ_INT(diag_json_write_error(err), 0);
    ASSERT_EQ_INT(diag_json_write_warnings(err), 0);
    diag_json_end();
    ASSERT_FALSE(diag_json_has_error());
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf,
                 "\"error\":{\"code\":\"usage\",\"message\":\"first\","
                 "\"fix\":\"Run 'remember --help' to see the commands.\",\"retryable\":false}");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
}

TEST(diag_code_fix_is_the_codes_sentence)
{
    ASSERT_STREQ(diag_code_fix(DIAG_OUT_OF_MEMORY),
                 "Close other apps to free memory, then try again.");
    ASSERT_STREQ(diag_code_fix(DIAG_SCHEMA_TOO_NEW), "Upgrade remember to make changes.");
    ASSERT_TRUE(diag_code_fix(DIAG_USAGE) == NULL);
    ASSERT_TRUE(diag_code_fix(DIAG_CODE_COUNT) == NULL);
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
    ASSERT_STREQ(buf, "\"error\":{\"code\":\"usage\",\"message\":\"a\\\"b\\\\c\\n\\u0001\","
                      "\"fix\":\"Run 'remember get --help' to see how to use it.\","
                      "\"retryable\":false}");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
}

/* Catalog fixes are spliced into JSON. Match the writer's quote and slash escapes. */
static int escape_json_text(const char *src, char *dst, size_t cap)
{
    size_t n = 0U;
    size_t i = 0U;

    if (cap == 0U) {
        return -1;
    }
    for (i = 0U; src[i] != '\0'; i++) {
        char byte = src[i];
        size_t need = 1U;

        if (byte == '"' || byte == '\\') {
            need = 2U;
        }
        if (n + need >= cap) {
            dst[0] = '\0';
            return -1;
        }
        if (need == 2U) {
            dst[n++] = '\\';
        }
        dst[n++] = byte;
    }
    dst[n] = '\0';
    return (int)n;
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
        char escaped_fix[EXPECTED_JSON_CAP];
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
        escaped_fix[0] = '\0';
        ASSERT_STREQ(plain_buf, "remember: sample text\n");
        ASSERT_TRUE(escape_json_text(k_error_fixes[i].fix, escaped_fix, sizeof(escaped_fix)) >= 0);
        wrote = snprintf(expected_json, sizeof(expected_json),
                         "\"error\":{\"code\":\"%s\",\"message\":\"sample text\",\"fix\":\"%s\","
                         "\"retryable\":%s}",
                         k_error_codes[i], escaped_fix, k_error_fixes[i].retryable);
        ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(expected_json));
        ASSERT_STREQ(json_buf, expected_json);
        app_set_streams((AppStreams){.out = NULL, .err = NULL});
        ASSERT_EQ_INT(fclose(plain), 0);
        ASSERT_EQ_INT(fclose(json), 0);
        free(plain_buf);
        free(json_buf);
    }
}

/* E2.3: the plain raw_query_ignored line names what was ignored; the JSON
   message stays the bare FTS text (union_search_unparseable_raw_part_is_a_json_warning). */
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
    ASSERT_STREQ(buf, "remember: warning: raw query ignored: x\n");
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
    ASSERT_STREQ(buf, ",\"warnings\":[{\"code\":\"deprecated_flag\",\"message\":\"x\"}]");
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
                      "\"error\":{\"code\":\"internal\",\"message\":\"\","
                      "\"fix\":\"This is a bug in remember. Report it with the command you ran.\","
                      "\"retryable\":false}"
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

/* decision:plan17a-plain-notice-one-line: one notice is one stderr line. */
TEST(plain_error_turns_a_newline_into_a_question_mark)
{
    char *line = plain_usage_line("bad\ninjected");

    ASSERT_TRUE(line != NULL);
    if (line == NULL) {
        return;
    }
    ASSERT_STREQ(line, "remember: bad?injected\n");
    free(line);
}

/* A CR would move the cursor back over the "remember: " prefix. */
TEST(plain_error_turns_a_carriage_return_into_a_question_mark)
{
    char *line = plain_usage_line("bad\rinjected");

    ASSERT_TRUE(line != NULL);
    if (line == NULL) {
        return;
    }
    ASSERT_STREQ(line, "remember: bad?injected\n");
    free(line);
}

TEST(plain_error_keeps_a_tab)
{
    char *line = plain_usage_line("bad\tinjected");

    ASSERT_TRUE(line != NULL);
    if (line == NULL) {
        return;
    }
    ASSERT_STREQ(line, "remember: bad\tinjected\n");
    free(line);
}

TEST(plain_error_turns_an_escape_into_a_question_mark)
{
    char *line = plain_usage_line("bad\x1b"
                                  "injected");

    ASSERT_TRUE(line != NULL);
    if (line == NULL) {
        return;
    }
    ASSERT_STREQ(line, "remember: bad?injected\n");
    free(line);
}

TEST(plain_error_turns_del_into_a_question_mark)
{
    char *line = plain_usage_line("bad\x7f"
                                  "injected");

    ASSERT_TRUE(line != NULL);
    if (line == NULL) {
        return;
    }
    ASSERT_STREQ(line, "remember: bad?injected\n");
    free(line);
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
    ASSERT_STREQ(err_buf, "remember: missing command (try --help)\n"
                          ",\"warnings\":[{\"code\":\"raw_query_ignored\",\"message\":\"x\"}]");
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

TEST(json_usage_error_is_one_stdout_envelope)
{
    char *db = make_temp_db_path();
    const char *args[] = {"--json"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(
        r.out,
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"unknown.result\",\"error\""
        ":{\"code\":\"usage\",\"message\":\"missing command (try --help)\",\"fix\":\"Run 'rem"
        "ember --help' to see the commands.\",\"retryable\":false}}\n");
    cmd_result_free(&r);
    free(db);
}

/* help's command stays help when the topic is unknown, and the error is still
   an envelope. A guard that skips the envelope for every help command would
   print the plain line instead. */
TEST(json_help_with_an_unknown_topic_is_an_envelope)
{
    char *db = make_temp_db_path();
    const char *args[] = {"--json", "help", "not-a-command"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out,
                 "{\"format\":\"remember-message\",\"version\":1,\"type\":\"unknown.result\","
                 "\"error\":{\"code\":\"usage\",\"message\":\"unknown command 'not-a-command'\","
                 "\"fix\":\"Run 'remember help --help' to see how to use it.\","
                 "\"retryable\":false}}\n");
    cmd_result_free(&r);
    free(db);
}

TEST(json_not_found_is_one_stdout_envelope)
{
    char *db = make_temp_db_path();
    const char *args[] = {"get", "--json", "--key", "nope"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 2);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(
        r.out,
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.get.result\",\"erro"
        "r\":{\"code\":\"not_found\",\"message\":\"not found\",\"fix\":\"Run 'remember list' "
        "to see keys.\",\"retryable\":false}}\n");
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
    ASSERT_STREQ(r.err, "remember: warning: --trash now means the Trash (deleted memories). "
                        "For expired memories, use --expired.\n");
    cmd_result_free(&r);
    free(db);
}

TEST(json_trash_warning_is_in_the_list_envelope)
{
    char *db = make_temp_db_path();
    const char *args[] = {"--json", "list", "--trash"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out,
                 "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.list.result\","
                 "\"data\":{\"count\":0,\"total\":0,\"entries\":[]},"
                 "\"next_cursor\":null,\"has_more\":false,"
                 "\"warnings\":[{\"code\":\"flag_meaning_changed\","
                 "\"message\":\"--trash now means the Trash (deleted memories). "
                 "For expired memories, use --expired.\","
                 "\"detail\":{\"flag\":\"--trash\",\"use\":\"--expired\"}}]}\n");
    cmd_result_free(&r);
    free(db);
}

TEST(json_synced_folder_warning_is_in_the_list_envelope)
{
    enum { PATH_CAP = 4096 };
    char tmpl[] = "/tmp/Dropbox-remember-XXXXXX";
    const char *dir = mkdtemp(tmpl);
    char path[PATH_CAP];
    const char *args[] = {"--json", "list"};
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
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out,
                 "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.list.result\","
                 "\"data\":{\"count\":0,\"total\":0,\"entries\":[]},"
                 "\"next_cursor\":null,\"has_more\":false,"
                 "\"warnings\":[{\"code\":\"synced_folder\",\"message\":\"database path looks "
                 "like a synced folder (iCloud/Dropbox/Google Drive); corruption risk - prefer "
                 "a local disk\"}]}\n");
    cmd_result_free(&r);
    harness_remove_tree(dir);
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
    expect_diag(code_for_store_text("HOME is not set"), DIAG_USAGE);
    expect_diag(
        code_for_store_text("database path must be a regular file (not :memory: or file: URI)"),
        DIAG_USAGE);
    expect_diag(code_for_store_text("out of memory"), DIAG_OUT_OF_MEMORY);
    expect_diag(code_for_store_text("database is newer than this remember"), DIAG_NEWER_DATABASE);
    expect_diag(code_for_store_text("unsupported database version"), DIAG_UNSUPPORTED_DATABASE);
    expect_diag(code_for_store_text("migration left a dangling reference"), DIAG_CORRUPT);
    expect_diag(code_for_store_text("migration failed the integrity check"), DIAG_CORRUPT);
    expect_diag(code_for_store_text("cannot create the device id folder"), DIAG_CANNOT_OPEN);
    expect_diag(code_for_store_text("cannot create user.db"), DIAG_CANNOT_OPEN);
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
    expect_diag(code_for_store_status(STORE_ERR_STALE), DIAG_STALE);
    expect_diag(code_for_store_status(STORE_ERR_BAD_VV), DIAG_USAGE);
    {
        enum { UNKNOWN_STORE_STATUS = 1000 };
        /* NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) */
        StoreStatus unknown = (StoreStatus)UNKNOWN_STORE_STATUS;

        expect_diag(code_for_store_status(unknown), DIAG_INTERNAL);
    }
}

/* A stdout that takes writes into its buffer and fails when they are flushed:
   the descriptor under the FILE is replaced by a read-only one, so write(2)
   fails with EBADF. NULL (after a failed assert) if it cannot be built. */
static FILE *given_out_failing_at_flush(void)
{
    char *path = make_temp_db_path();
    /* NOLINTNEXTLINE(android-cloexec-fopen) macOS fopen has no "e" mode */
    FILE *file = (path != NULL) ? fopen(path, "w") : NULL;
    int read_only = open("/dev/null", O_RDONLY | O_CLOEXEC);
    bool built = false;

    if (file != NULL && read_only >= 0 && dup2(read_only, fileno(file)) >= 0) {
        built = true;
    }

    ASSERT_TRUE(built);
    if (read_only >= 0) {
        (void)close(read_only);
    }
    free(path);
    if (!built && file != NULL) {
        (void)fclose(file);
        file = NULL;
    }
    return file;
}

/* A stdout that rejects every write at once (opened for reading). */
static FILE *given_out_rejecting_writes(void)
{
    char *path = make_temp_db_path();
    /* NOLINTNEXTLINE(android-cloexec-fopen) macOS fopen has no "e" mode */
    FILE *made = (path != NULL) ? fopen(path, "w") : NULL;
    FILE *file = NULL;

    if (made != NULL) {
        (void)fclose(made);
        /* NOLINTNEXTLINE(android-cloexec-fopen) macOS fopen has no "e" mode */
        file = fopen(path, "r");
    }
    ASSERT_TRUE(file != NULL);
    free(path);
    return file;
}

/* remember_run(argv) with stdout out; stderr is captured into *err_text
   (heap). -1 (after a failed assert) if stderr cannot be captured. */
static int when_run_with_out(const char *const *argv, int argc, FILE *out, char **err_text)
{
    size_t len = 0U;
    FILE *err = open_memstream(err_text, &len);
    int rc = -1;

    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        return -1;
    }
    rc = remember_run(argc, (char *const *)argv, out, err);
    (void)fclose(err);
    return rc;
}

/* The fake itself: a write is buffered and accepted; the flush fails. */
TEST(failing_flush_stream_accepts_a_write_and_fails_the_flush)
{
    FILE *out = given_out_failing_at_flush();

    if (out == NULL) {
        return;
    }
    ASSERT_TRUE(fputs("remember 0.2.0\n", out) >= 0);
    ASSERT_EQ_INT(fflush(out), EOF);
    (void)fclose(out);
}

/* The other fake: a write fails at once and sets the error flag. */
TEST(rejecting_stream_fails_a_write_and_sets_the_error_flag)
{
    FILE *out = given_out_rejecting_writes();

    if (out == NULL) {
        return;
    }
    ASSERT_EQ_INT(fputs("remember 0.2.0\n", out), EOF);
    ASSERT_TRUE(ferror(out) != 0);
    (void)fclose(out);
}

/* A warning stderr cannot take is a failed write. It does not count for --strict. */
TEST(plain_warning_that_cannot_be_written_does_not_count)
{
    FILE *err = given_out_rejecting_writes();

    if (err == NULL) {
        return;
    }
    diag_warnings_reset();
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_warning(DIAG_WARN_FLAG_MEANING_CHANGED,
                               "--trash now means the Trash (deleted memories). For expired "
                               "memories, use --expired."),
                  -1);
    ASSERT_EQ_INT(diag_warning_count(), 0U);
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    (void)fclose(err);
}

/* A failed unknown_field write does not count, and the same file still warns later. */
TEST(unknown_field_that_cannot_be_written_does_not_stick)
{
    char *buf = NULL;
    size_t len = 0U;
    FILE *bad = given_out_rejecting_writes();
    FILE *err = NULL;

    if (bad == NULL) {
        return;
    }
    diag_json_end();
    diag_warnings_reset();
    app_set_streams((AppStreams){.out = NULL, .err = bad});
    ASSERT_EQ_INT(diag_unknown_field((DiagUnknownField){.path = "/tmp/note.json", .name = "extra"}),
                  -1);
    ASSERT_EQ_INT(diag_warning_count(), 0U);
    (void)fclose(bad);
    err = open_memstream(&buf, &len);
    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        app_set_streams((AppStreams){.out = NULL, .err = NULL});
        diag_warnings_reset();
        return;
    }
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_unknown_field((DiagUnknownField){.path = "/tmp/note.json", .name = "extra"}),
                  0);
    ASSERT_EQ_INT(diag_warning_count(), 1U);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf, "remember: warning: \"/tmp/note.json\" has a field remember doesn't know, "
                      "\"extra\". The rest of the file was read.\n");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    diag_warnings_reset();
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
}

/* Output that fails only at the final flush is an error, not exit 0. */
TEST(run_reports_output_that_fails_at_the_final_flush)
{
    const char *const argv[] = {"remember", "--version"};
    FILE *out = given_out_failing_at_flush();
    char *err_text = NULL;
    int rc = 0;

    if (out == NULL) {
        return;
    }
    rc = when_run_with_out(argv, (int)(sizeof(argv) / sizeof(argv[0])), out, &err_text);
    ASSERT_EQ_INT(rc, 1);
    ASSERT_STREQ(err_text, "remember: failed to write output\n");
    (void)fclose(out);
    free(err_text);
}

/* A write the command did not check still fails the run (ferror). */
TEST(run_reports_a_write_the_command_did_not_check)
{
    const char *const argv[] = {"remember", "--version"};
    FILE *out = given_out_rejecting_writes();
    char *err_text = NULL;
    int rc = 0;

    if (out == NULL) {
        return;
    }
    rc = when_run_with_out(argv, (int)(sizeof(argv) / sizeof(argv[0])), out, &err_text);
    ASSERT_EQ_INT(rc, 1);
    ASSERT_STREQ(err_text, "remember: failed to write output\n");
    (void)fclose(out);
    free(err_text);
}

/* --json: when stdout cannot take the envelope, stderr is the one place left. */
TEST(run_reports_a_failed_json_flush_on_stderr)
{
    char *db = make_temp_db_path();
    const char *const argv[] = {"remember", "--db", db, "--json", "tags"};
    FILE *out = given_out_failing_at_flush();
    char *err_text = NULL;
    int rc = 0;

    if (db == NULL || out == NULL) {
        if (out != NULL) {
            (void)fclose(out);
        }
        free(db);
        return;
    }
    rc = when_run_with_out(argv, (int)(sizeof(argv) / sizeof(argv[0])), out, &err_text);
    ASSERT_EQ_INT(rc, 1);
    ASSERT_STREQ(err_text, "remember: failed to write output\n");
    (void)fclose(out);
    free(err_text);
    free(db);
}

/* --json help and version stay text: no envelope. */
TEST(json_help_and_version_print_text)
{
    char *db = make_temp_db_path();
    const char *version[] = {"--json", "--version"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, version, sizeof(version) / sizeof(version[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_PREFIX(r.out, "remember ");
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
    free(db);
}

/* A command whose own write failed reported it; the run adds no second line. */
TEST(run_keeps_one_error_line_when_the_command_failed)
{
    char *db = make_temp_db_path();
    const char *const argv[] = {"remember", "--db", db, "add", "hello"};
    FILE *out = given_out_rejecting_writes();
    char *err_text = NULL;
    int rc = 0;

    if (db == NULL || out == NULL) {
        if (out != NULL) {
            (void)fclose(out);
        }
        free(db);
        return;
    }
    rc = when_run_with_out(argv, (int)(sizeof(argv) / sizeof(argv[0])), out, &err_text);
    ASSERT_EQ_INT(rc, 1);
    ASSERT_STREQ(err_text, "remember: failed to write output\n");
    (void)fclose(out);
    free(err_text);
    free(db);
}

/* A keyed title and a keyless body, so "boo" has a keys facet and "dentist"
   has none (heap path; NULL after a failed assert). */
static char *given_search_brain(void)
{
    const char *const keyed[] = {"add", "--key", "recs:books", "--", "# Book recommendations"};
    const char *const keyless[] = {"add", "Book the dentist"};
    char *db = make_temp_db_path();
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return NULL;
    }
    r = run_remember(db, keyed, sizeof(keyed) / sizeof(keyed[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, keyless, sizeof(keyless) / sizeof(keyless[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    return db;
}

/* E2.7: the keys line is the first write; its failure is the command's error. */
TEST(search_reports_a_failed_facet_line)
{
    char *db = given_search_brain();
    const char *const argv[] = {"remember", "--db", db, "search", "boo"};
    FILE *out = given_out_rejecting_writes();
    char *err_text = NULL;
    int rc = 0;

    if (db == NULL || out == NULL) {
        if (out != NULL) {
            (void)fclose(out);
        }
        free(db);
        return;
    }
    rc = when_run_with_out(argv, (int)(sizeof(argv) / sizeof(argv[0])), out, &err_text);
    ASSERT_EQ_INT(rc, 1);
    ASSERT_STREQ(err_text, "remember: failed to write output\n");
    (void)fclose(out);
    free(err_text);
    free(db);
}

/* No facet matches "dentist", so the first write is an entry line. */
TEST(search_reports_a_failed_entry_line_without_facets)
{
    char *db = given_search_brain();
    const char *const argv[] = {"remember", "--db", db, "search", "dentist"};
    FILE *out = given_out_rejecting_writes();
    char *err_text = NULL;
    int rc = 0;

    if (db == NULL || out == NULL) {
        if (out != NULL) {
            (void)fclose(out);
        }
        free(db);
        return;
    }
    rc = when_run_with_out(argv, (int)(sizeof(argv) / sizeof(argv[0])), out, &err_text);
    ASSERT_EQ_INT(rc, 1);
    ASSERT_STREQ(err_text, "remember: failed to write output\n");
    (void)fclose(out);
    free(err_text);
    free(db);
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
    err_fmt(DIAG_USAGE, "unknown option '%s'", long_opt);
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_EQ_INT(fclose(err), 0);
    wrote = snprintf(expect, sizeof(expect), "remember: unknown option '%s'\n", long_opt);
    ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(expect));
    ASSERT_STREQ(err_buf, expect);
    free(err_buf);
}

enum { GOLDEN_MAX_ARGS = 6, GOLDEN_STEP_ARGS = 4, GOLDEN_LINE_BUF = 1024 };

/* One golden row: argv after --db, the expected message, its exit, its code.
   The import row carries the stage 3 E4.8 text (its 0.2.0 "database error" is
   now mapped per SQLite cause); every other row is the 0.2.0 line byte for
   byte. */
typedef struct {
    const char *args[GOLDEN_MAX_ARGS];
    const char *message;
    const char *code;
    const char *type; /* reply type <noun>.<verb> */
    const char *fix;
    int exit_code;
    bool with_detail; /* JSON error carries the E4.9 cannot_open detail (import row) */
    /* NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers) */
    char pad_[3];             /* explicit tail padding (kept -Wpadded-clean) */
    const char *json_message; /* NULL: JSON message is the plain message. C-01 quotes differ. */
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
   --json gives the D9 code with the same message in the stdout envelope, empty stderr. */
/* cmd_list's own write check: the first entry line fails on a rejecting stdout. */
TEST(list_reports_a_failed_entry_line)
{
    char *db = given_search_brain();
    const char *const argv[] = {"remember", "--db", db, "list"};
    FILE *out = given_out_rejecting_writes();
    char *err_text = NULL;
    int rc = 0;

    if (db == NULL || out == NULL) {
        if (out != NULL) {
            (void)fclose(out);
        }
        free(db);
        return;
    }
    rc = when_run_with_out(argv, (int)(sizeof(argv) / sizeof(argv[0])), out, &err_text);
    ASSERT_EQ_INT(rc, 1);
    ASSERT_STREQ(err_text, "remember: failed to write output\n");
    (void)fclose(out);
    free(err_text);
    free(db);
}

TEST(golden_error_lines_and_d9_codes)
{
    static const GoldenError rows[] = {
        {{"bogus"},
         "unknown command 'bogus'",
         "usage",
         "unknown",
         "Run 'remember --help' to see the commands.",
         1,
         false,
         {0},
         NULL},
        {{"--bogus", "list"},
         "unknown option '--bogus'",
         "usage",
         "unknown",
         "Run 'remember --help' to see the commands.",
         1,
         false,
         {0},
         NULL},
        {{"list", "--limit", "0"},
         "invalid --limit (must be 1..1000)",
         "usage",
         "memory.list",
         "Run 'remember list --help' to see how to use it.",
         1,
         false,
         {0},
         NULL},
        {{"get", "abc"},
         "\"abc\" isn't a memory id. Use a number from 'remember list', a UUID, or --key KEY.",
         "invalid_argument",
         "memory.get",
         "Use a number from 'remember list', a UUID, or --key KEY.",
         1,
         false,
         {0},
         "\\\"abc\\\" isn't a memory id."},
        {{"add", "--as", "martian", "x"},
         "invalid --as (use human, agent, tool, or share)",
         "usage",
         "memory.add",
         "Run 'remember add --help' to see how to use it.",
         1,
         false,
         {0},
         NULL},
        {{"add", "--key", "", "x"},
         "empty key",
         "usage",
         "memory.add",
         "Run 'remember add --help' to see how to use it.",
         1,
         false,
         {0},
         NULL},
        {{"conflict", "accept", "--id", "1", "--keep", "sideways"},
         "--keep must be local, incoming, or both",
         "usage",
         "conflict.accept",
         "Run 'remember conflict --help' to see how to use it.",
         1,
         false,
         {0},
         NULL},
        {{"device", "extra"},
         "device takes 'new' or no arguments",
         "usage",
         "device.get",
         "Run 'remember device --help' to see how to use it.",
         1,
         false,
         {0},
         NULL},
        {{"get", "999"},
         "not found",
         "not_found",
         "memory.get",
         "Run 'remember list' to see keys.",
         2,
         false,
         {0},
         NULL},
        {{"get", "--key", "k2"},
         "\"k2\" is in the Trash. Add --trash to use it.",
         "deleted",
         "memory.get",
         "Add --trash to use it.",
         3,
         false,
         {0},
         "\\\"k2\\\" is in the Trash."},
        {{"get", "--deleted", "--key", "k1"},
         "not_deleted",
         "not_deleted",
         "memory.get",
         "Run 'remember delete' on it first.",
         3,
         false,
         {0},
         NULL},
        {{"link", "1", "1"},
         "self-link",
         "self_link",
         "memory.link",
         "Link two different memories.",
         1,
         false,
         {0},
         NULL},
        {{"rekey", "--key", "k1", "--to-key", "k2"},
         "key conflicts with entry 2",
         "key_conflict",
         "memory.rekey",
         "Pick another key, or rekey the other memory first.",
         1,
         false,
         {0},
         NULL},
        {{"update", "4", "--text", "gamma"},
         "body hash conflicts with entry 3",
         "body_conflict",
         "memory.update",
         "Change the text, or update the memory that already has it.",
         1,
         false,
         {0},
         NULL},
        {{"import", "--from-db", "/nonexistent/remember-golden.db"},
         "database error: cannot open the import source (SQLITE_CANTOPEN). Check the --from-db "
         "path.",
         "cannot_open",
         "memory.import",
         "Check the --from-db path.",
         1,
         true,
         {0},
         NULL},
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
        const char *json_args[GOLDEN_MAX_ARGS + 1] = {"--json"};
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
        if (row->with_detail) {
            /* The one SQLite row (the import source is missing): short message, the
               fix in detail, and detail.path names the source, not the brain. */
            (void)snprintf(
                want, sizeof(want),
                "{\"format\":\"remember-message\",\"version\":1,\"type\":\"%s.result\","
                "\"error\":{\"code\":\"%s\",\"message\":\"database "
                "error: cannot open the import source\",\"fix\":\"%s\",\"retryable\":false,"
                "\"detail\":{\"sqlite_code\":14,"
                "\"sqlite_extended\":14,\"sqlite_name\":\"SQLITE_CANTOPEN\","
                "\"sqlite_message\":\"unable to open database file\","
                "\"errno\":%d,\"path\":\"%s\",\"reason\":"
                "\"missing\"}}}\n",
                row->type, row->code, row->fix, ENOENT, row->args[2]);
        } else {
            const char *json_message = row->json_message != NULL ? row->json_message : row->message;

            (void)snprintf(want, sizeof(want),
                           "{\"format\":\"remember-message\",\"version\":1,\"type\":\"%s.result\","
                           "\"error\":{\"code\":\"%s\",\"message\":\"%s\",\"fix\":\"%s\","
                           "\"retryable\":false}}\n",
                           row->type, row->code, json_message, row->fix);
        }
        ASSERT_STREQ(r.out, want);
        ASSERT_EQ_INT(r.exit_code, row->exit_code);
        ASSERT_STREQ(r.err, "");
        cmd_result_free(&r);
    }
    free(db);
}

/* D7: relative joins the cwd; "." and ".." normalize lexically; no symlink work. */
TEST(util_absolute_path_joins_cwd_and_normalizes_lexically)
{
    enum { PATH_CAP = 4096 };
    char cwd[PATH_CAP];
    char want[PATH_CAP];
    char got[PATH_CAP];
    int wrote = 0;

    ASSERT_TRUE(getcwd(cwd, sizeof(cwd)) != NULL);
    ASSERT_EQ_INT(util_absolute_path("sub/x.db", got, sizeof(got)), 0);
    wrote = snprintf(want, sizeof(want), "%s/sub/x.db", cwd);
    ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(want));
    ASSERT_STREQ(got, want);

    ASSERT_EQ_INT(util_absolute_path("foo/../bar.db", got, sizeof(got)), 0);
    wrote = snprintf(want, sizeof(want), "%s/bar.db", cwd);
    ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(want));
    ASSERT_STREQ(got, want);

    ASSERT_EQ_INT(util_absolute_path("/a/./b/../c.db", got, sizeof(got)), 0);
    ASSERT_STREQ(got, "/a/c.db");

    ASSERT_EQ_INT(util_absolute_path("/x.db", got, sizeof(got)), 0);
    ASSERT_STREQ(got, "/x.db");

    ASSERT_EQ_INT(util_absolute_path("/", got, sizeof(got)), 0);
    ASSERT_STREQ(got, "/");
    ASSERT_EQ_INT(util_absolute_path("/..", got, sizeof(got)), 0);
    ASSERT_STREQ(got, "/");

    ASSERT_EQ_INT(util_absolute_path(NULL, got, sizeof(got)), -1);
    ASSERT_EQ_INT(util_absolute_path("", got, sizeof(got)), -1);
    ASSERT_EQ_INT(util_absolute_path("x.db", NULL, 0), -1);

    /* "/" needs two bytes with the NUL; one byte cannot hold it. */
    {
        char tiny[1];

        ASSERT_EQ_INT(util_absolute_path("/", tiny, sizeof(tiny)), -1);
        ASSERT_EQ_INT(util_absolute_path("/..", tiny, sizeof(tiny)), -1);
    }
}

/* D7: lexical only — a symlink component stays in the result. */
TEST(util_absolute_path_keeps_a_symlink_component)
{
    enum { PATH_CAP = 4096 };
    char cwd[PATH_CAP];
    char realdir[] = "remember-d7-real-XXXXXX";
    char got[PATH_CAP];
    char want[PATH_CAP];
    int wrote = 0;

    ASSERT_TRUE(getcwd(cwd, sizeof(cwd)) != NULL);
    ASSERT_TRUE(mkdtemp(realdir) != NULL);
    ASSERT_EQ_INT(symlink(realdir, "remember-d7-link"), 0);
    ASSERT_EQ_INT(util_absolute_path("remember-d7-link/x.db", got, sizeof(got)), 0);
    wrote = snprintf(want, sizeof(want), "%s/remember-d7-link/x.db", cwd);
    ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(want));
    ASSERT_STREQ(got, want);
    (void)unlink("remember-d7-link");
    (void)rmdir(realdir);
}

/* Stage 3: a text file opens fine but the first statement is NOTADB (26). */
TEST(store_open_text_file_reports_not_a_database)
{
    char tmpl[] = "/tmp/remember-notadb-XXXXXX";
    int fd = mkstemp(tmpl);
    char err[ERR_BUFSIZE];
    StoreError detail;
    const Store *s = NULL;

    ASSERT_TRUE(fd >= 0);
    if (fd < 0) {
        return;
    }
    ASSERT_TRUE(write(fd, "this is not a database", sizeof("this is not a database") - 1U) > 0);
    ASSERT_EQ_INT(close(fd), 0);
    memset(&detail, 0, sizeof(detail));
    err[0] = '\0';
    s = store_open_opts(tmpl, NULL, err, sizeof(err), &detail);
    ASSERT_TRUE(s == NULL);
    ASSERT_EQ_INT((int)detail.error_class, (int)STORE_ERROR_NOT_A_DATABASE);
    ASSERT_EQ_INT(detail.sqlite_code, 26);
    ASSERT_EQ_INT(detail.sqlite_extended, 26);
    ASSERT_STREQ(detail.sqlite_name, "SQLITE_NOTADB");
    ASSERT_EQ_INT(detail.sys_errno, 0);
    (void)unlink(tmpl);
}

/* Stage 3: a write under an unwritable directory is SQLITE_READONLY_DIRECTORY. */
TEST(store_write_unwritable_dir_reports_read_only)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    StoreAddAction action = STORE_ADD_CREATED;
    Entry out;
    StoreStatus st = STORE_OK;
    const StoreError *detail = NULL;
    char *slash = NULL;
    int locked = 0;
    /* Fixed hash: keyless merge is by hash, and any 64-hex string stores. */
    static const char *const k_hash =
        "0000000000000000000000000000000000000000000000000000000000000001";
    static const char *const k_now = "2026-10-15T00:00:00.000Z";

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    /* Root ignores dir write bits. */
    if (geteuid() == 0) {
        free(db);
        return;
    }
    memset(&out, 0, sizeof(out));
    err[0] = '\0';
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    store_close(s);
    s = NULL;

    /* Parent of "<tmp>/test.db" is the mkdtemp dir itself. */
    slash = strrchr(db, '/');
    ASSERT_TRUE(slash != NULL);
    if (slash == NULL) {
        free(db);
        return;
    }
    *slash = '\0';
    ASSERT_EQ_INT(chmod(db, 0500), 0);
    locked = 1;
    *slash = '/';

    err[0] = '\0';
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        goto cleanup;
    }
    st = store_add(s, "unwritable", k_hash, NULL, NULL, 0U, "human", NULL, k_now, &action, &out);
    ASSERT_EQ_INT((int)st, (int)STORE_ERR_SQLITE);
    detail = store_last_error(s);
    ASSERT_EQ_INT((int)detail->error_class, (int)STORE_ERROR_READ_ONLY);
    ASSERT_EQ_INT(detail->sqlite_code, 8);
    ASSERT_EQ_INT(detail->sqlite_extended, 1544);
    ASSERT_STREQ(detail->sqlite_name, "SQLITE_READONLY_DIRECTORY");
    ASSERT_EQ_INT(detail->sys_errno, 0);
    store_entry_free(&out);

cleanup:
    if (s != NULL) {
        store_close(s);
    }
    /* Restore, or the temp-dir sweep cannot remove it. */
    if (locked) {
        *slash = '\0';
        (void)chmod(db, MODE_DIR_OPEN);
    }
    free(db);
}

/* Stage 3: no_create open failures classify (missing/dir/ENOTDIR/EACCES). */
TEST(store_open_no_create_classifies_cannot_open)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    StoreError detail;
    Store *s = NULL;
    const StoreOpenOpts opts = {.busy_timeout_ms = 100, .no_create = true};
    char missing[TEST_PATH_CAP];
    char dir[TEST_PATH_CAP];
    char sub[TEST_PATH_CAP];
    char noperm[TEST_PATH_CAP];
    int wrote = 0;
    int fd = -1;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    /* Root ignores file permission bits. */
    if (geteuid() == 0) {
        free(db);
        return;
    }

    /* Missing file: CANTOPEN + ENOENT, nothing created. */
    wrote = snprintf(missing, sizeof(missing), "%s-missing.db", db);
    ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(missing));
    memset(&detail, 0, sizeof(detail));
    err[0] = '\0';
    s = store_open_opts(missing, &opts, err, sizeof(err), &detail);
    ASSERT_TRUE(s == NULL);
    ASSERT_EQ_INT((int)detail.error_class, (int)STORE_ERROR_CANNOT_OPEN);
    ASSERT_EQ_INT(detail.sqlite_code, 14);
    ASSERT_STREQ(detail.sqlite_name, "SQLITE_CANTOPEN");
    ASSERT_EQ_INT(detail.sys_errno, 2);
    ASSERT_EQ_INT((int)detail.reason, (int)STORE_REASON_MISSING);
    ASSERT_TRUE(access(missing, F_OK) != 0);

    /* Split "<tmp>/test.db" into dir + file paths. */
    wrote = snprintf(dir, sizeof(dir), "%s", db);
    ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(dir));
    *strrchr(dir, '/') = '\0';

    /* Directory as path: CANTOPEN + EISDIR. */
    memset(&detail, 0, sizeof(detail));
    err[0] = '\0';
    s = store_open_opts(dir, &opts, err, sizeof(err), &detail);
    ASSERT_TRUE(s == NULL);
    ASSERT_EQ_INT((int)detail.error_class, (int)STORE_ERROR_CANNOT_OPEN);
    ASSERT_EQ_INT(detail.sqlite_code, 14);
    ASSERT_EQ_INT((int)detail.reason, (int)STORE_REASON_IS_DIRECTORY);

    /* Create the real database once for the remaining variants. */
    err[0] = '\0';
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    store_close(s);
    s = NULL;

    /* File as a path component: CANTOPEN + ENOTDIR is "other". */
    wrote = snprintf(sub, sizeof(sub), "%s/sub.db", db);
    ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(sub));
    memset(&detail, 0, sizeof(detail));
    err[0] = '\0';
    s = store_open_opts(sub, &opts, err, sizeof(err), &detail);
    ASSERT_TRUE(s == NULL);
    ASSERT_EQ_INT((int)detail.error_class, (int)STORE_ERROR_CANNOT_OPEN);
    ASSERT_EQ_INT(detail.sys_errno, 20);
    ASSERT_EQ_INT((int)detail.reason, (int)STORE_REASON_OTHER);

    /* Unreadable file: CANTOPEN + EACCES is "permission". */
    wrote = snprintf(noperm, sizeof(noperm), "%s-noperm.db", db);
    ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(noperm));
    fd = open(noperm, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, MODE_FILE_PRIVATE);
    ASSERT_TRUE(fd >= 0);
    if (fd >= 0) {
        ASSERT_EQ_INT(close(fd), 0);
    }
    ASSERT_EQ_INT(chmod(noperm, 0000), 0);
    memset(&detail, 0, sizeof(detail));
    err[0] = '\0';
    s = store_open_opts(noperm, &opts, err, sizeof(err), &detail);
    ASSERT_TRUE(s == NULL);
    ASSERT_EQ_INT((int)detail.error_class, (int)STORE_ERROR_CANNOT_OPEN);
    ASSERT_EQ_INT(detail.sqlite_code, 14);
    ASSERT_EQ_INT(detail.sys_errno, 13);
    ASSERT_EQ_INT((int)detail.reason, (int)STORE_REASON_PERMISSION);
    (void)chmod(noperm, MODE_FILE_PRIVATE);
    (void)unlink(noperm);
    free(db);
}

/* Stage 3: a write blocked by another connection's lock is SQLITE_BUSY. */
TEST(store_write_locked_db_reports_busy)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    const StoreOpenOpts opts = {.busy_timeout_ms = 0, .no_create = false};
    sqlite3 *lock = NULL;
    StoreAddAction action = STORE_ADD_CREATED;
    Entry out;
    StoreStatus st = STORE_OK;
    const StoreError *detail = NULL;
    static const char *const k_hash =
        "0000000000000000000000000000000000000000000000000000000000000002";
    static const char *const k_now = "2026-10-15T00:00:00.000Z";

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    memset(&out, 0, sizeof(out));
    err[0] = '\0';
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    store_close(s);
    s = NULL;

    /* Raw handle takes RESERVED and holds it; the store (timeout 0) fails fast. */
    ASSERT_EQ_INT(sqlite3_open_v2(db, &lock, SQLITE_OPEN_READWRITE, NULL), SQLITE_OK);
    ASSERT_EQ_INT(sqlite3_exec(lock, "BEGIN IMMEDIATE;", NULL, NULL, NULL), SQLITE_OK);

    err[0] = '\0';
    s = store_open_opts(db, &opts, err, sizeof(err), NULL);
    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        goto cleanup;
    }
    st = store_add(s, "locked", k_hash, NULL, NULL, 0U, "human", NULL, k_now, &action, &out);
    ASSERT_EQ_INT((int)st, (int)STORE_ERR_SQLITE);
    detail = store_last_error(s);
    ASSERT_EQ_INT((int)detail->error_class, (int)STORE_ERROR_BUSY);
    ASSERT_EQ_INT(detail->sqlite_code, 5);
    ASSERT_STREQ(detail->sqlite_name, "SQLITE_BUSY");
    store_entry_free(&out);

cleanup:
    if (s != NULL) {
        store_close(s);
    }
    if (lock != NULL) {
        (void)sqlite3_exec(lock, "ROLLBACK;", NULL, NULL, NULL);
        (void)sqlite3_close(lock);
    }
    free(db);
}

/* Stage 3: a valid header with a truncated body is corrupt, not not_a_database. */
TEST(store_open_truncated_db_reports_corrupt)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    StoreError detail;
    sqlite3 *raw = NULL;
    int fd = -1;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    /* Raw DELETE-mode database with real content past the 100-byte header. */
    ASSERT_EQ_INT(sqlite3_open_v2(db, &raw, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL),
                  SQLITE_OK);
    ASSERT_EQ_INT(sqlite3_exec(raw, "PRAGMA journal_mode=DELETE;", NULL, NULL, NULL), SQLITE_OK);
    ASSERT_EQ_INT(
        sqlite3_exec(raw, "CREATE TABLE t(x); INSERT INTO t VALUES('payload');", NULL, NULL, NULL),
        SQLITE_OK);
    ASSERT_EQ_INT(sqlite3_close(raw), SQLITE_OK);
    raw = NULL;

    /* Keep the header, lose the body: reads past byte 100 short-read. */
    fd = open(db, O_WRONLY | O_CLOEXEC, 0);
    ASSERT_TRUE(fd >= 0);
    if (fd >= 0) {
        ASSERT_EQ_INT(ftruncate(fd, 512), 0);
        ASSERT_EQ_INT(close(fd), 0);
    }

    memset(&detail, 0, sizeof(detail));
    err[0] = '\0';
    s = store_open_opts(db, NULL, err, sizeof(err), &detail);
    ASSERT_TRUE(s == NULL);
    if (s != NULL) {
        store_close(s);
        free(db);
        return;
    }
    ASSERT_EQ_INT((int)detail.error_class, (int)STORE_ERROR_CORRUPT);
    ASSERT_EQ_INT(detail.sqlite_code, 11);
    free(db);
}

/* Stage 3: no_create opens an existing database; a fresh store has no error. */
TEST(store_open_no_create_existing_opens_clean)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    const StoreOpenOpts opts = {.busy_timeout_ms = 100, .no_create = true};
    StoreError detail;
    const StoreError *last = NULL;
    char missing[TEST_PATH_CAP];
    int wrote = 0;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    err[0] = '\0';
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    store_close(s);

    memset(&detail, POISON_BYTE, sizeof(detail));
    err[0] = '\0';
    s = store_open_opts(db, &opts, err, sizeof(err), &detail);
    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    /* Untouched out_error would keep the sentinel; the open zeroes it. */
    ASSERT_EQ_INT((int)detail.error_class, (int)STORE_ERROR_NONE);
    last = store_last_error(s);
    ASSERT_TRUE(last != NULL);
    ASSERT_EQ_INT((int)last->error_class, (int)STORE_ERROR_NONE);
    store_close(s);
    s = NULL;

    /* NULL detail on failure is safe and still reports text. */
    wrote = snprintf(missing, sizeof(missing), "%s-gone.db", db);
    ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(missing));
    err[0] = '\0';
    s = store_open_opts(missing, &opts, err, sizeof(err), NULL);
    ASSERT_TRUE(s == NULL);
    ASSERT_TRUE(err[0] != '\0');
    ASSERT_TRUE(access(missing, F_OK) != 0);
    free(db);
}

#ifdef REMEMBER_TEST_HOOKS
/* Stage 3: a successful op clears the last SQLite failure (store.h: the
   detail is meaningful only after STORE_ERR_SQLITE, otherwise zero). */
TEST(store_device_new_clears_a_prior_sqlite_failure)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    StoreStatus st = STORE_OK;
    char prev[STORE_DEVICE_ID_LEN + 1];
    bool had_prev = false;
    const StoreError *last = NULL;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    err[0] = '\0';
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    /* Poison s->last with a real SQLITE failure on BEGIN. */
    store_test_fail_exec_after(0);
    st = store_device_new(s, prev, sizeof(prev), &had_prev, err, sizeof(err));
    store_test_fail_exec_after(-1);
    ASSERT_EQ_INT((int)st, (int)STORE_ERR_SQLITE);
    ASSERT_EQ_INT((int)store_last_error(s)->error_class, (int)STORE_ERROR_DATABASE);

    /* A successful op must leave the zero value behind, not the stale one. */
    st = store_device_new(s, prev, sizeof(prev), &had_prev, err, sizeof(err));
    ASSERT_EQ_INT((int)st, (int)STORE_OK);
    last = store_last_error(s);
    ASSERT_TRUE(last != NULL);
    if (last != NULL) {
        ASSERT_EQ_INT((int)last->error_class, (int)STORE_ERROR_NONE);
    }
    store_close(s);
    free(db);
}
#endif /* REMEMBER_TEST_HOOKS */

/* Stage 3 / E4.15: transient SQLITE_SCHEMA on exec runs the statement once
   more and succeeds; a forced second failure surfaces as a database error. */
#ifdef REMEMBER_TEST_HOOKS
TEST(exec_schema_retry_runs_once_more_then_database)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    StoreAddAction action = STORE_ADD_CREATED;
    Entry out;
    StoreStatus st = STORE_OK;
    const StoreError *detail = NULL;
    static const char *const k_hash =
        "0000000000000000000000000000000000000000000000000000000000000003";
    static const char *const k_now = "2026-10-15T00:00:00.000Z";

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    memset(&out, 0, sizeof(out));
    err[0] = '\0';
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    store_test_fail_exec_after(-1);
    store_test_fail_exec_schema_after(-1);

    /* One transient SCHEMA (on BEGIN): without the retry the op would fail;
       with it, the second run executes for real and the add succeeds. */
    store_test_fail_exec_schema_after(0);
    st = store_add(s, "retry me", k_hash, NULL, NULL, 0U, "human", NULL, k_now, &action, &out);
    store_test_fail_exec_schema_after(-1);
    ASSERT_EQ_INT((int)st, (int)STORE_OK);
    store_entry_free(&out);
    memset(&out, 0, sizeof(out));

    /* Forced second failure: first run SCHEMA, the retry ERROR → database. */
    store_test_fail_exec_after(1);
    store_test_fail_exec_schema_after(0);
    st = store_add(s, "wedged", k_hash, NULL, NULL, 0U, "human", NULL, k_now, &action, &out);
    store_test_fail_exec_after(-1);
    store_test_fail_exec_schema_after(-1);
    ASSERT_EQ_INT((int)st, (int)STORE_ERR_SQLITE);
    detail = store_last_error(s);
    ASSERT_EQ_INT((int)detail->error_class, (int)STORE_ERROR_DATABASE);
    store_entry_free(&out);

    store_close(s);
    free(db);
}
#endif /* REMEMBER_TEST_HOOKS */

#ifdef REMEMBER_TEST_HOOKS
/* -Wbad-function-cast forbids (int) on an enum-returning call; probe
   through a local first. */
static int class_probe(int extended)
{
    StoreErrorClass got = store_test_classify_extended(extended);

    return (int)got;
}

static int reason_probe(int extended, int sys_errno)
{
    StoreOpenReason got = store_test_derive_reason(extended, sys_errno);

    return (int)got;
}

/* Stage 3: the E4.7 mapping, first-match-wins in E4.7 order with D6 (NOMEM,
   IOERR_NOMEM → database) before the IOERR row. Anything the table misses
   falls back to its primary code; anything else is database. Fabricated
   high-bit variants pin the primary fallback for primaries whose extended
   set is already complete (a future SQLite variant must land the same way).
   Addition (not |) keeps -Weverything's signed-bitwise rule quiet. */
TEST(sqlite_mapping_covers_every_e47_row)
{
    static const struct {
        int extended;
        StoreErrorClass want;
    } k_cases[] = {
        {SQLITE_NOMEM, STORE_ERROR_DATABASE},
        {SQLITE_IOERR_NOMEM, STORE_ERROR_DATABASE},
        {SQLITE_BUSY, STORE_ERROR_BUSY},
        {SQLITE_BUSY_RECOVERY, STORE_ERROR_BUSY},
        {SQLITE_BUSY_SNAPSHOT, STORE_ERROR_BUSY},
        {SQLITE_BUSY_TIMEOUT, STORE_ERROR_BUSY},
        {SQLITE_LOCKED, STORE_ERROR_BUSY},
        {SQLITE_LOCKED_SHAREDCACHE, STORE_ERROR_BUSY},
        {SQLITE_LOCKED_VTAB, STORE_ERROR_BUSY},
        {SQLITE_PROTOCOL, STORE_ERROR_BUSY},
        {SQLITE_READONLY_DBMOVED, STORE_ERROR_CANNOT_OPEN},
        {SQLITE_READONLY, STORE_ERROR_READ_ONLY},
        {SQLITE_READONLY_RECOVERY, STORE_ERROR_READ_ONLY},
        {SQLITE_READONLY_CANTLOCK, STORE_ERROR_READ_ONLY},
        {SQLITE_READONLY_ROLLBACK, STORE_ERROR_READ_ONLY},
        {SQLITE_READONLY_CANTINIT, STORE_ERROR_READ_ONLY},
        {SQLITE_READONLY_DIRECTORY, STORE_ERROR_READ_ONLY},
        {SQLITE_FULL, STORE_ERROR_DISK_FULL},
        {SQLITE_CANTOPEN_NOTEMPDIR, STORE_ERROR_CANNOT_OPEN},
        {SQLITE_CANTOPEN_ISDIR, STORE_ERROR_CANNOT_OPEN},
        {SQLITE_CANTOPEN_FULLPATH, STORE_ERROR_CANNOT_OPEN},
        {SQLITE_CANTOPEN_CONVPATH, STORE_ERROR_CANNOT_OPEN},
        {SQLITE_CANTOPEN_DIRTYWAL, STORE_ERROR_CANNOT_OPEN},
        {SQLITE_CANTOPEN_SYMLINK, STORE_ERROR_CANNOT_OPEN},
        {SQLITE_PERM, STORE_ERROR_CANNOT_OPEN},
        {SQLITE_CANTOPEN, STORE_ERROR_CANNOT_OPEN},
        {SQLITE_NOTADB, STORE_ERROR_NOT_A_DATABASE},
        {SQLITE_CORRUPT, STORE_ERROR_CORRUPT},
        {SQLITE_CORRUPT_VTAB, STORE_ERROR_CORRUPT},
        {SQLITE_CORRUPT_SEQUENCE, STORE_ERROR_CORRUPT},
        {SQLITE_CORRUPT_INDEX, STORE_ERROR_CORRUPT},
        {SQLITE_IOERR_SHORT_READ, STORE_ERROR_CORRUPT},
        {SQLITE_IOERR, STORE_ERROR_IO_ERROR},
        {SQLITE_IOERR_READ, STORE_ERROR_IO_ERROR},
        {SQLITE_IOERR_WRITE, STORE_ERROR_IO_ERROR},
        {SQLITE_IOERR_FSYNC, STORE_ERROR_IO_ERROR},
        {SQLITE_IOERR_TRUNCATE, STORE_ERROR_IO_ERROR},
        {SQLITE_IOERR_FSTAT, STORE_ERROR_IO_ERROR},
        {SQLITE_IOERR_UNLOCK, STORE_ERROR_IO_ERROR},
        {SQLITE_IOERR_RDLOCK, STORE_ERROR_IO_ERROR},
        {SQLITE_IOERR_DELETE, STORE_ERROR_IO_ERROR},
        {SQLITE_IOERR_BLOCKED, STORE_ERROR_IO_ERROR},
        {SQLITE_IOERR_ACCESS, STORE_ERROR_IO_ERROR},
        {SQLITE_IOERR_CHECKRESERVEDLOCK, STORE_ERROR_IO_ERROR},
        {SQLITE_IOERR_LOCK, STORE_ERROR_IO_ERROR},
        {SQLITE_IOERR_CLOSE, STORE_ERROR_IO_ERROR},
        {SQLITE_IOERR_SHMOPEN, STORE_ERROR_IO_ERROR},
        {SQLITE_IOERR_SHMSIZE, STORE_ERROR_IO_ERROR},
        {SQLITE_IOERR_SHMLOCK, STORE_ERROR_IO_ERROR},
        {SQLITE_IOERR_SHMMAP, STORE_ERROR_IO_ERROR},
        {SQLITE_IOERR_SEEK, STORE_ERROR_IO_ERROR},
        {SQLITE_IOERR_MMAP, STORE_ERROR_IO_ERROR},
        {SQLITE_IOERR_GETTEMPPATH, STORE_ERROR_IO_ERROR},
        {SQLITE_IOERR_CONVPATH, STORE_ERROR_IO_ERROR},
        {SQLITE_IOERR_VNODE, STORE_ERROR_IO_ERROR},
        {SQLITE_IOERR_AUTH, STORE_ERROR_IO_ERROR},
        {SQLITE_IOERR_DATA, STORE_ERROR_IO_ERROR},
        {SQLITE_IOERR_CORRUPTFS, STORE_ERROR_IO_ERROR},
        {SQLITE_NOLFS, STORE_ERROR_IO_ERROR},
        /* Unlisted variants fall back to the primary code. */
        {SQLITE_IOERR_BEGIN_ATOMIC, STORE_ERROR_IO_ERROR},
        {SQLITE_IOERR_COMMIT_ATOMIC, STORE_ERROR_IO_ERROR},
        {SQLITE_CONSTRAINT_UNIQUE, STORE_ERROR_DATABASE},
        {SQLITE_SCHEMA, STORE_ERROR_DATABASE},
        {SQLITE_ERROR, STORE_ERROR_DATABASE},
        {SQLITE_MISUSE, STORE_ERROR_DATABASE},
        {SQLITE_BUSY + (99 * 256), STORE_ERROR_BUSY},
        {SQLITE_READONLY + (99 * 256), STORE_ERROR_READ_ONLY},
        {SQLITE_CANTOPEN + (99 * 256), STORE_ERROR_CANNOT_OPEN},
        {SQLITE_NOTADB + (99 * 256), STORE_ERROR_NOT_A_DATABASE},
        {SQLITE_CORRUPT + (99 * 256), STORE_ERROR_CORRUPT},
        {SQLITE_IOERR + (99 * 256), STORE_ERROR_IO_ERROR},
        {SQLITE_FULL + (99 * 256), STORE_ERROR_DISK_FULL},
    };
    size_t i = 0U;

    for (i = 0U; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        ASSERT_EQ_INT(class_probe(k_cases[i].extended), (int)k_cases[i].want);
    }
}

/* Stage 3: extended names resolve exactly, then via the primary code. */
TEST(sqlite_names_cover_table_and_primary_fallback)
{
    ASSERT_STREQ(store_test_sqlite_name(SQLITE_BUSY), "SQLITE_BUSY");
    ASSERT_STREQ(store_test_sqlite_name(SQLITE_READONLY_DIRECTORY), "SQLITE_READONLY_DIRECTORY");
    ASSERT_STREQ(store_test_sqlite_name(SQLITE_CANTOPEN_ISDIR), "SQLITE_CANTOPEN_ISDIR");
    ASSERT_STREQ(store_test_sqlite_name(SQLITE_IOERR_NOMEM), "SQLITE_IOERR_NOMEM");
    ASSERT_STREQ(store_test_sqlite_name(SQLITE_NOMEM), "SQLITE_NOMEM");
    /* CONSTRAINT_UNIQUE is not named: the primary name answers. */
    ASSERT_STREQ(store_test_sqlite_name(SQLITE_CONSTRAINT_UNIQUE), "SQLITE_CONSTRAINT");
}

/* Stage 3 / E4.7: the cannot_open reason follows sqlite3_system_errno;
   CANTOPEN_ISDIR names a directory even with no errno. */
TEST(sqlite_open_reason_follows_errno_and_isdir)
{
    ASSERT_EQ_INT(reason_probe(SQLITE_CANTOPEN, EACCES), (int)STORE_REASON_PERMISSION);
    ASSERT_EQ_INT(reason_probe(SQLITE_CANTOPEN, ENOENT), (int)STORE_REASON_MISSING);
    ASSERT_EQ_INT(reason_probe(SQLITE_CANTOPEN, EISDIR), (int)STORE_REASON_IS_DIRECTORY);
    ASSERT_EQ_INT(reason_probe(SQLITE_CANTOPEN_ISDIR, 0), (int)STORE_REASON_IS_DIRECTORY);
    ASSERT_EQ_INT(reason_probe(SQLITE_CANTOPEN, 0), (int)STORE_REASON_OTHER);
    ASSERT_EQ_INT(reason_probe(SQLITE_CANTOPEN, EIO), (int)STORE_REASON_OTHER);
}

/* Stage 3: the message clamp defaults NULL to "" and truncates over-long
   input; a NULL handle (open-time OOM) records NOMEM. */
TEST(sqlite_message_copy_clamps_and_defaults)
{
    enum { MSG_OVERFLOW_SLACK = 64 };
    StoreError out;
    char long_msg[(size_t)STORE_SQLITE_MSG_LEN + (size_t)MSG_OVERFLOW_SLACK];
    size_t i = 0U;

    for (i = 0U; i < sizeof(long_msg) - 1U; i++) {
        long_msg[i] = 'x';
    }
    long_msg[sizeof(long_msg) - 1U] = '\0';
    memset(&out, POISON_BYTE, sizeof(out));
    store_test_copy_message(&out, NULL);
    ASSERT_STREQ(out.sqlite_message, "");
    store_test_copy_message(&out, "short");
    ASSERT_STREQ(out.sqlite_message, "short");
    store_test_copy_message(&out, long_msg);
    ASSERT_EQ_INT((int)strlen(out.sqlite_message), STORE_SQLITE_MSG_LEN - 1);
    ASSERT_TRUE(strncmp(out.sqlite_message, long_msg, (size_t)(STORE_SQLITE_MSG_LEN - 1)) == 0);

    memset(&out, POISON_BYTE, sizeof(out));
    store_test_capture_null(&out);
    ASSERT_EQ_INT((int)out.error_class, (int)STORE_ERROR_DATABASE);
    ASSERT_EQ_INT(out.sqlite_code, SQLITE_NOMEM);
    ASSERT_EQ_INT(out.sqlite_extended, SQLITE_NOMEM);
    ASSERT_STREQ(out.sqlite_name, "SQLITE_NOMEM");
    ASSERT_STREQ(out.sqlite_message, "out of memory");
}
#endif /* REMEMBER_TEST_HOOKS */

/* E4.8 cause + fix with the extended name; E4.9 detail carries the codes,
   the name and the message. busy has no path, errno, reason or fts_only. */
TEST(sqlite_busy_reports_cause_fix_and_detail)
{
    static const char *const k_plain =
        "remember: database error: the brain is locked by another process (SQLITE_BUSY). "
        "Wait a moment and try again. Another app or a terminal may be using this brain.\n";
    static const char *const k_json =
        "\"error\":{\"code\":\"busy\",\"message\":\"database error: the brain is locked by an"
        "other process\",\"fix\":\"Wait a moment and try again. Another app or a terminal may"
        " be using this brain.\",\"retryable\":true,\"detail\":{\"sqlite_code\":5,\"sqlite_ex"
        "tended\":5,\"sqlite_name\":\"SQLITE_BUSY\",\"sqlite_message\":\"database is locked\""
        "}}";
    StoreError detail = given_sqlite_detail(STORE_ERROR_BUSY, SQLITE_BUSY, SQLITE_BUSY,
                                            "SQLITE_BUSY", "database is locked");
    char *buf = NULL;
    size_t len = 0U;
    FILE *err = open_memstream(&buf, &len);

    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        return;
    }
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_sqlite_as(false, DIAG_BUSY, &detail, NULL), 0);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf, k_plain);
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_sqlite_as(true, DIAG_BUSY, &detail, NULL), 0);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf + strlen(k_plain), k_json);
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
}

/* E4.7 reason + E4.9 errno/path: a relative path is emitted absolute (D7). */
TEST(sqlite_cannot_open_carries_errno_reason_and_absolute_path)
{
    static const char *const k_plain =
        "remember: database error: cannot open the brain file (SQLITE_CANTOPEN). "
        "Check that the folder exists and that you can read and write it.\n";
    StoreError detail =
        given_sqlite_detail(STORE_ERROR_CANNOT_OPEN, SQLITE_CANTOPEN, SQLITE_CANTOPEN,
                            "SQLITE_CANTOPEN", "unable to open database file");
    char cwd[REMEMBER_PATH_MAX];
    char want[REMEMBER_PATH_MAX * 2U];
    char *buf = NULL;
    size_t len = 0U;
    FILE *err = NULL;

    detail.sys_errno = ENOENT;
    detail.reason = STORE_REASON_MISSING;
    ASSERT_TRUE(getcwd(cwd, sizeof(cwd)) != NULL);
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        return;
    }
    (void)snprintf(
        want, sizeof(want),
        "\"error\":{\"code\":\"cannot_open\",\"message\":\"database error: cannot open the br"
        "ain file\",\"fix\":\"Check that the folder exists and that you can read and write it"
        ".\",\"retryable\":false,\"detail\":{\"sqlite_code\":14,\"sqlite_extended\":14,\"sqli"
        "te_name\":\"SQLITE_CANTOPEN\",\"sqlite_message\":\"unable to open database file\",\""
        "errno\":%d,\"path\":\"%s/some/rel.db\",\"reason\":\"missing\"}}",
        ENOENT, cwd);
    err = open_memstream(&buf, &len);
    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        return;
    }
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_sqlite_as(false, DIAG_CANNOT_OPEN, &detail, "some/rel.db"), 0);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf, k_plain);
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_sqlite_as(true, DIAG_CANNOT_OPEN, &detail, "some/rel.db"), 0);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf + strlen(k_plain), want);
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
}

/* database has no cause: its own text, no path even when one is passed.
   sqlite_message is always present (E4.9 lists no omit rule; errno/path/reason
   and fts_only are the fields that are gated). */
TEST(sqlite_database_keeps_its_own_text_without_a_path)
{
    static const char *const k_plain =
        "remember: database error (SQLITE_SCHEMA). Try again. If it keeps happening, report the "
        "details from --json.\n";
    static const char *const k_json =
        "\"error\":{\"code\":\"database\",\"message\":\"database error\",\"fix\":\"Try again."
        " If it keeps happening, report the details from --json.\",\"retryable\":false"
        ",\"detail\":{\"sqlite_code\":17,\"sqlite_extended\":17,\"sqlite_name\":\"SQLITE_SCHE"
        "MA\",\"sqlite_message\":\"\"}}";
    StoreError detail = given_sqlite_detail(STORE_ERROR_DATABASE, SQLITE_SCHEMA, SQLITE_SCHEMA,
                                            "SQLITE_SCHEMA", "");
    char *buf = NULL;
    size_t len = 0U;
    FILE *err = open_memstream(&buf, &len);

    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        return;
    }
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_sqlite_as(false, DIAG_DATABASE, &detail, "/tmp/x.db"), 0);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf, k_plain);
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_sqlite_as(true, DIAG_DATABASE, &detail, "/tmp/x.db"), 0);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf + strlen(k_plain), k_json);
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
}

/* Misuse writes nothing: a non-SQLite code, a NULL detail, or the zero
   StoreError (NONE) each return -1 in both modes. */
TEST(sqlite_emitter_rejects_misuse_quietly)
{
    StoreError busy = given_sqlite_detail(STORE_ERROR_BUSY, SQLITE_BUSY, SQLITE_BUSY, "SQLITE_BUSY",
                                          "database is locked");
    StoreError none;
    char *buf = NULL;
    size_t len = 0U;
    FILE *err = open_memstream(&buf, &len);

    memset(&none, 0, sizeof(none));
    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        return;
    }
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_sqlite_as(false, DIAG_USAGE, &busy, NULL), -1);
    ASSERT_EQ_INT(diag_sqlite_as(true, DIAG_BUSY, NULL, NULL), -1);
    ASSERT_EQ_INT(diag_sqlite_as(false, DIAG_BUSY, &none, NULL), -1);
    ASSERT_EQ_INT(diag_sqlite_as(true, DIAG_DATABASE, &none, "/tmp/x.db"), -1);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf, "");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
}

/* corrupt carries fts_only in both modes; the fts_only row swaps in the
   repair text while the base row keeps the damaged-brain text. */
TEST(sqlite_corrupt_marks_fts_only_and_keeps_base_text)
{
    static const char *const k_base_plain =
        "remember: database error: the brain file is damaged (SQLITE_CORRUPT). "
        "Copy the file somewhere safe before anything else, then restore a backup, "
        "or recover what can be read with `remember --db NEW.db import --from-db COPY`.\n";
    static const char *const k_fts_plain =
        "remember: database error: the search index is damaged (SQLITE_CORRUPT_VTAB). "
        "Run `remember repair search-index`. Your memories are not affected.\n";
    StoreError base = given_sqlite_detail(STORE_ERROR_CORRUPT, SQLITE_CORRUPT, SQLITE_CORRUPT,
                                          "SQLITE_CORRUPT", "database disk image is malformed");
    StoreError fts = given_sqlite_detail(STORE_ERROR_CORRUPT, SQLITE_CORRUPT, SQLITE_CORRUPT_VTAB,
                                         "SQLITE_CORRUPT_VTAB", "database disk image is malformed");
    char *buf = NULL;
    size_t len = 0U;
    FILE *err = open_memstream(&buf, &len);

    fts.fts_only = true;
    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        return;
    }
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_sqlite_as(false, DIAG_CORRUPT, &base, "/tmp/b.db"), 0);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf, k_base_plain);
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_sqlite_as(true, DIAG_CORRUPT, &base, "/tmp/b.db"), 0);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(
        buf + strlen(k_base_plain),
        "\"error\":{\"code\":\"corrupt\",\"message\":\"database error: the brain file is dama"
        "ged\",\"fix\":\"Copy the file somewhere safe before anything else, then restore a ba"
        "ckup, or recover what can be read with `remember --db NEW.db import --from-db COPY`."
        "\",\"retryable\":false,\"detail\":{\"sqlite_code\":11,\"sqlite_extended\":11,\"sqlit"
        "e_name\":\"SQLITE_CORRUPT\",\"sqlite_message\":\"database disk image is malformed\","
        "\"path\":\"/tmp/b.db\",\"fts_only\":false}}");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
    buf = NULL;
    len = 0U;
    err = open_memstream(&buf, &len);
    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        return;
    }
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_sqlite_as(false, DIAG_CORRUPT, &fts, "/tmp/b.db"), 0);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf, k_fts_plain);
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_sqlite_as(true, DIAG_CORRUPT, &fts, "/tmp/b.db"), 0);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(
        buf + strlen(k_fts_plain),
        "\"error\":{\"code\":\"corrupt\",\"message\":\"database error: the search index is da"
        "maged\",\"fix\":\"Run `remember repair search-index`. Your memories are not affected"
        ".\",\"retryable\":false,\"detail\":{\"sqlite_code\":11,\"sqlite_extended\":267,\"sql"
        "ite_name\":\"SQLITE_CORRUPT_VTAB\",\"sqlite_message\":\"database disk image is malfo"
        "rmed\",\"path\":\"/tmp/b.db\",\"fts_only\":true}}");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
}

/* Adapter class → catalog code; NONE is a store bug and maps internal. */
TEST(store_class_maps_to_its_diag_code)
{
    static const struct {
        StoreErrorClass error_class;
        DiagCode code;
    } rows[] = {
        {STORE_ERROR_BUSY, DIAG_BUSY},
        {STORE_ERROR_READ_ONLY, DIAG_READ_ONLY},
        {STORE_ERROR_DISK_FULL, DIAG_DISK_FULL},
        {STORE_ERROR_CANNOT_OPEN, DIAG_CANNOT_OPEN},
        {STORE_ERROR_NOT_A_DATABASE, DIAG_NOT_A_DATABASE},
        {STORE_ERROR_CORRUPT, DIAG_CORRUPT},
        {STORE_ERROR_IO_ERROR, DIAG_IO_ERROR},
        {STORE_ERROR_DATABASE, DIAG_DATABASE},
        {STORE_ERROR_NONE, DIAG_INTERNAL},
    };
    size_t i = 0;

    for (i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        DiagCode got = code_for_store_class(rows[i].error_class);

        ASSERT_EQ_INT((int)got, (int)rows[i].code);
    }
    {
        /* Outside the enum: the defensive default. */
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
        DiagCode got = code_for_store_class((StoreErrorClass)OUT_OF_RANGE_ENUM);

        ASSERT_EQ_INT((int)got, (int)DIAG_INTERNAL);
    }
}

/* E4.8 through open_store: a text file is not_a_database, and the JSON
   detail carries the absolute brain path (no errno: none was captured). */
TEST(cli_text_file_reports_not_a_database)
{
    static const char *const k_plain =
        "remember: database error: the file is not a remember database (SQLITE_NOTADB). "
        "Point --db at a remember brain, or move this file away.\n";
    char tmpl[] = "/tmp/remember-cli-notadb-XXXXXX";
    int fd = mkstemp(tmpl);
    const char *get[] = {"get", "1"};
    const char *json[] = {"--json", "get", "1"};
    char want[CLI_JSON_CAP];
    CmdResult r;

    ASSERT_TRUE(fd >= 0);
    if (fd < 0) {
        return;
    }
    ASSERT_TRUE(write(fd, "this is not a database", sizeof("this is not a database") - 1U) > 0);
    ASSERT_EQ_INT(close(fd), 0);
    r = run_remember(tmpl, get, sizeof(get) / sizeof(get[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, k_plain);
    cmd_result_free(&r);
    r = run_remember(tmpl, json, sizeof(json) / sizeof(json[0]), NULL);
    (void)snprintf(
        want, sizeof(want),
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.get.result\",\"erro"
        "r\":{\"code\":\"not_a_database\",\"message\":\"database error: the file is not a rem"
        "ember database\",\"fix\":\"Point --db at a remember brain, or move this file away.\""
        ",\"retryable\":false,\"detail\":{\"sqlite_code\":26,\"sqlite_extended\":26,\"sqlite_"
        "name\":\"SQLITE_NOTADB\",\"sqlite_message\":\"file is not a database\",\"path\":\"%s"
        "\"}}}\n",
        tmpl);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, want);
    cmd_result_free(&r);
    (void)unlink(tmpl);
}

/* Itay 2026-10-15: a bad import SOURCE names the source, not the brain.
   A text file as --from-db opens and fails at the source's version read. */
TEST(cli_import_text_source_names_the_source)
{
    static const char *const k_plain =
        "remember: database error: the import source is not a remember database "
        "(SQLITE_NOTADB). Check the --from-db path.\n";
    char tmpl[] = "/tmp/remember-cli-src-notadb-XXXXXX";
    int fd = mkstemp(tmpl);
    char *db = make_temp_db_path();
    const char *add[] = {"add", "x"};
    const char *imp[] = {"import", "--from-db", tmpl};
    const char *json[] = {"--json", "import", "--from-db", tmpl};
    char want[CLI_JSON_CAP];
    CmdResult r;

    ASSERT_TRUE(fd >= 0 && db != NULL);
    if (fd < 0 || db == NULL) {
        free(db);
        return;
    }
    ASSERT_TRUE(write(fd, "this is not a database", sizeof("this is not a database") - 1U) > 0);
    ASSERT_EQ_INT(close(fd), 0);
    r = run_remember(db, add, sizeof(add) / sizeof(add[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, imp, sizeof(imp) / sizeof(imp[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, k_plain);
    cmd_result_free(&r);
    r = run_remember(db, json, sizeof(json) / sizeof(json[0]), NULL);
    (void)snprintf(
        want, sizeof(want),
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.import.result\",\"e"
        "rror\":{\"code\":\"not_a_database\",\"message\":\"database error: the import source "
        "is not a remember database\",\"fix\":\"Check the --from-db path.\",\"retryable\":fal"
        "se,\"detail\":{\"sqlite_code\":26,\"sqlite_extended\":26,\"sqlite_name\":\"SQLITE_NO"
        "TADB\",\"sqlite_message\":\"file is not a database\",\"path\":\"%s\"}}}\n",
        tmpl);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, want);
    cmd_result_free(&r);
    (void)unlink(tmpl);
    free(db);
}

/* D7 through the CLI: a relative --db is emitted absolute, with no symlink work. */
TEST(cli_relative_db_emits_absolute_detail_path)
{
    char cwd[REMEMBER_PATH_MAX];
    char tmpl[] = "remember-d7-cli-XXXXXX";
    const char *dir = mkdtemp(tmpl);
    char want[CLI_JSON_CAP];
    const char *argv_store[] = {"remember", "--db", tmpl, "--json", "get", "1"};
    char *err_buf = NULL;
    char *out_buf = NULL;
    size_t err_len = 0U;
    size_t out_len = 0U;
    FILE *err = NULL;
    FILE *out = NULL;
    int rc = 0;
    int wrote = 0;

    ASSERT_TRUE(getcwd(cwd, sizeof(cwd)) != NULL);
    ASSERT_TRUE(dir != NULL);
    if (dir == NULL) {
        return;
    }
    err = open_memstream(&err_buf, &err_len);
    out = open_memstream(&out_buf, &out_len);
    ASSERT_TRUE(err != NULL && out != NULL);
    if (err == NULL || out == NULL) {
        if (err != NULL) {
            (void)fclose(err);
        }
        if (out != NULL) {
            (void)fclose(out);
        }
        (void)rmdir(tmpl);
        return;
    }
    rc = remember_run((int)(sizeof(argv_store) / sizeof(argv_store[0])), (char *const *)argv_store,
                      out, err);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_EQ_INT(fflush(out), 0);
    ASSERT_EQ_INT(rc, 1);
    ASSERT_STREQ(err_buf, "");
    wrote = snprintf(
        want, sizeof(want),
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.get.result\",\"error\""
        ":{\"code\":\"cannot_open\",\"message\":\"database error: cannot open the brain file"
        "\",\"fix\":\"Check that the folder exists and that you can read and write it.\",\"re"
        "tryable\":false,\"detail\":{\"sqlite_code\":14,\"sqlite_extended\":14,\"sqlite_name"
        "\":\"SQLITE_CANTOPEN\",\"sqlite_message\":\"unable to open database file\",\"errno\""
        ":%d,\"path\":\"%s/%s\",\"reason\":\"is_directory\"}}}\n",
        EISDIR, cwd, tmpl);
    ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(want));
    ASSERT_STREQ(out_buf, want);
    ASSERT_EQ_INT(fclose(err), 0);
    ASSERT_EQ_INT(fclose(out), 0);
    free(err_buf);
    free(out_buf);
    (void)rmdir(tmpl);
}

/* Root ignores file mode bits. Print the skip: a green root run did not check it. */
static bool skip_chmod_when_root(void)
{
    if (geteuid() != 0) {
        return false;
    }
    (void)printf("SKIP %s (running as root; file mode bits are ignored)\n", g_current_test);
    return true;
}

/* E4.8 through err_store: the first write to a read-only brain is read_only,
   with the absolute brain path and no errno. Root ignores file bits: skip. */
TEST(cli_read_only_file_reports_read_only_on_write)
{
    static const char *const k_plain =
        "remember: database error: the brain is read-only (SQLITE_READONLY). "
        "Give yourself write permission on the brain file and its folder, or move the brain to a "
        "writable folder.\n";
    char *db = make_temp_db_path();
    const char *seed[] = {"add", "seed"};
    const char *add[] = {"add", "world"};
    const char *json[] = {"--json", "add", "world"};
    char want[CLI_JSON_CAP];
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, seed, sizeof(seed) / sizeof(seed[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    if (skip_chmod_when_root()) {
        free(db);
        return;
    }
    ASSERT_EQ_INT(chmod(db, 0444), 0);
    r = run_remember(db, add, sizeof(add) / sizeof(add[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, k_plain);
    cmd_result_free(&r);
    r = run_remember(db, json, sizeof(json) / sizeof(json[0]), NULL);
    (void)snprintf(
        want, sizeof(want),
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.add.result\",\"erro"
        "r\":{\"code\":\"read_only\",\"message\":\"database error: the brain is read-only\","
        "\"fix\":\"Give yourself write permission on the brain file and its folder, or move t"
        "he brain to a writable folder.\",\"retryable\":false,\"detail\":{\"sqlite_code\":8,"
        "\"sqlite_extended\":8,\"sqlite_name\":\"SQLITE_READONLY\",\"sqlite_message\":\"attem"
        "pt to write a readonly database\",\"path\":\"%s\"}}}\n",
        db);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, want);
    cmd_result_free(&r);
    /* Restore, or the temp-dir sweep cannot remove it. */
    ASSERT_EQ_INT(chmod(db, 0600), 0);
    free(db);
}

/* An out-of-range reason can never come from the store; the emitter treats
   it as the specified catch-all (other). Covers the bounds fallback. */
TEST(sqlite_out_of_range_reason_falls_back_to_other)
{
    StoreError detail =
        given_sqlite_detail(STORE_ERROR_CANNOT_OPEN, SQLITE_CANTOPEN, SQLITE_CANTOPEN,
                            "SQLITE_CANTOPEN", "unable to open database file");
    char *buf = NULL;
    size_t len = 0U;
    FILE *err = open_memstream(&buf, &len);

    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    detail.reason = (StoreOpenReason)OUT_OF_RANGE_ENUM;
    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        return;
    }
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_sqlite_as(true, DIAG_CANNOT_OPEN, &detail, NULL), 0);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(
        buf, "\"error\":{\"code\":\"cannot_open\",\"message\":\"database error: cannot open the br"
             "ain file\",\"fix\":\"Check that the folder exists and that you can read and write it"
             ".\",\"retryable\":false,\"detail\":{\"sqlite_code\":14,\"sqlite_extended\":14,\"sqli"
             "te_name\":\"SQLITE_CANTOPEN\",\"sqlite_message\":\"unable to open database file\",\""
             "reason\":\"other\"}}");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
}

/* err_store_sqlite with no captured failure is a store bug: internal error.
   err_store with SQLITE but a clean last error keeps the legacy line. */
TEST(err_store_sqlite_misuse_and_clean_store_fall_back)
{
    StoreError none;
    char *db = make_temp_db_path();
    char errbuf[ERR_BUFSIZE];
    Store *s = NULL;
    char *buf = NULL;
    size_t len = 0U;
    FILE *err = NULL;

    memset(&none, 0, sizeof(none));
    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    err = open_memstream(&buf, &len);
    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        free(db);
        return;
    }
    app_set_streams((AppStreams){.out = NULL, .err = err});
    err_store_sqlite(NULL, NULL);
    err_store_sqlite(&none, "/tmp/x.db");
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf, "remember: internal error\nremember: internal error\n");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
    buf = NULL;
    len = 0U;

    /* A clean open leaves the zero last error: SQLITE without a capture. */
    errbuf[0] = '\0';
    s = store_open(db, errbuf, sizeof(errbuf));
    ASSERT_TRUE(s != NULL);
    if (s == NULL) {
        free(db);
        return;
    }
    err = open_memstream(&buf, &len);
    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        store_close(s);
        free(db);
        return;
    }
    app_set_streams((AppStreams){.out = NULL, .err = err});
    err_store(s, STORE_ERR_SQLITE);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf, "remember: database error\n");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
    store_close(s);
    free(db);
}

/* No SQLite failure exists when mkdir cannot run: the open keeps its stage 2
   code and text (D9 cannot_open row), with no JSON detail. */
TEST(cli_db_under_a_file_keeps_the_legacy_open_line)
{
    static const char *const k_plain = "remember: database path component is not a directory\n";
    static const char *const k_json =
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.get.result\",\"erro"
        "r\":{\"code\":\"cannot_open\",\"message\":\"database path component is not a directo"
        "ry\",\"fix\":\"Check that the folder exists and that you can read and write it.\",\""
        "retryable\":false}}\n";
    char *dir = make_temp_db_path();
    char file[REMEMBER_PATH_MAX];
    char nested[REMEMBER_PATH_MAX];
    const char *get[] = {"get", "1"};
    const char *json[] = {"--json", "get", "1"};
    char *slash = NULL;
    int fd = -1;
    CmdResult r;
    int wrote = 0;

    ASSERT_TRUE(dir != NULL);
    if (dir == NULL) {
        return;
    }
    /* make_temp_db_path returns "<tmpdir>/test.db": turn it into a file so
       "<file>/sub/b.db" fails before any SQLite call. */
    slash = strrchr(dir, '/');
    ASSERT_TRUE(slash != NULL);
    if (slash == NULL) {
        free(dir);
        return;
    }
    *slash = '\0';
    wrote = snprintf(file, sizeof(file), "%s/file", dir);
    ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(file));
    wrote = snprintf(nested, sizeof(nested), "%s/file/sub/b.db", dir);
    ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(nested));
    fd = open(file, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, MODE_FILE_PRIVATE);
    ASSERT_TRUE(fd >= 0);
    if (fd < 0) {
        free(dir);
        return;
    }
    ASSERT_EQ_INT(close(fd), 0);
    r = run_remember(nested, get, sizeof(get) / sizeof(get[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, k_plain);
    cmd_result_free(&r);
    r = run_remember(nested, json, sizeof(json) / sizeof(json[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, k_json);
    cmd_result_free(&r);
    (void)unlink(file);
    free(dir);
}

/* chmod 555 on the folder (file stays 666) is read_only, READONLY_DIRECTORY.
   Root skips: file mode bits do not apply. */
TEST(cli_readonly_directory_reports_read_only)
{
    static const char *const k_plain =
        "remember: database error: the brain is read-only (SQLITE_READONLY_DIRECTORY). "
        "Give yourself write permission on the brain file and its folder, or move the brain to a "
        "writable folder.\n";
    char *db = make_temp_db_path();
    char dir[TEST_PATH_CAP];
    const char *seed[] = {"add", "seed"};
    const char *add[] = {"add", "world"};
    const char *json[] = {"--json", "add", "world"};
    char want[CLI_JSON_CAP];
    char *slash = NULL;
    CmdResult r;
    int wrote = 0;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, seed, sizeof(seed) / sizeof(seed[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    if (skip_chmod_when_root()) {
        free(db);
        return;
    }
    slash = strrchr(db, '/');
    ASSERT_TRUE(slash != NULL && slash != db);
    if (slash == NULL || slash == db) {
        free(db);
        return;
    }
    wrote = snprintf(dir, sizeof(dir), "%.*s", (int)(slash - db), db);
    ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(dir));
    ASSERT_EQ_INT(chmod(db, 0666), 0);
    ASSERT_EQ_INT(chmod(dir, 0555), 0);
    r = run_remember(db, add, sizeof(add) / sizeof(add[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, k_plain);
    cmd_result_free(&r);
    r = run_remember(db, json, sizeof(json) / sizeof(json[0]), NULL);
    (void)snprintf(
        want, sizeof(want),
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.add.result\",\"erro"
        "r\":{\"code\":\"read_only\",\"message\":\"database error: the brain is read-only\","
        "\"fix\":\"Give yourself write permission on the brain file and its folder, or move t"
        "he brain to a writable folder.\",\"retryable\":false,\"detail\":{\"sqlite_code\":8,"
        "\"sqlite_extended\":1544,\"sqlite_name\":\"SQLITE_READONLY_DIRECTORY\",\"sqlite_mess"
        "age\":\"attempt to write a readonly database\",\"path\":\"%s\"}}}\n",
        db);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, want);
    cmd_result_free(&r);
    ASSERT_EQ_INT(chmod(dir, 0700), 0);
    ASSERT_EQ_INT(chmod(db, 0600), 0);
    free(db);
}

/* chmod 000 on the brain file is cannot_open, reason permission. Root skips. */
TEST(cli_mode_000_reports_cannot_open_permission)
{
    static const char *const k_plain =
        "remember: database error: cannot open the brain file (SQLITE_CANTOPEN). "
        "Check that the folder exists and that you can read and write it.\n";
    char *db = make_temp_db_path();
    const char *seed[] = {"add", "seed"};
    const char *get[] = {"get", "1"};
    const char *json[] = {"--json", "get", "1"};
    char want[CLI_JSON_CAP];
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, seed, sizeof(seed) / sizeof(seed[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    if (skip_chmod_when_root()) {
        free(db);
        return;
    }
    ASSERT_EQ_INT(chmod(db, 0), 0);
    r = run_remember(db, get, sizeof(get) / sizeof(get[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, k_plain);
    cmd_result_free(&r);
    r = run_remember(db, json, sizeof(json) / sizeof(json[0]), NULL);
    (void)snprintf(
        want, sizeof(want),
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.get.result\",\"erro"
        "r\":{\"code\":\"cannot_open\",\"message\":\"database error: cannot open the brain fi"
        "le\",\"fix\":\"Check that the folder exists and that you can read and write it.\",\""
        "retryable\":false,\"detail\":{\"sqlite_code\":14,\"sqlite_extended\":14,\"sqlite_nam"
        "e\":\"SQLITE_CANTOPEN\",\"sqlite_message\":\"unable to open database file\",\"errno"
        "\":%d,\"path\":\"%s\",\"reason\":\"permission\"}}}\n",
        EACCES, db);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, want);
    cmd_result_free(&r);
    ASSERT_EQ_INT(chmod(db, 0600), 0);
    free(db);
}

/* E4.10: damage one entries_fts_data block. Search's SQL names entries_fts and
   the base tables are intact, so the failure is the search index (CORRUPT_VTAB). */
static int damage_entries_fts_data(const char *db)
{
    sqlite3 *sql = NULL;
    int rc = 0;

    rc = sqlite3_open(db, &sql);
    if (rc != SQLITE_OK) {
        sqlite3_close(sql);
        return -1;
    }
    rc = sqlite3_exec(sql,
                      "UPDATE entries_fts_data SET block = zeroblob(32) "
                      "WHERE id = (SELECT MIN(id) FROM entries_fts_data);",
                      NULL, NULL, NULL);
    sqlite3_close(sql);
    if (rc != SQLITE_OK) {
        return -1;
    }
    return 0;
}

TEST(cli_damaged_fts_search_reports_fts_only)
{
    static const char *const k_plain =
        "remember: database error: the search index is damaged (SQLITE_CORRUPT_VTAB). "
        "Run `remember repair search-index`. Your memories are not affected.\n";
    char *db = make_temp_db_path();
    const char *seed[] = {"add", "alpha seed memory"};
    const char *search[] = {"search", "alpha"};
    const char *json[] = {"--json", "search", "alpha"};
    char want[CLI_JSON_CAP];
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, seed, sizeof(seed) / sizeof(seed[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    ASSERT_EQ_INT(damage_entries_fts_data(db), 0);
    r = run_remember(db, search, sizeof(search) / sizeof(search[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, k_plain);
    cmd_result_free(&r);
    r = run_remember(db, json, sizeof(json) / sizeof(json[0]), NULL);
    (void)snprintf(
        want, sizeof(want),
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.search.result\",\"e"
        "rror\":{\"code\":\"corrupt\",\"message\":\"database error: the search index is damag"
        "ed\",\"fix\":\"Run `remember repair search-index`. Your memories are not affected.\""
        ",\"retryable\":false,\"detail\":{\"sqlite_code\":11,\"sqlite_extended\":267,\"sqlite"
        "_name\":\"SQLITE_CORRUPT_VTAB\",\"sqlite_message\":\"database disk image is malforme"
        "d\",\"path\":\"%s\",\"fts_only\":true}}}\n",
        db);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, want);
    cmd_result_free(&r);
    free(db);
}

/* E4.14: repair rebuilds the index. N is the entries row count. Search works again. */
TEST(cli_repair_search_index_rebuilds)
{
    char *db = make_temp_db_path();
    const char *seed[] = {"add", "alpha seed memory"};
    const char *repair[] = {"--json", "repair", "search-index"};
    const char *search[] = {"search", "alpha"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, seed, sizeof(seed) / sizeof(seed[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    ASSERT_EQ_INT(damage_entries_fts_data(db), 0);
    r = run_remember(db, repair, sizeof(repair) / sizeof(repair[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, "{\"format\":\"remember-message\",\"version\":1,\"type\":\"repair.search_"
                        "index.result\",\"data\":{\"action\":\"rebuilt\",\"entries\":1}}\n");
    cmd_result_free(&r);
    r = run_remember(db, search, sizeof(search) / sizeof(search[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "alpha seed memory");
    cmd_result_free(&r);
    free(db);
}

/* D8: without --json the same repair prints rebuilt N. */
TEST(cli_repair_search_index_human_rebuilt)
{
    char *db = make_temp_db_path();
    const char *seed[] = {"add", "alpha seed memory"};
    const char *repair[] = {"repair", "search-index"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, seed, sizeof(seed) / sizeof(seed[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, repair, sizeof(repair) / sizeof(repair[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, "rebuilt 1\n");
    cmd_result_free(&r);
    free(db);
}

/* E4.14: N counts rows in every bin, including a soft-deleted one. */
TEST(cli_repair_counts_deleted_entries)
{
    char *db = make_temp_db_path();
    const char *first[] = {"add", "alpha seed memory"};
    const char *second[] = {"add", "beta other memory"};
    const char *drop[] = {"delete", "2"};
    const char *repair[] = {"--json", "repair", "search-index"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, first, sizeof(first) / sizeof(first[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, second, sizeof(second) / sizeof(second[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, drop, sizeof(drop) / sizeof(drop[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, repair, sizeof(repair) / sizeof(repair[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, "{\"format\":\"remember-message\",\"version\":1,\"type\":\"repair.search_"
                        "index.result\",\"data\":{\"action\":\"rebuilt\",\"entries\":2}}\n");
    cmd_result_free(&r);
    free(db);
}

/* Zero the entries root page. The file still opens; quick_check does not pass. */
/* Zero the root page of one table: page-level damage, not a bad value.
   db then name: the brain file, then the table whose root page is zeroed. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static int smash_root(const char *db, const char *name)
{
    sqlite3 *sql = NULL;
    sqlite3_stmt *stmt = NULL;
    char *zeros = NULL;
    int root = 0;
    int page_size = 0;
    int fd = -1;
    int status = -1;

    if (sqlite3_open(db, &sql) != SQLITE_OK) {
        sqlite3_close(sql);
        return -1;
    }
    if (sqlite3_prepare_v2(sql, "SELECT rootpage FROM sqlite_master WHERE name = ?1;", -1, &stmt,
                           NULL) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC) != SQLITE_OK ||
        sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        sqlite3_close(sql);
        return -1;
    }
    root = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    stmt = NULL;
    if (sqlite3_prepare_v2(sql, "PRAGMA page_size;", -1, &stmt, NULL) != SQLITE_OK ||
        sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        sqlite3_close(sql);
        return -1;
    }
    page_size = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    sqlite3_close(sql);
    if (root < 2 || page_size < MIN_PAGE_SIZE) {
        return -1;
    }
    zeros = (char *)calloc((size_t)page_size, 1U);
    if (zeros == NULL) {
        return -1;
    }
    fd = open(db, O_RDWR | O_CLOEXEC);
    if (fd < 0 || lseek(fd, (off_t)(root - 1) * page_size, SEEK_SET) < 0 ||
        write(fd, zeros, (size_t)page_size) != page_size) {
        if (fd >= 0) {
            (void)close(fd);
        }
        free(zeros);
        return -1;
    }
    status = close(fd) == 0 ? 0 : -1;
    free(zeros);
    return status;
}

/* E4.14: a damaged base table is corrupt with fts_only false, and nothing is rebuilt. */
TEST(cli_repair_refuses_damaged_base_table)
{
    static const char *const k_plain =
        "remember: database error: the brain file is damaged (SQLITE_CORRUPT). "
        "Copy the file somewhere safe before anything else, then restore a backup, "
        "or recover what can be read with `remember --db NEW.db import --from-db COPY`.\n";
    char *db = make_temp_db_path();
    const char *seed[] = {"add", "alpha seed memory"};
    const char *repair[] = {"repair", "search-index"};
    const char *json[] = {"--json", "repair", "search-index"};
    char want[CLI_JSON_CAP];
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, seed, sizeof(seed) / sizeof(seed[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    ASSERT_EQ_INT(smash_root(db, "entries"), 0);
    r = run_remember(db, repair, sizeof(repair) / sizeof(repair[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, k_plain);
    cmd_result_free(&r);
    r = run_remember(db, json, sizeof(json) / sizeof(json[0]), NULL);
    (void)snprintf(
        want, sizeof(want),
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"repair.search_index.result"
        "\",\"error\":{\"code\":\"corrupt\",\"message\":\"database error: the brain file is d"
        "amaged\",\"fix\":\"Copy the file somewhere safe before anything else, then restore a"
        " backup, or recover what can be read with `remember --db NEW.db import --from-db COP"
        "Y`.\",\"retryable\":false,\"detail\":{\"sqlite_code\":11,\"sqlite_extended\":11,\"sq"
        "lite_name\":\"SQLITE_CORRUPT\",\"sqlite_message\":\"*** in database main ***\\nTree "
        "23 page 23: btreeInitPage() returns error code 11\",\"path\":\"%s\",\"fts_only\":fal"
        "se}}}\n",
        db);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, want);
    cmd_result_free(&r);
    free(db);
}

/* E4.14: a truncated brain cannot pass quick_check. Repair refuses; it does not rebuild. */
/* Itay 2026-10-16: repair heals page-level damage in an FTS shadow table too
   ('rebuild' and DROP both fail there). Search works and the file checks clean. */
static void expect_repair_heals(const char *shadow)
{
    char *db = make_temp_db_path();
    const char *seed[] = {"add", "alpha seed memory"};
    const char *seed2[] = {"add", "--tag", "beta", "second alpha"};
    const char *repair[] = {"--json", "repair", "search-index"};
    const char *search[] = {"--json", "search", "alpha"};
    CmdResult r;
    char *check = NULL;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, seed, sizeof(seed) / sizeof(seed[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, seed2, sizeof(seed2) / sizeof(seed2[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    ASSERT_EQ_INT(smash_root(db, shadow), 0);
    r = run_remember(db, repair, sizeof(repair) / sizeof(repair[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, "{\"format\":\"remember-message\",\"version\":1,\"type\":\"repair.search_"
                        "index.result\",\"data\":{\"action\":\"rebuilt\",\"entries\":2}}\n");
    cmd_result_free(&r);
    r = run_remember(db, search, sizeof(search) / sizeof(search[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "\"total\":2");
    cmd_result_free(&r);
    check = harness_sqlite_query_line(db, "PRAGMA integrity_check;");
    ASSERT_STREQ(check != NULL ? check : "", "ok");
    free(check);
    free(db);
}

TEST(cli_repair_heals_page_damaged_fts_data)
{
    expect_repair_heals("entries_fts_data");
}

TEST(cli_repair_heals_page_damaged_fts_content)
{
    expect_repair_heals("entries_fts_content");
}

/* Itay 2026-10-16: tags are memories, not index. Damage there is the base line,
   fts_only false, and repair refuses instead of claiming success. */
TEST(cli_damaged_tags_is_brain_damage_not_fts_only)
{
    char *db = make_temp_db_path();
    const char *seed[] = {"add", "--tag", "beta", "alpha seed memory"};
    const char *search[] = {"--json", "search", "alpha"};
    const char *repair[] = {"--json", "repair", "search-index"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, seed, sizeof(seed) / sizeof(seed[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    ASSERT_EQ_INT(smash_root(db, "tags"), 0);
    r = run_remember(db, search, sizeof(search) / sizeof(search[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "\"code\":\"corrupt\"");
    ASSERT_STR_CONTAINS(r.out, "\"fts_only\":false");
    cmd_result_free(&r);
    r = run_remember(db, repair, sizeof(repair) / sizeof(repair[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "the brain file is damaged");
    ASSERT_STR_CONTAINS(r.out, "\"fts_only\":false");
    cmd_result_free(&r);
    free(db);
}

#ifdef REMEMBER_TEST_HOOKS
/* Itay 2026-10-16: every store connection is defensive, and repair's schema
   surgery (page-damaged FTS) leaves it defensive again. */
TEST(store_connection_is_defensive_before_and_after_repair)
{
    char *db = make_temp_db_path();
    char err[ERR_BUFSIZE];
    Store *s = NULL;
    Entry e;
    StoreAddAction act = STORE_ADD_CREATED;
    size_t entries = 0U;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    ASSERT_EQ_INT(store_test_defensive(s), 1);
    memset(&e, 0, sizeof(e));
    ASSERT_EQ_STATUS(store_add(s, "alpha",
                               "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                               NULL, NULL, 0U, "human", NULL, "2026-06-15T12:00:00.000Z", &act, &e),
                     STORE_OK);
    store_entry_free(&e);
    store_close(s);
    ASSERT_EQ_INT(smash_root(db, "entries_fts_data"), 0);
    s = store_open(db, err, sizeof(err));
    ASSERT_TRUE(s != NULL);
    ASSERT_EQ_STATUS(store_repair_search_index(s, &entries), STORE_OK);
    ASSERT_TRUE(entries == 1U);
    ASSERT_EQ_INT(store_test_defensive(s), 1);
    store_close(s);
    ASSERT_EQ_INT(store_test_defensive(NULL), -1);
    free(db);
}
#endif

TEST(cli_repair_refuses_truncated_brain)
{
    static const char *const k_plain =
        "remember: database error: the brain file is damaged (SQLITE_CORRUPT). "
        "Copy the file somewhere safe before anything else, then restore a backup, "
        "or recover what can be read with `remember --db NEW.db import --from-db COPY`.\n";
    char *db = make_temp_db_path();
    const char *seed[] = {"add", "seed"};
    const char *repair[] = {"repair", "search-index"};
    const char *json[] = {"--json", "repair", "search-index"};
    char want[CLI_JSON_CAP];
    CmdResult r;
    int fd = -1;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, seed, sizeof(seed) / sizeof(seed[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    fd = open(db, O_RDWR | O_CLOEXEC);
    ASSERT_TRUE(fd >= 0);
    if (fd >= 0) {
        ASSERT_EQ_INT(ftruncate(fd, 512), 0);
        ASSERT_EQ_INT(close(fd), 0);
    }
    r = run_remember(db, repair, sizeof(repair) / sizeof(repair[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, k_plain);
    cmd_result_free(&r);
    r = run_remember(db, json, sizeof(json) / sizeof(json[0]), NULL);
    (void)snprintf(
        want, sizeof(want),
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"repair.search_index.result"
        "\",\"error\":{\"code\":\"corrupt\",\"message\":\"database error: the brain file is d"
        "amaged\",\"fix\":\"Copy the file somewhere safe before anything else, then restore a"
        " backup, or recover what can be read with `remember --db NEW.db import --from-db COP"
        "Y`.\",\"retryable\":false,\"detail\":{\"sqlite_code\":11,\"sqlite_extended\":11,\"sq"
        "lite_name\":\"SQLITE_CORRUPT\",\"sqlite_message\":\"database disk image is malformed"
        "\",\"path\":\"%s\",\"fts_only\":false}}}\n",
        db);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, want);
    cmd_result_free(&r);
    free(db);
}

/* E4.10: SQL that names entries_fts is not enough. A failed base check keeps fts_only false. */
TEST(cli_search_on_damaged_base_is_not_fts_only)
{
    static const char *const k_plain =
        "remember: database error: the brain file is damaged (SQLITE_CORRUPT). "
        "Copy the file somewhere safe before anything else, then restore a backup, "
        "or recover what can be read with `remember --db NEW.db import --from-db COPY`.\n";
    char *db = make_temp_db_path();
    const char *seed[] = {"add", "alpha seed memory"};
    const char *search[] = {"search", "alpha"};
    const char *json[] = {"--json", "search", "alpha"};
    char want[CLI_JSON_CAP];
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, seed, sizeof(seed) / sizeof(seed[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    ASSERT_EQ_INT(smash_root(db, "entries"), 0);
    r = run_remember(db, search, sizeof(search) / sizeof(search[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, k_plain);
    cmd_result_free(&r);
    r = run_remember(db, json, sizeof(json) / sizeof(json[0]), NULL);
    (void)snprintf(
        want, sizeof(want),
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.search.result\",\"e"
        "rror\":{\"code\":\"corrupt\",\"message\":\"database error: the brain file is damaged"
        "\",\"fix\":\"Copy the file somewhere safe before anything else, then restore a backu"
        "p, or recover what can be read with `remember --db NEW.db import --from-db COPY`.\","
        "\"retryable\":false,\"detail\":{\"sqlite_code\":11,\"sqlite_extended\":11,\"sqlite_n"
        "ame\":\"SQLITE_CORRUPT\",\"sqlite_message\":\"database disk image is malformed\",\"p"
        "ath\":\"%s\",\"fts_only\":false}}}\n",
        db);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, want);
    cmd_result_free(&r);
    free(db);
}

/* Every misuse of repair is the one usage line. */
TEST(cli_repair_usage_is_search_index)
{
    static const char *const k_err = "remember: usage: repair search-index\n";
    char *db = make_temp_db_path();
    const char *none[] = {"repair"};
    const char *bad[] = {"repair", "foo"};
    const char *extra[] = {"repair", "search-index", "extra"};
    const char *opt[] = {"repair", "--nope"};
    const char *const *cases[] = {none, bad, extra, opt};
    const size_t counts[] = {1U, 2U, 3U, 2U};
    size_t i = 0;
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        r = run_remember(db, cases[i], counts[i], NULL);
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STREQ(r.out, "");
        ASSERT_STREQ(r.err, k_err);
        cmd_result_free(&r);
    }
    free(db);
}

/* decision:plan16-repair-no-create: repair on a missing brain is cannot_open
   missing, as if --no-create were given, and creates no folder or file. */
TEST(cli_repair_missing_brain_creates_nothing)
{
    char tmpl[] = "/tmp/remember-repair-XXXXXX";
    char path[TEST_PATH_CAP];
    const char *cmd[] = {"--json", "repair", "search-index"};
    const char *dir = NULL;
    int wrote = 0;
    CmdResult r;

    dir = mkdtemp(tmpl);
    ASSERT_TRUE(dir != NULL);
    if (dir == NULL) {
        return;
    }
    ASSERT_EQ_INT(rmdir(tmpl), 0);
    wrote = snprintf(path, sizeof(path), "%s/x.db", tmpl);
    ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(path));

    r = run_remember(path, cmd, sizeof(cmd) / sizeof(cmd[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "\"code\":\"cannot_open\"");
    ASSERT_STR_CONTAINS(r.out, "\"reason\":\"missing\"");
    ASSERT_TRUE(access(tmpl, F_OK) != 0);
    cmd_result_free(&r);
}

/* A malformed repair is usage before any open, so it creates no brain either. */
TEST(cli_repair_usage_creates_no_brain)
{
    char *db = make_temp_db_path();
    const char *bad[] = {"repair", "foo"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, bad, sizeof(bad) / sizeof(bad[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "remember: usage: repair search-index\n");
    ASSERT_TRUE(access(db, F_OK) != 0);
    cmd_result_free(&r);
    free(db);
}

TEST(cli_repair_help_names_the_command)
{
    char *db = make_temp_db_path();
    const char *help[] = {"help", "repair"};
    const char *general[] = {"--help"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, help, sizeof(help) / sizeof(help[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "remember repair search-index");
    ASSERT_STR_CONTAINS(r.out, "rebuilt N");
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
    r = run_remember(db, general, sizeof(general) / sizeof(general[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "repair");
    ASSERT_STR_CONTAINS(r.out, "Rebuild the search index");
    cmd_result_free(&r);
    free(db);
}

/* General help and every command footer name the globals. */
TEST(cli_help_names_json_busy_timeout_and_no_create)
{
    char *db = make_temp_db_path();
    const char *general[] = {"--help"};
    const char *topic[] = {"help", "add"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, general, sizeof(general) / sizeof(general[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "[--json] [--strict] [--color=auto|always|never] [--quiet] "
                               "[--busy-timeout MS] [--no-create]");
    ASSERT_STR_NOT_CONTAINS(r.out, "--json-errors");
    ASSERT_STR_CONTAINS(r.out, "--busy-timeout MS  Lock wait in ms (0..60000, default 5000)");
    ASSERT_STR_CONTAINS(r.out, "--no-create        Do not create a missing brain file");
    cmd_result_free(&r);
    r = run_remember(db, topic, sizeof(topic) / sizeof(topic[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "Global options: --db PATH, --json, --strict, "
                               "--color=auto|always|never, --quiet, "
                               "--busy-timeout MS, --no-create, --help, --version");
    cmd_result_free(&r);
    free(db);
}

/* A header with the body cut off is corrupt, fts_only false. */
TEST(cli_truncated_brain_reports_corrupt)
{
    static const char *const k_plain =
        "remember: database error: the brain file is damaged (SQLITE_CORRUPT). "
        "Copy the file somewhere safe before anything else, then restore a backup, "
        "or recover what can be read with `remember --db NEW.db import --from-db COPY`.\n";
    char *db = make_temp_db_path();
    const char *seed[] = {"add", "seed"};
    const char *get[] = {"get", "1"};
    const char *json[] = {"--json", "get", "1"};
    char want[CLI_JSON_CAP];
    CmdResult r;
    int fd = -1;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, seed, sizeof(seed) / sizeof(seed[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    fd = open(db, O_RDWR | O_CLOEXEC);
    ASSERT_TRUE(fd >= 0);
    if (fd >= 0) {
        ASSERT_EQ_INT(ftruncate(fd, 512), 0);
        ASSERT_EQ_INT(close(fd), 0);
    }
    r = run_remember(db, get, sizeof(get) / sizeof(get[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, k_plain);
    cmd_result_free(&r);
    r = run_remember(db, json, sizeof(json) / sizeof(json[0]), NULL);
    (void)snprintf(
        want, sizeof(want),
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.get.result\",\"erro"
        "r\":{\"code\":\"corrupt\",\"message\":\"database error: the brain file is damaged\","
        "\"fix\":\"Copy the file somewhere safe before anything else, then restore a backup, "
        "or recover what can be read with `remember --db NEW.db import --from-db COPY`.\",\"r"
        "etryable\":false,\"detail\":{\"sqlite_code\":11,\"sqlite_extended\":11,\"sqlite_name"
        "\":\"SQLITE_CORRUPT\",\"sqlite_message\":\"database disk image is malformed\",\"path"
        "\":\"%s\",\"fts_only\":false}}}\n",
        db);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, want);
    cmd_result_free(&r);
    free(db);
}

/* The shim tests need the remember CLI built with REMEMBER_TEST_VFS (the gate
   configures it ON). Without it they are skipped, and the run says so. */
#ifdef REMEMBER_TEST_VFS
/* Single-threaded tests; setenv and unsetenv have no portable reentrant variant. */
// NOLINTBEGIN(concurrency-mt-unsafe)

/* X-E02: REMEMBER_TEST_ENOSPC=1 makes the next write SQLITE_FULL. */
TEST(cli_enospc_shim_reports_disk_full)
{
    static const char *const k_plain =
        "remember: database error: disk is full (SQLITE_FULL). Free some space and try again.\n";
    char *db = make_temp_db_path();
    const char *add[] = {"add", "full"};
    const char *json[] = {"--json", "add", "full"};
    char want[CLI_JSON_CAP];
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    ASSERT_EQ_INT(setenv("REMEMBER_TEST_ENOSPC", "1", 1), 0);
    r = run_remember(db, add, sizeof(add) / sizeof(add[0]), NULL);
    ASSERT_EQ_INT(unsetenv("REMEMBER_TEST_ENOSPC"), 0);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, k_plain);
    cmd_result_free(&r);

    ASSERT_EQ_INT(setenv("REMEMBER_TEST_ENOSPC", "1", 1), 0);
    r = run_remember(db, json, sizeof(json) / sizeof(json[0]), NULL);
    ASSERT_EQ_INT(unsetenv("REMEMBER_TEST_ENOSPC"), 0);
    (void)snprintf(
        want, sizeof(want),
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.add.result\",\"erro"
        "r\":{\"code\":\"disk_full\",\"message\":\"database error: disk is full\",\"fix\":\"F"
        "ree some space and try again.\",\"retryable\":false,\"detail\":{\"sqlite_code\":13,"
        "\"sqlite_extended\":13,\"sqlite_name\":\"SQLITE_FULL\",\"sqlite_message\":\"database"
        " or disk is full\",\"path\":\"%s\"}}}\n",
        db);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, want);
    cmd_result_free(&r);
    free(db);
}

/* X-E02: REMEMBER_TEST_FAIL_WRITE=0 fails the first write with IOERR_WRITE. */
TEST(cli_fail_write_shim_reports_io_error)
{
    static const char *const k_plain =
        "remember: database error: the disk reported a read or write error "
        "(SQLITE_IOERR_WRITE). Check the disk or drive, then try again.\n";
    char *db = make_temp_db_path();
    const char *add[] = {"add", "write"};
    const char *json[] = {"--json", "add", "write"};
    char want[CLI_JSON_CAP];
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    ASSERT_EQ_INT(setenv("REMEMBER_TEST_FAIL_WRITE", "0", 1), 0);
    r = run_remember(db, add, sizeof(add) / sizeof(add[0]), NULL);
    ASSERT_EQ_INT(unsetenv("REMEMBER_TEST_FAIL_WRITE"), 0);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, k_plain);
    cmd_result_free(&r);

    ASSERT_EQ_INT(setenv("REMEMBER_TEST_FAIL_WRITE", "0", 1), 0);
    r = run_remember(db, json, sizeof(json) / sizeof(json[0]), NULL);
    ASSERT_EQ_INT(unsetenv("REMEMBER_TEST_FAIL_WRITE"), 0);
    (void)snprintf(
        want, sizeof(want),
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.add.result\",\"erro"
        "r\":{\"code\":\"io_error\",\"message\":\"database error: the disk reported a read or"
        " write error\",\"fix\":\"Check the disk or drive, then try again.\",\"retryable\":tr"
        "ue,\"detail\":{\"sqlite_code\":10,\"sqlite_extended\":778,\"sqlite_name\":\"SQLITE_I"
        "OERR_WRITE\",\"sqlite_message\":\"disk I/O error\",\"errno\":%d,\"path\":\"%s\"}}}\n",
        ENOENT, db);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, want);
    cmd_result_free(&r);
    free(db);
}

/* A large write budget still finishes the add. N=0 above is the failure side. */
TEST(cli_fail_write_budget_lets_an_add_finish)
{
    char *db = make_temp_db_path();
    const char *add[] = {"add", "budget"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    ASSERT_EQ_INT(setenv("REMEMBER_TEST_FAIL_WRITE", "100000", 1), 0);
    r = run_remember(db, add, sizeof(add) / sizeof(add[0]), NULL);
    ASSERT_EQ_INT(unsetenv("REMEMBER_TEST_FAIL_WRITE"), 0);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_NOT_CONTAINS(r.err, "database error");
    cmd_result_free(&r);
    free(db);
}

/* X-E02: REMEMBER_TEST_FAIL_READ=0 fails the first read with IOERR_READ. */
TEST(cli_fail_read_shim_reports_io_error)
{
    static const char *const k_plain =
        "remember: database error: the disk reported a read or write error "
        "(SQLITE_IOERR_READ). Check the disk or drive, then try again.\n";
    char *db = make_temp_db_path();
    const char *seed[] = {"add", "seed"};
    const char *list[] = {"list"};
    const char *json[] = {"--json", "list"};
    char want[CLI_JSON_CAP];
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, seed, sizeof(seed) / sizeof(seed[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    ASSERT_EQ_INT(setenv("REMEMBER_TEST_FAIL_READ", "0", 1), 0);
    r = run_remember(db, list, sizeof(list) / sizeof(list[0]), NULL);
    ASSERT_EQ_INT(unsetenv("REMEMBER_TEST_FAIL_READ"), 0);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, k_plain);
    cmd_result_free(&r);

    ASSERT_EQ_INT(setenv("REMEMBER_TEST_FAIL_READ", "0", 1), 0);
    r = run_remember(db, json, sizeof(json) / sizeof(json[0]), NULL);
    ASSERT_EQ_INT(unsetenv("REMEMBER_TEST_FAIL_READ"), 0);
    (void)snprintf(
        want, sizeof(want),
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.list.result\",\"err"
        "or\":{\"code\":\"io_error\",\"message\":\"database error: the disk reported a read o"
        "r write error\",\"fix\":\"Check the disk or drive, then try again.\",\"retryable\":t"
        "rue,\"detail\":{\"sqlite_code\":10,\"sqlite_extended\":266,\"sqlite_name\":\"SQLITE_"
        "IOERR_READ\",\"sqlite_message\":\"disk I/O error\",\"errno\":%d,\"path\":\"%s\"}}}\n",
        EEXIST, db);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, want);
    cmd_result_free(&r);
    free(db);
}

/* E4.14: a read fault during the base-table check is still corrupt, and the engine
   name stays the I/O error. Open of a seeded brain completes two reads; the next
   one is inside quick_check (observed on the vendored SQLite). */
/* Both repair tiers fail (every write fails): the message never sends the user
   back to repair, the failed surgery leaves a brain that still opens, and a
   later repair without the fault heals the page-damaged index. */
TEST(cli_repair_that_cannot_write_never_points_back_at_repair)
{
    char *db = make_temp_db_path();
    const char *seed[] = {"add", "alpha seed memory"};
    const char *repair[] = {"repair", "search-index"};
    const char *list[] = {"list"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, seed, sizeof(seed) / sizeof(seed[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    ASSERT_EQ_INT(smash_root(db, "entries_fts_data"), 0);

    ASSERT_EQ_INT(setenv("REMEMBER_TEST_FAIL_WRITE", "0", 1), 0);
    r = run_remember(db, repair, sizeof(repair) / sizeof(repair[0]), NULL);
    ASSERT_EQ_INT(unsetenv("REMEMBER_TEST_FAIL_WRITE"), 0);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_TRUE(strstr(r.err, "repair search-index") == NULL);
    cmd_result_free(&r);

    r = run_remember(db, list, sizeof(list) / sizeof(list[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "alpha seed memory");
    cmd_result_free(&r);
    r = run_remember(db, repair, sizeof(repair) / sizeof(repair[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.out, "rebuilt 1\n");
    cmd_result_free(&r);
    free(db);
}

TEST(cli_repair_read_fault_during_check_is_corrupt)
{
    static const char *const k_plain =
        "remember: database error: the brain file is damaged (SQLITE_IOERR_READ). "
        "Copy the file somewhere safe before anything else, then restore a backup, "
        "or recover what can be read with `remember --db NEW.db import --from-db COPY`.\n";
    char *db = make_temp_db_path();
    const char *seed[] = {"add", "seed"};
    const char *repair[] = {"repair", "search-index"};
    const char *json[] = {"--json", "repair", "search-index"};
    char want[CLI_JSON_CAP];
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, seed, sizeof(seed) / sizeof(seed[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    ASSERT_EQ_INT(setenv("REMEMBER_TEST_FAIL_READ", "7", 1), 0);
    r = run_remember(db, repair, sizeof(repair) / sizeof(repair[0]), NULL);
    ASSERT_EQ_INT(unsetenv("REMEMBER_TEST_FAIL_READ"), 0);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, k_plain);
    cmd_result_free(&r);

    ASSERT_EQ_INT(setenv("REMEMBER_TEST_FAIL_READ", "7", 1), 0);
    r = run_remember(db, json, sizeof(json) / sizeof(json[0]), NULL);
    ASSERT_EQ_INT(unsetenv("REMEMBER_TEST_FAIL_READ"), 0);
    (void)snprintf(
        want, sizeof(want),
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"repair.search_index.result"
        "\",\"error\":{\"code\":\"corrupt\",\"message\":\"database error: the brain file is d"
        "amaged\",\"fix\":\"Copy the file somewhere safe before anything else, then restore a"
        " backup, or recover what can be read with `remember --db NEW.db import --from-db COP"
        "Y`.\",\"retryable\":false,\"detail\":{\"sqlite_code\":10,\"sqlite_extended\":266,\"s"
        "qlite_name\":\"SQLITE_IOERR_READ\",\"sqlite_message\":\"disk I/O error\",\"errno\":%"
        "d,\"path\":\"%s\",\"fts_only\":false}}}\n",
        ENOENT, db);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, want);
    cmd_result_free(&r);
    free(db);
}

/* A large read budget still lists the seeded row. N=0 above is the failure side. */
TEST(cli_fail_read_budget_lets_a_list_finish)
{
    char *db = make_temp_db_path();
    const char *seed[] = {"add", "seed"};
    const char *list[] = {"list"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, seed, sizeof(seed) / sizeof(seed[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    ASSERT_EQ_INT(setenv("REMEMBER_TEST_FAIL_READ", "100000", 1), 0);
    r = run_remember(db, list, sizeof(list) / sizeof(list[0]), NULL);
    ASSERT_EQ_INT(unsetenv("REMEMBER_TEST_FAIL_READ"), 0);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STR_CONTAINS(r.out, "seed");
    cmd_result_free(&r);
    free(db);
}

/* X-E02 / D6: the shim returns IOERR_NOMEM. SQLite's apiHandleError rewrites
   that to SQLITE_NOMEM before the store sees it (Itay 2026-10-15: lock the
   observed name). The class is still database. */
TEST(cli_nomem_shim_reports_database)
{
    static const char *const k_plain =
        "remember: database error (SQLITE_NOMEM). Try again. If it keeps happening, report "
        "the details from --json.\n";
    char *db = make_temp_db_path();
    const char *add[] = {"add", "nomem"};
    const char *json[] = {"--json", "add", "nomem"};
    char want[CLI_JSON_CAP];
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    ASSERT_EQ_INT(setenv("REMEMBER_TEST_NOMEM", "1", 1), 0);
    r = run_remember(db, add, sizeof(add) / sizeof(add[0]), NULL);
    ASSERT_EQ_INT(unsetenv("REMEMBER_TEST_NOMEM"), 0);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, k_plain);
    cmd_result_free(&r);

    ASSERT_EQ_INT(setenv("REMEMBER_TEST_NOMEM", "1", 1), 0);
    r = run_remember(db, json, sizeof(json) / sizeof(json[0]), NULL);
    ASSERT_EQ_INT(unsetenv("REMEMBER_TEST_NOMEM"), 0);
    (void)snprintf(
        want, sizeof(want),
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.add.result\",\"erro"
        "r\":{\"code\":\"database\",\"message\":\"database error\",\"fix\":\"Try again. If it"
        " keeps happening, report the details from --json.\",\"retryable\":false,\"det"
        "ail\":{\"sqlite_code\":7,\"sqlite_extended\":7,\"sqlite_name\":\"SQLITE_NOMEM\",\"sq"
        "lite_message\":\"out of memory\"}}}\n");
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, want);
    cmd_result_free(&r);
    free(db);
}

/* Same rewrite on a read: list of an existing brain is also SQLITE_NOMEM. */
TEST(cli_nomem_shim_fails_a_list)
{
    static const char *const k_plain =
        "remember: database error (SQLITE_NOMEM). Try again. If it keeps happening, report "
        "the details from --json.\n";
    char *db = make_temp_db_path();
    const char *seed[] = {"add", "seed"};
    const char *list[] = {"list"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, seed, sizeof(seed) / sizeof(seed[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    ASSERT_EQ_INT(setenv("REMEMBER_TEST_NOMEM", "1", 1), 0);
    r = run_remember(db, list, sizeof(list) / sizeof(list[0]), NULL);
    ASSERT_EQ_INT(unsetenv("REMEMBER_TEST_NOMEM"), 0);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, k_plain);
    cmd_result_free(&r);
    free(db);
}

// NOLINTEND(concurrency-mt-unsafe)
#endif /* REMEMBER_TEST_VFS */

/* E4.13: 60001, -1 and abc are usage, with that exact sentence. Nothing is created. */
TEST(cli_busy_timeout_out_of_range_is_usage)
{
    static const char *const k_plain = "remember: invalid --busy-timeout (must be 0..60000)\n";
    static const char *const k_json =
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"unknown.result\",\"err"
        "or\":{\"code\":\"usage\",\"message\":\"invalid --busy-timeout (must be 0..60000)\","
        "\"fix\":\"Run 'remember --help' to see the commands.\",\"retryable\":false}}\n";
    char *db = make_temp_db_path();
    const char *hi[] = {"--busy-timeout", "60001", "list"};
    const char *neg[] = {"--busy-timeout", "-1", "list"};
    const char *word[] = {"--busy-timeout", "abc", "list"};
    const char *spaced[] = {"--busy-timeout", " 9", "list"};
    const char *json[] = {"--json", "--busy-timeout", "60001", "list"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, hi, sizeof(hi) / sizeof(hi[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, k_plain);
    ASSERT_TRUE(access(db, F_OK) != 0);
    cmd_result_free(&r);
    r = run_remember(db, neg, sizeof(neg) / sizeof(neg[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, k_plain);
    cmd_result_free(&r);
    r = run_remember(db, word, sizeof(word) / sizeof(word[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, k_plain);
    cmd_result_free(&r);
    r = run_remember(db, spaced, sizeof(spaced) / sizeof(spaced[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, k_plain);
    cmd_result_free(&r);
    r = run_remember(db, json, sizeof(json) / sizeof(json[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, k_json);
    cmd_result_free(&r);
    free(db);
}

/* E4.13: a held write lock plus --busy-timeout 500 is busy after about half a
   second, not after the 5000 ms default. */
enum { BUSY_FLAG_WAIT_MIN_MS = 250, BUSY_FLAG_WAIT_MAX_MS = 2000 };
enum { MS_PER_SEC = 1000, NS_PER_MS = 1000000 };

static long elapsed_ms(const struct timespec *start, const struct timespec *end)
{
    long sec = (long)(end->tv_sec - start->tv_sec);
    long nsec = end->tv_nsec - start->tv_nsec;

    return (sec * MS_PER_SEC) + (nsec / NS_PER_MS);
}

TEST(cli_busy_timeout_500_reports_busy_within_half_a_second)
{
    static const char *const k_plain =
        "remember: database error: the brain is locked by another process (SQLITE_BUSY). "
        "Wait a moment and try again. Another app or a terminal may be using this brain.\n";
    char *db = make_temp_db_path();
    const char *seed[] = {"add", "seed"};
    const char *add[] = {"--busy-timeout", "500", "add", "blocked"};
    sqlite3 *lock = NULL;
    struct timespec start;
    struct timespec end;
    long waited = 0;
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, seed, sizeof(seed) / sizeof(seed[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);

    ASSERT_EQ_INT(sqlite3_open_v2(db, &lock, SQLITE_OPEN_READWRITE, NULL), SQLITE_OK);
    ASSERT_EQ_INT(sqlite3_exec(lock, "BEGIN EXCLUSIVE;", NULL, NULL, NULL), SQLITE_OK);

    ASSERT_EQ_INT(clock_gettime(CLOCK_MONOTONIC, &start), 0);
    r = run_remember(db, add, sizeof(add) / sizeof(add[0]), NULL);
    ASSERT_EQ_INT(clock_gettime(CLOCK_MONOTONIC, &end), 0);
    waited = elapsed_ms(&start, &end);

    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, k_plain);
    ASSERT_TRUE(waited >= BUSY_FLAG_WAIT_MIN_MS);
    ASSERT_TRUE(waited < BUSY_FLAG_WAIT_MAX_MS);
    cmd_result_free(&r);

    {
        static const char *const k_json =
            "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.add.result\",\"erro"
            "r\":{\"code\":\"busy\",\"message\":\"database error: the brain is locked by another "
            "process\",\"fix\":\"Wait a moment and try again. Another app or a terminal may be us"
            "ing this brain.\",\"retryable\":true,\"detail\":{\"sqlite_code\":5,\"sqlite_extended"
            "\":5,\"sqlite_name\":\"SQLITE_BUSY\",\"sqlite_message\":\"database is locked\"}}}\n";
        const char *json[] = {"--json", "--busy-timeout", "500", "add", "blocked"};

        r = run_remember(db, json, sizeof(json) / sizeof(json[0]), NULL);
        ASSERT_EQ_INT(r.exit_code, 1);
        ASSERT_STREQ(r.err, "");
        ASSERT_STREQ(r.out, k_json);
        cmd_result_free(&r);
    }
    if (lock != NULL) {
        (void)sqlite3_exec(lock, "ROLLBACK;", NULL, NULL, NULL);
        (void)sqlite3_close(lock);
    }
    free(db);
}

/* E4.11 / D8: --no-create on a missing parent creates no folder, file, or sidecar. */
TEST(cli_no_create_missing_path_creates_nothing)
{
    static const char *const k_plain =
        "remember: database error: cannot open the brain file (SQLITE_CANTOPEN). "
        "Check that the folder exists and that you can read and write it.\n";
    char tmpl[] = "/tmp/remember-nocreate-XXXXXX";
    char path[TEST_PATH_CAP];
    char sidecar[TEST_PATH_CAP];
    char want[CLI_JSON_CAP];
    const char *list[] = {"--no-create", "list"};
    const char *json[] = {"--json", "--no-create", "list"};
    const char *dir = NULL;
    int wrote = 0;
    CmdResult r;

    dir = mkdtemp(tmpl);
    ASSERT_TRUE(dir != NULL);
    if (dir == NULL) {
        return;
    }
    ASSERT_EQ_INT(rmdir(tmpl), 0);
    wrote = snprintf(path, sizeof(path), "%s/x.db", tmpl);
    ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(path));
    wrote = snprintf(sidecar, sizeof(sidecar), "%s.d/device_id", path);
    ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(sidecar));

    r = run_remember(path, list, sizeof(list) / sizeof(list[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, k_plain);
    ASSERT_TRUE(access(tmpl, F_OK) != 0);
    ASSERT_TRUE(access(path, F_OK) != 0);
    ASSERT_TRUE(access(sidecar, F_OK) != 0);
    cmd_result_free(&r);

    r = run_remember(path, json, sizeof(json) / sizeof(json[0]), NULL);
    wrote = snprintf(
        want, sizeof(want),
        "{\"format\":\"remember-message\",\"version\":1,\"type\":\"memory.list.result\",\"err"
        "or\":{\"code\":\"cannot_open\",\"message\":\"database error: cannot open the brain f"
        "ile\",\"fix\":\"Check that the folder exists and that you can read and write it.\","
        "\"retryable\":false,\"detail\":{\"sqlite_code\":14,\"sqlite_extended\":14,\"sqlite_n"
        "ame\":\"SQLITE_CANTOPEN\",\"sqlite_message\":\"unable to open database file\",\"errn"
        "o\":%d,\"path\":\"%s\",\"reason\":\"missing\"}}}\n",
        ENOENT, path);
    ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(want));
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.err, "");
    ASSERT_STREQ(r.out, want);
    ASSERT_TRUE(access(tmpl, F_OK) != 0);
    ASSERT_TRUE(access(path, F_OK) != 0);
    ASSERT_TRUE(access(sidecar, F_OK) != 0);
    cmd_result_free(&r);
}

/* D8: the parent already exists, so only omitting SQLITE_OPEN_CREATE keeps the file away. */
TEST(cli_no_create_missing_file_creates_nothing)
{
    char *db = make_temp_db_path();
    char sidecar[TEST_PATH_CAP];
    const char *list[] = {"--no-create", "list"};
    int wrote = 0;
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    wrote = snprintf(sidecar, sizeof(sidecar), "%s.d/device_id", db);
    ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(sidecar));
    r = run_remember(db, list, sizeof(list) / sizeof(list[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STR_CONTAINS(r.err, "cannot open the brain file");
    ASSERT_STR_CONTAINS(r.err, "(SQLITE_CANTOPEN)");
    ASSERT_TRUE(access(db, F_OK) != 0);
    ASSERT_TRUE(access(sidecar, F_OK) != 0);
    cmd_result_free(&r);
    free(db);
}

/* --no-create still opens a brain that is already there. */
TEST(cli_no_create_existing_brain_opens)
{
    char *db = make_temp_db_path();
    const char *seed[] = {"add", "present"};
    const char *list[] = {"--no-create", "list"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, seed, sizeof(seed) / sizeof(seed[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    r = run_remember(db, list, sizeof(list) / sizeof(list[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    ASSERT_STR_CONTAINS(r.out, "present");
    cmd_result_free(&r);
    free(db);
}

/* E4.13 inclusive lower bound: 0 is a real timeout, not "use the 5000 default".
   The locked run is timed against the unlocked seed run (spawn + schema + add),
   not a fixed bound: a slow sanitizer container stretches both. Half the default
   leaves room for spawn jitter while a mutant that waits the default still fails. */
enum { BUSY_ZERO_EXTRA_MAX_MS = STORE_DEFAULT_BUSY_TIMEOUT_MS / 2 };

TEST(cli_busy_timeout_zero_is_busy_immediately)
{
    char *db = make_temp_db_path();
    const char *seed[] = {"add", "seed"};
    const char *add[] = {"--busy-timeout", "0", "add", "blocked"};
    sqlite3 *lock = NULL;
    struct timespec start;
    struct timespec end;
    long baseline = 0;
    long waited = 0;
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    ASSERT_EQ_INT(clock_gettime(CLOCK_MONOTONIC, &start), 0);
    r = run_remember(db, seed, sizeof(seed) / sizeof(seed[0]), NULL);
    ASSERT_EQ_INT(clock_gettime(CLOCK_MONOTONIC, &end), 0);
    baseline = elapsed_ms(&start, &end);
    ASSERT_EQ_INT(r.exit_code, 0);
    cmd_result_free(&r);
    ASSERT_EQ_INT(sqlite3_open_v2(db, &lock, SQLITE_OPEN_READWRITE, NULL), SQLITE_OK);
    ASSERT_EQ_INT(sqlite3_exec(lock, "BEGIN EXCLUSIVE;", NULL, NULL, NULL), SQLITE_OK);
    ASSERT_EQ_INT(clock_gettime(CLOCK_MONOTONIC, &start), 0);
    r = run_remember(db, add, sizeof(add) / sizeof(add[0]), NULL);
    ASSERT_EQ_INT(clock_gettime(CLOCK_MONOTONIC, &end), 0);
    waited = elapsed_ms(&start, &end);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STR_CONTAINS(r.err, "(SQLITE_BUSY)");
    ASSERT_TRUE(waited - baseline < BUSY_ZERO_EXTRA_MAX_MS);
    cmd_result_free(&r);
    if (lock != NULL) {
        (void)sqlite3_exec(lock, "ROLLBACK;", NULL, NULL, NULL);
        (void)sqlite3_close(lock);
    }
    free(db);
}

/* E4.13 inclusive upper bound. Accepted means list runs; it does not wait 60 s. */
TEST(cli_busy_timeout_60000_is_accepted)
{
    char *db = make_temp_db_path();
    const char *list[] = {"--busy-timeout", "60000", "list"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, list, sizeof(list) / sizeof(list[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 0);
    ASSERT_STREQ(r.err, "");
    cmd_result_free(&r);
    free(db);
}

/* A missing token follows --db. A present token, even "-1", is the value (E4.13). */
TEST(cli_busy_timeout_without_a_value_is_usage)
{
    char *db = make_temp_db_path();
    const char *args[] = {"--busy-timeout"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, "remember: --busy-timeout: option requires a value\n");
    ASSERT_TRUE(access(db, F_OK) != 0);
    cmd_result_free(&r);
    free(db);
}

/* The one decimal parser behind --limit, --offset, --facet-limit and
   --busy-timeout takes ASCII digits only. strtoull alone skips leading space
   and takes a sign, so it would wrap " -1" to ULLONG_MAX (0.2.0 accepted
   --offset " -1" that way) and read "+5" and " 7" as numbers
   (decision:size-option-digits-only). */
TEST(util_parse_nonneg_rejects_every_sign)
{
    static const char *const k_bad[] = {"",    "-1",  " -1", "\t-5",
                                        "abc", "5x",  "-0",  "+5",
                                        " 7",  "\t7", "7 ",  "99999999999999999999999"};
    unsigned long long value = 0;
    size_t i = 0;

    ASSERT_EQ_INT(util_parse_nonneg("0", &value), 0);
    ASSERT_TRUE(value == 0ULL);
    ASSERT_EQ_INT(util_parse_nonneg("60000", &value), 0);
    ASSERT_TRUE(value == 60000ULL);
    ASSERT_EQ_INT(util_parse_nonneg("007", &value), 0);
    ASSERT_TRUE(value == 7ULL);
    for (i = 0; i < sizeof(k_bad) / sizeof(k_bad[0]); i++) {
        ASSERT_EQ_INT(util_parse_nonneg(k_bad[i], &value), -1);
    }
    ASSERT_EQ_INT(util_parse_nonneg(NULL, &value), -1);
}

/* Through the CLI: a spaced negative offset is the usage line, not a huge offset. */
TEST(cli_offset_spaced_negative_is_usage)
{
    char *db = make_temp_db_path();
    const char *args[] = {"list", "--limit", " -1"};
    CmdResult r;

    ASSERT_TRUE(db != NULL);
    if (db == NULL) {
        return;
    }
    r = run_remember(db, args, sizeof(args) / sizeof(args[0]), NULL);
    ASSERT_EQ_INT(r.exit_code, 1);
    ASSERT_STREQ(r.out, "");
    ASSERT_STREQ(r.err, "remember: invalid --limit (must be 1..1000)\n");
    cmd_result_free(&r);
    free(db);
}

void register_errors_tests(void)
{
    RUN_TEST(util_absolute_path_joins_cwd_and_normalizes_lexically);
    RUN_TEST(util_absolute_path_keeps_a_symlink_component);
    RUN_TEST(store_open_text_file_reports_not_a_database);
    RUN_TEST(store_write_unwritable_dir_reports_read_only);
    RUN_TEST(store_open_no_create_classifies_cannot_open);
    RUN_TEST(store_write_locked_db_reports_busy);
    RUN_TEST(store_open_truncated_db_reports_corrupt);
    RUN_TEST(store_open_no_create_existing_opens_clean);
#ifdef REMEMBER_TEST_HOOKS
    RUN_TEST(exec_schema_retry_runs_once_more_then_database);
    RUN_TEST(store_device_new_clears_a_prior_sqlite_failure);
    RUN_TEST(sqlite_mapping_covers_every_e47_row);
    RUN_TEST(sqlite_names_cover_table_and_primary_fallback);
    RUN_TEST(sqlite_open_reason_follows_errno_and_isdir);
    RUN_TEST(sqlite_message_copy_clamps_and_defaults);
#endif
    RUN_TEST(error_catalog_round_trips_every_e4_3_code);
    RUN_TEST(error_catalog_rejects_names_outside_e4_3);
    RUN_TEST(warning_catalog_round_trips_every_code);
    RUN_TEST(warning_catalog_rejects_names_outside_the_catalog);
    RUN_TEST(plain_error_is_remember_prefix_and_the_message);
    RUN_TEST(json_error_is_one_line_and_not_the_plain_line);
    RUN_TEST(json_keeps_only_the_first_error);
    RUN_TEST(diag_code_fix_is_the_codes_sentence);
    RUN_TEST(error_outside_the_catalog_writes_nothing);
    RUN_TEST(json_error_escapes_message_bytes);
    RUN_TEST(every_error_code_json_message_matches_the_plain_line);
    RUN_TEST(plain_warning_is_remember_warning);
    RUN_TEST(json_warning_is_one_line_without_the_prefix);
    RUN_TEST(every_warning_uses_the_warning_prefix);
    RUN_TEST(plain_warning_that_cannot_be_written_does_not_count);
    RUN_TEST(unknown_field_that_cannot_be_written_does_not_stick);
    RUN_TEST(null_message_is_empty_text);
    RUN_TEST(plain_error_keeps_quote_and_backslash);
    RUN_TEST(plain_error_turns_a_newline_into_a_question_mark);
    RUN_TEST(plain_error_turns_a_carriage_return_into_a_question_mark);
    RUN_TEST(plain_error_keeps_a_tab);
    RUN_TEST(plain_error_turns_an_escape_into_a_question_mark);
    RUN_TEST(plain_error_turns_del_into_a_question_mark);
    RUN_TEST(error_and_warning_leave_stdout_empty);
    RUN_TEST(warning_outside_the_catalog_writes_nothing);
    RUN_TEST(plain_missing_command_is_unchanged);
    RUN_TEST(json_usage_error_is_one_stdout_envelope);
    RUN_TEST(json_help_with_an_unknown_topic_is_an_envelope);
    RUN_TEST(json_not_found_is_one_stdout_envelope);
    RUN_TEST(plain_not_found_is_unchanged);
    RUN_TEST(plain_trash_warning_uses_warning_prefix);
    RUN_TEST(json_trash_warning_is_in_the_list_envelope);
    RUN_TEST(json_synced_folder_warning_is_in_the_list_envelope);
    RUN_TEST(store_text_codes_follow_the_locked_split);
    RUN_TEST(err_fmt_keeps_a_long_unknown_option);
    RUN_TEST(internal_parse_stays_a_plain_internal_line);
    RUN_TEST(failing_flush_stream_accepts_a_write_and_fails_the_flush);
    RUN_TEST(rejecting_stream_fails_a_write_and_sets_the_error_flag);
    RUN_TEST(run_reports_output_that_fails_at_the_final_flush);
    RUN_TEST(run_reports_a_write_the_command_did_not_check);
    RUN_TEST(run_reports_a_failed_json_flush_on_stderr);
    RUN_TEST(json_help_and_version_print_text);
    RUN_TEST(run_keeps_one_error_line_when_the_command_failed);
    RUN_TEST(search_reports_a_failed_facet_line);
    RUN_TEST(search_reports_a_failed_entry_line_without_facets);
    RUN_TEST(list_reports_a_failed_entry_line);
    RUN_TEST(golden_error_lines_and_d9_codes);
    RUN_TEST(sqlite_busy_reports_cause_fix_and_detail);
    RUN_TEST(sqlite_cannot_open_carries_errno_reason_and_absolute_path);
    RUN_TEST(sqlite_database_keeps_its_own_text_without_a_path);
    RUN_TEST(sqlite_emitter_rejects_misuse_quietly);
    RUN_TEST(sqlite_corrupt_marks_fts_only_and_keeps_base_text);
    RUN_TEST(store_class_maps_to_its_diag_code);
    RUN_TEST(cli_text_file_reports_not_a_database);
    RUN_TEST(cli_import_text_source_names_the_source);
    RUN_TEST(cli_relative_db_emits_absolute_detail_path);
    RUN_TEST(cli_read_only_file_reports_read_only_on_write);
    RUN_TEST(cli_readonly_directory_reports_read_only);
    RUN_TEST(cli_mode_000_reports_cannot_open_permission);
    RUN_TEST(cli_damaged_fts_search_reports_fts_only);
    RUN_TEST(cli_repair_search_index_rebuilds);
    RUN_TEST(cli_repair_search_index_human_rebuilt);
    RUN_TEST(cli_repair_counts_deleted_entries);
    RUN_TEST(cli_repair_refuses_damaged_base_table);
    RUN_TEST(cli_repair_heals_page_damaged_fts_data);
    RUN_TEST(cli_repair_heals_page_damaged_fts_content);
    RUN_TEST(cli_damaged_tags_is_brain_damage_not_fts_only);
#ifdef REMEMBER_TEST_HOOKS
    RUN_TEST(store_connection_is_defensive_before_and_after_repair);
#endif
    RUN_TEST(cli_repair_refuses_truncated_brain);
    RUN_TEST(cli_search_on_damaged_base_is_not_fts_only);
    RUN_TEST(cli_repair_usage_is_search_index);
    RUN_TEST(cli_repair_missing_brain_creates_nothing);
    RUN_TEST(cli_repair_usage_creates_no_brain);
    RUN_TEST(cli_repair_help_names_the_command);
    RUN_TEST(cli_help_names_json_busy_timeout_and_no_create);
    RUN_TEST(cli_truncated_brain_reports_corrupt);
#ifdef REMEMBER_TEST_VFS
    RUN_TEST(cli_enospc_shim_reports_disk_full);
    RUN_TEST(cli_fail_write_shim_reports_io_error);
    RUN_TEST(cli_fail_write_budget_lets_an_add_finish);
    RUN_TEST(cli_fail_read_shim_reports_io_error);
    RUN_TEST(cli_repair_read_fault_during_check_is_corrupt);
    RUN_TEST(cli_repair_that_cannot_write_never_points_back_at_repair);
    RUN_TEST(cli_fail_read_budget_lets_a_list_finish);
    RUN_TEST(cli_nomem_shim_reports_database);
    RUN_TEST(cli_nomem_shim_fails_a_list);
#else
    (void)printf("SKIP the VFS shim tests (remember built without REMEMBER_TEST_VFS)\n");
#endif
    RUN_TEST(sqlite_out_of_range_reason_falls_back_to_other);
    RUN_TEST(err_store_sqlite_misuse_and_clean_store_fall_back);
    RUN_TEST(cli_db_under_a_file_keeps_the_legacy_open_line);
    RUN_TEST(util_parse_nonneg_rejects_every_sign);
    RUN_TEST(cli_offset_spaced_negative_is_usage);
    RUN_TEST(cli_busy_timeout_out_of_range_is_usage);
    RUN_TEST(cli_busy_timeout_500_reports_busy_within_half_a_second);
    RUN_TEST(cli_no_create_missing_path_creates_nothing);
    RUN_TEST(cli_no_create_missing_file_creates_nothing);
    RUN_TEST(cli_no_create_existing_brain_opens);
    RUN_TEST(cli_busy_timeout_zero_is_busy_immediately);
    RUN_TEST(cli_busy_timeout_60000_is_accepted);
    RUN_TEST(cli_busy_timeout_without_a_value_is_usage);
}
