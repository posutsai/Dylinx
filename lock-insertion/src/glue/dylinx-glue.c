#include "dylinx-glue.h"
// Include all of the lock definition in lock directory.
#include "lock/ttas-lock.h"
#include "lock/backoff-lock.h"
#include <errno.h>
#include <string.h>

#ifndef __DYLINX_GLUE__
#define __DYLINX_GLUE__
#define DYLINX_PTHREADMTX_ID 1
#pragma clang diagnostic ignored "-Waddress-of-packed-member"

#define COUNT_DOWN()                                                        \
  11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1

#define AVAILABLE_LOCK_TYPE_NUM(...)                                        \
  GET_MACRO(__VA_ARGS__)

#if AVAILABLE_LOCK_TYPE_NUM(ALLOWED_LOCK_TYPE, COUNT_DOWN()) > LOCK_TYPE_CNT
#error                                                                      \
Current number of available lock types isn't enough. Please reset           \
LOCK_TYPE_CNT macro and corresponding macro definition.
#endif

int dylinx_lock_disable(void *lock) {
  generic_interface_t *gen_lock = lock;
  pthread_mutex_unlock(gen_lock->cv_mtx);
  return gen_lock->methods->unlocker(&gen_lock->entity);
}

int dylinx_lock_destroy(void *lock) {
  generic_interface_t *gen_lock = lock;
  int ret = gen_lock->methods->destroyer(&gen_lock->entity);
  printf("return output of destroyer is %d\n", ret);
  pthread_mutex_destroy(gen_lock->cv_mtx);
#ifdef __DYLINX_DEBUG__
  if (ret)
      perror(strerror(ret));
#endif
  free(gen_lock->methods);
  return 1;
}

int dylinx_lock_condwait(pthread_cond_t *cond, void *lock) {
  generic_interface_t *gen_lock = lock;
  gen_lock->methods->unlocker(&gen_lock->entity);
  return pthread_cond_wait(cond, gen_lock->cv_mtx);
}

generic_interface_t *dylinx_genlock_forward(generic_interface_t *gen_lock) { return gen_lock; }

int dylinx_typeless_init(generic_interface_t *gen_lock, pthread_mutexattr_t *attr) {
  if (is_dylinx_defined(gen_lock)) {
    pthread_mutex_init(gen_lock->cv_mtx, NULL);
    return gen_lock->methods->initializer(&gen_lock->entity, attr);
  }
  return pthread_mutex_init((pthread_mutex_t *)gen_lock, attr);
}

int dylinx_typeless_enable(generic_interface_t *gen_lock) {
  // gen_lock is the lock types defined in Dylinx including dylinx compatible
  // pthread-mutex and other customized lock.
  if (is_dylinx_defined(gen_lock)) {
    gen_lock->methods->locker(&gen_lock->entity);
    pthread_mutex_init(gen_lock->cv_mtx, NULL);
  }
  // No issue since generic_interface_t and pthread_mutex_t both have the same
  // size.
  return pthread_mutex_lock((pthread_mutex_t *)gen_lock);
}

generic_interface_t *native_pthreadmtx_forward(pthread_mutex_t *mtx) {
  return (generic_interface_t *)mtx;
}

// Since reinitialization is an undefined behavior, we just do nothing.
void dummy_func(pthread_mutex_t *mtx, size_t len) {};

DYLINX_EXTERIOR_WRAPPER_IMPLE(pthreadmtx, 1);
// If the passed variable is typeless, the lock behavior will automatically
// degenerate to native pthread_mutex_t;
void dylinx_degenerate_fill_array(generic_interface_t *mtx, size_t len) {
  dylinx_pthreadmtxlock_fill_array(mtx, len);
}

#endif // __DYLINX_GLUE__
