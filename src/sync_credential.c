#include "sync_credential.h"

#include "diag.h"
#include "normalize.h"
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

/* Key is set and nothing usable came back. Wipes token before the notice. */
/* intentional: fmt_alloc lives in the CLI's commands_common, which the
   library does not include. One %s, sized by strlen, needs no second pass. */
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
    (void)diag_error_fix(DIAG_SYNC_CREDENTIAL_UNAVAILABLE, message,
                         diag_code_fix(DIAG_SYNC_CREDENTIAL_UNAVAILABLE));
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

/* True when s has no C0 control and no DEL. A token goes into a C string and
   later a header, so a NUL, CR, LF or ESC inside it is not a token. */
static bool token_bytes_ok(const char *s)
{
    for (; *s != '\0'; s++) {
        if (is_ascii_control((unsigned char)*s)) {
            return false;
        }
    }
    return true;
}

/* 1 when dst holds REMEMBER_SYNC_TOKEN, trimmed of ASCII whitespace like the
   helper line. 0 when the variable is missing, empty, or only whitespace; dst
   is then "". -1 when it does not fit cap or holds a control byte; dst is
   wiped. */
static int take_env_token(char *dst, size_t cap)
{
    // NOLINTNEXTLINE(concurrency-mt-unsafe) -- single-threaded; no reentrant getenv
    const char *env = getenv("REMEMBER_SYNC_TOKEN");
    size_t start = 0U;
    size_t end = 0U;

    sync_credential_wipe(dst, cap);
    if (env == NULL) {
        return 0;
    }
    end = strlen(env);
    ascii_ws_trim(env, &start, &end);
    if (start == end) {
        return 0;
    }
    if (end - start >= cap) {
        return -1;
    }
    memcpy(dst, env + start, end - start);
    if (!token_bytes_ok(dst)) {
        sync_credential_wipe(dst, cap);
        return -1;
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
        /* C-42: a refused variable has no sentence yet. */
        return take_env_token(in->token, in->token_cap) < 0 ? 2 : 0;
    }
    quoted = unquote_helper(got.value, id, sizeof(id));
    if (quoted) {
        shown = id;
    } else {
        shown = got.value;
    }
    if (strcmp(shown, "env") == 0) {
        if (take_env_token(in->token, in->token_cap) != 1) {
            return emit_unavailable(shown, in->token, in->token_cap);
        }
        return 0;
    }
    if (quoted && helper_id_ok(id) && in->host != NULL && in->host->token != NULL) {
        int rc = 0;

        memset(in->token, 0, in->token_cap);
        rc = in->host->token(in->host->callback_ctx, id, in->token, in->token_cap);
        if (rc == 0 && memchr(in->token, '\0', in->token_cap) != NULL && in->token[0] != '\0' &&
            token_bytes_ok(in->token)) {
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
