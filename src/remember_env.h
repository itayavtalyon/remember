#ifndef REMEMBER_ENV_H
#define REMEMBER_ENV_H

#include <stdbool.h>
#include <stddef.h>

/* H-0.6. Confirm and dispatch are plan 25 and stay unused. A NULL token
   callback leaves only the id env (ST-3.2). One callback_ctx for all three.
   Called on the thread that made the call. */
typedef bool (*remember_confirm_fn)(void *ctx, const char *kind, const char *name,
                                    const char *body);
typedef void (*remember_dispatch_fn)(void *ctx, const char *brain_path);
typedef int (*remember_token_fn)(void *ctx, const char *helper_id, char *buf, size_t buflen);

typedef struct RememberEnv {
    remember_confirm_fn confirm;
    remember_dispatch_fn dispatch;
    remember_token_fn token;
    void *callback_ctx;
} RememberEnv;

#endif /* REMEMBER_ENV_H */
