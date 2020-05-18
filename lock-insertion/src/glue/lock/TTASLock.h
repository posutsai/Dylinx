#include "padding.h"
#include "utils.h"
#include <stdio.h>
#ifndef __DYLINX_TTAS_LOCK__
#define __DYLINX_TTAS_LOCK__

typedef struct ttas_lock {
  volatile uint8_t spin_lock __attribute__((aligned(L_CACHE_LINE_SIZE)));
  char __pad[pad_to_cache_line(sizeof(uint8_t))];
} ttas_lock_t __attribute__((aligned(L_CACHE_LINE_SIZE)));

// Paper:
// The Performance of Spin Lock Alternatives for Shared-Memory Multiprocessors
// ---------------------------------------------------------------------------
// Note:
// 1. The private attribute ttas_lock_t *impl will switch among three states
//    {UNLOCKED=1, LOCKED-1, 255}

int ttas_init(void *entity, pthread_mutexattr_t *attr) {
  printf("TTAS lock initialize !!!\n");
  return 1;
}

int ttas_lock(void *entity) {
  printf("TTAS lock enable !!!!\n");
  return 1;
}

int ttas_unlock(void *entity) {
  printf("TTAS lock disable !!!!\n");
  return 1;
}

int ttas_destroy(void *entity) {
  printf("TTAS lock destroy !!!!\n");
  return 1;
}
#endif
