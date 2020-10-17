#include "dylinx-padding.h"
#include "dylinx-utils.h"
#include <stdio.h>

#ifndef __DYLINX_BACKOFF_LOCK__
#define __DYLINX_BACKOFF_LOCK__

// Source code implementation refers to following two places.
// 1. backoff-lock implementation in LITL
// https://github.com/multicore-locks/litl/blob/master/src/backoff.c
// 2. backoff-lock implementation in concurrencykit
// https://github.com/concurrencykit/ck/blob/master/include/ck_backoff.h

#define DEFAULT_BACKOFF_DELAY (1 << 9)
#define MAX_BACKOFF_DELAY ((1 << 20) -1)

typedef struct backoff_lock {
  volatile uint8_t spin_lock __attribute__((aligned(L_CACHE_LINE_SIZE)));
  pthread_mutex_t posix_lock;
} backoff_lock_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

int backoff_init(void **entity, pthread_mutexattr_t *attr) {
#ifdef __DYLINX_DEBUG__
  printf("backoff-lock is initialized\n");
#endif
  *entity = (backoff_lock_t *)alloc_cache_align(sizeof(backoff_lock_t));
  backoff_lock_t *mtx = *entity;
  mtx->spin_lock = UNLOCKED;
  pthread_mutex_init_original(&mtx->posix_lock, NULL);
  return 0;
}

int backoff_lock(void *entity) {
  uint32_t delay = DEFAULT_BACKOFF_DELAY;
  backoff_lock_t *mtx = entity;
  while (1) {
    while (mtx->spin_lock != UNLOCKED) {
      for (uint32_t i = 0; i < delay; i++)
        CPU_PAUSE();
      if (delay < MAX_BACKOFF_DELAY)
        delay *= 2;
    }
    if (l_tas_uint8(&mtx->spin_lock) == UNLOCKED)
      break;
  }
  int ret = pthread_mutex_lock_original(&mtx->posix_lock);
  assert(ret == 0);
  return 0;
}

int backoff_trylock(void *entity) {
  backoff_lock_t *mtx = entity;
  if (l_tas_uint8(&mtx->spin_lock) == UNLOCKED) {
    int ret = 0;
    while ((ret = pthread_mutex_try_original(&mtx->posix_lock)) == EBUSY)
      CPU_PAUSE();
    assert(ret == 0);
    return 0;
  }
  return EBUSY;
}

int __backoff_unlock(void *entity) {
  COMPILER_BARRIER();
  backoff_lock_t *mtx = entity;
  mtx->spin_lock = UNLOCKED;
#ifdef __DYLINX_DEBUG__
  printf("backoff-lock is disabled !!!\n");
#endif
  return 1;
}

int backoff_unlock(void *entity) {
  backoff_lock_t *mtx = entity;
  int ret = pthread_mutex_unlock_original(&mtx->posix_lock);
  assert(ret == 0);
  return __backoff_unlock(entity);
}

int backoff_destroy(void *entity) {
#ifdef __DYLINX_DEBUG__
  printf("backoff-lock is finalized !!!\n");
#endif
  backoff_lock_t *mtx = entity;
  int ret = pthread_mutex_destroy_original(&mtx->posix_lock);
  free(mtx);
  return ret;
}

int backoff_cond_timedwait(pthread_cond_t *cond, void *entity, const struct timespec *time) {
  int res;
  __backoff_unlock(entity);
  backoff_lock_t *mtx = entity;
  if (time)
    res = pthread_cond_timedwait_original(cond, &mtx->posix_lock, time);
  else
    res = pthread_cond_wait_original(cond, &mtx->posix_lock);

  if (res != 0 && res != ETIMEDOUT) {
    HANDLING_ERROR(
      "Error happens when trying to conduct "
      "pthread_cond_wait on internal posix_lock"
      "in a backoff_lock"
    );
  }
  int ret = 0;
  if (ret = pthread_mutex_unlock_original(&mtx->posix_lock) != 0) {
    HANDLING_ERROR(
      "Error happens when trying to conduct "
      "pthread_mutex_unlock on internal posix_lock"
      "in a backoff_lock"
    );
  }
  backoff_lock(entity);
  return res;
}

#endif
