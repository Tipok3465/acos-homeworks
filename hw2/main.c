#include "station.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) { usage(argv[0]); return OK; }
    }
    Config config;
    if (!read_config(argc, argv, &config)) {
        fprintf(stderr, "Invalid arguments. Run with --help.\n");
        return ERROR;
    }
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    int error_code = pthread_sigmask(SIG_BLOCK, &signals, NULL);
    if (error_code) { fprintf(stderr, "signal mask: %s\n", strerror(error_code)); return ERROR; }
    Scenario scenario;
    char error[256];
    if (!load_scenario(config.scenario_path, &scenario, error, sizeof(error))) {
        fprintf(stderr, "Scenario error: %s\n", error);
        return ERROR;
    }
    if (config.strategy_overridden) scenario.strategy = config.strategy;
    int fd = open(config.log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("log"); return ERROR; }
    int result = run_station(&scenario, fd, &config);
    if (close(fd) != 0) { perror("close"); return ERROR; }
    return result;
}
