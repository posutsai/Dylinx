#include "dylinx-padding.h"
#include "dylinx-utils.h"
#include <stdio.h>
#include <errno.h>

#ifndef __DYLINX_PTHREADMTX_LOCK__
#define __DYLINX_PTHREADMTX_LOCK__

typedef struct pthreadmtx_lock {
  pthread_mutex_t posix_lock;
} pthreadmtx_lock_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

int pthreadmtx_init(void **entity, pthread_mutexattr_t *attr) {
  *entity = malloc(sizeof(pthread_mutex_t));
  return pthread_mutex_init((pthread_mutex_t *)*entity, attr);
}
int pthreadmtx_lock(void **entity) {
  return pthread_mutex_lock((pthread_mutex_t *)*entity);
}
int pthreadmtx_unlock(void **entity) {
  return pthread_mutex_unlock((pthread_mutex_t *)*entity);
}
int pthreadmtx_destroy(void **entity) {
  return pthread_mutex_destroy((pthread_mutex_t *)*entity);
}
int pthreadmtx_condwait(pthread_cond_t *cond, void **entity) {}

DYLINX_EXTERIOR_WRAPPER_IMPLE(pthreadmtx);
#endif
