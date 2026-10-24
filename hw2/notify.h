#ifndef NOTIFY_H
#define NOTIFY_H

#ifdef USE_CONDVAR
#include <pthread.h>
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    unsigned count;
} Notification;
#else
#include <semaphore.h>
typedef struct { sem_t semaphore; } Notification;
#endif

int notification_init(Notification *notification);
int notification_wait(Notification *notification);
int notification_post(Notification *notification);
int notification_destroy(Notification *notification);
const char *notification_backend(void);
#endif
