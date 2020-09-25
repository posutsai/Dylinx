#include "dylinx-padding.h"
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#ifndef __DYLINX_TOPOLOGY__
#define __DYLINX_TOPOLOGY__
#pragma clang diagnostic ignored "-Waddress-of-packed-member"

#define L_CACHE_LINE_SIZE 64
#define LOCKED 0
#define UNLOCKED 1
#define CPU_PAUSE() __asm__ __volatile__("pause\n" : : : "memory")
#define HANDLING_ERROR(msg)                                                   \
  do {                                                                        \
    printf(                                                                   \
      "=============== [%30s] ===============\n"                              \
      "%s\n"                                                                  \
      "==============================================================\n",     \
      __FUNCTION__, msg                                                       \
    );                                                                        \
    assert(0);                                                                \
  } while(0)

extern inline void *alloc_cache_align(size_t n) {
  void *res = 0;
  if ((MEMALIGN(&res, L_CACHE_LINE_SIZE, cache_align(n)) < 0) || !res) {
    fprintf(stderr, "MEMALIGN(%llu, %llu)", (unsigned long long)n,
        (unsigned long long)cache_align(n));
    exit(-1);
  }
  return res;
}

// Refer to libstock implementation. The key is to make whole operation
// atomic. Test-and-set operation will try to assign 0b1 to certain memory
// address and return the old value. The implementation here requires
// some note to understand clearly. At the first glimpse, variable oldvar
// is not initialized. However, in the input operand list, "0" means
// (unsigned char) 0xff and oldvar refer to the same memory buffer. oldvar
// is assigned while designating (unsigned char) 0xff.
static inline uint8_t l_tas_uint8(volatile uint8_t *addr) {
  uint8_t oldval;
  __asm__ __volatile__("xchgb %0,%1"
      : "=q"(oldval), "=m"(*addr)
      : "0"((unsigned char)0xff), "m"(*addr)
      : "memory");
  return (uint8_t)oldval;
}

#define COMPILER_BARRIER() __asm__ __volatile__("" : : : "memory")

// Serve every dispatched initialization function call, including
// normal variable, array and pointer with malloc-like function
// call.
#define DLX_LOCK_TEMPLATE_IMPLEMENT(ltype)                                                                    \
int dlx_ ## ltype ##_var_init(                                                                                \
  dlx_ ## ltype ## _t *lock,                                                                                  \
  pthread_mutexattr_t *attr,                                                                                  \
  char *var_name, char *file, int line                                                                        \
  ) {                                                                                                         \
  dlx_generic_lock_t *gen_lock = (dlx_generic_lock_t *)lock;                                                  \
  gen_lock->methods = calloc(1, sizeof(dlx_injected_interface_t));                                            \
  gen_lock->methods->init_fptr = ltype ## _init;                                                               \
  gen_lock->methods->lock_fptr = ltype ## _lock;                                                               \
  gen_lock->methods->trylock_fptr = ltype ## _trylock;                                                         \
  gen_lock->methods->unlock_fptr = ltype ## _unlock;                                                           \
  gen_lock->methods->destroy_fptr = ltype ## _destroy;                                                         \
  gen_lock->methods->cond_timedwait_fptr = ltype ## _cond_timedwait;                                           \
  gen_lock->check_code = 0x32CB00B5;                                                                  \
  if (!gen_lock->methods || gen_lock->methods->init_fptr(&gen_lock->lock_obj, attr)) {                         \
    printf("Error happens while initializing lock variable %s in %s L%4d\n", var_name, file, line);           \
    return -1;                                                                                                \
  }                                                                                                           \
  return 0;                                                                                                   \
}                                                                                                             \
                                                                                                              \
int dlx_ ## ltype ## _arr_init(                                                                               \
  dlx_ ## ltype ## _t *head,                                                                                  \
  uint32_t len,                                                                                               \
  char *var_name, char *file, int line                                                                        \
  ) {                                                                                                         \
  for (int i = 0; i < len; i++) {                                                                             \
    dlx_generic_lock_t *gen_lock = (dlx_generic_lock_t *)head + i;                                            \
    gen_lock->methods = calloc(1, sizeof(dlx_injected_interface_t));                                          \
    gen_lock->methods->init_fptr = ltype ## _init;                                                             \
    gen_lock->methods->lock_fptr = ltype ## _lock;                                                             \
    gen_lock->methods->trylock_fptr = ltype ## _trylock;                                                       \
    gen_lock->methods->unlock_fptr = ltype ## _unlock;                                                         \
    gen_lock->methods->destroy_fptr = ltype ## _destroy;                                                       \
    gen_lock->methods->cond_timedwait_fptr = ltype ## _cond_timedwait;                                         \
    gen_lock->check_code = 0x32CB00B5;                                                                \
    if (!gen_lock->methods || gen_lock->methods->init_fptr(&gen_lock->lock_obj, NULL)) {                       \
      printf("Error happens while initializing lock array %s in %s L%4d\n", var_name, file, line);            \
      return -1;                                                                                              \
    }                                                                                                         \
  }                                                                                                           \
  return 0;                                                                                                   \
}                                                                                                             \
                                                                                                              \
void *dlx_ ## ltype ## _obj_init(                                                                             \
  uint32_t cnt,                                                                                               \
  uint32_t unit,                                                                                              \
  uint32_t *offsets,                                                                                          \
  uint32_t n_offset,                                                                                          \
  char *file,                                                                                                 \
  int line                                                                                                    \
  ) {                                                                                                         \
  char *object = calloc(cnt, unit);\
  if (object) {                                                                     \
    for (uint32_t c = 0; c < cnt; c++) {                                                                      \
      for (uint32_t n = 0; n < n_offset; n++) {                                                               \
        dlx_generic_lock_t *lock = object + c * unit + offsets[n];                                            \
        lock->methods = calloc(1, sizeof(dlx_injected_interface_t));                                          \
        lock->methods->init_fptr = ltype ## _init;                                                             \
        lock->methods->lock_fptr = ltype ## _lock;                                                             \
        lock->methods->trylock_fptr = ltype ## _trylock;                                                       \
        lock->methods->unlock_fptr = ltype ## _unlock;                                                         \
        lock->methods->destroy_fptr = ltype ## _destroy;                                                       \
        lock->methods->cond_timedwait_fptr = ltype ## _cond_timedwait;                                         \
        lock->check_code = 0x32CB00B5;                                                                \
        if ((!lock->methods || lock->methods->init_fptr(&lock->lock_obj, NULL))) {                         \
          printf("Error happens while initializing memory allocated object in %s L%4d\n", file, line);        \
          return NULL;                                                                                        \
        }                                                                                                     \
      }                                                                                                       \
    }                                                                                                         \
    return object;                                                                                            \
  }                                                                                                           \
  return NULL;                                                                                                \
}

#endif
