#ifndef REMEMBER_APP_H
#define REMEMBER_APP_H

#include <stdio.h>

/*
 * Run one remember invocation in-process — the CLI pipeline without process
 * main() (parse -> dispatch -> command -> output). This is the one symbol the
 * native GUI links so it reuses the exact CLI behavior and byte-identical JSON
 * instead of spawning the process.
 *
 * argv follows the CLI convention: argv[0] is the program name (ignored) and the
 * real arguments start at argv[1] — e.g. {"remember", "--db", PATH, "--json",
 * "add", "body"}. Normal output goes to `out`, diagnostics to `err`; both must
 * be non-NULL. Returns the stable exit code (0 ok / 1 usage|error / 2 not found)
 * and restores the default stdout/stderr streams before returning.
 */
int remember_run(int argc, char *const *argv, FILE *out, FILE *err);

/* CLI entrypoint convenience: remember_run against stdout/stderr. */
int remember_main(int argc, char *const *argv);

#endif /* REMEMBER_APP_H */
