#include "normalize.h"
#include "register.h"
#include "test.h"
#include "test_alloc.h"

#include <stdlib.h>
#include <string.h>

/* Fixed-length hash inputs / counts and invalid-byte fixtures for normalize tests. */
enum {
    HASH_IN_56 = 56,
    HASH_IN_64 = 64,
    TAG_COUNT_99 = 99,
    ASCII_DEL = 0x7f,       /* DEL control byte */
    UTF8_INVALID_FE = 0xfe, /* never a valid UTF-8 byte */
    UTF8_INVALID_FF = 0xff  /* never a valid UTF-8 byte */
};

/*
 * Pure unit tests for normalize + SHA-256 (step 03). No CLI, no store.
 */

TEST(body_trim_basic)
{
    char *out = NULL;
    size_t n = 0;
    NormStatus st = NORM_OK;

    st = body_trim_copy("  hello  ", sizeof("  hello  ") - 1, &out, &n);
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
    NormStatus st = body_trim_copy("  a  b  ", sizeof("  a  b  ") - 1, &out, &n);

    ASSERT_EQ_INT(st, NORM_OK);
    ASSERT_STREQ(out, "a  b");
    ASSERT_EQ_INT((int)n, 4);
    free(out);
}

TEST(body_trim_empty_and_ws_only)
{
    char *out = NULL;
    size_t n = TAG_COUNT_99;

    ASSERT_EQ_INT(body_trim_copy("", 0, &out, &n), NORM_ERR_EMPTY);
    ASSERT_TRUE(out == NULL);
    ASSERT_EQ_INT((int)n, 0);

    ASSERT_EQ_INT(body_trim_copy(" \t\n", 3, &out, &n), NORM_ERR_EMPTY);
    ASSERT_TRUE(out == NULL);

    ASSERT_EQ_INT(body_trim_copy(NULL, 0, &out, &n), NORM_ERR_EMPTY);
}

TEST(body_trim_too_long)
{
    char *buf = NULL;
    char *out = NULL;
    size_t i = 0;
    NormStatus st = NORM_OK;

    buf = (char *)malloc((size_t)REMEMBER_BODY_MAX + 2U);
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
    const char bad[] = {'a', (char)UTF8_INVALID_FF, 'b'};
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

/* Assert body_trim_copy rejects bytes[0..len) as ill-formed UTF-8. */
static void assert_body_utf8_rejected(const char *bytes, size_t len)
{
    char *out = NULL;
    ASSERT_EQ_INT(body_trim_copy(bytes, len, &out, NULL), NORM_ERR_INVALID_UTF8);
    ASSERT_TRUE(out == NULL);
    free(out);
}

/*
 * The security-relevant UTF-8 rejections (RFC 3629). These exercise the
 * validator branches — overlong, surrogate, out-of-range, truncation, and a
 * lone continuation byte — that plain "0xff" does not reach.
 */
TEST(body_trim_utf8_edge_rejections)
{
    assert_body_utf8_rejected("\xc0\x80", 2);         /* overlong 2-byte NUL */
    assert_body_utf8_rejected("\xc1\xbf", 2);         /* overlong 2-byte (max) */
    assert_body_utf8_rejected("\xe0\x80\x80", 3);     /* overlong 3-byte */
    assert_body_utf8_rejected("\xed\xa0\x80", 3);     /* surrogate U+D800 */
    assert_body_utf8_rejected("\xf4\x90\x80\x80", 4); /* U+110000 > U+10FFFF */
    assert_body_utf8_rejected("\xf5\x80\x80\x80", 4); /* lead > F4 */
    assert_body_utf8_rejected("\xc3", 1);             /* truncated 2-byte */
    assert_body_utf8_rejected("\xe2\x82", 2);         /* truncated 3-byte */
    assert_body_utf8_rejected("\x80", 1);             /* bare continuation */
    assert_body_utf8_rejected("a\xc3\x28", 3);        /* bad continuation byte */
}

/* Assert body_trim_copy accepts bytes as one valid scalar. */
static void assert_body_utf8_accepted(const char *bytes)
{
    char *out = NULL;

    ASSERT_EQ_INT(body_trim_copy(bytes, strlen(bytes), &out, NULL), NORM_OK);
    ASSERT_STREQ(out, bytes);
    free(out);
}

TEST(body_trim_accepts_the_first_scalar_after_the_surrogates)
{
    assert_body_utf8_accepted("\xee\x80\x80"); /* U+E000 */
}

TEST(body_trim_accepts_the_first_four_byte_scalar)
{
    assert_body_utf8_accepted("\xf0\x90\x80\x80"); /* U+10000 */
}

TEST(body_trim_rejects_a_four_byte_overlong)
{
    assert_body_utf8_rejected("\xf0\x8f\xbf\xbf", 4); /* U+FFFF in four bytes */
}

/*
 * Policy: a body is a C string, so the first NUL ends it. Bytes after are
 * ignored, and *out_len stays equal to strlen(*out) — otherwise the stored
 * text, its length, and its hash would disagree (see [[nul-is-end-of-string]]).
 */
TEST(body_trim_nul_terminates)
{
    char *out = NULL;
    size_t n = 0;
    const char in[] = {' ', 'a', 'b', '\0', 'c', 'd'}; /* trailing "cd" ignored */

    ASSERT_EQ_INT(body_trim_copy(in, sizeof(in), &out, &n), NORM_OK);
    ASSERT_STREQ(out, "ab");
    ASSERT_EQ_INT((int)n, 2);
    ASSERT_EQ_INT((int)n, (int)strlen(out));
    free(out);

    /* A leading NUL makes the whole body empty. */
    {
        const char lead[] = {'\0', 'x'};
        ASSERT_EQ_INT(body_trim_copy(lead, sizeof(lead), &out, &n), NORM_ERR_EMPTY);
        ASSERT_TRUE(out == NULL);
    }
}

/* NUL-terminated body hashes the same as the bare prefix (dedup depends on it). */
TEST(body_hash_ignores_bytes_after_nul)
{
    char *out = NULL;
    size_t n = 0;
    char hex[REMEMBER_SHA256_HEX_LEN + 1];
    char hex_direct[REMEMBER_SHA256_HEX_LEN + 1];
    const char in[] = {'a', 'b', 'c', '\0', 'X', 'Y'};

    ASSERT_EQ_INT(body_trim_copy(in, sizeof(in), &out, &n), NORM_OK);
    body_hash_hex(out, n, hex);
    body_hash_hex("abc", 3, hex_direct);
    ASSERT_STREQ(hex, hex_direct);
    free(out);
}

/* Control chars (other than NUL) stay in the body — only tokens forbid them.
   Downstream output must escape these; see [[output-escaping]]. */
TEST(body_trim_keeps_control_chars)
{
    char *out = NULL;
    size_t n = 0;
    const char in[] = {'x', 0x01, 0x1b, 'y'}; /* SOH, ESC preserved */

    ASSERT_EQ_INT(body_trim_copy(in, sizeof(in), &out, &n), NORM_OK);
    ASSERT_TRUE(out != NULL);
    if (out == NULL) {
        return;
    }
    ASSERT_EQ_INT((int)n, 4);
    /* Policy: raw control bytes must survive — length alone is not enough. */
    ASSERT_EQ_INT((int)(unsigned char)out[0], (int)'x');
    ASSERT_EQ_INT((int)(unsigned char)out[1], 0x01);
    ASSERT_EQ_INT((int)(unsigned char)out[2], 0x1b);
    ASSERT_EQ_INT((int)(unsigned char)out[3], (int)'y');
    free(out);
}

/* A missing or too-small output buffer is NORM_ERR_INTERNAL, not TOO_LONG/OOM. */
TEST(normalize_reports_bad_output_buffer)
{
    char small[3]; /* room for "ab" + NUL, but not a 4-char token */

    ASSERT_EQ_INT(normalize_token("abcd", small, sizeof(small)), NORM_ERR_INTERNAL);
    ASSERT_EQ_INT(normalize_token("ab", NULL, 0), NORM_ERR_INTERNAL);
    ASSERT_EQ_INT(body_trim_copy("ab", 2, NULL, NULL), NORM_ERR_INTERNAL);

    /* A genuinely over-long token is still TOO_LONG, not conflated. */
    {
        char big[REMEMBER_TOKEN_MAX + 2];
        char out[REMEMBER_TOKEN_MAX + 2];
        size_t i = 0;
        for (i = 0; i < (size_t)REMEMBER_TOKEN_MAX + 1U; i++) {
            big[i] = 'a';
        }
        big[REMEMBER_TOKEN_MAX + 1U] = '\0';
        ASSERT_EQ_INT(normalize_token(big, out, sizeof(out)), NORM_ERR_TOO_LONG);
    }
}

TEST(normalize_token_zero_capacity_is_internal)
{
    char out[1] = {'x'};

    ASSERT_EQ_INT(normalize_token("ab", out, 0), NORM_ERR_INTERNAL);
    ASSERT_EQ_INT(out[0], 'x');
}

TEST(body_hash_of_null_data_is_the_empty_digest)
{
    char hex[REMEMBER_SHA256_HEX_LEN + 1];

    body_hash_hex(NULL, 3, hex);
    ASSERT_STREQ(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(body_hash_empty_and_abc)
{
    char hex[REMEMBER_SHA256_HEX_LEN + 1];

    body_hash_hex("", 0, hex);
    ASSERT_STREQ(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    body_hash_hex("abc", 3, hex);
    ASSERT_STREQ(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

/*
 * Exercise the long-pad and multi-block paths in the instrumented digest TU
 * (see engineering-notes: empty/"abc" alone leave sha256_final's datalen>=56
 * branch and multi-block sha256_update unrun under UBSan).
 */
TEST(body_hash_long_pad_and_multiblock)
{
    char hex[REMEMBER_SHA256_HEX_LEN + 1];
    /* NIST FIPS 180-2 / FIPS 180-4 example: 56-byte message. */
    static const char s56[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    /* Exactly one full 64-byte block before final (multi-block transform). */
    static const char s64[] = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

    ASSERT_EQ_INT((int)strlen(s56), 56);
    body_hash_hex(s56, HASH_IN_56, hex);
    ASSERT_STREQ(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    ASSERT_EQ_INT((int)strlen(s64), 64);
    body_hash_hex(s64, HASH_IN_64, hex);
    ASSERT_STREQ(hex, "a8ae6e6ee929abea3afcfc5258c8ccd6f85273e0d4626d26c7279f3250f77c8e");
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

TEST(token_rejects_empty_and_nonspace_ws_control)
{
    char out[REMEMBER_TOKEN_MAX + 1];

    ASSERT_EQ_INT(normalize_token("", out, sizeof(out)), NORM_ERR_EMPTY);
    ASSERT_EQ_INT(normalize_token("   ", out, sizeof(out)), NORM_ERR_EMPTY);
    ASSERT_EQ_INT(normalize_token(NULL, out, sizeof(out)), NORM_ERR_EMPTY);

    /* Internal space is allowed now; other whitespace is still rejected. */
    ASSERT_EQ_INT(normalize_token("a\tb", out, sizeof(out)), NORM_ERR_INVALID_CHAR);
    ASSERT_EQ_INT(normalize_token("a\nb", out, sizeof(out)), NORM_ERR_INVALID_CHAR);

    {
        const char ctrl[] = {'a', 0x01, 'b', '\0'};
        ASSERT_EQ_INT(normalize_token(ctrl, out, sizeof(out)), NORM_ERR_INVALID_CHAR);
    }
    {
        const char del[] = {'k', ASCII_DEL, '\0'};
        ASSERT_EQ_INT(normalize_key(del, out, sizeof(out)), NORM_ERR_INVALID_CHAR);
    }
}

TEST(token_allows_internal_spaces)
{
    char out[REMEMBER_TOKEN_MAX + 1];

    /* Internal spaces survive; edges are still trimmed; casefold still applies. */
    ASSERT_EQ_INT(normalize_token("  My Key  ", out, sizeof(out)), NORM_OK);
    ASSERT_STREQ(out, "my key");

    ASSERT_EQ_INT(normalize_tag("Red Hot", out, sizeof(out)), NORM_OK);
    ASSERT_STREQ(out, "red hot");

    /* Runs are kept as-typed (not collapsed), like the body. */
    ASSERT_EQ_INT(normalize_key("a  b", out, sizeof(out)), NORM_OK);
    ASSERT_STREQ(out, "a  b");
}

TEST(token_length_and_utf8)
{
    char out[REMEMBER_TOKEN_MAX + 1];
    char ok64[REMEMBER_TOKEN_MAX + 1];
    char too[REMEMBER_TOKEN_MAX + 2];
    size_t i = 0;
    const char bad[] = {(char)UTF8_INVALID_FF, (char)UTF8_INVALID_FE, 'x', '\0'};

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

/* ---- allocation failure (plan 18b B3, shared hook) ----------------------- */

TEST(test_alloc_fails_only_the_nth_allocation)
{
    char *first = NULL;
    char *second = NULL;
    char *third = NULL;

    test_alloc_fail_after(1);
    ASSERT_EQ_INT(body_trim_copy("a", 1, &first, NULL), NORM_OK);
    ASSERT_EQ_INT(body_trim_copy("b", 1, &second, NULL), NORM_ERR_OOM);
    ASSERT_EQ_INT(body_trim_copy("c", 1, &third, NULL), NORM_OK);
    ASSERT_TRUE(second == NULL);
    free(first);
    free(third);
}

TEST(body_trim_copy_reports_oom)
{
    char *out = NULL;

    test_alloc_fail_after(0);
    ASSERT_EQ_INT(body_trim_copy("body", 4, &out, NULL), NORM_ERR_OOM);
    ASSERT_TRUE(out == NULL);
}

TEST(fold_hebrew_marks_reports_oom)
{
    char *out = NULL;

    test_alloc_fail_after(0);
    ASSERT_EQ_INT(fold_hebrew_marks("abc", 3, &out), NORM_ERR_OOM);
    ASSERT_TRUE(out == NULL);
}

TEST(norm_status_string_stable)
{
    ASSERT_STREQ(norm_status_string(NORM_OK), "ok");
    ASSERT_STREQ(norm_status_string(NORM_ERR_EMPTY), "empty");
    ASSERT_STREQ(norm_status_string(NORM_ERR_TOO_LONG), "too long");
    ASSERT_STREQ(norm_status_string(NORM_ERR_INVALID_UTF8), "invalid UTF-8");
    ASSERT_STREQ(norm_status_string(NORM_ERR_INVALID_CHAR), "invalid character");
    ASSERT_STREQ(norm_status_string(NORM_ERR_OOM), "out of memory");
    ASSERT_STREQ(norm_status_string(NORM_ERR_INTERNAL), "internal error");
    /* Exhaustive switch has a defensive default; only named values are public. */
    ASSERT_TRUE(norm_status_string(NORM_OK) != NULL);
}

/* ---- canonical stored forms (plan 18c, decision:plan18c-validate-peer-fields) ---- */

static const char UUID_A[] = "01a16342-0f26-7dc1-8a46-88703a48341b";

TEST(uuid_v7_accepts_each_variant)
{
    ASSERT_TRUE(uuid_v7_is_canonical("01a16342-0f26-7dc1-8a46-88703a48341b"));
    ASSERT_TRUE(uuid_v7_is_canonical("01a16342-0f26-7dc1-9a46-88703a48341b"));
    ASSERT_TRUE(uuid_v7_is_canonical("01a16342-0f26-7dc1-aa46-88703a48341b"));
    ASSERT_TRUE(uuid_v7_is_canonical("01a16342-0f26-7dc1-ba46-88703a48341b"));
}

TEST(uuid_v7_rejects_a_wrong_shape)
{
    ASSERT_FALSE(uuid_v7_is_canonical(NULL));
    ASSERT_FALSE(uuid_v7_is_canonical("01a16342-0f26-7dc1-8a46-88703a48341"));   /* 35 */
    ASSERT_FALSE(uuid_v7_is_canonical("01a16342-0f26-7dc1-8a46-88703a48341bb")); /* 37 */
    ASSERT_FALSE(uuid_v7_is_canonical("01a16342x0f26-7dc1-8a46-88703a48341b"));  /* no dash */
    ASSERT_FALSE(uuid_v7_is_canonical("01A16342-0f26-7dc1-8a46-88703a48341b"));  /* upper */
    ASSERT_FALSE(uuid_v7_is_canonical("01g16342-0f26-7dc1-8a46-88703a48341b"));  /* past f */
    ASSERT_FALSE(uuid_v7_is_canonical("01/16342-0f26-7dc1-8a46-88703a48341b"));  /* below 0 */
    ASSERT_FALSE(uuid_v7_is_canonical("01a16342-0f26-4dc1-8a46-88703a48341b"));  /* v4 */
    ASSERT_FALSE(uuid_v7_is_canonical("01a16342-0f26-7dc1-ca46-88703a48341b"));  /* variant */
}

TEST(utc_stamp_accepts_the_fixed_shape)
{
    ASSERT_TRUE(utc_stamp_is_canonical("2026-10-22T09:45:16.123Z"));
}

TEST(utc_stamp_rejects_any_other_shape)
{
    ASSERT_FALSE(utc_stamp_is_canonical(NULL));
    ASSERT_FALSE(utc_stamp_is_canonical("2026-10-22T09:45:16Z"));       /* no ms: ends early */
    ASSERT_FALSE(utc_stamp_is_canonical("2026-10-22 09:45:16.123Z"));   /* wrong literal */
    ASSERT_FALSE(utc_stamp_is_canonical("2026-1a-22T09:45:16.123Z"));   /* letter for digit */
    ASSERT_FALSE(utc_stamp_is_canonical("2026-1/-22T09:45:16.123Z"));   /* below 0 */
    ASSERT_FALSE(utc_stamp_is_canonical("2026-10-22T09:45:16.123Z\t")); /* trailing byte */
}

TEST(vv_json_accepts_the_compact_forms)
{
    ASSERT_TRUE(vv_json_is_canonical("{}"));
    ASSERT_TRUE(vv_json_is_canonical("{\"01a16342-0f26-7dc1-8a46-88703a48341b\":0}"));
    ASSERT_TRUE(vv_json_is_canonical("{\"01a16342-0f26-7dc1-8a46-88703a48341b\":12,"
                                     "\"01a16342-0f26-7dc1-8a46-88703a48341c\":999999999999999999,"
                                     "\"01a16342-0f26-7dc1-8a46-88703a48341d\":3}"));
}

TEST(vv_json_rejects_an_injected_or_malformed_vector)
{
    ASSERT_FALSE(vv_json_is_canonical(NULL));
    ASSERT_FALSE(vv_json_is_canonical("[]"));
    ASSERT_FALSE(vv_json_is_canonical("{}x"));
    ASSERT_FALSE(vv_json_is_canonical("{\"01a16342-0f26-7dc1-8a46-88703a48341b\":1},\"pwn\":true"));
    ASSERT_FALSE(vv_json_is_canonical("{ \"01a16342-0f26-7dc1-8a46-88703a48341b\":1}"));
    ASSERT_FALSE(vv_json_is_canonical("{\"a\":1}"));
    ASSERT_FALSE(vv_json_is_canonical("{\"01a16342-0f26-7dc1-8a46-88703a48341bx:1}"));
    ASSERT_FALSE(vv_json_is_canonical("{\"01a16342-0f26-7dc1-8a46-88703a48341b\" 1}"));
    ASSERT_FALSE(vv_json_is_canonical("{\"01a16342-0f26-7dc1-8a46-88703a48341b\":1;"));
    ASSERT_FALSE(vv_json_is_canonical("{\"01a16342-0f26-7dc1-8a46-88703a48341b\":1;"
                                      "\"01a16342-0f26-7dc1-8a46-88703a48341c\":2}"));
}

TEST(vv_json_rejects_a_counter_that_is_not_a_small_whole_number)
{
    ASSERT_FALSE(vv_json_is_canonical("{\"01a16342-0f26-7dc1-8a46-88703a48341b\":}"));
    ASSERT_FALSE(vv_json_is_canonical("{\"01a16342-0f26-7dc1-8a46-88703a48341b\":-1}"));
    ASSERT_FALSE(vv_json_is_canonical("{\"01a16342-0f26-7dc1-8a46-88703a48341b\":01}"));
    ASSERT_FALSE(vv_json_is_canonical("{\"01a16342-0f26-7dc1-8a46-88703a48341b\":1.5}"));
    ASSERT_FALSE(
        vv_json_is_canonical("{\"01a16342-0f26-7dc1-8a46-88703a48341b\":1000000000000000000}"));
}

/* decision:plan17a-duplicate-vv-key: the same device twice is malformed. */
TEST(vv_json_rejects_a_repeated_device)
{
    ASSERT_FALSE(vv_json_is_canonical("{\"01a16342-0f26-7dc1-8a46-88703a48341b\":1,"
                                      "\"01a16342-0f26-7dc1-8a46-88703a48341b\":1}"));
    ASSERT_FALSE(vv_json_is_canonical("{\"01a16342-0f26-7dc1-8a46-88703a48341b\":1,"
                                      "\"01a16342-0f26-7dc1-8a46-88703a48341c\":2,"
                                      "\"01a16342-0f26-7dc1-8a46-88703a48341c\":2}"));
}

TEST(source_accepts_only_the_closed_tokens)
{
    ASSERT_TRUE(source_is_valid("human"));
    ASSERT_TRUE(source_is_valid("agent"));
    ASSERT_TRUE(source_is_valid("tool"));
    ASSERT_TRUE(source_is_valid("share"));
    ASSERT_TRUE(source_is_valid("unknown"));
    ASSERT_FALSE(source_is_valid(NULL));
    ASSERT_FALSE(source_is_valid("robot"));
    ASSERT_FALSE(source_is_valid("hum"));
    ASSERT_FALSE(source_is_valid(UUID_A));
}

static void assert_prefix(const char *src, NormStatus want, const char *want_out)
{
    char out[REMEMBER_BUCKET_PREFIX_BUF];

    ASSERT_EQ_STATUS(bucket_prefix_fold(src, out, sizeof(out)), want);
    ASSERT_STREQ(out, want_out);
}

/* 007 ST-5.4, every branch side of bucket_prefix_fold. */
TEST(bucket_prefix_fold_follows_st_5_4)
{
    char out[REMEMBER_BUCKET_PREFIX_BUF];
    char long_name[REMEMBER_BUCKET_PREFIX_MAX_POINTS + 3];

    assert_prefix("project:", NORM_OK, "project:");
    assert_prefix("Pro_j-e.c9:", NORM_OK, "pro_j-e.c9:");
    /* U+05E9 (Lo), U+01C5 (Lt), U+02B0 (Lm), U+0661 (Nd): kept as is. */
    assert_prefix("\xD7\xA9\xC7\x85\xCA\xB0\xD9\xA1:", NORM_OK,
                  "\xD7\xA9\xC7\x85\xCA\xB0\xD9\xA1:");
    assert_prefix("", NORM_ERR_INVALID_CHAR, "");
    assert_prefix("project", NORM_ERR_INVALID_CHAR, "");
    assert_prefix(":", NORM_ERR_EMPTY, "");
    assert_prefix("my project:", NORM_ERR_INVALID_CHAR, "");
    assert_prefix("a:b:", NORM_ERR_INVALID_CHAR, "");
    /* U+0378 is unassigned (Cn, below Lu); U+0301 is a mark (Mn, above Lo). */
    assert_prefix("a\xCD\xB8:", NORM_ERR_INVALID_CHAR, "");
    assert_prefix("a\xCC\x81:", NORM_ERR_INVALID_CHAR, "");
    assert_prefix("a\xFF:", NORM_ERR_INVALID_UTF8, "");

    memset(long_name, 'a', sizeof(long_name));
    long_name[REMEMBER_BUCKET_PREFIX_MAX_POINTS] = ':';
    long_name[REMEMBER_BUCKET_PREFIX_MAX_POINTS + 1] = '\0';
    ASSERT_EQ_STATUS(bucket_prefix_fold(long_name, out, sizeof(out)), NORM_OK);
    long_name[REMEMBER_BUCKET_PREFIX_MAX_POINTS] = 'a';
    long_name[REMEMBER_BUCKET_PREFIX_MAX_POINTS + 1] = ':';
    long_name[REMEMBER_BUCKET_PREFIX_MAX_POINTS + 2] = '\0';
    ASSERT_EQ_STATUS(bucket_prefix_fold(long_name, out, sizeof(out)), NORM_ERR_TOO_LONG);

    ASSERT_EQ_STATUS(bucket_prefix_fold(NULL, out, sizeof(out)), NORM_ERR_INTERNAL);
    ASSERT_EQ_STATUS(bucket_prefix_fold("a:", NULL, sizeof(out)), NORM_ERR_INTERNAL);
    ASSERT_EQ_STATUS(bucket_prefix_fold("a:", out, sizeof(out) - 1U), NORM_ERR_INTERNAL);
}

void register_normalize_tests(void)
{
    RUN_TEST(bucket_prefix_fold_follows_st_5_4);
    RUN_TEST(body_trim_basic);
    RUN_TEST(body_trim_all_ws_kinds);
    RUN_TEST(body_trim_preserves_internal_ws);
    RUN_TEST(body_trim_empty_and_ws_only);
    RUN_TEST(body_trim_too_long);
    RUN_TEST(body_trim_invalid_utf8);
    RUN_TEST(body_trim_valid_utf8_multibyte);
    RUN_TEST(body_trim_utf8_edge_rejections);
    RUN_TEST(body_trim_accepts_the_first_scalar_after_the_surrogates);
    RUN_TEST(body_trim_accepts_the_first_four_byte_scalar);
    RUN_TEST(body_trim_rejects_a_four_byte_overlong);
    RUN_TEST(body_trim_nul_terminates);
    RUN_TEST(body_hash_ignores_bytes_after_nul);
    RUN_TEST(body_trim_keeps_control_chars);
    RUN_TEST(normalize_reports_bad_output_buffer);
    RUN_TEST(normalize_token_zero_capacity_is_internal);
    RUN_TEST(body_hash_of_null_data_is_the_empty_digest);
    RUN_TEST(body_hash_empty_and_abc);
    RUN_TEST(body_hash_long_pad_and_multiblock);
    RUN_TEST(body_hash_matches_trimmed);
    RUN_TEST(token_casefold_and_trim);
    RUN_TEST(token_tag_key_identical);
    RUN_TEST(token_rejects_empty_and_nonspace_ws_control);
    RUN_TEST(token_allows_internal_spaces);
    RUN_TEST(token_length_and_utf8);
    RUN_TEST(token_utf8_non_ascii_not_casefolded);
    RUN_TEST(test_alloc_fails_only_the_nth_allocation);
    RUN_TEST(body_trim_copy_reports_oom);
    RUN_TEST(fold_hebrew_marks_reports_oom);
    RUN_TEST(norm_status_string_stable);
    RUN_TEST(uuid_v7_accepts_each_variant);
    RUN_TEST(uuid_v7_rejects_a_wrong_shape);
    RUN_TEST(utc_stamp_accepts_the_fixed_shape);
    RUN_TEST(utc_stamp_rejects_any_other_shape);
    RUN_TEST(vv_json_accepts_the_compact_forms);
    RUN_TEST(vv_json_rejects_an_injected_or_malformed_vector);
    RUN_TEST(vv_json_rejects_a_counter_that_is_not_a_small_whole_number);
    RUN_TEST(vv_json_rejects_a_repeated_device);
    RUN_TEST(source_accepts_only_the_closed_tokens);
}
