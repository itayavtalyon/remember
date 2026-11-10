#ifndef REMEMBER_CMD_SETTINGS_H
#define REMEMBER_CMD_SETTINGS_H

#include <stddef.h>

/* CLI host token callback (H-0.6). 0 ok, -1 unknown id, -2 lookup failed.
   buf holds a NUL-terminated token on success. */
int cli_credential_token(void *ctx, const char *helper_id, char *buf, size_t buflen);

/* ST-3.3 deadline. 30000 unless a test hook shortens it. */
int cli_credential_timeout_ms(void);

#ifdef REMEMBER_TEST_HOOKS
void cli_credential_test_set_timeout_ms(int ms);
#endif

#endif /* REMEMBER_CMD_SETTINGS_H */
