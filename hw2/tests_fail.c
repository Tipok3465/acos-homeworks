#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>

int __real_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
#ifdef USE_CONDVAR
int __real_pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *);
#else
int __real_sem_init(sem_t *, int, unsigned);
#endif

int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*function)(void *), void *arg) {
    static int calls;
    const char *fail = getenv("FAIL_THREAD_AT");
    if (fail && ++calls == atoi(fail)) return EAGAIN;
    return __real_pthread_create(thread, attr, function, arg);
}

#ifdef USE_CONDVAR
int __wrap_pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
    static int calls;
    const char *fail = getenv("FAIL_COND_AT");
    if (fail && ++calls == atoi(fail)) return EAGAIN;
    return __real_pthread_cond_init(cond, attr);
}
#else
int __wrap_sem_init(sem_t *semaphore, int shared, unsigned value) {
    static int calls;
    const char *fail = getenv("FAIL_SEMAPHORE_AT");
    if (fail && ++calls == atoi(fail)) { errno = ENOSPC; return -1; }
    return __real_sem_init(semaphore, shared, value);
}
#endif
