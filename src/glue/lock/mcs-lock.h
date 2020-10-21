#include "dylinx-utils.h"
#ifndef __DYLINX_MCS_LOCK__
#define __DYLINX_MCS_LOCK__
typedef struct mcs_node {
  struct mcs_node *volatile next;
  volatile int spin  __attribute__((aligned(L_CACHE_LINE_SIZE)));
} mcs_node_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

typedef struct {
  mcs_node_t *volatile tail __attribute__((aligned(L_CACHE_LINE_SIZE)));
  mcs_node_t *volatile head __attribute__((aligned(L_CACHE_LINE_SIZE)));
} ht_pair_t;

typedef struct mcs_lock {
  pthread_mutex_t posix_lock;
  ht_pair_t ht;
} mcs_lock_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

int mcs_init(void **entity, pthread_mutexattr_t *attr) {
  *entity = (mcs_lock_t *)alloc_cache_align(sizeof(mcs_lock_t));
  mcs_lock_t *mtx = *entity;
  mtx->ht.tail = NULL;
  return pthread_mutex_init_original(&mtx->posix_lock, attr);
}

int __mcs_lock(mcs_lock_t *mtx) {
  mcs_node_t *node = (mcs_lock_t *)alloc_cache_align(sizeof(mcs_node_t));
  node->next = NULL;
  node->spin = LOCKED;
  mcs_node_t *prev_tail = xchg_64((void *)&mtx->ht.tail, (void *)node);
  if (!prev_tail) {
    mtx->ht.head = node;
    return 0;
  }
  prev_tail->next = node;
  COMPILER_BARRIER();
  // while (__sync_val_compare_and_swap(&node->is_locked, UNLOCKED, LOCKED) == LOCKED)
  //   CPU_PAUSE();
  while (node->spin == LOCKED)
    CPU_PAUSE();
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
  mcs_node_t *prev_tail;
  mcs_node_t *node = (mcs_lock_t *)alloc_cache_align(sizeof(mcs_node_t));
  node->next = NULL;
  node->spin = LOCKED;
  mcs_node_t empty = {NULL, NULL};
  __atomic_exchange(&mtx->ht, &empty, prev_tail, __ATOMIC_SEQ_CST);
  if (!mtx->ht.tail) {
    int ret = 0;
    while ((ret = pthread_mutex_trylock_original(&mtx->posix_lock)) == EBUSY);
    assert(ret == 0);
  }
  return EBUSY;
}

int __mcs_unlock(mcs_lock_t *mtx) {
  mcs_node_t *running = mtx->ht.head;
  // No visible successor but possible to wait for
  // future successor.
  if (!running->next) {
    ht_pair_t ref = {running, running};
    ht_pair_t empty = {NULL, NULL};
    if (__atomic_compare_exchange(&mtx->ht, &ref, &empty, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
      return 0;
    while (!running->next)
      CPU_PAUSE();
  }
  mtx->ht.head = running->next;
  running->next->spin = UNLOCKED;
  free(running);
  return 0;
}

int mcs_unlock(void *entity) {
  mcs_lock_t *mtx = entity;
  pthread_mutex_unlock_original(&mtx->posix_lock);
  __mcs_lock(mtx);
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
