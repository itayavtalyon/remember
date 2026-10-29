#include "sync_credential.h"

#include "diag.h"
#include "settings.h"
#include "store.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef REMEMBER_TEST_HOOKS
#include "test_alloc.h"
#endif

static const char k_unavailable_fmt[] = "the sync credential helper \"%s\" did not return a token.";

static const char k_unavailable_fix[] =
    "Check the helper, or set remember.sync.credential_helper to env and export "
    "REMEMBER_SYNC_TOKEN.";

/* Key is set and nothing usable came back. Wipes token before the notice. */
static int emit_unavailable(const char *id, char *token, size_t token_cap)
{
    const char *shown = id != NULL ? id : "";
    size_t cap = strlen(shown) + sizeof(k_unavailable_fmt);
    char *message = (char *)malloc(cap);
    int wrote = 0;

    sync_credential_wipe(token, token_cap);
    if (message == NULL) {
        (void)diag_error(DIAG_OUT_OF_MEMORY, "out of memory");
        return 1;
    }
    wrote = snprintf(message, cap, k_unavailable_fmt, shown);
    if (wrote < 0 || (size_t)wrote >= cap) {
        free(message);
        return 1;
    }
    (void)diag_error_fix(DIAG_SYNC_CREDENTIAL_UNAVAILABLE, message, k_unavailable_fix);
    free(message);
    return 1;
}

enum { HELPER_ID_CAP = 41 };

/* Stored form is a JSON string with no escapes (json_quote). False when it
   is not a quoted string, or the inside does not fit in id. */
static bool unquote_helper(const char *json, char *id, size_t cap)
{
    size_t n = 0U;

    if (json == NULL || id == NULL || cap == 0U) {
        return false;
    }
    n = strlen(json);
    if (n < 2U || json[0] != '"' || json[n - 1U] != '"') {
        return false;
    }
    if (n - 1U > cap) {
        return false;
    }
    memcpy(id, json + 1, n - 2U);
    id[n - 2U] = '\0';
    return true;
}

/* 1 when dst holds REMEMBER_SYNC_TOKEN. 0 when the variable is missing or
   empty; dst is then "". */
static int take_env_token(char *dst, size_t cap)
{
    // NOLINTNEXTLINE(concurrency-mt-unsafe) -- single-threaded; no reentrant getenv
    const char *env = getenv("REMEMBER_SYNC_TOKEN");
    size_t n = 0U;

    if (env == NULL || env[0] == '\0') {
        if (cap > 0U) {
            dst[0] = '\0';
        }
        return 0;
    }
    n = strlen(env);
    if (n >= cap) {
        sync_credential_wipe(dst, cap);
        return 0;
    }
    memcpy(dst, env, n + 1U);
    if (cap > n + 1U) {
        sync_credential_wipe(dst + n + 1U, cap - n - 1U);
    }
    return 1;
}

/* cppcheck-suppress staticFunction ; public API (sync_credential.h); used by tests */
int sync_credential_lookup(SyncCredentialLookup *in)
{
    SettingRead got;
    char id[HELPER_ID_CAP];
    const char *shown = NULL;
    bool quoted = false;
    SettingStatus st = SETTING_OK;

    in->err[0] = '\0';
    st = store_setting_read(in->store, in->user_db, "remember.sync.credential_helper", NULL, &got,
                            in->err, in->err_cap);
    if (st != SETTING_OK) {
        sync_credential_wipe(in->token, in->token_cap);
        return -1;
    }
    if (got.from == SETTING_FROM_DEFAULT && got.value[0] == '\0') {
        (void)take_env_token(in->token, in->token_cap);
        return 0;
    }
    quoted = unquote_helper(got.value, id, sizeof(id));
    if (quoted) {
        shown = id;
    } else {
        shown = got.value;
    }
    if (strcmp(shown, "env") == 0) {
        if (take_env_token(in->token, in->token_cap) == 0) {
            return emit_unavailable(shown, in->token, in->token_cap);
        }
        return 0;
    }
    if (quoted && helper_id_ok(id) && in->host != NULL && in->host->token != NULL) {
        int rc = 0;

        memset(in->token, 0, in->token_cap);
        rc = in->host->token(in->host->callback_ctx, id, in->token, in->token_cap);
        if (rc == 0 && memchr(in->token, '\0', in->token_cap) != NULL && in->token[0] != '\0') {
            return 0;
        }
    }
    return emit_unavailable(shown, in->token, in->token_cap);
}

/* cppcheck-suppress staticFunction ; public API (sync_credential.h); used by tests */
void sync_credential_wipe(void *buf, size_t n)
{
    volatile unsigned char *p = (volatile unsigned char *)buf;
    size_t i = 0U;

    for (i = 0U; i < n; i++) {
        p[i] = 0U;
    }
}
