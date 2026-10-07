#ifndef STATION_H
#define STATION_H

#include <signal.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_WAGONS 200
#define MAX_TRACKS 20
#define MAX_LOCOMOTIVES 10
#define MAX_ROUTES 40
#define MAX_CONFLICTS 10
#define MAX_TRAINS 40
#define MAX_REQUIREMENTS 40
#define NAME_SIZE 32

typedef enum { OK, ERROR, TIME_LIMIT, INVARIANT_ERROR, REMAINDER, INTERRUPTED } RunResult;
typedef enum { PLANNED, WAITING, MOVING, SORTED, DEPARTED } WagonState;
typedef enum { FIRST_FIT, MOST_FREE } Strategy;

/* Command-line settings that do not belong to the station itself. */
typedef struct {
    const char *scenario_path;
    const char *log_path;
    int delay_ms;
    int until;
    Strategy strategy;
    int strategy_overridden;
} Config;


typedef struct {
    int id;
    char destination[NAME_SIZE];
    int train_id;
    int track_id;
    int locomotive_id;
    int wait_since;
    WagonState state;
} Wagon;

typedef struct {
    int id;
    char purpose[NAME_SIZE];
    int capacity;
    int occupied;
    int peak;
} Track;

typedef struct {
    int id;
    double speed;
    int wagon_id;
    int route_id;
    int target_track_id;
    int start_time;
    int finish_time;
    int started;
    long busy_time;
} Locomotive;

typedef struct {
    int id;
    int from_track_id;
    int to_track_id;
    double length;
    int conflicts[MAX_CONFLICTS];
    int conflict_count;
    int reserved_by;
} Route;

typedef struct { int id, time, track_id, announced; } Arrival;

typedef struct {
    int id;
    char destination[NAME_SIZE];
    int wagon_count;
    int track_id;
    int departed;
} Requirement;


typedef struct {
    Track tracks[MAX_TRACKS]; int track_count;
    Locomotive locomotives[MAX_LOCOMOTIVES]; int locomotive_count;
    Route routes[MAX_ROUTES]; int route_count;
    Wagon wagons[MAX_WAGONS]; int wagon_count;
    Arrival arrivals[MAX_TRAINS]; int arrival_count;
    Requirement requirements[MAX_REQUIREMENTS]; int requirement_count;
    int uncoupling_duration;
    int coupling_duration;
    int maneuver_duration;
    int timing_defined;
    Strategy strategy;
} Scenario;

extern volatile sig_atomic_t stop_requested;

void usage(const char *program);
int read_config(int argc, char **argv, Config *config);
int load_scenario(const char *path, Scenario *scenario, char *error, size_t size);
RunResult simulate(Scenario *scenario, const Config *config, int log_fd);
int validate_state(const Scenario *scenario, char *error, size_t size);

#endif
