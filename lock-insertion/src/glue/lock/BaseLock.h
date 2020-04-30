#include <assert.h>
#ifndef __DYLINX_BASE_LOCK__
#define __DYLINX_BASE_LOCK__
#define BASELOCK_WARNING                                                  \
  do {                                                                    \
    assert(0 && "Do not use the method in BaseLock!!!\n");                \
    return -1;                                                            \
  } while(0)

class BaseLock {
public:
  BaseLock() {};
  ~BaseLock() {};
  int init(const pthread_mutexattr_t *attr) { BASELOCK_WARNING; }
  int lock() { BASELOCK_WARNING; }
  int unlock() { BASELOCK_WARNING; }
  int destroy() { BASELOCK_WARNING; }
};
#endif
