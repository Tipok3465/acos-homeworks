#include "station.h"

#include <stdio.h>
#include <string.h>

volatile sig_atomic_t stop_requested = 0;
static int failed = 0;

static void check(int condition, const char *name) {
    printf("%s: %s\n", condition ? "PASSED" : "FAILED", name);
    if (!condition) ++failed;
}

static void test_config(void) {
    Config config;
    char *valid[] = {"station", "--delay-ms", "10", "--strategy", "most"};
    char *invalid[] = {"station", "--delay-ms", "2000"};
    check(read_config(5, valid, &config) && config.delay_ms == 10 &&
          config.strategy == MOST_FREE, "valid command line");
    check(!read_config(3, invalid, &config), "invalid display delay");
}

static void test_scenarios(void) {
    Scenario scenario;
    char error[256];
    check(load_scenario("examples/station.scenario", &scenario, error, sizeof(error)) &&
          scenario.wagon_count == 2 && scenario.requirement_count == 2 &&
          scenario.uncoupling_duration == 2 && scenario.coupling_duration == 2 &&
          scenario.maneuver_duration == 4,
          "valid scenario");
    check(!load_scenario("examples/invalid.scenario", &scenario, error, sizeof(error)) &&
          strstr(error, "unknown train") != NULL, "invalid reference rejected");
    check(!load_scenario("examples/missing_timing.scenario", &scenario, error, sizeof(error)) &&
          strstr(error, "timing") != NULL, "missing timing rejected");
    check(!load_scenario("examples/negative_time.scenario", &scenario, error, sizeof(error)) &&
          strstr(error, "line") != NULL, "negative arrival time rejected");
    check(!load_scenario("examples/duplicate.scenario", &scenario, error, sizeof(error)) &&
          strstr(error, "duplicate") != NULL, "duplicate entity identifier rejected");
}

static void test_invariants(void) {
    Scenario scenario;
    char error[256];
    load_scenario("examples/station.scenario", &scenario, error, sizeof(error));
    scenario.tracks[1].occupied = scenario.tracks[1].capacity + 1;
    check(!validate_state(&scenario, error, sizeof(error)), "track capacity invariant");
    load_scenario("examples/station.scenario", &scenario, error, sizeof(error));
    scenario.routes[0].reserved_by = 1;
    scenario.routes[1].reserved_by = 2;
    check(!validate_state(&scenario, error, sizeof(error)), "route conflict invariant");
}

static void test_simulation(void) {
    Config config;
    Scenario scenario;
    char error[256];
    read_config(1, (char *[]){"station"}, &config);
    load_scenario("examples/station.scenario", &scenario, error, sizeof(error));
    check(simulate(&scenario, &config, -1) == OK, "complete simulation");
    load_scenario("examples/remainder.scenario", &scenario, error, sizeof(error));
    check(simulate(&scenario, &config, -1) == REMAINDER, "unfulfillable remainder");
    load_scenario("examples/capacity.scenario", &scenario, error, sizeof(error));
    check(simulate(&scenario, &config, -1) == REMAINDER,
          "incoming wagon reserves track capacity");
    load_scenario("examples/station.scenario", &scenario, error, sizeof(error));
    config.until = 2;
    check(simulate(&scenario, &config, -1) == TIME_LIMIT, "time limit");
    config.until = 0;
    stop_requested = 1;
    check(simulate(&scenario, &config, -1) == INTERRUPTED, "signal interruption");
    stop_requested = 0;
}

int main(void) {
    test_config();
    test_scenarios();
    test_invariants();
    test_simulation();
    if (failed == 0) {
        puts("All tests passed.");
        return 0;
    }
    printf("Failed tests: %d\n", failed);
    return 1;
}
