#include "dylinx-padding.h"
#ifndef __DYLINX_TOPOLOGY__
#define __DYLINX_TOPOLOGY__
#pragma clang diagnostic ignored "-Waddress-of-packed-member"

#define LOCK_TYPE_CNT 10
#define L_CACHE_LINE_SIZE 64
#define LOCKED 0
#define UNLOCKED 1
#define CPU_PAUSE() __asm__ __volatile__("pause\n" : : : "memory")

#define dylinx_init_proto(ltype) int ltype ## _init(void **, pthread_mutexattr_t *);
#define INIT_PROTO_LIST(...) FOR_EACH(dylinx_init_proto, __VA_ARGS__)
#define dylinx_lock_proto(ltype) int ltype ## _lock(void **);
#define LOCK_PROTO_LIST(...) FOR_EACH(dylinx_lock_proto, __VA_ARGS__)
#define dylinx_unlock_proto(ltype) int ltype ## _unlock(void **);
#define UNLOCK_PROTO_LIST(...) FOR_EACH(dylinx_unlock_proto, __VA_ARGS__)
#define dylinx_destroy_proto(ltype) int ltype ## _destroy(void **);
#define DESTROY_PROTO_LIST(...) FOR_EACH(dylinx_destroy_proto, __VA_ARGS__)
#define dylinx_condwait_proto(ltype) int ltype ## _condwait(pthread_cond_t *, void **);
#define CONDWAIT_PROTO_LIST(...) FOR_EACH(dylinx_condwait_proto, __VA_ARGS__)
#define HANDLING_ERROR(msg)                                                   \
  do {                                                                        \
    printf(                                                                   \
      "%s [%s in %s:%d]",                                                     \
      msg, __FUNCTION__, __FILE__, __LINE__                                   \
    );                                                                        \
    exit(-1);                                                                 \
  } while(0)

INIT_PROTO_LIST(ALLOWED_LOCK_TYPE)
LOCK_PROTO_LIST(ALLOWED_LOCK_TYPE)
UNLOCK_PROTO_LIST(ALLOWED_LOCK_TYPE)
DESTROY_PROTO_LIST(ALLOWED_LOCK_TYPE)
CONDWAIT_PROTO_LIST(ALLOWED_LOCK_TYPE)

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

// {{{ degenerate pthread interface
int pthreadmtx_init(void **entity, pthread_mutexattr_t *attr) {
  *entity = malloc(sizeof(pthread_mutex_t));
  return pthread_mutex_init((pthread_mutex_t *)*entity, attr);
}
int pthreadmtx_lock(void **entity) {
  return pthread_mutex_lock((pthread_mutex_t *)*entity);
}
int pthreadmtx_unlock(void **entity) {
  return pthread_mutex_unlock((pthread_mutex_t *)*entity);
}
int pthreadmtx_destroy(void **entity) {
  return pthread_mutex_destroy((pthread_mutex_t *)*entity);
}
// }}}

#define COMPILER_BARRIER() __asm__ __volatile__("" : : : "memory")
#define DYLINX_EXTERIOR_WRAPPER_IMPLE(ltype, num)                                                             \
int dylinx_ ## ltype ## lock_init(dylinx_ ## ltype ## lock_t *lock, pthread_mutexattr_t *attr) {              \
  generic_interface_t *gen_lock = (generic_interface_t *)lock;                                                \
  gen_lock->initializer = ltype ## _init;                                                                     \
  gen_lock->locker = ltype ## _lock;                                                                          \
  gen_lock->unlocker = ltype ## _unlock;                                                                      \
  gen_lock->finalizer = ltype ## _destroy;                                                                    \
  gen_lock->cond_waiter = ltype ## _condwait;                                                                 \
  return gen_lock->initializer(&gen_lock->entity, attr);                                             \
}                                                                                                             \
                                                                                                              \
void dylinx_ ## ltype ## lock_fill_array(dylinx_ ## ltype ## lock_t *head, size_t len) {                      \
  for (int i = 0; i < len; i++) {                                                                             \
    generic_interface_t *gen_lock = (generic_interface_t *)head + i;                                          \
    gen_lock->initializer = ltype ## _init;                                                                   \
    gen_lock->locker = ltype ## _lock;                                                                        \
    gen_lock->unlocker = ltype ## _unlock;                                                                    \
    gen_lock->finalizer = ltype ## _destroy;                                                                  \
    gen_lock->cond_waiter = ltype ## _condwait;                                                                \
  }                                                                                                           \
}

#endif
