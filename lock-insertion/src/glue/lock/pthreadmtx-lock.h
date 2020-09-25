#include "dylinx-padding.h"
#include "dylinx-utils.h"
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#ifndef __DYLINX_REPLACE_PTHREAD_NATIVE__
#define __DYLINX_REPLACE_PTHREAD_NATIVE__
#define pthread_mutex_t pthread_mutex_original_t
#define pthread_mutex_init pthread_mutex_init_original
#define pthread_mutex_lock pthread_mutex_lock_original
#define pthread_mutex_unlock pthread_mutex_unlock_original
#define pthread_mutex_destroy pthread_mutex_destroy_original
#define pthread_mutex_trylock pthread_mutex_trylock_original
#define pthread_cond_wait pthread_cond_wait_original
#define pthread_cond_timedwait pthread_cond_timedwait_original
#include <pthread.h>
#undef pthread_mutex_init
#undef pthread_mutex_lock
#undef pthread_mutex_unlock
#undef pthread_mutex_destroy
#undef pthread_mutex_trylock
#undef pthread_cond_wait
#undef pthread_cond_timedwait
#endif

#ifndef __DYLINX_PTHREADMTX_LOCK__
#define __DYLINX_PTHREADMTX_LOCK__

typedef struct pthreadmtx_lock {
  pthread_mutex_t posix_lock;
} pthreadmtx_lock_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

int pthreadmtx_init(void **entity, pthread_mutexattr_t *attr) {
  *entity = malloc(sizeof(pthread_mutex_t));
  return pthread_mutex_init_original((pthread_mutex_t *)(*entity), attr);
}
int pthreadmtx_lock(void *entity) {
  return pthread_mutex_lock_original((pthread_mutex_t *)entity);
}
int pthreadmtx_trylock(void *entity) {
  return pthread_mutex_trylock_original((pthread_mutex_t *)entity);
}
int pthreadmtx_unlock(void *entity) {
  return pthread_mutex_unlock_original((pthread_mutex_t *)entity);
}
int pthreadmtx_destroy(void *entity) {
  return pthread_mutex_destroy_original((pthread_mutex_t *)entity);
}
int pthreadmtx_cond_timedwait(pthread_cond_t *cond, void *entity, const struct timespec *t) {
}

#endif
