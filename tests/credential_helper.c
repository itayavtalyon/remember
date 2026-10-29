#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Stand-in for a user helper. Prints the argv the CLI passed, then one token line.
   A shell script cannot see that argv: the kernel's shebang path replaces $0. */
int main(int argc, char **argv)
{
    char scratch[1];
    const char *argv0 = (argc > 0 && argv[0] != NULL) ? argv[0] : "";
    const char *argv1 = (argc > 1 && argv[1] != NULL) ? argv[1] : "";
    ssize_t n = read(STDIN_FILENO, scratch, sizeof(scratch));
    long got = (n < 0) ? -1L : (long)n;

    if (fprintf(stderr, "enter passphrase\nargv0=%s argv1=%s stdin=%ld\n", argv0, argv1, got) < 0) {
        return 2;
    }
    if (strchr(argv0, '/') != NULL || strcmp(argv1, "get") != 0) {
        return 2;
    }
    if (n > 0) {
        return 3;
    }
    if (printf("  secret  \nignored\n") < 0) {
        return 2;
    }
    return 0;
}
