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

struct Methods4Lock {
  void *initializer;
  void *locker;
  void *unlocker;
  void *destroyer;
};

#define DYLINX_PTHREADMTX_ID 0
int indirect_pthread_mutex_init(void *mtx, pthread_mutexattr_t *attr) {
  return pthread_mutex_init((pthread_mutex_t *)mtx, NULL);
}

int indirect_pthread_mutex_lock(void *mtx) {
  return pthread_mutex_lock((pthread_mutex_t *)mtx);
}

int indirect_pthread_mutex_unlock(void *mtx) {
  return pthread_mutex_unlock((pthread_mutex_t *)mtx);
}

int indirect_pthread_mutex_destroy(void *mtx) {
  return pthread_mutex_destroy((pthread_mutex_t *)mtx);
}


static struct Methods4Lock id2methods_table[LOCK_TYPE_CNT] = {
  {
    indirect_pthread_mutex_init,
    indirect_pthread_mutex_lock,
    indirect_pthread_mutex_unlock,
    indirect_pthread_mutex_destroy
  }
};

int dylinx_retrievetype_init(entity_with_type_t ewt, pthread_mutexattr_t *attr) {
  struct Methods4Lock m;
  memcpy(&m, &id2methods_table[ewt.mtx_type], sizeof(struct Methods4Lock));
  generic_interface_t *interface = (generic_interface_t *)ewt.entity;
  interface->lock_init = m.initializer;
  interface->lock_enable = m.locker;
  interface->lock_disable = m.unlocker;
  interface->lock_destroy = m.destroyer;
  if (ewt.mtx_type != DYLINX_PTHREADMTX_ID)
    interface->isnt_pthread = 1;
  return interface->lock_init(ewt.entity, attr);
}

int dylinx_retrievetype_enable(entity_with_type_t ewt) {
  generic_interface_t *interface = (generic_interface_t *)ewt.entity;
  if (!interface->lock_init)
    dylinx_retrievetype_init(ewt, NULL);
  return interface->lock_enable(interface->entity);
}

int dylinx_retrievetype_disable(entity_with_type_t ewt) {
  generic_interface_t *interface = (generic_interface_t *)ewt.entity;
  return interface->lock_disable(interface->entity);
}

int dylinx_retrievetype_destroy(entity_with_type_t ewt) {
  generic_interface_t *interface = (generic_interface_t *)ewt.entity;
  return interface->lock_destroy(interface->entity);
}

entity_with_type_t dylinx_pthreadmtx_gettype(pthread_mutex_t *mtx) {
  entity_with_type_t l = {
    .mtx_type = DYLINX_PTHREADMTX_ID,
    .entity = (void *)mtx
  };
  return l;
}

int dylinx_pthreadmtx_init(pthread_mutex_t *lock, pthread_mutexattr_t *attr) {
  generic_interface_t *gen_lock = (generic_interface_t *)lock;
  memset(lock, 0, sizeof(generic_interface_t));
  gen_lock->lock_init = (initializer_fn)pthread_mutex_init;
  gen_lock->lock_enable = (locker_fn)pthread_mutex_lock;
  gen_lock->lock_disable = (unlocker_fn)pthread_mutex_unlock;
  gen_lock->lock_destroy = (destroyer_fn)pthread_mutex_destroy;
  gen_lock->isnt_pthread = 0;
  return pthread_mutex_init((pthread_mutex_t *)gen_lock->entity, attr);
}

int dylinx_pthreadmtx_enable(pthread_mutex_t *lock) {
  return pthread_mutex_lock(lock);
}

#define __dylinx_generic_get_type_(entity) _Generic((entity),               \
  pthread_mutex_t *: dylinx_pthreadmtx_gettype,                             \
  dylinx_ttaslock_t *: dylinx_ttaslock_gettype                              \
)(entity)

#define __dylinx_generic_init_(entity, attr) _Generic((entity),             \
  pthread_mutex_t *: dylinx_pthreadmtx_init,                                \
  dylinx_ttaslock_t *: dylinx_ttaslock_init,                                \
  entity_with_type_t: dylinx_retrievetype_init                              \
)(entity, attr)

#define __dylinx_generic_enable_(entity) _Generic((entity),                 \
  pthread_mutex_t *: dylinx_pthreadmtx_enable,                              \
  dylinx_ttaslock_t *: dylinx_ttaslock_enable                               \
  entity_with_type_t: dylinx_retrievetype_enable                            \
)(entity)

#define __dylinx_generic_disable_(entity) _Generic((entity),                \
  entity_with_type_t: pthread_mutex_unlock,                                  \
  default: dylinx_lock_disable                                              \
)(entity)

#define __dylinx_generic_destroy_(entity) _Generic((entity),                \
  entity_with_type_t: pthread_mutex_destroy,                                 \
  default: dylinx_lock_destroy                                              \
)(entity)

#endif // __DYLINX_GLUE__
