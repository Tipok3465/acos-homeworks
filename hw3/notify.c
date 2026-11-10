#include "notify.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#ifdef USE_NAMED
static sem_t *handles[512];
static unsigned serial;

static void semaphore_name(char name[64], long owner, unsigned id) {
    snprintf(name, 64, "/acos49-sem-%ld-%u", owner, id);
}
#endif

static sem_t *semaphore(Notification *n) {
#ifdef USE_NAMED
    return handles[n->id];
#else
    return &n->value;
#endif
}

int notification_init(Notification *n) {
#ifdef USE_NAMED
    char name[64];
    if (serial + 1 >= sizeof(handles) / sizeof(handles[0])) return ENOSPC;
    n->id = ++serial;
    semaphore_name(name, (long)getpid(), n->id);
    handles[n->id] = sem_open(name, O_CREAT | O_EXCL, 0600, 0);
    if (handles[n->id] == SEM_FAILED) return errno;
#ifndef USE_EXTERNAL
    if (sem_unlink(name) < 0) {
        int error = errno;
        sem_close(handles[n->id]);
        return error;
    }
#endif
    return 0;
#else
    return sem_init(&n->value, 1, 0) == 0 ? 0 : errno;
#endif
}

int notification_wait(Notification *n) {
    int result;
    do { result = sem_wait(semaphore(n)); } while (result < 0 && errno == EINTR);
    return result == 0 ? 0 : errno;
}

int notification_post(Notification *n) {
    return sem_post(semaphore(n)) == 0 ? 0 : errno;
}

int notification_destroy(Notification *n) {
#ifdef USE_NAMED
#ifdef USE_EXTERNAL
    char name[64];
    semaphore_name(name, (long)getpid(), n->id);
    int error = sem_unlink(name) < 0 ? errno : 0;
    if (sem_close(handles[n->id]) < 0 && !error) error = errno;
    return error;
#else
    return sem_close(handles[n->id]) == 0 ? 0 : errno;
#endif
#else
    return sem_destroy(&n->value) == 0 ? 0 : errno;
#endif
}

#ifdef USE_EXTERNAL
int notification_connect(const Notification *n, long owner) {
    if (!n->id || n->id >= sizeof(handles) / sizeof(handles[0])) return EINVAL;
    char name[64];
    semaphore_name(name, owner, n->id);
    handles[n->id] = sem_open(name, 0);
    return handles[n->id] == SEM_FAILED ? errno : 0;
}
#endif

const char *notification_backend(void) {
#ifdef USE_NAMED
    return "named POSIX semaphores";
#else
    return "unnamed POSIX semaphores";
#endif
}
