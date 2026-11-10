#ifndef NOTIFY_H
#define NOTIFY_H

#include <semaphore.h>

typedef struct {
#ifdef USE_NAMED
    unsigned id;
#else
    sem_t value;
#endif
} Notification;

int notification_init(Notification *notification);
int notification_wait(Notification *notification);
int notification_post(Notification *notification);
int notification_destroy(Notification *notification);
const char *notification_backend(void);
#ifdef USE_EXTERNAL
int notification_connect(const Notification *notification, long owner);
#endif
#endif
