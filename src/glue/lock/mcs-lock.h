#include "dylinx-utils.h"
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>
#ifndef __DYLINX_MCS_LOCK__
#define __DYLINX_MCS_LOCK__
typedef struct mcs_node {
  struct mcs_node *volatile next;
  volatile int spin  __attribute__((aligned(L_CACHE_LINE_SIZE)));
} mcs_node_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef struct mcs_lock {
  pthread_mutex_t posix_lock;
  pthread_key_t key;
  mcs_node_t *volatile tail __attribute__((aligned(L_CACHE_LINE_SIZE)));
} mcs_lock_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

int mcs_init(void **entity, pthread_mutexattr_t *attr) {
  *entity = (mcs_lock_t *)alloc_cache_align(sizeof(mcs_lock_t));
  mcs_lock_t *mtx = *entity;
  pthread_key_create(&mtx->key, NULL);
  mtx->tail = NULL;
  return pthread_mutex_init_original(&mtx->posix_lock, attr);
}

int __mcs_lock(mcs_lock_t *mtx) {
  mcs_node_t *node = (mcs_lock_t *)alloc_cache_align(sizeof(mcs_node_t));
  printf("mtx addr is %p\n", mtx);
  printf("locking node is %p\n", node);
  if (pthread_setspecific(mtx->key, node)) {
    printf("error msg: %s\n", strerror(errno));
    exit(0);
  }
  node->next = NULL;
  node->spin = LOCKED;
  mcs_node_t *tail = xchg_64((void *)&mtx->tail, (void *)node);
  if (!tail)
    return 0;
  tail->next = node;
  COMPILER_BARRIER();
  while (node->spin == LOCKED)
    CPU_PAUSE();
  printf("[tid = %ld] running head is %p\n", syscall(__NR_gettid), node);
  return 0;
}

int mcs_lock(void *entity) {
  mcs_lock_t *mtx = entity;
  int core_ret = __mcs_lock(entity);
  int posix_ret = pthread_mutex_lock_original(&mtx->posix_lock);
  return core_ret || posix_ret;
}

int mcs_trylock(void *entity) {
  mcs_lock_t *mtx = entity;
  mcs_node_t *node = (mcs_lock_t *)alloc_cache_align(sizeof(mcs_node_t));
  node->next = NULL;
  node->spin = LOCKED;
  pthread_setspecific(mtx->key, &node);
  __atomic_compare_exchange(&mtx->tail, NULL, node, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
  if (!mtx->tail) {
    int ret = 0;
    while ((ret = pthread_mutex_trylock_original(&mtx->posix_lock)) == EBUSY);
    assert(ret == 0);
    return 0;
  }
  return EBUSY;
}

int __mcs_unlock(mcs_lock_t *mtx) {
  printf("mtx addr is %p\n", mtx);
  mcs_node_t *node = (mcs_node_t *)pthread_getspecific(mtx->key);
  printf("unlocking node is %p\n", node);
  mcs_node_t *empty = NULL;
  if (!node->next) {
    if (__atomic_compare_exchange(&mtx->tail, &node, &empty, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
      return 0;
    while (!node->next)
      CPU_PAUSE();
  }
  node->next->spin = UNLOCKED;
  printf("[tid = %ld] perform unlock spinning node addr is %p\n", syscall(__NR_gettid), node);
  return 0;
}

int mcs_unlock(void *entity) {
  mcs_lock_t *mtx = entity;
  pthread_mutex_unlock_original(&mtx->posix_lock);
  return __mcs_unlock(mtx);
}

int mcs_destroy(void *entity) {
  mcs_lock_t *mtx = entity;
  pthread_mutex_destroy_original(&mtx->posix_lock);
  free(entity);
  return 0;
}

int mcs_cond_timedwait(pthread_cond_t *cond, void *entity, const struct timespec *time) {
  mcs_lock_t *mtx = entity;
  int res;
  __mcs_unlock(mtx);
  if (time)
    res = pthread_cond_timedwait_original(cond, &mtx->posix_lock, time);
  else
    res = pthread_cond_wait_original(cond, &mtx->posix_lock);
  if (res != 0 && res != ETIMEDOUT) {
    HANDLING_ERROR(
      "Error happens when trying to conduct "
      "pthread_cond_wait on internal posix_lock"
      "in a mcs_lock"
    );
  }
  int ret;
  if ((ret = pthread_mutex_unlock_original(&mtx->posix_lock)) != 0) {
    HANDLING_ERROR(
      "Error happens when trying to conduct "
      "pthread_cond_wait on internal posix_lock"
      "in a mcs_lock"
    );
  }
  mcs_lock(mtx);
  return res;
}

#endif // __DYLINX_MCS_LOCK__
