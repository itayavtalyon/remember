#include "cli.h"

#include <stddef.h>
#include <stdlib.h>
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

/* ---- command table ------------------------------------------------------- */

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
    {"tags", CLI_CMD_TAGS, "List all tags with entry counts"},
};

enum { COMMAND_COUNT = (int)(sizeof(k_commands) / sizeof(k_commands[0])) };

/* Linear scan: eight fixed entries. A hash map would be more code and mutable
   state, and slower at this size. */
static const CommandEntry *cmd_lookup(const char *name)
{
    int i;

    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < COMMAND_COUNT; i++) {
        if (strcmp(k_commands[i].name, name) == 0) {
            return &k_commands[i];
        }
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
    {
        static const char db_eq[] = "--db=";
        const size_t db_eq_len = sizeof(db_eq) - 1U;
        if (strncmp(arg, db_eq, db_eq_len) == 0) {
            (void)cursor_next(cur);
            if (arg[db_eq_len] == '\0') {
                out->error = CLI_ERR_MISSING_OPTION_VALUE;
                out->error_option = "--db";
                return true;
            }
            out->globals.db_path = arg + db_eq_len;
            return true;
        }
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

/* Append a subcommand-owned token to rest_argv (capacity is argc, never exceeded). */
static void rest_push(CliArgs *out, const char *tok)
{
    out->rest_argv[out->rest_argc] = tok;
    out->rest_argc++;
}

/* Record arg as the subcommand. arg is already known not to be a global/flag. */
static bool accept_subcommand(CliArgs *out, const char *arg, int at, int *command_index)
{
    out->command = command_from_name(arg);
    if (out->command == CLI_CMD_UNKNOWN) {
        out->error = CLI_ERR_UNKNOWN_COMMAND;
        out->error_arg = arg;
        return false;
    }
    *command_index = at;
    return true;
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
    return accept_subcommand(out, arg, at, command_index);
}

typedef struct {
    bool want_help;
    bool want_version;
    bool end_of_options;
    int command_index;
} ScanState;

/* Handle one token after "--": literal subcommand (if none yet) or rest arg. */
static bool scan_literal(ArgCursor *cur, CliArgs *out, const char *arg, int at, ScanState *st)
{
    (void)cursor_next(cur);
    if (st->command_index < 0) {
        return accept_subcommand(out, arg, at, &st->command_index);
    }
    rest_push(out, arg);
    return true;
}

/* Handle one token during normal option processing. false = stop (error set). */
static bool scan_option(ArgCursor *cur, CliArgs *out, const char *arg, int at, ScanState *st)
{
    if (strcmp(arg, "--") == 0) {
        (void)cursor_next(cur);
        st->end_of_options = true;
        /* After a subcommand is chosen, forward `--` into rest_argv so commands
         * can treat a following "-" as a literal body (not stdin). Before a
         * subcommand, `--` only ends global option scanning. */
        if (st->command_index >= 0) {
            rest_push(out, "--");
        }
        return true;
    }
    if (is_help_flag(arg)) {
        (void)cursor_next(cur);
        st->want_help = true;
        return true;
    }
    if (is_version_flag(arg)) {
        (void)cursor_next(cur);
        st->want_version = true;
        return true;
    }
    if (take_global(cur, out)) {
        return out->error == CLI_ERR_OK;
    }
    if (st->command_index < 0) {
        return take_subcommand(cur, out, at, &st->command_index);
    }
    (void)cursor_next(cur);
    rest_push(out, arg);
    return true;
}

/*
 * Single pass over argv. Globals (--db/--json) and meta flags (--help/--version)
 * are recognized wherever they appear and stripped out; the first bare token is
 * the subcommand; every other token becomes a subcommand arg in rest_argv. A
 * "--" ends option processing: subsequent tokens are literal, even if they look
 * like flags or globals.
 */
static void scan_argv(ArgCursor *cur, CliArgs *out, ScanState *st)
{
    while (!cursor_done(cur)) {
        const char *arg = cursor_peek(cur);
        int at = cur->i;

        if (arg == NULL) {
            (void)cursor_next(cur);
            continue;
        }
        if (st->end_of_options) {
            if (!scan_literal(cur, out, arg, at, st)) {
                return;
            }
            continue;
        }
        if (!scan_option(cur, out, arg, at, st)) {
            return;
        }
    }
}

void cli_parse(int argc, char *const *argv, CliArgs *out)
{
    ArgCursor cur;
    ScanState st = {false, false, false, -1};

    if (out == NULL) {
        return;
    }
    cli_args_clear(out);

    if (argc < 1 || argv == NULL) {
        out->error = CLI_ERR_INTERNAL;
        return;
    }

    /* Upper bound on subcommand args is argc; one allocation, no growth. */
    out->rest_argv = (const char **)malloc(sizeof(*out->rest_argv) * (size_t)argc);
    if (out->rest_argv == NULL) {
        out->error = CLI_ERR_INTERNAL;
        return;
    }

    cursor_init(&cur, argc, argv);
    scan_argv(&cur, out, &st);
    if (out->error != CLI_ERR_OK) {
        return;
    }

    if (st.want_help) {
        apply_help_request(out, st.command_index);
        return;
    }
    if (st.want_version) {
        out->command = CLI_CMD_VERSION;
        out->error = CLI_ERR_OK;
        return;
    }
    if (st.command_index < 0) {
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

void cli_args_free(CliArgs *out)
{
    if (out == NULL) {
        return;
    }
    free((void *)out->rest_argv);
    out->rest_argv = NULL;
    out->rest_argc = 0;
}
