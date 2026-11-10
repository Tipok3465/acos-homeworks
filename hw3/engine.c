#include "station.h"
#include "notify.h"

#include <errno.h>
#include <math.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_ACTORS (1 + MAX_LOCOMOTIVES + MAX_TRAINS + MAX_WAGONS + MAX_REQUIREMENTS)
typedef enum { DISPATCHER, LOCO, ARRIVAL, WAGON, DEPARTURE } Role;
typedef struct Engine Engine;
typedef struct {
    pid_t pid;
    Notification inbox;
    Role role;
    int index;
#ifdef USE_EXTERNAL
    int registered;
#endif
} Actor;

struct Engine {
    Notification mutex;
    Scenario shared_scenario;
    Notification ready;
    Actor actors[MAX_ACTORS];
    int dispatcher, locos[MAX_LOCOMOTIVES];
    int wagons[MAX_WAGONS], departures[MAX_REQUIREMENTS];
    int count, stopping, result, tick_ms, active, peak_active, log_failed;
    unsigned sequence;
    struct timespec epoch;
    char recent[8][160];
    unsigned recent_count;
#ifdef USE_EXTERNAL
    int registered, finished;
    char log_path[PATH_MAX];
#endif
};

static int process_log_fd;

typedef struct {
    Track tracks[MAX_TRACKS];
    Locomotive locos[MAX_LOCOMOTIVES];
    char recent[8][160];
    unsigned recent_count;
    int tracks_count, locos_count, wagons_count, states[5];
    int active, peak_active, stopping, result;
    long elapsed_ms;
} Snapshot;

static long elapsed_ms(const Engine *e) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - e->epoch.tv_sec) * 1000L +
           (now.tv_nsec - e->epoch.tv_nsec) / 1000000L;
}

static void stop_locked(Engine *e, int result);

static int wait_for(Engine *e, Notification *n) {
    int error = notification_wait(n);
    if (!error) return 0;
    fprintf(stderr, "notification wait: %s\n", strerror(error));
    notification_wait(&e->mutex);
    stop_locked(e, ERROR);
    notification_post(&e->mutex);
    return -1;
}
static void event(Engine *e, const char *name, const char *format, ...) {
    long ms = elapsed_ms(e);
    char detail[256];
    va_list args;
    va_start(args, format);
    vsnprintf(detail, sizeof(detail), format, args);
    va_end(args);
    if (dprintf(process_log_fd, "seq=%05u time_ms=%ld event=%s %s\n",
                ++e->sequence, ms, name, detail) < 0) {
        e->log_failed = 1;
        e->stopping = 1;
        e->result = ERROR;
    }
    snprintf(e->recent[e->recent_count % 8], sizeof(e->recent[0]),
             "%7ld ms %-9.9s %.125s", ms, name, detail);
    ++e->recent_count;
}

static void stop_locked(Engine *e, int result) {
    if (!e->stopping) {
        e->stopping = 1;
        e->result = result;
        event(e, "STOP", "result=%d active=%d", result, e->active);
    }
}

static int delay(Engine *e, int units) {
    long long remaining = (long long)units * e->tick_ms;
    do {
        notification_wait(&e->mutex);
        int stop = e->stopping;
        notification_post(&e->mutex);
        if (stop) return 0;
        if (remaining <= 0) break;
        int slice = remaining > 10 ? 10 : (int)remaining;
        struct timespec pause = {0, slice * 1000000L};
        while (nanosleep(&pause, &pause) != 0 && errno == EINTR) {}
        remaining -= slice;
    } while (1);
    return 1;
}

static int route_index(const Scenario *s, int id) {
    for (int i = 0; i < s->route_count; ++i) if (s->routes[i].id == id) return i;
    return -1;
}

static int track_index(const Scenario *s, int id) {
    for (int i = 0; i < s->track_count; ++i) if (s->tracks[i].id == id) return i;
    return -1;
}

static int source_track(const Scenario *s, const Wagon *w) {
    for (int i = 0; i < s->arrival_count; ++i)
        if (s->arrivals[i].id == w->train_id) return s->arrivals[i].track_id;
    return -1;
}

static int free_places(const Scenario *s, int track) {
    int free = s->tracks[track].capacity - s->tracks[track].occupied;
    for (int i = 0; i < s->locomotive_count; ++i)
        if (s->locomotives[i].wagon_id != -1 &&
            s->locomotives[i].target_track_id == s->tracks[track].id) --free;
    return free;
}

static int route_free(const Scenario *s, int index) {
    const Route *r = &s->routes[index];
    if (r->reserved_by != -1) return 0;
    for (int i = 0; i < r->conflict_count; ++i) {
        int other = route_index(s, r->conflicts[i]);
        if (other < 0 || s->routes[other].reserved_by != -1) return 0;
    }
    return 1;
}

static int available(const Scenario *s, const Requirement *r) {
    int count = 0;
    for (int i = 0; i < s->wagon_count; ++i)
        if (s->wagons[i].state == SORTED && s->wagons[i].planned_for == r->id &&
            s->wagons[i].track_id == r->track_id &&
            strcmp(s->wagons[i].destination, r->destination) == 0) ++count;
    return count;
}

static int enforce(Engine *e) {
    char error[128];
    int moving = 0;
    for (int w = 0; w < e->shared_scenario.wagon_count; ++w)
        moving += e->shared_scenario.wagons[w].state == MOVING;
    if (validate_state(&e->shared_scenario, error, sizeof(error))) {
        if (moving == e->active) return 1;
        snprintf(error, sizeof(error), "active assignment count mismatch");
    }
    event(e, "INVARIANT", "%s", error);
    if (e->stopping && !e->log_failed) e->result = INVARIANT_ERROR;
    stop_locked(e, INVARIANT_ERROR);
    return 0;
}

static void dispatch(Engine *e) {
    Scenario *s = &e->shared_scenario;
    int planned_remaining = 0;
    for (int w = 0; w < s->wagon_count; ++w)
        if (s->wagons[w].planned_for != -1 && s->wagons[w].state != DEPARTED)
            planned_remaining = 1;
    for (int w = 0; w < s->wagon_count; ++w) {
        Wagon *wagon = &s->wagons[w];
        if (wagon->state != WAITING) continue;
        if (wagon->planned_for == -1 && planned_remaining) continue;
        int planned_track = -1;
        for (int r = 0; r < s->requirement_count; ++r)
            if (s->requirements[r].id == wagon->planned_for)
                planned_track = s->requirements[r].track_id;
        int loco = -1, target = -1, route = -1, best_free = -1;
        for (int l = 0; l < s->locomotive_count; ++l)
            if (s->locomotives[l].wagon_id == -1) { loco = l; break; }
        if (loco < 0) break;
        for (int t = 0; t < s->track_count; ++t) {
            if (planned_track != -1 && s->tracks[t].id != planned_track) continue;
            int other_group = 0;
            for (int other = 0; other < s->wagon_count; ++other) {
                const Wagon *v = &s->wagons[other];
                if (v->planned_for == wagon->planned_for) continue;
                if (v->state == SORTED && v->track_id == s->tracks[t].id) other_group = 1;
                if (v->state == MOVING)
                    for (int l = 0; l < s->locomotive_count; ++l)
                        if (s->locomotives[l].wagon_id == v->id &&
                            s->locomotives[l].target_track_id == s->tracks[t].id) other_group = 1;
            }
            if (other_group) continue;
            int free = free_places(s, t);
            if (free <= 0 || strcmp(s->tracks[t].purpose, wagon->destination) != 0) continue;
            for (int r = 0; r < s->route_count; ++r) {
                if (s->routes[r].from_track_id != source_track(s, wagon) ||
                    s->routes[r].to_track_id != s->tracks[t].id || !route_free(s, r)) continue;
                if (target < 0 || (s->strategy == MOST_FREE && free > best_free)) {
                    target = t; route = r; best_free = free;
                }
                break;
            }
            if (target >= 0 && s->strategy == FIRST_FIT) break;
        }
        if (target < 0) continue;
        Locomotive *l = &s->locomotives[loco];
        l->wagon_id = wagon->id;
        l->route_id = s->routes[route].id;
        l->target_track_id = s->tracks[target].id;
        wagon->state = MOVING;
        wagon->locomotive_id = l->id;
        s->routes[route].reserved_by = l->id;
        ++e->active;
        if (e->active > e->peak_active) e->peak_active = e->active;
        event(e, "ASSIGN", "wagon=%d locomotive=%d track=%d", wagon->id, l->id, l->target_track_id);
        event(e, "RESERVE", "route=%d locomotive=%d active=%d", l->route_id, l->id, e->active);
        notification_post(&e->actors[e->locos[loco]].inbox);
    }
}

static void *dispatcher_main(Engine *e, Actor *a) {
    while (wait_for(e, &a->inbox) == 0) {
        notification_wait(&e->mutex);
        if (e->stopping) { notification_post(&e->mutex); break; }
        if (!enforce(e)) { notification_post(&e->mutex); break; }
        dispatch(e);
        int future = 0, departed = 0, pending_departure = 0;
        for (int w = 0; w < e->shared_scenario.wagon_count; ++w) {
            future += e->shared_scenario.wagons[w].state == PLANNED;
            departed += e->shared_scenario.wagons[w].state == DEPARTED;
        }
        for (int a = 0; a < e->shared_scenario.arrival_count; ++a)
            future += !e->shared_scenario.arrivals[a].announced;
        for (int r = 0; r < e->shared_scenario.requirement_count; ++r) {
            Requirement *requirement = &e->shared_scenario.requirements[r];
            if (!requirement->departed && available(&e->shared_scenario, requirement) >= requirement->wagon_count) {
                pending_departure = 1;
                notification_post(&e->actors[e->departures[r]].inbox);
            }
        }
        if (!enforce(e)) { notification_post(&e->mutex); break; }
        if (departed == e->shared_scenario.wagon_count && !future) stop_locked(e, OK);
        else if (!future && !e->active && !pending_departure) {
            event(e, "REMAINDER", "reason=selected_plan_exhausted strategy=%s remaining=%d",
                  e->shared_scenario.strategy == FIRST_FIT ? "first" : "most",
                  e->shared_scenario.wagon_count - departed);
            stop_locked(e, REMAINDER);
        }
        notification_post(&e->mutex);
    }
    return NULL;
}

static void *arrival_main(Engine *e, Actor *a) {
    Arrival *arrival = &e->shared_scenario.arrivals[a->index];
    if (!delay(e, arrival->time)) return NULL;
    notification_wait(&e->mutex);
    if (e->stopping) { notification_post(&e->mutex); return NULL; }
    event(e, "ARRIVE", "train=%d track=%d", arrival->id, arrival->track_id);
    notification_post(&e->mutex);
    if (!delay(e, e->shared_scenario.uncoupling_duration)) return NULL;
    notification_wait(&e->mutex);
    if (!e->stopping) {
        arrival->announced = 1;
        for (int w = 0; w < e->shared_scenario.wagon_count; ++w)
            if (e->shared_scenario.wagons[w].train_id == arrival->id) notification_post(&e->actors[e->wagons[w]].inbox);
        notification_post(&e->actors[e->dispatcher].inbox);
    }
    notification_post(&e->mutex);
    return NULL;
}

static void *wagon_main(Engine *e, Actor *a) {
    while (wait_for(e, &a->inbox) == 0) {
        notification_wait(&e->mutex);
        Wagon *w = &e->shared_scenario.wagons[a->index];
        int done = e->stopping || w->state == DEPARTED;
        if (!done && w->state == PLANNED) {
            w->state = WAITING;
            event(e, "UNCOUPLE", "wagon=%d train=%d", w->id, w->train_id);
            notification_post(&e->actors[e->dispatcher].inbox);
        }
        notification_post(&e->mutex);
        if (done) break;
    }
    return NULL;
}

static void *locomotive_main(Engine *e, Actor *a) {
    Scenario *s = &e->shared_scenario;
    Locomotive *l = &s->locomotives[a->index];
    while (wait_for(e, &a->inbox) == 0) {
        notification_wait(&e->mutex);
        if (e->stopping) { notification_post(&e->mutex); break; }
        int route = route_index(s, l->route_id);
        int duration = (int)ceil(s->routes[route].length / l->speed);
        if (duration < s->maneuver_duration) duration = s->maneuver_duration;
        notification_post(&e->mutex);
        if (!delay(e, s->coupling_duration)) break;
        notification_wait(&e->mutex);
        if (e->stopping) { notification_post(&e->mutex); break; }
        l->started = 1;
        event(e, "START", "wagon=%d locomotive=%d route=%d", l->wagon_id, l->id, l->route_id);
        notification_post(&e->mutex);
        if (!delay(e, duration)) break;
        notification_wait(&e->mutex);
        if (e->stopping) { notification_post(&e->mutex); break; }
        int t = track_index(s, l->target_track_id);
        for (int w = 0; w < s->wagon_count; ++w) if (s->wagons[w].id == l->wagon_id) {
            s->wagons[w].state = SORTED;
            s->wagons[w].track_id = l->target_track_id;
            s->wagons[w].locomotive_id = -1;
            break;
        }
        ++s->tracks[t].occupied;
        if (s->tracks[t].occupied > s->tracks[t].peak) s->tracks[t].peak = s->tracks[t].occupied;
        event(e, "FINISH", "wagon=%d locomotive=%d route=%d", l->wagon_id, l->id, l->route_id);
        event(e, "PLACE", "wagon=%d track=%d", l->wagon_id, l->target_track_id);
        s->routes[route].reserved_by = -1;
        l->wagon_id = -1;
        l->started = 0;
        --e->active;
        enforce(e);
        notification_post(&e->actors[e->dispatcher].inbox);
        notification_post(&e->mutex);
    }
    return NULL;
}

static void *departure_main(Engine *e, Actor *a) {
    Scenario *s = &e->shared_scenario;
    Requirement *r = &s->requirements[a->index];
    while (wait_for(e, &a->inbox) == 0) {
        notification_wait(&e->mutex);
        if (e->stopping || r->departed) { notification_post(&e->mutex); break; }
        if (available(s, r) >= r->wagon_count) {
            event(e, "FORM", "train=%d wagons=%d", r->id, r->wagon_count);
            int taken = 0, t = track_index(s, r->track_id);
            for (int w = 0; w < s->wagon_count && taken < r->wagon_count; ++w)
                if (s->wagons[w].state == SORTED && s->wagons[w].planned_for == r->id &&
                    s->wagons[w].track_id == r->track_id &&
                    strcmp(s->wagons[w].destination, r->destination) == 0) {
                    s->wagons[w].state = DEPARTED;
                    s->wagons[w].departure_id = r->id;
                    s->wagons[w].track_id = -1;
                    --s->tracks[t].occupied;
                    ++taken;
                    notification_post(&e->actors[e->wagons[w]].inbox);
                }
            r->departed = 1;
            enforce(e);
            event(e, "DEPART", "train=%d wagons=%d", r->id, taken);
            notification_post(&e->actors[e->dispatcher].inbox);
        }
        notification_post(&e->mutex);
    }
    return NULL;
}

static void *actor_main(Engine *e, Actor *a) {
    notification_post(&e->ready);
    if (wait_for(e, &a->inbox) != 0) return NULL;
    notification_wait(&e->mutex);
    int stop = e->stopping;
    notification_post(&e->mutex);
    if (stop) return NULL;
    switch (a->role) {
        case DISPATCHER: return dispatcher_main(e, a);
        case LOCO: return locomotive_main(e, a);
        case ARRIVAL: return arrival_main(e, a);
        case WAGON: return wagon_main(e, a);
        case DEPARTURE: return departure_main(e, a);
    }
    return NULL;
}

static int add_actor(Engine *e, Role role, int index) {
    Actor *a = &e->actors[e->count++];
    a->role = role; a->index = index;
    return e->count - 1;
}

static void capture(Engine *e, Snapshot *snapshot) {
    notification_wait(&e->mutex);
    *snapshot = (Snapshot){.tracks_count = e->shared_scenario.track_count,
        .locos_count = e->shared_scenario.locomotive_count, .wagons_count = e->shared_scenario.wagon_count,
        .recent_count = e->recent_count, .active = e->active,
        .peak_active = e->peak_active, .stopping = e->stopping,
        .result = e->result, .elapsed_ms = elapsed_ms(e)};
    memcpy(snapshot->tracks, e->shared_scenario.tracks, sizeof(snapshot->tracks));
    memcpy(snapshot->locos, e->shared_scenario.locomotives, sizeof(snapshot->locos));
    memcpy(snapshot->recent, e->recent, sizeof(snapshot->recent));
    for (int w = 0; w < e->shared_scenario.wagon_count; ++w)
        ++snapshot->states[e->shared_scenario.wagons[w].state];
    notification_post(&e->mutex);
}

static const char *result_name(int result) {
    switch (result) {
        case OK: return "completed";
        case TIME_LIMIT: return "time limit";
        case INTERRUPTED: return "interrupted";
        case REMAINDER: return "no further moves under selected plan";
        case INVARIANT_ERROR: return "invariant error";
        default: return "error";
    }
}

static void row(const char *format, ...) {
    char line[256];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    printf("| %-76.76s |\n", line);
}

static void render(const Snapshot *s, int tick_ms) {
    const char *border = "+------------------------------------------------------------------------------+";
    printf("\033[H\033[2J%s\n", border);
    row("SORTING STATION | %s", notification_backend());
    row("Status: %s", s->stopping ? result_name(s->result) : "running");
    row("Elapsed: %.2f s | Model: %.1f units | Scale: %d ms/unit",
        s->elapsed_ms / 1000.0, (double)s->elapsed_ms / tick_ms, tick_ms);
    row("Active assignments: %d | Peak: %d | Departed: %d / %d",
        s->active, s->peak_active, s->states[DEPARTED], s->wagons_count);
    puts(border);
    row("WAGONS  planned=%d waiting=%d moving=%d sorted=%d departed=%d",
        s->states[PLANNED], s->states[WAITING], s->states[MOVING],
        s->states[SORTED], s->states[DEPARTED]);
    puts(border);
    row("TRACK    PURPOSE                          OCCUPIED / CAPACITY   PEAK");
    for (int t = 0; t < s->tracks_count; ++t)
        row("%-8d %-32.32s %8d / %-8d %d", s->tracks[t].id,
            s->tracks[t].purpose, s->tracks[t].occupied, s->tracks[t].capacity,
            s->tracks[t].peak);
    puts(border);
    row("LOCOMOTIVE   STATE         WAGON   ROUTE   TARGET");
    for (int l = 0; l < s->locos_count; ++l) {
        const Locomotive *loco = &s->locos[l];
        if (loco->wagon_id == -1) row("%-12d free", loco->id);
        else row("%-12d %-12s %-7d %-7d %d", loco->id,
                 s->stopping ? "stopped" : (loco->started ? "moving" : "coupling"), loco->wagon_id,
                 loco->route_id, loco->target_track_id);
    }
    puts(border);
    row("RECENT EVENTS | Full history is saved to the log");
    unsigned first = s->recent_count > 8 ? s->recent_count - 8 : 0;
    for (unsigned i = first; i < s->recent_count; ++i) row("%s", s->recent[i % 8]);
    puts(border);
    row("Ctrl+C to stop safely");
    puts(border);
    fflush(stdout);
}

static void supervise(Engine *e, const Config *config, int live) {
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    for (;;) {
        long wait_ms = 100;
        notification_wait(&e->mutex);
        if (!e->stopping && config->until > 0) {
            long long left = (long long)config->until * e->tick_ms - elapsed_ms(e);
            if (left <= 0) stop_locked(e, TIME_LIMIT);
            else if (left < wait_ms) wait_ms = (long)left;
        }
        int stopping = e->stopping;
        notification_post(&e->mutex);
        if (stopping) break;
        if (live) {
            Snapshot snapshot;
            capture(e, &snapshot);
            render(&snapshot, e->tick_ms);
        }
        struct timespec timeout = {0, wait_ms * 1000000L};
        int signal = sigtimedwait(&signals, NULL, &timeout);
        if (signal == SIGINT || signal == SIGTERM) {
            notification_wait(&e->mutex);
            event(e, "SIGNAL", "number=%d", signal);
            stop_locked(e, INTERRUPTED);
            notification_post(&e->mutex);
        } else if (signal < 0 && errno != EAGAIN && errno != EINTR) {
            notification_wait(&e->mutex);
            stop_locked(e, ERROR);
            notification_post(&e->mutex);
        }
    }
}

#ifdef USE_EXTERNAL
static void wait_for_workers(Engine *e) {
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    for (;;) {
        notification_wait(&e->mutex);
        int ready = e->registered == e->count;
        if (!ready && elapsed_ms(e) >= 30000) {
            fprintf(stderr, "Timed out waiting for workers (%d/%d).\n", e->registered, e->count);
            stop_locked(e, ERROR);
        }
        int stopping = e->stopping;
        notification_post(&e->mutex);
        if (ready || stopping) return;
        struct timespec timeout = {0, 20000000L};
        int signal = sigtimedwait(&signals, NULL, &timeout);
        if (signal == SIGINT || signal == SIGTERM) {
            notification_wait(&e->mutex);
            event(e, "SIGNAL", "number=%d", signal);
            stop_locked(e, INTERRUPTED);
            notification_post(&e->mutex);
            return;
        }
    }
}

int run_worker(long owner, int slot) {
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &signals, NULL) < 0) return ERROR;
    char name[64];
    snprintf(name, sizeof(name), "/acos49-shm-%ld", owner);
    int fd = shm_open(name, O_RDWR, 0);
    if (fd < 0) { perror("worker shm_open"); return ERROR; }
    struct stat info;
    if (fstat(fd, &info) < 0 || info.st_size != (off_t)sizeof(Engine)) {
        close(fd);
        return ERROR;
    }
    Engine *e = mmap(NULL, sizeof(*e), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (e == MAP_FAILED) return ERROR;
    if (e->count < 1 || e->count > MAX_ACTORS || slot < 0 || slot >= e->count) goto failed;
    if (notification_connect(&e->mutex, owner) || notification_connect(&e->ready, owner)) goto failed;
    for (int i = 0; i < e->count; ++i)
        if (notification_connect(&e->actors[i].inbox, owner)) goto failed;
    process_log_fd = open(e->log_path, O_WRONLY | O_APPEND);
    if (process_log_fd < 0) goto failed;
    notification_wait(&e->mutex);
    Actor *actor = &e->actors[slot];
    if (e->stopping || actor->registered) {
        notification_post(&e->mutex);
        close(process_log_fd);
        goto failed;
    }
    actor->registered = 1;
    ++e->registered;
    notification_post(&e->mutex);
    actor_main(e, actor);
    close(process_log_fd);
    notification_wait(&e->mutex);
    ++e->finished;
    notification_post(&e->mutex);
    munmap(e, sizeof(*e));
    return OK;
failed:
    fprintf(stderr, "Could not attach worker %d to station %ld.\n", slot, owner);
    munmap(e, sizeof(*e));
    return ERROR;
}
#endif

int run_station(Scenario *s, int fd, const Config *config) {
    char name[64];
    snprintf(name, sizeof(name), "/acos49-shm-%ld", (long)getpid());
    int shm = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (shm < 0) { perror("shm_open"); return ERROR; }
#ifndef USE_EXTERNAL
    if (shm_unlink(name) < 0) { perror("shm_unlink"); close(shm); return ERROR; }
#endif
    if (ftruncate(shm, sizeof(Engine)) < 0) {
        perror("shared memory");
        close(shm);
        shm_unlink(name);
        return ERROR;
    }
    Engine *e = mmap(NULL, sizeof(*e), PROT_READ | PROT_WRITE, MAP_SHARED, shm, 0);
    close(shm);
    if (e == MAP_FAILED) { perror("mmap"); shm_unlink(name); return ERROR; }
#ifdef USE_EXTERNAL
    if (!realpath(config->log_path, e->log_path)) {
        perror("log path");
        shm_unlink(name);
        munmap(e, sizeof(*e));
        return ERROR;
    }
#endif
    e->shared_scenario = *s;
    s = &e->shared_scenario;
    process_log_fd = fd;
    e->tick_ms = config->tick_ms;
    e->result = ERROR;
    int live = !config->quiet && isatty(STDOUT_FILENO);
    int initialized = 0, created = 0;
    int rc = notification_init(&e->mutex);
    if (!rc) notification_post(&e->mutex);
    if (rc) { fprintf(stderr, "mutex: %s\n", strerror(rc)); shm_unlink(name); munmap(e, sizeof(*e)); return ERROR; }
    rc = notification_init(&e->ready);
    if (rc) { fprintf(stderr, "ready notification: %s\n", strerror(rc)); notification_destroy(&e->mutex); shm_unlink(name); munmap(e, sizeof(*e)); return ERROR; }
    e->dispatcher = add_actor(e, DISPATCHER, 0);
    for (int i = 0; i < s->locomotive_count; ++i) e->locos[i] = add_actor(e, LOCO, i);
    for (int i = 0; i < s->arrival_count; ++i) add_actor(e, ARRIVAL, i);
    for (int i = 0; i < s->wagon_count; ++i) e->wagons[i] = add_actor(e, WAGON, i);
    for (int i = 0; i < s->requirement_count; ++i) e->departures[i] = add_actor(e, DEPARTURE, i);
    clock_gettime(CLOCK_MONOTONIC, &e->epoch);
    plan_trains(s);
    for (int i = 0; i < e->count; ++i) {
        rc = notification_init(&e->actors[i].inbox);
        if (rc) { fprintf(stderr, "inbox notification: %s\n", strerror(rc)); goto cleanup; }
        ++initialized;
    }
#ifdef USE_EXTERNAL
    printf("READY %ld %d\n", (long)getpid(), e->count);
    fflush(stdout);
    wait_for_workers(e);
    if (e->stopping) goto cleanup;
    created = e->count;
#else
    for (int i = 0; i < e->count; ++i) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); goto cleanup; }
        if (pid == 0) { actor_main(e, &e->actors[i]); _exit(0); }
        e->actors[i].pid = pid;
        ++created;
    }
    for (int i = 0; i < created; ++i) if (wait_for(e, &e->ready) != 0) goto cleanup;
#endif
    clock_gettime(CLOCK_MONOTONIC, &e->epoch);
    for (int w = 0; w < s->wagon_count; ++w)
        if (s->wagons[w].planned_for != -1)
            event(e, "PLAN", "wagon=%d train=%d", s->wagons[w].id, s->wagons[w].planned_for);
    for (int i = 0; i < created; ++i) notification_post(&e->actors[i].inbox);
    notification_post(&e->actors[e->dispatcher].inbox);
    supervise(e, config, live);
cleanup:
    notification_wait(&e->mutex);
    e->stopping = 1;
#ifdef USE_EXTERNAL
    created = e->registered;
#endif
    notification_post(&e->mutex);
#ifdef USE_EXTERNAL
    for (int i = 0; i < initialized; ++i)
        if (e->actors[i].registered) notification_post(&e->actors[i].inbox);
    for (;;) {
        notification_wait(&e->mutex);
        int finished = e->finished;
        notification_post(&e->mutex);
        if (finished == created) break;
        struct timespec pause = {0, 10000000L};
        nanosleep(&pause, NULL);
    }
    shm_unlink(name);
#else
    for (int i = 0; i < created; ++i) notification_post(&e->actors[i].inbox);
    for (int i = 0; i < created; ++i) {
        int status;
        pid_t waited;
        do { waited = waitpid(e->actors[i].pid, &status, 0); } while (waited < 0 && errno == EINTR);
        if (waited < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) e->result = ERROR;
    }
#endif
    enforce(e);
    int departed = 0;
    for (int w = 0; w < s->wagon_count; ++w) departed += s->wagons[w].state == DEPARTED;
    event(e, "SUMMARY",
#ifdef USE_EXTERNAL
          "result=%d departed=%d peak_active=%d finished=%d",
#else
          "result=%d departed=%d peak_active=%d reaped=%d",
#endif
          e->result, departed, e->peak_active, created);
    for (int t = 0; t < s->track_count; ++t)
        event(e, "TRACK", "id=%d occupied=%d capacity=%d peak=%d",
              s->tracks[t].id, s->tracks[t].occupied, s->tracks[t].capacity, s->tracks[t].peak);
    if (live) {
        Snapshot snapshot;
        capture(e, &snapshot);
        render(&snapshot, e->tick_ms);
    }
    printf("Station: result=%d departed=%d/%d peak_active=%d processes=%d\n",
           e->result, departed, s->wagon_count, e->peak_active, created);
    if (e->log_failed) fprintf(stderr, "Could not write the complete event log.\n");
    for (int i = 0; i < initialized; ++i) notification_destroy(&e->actors[i].inbox);
    notification_destroy(&e->ready); notification_destroy(&e->mutex);
    int result = e->result;
    munmap(e, sizeof(*e));
    return result;
}
