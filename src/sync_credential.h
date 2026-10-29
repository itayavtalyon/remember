#ifndef REMEMBER_SYNC_CREDENTIAL_H
#define REMEMBER_SYNC_CREDENTIAL_H

#include "remember_env.h"
#include "store.h"

#include <stddef.h>

/* One lookup. token receives the secret, or "" when nothing is configured.
   err receives an existing store sentence on store failure. */
typedef struct SyncCredentialLookup {
    Store *store;
    const char *user_db;
    const RememberEnv *host;
    char *token;
    size_t token_cap;
    char *err;
    size_t err_cap;
} SyncCredentialLookup;

/* 0: token holds a secret, or "" when no row and REMEMBER_SYNC_TOKEN is
   missing or empty. 1: the key is set and no token came back; the diag
   emitter wrote sync_credential_unavailable and token is wiped. -1: the
   store failed; err holds that existing sentence; token is wiped. */
int sync_credential_lookup(SyncCredentialLookup *in);

/* Overwrite n bytes so a secret does not stay in the buffer. */
void sync_credential_wipe(void *buf, size_t n);

#endif /* REMEMBER_SYNC_CREDENTIAL_H */
