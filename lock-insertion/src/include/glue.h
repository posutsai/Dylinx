#include <cstdint>
#include <pthread.h>
#include <cstdio>
#include <typeinfo>

// TODO
// 1. Determine whether should include pthread_mutex as a derived class from
//    BaseLock.

#ifndef __GLUE__
#define __GLUE__

#define BASELOCK_WARNING                                                  \
  do {                                                                    \
    fprintf(stderr, "Do not use BaseLock!!!\n");                          \
    return -1;                                                            \
  } while(0)

class BaseLock {
public:
  BaseLock() {}
  BaseLock(pthread_mutex_t *l) {
    printf("pthread_mutex_lock is converted to baselock!!!\n");
  }
  int init() { BASELOCK_WARNING; }
  int lock() { BASELOCK_WARNING; }
  int unlock() { BASELOCK_WARNING; }
  int destroy() { BASELOCK_WARNING; }
};

class TTASLock: public BaseLock {
public:
  int init() {
    printf("TTASLock instance is initialized!!!!\n");
    return 0;
  }
  int lock() { return 0; }
  int unlock() { return 0; }
  int destroy() { return 0; }
};

int ___dylinx_slot_init_(pthread_mutex_t *lock, const pthread_mutexattr_t *attr) { return pthread_mutex_init(lock, attr); }
int ___dylinx_slot_lock_(pthread_mutex_t *lock) { return pthread_mutex_lock(lock); }
int ___dylinx_slot_unlock_(pthread_mutex_t *lock) { return pthread_mutex_unlock(lock); }
int ___dylinx_slot_destroy_(pthread_mutex_t *lock) { return pthread_mutex_destroy(lock); }

int ___dylinx_slot_init_(BaseLock *lock, const pthread_mutexattr_t *attr) { return lock->init(); }
int ___dylinx_slot_lock_(BaseLock *lock) { return lock->lock(); }
int ___dylinx_slot_unlock_(BaseLock *lock) { return lock->unlock(); }
int ___dylinx_slot_destroy_(BaseLock *lock) { return lock->destroy(); }

#endif
