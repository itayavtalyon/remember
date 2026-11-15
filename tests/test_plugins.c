#ifdef REMEMBER_PLUGINS_DIAG_TEST
#include "appio.h"
#include "diag.h"
#endif
#include "plugin_manifest.h"
#include "register.h"
#include "test.h"
#include "test_alloc.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The lengths and counts below are the limits under test, named by their decision. */
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

/* Limits: decision:2026-11-13-plugin-manifest-limits (L1 to L14), and
   decision:2026-11-13-plugin-origins-empty.

   Copy: R25-1 and C-36 (decision:2026-11-12-copy-batch-final, COPY-PENDING.md), C-35 (diag.c),
   and R25-2 (approved as decision:2026-11-14-copy-r25-2-manifest-approved, with its three rules:
   Q1 whole numbers by value, Q2 JSON escaping of plugin text, Q3 the plain phrase for a long
   duration or size). The R25_2_* macros hold the repeated shapes. */
#define R25_2_MISSING(where, field) "plugin.json" where ": \"" field "\" is missing."
#define R25_2_ADD(field) "Add \"" field "\"."
#define R25_2_MUST_BE(where, what) "plugin.json " where ": must be " what "."
#define R25_2_USE(what) "Use " what "."
#define R25_2_UNKNOWN(where, field) "plugin.json" where ": unknown field \"" field "\"."
#define R25_2_REMOVE(field) "Remove \"" field "\"."
#define R25_2_TWICE(where, x) "plugin.json" where ": \"" x "\" appears more than once."
#define R25_2_KEEP(x) "Keep one \"" x "\"."

#define HEAD                                                                                       \
    "{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"wake\",\"plugin_version\":\"1.0."    \
    "0\","                                                                                         \
    "\"protocol\":1,"
#define ACTION_RUN "\"run\":{\"command\":[\"/bin/echo\"]}"
#define ACTION_BODY "{\"name\":\"go\",\"events\":[\"memory.created\"]," ACTION_RUN
#define ACTIONS "\"actions\":[" ACTION_BODY "}]"
#define FILTER_RUN "\"run\":{\"process\":\"persistent\",\"command\":[\"hello.py\"]}"
#define FILTER_BODY "{\"name\":\"rank\",\"point\":\"search.results\"," FILTER_RUN
#define FILTERS "\"filters\":[" FILTER_BODY "}]"
/* A manifest with a valid format and plugin_version and one action; the caller supplies the rest.
 */
#define MINIMAL(extra)                                                                             \
    "{\"format\":\"remember-plugin\",\"plugin_version\":\"1.0.0\"," ACTIONS "," extra "}"
#define SETTING(extra) "\"settings\":[{\"key\":\"mode\",\"description\":\"A mode.\"," extra "}]"

#define SETTING_DOC(extra) HEAD ACTIONS "," SETTING(extra) "}"

static const char k_hello[] = HEAD FILTERS "}";

static PluginManifest *given_parse(const char *doc, PluginParseStatus want)
{
    PluginManifest *man = NULL;
    PluginParseStatus got = plugin_manifest_parse(doc, strlen(doc), &man);

    ASSERT_EQ_INT(got, want);
    ASSERT_TRUE(man != NULL);
    return man;
}

/* errors[i] when it exists. Otherwise the assert fails and a blank error comes back, so the test
   goes on red instead of crashing the suite on a NULL list (a mutant that parses too much). */
static const PluginError *error_at(const PluginManifest *man, size_t i)
{
    static const int k_none[1] = {0};
    static char k_blank[1] = "";
    static const PluginError k_missing = {.code = "",
                                          .rule = "",
                                          .supported = k_none,
                                          .path = k_blank,
                                          .message = k_blank,
                                          .fix = k_blank};

    ASSERT_TRUE(i < man->nerrors);
    return i < man->nerrors ? &man->errors[i] : &k_missing;
}

/* warnings[i], or a blank warning after a failed assert (see error_at). */
static const PluginUnknown *warning_at(const PluginManifest *man, size_t i)
{
    static char k_blank[1] = "";
    static const PluginUnknown k_missing = {.path = k_blank, .field = k_blank};

    ASSERT_TRUE(i < man->nwarnings);
    return i < man->nwarnings ? &man->warnings[i] : &k_missing;
}

/* Appends text to the string in dst, which holds cap bytes. */
static void append(char *dst, size_t cap, const char *text)
{
    size_t used = strlen(dst);
    size_t add = strlen(text);

    ASSERT_TRUE(used + add < cap);
    if (used + add < cap) {
        memcpy(dst + used, text, add + 1U);
    }
}

static void appendf(char *dst, size_t cap, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/* Appends the formatted text to the string in dst, which holds cap bytes. The size of the write
   comes from strlen, never from a snprintf result. */
static void appendf(char *dst, size_t cap, const char *fmt, ...)
{
    /* NOLINTNEXTLINE(cppcoreguidelines-init-variables) */
    va_list args;
    size_t used = strlen(dst);

    ASSERT_TRUE(used < cap);
    va_start(args, fmt);
    (void)vsnprintf(dst + used, cap - used, fmt, args);
    va_end(args);
}

/* One error, and no other: the whole document is wrong in exactly one place. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): a path, a rule, a message, then a fix
static void expect_error(const char *doc, const char *path, const char *rule, const char *message,
                         const char *fix)
{
    PluginManifest *man = given_parse(doc, PLUGIN_PARSE_INVALID);

    ASSERT_EQ_INT(man->nerrors, 1);
    if (man->nerrors != 1U) {
        plugin_manifest_free(man);
        return;
    }
    ASSERT_STREQ(error_at(man, 0)->code, "plugin_manifest_invalid");
    ASSERT_STREQ(error_at(man, 0)->path, path);
    ASSERT_STREQ(error_at(man, 0)->rule, rule);
    ASSERT_STREQ(error_at(man, 0)->message, message);
    ASSERT_STREQ(error_at(man, 0)->fix, fix);
    plugin_manifest_free(man);
}

static void expect_ok(const char *doc)
{
    PluginManifest *man = given_parse(doc, PLUGIN_PARSE_OK);

    ASSERT_EQ_INT(man->nerrors, 0);
    plugin_manifest_free(man);
}

/* A string of n copies of one byte, for the length limits. The text lives in one of four static
   buffers, used in turn, so a test holds at most four of them at once and frees none. */
static char *given_run_of(char ch, size_t n)
{
    enum { POOL = 4, POOL_CAP = 1024 };
    static char pool[POOL][POOL_CAP];
    static size_t next;
    char *text = pool[next % POOL];

    next++;
    ASSERT_TRUE(n < (size_t)POOL_CAP);
    if (n >= (size_t)POOL_CAP) {
        n = 0;
    }
    memset(text, ch, n);
    text[n] = '\0';
    return text;
}

/* A document with one filled-in %s. A macro, so fmt stays a literal for -Wformat. */
#define expect_error_in(fmt, arg, path, rule, message, fix)                                        \
    do {                                                                                           \
        char doc_[1500];                                                                           \
        (void)snprintf(doc_, sizeof(doc_), fmt, arg);                                              \
        expect_error(doc_, path, rule, message, fix);                                              \
    } while (0)

TEST(a_schema_valid_manifest_parses)
{
    PluginManifest *man = given_parse(k_hello, PLUGIN_PARSE_OK);

    ASSERT_EQ_INT(man->nerrors, 0);
    ASSERT_EQ_INT(man->nwarnings, 0);
    ASSERT_STREQ(man->name, "wake");
    ASSERT_STREQ(man->plugin_version, "1.0.0");
    ASSERT_EQ_INT(man->nentries, 1);
    ASSERT_TRUE(man->entries[0].is_filter);
    ASSERT_STREQ(man->entries[0].name, "rank");
    ASSERT_STREQ(man->entries[0].point, "search.results");
    ASSERT_STREQ(man->entries[0].run.argv[0], "hello.py");
    ASSERT_EQ_INT(man->entries[0].run.argc, 1);
    ASSERT_TRUE(man->entries[0].run.persistent);
    plugin_manifest_free(man);
}

TEST(a_full_schema_valid_manifest_keeps_every_field)
{
    static const char doc[] =
        HEAD "\"description\":\"Wakes.\",\"data_version\":3,\"idle_timeout\":\"5m\","
             "\"backfill\":{\"enabled\":true,\"max_batch_entries\":10,\"max_batch_size\":\"1MiB\"},"
             "\"actions\":[{\"name\":\"go\",\"events\":[\"memory.created\",\"memory.updated\"],"
             "\"filter\":{\"tags\":{\"any\":[\"a\"],\"all\":[\"b\",\"c\"],\"match_before\":true},"
             "\"created_by\":[\"human\"],\"updated_by\":[\"agent\",\"tool\"],\"bins\":[\"live\"],"
             "\"not_caused_by\":[\"wake\"],\"origins\":[\"sync\",\"import\"]},"
             "\"run\":{\"process\":\"persistent\",\"command\":[\"hello.py\"],\"timeout\":\"2m\"},"
             "\"permissions\":{\"needs\":[\"body\",\"remember.search.max_results\"]}}],"
             "\"filters\":[" FILTER_BODY
             ",\"priority\":7,\"budget\":\"2s\",\"cold_start_grace\":\"20s\"}]}";
    PluginManifest *man = given_parse(doc, PLUGIN_PARSE_OK);
    const PluginEntry *act = &man->entries[0];

    ASSERT_EQ_INT(man->nerrors, 0);
    ASSERT_STREQ(man->description, "Wakes.");
    ASSERT_TRUE(man->has_description);
    ASSERT_EQ_INT(man->data_version, 3);
    ASSERT_TRUE(man->has_idle_timeout);
    ASSERT_EQ_INT(man->idle_timeout_ms, 300000);
    ASSERT_TRUE(man->backfill);
    ASSERT_EQ_INT(man->max_batch_entries, 10);
    ASSERT_EQ_INT(man->max_batch_bytes, 1048576);
    ASSERT_EQ_INT(act->nevents, 2);
    ASSERT_STREQ(act->events[1], "memory.updated");
    ASSERT_EQ_INT(act->n_tags_any, 1);
    ASSERT_EQ_INT(act->n_tags_all, 2);
    ASSERT_STREQ(act->tags_all[1], "c");
    ASSERT_TRUE(act->has_match_before && act->match_before);
    ASSERT_STREQ(act->created_by[0], "human");
    ASSERT_EQ_INT(act->n_updated_by, 2);
    ASSERT_STREQ(act->bins[0], "live");
    ASSERT_STREQ(act->not_caused_by[0], "wake");
    ASSERT_EQ_INT(act->n_origins, 2);
    ASSERT_STREQ(act->origins[1], "import");
    ASSERT_TRUE(act->run.persistent);
    ASSERT_EQ_INT(act->run.argc, 1);
    ASSERT_TRUE(act->run.has_timeout);
    ASSERT_EQ_INT(act->run.timeout_ms, 120000);
    ASSERT_EQ_INT(act->nneeds, 2);
    ASSERT_STREQ(act->needs[1], "remember.search.max_results");
    ASSERT_EQ_INT(man->entries[1].priority, 7);
    ASSERT_EQ_INT(man->entries[1].budget_ms, 2000);
    ASSERT_EQ_INT(man->entries[1].grace_ms, 20000);
    plugin_manifest_free(man);
}

TEST(omitted_budget_grace_priority_process_and_origins_are_the_defaults)
{
    PluginManifest *man = given_parse(HEAD ACTIONS "," FILTERS "}", PLUGIN_PARSE_OK);

    ASSERT_EQ_INT(man->entries[0].run.persistent, 0);
    ASSERT_EQ_INT(man->entries[0].run.timeout_ms, 60000);
    ASSERT_EQ_INT(man->entries[0].run.has_timeout, 0);
    ASSERT_EQ_INT(man->entries[0].n_origins, 1);
    ASSERT_STREQ(man->entries[0].origins[0], "local");
    ASSERT_EQ_INT(man->entries[1].budget_ms, 800);
    ASSERT_EQ_INT(man->entries[1].grace_ms, 10000);
    ASSERT_EQ_INT(man->entries[1].priority, 500);
    ASSERT_EQ_INT(man->data_version, 1);
    ASSERT_EQ_INT(man->max_batch_entries, 100);
    ASSERT_EQ_INT(man->max_batch_bytes, 4194304);
    ASSERT_EQ_INT(man->backfill, 0);
    plugin_manifest_free(man);
}

TEST(an_action_filter_block_without_origins_still_defaults_to_local)
{
    PluginManifest *man = given_parse(
        HEAD "\"actions\":[" ACTION_BODY ",\"filter\":{\"bins\":[\"live\"]}}]}", PLUGIN_PARSE_OK);

    ASSERT_EQ_INT(man->entries[0].n_origins, 1);
    ASSERT_STREQ(man->entries[0].origins[0], "local");
    plugin_manifest_free(man);
}

TEST(an_empty_origins_list_stays_empty)
{
    PluginManifest *man = given_parse(
        HEAD "\"actions\":[" ACTION_BODY ",\"filter\":{\"origins\":[]}}]}", PLUGIN_PARSE_OK);

    ASSERT_EQ_INT(man->entries[0].n_origins, 0);
    plugin_manifest_free(man);
}

/* Names ---------------------------------------------------------------- */

TEST(a_hyphen_in_a_manifest_name_prints_the_c36_hyphen_lines)
{
    expect_error("{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"url-expand\","
                 "\"plugin_version\":\"1.0.0\",\"protocol\":1," ACTIONS "}",
                 "/name", "R2.22", "\"url-expand\" in plugin.json can't contain \"-\".",
                 "Use \"url_expand\".");
}

TEST(a_hyphen_in_an_entry_name_folds_every_hyphen_in_the_fix)
{
    expect_error(HEAD "\"actions\":[{\"name\":\"a-b-c\",\"events\":[\"memory.created\"]," ACTION_RUN
                      "}]}",
                 "/actions/0/name", "R2.22", "\"a-b-c\" in plugin.json can't contain \"-\".",
                 "Use \"a_b_c\".");
}

TEST(a_hyphen_name_of_40_characters_gets_the_hyphen_line_and_41_does_not)
{
    char *name = given_run_of('a', 40);
    char doc[512];
    char fix[128];
    PluginManifest *man = NULL;

    name[10] = '-';
    (void)snprintf(doc, sizeof(doc),
                   "{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"%s\","
                   "\"plugin_version\":\"1.0.0\",\"protocol\":1," ACTIONS "}",
                   name);
    man = given_parse(doc, PLUGIN_PARSE_INVALID);
    name[10] = '_';
    (void)snprintf(fix, sizeof(fix), "Use \"%s\".", name);
    ASSERT_STR_CONTAINS(error_at(man, 0)->message, "can't contain");
    ASSERT_STREQ(error_at(man, 0)->fix, fix);
    plugin_manifest_free(man);
    name = given_run_of('a', 41);
    name[10] = '-';
    (void)snprintf(doc, sizeof(doc),
                   "{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"%s\","
                   "\"plugin_version\":\"1.0.0\",\"protocol\":1," ACTIONS "}",
                   name);
    man = given_parse(doc, PLUGIN_PARSE_INVALID);
    ASSERT_STR_CONTAINS(error_at(man, 0)->message, "isn't a valid name.");
    plugin_manifest_free(man);
}

/* G5 A (decision:2026-11-15-plan25-review-grill): the hyphen line only when its fix is a valid
   name; otherwise sentence 6, so no fix suggests a name that is refused. */
TEST(g5_a_hyphen_name_whose_underscore_form_is_invalid_gets_sentence_6)
{
    expect_error(
        HEAD "\"actions\":[{\"name\":\"A-b\",\"events\":[\"memory.created\"]," ACTION_RUN "}]}",
        "/actions/0/name", "R2.22", "plugin.json /actions/0/name: \"A-b\" isn't a valid name.",
        "Use a lowercase letter, then lowercase letters, digits, or \"_\", up to 40 characters.");
    expect_error(
        "{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"-a\","
        "\"plugin_version\":\"1.0.0\",\"protocol\":1," ACTIONS "}",
        "/name", "R2.22", "plugin.json /name: \"-a\" isn't a valid name.",
        "Use a lowercase letter, then lowercase letters, digits, or \"_\", up to 40 characters.");
}

TEST(a_forty_one_character_name_is_not_a_valid_name)
{
    const char *name = given_run_of('a', 41);
    char doc[512];
    PluginManifest *man = NULL;

    (void)snprintf(doc, sizeof(doc),
                   "{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"%s\","
                   "\"plugin_version\":\"1.0.0\",\"protocol\":1," ACTIONS "}",
                   name);
    man = given_parse(doc, PLUGIN_PARSE_INVALID);
    ASSERT_STR_CONTAINS(error_at(man, 0)->message, "isn't a valid name.");
    plugin_manifest_free(man);
}

TEST(a_forty_character_name_is_valid)
{
    const char *name = given_run_of('a', 40);
    char doc[512];
    PluginManifest *man = NULL;

    (void)snprintf(doc, sizeof(doc),
                   "{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"%s\","
                   "\"plugin_version\":\"1.0.0\",\"protocol\":1," ACTIONS "}",
                   name);
    man = given_parse(doc, PLUGIN_PARSE_OK);
    ASSERT_STREQ(man->name, name);
    plugin_manifest_free(man);
}

TEST(a_name_must_start_with_a_lowercase_letter)
{
    expect_error("{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"1ab\","
                 "\"plugin_version\":\"1.0.0\",\"protocol\":1," ACTIONS "}",
                 "/name", "R2.22", "plugin.json /name: \"1ab\" isn't a valid name.",
                 "Use a lowercase letter, then lowercase letters, digits, or \"_\", up to 40 "
                 "characters.");
}

TEST(a_name_that_is_not_a_string_must_be_a_string)
{
    expect_error("{\"format\":\"remember-plugin\",\"version\":1,\"name\":5,"
                 "\"plugin_version\":\"1.0.0\",\"protocol\":1," ACTIONS "}",
                 "/name", "R2.22", R25_2_MUST_BE("/name", "a string"), R25_2_USE("a string"));
}

TEST(a_name_with_a_nul_escape_is_not_json_so_nothing_is_cut_short)
{
    PluginManifest *man = given_parse("{\"name\":\"wake\\u0000-evil\"}", PLUGIN_PARSE_SYNTAX);

    ASSERT_STREQ(error_at(man, 0)->message, "plugin.json isn't valid JSON.");
    plugin_manifest_free(man);
}

TEST(a_key_with_a_nul_escape_is_not_json_so_it_cannot_alias_a_field)
{
    PluginManifest *man =
        given_parse("{\"protocol\\u0000x\":2,\"protocol\":1}", PLUGIN_PARSE_SYNTAX);

    ASSERT_EQ_INT(man->nerrors, 1);
    plugin_manifest_free(man);
}

TEST(a_setting_key_enabled_prints_the_c36_reserved_key_lines)
{
    PluginManifest *man =
        given_parse(HEAD ACTIONS ",\"settings\":[{\"key\":\"enabled\",\"description\":\"x\","
                                 "\"type\":\"boolean\",\"default\":true}]}",
                    PLUGIN_PARSE_INVALID);

    ASSERT_EQ_INT(man->nerrors, 1);
    ASSERT_STREQ(error_at(man, 0)->path, "/settings/0/key");
    ASSERT_STREQ(error_at(man, 0)->rule, "R2.11");
    ASSERT_STREQ(error_at(man, 0)->message,
                 "\"wake\" declares a setting named \"enabled\", and that name is reserved.");
    ASSERT_STREQ(error_at(man, 0)->fix, "Rename that setting in plugin.json.");
    plugin_manifest_free(man);
}

TEST(a_reserved_setting_key_in_a_manifest_with_no_name_says_this_plugin)
{
    PluginManifest *man = given_parse(
        "{\"format\":\"remember-plugin\",\"version\":1,\"plugin_version\":\"1.0.0\",\"protocol\":1,"
        "\"actions\":[" ACTION_BODY "}],\"settings\":[{\"key\":\"enabled\",\"description\":\"x\","
        "\"type\":\"boolean\",\"default\":true}]}",
        PLUGIN_PARSE_INVALID);

    ASSERT_EQ_INT(man->nerrors, 2);
    ASSERT_STREQ(error_at(man, 1)->path, "/settings/0/key");
    ASSERT_STREQ(error_at(man, 1)->message,
                 "this plugin declares a setting named \"enabled\", and that name is reserved.");
    plugin_manifest_free(man);
}

/* R25-2 sentence 16 is for a name that is missing or not a string. Any string name is usable, as
   in sentences 9 and 10, and prints JSON-escaped (Q2). */
TEST(a_reserved_setting_key_names_a_string_name_even_when_the_name_is_invalid)
{
    PluginManifest *man =
        given_parse("{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"Wake\","
                    "\"plugin_version\":\"1.0.0\",\"protocol\":1," ACTIONS
                    ",\"settings\":[{\"key\":\"enabled\",\"description\":\"x\","
                    "\"type\":\"boolean\",\"default\":true}]}",
                    PLUGIN_PARSE_INVALID);

    ASSERT_EQ_INT(man->nerrors, 2);
    ASSERT_STREQ(error_at(man, 1)->message,
                 "\"Wake\" declares a setting named \"enabled\", and that name is reserved.");
    plugin_manifest_free(man);
}

TEST(the_c36_reserved_key_line_escapes_a_hostile_plugin_name)
{
    PluginManifest *man =
        given_parse("{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"a\\\"\\u001b\","
                    "\"plugin_version\":\"1.0.0\",\"protocol\":1," ACTIONS
                    ",\"settings\":[{\"key\":\"enabled\",\"description\":\"x\","
                    "\"type\":\"boolean\",\"default\":true}]}",
                    PLUGIN_PARSE_INVALID);

    ASSERT_EQ_INT(man->nerrors, 2);
    ASSERT_STREQ(
        error_at(man, 1)->message,
        "\"a\\\"\\u001b\" declares a setting named \"enabled\", and that name is reserved.");
    plugin_manifest_free(man);
}

TEST(a_setting_key_with_a_hyphen_prints_the_c36_hyphen_lines)
{
    expect_error(HEAD ACTIONS ",\"settings\":[{\"key\":\"max-size\",\"description\":\"x\","
                              "\"type\":\"boolean\",\"default\":true}]}",
                 "/settings/0/key", "R2.22", "\"max-size\" in plugin.json can't contain \"-\".",
                 "Use \"max_size\".");
}

/* Version and protocol -------------------------------------------------- */

TEST(protocol_2_returns_plugin_protocol_unsupported_with_the_r25_1_message_and_fix)
{
    PluginManifest *man =
        given_parse("{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"router\","
                    "\"plugin_version\":\"1.0.0\",\"protocol\":2," ACTIONS "}",
                    PLUGIN_PARSE_UNSUPPORTED);

    ASSERT_EQ_INT(man->nerrors, 1);
    ASSERT_STREQ(error_at(man, 0)->code, "plugin_protocol_unsupported");
    ASSERT_STREQ(error_at(man, 0)->path, "/protocol");
    ASSERT_STREQ(error_at(man, 0)->rule, "R2.4");
    ASSERT_STREQ(error_at(man, 0)->message,
                 "plugin.json /protocol: \"router\" was written for plugin protocol 2, and this "
                 "remember reads protocol 1.");
    ASSERT_STREQ(error_at(man, 0)->fix,
                 "Update remember, or install an older version of \"router\".");
    ASSERT_EQ_INT(error_at(man, 0)->nsupported, 1);
    ASSERT_EQ_INT(error_at(man, 0)->supported[0], 1);
    plugin_manifest_free(man);
}

/* Q1 (decision:2026-11-14-copy-r25-2-manifest-approved): whole numbers are read by value. */

#define PROTOCOL_DOC(token) MINIMAL("\"version\":1,\"name\":\"a\",\"protocol\":" token)

/* The same, with the number token chosen at run time. */
static const char *protocol_doc_of(const char *token)
{
    static char doc[600];

    (void)snprintf(doc, sizeof(doc), MINIMAL("\"version\":1,\"name\":\"a\",\"protocol\":%s"),
                   token);
    return doc;
}
#define PROTOCOL_DOC_OF(token) protocol_doc_of(token)

TEST(q1_protocol_written_as_1_0_or_1e0_is_protocol_1)
{
    expect_ok(PROTOCOL_DOC("1.0"));
    expect_ok(PROTOCOL_DOC("1e0"));
    expect_ok(PROTOCOL_DOC("10e-1"));
    expect_ok(PROTOCOL_DOC("0.1e1"));
    expect_ok(PROTOCOL_DOC("1.000"));
}

TEST(q1_version_written_as_1_0_is_version_1)
{
    expect_ok(MINIMAL("\"version\":1.0,\"name\":\"a\",\"protocol\":1"));
}

TEST(q1_a_protocol_prints_its_value_and_not_its_source_text)
{
    PluginManifest *man = given_parse(PROTOCOL_DOC("2.0"), PLUGIN_PARSE_UNSUPPORTED);

    ASSERT_STREQ(
        error_at(man, 0)->message,
        "plugin.json /protocol: \"a\" was written for plugin protocol 2, and this remember "
        "reads protocol 1.");
    plugin_manifest_free(man);
    man = given_parse(PROTOCOL_DOC("2e0"), PLUGIN_PARSE_UNSUPPORTED);
    ASSERT_STR_CONTAINS(error_at(man, 0)->message, "plugin protocol 2, and");
    plugin_manifest_free(man);
    man = given_parse(PROTOCOL_DOC("-0"), PLUGIN_PARSE_UNSUPPORTED);
    ASSERT_STR_CONTAINS(error_at(man, 0)->message, "plugin protocol 0, and");
    plugin_manifest_free(man);
    man = given_parse(PROTOCOL_DOC("-3"), PLUGIN_PARSE_UNSUPPORTED);
    ASSERT_STR_CONTAINS(error_at(man, 0)->message, "plugin protocol -3, and");
    plugin_manifest_free(man);
    man = given_parse(PROTOCOL_DOC("20E-1"), PLUGIN_PARSE_UNSUPPORTED);
    ASSERT_STR_CONTAINS(error_at(man, 0)->message, "plugin protocol 2, and");
    plugin_manifest_free(man);
}

TEST(q1_a_huge_protocol_prints_its_digits)
{
    PluginManifest *man =
        given_parse(PROTOCOL_DOC("99999999999999999999999999"), PLUGIN_PARSE_UNSUPPORTED);

    ASSERT_STR_CONTAINS(error_at(man, 0)->message,
                        "plugin protocol 99999999999999999999999999, and");
    plugin_manifest_free(man);
    man = given_parse(PROTOCOL_DOC("1e30"), PLUGIN_PARSE_UNSUPPORTED);
    ASSERT_STR_CONTAINS(error_at(man, 0)->message,
                        "plugin protocol 1000000000000000000000000000000, and");
    plugin_manifest_free(man);
    man = given_parse(PROTOCOL_DOC("-1e20"), PLUGIN_PARSE_UNSUPPORTED);
    ASSERT_STR_CONTAINS(error_at(man, 0)->message, "plugin protocol -100000000000000000000, and");
    plugin_manifest_free(man);
}

TEST(q1_a_protocol_with_a_fraction_is_not_a_whole_number)
{
    PluginManifest *man = given_parse(PROTOCOL_DOC("1.5"), PLUGIN_PARSE_INVALID);

    ASSERT_STREQ(error_at(man, 0)->code, "plugin_manifest_invalid");
    ASSERT_STREQ(error_at(man, 0)->message, R25_2_MUST_BE("/protocol", "a whole number"));
    plugin_manifest_free(man);
    man = given_parse(PROTOCOL_DOC("1.0001"), PLUGIN_PARSE_INVALID);
    ASSERT_STREQ(error_at(man, 0)->message, R25_2_MUST_BE("/protocol", "a whole number"));
    plugin_manifest_free(man);
    man = given_parse(PROTOCOL_DOC("1e-1"), PLUGIN_PARSE_INVALID);
    ASSERT_STREQ(error_at(man, 0)->message, R25_2_MUST_BE("/protocol", "a whole number"));
    plugin_manifest_free(man);
}

TEST(q1_an_absurd_exponent_is_not_a_whole_number_and_costs_nothing)
{
    PluginManifest *man = given_parse(PROTOCOL_DOC("1e999999999"), PLUGIN_PARSE_INVALID);

    ASSERT_STREQ(error_at(man, 0)->message, R25_2_MUST_BE("/protocol", "a whole number"));
    plugin_manifest_free(man);
    man = given_parse(PROTOCOL_DOC("0e999999999"), PLUGIN_PARSE_UNSUPPORTED);
    ASSERT_STR_CONTAINS(error_at(man, 0)->message, "plugin protocol 0, and");
    plugin_manifest_free(man);
}

TEST(q1_priority_written_as_a_float_or_minus_zero_parses_by_value)
{
    static const char fmt[] = HEAD "\"filters\":[{\"name\":\"rank\",\"point\":\"search.results\","
                                   "\"priority\":%s," FILTER_RUN "}]}";
    PluginManifest *man = NULL;
    char doc[512];

    (void)snprintf(doc, sizeof(doc), fmt, "7.0");
    man = given_parse(doc, PLUGIN_PARSE_OK);
    ASSERT_EQ_INT(man->entries[0].priority, 7);
    plugin_manifest_free(man);
    (void)snprintf(doc, sizeof(doc), fmt, "-0");
    man = given_parse(doc, PLUGIN_PARSE_OK);
    ASSERT_EQ_INT(man->entries[0].priority, 0);
    plugin_manifest_free(man);
    (void)snprintf(doc, sizeof(doc), fmt, "1e3");
    man = given_parse(doc, PLUGIN_PARSE_OK);
    ASSERT_EQ_INT(man->entries[0].priority, 1000);
    plugin_manifest_free(man);
    expect_error_in(fmt, "7.5", "/filters/0/priority", "R2.5",
                    R25_2_MUST_BE("/filters/0/priority", "a whole number from 0 to 1000"),
                    R25_2_USE("a whole number from 0 to 1000"));
    expect_error_in(fmt, "1e4", "/filters/0/priority", "R2.5",
                    R25_2_MUST_BE("/filters/0/priority", "a whole number from 0 to 1000"),
                    R25_2_USE("a whole number from 0 to 1000"));
}

TEST(q1_an_integer_setting_default_written_as_1e2_is_stored_as_100)
{
    PluginManifest *man =
        given_parse(SETTING_DOC("\"type\":\"integer\",\"default\":1e2,\"range\":{\"min\":10.0}"),
                    PLUGIN_PARSE_OK);

    ASSERT_STREQ(man->settings[0].default_text, "100");
    ASSERT_EQ_INT(man->settings[0].min_value, 10);
    plugin_manifest_free(man);
    man = given_parse(SETTING_DOC("\"type\":\"integer\",\"default\":2.5"), PLUGIN_PARSE_INVALID);
    ASSERT_STREQ(error_at(man, 0)->message, R25_2_MUST_BE("/settings/0/default", "a whole number"));
    plugin_manifest_free(man);
}

TEST(q1_data_version_and_batch_entries_read_by_value)
{
    PluginManifest *man = given_parse(HEAD FILTERS ",\"data_version\":2.0,\"backfill\":"
                                                   "{\"enabled\":true,\"max_batch_entries\":5e1}}",
                                      PLUGIN_PARSE_OK);

    ASSERT_EQ_INT(man->data_version, 2);
    ASSERT_EQ_INT(man->max_batch_entries, 50);
    plugin_manifest_free(man);
}

TEST(protocol_with_no_usable_name_uses_the_plugin_wording)
{
    PluginManifest *man =
        given_parse(MINIMAL("\"version\":1,\"name\":7,\"protocol\":2"), PLUGIN_PARSE_UNSUPPORTED);

    /* Document order: the /name error comes first. */
    ASSERT_STREQ(error_at(man, 1)->message,
                 "plugin.json /protocol: this plugin was written for plugin protocol 2, and this "
                 "remember reads protocol 1.");
    ASSERT_STREQ(error_at(man, 1)->fix,
                 "Update remember, or install an older version of the plugin.");
    plugin_manifest_free(man);
}

TEST(protocol_names_the_plugin_as_the_file_wrote_it_even_when_the_name_is_invalid)
{
    PluginManifest *man = given_parse(MINIMAL("\"version\":1,\"name\":\"Bad-Name\",\"protocol\":2"),
                                      PLUGIN_PARSE_UNSUPPORTED);

    ASSERT_STR_CONTAINS(error_at(man, 1)->message, "\"Bad-Name\" was written");
    plugin_manifest_free(man);
}

TEST(a_protocol_that_is_not_a_number_must_be_a_whole_number)
{
    PluginManifest *man = given_parse(MINIMAL("\"version\":1,\"name\":\"a\",\"protocol\":\"1\""),
                                      PLUGIN_PARSE_INVALID);

    ASSERT_STREQ(error_at(man, 0)->code, "plugin_manifest_invalid");
    ASSERT_STREQ(error_at(man, 0)->message, R25_2_MUST_BE("/protocol", "a whole number"));
    plugin_manifest_free(man);
}

TEST(manifest_version_2_returns_plugin_protocol_unsupported_in_the_r25_1_shape)
{
    PluginManifest *man =
        given_parse("{\"format\":\"remember-plugin\",\"version\":2,\"name\":\"router\","
                    "\"plugin_version\":\"1.0.0\",\"protocol\":1," ACTIONS "}",
                    PLUGIN_PARSE_UNSUPPORTED);

    ASSERT_EQ_INT(man->nerrors, 1);
    ASSERT_STREQ(error_at(man, 0)->code, "plugin_protocol_unsupported");
    ASSERT_STREQ(error_at(man, 0)->path, "/version");
    ASSERT_STREQ(error_at(man, 0)->message,
                 "plugin.json /version: \"router\" uses plugin.json format 2, and this remember "
                 "reads format 1.");
    ASSERT_STREQ(error_at(man, 0)->fix,
                 "Update remember, or install an older version of \"router\".");
    ASSERT_EQ_INT(error_at(man, 0)->supported[0], 1);
    plugin_manifest_free(man);
}

TEST(manifest_version_with_no_usable_name_uses_the_plugin_wording)
{
    PluginManifest *man =
        given_parse(MINIMAL("\"version\":3,\"protocol\":1"), PLUGIN_PARSE_UNSUPPORTED);

    /* The missing "name" names the top level, which comes first. */
    ASSERT_STREQ(error_at(man, 1)->message,
                 "plugin.json /version: this plugin uses plugin.json format 3, and this remember "
                 "reads format 1.");
    plugin_manifest_free(man);
}

TEST(both_a_bad_version_and_a_bad_protocol_come_back)
{
    PluginManifest *man = given_parse(MINIMAL("\"version\":2,\"name\":\"a\",\"protocol\":3"),
                                      PLUGIN_PARSE_UNSUPPORTED);

    ASSERT_STREQ(error_at(man, 0)->path, "/version");
    ASSERT_STREQ(error_at(man, 1)->path, "/protocol");
    plugin_manifest_free(man);
}

TEST(a_missing_version_and_protocol_are_missing_members)
{
    PluginManifest *man = given_parse("{\"name\":\"a\"}", PLUGIN_PARSE_INVALID);

    ASSERT_STREQ(error_at(man, 1)->message, R25_2_MISSING("", "version"));
    ASSERT_STREQ(error_at(man, 2)->message, R25_2_MISSING("", "protocol"));
    plugin_manifest_free(man);
}

TEST(a_version_that_is_not_a_number_must_be_a_whole_number)
{
    PluginManifest *man = given_parse(MINIMAL("\"version\":\"1\",\"name\":\"a\",\"protocol\":1"),
                                      PLUGIN_PARSE_INVALID);

    ASSERT_STREQ(error_at(man, 0)->message, R25_2_MUST_BE("/version", "a whole number"));
    plugin_manifest_free(man);
}

/* Not a manifest at all ------------------------------------------------- */

TEST(text_that_is_not_json_is_a_syntax_error_with_one_line)
{
    PluginManifest *man = given_parse("{\"name\":", PLUGIN_PARSE_SYNTAX);

    ASSERT_EQ_INT(man->nerrors, 1);
    ASSERT_STREQ(error_at(man, 0)->code, "plugin_manifest_invalid");
    ASSERT_STREQ(error_at(man, 0)->path, "");
    ASSERT_STREQ(error_at(man, 0)->rule, "R2.2");
    ASSERT_STREQ(error_at(man, 0)->message, "plugin.json isn't valid JSON.");
    ASSERT_STREQ(error_at(man, 0)->fix, "Fix the JSON syntax in plugin.json.");
    plugin_manifest_free(man);
}

TEST(a_json_array_at_the_top_is_not_an_object)
{
    PluginManifest *man = given_parse("[1]", PLUGIN_PARSE_SYNTAX);

    ASSERT_STREQ(error_at(man, 0)->message, "plugin.json must hold one JSON object.");
    ASSERT_STREQ(error_at(man, 0)->fix, "Make the top level of plugin.json an object.");
    plugin_manifest_free(man);
}

TEST(a_null_text_and_a_null_out_are_refused_without_a_crash)
{
    PluginManifest *man = NULL;

    ASSERT_EQ_STATUS(plugin_manifest_parse(NULL, 0, &man), PLUGIN_PARSE_SYNTAX);
    ASSERT_TRUE(man != NULL);
    plugin_manifest_free(man);
    ASSERT_EQ_STATUS(plugin_manifest_parse("{}", 2, NULL), PLUGIN_PARSE_SYNTAX);
    plugin_manifest_free(NULL);
}

TEST(a_manifest_with_neither_actions_nor_filters_is_refused)
{
    PluginManifest *man = given_parse(HEAD "\"x\":1}", PLUGIN_PARSE_INVALID);

    ASSERT_EQ_INT(man->nerrors, 1);
    ASSERT_STREQ(error_at(man, 0)->path, "");
    ASSERT_STREQ(error_at(man, 0)->message,
                 "plugin.json: \"actions\" and \"filters\" are both missing.");
    ASSERT_STREQ(error_at(man, 0)->fix, "Add \"actions\" or \"filters\".");
    plugin_manifest_free(man);
}

TEST(a_missing_member_names_the_object_that_lacks_it)
{
    expect_error(HEAD "\"filters\":[{\"name\":\"rank\"," FILTER_RUN "}]}", "/filters/0", "R2.5",
                 R25_2_MISSING(" /filters/0", "point"), R25_2_ADD("point"));
}

TEST(format_must_be_remember_plugin)
{
    expect_error(
        "{\"format\":\"other\",\"version\":1,\"name\":\"wake\",\"plugin_version\":\"1.0.0\","
        "\"protocol\":1," ACTIONS "}",
        "/format", "R2.2", R25_2_MUST_BE("/format", "\"remember-plugin\""),
        R25_2_USE("\"remember-plugin\""));
}

TEST(a_format_that_is_not_a_string_must_be_remember_plugin)
{
    expect_error("{\"format\":3,\"version\":1,\"name\":\"wake\",\"plugin_version\":\"1.0.0\","
                 "\"protocol\":1," ACTIONS "}",
                 "/format", "R2.2", R25_2_MUST_BE("/format", "\"remember-plugin\""),
                 R25_2_USE("\"remember-plugin\""));
}

/* Closed and open objects ----------------------------------------------- */

TEST(an_unknown_field_on_run_is_plugin_manifest_invalid_with_a_json_pointer)
{
    expect_error(HEAD
                 "\"filters\":[{\"name\":\"rank\",\"point\":\"search.results\","
                 "\"run\":{\"process\":\"persistent\",\"command\":[\"hello.py\"],\"extra\":1}}]}",
                 "/filters/0/run/extra", "R2.5", R25_2_UNKNOWN(" /filters/0/run", "extra"),
                 R25_2_REMOVE("extra"));
}

TEST(an_unknown_field_one_edit_from_a_known_one_gets_a_did_you_mean_fix)
{
    expect_error(HEAD
                 "\"filters\":[{\"name\":\"rank\",\"point\":\"search.results\","
                 "\"run\":{\"process\":\"persistent\",\"proces\":1,\"command\":[\"hello.py\"]}}]}",
                 "/filters/0/run/proces", "R2.5", R25_2_UNKNOWN(" /filters/0/run", "proces"),
                 "Did you mean \"process\"?");
}

TEST(the_did_you_mean_fix_covers_a_substitution_and_an_extra_letter)
{
    PluginManifest *man = given_parse(
        HEAD "\"filters\":[{\"name\":\"rank\",\"point\":\"search.results\","
             "\"run\":{\"process\":\"persistent\",\"command\":[\"hello.py\"],\"comman\":1,"
             "\"commandx\":1,\"commard\":1,\"cmmand\":1,\"cmdd\":1}}]}",
        PLUGIN_PARSE_INVALID);

    ASSERT_EQ_INT(man->nerrors, 5);
    ASSERT_STREQ(error_at(man, 0)->fix, "Did you mean \"command\"?");
    ASSERT_STREQ(error_at(man, 1)->fix, "Did you mean \"command\"?");
    ASSERT_STREQ(error_at(man, 2)->fix, "Did you mean \"command\"?");
    ASSERT_STREQ(error_at(man, 3)->fix, "Did you mean \"command\"?");
    ASSERT_STREQ(error_at(man, 4)->fix, R25_2_REMOVE("cmdd"));
    plugin_manifest_free(man);
}

TEST(a_field_two_edits_away_gets_the_remove_fix)
{
    expect_error(HEAD
                 "\"filters\":[{\"name\":\"rank\",\"point\":\"search.results\","
                 "\"run\":{\"process\":\"persistent\",\"command\":[\"hello.py\"],\"commm\":1}}]}",
                 "/filters/0/run/commm", "R2.5", R25_2_UNKNOWN(" /filters/0/run", "commm"),
                 R25_2_REMOVE("commm"));
}

TEST(a_same_length_name_two_substitutions_away_is_not_a_did_you_mean)
{
    expect_error(HEAD
                 "\"filters\":[{\"name\":\"rank\",\"point\":\"search.results\","
                 "\"run\":{\"process\":\"persistent\",\"command\":[\"hello.py\"],\"zzmand\":1}}]}",
                 "/filters/0/run/zzmand", "R2.5", R25_2_UNKNOWN(" /filters/0/run", "zzmand"),
                 R25_2_REMOVE("zzmand"));
}

TEST(an_unknown_field_on_permissions_is_invalid)
{
    expect_error(HEAD "\"filters\":[{\"name\":\"rank\",\"point\":\"search.results\"," FILTER_RUN
                      ",\"permissions\":{\"neeeds\":[]}}]}",
                 "/filters/0/permissions/neeeds", "R2.5",
                 R25_2_UNKNOWN(" /filters/0/permissions", "neeeds"), "Did you mean \"needs\"?");
}

TEST(an_unknown_field_in_an_action_filter_block_is_invalid)
{
    expect_error(HEAD "\"actions\":[" ACTION_BODY ",\"filter\":{\"binz\":[]}}]}",
                 "/actions/0/filter/binz", "R2.21", R25_2_UNKNOWN(" /actions/0/filter", "binz"),
                 "Did you mean \"bins\"?");
}

TEST(a_filter_run_timeout_is_an_unknown_closed_field)
{
    expect_error(HEAD "\"filters\":[{\"name\":\"rank\",\"point\":\"search.results\","
                      "\"run\":{\"process\":\"persistent\",\"command\":[\"hello.py\"],"
                      "\"timeout\":\"5s\"}}]}",
                 "/filters/0/run/timeout", "R2.5", R25_2_UNKNOWN(" /filters/0/run", "timeout"),
                 R25_2_REMOVE("timeout"));
}

TEST(top_level_run_permissions_and_filter_are_errors_not_warnings)
{
    PluginManifest *man = given_parse(HEAD ACTIONS ",\"run\":{},\"permissions\":{},\"filter\":{}}",
                                      PLUGIN_PARSE_INVALID);

    ASSERT_EQ_INT(man->nerrors, 3);
    ASSERT_EQ_INT(man->nwarnings, 0);
    ASSERT_STREQ(error_at(man, 0)->path, "/run");
    ASSERT_STREQ(error_at(man, 0)->rule, "R2.5");
    ASSERT_STREQ(error_at(man, 0)->message, "plugin.json: \"run\" can't be at the top level.");
    ASSERT_STREQ(error_at(man, 0)->fix, "Move \"run\" into an action or a filter.");
    ASSERT_STREQ(error_at(man, 1)->path, "/permissions");
    ASSERT_STREQ(error_at(man, 1)->rule, "R2.5");
    ASSERT_STREQ(error_at(man, 1)->message,
                 "plugin.json: \"permissions\" can't be at the top level.");
    ASSERT_STREQ(error_at(man, 1)->fix, "Move \"permissions\" into an action or a filter.");
    ASSERT_STREQ(error_at(man, 2)->path, "/filter");
    ASSERT_STREQ(error_at(man, 2)->rule, "R2.21");
    ASSERT_STREQ(error_at(man, 2)->message, "plugin.json: \"filter\" can't be at the top level.");
    ASSERT_STREQ(error_at(man, 2)->fix, "Move \"filter\" into an action.");
    plugin_manifest_free(man);
}

TEST(an_unknown_root_field_is_a_warning_and_the_rest_parses)
{
    PluginManifest *man = given_parse(HEAD ACTIONS ",\"homepage\":\"x\"}", PLUGIN_PARSE_OK);

    ASSERT_EQ_INT(man->nerrors, 0);
    ASSERT_EQ_INT(man->nwarnings, 1);
    ASSERT_STREQ(warning_at(man, 0)->path, "");
    ASSERT_STREQ(warning_at(man, 0)->field, "homepage");
    ASSERT_EQ_INT(man->nentries, 1);
    plugin_manifest_free(man);
}

#ifdef REMEMBER_PLUGINS_DIAG_TEST
TEST(an_unknown_root_field_prints_the_c35_file_line_and_the_rest_parses)
{
    PluginManifest *man = given_parse(HEAD ACTIONS ",\"homepage\":\"x\"}", PLUGIN_PARSE_OK);
    char *buf = NULL;
    size_t len = 0U;
    FILE *err = open_memstream(&buf, &len);
    char file[] = "/tmp/wake/plugin.json";

    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        plugin_manifest_free(man);
        return;
    }
    diag_json_end();
    diag_warnings_reset();
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(
        diag_unknown_field((DiagUnknownField){.path = file, .name = warning_at(man, 0)->field}), 0);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf, "remember: warning: \"/tmp/wake/plugin.json\" has a field remember doesn't "
                      "know, \"homepage\". The rest of the file was read.\n");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    diag_warnings_reset();
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
    ASSERT_EQ_INT(man->nentries, 1);
    plugin_manifest_free(man);
}

/* DoD: the R25-1, C-35 and C-36 lines escape a hostile name the same way (G2 A). */
TEST(the_c35_line_escapes_a_hostile_unknown_root_field)
{
    PluginManifest *man = given_parse(HEAD ACTIONS ",\"home\\u001b\\\"\":\"x\"}", PLUGIN_PARSE_OK);
    char *buf = NULL;
    size_t len = 0U;
    FILE *err = open_memstream(&buf, &len);
    char file[] = "/tmp/wake/plugin.json";

    ASSERT_TRUE(err != NULL);
    if (err == NULL) {
        plugin_manifest_free(man);
        return;
    }
    diag_json_end();
    diag_warnings_reset();
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(
        diag_unknown_field((DiagUnknownField){.path = file, .name = warning_at(man, 0)->field}), 0);
    ASSERT_EQ_INT(fflush(err), 0);
    ASSERT_STREQ(buf, "remember: warning: \"/tmp/wake/plugin.json\" has a field remember doesn't "
                      "know, \"home\\u001b\\\"\". The rest of the file was read.\n");
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    diag_warnings_reset();
    ASSERT_EQ_INT(fclose(err), 0);
    free(buf);
    ASSERT_EQ_INT(man->nentries, 1);
    plugin_manifest_free(man);
}
#endif

TEST(working_directory_is_an_unknown_action_field)
{
    PluginManifest *man = given_parse(
        HEAD "\"actions\":[" ACTION_BODY ",\"working_directory\":\"../../..\"}]}", PLUGIN_PARSE_OK);

    ASSERT_EQ_INT(man->nwarnings, 1);
    ASSERT_STREQ(warning_at(man, 0)->path, "/actions/0");
    ASSERT_STREQ(warning_at(man, 0)->field, "working_directory");
    plugin_manifest_free(man);
}

TEST(an_unknown_field_in_open_tags_is_a_warning)
{
    PluginManifest *man = given_parse(HEAD "\"actions\":[" ACTION_BODY
                                           ",\"filter\":{\"tags\":{\"any\":[],\"extra\":1}}}]}",
                                      PLUGIN_PARSE_OK);

    ASSERT_EQ_INT(man->nwarnings, 1);
    ASSERT_STREQ(warning_at(man, 0)->path, "/actions/0/filter/tags");
    ASSERT_STREQ(warning_at(man, 0)->field, "extra");
    plugin_manifest_free(man);
}

TEST(an_unknown_field_in_a_setting_range_is_a_warning)
{
    PluginManifest *man = given_parse(
        HEAD ACTIONS ",\"settings\":[{\"key\":\"n\",\"description\":\"d\",\"type\":\"integer\","
                     "\"default\":1,\"range\":{\"min\":0,\"max\":5,\"step\":1}}]}",
        PLUGIN_PARSE_OK);

    ASSERT_EQ_INT(man->nwarnings, 1);
    ASSERT_STREQ(warning_at(man, 0)->path, "/settings/0/range");
    plugin_manifest_free(man);
}

TEST(unknown_field_pointer_escapes_slash_and_tilde)
{
    PluginManifest *man =
        given_parse(HEAD "\"filters\":[{\"name\":\"rank\",\"point\":\"search.results\","
                         "\"run\":{\"process\":\"persistent\",\"command\":[\"x\"],\"a/b~c\":1}}]}",
                    PLUGIN_PARSE_INVALID);

    ASSERT_STREQ(error_at(man, 0)->path, "/filters/0/run/a~1b~0c");
    plugin_manifest_free(man);
}

TEST(a_long_unknown_field_keeps_its_whole_name_and_pointer)
{
    const char *key = given_run_of('k', 300);
    char doc[600];
    char path[400];
    PluginManifest *man = NULL;

    (void)snprintf(doc, 600U,
                   HEAD "\"filters\":[{\"name\":\"rank\",\"point\":\"search.results\","
                        "\"run\":{\"process\":\"persistent\",\"command\":[\"x\"],\"%s\":1}}]}",
                   key);
    man = given_parse(doc, PLUGIN_PARSE_INVALID);
    (void)snprintf(path, 400U, "/filters/0/run/%s", key);
    ASSERT_STREQ(error_at(man, 0)->path, path);
    ASSERT_EQ_INT(strlen(error_at(man, 0)->message),
                  strlen("plugin.json /filters/0/run: unknown field \"\".") + 300U);
    ASSERT_EQ_INT(strlen(error_at(man, 0)->fix), strlen("Remove \"\".") + 300U);
    plugin_manifest_free(man);
}

TEST(a_long_unknown_open_field_is_kept_whole_in_the_warning)
{
    const char *key = given_run_of('w', 300);
    char doc[900];
    PluginManifest *man = NULL;

    (void)snprintf(doc, 900U, HEAD ACTIONS ",\"%s\":1}", key);
    man = given_parse(doc, PLUGIN_PARSE_OK);
    ASSERT_STREQ(warning_at(man, 0)->field, key);
    plugin_manifest_free(man);
}

TEST(a_repeated_protocol_is_refused)
{
    PluginManifest *man = given_parse("{\"format\":\"remember-plugin\",\"version\":1,\"name\":"
                                      "\"wake\",\"plugin_version\":\"1.0.0\","
                                      "\"protocol\":1,\"protocol\":2," ACTIONS "}",
                                      PLUGIN_PARSE_INVALID);

    ASSERT_EQ_INT(man->nerrors, 1);
    ASSERT_STREQ(error_at(man, 0)->path, "/protocol");
    ASSERT_STREQ(error_at(man, 0)->rule, "R2.2");
    ASSERT_STREQ(error_at(man, 0)->message, R25_2_TWICE("", "protocol"));
    ASSERT_STREQ(error_at(man, 0)->fix, R25_2_KEEP("protocol"));
    plugin_manifest_free(man);
}

TEST(repeated_permissions_are_refused)
{
    PluginManifest *man =
        given_parse(HEAD "\"actions\":[" ACTION_BODY ",\"permissions\":{\"needs\":[\"body\"]},"
                         "\"permissions\":{\"needs\":[\"links\"]}}]}",
                    PLUGIN_PARSE_INVALID);

    ASSERT_EQ_INT(man->nerrors, 1);
    ASSERT_STREQ(error_at(man, 0)->path, "/actions/0/permissions");
    ASSERT_STREQ(error_at(man, 0)->message, R25_2_TWICE(" /actions/0", "permissions"));
    plugin_manifest_free(man);
}

TEST(a_key_repeated_a_third_time_is_reported_again)
{
    PluginManifest *man =
        given_parse(HEAD ACTIONS ",\"x\":1,\"x\":2,\"x\":3}", PLUGIN_PARSE_INVALID);

    ASSERT_EQ_INT(man->nerrors, 2);
    plugin_manifest_free(man);
}

TEST(a_repeated_key_with_a_slash_has_an_escaped_pointer)
{
    PluginManifest *man = given_parse(HEAD ACTIONS ",\"a/b\":1,\"a/b\":2}", PLUGIN_PARSE_INVALID);

    ASSERT_STREQ(error_at(man, 0)->path, "/a~1b");
    plugin_manifest_free(man);
}

TEST(a_hostile_object_with_thousands_of_keys_still_parses_fast)
{
    enum { KEYS = 20000 };
    static char doc[(KEYS * 20) + 1024];
    PluginManifest *man = NULL;
    size_t used = 0;
    int i = 0;

    (void)snprintf(doc, sizeof(doc), "%s", HEAD ACTIONS);
    used = strlen(doc);
    for (i = 0; i < KEYS; i++) {
        char piece[32];
        size_t len = 0;

        (void)snprintf(piece, sizeof(piece), ",\"k%d\":1", i % 5000);
        len = strlen(piece);
        memcpy(doc + used, piece, len + 1U);
        used += len;
    }
    appendf(doc + used, sizeof(doc) - used, "}");
    man = given_parse(doc, PLUGIN_PARSE_INVALID);
    ASSERT_EQ_INT(man->nerrors, KEYS - 5000);
    ASSERT_EQ_INT(man->nwarnings, 5000);
    plugin_manifest_free(man);
}

/* Every error at once --------------------------------------------------- */

TEST(two_errors_in_one_file_both_come_back_from_one_parse)
{
    PluginManifest *man = given_parse(
        "{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"url-x\","
        "\"plugin_version\":\"1.0.0\",\"protocol\":1,\"filters\":[{\"name\":\"rank\","
        "\"point\":\"search.results\",\"run\":{\"process\":\"persistent\",\"command\":[\"x\"],"
        "\"proces\":1}}]}",
        PLUGIN_PARSE_INVALID);

    ASSERT_EQ_INT(man->nerrors, 2);
    ASSERT_STREQ(error_at(man, 0)->path, "/name");
    ASSERT_STREQ(error_at(man, 1)->path, "/filters/0/run/proces");
    plugin_manifest_free(man);
}

TEST(a_missing_name_does_not_hide_the_errors_after_it)
{
    PluginManifest *man = given_parse(
        "{\"format\":\"remember-plugin\",\"version\":1,\"plugin_version\":\"x\",\"protocol\":1,"
        "\"description\":5,\"data_version\":0,\"idle_timeout\":\"soon\"," ACTIONS "}",
        PLUGIN_PARSE_INVALID);

    ASSERT_EQ_INT(man->nerrors, 5);
    ASSERT_STREQ(error_at(man, 0)->message, R25_2_MISSING("", "name"));
    ASSERT_STREQ(error_at(man, 1)->path, "/plugin_version");
    ASSERT_STREQ(error_at(man, 2)->path, "/description");
    ASSERT_STREQ(error_at(man, 3)->path, "/data_version");
    ASSERT_STREQ(error_at(man, 4)->path, "/idle_timeout");
    plugin_manifest_free(man);
}

TEST(every_error_comes_back_past_sixteen)
{
    enum { ACTIONS_N = 16 };
    char doc[2048] = HEAD "\"actions\":[";
    size_t i = 0;
    PluginManifest *man = NULL;

    for (i = 0; i < ACTIONS_N; i++) {
        (void)snprintf(doc + strlen(doc), sizeof(doc) - strlen(doc),
                       "%s{\"name\":\"Bad\",\"events\":[\"no\"],\"run\":{\"command\":[\"a\"]}}",
                       i == 0U ? "" : ",");
    }
    append(doc, sizeof(doc), "]}");
    man = given_parse(doc, PLUGIN_PARSE_INVALID);
    ASSERT_EQ_INT(man->nerrors, 2 * ACTIONS_N);
    plugin_manifest_free(man);
}

/* Shapes and ranges ----------------------------------------------------- */

TEST(budget_0ms_and_31s_are_invalid_and_1ms_and_30s_parse)
{
    static const char fmt[] = HEAD "\"filters\":[{\"name\":\"rank\",\"point\":\"search.results\","
                                   "\"budget\":\"%s\"," FILTER_RUN "}]}";

    expect_error_in(fmt, "0ms", "/filters/0/budget", "budget_range",
                    R25_2_MUST_BE("/filters/0/budget", "a duration from 1ms to 30s"),
                    R25_2_USE("a duration from 1ms to 30s"));
    expect_error_in(fmt, "30001ms", "/filters/0/budget", "budget_range",
                    R25_2_MUST_BE("/filters/0/budget", "a duration from 1ms to 30s"),
                    R25_2_USE("a duration from 1ms to 30s"));
    expect_error_in(fmt, "31s", "/filters/0/budget", "budget_range",
                    R25_2_MUST_BE("/filters/0/budget", "a duration from 1ms to 30s"),
                    R25_2_USE("a duration from 1ms to 30s"));
    {
        PluginManifest *man = NULL;
        char doc[512];

        (void)snprintf(doc, sizeof(doc), fmt, "1ms");
        man = given_parse(doc, PLUGIN_PARSE_OK);
        ASSERT_EQ_INT(man->entries[0].budget_ms, 1);
        plugin_manifest_free(man);
        (void)snprintf(doc, sizeof(doc), fmt, "30s");
        man = given_parse(doc, PLUGIN_PARSE_OK);
        ASSERT_EQ_INT(man->entries[0].budget_ms, 30000);
        plugin_manifest_free(man);
    }
}

TEST(grace_0ms_is_invalid_1ms_and_30s_parse_and_31s_is_invalid)
{
    static const char fmt[] = HEAD "\"filters\":[{\"name\":\"rank\",\"point\":\"search.results\","
                                   "\"cold_start_grace\":\"%s\"," FILTER_RUN "}]}";
    PluginManifest *man = NULL;
    char doc[512];

    expect_error_in(fmt, "0ms", "/filters/0/cold_start_grace", "R2.13",
                    R25_2_MUST_BE("/filters/0/cold_start_grace", "a duration from 1ms to 30s"),
                    R25_2_USE("a duration from 1ms to 30s"));
    expect_error_in(fmt, "30001ms", "/filters/0/cold_start_grace", "R2.13",
                    R25_2_MUST_BE("/filters/0/cold_start_grace", "a duration from 1ms to 30s"),
                    R25_2_USE("a duration from 1ms to 30s"));
    expect_error_in(fmt, "31s", "/filters/0/cold_start_grace", "R2.13",
                    R25_2_MUST_BE("/filters/0/cold_start_grace", "a duration from 1ms to 30s"),
                    R25_2_USE("a duration from 1ms to 30s"));
    (void)snprintf(doc, sizeof(doc), fmt, "1ms");
    man = given_parse(doc, PLUGIN_PARSE_OK);
    ASSERT_EQ_INT(man->entries[0].grace_ms, 1);
    plugin_manifest_free(man);
    (void)snprintf(doc, sizeof(doc), fmt, "30s");
    man = given_parse(doc, PLUGIN_PARSE_OK);
    ASSERT_EQ_INT(man->entries[0].grace_ms, 30000);
    plugin_manifest_free(man);
}

TEST(a_budget_that_is_not_a_duration_names_the_same_range)
{
    static const char fmt[] = HEAD "\"filters\":[{\"name\":\"rank\",\"point\":\"search.results\","
                                   "\"budget\":%s," FILTER_RUN "}]}";

    expect_error_in(fmt, "800", "/filters/0/budget", "budget_range",
                    R25_2_MUST_BE("/filters/0/budget", "a duration from 1ms to 30s"),
                    R25_2_USE("a duration from 1ms to 30s"));
    expect_error_in(fmt, "\"1.5s\"", "/filters/0/budget", "budget_range",
                    R25_2_MUST_BE("/filters/0/budget", "a duration from 1ms to 30s"),
                    R25_2_USE("a duration from 1ms to 30s"));
}

TEST(priority_runs_from_0_to_1000)
{
    static const char fmt[] = HEAD "\"filters\":[{\"name\":\"rank\",\"point\":\"search.results\","
                                   "\"priority\":%s," FILTER_RUN "}]}";
    PluginManifest *man = NULL;
    char doc[512];

    expect_error_in(fmt, "1001", "/filters/0/priority", "R2.5",
                    R25_2_MUST_BE("/filters/0/priority", "a whole number from 0 to 1000"),
                    R25_2_USE("a whole number from 0 to 1000"));
    expect_error_in(fmt, "-1", "/filters/0/priority", "R2.5",
                    R25_2_MUST_BE("/filters/0/priority", "a whole number from 0 to 1000"),
                    R25_2_USE("a whole number from 0 to 1000"));
    expect_error_in(fmt, "\"5\"", "/filters/0/priority", "R2.5",
                    R25_2_MUST_BE("/filters/0/priority", "a whole number from 0 to 1000"),
                    R25_2_USE("a whole number from 0 to 1000"));
    (void)snprintf(doc, sizeof(doc), fmt, "0");
    man = given_parse(doc, PLUGIN_PARSE_OK);
    ASSERT_EQ_INT(man->entries[0].priority, 0);
    plugin_manifest_free(man);
    (void)snprintf(doc, sizeof(doc), fmt, "1000");
    man = given_parse(doc, PLUGIN_PARSE_OK);
    ASSERT_EQ_INT(man->entries[0].priority, 1000);
    plugin_manifest_free(man);
}

TEST(a_filter_point_must_be_search_query_or_search_results)
{
    expect_error(HEAD "\"filters\":[{\"name\":\"rank\",\"point\":\"search.x\"," FILTER_RUN "}]}",
                 "/filters/0/point", "R2.5",
                 R25_2_MUST_BE("/filters/0/point", "\"search.query\" or \"search.results\""),
                 R25_2_USE("\"search.query\" or \"search.results\""));
}

TEST(a_filter_must_declare_process_persistent)
{
    expect_error(HEAD "\"filters\":[{\"name\":\"rank\",\"point\":\"search.query\","
                      "\"run\":{\"process\":\"once\",\"command\":[\"x\"]}}]}",
                 "/filters/0/run/process", "R2.9",
                 R25_2_MUST_BE("/filters/0/run/process", "\"persistent\""),
                 R25_2_USE("\"persistent\""));
    expect_error(HEAD "\"filters\":[{\"name\":\"rank\",\"point\":\"search.query\","
                      "\"run\":{\"command\":[\"x\"]}}]}",
                 "/filters/0/run", "R2.9", R25_2_MISSING(" /filters/0/run", "process"),
                 R25_2_ADD("process"));
}

TEST(an_action_process_is_once_or_persistent)
{
    expect_error(HEAD "\"actions\":[{\"name\":\"go\",\"events\":[\"memory.created\"],"
                      "\"run\":{\"process\":\"daemon\",\"command\":[\"x\"]}}]}",
                 "/actions/0/run/process", "R2.9",
                 R25_2_MUST_BE("/actions/0/run/process", "\"once\" or \"persistent\""),
                 R25_2_USE("\"once\" or \"persistent\""));
}

TEST(an_action_timeout_is_a_duration)
{
    expect_error(HEAD "\"actions\":[{\"name\":\"go\",\"events\":[\"memory.created\"],"
                      "\"run\":{\"command\":[\"x\"],\"timeout\":\"soon\"}}]}",
                 "/actions/0/run/timeout", "R2.5",
                 R25_2_MUST_BE("/actions/0/run/timeout", "a duration like \"10m\""),
                 R25_2_USE("a duration like \"10m\""));
}

TEST(a_run_section_is_required_and_must_be_an_object)
{
    expect_error(HEAD "\"actions\":[{\"name\":\"go\",\"events\":[\"memory.created\"]}]}",
                 "/actions/0", "R2.5", R25_2_MISSING(" /actions/0", "run"), R25_2_ADD("run"));
    expect_error(HEAD "\"actions\":[{\"name\":\"go\",\"events\":[\"memory.created\"],\"run\":[]}]}",
                 "/actions/0/run", "R2.5", R25_2_MUST_BE("/actions/0/run", "an object"),
                 R25_2_USE("an object"));
}

TEST(a_command_is_required_and_is_a_short_list_of_nonempty_strings)
{
    expect_error(HEAD "\"actions\":[{\"name\":\"go\",\"events\":[\"memory.created\"],"
                      "\"run\":{}}]}",
                 "/actions/0/run", "R2.8", R25_2_MISSING(" /actions/0/run", "command"),
                 R25_2_ADD("command"));
    expect_error(HEAD "\"actions\":[{\"name\":\"go\",\"events\":[\"memory.created\"],"
                      "\"run\":{\"command\":[]}}]}",
                 "/actions/0/run/command", "R2.8",
                 R25_2_MUST_BE("/actions/0/run/command", "a list of 1 to 8 strings"),
                 R25_2_USE("a list of 1 to 8 strings"));
    expect_error(HEAD
                 "\"actions\":[{\"name\":\"go\",\"events\":[\"memory.created\"],"
                 "\"run\":{\"command\":[\"a\",\"b\",\"c\",\"d\",\"e\",\"f\",\"g\",\"h\",\"i\"]}}]}",
                 "/actions/0/run/command", "R2.8",
                 R25_2_MUST_BE("/actions/0/run/command", "a list of 1 to 8 strings"),
                 R25_2_USE("a list of 1 to 8 strings"));
    expect_error(HEAD "\"actions\":[{\"name\":\"go\",\"events\":[\"memory.created\"],"
                      "\"run\":{\"command\":[\"a\",\"\"]}}]}",
                 "/actions/0/run/command/1", "R2.8",
                 R25_2_MUST_BE("/actions/0/run/command/1", "a string of 1 to 255 bytes"),
                 R25_2_USE("a string of 1 to 255 bytes"));
    expect_error(HEAD "\"actions\":[{\"name\":\"go\",\"events\":[\"memory.created\"],"
                      "\"run\":{\"command\":[7]}}]}",
                 "/actions/0/run/command/0", "R2.8",
                 R25_2_MUST_BE("/actions/0/run/command/0", "a string of 1 to 255 bytes"),
                 R25_2_USE("a string of 1 to 255 bytes"));
}

TEST(a_command_argument_of_255_bytes_fits_and_256_does_not)
{
    static const char fmt[] = HEAD "\"actions\":[{\"name\":\"go\",\"events\":[\"memory.created\"],"
                                   "\"run\":{\"command\":[\"/%s\"]}}]}";
    const char *fit = given_run_of('a', 254);
    const char *over = given_run_of('a', 255);
    char doc[512];
    PluginManifest *man = NULL;

    (void)snprintf(doc, sizeof(doc), fmt, fit);
    man = given_parse(doc, PLUGIN_PARSE_OK);
    ASSERT_EQ_INT(man->entries[0].run.argv[0][254], 'a');
    ASSERT_EQ_INT(man->entries[0].run.argv[0][255], 0);
    plugin_manifest_free(man);
    (void)snprintf(doc, sizeof(doc), fmt, over);
    man = given_parse(doc, PLUGIN_PARSE_INVALID);
    ASSERT_STREQ(error_at(man, 0)->path, "/actions/0/run/command/0");
    plugin_manifest_free(man);
}

TEST(a_relative_command_with_a_dotdot_segment_is_refused)
{
    static const char fmt[] = HEAD "\"actions\":[{\"name\":\"go\",\"events\":[\"memory.created\"],"
                                   "\"run\":{\"command\":[\"%s\"]}}]}";

    expect_error_in(
        fmt, "../../bin/evil", "/actions/0/run/command/0", "R2.8",
        "plugin.json /actions/0/run/command/0: a relative command can't contain \"..\".",
        "Use a path inside the plugin folder, or an absolute path.");
    expect_error_in(
        fmt, "bin/../../evil", "/actions/0/run/command/0", "R2.8",
        "plugin.json /actions/0/run/command/0: a relative command can't contain \"..\".",
        "Use a path inside the plugin folder, or an absolute path.");
    expect_error_in(
        fmt, "..", "/actions/0/run/command/0", "R2.8",
        "plugin.json /actions/0/run/command/0: a relative command can't contain \"..\".",
        "Use a path inside the plugin folder, or an absolute path.");
    expect_error_in(
        fmt, "bin/..", "/actions/0/run/command/0", "R2.8",
        "plugin.json /actions/0/run/command/0: a relative command can't contain \"..\".",
        "Use a path inside the plugin folder, or an absolute path.");
}

static void expect_command_ok(const char *command)
{
    static const char fmt[] = HEAD "\"actions\":[{\"name\":\"go\",\"events\":[\"memory.created\"],"
                                   "\"run\":{\"command\":[\"%s\"]}}]}";
    char doc[512];

    (void)snprintf(doc, sizeof(doc), fmt, command);
    expect_ok(doc);
}

TEST(an_absolute_command_and_dotted_names_that_are_not_dotdot_are_allowed)
{
    expect_command_ok("/usr/../bin/echo");
    expect_command_ok("run..sh");
    expect_command_ok("...");
    expect_command_ok("./run.sh");
    expect_command_ok("..a");
    expect_command_ok("a..");
}

TEST(only_the_program_path_is_checked_for_dotdot_not_its_arguments)
{
    expect_ok(HEAD "\"actions\":[{\"name\":\"go\",\"events\":[\"memory.created\"],"
                   "\"run\":{\"command\":[\"run.sh\",\"../data\"]}}]}");
}

TEST(events_are_a_short_list_of_known_event_types)
{
    static const char want[] =
        "a list of 1 to 8 of \"memory.created\", \"memory.updated\", \"memory.deleted\", "
        "\"memory.restored\", \"memory.purged\", \"memory.expired\", or \"memory.*\"";
    char message[600];
    char fix[600];

    (void)snprintf(message, sizeof(message), "plugin.json /actions/0/events: must be %s.", want);
    (void)snprintf(fix, sizeof(fix), "Use %s.", want);
    expect_error(HEAD "\"actions\":[{\"name\":\"go\",\"events\":[]," ACTION_RUN "}]}",
                 "/actions/0/events", "R2.21", message, fix);
    expect_error(HEAD "\"actions\":[{\"name\":\"go\",\"events\":\"memory.created\"," ACTION_RUN
                      "}]}",
                 "/actions/0/events", "R2.21", message, fix);
}

TEST(an_unknown_event_type_is_named_with_its_index)
{
    expect_error(HEAD
                 "\"actions\":[{\"name\":\"go\",\"events\":[\"memory.created\",\"no\"]," ACTION_RUN
                 "}]}",
                 "/actions/0/events/1", "R2.21",
                 "plugin.json /actions/0/events/1: must be \"memory.created\", \"memory.updated\", "
                 "\"memory.deleted\", \"memory.restored\", \"memory.purged\", "
                 "\"memory.expired\", or \"memory.*\".",
                 "Use \"memory.created\", \"memory.updated\", \"memory.deleted\", "
                 "\"memory.restored\", \"memory.purged\", \"memory.expired\", or \"memory.*\".");
}

TEST(memory_star_must_be_the_only_event)
{
    expect_error(
        HEAD
        "\"actions\":[{\"name\":\"go\",\"events\":[\"memory.*\",\"memory.created\"]," ACTION_RUN
        "}]}",
        "/actions/0/events", "R2.21",
        "plugin.json /actions/0/events: \"memory.*\" can't be listed with other events.",
        "List \"memory.*\" alone.");
    {
        PluginManifest *man = given_parse(
            HEAD "\"actions\":[{\"name\":\"go\",\"events\":[\"memory.*\"]," ACTION_RUN "}]}",
            PLUGIN_PARSE_OK);

        ASSERT_TRUE(man->entries[0].all_events);
        plugin_manifest_free(man);
    }
}

/* Lists, filters and permissions ----------------------------------------- */

TEST(an_action_filter_block_keeps_every_list)
{
    PluginManifest *man = given_parse(HEAD "\"actions\":[" ACTION_BODY
                                           ",\"filter\":{\"tags\":{\"match_before\":false}}}]}",
                                      PLUGIN_PARSE_OK);

    ASSERT_TRUE(man->entries[0].has_match_before);
    ASSERT_FALSE(man->entries[0].match_before);
    plugin_manifest_free(man);
}

TEST(a_filter_block_that_is_not_an_object_must_be_an_object)
{
    expect_error(HEAD "\"actions\":[" ACTION_BODY ",\"filter\":[]}]}", "/actions/0/filter", "R2.21",
                 R25_2_MUST_BE("/actions/0/filter", "an object"), R25_2_USE("an object"));
}

TEST(tags_that_are_not_an_object_must_be_an_object)
{
    expect_error(HEAD "\"actions\":[" ACTION_BODY ",\"filter\":{\"tags\":3}}]}",
                 "/actions/0/filter/tags", "R2.21",
                 R25_2_MUST_BE("/actions/0/filter/tags", "an object"), R25_2_USE("an object"));
}

TEST(match_before_must_be_true_or_false)
{
    expect_error(HEAD "\"actions\":[" ACTION_BODY ",\"filter\":{\"tags\":{\"match_before\":1}}}]}",
                 "/actions/0/filter/tags/match_before", "R2.21",
                 R25_2_MUST_BE("/actions/0/filter/tags/match_before", "true or false"),
                 R25_2_USE("true or false"));
}

TEST(a_tag_list_holds_at_most_eight_strings_of_63_bytes)
{
    expect_error(HEAD "\"actions\":[" ACTION_BODY
                      ",\"filter\":{\"tags\":{\"any\":[\"1\",\"2\",\"3\",\"4\",\"5\",\"6\",\"7\","
                      "\"8\",\"9\"]}}}]}",
                 "/actions/0/filter/tags/any", "R2.21",
                 R25_2_MUST_BE("/actions/0/filter/tags/any", "a list of at most 8 strings"),
                 R25_2_USE("a list of at most 8 strings"));
    expect_error(HEAD "\"actions\":[" ACTION_BODY ",\"filter\":{\"tags\":{\"all\":[3]}}}]}",
                 "/actions/0/filter/tags/all/0", "R2.21",
                 R25_2_MUST_BE("/actions/0/filter/tags/all/0", "a string of at most 63 bytes"),
                 R25_2_USE("a string of at most 63 bytes"));
}

TEST(actor_kinds_are_the_five_the_schema_lists)
{
    static const char want[] = "\"human\", \"agent\", \"tool\", \"share\", or \"import\"";
    char message[300];
    char fix[300];

    (void)snprintf(message, sizeof(message),
                   "plugin.json /actions/0/filter/created_by/0: must be %s.", want);
    (void)snprintf(fix, sizeof(fix), "Use %s.", want);
    expect_error(HEAD "\"actions\":[" ACTION_BODY ",\"filter\":{\"created_by\":[\"sync\"]}}]}",
                 "/actions/0/filter/created_by/0", "R2.21", message, fix);
    (void)snprintf(message, sizeof(message),
                   "plugin.json /actions/0/filter/updated_by: must be a list of at most 5 of %s.",
                   want);
    (void)snprintf(fix, sizeof(fix), "Use a list of at most 5 of %s.", want);
    expect_error(HEAD "\"actions\":[" ACTION_BODY ",\"filter\":{\"updated_by\":\"human\"}}]}",
                 "/actions/0/filter/updated_by", "R2.21", message, fix);
}

TEST(bins_origins_and_not_caused_by_are_checked)
{
    expect_error(HEAD "\"actions\":[" ACTION_BODY ",\"filter\":{\"bins\":[\"trash\"]}}]}",
                 "/actions/0/filter/bins/0", "R2.21",
                 R25_2_MUST_BE("/actions/0/filter/bins/0",
                               "\"live\", \"expired\", \"deleted\", or \"purged\""),
                 R25_2_USE("\"live\", \"expired\", \"deleted\", or \"purged\""));
    expect_error(HEAD "\"actions\":[" ACTION_BODY ",\"filter\":{\"origins\":[\"cloud\"]}}]}",
                 "/actions/0/filter/origins/0", "R2.21",
                 R25_2_MUST_BE("/actions/0/filter/origins/0", "\"local\", \"sync\", or \"import\""),
                 R25_2_USE("\"local\", \"sync\", or \"import\""));
    expect_error(HEAD "\"actions\":[" ACTION_BODY ",\"filter\":{\"not_caused_by\":[1]}}]}",
                 "/actions/0/filter/not_caused_by/0", "R2.21",
                 R25_2_MUST_BE("/actions/0/filter/not_caused_by/0", "a string of at most 79 bytes"),
                 R25_2_USE("a string of at most 79 bytes"));
}

TEST(all_four_bins_fit_in_the_bins_list)
{
    PluginManifest *man =
        given_parse(HEAD "\"actions\":[" ACTION_BODY
                         ",\"filter\":{\"bins\":[\"live\",\"expired\",\"deleted\",\"purged\"]}}]}",
                    PLUGIN_PARSE_OK);

    ASSERT_EQ_INT(man->entries[0].n_bins, 4);
    plugin_manifest_free(man);
}

TEST(needs_accept_memory_fields_and_remember_setting_keys)
{
    PluginManifest *man =
        given_parse(HEAD "\"filters\":[" FILTER_BODY ",\"permissions\":{\"needs\":[\"links\","
                         "\"remember.search.max_results\",\"remember.a.b.c\"]}}]}",
                    PLUGIN_PARSE_OK);

    ASSERT_EQ_INT(man->entries[0].nneeds, 3);
    plugin_manifest_free(man);
}

static void expect_need_refused(const char *need)
{
    static const char want[] =
        "a memory field (\"key\", \"body\", \"tags\", \"bin\", \"created_at\", \"updated_at\", "
        "\"expires_at\", \"deleted_at\", \"change_seq\", \"created_by\", \"updated_by\", or "
        "\"links\"), or a \"remember.\" setting key";
    char doc[512];
    char message[800];
    char fix[800];

    (void)snprintf(doc, sizeof(doc),
                   HEAD "\"filters\":[" FILTER_BODY ",\"permissions\":{\"needs\":[\"%s\"]}}]}",
                   need);
    (void)snprintf(message, sizeof(message),
                   "plugin.json /filters/0/permissions/needs/0: must be %s.", want);
    (void)snprintf(fix, sizeof(fix), "Use %s.", want);
    expect_error(doc, "/filters/0/permissions/needs/0", "R2.16", message, fix);
}

TEST(a_need_that_is_no_field_and_no_global_setting_is_refused)
{
    expect_need_refused("id");
    expect_need_refused("version_vector");
    expect_need_refused("source");
    expect_need_refused("fortkeeps.x.y");
    expect_need_refused("plugin.x.y");
    expect_need_refused("remember.x");
    expect_need_refused("remember.a.b.c.d");
    expect_need_refused("remember.A.b");
    expect_need_refused("remember..b");
    expect_need_refused("remember.a.b.");
}

TEST(a_global_setting_key_of_96_characters_fits_and_97_does_not)
{
    char fit[100];
    char over[100];
    const char *seg = given_run_of('a', 40);

    (void)snprintf(fit, sizeof(fit), "remember.%s.%s.%.5s", seg, seg, seg);
    (void)snprintf(over, sizeof(over), "remember.%s.%s.%.6s", seg, seg, seg);
    ASSERT_EQ_INT(strlen(fit), 96);
    ASSERT_EQ_INT(strlen(over), 97);
    {
        char doc[512];

        (void)snprintf(doc, sizeof(doc),
                       HEAD "\"filters\":[" FILTER_BODY ",\"permissions\":{\"needs\":[\"%s\"]}}]}",
                       fit);
        expect_ok(doc);
    }
    expect_need_refused(over);
}

TEST(a_need_listed_twice_is_refused)
{
    expect_error(HEAD "\"filters\":[" FILTER_BODY
                      ",\"permissions\":{\"needs\":[\"body\",\"body\"]}}]}",
                 "/filters/0/permissions/needs/1", "R2.16",
                 R25_2_TWICE(" /filters/0/permissions/needs", "body"), R25_2_KEEP("body"));
}

TEST(a_needs_list_has_at_most_sixteen_items)
{
    char doc[1024];
    size_t i = 0;

    (void)snprintf(doc, sizeof(doc), "%s",
                   HEAD "\"filters\":[" FILTER_BODY ",\"permissions\":{\"needs\":[");
    for (i = 0; i < 17U; i++) {
        appendf(doc, sizeof(doc), "%s\"remember.a.k%zu\"", i == 0U ? "" : ",", i);
    }
    append(doc, sizeof(doc), "]}}]}");
    expect_error(doc, "/filters/0/permissions/needs", "R2.16",
                 R25_2_MUST_BE("/filters/0/permissions/needs", "a list of at most 16 items"),
                 R25_2_USE("a list of at most 16 items"));
}

TEST(needs_that_is_not_a_list_and_permissions_that_is_not_an_object_are_refused)
{
    expect_error(HEAD "\"filters\":[" FILTER_BODY ",\"permissions\":{\"needs\":\"body\"}}]}",
                 "/filters/0/permissions/needs", "R2.16",
                 R25_2_MUST_BE("/filters/0/permissions/needs", "a list of at most 16 items"),
                 R25_2_USE("a list of at most 16 items"));
    expect_error(HEAD "\"filters\":[" FILTER_BODY ",\"permissions\":[]}]}",
                 "/filters/0/permissions", "R2.5",
                 R25_2_MUST_BE("/filters/0/permissions", "an object"), R25_2_USE("an object"));
}

/* Persistent entries and backfill ---------------------------------------- */

TEST(two_persistent_entries_with_one_command_parse)
{
    PluginManifest *man = given_parse(
        HEAD "\"actions\":[{\"name\":\"go\",\"events\":[\"memory.created\"],"
             "\"run\":{\"process\":\"persistent\",\"command\":[\"hello.py\"]}}]," FILTERS "}",
        PLUGIN_PARSE_OK);

    ASSERT_EQ_INT(man->nentries, 2);
    plugin_manifest_free(man);
}

TEST(a_second_persistent_command_points_at_its_own_entry)
{
    expect_error(HEAD "\"actions\":[{\"name\":\"go\",\"events\":[\"memory.created\"],"
                      "\"run\":{\"process\":\"persistent\",\"command\":[\"other.py\"]}}]," FILTERS
                      "}",
                 "/filters/0/run/command", "R2.10",
                 "plugin.json /filters/0/run/command: this entry runs a different command than "
                 "/actions/0. All persistent entries share one process.",
                 "Give every persistent entry the same command.");
}

TEST(a_second_persistent_action_with_another_command_points_at_that_action)
{
    expect_error(HEAD "\"actions\":[{\"name\":\"one\",\"events\":[\"memory.created\"],"
                      "\"run\":{\"process\":\"persistent\",\"command\":[\"a\"]}},"
                      "{\"name\":\"two\",\"events\":[\"memory.created\"],"
                      "\"run\":{\"process\":\"persistent\",\"command\":[\"a\",\"b\"]}}]}",
                 "/actions/1/run/command", "R2.10",
                 "plugin.json /actions/1/run/command: this entry runs a different command than "
                 "/actions/0. All persistent entries share one process.",
                 "Give every persistent entry the same command.");
}

TEST(persistent_commands_that_differ_in_one_argument_differ)
{
    expect_error(HEAD "\"actions\":[{\"name\":\"one\",\"events\":[\"memory.created\"],"
                      "\"run\":{\"process\":\"persistent\",\"command\":[\"a\",\"x\"]}},"
                      "{\"name\":\"two\",\"events\":[\"memory.created\"],"
                      "\"run\":{\"process\":\"persistent\",\"command\":[\"a\",\"y\"]}}]}",
                 "/actions/1/run/command", "R2.10",
                 "plugin.json /actions/1/run/command: this entry runs a different command than "
                 "/actions/0. All persistent entries share one process.",
                 "Give every persistent entry the same command.");
}

TEST(backfill_needs_a_persistent_entry)
{
    expect_error(HEAD ACTIONS ",\"backfill\":{\"enabled\":true}}", "/backfill/enabled", "R2.17",
                 "plugin.json /backfill/enabled: backfill is on, and no entry is persistent.",
                 "Set \"process\" to \"persistent\" in an action's \"run\", or set \"enabled\" to "
                 "false.");
    expect_ok(HEAD ACTIONS ",\"backfill\":{\"enabled\":false}}");
    expect_ok(HEAD FILTERS ",\"backfill\":{\"enabled\":true}}");
}

TEST(backfill_limits_are_100_entries_and_4mib)
{
    expect_error(HEAD FILTERS ",\"backfill\":{\"enabled\":true,\"max_batch_entries\":101}}",
                 "/backfill/max_batch_entries", "R2.18",
                 R25_2_MUST_BE("/backfill/max_batch_entries", "a whole number from 1 to 100"),
                 R25_2_USE("a whole number from 1 to 100"));
    expect_error(HEAD FILTERS ",\"backfill\":{\"enabled\":true,\"max_batch_entries\":0}}",
                 "/backfill/max_batch_entries", "R2.18",
                 R25_2_MUST_BE("/backfill/max_batch_entries", "a whole number from 1 to 100"),
                 R25_2_USE("a whole number from 1 to 100"));
    expect_error(HEAD FILTERS ",\"backfill\":{\"enabled\":true,\"max_batch_size\":\"4097KiB\"}}",
                 "/backfill/max_batch_size", "R2.18",
                 R25_2_MUST_BE("/backfill/max_batch_size", "a size of at most 4MiB"),
                 R25_2_USE("a size of at most 4MiB"));
    {
        PluginManifest *man =
            given_parse(HEAD FILTERS ",\"backfill\":{\"enabled\":true,\"max_batch_entries\":100,"
                                     "\"max_batch_size\":\"4096KiB\"}}",
                        PLUGIN_PARSE_OK);

        ASSERT_EQ_INT(man->max_batch_entries, 100);
        ASSERT_EQ_INT(man->max_batch_bytes, 4194304);
        plugin_manifest_free(man);
    }
}

TEST(backfill_enabled_is_required_and_a_boolean)
{
    expect_error(HEAD FILTERS ",\"backfill\":{}}", "/backfill", "R2.17",
                 R25_2_MISSING(" /backfill", "enabled"), R25_2_ADD("enabled"));
    expect_error(HEAD FILTERS ",\"backfill\":{\"enabled\":\"yes\"}}", "/backfill/enabled", "R2.17",
                 R25_2_MUST_BE("/backfill/enabled", "true or false"), R25_2_USE("true or false"));
    expect_error(HEAD FILTERS ",\"backfill\":3}", "/backfill", "R2.17",
                 R25_2_MUST_BE("/backfill", "an object"), R25_2_USE("an object"));
}

/* Entries ---------------------------------------------------------------- */

TEST(actions_and_filters_are_lists_of_one_to_sixteen_entries)
{
    char doc[4096];
    size_t i = 0;

    expect_error(HEAD "\"actions\":[]}", "/actions", "R2.5",
                 R25_2_MUST_BE("/actions", "a list of 1 to 16 entries"),
                 R25_2_USE("a list of 1 to 16 entries"));
    expect_error(HEAD "\"filters\":{}}", "/filters", "R2.5",
                 R25_2_MUST_BE("/filters", "a list of 1 to 16 entries"),
                 R25_2_USE("a list of 1 to 16 entries"));
    (void)snprintf(doc, sizeof(doc), "%s", HEAD "\"actions\":[");
    for (i = 0; i < 17U; i++) {
        appendf(doc, sizeof(doc), "%s" ACTION_BODY "}", i == 0U ? "" : ",");
    }
    append(doc, sizeof(doc), "]}");
    expect_error(doc, "/actions", "R2.5", R25_2_MUST_BE("/actions", "a list of 1 to 16 entries"),
                 R25_2_USE("a list of 1 to 16 entries"));
}

TEST(sixteen_actions_and_sixteen_filters_parse_together)
{
    char doc[8192];
    size_t i = 0;
    PluginManifest *man = NULL;

    (void)snprintf(doc, sizeof(doc), "%s", HEAD "\"actions\":[");
    for (i = 0; i < 16U; i++) {
        appendf(doc, sizeof(doc),
                "%s{\"name\":\"a%zu\",\"events\":[\"memory.created\"]," ACTION_RUN "}",
                i == 0U ? "" : ",", i);
    }
    appendf(doc, sizeof(doc), "],\"filters\":[");
    for (i = 0; i < 16U; i++) {
        appendf(doc, sizeof(doc),
                "%s{\"name\":\"f%zu\",\"point\":\"search.results\"," FILTER_RUN "}",
                i == 0U ? "" : ",", i);
    }
    append(doc, sizeof(doc), "]}");
    man = given_parse(doc, PLUGIN_PARSE_OK);
    ASSERT_EQ_INT(man->nentries, 32);
    ASSERT_TRUE(man->entries[31].is_filter);
    plugin_manifest_free(man);
}

TEST(an_entry_that_is_not_an_object_must_be_an_object)
{
    expect_error(HEAD "\"actions\":[3]}", "/actions/0", "R2.5",
                 R25_2_MUST_BE("/actions/0", "an object"), R25_2_USE("an object"));
    expect_error(HEAD "\"filters\":[3]}", "/filters/0", "R2.5",
                 R25_2_MUST_BE("/filters/0", "an object"), R25_2_USE("an object"));
}

TEST(an_entry_with_no_name_has_a_missing_name)
{
    expect_error(HEAD "\"actions\":[{\"events\":[\"memory.created\"]," ACTION_RUN "}]}",
                 "/actions/0", "R2.22", R25_2_MISSING(" /actions/0", "name"), R25_2_ADD("name"));
    expect_error(HEAD "\"actions\":[{\"name\":\"go\"," ACTION_RUN "}]}", "/actions/0", "R2.21",
                 R25_2_MISSING(" /actions/0", "events"), R25_2_ADD("events"));
}

/* Plain fields ------------------------------------------------------------ */

TEST(a_description_counts_characters_so_150_hebrew_letters_fit)
{
    char text[400] = "";
    char doc[800];
    size_t i = 0;
    PluginManifest *man = NULL;

    for (i = 0; i < 150U; i++) {
        append(text, sizeof(text), "\xd7\x90");
    }
    (void)snprintf(doc, sizeof(doc), HEAD ACTIONS ",\"description\":\"%s\"}", text);
    man = given_parse(doc, PLUGIN_PARSE_OK);
    ASSERT_EQ_INT(strlen(man->description), 300);
    plugin_manifest_free(man);
}

TEST(a_description_of_200_characters_fits_and_201_does_not)
{
    const char *fit = given_run_of('d', 200);
    const char *over = given_run_of('d', 201);
    char doc[800];
    PluginManifest *man = NULL;

    (void)snprintf(doc, sizeof(doc), HEAD ACTIONS ",\"description\":\"%s\"}", fit);
    expect_ok(doc);
    (void)snprintf(doc, sizeof(doc), HEAD ACTIONS ",\"description\":\"%s\"}", over);
    man = given_parse(doc, PLUGIN_PARSE_INVALID);
    ASSERT_STREQ(error_at(man, 0)->message,
                 R25_2_MUST_BE("/description", "a string of at most 200 characters"));
    ASSERT_FALSE(man->has_description);
    plugin_manifest_free(man);
}

TEST(a_description_of_800_bytes_in_one_hundred_characters_beyond_the_buffer_is_refused)
{
    const char *big = given_run_of('d', 900);
    char doc[1200];
    PluginManifest *man = NULL;

    (void)snprintf(doc, sizeof(doc), HEAD ACTIONS ",\"description\":\"%s\"}", big);
    man = given_parse(doc, PLUGIN_PARSE_INVALID);
    ASSERT_STREQ(error_at(man, 0)->path, "/description");
    plugin_manifest_free(man);
}

TEST(plugin_version_is_a_semver)
{
    static const char fmt[] =
        "{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"wake\",\"plugin_version\":\"%s\","
        "\"protocol\":1," ACTIONS "}";
    char doc[512];
    PluginManifest *man = NULL;

    expect_error_in(fmt, "1.0", "/plugin_version", "R2.2",
                    R25_2_MUST_BE("/plugin_version", "a version like \"1.0.0\""),
                    R25_2_USE("a version like \"1.0.0\""));
    expect_error_in(fmt, "1.0.0.0", "/plugin_version", "R2.2",
                    R25_2_MUST_BE("/plugin_version", "a version like \"1.0.0\""),
                    R25_2_USE("a version like \"1.0.0\""));
    expect_error_in(fmt, ".1.0", "/plugin_version", "R2.2",
                    R25_2_MUST_BE("/plugin_version", "a version like \"1.0.0\""),
                    R25_2_USE("a version like \"1.0.0\""));
    expect_error_in(fmt, "1..0", "/plugin_version", "R2.2",
                    R25_2_MUST_BE("/plugin_version", "a version like \"1.0.0\""),
                    R25_2_USE("a version like \"1.0.0\""));
    expect_error_in(fmt, "1.0.0-", "/plugin_version", "R2.2",
                    R25_2_MUST_BE("/plugin_version", "a version like \"1.0.0\""),
                    R25_2_USE("a version like \"1.0.0\""));
    expect_error_in(fmt, "1.0.0-a!b", "/plugin_version", "R2.2",
                    R25_2_MUST_BE("/plugin_version", "a version like \"1.0.0\""),
                    R25_2_USE("a version like \"1.0.0\""));
    expect_error_in(fmt, "1.0.0x", "/plugin_version", "R2.2",
                    R25_2_MUST_BE("/plugin_version", "a version like \"1.0.0\""),
                    R25_2_USE("a version like \"1.0.0\""));
    expect_error_in(fmt, "v1.0.0", "/plugin_version", "R2.2",
                    R25_2_MUST_BE("/plugin_version", "a version like \"1.0.0\""),
                    R25_2_USE("a version like \"1.0.0\""));
    (void)snprintf(doc, sizeof(doc), fmt, "10.20.30-rc.1.build-5");
    man = given_parse(doc, PLUGIN_PARSE_OK);
    ASSERT_STREQ(man->plugin_version, "10.20.30-rc.1.build-5");
    plugin_manifest_free(man);
}

TEST(data_version_and_idle_timeout_defaults_and_limits)
{
    PluginManifest *man =
        given_parse(HEAD ACTIONS ",\"data_version\":2147483647}", PLUGIN_PARSE_OK);

    ASSERT_EQ_INT(man->data_version, 2147483647);
    ASSERT_FALSE(man->has_idle_timeout);
    plugin_manifest_free(man);
    expect_error(HEAD ACTIONS ",\"data_version\":1.5}", "/data_version", "R2.14",
                 R25_2_MUST_BE("/data_version", "a whole number of 1 or more"),
                 R25_2_USE("a whole number of 1 or more"));
    expect_error(HEAD ACTIONS ",\"idle_timeout\":\"10 min\"}", "/idle_timeout", "R2.20",
                 R25_2_MUST_BE("/idle_timeout", "a duration like \"10m\""),
                 R25_2_USE("a duration like \"10m\""));
}

TEST(a_duration_that_the_schema_refuses_is_refused_here_too)
{
    expect_error(HEAD ACTIONS ",\"idle_timeout\":\"1.5s\"}", "/idle_timeout", "R2.20",
                 R25_2_MUST_BE("/idle_timeout", "a duration like \"10m\""),
                 R25_2_USE("a duration like \"10m\""));
    expect_error(HEAD ACTIONS ",\"idle_timeout\":\"s\"}", "/idle_timeout", "R2.20",
                 R25_2_MUST_BE("/idle_timeout", "a duration like \"10m\""),
                 R25_2_USE("a duration like \"10m\""));
    expect_error(HEAD ACTIONS ",\"idle_timeout\":\"5d\"}", "/idle_timeout", "R2.20",
                 R25_2_MUST_BE("/idle_timeout", "a duration like \"10m\""),
                 R25_2_USE("a duration like \"10m\""));
}

TEST(a_size_the_schema_refuses_is_refused_here_too)
{
    expect_error(HEAD FILTERS ",\"backfill\":{\"enabled\":true,\"max_batch_size\":\"1.5MiB\"}}",
                 "/backfill/max_batch_size", "R2.18",
                 R25_2_MUST_BE("/backfill/max_batch_size", "a size of at most 4MiB"),
                 R25_2_USE("a size of at most 4MiB"));
    expect_error(HEAD FILTERS ",\"backfill\":{\"enabled\":true,\"max_batch_size\":\"1GiB\"}}",
                 "/backfill/max_batch_size", "R2.18",
                 R25_2_MUST_BE("/backfill/max_batch_size", "a size of at most 4MiB"),
                 R25_2_USE("a size of at most 4MiB"));
}

/* Settings --------------------------------------------------------------- */

static PluginManifest *given_setting(const char *doc)
{
    PluginManifest *man = given_parse(doc, PLUGIN_PARSE_OK);

    ASSERT_EQ_INT(man->nsettings, 1);
    return man;
}

TEST(every_setting_type_parses_with_its_default)
{
    PluginManifest *man = given_setting(SETTING_DOC("\"type\":\"boolean\",\"default\":true"));

    ASSERT_EQ_INT(man->settings[0].type, PLUGIN_SETTING_BOOLEAN);
    ASSERT_STREQ(man->settings[0].default_text, "true");
    ASSERT_TRUE(man->settings[0].has_default);
    ASSERT_TRUE(man->settings[0].allow_user);
    ASSERT_FALSE(man->settings[0].allow_brain);
    ASSERT_STREQ(man->settings[0].key, "mode");
    ASSERT_STREQ(man->settings[0].description, "A mode.");
    plugin_manifest_free(man);
    man = given_setting(SETTING_DOC("\"type\":\"boolean\",\"default\":false"));
    ASSERT_STREQ(man->settings[0].default_text, "false");
    plugin_manifest_free(man);
    man = given_setting(SETTING_DOC("\"type\":\"integer\",\"default\":3"));
    ASSERT_EQ_INT(man->settings[0].type, PLUGIN_SETTING_INTEGER);
    ASSERT_STREQ(man->settings[0].default_text, "3");
    plugin_manifest_free(man);
    man = given_setting(SETTING_DOC("\"type\":\"duration\",\"default\":\"90s\""));
    ASSERT_EQ_INT(man->settings[0].type, PLUGIN_SETTING_DURATION);
    ASSERT_STREQ(man->settings[0].default_text, "90s");
    plugin_manifest_free(man);
    man = given_setting(SETTING_DOC("\"type\":\"size\",\"default\":\"2KiB\""));
    ASSERT_EQ_INT(man->settings[0].type, PLUGIN_SETTING_SIZE);
    ASSERT_STREQ(man->settings[0].default_text, "2KiB");
    plugin_manifest_free(man);
    man = given_setting(SETTING_DOC("\"type\":\"string\",\"default\":\"hi\""));
    ASSERT_EQ_INT(man->settings[0].type, PLUGIN_SETTING_STRING);
    ASSERT_STREQ(man->settings[0].default_text, "hi");
    plugin_manifest_free(man);
    man = given_setting(SETTING_DOC("\"type\":\"string\",\"default\":\"\""));
    ASSERT_TRUE(man->settings[0].has_default);
    ASSERT_STREQ(man->settings[0].default_text, "");
    plugin_manifest_free(man);
}

TEST(a_string_list_default_keeps_its_strings_as_source_json)
{
    PluginManifest *man = given_setting(SETTING_DOC(
        "\"type\":\"string_list\",\"default\":[ \"a\", \"b\" ],\"range\":{\"max_items\":5}"));

    ASSERT_EQ_INT(man->settings[0].type, PLUGIN_SETTING_STRING_LIST);
    ASSERT_STREQ(man->settings[0].default_text, "[ \"a\", \"b\" ]");
    ASSERT_EQ_INT(man->settings[0].max_items, 5);
    plugin_manifest_free(man);
    man = given_setting(SETTING_DOC("\"type\":\"string_list\",\"default\":[]"));
    ASSERT_STREQ(man->settings[0].default_text, "[]");
    ASSERT_EQ_INT(man->settings[0].max_items, 0);
    plugin_manifest_free(man);
}

TEST(a_string_list_default_must_be_a_list_of_strings)
{
    expect_error(
        SETTING_DOC("\"type\":\"string_list\",\"default\":[1]"), "/settings/0/default", "R2.11",
        R25_2_MUST_BE("/settings/0/default", "a list of strings of at most 511 bytes as written"),
        R25_2_USE("a list of strings of at most 511 bytes as written"));
    expect_error(
        SETTING_DOC("\"type\":\"string_list\",\"default\":\"a\""), "/settings/0/default", "R2.11",
        R25_2_MUST_BE("/settings/0/default", "a list of strings of at most 511 bytes as written"),
        R25_2_USE("a list of strings of at most 511 bytes as written"));
}

TEST(a_string_default_of_511_bytes_fits_and_512_does_not)
{
    const char *fit = given_run_of('s', 511);
    const char *over = given_run_of('s', 512);
    char doc[1400];
    PluginManifest *man = NULL;

    (void)snprintf(doc, sizeof(doc), SETTING_DOC("\"type\":\"string\",\"default\":\"%s\""), fit);
    man = given_setting(doc);
    ASSERT_EQ_INT(strlen(man->settings[0].default_text), 511);
    plugin_manifest_free(man);
    (void)snprintf(doc, sizeof(doc), SETTING_DOC("\"type\":\"string\",\"default\":\"%s\""), over);
    man = given_parse(doc, PLUGIN_PARSE_INVALID);
    ASSERT_STREQ(error_at(man, 0)->message,
                 R25_2_MUST_BE("/settings/0/default", "a string of at most 511 bytes"));
    plugin_manifest_free(man);
}

TEST(a_string_list_default_of_512_source_bytes_is_refused)
{
    const char *pad = given_run_of('a', 500);
    char doc[1400];

    (void)snprintf(
        doc, sizeof(doc),
        SETTING_DOC("\"type\":\"string_list\",\"default\":[\"%s\",\"bbbbbbbbbbbbbbbbbbbbb\"]"),
        pad);
    expect_error(
        doc, "/settings/0/default", "R2.11",
        R25_2_MUST_BE("/settings/0/default", "a list of strings of at most 511 bytes as written"),
        R25_2_USE("a list of strings of at most 511 bytes as written"));
}

TEST(a_default_of_the_wrong_type_names_what_the_type_needs)
{
    expect_error(SETTING_DOC("\"type\":\"boolean\",\"default\":\"yes\""), "/settings/0/default",
                 "R2.11", R25_2_MUST_BE("/settings/0/default", "true or false"),
                 R25_2_USE("true or false"));
    expect_error(SETTING_DOC("\"type\":\"integer\",\"default\":\"3\""), "/settings/0/default",
                 "R2.11", R25_2_MUST_BE("/settings/0/default", "a whole number"),
                 R25_2_USE("a whole number"));
    expect_error(SETTING_DOC("\"type\":\"integer\",\"default\":-3"), "/settings/0/default", "R2.11",
                 R25_2_MUST_BE("/settings/0/default", "a whole number"),
                 R25_2_USE("a whole number"));
    expect_error(SETTING_DOC("\"type\":\"duration\",\"default\":5"), "/settings/0/default", "R2.11",
                 R25_2_MUST_BE("/settings/0/default", "a duration like \"10m\""),
                 R25_2_USE("a duration like \"10m\""));
    expect_error(SETTING_DOC("\"type\":\"size\",\"default\":\"5 KB\""), "/settings/0/default",
                 "R2.11", R25_2_MUST_BE("/settings/0/default", "a size like \"4MiB\""),
                 R25_2_USE("a size like \"4MiB\""));
    expect_error(SETTING_DOC("\"type\":\"string\",\"default\":1"), "/settings/0/default", "R2.11",
                 R25_2_MUST_BE("/settings/0/default", "a string of at most 511 bytes"),
                 R25_2_USE("a string of at most 511 bytes"));
}

TEST(a_setting_needs_key_description_type_and_default)
{
    PluginManifest *man = given_parse(HEAD ACTIONS ",\"settings\":[{}]}", PLUGIN_PARSE_INVALID);

    ASSERT_EQ_INT(man->nerrors, 4);
    ASSERT_STREQ(error_at(man, 0)->message, R25_2_MISSING(" /settings/0", "key"));
    ASSERT_STREQ(error_at(man, 1)->message, R25_2_MISSING(" /settings/0", "description"));
    ASSERT_STREQ(error_at(man, 2)->message, R25_2_MISSING(" /settings/0", "type"));
    ASSERT_STREQ(error_at(man, 3)->message, R25_2_MISSING(" /settings/0", "default"));
    plugin_manifest_free(man);
}

TEST(a_setting_type_outside_the_seven_is_refused)
{
    static const char want[] =
        "\"boolean\", \"integer\", \"enum\", \"duration\", \"size\", \"string\", or "
        "\"string_list\"";
    char message[300];
    char fix[300];

    (void)snprintf(message, sizeof(message), "plugin.json /settings/0/type: must be %s.", want);
    (void)snprintf(fix, sizeof(fix), "Use %s.", want);
    expect_error(SETTING_DOC("\"type\":\"float\",\"default\":1"), "/settings/0/type", "R2.11",
                 message, fix);
    expect_error(SETTING_DOC("\"type\":4,\"default\":1"), "/settings/0/type", "R2.11", message,
                 fix);
}

TEST(a_setting_that_is_not_an_object_and_a_settings_that_is_not_a_list_are_refused)
{
    expect_error(HEAD ACTIONS ",\"settings\":[1]}", "/settings/0", "R2.11",
                 R25_2_MUST_BE("/settings/0", "an object"), R25_2_USE("an object"));
    expect_error(HEAD ACTIONS ",\"settings\":{}}", "/settings", "R2.11",
                 R25_2_MUST_BE("/settings", "a list of at most 16 settings"),
                 R25_2_USE("a list of at most 16 settings"));
}

TEST(a_manifest_has_at_most_sixteen_settings)
{
    char doc[4096];
    size_t i = 0;

    (void)snprintf(doc, sizeof(doc), "%s", HEAD ACTIONS ",\"settings\":[");
    for (i = 0; i < 17U; i++) {
        appendf(doc, sizeof(doc),
                "%s{\"key\":\"k%zu\",\"description\":\"d\",\"type\":\"boolean\","
                "\"default\":true}",
                i == 0U ? "" : ",", i);
    }
    append(doc, sizeof(doc), "]}");
    expect_error(doc, "/settings", "R2.11",
                 R25_2_MUST_BE("/settings", "a list of at most 16 settings"),
                 R25_2_USE("a list of at most 16 settings"));
}

TEST(a_setting_description_is_a_string_of_at_most_200_characters)
{
    expect_error(HEAD ACTIONS
                 ",\"settings\":[{\"key\":\"a\",\"description\":5,\"type\":\"boolean\","
                 "\"default\":true}]}",
                 "/settings/0/description", "R2.5",
                 R25_2_MUST_BE("/settings/0/description", "a string of at most 200 characters"),
                 R25_2_USE("a string of at most 200 characters"));
}

TEST(a_setting_scope_defaults_to_user_and_reads_user_brain_and_both)
{
    PluginManifest *man =
        given_setting(SETTING_DOC("\"type\":\"boolean\",\"default\":true,\"scope\":\"brain\""));

    ASSERT_FALSE(man->settings[0].allow_user);
    ASSERT_TRUE(man->settings[0].allow_brain);
    plugin_manifest_free(man);
    man = given_setting(SETTING_DOC("\"type\":\"boolean\",\"default\":true,\"scope\":\"both\""));
    ASSERT_TRUE(man->settings[0].allow_user);
    ASSERT_TRUE(man->settings[0].allow_brain);
    plugin_manifest_free(man);
    man = given_setting(SETTING_DOC("\"type\":\"boolean\",\"default\":true,\"scope\":\"user\""));
    ASSERT_TRUE(man->settings[0].allow_user);
    ASSERT_FALSE(man->settings[0].allow_brain);
    plugin_manifest_free(man);
}

TEST(a_setting_scope_outside_the_three_is_refused)
{
    expect_error(SETTING_DOC("\"type\":\"boolean\",\"default\":true,\"scope\":\"team\""),
                 "/settings/0/scope", "R2.12",
                 R25_2_MUST_BE("/settings/0/scope", "\"user\", \"brain\", or \"both\""),
                 R25_2_USE("\"user\", \"brain\", or \"both\""));
    expect_error(SETTING_DOC("\"type\":\"boolean\",\"default\":true,\"scope\":1"),
                 "/settings/0/scope", "R2.12",
                 R25_2_MUST_BE("/settings/0/scope", "\"user\", \"brain\", or \"both\""),
                 R25_2_USE("\"user\", \"brain\", or \"both\""));
}

TEST(an_enum_needs_a_range_with_values_and_a_default_in_them)
{
    PluginManifest *man = given_setting(
        SETTING_DOC("\"type\":\"enum\",\"default\":\"b\",\"range\":{\"values\":[\"a\",\"b\"]}"));

    ASSERT_EQ_INT(man->settings[0].nvalues, 2);
    ASSERT_STREQ(man->settings[0].values[1], "b");
    plugin_manifest_free(man);
    expect_error(SETTING_DOC("\"type\":\"enum\",\"default\":\"a\""), "/settings/0", "R2.11",
                 R25_2_MISSING(" /settings/0", "range"), R25_2_ADD("range"));
    expect_error(SETTING_DOC("\"type\":\"enum\",\"default\":\"a\",\"range\":{}"),
                 "/settings/0/range", "R2.11", R25_2_MISSING(" /settings/0/range", "values"),
                 R25_2_ADD("values"));
    expect_error(
        SETTING_DOC("\"type\":\"enum\",\"default\":\"c\",\"range\":{\"values\":[\"a\",\"b\"]}"),
        "/settings/0/default", "R2.11",
        R25_2_MUST_BE("/settings/0/default", "one of the \"values\" in \"range\""),
        R25_2_USE("one of the \"values\" in \"range\""));
    expect_error(SETTING_DOC("\"type\":\"enum\",\"default\":\"\",\"range\":{\"values\":[\"a\"]}"),
                 "/settings/0/default", "R2.11",
                 R25_2_MUST_BE("/settings/0/default", "one of the \"values\" in \"range\""),
                 R25_2_USE("one of the \"values\" in \"range\""));
}

/* The schema and the approved phrase ("a list of at most 8 strings") allow an empty list. */
TEST(an_empty_enum_values_list_is_a_list_and_the_default_is_not_in_it)
{
    expect_error(SETTING_DOC("\"type\":\"enum\",\"default\":\"a\",\"range\":{\"values\":[]}"),
                 "/settings/0/default", "R2.11",
                 R25_2_MUST_BE("/settings/0/default", "one of the \"values\" in \"range\""),
                 R25_2_USE("one of the \"values\" in \"range\""));
}

TEST(enum_values_are_at_most_eight_strings_of_63_bytes)
{
    expect_error(
        SETTING_DOC("\"type\":\"enum\",\"default\":\"a\",\"range\":{\"values\":[\"a\",3]}"),
        "/settings/0/range/values/1", "R2.11",
        R25_2_MUST_BE("/settings/0/range/values/1", "a string of at most 63 bytes"),
        R25_2_USE("a string of at most 63 bytes"));
}

TEST(a_range_that_is_not_an_object_is_refused)
{
    expect_error(SETTING_DOC("\"type\":\"integer\",\"default\":1,\"range\":5"), "/settings/0/range",
                 "R2.11", R25_2_MUST_BE("/settings/0/range", "an object"), R25_2_USE("an object"));
}

TEST(an_integer_range_checks_its_default_and_its_order)
{
    PluginManifest *man = given_setting(
        SETTING_DOC("\"type\":\"integer\",\"default\":5,\"range\":{\"min\":1,\"max\":5}"));

    ASSERT_TRUE(man->settings[0].has_min && man->settings[0].has_max);
    ASSERT_EQ_INT(man->settings[0].min_value, 1);
    ASSERT_EQ_INT(man->settings[0].max_value, 5);
    plugin_manifest_free(man);
    expect_error(SETTING_DOC("\"type\":\"integer\",\"default\":6,\"range\":{\"min\":1,\"max\":5}"),
                 "/settings/0/default", "R2.11",
                 R25_2_MUST_BE("/settings/0/default", "a value inside \"range\""),
                 R25_2_USE("a value inside \"range\""));
    expect_error(SETTING_DOC("\"type\":\"integer\",\"default\":0,\"range\":{\"min\":1,\"max\":5}"),
                 "/settings/0/default", "R2.11",
                 R25_2_MUST_BE("/settings/0/default", "a value inside \"range\""),
                 R25_2_USE("a value inside \"range\""));
    expect_error(SETTING_DOC("\"type\":\"integer\",\"default\":3,\"range\":{\"min\":5,\"max\":1}"),
                 "/settings/0/range", "R2.11",
                 "plugin.json /settings/0/range: \"min\" is greater than \"max\".",
                 "Set \"min\" to \"max\" or lower.");
}

TEST(a_range_with_only_min_or_only_max_parses)
{
    PluginManifest *man =
        given_setting(SETTING_DOC("\"type\":\"integer\",\"default\":5,\"range\":{\"min\":1}"));

    ASSERT_TRUE(man->settings[0].has_min);
    ASSERT_FALSE(man->settings[0].has_max);
    plugin_manifest_free(man);
    man = given_setting(SETTING_DOC("\"type\":\"integer\",\"default\":5,\"range\":{\"max\":9}"));
    ASSERT_FALSE(man->settings[0].has_min);
    ASSERT_TRUE(man->settings[0].has_max);
    plugin_manifest_free(man);
    expect_error(SETTING_DOC("\"type\":\"integer\",\"default\":5,\"range\":{\"max\":4}"),
                 "/settings/0/default", "R2.11",
                 R25_2_MUST_BE("/settings/0/default", "a value inside \"range\""),
                 R25_2_USE("a value inside \"range\""));
}

TEST(a_duration_range_reads_milliseconds_and_a_size_range_reads_bytes)
{
    PluginManifest *man = given_setting(SETTING_DOC(
        "\"type\":\"duration\",\"default\":\"5s\",\"range\":{\"min\":\"1s\",\"max\":\"1m\"}"));

    ASSERT_EQ_INT(man->settings[0].min_value, 1000);
    ASSERT_EQ_INT(man->settings[0].max_value, 60000);
    plugin_manifest_free(man);
    man = given_setting(SETTING_DOC(
        "\"type\":\"size\",\"default\":\"2KiB\",\"range\":{\"min\":\"1KiB\",\"max\":\"1MiB\"}"));
    ASSERT_EQ_INT(man->settings[0].min_value, 1024);
    ASSERT_EQ_INT(man->settings[0].max_value, 1048576);
    plugin_manifest_free(man);
    expect_error(
        SETTING_DOC(
            "\"type\":\"duration\",\"default\":\"5s\",\"range\":{\"min\":\"1s\",\"max\":\"4s\"}"),
        "/settings/0/default", "R2.11",
        R25_2_MUST_BE("/settings/0/default", "a value inside \"range\""),
        R25_2_USE("a value inside \"range\""));
    expect_error(
        SETTING_DOC(
            "\"type\":\"size\",\"default\":\"3KiB\",\"range\":{\"min\":\"1KiB\",\"max\":\"2KiB\"}"),
        "/settings/0/default", "R2.11",
        R25_2_MUST_BE("/settings/0/default", "a value inside \"range\""),
        R25_2_USE("a value inside \"range\""));
}

TEST(a_range_bound_of_the_wrong_type_names_what_the_type_needs)
{
    expect_error(SETTING_DOC("\"type\":\"integer\",\"default\":1,\"range\":{\"min\":\"1\"}"),
                 "/settings/0/range/min", "R2.11",
                 R25_2_MUST_BE("/settings/0/range/min", "a whole number"),
                 R25_2_USE("a whole number"));
    expect_error(SETTING_DOC("\"type\":\"duration\",\"default\":\"1s\",\"range\":{\"max\":9}"),
                 "/settings/0/range/max", "R2.11",
                 R25_2_MUST_BE("/settings/0/range/max", "a duration like \"10m\""),
                 R25_2_USE("a duration like \"10m\""));
    expect_error(SETTING_DOC("\"type\":\"size\",\"default\":\"1B\",\"range\":{\"max\":\"big\"}"),
                 "/settings/0/range/max", "R2.11",
                 R25_2_MUST_BE("/settings/0/range/max", "a size like \"4MiB\""),
                 R25_2_USE("a size like \"4MiB\""));
}

TEST(a_range_on_a_boolean_or_a_string_is_ignored)
{
    PluginManifest *man =
        given_setting(SETTING_DOC("\"type\":\"boolean\",\"default\":true,\"range\":{\"min\":3}"));

    ASSERT_FALSE(man->settings[0].has_min);
    plugin_manifest_free(man);
    man = given_setting(SETTING_DOC("\"type\":\"string\",\"default\":\"a\",\"range\":{\"max\":3}"));
    ASSERT_FALSE(man->settings[0].has_max);
    plugin_manifest_free(man);
}

TEST(string_list_max_items_is_a_whole_number_of_one_or_more)
{
    expect_error(SETTING_DOC("\"type\":\"string_list\",\"default\":[],\"range\":{\"max_items\":0}"),
                 "/settings/0/range/max_items", "R2.11",
                 R25_2_MUST_BE("/settings/0/range/max_items", "a whole number of 1 or more"),
                 R25_2_USE("a whole number of 1 or more"));
}

TEST(an_invalid_setting_type_skips_its_default_and_range_checks_without_a_second_error)
{
    PluginManifest *man = given_parse(
        SETTING_DOC("\"type\":\"nope\",\"default\":1,\"range\":{\"min\":9}"), PLUGIN_PARSE_INVALID);

    ASSERT_EQ_INT(man->nerrors, 1);
    plugin_manifest_free(man);
}

TEST(a_setting_key_must_be_a_name)
{
    expect_error(
        HEAD ACTIONS ",\"settings\":[{\"key\":\"Max\",\"description\":\"d\",\"type\":\"boolean\","
                     "\"default\":true}]}",
        "/settings/0/key", "R2.22", "plugin.json /settings/0/key: \"Max\" isn't a valid name.",
        "Use a lowercase letter, then lowercase letters, digits, or \"_\", up to 40 "
        "characters.");
}

/* Hostile and failing input ---------------------------------------------- */

TEST(nesting_past_64_levels_has_its_own_sentence)
{
    char doc[400];
    size_t i = 0;
    PluginManifest *man = NULL;

    (void)snprintf(doc, sizeof(doc), "%s", "{\"a\":");
    for (i = 0; i < 70U; i++) {
        append(doc, sizeof(doc), "[");
    }
    for (i = 0; i < 70U; i++) {
        append(doc, sizeof(doc), "]");
    }
    append(doc, sizeof(doc), "}");
    man = given_parse(doc, PLUGIN_PARSE_SYNTAX);
    ASSERT_EQ_INT(man->nerrors, 1);
    ASSERT_STREQ(error_at(man, 0)->code, "plugin_manifest_invalid");
    ASSERT_STREQ(error_at(man, 0)->path, "");
    ASSERT_STREQ(error_at(man, 0)->rule, "R2.2");
    ASSERT_STREQ(error_at(man, 0)->message, "plugin.json is nested more than 64 levels deep.");
    ASSERT_STREQ(error_at(man, 0)->fix, "Reduce the nesting in plugin.json to 64 levels or fewer.");
    plugin_manifest_free(man);
}

TEST(an_unfinished_deep_nest_is_still_not_valid_json)
{
    char doc[400];
    size_t i = 0;
    PluginManifest *man = NULL;

    (void)snprintf(doc, sizeof(doc), "%s", "{\"a\":");
    for (i = 0; i < 70U; i++) {
        append(doc, sizeof(doc), "[");
    }
    man = given_parse(doc, PLUGIN_PARSE_SYNTAX);
    ASSERT_EQ_INT(man->nerrors, 1);
    plugin_manifest_free(man);
}

TEST(invalid_utf8_is_a_syntax_error)
{
    PluginManifest *man = given_parse("{\"name\":\"\xff\"}", PLUGIN_PARSE_SYNTAX);

    ASSERT_EQ_INT(man->nerrors, 1);
    plugin_manifest_free(man);
}

TEST(shell_metacharacters_in_a_command_are_inert_text_in_an_argv)
{
    PluginManifest *man =
        given_parse(HEAD "\"actions\":[{\"name\":\"go\",\"events\":[\"memory.created\"],"
                         "\"run\":{\"command\":[\"/bin/echo\",\"; rm -rf ~ $(x)\"]}}]}",
                    PLUGIN_PARSE_OK);

    ASSERT_EQ_INT(man->entries[0].run.argc, 2);
    ASSERT_STREQ(man->entries[0].run.argv[1], "; rm -rf ~ $(x)");
    plugin_manifest_free(man);
}

TEST(a_control_byte_in_an_unknown_field_name_is_kept_for_the_printer_to_mask)
{
    PluginManifest *man = given_parse(HEAD ACTIONS ",\"a\\u001b[2J\\nb\":1}", PLUGIN_PARSE_OK);

    ASSERT_STREQ(warning_at(man, 0)->field, "a\x1b[2J\nb");
    plugin_manifest_free(man);
}

TEST(plugin_manifest_survives_each_allocation_failure)
{
    static const char doc[] =
        "{\"format\":\"remember-plugin\",\"version\":1,\"plugin_version\":\"1.0.0\","
        "\"protocol\":2,\"name\":\"url-x\",\"x/y\":1,\"x/y\":2,\"run\":{},"
        "\"actions\":[{\"name\":\"Bad\",\"events\":[\"no\"],"
        "\"run\":{\"command\":[\"a\"],\"proces\":1}}]}";
    PluginManifest sentinel;
    int n = 0;
    PluginParseStatus st = PLUGIN_PARSE_OOM;
    PluginManifest *man = NULL;

    for (n = 0; n < 600 && st == PLUGIN_PARSE_OOM; n++) {
        man = &sentinel;
        test_alloc_fail_after(n);
        st = plugin_manifest_parse(doc, sizeof(doc) - 1U, &man);
        test_alloc_fail_after(-1);
        ASSERT_TRUE(st != PLUGIN_PARSE_OOM || man == NULL);
    }
    ASSERT_EQ_INT(st, PLUGIN_PARSE_UNSUPPORTED);
    ASSERT_EQ_INT(man->nerrors, 7);
    ASSERT_EQ_INT(man->nwarnings, 1);
    plugin_manifest_free(man);
}

TEST(a_failed_allocation_in_a_syntax_error_is_out_of_memory)
{
    PluginManifest *man = NULL;
    int n = 0;
    PluginParseStatus st = PLUGIN_PARSE_OOM;

    for (n = 0; n < 20 && st == PLUGIN_PARSE_OOM; n++) {
        test_alloc_fail_after(n);
        st = plugin_manifest_parse("{", 1U, &man);
        test_alloc_fail_after(-1);
        ASSERT_TRUE(st != PLUGIN_PARSE_OOM || man == NULL);
    }
    ASSERT_EQ_INT(st, PLUGIN_PARSE_SYNTAX);
    plugin_manifest_free(man);
}

/* Limits (decision:2026-11-13-plugin-manifest-limits). Each one parses at N and is refused at
   N + 1, with the sentence of R25-2 sentence 5. */

/* n copies of one JSON item, comma-joined. */
static void fill_list(char *dst, size_t cap, const char *item, size_t n)
{
    size_t i = 0;

    dst[0] = '\0';
    for (i = 0; i < n; i++) {
        appendf(dst, cap, "%s%s", i == 0U ? "" : ",", item);
    }
}

#define EXPECT_LIMIT(fmt, item, at, path, rule, what)                                              \
    do {                                                                                           \
        char list_[1100];                                                                          \
        char doc_[2400];                                                                           \
        char message_[900];                                                                        \
        char fix_[900];                                                                            \
        fill_list(list_, sizeof(list_), item, at);                                                 \
        (void)snprintf(doc_, sizeof(doc_), fmt, list_);                                            \
        expect_ok(doc_);                                                                           \
        fill_list(list_, sizeof(list_), item, (at) + 1U);                                          \
        (void)snprintf(doc_, sizeof(doc_), fmt, list_);                                            \
        (void)snprintf(message_, sizeof(message_), "plugin.json %s: must be %s.", path, what);     \
        (void)snprintf(fix_, sizeof(fix_), "Use %s.", what);                                       \
        expect_error(doc_, path, rule, message_, fix_);                                            \
    } while (0)

/* A string of at bytes parses; one of at + 1 bytes is refused. */
#define EXPECT_LENGTH(fmt, ch, at, path, rule, what)                                               \
    do {                                                                                           \
        const char *text_ = given_run_of(ch, (at));                                                \
        char doc_[2400];                                                                           \
        char message_[900];                                                                        \
        char fix_[900];                                                                            \
        (void)snprintf(doc_, sizeof(doc_), fmt, text_);                                            \
        expect_ok(doc_);                                                                           \
        text_ = given_run_of(ch, (at) + 1U);                                                       \
        (void)snprintf(doc_, sizeof(doc_), fmt, text_);                                            \
        (void)snprintf(message_, sizeof(message_), "plugin.json %s: must be %s.", path, what);     \
        (void)snprintf(fix_, sizeof(fix_), "Use %s.", what);                                       \
        expect_error(doc_, path, rule, message_, fix_);                                            \
    } while (0)

#define ACTION_WITH(member) HEAD "\"actions\":[" ACTION_BODY ",\"filter\":{" member "}}]}"
#define ACTORS_WHAT "\"human\", \"agent\", \"tool\", \"share\", or \"import\""

TEST(l2_an_action_lists_at_most_eight_events)
{
    EXPECT_LIMIT(HEAD "\"actions\":[{\"name\":\"go\",\"events\":[%s]," ACTION_RUN "}]}",
                 "\"memory.created\"", 8U, "/actions/0/events", "R2.21",
                 "a list of 1 to 8 of \"memory.created\", \"memory.updated\", \"memory.deleted\", "
                 "\"memory.restored\", \"memory.purged\", \"memory.expired\", or \"memory.*\"");
}

TEST(l3_a_command_has_at_most_eight_strings)
{
    EXPECT_LIMIT(HEAD "\"actions\":[{\"name\":\"go\",\"events\":[\"memory.created\"],"
                      "\"run\":{\"command\":[%s]}}]}",
                 "\"a\"", 8U, "/actions/0/run/command", "R2.8", "a list of 1 to 8 strings");
}

TEST(l5_permissions_needs_has_at_most_sixteen_items)
{
    char doc[1500];
    size_t i = 0;

    (void)snprintf(doc, sizeof(doc), "%s",
                   HEAD "\"filters\":[" FILTER_BODY ",\"permissions\":{\"needs\":[");
    for (i = 0; i < 16U; i++) {
        appendf(doc, sizeof(doc), "%s\"remember.a.k%zu\"", i == 0U ? "" : ",", i);
    }
    append(doc, sizeof(doc), "]}}]}");
    expect_ok(doc);
}

TEST(l6_tags_any_holds_eight_strings)
{
    EXPECT_LIMIT(ACTION_WITH("\"tags\":{\"any\":[%s]}"), "\"a\"", 8U, "/actions/0/filter/tags/any",
                 "R2.21", "a list of at most 8 strings");
}

TEST(l6_tags_all_holds_eight_strings)
{
    EXPECT_LIMIT(ACTION_WITH("\"tags\":{\"all\":[%s]}"), "\"a\"", 8U, "/actions/0/filter/tags/all",
                 "R2.21", "a list of at most 8 strings");
}

TEST(l6_a_tag_holds_63_bytes)
{
    EXPECT_LENGTH(ACTION_WITH("\"tags\":{\"any\":[\"%s\"]}"), 't', 63U,
                  "/actions/0/filter/tags/any/0", "R2.21", "a string of at most 63 bytes");
}

TEST(l7_created_by_and_updated_by_hold_five_kinds)
{
    EXPECT_LIMIT(ACTION_WITH("\"created_by\":[%s]"), "\"human\"", 5U,
                 "/actions/0/filter/created_by", "R2.21", "a list of at most 5 of " ACTORS_WHAT);
    EXPECT_LIMIT(ACTION_WITH("\"updated_by\":[%s]"), "\"human\"", 5U,
                 "/actions/0/filter/updated_by", "R2.21", "a list of at most 5 of " ACTORS_WHAT);
}

TEST(l7_bins_hold_four)
{
    EXPECT_LIMIT(ACTION_WITH("\"bins\":[%s]"), "\"live\"", 4U, "/actions/0/filter/bins", "R2.21",
                 "a list of at most 4 of \"live\", \"expired\", \"deleted\", or \"purged\"");
}

TEST(l7_origins_hold_three)
{
    EXPECT_LIMIT(ACTION_WITH("\"origins\":[%s]"), "\"local\"", 3U, "/actions/0/filter/origins",
                 "R2.21", "a list of at most 3 of \"local\", \"sync\", or \"import\"");
}

TEST(l8_not_caused_by_holds_eight_strings_of_79_bytes)
{
    EXPECT_LIMIT(ACTION_WITH("\"not_caused_by\":[%s]"), "\"a\"", 8U,
                 "/actions/0/filter/not_caused_by", "R2.21", "a list of at most 8 strings");
    EXPECT_LENGTH(ACTION_WITH("\"not_caused_by\":[\"%s\"]"), 'c', 79U,
                  "/actions/0/filter/not_caused_by/0", "R2.21", "a string of at most 79 bytes");
}

TEST(l9_a_manifest_has_at_most_sixteen_settings)
{
    char list[1100];
    char doc[1400];
    size_t i = 0;

    list[0] = '\0';
    for (i = 0; i < 16U; i++) {
        appendf(list, sizeof(list),
                "%s{\"key\":\"k%zu\",\"description\":\"d\",\"type\":\"boolean\","
                "\"default\":true}",
                i == 0U ? "" : ",", i);
    }
    (void)snprintf(doc, sizeof(doc), HEAD ACTIONS ",\"settings\":[%s]}", list);
    expect_ok(doc);
    fill_list(list, sizeof(list),
              "{\"key\":\"k\",\"description\":\"d\",\"type\":\"boolean\",\"default\":true}", 17U);
    (void)snprintf(doc, sizeof(doc), HEAD ACTIONS ",\"settings\":[%s]}", list);
    expect_error(doc, "/settings", "R2.11",
                 "plugin.json /settings: must be a list of at most 16 settings.",
                 "Use a list of at most 16 settings.");
}

TEST(l10_an_enum_has_at_most_eight_values_of_63_bytes)
{
    EXPECT_LIMIT(SETTING_DOC("\"type\":\"enum\",\"default\":\"a\",\"range\":{\"values\":[%s]}"),
                 "\"a\"", 8U, "/settings/0/range/values", "R2.11", "a list of at most 8 strings");
    EXPECT_LENGTH(SETTING_DOC("\"type\":\"enum\",\"default\":\"a\",\"range\":{\"values\":"
                              "[\"a\",\"%s\"]}"),
                  'v', 63U, "/settings/0/range/values/1", "R2.11", "a string of at most 63 bytes");
}

TEST(l11_a_string_list_default_holds_511_bytes_of_source_json)
{
    EXPECT_LENGTH(SETTING_DOC("\"type\":\"string_list\",\"default\":[\"%s\"]"), 'a', 507U,
                  "/settings/0/default", "R2.11",
                  "a list of strings of at most 511 bytes as written");
}

TEST(l12_a_plugin_version_holds_63_bytes)
{
    EXPECT_LENGTH("{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"wake\","
                  "\"plugin_version\":\"1.0.0-%s\",\"protocol\":1," ACTIONS "}",
                  'a', 57U, "/plugin_version", "R2.2", "a version of at most 63 bytes");
}

TEST(l13_a_duration_string_holds_23_bytes)
{
    EXPECT_LENGTH(HEAD ACTIONS ",\"idle_timeout\":\"%sms\"}", '0', 21U, "/idle_timeout", "R2.20",
                  "a duration like \"10m\"");
}

/* Q2: plugin-controlled text prints with JSON string escaping, in the message and in the fix. */

TEST(q2_an_unknown_field_name_prints_escaped_in_the_message_and_the_fix)
{
    PluginManifest *man = given_parse(
        HEAD "\"filters\":[{\"name\":\"rank\",\"point\":\"search.results\","
             "\"run\":{\"process\":\"persistent\",\"command\":[\"x\"],\"a\\\"b\\\\c\\nd\":1}}]}",
        PLUGIN_PARSE_INVALID);

    ASSERT_STREQ(error_at(man, 0)->message,
                 "plugin.json /filters/0/run: unknown field \"a\\\"b\\\\c\\nd\".");
    ASSERT_STREQ(error_at(man, 0)->fix, "Remove \"a\\\"b\\\\c\\nd\".");
    ASSERT_STREQ(error_at(man, 0)->path, "/filters/0/run/a\"b\\c\nd");
    plugin_manifest_free(man);
}

TEST(q2_an_escape_a_delete_and_a_c1_control_print_as_u_escapes)
{
    PluginManifest *man =
        given_parse(HEAD "\"filters\":[{\"name\":\"rank\",\"point\":\"search.results\","
                         "\"run\":{\"process\":\"persistent\",\"command\":[\"x\"],"
                         "\"\\u001b[2J\\u007f\\u0085\\t\\r\\b\\f\":1}}]}",
                    PLUGIN_PARSE_INVALID);

    ASSERT_STREQ(error_at(man, 0)->message, "plugin.json /filters/0/run: unknown field "
                                            "\"\\u001b[2J\\u007f\\u0085\\t\\r\\b\\f\".");
    plugin_manifest_free(man);
}

TEST(q2_text_that_is_not_a_control_prints_as_written)
{
    PluginManifest *man = given_parse(
        HEAD "\"filters\":[{\"name\":\"rank\",\"point\":\"search.results\","
             "\"run\":{\"process\":\"persistent\",\"command\":[\"x\"],\"\xd7\x90\xc2\xa0z\":1}}]}",
        PLUGIN_PARSE_INVALID);

    ASSERT_STREQ(error_at(man, 0)->message,
                 "plugin.json /filters/0/run: unknown field \"\xd7\x90\xc2\xa0z\".");
    plugin_manifest_free(man);
}

TEST(q2_an_invalid_name_and_a_hyphen_name_print_escaped)
{
    expect_error("{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"A\\u0007\\\"\","
                 "\"plugin_version\":\"1.0.0\",\"protocol\":1," ACTIONS "}",
                 "/name", "R2.22", "plugin.json /name: \"A\\u0007\\\"\" isn't a valid name.",
                 "Use a lowercase letter, then lowercase letters, digits, or \"_\", up to 40 "
                 "characters.");
    expect_error(
        "{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"a-\\u001b\\n\","
        "\"plugin_version\":\"1.0.0\",\"protocol\":1," ACTIONS "}",
        "/name", "R2.22", "plugin.json /name: \"a-\\u001b\\n\" isn't a valid name.",
        "Use a lowercase letter, then lowercase letters, digits, or \"_\", up to 40 characters.");
}

TEST(q2_the_plugin_name_in_a_protocol_error_prints_escaped)
{
    PluginManifest *man =
        given_parse("{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"x\\ny\\\"\","
                    "\"plugin_version\":\"1.0.0\",\"protocol\":2," ACTIONS "}",
                    PLUGIN_PARSE_UNSUPPORTED);

    ASSERT_STREQ(error_at(man, 1)->message,
                 "plugin.json /protocol: \"x\\ny\\\"\" was written for plugin protocol 2, and this "
                 "remember reads protocol 1.");
    ASSERT_STREQ(error_at(man, 1)->fix,
                 "Update remember, or install an older version of \"x\\ny\\\"\".");
    plugin_manifest_free(man);
}

TEST(q2_a_repeated_key_prints_escaped)
{
    PluginManifest *man =
        given_parse(HEAD ACTIONS ",\"a\\u001b\":1,\"a\\u001b\":2}", PLUGIN_PARSE_INVALID);

    ASSERT_STREQ(error_at(man, 0)->message, "plugin.json: \"a\\u001b\" appears more than once.");
    ASSERT_STREQ(error_at(man, 0)->fix, "Keep one \"a\\u001b\".");
    ASSERT_STREQ(error_at(man, 0)->path, "/a\x1b");
    plugin_manifest_free(man);
}

/* Q3: a duration or size over 23 bytes gets the ordinary phrase. */

TEST(q3_a_long_duration_or_size_uses_the_ordinary_phrase)
{
    expect_error(HEAD ACTIONS ",\"idle_timeout\":\"000000000000000000000000001ms\"}",
                 "/idle_timeout", "R2.20",
                 R25_2_MUST_BE("/idle_timeout", "a duration like \"10m\""),
                 R25_2_USE("a duration like \"10m\""));
    expect_error(HEAD FILTERS ",\"backfill\":{\"enabled\":true,\"max_batch_size\":"
                              "\"000000000000000000000000001B\"}}",
                 "/backfill/max_batch_size", "R2.18",
                 R25_2_MUST_BE("/backfill/max_batch_size", "a size of at most 4MiB"),
                 R25_2_USE("a size of at most 4MiB"));
}

/* A name used twice in one list (R25-2 sentence 8). */

TEST(two_actions_with_one_name_are_refused)
{
    expect_error(HEAD "\"actions\":[" ACTION_BODY "}," ACTION_BODY "}]}", "/actions/1/name",
                 "R2.22", R25_2_TWICE(" /actions", "go"), "Rename one of them.");
}

TEST(two_filters_with_one_name_are_refused)
{
    expect_error(HEAD "\"filters\":[" FILTER_BODY "}," FILTER_BODY "}]}", "/filters/1/name",
                 "R2.22", R25_2_TWICE(" /filters", "rank"), "Rename one of them.");
}

TEST(two_settings_with_one_key_are_refused)
{
    expect_error(HEAD ACTIONS ",\"settings\":[{\"key\":\"k\",\"description\":\"d\",\"type\":"
                              "\"boolean\",\"default\":true},{\"key\":\"k\",\"description\":\"d\","
                              "\"type\":\"boolean\",\"default\":false}]}",
                 "/settings/1/key", "R2.22", R25_2_TWICE(" /settings", "k"), "Rename one of them.");
}

TEST(an_action_and_a_filter_may_share_a_name)
{
    expect_ok(HEAD "\"actions\":[{\"name\":\"rank\",\"events\":[\"memory.created\"]," ACTION_RUN
                   "}],"
                   "\"filters\":[" FILTER_BODY "}]}");
}

TEST(a_third_use_of_a_name_is_reported_again)
{
    PluginManifest *man =
        given_parse(HEAD "\"actions\":[" ACTION_BODY "}," ACTION_BODY "}," ACTION_BODY "}]}",
                    PLUGIN_PARSE_INVALID);

    ASSERT_EQ_INT(man->nerrors, 2);
    ASSERT_STREQ(error_at(man, 1)->path, "/actions/2/name");
    plugin_manifest_free(man);
}

TEST(an_invalid_name_is_not_also_reported_as_a_repeat)
{
    PluginManifest *man = given_parse(HEAD "\"actions\":[{\"name\":\"A\",\"events\":"
                                           "[\"memory.created\"]," ACTION_RUN "},{\"name\":\"A\","
                                           "\"events\":[\"memory.created\"]," ACTION_RUN "}]}",
                                      PLUGIN_PARSE_INVALID);

    ASSERT_EQ_INT(man->nerrors, 2);
    ASSERT_STR_CONTAINS(error_at(man, 0)->message, "isn't a valid name");
    ASSERT_STR_CONTAINS(error_at(man, 1)->message, "isn't a valid name");
    plugin_manifest_free(man);
}

TEST(each_persistent_entry_that_differs_gets_its_own_error)
{
    PluginManifest *man =
        given_parse(HEAD "\"actions\":[{\"name\":\"one\",\"events\":[\"memory.created\"],"
                         "\"run\":{\"process\":\"persistent\",\"command\":[\"a\"]}},"
                         "{\"name\":\"two\",\"events\":[\"memory.created\"],"
                         "\"run\":{\"process\":\"persistent\",\"command\":[\"b\"]}},"
                         "{\"name\":\"three\",\"events\":[\"memory.created\"],"
                         "\"run\":{\"process\":\"persistent\",\"command\":[\"a\"]}},"
                         "{\"name\":\"four\",\"events\":[\"memory.created\"],"
                         "\"run\":{\"process\":\"persistent\",\"command\":[\"c\"]}}]}",
                    PLUGIN_PARSE_INVALID);

    ASSERT_EQ_INT(man->nerrors, 2);
    ASSERT_STREQ(error_at(man, 0)->path, "/actions/1/run/command");
    ASSERT_STREQ(error_at(man, 1)->path, "/actions/3/run/command");
    ASSERT_STR_CONTAINS(error_at(man, 1)->message, "different command than /actions/0.");
    plugin_manifest_free(man);
}

TEST(a_plugin_version_that_is_not_a_string_must_be_a_version)
{
    expect_error("{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"wake\","
                 "\"plugin_version\":5,\"protocol\":1," ACTIONS "}",
                 "/plugin_version", "R2.2",
                 R25_2_MUST_BE("/plugin_version", "a version like \"1.0.0\""),
                 R25_2_USE("a version like \"1.0.0\""));
}

TEST(q1_a_nineteen_digit_protocol_prints_its_digits_and_not_a_saturated_value)
{
    PluginManifest *man =
        given_parse(PROTOCOL_DOC("9999999999999999999"), PLUGIN_PARSE_UNSUPPORTED);

    ASSERT_STR_CONTAINS(error_at(man, 0)->message, "plugin protocol 9999999999999999999, and");
    plugin_manifest_free(man);
    man = given_parse(PROTOCOL_DOC("-123456789012345678"), PLUGIN_PARSE_UNSUPPORTED);
    ASSERT_STR_CONTAINS(error_at(man, 0)->message, "plugin protocol -123456789012345678, and");
    plugin_manifest_free(man);
}

TEST(q1_a_whole_number_of_64_digits_is_printed_and_one_of_65_is_not_whole)
{
    PluginManifest *man = given_parse(PROTOCOL_DOC("1e63"), PLUGIN_PARSE_UNSUPPORTED);

    ASSERT_STR_CONTAINS(
        error_at(man, 0)->message,
        "plugin protocol 1000000000000000000000000000000000000000000000000000000000000000, and");
    plugin_manifest_free(man);
    man = given_parse(PROTOCOL_DOC("1e64"), PLUGIN_PARSE_INVALID);
    ASSERT_STREQ(error_at(man, 0)->message, R25_2_MUST_BE("/protocol", "a whole number"));
    plugin_manifest_free(man);
    man = given_parse(PROTOCOL_DOC("5e-2"), PLUGIN_PARSE_INVALID);
    ASSERT_STREQ(error_at(man, 0)->message, R25_2_MUST_BE("/protocol", "a whole number"));
    plugin_manifest_free(man);
}

TEST(q2_a_unit_separator_escapes_and_a_space_does_not)
{
    PluginManifest *man = given_parse(
        HEAD "\"filters\":[{\"name\":\"rank\",\"point\":\"search.results\","
             "\"run\":{\"process\":\"persistent\",\"command\":[\"x\"],\"a\\u001f \\u0085\":1}}]}",
        PLUGIN_PARSE_INVALID);

    ASSERT_STREQ(error_at(man, 0)->message,
                 "plugin.json /filters/0/run: unknown field \"a\\u001f \\u0085\".");
    plugin_manifest_free(man);
}

TEST(a_min_one_above_max_is_refused)
{
    expect_error(SETTING_DOC("\"type\":\"integer\",\"default\":2,\"range\":{\"min\":2,\"max\":1}"),
                 "/settings/0/range", "R2.11",
                 "plugin.json /settings/0/range: \"min\" is greater than \"max\".",
                 "Set \"min\" to \"max\" or lower.");
}

TEST(a_repeated_key_is_the_later_one_and_errors_come_in_document_order)
{
    PluginManifest *man = given_parse(
        HEAD
        "\"filters\":[{\"name\":\"rank\",\"point\":\"search.results\","
        "\"run\":{\"process\":\"persistent\",\"command\":[\"x\"],\"zz\":1,\"yy\":1,\"zz\":2}}]}",
        PLUGIN_PARSE_INVALID);

    ASSERT_EQ_INT(man->nerrors, 3);
    ASSERT_STREQ(error_at(man, 0)->message, R25_2_UNKNOWN(" /filters/0/run", "zz"));
    ASSERT_STREQ(error_at(man, 1)->message, R25_2_UNKNOWN(" /filters/0/run", "yy"));
    ASSERT_STREQ(error_at(man, 2)->message, R25_2_TWICE(" /filters/0/run", "zz"));
    plugin_manifest_free(man);
}

TEST(q1_a_65_digit_integer_is_not_a_whole_number_and_a_64_digit_one_is_printed)
{
    char token[80];
    PluginManifest *man = NULL;

    (void)snprintf(token, sizeof(token), "1%s", given_run_of('0', 64));
    man = given_parse(PROTOCOL_DOC_OF(token), PLUGIN_PARSE_INVALID);
    ASSERT_STREQ(error_at(man, 0)->message, R25_2_MUST_BE("/protocol", "a whole number"));
    plugin_manifest_free(man);
    (void)snprintf(token, sizeof(token), "1%s", given_run_of('0', 63));
    man = given_parse(PROTOCOL_DOC_OF(token), PLUGIN_PARSE_UNSUPPORTED);
    ASSERT_EQ_INT(strlen(error_at(man, 0)->message),
                  strlen("plugin.json /protocol: \"a\" was written for plugin protocol , and this "
                         "remember reads protocol 1.") +
                      64U);
    plugin_manifest_free(man);
}

/* The schema gives data_version a minimum and no maximum, so a value past 32 bits parses. */
TEST(a_data_version_past_32_bits_parses)
{
    PluginManifest *man =
        given_parse(HEAD ACTIONS ",\"data_version\":2147483648}", PLUGIN_PARSE_OK);

    ASSERT_EQ_INT(man->data_version, 2147483648LL);
    plugin_manifest_free(man);
}

/* Q1 reads by value: zeros after the point add no digit, so 70 of them still read 1. */
/* R25-2 sentence 13: every filter is persistent, so a filter with a broken "process" is enough. */
TEST(backfill_with_a_filter_whose_process_is_missing_names_only_the_process)
{
    PluginManifest *man =
        given_parse(HEAD "\"backfill\":{\"enabled\":true},\"filters\":[{\"name\":\"f\",\"point\":"
                         "\"search.query\",\"run\":{\"command\":[\"x\"]}}]}",
                    PLUGIN_PARSE_INVALID);

    ASSERT_EQ_INT(man->nerrors, 1);
    ASSERT_STREQ(error_at(man, 0)->message, "plugin.json /filters/0/run: \"process\" is missing.");
    plugin_manifest_free(man);
}

/* "One parse returns every error in the file": every bad command item, not the first. */
TEST(every_bad_command_item_gets_its_own_error)
{
    PluginManifest *man =
        given_parse(HEAD "\"actions\":[{\"name\":\"go\",\"events\":[\"memory.created\"],"
                         "\"run\":{\"command\":[\"x\",\"\",\"\"]}}]}",
                    PLUGIN_PARSE_INVALID);

    ASSERT_EQ_INT(man->nerrors, 2);
    ASSERT_STREQ(error_at(man, 0)->path, "/actions/0/run/command/1");
    ASSERT_STREQ(error_at(man, 1)->path, "/actions/0/run/command/2");
    plugin_manifest_free(man);
}

/* Q1: a 19-digit whole number that fits a long long is read; one past it is not. */
TEST(q1_a_19_digit_whole_number_that_fits_is_read)
{
    PluginManifest *man = given_parse(
        SETTING_DOC("\"type\":\"integer\",\"default\":9223372036854775807"), PLUGIN_PARSE_OK);

    plugin_manifest_free(man);
    man = given_parse(SETTING_DOC("\"type\":\"integer\",\"default\":1e18"), PLUGIN_PARSE_OK);
    plugin_manifest_free(man);
    expect_error(SETTING_DOC("\"type\":\"integer\",\"default\":9223372036854775808"),
                 "/settings/0/default", "R2.11",
                 R25_2_MUST_BE("/settings/0/default", "a whole number"),
                 R25_2_USE("a whole number"));
}

TEST(an_enum_default_that_is_not_a_string_uses_the_enum_phrase)
{
    expect_error(SETTING_DOC("\"type\":\"enum\",\"default\":5,\"range\":{\"values\":[\"a\"]}"),
                 "/settings/0/default", "R2.11",
                 R25_2_MUST_BE("/settings/0/default", "one of the \"values\" in \"range\""),
                 R25_2_USE("one of the \"values\" in \"range\""));
}

/* Sentence 7b once, at the first "run"; sentence 8 at the second. */
TEST(a_repeated_top_level_run_is_misplaced_once_and_repeated_once)
{
    PluginManifest *man =
        given_parse(HEAD "\"run\":1,\"run\":2," ACTIONS "}", PLUGIN_PARSE_INVALID);

    ASSERT_EQ_INT(man->nerrors, 2);
    ASSERT_STREQ(error_at(man, 0)->message, "plugin.json: \"run\" can't be at the top level.");
    ASSERT_STREQ(error_at(man, 1)->message, R25_2_TWICE("", "run"));
    plugin_manifest_free(man);
}

/* R25-2: "The order is document order", across the whole file, not per check. */
TEST(errors_come_in_document_order_across_the_file)
{
    PluginManifest *man = given_parse(
        "{\"actions\":[{\"name\":\"Bad\",\"events\":[\"memory.created\"]," ACTION_RUN "}],"
        "\"format\":\"nope\",\"version\":1,\"protocol\":1,\"name\":\"wake\","
        "\"plugin_version\":\"1.0.0\"}",
        PLUGIN_PARSE_INVALID);

    ASSERT_EQ_INT(man->nerrors, 2);
    ASSERT_STREQ(error_at(man, 0)->path, "/actions/0/name");
    ASSERT_STREQ(error_at(man, 1)->path, "/format");
    plugin_manifest_free(man);
}

/* "A repeated key in any object is an error": inside values the schema leaves open too. */
TEST(a_repeated_key_inside_an_unknown_member_is_an_error)
{
    PluginManifest *man = given_parse(
        HEAD "\"x_custom\":{\"k\":1,\"k\":2},\"x_list\":[{\"j\":1,\"j\":2}]," ACTIONS "}",
        PLUGIN_PARSE_INVALID);

    ASSERT_EQ_INT(man->nerrors, 2);
    ASSERT_STREQ(error_at(man, 0)->path, "/x_custom/k");
    ASSERT_STREQ(error_at(man, 0)->message, R25_2_TWICE(" /x_custom", "k"));
    ASSERT_STREQ(error_at(man, 1)->path, "/x_list/0/j");
    ASSERT_EQ_INT(man->nwarnings, 2);
    plugin_manifest_free(man);
}

/* Events may repeat (the schema has no uniqueItems there), and "memory.*" twice lists no other. */
/* Q2 reaches the pointer in <where>: a repeat under a hostile unknown key prints it escaped. */
TEST(q2_a_pointer_in_a_message_prints_escaped)
{
    PluginManifest *man = given_parse(HEAD "\"x\\u001b[2J\\\"\":{\"k\":1,\"k\":2}," ACTIONS "}",
                                      PLUGIN_PARSE_INVALID);

    ASSERT_EQ_INT(man->nerrors, 1);
    ASSERT_STREQ(error_at(man, 0)->message, R25_2_TWICE(" /x\\u001b[2J\\\"", "k"));
    ASSERT_STREQ(error_at(man, 0)->path, "/x\033[2J\"/k");
    plugin_manifest_free(man);
}

/* DoD: 1.0, 1e0 and -0 by value in every whole-number field (where 0 is out of range, -0 is the
   range error, as 0 is). */
TEST(q1_every_whole_field_reads_1_0_1e0_and_minus_0_by_value)
{
    static const char *const ones[] = {"1.0", "1e0"};
    size_t i = 0;

    for (i = 0; i < 2U; i++) {
        char doc[1024];
        PluginManifest *man = NULL;

        (void)snprintf(doc, sizeof(doc),
                       "{\"format\":\"remember-plugin\",\"version\":%s,\"name\":\"wake\","
                       "\"plugin_version\":\"1.0.0\",\"protocol\":%s,\"data_version\":%s,"
                       "\"backfill\":{\"enabled\":true,\"max_batch_entries\":%s},"
                       "\"filters\":[{\"name\":\"rank\",\"point\":\"search.results\","
                       "\"priority\":%s," FILTER_RUN "}],"
                       "\"settings\":[{\"key\":\"n\",\"description\":\"d\",\"type\":\"integer\","
                       "\"default\":%s}]}",
                       ones[i], ones[i], ones[i], ones[i], ones[i], ones[i]);
        man = given_parse(doc, PLUGIN_PARSE_OK);
        ASSERT_EQ_INT(man->data_version, 1);
        ASSERT_EQ_INT(man->max_batch_entries, 1);
        ASSERT_EQ_INT(man->entries[0].priority, 1);
        ASSERT_STREQ(man->settings[0].default_text, "1");
        plugin_manifest_free(man);
    }
    expect_error(HEAD FILTERS ",\"data_version\":-0}", "/data_version", "R2.14",
                 R25_2_MUST_BE("/data_version", "a whole number of 1 or more"),
                 R25_2_USE("a whole number of 1 or more"));
    expect_ok(SETTING_DOC("\"type\":\"integer\",\"default\":-0"));
}

TEST(q1_protocol_2_0_prints_2_in_the_no_name_sentence_too)
{
    PluginManifest *man =
        given_parse(MINIMAL("\"version\":1,\"protocol\":2.0"), PLUGIN_PARSE_UNSUPPORTED);

    ASSERT_STREQ(error_at(man, 1)->message,
                 "plugin.json /protocol: this plugin was written for plugin protocol 2, and this "
                 "remember reads protocol 1.");
    plugin_manifest_free(man);
}

/* DoD: 24 bytes is the ordinary phrase, with no 23-byte wording; 23 bytes is read by value. */
TEST(q3_24_bytes_is_the_ordinary_phrase_and_23_bytes_is_read)
{
    PluginManifest *man = NULL;

    expect_error(HEAD ACTIONS ",\"idle_timeout\":\"0000000000000000000001ms\"}", "/idle_timeout",
                 "R2.20", R25_2_MUST_BE("/idle_timeout", "a duration like \"10m\""),
                 R25_2_USE("a duration like \"10m\""));
    expect_error(SETTING_DOC("\"type\":\"size\",\"default\":\"00000000000000000000001B\""),
                 "/settings/0/default", "R2.11",
                 R25_2_MUST_BE("/settings/0/default", "a size like \"4MiB\""),
                 R25_2_USE("a size like \"4MiB\""));
    man =
        given_parse(HEAD ACTIONS ",\"idle_timeout\":\"000000000000000000001ms\"}", PLUGIN_PARSE_OK);
    ASSERT_EQ_INT(man->idle_timeout_ms, 1);
    plugin_manifest_free(man);
    expect_ok(SETTING_DOC("\"type\":\"size\",\"default\":\"0000000000000000000001B\""));
}

/* The exponent clamp: an exponent of 30 digits would overflow a long long without it. */
TEST(q1_an_exponent_past_a_long_long_is_not_a_whole_number)
{
    expect_error(PROTOCOL_DOC("1e999999999999999999999999999999"), "/protocol", "R2.4",
                 R25_2_MUST_BE("/protocol", "a whole number"), R25_2_USE("a whole number"));
    expect_error(PROTOCOL_DOC("1e-999999999999999999999999999999"), "/protocol", "R2.4",
                 R25_2_MUST_BE("/protocol", "a whole number"), R25_2_USE("a whole number"));
}

TEST(memory_star_listed_twice_is_still_alone)
{
    PluginManifest *man = given_parse(
        HEAD "\"actions\":[{\"name\":\"go\",\"events\":[\"memory.*\",\"memory.*\"]," ACTION_RUN
             "}]}",
        PLUGIN_PARSE_OK);

    plugin_manifest_free(man);
}

TEST(q1_zeros_after_the_point_are_not_digits)
{
    expect_ok(
        PROTOCOL_DOC("1.0000000000000000000000000000000000000000000000000000000000000000000000"));
}

/* The broken command already has its error. Comparing it with the first command says nothing. */
TEST(a_persistent_entry_with_a_broken_command_gets_only_its_command_error)
{
    expect_error(HEAD "\"filters\":[" FILTER_BODY "},{\"name\":\"two\",\"point\":\"search.query\","
                      "\"run\":{\"process\":\"persistent\",\"command\":[\"\"]}}]}",
                 "/filters/1/run/command/0", "R2.8",
                 R25_2_MUST_BE("/filters/1/run/command/0", "a string of 1 to 255 bytes"),
                 R25_2_USE("a string of 1 to 255 bytes"));
}

/* The entry is persistent, so backfill has its persistent entry: only the command is wrong. */
TEST(backfill_with_a_persistent_entry_whose_command_is_broken_names_only_the_command)
{
    expect_error(HEAD "\"actions\":[{\"name\":\"go\",\"events\":[\"memory.created\"],"
                      "\"run\":{\"process\":\"persistent\",\"command\":[\"\"]}}],"
                      "\"backfill\":{\"enabled\":true}}",
                 "/actions/0/run/command/0", "R2.8",
                 R25_2_MUST_BE("/actions/0/run/command/0", "a string of 1 to 255 bytes"),
                 R25_2_USE("a string of 1 to 255 bytes"));
}

void register_plugins_tests(void)
{
    RUN_TEST(a_schema_valid_manifest_parses);
    RUN_TEST(a_full_schema_valid_manifest_keeps_every_field);
    RUN_TEST(omitted_budget_grace_priority_process_and_origins_are_the_defaults);
    RUN_TEST(an_action_filter_block_without_origins_still_defaults_to_local);
    RUN_TEST(an_empty_origins_list_stays_empty);
    RUN_TEST(a_hyphen_in_a_manifest_name_prints_the_c36_hyphen_lines);
    RUN_TEST(a_hyphen_in_an_entry_name_folds_every_hyphen_in_the_fix);
    RUN_TEST(a_hyphen_name_of_40_characters_gets_the_hyphen_line_and_41_does_not);
    RUN_TEST(g5_a_hyphen_name_whose_underscore_form_is_invalid_gets_sentence_6);
    RUN_TEST(a_forty_one_character_name_is_not_a_valid_name);
    RUN_TEST(a_forty_character_name_is_valid);
    RUN_TEST(a_name_must_start_with_a_lowercase_letter);
    RUN_TEST(a_name_that_is_not_a_string_must_be_a_string);
    RUN_TEST(a_name_with_a_nul_escape_is_not_json_so_nothing_is_cut_short);
    RUN_TEST(a_key_with_a_nul_escape_is_not_json_so_it_cannot_alias_a_field);
    RUN_TEST(a_setting_key_enabled_prints_the_c36_reserved_key_lines);
    RUN_TEST(a_reserved_setting_key_in_a_manifest_with_no_name_says_this_plugin);
    RUN_TEST(a_reserved_setting_key_names_a_string_name_even_when_the_name_is_invalid);
    RUN_TEST(the_c36_reserved_key_line_escapes_a_hostile_plugin_name);
    RUN_TEST(a_setting_key_with_a_hyphen_prints_the_c36_hyphen_lines);
    RUN_TEST(protocol_2_returns_plugin_protocol_unsupported_with_the_r25_1_message_and_fix);
    RUN_TEST(q1_protocol_written_as_1_0_or_1e0_is_protocol_1);
    RUN_TEST(q1_version_written_as_1_0_is_version_1);
    RUN_TEST(q1_a_protocol_prints_its_value_and_not_its_source_text);
    RUN_TEST(q1_a_huge_protocol_prints_its_digits);
    RUN_TEST(q1_a_protocol_with_a_fraction_is_not_a_whole_number);
    RUN_TEST(q1_an_absurd_exponent_is_not_a_whole_number_and_costs_nothing);
    RUN_TEST(q1_priority_written_as_a_float_or_minus_zero_parses_by_value);
    RUN_TEST(q1_an_integer_setting_default_written_as_1e2_is_stored_as_100);
    RUN_TEST(q1_data_version_and_batch_entries_read_by_value);
    RUN_TEST(protocol_with_no_usable_name_uses_the_plugin_wording);
    RUN_TEST(protocol_names_the_plugin_as_the_file_wrote_it_even_when_the_name_is_invalid);
    RUN_TEST(a_protocol_that_is_not_a_number_must_be_a_whole_number);
    RUN_TEST(manifest_version_2_returns_plugin_protocol_unsupported_in_the_r25_1_shape);
    RUN_TEST(manifest_version_with_no_usable_name_uses_the_plugin_wording);
    RUN_TEST(both_a_bad_version_and_a_bad_protocol_come_back);
    RUN_TEST(a_missing_version_and_protocol_are_missing_members);
    RUN_TEST(a_version_that_is_not_a_number_must_be_a_whole_number);
    RUN_TEST(text_that_is_not_json_is_a_syntax_error_with_one_line);
    RUN_TEST(a_json_array_at_the_top_is_not_an_object);
    RUN_TEST(a_null_text_and_a_null_out_are_refused_without_a_crash);
    RUN_TEST(a_manifest_with_neither_actions_nor_filters_is_refused);
    RUN_TEST(a_missing_member_names_the_object_that_lacks_it);
    RUN_TEST(format_must_be_remember_plugin);
    RUN_TEST(a_format_that_is_not_a_string_must_be_remember_plugin);
    RUN_TEST(an_unknown_field_on_run_is_plugin_manifest_invalid_with_a_json_pointer);
    RUN_TEST(an_unknown_field_one_edit_from_a_known_one_gets_a_did_you_mean_fix);
    RUN_TEST(the_did_you_mean_fix_covers_a_substitution_and_an_extra_letter);
    RUN_TEST(a_field_two_edits_away_gets_the_remove_fix);
    RUN_TEST(a_same_length_name_two_substitutions_away_is_not_a_did_you_mean);
    RUN_TEST(an_unknown_field_on_permissions_is_invalid);
    RUN_TEST(an_unknown_field_in_an_action_filter_block_is_invalid);
    RUN_TEST(a_filter_run_timeout_is_an_unknown_closed_field);
    RUN_TEST(top_level_run_permissions_and_filter_are_errors_not_warnings);
    RUN_TEST(an_unknown_root_field_is_a_warning_and_the_rest_parses);
#ifdef REMEMBER_PLUGINS_DIAG_TEST
    RUN_TEST(an_unknown_root_field_prints_the_c35_file_line_and_the_rest_parses);
    RUN_TEST(the_c35_line_escapes_a_hostile_unknown_root_field);
#endif
    RUN_TEST(working_directory_is_an_unknown_action_field);
    RUN_TEST(an_unknown_field_in_open_tags_is_a_warning);
    RUN_TEST(an_unknown_field_in_a_setting_range_is_a_warning);
    RUN_TEST(unknown_field_pointer_escapes_slash_and_tilde);
    RUN_TEST(a_long_unknown_field_keeps_its_whole_name_and_pointer);
    RUN_TEST(a_long_unknown_open_field_is_kept_whole_in_the_warning);
    RUN_TEST(a_repeated_protocol_is_refused);
    RUN_TEST(repeated_permissions_are_refused);
    RUN_TEST(a_key_repeated_a_third_time_is_reported_again);
    RUN_TEST(a_repeated_key_with_a_slash_has_an_escaped_pointer);
    RUN_TEST(a_hostile_object_with_thousands_of_keys_still_parses_fast);
    RUN_TEST(two_errors_in_one_file_both_come_back_from_one_parse);
    RUN_TEST(a_missing_name_does_not_hide_the_errors_after_it);
    RUN_TEST(every_error_comes_back_past_sixteen);
    RUN_TEST(budget_0ms_and_31s_are_invalid_and_1ms_and_30s_parse);
    RUN_TEST(grace_0ms_is_invalid_1ms_and_30s_parse_and_31s_is_invalid);
    RUN_TEST(a_budget_that_is_not_a_duration_names_the_same_range);
    RUN_TEST(priority_runs_from_0_to_1000);
    RUN_TEST(a_filter_point_must_be_search_query_or_search_results);
    RUN_TEST(a_filter_must_declare_process_persistent);
    RUN_TEST(an_action_process_is_once_or_persistent);
    RUN_TEST(an_action_timeout_is_a_duration);
    RUN_TEST(a_run_section_is_required_and_must_be_an_object);
    RUN_TEST(a_command_is_required_and_is_a_short_list_of_nonempty_strings);
    RUN_TEST(a_command_argument_of_255_bytes_fits_and_256_does_not);
    RUN_TEST(a_relative_command_with_a_dotdot_segment_is_refused);
    RUN_TEST(an_absolute_command_and_dotted_names_that_are_not_dotdot_are_allowed);
    RUN_TEST(only_the_program_path_is_checked_for_dotdot_not_its_arguments);
    RUN_TEST(events_are_a_short_list_of_known_event_types);
    RUN_TEST(an_unknown_event_type_is_named_with_its_index);
    RUN_TEST(memory_star_must_be_the_only_event);
    RUN_TEST(an_action_filter_block_keeps_every_list);
    RUN_TEST(a_filter_block_that_is_not_an_object_must_be_an_object);
    RUN_TEST(tags_that_are_not_an_object_must_be_an_object);
    RUN_TEST(match_before_must_be_true_or_false);
    RUN_TEST(a_tag_list_holds_at_most_eight_strings_of_63_bytes);
    RUN_TEST(actor_kinds_are_the_five_the_schema_lists);
    RUN_TEST(bins_origins_and_not_caused_by_are_checked);
    RUN_TEST(all_four_bins_fit_in_the_bins_list);
    RUN_TEST(needs_accept_memory_fields_and_remember_setting_keys);
    RUN_TEST(a_need_that_is_no_field_and_no_global_setting_is_refused);
    RUN_TEST(a_global_setting_key_of_96_characters_fits_and_97_does_not);
    RUN_TEST(a_need_listed_twice_is_refused);
    RUN_TEST(a_needs_list_has_at_most_sixteen_items);
    RUN_TEST(needs_that_is_not_a_list_and_permissions_that_is_not_an_object_are_refused);
    RUN_TEST(two_persistent_entries_with_one_command_parse);
    RUN_TEST(a_second_persistent_command_points_at_its_own_entry);
    RUN_TEST(a_second_persistent_action_with_another_command_points_at_that_action);
    RUN_TEST(persistent_commands_that_differ_in_one_argument_differ);
    RUN_TEST(backfill_needs_a_persistent_entry);
    RUN_TEST(backfill_limits_are_100_entries_and_4mib);
    RUN_TEST(backfill_enabled_is_required_and_a_boolean);
    RUN_TEST(actions_and_filters_are_lists_of_one_to_sixteen_entries);
    RUN_TEST(sixteen_actions_and_sixteen_filters_parse_together);
    RUN_TEST(an_entry_that_is_not_an_object_must_be_an_object);
    RUN_TEST(an_entry_with_no_name_has_a_missing_name);
    RUN_TEST(a_description_counts_characters_so_150_hebrew_letters_fit);
    RUN_TEST(a_description_of_200_characters_fits_and_201_does_not);
    RUN_TEST(a_description_of_800_bytes_in_one_hundred_characters_beyond_the_buffer_is_refused);
    RUN_TEST(plugin_version_is_a_semver);
    RUN_TEST(data_version_and_idle_timeout_defaults_and_limits);
    RUN_TEST(a_duration_that_the_schema_refuses_is_refused_here_too);
    RUN_TEST(a_size_the_schema_refuses_is_refused_here_too);
    RUN_TEST(every_setting_type_parses_with_its_default);
    RUN_TEST(a_string_list_default_keeps_its_strings_as_source_json);
    RUN_TEST(a_string_list_default_must_be_a_list_of_strings);
    RUN_TEST(a_string_default_of_511_bytes_fits_and_512_does_not);
    RUN_TEST(a_string_list_default_of_512_source_bytes_is_refused);
    RUN_TEST(a_default_of_the_wrong_type_names_what_the_type_needs);
    RUN_TEST(a_setting_needs_key_description_type_and_default);
    RUN_TEST(a_setting_type_outside_the_seven_is_refused);
    RUN_TEST(a_setting_that_is_not_an_object_and_a_settings_that_is_not_a_list_are_refused);
    RUN_TEST(a_manifest_has_at_most_sixteen_settings);
    RUN_TEST(a_setting_description_is_a_string_of_at_most_200_characters);
    RUN_TEST(a_setting_scope_defaults_to_user_and_reads_user_brain_and_both);
    RUN_TEST(a_setting_scope_outside_the_three_is_refused);
    RUN_TEST(an_enum_needs_a_range_with_values_and_a_default_in_them);
    RUN_TEST(an_empty_enum_values_list_is_a_list_and_the_default_is_not_in_it);
    RUN_TEST(enum_values_are_at_most_eight_strings_of_63_bytes);
    RUN_TEST(a_range_that_is_not_an_object_is_refused);
    RUN_TEST(an_integer_range_checks_its_default_and_its_order);
    RUN_TEST(a_range_with_only_min_or_only_max_parses);
    RUN_TEST(a_duration_range_reads_milliseconds_and_a_size_range_reads_bytes);
    RUN_TEST(a_range_bound_of_the_wrong_type_names_what_the_type_needs);
    RUN_TEST(a_range_on_a_boolean_or_a_string_is_ignored);
    RUN_TEST(string_list_max_items_is_a_whole_number_of_one_or_more);
    RUN_TEST(an_invalid_setting_type_skips_its_default_and_range_checks_without_a_second_error);
    RUN_TEST(a_setting_key_must_be_a_name);
    RUN_TEST(nesting_past_64_levels_has_its_own_sentence);
    RUN_TEST(an_unfinished_deep_nest_is_still_not_valid_json);
    RUN_TEST(invalid_utf8_is_a_syntax_error);
    RUN_TEST(shell_metacharacters_in_a_command_are_inert_text_in_an_argv);
    RUN_TEST(a_control_byte_in_an_unknown_field_name_is_kept_for_the_printer_to_mask);
    RUN_TEST(plugin_manifest_survives_each_allocation_failure);
    RUN_TEST(a_failed_allocation_in_a_syntax_error_is_out_of_memory);
    RUN_TEST(l2_an_action_lists_at_most_eight_events);
    RUN_TEST(l3_a_command_has_at_most_eight_strings);
    RUN_TEST(l5_permissions_needs_has_at_most_sixteen_items);
    RUN_TEST(l6_tags_any_holds_eight_strings);
    RUN_TEST(l6_tags_all_holds_eight_strings);
    RUN_TEST(l6_a_tag_holds_63_bytes);
    RUN_TEST(l7_created_by_and_updated_by_hold_five_kinds);
    RUN_TEST(l7_bins_hold_four);
    RUN_TEST(l7_origins_hold_three);
    RUN_TEST(l8_not_caused_by_holds_eight_strings_of_79_bytes);
    RUN_TEST(l9_a_manifest_has_at_most_sixteen_settings);
    RUN_TEST(l10_an_enum_has_at_most_eight_values_of_63_bytes);
    RUN_TEST(l11_a_string_list_default_holds_511_bytes_of_source_json);
    RUN_TEST(l12_a_plugin_version_holds_63_bytes);
    RUN_TEST(l13_a_duration_string_holds_23_bytes);
    RUN_TEST(q2_an_unknown_field_name_prints_escaped_in_the_message_and_the_fix);
    RUN_TEST(q2_an_escape_a_delete_and_a_c1_control_print_as_u_escapes);
    RUN_TEST(q2_text_that_is_not_a_control_prints_as_written);
    RUN_TEST(q2_an_invalid_name_and_a_hyphen_name_print_escaped);
    RUN_TEST(q2_the_plugin_name_in_a_protocol_error_prints_escaped);
    RUN_TEST(q2_a_repeated_key_prints_escaped);
    RUN_TEST(q3_a_long_duration_or_size_uses_the_ordinary_phrase);
    RUN_TEST(two_actions_with_one_name_are_refused);
    RUN_TEST(two_filters_with_one_name_are_refused);
    RUN_TEST(two_settings_with_one_key_are_refused);
    RUN_TEST(an_action_and_a_filter_may_share_a_name);
    RUN_TEST(a_third_use_of_a_name_is_reported_again);
    RUN_TEST(an_invalid_name_is_not_also_reported_as_a_repeat);
    RUN_TEST(each_persistent_entry_that_differs_gets_its_own_error);
    RUN_TEST(a_plugin_version_that_is_not_a_string_must_be_a_version);
    RUN_TEST(q1_a_nineteen_digit_protocol_prints_its_digits_and_not_a_saturated_value);
    RUN_TEST(q1_a_whole_number_of_64_digits_is_printed_and_one_of_65_is_not_whole);
    RUN_TEST(q2_a_unit_separator_escapes_and_a_space_does_not);
    RUN_TEST(a_min_one_above_max_is_refused);
    RUN_TEST(a_repeated_key_is_the_later_one_and_errors_come_in_document_order);
    RUN_TEST(q1_a_65_digit_integer_is_not_a_whole_number_and_a_64_digit_one_is_printed);
    RUN_TEST(a_data_version_past_32_bits_parses);
    RUN_TEST(q1_zeros_after_the_point_are_not_digits);
    RUN_TEST(backfill_with_a_filter_whose_process_is_missing_names_only_the_process);
    RUN_TEST(every_bad_command_item_gets_its_own_error);
    RUN_TEST(q1_a_19_digit_whole_number_that_fits_is_read);
    RUN_TEST(an_enum_default_that_is_not_a_string_uses_the_enum_phrase);
    RUN_TEST(a_repeated_top_level_run_is_misplaced_once_and_repeated_once);
    RUN_TEST(errors_come_in_document_order_across_the_file);
    RUN_TEST(a_repeated_key_inside_an_unknown_member_is_an_error);
    RUN_TEST(memory_star_listed_twice_is_still_alone);
    RUN_TEST(q1_an_exponent_past_a_long_long_is_not_a_whole_number);
    RUN_TEST(q2_a_pointer_in_a_message_prints_escaped);
    RUN_TEST(q1_every_whole_field_reads_1_0_1e0_and_minus_0_by_value);
    RUN_TEST(q1_protocol_2_0_prints_2_in_the_no_name_sentence_too);
    RUN_TEST(q3_24_bytes_is_the_ordinary_phrase_and_23_bytes_is_read);
    RUN_TEST(a_persistent_entry_with_a_broken_command_gets_only_its_command_error);
    RUN_TEST(backfill_with_a_persistent_entry_whose_command_is_broken_names_only_the_command);
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
