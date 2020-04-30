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

#define PROB_SELECTION_RESOLUTION 10000

void __dylinx_init__() {
  srand((uint32_t) time(0));
}

template <typename T>
BaseLock *create_lock() {
  return new T();
}

typedef BaseLock *(*create_fn)();
create_fn creators[LOCKTYPE_CNT] = {
  // &create_lock<MutexLock>,
  &create_lock<TTASLock>,
  &create_lock<TTASLock>,
  &create_lock<TTASLock>
};
enum class LockType {
  TTAS,
  Mutex
};
class AbstractLock {
public:
  AbstractLock(const float *prob_dist=NULL) {
    uint32_t divider[LOCKTYPE_CNT];
    sp_lock = nullptr;
    float prob_sum = 0.;
    for (uint32_t l = 0; l < LOCKTYPE_CNT; l++) {
      prob_sum += prob_dist[l];
      divider[l] = PROB_SELECTION_RESOLUTION * prob_sum;
    }
    uint32_t s = rand() % PROB_SELECTION_RESOLUTION;
    for (uint32_t t = 0;; t++) {
      if (t == LOCKTYPE_CNT - 1)
        break;
      if (s <= divider[t])
        sp_lock = creators[t]();
    }
  }
  int forward_init(const pthread_mutexattr_t *attr) {
    return sp_lock->init(attr);
  }
  int forward_lock() {
    return sp_lock->lock();
  }
  int forward_unlock() {
    return sp_lock->unlock();
  }
  int forward_destroy() {
    return sp_lock->destroy();
  }
  ~AbstractLock() {
    delete sp_lock;
  }
private:
  BaseLock *sp_lock;
};


// class MutexLock: public BaseLock {
// public:
//   int init(const pthread_mutexattr_t *attr) {
//     printf("MutexLock instance is initialized!!!!\n");
//     return pthread_mutex_init(&mutex, attr);
//   }
//   int lock() { return pthread_mutex_lock(&mutex); }
//   int unlock() { return ptread_mutex_unlock(&mutex); }
//   int destroy() { return pthread_mutex_destroy(&mutex); }
// private:
//   pthread_mutex_t mutex;
// };

int __dylinx_slot_init__(AbstractLock *lock, const pthread_mutexattr_t *attr) { return lock->forward_init(attr); }
int __dylinx_slot_lock__(AbstractLock *lock) { return lock->forward_lock(); }
int __dylinx_slot_unlock__(AbstractLock *lock) { return lock->forward_unlock(); }
int __dylinx_slot_destroy__(AbstractLock *lock) { return lock->forward_destroy(); }

#endif
