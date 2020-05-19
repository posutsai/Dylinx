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
int dylinx_lock_disable(void *lock) {
  generic_interface_t *gen_lock = lock;
  return gen_lock->lock_disable(gen_lock->entity);
}

int dylinx_lock_destroy(void *lock) {
  generic_interface_t *gen_lock = lock;
  return gen_lock->lock_destroy(gen_lock->entity);
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
  pthread_mutex_t *: pthread_mutex_unlock,                                  \
  default: dylinx_lock_disable                                              \
)(entity)

#define __dylinx_generic_destroy_(entity) _Generic((entity),                \
  pthread_mutex_t *: pthread_mutex_destroy,                                 \
  default: dylinx_lock_destroy                                              \
)(entity)

#endif // __DYLINX_GLUE__
