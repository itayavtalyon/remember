#ifndef REMEMBER_CLI_H
#define REMEMBER_CLI_H

#include <stdbool.h>

typedef enum {
    CLI_CMD_NONE = 0,
    CLI_CMD_HELP,
    CLI_CMD_VERSION,
    CLI_CMD_ADD,
    CLI_CMD_SEARCH,
    CLI_CMD_LIST,
    CLI_CMD_GET,
    CLI_CMD_UPDATE,
    CLI_CMD_DELETE,
    CLI_CMD_UNKNOWN
} CliCommand;

/* Parse / usage failures. CLI_ERR_OK means a runnable or meta request. */
typedef enum {
    CLI_ERR_OK = 0,
    CLI_ERR_MISSING_COMMAND,
    CLI_ERR_UNKNOWN_COMMAND,
    CLI_ERR_UNKNOWN_OPTION,
    CLI_ERR_MISSING_OPTION_VALUE,
    CLI_ERR_INTERNAL
} CliError;

typedef struct {
    const char *db_path; /* NULL if omitted; aliases argv */
    bool json;
} CliGlobals;

/*
 * Result of parsing argv. Pointers alias argv (no ownership).
 * On error != CLI_ERR_OK, only error / error_arg / error_option are meaningful
 * for messaging; command may still be set for context (e.g. help target).
 */
typedef struct {
    CliError error;
    const char *error_arg;    /* unknown command/option token, if any */
    const char *error_option; /* option name missing a value, e.g. "--db" */

    CliCommand command;
    CliCommand help_topic; /* when command==HELP: NONE = general, else topic */
    CliGlobals globals;

    /* Tokens after the subcommand name, excluding consumed globals. */
    int rest_argc;
    char *const *rest_argv;
} CliArgs;

/* Fills *out. Never returns a status code — inspect out->error. */
void cli_parse(int argc, char *const *argv, CliArgs *out);

/* User-facing base message for err (no trailing newline). Never NULL. */
const char *cli_error_message(CliError err);

/* Subcommand name for cmd, or NULL if not a real subcommand. */
const char *cli_command_name(CliCommand cmd);

/* One-line description for help, or NULL. */
const char *cli_command_summary(CliCommand cmd);

#endif /* REMEMBER_CLI_H */
