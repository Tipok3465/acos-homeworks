#define _POSIX_C_SOURCE 200809L
#include "station.h"

#include <ctype.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static Track *track_by_id(Scenario *s, int id) {
    for (int i = 0; i < s->track_count; ++i) if (s->tracks[i].id == id) return &s->tracks[i];
    return NULL;
}
static Route *route_by_id(Scenario *s, int id) {
    for (int i = 0; i < s->route_count; ++i) if (s->routes[i].id == id) return &s->routes[i];
    return NULL;
}
static Arrival *arrival_by_id(Scenario *s, int id) {
    for (int i = 0; i < s->arrival_count; ++i) if (s->arrivals[i].id == id) return &s->arrivals[i];
    return NULL;
}

static char *trim(char *text) {
    while (isspace((unsigned char)*text)) ++text;
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return text;
}

static int unique_ids(const Scenario *s) {
    for (int i = 0; i < s->track_count; ++i)
        for (int j = i + 1; j < s->track_count; ++j)
            if (s->tracks[i].id == s->tracks[j].id) return 0;
    for (int i = 0; i < s->route_count; ++i)
        for (int j = i + 1; j < s->route_count; ++j)
            if (s->routes[i].id == s->routes[j].id) return 0;
    for (int i = 0; i < s->wagon_count; ++i)
        for (int j = i + 1; j < s->wagon_count; ++j)
            if (s->wagons[i].id == s->wagons[j].id) return 0;
    for (int i = 0; i < s->locomotive_count; ++i)
        for (int j = i + 1; j < s->locomotive_count; ++j)
            if (s->locomotives[i].id == s->locomotives[j].id) return 0;
    for (int i = 0; i < s->arrival_count; ++i)
        for (int j = i + 1; j < s->arrival_count; ++j)
            if (s->arrivals[i].id == s->arrivals[j].id) return 0;
    for (int i = 0; i < s->requirement_count; ++i)
        for (int j = i + 1; j < s->requirement_count; ++j)
            if (s->requirements[i].id == s->requirements[j].id) return 0;
    return 1;
}

static int validate_scenario(Scenario *s, char *error, size_t size) {
    if (!s->track_count || !s->locomotive_count || !s->route_count ||
        !s->wagon_count || !s->arrival_count || !s->requirement_count) {
        snprintf(error, size, "all entity types must be present"); return 0;
    }
    if (!s->timing_defined) {
        snprintf(error, size, "timing must be specified"); return 0;
    }
    if (!unique_ids(s)) { snprintf(error, size, "duplicate identifier"); return 0; }
    for (int i = 0; i < s->route_count; ++i) {
        Route *route = &s->routes[i];
        if (!track_by_id(s, route->from_track_id) || !track_by_id(s, route->to_track_id)) {
            snprintf(error, size, "route %d references unknown track", route->id); return 0;
        }
        for (int j = 0; j < route->conflict_count; ++j) {
            Route *other = route_by_id(s, route->conflicts[j]);
            int symmetric = 0;
            if (!other || other == route) { snprintf(error, size, "invalid route conflict"); return 0; }
            for (int k = 0; k < other->conflict_count; ++k)
                if (other->conflicts[k] == route->id) symmetric = 1;
            if (!symmetric) { snprintf(error, size, "route conflict must be symmetric"); return 0; }
        }
    }
    for (int i = 0; i < s->arrival_count; ++i) {
        if (!track_by_id(s, s->arrivals[i].track_id)) {
            snprintf(error, size, "arrival %d references unknown track",
                     s->arrivals[i].id);
            return 0;
        }
    }
    for (int i = 0; i < s->wagon_count; ++i) {
        Wagon *wagon = &s->wagons[i];
        Arrival *arrival = arrival_by_id(s, wagon->train_id);
        int reachable = 0;
        if (!arrival) { snprintf(error, size, "wagon %d has unknown train", wagon->id); return 0; }
        for (int j = 0; j < s->route_count; ++j) {
            Track *target = track_by_id(s, s->routes[j].to_track_id);
            if (s->routes[j].from_track_id == arrival->track_id && target &&
                strcmp(target->purpose, wagon->destination) == 0) reachable = 1;
        }
        if (!reachable) { snprintf(error, size, "wagon %d destination is unreachable", wagon->id); return 0; }
    }
    for (int i = 0; i < s->requirement_count; ++i) {
        Track *track = track_by_id(s, s->requirements[i].track_id);
        if (!track || strcmp(track->purpose, s->requirements[i].destination) != 0) {
            snprintf(error, size, "requirement %d has incompatible track", s->requirements[i].id); return 0;
        }
    }
    return 1;
}

int load_scenario(const char *path, Scenario *s, char *error, size_t size) {
    int input_fd = open(path, O_RDONLY);
    if (input_fd < 0) { snprintf(error, size, "cannot open %s", path); return 0; }
    FILE *file = fdopen(input_fd, "r");
    if (!file) {
        close(input_fd);
        snprintf(error, size, "cannot read %s", path);
        return 0;
    }
    memset(s, 0, sizeof(*s));
    s->strategy = FIRST_FIT;
    char line[256];
    int line_number = 0;
    while (fgets(line, sizeof(line), file)) {
        ++line_number;
        char *p = trim(line);
        if (!*p || *p == '#') continue;
        int a, b, c, count;
        double value;
        char name[NAME_SIZE], extra[128];
        if (sscanf(p, "track=%d,%31[^,],%d%n", &a, name, &b, &count) == 3 && p[count] == '\0' &&
            s->track_count < MAX_TRACKS && a >= 0 && b > 0) {
            s->tracks[s->track_count] = (Track){.id = a, .capacity = b};
            snprintf(s->tracks[s->track_count++].purpose, NAME_SIZE, "%s", name);
        } else if (sscanf(p, "locomotive=%d,%lf%n", &a, &value, &count) == 2 && p[count] == '\0' &&
                   s->locomotive_count < MAX_LOCOMOTIVES && a >= 0 && value > 0.0) {
            s->locomotives[s->locomotive_count++] = (Locomotive){.id = a, .speed = value, .wagon_id = -1};
        } else if (sscanf(p, "route=%d,%d,%d,%lf,%127s%n", &a, &b, &c, &value, extra, &count) == 5 &&
                   p[count] == '\0' && s->route_count < MAX_ROUTES &&
                   a >= 0 && b >= 0 && c >= 0 && value > 0.0) {
            Route *route = &s->routes[s->route_count++];
            *route = (Route){.id = a, .from_track_id = b, .to_track_id = c,
                             .length = value, .reserved_by = -1};
            if (strcmp(extra, "-") != 0) {
                char *save = NULL;
                for (char *token = strtok_r(extra, "|", &save); token;
                     token = strtok_r(NULL, "|", &save)) {
                    if (route->conflict_count == MAX_CONFLICTS) goto bad_line;
                    route->conflicts[route->conflict_count++] = atoi(token);
                }
            }
        } else if (sscanf(p, "wagon=%d,%d,%31s%n", &a, &b, name, &count) == 3 && p[count] == '\0' &&
                   s->wagon_count < MAX_WAGONS && a >= 0 && b >= 0) {
            Wagon *wagon = &s->wagons[s->wagon_count++];
            *wagon = (Wagon){.id = a, .train_id = b, .track_id = -1,
                             .locomotive_id = -1, .wait_since = -1, .state = PLANNED};
            snprintf(wagon->destination, NAME_SIZE, "%s", name);
        } else if (sscanf(p, "arrival=%d,%d,%d%n", &a, &b, &c, &count) == 3 && p[count] == '\0' &&
                   s->arrival_count < MAX_TRAINS && a >= 0 && b >= 0 && c >= 0) {
            s->arrivals[s->arrival_count++] = (Arrival){a, b, c, 0};
        } else if (sscanf(p, "requirement=%d,%31[^,],%d,%d%n", &a, name, &b, &c, &count) == 4 &&
                   p[count] == '\0' && s->requirement_count < MAX_REQUIREMENTS &&
                   a >= 0 && b > 0 && c >= 0) {
            Requirement *r = &s->requirements[s->requirement_count++];
            *r = (Requirement){.id = a, .wagon_count = b, .track_id = c};
            snprintf(r->destination, NAME_SIZE, "%s", name);
        } else if (sscanf(p, "timing=%d,%d,%d%n", &a, &b, &c, &count) == 3 &&
                   p[count] == '\0' && a > 0 && b > 0 && c > 0 && !s->timing_defined) {
            s->uncoupling_duration = a;
            s->coupling_duration = b;
            s->maneuver_duration = c;
            s->timing_defined = 1;
        } else if (strcmp(p, "strategy=first_fit") == 0) s->strategy = FIRST_FIT;
        else if (strcmp(p, "strategy=most_free_capacity") == 0) s->strategy = MOST_FREE;
        else {
bad_line:
            snprintf(error, size, "line %d is invalid", line_number);
            fclose(file); return 0;
        }
    }
    if (ferror(file) || fclose(file) != 0) { snprintf(error, size, "read error"); return 0; }
    return validate_scenario(s, error, size);
}

static int main_event(const char *name) {
    return strcmp(name, "ARRIVE") == 0 || strcmp(name, "UNCOUPLE") == 0 ||
           strcmp(name, "ASSIGN") == 0 || strcmp(name, "RESERVE") == 0 ||
           strcmp(name, "START") == 0 || strcmp(name, "FINISH") == 0 ||
           strcmp(name, "PLACE") == 0 || strcmp(name, "FORM") == 0 ||
           strcmp(name, "DEPART") == 0 ||
           strcmp(name, "INVARIANT") == 0;
}

static char screen_events[16][96];
static int screen_event_count = 0;
static Scenario *display_scenario = NULL;
static int display_delay_ms = 0;
static int render_screen(const Scenario *s);
static void pause_display(int milliseconds);

static void event(int fd, int time, const char *name, const char *format, ...) {
    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    if (main_event(name) && screen_event_count < 16) {
        snprintf(screen_events[screen_event_count], sizeof(screen_events[0]),
                 "| %5d | %-9s | %-62.62s |", time, name, message);
        ++screen_event_count;
        if (display_scenario && isatty(STDOUT_FILENO)) {
            (void)render_screen(display_scenario);
            pause_display(display_delay_ms);
        }
    }
    if (fd >= 0) dprintf(fd, "time=%05d event=%-9s | %s\n", time, name, message);
}

static void print_header(const Scenario *s) {
    const char *strategy = s->strategy == FIRST_FIT ? "first suitable track" :
                                                        "most free capacity";
    char line[128];
    dprintf(STDOUT_FILENO,
            "+------------------------------------------------------------------------------+\n"
            "|                      SORTING STATION SIMULATION                          |\n"
            "+------------------------------------------------------------------------------+\n");
    snprintf(line, sizeof(line), "Tracks: %d   Locomotives: %d   Routes: %d   Wagons: %d",
             s->track_count, s->locomotive_count, s->route_count, s->wagon_count);
    dprintf(STDOUT_FILENO, "| %-76.76s |\n", line);
    snprintf(line, sizeof(line), "Arriving trains: %d   Required trains: %d",
             s->arrival_count, s->requirement_count);
    dprintf(STDOUT_FILENO, "| %-76.76s |\n", line);
    snprintf(line, sizeof(line), "Dispatch strategy: %s", strategy);
    dprintf(STDOUT_FILENO, "| %-76.76s |\n", line);
    dprintf(STDOUT_FILENO,
            "+------------------------------------------------------------------------------+\n\n"
            "EVENTS\n"
            "+-------+-----------+----------------------------------------------------------------+\n"
            "|  TIME | EVENT     | DETAILS                                                        |\n"
            "+-------+-----------+----------------------------------------------------------------+\n");
}

static int render_screen(const Scenario *s) {
    if (screen_event_count == 0) return 0;
    if (isatty(STDOUT_FILENO)) dprintf(STDOUT_FILENO, "\033[2J\033[H");
    print_header(s);
    for (int i = 0; i < screen_event_count; ++i)
        dprintf(STDOUT_FILENO, "%s\n", screen_events[i]);
    dprintf(STDOUT_FILENO,
            "+-------+-----------+----------------------------------------------------------------+\n\n"
            "LIVE RESOURCES\n"
            "+------+------------------+----------------------+\n"
            "| TYPE | ID / PURPOSE     | CURRENT STATE        |\n"
            "+------+------------------+----------------------+\n");
    for (int i = 0; i < s->track_count; ++i) {
        char identity[64], state[32];
        snprintf(identity, sizeof(identity), "%d %s", s->tracks[i].id,
                 s->tracks[i].purpose);
        snprintf(state, sizeof(state), "wagons %d / %d", s->tracks[i].occupied,
                 s->tracks[i].capacity);
        dprintf(STDOUT_FILENO, "| %-4s | %-16.16s | %-20.20s |\n",
                "PATH", identity, state);
    }
    for (int i = 0; i < s->locomotive_count; ++i) {
        char identity[32];
        snprintf(identity, sizeof(identity), "%d", s->locomotives[i].id);
        dprintf(STDOUT_FILENO, "| %-4s | %-16.16s | %-20.20s |\n", "LOCO", identity,
                s->locomotives[i].wagon_id == -1 ? "free" : "maneuver in progress");
    }
    dprintf(STDOUT_FILENO, "+------+------------------+----------------------+\n");
    screen_event_count = 0;
    return 1;
}

static void print_summary(const Scenario *s, int time, const char *reason,
                          int departed_wagons, int departed_trains,
                          int maneuvers, long wait_total, int wait_max) {
    char line[128];
    dprintf(STDOUT_FILENO,
            "+-------+-----------+----------------------------------------------------------------+\n\n"
            "RESULT\n"
            "+------------------------------------------------------------------------------+\n");
    snprintf(line, sizeof(line), "Status: %s", reason);
    dprintf(STDOUT_FILENO, "| %-76.76s |\n", line);
    snprintf(line, sizeof(line),
             "Virtual time: %d   Maneuvers: %d   Average wait: %.2f   Maximum wait: %d",
             time, maneuvers, maneuvers ? (double)wait_total / maneuvers : 0.0, wait_max);
    dprintf(STDOUT_FILENO, "| %-76.76s |\n", line);
    snprintf(line, sizeof(line), "Departed wagons: %d/%d   Departed trains: %d/%d",
             departed_wagons, s->wagon_count, departed_trains, s->requirement_count);
    dprintf(STDOUT_FILENO, "| %-76.76s |\n", line);
    dprintf(STDOUT_FILENO,
            "+------------------------------------------------------------------------------+\n"
            "RESOURCES\n"
            "+------+------------------+------------+------------+\n"
            "| TYPE | ID / PURPOSE     | FINAL      | PEAK/BUSY  |\n"
            "+------+------------------+------------+------------+\n");
    for (int i = 0; i < s->track_count; ++i) {
        char identity[64], final[24], peak[24];
        snprintf(identity, sizeof(identity), "%d %s", s->tracks[i].id,
                 s->tracks[i].purpose);
        snprintf(final, sizeof(final), "%d / %d", s->tracks[i].occupied,
                 s->tracks[i].capacity);
        snprintf(peak, sizeof(peak), "%d / %d", s->tracks[i].peak,
                 s->tracks[i].capacity);
        dprintf(STDOUT_FILENO, "| %-4s | %-16.16s | %-10.10s | %-10.10s |\n",
                "PATH", identity, final, peak);
    }
    for (int i = 0; i < s->locomotive_count; ++i) {
        double utilization = time ? 100.0 * s->locomotives[i].busy_time / time : 0.0;
        char identity[32], busy[24];
        snprintf(identity, sizeof(identity), "%d", s->locomotives[i].id);
        snprintf(busy, sizeof(busy), "%.1f%%", utilization);
        dprintf(STDOUT_FILENO, "| %-4s | %-16.16s | %-10.10s | %-10.10s |\n",
                "LOCO", identity,
                s->locomotives[i].wagon_id == -1 ? "free" : "busy", busy);
    }
    dprintf(STDOUT_FILENO,
            "+------+------------------+------------+------------+\n");
}

int validate_state(const Scenario *s, char *error, size_t size) {
    for (int i = 0; i < s->track_count; ++i) {
        int actual = 0, incoming = 0;
        for (int j = 0; j < s->wagon_count; ++j)
            if (s->wagons[j].state == SORTED && s->wagons[j].track_id == s->tracks[i].id) ++actual;
        for (int j = 0; j < s->locomotive_count; ++j)
            if (s->locomotives[j].wagon_id != -1 &&
                s->locomotives[j].target_track_id == s->tracks[i].id) ++incoming;
        if (actual != s->tracks[i].occupied ||
            actual + incoming > s->tracks[i].capacity) {
            snprintf(error, size, "track %d occupancy", s->tracks[i].id); return 0;
        }
    }
    for (int i = 0; i < s->wagon_count; ++i) {
        const Wagon *wagon = &s->wagons[i];
        int known_locomotive = 0;
        for (int j = 0; j < s->locomotive_count; ++j)
            if (s->locomotives[j].id == wagon->locomotive_id) known_locomotive = 1;
        int valid_location =
            ((wagon->state == PLANNED || wagon->state == WAITING) &&
             wagon->track_id == -1 && wagon->locomotive_id == -1) ||
            (wagon->state == MOVING && wagon->track_id == -1 &&
             known_locomotive) ||
            (wagon->state == SORTED &&
             track_by_id((Scenario *)s, wagon->track_id) != NULL &&
             wagon->locomotive_id == -1) ||
            (wagon->state == DEPARTED && wagon->locomotive_id == -1);
        if (!valid_location) {
            snprintf(error, size, "wagon %d location", wagon->id); return 0;
        }
    }
    for (int i = 0; i < s->locomotive_count; ++i) {
        int attached = 0;
        for (int j = 0; j < s->wagon_count; ++j)
            if (s->wagons[j].state == MOVING && s->wagons[j].locomotive_id == s->locomotives[i].id) ++attached;
        if (attached > 1 || (attached == 0) != (s->locomotives[i].wagon_id == -1)) {
            snprintf(error, size, "locomotive %d state", s->locomotives[i].id); return 0;
        }
    }
    for (int i = 0; i < s->route_count; ++i) if (s->routes[i].reserved_by != -1)
        for (int j = 0; j < s->routes[i].conflict_count; ++j) {
            Route *other = route_by_id((Scenario *)s, s->routes[i].conflicts[j]);
            if (other && other->reserved_by != -1) {
                snprintf(error, size, "routes %d and %d conflict", s->routes[i].id, other->id); return 0;
            }
        }
    return 1;
}

static Track *choose_track(Scenario *s, Wagon *wagon) {
    Track *best = NULL;
    int best_free = -1;
    for (int i = 0; i < s->track_count; ++i) {
        Track *track = &s->tracks[i];
        int incoming = 0;
        for (int j = 0; j < s->locomotive_count; ++j)
            if (s->locomotives[j].wagon_id != -1 &&
                s->locomotives[j].target_track_id == track->id) ++incoming;
        int free_slots = track->capacity - track->occupied - incoming;
        if (strcmp(track->purpose, wagon->destination) != 0 || free_slots <= 0) continue;
        if (!best || (s->strategy == MOST_FREE && free_slots > best_free)) {
            best = track;
            best_free = free_slots;
        }
    }
    return best;
}

static int route_available(Scenario *s, Route *route) {
    if (route->reserved_by != -1) return 0;
    for (int i = 0; i < route->conflict_count; ++i) {
        Route *other = route_by_id(s, route->conflicts[i]);
        if (other && other->reserved_by != -1) return 0;
    }
    return 1;
}

static int dispatch(Scenario *s, int time, int fd, long *wait_total, int *wait_max) {
    int assigned = 0;
    for (int w = 0; w < s->wagon_count; ++w) {
        Wagon *wagon = &s->wagons[w];
        if (wagon->state != WAITING) continue;
        Track *target = choose_track(s, wagon);
        Locomotive *locomotive = NULL;
        Route *route = NULL;
        if (!target) continue;
        Arrival *arrival = arrival_by_id(s, wagon->train_id);
        for (int i = 0; i < s->locomotive_count && !locomotive; ++i)
            if (s->locomotives[i].wagon_id == -1) locomotive = &s->locomotives[i];
        for (int i = 0; i < s->route_count && !route; ++i)
            if (s->routes[i].from_track_id == arrival->track_id &&
                s->routes[i].to_track_id == target->id && route_available(s, &s->routes[i])) route = &s->routes[i];
        if (!locomotive || !route) continue;
        int travel_duration = (int)ceil(route->length / locomotive->speed);
        int maneuver_duration = s->maneuver_duration > travel_duration ?
                                s->maneuver_duration : travel_duration;
        int duration = s->coupling_duration + maneuver_duration;
        int waited = time - wagon->wait_since;
        *wait_total += waited;
        if (waited > *wait_max) *wait_max = waited;
        wagon->state = MOVING;
        wagon->locomotive_id = locomotive->id;
        locomotive->wagon_id = wagon->id;
        locomotive->route_id = route->id;
        locomotive->target_track_id = target->id;
        locomotive->start_time = time + s->coupling_duration;
        locomotive->finish_time = time + duration;
        locomotive->started = 0;
        locomotive->busy_time += duration;
        route->reserved_by = locomotive->id;
        event(fd, time, "ASSIGN", "wagon=%d locomotive=%d target=%d", wagon->id, locomotive->id, target->id);
        event(fd, time, "RESERVE", "route=%d locomotive=%d", route->id, locomotive->id);
        ++assigned;
    }
    return assigned;
}

static void start_moves(Scenario *s, int time, int fd) {
    for (int i = 0; i < s->locomotive_count; ++i) {
        Locomotive *loco = &s->locomotives[i];
        if (loco->wagon_id == -1 || loco->started || loco->start_time != time) continue;
        loco->started = 1;
        event(fd, time, "START", "wagon=%d locomotive=%d route=%d",
              loco->wagon_id, loco->id, loco->route_id);
    }
}

static void finish_moves(Scenario *s, int time, int fd, int *maneuvers) {
    for (int i = 0; i < s->locomotive_count; ++i) {
        Locomotive *loco = &s->locomotives[i];
        if (loco->wagon_id == -1 || loco->finish_time != time) continue;
        Wagon *wagon = NULL;
        for (int j = 0; j < s->wagon_count; ++j) if (s->wagons[j].id == loco->wagon_id) wagon = &s->wagons[j];
        Track *track = track_by_id(s, loco->target_track_id);
        Route *route = route_by_id(s, loco->route_id);
        wagon->state = SORTED;
        wagon->track_id = track->id;
        wagon->locomotive_id = -1;
        ++track->occupied;
        if (track->occupied > track->peak) track->peak = track->occupied;
        route->reserved_by = -1;
        event(fd, time, "FINISH", "wagon=%d locomotive=%d route=%d", wagon->id, loco->id, route->id);
        event(fd, time, "PLACE", "wagon=%d track=%d", wagon->id, track->id);
        loco->wagon_id = -1;
        loco->started = 0;
        ++*maneuvers;
    }
}

static int form_trains(Scenario *s, int time, int fd) {
    int formed = 0;
    for (int i = 0; i < s->requirement_count; ++i) {
        Requirement *r = &s->requirements[i];
        if (r->departed) continue;
        int available = 0;
        for (int j = 0; j < s->wagon_count; ++j)
            if (s->wagons[j].state == SORTED && s->wagons[j].track_id == r->track_id &&
                strcmp(s->wagons[j].destination, r->destination) == 0) ++available;
        if (available < r->wagon_count) continue;
        event(fd, time, "FORM", "train=%d destination=%s wagons=%d", r->id, r->destination, r->wagon_count);
        Track *track = track_by_id(s, r->track_id);
        int taken = 0;
        for (int j = 0; j < s->wagon_count && taken < r->wagon_count; ++j) {
            Wagon *wagon = &s->wagons[j];
            if (wagon->state == SORTED && wagon->track_id == r->track_id &&
                strcmp(wagon->destination, r->destination) == 0) {
                wagon->state = DEPARTED; wagon->track_id = r->id; --track->occupied; ++taken;
            }
        }
        r->departed = 1;
        event(fd, time, "DEPART", "train=%d destination=%s wagons=%d", r->id, r->destination, taken);
        ++formed;
    }
    return formed;
}

static void pause_display(int milliseconds) {
    if (milliseconds <= 0) return;
    struct timespec delay = {milliseconds / 1000, (milliseconds % 1000) * 1000000L};
    nanosleep(&delay, NULL);
}

RunResult simulate(Scenario *s, const Config *config, int fd) {
    int time = 0, maneuvers = 0, departed_trains = 0, departed_wagons = 0;
    int wait_max = 0;
    long wait_total = 0;
    RunResult result = OK;
    char error[128];
    screen_event_count = 0;
    display_scenario = s;
    display_delay_ms = config->delay_ms;
    /* One loop iteration represents one unit of virtual time. */
    while (1) {
        if (stop_requested) { result = INTERRUPTED; break; }
        if (config->until > 0 && time >= config->until) { result = TIME_LIMIT; break; }
        finish_moves(s, time, fd, &maneuvers);
        for (int i = 0; i < s->arrival_count; ++i) {
            Arrival *arrival = &s->arrivals[i];
            if (!arrival->announced && arrival->time == time) {
                arrival->announced = 1;
                event(fd, time, "ARRIVE", "train=%d track=%d", arrival->id, arrival->track_id);
            }
        }
        for (int i = 0; i < s->wagon_count; ++i) {
            Wagon *wagon = &s->wagons[i];
            Arrival *arrival = arrival_by_id(s, wagon->train_id);
            if (wagon->state == PLANNED &&
                time == arrival->time + s->uncoupling_duration) {
                wagon->state = WAITING;
                wagon->wait_since = time;
                event(fd, time, "UNCOUPLE", "train=%d wagon=%d destination=%s",
                      wagon->train_id, wagon->id, wagon->destination);
            }
        }
        start_moves(s, time, fd);
        departed_trains += form_trains(s, time, fd);
        (void)dispatch(s, time, fd, &wait_total, &wait_max);
        if (!validate_state(s, error, sizeof(error))) {
            event(fd, time, "INVARIANT", "%s", error); result = INVARIANT_ERROR; break;
        }
        departed_wagons = 0;
        int active = 0, future = 0;
        for (int i = 0; i < s->wagon_count; ++i) {
            if (s->wagons[i].state == DEPARTED) ++departed_wagons;
            if (s->wagons[i].state == MOVING) active = 1;
            if (s->wagons[i].state == PLANNED) future = 1;
        }
        if (departed_wagons == s->wagon_count) break;
        if (!active && !future) { result = REMAINDER; break; }
        ++time;
    }
    const char *reason = result == OK ? "completed" : result == TIME_LIMIT ? "time limit" :
                         result == REMAINDER ? "unfulfillable remainder" :
                         result == INTERRUPTED ? "interrupted" : "invariant error";
    event(fd, time, "SUMMARY",
          "%s; wagons=%d/%d trains=%d/%d maneuvers=%d avg_wait=%.2f max_wait=%d",
          reason, departed_wagons, s->wagon_count, departed_trains,
          s->requirement_count, maneuvers,
          maneuvers ? (double)wait_total / maneuvers : 0.0, wait_max);
    (void)render_screen(s);
    print_summary(s, time, reason, departed_wagons, departed_trains,
                  maneuvers, wait_total, wait_max);
    display_scenario = NULL;
    for (int i = 0; i < s->track_count; ++i)
        event(fd, time, "TRACK", "id=%d final=%d/%d peak=%d",
              s->tracks[i].id, s->tracks[i].occupied, s->tracks[i].capacity, s->tracks[i].peak);
    for (int i = 0; i < s->locomotive_count; ++i)
        event(fd, time, "LOCOMOTIVE", "id=%d busy=%ld utilization=%.1f%%",
              s->locomotives[i].id, s->locomotives[i].busy_time,
              time ? 100.0 * s->locomotives[i].busy_time / time : 0.0);
    return result;
}
