#ifndef REMEMBER_COMMANDS_H
#define REMEMBER_COMMANDS_H

#include "store.h"

#include <stdbool.h>

/* Subcommand runners. rest_argv is command-local (globals already stripped).
 * Return process exit codes: REMEMBER_OK / REMEMBER_ERR / REMEMBER_NOT_FOUND.
 */
int cmd_add(Store *s, bool json, int rest_argc, const char **rest_argv);
int cmd_get(Store *s, bool json, int rest_argc, const char **rest_argv);
int cmd_list(Store *s, bool json, int rest_argc, const char **rest_argv);
int cmd_search(Store *s, bool json, int rest_argc, const char **rest_argv);
int cmd_delete(Store *s, bool json, int rest_argc, const char **rest_argv);
int cmd_update(Store *s, bool json, int rest_argc, const char **rest_argv);
int cmd_tags(Store *s, bool json, int rest_argc, const char **rest_argv);

#endif /* REMEMBER_COMMANDS_H */
