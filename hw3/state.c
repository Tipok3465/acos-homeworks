#include "station.h"

#include <stdio.h>
#include <string.h>

static int bad(char *error, size_t size, const char *message, int id) {
    if (error && size) snprintf(error, size, "%s: %d", message, id);
    return 0;
}

static int wagon_index(const Scenario *s, int id) {
    for (int i = 0; i < s->wagon_count; ++i) if (s->wagons[i].id == id) return i;
    return -1;
}
static int loco_index(const Scenario *s, int id) {
    for (int i = 0; i < s->locomotive_count; ++i) if (s->locomotives[i].id == id) return i;
    return -1;
}
static int track_index(const Scenario *s, int id) {
    for (int i = 0; i < s->track_count; ++i) if (s->tracks[i].id == id) return i;
    return -1;
}
static int route_index(const Scenario *s, int id) {
    for (int i = 0; i < s->route_count; ++i) if (s->routes[i].id == id) return i;
    return -1;
}

int validate_state(const Scenario *s, char *error, size_t size) {
    for (int w = 0; w < s->wagon_count; ++w) {
        const Wagon *wagon = &s->wagons[w];
        if (wagon->state < PLANNED || wagon->state > DEPARTED)
            return bad(error, size, "invalid wagon state", wagon->id);
        if (wagon->state != MOVING && wagon->locomotive_id != -1)
            return bad(error, size, "unexpected wagon locomotive", wagon->id);
        if (wagon->state != SORTED && wagon->track_id != -1)
            return bad(error, size, "unexpected wagon track", wagon->id);
        if (wagon->state != DEPARTED && wagon->departure_id != -1)
            return bad(error, size, "unexpected departure owner", wagon->id);
        if (wagon->planned_for != -1) {
            int found = 0;
            for (int r = 0; r < s->requirement_count; ++r)
                if (s->requirements[r].id == wagon->planned_for &&
                    strcmp(s->requirements[r].destination, wagon->destination) == 0) {
                    found = 1;
                    if (wagon->state == SORTED && wagon->track_id != s->requirements[r].track_id)
                        return bad(error, size, "wagon on wrong planned track", wagon->id);
                    if (wagon->state == MOVING) {
                        int l = loco_index(s, wagon->locomotive_id);
                        if (l < 0 || s->locomotives[l].target_track_id != s->requirements[r].track_id)
                            return bad(error, size, "wagon moving to wrong planned track", wagon->id);
                    }
                    if (wagon->state == DEPARTED && wagon->departure_id != wagon->planned_for)
                        return bad(error, size, "wagon departed in wrong train", wagon->id);
                }
            if (!found) return bad(error, size, "invalid planned train", wagon->id);
        }
        if (wagon->state == MOVING) {
            int l = loco_index(s, wagon->locomotive_id);
            if (l < 0 || s->locomotives[l].wagon_id != wagon->id)
                return bad(error, size, "wagon has no matching locomotive", wagon->id);
        }
        if (wagon->state == SORTED) {
            int t = track_index(s, wagon->track_id);
            if (t < 0 || strcmp(s->tracks[t].purpose, wagon->destination) != 0)
                return bad(error, size, "wagon on incompatible track", wagon->id);
        }
        if (wagon->state == DEPARTED) {
            int found = 0;
            for (int r = 0; r < s->requirement_count; ++r)
                if (s->requirements[r].id == wagon->departure_id &&
                    s->requirements[r].departed &&
                    strcmp(s->requirements[r].destination, wagon->destination) == 0) found = 1;
            if (!found) return bad(error, size, "invalid departed wagon", wagon->id);
        }
    }
    for (int t = 0; t < s->track_count; ++t) {
        int actual = 0, incoming = 0;
        for (int w = 0; w < s->wagon_count; ++w)
            actual += s->wagons[w].state == SORTED && s->wagons[w].track_id == s->tracks[t].id;
        for (int l = 0; l < s->locomotive_count; ++l)
            incoming += s->locomotives[l].wagon_id != -1 &&
                        s->locomotives[l].target_track_id == s->tracks[t].id;
        if (actual != s->tracks[t].occupied || actual + incoming > s->tracks[t].capacity)
            return bad(error, size, "track occupancy mismatch", s->tracks[t].id);
    }
    for (int l = 0; l < s->locomotive_count; ++l) {
        const Locomotive *loco = &s->locomotives[l];
        if (loco->wagon_id == -1) {
            if (loco->started) return bad(error, size, "idle locomotive is moving", loco->id);
            continue;
        }
        int w = wagon_index(s, loco->wagon_id);
        int r = route_index(s, loco->route_id);
        int t = track_index(s, loco->target_track_id);
        if (w < 0 || r < 0 || t < 0 || s->wagons[w].state != MOVING ||
            s->wagons[w].locomotive_id != loco->id || s->routes[r].reserved_by != loco->id ||
            s->routes[r].to_track_id != loco->target_track_id ||
            strcmp(s->tracks[t].purpose, s->wagons[w].destination) != 0)
            return bad(error, size, "invalid locomotive assignment", loco->id);
        int source_matches = 0;
        for (int a = 0; a < s->arrival_count; ++a)
            if (s->arrivals[a].id == s->wagons[w].train_id &&
                s->arrivals[a].track_id == s->routes[r].from_track_id) source_matches = 1;
        if (!source_matches) return bad(error, size, "wrong route origin", loco->id);
    }
    for (int r = 0; r < s->route_count; ++r) {
        const Route *route = &s->routes[r];
        if (route->reserved_by == -1) continue;
        int l = loco_index(s, route->reserved_by);
        if (l < 0 || s->locomotives[l].wagon_id == -1 || s->locomotives[l].route_id != route->id)
            return bad(error, size, "route has no matching owner", route->id);
        for (int j = 0; j < route->conflict_count; ++j) {
            int other = route_index(s, route->conflicts[j]);
            if (other < 0 || s->routes[other].reserved_by != -1)
                return bad(error, size, "conflicting route reservations", route->id);
        }
    }
    for (int r = 0; r < s->requirement_count; ++r) {
        const Requirement *requirement = &s->requirements[r];
        int actual = 0;
        for (int w = 0; w < s->wagon_count; ++w)
            actual += s->wagons[w].state == DEPARTED &&
                      s->wagons[w].departure_id == requirement->id;
        if (actual != (requirement->departed ? requirement->wagon_count : 0))
            return bad(error, size, "departed train composition mismatch", requirement->id);
    }
    return 1;
}
