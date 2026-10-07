#include "station.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void usage(const char *program) {
    printf("Usage: %s [options]\n"
           "  --scenario PATH       station scenario file\n"
           "  --strategy first|most dispatch strategy\n"
           "  --delay-ms N          display delay, 0..1000 ms\n"
           "  --until N             virtual-time limit, 0 means unlimited\n"
           "  --log PATH            detailed log file\n"
           "  --help                show this message\n", program);
}

static int number(const char *text, int *value) {
    char *end = NULL;
    long result = strtol(text, &end, 10);
    if (*text == '\0' || *end != '\0' || result < 0 || result > 1000000) return 0;
    *value = (int)result;
    return 1;
}

int read_config(int argc, char **argv, Config *config) {
    *config = (Config){.scenario_path = "examples/station.scenario",
                       .log_path = "station.log", .delay_ms = 1000,
                       .strategy = FIRST_FIT};
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            exit(OK);
        }
        if (i + 1 >= argc) return 0;
        const char *key = argv[i++];
        const char *value = argv[i];
        if (strcmp(key, "--scenario") == 0) config->scenario_path = value;
        else if (strcmp(key, "--log") == 0) config->log_path = value;
        else if (strcmp(key, "--delay-ms") == 0) {
            if (!number(value, &config->delay_ms) || config->delay_ms > 1000) return 0;
        } else if (strcmp(key, "--until") == 0) {
            if (!number(value, &config->until)) return 0;
        } else if (strcmp(key, "--strategy") == 0) {
            if (strcmp(value, "first") == 0) config->strategy = FIRST_FIT;
            else if (strcmp(value, "most") == 0) config->strategy = MOST_FREE;
            else return 0;
            config->strategy_overridden = 1;
        } else return 0;
    }
    return 1;
}
