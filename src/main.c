#include "cli.h"
#include "exit_codes.h"

#include <stddef.h>
#include <stdio.h>

#ifndef REMEMBER_VERSION
#define REMEMBER_VERSION "0.1.0"
#endif

/* Scaffold only — not part of the stable exit-code contract (0/1/2). */
#define REMEMBER_NYI 3

static void print_version(void)
{
    (void)printf("remember %s\n", REMEMBER_VERSION);
}

static void print_general_help(void)
{
    static const char help[] = "remember - local personal memory (SQLite + FTS5)\n"
                               "\n"
                               "Usage:\n"
                               "  remember [--db PATH] [--json] <command> [options] [args]\n"
                               "\n"
                               "Commands:\n"
                               "  add       Store a memory (optional --key, --tag, --source)\n"
                               "  search    Full-text search\n"
                               "  list      List memories with filters\n"
                               "  get       Fetch one entry by id or --key\n"
                               "  update    Change body and/or tags by id or --key\n"
                               "  delete    Remove an entry by id or --key\n"
                               "  help      Show this help (help <command> for a command)\n"
                               "  version   Show version\n"
                               "\n"
                               "Global options (allowed before or after the command):\n"
                               "  --db PATH   Database file (overrides REMEMBER_DB)\n"
                               "  --json      Machine-readable JSON on stdout\n"
                               "  --help, -h  Show help\n"
                               "  --version   Show version\n";

    (void)fputs(help, stdout);
}

static void print_command_help(CliCommand topic)
{
    const char *name = cli_command_name(topic);
    const char *summary = cli_command_summary(topic);

    if (name == NULL) {
        print_general_help();
        return;
    }
    (void)printf("remember %s - %s\n", name, summary != NULL ? summary : "");
    (void)printf("\n");
    (void)printf("Usage:\n");
    (void)printf("  remember [global options] %s [command options] [args]\n", name);
    (void)printf("\n");
    (void)printf("Global options: --db PATH, --json, --help, --version\n");
    (void)printf("See also: remember --help\n");
}

static void print_parse_error(const CliArgs *args)
{
    const char *base = cli_error_message(args->error);

    switch (args->error) {
    case CLI_ERR_UNKNOWN_COMMAND:
    case CLI_ERR_UNKNOWN_OPTION:
        if (args->error_arg != NULL) {
            (void)fprintf(stderr, "remember: %s '%s'\n", base, args->error_arg);
            return;
        }
        break;
    case CLI_ERR_MISSING_OPTION_VALUE:
        if (args->error_option != NULL) {
            (void)fprintf(stderr, "remember: %s: %s\n", args->error_option, base);
            return;
        }
        break;
    case CLI_ERR_OK:
    case CLI_ERR_MISSING_COMMAND:
    case CLI_ERR_INTERNAL:
    default:
        break;
    }
    (void)fprintf(stderr, "remember: %s\n", base);
}

static int dispatch_nyi(const char *name)
{
    (void)fprintf(stderr, "remember: command '%s' is not implemented yet\n", name);
    return REMEMBER_NYI;
}

int main(int argc, char **argv)
{
    CliArgs args;

    cli_parse(argc, argv, &args);

    if (args.error != CLI_ERR_OK) {
        print_parse_error(&args);
        return REMEMBER_ERR;
    }

    switch (args.command) {
    case CLI_CMD_HELP:
        if (args.help_topic != CLI_CMD_NONE) {
            print_command_help(args.help_topic);
        } else {
            print_general_help();
        }
        return REMEMBER_OK;
    case CLI_CMD_VERSION:
        print_version();
        return REMEMBER_OK;
    case CLI_CMD_NONE:
        /* Should be CLI_ERR_MISSING_COMMAND; defensive. */
        (void)fprintf(stderr, "remember: %s\n", cli_error_message(CLI_ERR_MISSING_COMMAND));
        return REMEMBER_ERR;
    case CLI_CMD_UNKNOWN:
        (void)fprintf(stderr, "remember: %s\n", cli_error_message(CLI_ERR_UNKNOWN_COMMAND));
        return REMEMBER_ERR;
    case CLI_CMD_ADD:
        return dispatch_nyi("add");
    case CLI_CMD_SEARCH:
        return dispatch_nyi("search");
    case CLI_CMD_LIST:
        return dispatch_nyi("list");
    case CLI_CMD_GET:
        return dispatch_nyi("get");
    case CLI_CMD_UPDATE:
        return dispatch_nyi("update");
    case CLI_CMD_DELETE:
        return dispatch_nyi("delete");
    default:
        (void)fprintf(stderr, "remember: internal error\n");
        return REMEMBER_ERR;
    }
}
