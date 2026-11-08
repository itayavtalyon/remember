#ifndef REMEMBER_EXIT_CODES_H
#define REMEMBER_EXIT_CODES_H

/* Stable process exit codes (design contract). 4 is only a warning under --strict (S-09). */
enum {
    REMEMBER_OK = 0,
    REMEMBER_ERR = 1,
    REMEMBER_NOT_FOUND = 2,
    REMEMBER_WRONG_BIN = 3,
    REMEMBER_STRICT = 4
};

#endif /* REMEMBER_EXIT_CODES_H */
