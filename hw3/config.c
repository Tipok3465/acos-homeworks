#include "station.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void usage(const char *program) {
    printf("Usage: %s [scenario [tick_ms]] [options]\n"
           "  --scenario PATH       input file\n"
           "  --tick-ms N           real milliseconds per model unit (1..1000)\n"
           "  --until N             stop after N model units (0: unlimited)\n"
           "  --strategy first|most train groups: track order or larger capacity\n"
           "  --log PATH            full event log\n"
           "  --quiet               disable live dashboard\n"
           "  --help                show usage\n", program);
}

static int parse_number(const char *text, int minimum, int maximum, int *value) {
    char *end;
    errno = 0;
    long number = strtol(text, &end, 10);
    if (errno || !*text || *end || number < minimum || number > maximum) return 0;
    *value = (int)number;
    return 1;
}

int read_config(int argc, char **argv, Config *config) {
    *config = (Config){.scenario_path = "examples/station.scenario",
                       .log_path = "station.log", .tick_ms = 100};
    int positional = 0;
    for (int i = 1; i < argc; ++i) {
        const char *key = argv[i];
        if (strcmp(key, "--quiet") == 0) { config->quiet = 1; continue; }
        if (key[0] != '-') {
            if (positional == 0) config->scenario_path = key;
            else if (positional == 1) {
                if (!parse_number(key, 1, 1000, &config->tick_ms)) return 0;
            } else return 0;
            ++positional;
            continue;
        }
        if (i + 1 >= argc) return 0;
        const char *value = argv[++i];
        if (strcmp(key, "--scenario") == 0) config->scenario_path = value;
        else if (strcmp(key, "--log") == 0) config->log_path = value;
        else if (strcmp(key, "--tick-ms") == 0) {
            if (!parse_number(value, 1, 1000, &config->tick_ms)) return 0;
        } else if (strcmp(key, "--until") == 0) {
            if (!parse_number(value, 0, 1000000, &config->until)) return 0;
        } else if (strcmp(key, "--strategy") == 0) {
            if (strcmp(value, "first") == 0) config->strategy = FIRST_FIT;
            else if (strcmp(value, "most") == 0) config->strategy = MOST_FREE;
            else return 0;
            config->strategy_overridden = 1;
        } else return 0;
    }
    return 1;
}
