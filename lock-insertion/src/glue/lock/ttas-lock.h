#include "dylinx-padding.h"
#include "dylinx-utils.h"
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

int ttas_init(void **entity, pthread_mutexattr_t *attr) {
#ifdef __DYLINX_DEBUG__
  printf("ttas-lock is initialized !!!\n");
#endif
  ttas_lock_t *mtx = *entity;
  mtx = (ttas_lock_t *)alloc_cache_align(sizeof(ttas_lock_t));
  mtx->spin_lock = UNLOCKED;
  return 0;
}

int ttas_lock(void **entity) {
  ttas_lock_t *mtx = *entity;
  while (1) {
    while (mtx->spin_lock != UNLOCKED)
      CPU_PAUSE();
    if (l_tas_uint8(&mtx->spin_lock) == UNLOCKED)
      break;
  }
  return 0;
}

int ttas_unlock(void **entity) {
  COMPILER_BARRIER();
  ttas_lock_t *mtx = *entity;
  mtx->spin_lock = UNLOCKED;
  return 1;
}

int ttas_destroy(void **entity) {
  ttas_lock_t *mtx = *entity;
  free(mtx);
  return 1;
}

DYLINX_EXTERIOR_WRAPPER_IMPLE(ttas, 2);

#endif
