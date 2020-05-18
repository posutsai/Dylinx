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

typedef int (*initializer_fn)(void *, pthread_mutexattr_t *);
typedef int (*locker_fn)(void *);
typedef int (*unlocker_fn)(void *);
typedef int (*destroyer_fn)(void *);

typedef struct GenericInterface {
  ttas_lock_t *entity;
  initializer_fn lock_init;
  locker_fn lock_enable;
  unlocker_fn lock_disable;
  destroyer_fn lock_destroy;
  char padding[sizeof(pthread_mutex_t) - sizeof(initializer_fn) * 4];
  uint32_t type;
} generic_interface_t;

typedef union DylinxTTASLock {
  pthread_mutex_t dummy_lock;
  generic_interface_t interface;
} dylinx_ttaslock_t;

int dylinx_ttaslock_init(dylinx_ttaslock_t *lock, pthread_mutexattr_t *attr) {
  memset(lock, 0, sizeof(generic_interface_t));
  lock->interface.lock_init = ttas_init;
  lock->interface.lock_enable = ttas_lock;
  lock->interface.lock_disable = ttas_unlock;
  lock->interface.lock_destroy = ttas_destroy;
  lock->interface.type = 1;
  return lock->interface.lock_init((void *)lock->interface.entity, attr);
}

int dylinx_ttaslock_enable(dylinx_ttaslock_t *lock) {
  if (!lock->interface.type)
    dylinx_ttaslock_init(lock, NULL);
  return lock->interface.lock_enable((void *)lock->interface.entity);
}

int dylinx_lock_disable(void *lock) {
  generic_interface_t *gen_lock = lock;
  return gen_lock->lock_disable((void *)gen_lock->entity);
}

int dylinx_lock_destroy(void *lock) {
  generic_interface_t *gen_lock = lock;
  return gen_lock->lock_destroy((void *)gen_lock->entity);
}

#define __dylinx_generic_init_(entity, attr) _Generic((entity),             \
  pthread_mutex_t *: pthread_mutex_init,                                    \
  dylinx_ttaslock_t *: dylinx_ttaslock_init                                 \
)(entity, attr)

#define __dylinx_generic_enable_(entity) _Generic((entity),                 \
  pthread_mutex_t *: pthread_mutex_lock,                                    \
  dylinx_ttaslock_t *: dylinx_ttaslock_enable                               \
)(entity)

#define __dylinx_generic_disable_(entity) _Generic((entity),                \
  pthread_mutex_t: pthread_mutex_unlock,                                    \
  default: dylinx_lock_disable                                              \
)(entity)

#define __dylinx_generic_destroy_(entity) _Generic((entity),                \
  pthread_mutex_t: pthread_mutex_destroy,                                   \
  default: dylinx_lock_destroy                                              \
)(entity)

#endif // __DYLINX_GLUE__
