#include "dylinx-padding.h"
#ifndef __DYLINX_TOPOLOGY__
#define __DYLINX_TOPOLOGY__
#pragma clang diagnostic ignored "-Waddress-of-packed-member"

#define LOCK_TYPE_CNT 10
#define L_CACHE_LINE_SIZE 64
#define LOCKED 0
#define UNLOCKED 1
#define CPU_PAUSE() __asm__ __volatile__("pause\n" : : : "memory")
#define NEGA(num) (~num + 1)

typedef int (*initializer_fn)(void **, pthread_mutexattr_t *);
typedef int (*locker_fn)(void **);
typedef int (*unlocker_fn)(void **);
typedef int (*destroyer_fn)(void **);
typedef int (*condwait_fn)(void **);

#define dylinx_init_proto(ltype) int ltype ## _init(void **, pthread_mutexattr_t *);
#define INIT_PROTO_LIST(...) FOR_EACH(dylinx_init_proto, __VA_ARGS__)
#define dylinx_lock_proto(ltype) int ltype ## _lock(void **);
#define LOCK_PROTO_LIST(...) FOR_EACH(dylinx_lock_proto, __VA_ARGS__)
#define dylinx_unlock_proto(ltype) int ltype ## _unlock(void **);
#define UNLOCK_PROTO_LIST(...) FOR_EACH(dylinx_unlock_proto, __VA_ARGS__)
#define dylinx_destroy_proto(ltype) int ltype ## _destroy(void **);
#define DESTROY_PROTO_LIST(...) FOR_EACH(dylinx_destroy_proto, __VA_ARGS__)

INIT_PROTO_LIST(ALLOWED_LOCK_TYPE)
LOCK_PROTO_LIST(ALLOWED_LOCK_TYPE)
UNLOCK_PROTO_LIST(ALLOWED_LOCK_TYPE)
DESTROY_PROTO_LIST(ALLOWED_LOCK_TYPE)

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

int pthreadmtx_lock(void **entity) {
  return pthread_mutex_lock((pthread_mutex_t *)*entity);
}
int pthreadmtx_unlock(void **entity) {
  return pthread_mutex_unlock((pthread_mutex_t *)*entity);
}
int pthreadmtx_destroy(void **entity) {
  return pthread_mutex_destroy((pthread_mutex_t *)*entity);
}

#define LOCK_METHODS_AND_MTX(ltype)                                         \
  {                                                                         \
    ltype ## _init,                                                         \
    ltype ## _lock,                                                         \
    ltype ## _unlock,                                                       \
    ltype ## _destroy,                                                      \
    PTHREAD_MUTEX_INITIALIZER                                               \
  },
#define ID2METHODS_EL_LIST(...) FOR_EACH(LOCK_METHODS_AND_MTX, __VA_ARGS__)

static struct MethodsWithMtx id2methods_table[LOCK_TYPE_CNT] = {
  ID2METHODS_EL_LIST(ALLOWED_LOCK_TYPE)
};

//! The glue code here is only for Linux POSIX interface implementation.
//  However, it is still easy to port Dylinx on other OS. The only
//  requirement is to place the member dylinx_type at the exact same
//  offset in the pthread mutex member of the targeting OS which is
//  defined as integer but the allowed value is only positive.
// typedef struct __attribute__((packed)) GenericInterface {
//   void *entity;
//   // dylinx_type has the same offset as __owners in
//   // pthread_mutex_t.
//   int32_t dylinx_type;
//   struct Methods4Lock *methods;
//   char padding[sizeof(pthread_mutex_t) - 20];
// } generic_interface_t;

static pthread_mutex_t init_lock = PTHREAD_MUTEX_INITIALIZER;
// Checking condition should be as strict as possible to make sure
// there is no double allocation.
int is_dylinx_defined(generic_interface_t *gen_lock) {
  return NEGA(LOCK_TYPE_CNT) <= gen_lock->dylinx_type &&
    gen_lock->dylinx_type < 0 &&
    !memcmp(id2methods_table + NEGA(gen_lock->dylinx_type) - 1, gen_lock->methods, sizeof(struct Methods4Lock));
}

// In order to support condition variable, Dylinx requires an extra
// pthread_mutex_t in generic_interface_t, whose name is cv_mutx.
// However, since Dylinx and LITL wrap lock implementation in
// different ways, the duty of taking care of cv_mtx is able to get
// shifted to exterior and leave corresponding four functions of
// locks as clean as possible. Nevertheless, customized lock is
// responsible to confront the contention of multiple threads. Make
// sure user defined lock will lock before cv_mtx is locked.
//
// On the other hand, compared to mutex within element of
// id2methods_table, the fifth member mtx is used to make sure that
// the lock's memory allocation is not going to be double assigned.

#define COMPILER_BARRIER() __asm__ __volatile__("" : : : "memory")
#define DYLINX_EXTERIOR_WRAPPER_IMPLE(ltype, num)                                                             \
static uint32_t __dylinx_ ## ltype ## _ID = num;                                                              \
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
    gen_lock->cv_mtx = malloc(sizeof(pthread_mutex_t));                                                       \
    pthread_mutex_init(gen_lock->cv_mtx, NULL);                                                               \
    gen_lock->dylinx_type = NEGA(__dylinx_ ## ltype ## _ID);                                                  \
    pthread_mutex_unlock(&id2methods_table[__dylinx_ ## ltype ## _ID - 1].mtx);                               \
  }                                                                                                           \
  return gen_lock->methods->initializer(&gen_lock->entity, attr);                                             \
}                                                                                                             \
                                                                                                              \
int dylinx_ ## ltype ## lock_enable(dylinx_ ## ltype ## lock_t *lock) {                                       \
  if (lock->interface.dylinx_type != NEGA(__dylinx_ ## ltype ## _ID))                                         \
    dylinx_ ## ltype ## lock_init(lock, NULL);                                                                \
  lock->interface.methods->locker(&lock->interface.entity);                                                   \
  return pthread_mutex_lock(lock->interface.cv_mtx);                                                          \
}                                                                                                             \
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
    pthread_mutex_init(gen_lock->cv_mtx, NULL);                                                               \
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
    pthread_mutex_init(gen_lock->cv_mtx, NULL);                                                               \
    gen_lock->dylinx_type = NEGA(__dylinx_ ## ltype ## _ID);                                                  \
  }                                                                                                           \
}

#endif
