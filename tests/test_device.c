#include "harness.h"
#include "output.h"
#include "register.h"
#include "remember_app.h"
#include "store.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    DEVICE_ID_LEN = 36,
    DEVICE_ID_BUF = 37,
    DEVICE_JSON_BUF = 160,
    DEVICE_VV_BUF = 192,
    DEVICE_COUNTER_BUF = 48
};
enum { DEVICE_PATTERN_BUF = 64 };

static char *sidecar_text(const char *db)
{
    char path[REMEMBER_PATH_MAX];
    FILE *file = NULL;
    char buf[DEVICE_ID_BUF + 4];
    size_t n = 0;
    char *out = NULL;

    (void)snprintf(path, sizeof(path), "%s.device_id", db);
    /* NOLINTNEXTLINE(android-cloexec-fopen) */
    file = fopen(path, "r");
    if (file == NULL || fgets(buf, (int)sizeof(buf), file) == NULL) {
        if (file != NULL) {
            (void)fclose(file);
        }
        return NULL;
    }
    (void)fclose(file);
    n = strlen(buf);
    while (n > 0U && (buf[n - 1U] == '\n' || buf[n - 1U] == '\r')) {
        buf[--n] = '\0';
    }
    out = (char *)malloc(n + 1U);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, buf, n + 1U);
    return out;
}

/* json then key: the document, then the field to copy. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static int copy_json_field(const char *json, const char *key, char *out, size_t outlen)
{
    char pattern[DEVICE_PATTERN_BUF];
    const char *found = NULL;
    size_t i = 0;
    int wrote = 0;

    wrote = snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    if (wrote < 0 || (size_t)wrote >= sizeof(pattern) || json == NULL || out == NULL) {
        return -1;
    }
    found = strstr(json, pattern);
    if (found == NULL) {
        return -1;
    }
    found += (size_t)wrote;
    while (i + 1U < outlen && found[i] != '\0' && found[i] != '"') {
        out[i] = found[i];
        i++;
    }
    if (found[i] != '"') {
        return -1;
    }
    out[i] = '\0';
    return 0;
}

static int copy_version_vector(const char *json, char *out, size_t outlen)
{
    const char *found = NULL;
    const char *end = NULL;
    size_t n = 0;

    if (json == NULL || out == NULL) {
        return -1;
    }
    found = strstr(json, "\"version_vector\":");
    if (found == NULL) {
        return -1;
    }
    found += strlen("\"version_vector\":");
    if (*found != '{') {
        return -1;
    }
    end = strchr(found, '}');
    if (end == NULL) {
        return -1;
    }
    n = (size_t)(end - found) + 1U;
    if (n >= outlen) {
        return -1;
    }
    memcpy(out, found, n);
    out[n] = '\0';
    return 0;
}

TEST(device_prints_the_sidecar_id)
{
    char *db = make_temp_db_path();
    const char *human[] = {"device"};
    const char *json[] = {"--json", "device"};
    CmdResult result;
    char *id = NULL;
    char expect[DEVICE_JSON_BUF];

    ASSERT_TRUE(db != NULL);
    result = run_remember(db, human, sizeof(human) / sizeof(human[0]), NULL);
    ASSERT_EQ_INT(result.exit_code, 0);
    ASSERT_STREQ(result.err, "");
    id = sidecar_text(db);
    ASSERT_TRUE(id != NULL);
    ASSERT_EQ_INT(id != NULL ? (int)strlen(id) : 0, DEVICE_ID_LEN);
    trim_trailing_newlines(result.out);
    ASSERT_STREQ(result.out, id != NULL ? id : "");
    cmd_result_free(&result);

    result = run_remember(db, json, sizeof(json) / sizeof(json[0]), NULL);
    ASSERT_EQ_INT(result.exit_code, 0);
    (void)snprintf(expect, sizeof(expect), "{\"version\":1,\"device_id\":\"%s\"}\n",
                   id != NULL ? id : "");
    ASSERT_STREQ(result.out, expect);
    cmd_result_free(&result);
    free(id);
    free(db);
}

TEST(device_new_keeps_the_old_vector_and_counts_the_next_edit_under_the_new_id)
{
    char *db = make_temp_db_path();
    const char *add[] = {"add", "--key", "pref:editor", "helix"};
    const char *show[] = {"device"};
    const char *mint_json[] = {"--json", "device", "new"};
    const char *get[] = {"--json", "get", "--key", "pref:editor"};
    const char *edit[] = {"update", "--key", "pref:editor", "--text", "nvim"};
    CmdResult result;
    char *first = NULL;
    char *second = NULL;
    char before[DEVICE_VV_BUF];
    char after_mint[DEVICE_VV_BUF];
    char after_edit[DEVICE_VV_BUF];
    char old_counter[DEVICE_COUNTER_BUF];
    char new_counter[DEVICE_COUNTER_BUF];
    char expect[DEVICE_JSON_BUF];
    char minted[DEVICE_ID_BUF];
    char previous[DEVICE_ID_BUF];

    ASSERT_TRUE(db != NULL);
    result = run_remember(db, add, sizeof(add) / sizeof(add[0]), NULL);
    ASSERT_EQ_INT(result.exit_code, 0);
    cmd_result_free(&result);

    result = run_remember(db, show, sizeof(show) / sizeof(show[0]), NULL);
    ASSERT_EQ_INT(result.exit_code, 0);
    trim_trailing_newlines(result.out);
    first = sidecar_text(db);
    ASSERT_STREQ(result.out, first != NULL ? first : "");
    cmd_result_free(&result);

    result = run_remember(db, get, sizeof(get) / sizeof(get[0]), NULL);
    ASSERT_EQ_INT(result.exit_code, 0);
    ASSERT_EQ_INT(copy_version_vector(result.out, before, sizeof(before)), 0);
    cmd_result_free(&result);

    result = run_remember(db, mint_json, sizeof(mint_json) / sizeof(mint_json[0]), NULL);
    ASSERT_EQ_INT(result.exit_code, 0);
    ASSERT_STREQ(result.err, "");
    ASSERT_EQ_INT(copy_json_field(result.out, "previous", previous, sizeof(previous)), 0);
    ASSERT_STREQ(previous, first != NULL ? first : "");
    ASSERT_EQ_INT(copy_json_field(result.out, "device_id", minted, sizeof(minted)), 0);
    ASSERT_TRUE(strcmp(minted, previous) != 0);
    (void)snprintf(expect, sizeof(expect),
                   "{\"version\":1,\"device_id\":\"%s\",\"previous\":\"%s\"}\n", minted, previous);
    ASSERT_STREQ(result.out, expect);
    cmd_result_free(&result);
    second = sidecar_text(db);
    ASSERT_STREQ(second != NULL ? second : "", minted);

    result = run_remember(db, show, sizeof(show) / sizeof(show[0]), NULL);
    ASSERT_EQ_INT(result.exit_code, 0);
    trim_trailing_newlines(result.out);
    ASSERT_STREQ(result.out, minted);
    cmd_result_free(&result);

    result = run_remember(db, get, sizeof(get) / sizeof(get[0]), NULL);
    ASSERT_EQ_INT(result.exit_code, 0);
    ASSERT_EQ_INT(copy_version_vector(result.out, after_mint, sizeof(after_mint)), 0);
    ASSERT_STREQ(after_mint, before);
    cmd_result_free(&result);

    result = run_remember(db, edit, sizeof(edit) / sizeof(edit[0]), NULL);
    ASSERT_EQ_INT(result.exit_code, 0);
    cmd_result_free(&result);
    result = run_remember(db, get, sizeof(get) / sizeof(get[0]), NULL);
    ASSERT_EQ_INT(result.exit_code, 0);
    ASSERT_EQ_INT(copy_version_vector(result.out, after_edit, sizeof(after_edit)), 0);
    (void)snprintf(old_counter, sizeof(old_counter), "\"%s\":1", first != NULL ? first : "");
    (void)snprintf(new_counter, sizeof(new_counter), "\"%s\":1", second != NULL ? second : "");
    ASSERT_STR_CONTAINS(after_edit, old_counter);
    ASSERT_STR_CONTAINS(after_edit, new_counter);
    (void)snprintf(old_counter, sizeof(old_counter), "\"%s\":2", first != NULL ? first : "");
    ASSERT_STR_NOT_CONTAINS(after_edit, old_counter);
    cmd_result_free(&result);
    free(first);
    free(second);
    free(db);
}

TEST(device_new_human_prints_only_the_new_id)
{
    char *db = make_temp_db_path();
    const char *show[] = {"device"};
    const char *mint[] = {"device", "new"};
    CmdResult result;
    char *first = NULL;
    char *second = NULL;

    ASSERT_TRUE(db != NULL);
    result = run_remember(db, show, sizeof(show) / sizeof(show[0]), NULL);
    ASSERT_EQ_INT(result.exit_code, 0);
    cmd_result_free(&result);
    first = sidecar_text(db);

    result = run_remember(db, mint, sizeof(mint) / sizeof(mint[0]), NULL);
    ASSERT_EQ_INT(result.exit_code, 0);
    ASSERT_STREQ(result.err, "");
    ASSERT_STR_NOT_CONTAINS(result.out, "previous");
    trim_trailing_newlines(result.out);
    second = sidecar_text(db);
    ASSERT_STREQ(result.out, second != NULL ? second : "");
    ASSERT_TRUE(first != NULL && second != NULL && strcmp(first, second) != 0);
    cmd_result_free(&result);
    free(first);
    free(second);
    free(db);
}

TEST(device_rejects_extra_words_and_options)
{
    char *db = make_temp_db_path();
    const char *extra[] = {"device", "extra"};
    const char *tail[] = {"device", "new", "extra"};
    const char *option[] = {"device", "--bogus"};
    CmdResult result;

    ASSERT_TRUE(db != NULL);
    result = run_remember(db, extra, sizeof(extra) / sizeof(extra[0]), NULL);
    ASSERT_EQ_INT(result.exit_code, 1);
    ASSERT_STR_CONTAINS(result.err, "device takes 'new' or no arguments");
    cmd_result_free(&result);

    result = run_remember(db, tail, sizeof(tail) / sizeof(tail[0]), NULL);
    ASSERT_EQ_INT(result.exit_code, 1);
    ASSERT_STR_CONTAINS(result.err, "device new takes no arguments");
    cmd_result_free(&result);

    result = run_remember(db, option, sizeof(option) / sizeof(option[0]), NULL);
    ASSERT_EQ_INT(result.exit_code, 1);
    ASSERT_STR_CONTAINS(result.err, "unknown option");
    cmd_result_free(&result);
    free(db);
}

TEST(help_device_describes_new)
{
    char *db = make_temp_db_path();
    const char *general[] = {"--help"};
    const char *topic[] = {"help", "device"};
    CmdResult result;

    ASSERT_TRUE(db != NULL);
    result = run_remember(db, general, sizeof(general) / sizeof(general[0]), NULL);
    ASSERT_EQ_INT(result.exit_code, 0);
    ASSERT_STR_CONTAINS(result.out, "Print the device id");
    cmd_result_free(&result);

    result = run_remember(db, topic, sizeof(topic) / sizeof(topic[0]), NULL);
    ASSERT_EQ_INT(result.exit_code, 0);
    ASSERT_STR_CONTAINS(result.out, "device new");
    ASSERT_STR_CONTAINS(result.out, "previous");
    ASSERT_STR_CONTAINS(result.out, "version vector");
    ASSERT_STR_CONTAINS(result.out, "restore or a copied brain");
    ASSERT_STR_CONTAINS(result.out, "invalid sidecar");
    cmd_result_free(&result);
    free(db);
}

TEST(device_new_json_previous_is_null_when_missing)
{
    char *buf = NULL;
    size_t len = 0U;
    FILE *out = NULL;
    const char *id = "018f6b2e-7c3a-7b2e-8f3a-7c3a7b2e8f3a";

    out = open_memstream(&buf, &len);
    ASSERT_TRUE(out != NULL);
    if (out == NULL) {
        return;
    }
    ASSERT_EQ_INT(output_device_new_envelope(out, id, NULL), 0);
    ASSERT_EQ_INT(fclose(out), 0);
    ASSERT_STREQ(buf, "{\"version\":1,\"device_id\":\"018f6b2e-7c3a-7b2e-8f3a-7c3a7b2e8f3a\","
                      "\"previous\":null}\n");
    free(buf);
}

/* db then text: the database path, then the sidecar contents. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void write_sidecar_text(const char *db, const char *text)
{
    char path[REMEMBER_PATH_MAX];
    FILE *file = NULL;

    (void)snprintf(path, sizeof(path), "%s.device_id", db);
    /* NOLINTNEXTLINE(android-cloexec-fopen) */
    file = fopen(path, "w");
    ASSERT_TRUE(file != NULL);
    if (file != NULL) {
        (void)fputs(text, file);
        (void)fclose(file);
    }
}

/* device new always mints (Itay 2026-10-13): it replaces a bad sidecar. Every
   other command, `device` and `device new extra` included, still refuses it
   and leaves the file alone (E14.4). */
TEST(device_new_replaces_an_invalid_sidecar)
{
    char *db = make_temp_db_path();
    const char *show[] = {"device"};
    const char *tail[] = {"device", "new", "extra"};
    const char *mint[] = {"--json", "device", "new"};
    char id[DEVICE_ID_BUF];
    char *side = NULL;
    CmdResult result;

    ASSERT_TRUE(db != NULL);
    result = run_remember(db, show, sizeof(show) / sizeof(show[0]), NULL);
    ASSERT_EQ_INT(result.exit_code, 0);
    cmd_result_free(&result);

    write_sidecar_text(db, "GARBAGE\n");
    result = run_remember(db, show, sizeof(show) / sizeof(show[0]), NULL);
    ASSERT_EQ_INT(result.exit_code, 1);
    ASSERT_STREQ(result.err, "remember: invalid device_id sidecar\n");
    cmd_result_free(&result);
    result = run_remember(db, tail, sizeof(tail) / sizeof(tail[0]), NULL);
    ASSERT_EQ_INT(result.exit_code, 1);
    cmd_result_free(&result);
    side = sidecar_text(db);
    ASSERT_STREQ(side != NULL ? side : "", "GARBAGE");
    free(side);

    result = run_remember(db, mint, sizeof(mint) / sizeof(mint[0]), NULL);
    ASSERT_EQ_INT(result.exit_code, 0);
    ASSERT_STREQ(result.err, "");
    ASSERT_EQ_INT(copy_json_field(result.out, "device_id", id, sizeof(id)), 0);
    /* Open minted over the bad file first, so previous is that id (D4). */
    ASSERT_STR_CONTAINS(result.out, "\"previous\":\"");
    side = sidecar_text(db);
    ASSERT_STREQ(side != NULL ? side : "", id);
    free(side);
    cmd_result_free(&result);

    result = run_remember(db, show, sizeof(show) / sizeof(show[0]), NULL);
    ASSERT_EQ_INT(result.exit_code, 0);
    ASSERT_STR_CONTAINS(result.out, id);
    cmd_result_free(&result);
    free(db);
}

void register_device_tests(void)
{
    RUN_TEST(device_prints_the_sidecar_id);
    RUN_TEST(device_new_keeps_the_old_vector_and_counts_the_next_edit_under_the_new_id);
    RUN_TEST(device_new_human_prints_only_the_new_id);
    RUN_TEST(device_rejects_extra_words_and_options);
    RUN_TEST(help_device_describes_new);
    RUN_TEST(device_new_json_previous_is_null_when_missing);
    RUN_TEST(device_new_replaces_an_invalid_sidecar);
}
