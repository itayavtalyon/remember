#include "jsonread.h"
#include "register.h"
#include "test.h"
#include "test_alloc.h"

#include <stdlib.h>
#include <string.h>

/*
 * Pure unit tests for jsonread (plan 23 stage 1, D3, P11). No CLI, no store.
 */

enum {
    NEST_BUF = (JSONREAD_NESTING_MAX * 2) + 16,
    UTF8_INVALID = 0xFF,
    UTF8_LEAD3 = 0xE0,
    UTF8_CONT = 0x80
};

/* Build "[" * depth + "0" + "]" * depth into buf. Returns byte length. */
static size_t nest_array(int depth, char *buf, size_t cap)
{
    size_t at = 0;
    int i = 0;

    for (i = 0; i < depth; i++) {
        if (at + 1U >= cap) {
            return 0;
        }
        buf[at++] = '[';
    }
    if (at + 1U >= cap) {
        return 0;
    }
    buf[at++] = '0';
    for (i = 0; i < depth; i++) {
        if (at + 1U >= cap) {
            return 0;
        }
        buf[at++] = ']';
    }
    buf[at] = '\0';
    return at;
}

static JsonValue *parse_ok(const char *src)
{
    JsonValue *root = NULL;
    JsonReadStatus st = jsonread_parse(src, strlen(src), &root);

    ASSERT_EQ_INT(st, JSONREAD_OK);
    ASSERT_TRUE(root != NULL);
    return root;
}

static void parse_fails(JsonReadStatus want, const char *src, size_t len)
{
    JsonValue not_null = {.type = JSON_NULL}; /* the parser must clear root */
    JsonValue *root = &not_null;
    JsonReadStatus st = jsonread_parse(src, len, &root);

    ASSERT_EQ_INT(st, want);
    ASSERT_TRUE(root == NULL);
}

TEST(jsonread_accepts_nesting_at_the_named_limit)
{
    char buf[NEST_BUF];
    size_t n = nest_array(JSONREAD_NESTING_MAX, buf, sizeof(buf));
    JsonValue *root = NULL;
    JsonReadStatus st = JSONREAD_ERR_INTERNAL;
    int depth = 0;
    JsonValue *cur = NULL;

    ASSERT_TRUE(n > 0U);
    st = jsonread_parse(buf, n, &root);
    ASSERT_EQ_INT(st, JSONREAD_OK);
    if (root == NULL) {
        return;
    }
    ASSERT_EQ_INT(root->type, JSON_ARRAY);
    ASSERT_EQ_INT(root->line, 1);
    cur = root;
    for (depth = 0; depth < JSONREAD_NESTING_MAX; depth++) {
        ASSERT_EQ_INT(cur->type, JSON_ARRAY);
        ASSERT_EQ_INT((int)cur->u.array.n, 1);
        cur = cur->u.array.items[0];
    }
    ASSERT_EQ_INT(cur->type, JSON_NUMBER);
    ASSERT_STREQ(cur->u.number.bytes, "0");
    jsonread_free(root);
}

TEST(jsonread_rejects_nesting_past_the_named_limit)
{
    char buf[NEST_BUF + 4];
    size_t n = nest_array(JSONREAD_NESTING_MAX + 1, buf, sizeof(buf));

    ASSERT_TRUE(n > 0U);
    parse_fails(JSONREAD_ERR_NESTING, buf, n);
}

TEST(jsonread_rejects_trailing_garbage)
{
    parse_fails(JSONREAD_ERR_SYNTAX, "0 1", strlen("0 1"));
    parse_fails(JSONREAD_ERR_SYNTAX, "truefalse", strlen("truefalse"));
    parse_fails(JSONREAD_ERR_SYNTAX, "[]{}", strlen("[]{}"));
    parse_fails(JSONREAD_ERR_SYNTAX, "\"a\"x", strlen("\"a\"x"));
}

TEST(jsonread_rejects_nan_and_infinity)
{
    parse_fails(JSONREAD_ERR_SYNTAX, "NaN", strlen("NaN"));
    parse_fails(JSONREAD_ERR_SYNTAX, "Infinity", strlen("Infinity"));
    parse_fails(JSONREAD_ERR_SYNTAX, "-Infinity", strlen("-Infinity"));
    parse_fails(JSONREAD_ERR_SYNTAX, "+1", strlen("+1"));
}

/* decision:plan23-review-g3-nul-is-import-parse: a NUL escape in a string or a key. */
TEST(jsonread_rejects_a_nul_escape)
{
    parse_fails(JSONREAD_ERR_SYNTAX, "\"a\\u0000b\"", strlen("\"a\\u0000b\""));
    parse_fails(JSONREAD_ERR_SYNTAX, "{\"\\u0000\":1}", strlen("{\"\\u0000\":1}"));
    jsonread_free(parse_ok("\"\\u0001\""));
}

TEST(jsonread_rejects_invalid_utf8_and_lone_surrogates)
{
    char bad1[] = {'"', (char)UTF8_INVALID, '"'};
    char bad2[] = {'"', (char)UTF8_LEAD3, (char)UTF8_CONT, (char)UTF8_CONT, '"'}; /* overlong */
    const char *lone_high = "\"\\uD800\"";
    const char *lone_low = "\"\\uDC00\"";
    const char *bad_pair = "\"\\uD800\\uD800\"";
    const char *ok_pair = "\"\\uD83D\\uDE00\""; /* U+1F600 */
    JsonValue *root = NULL;

    parse_fails(JSONREAD_ERR_UTF8, bad1, sizeof(bad1));
    parse_fails(JSONREAD_ERR_UTF8, bad2, sizeof(bad2));
    parse_fails(JSONREAD_ERR_UTF8, lone_high, strlen(lone_high));
    parse_fails(JSONREAD_ERR_UTF8, lone_low, strlen(lone_low));
    parse_fails(JSONREAD_ERR_UTF8, bad_pair, strlen(bad_pair));
    root = parse_ok(ok_pair);
    if (root == NULL) {
        return;
    }
    ASSERT_EQ_INT(root->type, JSON_STRING);
    ASSERT_EQ_INT((int)root->u.string.len, 4); /* F0 9F 98 80 */
    jsonread_free(root);
}

TEST(jsonread_tracks_the_one_based_line_of_each_value)
{
    JsonValue *root = parse_ok("{\n  \"a\": [\n    true,\n    null\n  ]\n}");
    const JsonValue *arr = NULL;

    if (root == NULL) {
        return;
    }
    ASSERT_EQ_INT(root->type, JSON_OBJECT);
    ASSERT_EQ_INT(root->line, 1);
    ASSERT_EQ_INT((int)root->u.object.n, 1);
    ASSERT_STREQ(root->u.object.members[0].key, "a");
    arr = root->u.object.members[0].value;
    ASSERT_EQ_INT(arr->type, JSON_ARRAY);
    ASSERT_EQ_INT(arr->line, 2);
    ASSERT_EQ_INT(arr->u.array.items[0]->line, 3);
    ASSERT_EQ_INT(arr->u.array.items[0]->type, JSON_BOOL);
    ASSERT_TRUE(arr->u.array.items[0]->u.boolean);
    ASSERT_EQ_INT(arr->u.array.items[1]->line, 4);
    ASSERT_EQ_INT(arr->u.array.items[1]->type, JSON_NULL);
    jsonread_free(root);
}

TEST(jsonread_keeps_duplicate_object_members)
{
    JsonValue *root = parse_ok("{\"a\":1,\"a\":2}");

    if (root == NULL) {
        return;
    }
    ASSERT_EQ_INT((int)root->u.object.n, 2);
    ASSERT_STREQ(root->u.object.members[0].value->u.number.bytes, "1");
    ASSERT_STREQ(root->u.object.members[1].value->u.number.bytes, "2");
    jsonread_free(root);
}

TEST(jsonread_parses_numbers_as_raw_text)
{
    JsonValue *root = parse_ok("-12.50e+3");

    if (root == NULL) {
        return;
    }
    ASSERT_EQ_INT(root->type, JSON_NUMBER);
    ASSERT_STREQ(root->u.number.bytes, "-12.50e+3");
    jsonread_free(root);
}

TEST(jsonread_null_out_is_internal)
{
    ASSERT_EQ_INT(jsonread_parse("0", 1, NULL), JSONREAD_ERR_INTERNAL);
}

TEST(jsonread_reports_oom_on_the_first_allocation)
{
    JsonValue not_null = {.type = JSON_NULL}; /* the parser must clear root */
    JsonValue *root = &not_null;
    JsonReadStatus st = JSONREAD_OK;

    test_alloc_fail_after(0);
    st = jsonread_parse("0", 1, &root);
    test_alloc_fail_after(-1);
    ASSERT_EQ_INT(st, JSONREAD_ERR_OOM);
    ASSERT_TRUE(root == NULL);
}

TEST(jsonread_status_string_is_stable)
{
    ASSERT_STREQ(jsonread_status_string(JSONREAD_OK), "ok");
    ASSERT_STREQ(jsonread_status_string(JSONREAD_ERR_SYNTAX), "syntax");
    ASSERT_STREQ(jsonread_status_string(JSONREAD_ERR_UTF8), "utf8");
    ASSERT_STREQ(jsonread_status_string(JSONREAD_ERR_NESTING), "nesting");
    ASSERT_STREQ(jsonread_status_string(JSONREAD_ERR_OOM), "oom");
    ASSERT_STREQ(jsonread_status_string(JSONREAD_ERR_INTERNAL), "internal");
}

TEST(jsonread_parses_false)
{
    JsonValue *root = parse_ok("false");

    if (root == NULL) {
        return;
    }
    ASSERT_EQ_INT(root->type, JSON_BOOL);
    ASSERT_TRUE(!root->u.boolean);
    jsonread_free(root);
}

/* \u escapes encode as two- and three-byte UTF-8; hex digits may be lowercase. */
TEST(jsonread_encodes_u_escapes_as_utf8)
{
    JsonValue *root = parse_ok("\"\\u00e9\\u20AC\"");

    if (root == NULL) {
        return;
    }
    ASSERT_EQ_INT(root->type, JSON_STRING);
    ASSERT_EQ_INT((int)root->u.string.len, 5);
    ASSERT_TRUE(memcmp(root->u.string.bytes, "\xC3\xA9\xE2\x82\xAC", 5) == 0);
    jsonread_free(root);
}

TEST(jsonread_null_source_with_a_length_is_internal)
{
    parse_fails(JSONREAD_ERR_INTERNAL, NULL, 1);
}

TEST(jsonread_rejects_a_missing_separator_after_an_item)
{
    parse_fails(JSONREAD_ERR_SYNTAX, "[1 2]", strlen("[1 2]"));
    parse_fails(JSONREAD_ERR_SYNTAX, "{\"a\":1,5}", strlen("{\"a\":1,5}"));
    parse_fails(JSONREAD_ERR_SYNTAX, "{\"a\":1,\"b\" 2}", strlen("{\"a\":1,\"b\" 2}"));
    parse_fails(JSONREAD_ERR_SYNTAX, "{\"a\":1 \"b\":2}", strlen("{\"a\":1 \"b\":2}"));
}

void register_jsonread_tests(void)
{
    RUN_TEST(jsonread_accepts_nesting_at_the_named_limit);
    RUN_TEST(jsonread_rejects_nesting_past_the_named_limit);
    RUN_TEST(jsonread_rejects_trailing_garbage);
    RUN_TEST(jsonread_rejects_nan_and_infinity);
    RUN_TEST(jsonread_rejects_a_nul_escape);
    RUN_TEST(jsonread_rejects_invalid_utf8_and_lone_surrogates);
    RUN_TEST(jsonread_tracks_the_one_based_line_of_each_value);
    RUN_TEST(jsonread_keeps_duplicate_object_members);
    RUN_TEST(jsonread_parses_numbers_as_raw_text);
    RUN_TEST(jsonread_null_out_is_internal);
    RUN_TEST(jsonread_reports_oom_on_the_first_allocation);
    RUN_TEST(jsonread_status_string_is_stable);
    RUN_TEST(jsonread_parses_false);
    RUN_TEST(jsonread_encodes_u_escapes_as_utf8);
    RUN_TEST(jsonread_null_source_with_a_length_is_internal);
    RUN_TEST(jsonread_rejects_a_missing_separator_after_an_item);
}
