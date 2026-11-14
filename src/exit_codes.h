#ifndef REMEMBER_EXIT_CODES_H
#define REMEMBER_EXIT_CODES_H

/* Stable process exit codes (design contract). 4 is only a warning under --strict (S-09).
   5 is confirmation_required (decision:2026-11-03-confirmation-required-exit-5).
   75 is EX_TEMPFAIL: schema_too_new, a write to a brain made by a newer remember
   (decision:2026-10-31-cli-outdated-exit-code). */
enum {
    REMEMBER_OK = 0,
    REMEMBER_ERR = 1,
    REMEMBER_NOT_FOUND = 2,
    REMEMBER_WRONG_BIN = 3,
    REMEMBER_STRICT = 4,
    REMEMBER_CONFIRM = 5,
    REMEMBER_TEMPFAIL = 75
};

#endif /* REMEMBER_EXIT_CODES_H */
