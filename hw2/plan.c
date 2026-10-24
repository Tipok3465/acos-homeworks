#include "station.h"

#include <string.h>

static int track_index(const Scenario *s, int id) {
    for (int i = 0; i < s->track_count; ++i) if (s->tracks[i].id == id) return i;
    return -1;
}

static int reachable(const Scenario *s, int w, int requirement) {
    const Requirement *r = &s->requirements[requirement];
    const Wagon *wagon = &s->wagons[w];
    if (strcmp(wagon->destination, r->destination) != 0) return 0;
    int source = -1;
    for (int a = 0; a < s->arrival_count; ++a)
        if (s->arrivals[a].id == wagon->train_id) source = s->arrivals[a].track_id;
    for (int route = 0; route < s->route_count; ++route)
        if (s->routes[route].from_track_id == source &&
            s->routes[route].to_track_id == r->track_id) return 1;
    return 0;
}

/* slots хранит номера требований, owner[w] — место, занятое вагоном w.
 * Если вагон уже занят, пробуем найти другое назначение для его места. */
static int match(const Scenario *s, int slot, const int *slots, int *owner, int *seen) {
    for (int w = 0; w < s->wagon_count; ++w) {
        if (seen[w] || !reachable(s, w, slots[slot])) continue;
        seen[w] = 1;
        if (owner[w] < 0 || match(s, owner[w], slots, owner, seen)) {
            owner[w] = slot;
            return 1;
        }
    }
    return 0;
}

void plan_trains(Scenario *s) {
    int owner[MAX_WAGONS], slots[MAX_WAGONS], order[MAX_REQUIREMENTS], count = 0;
    for (int w = 0; w < MAX_WAGONS; ++w) owner[w] = -1;
    for (int w = 0; w < s->wagon_count; ++w) {
        s->wagons[w].planned_for = -1;
    }
    for (int r = 0; r < s->requirement_count; ++r) order[r] = r;
    for (int i = 1; i < s->requirement_count; ++i) {
        int r = order[i], j = i;
        int t = track_index(s, s->requirements[r].track_id);
        while (j > 0) {
            int previous = track_index(s, s->requirements[order[j - 1]].track_id);
            int before = s->strategy == MOST_FREE ?
                s->tracks[t].capacity > s->tracks[previous].capacity : t < previous;
            if (!before) break;
            order[j] = order[j - 1];
            --j;
        }
        order[j] = r;
    }
    for (int i = 0; i < s->requirement_count; ++i) {
        int r = order[i];
        int needed = s->requirements[r].wagon_count;
        int t = track_index(s, s->requirements[r].track_id);
        if (needed > s->tracks[t].capacity || needed > s->wagon_count - count) continue;
        int backup[MAX_WAGONS];
        memcpy(backup, owner, sizeof(owner));
        int accepted = 1;
        for (int n = 0; n < needed; ++n) {
            slots[count + n] = r;
            int seen[MAX_WAGONS] = {0};
            if (!match(s, count + n, slots, owner, seen)) { accepted = 0; break; }
        }
        /* Частичный комплект займёт путь, но не сможет уехать. */
        if (accepted) count += needed;
        else memcpy(owner, backup, sizeof(owner));
    }
    for (int w = 0; w < s->wagon_count; ++w)
        if (owner[w] >= 0) s->wagons[w].planned_for = s->requirements[slots[owner[w]]].id;
}
