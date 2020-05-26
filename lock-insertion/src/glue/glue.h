#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <assert.h>

// #include "runtime-config.h"
// Include all of the lock definition in lock directory.
#include "lock/TTASLock.h"
// #include "lock/MutexLock.h"

#ifndef __DYLINX_GLUE__
#define __DYLINX_GLUE__
#define DYLINX_PTHREADMTX_ID 1
int dylinx_lock_disable(void *lock) {
  generic_interface_t *gen_lock = lock;
  return gen_lock->methods->unlocker(gen_lock->entity);
}

int dylinx_lock_destroy(void *lock) {
  generic_interface_t *gen_lock = lock;
  gen_lock->methods->destroyer(gen_lock->entity);
  free(gen_lock->methods);
  return 1;
}

generic_interface_t *dylinx_genlock_forward(generic_interface_t *gen_lock) { return gen_lock; }

int dylinx_typeless_init(generic_interface_t *gen_lock, pthread_mutexattr_t *attr) {
  if (is_dylinx_defined(gen_lock)) {
    return gen_lock->methods->initializer(gen_lock->entity, attr);
  }
  return pthread_mutex_init((pthread_mutex_t *)gen_lock, attr);
}

int dylinx_typeless_enable(generic_interface_t *gen_lock) {
  // gen_lock is the lock types defined in Dylinx including dylinx compatible
  // pthread-mutex and other customized lock.
  if (is_dylinx_defined(gen_lock)) {
    gen_lock->methods->locker(gen_lock->entity);
  }
  // No issue since generic_interface_t and pthread_mutex_t both have the same
  // size.
  return pthread_mutex_lock((pthread_mutex_t *)gen_lock);
}

int dylinx_typeless_disable(generic_interface_t *gen_lock) {
  if (is_dylinx_defined(gen_lock))
    gen_lock->methods->unlocker(gen_lock->entity);
  return pthread_mutex_unlock((pthread_mutex_t *)gen_lock);
}

int dylinx_typeless_destroy(generic_interface_t *gen_lock) {
  if (is_dylinx_defined(gen_lock))
    gen_lock->methods->destroyer(gen_lock->entity);
  return pthread_mutex_destroy((pthread_mutex_t *)gen_lock);
}

generic_interface_t *native_pthreadmtx_forward(pthread_mutex_t *mtx) {
  return (generic_interface_t *)mtx;
}

DYLINX_INIT_LOCK(pthreadmtx, 1);

// dylinx_genlock_forward is only apply when the lock instance is passed nestedly
// in interior scope.
#define __dylinx_generic_cast_(entity) _Generic((entity),                   \
  pthread_mutex_t *: native_pthreadmtx_forward,                             \
  dylinx_pthreadmtxlock_t *: dylinx_pthreadmtxlock_cast,                    \
  dylinx_ttaslock_t *: dylinx_ttaslock_cast,                                \
  default: dylinx_genlock_forward                                           \
)(entity)

#define __dylinx_generic_init_(entity, attr) _Generic((entity),             \
  pthread_mutex_t *: pthread_mutex_init,                                    \
  dylinx_pthreadmtxlock_t *: dylinx_pthreadmtxlock_init,                    \
  dylinx_ttaslock_t *: dylinx_ttaslock_init,                                \
  generic_interface_t *: dylinx_typeless_init                               \
)(entity, attr)

#define __dylinx_generic_enable_(entity) _Generic((entity),                 \
  pthread_mutex_t *: pthread_mutex_lock,                                    \
  dylinx_pthreadmtx_t *: dylinx_pthreadmtxlock_enable,                      \
  dylinx_ttaslock_t *: dylinx_ttaslock_enable,                              \
  generic_interface_t *: dylinx_typeless_enable                             \
)(entity)

#define __dylinx_generic_disable_(entity) _Generic((entity),                \
  pthread_mutex_t *: pthread_mutex_unlock,                                  \
  default: dylinx_lock_disable                                              \
)(entity)

#define __dylinx_generic_destroy_(entity) _Generic((entity),                \
  pthread_mutex_t *: pthread_mutex_destroy,                                 \
  default: dylinx_lock_destroy                                              \
)(entity)

#define FILL_ARRAY(type, head, len)                                         \
  do {                                                                      \
    dylinx_ ## type ## lock_fill_array(head, len);                          \
  } while(0)

#endif // __DYLINX_GLUE__
