#ifdef REMEMBER_PLUGINS_DIAG_TEST
#include "appio.h"
#include "cli.h"
#include "diag.h"
#include "exit_codes.h"
#endif
#include "fileio.h"
#include "harness.h"
#include "plugin_settings.h"
#include "plugin_trust.h"
#include "register.h"
#include "remember_env.h"
#include "test.h"
#include "test_alloc.h"

#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

/* Plan 33 (specs/PLUGIN-TRUST.md). Decisions: decision:2026-11-12-trust-file,
   decision:2026-11-12-plugin-approval-required-exit-5,
   decision:2026-11-12-hook-approval-required-exit-5, decision:2026-11-22-plan33-folder-hash,
   -hash-caps, -many-entries, -argv-paths, -enable-guard-error, -import-not-guarded,
   -trust-json-unreadable, -trust-lock. */
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

enum { PATH_BUF = 1024, FIXED_MTIME = 1700000000, BODY_BUF = 2048 };

#define MANIFEST_TEXT "{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"wake\"}"
#define RUN_SH_TEXT "#!/bin/sh\necho wake\n"
#define TOOL_SH_TEXT "echo tool\n"

/* What the fake host saw and what it answers. */
typedef struct HostLog {
    int calls;
    bool answer;
    char pad_[3]; /* explicit tail padding (kept -Wpadded-clean) */
    char kind[16];
    char name[64];
    char body[BODY_BUF];
} HostLog;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the callback's own three strings
static bool host_confirm(void *ctx, const char *kind, const char *name, const char *body)
{
    HostLog *log = (HostLog *)ctx;

    log->calls++;
    (void)snprintf(log->kind, sizeof(log->kind), "%s", kind);
    (void)snprintf(log->name, sizeof(log->name), "%s", name);
    (void)snprintf(log->body, sizeof(log->body), "%s", body);
    return log->answer;
}

static RememberEnv given_host(HostLog *log, bool answer)
{
    RememberEnv env;

    memset(log, 0, sizeof(*log));
    log->answer = answer;
    memset(&env, 0, sizeof(env));
    env.confirm = host_confirm;
    env.callback_ctx = log;
    return env;
}

/* A fresh data dir (heap), in the realpath form: a plugin's files are recorded by realpath. */
static char *given_data_dir(void)
{
    char *db = make_temp_db_path();
    char *root = db != NULL ? dir_of_path(db) : NULL;
    char *real = root != NULL ? realpath(root, NULL) : NULL;

    ASSERT_TRUE(real != NULL);
    free(db);
    free(root);
    return real;
}

static void path_of(char *out, size_t cap, const char *dir, const char *rel)
{
    int n = snprintf(out, cap, "%s/%s", dir, rel);

    ASSERT_TRUE(n > 0 && (size_t)n < cap);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) folder, relative path, then the text
static void given_file(const char *dir, const char *rel, const char *text, mode_t mode)
{
    char path[PATH_BUF];
    struct timeval times[2] = {{FIXED_MTIME, 0}, {FIXED_MTIME, 0}};

    path_of(path, sizeof(path), dir, rel);
    harness_write_file(path, text);
    ASSERT_EQ_INT(chmod(path, mode), 0);
    ASSERT_EQ_INT(utimes(path, times), 0);
}

/* <data>/plugins/wake with plugin.json and run.sh, and <data>/tools/tool.sh outside it. */
static void given_wake(const char *data)
{
    given_file(data, "plugins/wake/plugin.json", MANIFEST_TEXT, 0644);
    given_file(data, "plugins/wake/run.sh", RUN_SH_TEXT, 0755);
    given_file(data, "tools/tool.sh", TOOL_SH_TEXT, 0755);
}

/* The subject of wake: one command, <data>/tools/tool.sh --flag run.sh. */
typedef struct WakeSubject {
    char folder[PATH_BUF];
    char tool[PATH_BUF];
    const char *argv[3];
    PluginTrustCommand command;
    const char *needs[1];
    PluginTrustSubject subject;
} WakeSubject;

static void given_wake_subject(WakeSubject *w, const char *data)
{
    memset(w, 0, sizeof(*w));
    path_of(w->folder, sizeof(w->folder), data, "plugins/wake");
    path_of(w->tool, sizeof(w->tool), data, "tools/tool.sh");
    w->argv[0] = w->tool;
    w->argv[1] = "--flag";
    w->argv[2] = "run.sh";
    w->command = (PluginTrustCommand){.argv = w->argv, .argc = 3U};
    w->needs[0] = "memories:read";
    w->subject = (PluginTrustSubject){.name = "wake",
                                      .folder = w->folder,
                                      .manifest = MANIFEST_TEXT,
                                      .manifest_len = sizeof(MANIFEST_TEXT) - 1U,
                                      .commands = &w->command,
                                      .ncommands = 1U,
                                      .needs = w->needs,
                                      .nneeds = 1U,
                                      .kind = PLUGIN_TRUST_KIND_PLUGIN};
}

static char *trust_text(const char *data)
{
    char path[PATH_BUF];
    size_t len = 0;

    path_of(path, sizeof(path), data, "trust.json");
    return harness_read_file(path, &len);
}

TEST(record_is_data_dir_trust_json)
{
    char *data = given_data_dir();
    WakeSubject w;
    HostLog log;
    RememberEnv env = given_host(&log, true);
    char path[PATH_BUF];
    struct stat st;
    char *text = NULL;

    given_wake(data);
    given_wake_subject(&w, data);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, "2026-11-22T10:00:00.000Z"),
                     PLUGIN_TRUST_OK);
    path_of(path, sizeof(path), data, "trust.json");
    ASSERT_EQ_INT(stat(path, &st), 0);
    ASSERT_EQ_INT((unsigned)st.st_mode & (unsigned)PERM_BITS_MASK, DB_FILE_PERMS);
    text = trust_text(data);
    ASSERT_TRUE(text != NULL);
    ASSERT_STR_PREFIX(text, "{\"format\":\"remember-trust\",\"version\":1,\"entries\":[{\"kind\":"
                            "\"plugin\",\"name\":\"wake\",");
    path_of(path, sizeof(path), data, "plugins/trust.json");
    ASSERT_FALSE(access(path, F_OK) == 0);
    path_of(path, sizeof(path), data, "plugins/wake/trust.json");
    ASSERT_FALSE(access(path, F_OK) == 0);
    free(text);
    harness_remove_tree(data);
    free(data);
}

/* The hashes are computed apart from the code (python hashlib over the fixture bytes): the folder
   hash is the records 'F', 'x' or '-', the content hash, the relative path and a NUL, in path
   order. */
#define WAKE_FOLDER_SHA "9be228b364c6e361c551f581564c7929661d20580c28c3881714972e4ee12313"
#define WAKE_MANIFEST_SHA "13a956f770920ac9b26b581f604c8d3550458fefcb698395f4efa14e586a2f6c"
#define TOOL_SHA "dbdf94a50c89a7810193760766fc0892bfd02e687488dd3ef4ee83d7da6d3f33"
#define NOW "2026-11-22T10:00:00.000Z"

TEST(record_fields_h_9_2)
{
    char *data = given_data_dir();
    WakeSubject w;
    HostLog log;
    RememberEnv env = given_host(&log, true);
    char want[BODY_BUF];
    char *text = NULL;

    given_wake(data);
    given_wake_subject(&w, data);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_OK);
    (void)snprintf(
        want, sizeof(want),
        "{\"format\":\"remember-trust\",\"version\":1,\"entries\":[{\"kind\":\"plugin\","
        "\"name\":\"wake\",\"path\":\"%s/plugins/wake\",\"folder_sha256\":\"" WAKE_FOLDER_SHA
        "\",\"manifest_sha256\":\"" WAKE_MANIFEST_SHA "\",\"files\":[{\"path\":\"%s/tools/"
        "tool.sh\",\"sha256\":\"" TOOL_SHA "\",\"size\":10,\"mtime\":1700000000}],"
        "\"interpreter\":null,\"needs\":[\"memories:read\"],\"trusted_at\":\"" NOW
        "\",\"dev\":false}]}\n",
        data, data);
    text = trust_text(data);
    ASSERT_TRUE(text != NULL);
    ASSERT_STREQ(text, want);
    free(text);
    harness_remove_tree(data);
    free(data);
}

/* A subject of wake with up to two commands. */
typedef struct TwoCommands {
    PluginTrustCommand commands[2];
    PluginTrustSubject subject;
    char folder[PATH_BUF];
} TwoCommands;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the two argvs, then their lengths
static void given_commands(TwoCommands *two, const char *data, const char *const *argv_a,
                           size_t argc_a, const char *const *argv_b, size_t argc_b)
{
    memset(two, 0, sizeof(*two));
    path_of(two->folder, sizeof(two->folder), data, "plugins/wake");
    two->commands[0] = (PluginTrustCommand){.argv = argv_a, .argc = argc_a};
    two->commands[1] = (PluginTrustCommand){.argv = argv_b, .argc = argc_b};
    two->subject = (PluginTrustSubject){.name = "wake",
                                        .folder = two->folder,
                                        .manifest = MANIFEST_TEXT,
                                        .manifest_len = sizeof(MANIFEST_TEXT) - 1U,
                                        .commands = two->commands,
                                        .ncommands = argv_b != NULL ? 2U : 1U,
                                        .kind = PLUGIN_TRUST_KIND_PLUGIN};
}

/* A plugin whose one command is argv, allowed with a yes. The status of the allow. */
static PluginTrustStatus allow_command(const char *data, const char *const *argv, size_t argc,
                                       const char *search_path)
{
    TwoCommands two;
    HostLog log;
    RememberEnv env = given_host(&log, true);

    given_commands(&two, data, argv, argc, NULL, 0U);
    two.subject.search_path = search_path;
    return plugin_trust_allow(data, &env, &two.subject, NOW);
}

/* G-29: command[0] and every later item that names an existing regular file, at most 8, in argv
   order. Folder files are not counted: the folder hash covers them. */
TEST(hash_argv_files_max_8)
{
    char *data = given_data_dir();
    char names[9][PATH_BUF];
    const char *argv[10];
    char *text = NULL;
    size_t i = 0;
    size_t at = 0;

    given_wake(data);
    for (i = 0; i < 9U; i++) {
        char rel[32];

        (void)snprintf(rel, sizeof(rel), "tools/f%zu", i);
        given_file(data, rel, "x\n", 0644);
        path_of(names[i], sizeof(names[i]), data, rel);
        argv[i] = names[i];
    }
    argv[9] = names[0];
    ASSERT_EQ_STATUS(allow_command(data, argv, 9U, NULL), PLUGIN_TRUST_UNHASHABLE);
    ASSERT_EQ_STATUS(allow_command(data, argv, 8U, NULL), PLUGIN_TRUST_OK);
    text = trust_text(data);
    ASSERT_TRUE(text != NULL);
    for (i = 0; i < 8U; i++) {
        const char *found = strstr(text + at, names[i]);

        ASSERT_TRUE(found != NULL);
        at = found != NULL ? (size_t)(found - text) : 0U;
    }
    ASSERT_STR_NOT_CONTAINS(text, names[8]);
    free(text);
    harness_remove_tree(data);
    free(data);
}

TEST(a_file_named_twice_and_a_file_in_the_folder_do_not_count_toward_the_8)
{
    char *data = given_data_dir();
    char names[8][PATH_BUF];
    const char *argv[11];
    size_t i = 0;

    given_wake(data);
    for (i = 0; i < 8U; i++) {
        char rel[32];

        (void)snprintf(rel, sizeof(rel), "tools/f%zu", i);
        given_file(data, rel, "x\n", 0644);
        path_of(names[i], sizeof(names[i]), data, rel);
        argv[i] = names[i];
    }
    argv[8] = names[3];
    argv[9] = "run.sh";
    argv[10] = "not a file";
    ASSERT_EQ_STATUS(allow_command(data, argv, 8U, NULL), PLUGIN_TRUST_OK);
    harness_remove_tree(data);
    free(data);
}

/* A file the folder does not hold and the filesystem has, named in a later argv item, is hashed;
   a folder or a missing path is text. */
TEST(a_later_argv_item_that_is_a_folder_or_missing_is_text)
{
    char *data = given_data_dir();
    char tools[PATH_BUF];
    const char *argv[3];

    given_wake(data);
    path_of(tools, sizeof(tools), data, "tools");
    argv[0] = tools;
    argv[1] = "/nonexistent/for/sure";
    argv[2] = "";
    ASSERT_EQ_STATUS(allow_command(data, argv, 3U, NULL), PLUGIN_TRUST_OK);
    harness_remove_tree(data);
    free(data);
}

#define FAKE_PY_SHA "8b69a36ab4a206f8be78a85ffe8717cb20f5b4e7f299f8c714e36b42f1f59729"

/* H-9.1: command[0] named sh, bash, zsh, python, python3, node, ruby, perl, deno or bun. */
TEST(interpreter_hashed)
{
    char *data = given_data_dir();
    char fake_py[PATH_BUF];
    char want[PATH_BUF];
    const char *argv[2];
    char *text = NULL;

    given_wake(data);
    given_file(data, "bin/python3", "#!/bin/sh\necho py\n", 0755);
    path_of(fake_py, sizeof(fake_py), data, "bin/python3");
    argv[0] = fake_py;
    argv[1] = "run.sh";
    ASSERT_EQ_STATUS(allow_command(data, argv, 2U, NULL), PLUGIN_TRUST_OK);
    (void)snprintf(want, sizeof(want),
                   "\"interpreter\":{\"path\":\"%s\",\"sha256\":\"" FAKE_PY_SHA "\"}", fake_py);
    text = trust_text(data);
    ASSERT_STR_CONTAINS(text, want);
    free(text);
    harness_remove_tree(data);
    free(data);
}

TEST(interpreter_after_usr_bin_env_is_found_in_the_search_path)
{
    char *data = given_data_dir();
    char bin[PATH_BUF];
    char search[2 * PATH_BUF];
    char want[PATH_BUF];
    const char *argv[3] = {"/usr/bin/env", "python3", "run.sh"};
    char *text = NULL;

    given_wake(data);
    given_file(data, "bin/python3", "#!/bin/sh\necho py\n", 0755);
    given_file(data, "later/python3", "#!/bin/sh\necho py2\n", 0755);
    given_file(data, "plain/python3", "#!/bin/sh\necho py2\n", 0644);
    path_of(bin, sizeof(bin), data, "bin");
    /* An empty entry and a file that is not executable come first and are passed over. */
    (void)snprintf(search, sizeof(search), "::%s/plain:%s:%s/later", data, bin, data);
    ASSERT_EQ_STATUS(allow_command(data, argv, 3U, search), PLUGIN_TRUST_OK);
    (void)snprintf(want, sizeof(want),
                   "\"interpreter\":{\"path\":\"%s/python3\",\"sha256\":\"" FAKE_PY_SHA "\"}", bin);
    text = trust_text(data);
    ASSERT_STR_CONTAINS(text, want);
    free(text);
    harness_remove_tree(data);
    free(data);
}

TEST(an_env_interpreter_that_the_search_path_does_not_find_is_not_recorded)
{
    char *data = given_data_dir();
    const char *argv[3] = {"/usr/bin/env", "python3", "run.sh"};
    char *text = NULL;

    given_wake(data);
    ASSERT_EQ_STATUS(allow_command(data, argv, 3U, NULL), PLUGIN_TRUST_OK);
    text = trust_text(data);
    ASSERT_STR_CONTAINS(text, "\"interpreter\":null");
    free(text);
    harness_remove_tree(data);
    free(data);
}

TEST(env_followed_by_a_name_that_is_not_an_interpreter_records_none)
{
    char *data = given_data_dir();
    char bin[PATH_BUF];
    const char *argv[3] = {"/usr/bin/env", "make", "run.sh"};
    char *text = NULL;

    given_wake(data);
    given_file(data, "bin/make", "x\n", 0755);
    path_of(bin, sizeof(bin), data, "bin");
    ASSERT_EQ_STATUS(allow_command(data, argv, 3U, bin), PLUGIN_TRUST_OK);
    text = trust_text(data);
    ASSERT_STR_CONTAINS(text, "\"interpreter\":null");
    free(text);
    harness_remove_tree(data);
    free(data);
}

TEST(two_different_interpreters_in_one_plugin_are_unhashable)
{
    char *data = given_data_dir();
    TwoCommands two;
    HostLog log;
    RememberEnv env = given_host(&log, true);
    char fake_py[PATH_BUF];
    char node[PATH_BUF];
    const char *argv_a[2];
    const char *argv_b[2];

    given_wake(data);
    given_file(data, "bin/python3", "#!/bin/sh\necho py\n", 0755);
    given_file(data, "bin/node", "#!/bin/sh\necho py2\n", 0755);
    path_of(fake_py, sizeof(fake_py), data, "bin/python3");
    path_of(node, sizeof(node), data, "bin/node");
    argv_a[0] = fake_py;
    argv_a[1] = "run.sh";
    argv_b[0] = node;
    argv_b[1] = "run.sh";
    given_commands(&two, data, argv_a, 2U, argv_b, 2U);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &two.subject, NOW), PLUGIN_TRUST_UNHASHABLE);
    given_commands(&two, data, argv_a, 2U, argv_a, 2U);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &two.subject, NOW), PLUGIN_TRUST_OK);
    harness_remove_tree(data);
    free(data);
}

/* H-9.1 (HK 3B.2): for sh -c "<inline>" the inline text is part of the manifest hash. The
   plugin.json bytes stay the same here, so only the argv can move the hash. */
TEST(sh_c_inline_in_manifest_hash)
{
    char *data = given_data_dir();
    char first[2 * PATH_BUF];
    char second[2 * PATH_BUF];
    const char *argv_a[3] = {"/bin/sh", "-c", "echo one"};
    const char *argv_b[3] = {"/bin/sh", "-c", "echo two"};
    const char *argv_c[3] = {"/bin/sh", "-x", "echo one"};
    char *text = NULL;

    given_wake(data);
    ASSERT_EQ_STATUS(allow_command(data, argv_a, 3U, NULL), PLUGIN_TRUST_OK);
    text = trust_text(data);
    (void)snprintf(first, sizeof(first), "%s", strstr(text, "\"manifest_sha256\""));
    first[sizeof("\"manifest_sha256\":\"") + 64] = '\0';
    free(text);
    ASSERT_EQ_STATUS(allow_command(data, argv_b, 3U, NULL), PLUGIN_TRUST_OK);
    text = trust_text(data);
    (void)snprintf(second, sizeof(second), "%s", strstr(text, "\"manifest_sha256\""));
    second[sizeof("\"manifest_sha256\":\"") + 64] = '\0';
    free(text);
    ASSERT_STR_NOT_CONTAINS(second, first + sizeof("\"manifest_sha256\":\"") - 1);
    ASSERT_STR_NOT_CONTAINS(first, WAKE_MANIFEST_SHA);
    ASSERT_EQ_STATUS(allow_command(data, argv_c, 3U, NULL), PLUGIN_TRUST_OK);
    text = trust_text(data);
    ASSERT_STR_CONTAINS(text, "\"manifest_sha256\":\"" WAKE_MANIFEST_SHA "\"");
    free(text);
    harness_remove_tree(data);
    free(data);
}

static bool exists_at(const char *dir, const char *rel)
{
    char path[PATH_BUF];

    path_of(path, sizeof(path), dir, rel);
    return access(path, F_OK) == 0;
}

/* The hook "ping": a folder with plugin.json and the command <data>/tools/hook.sh. */
typedef struct PingSubject {
    char folder[PATH_BUF];
    char command_path[PATH_BUF];
    const char *argv[2];
    PluginTrustCommand command;
    PluginTrustSubject subject;
} PingSubject;

static void given_ping(PingSubject *ping, const char *data)
{
    given_file(data, "plugins/ping/plugin.json", MANIFEST_TEXT, 0644);
    given_file(data, "tools/hook.sh", TOOL_SH_TEXT, 0755);
    memset(ping, 0, sizeof(*ping));
    path_of(ping->folder, sizeof(ping->folder), data, "plugins/ping");
    path_of(ping->command_path, sizeof(ping->command_path), data, "tools/hook.sh");
    ping->argv[0] = ping->command_path;
    ping->argv[1] = "--on-write";
    ping->command = (PluginTrustCommand){.argv = ping->argv, .argc = 2U};
    ping->subject = (PluginTrustSubject){.name = "ping",
                                         .folder = ping->folder,
                                         .manifest = MANIFEST_TEXT,
                                         .manifest_len = sizeof(MANIFEST_TEXT) - 1U,
                                         .commands = &ping->command,
                                         .ncommands = 1U,
                                         .kind = PLUGIN_TRUST_KIND_HOOK};
}

/* decision:2026-11-12-plugin-approval-required-exit-5: no callback records nothing. */
TEST(null_confirm_exits_5_c15_records_nothing)
{
    char *data = given_data_dir();
    WakeSubject w;
    RememberEnv no_callback;

    memset(&no_callback, 0, sizeof(no_callback));
    given_wake(data);
    given_wake_subject(&w, data);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &no_callback, &w.subject, NOW),
                     PLUGIN_TRUST_NO_CALLBACK);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, NULL, &w.subject, NOW), PLUGIN_TRUST_NO_CALLBACK);
    ASSERT_FALSE(exists_at(data, "trust.json"));
    harness_remove_tree(data);
    free(data);
}

/* decision:2026-11-12-hook-approval-required-exit-5. */
TEST(null_confirm_hook_exits_5_records_nothing)
{
    char *data = given_data_dir();
    PingSubject ping;
    RememberEnv no_callback;

    memset(&no_callback, 0, sizeof(no_callback));
    given_ping(&ping, data);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &no_callback, &ping.subject, NOW),
                     PLUGIN_TRUST_NO_CALLBACK);
    ASSERT_FALSE(exists_at(data, "trust.json"));
    harness_remove_tree(data);
    free(data);
}

TEST(false_confirm_exits_1_c16_records_nothing)
{
    char *data = given_data_dir();
    WakeSubject w;
    HostLog log;
    RememberEnv env = given_host(&log, false);

    given_wake(data);
    given_wake_subject(&w, data);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_DENIED);
    ASSERT_EQ_INT(log.calls, 1);
    ASSERT_FALSE(exists_at(data, "trust.json"));
    harness_remove_tree(data);
    free(data);
}

TEST(a_denied_confirm_leaves_an_existing_trust_json_as_it_was)
{
    char *data = given_data_dir();
    WakeSubject w;
    HostLog log;
    RememberEnv yes = given_host(&log, true);
    char *before = NULL;
    char *after = NULL;

    given_wake(data);
    given_wake_subject(&w, data);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &yes, &w.subject, NOW), PLUGIN_TRUST_OK);
    before = trust_text(data);
    given_file(data, "plugins/wake/run.sh", "#!/bin/sh\necho changed\n", 0755);
    yes = given_host(&log, false);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &yes, &w.subject, "2030-01-01T00:00:00.000Z"),
                     PLUGIN_TRUST_DENIED);
    after = trust_text(data);
    ASSERT_STREQ(before, after);
    free(before);
    free(after);
    harness_remove_tree(data);
    free(data);
}

/* H-9.4: the host gets the kind, the name and the body. */
TEST(the_host_gets_the_plugin_kind_name_and_folder_body)
{
    char *data = given_data_dir();
    WakeSubject w;
    HostLog log;
    RememberEnv env = given_host(&log, true);
    const char *needs[2] = {"memories:read", "links"};

    given_wake(data);
    given_wake_subject(&w, data);
    w.subject.needs = needs;
    w.subject.nneeds = 2U;
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_OK);
    ASSERT_EQ_INT(log.calls, 1);
    ASSERT_STREQ(log.kind, "plugin");
    ASSERT_STREQ(log.name, "wake");
    ASSERT_STREQ(log.body, "Allow \"wake\" to read your memories? It can send them off this Mac.\n"
                           "memories:read\nlinks");
    harness_remove_tree(data);
    free(data);
}

TEST(the_host_gets_the_hook_kind_name_and_hook_body)
{
    char *data = given_data_dir();
    PingSubject ping;
    HostLog log;
    RememberEnv env = given_host(&log, true);
    char want[BODY_BUF];

    given_ping(&ping, data);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &ping.subject, NOW), PLUGIN_TRUST_OK);
    (void)snprintf(want, sizeof(want), "Allow hook \"ping\" to run %s?", ping.command_path);
    ASSERT_STREQ(log.kind, "hook");
    ASSERT_STREQ(log.name, "ping");
    ASSERT_STREQ(log.body, want);
    harness_remove_tree(data);
    free(data);
}

/* H-9.6: an update shows only the changed needs. */
TEST(an_update_shows_only_the_needs_the_old_record_lacks)
{
    char *data = given_data_dir();
    WakeSubject w;
    HostLog log;
    RememberEnv env = given_host(&log, true);
    const char *first[2] = {"memories:read", "links"};
    const char *second[3] = {"memories:read", "links", "tags"};

    given_wake(data);
    given_wake_subject(&w, data);
    w.subject.needs = first;
    w.subject.nneeds = 2U;
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_OK);
    w.subject.needs = second;
    w.subject.nneeds = 3U;
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_OK);
    ASSERT_EQ_INT(log.calls, 2);
    ASSERT_STREQ(log.body, "Allow \"wake\" to read your memories? It can send them off this Mac.\n"
                           "tags");
    harness_remove_tree(data);
    free(data);
}

TEST(no_env_confirm_is_never_a_yes)
{
    RememberEnv env;

    memset(&env, 0, sizeof(env));
    ASSERT_EQ_STATUS(plugin_trust_confirm(&env, PLUGIN_TRUST_KIND_PLUGIN, "wake", "body"),
                     PLUGIN_TRUST_NO_CALLBACK);
    ASSERT_EQ_STATUS(plugin_trust_confirm(NULL, PLUGIN_TRUST_KIND_HOOK, "wake", "body"),
                     PLUGIN_TRUST_NO_CALLBACK);
}

/* Allowed wake: <data> holds the plugin, its tool, and a trust.json that says yes to them. */
static void given_trusted_wake(const char *data, WakeSubject *w)
{
    HostLog log;
    RememberEnv env = given_host(&log, true);

    given_wake(data);
    given_wake_subject(w, data);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w->subject, NOW), PLUGIN_TRUST_OK);
}

TEST(check_is_ok_for_the_code_that_was_allowed)
{
    char *data = given_data_dir();
    WakeSubject w;

    given_trusted_wake(data, &w);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &w.subject), PLUGIN_TRUST_OK);
    harness_remove_tree(data);
    free(data);
}

TEST(check_is_untrusted_when_nothing_was_allowed)
{
    char *data = given_data_dir();
    WakeSubject w;

    given_wake(data);
    given_wake_subject(&w, data);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &w.subject), PLUGIN_TRUST_UNTRUSTED);
    harness_remove_tree(data);
    free(data);
}

TEST(a_record_of_another_name_does_not_trust_this_plugin)
{
    char *data = given_data_dir();
    WakeSubject w;

    given_trusted_wake(data, &w);
    w.subject.name = "other";
    ASSERT_EQ_STATUS(plugin_trust_check(data, &w.subject), PLUGIN_TRUST_UNTRUSTED);
    harness_remove_tree(data);
    free(data);
}

TEST(a_changed_folder_file_is_untrusted)
{
    char *data = given_data_dir();
    WakeSubject w;

    given_trusted_wake(data, &w);
    given_file(data, "plugins/wake/run.sh", "#!/bin/sh\necho changed\n", 0755);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &w.subject), PLUGIN_TRUST_UNTRUSTED);
    harness_remove_tree(data);
    free(data);
}

TEST(a_new_file_in_the_folder_is_untrusted)
{
    char *data = given_data_dir();
    WakeSubject w;

    given_trusted_wake(data, &w);
    given_file(data, "plugins/wake/lib/extra.sh", "echo extra\n", 0644);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &w.subject), PLUGIN_TRUST_UNTRUSTED);
    harness_remove_tree(data);
    free(data);
}

TEST(a_removed_folder_file_is_untrusted)
{
    char *data = given_data_dir();
    WakeSubject w;
    char path[PATH_BUF];

    given_trusted_wake(data, &w);
    path_of(path, sizeof(path), data, "plugins/wake/run.sh");
    ASSERT_EQ_INT(unlink(path), 0);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &w.subject), PLUGIN_TRUST_UNTRUSTED);
    harness_remove_tree(data);
    free(data);
}

TEST(a_changed_executable_bit_is_untrusted)
{
    char *data = given_data_dir();
    WakeSubject w;

    given_trusted_wake(data, &w);
    given_file(data, "plugins/wake/run.sh", RUN_SH_TEXT, 0644);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &w.subject), PLUGIN_TRUST_UNTRUSTED);
    harness_remove_tree(data);
    free(data);
}

TEST(a_changed_manifest_is_untrusted)
{
    char *data = given_data_dir();
    WakeSubject w;

    given_trusted_wake(data, &w);
    given_file(data, "plugins/wake/plugin.json", MANIFEST_TEXT " ", 0644);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &w.subject), PLUGIN_TRUST_UNTRUSTED);
    harness_remove_tree(data);
    free(data);
}

/* The bytes of plugin.json do not change here, so only manifest_sha256 can see the inline text. */
TEST(a_changed_sh_c_inline_text_is_untrusted)
{
    char *data = given_data_dir();
    const char *before[3] = {"/bin/sh", "-c", "echo one"};
    const char *after[3] = {"/bin/sh", "-c", "echo two"};
    TwoCommands two;
    HostLog log;
    RememberEnv env = given_host(&log, true);

    given_wake(data);
    given_commands(&two, data, before, 3U, NULL, 0U);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &two.subject, NOW), PLUGIN_TRUST_OK);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &two.subject), PLUGIN_TRUST_OK);
    given_commands(&two, data, after, 3U, NULL, 0U);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &two.subject), PLUGIN_TRUST_UNTRUSTED);
    harness_remove_tree(data);
    free(data);
}

TEST(a_changed_argv_file_is_untrusted)
{
    char *data = given_data_dir();
    WakeSubject w;

    given_trusted_wake(data, &w);
    given_file(data, "tools/tool.sh", "echo changed\n", 0755);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &w.subject), PLUGIN_TRUST_UNTRUSTED);
    harness_remove_tree(data);
    free(data);
}

TEST(an_argv_item_that_becomes_a_file_is_untrusted)
{
    char *data = given_data_dir();
    char late[PATH_BUF];
    const char *argv[2];
    TwoCommands two;
    HostLog log;
    RememberEnv env = given_host(&log, true);

    given_wake(data);
    path_of(late, sizeof(late), data, "tools/late");
    argv[0] = "run.sh";
    argv[1] = late;
    given_commands(&two, data, argv, 2U, NULL, 0U);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &two.subject, NOW), PLUGIN_TRUST_OK);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &two.subject), PLUGIN_TRUST_OK);
    given_file(data, "tools/late", "x\n", 0644);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &two.subject), PLUGIN_TRUST_UNTRUSTED);
    harness_remove_tree(data);
    free(data);
}

TEST(a_changed_interpreter_is_untrusted)
{
    char *data = given_data_dir();
    char fake_py[PATH_BUF];
    const char *argv[2];
    TwoCommands two;
    HostLog log;
    RememberEnv env = given_host(&log, true);

    given_wake(data);
    given_file(data, "bin/python3", "#!/bin/sh\necho py\n", 0755);
    path_of(fake_py, sizeof(fake_py), data, "bin/python3");
    argv[0] = fake_py;
    argv[1] = "run.sh";
    given_commands(&two, data, argv, 2U, NULL, 0U);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &two.subject, NOW), PLUGIN_TRUST_OK);
    given_file(data, "bin/python3", "#!/bin/sh\necho py2\n", 0755);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &two.subject), PLUGIN_TRUST_UNTRUSTED);
    harness_remove_tree(data);
    free(data);
}

/* The env interpreter is found in the PATH the caller passes; another PATH is other code. */
TEST(an_env_interpreter_that_a_new_search_path_finds_is_untrusted)
{
    char *data = given_data_dir();
    char bin[PATH_BUF];
    const char *argv[3] = {"/usr/bin/env", "python3", "run.sh"};
    TwoCommands two;
    HostLog log;
    RememberEnv env = given_host(&log, true);

    given_wake(data);
    given_file(data, "bin/python3", "#!/bin/sh\necho py\n", 0755);
    path_of(bin, sizeof(bin), data, "bin");
    given_commands(&two, data, argv, 3U, NULL, 0U);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &two.subject, NOW), PLUGIN_TRUST_OK);
    two.subject.search_path = bin;
    ASSERT_EQ_STATUS(plugin_trust_check(data, &two.subject), PLUGIN_TRUST_UNTRUSTED);
    harness_remove_tree(data);
    free(data);
}

TEST(a_record_of_a_plugin_does_not_trust_a_hook_of_that_name)
{
    char *data = given_data_dir();
    WakeSubject w;
    PingSubject ping;

    given_trusted_wake(data, &w);
    given_ping(&ping, data);
    ping.subject.name = "wake";
    ASSERT_EQ_STATUS(plugin_trust_check(data, &ping.subject), PLUGIN_TRUST_UNTRUSTED);
    harness_remove_tree(data);
    free(data);
}

/* Size and mtime are stored for plan 35's pre-flight; the hashes are what a check compares. */
TEST(a_file_touched_with_the_same_bytes_is_still_trusted)
{
    char *data = given_data_dir();
    WakeSubject w;
    struct timeval times[2] = {{FIXED_MTIME + 5000, 0}, {FIXED_MTIME + 5000, 0}};
    char path[PATH_BUF];

    given_trusted_wake(data, &w);
    path_of(path, sizeof(path), data, "tools/tool.sh");
    ASSERT_EQ_INT(utimes(path, times), 0);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &w.subject), PLUGIN_TRUST_OK);
    harness_remove_tree(data);
    free(data);
}

/* decision:2026-11-12-trust-file: a mismatch deletes nothing; trust.json and the plugin stay. */
TEST(mismatch_sets_untrusted_keeps_enabled)
{
    char *data = given_data_dir();
    WakeSubject w;
    char *before = NULL;
    char *after = NULL;

    given_trusted_wake(data, &w);
    before = trust_text(data);
    given_file(data, "plugins/wake/run.sh", "#!/bin/sh\necho changed\n", 0755);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &w.subject), PLUGIN_TRUST_UNTRUSTED);
    after = trust_text(data);
    ASSERT_STREQ(before, after);
    ASSERT_TRUE(exists_at(data, "plugins/wake/run.sh"));
    ASSERT_TRUE(exists_at(data, "plugins/wake/plugin.json"));
    ASSERT_TRUE(exists_at(data, "tools/tool.sh"));
    free(before);
    free(after);
    harness_remove_tree(data);
    free(data);
}

/* 010 test 13: a successful trust after the edit makes the next check pass. */
TEST(trust_clears_reason)
{
    char *data = given_data_dir();
    WakeSubject w;
    HostLog log;
    RememberEnv env = given_host(&log, true);

    given_trusted_wake(data, &w);
    given_file(data, "plugins/wake/run.sh", "#!/bin/sh\necho changed\n", 0755);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &w.subject), PLUGIN_TRUST_UNTRUSTED);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_OK);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &w.subject), PLUGIN_TRUST_OK);
    harness_remove_tree(data);
    free(data);
}

TEST(a_hook_is_trusted_by_its_command_file_and_untrusted_when_that_changes)
{
    char *data = given_data_dir();
    PingSubject ping;
    HostLog log;
    RememberEnv env = given_host(&log, true);

    given_ping(&ping, data);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &ping.subject, NOW), PLUGIN_TRUST_OK);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &ping.subject), PLUGIN_TRUST_OK);
    given_file(data, "tools/hook.sh", "echo changed\n", 0755);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &ping.subject), PLUGIN_TRUST_UNTRUSTED);
    harness_remove_tree(data);
    free(data);
}

TEST(builtin_trusted_no_record)
{
    char *data = given_data_dir();
    WakeSubject w;
    HostLog log;
    RememberEnv env = given_host(&log, true);

    given_wake(data);
    given_wake_subject(&w, data);
    w.subject.name = "hybrid";
    ASSERT_EQ_STATUS(plugin_trust_check(data, &w.subject), PLUGIN_TRUST_OK);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_OK);
    ASSERT_EQ_INT(log.calls, 0);
    ASSERT_FALSE(exists_at(data, "trust.json"));
    harness_remove_tree(data);
    free(data);
}

TEST(a_hook_named_hybrid_is_not_the_built_in)
{
    char *data = given_data_dir();
    PingSubject ping;

    given_ping(&ping, data);
    ping.subject.name = "hybrid";
    ASSERT_EQ_STATUS(plugin_trust_check(data, &ping.subject), PLUGIN_TRUST_UNTRUSTED);
    harness_remove_tree(data);
    free(data);
}

/* ---- trust.json that cannot be used ------------------------------------------------------- */

#define HEX_A "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define DOC_HEAD "{\"format\":\"remember-trust\",\"version\":1,\"entries\":["
#define ENTRY_HEAD "{\"kind\":\"plugin\",\"name\":\"wake\",\"path\":\"/x\","
#define ENTRY_SHAS "\"folder_sha256\":\"" HEX_A "\",\"manifest_sha256\":\"" HEX_A "\","
#define ENTRY_TAIL "\"interpreter\":null,\"needs\":[],\"trusted_at\":\"t\",\"dev\":false}"
#define ENTRY_OK ENTRY_HEAD ENTRY_SHAS "\"files\":[]," ENTRY_TAIL
#define FILE_ROW(size) "{\"path\":\"/f\",\"sha256\":\"" HEX_A "\",\"size\":" size ",\"mtime\":1}"

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the data dir, then the text
static void given_trust_text(const char *data, const char *text)
{
    char path[PATH_BUF];

    path_of(path, sizeof(path), data, "trust.json");
    harness_write_file(path, text);
}

/* A hand-written copy of the decided document reads: its entry just does not match the code. */
TEST(a_hand_written_trust_json_of_the_decided_shape_is_read)
{
    char *data = given_data_dir();
    WakeSubject w;

    given_wake(data);
    given_wake_subject(&w, data);
    given_trust_text(data, DOC_HEAD ENTRY_OK "]}");
    ASSERT_EQ_STATUS(plugin_trust_check(data, &w.subject), PLUGIN_TRUST_UNTRUSTED);
    harness_remove_tree(data);
    free(data);
}

TEST(a_trust_json_that_is_not_the_document_is_unreadable_and_never_written_over)
{
    static const char *const k_bad[] = {
        "not json",
        "",
        "[]",
        "{\"format\":\"remember-plugin-trust\",\"version\":1,\"entries\":[]}",
        "{\"format\":\"remember-trust\",\"version\":2,\"entries\":[]}",
        "{\"format\":\"remember-trust\",\"version\":\"1\",\"entries\":[]}",
        "{\"format\":7,\"version\":1,\"entries\":[]}",
        "{\"format\":\"remember-trust\",\"version\":1,\"entries\":[],\"x\":1}",
        "{\"format\":\"remember-trust\",\"version\":1,\"version\":1,\"entries\":[]}",
        "{\"format\":\"remember-trust\",\"version\":1}",
        "{\"format\":\"remember-trust\",\"version\":1,\"entries\":{}}",
        DOC_HEAD "1]}",
        DOC_HEAD ENTRY_OK ",{}]}",
        DOC_HEAD ENTRY_OK "," ENTRY_OK "]}",
        DOC_HEAD ENTRY_HEAD ENTRY_SHAS "\"files\":[],\"extra\":1," ENTRY_TAIL "]}",
        DOC_HEAD ENTRY_HEAD ENTRY_SHAS "\"files\":[]," ENTRY_TAIL "]}"
                                       "x",
        DOC_HEAD "{\"kind\":\"service\",\"name\":\"wake\",\"path\":\"/x\"," ENTRY_SHAS
                 "\"files\":[]," ENTRY_TAIL "]}",
        DOC_HEAD "{\"kind\":\"plugin\",\"name\":\"\",\"path\":\"/x\"," ENTRY_SHAS
                 "\"files\":[]," ENTRY_TAIL "]}",
        DOC_HEAD "{\"kind\":\"plugin\",\"name\":\"wake\",\"path\":5," ENTRY_SHAS
                 "\"files\":[]," ENTRY_TAIL "]}",
        DOC_HEAD ENTRY_HEAD
        "\"folder_sha256\":\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\","
        "\"manifest_sha256\":\"" HEX_A "\",\"files\":[]," ENTRY_TAIL "]}",
        DOC_HEAD ENTRY_HEAD "\"folder_sha256\":\"" HEX_A "a\",\"manifest_sha256\":\"" HEX_A
                            "\",\"files\":[]," ENTRY_TAIL "]}",
        DOC_HEAD ENTRY_HEAD "\"folder_sha256\":\"aa\",\"manifest_sha256\":\"" HEX_A
                            "\",\"files\":[]," ENTRY_TAIL "]}",
        DOC_HEAD ENTRY_HEAD ENTRY_SHAS "\"files\":[" FILE_ROW("-1") "]," ENTRY_TAIL "]}",
        DOC_HEAD ENTRY_HEAD ENTRY_SHAS "\"files\":[" FILE_ROW("1.5") "]," ENTRY_TAIL "]}",
        DOC_HEAD ENTRY_HEAD ENTRY_SHAS
        "\"files\":[" FILE_ROW("99999999999999999999") "]," ENTRY_TAIL "]}",
        DOC_HEAD ENTRY_HEAD ENTRY_SHAS
        "\"files\":[" FILE_ROW("1") "," FILE_ROW("1") "," FILE_ROW("1") "," FILE_ROW("1") "," FILE_ROW(
            "1") "," FILE_ROW("1") "," FILE_ROW("1") "," FILE_ROW("1") "," FILE_ROW("1") "]"
                                                                                         "," ENTRY_TAIL
                                                                                         "]}",
        DOC_HEAD ENTRY_HEAD ENTRY_SHAS "\"files\":[{\"path\":\"\",\"sha256\":\"" HEX_A
                                       "\",\"size\":1,\"mtime\":1}]," ENTRY_TAIL "]}",
        DOC_HEAD ENTRY_HEAD ENTRY_SHAS "\"files\":[],"
                                       "\"interpreter\":{\"path\":\"/x\"},"
                                       "\"needs\":[],\"trusted_at\":\"t\",\"dev\":false}]}",
        DOC_HEAD ENTRY_HEAD ENTRY_SHAS "\"files\":[],\"interpreter\":3,\"needs\":[],"
                                       "\"trusted_at\":\"t\",\"dev\":false}]}",
        DOC_HEAD ENTRY_HEAD ENTRY_SHAS "\"files\":[],\"interpreter\":null,\"needs\":[1],"
                                       "\"trusted_at\":\"t\",\"dev\":false}]}",
        DOC_HEAD ENTRY_HEAD ENTRY_SHAS "\"files\":[],\"interpreter\":null,\"needs\":[],"
                                       "\"trusted_at\":1,\"dev\":false}]}",
        DOC_HEAD ENTRY_HEAD ENTRY_SHAS "\"files\":[],\"interpreter\":null,\"needs\":[],"
                                       "\"trusted_at\":\"t\",\"dev\":\"false\"}]}",
    };
    size_t i = 0;

    for (i = 0; i < sizeof(k_bad) / sizeof(k_bad[0]); i++) {
        char *data = given_data_dir();
        WakeSubject w;
        HostLog log;
        RememberEnv env = given_host(&log, true);
        char *after = NULL;

        given_wake(data);
        given_wake_subject(&w, data);
        given_trust_text(data, k_bad[i]);
        ASSERT_EQ_STATUS(plugin_trust_check(data, &w.subject), PLUGIN_TRUST_UNREADABLE);
        ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_UNREADABLE);
        ASSERT_EQ_INT(log.calls, 0);
        after = trust_text(data);
        ASSERT_STREQ(after, k_bad[i]);
        ASSERT_FALSE(exists_at(data, "trust.json.tmp"));
        free(after);
        harness_remove_tree(data);
        free(data);
    }
}

TEST(a_trust_json_over_a_mebibyte_is_unreadable)
{
    char *data = given_data_dir();
    WakeSubject w;
    char path[PATH_BUF];
    FILE *file = NULL;
    size_t i = 0;

    given_wake(data);
    given_wake_subject(&w, data);
    path_of(path, sizeof(path), data, "trust.json");
    // NOLINTNEXTLINE(android-cloexec-fopen)
    file = fopen(path, "w");
    ASSERT_TRUE(file != NULL);
    if (file == NULL) {
        return;
    }
    ASSERT_TRUE(fputs(DOC_HEAD, file) >= 0);
    for (i = 0; i < (size_t)PLUGIN_TRUST_FILE_MAX; i++) {
        ASSERT_TRUE(fputc(' ', file) != EOF);
    }
    ASSERT_TRUE(fputs("]}", file) >= 0);
    ASSERT_EQ_INT(fclose(file), 0);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &w.subject), PLUGIN_TRUST_UNREADABLE);
    harness_remove_tree(data);
    free(data);
}

TEST(a_trust_json_that_is_a_folder_or_a_fifo_is_unreadable_and_does_not_hang)
{
    char *data = given_data_dir();
    WakeSubject w;
    char path[PATH_BUF];

    given_wake(data);
    given_wake_subject(&w, data);
    path_of(path, sizeof(path), data, "trust.json");
    ASSERT_EQ_INT(mkdir(path, 0700), 0);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &w.subject), PLUGIN_TRUST_UNREADABLE);
    ASSERT_EQ_INT(rmdir(path), 0);
    ASSERT_EQ_INT(mkfifo(path, 0600), 0);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &w.subject), PLUGIN_TRUST_UNREADABLE);
    harness_remove_tree(data);
    free(data);
}

/* ---- code that cannot be hashed ------------------------------------------------------------ */

/* The status of allowing wake as it is on disk now; the host must not have been asked. */
static PluginTrustStatus allow_wake_unasked(const char *data)
{
    WakeSubject w;
    HostLog log;
    RememberEnv env = given_host(&log, true);
    PluginTrustStatus status = PLUGIN_TRUST_OK;

    given_wake_subject(&w, data);
    status = plugin_trust_allow(data, &env, &w.subject, NOW);
    ASSERT_EQ_INT(log.calls, status == PLUGIN_TRUST_OK ? 1 : 0);
    return status;
}

TEST(a_symlink_in_the_folder_makes_it_unhashable)
{
    char *data = given_data_dir();
    char target[PATH_BUF];
    char link_path[PATH_BUF];

    given_wake(data);
    path_of(target, sizeof(target), data, "tools/tool.sh");
    path_of(link_path, sizeof(link_path), data, "plugins/wake/linked.sh");
    ASSERT_EQ_INT(symlink(target, link_path), 0);
    ASSERT_EQ_STATUS(allow_wake_unasked(data), PLUGIN_TRUST_UNHASHABLE);
    ASSERT_FALSE(exists_at(data, "trust.json"));
    harness_remove_tree(data);
    free(data);
}

TEST(a_symlinked_folder_inside_the_plugin_makes_it_unhashable)
{
    char *data = given_data_dir();
    char target[PATH_BUF];
    char link_path[PATH_BUF];

    given_wake(data);
    path_of(target, sizeof(target), data, "tools");
    path_of(link_path, sizeof(link_path), data, "plugins/wake/lib");
    ASSERT_EQ_INT(symlink(target, link_path), 0);
    ASSERT_EQ_STATUS(allow_wake_unasked(data), PLUGIN_TRUST_UNHASHABLE);
    harness_remove_tree(data);
    free(data);
}

TEST(a_fifo_in_the_folder_makes_it_unhashable)
{
    char *data = given_data_dir();
    char fifo[PATH_BUF];

    given_wake(data);
    path_of(fifo, sizeof(fifo), data, "plugins/wake/pipe");
    ASSERT_EQ_INT(mkfifo(fifo, 0600), 0);
    ASSERT_EQ_STATUS(allow_wake_unasked(data), PLUGIN_TRUST_UNHASHABLE);
    harness_remove_tree(data);
    free(data);
}

TEST(the_plugin_folder_itself_as_a_symlink_is_unhashable)
{
    char *data = given_data_dir();
    char moved_from[PATH_BUF];
    char moved[PATH_BUF];

    given_wake(data);
    path_of(moved_from, sizeof(moved_from), data, "plugins/wake");
    path_of(moved, sizeof(moved), data, "plugins/wake-real");
    ASSERT_EQ_INT(rename(moved_from, moved), 0);
    ASSERT_EQ_INT(symlink(moved, moved_from), 0);
    ASSERT_EQ_STATUS(allow_wake_unasked(data), PLUGIN_TRUST_UNHASHABLE);
    harness_remove_tree(data);
    free(data);
}

TEST(a_folder_file_that_cannot_be_read_makes_it_unhashable)
{
    char *data = given_data_dir();
    char path[PATH_BUF];
    PluginTrustStatus status = PLUGIN_TRUST_OK;

    given_wake(data);
    path_of(path, sizeof(path), data, "plugins/wake/run.sh");
    ASSERT_EQ_INT(chmod(path, 0000), 0);
    status = allow_wake_unasked(data);
    ASSERT_EQ_INT(chmod(path, 0600), 0);
    ASSERT_EQ_INT(status, geteuid() == 0 ? PLUGIN_TRUST_OK : PLUGIN_TRUST_UNHASHABLE);
    harness_remove_tree(data);
    free(data);
}

TEST(an_argv_file_that_exists_and_cannot_be_read_is_unhashable)
{
    char *data = given_data_dir();
    char path[PATH_BUF];
    PluginTrustStatus status = PLUGIN_TRUST_OK;

    given_wake(data);
    path_of(path, sizeof(path), data, "tools/tool.sh");
    ASSERT_EQ_INT(chmod(path, 0000), 0);
    status = allow_wake_unasked(data);
    ASSERT_EQ_INT(chmod(path, 0600), 0);
    ASSERT_EQ_INT(status, geteuid() == 0 ? PLUGIN_TRUST_OK : PLUGIN_TRUST_UNHASHABLE);
    harness_remove_tree(data);
    free(data);
}

/* Nested folders to depth levels under the plugin folder, one file at the bottom. */
static void given_deep_folder(const char *data, int levels)
{
    char rel[PATH_BUF] = "plugins/wake";
    size_t used = strlen(rel);

    for (int i = 0; i < levels; i++) {
        int wrote = snprintf(rel + used, sizeof(rel) - used, "/d");

        ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(rel) - used);
        used += (size_t)wrote;
    }
    ASSERT_TRUE(snprintf(rel + used, sizeof(rel) - used, "/leaf") > 0);
    given_file(data, rel, "x\n", 0644);
}

TEST(a_folder_16_levels_deep_is_hashed_and_17_is_not)
{
    char *deep = given_data_dir();
    char *deeper = given_data_dir();

    given_wake(deep);
    given_deep_folder(deep, 16);
    ASSERT_EQ_STATUS(allow_wake_unasked(deep), PLUGIN_TRUST_OK);
    given_wake(deeper);
    given_deep_folder(deeper, 17);
    ASSERT_EQ_STATUS(allow_wake_unasked(deeper), PLUGIN_TRUST_UNHASHABLE);
    harness_remove_tree(deep);
    harness_remove_tree(deeper);
    free(deep);
    free(deeper);
}

/* count empty files under <plugin>/<dir>/ (dir_count folders, one file in each when asked). */
static void given_many_files(const char *data, size_t count)
{
    size_t i = 0;
    char dir[PATH_BUF];

    path_of(dir, sizeof(dir), data, "plugins/wake/many");
    ASSERT_EQ_INT(mkdir(dir, 0700), 0);
    for (i = 0; i < count; i++) {
        char path[PATH_BUF];
        int fd = -1;

        (void)snprintf(path, sizeof(path), "%s/f%zu", dir, i);
        fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
        ASSERT_TRUE(fd >= 0);
        if (fd >= 0) {
            ASSERT_EQ_INT(close(fd), 0);
        }
    }
}

TEST(a_folder_of_4096_files_is_hashed_and_4097_is_not)
{
    char *fits = given_data_dir();
    char *over = given_data_dir();

    given_wake(fits);
    given_many_files(fits, (size_t)PLUGIN_TRUST_FOLDER_FILES_MAX - 2U); /* plugin.json, run.sh */
    ASSERT_EQ_STATUS(allow_wake_unasked(fits), PLUGIN_TRUST_OK);
    given_wake(over);
    given_many_files(over, (size_t)PLUGIN_TRUST_FOLDER_FILES_MAX - 1U);
    ASSERT_EQ_STATUS(allow_wake_unasked(over), PLUGIN_TRUST_UNHASHABLE);
    harness_remove_tree(fits);
    harness_remove_tree(over);
    free(fits);
    free(over);
}

TEST(a_folder_of_4097_folders_is_unhashable)
{
    char *data = given_data_dir();
    size_t i = 0;

    given_wake(data);
    for (i = 0; i < (size_t)PLUGIN_TRUST_FOLDER_DIRS_MAX + 1U; i++) {
        char rel[64];
        char path[PATH_BUF];

        (void)snprintf(rel, sizeof(rel), "plugins/wake/d%zu", i);
        path_of(path, sizeof(path), data, rel);
        ASSERT_EQ_INT(mkdir(path, 0700), 0);
    }
    ASSERT_EQ_STATUS(allow_wake_unasked(data), PLUGIN_TRUST_UNHASHABLE);
    harness_remove_tree(data);
    free(data);
}

TEST(a_file_over_512_mebibytes_is_unhashable_and_one_of_exactly_that_is_hashed)
{
    char *data = given_data_dir();
    char path[PATH_BUF];

    given_wake(data);
    path_of(path, sizeof(path), data, "tools/tool.sh");
    ASSERT_EQ_INT(truncate(path, (off_t)PLUGIN_TRUST_HASHED_FILE_MAX + 1), 0);
    ASSERT_EQ_STATUS(allow_wake_unasked(data), PLUGIN_TRUST_UNHASHABLE);
    ASSERT_EQ_INT(truncate(path, (off_t)PLUGIN_TRUST_HASHED_FILE_MAX), 0);
    ASSERT_EQ_STATUS(allow_wake_unasked(data), PLUGIN_TRUST_OK);
    harness_remove_tree(data);
    free(data);
}

/* The manifest bytes are the caller's (the parse), so the cap is on those; a plugin.json the
   reader refuses never reaches a subject (plugin_trust_load_installed). */
TEST(a_missing_folder_or_manifest_or_one_over_the_reader_cap_is_unhashable)
{
    char *data = given_data_dir();
    WakeSubject w;
    HostLog log;
    RememberEnv env = given_host(&log, true);
    PluginTrustInstalled installed;
    char path[PATH_BUF];
    char big[PLUGIN_JSON_MAX + 2];

    given_wake(data);
    given_wake_subject(&w, data);
    w.subject.manifest_len = (size_t)PLUGIN_JSON_MAX + 1U;
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_UNHASHABLE);
    w.subject.manifest_len = (size_t)PLUGIN_JSON_MAX;
    w.subject.manifest = big;
    memset(big, 'a', sizeof(big) - 1U);
    big[sizeof(big) - 1U] = '\0';
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_OK);
    w.subject.manifest = NULL;
    w.subject.manifest_len = 0U;
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_UNHASHABLE);
    path_of(path, sizeof(path), data, "plugins/wake/plugin.json");
    harness_write_file(path, big);
    memset(&installed, 0, sizeof(installed));
    ASSERT_EQ_STATUS(
        plugin_trust_load_installed(data, "wake", PLUGIN_TRUST_KIND_PLUGIN, NULL, &installed),
        PLUGIN_TRUST_UNHASHABLE);
    ASSERT_EQ_INT(unlink(path), 0);
    ASSERT_EQ_STATUS(
        plugin_trust_load_installed(data, "wake", PLUGIN_TRUST_KIND_PLUGIN, NULL, &installed),
        PLUGIN_TRUST_UNHASHABLE);
    harness_remove_tree(data);
    ASSERT_EQ_STATUS(allow_wake_unasked(data), PLUGIN_TRUST_UNHASHABLE);
    free(data);
}

TEST(a_path_or_name_that_is_not_utf8_is_unhashable)
{
    char *data = given_data_dir();
    WakeSubject w;
    HostLog log;
    RememberEnv env = given_host(&log, true);
    char odd[PATH_BUF];
    TwoCommands two;
    int fd = -1;

    given_wake(data);
    given_wake_subject(&w, data);
    w.subject.name = "wa\xff"
                     "ke";
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_UNHASHABLE);
    given_wake_subject(&w, data);
    w.needs[0] = "bo\xc3";
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_UNHASHABLE);
    ASSERT_EQ_INT(log.calls, 0);
    /* A filesystem that keeps names as UTF-8 (APFS) cannot make this file; Linux can. */
    (void)snprintf(odd, sizeof(odd), "%s/tools/odd\xff", data);
    fd = open(odd, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
    if (fd >= 0) {
        const char *argv[1];

        ASSERT_EQ_INT(close(fd), 0);
        argv[0] = odd;
        given_commands(&two, data, argv, 1U, NULL, 0U);
        ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &two.subject, NOW),
                         PLUGIN_TRUST_UNHASHABLE);
        ASSERT_EQ_INT(log.calls, 0);
    }
    harness_remove_tree(data);
    free(data);
}

TEST(a_subject_the_caller_built_wrong_is_unhashable)
{
    char *data = given_data_dir();
    WakeSubject w;
    HostLog log;
    RememberEnv env = given_host(&log, true);
    const char *nine[9] = {"a", "b", "c", "d", "e", "f", "g", "h", "i"};
    const char *holey[2] = {"a", NULL};
    PluginTrustCommand too_long = {.argv = nine, .argc = 9U};
    PluginTrustCommand has_null = {.argv = holey, .argc = 2U};
    PluginTrustCommand empty = {.argv = nine, .argc = 0U};

    given_wake(data);
    given_wake_subject(&w, data);
    w.subject.commands = &too_long;
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_UNHASHABLE);
    w.subject.commands = &has_null;
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_UNHASHABLE);
    w.subject.commands = &empty;
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_UNHASHABLE);
    {
        PluginTrustCommand many[PLUGIN_TRUST_COMMANDS_MAX + 1];
        size_t at = 0;

        for (at = 0; at < sizeof(many) / sizeof(many[0]); at++) {
            many[at] = (PluginTrustCommand){.argv = w.argv, .argc = 1U};
        }
        given_wake_subject(&w, data);
        w.subject.commands = many;
        w.subject.ncommands = (size_t)PLUGIN_TRUST_COMMANDS_MAX;
        ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_OK);
        w.subject.ncommands = (size_t)PLUGIN_TRUST_COMMANDS_MAX + 1U;
        ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_UNHASHABLE);
    }
    given_wake_subject(&w, data);
    w.subject.name = "";
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_UNHASHABLE);
    given_wake_subject(&w, data);
    w.subject.folder = NULL;
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_UNHASHABLE);
    ASSERT_EQ_INT(log.calls, 1); /* only the 32-command subject was asked about */
    harness_remove_tree(data);
    free(data);
}

TEST(a_hook_needs_exactly_one_command)
{
    char *data = given_data_dir();
    PingSubject ping;
    HostLog log;
    RememberEnv env = given_host(&log, true);

    given_ping(&ping, data);
    ping.subject.ncommands = 2U;
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &ping.subject, NOW), PLUGIN_TRUST_UNHASHABLE);
    harness_remove_tree(data);
    free(data);
}

TEST(an_argv_fifo_a_folder_and_a_directory_named_as_the_interpreter_are_not_files)
{
    char *data = given_data_dir();
    char fifo[PATH_BUF];
    char search[2 * PATH_BUF];
    char folder_in_bin1[PATH_BUF];
    char tools[PATH_BUF];
    const char *argv[3];
    char *text = NULL;

    given_wake(data);
    given_file(data, "bin2/python3", "#!/bin/sh\necho py\n", 0755);
    path_of(fifo, sizeof(fifo), data, "tools/pipe");
    ASSERT_EQ_INT(mkfifo(fifo, 0600), 0);
    path_of(tools, sizeof(tools), data, "tools");
    path_of(folder_in_bin1, sizeof(folder_in_bin1), data, "bin1/python3");
    given_file(data, "bin1/other", "x\n", 0644);
    ASSERT_EQ_INT(mkdir(folder_in_bin1, 0755), 0);
    (void)snprintf(search, sizeof(search), "%s/bin1:%s/bin2", data, data);
    argv[0] = "/usr/bin/env";
    argv[1] = "python3";
    argv[2] = fifo;
    ASSERT_EQ_STATUS(allow_command(data, argv, 3U, search), PLUGIN_TRUST_OK);
    text = trust_text(data);
    ASSERT_STR_CONTAINS(text, "bin2/python3\",\"sha256\":\"" FAKE_PY_SHA "\"");
    ASSERT_STR_NOT_CONTAINS(text, "tools/pipe");
    free(text);
    harness_remove_tree(data);
    free(data);
}

/* ---- the write ------------------------------------------------------------------------------ */

TEST(two_plugins_share_one_file_and_an_update_replaces_only_its_own_entry)
{
    char *data = given_data_dir();
    WakeSubject w;
    PingSubject ping;
    HostLog log;
    RememberEnv env = given_host(&log, true);
    char *first = NULL;
    char *second = NULL;
    const char *at_ping = NULL;

    given_wake(data);
    given_ping(&ping, data);
    given_wake_subject(&w, data);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_OK);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &ping.subject, NOW), PLUGIN_TRUST_OK);
    first = trust_text(data);
    ASSERT_STR_CONTAINS(first, "\"name\":\"wake\"");
    ASSERT_STR_CONTAINS(first, "\"kind\":\"hook\",\"name\":\"ping\"");
    given_file(data, "plugins/wake/run.sh", "#!/bin/sh\necho changed\n", 0755);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, "2030-01-01T00:00:00.000Z"),
                     PLUGIN_TRUST_OK);
    second = trust_text(data);
    ASSERT_STR_CONTAINS(second, "2030-01-01T00:00:00.000Z");
    at_ping = strstr(first, "{\"kind\":\"hook\"");
    ASSERT_TRUE(at_ping != NULL);
    ASSERT_STR_CONTAINS(second, at_ping);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &ping.subject), PLUGIN_TRUST_OK);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &w.subject), PLUGIN_TRUST_OK);
    free(first);
    free(second);
    harness_remove_tree(data);
    free(data);
}

/* Review point: trust.json is rewritten only through rename. A symlink or a stale file at the
   temp name is not followed. */
TEST(a_stale_or_symlinked_temp_file_is_not_followed_and_none_is_left)
{
    char *data = given_data_dir();
    WakeSubject w;
    HostLog log;
    RememberEnv env = given_host(&log, true);
    char victim[PATH_BUF];
    char tmp[PATH_BUF];
    char *text = NULL;

    given_wake(data);
    given_wake_subject(&w, data);
    given_file(data, "victim", "keep me\n", 0644);
    path_of(victim, sizeof(victim), data, "victim");
    path_of(tmp, sizeof(tmp), data, "trust.json.tmp");
    ASSERT_EQ_INT(symlink(victim, tmp), 0);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_OK);
    text = harness_read_file(victim, &(size_t){0});
    ASSERT_STREQ(text, "keep me\n");
    free(text);
    ASSERT_FALSE(exists_at(data, "trust.json.tmp"));
    harness_remove_tree(data);
    free(data);
}

TEST(a_data_dir_that_is_not_a_folder_is_an_io_failure_after_the_host_was_asked_once)
{
    char *data = given_data_dir();
    WakeSubject w;
    HostLog log;
    RememberEnv env = given_host(&log, true);
    char blocker[PATH_BUF];
    char nested[PATH_BUF];

    given_wake(data);
    given_wake_subject(&w, data);
    path_of(blocker, sizeof(blocker), data, "blocker");
    harness_write_file(blocker, "x");
    path_of(nested, sizeof(nested), blocker, "inside");
    ASSERT_EQ_STATUS(plugin_trust_allow(nested, &env, &w.subject, NOW), PLUGIN_TRUST_IO);
    ASSERT_EQ_INT(log.calls, 1);
    harness_remove_tree(data);
    free(data);
}

TEST(a_data_dir_that_does_not_exist_yet_is_made)
{
    char *data = given_data_dir();
    WakeSubject w;
    HostLog log;
    RememberEnv env = given_host(&log, true);
    char fresh[PATH_BUF];

    given_wake(data);
    given_wake_subject(&w, data);
    path_of(fresh, sizeof(fresh), data, "new/data");
    ASSERT_EQ_STATUS(plugin_trust_allow(fresh, &env, &w.subject, NOW), PLUGIN_TRUST_OK);
    ASSERT_EQ_STATUS(plugin_trust_check(fresh, &w.subject), PLUGIN_TRUST_OK);
    harness_remove_tree(data);
    free(data);
}

/* decision:2026-11-22-plan33-trust-lock: another process holds the lock, so the write waits and
   then says busy, with the file as it was. */
TEST(a_held_lock_makes_the_write_busy_and_leaves_the_file_alone)
{
    char *data = given_data_dir();
    WakeSubject w;
    HostLog log;
    RememberEnv env = given_host(&log, true);
    char lock_path[PATH_BUF];
    int ready[2];
    int release[2];
    pid_t child = -1;
    char byte = 'x';

    given_wake(data);
    given_wake_subject(&w, data);
    path_of(lock_path, sizeof(lock_path), data, "trust.json.lock");
    /* The child must inherit both ends, so no O_CLOEXEC. */
    // NOLINTNEXTLINE(android-cloexec-pipe)
    ASSERT_EQ_INT(pipe(ready), 0);
    // NOLINTNEXTLINE(android-cloexec-pipe)
    ASSERT_EQ_INT(pipe(release), 0);
    child = fork();
    ASSERT_TRUE(child >= 0);
    if (child == 0) {
        int fd = fileio_lock_open(lock_path, 0);

        byte = fd >= 0 ? 'k' : 'f';
        (void)write(ready[1], &byte, 1);
        (void)read(release[0], &byte, 1);
        _exit(0);
    }
    ASSERT_EQ_INT(read(ready[0], &byte, 1), 1);
    ASSERT_EQ_INT(byte, 'k');
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_BUSY);
    ASSERT_FALSE(exists_at(data, "trust.json"));
    (void)write(release[1], &byte, 1);
    (void)waitpid(child, NULL, 0);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_OK);
    (void)close(ready[0]);
    (void)close(ready[1]);
    (void)close(release[0]);
    (void)close(release[1]);
    harness_remove_tree(data);
    free(data);
}

TEST(the_file_path_is_trust_json_in_the_data_dir)
{
    char *path = plugin_trust_file_path("/data/dir");

    ASSERT_STREQ(path, "/data/dir/trust.json");
    free(path);
}

/* ---- memory runs out --------------------------------------------------------------------------
 */

/* Make the n-th allocation fail from now: n = 0 is the next one. */
TEST(an_allocation_that_fails_in_allow_is_out_of_memory_and_writes_nothing)
{
    int n = 0;
    bool saw_oom = false;
    PluginTrustStatus status = PLUGIN_TRUST_OOM;

    for (n = 0; n < 400 && status != PLUGIN_TRUST_OK; n++) {
        char *data = given_data_dir();
        WakeSubject w;
        HostLog log;
        RememberEnv env = given_host(&log, true);

        given_wake(data);
        given_wake_subject(&w, data);
        test_alloc_fail_after(n);
        status = plugin_trust_allow(data, &env, &w.subject, NOW);
        test_alloc_fail_after(-1);
        if (status == PLUGIN_TRUST_OK) {
            ASSERT_EQ_STATUS(plugin_trust_check(data, &w.subject), PLUGIN_TRUST_OK);
        } else {
            saw_oom = true;
            ASSERT_EQ_INT(status, PLUGIN_TRUST_OOM);
            ASSERT_FALSE(exists_at(data, "trust.json"));
        }
        harness_remove_tree(data);
        free(data);
    }
    ASSERT_TRUE(saw_oom);
    ASSERT_EQ_INT(status, PLUGIN_TRUST_OK);
}

TEST(an_allocation_that_fails_in_check_is_out_of_memory_never_untrusted_or_trusted)
{
    char *data = given_data_dir();
    WakeSubject w;
    int n = 0;
    bool saw_oom = false;
    PluginTrustStatus status = PLUGIN_TRUST_OOM;

    given_trusted_wake(data, &w);
    for (n = 0; n < 400 && status != PLUGIN_TRUST_OK; n++) {
        test_alloc_fail_after(n);
        status = plugin_trust_check(data, &w.subject);
        test_alloc_fail_after(-1);
        if (status == PLUGIN_TRUST_OOM) {
            saw_oom = true;
        }
        ASSERT_TRUE(status == PLUGIN_TRUST_OOM || status == PLUGIN_TRUST_OK);
    }
    ASSERT_TRUE(saw_oom);
    harness_remove_tree(data);
    free(data);
}

TEST(an_argv_file_in_a_folder_that_only_shares_the_plugin_folders_name_prefix_is_recorded)
{
    char *data = given_data_dir();
    char sibling[PATH_BUF];
    const char *argv[1];
    char *text = NULL;

    given_wake(data);
    given_file(data, "plugins/wake2/x.sh", "echo x\n", 0755);
    path_of(sibling, sizeof(sibling), data, "plugins/wake2/x.sh");
    argv[0] = sibling;
    ASSERT_EQ_STATUS(allow_command(data, argv, 1U, NULL), PLUGIN_TRUST_OK);
    text = trust_text(data);
    ASSERT_STR_CONTAINS(text, "plugins/wake2/x.sh");
    free(text);
    harness_remove_tree(data);
    free(data);
}

/* The 64 hex digits after "folder_sha256":" in trust.json (heap). */
static char *folder_sha_of(const char *data)
{
    char *text = trust_text(data);
    const char *at = text != NULL ? strstr(text, "\"folder_sha256\":\"") : NULL;
    char *sha = at != NULL ? (char *)malloc(65U) : NULL;

    ASSERT_TRUE(sha != NULL);
    if (sha != NULL) {
        memcpy(sha, at + sizeof("\"folder_sha256\":\"") - 1U, 64U);
        sha[64] = '\0';
    }
    free(text);
    return sha;
}

TEST(the_folder_hash_does_not_depend_on_the_order_the_files_were_made)
{
    char *forward = given_data_dir();
    char *backward = given_data_dir();
    WakeSubject w;
    HostLog log;
    RememberEnv env = given_host(&log, true);
    static const char *const k_names[] = {"a", "b", "c", "d", "e", "f"};
    const size_t name_count = sizeof(k_names) / sizeof(k_names[0]);
    size_t i = 0;
    char *first = NULL;
    char *second = NULL;

    given_file(forward, "plugins/wake/plugin.json", MANIFEST_TEXT, 0644);
    given_file(backward, "plugins/wake/plugin.json", MANIFEST_TEXT, 0644);
    for (i = 0; i < name_count; i++) {
        const char *mirror = k_names[(name_count - 1U) - i];
        char rel[64];

        (void)snprintf(rel, sizeof(rel), "plugins/wake/lib/%s", k_names[i]);
        given_file(forward, rel, k_names[i], 0644);
        (void)snprintf(rel, sizeof(rel), "plugins/wake/lib/%s", mirror);
        given_file(backward, rel, mirror, 0644);
    }
    given_wake_subject(&w, forward);
    ASSERT_EQ_STATUS(plugin_trust_allow(forward, &env, &w.subject, NOW), PLUGIN_TRUST_OK);
    given_wake_subject(&w, backward);
    ASSERT_EQ_STATUS(plugin_trust_allow(backward, &env, &w.subject, NOW), PLUGIN_TRUST_OK);
    first = folder_sha_of(forward);
    second = folder_sha_of(backward);
    ASSERT_STREQ(first, second);
    free(first);
    free(second);
    harness_remove_tree(forward);
    harness_remove_tree(backward);
    free(forward);
    free(backward);
}

/* Count empty folders under <plugin>/dirs/. */
static void given_many_dirs(const char *data, size_t count)
{
    size_t i = 0;
    char dir[PATH_BUF];

    path_of(dir, sizeof(dir), data, "plugins/wake/dirs");
    ASSERT_EQ_INT(mkdir(dir, 0700), 0);
    for (i = 0; i < count; i++) {
        char sub[PATH_BUF];

        (void)snprintf(sub, sizeof(sub), "%s/d%zu", dir, i);
        ASSERT_EQ_INT(mkdir(sub, 0700), 0);
    }
}

TEST(a_folder_of_4096_folders_is_hashed)
{
    char *data = given_data_dir();

    given_wake(data);
    given_many_dirs(data, (size_t)PLUGIN_TRUST_FOLDER_DIRS_MAX - 1U); /* and dirs/ itself */
    ASSERT_EQ_STATUS(allow_wake_unasked(data), PLUGIN_TRUST_OK);
    harness_remove_tree(data);
    free(data);
}

/* Two commands, so the 8 files are not held back by the 8 items of one argv. */
static PluginTrustStatus allow_two(const char *data, size_t first, size_t second)
{
    TwoCommands two;
    HostLog log;
    RememberEnv env = given_host(&log, true);
    char names[10][PATH_BUF];
    const char *argv_a[8] = {NULL};
    const char *argv_b[8] = {NULL};
    size_t i = 0;

    for (i = 0; i < 10U; i++) {
        char rel[32];

        (void)snprintf(rel, sizeof(rel), "tools/f%zu", i);
        given_file(data, rel, "x\n", 0644);
        path_of(names[i], sizeof(names[i]), data, rel);
    }
    for (i = 0; i < first; i++) {
        argv_a[i] = names[i];
    }
    for (i = 0; i < second; i++) {
        argv_b[i] = names[first + i];
    }
    given_commands(&two, data, argv_a, first, argv_b, second);
    return plugin_trust_allow(data, &env, &two.subject, NOW);
}

TEST(eight_files_over_two_commands_are_hashed_and_nine_are_not)
{
    char *eight = given_data_dir();
    char *nine = given_data_dir();

    given_wake(eight);
    ASSERT_EQ_STATUS(allow_two(eight, 5U, 3U), PLUGIN_TRUST_OK);
    given_wake(nine);
    ASSERT_EQ_STATUS(allow_two(nine, 5U, 4U), PLUGIN_TRUST_UNHASHABLE);
    harness_remove_tree(eight);
    harness_remove_tree(nine);
    free(eight);
    free(nine);
}

TEST(a_file_both_commands_name_counts_once_toward_the_8)
{
    char *data = given_data_dir();
    TwoCommands two;
    HostLog log;
    RememberEnv env = given_host(&log, true);
    char names[8][PATH_BUF];
    const char *argv_a[8];
    const char *argv_b[1];
    size_t i = 0;

    given_wake(data);
    for (i = 0; i < 8U; i++) {
        char rel[32];

        (void)snprintf(rel, sizeof(rel), "tools/f%zu", i);
        given_file(data, rel, "x\n", 0644);
        path_of(names[i], sizeof(names[i]), data, rel);
        argv_a[i] = names[i];
    }
    argv_b[0] = names[3];
    given_commands(&two, data, argv_a, 8U, argv_b, 1U);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &two.subject, NOW), PLUGIN_TRUST_OK);
    harness_remove_tree(data);
    free(data);
}

TEST(a_bare_env_command_with_no_name_after_it_records_no_interpreter)
{
    char *data = given_data_dir();
    const char *argv[1] = {"/usr/bin/env"};
    char *text = NULL;

    given_wake(data);
    ASSERT_EQ_STATUS(allow_command(data, argv, 1U, "/usr/bin"), PLUGIN_TRUST_OK);
    text = trust_text(data);
    ASSERT_STR_CONTAINS(text, "\"interpreter\":null");
    free(text);
    harness_remove_tree(data);
    free(data);
}

/* The inline text is part of the manifest hash for a shell only. The plugin.json bytes stay the
   same, and python's own file is the same, so only the argv could move the hash. */
TEST(inline_text_after_dash_c_of_a_program_that_is_not_a_shell_is_not_in_the_manifest_hash)
{
    char *data = given_data_dir();
    char fake_py[PATH_BUF];
    const char *before[3];
    const char *after[3];
    TwoCommands two;
    HostLog log;
    RememberEnv env = given_host(&log, true);

    given_wake(data);
    given_file(data, "bin/python3", "#!/bin/sh\necho py\n", 0755);
    path_of(fake_py, sizeof(fake_py), data, "bin/python3");
    before[0] = fake_py;
    before[1] = "-c";
    before[2] = "print(1)";
    after[0] = fake_py;
    after[1] = "-c";
    after[2] = "print(2)";
    given_commands(&two, data, before, 3U, NULL, 0U);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &two.subject, NOW), PLUGIN_TRUST_OK);
    given_commands(&two, data, after, 3U, NULL, 0U);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &two.subject), PLUGIN_TRUST_OK);
    harness_remove_tree(data);
    free(data);
}

TEST(the_same_bytes_under_another_argv_path_are_untrusted)
{
    char *data = given_data_dir();
    WakeSubject w;

    given_trusted_wake(data, &w);
    given_file(data, "tools/copy.sh", TOOL_SH_TEXT, 0755);
    path_of(w.tool, sizeof(w.tool), data, "tools/copy.sh");
    ASSERT_EQ_STATUS(plugin_trust_check(data, &w.subject), PLUGIN_TRUST_UNTRUSTED);
    harness_remove_tree(data);
    free(data);
}

TEST(an_env_interpreter_whose_bytes_change_is_untrusted)
{
    char *data = given_data_dir();
    char bin[PATH_BUF];
    const char *argv[3] = {"/usr/bin/env", "python3", "run.sh"};
    TwoCommands two;
    HostLog log;
    RememberEnv env = given_host(&log, true);

    given_wake(data);
    given_file(data, "bin/python3", "#!/bin/sh\necho py\n", 0755);
    path_of(bin, sizeof(bin), data, "bin");
    given_commands(&two, data, argv, 3U, NULL, 0U);
    two.subject.search_path = bin;
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &two.subject, NOW), PLUGIN_TRUST_OK);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &two.subject), PLUGIN_TRUST_OK);
    given_file(data, "bin/python3", "#!/bin/sh\necho py2\n", 0755);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &two.subject), PLUGIN_TRUST_UNTRUSTED);
    harness_remove_tree(data);
    free(data);
}

TEST(an_env_interpreter_found_at_another_path_with_the_same_bytes_is_untrusted)
{
    char *data = given_data_dir();
    char both[2 * PATH_BUF];
    char flipped[2 * PATH_BUF];
    const char *argv[3] = {"/usr/bin/env", "python3", "run.sh"};
    TwoCommands two;
    HostLog log;
    RememberEnv env = given_host(&log, true);

    given_wake(data);
    given_file(data, "bin1/python3", "#!/bin/sh\necho py\n", 0755);
    given_file(data, "bin2/python3", "#!/bin/sh\necho py\n", 0755);
    (void)snprintf(both, sizeof(both), "%s/bin1:%s/bin2", data, data);
    (void)snprintf(flipped, sizeof(flipped), "%s/bin2:%s/bin1", data, data);
    given_commands(&two, data, argv, 3U, NULL, 0U);
    two.subject.search_path = both;
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &two.subject, NOW), PLUGIN_TRUST_OK);
    two.subject.search_path = flipped;
    ASSERT_EQ_STATUS(plugin_trust_check(data, &two.subject), PLUGIN_TRUST_UNTRUSTED);
    harness_remove_tree(data);
    free(data);
}

TEST(the_same_code_as_a_hook_is_not_trusted_as_a_plugin_and_back)
{
    char *data = given_data_dir();
    WakeSubject w;

    given_trusted_wake(data, &w);
    w.subject.kind = PLUGIN_TRUST_KIND_HOOK;
    ASSERT_EQ_STATUS(plugin_trust_check(data, &w.subject), PLUGIN_TRUST_UNTRUSTED);
    harness_remove_tree(data);
    free(data);
}

/* A hand-written document of count entries with distinct names. */
static void given_doc_of(const char *data, size_t count)
{
    char path[PATH_BUF];
    FILE *file = NULL;
    size_t i = 0;

    path_of(path, sizeof(path), data, "trust.json");
    // NOLINTNEXTLINE(android-cloexec-fopen)
    file = fopen(path, "w");
    ASSERT_TRUE(file != NULL);
    if (file == NULL) {
        return;
    }
    ASSERT_TRUE(fputs(DOC_HEAD, file) >= 0);
    for (i = 0; i < count; i++) {
        ASSERT_TRUE(fprintf(file,
                            "%s{\"kind\":\"plugin\",\"name\":\"p%zu\",\"path\":\"/x\"," ENTRY_SHAS
                            "\"files\":[]," ENTRY_TAIL,
                            i == 0U ? "" : ",", i) > 0);
    }
    ASSERT_TRUE(fputs("]}", file) >= 0);
    ASSERT_EQ_INT(fclose(file), 0);
}

TEST(a_file_of_1024_entries_is_read_and_one_of_1025_is_not)
{
    char *full = given_data_dir();
    char *over = given_data_dir();
    WakeSubject w;

    given_wake(full);
    given_doc_of(full, (size_t)PLUGIN_TRUST_ENTRIES_MAX);
    given_wake_subject(&w, full);
    ASSERT_EQ_STATUS(plugin_trust_check(full, &w.subject), PLUGIN_TRUST_UNTRUSTED);
    given_wake(over);
    given_doc_of(over, (size_t)PLUGIN_TRUST_ENTRIES_MAX + 1U);
    given_wake_subject(&w, over);
    ASSERT_EQ_STATUS(plugin_trust_check(over, &w.subject), PLUGIN_TRUST_UNREADABLE);
    harness_remove_tree(full);
    harness_remove_tree(over);
    free(full);
    free(over);
}

TEST(a_full_file_takes_no_new_name_and_replaces_an_old_one)
{
    char *data = given_data_dir();
    WakeSubject w;
    HostLog log;
    RememberEnv env = given_host(&log, true);

    given_wake(data);
    given_doc_of(data, (size_t)PLUGIN_TRUST_ENTRIES_MAX);
    given_wake_subject(&w, data);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_UNREADABLE);
    w.subject.name = "p7";
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_OK);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &w.subject), PLUGIN_TRUST_OK);
    harness_remove_tree(data);
    free(data);
}

/* A command whose interpreter file is not there is hashed as text, and records no interpreter. */
TEST(a_command_named_like_an_interpreter_whose_file_is_missing_records_none)
{
    char *data = given_data_dir();
    const char *argv[2] = {"/nonexistent/bin/python3", "run.sh"};
    char *text = NULL;

    given_wake(data);
    ASSERT_EQ_STATUS(allow_command(data, argv, 2U, NULL), PLUGIN_TRUST_OK);
    text = trust_text(data);
    ASSERT_STR_CONTAINS(text, "\"interpreter\":null");
    free(text);
    harness_remove_tree(data);
    free(data);
}

/* A dev plugin (plugin add --dev, H-9.7) keeps its flag in the record and still checks OK. */
TEST(the_dev_flag_is_stored_and_a_dev_record_reads_back)
{
    char *data = given_data_dir();
    WakeSubject w;
    HostLog log;
    RememberEnv env = given_host(&log, true);
    char *text = NULL;

    given_wake(data);
    given_wake_subject(&w, data);
    w.subject.dev = true;
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_OK);
    text = trust_text(data);
    ASSERT_STR_CONTAINS(text, "\"dev\":true}");
    ASSERT_EQ_STATUS(plugin_trust_check(data, &w.subject), PLUGIN_TRUST_OK);
    free(text);
    harness_remove_tree(data);
    free(data);
}

/* The sweep again with an env interpreter, so the search of the PATH meets every allocation. */
TEST(an_allocation_that_fails_in_the_path_search_is_out_of_memory)
{
    int n = 0;
    bool saw_oom = false;
    PluginTrustStatus status = PLUGIN_TRUST_OOM;

    for (n = 0; n < 400 && status != PLUGIN_TRUST_OK; n++) {
        char *data = given_data_dir();
        char bin[PATH_BUF];
        const char *argv[3] = {"/usr/bin/env", "python3", "run.sh"};

        given_wake(data);
        given_file(data, "bin/python3", "#!/bin/sh\necho py\n", 0755);
        path_of(bin, sizeof(bin), data, "bin");
        test_alloc_fail_after(n);
        status = allow_command(data, argv, 3U, bin);
        test_alloc_fail_after(-1);
        if (status == PLUGIN_TRUST_OOM) {
            saw_oom = true;
        } else {
            ASSERT_EQ_INT(status, PLUGIN_TRUST_OK);
        }
        harness_remove_tree(data);
        free(data);
    }
    ASSERT_TRUE(saw_oom);
}

/* An installed plugin: a real plugin.json (needs body and links) and its run.sh. */
#define WAKE_MANIFEST                                                                              \
    "{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"wake\",\"plugin_version\":\"1.0."    \
    "0\","                                                                                         \
    "\"protocol\":1,\"actions\":[{\"name\":\"go\",\"events\":[\"memory.created\"],\"run\":{"       \
    "\"command\":"                                                                                 \
    "[\"run.sh\"]},\"permissions\":{\"needs\":[\"body\",\"links\"]}}]}"

static void given_installed_wake(const char *data)
{
    given_file(data, "plugins/wake/plugin.json", WAKE_MANIFEST, 0644);
    given_file(data, "plugins/wake/run.sh", RUN_SH_TEXT, 0755);
}

/* Allow wake the way plugin add will: load the installed code, then ask. */
static PluginTrustStatus allow_installed(const char *data, const char *name, PluginTrustKind kind,
                                         bool answer, HostLog *log)
{
    PluginTrustInstalled installed;
    RememberEnv env = given_host(log, answer);
    PluginTrustStatus status = PLUGIN_TRUST_OK;

    memset(&installed, 0, sizeof(installed));
    status = plugin_trust_load_installed(data, name, kind, NULL, &installed);
    if (status == PLUGIN_TRUST_OK) {
        status = plugin_trust_allow(data, &env, &installed.subject, NOW);
    }
    plugin_trust_installed_free(&installed);
    return status;
}

TEST(an_installed_plugin_loads_its_commands_and_distinct_needs_from_plugin_json)
{
    char *data = given_data_dir();
    PluginTrustInstalled installed;
    HostLog log;

    given_installed_wake(data);
    ASSERT_EQ_STATUS(allow_installed(data, "wake", PLUGIN_TRUST_KIND_PLUGIN, true, &log),
                     PLUGIN_TRUST_OK);
    ASSERT_STREQ(log.body, "Allow \"wake\" to read your memories? It can send them off this Mac.\n"
                           "body\nlinks");
    memset(&installed, 0, sizeof(installed));
    ASSERT_EQ_STATUS(
        plugin_trust_load_installed(data, "wake", PLUGIN_TRUST_KIND_PLUGIN, NULL, &installed),
        PLUGIN_TRUST_OK);
    ASSERT_EQ_INT(installed.subject.ncommands, 1);
    ASSERT_STREQ(installed.subject.commands[0].argv[0], "run.sh");
    ASSERT_EQ_INT(installed.subject.nneeds, 2);
    plugin_trust_installed_free(&installed);
    harness_remove_tree(data);
    free(data);
}

TEST(check_installed_is_ok_after_allow_and_untrusted_after_an_edit)
{
    char *data = given_data_dir();
    HostLog log;

    given_installed_wake(data);
    ASSERT_EQ_STATUS(plugin_trust_check_installed(data, "wake", NULL), PLUGIN_TRUST_UNTRUSTED);
    ASSERT_EQ_STATUS(allow_installed(data, "wake", PLUGIN_TRUST_KIND_PLUGIN, true, &log),
                     PLUGIN_TRUST_OK);
    ASSERT_EQ_STATUS(plugin_trust_check_installed(data, "wake", NULL), PLUGIN_TRUST_OK);
    given_file(data, "plugins/wake/run.sh", "#!/bin/sh\necho changed\n", 0755);
    ASSERT_EQ_STATUS(plugin_trust_check_installed(data, "wake", NULL), PLUGIN_TRUST_UNTRUSTED);
    harness_remove_tree(data);
    free(data);
}

TEST(check_installed_reads_a_hook_record_as_a_hook)
{
    char *data = given_data_dir();
    HostLog log;

    given_file(
        data, "plugins/ping/plugin.json",
        "{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"ping\",\"plugin_version\":"
        "\"1.0.0\",\"protocol\":1,\"actions\":[{\"name\":\"run\",\"events\":[\"memory.created\"],"
        "\"run\":{\"command\":[\"hook.sh\"]}}]}",
        0644);
    given_file(data, "plugins/ping/hook.sh", TOOL_SH_TEXT, 0755);
    ASSERT_EQ_STATUS(allow_installed(data, "ping", PLUGIN_TRUST_KIND_HOOK, true, &log),
                     PLUGIN_TRUST_OK);
    ASSERT_STREQ(log.kind, "hook");
    ASSERT_EQ_STATUS(plugin_trust_check_installed(data, "ping", NULL), PLUGIN_TRUST_OK);
    harness_remove_tree(data);
    free(data);
}

/* Exploit (plan 33 review): plugin.json swapped between the parse that gives the argv a caller
   runs and the read the hash takes. The approved folder also ships evil.sh; the swapped manifest
   runs it. The check must be about the bytes that were parsed, not the bytes on disk later. */
TEST(a_manifest_swapped_after_the_parse_is_untrusted)
{
    char *data = given_data_dir();
    HostLog log;
    PluginTrustInstalled installed;

    given_installed_wake(data);
    given_file(data, "plugins/wake/evil.sh", "#!/bin/sh\necho evil\n", 0755);
    ASSERT_EQ_STATUS(allow_installed(data, "wake", PLUGIN_TRUST_KIND_PLUGIN, true, &log),
                     PLUGIN_TRUST_OK);
    given_file(
        data, "plugins/wake/plugin.json",
        "{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"wake\",\"plugin_version\":"
        "\"1.0.0\",\"protocol\":1,\"actions\":[{\"name\":\"go\",\"events\":[\"memory.created\"],"
        "\"run\":{\"command\":[\"evil.sh\"]},\"permissions\":{\"needs\":[\"body\",\"links\"]}}]}",
        0644);
    memset(&installed, 0, sizeof(installed));
    ASSERT_EQ_STATUS(
        plugin_trust_load_installed(data, "wake", PLUGIN_TRUST_KIND_PLUGIN, NULL, &installed),
        PLUGIN_TRUST_OK);
    ASSERT_STREQ(installed.subject.commands[0].argv[0], "evil.sh");
    given_file(data, "plugins/wake/plugin.json", WAKE_MANIFEST, 0644);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &installed.subject), PLUGIN_TRUST_UNTRUSTED);
    plugin_trust_installed_free(&installed);
    harness_remove_tree(data);
    free(data);
}

TEST(an_installed_plugin_that_cannot_be_read_is_not_trusted)
{
    char *data = given_data_dir();
    HostLog log;
    PluginTrustInstalled installed;

    memset(&installed, 0, sizeof(installed));
    ASSERT_EQ_STATUS(
        plugin_trust_load_installed(data, "wake", PLUGIN_TRUST_KIND_PLUGIN, NULL, &installed),
        PLUGIN_TRUST_UNHASHABLE);
    given_file(data, "plugins/wake/plugin.json", "{not json", 0644);
    ASSERT_EQ_STATUS(allow_installed(data, "wake", PLUGIN_TRUST_KIND_PLUGIN, true, &log),
                     PLUGIN_TRUST_UNHASHABLE);
    given_file(data, "plugins/wake/plugin.json", WAKE_MANIFEST "", 0644);
    given_file(data, "plugins/other/plugin.json", WAKE_MANIFEST, 0644);
    ASSERT_EQ_STATUS(allow_installed(data, "other", PLUGIN_TRUST_KIND_PLUGIN, true, &log),
                     PLUGIN_TRUST_UNHASHABLE);
    ASSERT_EQ_STATUS(plugin_trust_check_installed(data, "../wake", NULL), PLUGIN_TRUST_UNTRUSTED);
    ASSERT_EQ_STATUS(allow_installed(data, "../wake", PLUGIN_TRUST_KIND_PLUGIN, true, &log),
                     PLUGIN_TRUST_UNHASHABLE);
    ASSERT_EQ_STATUS(allow_installed(data, "", PLUGIN_TRUST_KIND_PLUGIN, true, &log),
                     PLUGIN_TRUST_UNHASHABLE);
    harness_remove_tree(data);
    free(data);
}

TEST(a_hook_whose_manifest_has_two_commands_is_unhashable)
{
    char *data = given_data_dir();
    HostLog log;

    given_installed_wake(data);
    ASSERT_EQ_STATUS(allow_installed(data, "wake", PLUGIN_TRUST_KIND_HOOK, true, &log),
                     PLUGIN_TRUST_OK);
    given_file(
        data, "plugins/wake/plugin.json",
        "{\"format\":\"remember-plugin\",\"version\":1,\"name\":\"wake\",\"plugin_version\":"
        "\"1.0.0\",\"protocol\":1,\"actions\":[{\"name\":\"a\",\"events\":[\"memory.created\"],"
        "\"run\":{\"command\":[\"run.sh\"]}},{\"name\":\"b\",\"events\":[\"memory.created\"],"
        "\"run\":{\"command\":[\"run.sh\"]}}]}",
        0644);
    ASSERT_EQ_STATUS(plugin_trust_check_installed(data, "wake", NULL), PLUGIN_TRUST_UNHASHABLE);
    harness_remove_tree(data);
    free(data);
}

TEST(an_allocation_that_fails_in_the_installed_loader_is_out_of_memory)
{
    char *data = given_data_dir();
    int n = 0;
    bool saw_oom = false;
    PluginTrustStatus status = PLUGIN_TRUST_OOM;
    HostLog log;

    given_installed_wake(data);
    ASSERT_EQ_STATUS(allow_installed(data, "wake", PLUGIN_TRUST_KIND_PLUGIN, true, &log),
                     PLUGIN_TRUST_OK);
    for (n = 0; n < 400 && status != PLUGIN_TRUST_OK; n++) {
        test_alloc_fail_after(n);
        status = plugin_trust_check_installed(data, "wake", NULL);
        test_alloc_fail_after(-1);
        if (status == PLUGIN_TRUST_OOM) {
            saw_oom = true;
        }
        ASSERT_TRUE(status == PLUGIN_TRUST_OOM || status == PLUGIN_TRUST_OK);
    }
    ASSERT_TRUE(saw_oom);
    harness_remove_tree(data);
    free(data);
}

#ifdef REMEMBER_PLUGINS_DIAG_TEST
/* What diag_plugin_approval writes in plain mode (heap) and in JSON mode (the "error" member). */
typedef struct Printed {
    char *plain;
    char *json;
    int rc;
    char pad_[4]; /* explicit tail padding (kept -Wpadded-clean) */
} Printed;

static void printed_free(Printed *printed)
{
    free(printed->plain);
    free(printed->json);
}

/* Close a stream a test opened, so its memory buffer is final. */
static void close_stream(FILE *stream)
{
    if (stream != NULL) {
        ASSERT_EQ_INT(fclose(stream), 0);
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) kind, name, command, then the status
static Printed print_approval(PluginTrustStatus status, PluginTrustKind kind, const char *name,
                              const char *command)
{
    Printed printed = {.plain = NULL, .json = NULL, .rc = 0};
    size_t plain_len = 0;
    size_t json_len = 0;
    FILE *plain = open_memstream(&printed.plain, &plain_len);
    FILE *json = open_memstream(&printed.json, &json_len);

    ASSERT_TRUE(plain != NULL && json != NULL);
    diag_json_end();
    diag_warnings_reset();
    app_set_streams((AppStreams){.out = NULL, .err = plain});
    printed.rc = diag_plugin_approval(status, kind, name, command);
    ASSERT_EQ_INT(diag_json_begin("plugin"), 0);
    (void)diag_plugin_approval(status, kind, name, command);
    if (diag_json_has_error()) {
        ASSERT_EQ_INT(diag_json_write_error(json), 0);
    }
    diag_json_end();
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    close_stream(plain);
    close_stream(json);
    return printed;
}

#define C15_MESSAGE                                                                                \
    "\"wake\" has to be allowed first, and there's no terminal to ask you in. Nothing was "        \
    "installed."
#define C15_FIX "Run 'remember plugin add ./wake' in your own terminal."

TEST(c15_plugin_approval_required_prints_message_and_fix_and_exits_5)
{
    Printed got = print_approval(PLUGIN_TRUST_NO_CALLBACK, PLUGIN_TRUST_KIND_PLUGIN, "wake",
                                 "remember plugin add ./wake");

    ASSERT_EQ_INT(got.rc, REMEMBER_CONFIRM);
    ASSERT_STREQ(got.plain, "remember: " C15_MESSAGE " " C15_FIX "\n");
    ASSERT_STREQ(
        got.json,
        "\"error\":{\"code\":\"plugin_approval_required\",\"message\":\"\\\"wake\\\" has to "
        "be allowed first, and there's no terminal to ask you in. Nothing was "
        "installed.\",\"fix\":\"" C15_FIX "\",\"retryable\":false}");
    printed_free(&got);
}

/* decision:open-2026-11-22-plan33-hook-approval-copy: the C-15 words until Story Weaver words it.
 */
TEST(a_hook_with_no_callback_exits_5_with_the_hook_code_and_the_c15_words)
{
    Printed got = print_approval(PLUGIN_TRUST_NO_CALLBACK, PLUGIN_TRUST_KIND_HOOK, "wake",
                                 "remember plugin add ./wake");

    ASSERT_EQ_INT(got.rc, REMEMBER_CONFIRM);
    ASSERT_STREQ(got.plain, "remember: " C15_MESSAGE " " C15_FIX "\n");
    ASSERT_STR_CONTAINS(got.json, "\"code\":\"hook_approval_required\"");
    printed_free(&got);
}

#define C16_MESSAGE "\"wake\" wasn't allowed, so nothing was installed."
#define C16_FIX "To try again, run 'remember plugin add ./wake' and type \"wake\" to allow it."

TEST(c16_plugin_approval_denied_prints_message_and_fix_and_exits_1)
{
    Printed got = print_approval(PLUGIN_TRUST_DENIED, PLUGIN_TRUST_KIND_PLUGIN, "wake",
                                 "remember plugin add ./wake");

    ASSERT_EQ_INT(got.rc, REMEMBER_ERR);
    ASSERT_STREQ(got.plain, "remember: " C16_MESSAGE " " C16_FIX "\n");
    ASSERT_STREQ(got.json,
                 "\"error\":{\"code\":\"plugin_approval_denied\",\"message\":\"\\\"wake\\\" wasn't "
                 "allowed, so nothing was installed.\",\"fix\":\"To try again, run 'remember "
                 "plugin add ./wake' and type \\\"wake\\\" to allow it.\",\"retryable\":false}");
    printed_free(&got);
}

TEST(a_denied_hook_is_also_plugin_approval_denied)
{
    Printed got = print_approval(PLUGIN_TRUST_DENIED, PLUGIN_TRUST_KIND_HOOK, "wake", "x");

    ASSERT_EQ_INT(got.rc, REMEMBER_ERR);
    ASSERT_STR_CONTAINS(got.json, "\"code\":\"plugin_approval_denied\"");
    printed_free(&got);
}

TEST(a_status_that_is_not_an_approval_outcome_prints_nothing_and_is_minus_1)
{
    Printed got = print_approval(PLUGIN_TRUST_UNTRUSTED, PLUGIN_TRUST_KIND_PLUGIN, "wake", "x");

    ASSERT_EQ_INT(got.rc, -1);
    ASSERT_STREQ(got.plain, "");
    ASSERT_STREQ(got.json, "");
    printed_free(&got);
}

TEST(a_name_with_an_escape_byte_prints_masked_in_plain_and_escaped_in_json)
{
    Printed got = print_approval(PLUGIN_TRUST_NO_CALLBACK, PLUGIN_TRUST_KIND_PLUGIN, "wa\033ke",
                                 "remember plugin add ./wake");

    ASSERT_STR_CONTAINS(got.plain, "\"wa?ke\" has to be allowed first");
    ASSERT_STR_CONTAINS(got.json, "\\\"wa\\u001bke\\\" has to be allowed first");
    printed_free(&got);
}

/* The CLI's confirm: it prints the body, asks for the name, and says yes to the name only. */
typedef struct Typed {
    char *shown; /* what the CLI wrote on the error stream */
    bool answer;
    char pad_[7]; /* explicit tail padding (kept -Wpadded-clean) */
} Typed;

/* Run cli_confirm_typed_name with input as what the person types. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the name, the body, then the typed text
static Typed typed_answer(const char *name, const char *body, const char *input)
{
    Typed typed = {.shown = NULL, .answer = false};
    size_t len = 0;
    FILE *err = open_memstream(&typed.shown, &len);
    FILE *in = tmpfile();

    ASSERT_TRUE(err != NULL && in != NULL);
    if (err == NULL || in == NULL) {
        close_stream(err);
        close_stream(in);
        return typed;
    }
    ASSERT_TRUE(fputs(input, in) >= 0);
    ASSERT_EQ_INT(fseek(in, 0L, SEEK_SET), 0);
    app_set_streams((AppStreams){.out = NULL, .err = err});
    typed.answer = cli_confirm_typed_name(in, "plugin", name, body);
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    close_stream(err);
    close_stream(in);
    return typed;
}

#define BODY_TEXT "Allow \"wake\" to read your memories? It can send them off this Mac.\nlinks"
#define ASK_TEXT "Type \"wake\" to allow it, or press Enter to cancel: "

TEST(typed_name_must_match)
{
    Typed yes = typed_answer("wake", BODY_TEXT, "wake\n");

    ASSERT_TRUE(yes.answer);
    ASSERT_STREQ(yes.shown, BODY_TEXT "\n" ASK_TEXT);
    free(yes.shown);
}

TEST(a_typed_text_that_is_not_the_name_is_no)
{
    static const char *const k_wrong[] = {"wak\n",    "wakee\n", "WAKE\n", " wake\n", "wake \n",
                                          "wake\r\n", "\n",      "",       "wake",    "ke\n"};
    size_t i = 0;

    for (i = 0; i < sizeof(k_wrong) / sizeof(k_wrong[0]); i++) {
        Typed typed = typed_answer("wake", BODY_TEXT, k_wrong[i]);

        ASSERT_FALSE(typed.answer);
        free(typed.shown);
    }
}

TEST(a_line_longer_than_the_prompt_buffer_is_no)
{
    char line[400];
    Typed typed;

    memset(line, 'w', sizeof(line) - 2U);
    line[sizeof(line) - 2U] = '\n';
    line[sizeof(line) - 1U] = '\0';
    typed = typed_answer("wake", BODY_TEXT, line);
    ASSERT_FALSE(typed.answer);
    free(typed.shown);
}

TEST(the_body_is_shown_with_its_escape_bytes_masked)
{
    Typed typed = typed_answer("wake", "Allow\n\033[2Jx", "\n");

    ASSERT_FALSE(typed.answer);
    ASSERT_STREQ(typed.shown, "Allow\n?[2Jx\n" ASK_TEXT);
    free(typed.shown);
}

/* Old plan 25 probe 1, through the whole path: the name typed is a recorded trust; anything else
   records nothing. */
TEST(the_cli_confirm_records_trust_only_for_the_typed_name)
{
    char *data = given_data_dir();
    WakeSubject w;
    FILE *in = tmpfile();
    RememberEnv env;
    size_t len = 0;
    char *shown = NULL;
    FILE *err = open_memstream(&shown, &len);

    ASSERT_TRUE(in != NULL && err != NULL);
    if (in == NULL || err == NULL) {
        close_stream(err);
        close_stream(in);
        return;
    }
    memset(&env, 0, sizeof(env));
    env.confirm = cli_confirm_typed_name;
    env.callback_ctx = in;
    given_wake(data);
    given_wake_subject(&w, data);
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_TRUE(fputs("nope\nwake\n", in) >= 0);
    ASSERT_EQ_INT(fseek(in, 0L, SEEK_SET), 0);
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_DENIED);
    ASSERT_FALSE(exists_at(data, "trust.json"));
    ASSERT_EQ_STATUS(plugin_trust_allow(data, &env, &w.subject, NOW), PLUGIN_TRUST_OK);
    ASSERT_EQ_STATUS(plugin_trust_check(data, &w.subject), PLUGIN_TRUST_OK);
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    close_stream(err);
    close_stream(in);
    free(shown);
    harness_remove_tree(data);
    free(data);
}

/* 007 test 23, decision:2026-11-22-plan33-enable-guard-error: C-17's two sentences as an error. */
#define C17_LINE                                                                                   \
    "remember: \"wake\" changed since you allowed it, so it didn't run. Run 'remember plugin "     \
    "trust wake' to review the changes and allow it again.\n"

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) the brain, the key, then the value
static CmdResult run_set(const char *db, const char *key, const char *value, bool json)
{
    const char *const plain[] = {"settings", "set", key, value, "--scope", "brain"};
    const char *const as_json[] = {"--json", "settings", "set", key, value, "--scope", "brain"};

    if (json) {
        return run_remember(db, as_json, sizeof(as_json) / sizeof(as_json[0]), NULL);
    }
    return run_remember(db, plain, sizeof(plain) / sizeof(plain[0]), NULL);
}

/* <XDG_DATA_HOME>/remember, the data dir of the CLI under test (heap). */
static char *given_cli_data_dir(void)
{
    const char *xdg = getenv("XDG_DATA_HOME"); // NOLINT(concurrency-mt-unsafe)
    size_t cap = (xdg != NULL ? strlen(xdg) : 0U) + sizeof("/remember");
    char *dir = (char *)malloc(cap);

    ASSERT_TRUE(xdg != NULL && dir != NULL);
    if (xdg == NULL || dir == NULL) {
        abort();
    }
    (void)snprintf(dir, cap, "%s/remember", xdg);
    return dir;
}

TEST(untrusted_enable_setting_fails_c17)
{
    char *data = given_cli_data_dir();
    char *db = make_temp_db_path();
    HostLog log;
    CmdResult got;

    given_installed_wake(data);
    got = run_set(db, "plugin.wake.enabled", "true", false);
    ASSERT_EQ_INT(got.exit_code, REMEMBER_ERR);
    ASSERT_STREQ(got.err, C17_LINE);
    ASSERT_STREQ(got.out, "");
    cmd_result_free(&got);
    got = run_set(db, "plugin.wake.enabled", "true", true);
    ASSERT_EQ_INT(got.exit_code, REMEMBER_ERR);
    ASSERT_STR_CONTAINS(got.out, "\"code\":\"plugin_untrusted\"");
    ASSERT_STR_CONTAINS(got.out, "\"fix\":\"Run 'remember plugin trust wake' to review the changes "
                                 "and allow it again.\"");
    cmd_result_free(&got);
    ASSERT_EQ_STATUS(allow_installed(data, "wake", PLUGIN_TRUST_KIND_PLUGIN, true, &log),
                     PLUGIN_TRUST_OK);
    got = run_set(db, "plugin.wake.enabled", "true", false);
    ASSERT_EQ_INT(got.exit_code, 0);
    cmd_result_free(&got);
    free(db);
    free(data);
}

/* 010 test 13: the edit does not turn the setting off, and the guard asks again on a new set. */
TEST(an_edit_after_trust_keeps_enabled_and_refuses_the_next_set_true)
{
    char *data = given_cli_data_dir();
    char *db = make_temp_db_path();
    HostLog log;
    CmdResult got;
    const char *const get_args[] = {"settings", "get", "plugin.wake.enabled", "--scope", "brain"};

    given_installed_wake(data);
    ASSERT_EQ_STATUS(allow_installed(data, "wake", PLUGIN_TRUST_KIND_PLUGIN, true, &log),
                     PLUGIN_TRUST_OK);
    got = run_set(db, "plugin.wake.enabled", "true", false);
    ASSERT_EQ_INT(got.exit_code, 0);
    cmd_result_free(&got);
    given_file(data, "plugins/wake/run.sh", "#!/bin/sh\necho changed\n", 0755);
    got = run_remember(db, get_args, sizeof(get_args) / sizeof(get_args[0]), NULL);
    ASSERT_STR_CONTAINS(got.out, "true");
    cmd_result_free(&got);
    got = run_set(db, "plugin.wake.enabled", "true", false);
    ASSERT_EQ_INT(got.exit_code, REMEMBER_ERR);
    ASSERT_STREQ(got.err, C17_LINE);
    cmd_result_free(&got);
    got = run_set(db, "plugin.wake.enabled", "false", false);
    ASSERT_EQ_INT(got.exit_code, 0);
    cmd_result_free(&got);
    free(db);
    free(data);
}

TEST(the_built_in_plugin_enables_without_a_record)
{
    char *db = make_temp_db_path();
    CmdResult got = run_set(db, "plugin.hybrid.enabled", "true", false);

    ASSERT_EQ_INT(got.exit_code, 0);
    cmd_result_free(&got);
    free(db);
}

/* An installed folder whose trust.json cannot be used reads as untrusted, and is left alone. */
TEST(an_unreadable_trust_json_refuses_the_enable_and_is_not_written_over)
{
    char *data = given_cli_data_dir();
    char *db = make_temp_db_path();
    char path[PATH_BUF];
    char *after = NULL;
    CmdResult got;

    given_installed_wake(data);
    path_of(path, sizeof(path), data, "trust.json");
    harness_write_file(path, "not json");
    got = run_set(db, "plugin.wake.enabled", "true", false);
    ASSERT_EQ_INT(got.exit_code, REMEMBER_ERR);
    ASSERT_STREQ(got.err, C17_LINE);
    cmd_result_free(&got);
    after = trust_text(data);
    ASSERT_STREQ(after, "not json");
    free(after);
    free(db);
    free(data);
}

TEST(c17_the_untrusted_warning_and_error_say_the_same_two_sentences)
{
    char *warn_buf = NULL;
    char *err_buf = NULL;
    size_t warn_len = 0;
    size_t err_len = 0;
    FILE *warn = open_memstream(&warn_buf, &warn_len);
    FILE *err = open_memstream(&err_buf, &err_len);

    ASSERT_TRUE(warn != NULL && err != NULL);
    diag_json_end();
    diag_warnings_reset();
    app_set_streams((AppStreams){.out = NULL, .err = warn});
    ASSERT_EQ_INT(diag_plugin_untrusted_warning("wake"), 0);
    app_set_streams((AppStreams){.out = NULL, .err = err});
    ASSERT_EQ_INT(diag_plugin_untrusted_error("wake"), REMEMBER_ERR);
    app_set_streams((AppStreams){.out = NULL, .err = NULL});
    diag_warnings_reset();
    close_stream(warn);
    close_stream(err);
    ASSERT_STREQ(warn_buf,
                 "remember: warning: \"wake\" changed since you allowed it, so it didn't run. Run "
                 "'remember plugin trust wake' to review the changes and allow it again.\n");
    ASSERT_STREQ(err_buf,
                 "remember: \"wake\" changed since you allowed it, so it didn't run. Run "
                 "'remember plugin trust wake' to review the changes and allow it again.\n");
    free(warn_buf);
    free(err_buf);
}

TEST(the_untrusted_warning_in_json_has_a_message_and_a_non_empty_fix)
{
    char *buf = NULL;
    size_t len = 0;
    FILE *out = open_memstream(&buf, &len);

    ASSERT_TRUE(out != NULL);
    diag_warnings_reset();
    ASSERT_EQ_INT(diag_json_begin("plugin"), 0);
    ASSERT_EQ_INT(diag_plugin_untrusted_warning("wa\033ke"), 0);
    ASSERT_EQ_INT(diag_json_write_warnings(out), 0);
    diag_json_end();
    diag_warnings_reset();
    close_stream(out);
    ASSERT_STR_CONTAINS(buf, "\"code\":\"plugin_untrusted\"");
    ASSERT_STR_CONTAINS(buf,
                        "\"message\":\"\\\"wa\\u001bke\\\" changed since you allowed it, so it "
                        "didn't run.\"");
    ASSERT_STR_CONTAINS(buf, "\"fix\":\"Run 'remember plugin trust wa\\u001bke' to review the "
                             "changes and allow it again.\"");
    free(buf);
}
#endif /* REMEMBER_PLUGINS_DIAG_TEST */

void register_plugin_trust_tests(void)
{
    RUN_TEST(record_is_data_dir_trust_json);
    RUN_TEST(record_fields_h_9_2);
    RUN_TEST(hash_argv_files_max_8);
    RUN_TEST(a_file_named_twice_and_a_file_in_the_folder_do_not_count_toward_the_8);
    RUN_TEST(a_later_argv_item_that_is_a_folder_or_missing_is_text);
    RUN_TEST(interpreter_hashed);
    RUN_TEST(interpreter_after_usr_bin_env_is_found_in_the_search_path);
    RUN_TEST(an_env_interpreter_that_the_search_path_does_not_find_is_not_recorded);
    RUN_TEST(env_followed_by_a_name_that_is_not_an_interpreter_records_none);
    RUN_TEST(two_different_interpreters_in_one_plugin_are_unhashable);
    RUN_TEST(sh_c_inline_in_manifest_hash);
    RUN_TEST(null_confirm_exits_5_c15_records_nothing);
    RUN_TEST(null_confirm_hook_exits_5_records_nothing);
    RUN_TEST(false_confirm_exits_1_c16_records_nothing);
    RUN_TEST(a_denied_confirm_leaves_an_existing_trust_json_as_it_was);
    RUN_TEST(the_host_gets_the_plugin_kind_name_and_folder_body);
    RUN_TEST(the_host_gets_the_hook_kind_name_and_hook_body);
    RUN_TEST(an_update_shows_only_the_needs_the_old_record_lacks);
    RUN_TEST(no_env_confirm_is_never_a_yes);
    RUN_TEST(check_is_ok_for_the_code_that_was_allowed);
    RUN_TEST(check_is_untrusted_when_nothing_was_allowed);
    RUN_TEST(a_record_of_another_name_does_not_trust_this_plugin);
    RUN_TEST(a_changed_folder_file_is_untrusted);
    RUN_TEST(a_new_file_in_the_folder_is_untrusted);
    RUN_TEST(a_removed_folder_file_is_untrusted);
    RUN_TEST(a_changed_executable_bit_is_untrusted);
    RUN_TEST(a_changed_manifest_is_untrusted);
    RUN_TEST(a_changed_sh_c_inline_text_is_untrusted);
    RUN_TEST(a_changed_argv_file_is_untrusted);
    RUN_TEST(an_argv_item_that_becomes_a_file_is_untrusted);
    RUN_TEST(a_changed_interpreter_is_untrusted);
    RUN_TEST(an_env_interpreter_that_a_new_search_path_finds_is_untrusted);
    RUN_TEST(a_record_of_a_plugin_does_not_trust_a_hook_of_that_name);
    RUN_TEST(a_file_touched_with_the_same_bytes_is_still_trusted);
    RUN_TEST(mismatch_sets_untrusted_keeps_enabled);
    RUN_TEST(trust_clears_reason);
    RUN_TEST(a_hook_is_trusted_by_its_command_file_and_untrusted_when_that_changes);
    RUN_TEST(builtin_trusted_no_record);
    RUN_TEST(a_hook_named_hybrid_is_not_the_built_in);
    RUN_TEST(a_hand_written_trust_json_of_the_decided_shape_is_read);
    RUN_TEST(a_trust_json_that_is_not_the_document_is_unreadable_and_never_written_over);
    RUN_TEST(a_trust_json_over_a_mebibyte_is_unreadable);
    RUN_TEST(a_trust_json_that_is_a_folder_or_a_fifo_is_unreadable_and_does_not_hang);
    RUN_TEST(a_symlink_in_the_folder_makes_it_unhashable);
    RUN_TEST(a_symlinked_folder_inside_the_plugin_makes_it_unhashable);
    RUN_TEST(a_fifo_in_the_folder_makes_it_unhashable);
    RUN_TEST(the_plugin_folder_itself_as_a_symlink_is_unhashable);
    RUN_TEST(a_folder_file_that_cannot_be_read_makes_it_unhashable);
    RUN_TEST(an_argv_file_that_exists_and_cannot_be_read_is_unhashable);
    RUN_TEST(a_folder_16_levels_deep_is_hashed_and_17_is_not);
    RUN_TEST(a_folder_of_4096_files_is_hashed_and_4097_is_not);
    RUN_TEST(a_folder_of_4097_folders_is_unhashable);
    RUN_TEST(a_file_over_512_mebibytes_is_unhashable_and_one_of_exactly_that_is_hashed);
    RUN_TEST(a_missing_folder_or_manifest_or_one_over_the_reader_cap_is_unhashable);
    RUN_TEST(a_path_or_name_that_is_not_utf8_is_unhashable);
    RUN_TEST(a_subject_the_caller_built_wrong_is_unhashable);
    RUN_TEST(a_hook_needs_exactly_one_command);
    RUN_TEST(an_argv_fifo_a_folder_and_a_directory_named_as_the_interpreter_are_not_files);
    RUN_TEST(two_plugins_share_one_file_and_an_update_replaces_only_its_own_entry);
    RUN_TEST(a_stale_or_symlinked_temp_file_is_not_followed_and_none_is_left);
    RUN_TEST(a_data_dir_that_is_not_a_folder_is_an_io_failure_after_the_host_was_asked_once);
    RUN_TEST(a_data_dir_that_does_not_exist_yet_is_made);
    RUN_TEST(a_held_lock_makes_the_write_busy_and_leaves_the_file_alone);
    RUN_TEST(the_file_path_is_trust_json_in_the_data_dir);
    RUN_TEST(a_command_named_like_an_interpreter_whose_file_is_missing_records_none);
    RUN_TEST(the_dev_flag_is_stored_and_a_dev_record_reads_back);
    RUN_TEST(an_allocation_that_fails_in_the_path_search_is_out_of_memory);
    RUN_TEST(a_folder_of_4096_folders_is_hashed);
    RUN_TEST(eight_files_over_two_commands_are_hashed_and_nine_are_not);
    RUN_TEST(a_file_both_commands_name_counts_once_toward_the_8);
    RUN_TEST(a_bare_env_command_with_no_name_after_it_records_no_interpreter);
    RUN_TEST(inline_text_after_dash_c_of_a_program_that_is_not_a_shell_is_not_in_the_manifest_hash);
    RUN_TEST(the_same_bytes_under_another_argv_path_are_untrusted);
    RUN_TEST(an_env_interpreter_whose_bytes_change_is_untrusted);
    RUN_TEST(an_env_interpreter_found_at_another_path_with_the_same_bytes_is_untrusted);
    RUN_TEST(the_same_code_as_a_hook_is_not_trusted_as_a_plugin_and_back);
    RUN_TEST(a_file_of_1024_entries_is_read_and_one_of_1025_is_not);
    RUN_TEST(a_full_file_takes_no_new_name_and_replaces_an_old_one);
    RUN_TEST(an_argv_file_in_a_folder_that_only_shares_the_plugin_folders_name_prefix_is_recorded);
    RUN_TEST(the_folder_hash_does_not_depend_on_the_order_the_files_were_made);
    RUN_TEST(an_allocation_that_fails_in_allow_is_out_of_memory_and_writes_nothing);
    RUN_TEST(an_allocation_that_fails_in_check_is_out_of_memory_never_untrusted_or_trusted);
    RUN_TEST(an_allocation_that_fails_in_the_installed_loader_is_out_of_memory);
    RUN_TEST(an_installed_plugin_loads_its_commands_and_distinct_needs_from_plugin_json);
    RUN_TEST(check_installed_is_ok_after_allow_and_untrusted_after_an_edit);
    RUN_TEST(check_installed_reads_a_hook_record_as_a_hook);
    RUN_TEST(a_manifest_swapped_after_the_parse_is_untrusted);
    RUN_TEST(an_installed_plugin_that_cannot_be_read_is_not_trusted);
    RUN_TEST(a_hook_whose_manifest_has_two_commands_is_unhashable);
#ifdef REMEMBER_PLUGINS_DIAG_TEST
    RUN_TEST(c15_plugin_approval_required_prints_message_and_fix_and_exits_5);
    RUN_TEST(a_hook_with_no_callback_exits_5_with_the_hook_code_and_the_c15_words);
    RUN_TEST(c16_plugin_approval_denied_prints_message_and_fix_and_exits_1);
    RUN_TEST(a_denied_hook_is_also_plugin_approval_denied);
    RUN_TEST(a_status_that_is_not_an_approval_outcome_prints_nothing_and_is_minus_1);
    RUN_TEST(a_name_with_an_escape_byte_prints_masked_in_plain_and_escaped_in_json);
    RUN_TEST(typed_name_must_match);
    RUN_TEST(a_typed_text_that_is_not_the_name_is_no);
    RUN_TEST(a_line_longer_than_the_prompt_buffer_is_no);
    RUN_TEST(the_body_is_shown_with_its_escape_bytes_masked);
    RUN_TEST(the_cli_confirm_records_trust_only_for_the_typed_name);
    RUN_TEST(c17_the_untrusted_warning_and_error_say_the_same_two_sentences);
    RUN_TEST(the_untrusted_warning_in_json_has_a_message_and_a_non_empty_fix);
    RUN_TEST(untrusted_enable_setting_fails_c17);
    RUN_TEST(an_edit_after_trust_keeps_enabled_and_refuses_the_next_set_true);
    RUN_TEST(the_built_in_plugin_enables_without_a_record);
    RUN_TEST(an_unreadable_trust_json_refuses_the_enable_and_is_not_written_over);
#endif
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
