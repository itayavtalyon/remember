#include "cli.h"

#include <stddef.h>
#include <string.h>

/* ---- argument cursor ----------------------------------------------------- */

typedef struct {
    int i;
    int argc;
    char *const *argv;
} ArgCursor;

static void cursor_init(ArgCursor *c, int argc, char *const *argv)
{
    c->i = 1;
    c->argc = argc;
    c->argv = argv;
}

static bool cursor_done(const ArgCursor *c)
{
    if (c->i >= c->argc) {
        return true;
    }
    return false;
}

static const char *cursor_peek(const ArgCursor *c)
{
    if (cursor_done(c)) {
        return NULL;
    }
    return c->argv[c->i];
}

static const char *cursor_next(ArgCursor *c)
{
    const char *arg;

    if (cursor_done(c)) {
        return NULL;
    }
    arg = c->argv[c->i];
    c->i++;
    return arg;
}

/* ---- command table + open-addressing hash map ---------------------------- */

typedef struct {
    const char *name;
    CliCommand command;
    const char *summary;
} CommandEntry;

static const CommandEntry k_commands[] = {
    {"help", CLI_CMD_HELP, "Show help"},
    {"version", CLI_CMD_VERSION, "Show version"},
    {"add", CLI_CMD_ADD, "Store a memory (optional --key, --tag, --source)"},
    {"search", CLI_CMD_SEARCH, "Full-text search"},
    {"list", CLI_CMD_LIST, "List memories with filters"},
    {"get", CLI_CMD_GET, "Fetch one entry by id or --key"},
    {"update", CLI_CMD_UPDATE, "Change body and/or tags by id or --key"},
    {"delete", CLI_CMD_DELETE, "Remove an entry by id or --key"},
};

enum { COMMAND_COUNT = (int)(sizeof(k_commands) / sizeof(k_commands[0])) };
enum { CMD_MAP_SIZE = 32 };

static int g_cmd_map[CMD_MAP_SIZE];
static bool g_cmd_map_ready;

static unsigned cmd_hash(const char *s)
{
    unsigned h = 5381U;
    while (*s != '\0') {
        h = ((h << 5) + h) + (unsigned char)(*s);
        s++;
    }
    return h;
}

static void cmd_map_init(void)
{
    int i;

    if (g_cmd_map_ready) {
        return;
    }
    for (i = 0; i < CMD_MAP_SIZE; i++) {
        g_cmd_map[i] = -1;
    }
    for (i = 0; i < COMMAND_COUNT; i++) {
        unsigned h = cmd_hash(k_commands[i].name) % (unsigned)CMD_MAP_SIZE;
        while (g_cmd_map[h] != -1) {
            h = (h + 1U) % (unsigned)CMD_MAP_SIZE;
        }
        g_cmd_map[h] = i;
    }
    g_cmd_map_ready = true;
}

static const CommandEntry *cmd_lookup(const char *name)
{
    unsigned h;
    unsigned probes;

    if (name == NULL) {
        return NULL;
    }
    cmd_map_init();
    h = cmd_hash(name) % (unsigned)CMD_MAP_SIZE;
    for (probes = 0; probes < (unsigned)CMD_MAP_SIZE; probes++) {
        int idx = g_cmd_map[h];
        if (idx < 0) {
            return NULL;
        }
        if (strcmp(k_commands[idx].name, name) == 0) {
            return &k_commands[idx];
        }
        h = (h + 1U) % (unsigned)CMD_MAP_SIZE;
    }
    return NULL;
}

static CliCommand command_from_name(const char *name)
{
    const CommandEntry *e = cmd_lookup(name);
    return e != NULL ? e->command : CLI_CMD_UNKNOWN;
}

const char *cli_command_name(CliCommand cmd)
{
    int i;
    for (i = 0; i < COMMAND_COUNT; i++) {
        if (k_commands[i].command == cmd) {
            return k_commands[i].name;
        }
    }
    return NULL;
}

const char *cli_command_summary(CliCommand cmd)
{
    int i;
    for (i = 0; i < COMMAND_COUNT; i++) {
        if (k_commands[i].command == cmd) {
            return k_commands[i].summary;
        }
    }
    return NULL;
}

const char *cli_error_message(CliError err)
{
    switch (err) {
    case CLI_ERR_OK:
        return "ok";
    case CLI_ERR_MISSING_COMMAND:
        return "missing command (try --help)";
    case CLI_ERR_UNKNOWN_COMMAND:
        return "unknown command";
    case CLI_ERR_UNKNOWN_OPTION:
        return "unknown option";
    case CLI_ERR_MISSING_OPTION_VALUE:
        return "option requires a value";
    case CLI_ERR_INTERNAL:
        return "internal argument error";
    default:
        return "unknown error";
    }
}

/* ---- parse --------------------------------------------------------------- */

static void cli_args_clear(CliArgs *out)
{
    out->error = CLI_ERR_OK;
    out->error_arg = NULL;
    out->error_option = NULL;
    out->command = CLI_CMD_NONE;
    out->help_topic = CLI_CMD_NONE;
    out->globals.db_path = NULL;
    out->globals.json = false;
    out->rest_argc = 0;
    out->rest_argv = NULL;
}

static bool is_help_flag(const char *arg)
{
    if (strcmp(arg, "--help") == 0) {
        return true;
    }
    if (strcmp(arg, "-h") == 0) {
        return true;
    }
    return false;
}

static bool is_version_flag(const char *arg)
{
    if (strcmp(arg, "--version") == 0) {
        return true;
    }
    if (strcmp(arg, "-V") == 0) {
        return true;
    }
    return false;
}

static bool looks_like_option(const char *arg)
{
    if (arg == NULL) {
        return false;
    }
    if (arg[0] != '-') {
        return false;
    }
    if (arg[1] == '\0') {
        return false;
    }
    return true;
}

/* Consume a global at cursor if present. true = consumed (or failed in out). */
static bool take_global(ArgCursor *cur, CliArgs *out)
{
    const char *arg = cursor_peek(cur);

    if (arg == NULL) {
        return false;
    }
    if (strcmp(arg, "--json") == 0) {
        (void)cursor_next(cur);
        out->globals.json = true;
        return true;
    }
    if (strcmp(arg, "--db") == 0) {
        (void)cursor_next(cur);
        if (cursor_done(cur) || looks_like_option(cursor_peek(cur))) {
            out->error = CLI_ERR_MISSING_OPTION_VALUE;
            out->error_option = "--db";
            return true;
        }
        out->globals.db_path = cursor_next(cur);
        return true;
    }
    if (strncmp(arg, "--db=", 5) == 0) {
        (void)cursor_next(cur);
        if (arg[5] == '\0') {
            out->error = CLI_ERR_MISSING_OPTION_VALUE;
            out->error_option = "--db";
            return true;
        }
        out->globals.db_path = arg + 5;
        return true;
    }
    return false;
}

static bool resolve_help_topic_token(CliArgs *out, const char *token)
{
    CliCommand topic;

    if (token == NULL || looks_like_option(token)) {
        return true;
    }
    topic = command_from_name(token);
    if (topic == CLI_CMD_UNKNOWN) {
        out->error = CLI_ERR_UNKNOWN_COMMAND;
        out->error_arg = token;
        return false;
    }
    if (topic != CLI_CMD_HELP && topic != CLI_CMD_VERSION) {
        out->help_topic = topic;
    }
    return true;
}

static void apply_help_request(CliArgs *out, int command_index)
{
    out->help_topic = CLI_CMD_NONE;
    if (command_index >= 0 && out->command != CLI_CMD_HELP && out->command != CLI_CMD_VERSION) {
        out->help_topic = out->command;
    } else if (out->command == CLI_CMD_HELP && out->rest_argc > 0 && out->rest_argv != NULL) {
        if (!resolve_help_topic_token(out, out->rest_argv[0])) {
            return;
        }
    }
    out->command = CLI_CMD_HELP;
    out->error = CLI_ERR_OK;
}

static void apply_bare_help_command(CliArgs *out)
{
    out->help_topic = CLI_CMD_NONE;
    if (out->rest_argc > 0 && out->rest_argv != NULL) {
        if (!resolve_help_topic_token(out, out->rest_argv[0])) {
            return;
        }
    }
    out->error = CLI_ERR_OK;
}

static bool take_subcommand(ArgCursor *cur, CliArgs *out, int at, int *command_index)
{
    const char *arg = cursor_peek(cur);

    if (looks_like_option(arg)) {
        out->error = CLI_ERR_UNKNOWN_OPTION;
        out->error_arg = arg;
        return false;
    }
    (void)cursor_next(cur);
    out->command = command_from_name(arg);
    if (out->command == CLI_CMD_UNKNOWN) {
        out->error = CLI_ERR_UNKNOWN_COMMAND;
        out->error_arg = arg;
        return false;
    }
    *command_index = at;
    return true;
}

static void scan_argv(ArgCursor *cur, CliArgs *out, bool *want_help, bool *want_version,
                      int *command_index)
{
    while (!cursor_done(cur)) {
        const char *arg = cursor_peek(cur);
        int at = cur->i;

        if (arg == NULL) {
            (void)cursor_next(cur);
            continue;
        }
        if (is_help_flag(arg)) {
            (void)cursor_next(cur);
            *want_help = true;
            continue;
        }
        if (is_version_flag(arg)) {
            (void)cursor_next(cur);
            *want_version = true;
            continue;
        }
        if (take_global(cur, out)) {
            if (out->error != CLI_ERR_OK) {
                return;
            }
            continue;
        }
        if (*command_index < 0) {
            if (!take_subcommand(cur, out, at, command_index)) {
                return;
            }
            continue;
        }
        (void)cursor_next(cur);
    }
}

void cli_parse(int argc, char *const *argv, CliArgs *out)
{
    ArgCursor cur;
    bool want_help = false;
    bool want_version = false;
    int command_index = -1;

    if (out == NULL) {
        return;
    }
    cli_args_clear(out);

    if (argc < 1 || argv == NULL) {
        out->error = CLI_ERR_INTERNAL;
        return;
    }

    cursor_init(&cur, argc, argv);
    scan_argv(&cur, out, &want_help, &want_version, &command_index);
    if (out->error != CLI_ERR_OK) {
        return;
    }

    if (command_index >= 0) {
        int rest_start = command_index + 1;
        if (rest_start < argc) {
            out->rest_argc = argc - rest_start;
            out->rest_argv = &argv[rest_start];
        }
    }

    if (want_help) {
        apply_help_request(out, command_index);
        return;
    }
    if (want_version) {
        out->command = CLI_CMD_VERSION;
        out->error = CLI_ERR_OK;
        return;
    }
    if (command_index < 0) {
        out->command = CLI_CMD_NONE;
        out->error = CLI_ERR_MISSING_COMMAND;
        return;
    }
    if (out->command == CLI_CMD_HELP) {
        apply_bare_help_command(out);
        return;
    }
    out->error = CLI_ERR_OK;
}
