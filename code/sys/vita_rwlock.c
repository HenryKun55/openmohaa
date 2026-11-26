/*
 * pthread read-write locks without kernel objects, for the eboot (-Wl,--wrap=pthread_rwlock_*,
 * cmake/platforms/vita.cmake).
 *
 * OpenSSL 1.1.1 gives every certificate, BIO and connection a lock of its own
 * (CRYPTO_THREAD_lock_new -> pthread_rwlock_init). The SDK's pthread rwlock (rebuilt SDK, from
 * v0.6 on) creates kernel objects, which run out on the console (286 could be made at the
 * update check, 1024+ in Vita3K): loading the 121 certificates of cacert.pem stopped at 91, then "could not create a context", and OpenSSL reports every
 * one of those failures as "malloc failure" (the update check broke from v0.6 on; v0.5, built
 * with the old SDK, downloaded v0.6 fine). Vita3K has no such limit.
 *
 * These locks live in ordinary memory and spin on an atomic word, giving the CPU up between
 * tries: OpenSSL's locks are almost never contended. state: 0 free, > 0 readers, -1 writer.
 */

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <psp2/kernel/threadmgr.h>

typedef struct {
    volatile int state;
} vitaRwlock_t;

#define VITA_RWLOCK_STATIC ((pthread_rwlock_t)-1) /* PTHREAD_RWLOCK_INITIALIZER */

static void VitaRw_Pause(int spins)
{
    if (spins > 64) {
        sceKernelDelayThread(100);
    }
}

/* the lock behind *lock, created on first use for PTHREAD_RWLOCK_INITIALIZER */
static vitaRwlock_t *VitaRw_Get(pthread_rwlock_t *lock)
{
    pthread_rwlock_t cur;

    if (!lock) {
        return NULL;
    }
    cur = *lock;
    if (cur && cur != VITA_RWLOCK_STATIC) {
        return (vitaRwlock_t *)cur;
    }
    if (cur == VITA_RWLOCK_STATIC) {
        vitaRwlock_t *fresh = (vitaRwlock_t *)calloc(1, sizeof(*fresh));
        if (!fresh) {
            return NULL;
        }
        if (!__sync_bool_compare_and_swap(lock, VITA_RWLOCK_STATIC, (pthread_rwlock_t)fresh)) {
            free(fresh); /* another thread won */
        }
        return (vitaRwlock_t *)*lock;
    }
    return NULL;
}

int __wrap_pthread_rwlock_init(pthread_rwlock_t *lock, const pthread_rwlockattr_t *attr)
{
    vitaRwlock_t *l;

    (void)attr;
    if (!lock) {
        return EINVAL;
    }
    l = (vitaRwlock_t *)calloc(1, sizeof(*l));
    if (!l) {
        return ENOMEM;
    }
    *lock = (pthread_rwlock_t)l;
    return 0;
}

int __wrap_pthread_rwlock_destroy(pthread_rwlock_t *lock)
{
    if (!lock) {
        return EINVAL;
    }
    if (*lock && *lock != VITA_RWLOCK_STATIC) {
        free(*lock);
    }
    *lock = NULL;
    return 0;
}

int __wrap_pthread_rwlock_tryrdlock(pthread_rwlock_t *lock)
{
    vitaRwlock_t *l = VitaRw_Get(lock);
    int           s;

    if (!l) {
        return EINVAL;
    }
    s = l->state;
    if (s >= 0 && __sync_bool_compare_and_swap(&l->state, s, s + 1)) {
        return 0;
    }
    return EBUSY;
}

int __wrap_pthread_rwlock_trywrlock(pthread_rwlock_t *lock)
{
    vitaRwlock_t *l = VitaRw_Get(lock);

    if (!l) {
        return EINVAL;
    }
    return __sync_bool_compare_and_swap(&l->state, 0, -1) ? 0 : EBUSY;
}

int __wrap_pthread_rwlock_rdlock(pthread_rwlock_t *lock)
{
    vitaRwlock_t *l = VitaRw_Get(lock);
    int           spins;

    if (!l) {
        return EINVAL;
    }
    for (spins = 0;; spins++) {
        const int s = l->state;
        if (s >= 0 && __sync_bool_compare_and_swap(&l->state, s, s + 1)) {
            return 0;
        }
        VitaRw_Pause(spins);
    }
}

int __wrap_pthread_rwlock_wrlock(pthread_rwlock_t *lock)
{
    vitaRwlock_t *l = VitaRw_Get(lock);
    int           spins;

    if (!l) {
        return EINVAL;
    }
    for (spins = 0;; spins++) {
        if (__sync_bool_compare_and_swap(&l->state, 0, -1)) {
            return 0;
        }
        VitaRw_Pause(spins);
    }
}

int __wrap_pthread_rwlock_timedrdlock(pthread_rwlock_t *lock, const struct timespec *abstime)
{
    (void)abstime;
    return __wrap_pthread_rwlock_rdlock(lock);
}

int __wrap_pthread_rwlock_timedwrlock(pthread_rwlock_t *lock, const struct timespec *abstime)
{
    (void)abstime;
    return __wrap_pthread_rwlock_wrlock(lock);
}

int __wrap_pthread_rwlock_unlock(pthread_rwlock_t *lock)
{
    vitaRwlock_t *l = VitaRw_Get(lock);

    if (!l) {
        return EINVAL;
    }
    if (l->state == -1) {
        __sync_synchronize();
        l->state = 0;
    } else if (l->state > 0) {
        __sync_fetch_and_sub(&l->state, 1);
    } else {
        return EPERM;
    }
    return 0;
}
