#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#ifndef __DYLINX_ADAPTIVEMTX_LOCK__
#define __DYLINX_ADAPTIVEMTX_LOCK__

typedef struct adaptivemtx_lock {
  pthread_mutex_t posix_lock;
  pthread_mutex_t core;
} adaptivemtx_lock_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

int adaptivemtx_init(void **entity, pthread_mutexattr_t *cond_attr) {
  *entity = (adaptivemtx_lock_t *)alloc_cache_align(sizeof(adaptivemtx_lock_t));
  adaptivemtx_lock_t *mtx = *entity;
  pthread_mutexattr_t adap_attr;
  pthread_mutexattr_init(&adap_attr);
  pthread_mutexattr_settype(&adap_attr, PTHREAD_MUTEX_ADAPTIVE_NP);
  int core_ret = pthread_mutex_init_original(&mtx->core, &adap_attr);
  int cond_ret = pthread_mutex_init_original(&mtx->posix_lock, cond_attr);
#ifdef __DYLINX_DEBUG__
  printf("adaptivemtx-lock is initialized !!!\n");
#endif
  return core_ret || cond_ret;
}

int adaptivemtx_lock(void *entity) {
  adaptivemtx_lock_t *mtx = entity;
  int core_ret = pthread_mutex_lock_original(&mtx->core);
  int posix_ret = pthread_mutex_lock_original(&mtx->posix_lock);
  return core_ret || posix_ret;
}

int adaptivemtx_trylock(void *entity) {
  adaptivemtx_lock_t *mtx = entity;
  if (pthread_mutex_trylock_original(&mtx->core) != EBUSY) {
    int ret = 0;
    while((ret = pthread_mutex_trylock_original(&mtx->posix_lock)) == EBUSY)
      CPU_PAUSE();
    assert(ret == 0);
    return 0;
  }
  return EBUSY;
}

int adaptivemtx_unlock(void *entity) {
  adaptivemtx_lock_t *mtx = entity;
  int posix_ret = pthread_mutex_unlock_original(&mtx->posix_lock);
  int core_ret = pthread_mutex_unlock_original(&mtx->core);
  return posix_ret || core_ret;
}

int adaptivemtx_destroy(void *entity) {
  adaptivemtx_lock_t *mtx = entity;
  int posix_ret = pthread_mutex_destroy_original(&mtx->posix_lock);
  int core_ret = pthread_mutex_destroy_original(&mtx->core);
  free(mtx);
  return posix_ret || core_ret;
}

int adaptivemtx_cond_timedwait(pthread_cond_t *cond, void *entity, const struct timespec *time) {
  int res;
  adaptivemtx_lock_t *mtx = entity;
  pthread_mutex_unlock_original(&mtx->core);
  if (time)
    res = pthread_cond_timedwait_original(cond, &mtx->posix_lock, time);
  else
    res = pthread_cond_wait_original(cond, &mtx->posix_lock);
  if (res != 0 && res != ETIMEDOUT) {
    HANDLING_ERROR(
      "Error happens when trying to conduct "
      "pthread_cond_wait on internal posix_lock"
      "in a adaptivemtx_lock"
    );
  }
  int ret;
  if ((ret = pthread_mutex_unlock_original(&mtx->posix_lock)) != 0) {
    HANDLING_ERROR(
      "Error happens when trying to conduct "
      "pthread_cond_wait on internal posix_lock"
      "in a adaptivemtx_lock"
    );
  }
  pthread_mutex_lock_original(&mtx->core);
  pthread_mutex_lock_original(&mtx->posix_lock);
  return res;
}

#endif // __DYLINX_ADAPTIVEMTX_LOCK__
