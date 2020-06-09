#include "dylinx-glue.h"
// Include all of the lock definition in lock directory.
#include "lock/TTASLock.h"
// #include "lock/MutexLock.h"

#ifndef __DYLINX_GLUE__
#define __DYLINX_GLUE__
#define DYLINX_PTHREADMTX_ID 1
#pragma clang diagnostic ignored "-Waddress-of-packed-member"
int dylinx_lock_disable(void *lock) {
  generic_interface_t *gen_lock = lock;
  return gen_lock->methods->unlocker(&gen_lock->entity);
}

int dylinx_lock_destroy(void *lock) {
  generic_interface_t *gen_lock = lock;
  gen_lock->methods->destroyer(&gen_lock->entity);
  free(gen_lock->methods);
  return 1;
}

generic_interface_t *dylinx_genlock_forward(generic_interface_t *gen_lock) { return gen_lock; }

int dylinx_typeless_init(generic_interface_t *gen_lock, pthread_mutexattr_t *attr) {
  if (is_dylinx_defined(gen_lock)) {
    return gen_lock->methods->initializer(&gen_lock->entity, attr);
  }
  return pthread_mutex_init((pthread_mutex_t *)gen_lock, attr);
}

int dylinx_typeless_enable(generic_interface_t *gen_lock) {
  // gen_lock is the lock types defined in Dylinx including dylinx compatible
  // pthread-mutex and other customized lock.
  if (is_dylinx_defined(gen_lock)) {
    gen_lock->methods->locker(&gen_lock->entity);
  }
  // No issue since generic_interface_t and pthread_mutex_t both have the same
  // size.
  return pthread_mutex_lock((pthread_mutex_t *)gen_lock);
}

int dylinx_typeless_disable(generic_interface_t *gen_lock) {
  if (is_dylinx_defined(gen_lock))
    gen_lock->methods->unlocker(&gen_lock->entity);
  return pthread_mutex_unlock((pthread_mutex_t *)gen_lock);
}

int dylinx_typeless_destroy(generic_interface_t *gen_lock) {
  if (is_dylinx_defined(gen_lock))
    gen_lock->methods->destroyer(&gen_lock->entity);
  return pthread_mutex_destroy((pthread_mutex_t *)gen_lock);
}

generic_interface_t *native_pthreadmtx_forward(pthread_mutex_t *mtx) {
  return (generic_interface_t *)mtx;
}

// Since reinitialization is an undefined behavior, we just do nothing.
void dummy_func(pthread_mutex_t *mtx, size_t len) {};

DYLINX_INIT_LOCK(pthreadmtx, 1);
// If the passed variable is typeless, the lock behavior will automatically
// degenerate to native pthread_mutex_t;
void dylinx_degenerate_fill_array(generic_interface_t *mtx, size_t len) {
  dylinx_pthreadmtxlock_fill_array(mtx, len);
}

#endif // __DYLINX_GLUE__
