#include "normalize.h"
#include "register.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

/*
 * Pure unit tests for normalize + SHA-256 (step 03). No CLI, no store.
 */

TEST(body_trim_basic)
{
    char *out = NULL;
    size_t n = 0;
    NormStatus st;

    st = body_trim_copy("  hello  ", 9, &out, &n);
    ASSERT_EQ_INT(st, NORM_OK);
    ASSERT_TRUE(out != NULL);
    ASSERT_STREQ(out, "hello");
    ASSERT_EQ_INT((int)n, 5);
    free(out);
}

TEST(body_trim_all_ws_kinds)
{
    char *out = NULL;
    size_t n = 0;
    const char *in = " \t\n\r\v\fx\t\n ";
    NormStatus st = body_trim_copy(in, strlen(in), &out, &n);

    ASSERT_EQ_INT(st, NORM_OK);
    ASSERT_STREQ(out, "x");
    ASSERT_EQ_INT((int)n, 1);
    free(out);
}

TEST(body_trim_preserves_internal_ws)
{
    char *out = NULL;
    size_t n = 0;
    NormStatus st = body_trim_copy("  a  b  ", 8, &out, &n);

    ASSERT_EQ_INT(st, NORM_OK);
    ASSERT_STREQ(out, "a  b");
    ASSERT_EQ_INT((int)n, 4);
    free(out);
}

TEST(body_trim_empty_and_ws_only)
{
    char *out = NULL;
    size_t n = 99;

    ASSERT_EQ_INT(body_trim_copy("", 0, &out, &n), NORM_ERR_EMPTY);
    ASSERT_TRUE(out == NULL);
    ASSERT_EQ_INT((int)n, 0);

    ASSERT_EQ_INT(body_trim_copy(" \t\n", 3, &out, &n), NORM_ERR_EMPTY);
    ASSERT_TRUE(out == NULL);

    ASSERT_EQ_INT(body_trim_copy(NULL, 0, &out, &n), NORM_ERR_EMPTY);
}

TEST(body_trim_too_long)
{
    char *buf;
    char *out = NULL;
    size_t i;
    NormStatus st;

    buf = malloc((size_t)REMEMBER_BODY_MAX + 2U);
    if (buf == NULL) {
        ASSERT_TRUE(0); /* OOM — fail the test */
        return;
    }
    for (i = 0; i < (size_t)REMEMBER_BODY_MAX + 1U; i++) {
        buf[i] = 'a';
    }
    buf[REMEMBER_BODY_MAX + 1U] = '\0';

    st = body_trim_copy(buf, (size_t)REMEMBER_BODY_MAX + 1U, &out, NULL);
    ASSERT_EQ_INT(st, NORM_ERR_TOO_LONG);
    ASSERT_TRUE(out == NULL);

    /* Exactly max is OK. */
    st = body_trim_copy(buf, (size_t)REMEMBER_BODY_MAX, &out, NULL);
    ASSERT_EQ_INT(st, NORM_OK);
    ASSERT_TRUE(out != NULL);
    ASSERT_EQ_INT((int)strlen(out), REMEMBER_BODY_MAX);
    free(out);
    free(buf);
}

TEST(body_trim_invalid_utf8)
{
    char *out = NULL;
    char bad[] = {'a', (char)0xff, 'b'};
    NormStatus st = body_trim_copy(bad, 3, &out, NULL);

    ASSERT_EQ_INT(st, NORM_ERR_INVALID_UTF8);
    ASSERT_TRUE(out == NULL);
}

TEST(body_trim_valid_utf8_multibyte)
{
    /* "cafés" — é is C3 A9 */
    const char *in = "caf\xc3\xa9s";
    char *out = NULL;
    size_t n = 0;
    NormStatus st = body_trim_copy(in, strlen(in), &out, &n);

    ASSERT_EQ_INT(st, NORM_OK);
    ASSERT_STREQ(out, in);
    ASSERT_EQ_INT((int)n, (int)strlen(in));
    free(out);
}

TEST(body_hash_empty_and_abc)
{
    char hex[REMEMBER_SHA256_HEX_LEN + 1];

    body_hash_hex("", 0, hex);
    ASSERT_STREQ(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    body_hash_hex("abc", 3, hex);
    ASSERT_STREQ(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(body_hash_matches_trimmed)
{
    char *body = NULL;
    size_t n = 0;
    char hex[REMEMBER_SHA256_HEX_LEN + 1];
    char hex_direct[REMEMBER_SHA256_HEX_LEN + 1];

    ASSERT_EQ_INT(body_trim_copy("  abc  ", 7, &body, &n), NORM_OK);
    body_hash_hex(body, n, hex);
    body_hash_hex("abc", 3, hex_direct);
    ASSERT_STREQ(hex, hex_direct);
    free(body);
}

TEST(token_casefold_and_trim)
{
    char out[REMEMBER_TOKEN_MAX + 1];

    ASSERT_EQ_INT(normalize_token("  FooBar  ", out, sizeof(out)), NORM_OK);
    ASSERT_STREQ(out, "foobar");

    ASSERT_EQ_INT(normalize_tag("Pref:Editor", out, sizeof(out)), NORM_OK);
    ASSERT_STREQ(out, "pref:editor");

    ASSERT_EQ_INT(normalize_key("  HELLO_world-1.2/x  ", out, sizeof(out)), NORM_OK);
    ASSERT_STREQ(out, "hello_world-1.2/x");
}

TEST(token_tag_key_identical)
{
    char tag[REMEMBER_TOKEN_MAX + 1];
    char key[REMEMBER_TOKEN_MAX + 1];
    char tok[REMEMBER_TOKEN_MAX + 1];

    ASSERT_EQ_INT(normalize_tag("AbC:1", tag, sizeof(tag)), NORM_OK);
    ASSERT_EQ_INT(normalize_key("AbC:1", key, sizeof(key)), NORM_OK);
    ASSERT_EQ_INT(normalize_token("AbC:1", tok, sizeof(tok)), NORM_OK);
    ASSERT_STREQ(tag, key);
    ASSERT_STREQ(tag, tok);
}

TEST(token_rejects_empty_ws_control_space)
{
    char out[REMEMBER_TOKEN_MAX + 1];

    ASSERT_EQ_INT(normalize_token("", out, sizeof(out)), NORM_ERR_EMPTY);
    ASSERT_EQ_INT(normalize_token("   ", out, sizeof(out)), NORM_ERR_EMPTY);
    ASSERT_EQ_INT(normalize_token(NULL, out, sizeof(out)), NORM_ERR_EMPTY);

    ASSERT_EQ_INT(normalize_token("a b", out, sizeof(out)), NORM_ERR_INVALID_CHAR);
    ASSERT_EQ_INT(normalize_token("a\tb", out, sizeof(out)), NORM_ERR_INVALID_CHAR);

    {
        char ctrl[] = {'a', 0x01, 'b', '\0'};
        ASSERT_EQ_INT(normalize_token(ctrl, out, sizeof(out)), NORM_ERR_INVALID_CHAR);
    }
    {
        char del[] = {'k', 0x7f, '\0'};
        ASSERT_EQ_INT(normalize_key(del, out, sizeof(out)), NORM_ERR_INVALID_CHAR);
    }
}

TEST(token_length_and_utf8)
{
    char out[REMEMBER_TOKEN_MAX + 1];
    char ok64[REMEMBER_TOKEN_MAX + 1];
    char too[REMEMBER_TOKEN_MAX + 2];
    size_t i;
    char bad[] = {(char)0xff, (char)0xfe, 'x', '\0'};

    for (i = 0; i < (size_t)REMEMBER_TOKEN_MAX; i++) {
        ok64[i] = 'a';
    }
    ok64[REMEMBER_TOKEN_MAX] = '\0';
    ASSERT_EQ_INT(normalize_token(ok64, out, sizeof(out)), NORM_OK);
    ASSERT_EQ_INT((int)strlen(out), REMEMBER_TOKEN_MAX);

    for (i = 0; i < (size_t)REMEMBER_TOKEN_MAX + 1U; i++) {
        too[i] = 'b';
    }
    too[REMEMBER_TOKEN_MAX + 1U] = '\0';
    ASSERT_EQ_INT(normalize_token(too, out, sizeof(out)), NORM_ERR_TOO_LONG);

    ASSERT_EQ_INT(normalize_tag(bad, out, sizeof(out)), NORM_ERR_INVALID_UTF8);
}

TEST(token_utf8_non_ascii_not_casefolded)
{
    /* "Ä" is C3 84; casefold is ASCII-only so it stays C3 84. */
    char out[REMEMBER_TOKEN_MAX + 1];
    const char *in = "\xc3\x84";

    ASSERT_EQ_INT(normalize_token(in, out, sizeof(out)), NORM_OK);
    ASSERT_STREQ(out, in);
}

TEST(norm_status_string_stable)
{
    ASSERT_STREQ(norm_status_string(NORM_OK), "ok");
    ASSERT_STREQ(norm_status_string(NORM_ERR_EMPTY), "empty");
    ASSERT_STREQ(norm_status_string(NORM_ERR_TOO_LONG), "too long");
    ASSERT_STREQ(norm_status_string(NORM_ERR_INVALID_UTF8), "invalid UTF-8");
    ASSERT_STREQ(norm_status_string(NORM_ERR_INVALID_CHAR), "invalid character");
    ASSERT_STREQ(norm_status_string(NORM_ERR_OOM), "out of memory");
    /* Exhaustive switch has a defensive default; only named values are public. */
    ASSERT_TRUE(norm_status_string(NORM_OK) != NULL);
}

void register_normalize_tests(void)
{
    RUN_TEST(body_trim_basic);
    RUN_TEST(body_trim_all_ws_kinds);
    RUN_TEST(body_trim_preserves_internal_ws);
    RUN_TEST(body_trim_empty_and_ws_only);
    RUN_TEST(body_trim_too_long);
    RUN_TEST(body_trim_invalid_utf8);
    RUN_TEST(body_trim_valid_utf8_multibyte);
    RUN_TEST(body_hash_empty_and_abc);
    RUN_TEST(body_hash_matches_trimmed);
    RUN_TEST(token_casefold_and_trim);
    RUN_TEST(token_tag_key_identical);
    RUN_TEST(token_rejects_empty_ws_control_space);
    RUN_TEST(token_length_and_utf8);
    RUN_TEST(token_utf8_non_ascii_not_casefolded);
    RUN_TEST(norm_status_string_stable);
}
