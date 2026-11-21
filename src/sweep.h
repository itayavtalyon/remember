#ifndef REMEMBER_SWEEP_H
#define REMEMBER_SWEEP_H

#include "store.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * The expiry sweep (plan 32; log 010 H-1.5; specs/EVENTS.md "Expiry"). The store marks the rows
 * (store_sweep) and writes their memory.expired events; this module is the command around it.
 */

/* `brain sweep [--all]` (the arguments after "sweep"): 0 with *all set, or -1 after the usage error
 * is printed. */
int sweep_parse(int rest_argc, const char **rest_argv, bool *all);

/* Sweep this brain now, if this process can write it (a newer brain is skipped, and *marked is
   0). The status of the store, with nothing printed. */
StoreStatus sweep_now(Store *s, size_t *marked);

/* `remember brain sweep`: one brain. Prints the count (plain) or the brain.sweep.result data
   (JSON). */
int cmd_sweep(Store *s, bool json, int rest_argc, const char **rest_argv);

/* Sweeps the brain at path: 0 with *marked, or -1 when it could not be swept, with the warning
   printed. A brain from a newer remember is 0 with nothing marked. */
typedef int (*SweepBrainFn)(const void *ctx, const char *path, size_t *marked);

/* `remember brain sweep --all`: each brain of brains.json, in file order. A path with no brain gets
   the brain_missing warning and its entry stays. 0, or the exit code. */
int cmd_sweep_all(bool json, SweepBrainFn sweep_one, const void *ctx);

#endif /* REMEMBER_SWEEP_H */
