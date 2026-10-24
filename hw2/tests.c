#include "station.h"
#include "notify.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int checks, failures;
static void check(int condition, const char *name) {
    ++checks;
    printf("%s %.*s\n", condition ? "PASS" : "FAIL", (int)strcspn(name, "\n"), name);
    failures += !condition;
}

static void config_tests(void) {
    Config config;
    char *valid[] = {"station", "--tick-ms", "10", "--until", "3", "--quiet", "--strategy", "most"};
    check(read_config(8, valid, &config) && config.tick_ms == 10 && config.until == 3 &&
          config.quiet && config.strategy == MOST_FREE, "named CLI options");
    char *bad[] = {"0", "-1", "1001", "nan", "5extra", "99999999999999999999999999"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        char *args[] = {"station", "--tick-ms", bad[i]};
        check(!read_config(3, args, &config), "invalid CLI number rejected");
    }
    char *missing[] = {"station", "--log"};
    check(!read_config(2, missing, &config), "missing CLI value rejected");
}

static void notification_tests(void) {
    Notification n;
    int ready = notification_init(&n) == 0;
    check(ready, "notification initialized");
    if (!ready) return;
    check(notification_post(&n) == 0 && notification_post(&n) == 0,
          "notifications posted before wait");
    check(notification_wait(&n) == 0 && notification_wait(&n) == 0,
          "early notifications preserved");
    check(notification_destroy(&n) == 0, "notification destroyed");
}

static int load_text(const char *text, Scenario *s, char *error, size_t size) {
    char path[] = "/tmp/hw2-scenario-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return 0;
    size_t length = strlen(text);
    int written = write(fd, text, length) == (ssize_t)length;
    int closed = close(fd) == 0;
    int result = written && closed && load_scenario(path, s, error, size);
    unlink(path);
    return result;
}

static void parser_tests(void) {
    Scenario s;
    char error[256], text[4096];
    const char *base = "track=1,arrival,4\ntrack=2,north,2\n"
        "locomotive=1,5\nroute=1,1,2,10,-\nwagon=1,1,north\n"
        "arrival=1,0,1\nrequirement=101,north,1,2\ntiming=1,1,2\n";
    check(load_text(base, &s, error, sizeof(error)), "complete scenario accepted");
    snprintf(text, sizeof(text), "%s", base);
    *strstr(text, "timing=") = '\0';
    check(!load_text(text, &s, error, sizeof(error)), "missing timing rejected");
    const char *bad[] = {
        "locomotive=2,inf\n", "locomotive=2,nan\n", "locomotive=2,0\n",
        "locomotive=99999999999999999999999999,5\n", "arrival=2,-1,1\n",
        "route=2,1,2,10,abc\n", "route=2,1,2,10,1||1\n",
        "route=2,1,2,10,1|\n", "track=3,,4\n", "wagon=2,1,north,extra\n",
        "timing=0,1,1\n", "route=2,99,2,10,-\n", "arrival=2,0,99\n",
        "wagon=2,99,north\n", "locomotive=1,5\n", "mystery=1\n",
        "requirement=102,south,1,2\n", "locomotive=2,0.000001\n"
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        snprintf(text, sizeof(text), "%s%s", base, bad[i]);
        check(!load_text(text, &s, error, sizeof(error)), bad[i]);
    }
    snprintf(text, sizeof(text), "%s#%0256d\n", base, 0);
    check(!load_text(text, &s, error, sizeof(error)), "overlong line rejected");
    check(load_text("track = 1, arrival, 4\ntrack=2,north,2\nlocomotive=1,5\n"
                    "route=1,1,2,10,-\nwagon=1,1,north\narrival=1,0,1\n"
                    "requirement=101,north,1,2\ntiming=1,1,2", &s, error, sizeof(error)),
          "whitespace and missing final newline accepted");
}

static void invariant_tests(void) {
    Scenario s;
    char error[256];
    if (!load_scenario("examples/station.scenario", &s, error, sizeof(error))) {
        check(0, "invariant test fixture"); return;
    }
    check(validate_state(&s, error, sizeof(error)), "initial state valid");
    s.wagons[0].locomotive_id = 1;
    check(!validate_state(&s, error, sizeof(error)), "waiting wagon cannot own locomotive");
    s.wagons[0].locomotive_id = -1;
    s.wagons[0].state = MOVING;
    s.wagons[0].locomotive_id = 999;
    check(!validate_state(&s, error, sizeof(error)), "unknown locomotive rejected");
    s.wagons[0].state = PLANNED;
    s.wagons[0].locomotive_id = -1;
    s.routes[0].reserved_by = 1;
    check(!validate_state(&s, error, sizeof(error)), "orphan reservation rejected");
    s.routes[0].reserved_by = -1;
    s.tracks[1].occupied = 1;
    check(!validate_state(&s, error, sizeof(error)), "incorrect occupancy rejected");
    s.tracks[1].occupied = 0;
    s.requirements[0].departed = 1;
    check(!validate_state(&s, error, sizeof(error)), "empty departed train rejected");
    s.wagons[0].state = DEPARTED;
    s.wagons[0].departure_id = s.requirements[0].id;
    check(validate_state(&s, error, sizeof(error)), "exact departed train accepted");
    snprintf(s.wagons[0].destination, NAME_SIZE, "wrong");
    check(!validate_state(&s, error, sizeof(error)), "wrong departed destination rejected");
}

static void planning_tests(void) {
    Scenario s;
    char error[256];
    if (!load_scenario("test-data/allocation.scenario", &s, error, sizeof(error))) {
        check(0, "planning fixture"); return;
    }
    plan_trains(&s);
    check(s.wagons[0].planned_for == 101 && s.wagons[1].planned_for == 101,
          "complete group avoids split allocation");
    if (!load_scenario("test-data/flexible.scenario", &s, error, sizeof(error))) {
        check(0, "flexible fixture"); return;
    }
    plan_trains(&s);
    check(s.wagons[0].planned_for == 102 && s.wagons[1].planned_for == 101,
          "augmenting path preserves constrained wagon");
    if (!load_scenario("test-data/capacity.scenario", &s, error, sizeof(error))) {
        check(0, "capacity fixture"); return;
    }
    plan_trains(&s);
    check(s.wagons[0].planned_for == -1 && s.wagons[1].planned_for == -1,
          "oversized group is not partially reserved");

    int correct = 1;
    for (unsigned graph = 0; graph < 256; ++graph) {
        Scenario small = {.track_count = 6, .arrival_count = 4, .wagon_count = 4,
                          .requirement_count = 2, .strategy = FIRST_FIT};
        for (int t = 0; t < 6; ++t) small.tracks[t] = (Track){.id = t + 1, .capacity = 2};
        for (int r = 0; r < 2; ++r) {
            small.requirements[r] = (Requirement){.id = 101 + r, .track_id = 5 + r, .wagon_count = 2};
            snprintf(small.requirements[r].destination, NAME_SIZE, "north");
        }
        for (int w = 0; w < 4; ++w) {
            small.arrivals[w] = (Arrival){.id = w + 1, .track_id = w + 1};
            small.wagons[w] = (Wagon){.id = w + 1, .train_id = w + 1};
            snprintf(small.wagons[w].destination, NAME_SIZE, "north");
            for (int r = 0; r < 2; ++r)
                if (graph & (1u << (2 * w + r))) {
                    int route = small.route_count++;
                    small.routes[route] = (Route){.id = route, .from_track_id = w + 1, .to_track_id = 5 + r};
                }
        }
        int feasible = 0;
        for (unsigned choice = 0; choice < 16; ++choice) {
            int first = 0, possible = 1;
            for (int w = 0; w < 4; ++w) {
                int r = (choice & (1u << w)) ? 0 : 1;
                first += r == 0;
                possible &= (graph & (1u << (2 * w + r))) != 0;
            }
            if (first == 2 && possible) feasible = 1;
        }
        plan_trains(&small);
        int assigned = 0;
        for (int w = 0; w < 4; ++w) assigned += small.wagons[w].planned_for != -1;
        if ((assigned == 4) != feasible) correct = 0;
    }
    check(correct, "planner agrees with exhaustive search on 256 reachability graphs");
}

int main(void) {
    config_tests();
    notification_tests();
    parser_tests();
    invariant_tests();
    planning_tests();
    printf("Unit checks: %d, failed: %d\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
