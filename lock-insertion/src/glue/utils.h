#include "padding.h"
#ifndef __DYLINX_TOPOLOGY__
#define __DYLINX_TOPOLOGY__

#define L_CACHE_LINE_SIZE 64
#define LOCKED 0
#define UNLOCKED 1
#define CPU_PAUSE() asm volatile("pause\n" : : : "memory")

typedef int (*initializer_fn)(void *, pthread_mutexattr_t *);
typedef int (*locker_fn)(void *);
typedef int (*unlocker_fn)(void *);
typedef int (*destroyer_fn)(void *);

inline void *alloc_cache_align(size_t n) {
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
static inline uint8_t tas_uint8(volatile uint8_t *addr) {
  uint8_t oldval;
  __asm__ __volatile__("xchgb %0,%1"
      : "=q"(oldval), "=m"(*addr)
      : "0"((unsigned char)0xff), "m"(*addr)
      : "memory");
  return (uint8_t)oldval;
}

typedef struct GenericInterface {
  char padding[sizeof(pthread_mutex_t)];
  void *entity;
  initializer_fn lock_init;
  locker_fn lock_enable;
  unlocker_fn lock_disable;
  destroyer_fn lock_destroy;
  uint32_t isnt_pthread;
} generic_interface_t;

typedef struct EntityWithType {
  uint32_t mtx_type;
  void *entity;
} entity_with_type_t;

#define COMPILER_BARRIER() asm volatile("" : : : "memory")
#define DYLINX_INIT_LOCK(ltype, num)                                                                           \
typedef union {                                                                                               \
  pthread_mutex_t dummy_lock;                                                                                 \
  generic_interface_t interface;                                                                              \
} dylinx_ ## ltype ## lock_t;                                                                                  \
                                                                                                              \
int dylinx_ ## ltype ## lock_init(dylinx_ ## ltype ## lock_t *lock, pthread_mutexattr_t *attr) {                \
  memset(lock, 0, sizeof(generic_interface_t));                                                               \
  lock->interface.lock_init = ltype ## _init;                                                                  \
  lock->interface.lock_enable = ltype ## _lock;                                                                \
  lock->interface.lock_disable = ltype ## _unlock;                                                             \
  lock->interface.lock_destroy = ltype ## _destroy;                                                            \
  lock->interface.isnt_pthread = 1;                                                                           \
  return lock->interface.lock_init(lock->interface.entity, attr);                                             \
}                                                                                                             \
                                                                                                              \
int dylinx_ ## ltype ## lock_enable(dylinx_ ## ltype ## lock_t *lock) {                                         \
  if (!lock->interface.isnt_pthread)                                                                          \
    dylinx_ ## ltype ## lock_init(lock, NULL);                                                                 \
  return lock->interface.lock_enable(lock->interface.entity);                                                 \
}                                                                                                             \
                                                                                                              \
static dylinx_## ltype ## lock_t __dylinx_ ## ltype ## lock_instance;                                           \
static uint32_t __dylinx_ ## ltype ## _ID = num;                                                               \
                                                                                                              \
entity_with_type_t dylinx_ ## ltype ## lock_gettype(dylinx_ ## ltype ## lock_t *lock) {                         \
  entity_with_type_t l = {                                                                                    \
    .mtx_type = __dylinx_ ## ltype ## _ID,                                                                     \
    .entity = (void *)lock                                                                                    \
  };                                                                                                          \
  return l;                                                                                                   \
}

#endif
