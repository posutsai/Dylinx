#include <cstdint>
#include <pthread.h>
#include <cstdio>
#include <typeinfo>
#include <assert.h>

#include "runtime-config.h"
// Include all of the lock definition in lock directory.
#include "lock/TTASLock.h"
// #include "lock/MutexLock.h"

#ifndef __DYLINX_GLUE__
#define __DYLINX_GLUE__

#ifndef LOCKTYPE_CNT
static_assert(0, "LOCKTYPE_CNT is one of required macro");
#endif


enum lock_type {
  Ttas, Mutex, Ttas_ls, Spinlock
};

typedef struct AbstractLock {
  enum lock_type;
  int32_t id;
  // the value here can only be assigned in initializer and it
  // is NULL while contructing.
  void *block;
} AbstractLock_t;

typedef int (*initializer_fn)(void *, pthread_mutexattr_t *);
static initializer_fn initializers[] {
  ttas_init,
  mutex_init,
  ttas_ls_init,
  spinlock_init
};

typedef int (*locker_fn)(void *, int);
static locker_fn lockers[] {
  ttas_lock,
  mutex_lock,
  ttas_ls_lock,
  spinlock_lock
};

typedef int (*unlocker_fn)(void *, int);
static locker_fn unlockers[] {
  ttas_unlock,
  mutex_unlock,
  ttas_ls_unlock,
  spinlock_unlock
};

typedef int (*destroyer_fn)(void *);
static destroyer_fn destroyers[] {
  ttas_destroy,
  mutex_destroy,
  ttas_ls_destroy,
  spinlock_destroy
};

int __dylinx_slot_init(AbstractLock_t *ablock, const pthread_mutexattr_t *attr) {
  return initializers[ablock->lock_type](ablock->block, attr);
}
int __dylinx_slot_lock(AbstractLock_t *ablock, int lock_id) {
  return lockers[ablock->lock_type](ablock->block, lock_id);
}
int __dylinx_slot_unlock(AbstractLock *ablock, int lock_id) {
  return unlockers[ablock->lock_type](ablock->block, lock_id);
}
int __dylinx_slot_destroy(AbstractLock *ablock) {
  return destroyers[ablock->lock_type](ablock->block);
}

#endif
