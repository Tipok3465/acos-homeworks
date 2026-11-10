#include "station.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

int run_worker(long owner, int slot);

static int number(const char *value, long min, long max, long *out) {
    char *end;
    errno = 0;
    long n = strtol(value, &end, 10);
    if (errno || end == value || *end || n < min || n > max) return 0;
    *out = n;
    return 1;
}

int main(int argc, char **argv) {
    long owner, slot;
    if (argc != 3 || !number(argv[1], 1, INT_MAX, &owner) ||
        !number(argv[2], 0, INT_MAX, &slot)) {
        fprintf(stderr, "Usage: %s station_pid slot\n", argv[0]);
        return ERROR;
    }
    return run_worker(owner, (int)slot);
}
