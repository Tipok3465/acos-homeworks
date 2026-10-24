#include "notify.h"

#include <errno.h>
#include <limits.h>

#ifdef USE_CONDVAR
int notification_init(Notification *n) {
    int error = pthread_mutex_init(&n->mutex, NULL);
    if (error) return error;
    error = pthread_cond_init(&n->changed, NULL);
    if (error) { pthread_mutex_destroy(&n->mutex); return error; }
    n->count = 0;
    return 0;
}

int notification_post(Notification *n) {
    int error = pthread_mutex_lock(&n->mutex);
    if (error) return error;
    if (n->count == UINT_MAX) error = EOVERFLOW;
    else {
        /* Сохраняем уведомление, даже если получатель ещё не ждёт. */
        ++n->count;
        error = pthread_cond_signal(&n->changed);
    }
    int unlock_error = pthread_mutex_unlock(&n->mutex);
    return error ? error : unlock_error;
}

int notification_wait(Notification *n) {
    int error = pthread_mutex_lock(&n->mutex);
    if (error) return error;
    while (!n->count && !error) error = pthread_cond_wait(&n->changed, &n->mutex);
    if (!error) --n->count;
    int unlock_error = pthread_mutex_unlock(&n->mutex);
    return error ? error : unlock_error;
}

int notification_destroy(Notification *n) {
    int error = pthread_cond_destroy(&n->changed);
    int mutex_error = pthread_mutex_destroy(&n->mutex);
    return error ? error : mutex_error;
}
const char *notification_backend(void) { return "condition variables"; }
#else
int notification_init(Notification *n) {
    return sem_init(&n->semaphore, 0, 0) == 0 ? 0 : errno;
}
int notification_post(Notification *n) {
    return sem_post(&n->semaphore) == 0 ? 0 : errno;
}
int notification_wait(Notification *n) {
    int result;
    do { result = sem_wait(&n->semaphore); } while (result != 0 && errno == EINTR);
    return result == 0 ? 0 : errno;
}
int notification_destroy(Notification *n) {
    return sem_destroy(&n->semaphore) == 0 ? 0 : errno;
}
const char *notification_backend(void) { return "semaphores"; }
#endif
