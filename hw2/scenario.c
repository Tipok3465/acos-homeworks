#define _POSIX_C_SOURCE 200809L
#include "station.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static int integer(const char *text, int minimum, int *value) {
    if (!isdigit((unsigned char)*text)) return 0;
    char *end;
    errno = 0;
    long parsed = strtol(text, &end, 10);
    if (errno || *end || parsed < minimum || parsed > MAX_NUMBER) return 0;
    *value = (int)parsed;
    return 1;
}

static int positive_real(const char *text, double *value) {
    char *end;
    errno = 0;
    double parsed = strtod(text, &end);
    if (errno || !*text || *end || !isfinite(parsed) ||
        parsed < 0.000001 || parsed > MAX_NUMBER) return 0;
    *value = parsed;
    return 1;
}

static int name_valid(const char *text) {
    size_t length = strlen(text);
    if (!length || length >= NAME_SIZE) return 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        if (!isalnum(*p) && *p != '_' && *p != '-') return 0;
    return 1;
}

static int fields(char *text, char separator, char **result, int capacity) {
    int count = 0;
    for (;;) {
        if (count == capacity) return -1;
        char *next = strchr(text, separator);
        if (next) *next = '\0';
        result[count] = trim(text);
        if (!*result[count++]) return -1;
        if (!next) return count;
        text = next + 1;
    }
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
            for (int k = 0; k < j; ++k)
                if (route->conflicts[k] == route->conflicts[j]) {
                    snprintf(error, size, "duplicate route conflict"); return 0;
                }
        }
        for (int l = 0; l < s->locomotive_count; ++l)
            if (ceil(route->length / s->locomotives[l].speed) > MAX_NUMBER) {
                snprintf(error, size, "route travel duration exceeds limit"); return 0;
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
        if (strlen(line) == sizeof(line) - 1 && !strchr(line, '\n')) {
            snprintf(error, size, "line %d exceeds 254 characters", line_number);
            fclose(file); return 0;
        }
        char *p = trim(line);
        if (!*p || *p == '#') continue;
        int a, b, c;
        double value;
        char *equal = strchr(p, '=');
        if (!equal) goto bad_line;
        *equal = '\0';
        char *key = trim(p), *parts[5];
        int count = fields(equal + 1, ',', parts, 5);
        if (count < 0) goto bad_line;
        if (strcmp(key, "track") == 0 && count == 3 &&
            integer(parts[0], 0, &a) && name_valid(parts[1]) && integer(parts[2], 1, &b) &&
            s->track_count < MAX_TRACKS) {
            s->tracks[s->track_count] = (Track){.id = a, .capacity = b};
            snprintf(s->tracks[s->track_count++].purpose, NAME_SIZE, "%s", parts[1]);
        } else if (strcmp(key, "locomotive") == 0 && count == 2 &&
                   integer(parts[0], 0, &a) && positive_real(parts[1], &value) &&
                   s->locomotive_count < MAX_LOCOMOTIVES) {
            s->locomotives[s->locomotive_count++] = (Locomotive){.id = a, .speed = value, .wagon_id = -1};
        } else if (strcmp(key, "route") == 0 && count == 5 &&
                   integer(parts[0], 0, &a) && integer(parts[1], 0, &b) &&
                   integer(parts[2], 0, &c) && positive_real(parts[3], &value) &&
                   s->route_count < MAX_ROUTES) {
            Route *route = &s->routes[s->route_count++];
            *route = (Route){.id = a, .from_track_id = b, .to_track_id = c,
                             .length = value, .reserved_by = -1};
            if (strcmp(parts[4], "-") != 0) {
                char *conflicts[MAX_CONFLICTS];
                route->conflict_count = fields(parts[4], '|', conflicts, MAX_CONFLICTS);
                if (route->conflict_count < 0) goto bad_line;
                for (int i = 0; i < route->conflict_count; ++i)
                    if (!integer(conflicts[i], 0, &route->conflicts[i])) goto bad_line;
            }
        } else if (strcmp(key, "wagon") == 0 && count == 3 &&
                   integer(parts[0], 0, &a) && integer(parts[1], 0, &b) && name_valid(parts[2]) &&
                   s->wagon_count < MAX_WAGONS) {
            Wagon *wagon = &s->wagons[s->wagon_count++];
            *wagon = (Wagon){.id = a, .train_id = b, .track_id = -1,
                             .locomotive_id = -1, .departure_id = -1, .planned_for = -1, .state = PLANNED};
            snprintf(wagon->destination, NAME_SIZE, "%s", parts[2]);
        } else if (strcmp(key, "arrival") == 0 && count == 3 &&
                   integer(parts[0], 0, &a) && integer(parts[1], 0, &b) && integer(parts[2], 0, &c) &&
                   s->arrival_count < MAX_TRAINS) {
            s->arrivals[s->arrival_count++] = (Arrival){a, b, c, 0};
        } else if (strcmp(key, "requirement") == 0 && count == 4 &&
                   integer(parts[0], 0, &a) && name_valid(parts[1]) &&
                   integer(parts[2], 1, &b) && integer(parts[3], 0, &c) &&
                   s->requirement_count < MAX_REQUIREMENTS) {
            Requirement *r = &s->requirements[s->requirement_count++];
            *r = (Requirement){.id = a, .wagon_count = b, .track_id = c};
            snprintf(r->destination, NAME_SIZE, "%s", parts[1]);
        } else if (strcmp(key, "timing") == 0 && count == 3 &&
                   integer(parts[0], 1, &a) && integer(parts[1], 1, &b) && integer(parts[2], 1, &c) &&
                   !s->timing_defined) {
            s->uncoupling_duration = a;
            s->coupling_duration = b;
            s->maneuver_duration = c;
            s->timing_defined = 1;
        } else if (strcmp(key, "strategy") == 0 && count == 1 &&
                   strcmp(parts[0], "first_fit") == 0) s->strategy = FIRST_FIT;
        else if (strcmp(key, "strategy") == 0 && count == 1 &&
                 strcmp(parts[0], "most_free_capacity") == 0) s->strategy = MOST_FREE;
        else {
bad_line:
            snprintf(error, size, "line %d is invalid", line_number);
            fclose(file); return 0;
        }
    }
    int read_failed = ferror(file);
    int close_failed = fclose(file);
    if (read_failed || close_failed != 0) { snprintf(error, size, "read error"); return 0; }
    return validate_scenario(s, error, size);
}

