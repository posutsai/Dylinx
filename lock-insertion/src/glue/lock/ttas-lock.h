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

#ifndef __DYLINX_TTAS_LOCK__
#define __DYLINX_TTAS_LOCK__

typedef struct ttas_lock {
  volatile uint8_t spin_lock __attribute__((aligned(L_CACHE_LINE_SIZE)));
  char __pad[pad_to_cache_line(sizeof(uint8_t))];
  pthread_mutex_original_t posix_lock;
} ttas_lock_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

// Paper:
// The Performance of Spin Lock Alternatives for Shared-Memory Multiprocessors
// ---------------------------------------------------------------------------
// Note:
// 1. The private attribute ttas_lock_t *impl will switch among three states
//    {UNLOCKED=1, LOCKED-1, 255}

int ttas_init(void **entity, pthread_mutexattr_t *attr) {
#ifdef __DYLINX_DEBUG__
  printf("ttas-lock is initialized !!!\n");
#endif
  // *entity = (ttas_lock_t *)alloc_cache_align(sizeof(ttas_lock_t));
  *entity = (ttas_lock_t *)malloc(sizeof(ttas_lock_t));
  ttas_lock_t *mtx = *entity;
  mtx->spin_lock = UNLOCKED;
  pthread_mutex_init_original(&mtx->posix_lock, attr);
  return 0;
}

int ttas_lock(void *entity) {
  ttas_lock_t *mtx = entity;
  while (1) {
    while (mtx->spin_lock != UNLOCKED)
      CPU_PAUSE();
    if (l_tas_uint8(&mtx->spin_lock) == UNLOCKED)
      break;
  }
  int ret = pthread_mutex_lock_original(&mtx->posix_lock);
  assert(ret == 0);
#ifdef __DYLINX_DEBUG__
  printf("ttas-lock is enabled !!!\n");
#endif
  return 0;
}

int ttas_trylock(void *entity) {}

int __ttas_unlock(void *entity) {
  COMPILER_BARRIER();
  ttas_lock_t *mtx = entity;
  mtx->spin_lock = UNLOCKED;
#ifdef __DYLINX_DEBUG__
  printf("ttas-lock is disabled !!!\n");
#endif
  return 1;
}

int ttas_unlock(void *entity) {
  ttas_lock_t *mtx = entity;
  int ret = pthread_mutex_unlock_original(&mtx->posix_lock);
  assert(ret == 0);
  return __ttas_unlock(entity);
}

int ttas_destroy(void *entity) {
#ifdef __DYLINX_DEBUG__
  printf("ttas-lock is finalized !!!\n");
#endif
  ttas_lock_t *mtx = entity;
  int ret = pthread_mutex_destroy_original(&mtx->posix_lock);
  assert(ret == 0);
  free(mtx);
  return 1;
}

int ttas_condwait(pthread_cond_t *cond, void *entity) {
  int res;
  __ttas_unlock(entity);
  ttas_lock_t *mtx = entity;
  res = pthread_cond_wait_original(cond, &mtx->posix_lock);
  if (res != 0 && res != ETIMEDOUT) {
    HANDLING_ERROR(
      "Error happens when trying to conduct "
      "pthread_cond_wait on internal posix_lock"
    );
  }
  int ret = 0;
  if (ret = pthread_mutex_unlock_original(&mtx->posix_lock) != 0) {
    HANDLING_ERROR(
      "Error happens when trying to conduct "
      "pthread_mutex_unlock on internal posix_lock"
    );
  }
  ttas_lock(entity);
  return res;
}


#endif
