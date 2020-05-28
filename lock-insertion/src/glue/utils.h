#include "padding.h"
#ifndef __DYLINX_TOPOLOGY__
#define __DYLINX_TOPOLOGY__
#pragma clang diagnostic ignored "-Waddress-of-packed-member"

#define L_CACHE_LINE_SIZE 64
#define LOCKED 0
#define UNLOCKED 1
#define CPU_PAUSE() asm volatile("pause\n" : : : "memory")
#define NEGA(num) (~num + 1)

typedef int (*initializer_fn)(void **, pthread_mutexattr_t *);
typedef int (*locker_fn)(void **);
typedef int (*unlocker_fn)(void **);
typedef int (*destroyer_fn)(void **);

int ttas_init(void **, pthread_mutexattr_t *);
int ttas_lock(void **);
int ttas_unlock(void **);
int ttas_destroy(void **);

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

struct Methods4Lock {
  initializer_fn initializer;
  locker_fn locker;
  unlocker_fn unlocker;
  destroyer_fn destroyer;
};

struct MethodsWithMtx {
  initializer_fn initializer;
  locker_fn locker;
  unlocker_fn unlocker;
  destroyer_fn destroyer;
  pthread_mutex_t mtx;
};

int pthreadmtx_init(void **entity, pthread_mutexattr_t *attr) {
  *entity = malloc(sizeof(pthread_mutex_t));
  return pthread_mutex_init((pthread_mutex_t *)*entity, attr);
}

int pthreadmtx_lock(void **entity) { return pthread_mutex_lock((pthread_mutex_t *)*entity); }
int pthreadmtx_unlock(void **entity) { return pthread_mutex_unlock((pthread_mutex_t *)*entity); }
int pthreadmtx_destroy(void **entity) { return pthread_mutex_destroy((pthread_mutex_t *)*entity); }

static struct MethodsWithMtx id2methods_table[LOCK_TYPE_CNT] = {
  {
    pthreadmtx_init,
    pthreadmtx_lock,
    pthreadmtx_unlock,
    pthreadmtx_destroy,
    PTHREAD_MUTEX_INITIALIZER
  },
  {
    ttas_init,
    ttas_lock,
    ttas_unlock,
    ttas_destroy,
    PTHREAD_MUTEX_INITIALIZER
  }
};

//! The glue code here is only for Linux POSIX interface implementation.
//  However, it is still easy to port Dylinx on other OS. The only
//  requirement is to place the member dylinx_type at the exact same
//  offset in the pthread mutex member of the targeting OS which is
//  defined as integer but the allowed value is only positive.
typedef struct __attribute__((packed)) GenericInterface {
  void *entity;
  // dylinx_type has the same offset as __owners in
  // pthread_mutex_t.
  int32_t dylinx_type;
  struct Methods4Lock *methods;
  char padding[sizeof(pthread_mutex_t) - 24];
} generic_interface_t;

static pthread_mutex_t init_lock = PTHREAD_MUTEX_INITIALIZER;;
// Checking condition should be as strict as possible to make sure
// there is no double allocation.
int is_dylinx_defined(generic_interface_t *gen_lock) {
  return NEGA(LOCK_TYPE_CNT) <= gen_lock->dylinx_type &&
    gen_lock->dylinx_type < 0 &&
    !memcmp(id2methods_table + NEGA(gen_lock->dylinx_type) - 1, gen_lock->methods, sizeof(struct Methods4Lock));
}

#define COMPILER_BARRIER() asm volatile("" : : : "memory")
#define DYLINX_INIT_LOCK(ltype, num)                                                                          \
static uint32_t __dylinx_ ## ltype ## _ID = num;                                                              \
                                                                                                              \
typedef union {                                                                                               \
  pthread_mutex_t dummy_lock;                                                                                 \
  generic_interface_t interface;                                                                              \
} dylinx_ ## ltype ## lock_t;                                                                                 \
                                                                                                              \
int dylinx_ ## ltype ## lock_init(dylinx_ ## ltype ## lock_t *lock, pthread_mutexattr_t *attr) {              \
  generic_interface_t *gen_lock = (generic_interface_t *)lock;                                                \
  if (!is_dylinx_defined(gen_lock)) {                                                                         \
    pthread_mutex_lock(&id2methods_table[__dylinx_ ## ltype ## _ID - 1].mtx);                                 \
    if (is_dylinx_defined(gen_lock)) {                                                                        \
      pthread_mutex_unlock(&id2methods_table[__dylinx_ ## ltype ## _ID - 1].mtx);                             \
      return 1;                                                                                               \
    }                                                                                                         \
    memset(gen_lock, 0, sizeof(generic_interface_t));                                                         \
    gen_lock->methods = malloc(sizeof(struct Methods4Lock));                                                  \
    gen_lock->methods->initializer = ltype ## _init;                                                          \
    gen_lock->methods->locker = ltype ## _lock;                                                               \
    gen_lock->methods->unlocker = ltype ## _unlock;                                                           \
    gen_lock->methods->destroyer = ltype ## _destroy;                                                         \
    gen_lock->dylinx_type = NEGA(__dylinx_ ## ltype ## _ID);                                                  \
    pthread_mutex_unlock(&id2methods_table[__dylinx_ ## ltype ## _ID - 1].mtx);                               \
  }                                                                                                           \
  return gen_lock->methods->initializer(&gen_lock->entity, attr);                                             \
}                                                                                                             \
                                                                                                              \
int dylinx_ ## ltype ## lock_enable(dylinx_ ## ltype ## lock_t *lock) {                                       \
  if (lock->interface.dylinx_type != NEGA(__dylinx_ ## ltype ## _ID))                                         \
    dylinx_ ## ltype ## lock_init(lock, NULL);                                                                \
  return lock->interface.methods->locker(&lock->interface.entity);                                            \
}                                                                                                             \
                                                                                                              \
static dylinx_## ltype ## lock_t __dylinx_ ## ltype ## lock_instance;                                         \
                                                                                                              \
generic_interface_t *dylinx_ ## ltype ## lock_cast(dylinx_ ## ltype ## lock_t *lock) {                        \
  generic_interface_t *gen_lock = (generic_interface_t *)lock;                                                \
  if (!is_dylinx_defined(gen_lock)) {                                                                         \
    pthread_mutex_lock(&id2methods_table[__dylinx_ ## ltype ## _ID - 1].mtx);                                 \
    if (is_dylinx_defined(gen_lock)) {                                                                        \
      pthread_mutex_unlock(&id2methods_table[__dylinx_ ## ltype ## _ID - 1].mtx);                             \
      return gen_lock;                                                                                        \
    }                                                                                                         \
    memset(gen_lock, 0, sizeof(generic_interface_t));                                                         \
    gen_lock->methods = malloc(sizeof(struct Methods4Lock));                                                  \
    gen_lock->methods->initializer = ltype ## _init;                                                          \
    gen_lock->methods->locker = ltype ## _lock;                                                               \
    gen_lock->methods->unlocker = ltype ## _unlock;                                                           \
    gen_lock->methods->destroyer = ltype ## _destroy;                                                         \
    gen_lock->dylinx_type = NEGA(__dylinx_ ## ltype ## _ID);                                                  \
    pthread_mutex_unlock(&id2methods_table[__dylinx_ ## ltype ## _ID - 1].mtx);                               \
  }                                                                                                           \
  return gen_lock;                                                                                            \
}                                                                                                             \
                                                                                                              \
void dylinx_ ## ltype ## lock_fill_array(dylinx_ ## ltype ## lock_t *head, size_t len) {                      \
  for (int i = 0; i < len; i++) {                                                                             \
    generic_interface_t *gen_lock = (generic_interface_t *)head + i;                                          \
    memset(gen_lock, 0, sizeof(generic_interface_t));                                                         \
    gen_lock->methods = malloc(sizeof(struct Methods4Lock));                                                  \
    gen_lock->methods->initializer = ltype ## _init;                                                          \
    gen_lock->methods->locker = ltype ## _lock;                                                               \
    gen_lock->methods->unlocker = ltype ## _unlock;                                                           \
    gen_lock->methods->destroyer = ltype ## _destroy;                                                         \
    gen_lock->dylinx_type = NEGA(__dylinx_ ## ltype ## _ID);                                                  \
  }                                                                                                           \
}

#endif
