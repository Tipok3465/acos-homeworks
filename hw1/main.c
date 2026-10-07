#define _POSIX_C_SOURCE 200809L
#include "station.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

volatile sig_atomic_t stop_requested = 0;

static void stop_handler(int signal_number) {
    (void)signal_number;
    stop_requested = 1;
}

int main(int argc, char **argv) {
    Config config;
    if (!read_config(argc, argv, &config)) {
        fprintf(stderr, "Invalid arguments. Run with --help.\n");
        return ERROR;
    }
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = stop_handler;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) != 0 || sigaction(SIGTERM, &action, NULL) != 0) {
        perror("sigaction");
        return ERROR;
    }
    Scenario scenario;
    char message[256];
    if (!load_scenario(config.scenario_path, &scenario, message, sizeof(message))) {
        fprintf(stderr, "Scenario error: %s\n", message);
        return ERROR;
    }
    if (config.strategy_overridden) scenario.strategy = config.strategy;
    int log_fd = open(config.log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (log_fd < 0) {
        perror("log");
        return ERROR;
    }
    RunResult result = simulate(&scenario, &config, log_fd);
    if (close(log_fd) != 0) return ERROR;
    printf("\nDetailed log saved to: %s\n", config.log_path);
    return result;
}
