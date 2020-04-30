#include "lock/BaseLock.h"
#include "padding.h"
#include "utils.h"
#ifndef __DYLINX_TTAS_LOCK__
#define __DYLINX_TTAS_LOCK__
typedef struct ttas_mutex {
  volatile uint8_t spin_lock __attribute__((aligned(L_CACHE_LINE_SIZE)));
  char __pad[pad_to_cache_line(sizeof(uint8_t))];
} ttas_lock_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

// Paper:
// The Performance of Spin Lock Alternatives for Shared-Memory Multiprocessors
// ---------------------------------------------------------------------------
// Note:
// 1. The private attribute ttas_lock_t *impl will switch among three states
//    {UNLOCKED=1, LOCKED-1, 255}
class TTASLock: public BaseLock {
public:
  int init(const pthread_mutexattr_t *attr) {
    impl = (ttas_lock_t *)alloc_cache_align(sizeof(ttas_lock_t));
    impl->spin_lock = UNLOCKED;
    return 0;
  }
  int lock() {
    while (1) {
      while (impl->spin_lock != UNLOCKED)
        CPU_PAUSE();
      if (tas_uint8(&impl->spin_lock) == UNLOCKED)
        break;
    }
    return 0;
  }
  int unlock() {
    COMPILER_BARRIER();
    impl->spin_lock = LOCKED;
    return 0;
  }
  int destroy() {
    free(impl);
    impl = nullptr;
    return 0;
  }
private:
  ttas_lock_t *impl;
};
#endif
